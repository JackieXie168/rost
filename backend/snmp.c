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
#ifdef HAVE_ROST_CONFIG_H
#include "rost_config.h" /* generated by config & autoconf */
#endif /* HAVE_ROST_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
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


static char *snmpd_keys[] = {
    "snmp.location",
    "snmp.contact",
    "snmp.community.ro[]",
    "snmp.community.rw[]",
    NULL, 
};
static char *snmpd_conf_fmt = 
    "sysLocation\t\"$snmp.location->location\"\\n"
    "sysContact\t\"$snmp.contact->contact\"\\n"
    "@EACH($snmp.community.ro[], $ro)\\n"
    "rocommunity\t\"$ro->community\"\\n"
    "@END\\n"
    "@EACH($snmp.community.rw[], $rw)\\n"
    "rwcommunity\t\"$rw->community\"\\n"
    "@END\\n";

/*
 * Commit callback. 
 * We do nothing here but simply create the config based on the current 
 * db once everything is done as if will then contain the new config.
 */
int
snmp_commit(clicon_handle h, commit_op op, commit_data d)
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
    char *d2t;
    
    if (snmp_reload == 0)
	return 0; /* Nothing has changed */
    
    /* Get product ID */
    if (rost_get_product(product, sizeof(product)) < 0)
	goto catch;
    
    if ((out = fopen(SNMPD_CONF, "w")) == NULL) {
	clicon_err(OE_CFG, errno, "%s: fopen(%s)", __FUNCTION__, SNMPD_CONF);
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
    if ((d2t = clicon_db2txt_buf(h, clicon_running_db(h), snmpd_conf_fmt)) != NULL) {
	fprintf (out, "%s", d2t);
	free(d2t);
    }
    fclose(out);
  
    if (debug)
	fprintf(stderr, "Re-load snmpd config\n");
    rost_proc_killbyname (SNMPD, SIGHUP);

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

    for (i = 0; snmpd_keys[i]; i++) {
	key = snmpd_keys[i];
	if (dbdep(h, 0, snmp_commit, (void *)NULL, key) == NULL) {
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
