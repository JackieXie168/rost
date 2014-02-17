/*
 *  CVS Version: $Id: dns.c,v 1.18 2013/08/05 14:30:44 olof Exp $
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
#include <netinet/in.h>

/* clicon */
#include <cligen/cligen.h>
#include <clicon/clicon.h>
#include <clicon/clicon_backend.h>

#define RESOLV_CONF		"/etc/resolv.conf"

/* Flag set if config changed and we need create a new RESOLV_CONF */
static int dns_reload;

/* lvmap formats for db to config-file transforms */
static struct lvmap resolv_conf_fmts[] = {
  {"ipv4.domain", "domain\t$domain", NULL, LVPRINT_CMD},
  {"ipv4.name-server[]", "nameserver $address", NULL, LVPRINT_CMD},
  {NULL, NULL, NULL}
};

/*
 * Commit callback. 
 * We do nothing here but simply create the config based on the current 
 * db once everything is done as if will then contain the new config.
 */
static int
dns_commit(clicon_handle h, char *db,
	   trans_cb_type tt, 
	    lv_op_t op,
	    char *key,
	    void *arg)
{
    dns_reload = 1; /* Mark DNS config as changed */
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

    for (i = 0; resolv_conf_fmts[i].lm_key; i++) {
	key = resolv_conf_fmts[i].lm_key;
	if (dbdep(h, TRANS_CB_COMMIT, dns_commit, (void *)NULL, 1, key) == NULL) {
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
 * Plugin initialization
 */
int
plugin_start(clicon_handle h, int argc, char **argv)
{
    return 0;
}

/*
 * Reset reload-flag before we start.
 */
int
transaction_begin(clicon_handle h)
{
    dns_reload = 0;
    return 0;
}


/*
 * If commit is successful the current db has been replaced by the committed 
 * candidate. We simply re-create a new NTP_CONF based on it.
 */
int
transaction_end(clicon_handle h)
{    
    FILE *out = NULL;
    
    if (dns_reload == 0)
	return 0; /* Nothing has changed */

    if ((out = fopen(RESOLV_CONF, "w")) == NULL) {
	clicon_err(OE_CFG, errno, "%s: fopen", __FUNCTION__);
	return -1;
    }
    
    lvmap_print(out, clicon_running_db(h), resolv_conf_fmts, NULL);
    fclose(out);
    
    return 0;
}

