/*
 *  CVS Version: $Id: quaggapi.c,v 1.24 2012/01/08 22:13:00 olof Exp $
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
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <syslog.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <net/if.h>
#include <netinet/in.h>

/* clicon */
#include <cligen/cligen.h>
#include <clicon/clicon.h>

/* lib */
#include "quaggapi.h"

/*
 * String representations of quagga return codes
 */
const char *quagga_cmd_retstr[] = {
  "success",
  "warning",
  "unknown command",
  "ambiguous command",
  "incomplete command",
  "too many arguments",
  "nothing to do",
  "complete full match",
  "complete match",
  "complete list match",
  "success daemon",
};

/*
 * QuaggAPI protocol identifiers
 */
const char *quaggapi_proto_str[] = {
  NULL,				/* QUAGGAPI_PROTO_NONE */
  "kernel",			/* QUAGGAPI_PROTO_KERNEL */
  "connected",			/* QUAGGAPI_PROTO_CONNECTED */
  "static",			/* QUAGGAPI_PROTO_STATIC */
  "rip",			/* QUAGGAPI_PROTO_RIP */
  "ripng",			/* QUAGGAPI_PROTO_RIPNG */
  "ospf",			/* QUAGGAPI_PROTO_OSPF */
  "ospf6",			/* QUAGGAPI_PROTO_OSPF6 */
  "isis",			/* QUAGGAPI_PROTO_ISIS */
  "bgp",			/* QUAGGAPI_PROTO_BGP */
  NULL				/* QUAGGAPI_PROTO_MAX */
};

/*
 * Connect to QuaggAPI socket.
 *
 * Arguments:
 *	sockpath  	- Path to quagga API unix socket to connect to.
 *
 * Returns: Socket file descriptor or -1 on failure.
 */
static int
quaggapi_connect (const char *sockpath)
{
  int ret;
  int sock;
  struct sockaddr_un addr;

  sock = socket (AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0){
      clicon_err(OE_UNIX, errno, "Connecting to quagga");
      return -1;
  }
  
  memset (&addr, 0, sizeof (struct sockaddr_un));
  addr.sun_family = AF_UNIX;
  strncpy (addr.sun_path, sockpath, strlen (sockpath));

  ret = connect (sock, (struct sockaddr *) &addr, SUN_LEN(&addr));
  if (ret < 0) {
      clicon_err(OE_UNIX, errno, "Connecting to quagga");
    close (sock);
    return -1;
  }

  return sock;
}

/*
 * Disconnect from Quagga API socket.
 *
 * Arguments:
 *	sock		- Socket descriptor
 */
static void
quaggapi_disconnect(int sock)
{
  close (sock);
}


/*
 * Initiate QuaggAPI batch structure. Allocates a new structure and a
 * base list of empty quagga API command structures.
 *
 * Returns: The new structure.
 *
 * Arguments: cb  - a quaggapi_cb callback function for successful calls.
 */
struct quaggapi_batch *
quaggapi_batchinit(quaggapi_cb *cb)
{
  int idx;
  struct quaggapi_batch *batch;

  if ((batch = (struct quaggapi_batch *) calloc (1, sizeof (struct quaggapi_batch))) ==NULL){
      clicon_err(OE_UNIX, errno, "calloc");
      return NULL;
  }

  batch->cmds = calloc (QUAGGAPI_BASE_CMDS, sizeof (struct quaggapi_cmd));
  if (batch->cmds == NULL) {
      clicon_err(OE_UNIX, errno, "calloc");
      free (batch);
      return NULL;
  }

  batch->maxcmd = QUAGGAPI_BASE_CMDS;

  for (idx = 0; idx < batch->maxcmd; idx++)
    batch->cmds[idx].retcode = -1;
  
  batch->cb = cb;

  return batch;
}

/*
 * quaggapi_cmdadd_args()
 *
 * Substitute a command string format and add a command to batch structure.
 * If there are no free command structures in the batch, allocate more space
 * to extend it.
 *
 * Arguments:
 *	batch  		- Batch struct defining the list of commands to execute
 *	fmt		- Command "printf-format" string.
 *	args		- Substitution arguments for 'fmt' in a va_list.
 *
 * Returns: -1 on failure, 0 on success.
 */
static int
quaggapi_cmdadd_args (struct quaggapi_batch *batch, char *format, va_list args)
{
  int idx;
  int cmdlen;
  struct quaggapi_cmd *cmdp;

  if (batch == NULL || format == NULL)
    return -1;

  /* If allocated commands are already assigned, allocated a new chunk */
  if (batch->numcmd >= batch->maxcmd) {

    struct quaggapi_cmd *new = 
      realloc (batch->cmds,
	       (batch->maxcmd + QUAGGAPI_BASE_CMDS) * sizeof (struct quaggapi_cmd));
    if (new == NULL)
      return -1;
    memset (new + batch->maxcmd, 0,
	    QUAGGAPI_BASE_CMDS * sizeof (struct quaggapi_cmd));
    for (idx = batch->maxcmd; idx < batch->maxcmd + QUAGGAPI_BASE_CMDS; idx++)
      batch->cmds[idx].retcode = -1;
  
    batch->maxcmd +=  QUAGGAPI_BASE_CMDS;

    batch->cmds = new;
  }

  cmdp = batch->cmds  + batch->numcmd;

  /* Calculate length of resulting command */
  cmdlen = vsnprintf (NULL, 0, format, args);
  if (cmdlen <= 0)
    return -1;
  cmdlen += 2;	/* space for trailing \n\0 */

  /* Allocate memory for command */
  cmdp->cmd = (char *)calloc (cmdlen, sizeof (char));
  if ( !cmdp->cmd )
    return -1;

  /* Generate command string */
  vsnprintf (cmdp->cmd, cmdlen, format, args);
  cmdp->cmd[cmdlen-2] = '\n';

  batch->numcmd++;

  return 0;
}


/*
 * quaggapi_cmdadd()
 *
 * Substitute a command string format and add a command to batch structure.
 *
 * Arguments:
 *	batch  		- Batch struct defining the list of commands to execute
 *	fmt		- Command "printf-format" string.
 *	...		- Substitution arguments for 'fmt'
 *
 * Returns: result of quaggapi_cmdadd_args()
 */
int
quaggapi_cmdadd (struct quaggapi_batch *batch, char *fmt, ...)
{
  int ret;
  va_list args;

  va_start(args, fmt);
  ret = quaggapi_cmdadd_args (batch, fmt, args);
  va_end (args);

  return ret;
}


/*
 * quaggapi_free()
 *
 * Free up memory allocated by a batch structure.
 *
 * Arguments:
 *	batch  		- Batch struct defining the list of commands to execute
 */
void
quaggapi_free (struct quaggapi_batch *batch)
{
  int idx;

  for (idx = 0; idx < batch->maxcmd; idx++) {
    if (batch->cmds[idx].cmd)
      free (batch->cmds[idx].cmd);
    if (batch->cmds[idx].output)
      free (batch->cmds[idx].output);
  }
  
  free (batch->cmds);    
  free (batch);
}


/*
 * quaggapi_exec()
 *
 * Execute batch of commands. 
 *
 * Arguments:
 *	sockpath	- Quagga API socket path.
 *	batch  		- Batch struct defining the list of commands to execute
 *	ignerr		- Do not terminate batch run on non successful command.
 * 
 * Returns: The number of commands in batch that was executed, or -1 in case
 * of system failure (connection to quagga failed, memory allocation 
 * failed etc). For example, if we have 5 commands in the batch, and execution
 * is interrupted at command 3 because a non-success value was reported by
 * quagga, 3 will be returned (and assigned to the numexec variable in the
 * batch). If 'ignerr' is non-zero, all commands in batch will be executed
 * even if quagga reports one of more successful commands.
 */
static int
quaggapi_exec (const char *sockpath, struct quaggapi_batch *batch, int ignerr)
{
  int idx, n, ntot, msglen;
  int sock = -1;
  char b[2];
  char *ptr;
  char hbuf[256];
  char buf[16384];
  char *bufp;
  size_t buflen;
  char *cp;
  char **vec;
  int nvec;

  sock = quaggapi_connect (sockpath);
  if (sock < 0) {
      batch->numexec = -1;
      return -1;
  }

  for (idx = 0; idx < batch->numcmd; idx++) {
    cp = batch->cmds[idx].cmd;
    clicon_debug(1, "%s: write %s",  __FUNCTION__, cp);
    if (write (sock, cp, strlen (cp)) != strlen (cp)) {
	clicon_err(OE_UNIX, errno, "Connecting to quagga: write");
	batch->numexec = -1;
	goto done;
    }

    /* First read header line */
    memset(hbuf, '\0', sizeof(hbuf));
    ptr = hbuf;
    while (strlen(hbuf) < sizeof(hbuf) && 
	   (n = read(sock, b, 1)) > 0 &&
	   b[0] != '\n')
	*ptr++ = b[0];
    if (n < 0) {
	clicon_err(OE_UNIX, errno, "Connecting to quagga: read");
	batch->numexec = -1;
	goto done;
    }

    vec = clicon_strsplit(hbuf, "#", &nvec, __FUNCTION__);
    if (vec == NULL || nvec != 3) {
	clicon_err(OE_PLUGIN, 0, "Unexpected Quagga API format");
	batch->numexec = -1;
	goto done;
    }

    batch->cmds[idx].retcode = atoi(vec[1]);
    msglen = atoi (vec[2]);

    /* If we expect an output message, read it now */
    if (msglen > 0) {

      if(batch->cb) {
	bufp = buf;
	buflen = sizeof(buf);
      } 
      else {
	/* Allocate space */
	batch->cmds[idx].output = (char *) calloc (msglen+1, sizeof (char));
	if (batch->cmds[idx].output == NULL) {
	    clicon_err(OE_UNIX, errno, "calloc");
	    batch->numexec = -1;	
	    goto done;
	}
	bufp = batch->cmds[idx].output;
	buflen = msglen+1;
      }

      /* Read output from quagga */
      ntot = 0;
      while (ntot < msglen && (n != -1 || errno == EINTR)) {
	n = read ( sock, bufp, buflen-1);
	if (batch->cb) {
	    buf[n]='\0';
	    batch->cb(buf);
	}
	else 
	    bufp += n;
	ntot += n;
      }
      if (ntot  != msglen) {
	  clicon_err(OE_UNIX, errno, "read");
	  batch->numexec = idx;
	  goto done;
      }
      if (batch->cb == NULL)
	  batch->cmds[idx].output[n] = '\0';
    }

    /* Update number of executed commands */
    batch->numexec = idx+1;

    
    /* If quagga reported a problem with the command, break unless ignerr */
    if ( batch->cmds[idx].retcode != QUAGGA_SUCCESS && ignerr == 0 )
      break;
  }
  
 done:
  quaggapi_disconnect(sock);
  unchunk_group(__FUNCTION__);
  return batch->numexec;
}

/*
 * quaggapi_listexec()
 *
 * Create batch from list of commands and execute. Zero-length commands
 * are ignored. 
 *
 * Arguments:
 *	sockpath	- Quagga API socket path.
 *	ignerr		- Do not terminate batch run on non successful command.
 *	cmds		- String vector
 *	ncmd		- Number of entries in vector.
 *
 * Returns: A batch structure populated with the results of the execution or
 * NULL in case of system failure.
 */
struct quaggapi_batch *
quaggapi_listexec (const char *sockpath, quaggapi_cb *cb, int ignerr, char **cmds, int ncmd) 
{
  int i;
  char *ptr;
  struct quaggapi_batch *batch;

  batch = quaggapi_batchinit (cb);
  if (batch == NULL)
    return NULL;
  
  for (i = 0; i < ncmd && cmds[i] != NULL; i++) {
    
    /* Remove trailing '\n' if any. */
    if ( (ptr = index (cmds[i], '\n')) ) 
      *ptr = '\0';
    
    /* Add command to batch if non-zero length */
    if (strlen (cmds[i]))
      quaggapi_cmdadd (batch, cmds[i]);
  }    
  
  /* XXX: should not ignore retval? */
  quaggapi_exec (sockpath, batch, ignerr);


  return batch;
}


/*
 * quaggapi_strexec()
 *
 * Execute batch of quagga commands give as a '\n' delimited format string.
 * String format will be substituted before split into a vector.
 *
 * Arguments:
 *	sockpath	- Quagga API socket path.
 *	ignerr		- Do not terminate batch run on non successful command.
 *	fmt		- '\n' delimited command format string
 *	...		- vsnprintf args for 'fmt'
 *
 * Returns: A batch structure populated with the results of the execution or
 * NULL in case ot system failure.
 */
struct quaggapi_batch *
quaggapi_strexec (const char *sockpath, quaggapi_cb *cb, int ignerr, char *fmt, ...)
{
  int len;
  int nvec;
  char *str = NULL;
  char **vec = NULL;;
  va_list args;
  struct quaggapi_batch *batch = NULL;

  /* Substitute format string into new allocated string */
  va_start(args, fmt) ;
  len = vsnprintf (NULL, 0, fmt, args);
  va_end (args);
  if ((str = chunk (len * sizeof (char) +1, __FUNCTION__)) == NULL){
      clicon_err(OE_UNIX, errno, "chunk");
      goto catch;
  }
  va_start(args, fmt) ;
  vsnprintf (str, len+1, fmt, args);
  va_end (args);
  /* Split string into vector delimited by '\n' */
  if ((vec = clicon_strsplit(str, "\n", &nvec, __FUNCTION__)) == NULL){
      clicon_err(OE_UNIX, 0, "clicon_strsplit");
      goto catch;
  }
  
  if ((batch = quaggapi_listexec (sockpath, cb, ignerr, vec, nvec)) == NULL)
      goto catch;

  /* Fall through to 'catch' to clean-up and return batch */

 catch:
  unchunk_group (__FUNCTION__);
  
  return batch;
}



/*
 * List of quagga commands to enter each specific mode. Used by 
 * quaggapi_modeexec()
 */
static struct  {
  char *cmds[64];
} quaggapi_mode_cmd[] = {
  {{ NULL, }},			     				/* None */
  {{ NULL, }},			     				/* Base mode */
  {{ NULL, }},			     				/* Enable mode */
  {{ "configure terminal", NULL, }},				/* Config mode */
  {{ "configure terminal", "interface %s", NULL, }},		/* Interface mode */
  {{ "configure terminal", "router rip", NULL, }},		/* RIP mode */
  {{ "configure terminal", "router ripng", NULL, }},		/* RIPng mode */
  {{ "configure terminal", "router ospf", NULL, }},		/* OSPF  mode */
  {{ "configure terminal", "router ospf6", NULL, }},		/* OSPF6 mode */
  {{ "configure terminal", "router bgp %u", NULL, }},		/* BGP mode */
  {{ "configure terminal", "router isis", NULL, }},		/* IS-IS mode */
  {{ "configure terminal", "route-map %s %s %u", NULL, }},	/* route-map mode */
};

/*
 * quaggapi_mode_exec()
 *
 * Execute a quagga command in a specified quagga mode. 
 *
 * Arguments:
 *	sockpath	- Quagga API socket path.
 *	ignerr		- Do not terminate batch run on non successful command.
 *	fmt		- command format string
 *	...		- vsnprintf args for 'fmt'
 *
 * Returns: A batch structure populated with the results of the execution
 * or NULL in case of system failure.
 */
struct quaggapi_batch *
quaggapi_modeexec (const char *sockpath, quaggapi_cb *cb, struct qa_mode *modep, char *fmt, ...)
{ 
  va_list args;
  char **modecmdp;
  struct quaggapi_batch *batch = NULL;
  
  if ( !modep || modep->mode < QA_MODE_BASE || modep->mode >= QA_MODE_MAX)
    goto catch;
    
  batch = quaggapi_batchinit (cb);
  if (batch == NULL)
    goto catch;

  modecmdp = quaggapi_mode_cmd[modep->mode].cmds;
  while (*modecmdp) {
    
    switch (modep->mode) {

    case QA_MODE_INTERFACE:
      quaggapi_cmdadd (batch, *modecmdp++, modep->u.iface);
      break;

    case QA_MODE_BGP:
      quaggapi_cmdadd (batch, *modecmdp++, modep->u.bgp_as);
      break;

    case QA_MODE_ROUTE_MAP:
      quaggapi_cmdadd (batch, *modecmdp++, 
		       modep->u.route_map.name, 
		       modep->u.route_map.permit ? "permit" : "deny",
		       modep->u.route_map.seq
		       );
      break;

    default:
      quaggapi_cmdadd (batch, *modecmdp++);
      break;
    }
  }
  va_start(args, fmt);
  quaggapi_cmdadd_args (batch, fmt, args);
  va_end (args);
  
  if (quaggapi_exec (sockpath, batch, 0) < 0)
    goto catch;

  return batch;

  
 catch:
  if (batch)
    quaggapi_free (batch);

  return NULL;
}



