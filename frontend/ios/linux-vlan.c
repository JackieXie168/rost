/*
 *  CVS Version: $Id: linux-vlan.c,v 1.8 2013/08/05 14:31:08 olof Exp $
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
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

/* clicon */
#include <cligen/cligen.h>
#include <clicon/clicon.h>
#include <clicon/clicon_cli.h>

/* osr lib */
#include <quaggapi.h>
#include <linux-vlan.h>

/* local */
#include "ios.h"

int 
vlan_add_interface(void *handle, cvec *vars, cg_var *arg)
{
    char *dot;
    cg_var *cv;
    cg_var *ifname;
    char *str;

    if ((ifname = cvec_find_var(vars, "name")) == NULL) {
	clicon_err(OE_CFG, ENOENT, "Could not find 'name' in variable vector");
	goto catch;
    }

    dot = strrchr(cv_string_get(ifname), '.');
    *dot = '\0';
	
    if (if_nametoindex(cv_string_get(ifname)) == 0) {
	clicon_err(OE_CFG, ENODEV, "Parent interface '%s'", ifname);
	goto catch;
    }
    *dot = '.';

    /* range check vlan id */
    if (atoi(dot+1) > MAX_VID || atoi(dot+1) < MIN_VID) {
	clicon_err(OE_CFG, ERANGE, "VLAN ID: %s", dot+1);
	goto catch;
    }

    str = chunk_sprintf(__FUNCTION__, 
		  "interface[].unit[].dot1q $!name $!unit=(int)0 $vlan=(int)%s",
		  dot+1);
    if ((cv = cv_new(CGV_STRING)) == NULL){
	fprintf(stderr, "%s: cv_new: %s\n", __FUNCTION__, strerror(errno));
	goto catch;
    }
    if(cv_parse(str, cv) < 0) {
	clicon_err(OE_CFG, EINVAL, "Failed to parse new cgv");
	goto catch;
    }

    if (cli_ios_mode(handle, vars, arg))
	cli_set (handle, vars, cv);
 catch:
    if (cv)
	cv_free(cv);
    unchunk_group(__FUNCTION__);
    return 0;
}


#if 0
int 
vlan_del_interface(void *handle, cvec *vars, cg_var *arg)
{
    char *cmd;
    cg_var *cv = NULL;

    cmd = "^interface\\.[0-9]+\\.unit\\.[0-9]+ $!name $!unit=(int)0";
    if ((cv = cv_new(CGV_STRING)) == NULL){
	fprintf(stderr, "%s: cv_new: %s\n", __FUNCTION__, strerror(errno));
	goto done;
    }
    if(cv_parse(cmd, cv) < 0) {
	clicon_err(OE_CFG, EINVAL, "Failed to parse new cgv");
	goto quit;
    }
    cli_del(handle, vars, cv); 

quit:
    if (cv)
	cv_free(cv);

    unchunk_group(__FUNCTION__);
    return 0;

}
#endif

/*
 * Change vlan id on interface.
 * XXX Not supported. Just accept same ID.
 */
int 
vlan_encapsulation(clicon_handle h, cvec *vars, cg_var *arg)
{
    int vid;
    char *ptr;
    int matchlen;
    cg_var *cv1 = cvec_i(vars, 1);


    matchlen = clicon_strmatch(cli_mode.u.iface, LINUX_VLAN_IFRX_FULL, NULL);
    if (matchlen <= 0) {
	cli_output(stderr, "802.1Q encapsulation only supported on VLAN interfaces\n");
	return 0;
    }
    
    if ((ptr = strrchr(cli_mode.u.iface, '.')) == NULL) {
	cli_output(stderr, "No dot found in active inteface name\n");
	return 0;
    }
    vid = atoi(ptr+1);

    if (vid != cv_int_get(cv1)) {
	cli_output(stdout, "Not implemented\n");
	return 0;
    }

    return 0;
}
