#!/bin/sh
#
# new age sh (SUSv2 etc) are supposed to handle our syntax
# but if that's not true, try a bash or ksh here.
#
# we could also use a strategy for finding the best bash or ksh
# on this system and re-execing ourselves because an old bourne
# shell will not be able to deal with this script completely

####### psyced installation script #######
#
# original version 2000-08-22 by Kai 'Oswald' Seidler (oswaldism.de)
# heavy improvements by heldensaga and psyc://psyced.org/~lynX
# switched from function foo to foo() syntax as suggested by cebewee
#
#######

# Use 'ldmud' here if you want to use an ldmud rather than a psyclpc'
#driver="ldmud"
#zip="gz"
#zipcmd="gzip"
#
# psyclpc as obtained from http://lpc.psyc.eu
driver="psyclpc"
driverver="20111122"
lpcbaseurl="http://www.psyced.org/files/"
zip="bz2"
zipcmd="bzip2"

# useful for debugging - see what files it would produce
exit="exit 1"
rm="rm"
#exit="echo [debug] Not exiting."
#rm="echo [debug] Not removing"

DATA_PERM="700"
BASE_PERM="700"
CONF_PERM="700"
UMASK="7"

hi="[1m"
lo="[m"

if test -d "/etc/portage"
then
	cat <<X

!!${hi} HEY YOU, PORTAGE USER ${lo}!!
If you are running gentoo/portage you should try out our beautiful ebuilds
at http://www.psyced.org/files/gentoo.tar.bz2 instead of this installation
script. Stop it now.

${hi}Warning: OLD-SCHOOL install.sh STARTING${lo} ...

X
	sleep 2
fi

if test -e .config
then
	cat <<X
You have been installing this before. I will use the previous install .config
as defaults for this run.

X
else
	cat <<X
Should you want to use the install settings from the last time you installed
psyced, please copy the .config file into here and restart this script.

X
fi

if touch .config 2> /dev/null
then
	:
else
	# ok, ich kann die .config nicht touchen
	if rm -f .config 2>/dev/null
	then 
		# aber ich kann sie  loeschen
		touch .config
	else
		# echt scheisse
		echo "I need write permissions for this directory. Please!!"
		$exit
	fi
fi
chmod 700 .config

arch=`uname -s | tr "A-Z" "a-z"`
userid=`id | sed "s/).*//" | sed "s/.*(//"`

if test "`echo -n`" = ""
then
	echo="echo -n"
	echo_nlf=""
else
	echo="echo"
	echo_nlf="\c"	# "
fi

echo ""
yacc=`which yacc`
bison=`which bison`
if test "$yacc" = "" -a "$bison" = ""
then
	# tjgillies says: on fedora bison doen't symlink to yacc
	echo "Please install 'yacc' or 'bison' on this system."
	$exit
fi
#echo "Using '$bison' or '$yacc' during the compilation process."

if test -f "/usr/include/openssl/ssl.h"
then
	tls="y"
else
	tls="n"
	echo ""
	echo "${hi}Warning: ${lo}You are apparently missing the OpenSSL header files!"
	echo "If you're on debian/ubuntu you may have to 'apt-get install libssl-dev' now"
	echo "or your psyclpc will compile without support for encryption."
	sleep 2
fi

ask() {
	echo ""
	eval $echo \"\$1 [\$$2]? $echo_nlf\"
	read answer
	if test "$answer" = ""
	then
		eval answer="\$$2"
	fi
	eval $2=\"$answer\"
	save "$2" "$answer"
}

save() {
	touch .config
	egrep -v "^$1=" .config > .config.tmp
	echo "$1=\"$2\"" >> .config.tmp
	mv .config.tmp .config
}

get() {
	touch .config
	eval `egrep "^$1=" .config`
	eval tmp=\"\$$1\"
	if test "$tmp" = "" -a "$2" != ""
	then
		eval $1="$2"
	fi
}

#uid() {
#	id|sed 's/uid=//
#		s/(.*//'
#}

getuid() {
	egrep "^$1:" /etc/passwd | awk -F: '{print $3}'
}

getgid() {
	egrep "^$1:" /etc/group | awk -F: '{print $3}'
}


###############################################################################
# INTERVIEW
###############################################################################

echo ""
echo ""
echo "${hi}PSYCED INSTALLATION WIZARD${lo}"

if ! test -e data.tar && ! test -d .git
then
    cat <<X
This installation script is designed to work with an image of the current
development tree in a file called data.tar. Obtain a psyced release tar from
http://www.psyced.org, which contains both this script and its data.tar.

X
    $exit
fi

#get WITHOUT_DRIVER "n"
WITHOUT_DRIVER="n"
echo ""
if ! test `ls -1 ${driver}-*tar.${zip} 2>/dev/null`
    then
        if wget "${lpcbaseurl}/${driver}-${driverver}.tar.${zip}"
        then
                echo "${driver} successfully retrieved."
                else
        echo "${hi}ATTENTION: Unable to download ${driver}"
        echo "${hi}ATTENTION: ${lo}You have no ${driver}-*.tar.${zip} in this directory."
        echo "Please obtain one from http://lpc.psyc.eu."
        $exit
        fi
    else
if test -d .git
then
    :
elif test `ls -1 ${driver}-*tar.${zip} 2>/dev/null |wc -l` -gt 1
then
    echo "${hi}ATTENTION:${lo} you've got more than one ${driver}-*tar.${zip}"
    echo "in this directory. Please tidy up before continuing!"
    $exit
else
    if test `ls -d1 */src 2>/dev/null |wc -l` -gt 1
    then
	echo "${hi}ATTENTION:${lo} you've got more than one ${driver}"
	echo "(sub)directory in this directory. Please tidy up before continuing!"
	$exit
    else
	if ! test `ls -1 ${driver}-*tar.${zip} 2>/dev/null`
	then
	    echo "${hi}ATTENTION: ${lo}You have no ${driver}-*.tar.${zip} in this directory."
	    echo "Please obtain one from http://lpc.psyc.eu."
#	    echo "Please obtain one from http://www.psyced.org/ldmud (stable) or"
#	    echo "http://www.bearnip.com/lars/proj/ldmud-dev.html (bleeding edge),"
#	    echo "then restart this script."
#	    echo "If you're interested in LPC, inspect http://lpc.pages.de"

	    ask "Continue without $driver" WITHOUT_DRIVER
	    if ! test $WITHOUT_DRIVER = "y"
	    then
#		bart meint, man sollte das .config hier loeschen
		rm -f .config 2>/dev/null
		$exit
	    fi
	else
	    echo "I can see you have a ${driver} tar here. That's good."
fi
	    echo ""
	fi
    fi
fi

echo ""
echo "${hi}INSTALLATION SPECIFIC QUESTIONS${lo}"

echo ""
echo "Please specify the directory path where to install the psyced components."

echo "userid = $userid"

# does `whoami || who am i` work for solaris etc?
#if test `whoami` = root
if test "x$userid" != "xroot"
then
	BASE_DIR="$HOME/psyced"
	CONFIG_DIR=$BASE_DIR
	echo "Since you started the installation not as root, you will see non-root defaults."
else
	if test -d /opt
	then
		BASE_DIR="/opt/psyced"
	else
		BASE_DIR="/usr/local/psyced"
	fi
	CONFIG_DIR="/etc/psyc"
	CONF_PERM="750"
fi

get BASE_DIR 
ask "PSYCED installation directory" BASE_DIR
echo "[base directory is set to $BASE_DIR]"

if test -f $BASE_DIR
then
	echo ""
	echo "$BASE_DIR already exists."
	echo "Please make a backup and remove it or choose another directory."
	$exit
fi

# one day we should seperate variable files from static files better
LOG_DIR="$BASE_DIR/log"
DATA_DIR="$BASE_DIR/data"
LIB_DIR="$BASE_DIR/world"

echo ""
echo "psyconf will automatically search /etc/psyc for psyced.ini."
echo "If you plan to put this file anywhere else, you will have to"
echo "pass it as the argument to psyconf."

get CONFIG_DIR 
ask "PSYCED configuration directory" CONFIG_DIR
echo "[config directory is set to $CONFIG_DIR]"
echo ""
# setting up ARCH_DIR directly because there is no need to bother the
# user with such a detail. if you think we should, then fix all the
# 'i have a feeling' places in this file and make psyconf use ARCH_DIR too
ARCH_DIR="$BASE_DIR/bin-$arch"
echo "[binary directory is $ARCH_DIR]"
#echo "Where do you want to install architecture dependent PSYC binaries?"
#
## uname -m returns "Power Macintosh" on macosx. very unuseful.
##get ARCH_DIR "$BASE_DIR/bin-`uname -m`"
## why did we call uname twice anyway? uname -s returns such a nice "darwin"
## on linux it returns the actual processor type (i686, x86_64, etc)
## which these days needs to be considered.. FIXME
##
#get ARCH_DIR "$BASE_DIR/bin-$arch"
#ask "Binary installation directory" ARCH_DIR
#echo "[binary directory is set to $ARCH_DIR]"
echo ""

echo ""
echo "Hostname would typically be 'psyc' or 'dishwasher' without a domain name"
echo "which is going to be the next question. If you want to install psyced as"
echo "something like 'example.net' use 'example' here and 'net' on the next"
echo "input line."

get HOST_NAME `hostname | sed "s/\..*//"`
ask "Server host name" HOST_NAME

# freebsd does not support -sil, other systems don't even have nslookup
#get DOMAIN_NAME `nslookup -sil $HOST_NAME | tail -n 3 | head -n 1 | sed "s/[^.]*\.//"`
# this grep isn't safe from having spaces behind the domain name or suchlike
get DOMAIN_NAME "" # `grep ^domain /etc/resolv.conf | sed "s/^domain.//"`
ask "Your domain name" DOMAIN_NAME

#get HOST_IP "127.0.0.1"
get HOST_IP
# `nslookup -sil $HOST_NAME | tail -n 2 | head -n 1 | awk '{print $2}' | sed "s/,//"`
echo ""
echo "If you have a static IP address for your server, please tell me."
echo "Otherwise I will resolve my own hostname at runtime in order to get my"
echo "current IP address."
ask "Server IP address" HOST_IP

echo ""

get USER "psyc"
if test "x$USER" = "xroot"; then
	echo ""
	echo "You shouldn't run psyced as root, so what about a 'psyc' user?"
	# indigo6 thinks we should run useradd here, even if some unices
	# do not provide that command. we can >/dev/null the error though...
	echo "If the user doesn't exist yet, please make one."
fi
#while true
#do
	ask "Which user do you want to run psyced as" USER
#	if id -u $USER > /dev/null
#	then
#		echo "[User $USER selected.]"
#		break
#	fi
#echo "No such user."
#	continue
#done

get GROUP "psyc"
#while true
#do
	echo "If such a group doesn't exist yet, please create it now."
	ask "Which group do you want to run psyced as" GROUP
#	if `id -Gn $USER | grep $GROUP > /dev/null`
#	then
#		echo "[Group $GROUP selected.]"
#		break
#	fi
#	echo "No such group or you are not a member of it."
#	continue
#done

if test "x$USER" != "x$userid" -a "x$userid" != "xroot"
then
	echo "You want to install files as $USER. Please change to this user or become root."
	$exit
fi

echo ""
echo "Where do you want psyced runtime output? For manually started development"
echo "servers choose 'console', for background daemon service use 'files'."
echo ""
echo "['files' for log files, 'console' for server console]"
# replace "files" by "buffered" vs. "flushed" .. see also TODO

get RUNTIME_OUTPUT "files"

while true
do
	ask "Send server runtime output to" RUNTIME_OUTPUT

	if test "$RUNTIME_OUTPUT" = "console" -o "$RUNTIME_OUTPUT" = "files"
	then
		break	
	else
		echo "Please choose 'files' or 'console' output."
	fi
done
#echo "[server output goes to $RUNTIME_OUTPUT]"
 
## BUG IN ORDER!!! we dont have $PSYC_PORT yet!!!!! TODO!!111
## also HOST_IP may be empty
RUNTIME_OUTPUT_DIR="$LOG_DIR/$HOST_IP-$PSYC_PORT"
RUNTIME_OUTPUT_STDERR="$RUNTIME_OUTPUT_DIR/stderr"
RUNTIME_OUTPUT_STDOUT="$RUNTIME_OUTPUT_DIR/stdout"

if test "$RUNTIME_OUTPUT" = "files"
then
	echo "[runtime output log directory is $RUNTIME_OUTPUT_DIR/.]"
	get DEBUG "0"
else
	get DEBUG "1"
fi

echo ""
echo "Debug level 0 gives you minimum output, level 1 gives you interesting"
echo "output. Level 2 and more is for real down-to-earth debugging. It gives"
echo "you messages that will make you think something is going wrong even if"
echo "everything is going fine, so please use level 1 unless you are going"
echo "to read the source code for every nervous message you see.  ;-)"

ask "Debug level (0..2)" DEBUG
#echo "[debug level set to $DEBUG]"

echo ""
echo ""
echo "${hi}PSYC SPECIFIC OPTIONS${lo}"
echo ""

echo "Set the PSYC identification for your server. e.g. psyc.$DOMAIN_NAME."
echo "If you are using dial-up internet, you can try out a few things, but"
echo "if you want this software to serve a serious purpose you need to have"
echo "a dynamic DNS address for this machine installed and provide it here."
echo "A static address is even better. See the FIRSTSTEPS document for more."

#get SERVER_HOST "$HOST_NAME.$DOMAIN_NAME"
SERVER_HOST="$HOST_NAME.$DOMAIN_NAME"
ask "Set PSYC hostname to" SERVER_HOST

get CHATNAME $HOST_NAME
#ask "Name of your chat service" CHATNAME

cat <<X

Now comes the best part. You get to decide which of the many protocols and
services that psyced provides you want to activate. Since ${driver} doesn't
have the ability to run safely as root, all protocols use non-privileged
port numbers. We also mention the official privileged port numbers in case
you want to set up a firewall based port mapping.

If you need to change the port numbers later on, you can do so by editing
the psyconf.ini configuration file.
X
# FIXME: in fact we should probably not ask about port numbers here

get PSYC_YN "y"
ask "Enable PSYC (you better say yes here)" PSYC_YN

if test "$PSYC_YN" = "n"
then
	PSYC_PORT=""
	echo "[PSYC disabled. Ouch!]"
	UDP=""
else
	get PSYC_PORT "4404"
#
# if i'm not mistaken all ports are now passed to the mudlib so
# there is no reason to make *any* limitations here
# we could delete the "between" ranges from all protocols
#
#	ask "Which port number between 4400 and 4409" PSYC_PORT
	ask "Which port number " PSYC_PORT
	echo "[PSYC enabled on port $PSYC_PORT.]"
	UDP="-u $PSYC_PORT"
fi

echo ""
echo ""
echo "${hi}PSYCED REGULAR PROTOCOL SERVICES${lo}"

get INTERJABBER_YN "y"
ask "Enable XMPP communication with other JABBER servers" INTERJABBER_YN

if test "$INTERJABBER_YN" = "n"
then
	INTERJABBER_PORT=""
	echo "[JABBER S2S disabled.]"
else
	get INTERJABBER_PORT "5269"
	echo "Note: If you change the port number, you will have to set up DNS SRV records"
	ask "Which port number" INTERJABBER_PORT
	echo "[JABBER interserver communication enabled on port $INTERJABBER_PORT.]"
fi

get IRC_YN "y"
ask "Enable access for IRC clients" IRC_YN

if test "$IRC_YN" = "n"
then
	IRC_PORT=""
	echo "[IRC client access disabled.]"
else
	get IRC_PORT "6667"
	ask "Which port number between 6600 and 6699" IRC_PORT
	echo "[IRC client access enabled on port $IRC_PORT.]"
fi

get JABBER_YN "y"
ask "Enable access for Jabber/XMPP clients" JABBER_YN

if test "$JABBER_YN" = "n"
then
	JABBER_PORT=""
	echo "[JABBER client access disabled.]"
else
	get JABBER_PORT "5222"
	ask "Which port number (5222 or 55222)" JABBER_PORT
	echo "[JABBER client access enabled on port $JABBER_PORT.]"
fi

get SMTP_YN "n"
ask "Enable SMTP reception server (only for messaging)" SMTP_YN

if test "$SMTP_YN" = "n"
then
	SMTP_PORT=""
	echo "[SMTP server disabled.]"
else
	get SMTP_PORT "2525"
	ask "Which port number between 2500 and 2599" SMTP_PORT
	echo "[SMTP server enabled on port $SMTP_PORT (official 25).]"
fi

get POP3_YN "n"
ask "Enable POP3 server (experimental)" POP3_YN

if test "$POP3_YN" = "n"
then
	POP3_PORT=""
	echo "[POP3 server disabled.]"
else
	get POP3_PORT "1100"
	ask "Which port number should we use" POP3_PORT
	echo "[POP3 server enabled on port $POP3_PORT (official 110).]"
fi

get NNTP_YN "n"
ask "Enable access for NNTP readers (experimental)" NNTP_YN

if test "$NNTP_YN" = "n"
then
	NNTP_PORT=""
	echo "[NNTP reader access disabled.]"
else
	get NNTP_PORT "1199"
	ask "Which port number between 1190 and 1199" NNTP_PORT
	echo "[NNTP reader access enabled on port $NNTP_PORT (official 119).]"
fi

get TELNET_YN "y"
ask "Enable telnet access" TELNET_YN

if test "$TELNET_YN" = "n"
then
	TELNET_PORT=""
	echo "[telnet access disabled.]"
else
#	if ! test `whoami` = "root"
#	then
#		TELNET_PORT="2323"
#	fi
#	if egrep "^telnet" /etc/inetd.conf > /dev/null 2>&1
#	then
#		TELNET_PORT="2323"
#	else
#		if test `whoami` = "root"
#		then
#		    TELNET_PORT="23"
#		    echo "[According to your /etc/inetd.conf your system doesn't run any"
#		    echo "telnetd on port 23. You may want psyced to use this port!]"
#		fi
#	fi
	get TELNET_PORT 2323
	ask "Which port between 2300 and 2399 to use for telnet" TELNET_PORT
	echo "[telnet access enabled on port $TELNET_PORT (instead of 23).]"
fi

echo ""
echo "HTTP is necessary for the social network functions, the web-based "
echo "configuration, various chatroom export features and the WAP gateway.. "

get HTTP_YN "y"
ask "Enable builtin HTTP daemon" HTTP_YN
webconfig=""

if test "$HTTP_YN" = "n"
then
	HTTP_PORT=""
	echo "[HTTP service disabled.]"
else
	get HTTP_PORT 44444
	ask "Which port number" HTTP_PORT
	echo "[HTTP service enabled on port $HTTP_PORT (instead of 80).]"

# currently not in use and not configured by install.sh
	HTTPCONFIG_YN="n"
#	get HTTPCONFIG_YN "y"
#	ask "Activate web-based configuration for localhost users" HTTPCONFIG_YN
#
#	if test "$HTTPCONFIG_YN" = "n"
#	then
#		echo "[WEB_CONFIGURE disabled.]"
#	else
#		echo "[WEB_CONFIGURE enabled.]"
#		webconfig="#define WEB_CONFIGURE"
#	fi
fi

get APPLET_YN "n"
ask "Enable applet access" APPLET_YN

if test "$APPLET_YN" = "n"
then
	APPLET_PORT=""
	echo "[applet access disabled.]"
else
	echo ""
	echo "world/static/index.html configures the applet to use port 2008."
	echo "Should you want to use an other one, you need to edit that file."
	echo ""
	get APPLET_PORT 2008
	ask "Which port number " APPLET_PORT
	echo "[applet access enabled on port $APPLET_PORT.]"
fi

echo ""
echo ""
echo "${hi}PSYCED ENCRYPTED PROTOCOL SERVICES${lo}"

echo ""
#echo "With either openssl or gnutls installed, your driver may provide TLS/SSL."
echo "With openssl libs installed, your driver should provide TLS/SSL."
echo "If you don't have it installed, you must say 'n' here."
echo "Would you like to configure any ports for TLS-enhanced protocols?"

get TLS_YN $tls
ask "Let's use some TLS cryptography" TLS_YN
# das ganze tls-geviech macht nur sinn, wenn man cert und privkey hat
# ergo die pfade fuer die abfragen und dann entscheiden, ob...

PSYCS_PORT=""
IRCS_PORT=""
JABBERS_PORT=""
SMTPS_PORT=""
NNTPS_PORT=""
TELNETS_PORT=""

if test "$TLS_YN" = "n"
then
    tlso=""
    echo "[No crypto protocols.]"
else
    tlso="--tls-key $CONFIG_DIR/key.pem --tls-cert $CONFIG_DIR/cert.pem"
    echo ""
    echo "Alright. You need to create a key.pem and cert.pem file using"
    echo "any openssl or gnutls tool, then place them in $CONFIG_DIR."
    echo "These will be the identity of your new PSYC homeserver."
    echo "Help needed? http://www.openssl.org/docs/HOWTO/certificates.txt"
    echo ""
    echo "PSYC intentionally uses a dedicated TLS port not just for"
    echo "simplicity, but also because it reduces interserver latency"
    echo "as we can leave out negotiation. Newer versions of psyclpc when"
    echo "compiled with libpsyc support automatic detection of immediate"
    echo "TLS on the same ports as the unencrypted protocols, so in that"
    echo "case you don't need to define dedicated TLS ports."

    get PSYCS_YN "y"
    ask "Enable PSYC over TLS" PSYCS_YN

    if test "$PSYCS_YN" = "n"
    then
	    echo "[PSYCS access disabled.]"
    else
	    get PSYCS_PORT "9404"
	    ask "Which port number between 9400 and 9499" PSYCS_PORT
	    echo "[PSYCS access enabled on port $PSYCS_PORT.]"
    fi

    get IRCS_YN "y"
    ask "Enable IRC over TLS" IRCS_YN

    if test "$IRCS_YN" = "n"
    then
	    echo "[IRCS access disabled.]"
    else
	    get IRCS_PORT "9999"
	    ask "Which port number between 9960 and 9999" IRCS_PORT
	    echo "[IRCS access enabled on port $IRCS_PORT (instead of 994).]"
    fi

    get JABBERS_YN "y"

    ask "Enable legacy JABBER client access over TLS" JABBERS_YN
# das ist eigentlich nen altmodischer weg, starttls ist toller und braucht
# keinen extra-port

    if test "$JABBERS_YN" = "n"
    then
	    echo "[JABBERS client access disabled.]"
    else
	    get JABBERS_PORT "5223"
	    ask "Which port number (5223 or 55223)" JABBERS_PORT
	    echo "[JABBERS client access enabled on port $JABBERS_PORT.]"
    fi

    get HTTPS_YN "y"
    ask "Enable HTTPS daemon" HTTPS_YN

    if test "$HTTPS_YN" = "n"
    then
	    HTTPS_PORT=""
	    echo "[HTTPS service disabled.]"
    else
	    get HTTPS_PORT "4433"
	    ask "Which port number (4433 or 44300 .. 44443)" HTTPS_PORT
	    echo "[HTTPS service enabled on port $HTTPS_PORT (instead of 443).]"
    fi

    get SMTPS_YN "n"
    ask "Enable SMTP over TLS" SMTPS_YN

    if test "$SMTPS_YN" = "n"
    then
	    echo "[SMTPS server disabled.]"
    else
	    get SMTPS_PORT "4656"
	    ask "Which port number between 4650 and 4659" SMTPS_PORT
	    echo "[SMTPS server enabled on port $SMTPS_PORT (instead of 465).]"
    fi

    get POP3S_YN "n"
    ask "Enable POP3 over TLS" POP3S_YN

    if test "$POP3S_YN" = "n"
    then
	    echo "[POP3S server disabled.]"
    else
	    get POP3S_PORT "9950"
	    ask "Official port would be 995. Use" POP3S_PORT
	    echo "[POP3S server enabled on port $POP3S_PORT.]"
    fi

    get NNTPS_YN "n"
    ask "Enable NNTP over TLS" NNTPS_YN

    if test "$NNTPS_YN" = "n"
    then
	    echo "[NNTPS reader access disabled.]"
    else
	    get NNTPS_PORT "5636"
	    ask "Which port number between 5630 and 5639" NNTPS_PORT
	    echo "[NNTPS enabled on port $NNTPS_PORT (instead of 563).]"
    fi

    echo ""
    echo "In theory telnet should negotiate TLS/SSL internally, but we haven't"
    echo "looked into that yet, so if you want a custom telnets: port.."

    get TELNETS_YN "n"
    ask "Enable telnet over TLS" TELNETS_YN

    if test "$TELNETS_YN" = "n"
    then
	    echo "[telnet over SSL disabled.]"
    else
	    get TELNETS_PORT "9992"
	    ask "Which port number (9992 or 9920 .. 9929)" TELNETS_PORT
	    echo "[telnet over SSL enabled on $TELNETS_PORT (instead of 992).]"
    fi

fi

if test "$IRCNICK" != ""; then
	ADMIN_NICKNAME="$IRCNICK"
else
	if test "$USER" != ""; then
		ADMIN_NICKNAME="$USER"
	else
		ADMIN_NICKNAME="`logname`"
	fi
fi

echo ""
echo ""
echo "${hi}MISCELLANEOUS CONFIGURATION SETTINGS${lo}"
echo ""

get ADMIN_NICKNAME 
ask "Admin Nickname" ADMIN_NICKNAME

get ADMIN_PASSWORD hackme
#ask "Admin Password" ADMIN_PASSWORD

echo ""
echo "psyced can provide all of its system messages in either english or"
echo "german as of now. Pick 'de' or 'en' as default language."

get DEFLANG en
ask "Default Language" DEFLANG

get WANT_ERQ "y"

echo ""
echo ""
echo "psyced uses an external program called 'erq' for non-blocking resolution"
echo "of IP addresses. Both PSYC and XMPP will not operate correctly without it."
# stupid question
#ask "Do you want this additional process to be activated?" WANT_ERQ

if test "$WANT_ERQ" != "n"
then
	WANT_ERQ="y"
	echo "[host name resolving enabled (start erq).]"
else
	echo "[host name resolving disabled (don't start erq).]"
fi

## TODO, should be disabled when there is no HOST_IP?
get WANT_PORTRULES "y"

echo ""
echo ""
echo "Something you may find useful later: I will generate a file for you"
echo "which contains suitable rules for an iptables-type firewall, mapping"
echo "privileged ports to the ones you have actually chosen for your"
echo "non-privileged psyced process (DNAT). You can look at it anytime"
echo "you feel ready for it. Say yes here. It's just a file."
ask "Do you want some iptable lines?" WANT_PORTRULES

PR_FILE="portrules.iptables"

if test "$WANT_PORTRULES" = "y"
then
	echo "# typical way of routing privileged ports to a psyced running non-privileged" > $PR_FILE
	echo "# this file has been generated by psyced's install.sh" >> $PR_FILE
	echo "" >> $PR_FILE
	echo "IF_EX=eth0" >> $PR_FILE
	echo "IP_PSYC=$HOST_IP" >> $PR_FILE
	echo "IPT=/sbin/iptables" >> $PR_FILE
	echo "" >> $PR_FILE

	RULE_BEGIN="\$IPT -t nat -A PREROUTING -i \$IF_EX -d \$IP_PSYC -p tcp --dport"
	RULE_END="-j DNAT --to :"

	if test $SMTP_PORT; then
		echo "$RULE_BEGIN 25 $RULE_END $SMTP_PORT	# SMTP" >> $PR_FILE
	fi
	if test $POP3_PORT; then
		echo "$RULE_BEGIN 110 ${RULE_END}${POP3_PORT}	# POP3" >> $PR_FILE
	fi
	if test $NNTP_PORT; then
		echo "$RULE_BEGIN 119 ${RULE_END}${NNTP_PORT}	# NNTP" >> $PR_FILE
	fi
	if test $TELNET_PORT; then
		echo "$RULE_BEGIN 23 ${RULE_END}${TELNET_PORT}	# TELNET" >> $PR_FILE
	fi
	if test $HTTP_PORT; then
		echo "$RULE_BEGIN 80 ${RULE_END}${HTTP_PORT}	# HTTP" >> $PR_FILE
	fi
#	if test $PSYCS_PORT; then
#		echo "$RULE_BEGIN 18 ${RULE_END}${PSYCS_PORT}	# PSYCS" >> $PR_FILE
#	fi
	if test $IRCS_PORT; then
		echo "$RULE_BEGIN 994 ${RULE_END}${IRCS_PORT}	# IRCS" >> $PR_FILE
	fi
	if test $HTTPS_PORT; then
		echo "$RULE_BEGIN 443 ${RULE_END}${HTTPS_PORT}	# HTTPS" >> $PR_FILE
	fi
	if test $SMTPS_PORT; then
		echo ""$RULE_BEGIN 465 ${RULE_END}${SMTPS_PORT}	# SMTPS" >> $PR_FILE
	fi
	if test $POP3S_PORT; then
		echo "$RULE_BEGIN 995 ${RULE_END}${POP3S_PORT}	# POP3S" >> $PR_FILE
	fi
	if test $NNTPS_PORT; then
		echo "$RULE_BEGIN 563 ${RULE_END}${NNTPS_PORT}	# NNTPS" >> $PR_FILE
	fi
	if test $TELNETS_PORT; then
		echo "$RULE_BEGIN 992 ${RULE_END}${TELNETS_PORT}	# TELNETS" >> $PR_FILE
	fi

	echo "[port rules written to '$PR_FILE'.]"
else
	echo "[no port rules written.]"
fi

#get WANT_CVSUP "n"
#
# would be soooo smart if we'd ask for update before we even enter
# the install.sh interview because frequently there is a better
# install.sh in the repo worth running instead. TODO
#echo ""
#echo ""
#echo "The version you are about to install is considered stable,"
#echo "If you need to run the latest off-the-mill version you can"
#echo "update the code tree via CVS. You can choose to do so now or"
#echo "anytime later using the -u option of psyced. You can"
#echo "even inspect the changes in the code before updating, using"
#echo "psyced -d. We think this feature is quite cool."
#echo ""
#echo "${hi}But be aware, by updating you may be switching"
#echo "to an unstable or otherwise unusable version.${lo}"
#ask "Update your installation by CVS?" WANT_CVSUP

echo ""
echo ""
echo ""
echo "${hi}OKAY!! HERE WE GOOOOO!!!${lo}"
echo ""
echo ""

###############################################################################
# ACTION
###############################################################################

echo "Creating configuration files..."

if test "$HTTPCONFIG_YN" = "y"
then
    HTTPCONFIG_10="1"
else
    HTTPCONFIG_10="0"
fi

if test "$RUNTIME_OUTPUT" = "console"
then
   CONSOLE_10="1" 
else
   CONSOLE_10="0" 
fi
FILES_10="0"

if test "$TLS_YN" = "y"
then
    TLS_10="1"
else
    TLS_10="0"
fi

# so we essentially have this file twice.. in here and in config/
# what kind of trick could we use to come up with a common template?
#
# i have a feeling i should put ARCH_DIR into psyced.ini
cat << EOT > psyced.ini
; this is the psyced configuration file
; automatically generated by install.sh
;
; after modifying this file you must always run 'psyconf'.
; inspect http://about.psyc.eu/psyced for further instructions.
;
; boolean variables are 0 = false (no) and 1 = true (yes).

[_basic]
; Base directory of the psyced installation
_path_base = $BASE_DIR

; Configuration directory of this PSYCED installation
; psyconf will automatically search /etc/psyc for psyced.ini.
; If you plan to put this file anywhere else, you will have to pass it
; as argument to psyconf.
_path_configuration = $CONFIG_DIR

; Path leading to your private and public TLS keys
; (absolute or relative to _path_configuration)
_path_PEM_key = key.pem
_path_PEM_certificate = cert.pem
; You can run 'make' in the 'utility/gencert' folder to create a pair

; Path to the TLS trust directory where certs are kept.
; If unset this will default to your system installation's defaults.
;_path_trust = trust
;
; Path to the TLS CRL directory where certificate revocation lists are kept.
; We currently simply use the same one as for the certs. In fact we don't use
; these things yet, but it is a good idea to start doing so.
;_path_revocation = trust

; Do you want psyced to be launched automatically at system startup?
; List of filenames a System V start/stop script shall be generated to.
; Purpose of this is: you can _really_ move the installation
;                     to another _path_base.
;
; May look like this for a classic System V set-up:
;_list_script_init = /etc/rc.d/psyced /etc/rc.d/rc3.d/K04psyced /etc/rc.d/rc3.d/S44psyced
; For a BSD it should be something like this:
;_list_script_init = /etc/init.d/psyced /etc/rc3.d/K04psyced /etc/rc3.d/S44psyced
; or it should look like this for gentoo:
;_list_script_init = /etc/init.d/psyced
; You can simply disable the line to turn off this feature. If you want to
; use this function instead, please make sure your distribution has /bin/sh
; in /etc/shells. Recently Slackware has decided to remove that, which
; probably means it is no longer POSIX compliant  ;)
; Maybe it is not the only one..
;
; Userid to run the psyced as, when started from the init script.
_system_user = $USER
;
; Unused as yet:
;_system_group = $GROUP

; Where new users will be sent to
_place_default = RendezVous

; How the system speaks to you unless specified.
; de = German, en = English, en_g = English for Geeks
_language_default = $DEFLANG

; The externally visible name & domain of your host
_host_name = $HOST_NAME
_host_domain = $DOMAIN_NAME

; Would you like to bind the server to a specific IP address?
; If you do, you MUST also provide _host_name and _host_domain
; If you leave this empty, psyced will find out at runtime.
_host_IP = $HOST_IP

; Nickname for the chatserver. Appears in login message, telnet prompt,
; IRC gateways and some web pages. Will use _host_name if unspecified.
_nick_server = $HOST_NAME

[_administrators]
; Space-seperated list of administrator user nicknames.
_list_nicks = $ADMIN_NICKNAME
; If the administrators have not been registered yet, this password will be
; assigned to them. If you leave this out you will be prompted for each as yet
; unregistered administrator, but you have to run psyconf manually!
;_password_default = $ADMIN_PASSWORD

[_protocols]
; if you don't have TLS or SSL simply set this to
; 0 and all the _encrypted ports will be ignored
_use_encryption = $TLS_10

[_protocols_port]
_PSYC = $PSYC_PORT
_PSYC_encrypted = $PSYCS_PORT
; experimental new PSYC syntax
;_SPYC = $PSYC_PORT
_telnet = $TELNET_PORT
_telnet_encrypted = ${TELNETS_PORT}
_jabber_S2S = $INTERJABBER_PORT
_jabber_clients = $JABBER_PORT
_jabber_clients_encrypted = $JABBERS_PORT
_IRC = $IRC_PORT
_IRC_encrypted = $IRCS_PORT
_HTTP = $HTTP_PORT
_HTTP_encrypted = $HTTPS_PORT
_applet = $APPLET_PORT
_SMTP = $SMTP_PORT
_SMTP_encrypted = $SMTPS_PORT

; Experimental protocol services
_POP3 = $POP3_PORT
_POP3_encrypted = $POP3S_PORT
_NNTP = $NNTP_PORT
_NNTP_encrypted = $NNTPS_PORT

[_optional]
; Enable web-based configuration tool
_config_HTTP = ${HTTPCONFIG_10}

; Runtime output can either be in .out and .err files or onto the console
; For development, _console_debug is extremely useful,
; for regular service it is better to have output in files.
_console_debug = $CONSOLE_10
; '0' is tranquility unless something serious happens. best choice.
; '1' gives you slightly interesting output and LPC development debug.
; '2' or '3' is too much and too detailed. don't use it globally, as it         ; may even trigger exceptions. use it only with _extra_debug below.
_level_debug = $DEBUG
; Advanced extra debug flags for the psyclpc command line. You can debug
; specific parts of psyced like for example the textdb subsystem by adding
; -DDtext=2 here. You can figure out which other parts of psyced are debuggable
; by doing a "grep -r 'define DEBUG D' ." in the world directory.
;_extra_debug = 

; We create files that are editable by the psyc group
_umask = $UMASK

EOT
#; Have errors logged to an extra psyced.debug file
#_use_file_debug = $FILE_10

# this here is no longer an option.. as PSYC uses UTF-8 on the wire.
#
#; psyced has its internal data formatted in ISO-8859-15, but it has now
#; learned to convert it. so if you expect your system to be used predominantely
#; by UTF-8 clients, you may want to run it with a system charset of UTF-8.
#; this is a little experimental still, though
#_charset_system = ISO-8859-15
#; consider that you can take this decision only once at the beginning of an
#; installation, since later all the lastlogs and histories in the .o files
#; will be using that encoding, and switching to an other will likely produce
#; runtime errors because convert_charset() is unnecessarily intolerant.

# paranoidly check again
if test "x$USER" != "x$userid" -a "x$userid" != "xroot"
then
	echo "You want to install files as $USER. Please change to this user or become root."
	$exit
fi

if test ! -d $BASE_DIR
then
	echo "Creating $BASE_DIR..."
	if mkdir -m $BASE_PERM -p $BASE_DIR 2> /dev/null
	then
		echo "";    # nop?
	else
		if test "x$userid" = "xroot"
		then
			echo "Couldn't create $BASE_DIR. VERY STRANGE!!"
			$exit
		else
			echo "Couldn't create $BASE_DIR. Do you have the permissions to set up this directory?"
			$exit
		fi
	fi
fi

if test ! -d $CONFIG_DIR
then
	echo "Creating $CONFIG_DIR..."
	if mkdir -m $CONF_PERM -p $CONFIG_DIR 2> /dev/null
	then
		:
	else
		if test "x$userid" = "xroot"
		then
			echo "Couldn't create $CONFIG_DIR. VERY STRANGE!!"
			$exit
		else
			echo "Couldn't create $CONFIG_DIR. Do you have the permissions to set up this directory?"
			$exit
		fi
	fi
fi

echo "Extracting psyced data..."

if test -d .git
then
    # eh? what was 'realpath' supposed to be?
    if ! test `pwd` = $BASE_DIR
    then
        if git clone . $BASE_DIR && cp -a .git/config $BASE_DIR/.git
	then
	    :
	else
	    echo "Could not git clone. abort."
	    $exit
	fi
    fi
elif tar xf data.tar -C $BASE_DIR
then
    :
else
    echo "Could not extract program data. abort."
    $exit
fi

# we need to be completely sure these directories exist,
# so we just go ahead with brute force  :)
#
mkdir -m $BASE_PERM -p $LOG_DIR 2> /dev/null
mkdir -m $BASE_PERM -p $LOG_DIR/place 2> /dev/null
mkdir -m $BASE_PERM -p $DATA_DIR 2> /dev/null
mkdir -m $BASE_PERM -p $DATA_DIR/person 2> /dev/null
mkdir -m $BASE_PERM -p $DATA_DIR/place 2> /dev/null
mkdir -p $ARCH_DIR 2>/dev/null

if test -d $ARCH_DIR 
then
	if test ! -w $ARCH_DIR
	then
		echo "Hmm.. couldn't write to $ARCH_DIR! I get lost..."
		$exit
	fi
		
	if ! test $WITHOUT_DRIVER = "y"
	then
	    echo ""
	    echo "${hi}COMPILING ${driver}${lo}"
	    echo ""
	    if test -d .git; then
		git submodule init
		git submodule update
	    elif test `ls -d1 */src 2>/dev/null |wc -l` -lt 1; then
		echo ""
		echo "Extracting $driver source..."
		echo ""
		${zipcmd} -dc `ls -1 ${driver}-*tar.${zip}` 2>/dev/null | tar xf -
	    else
		echo ""
		echo "Warning: Re-using extracted $driver source..."
		echo ""
	    fi
	    src=`ls -d1 */src 2>/dev/null`
	    if test `echo $src |wc -w` -ne 1
	    then
		echo "${hi}ATTENTION: ${lo} More than one ${driver}-dir found. Skipping."
	    else
## SPECIAL CASE: currently ldmud erq doesn't support SRV
		if test -d $src/util/xerq
		then
		    rm -r $src/util/xerq
		    mv $src/util/erq $src/util/erq-non-srv
		    cp -rp $BASE_DIR/utility/erq $src/util/erq
		# else: presume erq-srv has already been copied to the
		# right place by an earlier run of this script
		# or even better, we are dealing with psyclpc
		fi
		if cd $src
		then
		    cat << EOF > settings/mypsyced

#!/bin/sh
#----- GENERATED BY install.sh

exec ./configure --prefix=$BASE_DIR --bindir=$ARCH_DIR --libdir=$BASE_DIR/world --libexec=$BASE_DIR/run --with-setting=mypsyced \$*
exit 1

#----- END OF PART GENERATED BY install.sh
#----- now we simply append $BASE_DIR/config/psyced.settings

EOF
		    if test -r settings/psyced; then
			# append settings/psyced instead, if it's a psyclpc
			cat settings/psyced >> settings/mypsyced
		    else
			cat $BASE_DIR/config/psyced.settings >> settings/mypsyced
		    fi
		    if chmod u+x settings/mypsyced ; settings/mypsyced ; make
		    then
			cd ../..
			# i have a feeling i should be using ARCH_DIR here
			if ! test -d bin-$arch
			then
			    mkdir bin-$arch
			fi
			cp "$src/$driver" bin-$arch
			if test "$WANT_ERQ" = "y"
			then
			    echo ""
			    echo "${hi}NOW COMPILING erq${lo}"
			    echo ""
#			    (cd "$src" && make utils)
			    (cd "$src/util/erq" && make erq)
			    cp "$src/util/erq/erq" bin-$arch
			    # TODO: check success here!!
			fi
			    
		    fi
		fi
	    fi
	    echo ""
#	    # TODO: don't say this if either $driver or erq failed to compile!
#	    echo "${hi}COMPILATION DONE${lo}"
	    echo ""

	    # i have a feeling i should be using ARCH_DIR here
	    if test -d bin-$arch 
	    then
		cd bin-$arch
		for i in *
		do
			cp $i $ARCH_DIR/$i
			chown $USER $ARCH_DIR/$i
			chgrp $GROUP $ARCH_DIR/$i
			chmod $BASE_PERM $ARCH_DIR/$i
			chmod u+x $ARCH_DIR/$i
		done
		cd ..

	    else
		echo "${hi}WARNING:${lo} Couldn't install architecture dependent binaries because I can't find them!"
		echo ""
	    fi
	fi
else
	echo "Hmm.. couldn't create $ARCH_DIR! Aborting."
	$exit
fi

if test -r "$CONFIG_DIR/psyced.ini"; then
	echo "${hi}WARNING:${lo} Renaming your old psyced.ini into psyced-old-$$.ini!"
	echo ""
	mv "$CONFIG_DIR/psyced.ini" "$CONFIG_DIR/psyced-old-$$.ini"
fi

cp -p psyced.ini "$CONFIG_DIR"
cp -p .config *.txt "$BASE_DIR"

cd "$BASE_DIR"

# rerunning install.sh implies redoing your configuration from scratch!
# but we give you a chance to keep a backup. should we inform the user
# about it?
#
cp -rp "$BASE_DIR/local" "/tmp/local$$" 2> /dev/null
rm -f $BASE_DIR/local 2> /dev/null

# we previously tried to use symlinks or even partial symlinks for
# unmodified files only, but it can result in cvs collisions and
# headaches. so far the plain copy approach is best.
#
cp -rp "$BASE_DIR/config/blueprint" "$BASE_DIR/local"
#
# let's make sure it won't happen again   ;)
#rm -rf "$BASE_DIR/local/CVS"

if test "$RUNTIME_OUTPUT" = "files"
then
	if test ! -d $RUNTIME_OUTPUT_DIR
	then
		if mkdir -p $RUNTIME_OUTPUT_DIR 2> /dev/null
		then
			:
		else
			echo "ERROR: Couldn't create log directory $RUNTIME_OUTPUT_DIR"
			$exit
		fi	
	fi
fi

#if ! test "$WANT_CVSUP" = "n"
#then
#    echo "Updating to newest state by using CVS"
#    echo "Using CVSROOT `cat $BASE_DIR/CVS/Root`"
#    echo "${hi}ATTENTION: ${lo}Please give an empty password to log in (-> press enter)"
#    if (cd $BASE_DIR && cvs login && cvs -q update -d && cvs logout)
#    then
#	:
#    else
#	echo "${hi}Warning: ${lo}Something failed while trying to update. No CVS available?"
#	echo "The installation should be functioning however, using the stable code."
#    fi
#fi

echo ""
echo "Setting permissions for program files..."

chown -R $USER $BASE_DIR
chgrp -R $GROUP $BASE_DIR
# does this mark all files executable, even .c?
chmod -R $BASE_PERM $BASE_DIR
chmod -R u+x $BASE_DIR/bin 

echo "Setting permissions for data and log files..."

chmod -R $DATA_PERM $BASE_DIR/data $BASE_DIR/log

echo "Setting $GROUP group on configuration files..."

chgrp -R $GROUP $CONFIG_DIR

# and now we'll see if perl is installed :)
bin/psyconf psyced.ini

#echo ""
#echo "Installation finished.  :)" # (Sieg durch Selbstgefälligkeit)
#echo ""

echo "You may want to edit psyced.ini and run 'bin/psyconf psyced.ini'"
echo "again, to tweak some more detailed settings."
echo ""

if test "$HTTP_YN" = "y"
then
	echo "Once started you will find usage manuals at"
	echo "http://$SERVER_HOST:$HTTP_PORT/$DEFLANG/help/index.page"
	echo ""
	if test "$HTTPCONFIG_YN" = "y"
	then
		echo "And don't forget to inspect the web configurator at"
		echo "http://127.1:$HTTP_PORT/net/http/configure"
		echo ""
	fi
fi
