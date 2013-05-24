/****************************************************************************
** Definition of something or other
**
** Created : 979899
**
** Copyright (C) 1997 by Troll Tech AS.  All rights reserved.
**
****************************************************************************/
/* originally based on a srv implementation by Arnt Gulbrandsen 
 * which was released as
 * Copyright 1998 Troll Tech.  Use, modification and distribution
 * is allowed without limitation, warranty, or liability of any
 * kind.
 */

#ifndef SRV_H
#define SRV_H

struct srvhost {
  unsigned int pref;
  struct srvhost * next;
  unsigned int port;
  unsigned int weight;
  unsigned int rweight;
  char name[1];
};


extern void freesrvhost ( struct srvhost * );

extern struct srvhost * getsrv( const char * domain,
				const char * service,
				const char * protocol );

#endif

