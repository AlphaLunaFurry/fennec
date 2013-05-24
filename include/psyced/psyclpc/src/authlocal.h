/* authlocal.h - auto-authenticate users connecting from localhost.

	The procedure you want to call from your code is getUID,
	which returns a uid_t, or 0 if authentication failed.
	Be sure to #define USE_AUTHLOCAL if you want it to work!

    Copyright (C) 1999-2002 Jeremy D. Monin (jeremy@shadowlands.org).
	http://shadowlands.org/authlocal/

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



#ifndef _AUTHLOCAL_H_

#ifdef __cplusplus
extern "C" {
#endif

  /* Below this minimum UID, no users will be authenticated.
   * (these are usually system pseudouser accounts.)
   */
#define AUTHLOCAL_MIN_UID 100

uid_t getUID (unsigned int localhost_ip,
              int  userport, int daemonport);


#ifdef __cplusplus
}
#endif

#define _AUTHLOCAL_H_
#endif

