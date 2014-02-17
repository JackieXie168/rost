/*
 *  CVS Version: $Id: snmp.c,v 1.21 2013/08/05 14:30:44 olof Exp $
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
#ifdef HAVE_OSR_CONFIG_H
#include "osr_config.h" /* generated by config & autoconf */
#endif /* HAVE_OSR_CONFIG_H */

#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <netinet/in.h>

/* clicon */
#include <cligen/cligen.h>
#include <clicon/clicon.h>
#include <clicon/clicon_backend.h>

/* lib */
#include "system.h"

#define SYSSERVICES	"78"

#define SNMPD			"snmpd"
#define SNMPD_CONF		"/etc/snmp/snmpd.conf"


/* Flag set if config changed and we need to signal snmpd in the commit hook */
static int snmp_reload = 0;


/* lvmap formats for db to config-file transforms */
static struct lvmap snmpd_conf_fmts[] = {
  {"snmp.location", "sysLocation\t\"$location\"", NULL, LVPRINT_CMD},
  {"snmp.contact", "sysContact\t\"$contact\"", NULL, LVPRINT_CMD},
  {"snmp.community.ro[]", "rocommunity\t\"$community\"", NULL, LVPRINT_CMD},
  {"snmp.community.rw[]", "rwcommunity\t\"$community\"", NULL, LVPRINT_CMD},
  {NULL, NULL, NULL}

};


/*
 * Commit callback. 
 * We do nothing here but simply create the config based on the current 
 * db once everything is done as if will then contain the new config.
 */
int
snmp_commit(clicon_handle h, char *db,
	    trans_cb_type tt, 
	    lv_op_t op,
	    char *key,
	    void *arg)
{
    snmp_reload = 1; /* Mark NTP config as changed */
    return 0;
}

/*
 * Reset reload-flag before we start.
 */
int
transaction_begin(clicon_handle h)
{
    snmp_reload = 0;
    return 0;
}


/*
 * Post-commit function
 */
int
transaction_end(clicon_handle h)
{
    int retval = -1;
    FILE *out = NULL;
    char product[128];
    
    if (snmp_reload == 0)
	return 0; /* Nothing has changed */
    
    /* Get product ID */
    if (osr_get_product(product, sizeof(product)) < 0)
	goto catch;
    
    if ((out = fopen(SNMPD_CONF, "w")) == NULL) {
	clicon_err(OE_CFG, errno, "%s: fopen", __FUNCTION__);
	goto catch;
    }
    
#if 0
    fprintf (out, "sysDescr \"%s, Version %s\r\nTechnical Support: %s\r\n%s\"\nCompiled: %s\"\r\n", 
	     product, CLICON_VERSION,
	     SUPPORTPAGE,
	     COPYRIGHTSTATEMENT,
	     CLICON_BUILDSTR);
#else
    fprintf (out, "sysDescr\t\"%s, Version %s\"\n", product, CLICON_VERSION);
#endif
    fprintf (out, "sysServices\t%s\n", SYSSERVICES);
    lvmap_print(out, clicon_running_db(h), snmpd_conf_fmts, NULL);
    fclose(out);
  
    if (debug)
	fprintf(stderr, "Re-load snmpd config\n");
    osr_proc_killbyname (SNMPD, SIGHUP);

    retval = 0;

catch:
    unchunk_group(__FUNCTION__);
    return retval;
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

    for (i = 0; snmpd_conf_fmts[i].lm_key; i++) {
	key = snmpd_conf_fmts[i].lm_key;
	if (dbdep(h, TRANS_CB_COMMIT, snmp_commit, (void *)NULL, 1, key) == NULL) {
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
