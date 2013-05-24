Since you unpacked this you probably want to install it.
You can go straight to the INSTALL.txt file for instructions.
If it is missing, try [1]http://www.psyced.org/INSTALL.html



                PROTOCOL for SYNCHRONOUS CONFERENCING
            =============================================
                ___   __  _   _  __ ___ __
                |  \ (__   \ /  /   |   | \
                |__/    \   V  |    |-  |  )   <<<<<<
                |    (__/   |   \__ |__ |_/



This is 'psyced', the multi-user virtual environment.
It is a server and gateway implementation of PSYC.

The latest version is available on [2]http://www.psyced.org/download

The project homepage is [3]http://www.psyced.org
The protocol homepage is [4]http://www.psyc.eu
The user manual resides at [5]http://help.pages.de

psyced isn't just a PSYC server; it also simulates the
functionality of PSYC clients allowing users of various
sorts of more or less dumb applications to enter the PSYCspace.

psyced is implemented in LPC and uses a driver called psyclpc.
Why LPC you ask? [6]http://about.psyc.eu/LPC
See INSTALL for details.

The files in the distribution directory are:
  AGENDA.txt    : future plans (not a TODO really)
  BANNER.txt    : advertisement or welcome message
  COPYLEFT.txt  : GNU GENERAL PUBLIC LICENSE
  INSTALL.txt   : installation hints and notes
  LICENSE.txt   : something you are supposed to read (copyright info)
  README.txt

  install.sh    : an installation script (ksh/bash).
  [7]makefile   : some useful functions (optional).
  bin/          : various scripts
                  but the only one you really need, "psyced",
                  will be created by 'install.sh'.
  config/       : depot of configurations.
                  also contains some tcsh and powwow settings.
  data/         : this is where psyced stores your user and room data.
  local/        : your local configuration of the server
                  is created by 'install.sh' but you can also make
                  it a symlink into a config/something directory
  log/          : where the server logfiles end up.
                  may be a symlink into the /var partition.
  place/        : here you can implement your own room objects in lpc.
                  some examples of public rooms are waiting for you there.
  [8]run/       : the LPC equivalent of a CGI directory. LPC can spawn
                  a subprocess to do some jobs which are too hard to achieve
                  in LPC-world. see [9]about:spawn on this.
  utility/      : the applet code and other things that may be useful.
  [10]world/            : this is the directory tree that is visible from
                  within the lpc interpreter and therefore contains
                  all the actual lpc program code.
       data/            : symlink to data/
       [11]default/     : the text database for multiple languages and formats
       [12]drivers/     : glue code to interface LPC drivers to psyced
       local/           : symlink to local/
       log/             : symlink to log/
       [13]net/         : all of the psyced code is in a "net" hierarchy
                        : so it can be merged with an existing MUD
       obj/             : just in case you misconfigured your driver
       place/           : symlink to place/
       [14]static/      : contains static files for httpd export
                        : you can use them with the internal httpd
                        : or copy them to yours

Don't be disturbed by the way traditional LPC drivers keep their LPC
source files with a ".c" suffix and data files with a ".o" suffix.
More oddities are described in http://www.psyced.org/DEVELOP if you want
to find your way around the psyced source code.



   --
   http://www.psyced.org/README.html
   last change by psyc on lo at 2011-06-15 15:25:34 MEST

References

   1. http://www.psyced.org/INSTALL.html
   2. http://www.psyced.org/download
   3. http://www.psyced.org/
   4. http://www.psyc.eu/
   5. http://help.pages.de/
   6. http://about.psyc.eu/LPC
   7. http://www.psyced.org/dist/makefile
   8. http://www.psyced.org/dist/run/
   9. http://about.psyc.eu/spawn
  10. http://www.psyced.org/dist/world/
  11. http://www.psyced.org/dist/world/default/
  12. http://www.psyced.org/dist/world/drivers/
  13. http://www.psyced.org/dist/world/net/
  14. http://www.psyced.org/dist/world/static/
