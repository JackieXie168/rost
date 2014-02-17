/*
 *  CVS Version: $Id: ethtool.c,v 1.7 2013/08/05 14:30:44 olof Exp $
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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/ethtool.h>

/* clicon */
#include <cligen/cligen.h>
#include <clicon/clicon.h>
#include <clicon/clicon_backend.h>

#include <ethtool.h>

int
plugin_init(clicon_handle h)
{
    return 0;
}


/*
 * A "Down-call" function. Return settings for interface.
 */
int
ethtool_gset(clicon_handle h, uint16_t op, uint16_t len, void *arg, 
	     uint16_t *reply_data_len, void **reply_data)
{
    char ifname[FILENAME_MAX];
    struct ethtool_cmd cmd, *ret;

    memset(&cmd, 0, sizeof(cmd));
    if ((ret = malloc(sizeof(*ret))) == NULL) {
	clicon_err(OE_UNIX, errno , "Failed to allocate memory");
	return -1;
    }
	
    memset (ifname, 0, sizeof(ifname));
    memcpy (ifname, arg, len <= sizeof(ifname) ? len : sizeof(ifname));
    if (osr_ethtool_gset(ifname, &cmd) < 0) {
	clicon_err(OE_UNIX, errno, "Unable to get settings for %s", ifname);
	free(ret);
	return -1;
    }
    
    /* XXX We should return a textual format */
    memcpy(ret, &cmd, sizeof(*ret));
    /* Swap to network byte order */
    ret->cmd = htonl(cmd.cmd);
    ret->supported = htonl(cmd.supported);
    ret->advertising = htonl(cmd.advertising);
    ret->speed = htons(cmd.speed);
    ret->maxtxpkt = htonl(cmd.maxtxpkt);
    ret->maxrxpkt = htonl(cmd.maxrxpkt);
    ret->speed_hi = htons(cmd.speed_hi);
    ret->reserved2 = htons(cmd.reserved2);
    ret->reserved[0] = htonl(cmd.reserved[0]);
    ret->reserved[1] = htonl(cmd.reserved[1]);
//    ret->reserved[2] = htonl(cmd.reserved[2]);

    *reply_data = (void *)ret;
    *reply_data_len = sizeof(*ret);

    return 0;
}

/*
 * A "Down-call" function. Return settings for interface.
 */
int
ethtool_gstats(clicon_handle h, uint16_t op, uint16_t len, void *arg, 
	       uint16_t *reply_data_len, void **reply_data)
{
    int i;
    int retlen;
    char ifname[FILENAME_MAX];
    struct osr_etstats *ret, *etsp;

    memset (ifname, 0, sizeof(ifname));
    memcpy (ifname, arg, len <= sizeof(ifname) ? len : sizeof(ifname));

    etsp = osr_ethtool_gstats(ifname, __FUNCTION__);
    if (etsp == NULL) {
	clicon_err(OE_UNIX, errno, "No stats available for %s", ifname);
	return -1;
    }
    
    retlen = (sizeof(*etsp) + (etsp->ets_nstats * sizeof(etsp->ets_stat[0])));
    if ((ret = malloc(retlen)) == NULL) {
	clicon_err(OE_UNIX, errno , "Failed to allocate memory");
	unchunk(etsp);
	return -1;
    }
    memcpy(ret, etsp, retlen);
    for (i = 0; i < ret->ets_nstats; i++)
	ret->ets_stat[i].etse_value = htonll(ret->ets_stat[i].etse_value);

    *reply_data = (void *)ret;
    *reply_data_len = retlen;
    
    return 0;
}

/*
 * A "Down-call" function. Return settings for interface.
 */
int
ethtool_getlink(clicon_handle h, uint16_t op, uint16_t len, void *arg, 
		uint16_t *reply_data_len, void **reply_data)
{
    int n;
    uint8_t *status;
    char ifname[FILENAME_MAX];

    memset (ifname, 0, sizeof(ifname));
    memcpy (ifname, arg, len <= sizeof(ifname) ? len : sizeof(ifname));

    if (osr_ethtool_getlink(ifname, &n) < 0) {
	clicon_err(OE_UNIX, errno, "Unable to get link status for %s", ifname);
	return -1;
    }

    if ((status = malloc(sizeof(*status))) == NULL) {
	clicon_err(OE_UNIX, errno , "Failed to allocate memory");
	return -1;
    }

    *status = n ? 1 : 0;
    *reply_data = (void *)status;
    *reply_data_len = sizeof(*status);
    return 0;
}

    
	    
/*
 * A "Down-call" function. Return settings for interface.
 */
int
ethtool_drvinfo(clicon_handle h, uint16_t op, uint16_t len, void *arg, 
	       uint16_t *reply_data_len, void **reply_data)
{
    char ifname[FILENAME_MAX];
    struct ethtool_drvinfo *info;

    memset (ifname, 0, sizeof(ifname));
    memcpy (ifname, arg, len <= sizeof(ifname) ? len : sizeof(ifname));

    if ((info = malloc(sizeof(*info))) == NULL) {
	clicon_err(OE_UNIX, errno , "Failed to allocate memory");
	return -1;
    }

    if (osr_ethtool_drvinfo(ifname, info) < 0) {
	clicon_err(OE_UNIX, errno, "Unable to get driver info for %s", ifname);
	free(info);
    }

    info->cmd = htonl(info->cmd);
    info->n_priv_flags = htonl(info->n_priv_flags);
    info->n_stats = htonl(info->n_stats);
    info->testinfo_len = htonl(info->testinfo_len);
    info->eedump_len = htonl(info->eedump_len);
    info->regdump_len = htonl(info->regdump_len);

    *reply_data = (char *)info;
    *reply_data_len = sizeof(*info);
    return 0;
}
