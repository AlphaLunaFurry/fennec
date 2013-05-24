/* authlocal.c 1.0.5 - auto-authenticate users connecting from localhost.
 *
 *	The procedure you want to call from your code is getUID,
 *	which returns a uid_t, or 0 if authentication failed.
 *	---->> Be sure to #define USE_AUTHLOCAL if you want it to work! <<----
 *
 *	Remember that uid's under 100 won't be recognized; this is because
 *	they're normally system daemon accounts, not real users.  If you
 *	need to change this behavior, change AUTHLOCAL_MIN_UID in authlocal.h
 *	before compiling.
 *
 * Copyright (C) 2002 Jeremy Monin <jeremy@shadowlands.org>
 *	http://shadowlands.org/authlocal/
 * 1.0.5- 20020704.1140
 *	Fixed fencepost error re matching lines on buffer boundary.
 * 1.0.4- 20020329.0140
 *	Added linux 2.4 support, many descriptive comments, efficiency
 *	improvements, and C-comments, not C++-comments (no //)
 * original ver. was 19990907.1515
 */

#include <unistd.h>  /* for ssize_t, uid_t */
#include <stdio.h>
#include <stdlib.h>  /* for atol */
#include <sys/types.h>  /* for open */
#include <sys/stat.h>   /* for open */
#include <fcntl.h>      /* for open */
#include <string.h>  /* for memmove */
#include <sys/socket.h>  /* this is   */
#include <netinet/in.h>  /* all for   */
#include <arpa/inet.h>   /* inet_addr */

#include "authlocal.h"

/* -- for debug -- 

#define DEBUG_AUTHLOCAL 1
#define USE_AUTHLOCAL 1
char* tcpstat_fname_str = "/proc/net/tcp";
#define TCPSTAT_FNAME tcpstat_fname_str

 -- /for debug -- */


/*
 * How it Works:
 *
 * When connecting to a TCP server from localhost, the server knows
 * the arbitrary port# you're connecting from... under linux, the file
 * /proc/net/tcp contains data about this socket, including its owner.
 * This can be used to automatically, securely authenticate.
 * See an example at the bottom of this file.
 *
 * It does lead to the problem of not _wanting_ to log in as yourself,
 * but this is a separate issue.  Also, be sure root doesn't log in this way.
 * We also should check to be sure the logging-in user has a valid shell,
 * but we don't right now.
 *
 * Tested and successful under linux 2.0.x, 2.2.x, 2.4.x
 *
 * Be sure to define USE_AUTHLOCAL if you want to use this;
 * if this isn't defined, the entire getUID function will ALWAYS
 * return 0 (failure).  This means you don't need to change any of
 * your other code, assuming you have a fallback method for authenticating
 * people, which you'll need anyway if you want them to be able to
 * get in from other hosts.
 */

/*
    authlocal.c version 1.0.x - Automatic authentication of connections
    from localhost.
    Copyright (C) 2000-2002 Jeremy D. Monin (jeremy@shadowlands.org).

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/



#ifdef USE_AUTHLOCAL

	/* yes, most of the file's in this ifdef. */
	/* Only thing that isn't is, if USE_AUTHLOCAL
	 * isn't defined, the function that gets called
	 * from outside this module always returns 0 (failure),
	 * meaning you should fall back to standard username-and-pw-prompt
	 * authentication.
	 */


/* format of /proc/net/tcp lines: (as of linux v2.0-2.4, unsure in other vers)
 * We want to get the UID.
 *    sl    addy       addy   st dunno:d   d:dunno uh uid timeout inode ...
 * \n  n: local:port rem:port nn nnnn:nnn nn:nnnn nnn uid  n n ...
 *        ^^^^^^^^    ^^^^^  in hex; each is 8:4 chars 
 *  the ... after inode were added after 2.2.x.
 */

/* PATSIZE is used in allocating buffer space AND in moving the char ptr;
 * it MUST be the correct length (incl trailing \0).
 */
#define PATSIZE    (8+1+4+1+8+1+4+1)  /* "0100007f:xxxx 0100007f:xxxx\0" */
#define LINEBUFSIZ 4096               /* must be much bigger than line len */
#ifndef TCPSTAT_FNAME
#define TCPSTAT_FNAME "/proc/net/tcp"
#endif


/* check to see if we need to read more chars into the buffer;
 * if so, do that and adjust the end-of-buffer-data pointer.
 * Either way, return it.  Return NULL for EOF.
 * On entry, *c points to the current buffer pos (the part we must keep).
 * (We pass **c so we can change the current buffer pos pointer in the calling
 *	procedure.)
 * *c must be past the start of the buffer. (*c != buf)  Otherwise how can
 * we have any room to read new data into?
 */
static char *read_chunk (char *buf, char **c, char *bufend, int inputfd)
{
  ssize_t nread;
  int inbufloc = *c - buf;  /* how far into buffer are we? */

  /* Must read some into the buffer, but retain partial lines (retain what's
   * between *c and *bufend)...  First, move current buffer contents up so we
   * have room for more, discarding (overwriting) what's currently before *c
   * in the buffer.
   */
  if (inbufloc > 0)
  {
	  memmove (buf, *c, LINEBUFSIZ - inbufloc);
	  *c = buf + (LINEBUFSIZ - inbufloc); 
	  /* *c now points right after the end of the current content;
	   * but in some cases this will be null-terminated, so we need to
	   * not retain any \0's in the current content.
	   */
	  --(*c);
	  while (( *c > buf ) && (**c == '\0'))
		  --(*c);
	  if (**c != '\0') ++(*c);
	  /* *c now points to where new stuff should be added; null-term it. 
	   * we'll adjust *c again soon, but this is where we need it for now.
	   */
	  **c = '\0';
  }    

  /* so add the new stuff, try to fill to end of buffer */
  nread = read (inputfd, *c, LINEBUFSIZ - (*c - buf) - 1);
  if (nread == -1) {
#ifdef DEBUG_AUTHLOCAL
    perror ("read");
#endif
    return NULL;
  }
  if (nread == 0)    /* EOF */
    return NULL;

  /* adjust c to start of buf, adjust end of buf ptr and return it */
  nread += (*c - buf);
  *(buf + nread) = '\0';
  *c = buf;

  return (buf + nread);
}


/** Begin reading from a file; return the end-of-buffer-data pointer,
 *  a pointer (inside buf) to the \0 right after the end of where we read,
 *  or return NULL if we can't read.
 */
static char *buf_begin_get(char *buf, int readfromfd)
{
  ssize_t nread = read (readfromfd, buf, LINEBUFSIZ-1);
  if (nread == -1) {
    return NULL;
  }

  *(buf + nread) = '\0';
  return (buf + nread);
}



/* returns 0 if something goes wrong and it can't be found.
 * (UID 0 really shouldn't be logging in from a network anyway.)
 */
static uid_t getUID_impl (unsigned int /* type? */ localhost_ip,
              int /* type? */ userportno, int daemonportno,
              int inputfd)
{
  char wantthis_in_hex[PATSIZE];
  char buf[LINEBUFSIZ];
  char *b;
  char *bufend;
  char *c;

  sprintf (wantthis_in_hex, "%08X:%04X %08X:%04X",
	   localhost_ip, (int) userportno, localhost_ip, (int) daemonportno);

# ifdef DEBUG_AUTHLOCAL
//fprintf (stderr, "authlocal wantthis %s\n", wantthis_in_hex);
# endif

  /* search for a line containing this */
  bufend = buf_begin_get (buf, inputfd);
  if (bufend == NULL) {
    return (uid_t) 0;  /* couldn't read */
  }

  /* we have c point at our current place in the buffer we search;
   * as we read more, read_chunk will adjust c as the buffer moves.
   */
  for ( c = buf;
	(bufend != NULL) && (*c != '\0') && (c < bufend);
	bufend = read_chunk (buf, &c, bufend, inputfd))
  {
    /* remember our place, in case c is about to become NULL */
    b = c;
# ifdef DEBUG_AUTHLOCAL
//  fprintf (stderr, "chunk is »%s«\n", b);
# endif
    /* try to get c to point to the string we seek  */
    c = strstr (c, wantthis_in_hex);

    if (c != NULL)
    {
      /*  Found a match to our pattern.
       *  Now get uid and return it; we must skip ahead from where we
       *  found our src/dest port pair; to go from the end of our match
       *  to uid, we find and skip 5 blank areas (incl the one immediately
       *  after end of match).
       *  If we run out of string (find \0) before then, we have a match on
       *  a partial line and need to read more of the file into the buffer.
       */
      int bkfld;

      /* point c at end of pattern */
      c += (PATSIZE - 1);

# ifdef DEBUG_AUTHLOCAL
      fprintf (stderr, "authlocal found %s\n", wantthis_in_hex);
# endif

      /* we're now at white space.  We must skip 5 whitespace areas, and
       * the 4 fields (non-whitespace) between them.  Right after the 5th
       * whitespace area is the uid field.  We skip over that too, because
       * we want to null-terminate that and then go back to its start.
       */
      for (bkfld = 1; bkfld <= 5; ++bkfld)
      {
	/* inch c forward through whitespace */
	while (*c <= ' ')
	{
		if (*c == '\0')
		{
			/* we're on a partial line, must read more file into
			 * the buffer and then retry the search.
			 */
			c = NULL;
			break;
		}
		++c;
	}
	if (c == NULL) break;

	/* now inch c forward through non-whitespace */
	while (*c > ' ')
		++c;
      }

      if (c != NULL)
      {
	/* we're now right after end of UID; null-terminate it and move back */
        *c = '\0';  --c;
	
        /* this is the field data; inch backward to next whitespace */
        while (*c > ' ')
	      --c;
	    
        /* now nudge forward and read it!  have a nice day,
         * we're done.
         */
        ++c;
        /* read it */
        return (uid_t) atol(c);   /*  <-------- return the uid ------  */

      }  /* end if c-not-null (partial line after match) */
    }  /* end if c-not-null (no match at all) */

    /* not found, grab more (to search through) from file into buffer, but
     * we must retain the entire last line or two of the current buffer, in
     * case we have just part of that line right now, and it's the right line
     * but we don't know it because we don't have the entire line.
     */
    c = bufend - 1;
    /* find start of final line: */
    while ((*c != '\n') && (c != buf))
	--c;
    if ((c == buf) && (*c != '\n'))
    {
	    /* found no \n in the whole buffer; a very long line.
	     * the line must go away to make room. (not likely to happen)
	     */
	    c = bufend;
    } else {
	    char *lastlinec = c;   /* remember start of final line */
	    /* find start of next-to-final line: */
	    while ((*c != '\n') && (c != buf))
		    --c;
	    if ((c == buf) && (*c != '\n'))
		    c = lastlinec;  /* can't keep all of the 2 lines: keep 1 */
    }
    
  }  /* end while-not-eof loop */

  return (uid_t) 0;  /*  <-------- couldn't find it. ----------  */
}




#endif	/* the big IFDEF */



/* Finally, here's what we need!
 * Returns 0 if something goes wrong and it can't be found.
 * (UID 0 really shouldn't be logging in from a network anyway.)
 */
uid_t getUID (unsigned int localhost_ip,
              int userport, int daemonport)
{

#ifndef USE_AUTHLOCAL
# ifdef DEBUG_AUTHLOCAL
  fprintf (stderr, "authlocal disabled\n");
# endif
  return (uid_t) 0;
#else
  uid_t retval;

  int fd = open(TCPSTAT_FNAME, O_RDONLY);
  if (fd == -1) {
#ifdef DEBUG_AUTHLOCAL
    fprintf (stderr, "authlocal could not open " TCPSTAT_FNAME "\n");
#endif
    return (uid_t) 0;
  }
  retval = getUID_impl (localhost_ip, userport, daemonport, fd);
# ifdef DEBUG_AUTHLOCAL
  fprintf (stderr, "authlocal got uid %d for connection from %d on %d\n",
	retval, userport, daemonport);
# endif
  close (fd);

  if (retval > AUTHLOCAL_MIN_UID)
    return retval;
  else
    return 0;

#endif	/* ifdef (USE_AUTHLOCAL) */
}


/* code for usage demonstration purposes.  Note that it's commented out.

		static struct in_addr localhost_ip;
		localhost_ip.s_addr = htonl(0x7f000001);
	...
		struct 	sockaddr_in user_address;
		int user_fd = accept
		  ( server_fd, (sockaddr *)&user_address, &len );
	...
		uid_t authenticated = 0;
		if (user_address.sin_addr.s_addr == localhost_ip.s_addr ) {

		Auto-Authenticate user if logging in from localhost!

		  authenticated = getUID
		    ( localhost_ip.s_addr, ntohs(user_address.sin_port),
		      server_portnum );
		}


int main(int argc, char **argv)
{
  int pnum_cli = 2048;
  int pnum_srv = 5000;
  int retuid;

  if (argc > 1)
  {
    if (strncasecmp(argv[1], "0x", 2))
      pnum_cli = (int) strtol(argv[1], (char **)NULL, 10);
    else
      pnum_cli = (int) strtol(argv[1], (char **)NULL, 16);
  }
  if (argc > 2)
  {
    if (strncasecmp(argv[2], "0x", 2))
      pnum_srv = (int) strtol(argv[2], (char **)NULL, 10);
    else
      pnum_srv = (int) strtol(argv[2], (char **)NULL, 16);
  }
#ifdef DEBUG_AUTHLOCAL
  if (argc > 3)
	  tcpstat_fname_str = argv[3];
  printf ("file for /proc/net/tcp is %s\n", tcpstat_fname_str);
#endif

  printf ("lookin' for a connection from port %d (0x%04X) to %d (0x%04X)\n",
	pnum_cli, pnum_cli, pnum_srv, pnum_srv);
  retuid = (int) getUID ( inet_addr ("127.0.0.1"), pnum_cli, pnum_srv);
  printf ("returned %d\n", retuid);

  return (retuid ? 0 : 1);
}


 authlocal.c ENDS. */
