/*
 *  CVS Version: $Id: syslog.c,v 1.25 2013/08/05 14:30:44 olof Exp $
 *
 *  Copyright (C) 2009-2014 Olof Hagsand and Benny Holmgren
 *
 *  This file is part of ROST.
 *
 *  ROST is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  ROST is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along wth ROST; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */
#ifdef HAVE_ROST_CONFIG_H
#include "rost_config.h" /* generated by config & autoconf */
#endif /* HAVE_ROST_CONFIG_H */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sqlite3.h>

/* clicon */
#include <cligen/cligen.h>
#include <clicon/clicon.h>
#include <clicon/clicon_backend.h>

/* lib */
#include "rost_syslog.h"
#include "system.h"

#define SYSLOG		"syslog-ng"
#define SYSLOG_PARMS	""
#define SQLITE3		"/usr/bin/sqlite3"

#define SYSLOG_CONF	"/etc/syslog-ng/syslog-ng.conf"
#define SYSLOG_CONF_BASE	\
  "@version: 3.2\n"		\
  "\n"				\
  "options {\n"			\
  "  stats_freq (0);\n"		\
  "  flush_lines (0);\n"	\
  "  time_reopen (10);\n"	\
  "  log_fifo_size (1000);\n"	\
  "  long_hostnames(off);\n"	\
  "  use_dns (no);\n"		\
  "  use_fqdn (no);\n"		\
  "  create_dirs (no);\n"	\
  "  keep_hostname (yes);\n"	\
  "  perm(0640);\n"		\
  "  group(\"log\");\n"		\
  "};\n"			\
  "\n"				\
  "source s_local {\n"		\
  "  file(\"/proc/kmsg\" flags(no-multi-line));\n"	\
  "  unix-stream(\"/dev/log\" flags(no-multi-line));\n"	\
  "  internal();\n"					\
  "};\n"			\
  "\n"				\
  "filter f_trap { level(emerg..notice); };\n"		\
  "\n"



/* Flag set if config changed and we need to signal syslogd to re-load config */
static int syslog_reload;
/* Flag set if we need to reset log table trigger */
static int syslog_trigger;

static char *syslog_keys[] = {
    "logging.trap",
    "logging.host[]",
    "logging.buffered",
    NULL
};
static char *syslog_conf_fmt = 
    "@IF($logging.trap->level)\\n"
    "filter f_trap { level(emerg..$logging.trap->level); };\\n"
    "@END\\n"
    "@EACH($logging.host[], $host)\\n"
    "destination d_host_${host->host} { ${host->protocol}(\"${host->host}\" port(${host->port}));  };\\nlog { source(s_local); filter(f_trap); destination(d_host_${host->host});  };\\n"
    "@END\\n"
    "@IF($logging.buffered)\\n"
    "filter f_local { level(emerg..$logging.buffered->level); };\\ndestination d_sql { program('" SQLITE3 " " SYSLOG_DB "' template(\"INSERT INTO logs VALUES(NULL, '\\${R_YEAR}-\\${R_MONTH}-\\${R_DAY} \\${R_HOUR}:\\${R_MIN}:\\${R_SEC}','\\$FACILITY','\\$LEVEL','\\$HOST','\\$PROGRAM','\\$PID','\\$MSGONLY');\\n\") template-escape(yes) ); };\\nlog { source(s_local); filter(f_local); destination(d_sql); };\\n"
    "@END\\n";


/* Declaration of support functions */
static int syslog_create_trigger(char *db);
static int syslog_create_db();


/*
 * Commit callback. 
 * We do nothing here but simply create the config based on the current 
 * db once everything is done as if will then contain the new config.
 */
int
syslog_commit(clicon_handle h, lv_op_t op, commit_data d)
{
    char *key;

    key = op==LV_DELETE ? commit_key1(d) : commit_key2(d);

    syslog_reload = 1; /* Mark syslog config as changed */
    if (strcmp(key, "logging.buffered") == 0)
	syslog_trigger = 1; /* Need to update table insert trigger */
    return 0;
}

/*
 * Reset reload-flag before we start.
 */
int
transaction_begin(clicon_handle h)
{
    syslog_reload = 0;
    syslog_trigger = 0;
    return 0;
}


/*
 * Post-commit function
 */
int
transaction_end(clicon_handle h)
{
    FILE *out = NULL;
    char *d2t;

    if (syslog_reload == 0)
	return 0; /* Unchanged */
    
    if ((out = fopen(SYSLOG_CONF, "w")) == NULL) {
	clicon_err(OE_SYSLOG, errno, SYSLOG_CONF);
	return -1;
    }
    
    fprintf (out, SYSLOG_CONF_BASE);
    d2t = clicon_db2txt_buf(h, clicon_running_db(h), syslog_conf_fmt);
    if (d2t != NULL) {
	fprintf (out, "%s", d2t);
	free(d2t);
    }
    fclose(out);
    
    syslog_create_db();	/* Make sure DB-file exists */
    
    if (syslog_trigger)
	syslog_create_trigger(clicon_running_db(h));
    
    if (debug)
	fprintf(stderr, "Re-load syslogd config\n");
    rost_proc_killbyname (SYSLOG, SIGHUP);	/* Re-config syslog daemon */
    
    return 0;
}

/*
 * Plugin initialization
 */
int
plugin_init(clicon_handle h)
{
    int i;
    char *key;
    int retval = -1;

    for (i = 0; syslog_keys[i]; i++) {
	key = syslog_keys[i];
	if (dbdep(h, 0, syslog_commit, (void *)NULL, key) == NULL) {
	    clicon_debug(1, "Failed to create dependency '%s'", key);
	    goto done;
	}
	clicon_debug(1, "Created dependency '%s'", key);

    }
    retval = 0;
  done:
    return retval;
}

/*
 * Plugin start
 */
int
plugin_start(clicon_handle h, int argc, char **argv)
{
    return 0;
}

/*
 * Create an insert trigger in logs table
 */
static int
syslog_create_trigger(char *db)
{
    int retval = -1;
    cg_var *cgv;
    char *query;
    int rows;
    char *queryfmt =
	"DROP TRIGGER IF EXISTS logs_rollup; 				\
	 CREATE TRIGGER logs_rollup INSERT ON logs			\
	 BEGIN								\
	   DELETE FROM logs WHERE id IN (				\
	     SELECT id FROM logs ORDER BY id ASC LIMIT (		\
	       CASE							\
	         WHEN (SELECT COUNT(id) FROM logs) < (%d-1) THEN 0	\
 		 ELSE (SELECT COUNT(id) FROM logs) - %d+1		\
 	       END							\
	     )								\
	   );								\
	END;";
    
    /* Get configured number of log-buffer rows */
    cgv = dbvar2cv (db, "logging.buffered", "rows");
    if (cgv == NULL)
	goto catch;
    rows = cv_int_get(cgv);

    /* Format SQL query */
    query = chunk_sprintf(__FUNCTION__, queryfmt, rows, rows);
    if (query == NULL)
	goto catch;

    /* Execute query */
    retval = syslog_sqlite3_exec(SYSLOG_DB, 
			      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
			      query, 
			      NULL,
			      NULL);
catch:
    if (cgv) 
	cv_free(cgv);
    unchunk_group(__FUNCTION__);

    return retval;
}

/*
 * Create logs table.
 */
static int
syslog_create_db()
{
    int ret;
    
    char *query =
       "PRAGMA auto_vacuum = 1;       		  \
	CREATE TABLE IF NOT EXISTS logs (	  \
	id integer primary key,			  \
	datetime text default  CURRENT_TIMESTAMP, \
	facility varchar(10) default NULL,	  \
	priority varchar(10) default NULL,	  \
	host text default NULL,			  \
	program varchar(15) default NULL,	  \
	pid varchar(8) default NULL,		  \
	msg text);";

    
    ret = syslog_sqlite3_exec(SYSLOG_DB, 
			   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
			   query, 
			   NULL,
			   NULL);
    
    unchunk_group(__FUNCTION__);
    return ret;
}
