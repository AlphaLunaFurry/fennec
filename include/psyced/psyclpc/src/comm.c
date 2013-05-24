/*---------------------------------------------------------------------------
 * Gamedriver Communications module.
 *
 *---------------------------------------------------------------------------
 * Throughout the module the fact is used that valid socket numbers
 * are always >= 0. Unused sockets are therefore marked with negative
 * numbers.
 *
 * All information needed for an interactive object are stored in
 * a 'interactive_t'. This struct is linked to by the shadow sentence
 * of the interactive object.
 *
 * Sending data is performed through the function add_message().
 * The function collects the data in interactive.message_buf[] until
 * it is filled (or a special 'flush' message is passed), then the
 * data is written en-bloc to the network.
 *
 * Incoming data is collected in interactive.text[]. interactive.text_end
 * indexes the first free character in the buffer where new data is to
 * be appended. The new data is not passed directly to the command parser,
 * instead it is processed by a dfa implementing the important parts
 * of the telnet protocol. The dfa analyses the data read, interprets
 * any telnet commands and stores the remaining 'pure' data starting
 * from the beginning of .text[].
 *
 * Initialized to start working in the state TS_DATA, the dfa does its
 * thing until it either reaches a line end or the end of the current
 * data. If it is a line end, it terminates the pure data collected so
 * far with a '\0', goes into state TS_READY, and lets .tn_end index
 * the next unprocessed raw data char. If it is the end of the current
 * data, the dfa stays in whatever state it was and indexes the current end
 * of the pure data gathered so far with interactive.command_end. Once
 * a new chunk of data has been read, the dfa simply continues where it
 * took off.
 *
 * There are some variations to this scheme, but you get the idea. It is
 * possible to do all telnet negotiations on mudlib level, but it must
 * be either the driver or the mudlib, not both.
 *
 * To understand get_message() itself fully, think of it as a coroutine
 * with its own state. It does not really return to the caller (though
 * that is how it is implemented), in merely yields control back to
 * caller in order to process the found command or the pending heartbeat.
 * TODO: Obvious possibility for implementation multi-threading.
 *
 * Timing is implemented this way: The driver usually stays in the input
 * loop, waiting in 1 second intervals for incoming data. An alarm() is
 * triggered by backend.c every 2 seconds and sets the flag variable
 * comm_time_to_call_heart_beat. The loop checks this variable every second
 * and, if it is set, aborts its input loop and returns to the backend.
 * To mark the cause of the return, the variable time_to_call_heart_beat is
 * set before return.
 *
 * TODO: The noecho/charmode logic, especially in combination with
 * TODO:: the telnet machine is frustratingly underdocumented.
 *
#ifdef USE_PTHREADS
 * The data is not written directly to the sockets, but instead to
 * an intermediate buffer, from which a secondary thread does the actual
 * writing. The buffers are stored in a linked list in the interactive-s
 * structure. Advantage is that the driver is no longer bothered by blocking
 * sockets.
 *
 * TODO: Generalize the background buffer and either use pthreads, or a call
 * TODO:: from the backend loop to write the data. Also  don't
 * TODO:: immediately discard EWOULDBLOCK-failed messages.
#endif
 *
 * TODO: Fiona says: The telnet code is frustrating. It would be better if
 * TODO:: the common handling of e.g. TELNET_NAWS is offered by hooks,
 * TODO:: as the Anarres version of MudOS does. This would mean a rewrite.
 *---------------------------------------------------------------------------
 */

#define SAVE_NOECHO       /* TODO: Define to enable safe NOECHO mode */
#define SIMULATE_CHARMODE /* TODO: Even linemode clients stay in charmode */

#include "driver.h"
#include "typedefs.h"

#include "my-alloca.h"

#include <stdio.h>
#include <ctype.h>
#include <sys/time.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/ioctl.h>

#define TELOPTS
#include "../mudlib/sys/telnet.h"

#ifdef HAVE_NETDB_H
#    include <netdb.h>
#endif

#ifdef HAVE_SYS_PARAM_H
#    include <sys/param.h>
#endif

#include <signal.h>

#if defined(_AIX) || defined(__EMX__) || defined(OS2)
#    include <sys/select.h>
#endif

#ifdef HAVE_FCNTL
#    include <fcntl.h>
#endif

#ifdef SOCKET_INC
#    include SOCKET_INC
#endif

#include "comm.h"
#include "access_check.h"
#include "actions.h"
#include "array.h"
#include "closure.h"
#include "ed.h"
#include "exec.h"
#include "filestat.h"
#include "gcollect.h"
#include "interpret.h"
#include "main.h"
#include "mstrings.h"
#include "object.h"
#include "pkg-mccp.h"
#include "pkg-pgsql.h"
#ifdef USE_TLS
#include "pkg-tls.h"
#endif
#include "sent.h"
#include "simulate.h"
#include "stdstrings.h"
#include "svalue.h"
#ifdef USE_SWAP
#include "swap.h"
#endif
#include "wiz_list.h"
#include "xalloc.h"

#include "i-eval_cost.h"

#include "../mudlib/sys/comm.h"
#include "../mudlib/sys/driver_hook.h"
#include "../mudlib/sys/input_to.h"

/* if driver is compiled for ERQ demon then include the necessary file
 */
#ifdef ERQ_DEMON
#    ifdef ERQ_INCLUDE
#        include ERQ_INCLUDE
#    else
#        include "util/erq/erq.h"
#    endif
#endif

/* When no special networking code is needed, define the
 * socket function to their normal Unix names.
 */
#if !defined (SOCKET_LIB) && !defined(SOCKET_INC)
#    define socket_number(s) (s)
#    define socket_ioctl  ioctl
#    ifndef hpux
#        define socket_select select
#    else
#        define socket_select(n,r,w,x,t) select(n, (int *)r, (int *)w, (int *)x, t)
         /* Silences the compiler */
#    endif
#    define socket_read   read
#    define socket_write  write
#    define socket_close  close
#endif /* SOCKET_LIB */

#if defined(SunOS4)
struct timeval;
extern SOCKET_T socket(int, int, int);
extern int getpeername(SOCKET_T, struct sockaddr *, int *);
extern void  shutdown(SOCKET_T, int);
extern int setsockopt(SOCKET_T, int, int, char *, int);
extern int bind(SOCKET_T, struct sockaddr *, int);
extern int listen(SOCKET_T, int);
extern SOCKET_T accept(SOCKET_T, struct sockaddr *, int *);
extern int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
#endif /* SunOS4 */

#if defined(_AIX)
typedef unsigned long length_t;
#elif defined(__INTEL_COMPILER) || defined (__GNUC__)
typedef socklen_t length_t;
#else
typedef int length_t;
#endif

#if defined(CYGWIN)
extern int socketpair(int, int, int, int[2]);
#endif

#ifndef EPROTO
#    define EPROTO EINTR
#endif
#ifndef SIGCLD
#    define SIGCLD SIGCHLD
#endif

#ifndef MAXHOSTNAMELEN
#    define MAXHOSTNAMELEN 64
#endif

#ifndef INET_ADDRSTRLEN
#    define INET_ADDRSTRLEN 16
#endif

/* Amazing how complicated networking can be, hm? */

/*-------------------------------------------------------------------------*/

  /* number of pending out connections, avoids unnecessary loops
   */
int num_pending_outconns = 0;

interactive_t *all_players[MAX_PLAYERS];
  /* Pointers to the structures of the interactive users.
   * Unused entries are NULL.
   * TODO: A list would be nicer
   */

static int max_player = -1;
  /* Index of the last used entry in all_players[]. */

int num_player = 0;
  /* The current number of active users */

char *message_flush = NULL;
  /* Special flush message for add_message().
   * It is a variable instead of a define to keep gcc from complaining about
   * a 'null format string'.
   */

long pthread_write_max_size = PTHREAD_WRITE_MAX_SIZE;
  /* Amount of data held pending in the pthread fifo queue.
   * Evaluated only with USE_PTHREADS.
   */


#ifdef COMM_STAT

/* The statistics were originally introduced to measure the efficiency
 * of the message buffering in comparison to the unbuffered sending of
 * data. Nowadays, it's just interesting to know how much bandwidth you
 * use.
 */

unsigned long add_message_calls = 0;
  /* Number of calls to add_message() */

unsigned long inet_packets = 0;
  /* Number packets sent to the users */

unsigned long inet_volume = 0;
  /* Amount of data sent to the users */

unsigned long inet_packets_in = 0;
  /* Number packets received from the users */

unsigned long inet_volume_in = 0;
  /* Amount of data received from the users */

#endif

/*-------------------------------------------------------------------------*/

#ifdef ERQ_DEMON

#define MAX_PENDING_ERQ  32  /* Max number of pending requests */

#define FLAG_NO_ERQ   -2    /* No ERQ connected */
#define FLAG_ERQ_STOP -1    /* Severing connection */

static SOCKET_T erq_demon = FLAG_NO_ERQ;
  /* Socket of the connection to the erq demon. */

static SOCKET_T erq_proto_demon = -1;
  /* Socket to hold the connection to an aspiring new erq demon
   * while the connection to the current one is being severed.
   */

static char buf_from_erq[ERQ_MAX_REPLY];
  /* Buffer for the data received from the erq */

static char * input_from_erq = &buf_from_erq[0];
  /* Pointer to the first free byte in buf_from_erq. */

static unsigned long erq_pending_len = 0;
  /* erq_pending_len is used by send_erq(), but needs
   * to be cleared by stop_erq_demon().
   */

typedef struct erq_callback_s {
    svalue_t fun;
    Bool     string_arg;
} erq_callback_t;

static erq_callback_t pending_erq[MAX_PENDING_ERQ+1];
  /* ERQ callback handles. The last one is reserved for callback-free
   * requests.
   * .fun is the callback closure, .string_arg is TRUE if the closure
   * takes its data as a string instead of an array as argument.
   *
   * The free entries are organised in a singly linked list of
   * T_INVALID .fun svalues, using the .fun.u.generic to point to the next
   * free entry.
   */

static erq_callback_t *free_erq;
  /* The first free entry in the freelist in pending_erq[] */

/* The size of the IPTABLE depends on the number of users,
 * and is at least 200.
 */
#if MAX_PLAYERS > 700
#    define IPSIZE MAX_PLAYERS
#else
#    if MAX_PLAYERS > 100
#        define IPSIZE (MAX_PLAYERS*2)
#    else
#        define IPSIZE 200
#    endif
#endif

static struct ipentry {
    struct in_addr  addr;  /* The address (only .s_addr is significant) */
    string_t       *name;  /* tabled string with the hostname for <addr> */
} iptable[IPSIZE];
  /* Cache of known names for given IP addresses.
   * It is used as a ringbuffer, indexed by ipcur.
   * TODO: Instead of a simple circular buffer, the lookup should be
   * TODO:: hashed over the IP address. Worst case would still be O(IPSIZE),
   * TODO:: but best case would be O(1).
   * TODO: Allow unlimited numbers of entries, but give them a max lifetime
   * TODO:: of a day. After that, even existing entries need to be
   * TODO:: re-resolved. Maybe an additional size limit. (suggested by Coogan)
   */

static int ipcur = 0;
  /* Index of the next entry to use in the iptable[].
   */

#endif /* ERQ_DEMON */

/*-------------------------------------------------------------------------*/

/* --- Communication sockets --- */

static SOCKET_T sos[MAXNUMPORTS];
  /* The login sockets.
   */

static SOCKET_T udp_s = -1;
  /* The UDP socket */

/* --- Networking information --- */

static char host_name[MAXHOSTNAMELEN+1];
  /* This computer's hostname, used for query_host_name() efun.
   */

static struct in_addr host_ip_number;
  /* This computer's numeric IP address only, used for
   * the query_host_ip_number() efun.
   */

static struct sockaddr_in host_ip_addr_template;
  /* The template address of this computer. It is copied locally
   * and augmented with varying port numbers to open the driver's ports.
   */

char * domain_name = NULL;
  /* This computer's domain name, as needed by lex.c::get_domainname().
   */

static int min_nfds = 0;
  /* The number of fds used by the driver's sockets (udp, erq, login).
   * It is the number of the highest fd plus one.
   */

/* --- Telnet handling --- */

Bool sending_telnet_command = MY_FALSE;
  /* Mutex queried in add_message() to hide telnet commands
   * from snoopers and shadows.
   */

static void (*telopts_do  [NTELOPTS])(int);
static void (*telopts_dont[NTELOPTS])(int);
static void (*telopts_will[NTELOPTS])(int);
static void (*telopts_wont[NTELOPTS])(int);

  /* Tables with the telnet statemachine handlers.
   */

enum telnet_states {
  TS_DATA = 0,
  TS_IAC,
  TS_WILL,
  TS_WONT,
  TS_DO,
  TS_DONT,
  TS_SB,
  TS_SB_IAC,
  TS_READY,
  TS_SYNCH,
  TS_INVALID
};

  /* Telnet states
   */

static inline int TN_START_VALID(int x) {
  return (x & ~TS_IAC) == TS_SB;
}


/* --- Misc --- */

static volatile Bool urgent_data = MY_FALSE;
  /* Flag set when a SIGURG/SIGIO announces the arrival of
   * OOB data.
   */

static volatile mp_int urgent_data_time;
  /* The backend::current_time when urgent_data was set last.
   */

static object_t *first_player_for_flush = NULL;
  /* First interactive user object to flush. Marks the head
   * of the list formed by interactive.{next,previous}_player_for_flush
   */


/* Bitflags for interactive.do_close
 *
 * Putting PROTO_ERQ into do_close looks strange, but actually makes
 * sense because some of the steps to be taken for both are the
 * same.
 */

#define FLAG_DO_CLOSE   0x1
#define FLAG_PROTO_ERQ  0x2


/*-------------------------------------------------------------------------*/

/* Outgoing connections in-progress */

typedef enum {
      ocNotUsed    /* Entry not used */
    , ocUsed       /* Entry holds pending connection */
    , ocLoggingOn  /* Entry is doing the LPC logon protocol.
                    * This value should not appear outside of
                    * check_for_out_connections(), if it does, it
                    * means that LPC logon threw an error.
                    */
} OutConnStatus;

struct OutConn {
    struct sockaddr_in   target;   /* Address connected to (allocated) */
    object_t           * curr_obj; /* Associated object */
    int                  socket;   /* Socket on our side */
    OutConnStatus        status;   /* Status of this entry */
} outconn[MAX_OUTCONN];

/*-------------------------------------------------------------------------*/

/* Forward declarations */

static void mccp_telnet_neg(int);

static void free_input_to(input_to_t *);
static void telnet_neg(interactive_t *);
static void send_will(int);
static void send_wont(int);
static void send_do(int);
static void send_dont(int);
static void remove_flush_entry(interactive_t *ip);
static void new_player(object_t *receiver, SOCKET_T new_socket, struct sockaddr_in *addr, size_t len, int login_port);

#ifdef ERQ_DEMON

static long read_32(char *);
static Bool send_erq(int handle, int request, const char *arg, size_t arglen);
static void shutdown_erq_demon(void);
static void stop_erq_demon(Bool);
static string_t * lookup_ip_entry (struct in_addr addr, Bool useErq);
static void add_ip_entry(struct in_addr addr, const char *name);
#ifdef USE_IPV6
static void update_ip_entry(const char *oldname, const char *newname);
static int open_ipv6_conn(const char *hostname, const unsigned short int port, struct sockaddr_in *pTarget);
#endif

#endif /* ERQ_DEMON */

#ifdef USE_PTHREADS
static void *writer_thread(void *arg);
#endif

#ifdef USE_AUTHLOCAL
/*define DEBUG_AUTHLOCAL */
# include "authlocal.c"
#endif

#ifdef USE_IPV6

/*-------------------------------------------------------------------------*/

/* Not every IPv6 supporting platform has all the defines (like AIX 4.3) */

#ifndef IPV6_ADDR_SCOPE_GLOBAL
#  define IPV6_ADDR_SCOPE_GLOBAL 0x0e
#endif

#ifndef s6_addr8
#  define s6_addr8  __u6_addr.__u6_addr8
#endif

#ifndef s6_addr16
#  define s6_addr16 __u6_addr.__u6_addr16
#endif

#ifndef s6_addr32
#  define s6_addr32 __u6_addr.__u6_addr32
#endif

static inline void CREATE_IPV6_MAPPED(struct in_addr *v6, uint32 v4) {
  v6->s6_addr32[0] = 0;
  v6->s6_addr32[1] = 0;
  v6->s6_addr32[2] = 0x0000ffff;
  v6->s6_addr32[2] = 0xffff0000;
  v6->s6_addr32[3] = v4;
}

/* These are the typical IPv6 structures - we use them transparently.
 *
 * --- arpa/inet.h ---
 *
 * struct in6_addr {
 *         union {
 *                 u_int32_t u6_addr32[4];
 * #ifdef notyet
 *                 u_int64_t u6_addr64[2];
 * #endif
 *                 u_int16_t u6_addr16[8];
 *                 u_int8_t  u6_addr8[16];
 *         } u6_addr;
 * };
 * #define s6_addr32       u6_addr.u6_addr32
 * #ifdef notyet
 * #define s6_addr64       u6_addr.u6_addr64
 * #endif
 * #define s6_addr16       u6_addr.u6_addr16
 * #define s6_addr8        u6_addr.u6_addr8
 * #define s6_addr         u6_addr.u6_addr8
 *
 * --- netinet/in.h ---
 *
 * struct sockaddr_in6 {
 *    u_char                sin6_len;
 *    u_char                sin6_family;
 *    u_int16_t        sin6_port;
 *    u_int32_t        sin6_flowinfo;
 *    struct                 in6_addr        sin6_addr;
 * };
 *
 */

/*-------------------------------------------------------------------------*/
static char *
inet6_ntoa (struct in6_addr in)

/* Convert the ipv6 address <in> into a string and return it.
 * Note: the string is stored in a local buffer.
 */

{
    static char str[INET6_ADDRSTRLEN+1];

    if (NULL == inet_ntop(AF_INET6, &in, str, INET6_ADDRSTRLEN))
    {
        perror("inet_ntop");
    }
    return str;
} /* inet6_ntoa() */

/*-------------------------------------------------------------------------*/
static struct in6_addr
inet6_addr (const char *to_host)

/* Convert the name <to_host> into a ipv6 address and return it.
 */

{
    struct in6_addr addr;

    inet_pton(AF_INET6, to_host, &addr);
    return addr;
} /* inet6_addr() */

#endif /* USE_IPV6 */

/*-------------------------------------------------------------------------*/
static char *
decode_noecho (char noecho)

/* Decode the <noecho> flag byte into a string.
 * Result is a pointer to a static buffer.
 */

{
    static char buf[100];
    strcpy(buf, "(");
    if (noecho & NOECHO_REQ) strcat(buf, "NOECHO_REQ, ");
    if (noecho & CHARMODE_REQ) strcat(buf, "CHARMODE_REQ, ");
    if (noecho & NOECHO) strcat(buf, "NOECHO, ");
    if (noecho & CHARMODE) strcat(buf, "CHARMODE, ");
    if (noecho & NOECHO_ACK) strcat(buf, "NOECHO_ACK, ");
    if (noecho & CHARMODE_ACK) strcat(buf, "CHARMODE_ACK, ");
#ifdef SAVE_NOECHO
    if (noecho & NOECHO_DELAYED) strcat(buf, "NOECHO_DELAYED, ");
#endif
    if (noecho & NOECHO_STALE) strcat(buf, "NOECHO_STALE, ");
    if (noecho & IGNORE_BANG) strcat(buf, "IGNORE_BANG");
    strcat(buf, ")");

    return buf;
} /* decode_noecho() */

/*-------------------------------------------------------------------------*/
static void
dump_bytes (void * data, size_t length, int indent)

/* Write the datablock starting at <data> of size <length> to stderr.
 * If it spans more than one line, indent the following lines by <indent>.
 */

{
    int cur_indent = 0;
    unsigned char * datap = (unsigned char *)data;

    while (length > 0)
    {
        size_t count;

        if (cur_indent)
            fprintf(stderr, "%*.*s", cur_indent, cur_indent, " ");
        else
            cur_indent = indent;
        fprintf(stderr, " %p:", datap);

        for (count = 0; count < 16 && length > 0; ++count, --length, ++datap)
        {
            fprintf(stderr, " %02x", *datap);
        }
        putc('\n', stderr);
    }
} /* dump_bytes() */

/*-------------------------------------------------------------------------*/
static void comm_fatal (interactive_t *ip, char *fmt, ...)
                          FORMATDEBUG(printf,2,3) ;
static void
comm_fatal (interactive_t *ip, char *fmt, ...)

/* The telnet code ran into a fatal error.
 * Dump the data from the current interactive structure and disconnect
 * the user (we have to assume that the interactive structure is
 * irrecoverably hosed).
 * TODO: Make similar functions comm_error(), comm_perror() which prefix
 * TODO:: the error message with the ip %p and obj name.
 */

{
    va_list va;
    static Bool in_fatal = MY_FALSE;
    char * ts;
    char * msg = "\r\n=== Internal communications error in mud driver.\r\n"
                   "=== Please log back in and inform the administration.\r\n"
                   "\r\n";

    /* Prevent double fatal. */
    if (in_fatal)
        fatal("Recursive call to comm_fatal().");
    in_fatal = MY_TRUE;
    ts = time_stamp();

    /* Print the error message */

    va_start(va, fmt);

#ifdef VERBOSE
    fflush(stdout);
    fprintf(stderr, "%s ", ts);
    vfprintf(stderr, fmt, va);
    fflush(stderr);
    if (current_object)
        fprintf(stderr, "%s Current object was %s\n"
                      , ts, current_object->name
                            ? get_txt(current_object->name) : "<null>");
#endif
    debug_message("%s ", ts);
    vdebug_message(fmt, va);
    if (current_object)
        debug_message("%s Current object was %s\n"
                     , ts, current_object->name
                           ? get_txt(current_object->name) : "<null>");
    debug_message("%s Dump of the call chain:\n", ts);
    (void)dump_trace(MY_TRUE, NULL); fflush(stdout);

    va_end(va);

    /* Dump the interactive structure */

    fprintf(stderr, "--- Dump of current interactive structure (%p..%p) --- \n"
                  , ip, ip + sizeof(*ip) - 1);
    fprintf(stderr, "  .socket:            %d\n", ip->socket);
    fprintf(stderr, "  .ob:                %p", ip->ob);
      if (ip->ob)  fprintf(stderr, " (%s)", get_txt(ip->ob->name));
      putc('\n', stderr);
    fprintf(stderr, "  .input_to:          %p\n", ip->input_to);
    fprintf(stderr, "  .modify_command:    %p", ip->modify_command);
      if (ip->modify_command)  fprintf(stderr, " (%s)", get_txt(ip->modify_command->name));
      putc('\n', stderr);
    fprintf(stderr, "  .prompt:           ");
      dump_bytes(&(ip->prompt), sizeof(ip->prompt), 21);
    fprintf(stderr, "  .addr:             ");
      dump_bytes(&(ip->addr), sizeof(ip->addr), 21);
    fprintf(stderr, "  .msg_discarded:     %02hhx\n", (unsigned char)ip->msg_discarded);
    fprintf(stderr, "  .set_input_to:      %02hhx\n", (unsigned char)ip->set_input_to);
    fprintf(stderr, "  .closing:           %02hhx\n", (unsigned char)ip->closing);
    fprintf(stderr, "  .tn_enabled:        %02hhx\n", (unsigned char)ip->tn_enabled);
    fprintf(stderr, "  .do_close:          %02hhx", (unsigned char)ip->do_close);
      if (ip->do_close & (FLAG_DO_CLOSE|FLAG_PROTO_ERQ)) fprintf(stderr, " (");
      if (ip->do_close & FLAG_DO_CLOSE) fprintf(stderr, "DO_CLOSE");
      if (ip->do_close & (FLAG_DO_CLOSE|FLAG_PROTO_ERQ)) fprintf(stderr, ", ");
      if (ip->do_close & FLAG_PROTO_ERQ) fprintf(stderr, "PROTO_ERQ");
      if (ip->do_close & (FLAG_DO_CLOSE|FLAG_PROTO_ERQ)) fprintf(stderr, ")");
      putc('\n', stderr);
    fprintf(stderr, "  .noecho:            %02hhx", (unsigned char)ip->noecho);
      fprintf(stderr, " %s\n", decode_noecho(ip->noecho));
    fprintf(stderr, "  .tn_state:          %hhd", ip->tn_state);
      switch(ip->tn_state) {
      case TS_DATA:    fprintf(stderr, " (TS_DATA)\n"); break;
      case TS_IAC:     fprintf(stderr, " (TS_IAC)\n"); break;
      case TS_WILL:    fprintf(stderr, " (TS_WILL)\n"); break;
      case TS_WONT:    fprintf(stderr, " (TS_WONT)\n"); break;
      case TS_DO:      fprintf(stderr, " (TS_DO)\n"); break;
      case TS_DONT:    fprintf(stderr, " (TS_DONT)\n"); break;
      case TS_SB:      fprintf(stderr, " (TS_SB)\n"); break;
      case TS_SB_IAC:  fprintf(stderr, " (TS_SB_IAC)\n"); break;
      case TS_READY:   fprintf(stderr, " (TS_READY)\n"); break;
      case TS_SYNCH:   fprintf(stderr, " (TS_SYNCH)\n"); break;
      case TS_INVALID: fprintf(stderr, " (TS_INVALID)\n"); break;
      default: putc('\n', stderr);
      }
    fprintf(stderr, "  .save_tn_state:     %hhd", ip->save_tn_state);
      switch(ip->save_tn_state) {
      case TS_DATA:    fprintf(stderr, " (TS_DATA)\n"); break;
      case TS_IAC:     fprintf(stderr, " (TS_IAC)\n"); break;
      case TS_WILL:    fprintf(stderr, " (TS_WILL)\n"); break;
      case TS_WONT:    fprintf(stderr, " (TS_WONT)\n"); break;
      case TS_DO:      fprintf(stderr, " (TS_DO)\n"); break;
      case TS_DONT:    fprintf(stderr, " (TS_DONT)\n"); break;
      case TS_SB:      fprintf(stderr, " (TS_SB)\n"); break;
      case TS_SB_IAC:  fprintf(stderr, " (TS_SB_IAC)\n"); break;
      case TS_READY:   fprintf(stderr, " (TS_READY)\n"); break;
      case TS_SYNCH:   fprintf(stderr, " (TS_SYNCH)\n"); break;
      case TS_INVALID: fprintf(stderr, " (TS_INVALID)\n"); break;
      default: putc('\n', stderr);
      }
    fprintf(stderr, "  .supress_go_ahead:  %02hhx\n", (unsigned char)ip->supress_go_ahead);
    fprintf(stderr, "  .text_end:          %hd (%p)\n", ip->text_end, ip->text+ip->text_end);
    fprintf(stderr, "  .command_start:     %hd (%p)\n", ip->command_start, ip->text+ip->command_start);
    fprintf(stderr, "  .command_end:       %hd (%p)\n", ip->command_end, ip->text+ip->command_end);
    fprintf(stderr, "  .tn_start:          %hd (%p)\n", ip->tn_start, ip->text+ip->tn_start);
    fprintf(stderr, "  .tn_end:            %hd (%p)\n", ip->tn_end, ip->text+ip->tn_end);
    fprintf(stderr, "  .chars_ready:       %"PRId32"\n",ip->chars_ready);
#ifdef USE_SNOOPING
    fprintf(stderr, "  .snoop_on:          %p", ip->snoop_on);
      if (ip->snoop_on && ip->snoop_on->ob) fprintf(stderr, " (%s)", get_txt(ip->snoop_on->ob->name));
      putc('\n', stderr);
    fprintf(stderr, "  .snoop_by:          %p", ip->snoop_by);
      if (ip->snoop_by) fprintf(stderr, " (%s)", get_txt(ip->snoop_by->name));
      putc('\n', stderr);
#endif
    fprintf(stderr, "  .last_time:         %"PRIdMPINT"\n", ip->last_time);
#ifdef USE_SOCKET_LIMITS
    fprintf(stderr, "  .numCmds:           %ld\n", ip->numCmds);
    fprintf(stderr, "  .maxNumCmds:        %ld\n", ip->maxNumCmds);
#endif
    fprintf(stderr, "  .trace_level:       %d\n", ip->trace_level);
    fprintf(stderr, "  .trace_prefix:      %p", ip->trace_prefix);
      if (ip->trace_prefix) fprintf(stderr, " '%s'", get_txt(ip->trace_prefix));
      putc('\n', stderr);
    fprintf(stderr, "  .message_length:    %d (%p)\n", ip->message_length, ip->message_buf+ip->message_length);
    fprintf(stderr, "  .next_for_flush:    %p", ip->next_player_for_flush);
      if (ip->next_player_for_flush) fprintf(stderr, " (%s)", get_txt(ip->next_player_for_flush->name));
      putc('\n', stderr);
    fprintf(stderr, "  .prev_for_flush:    %p", ip->previous_player_for_flush);
      if (ip->previous_player_for_flush) fprintf(stderr, " (%s)", get_txt(ip->previous_player_for_flush->name));
      putc('\n', stderr);
    fprintf(stderr, "  .access_class:      %ld\n", ip->access_class);
    fprintf(stderr, "  .charset:          ");
      dump_bytes(&(ip->charset), sizeof(ip->charset), 21);
    fprintf(stderr, "  .combine_cset:     ");
      dump_bytes(&(ip->combine_cset), sizeof(ip->combine_cset), 21);
    fprintf(stderr, "  .quote_iac:         %02hhx\n", (unsigned char)ip->quote_iac);
    fprintf(stderr, "  .catch_tell_activ:  %02hhx\n", (unsigned char)ip->catch_tell_activ);
    fprintf(stderr, "  .gobble_char:       %02hhx\n", (unsigned char)ip->gobble_char);
    fprintf(stderr, "  .ts_data:           %02hhx\n", (unsigned char)ip->ts_data);
    fprintf(stderr, "  .text:             ");
      dump_bytes(&(ip->text), sizeof(ip->text), 21);
    fprintf(stderr, "  .message_buf:      ");
      dump_bytes(&(ip->message_buf), sizeof(ip->message_buf), 21);
    fprintf(stderr, "------\n");

    /* Disconnect the user */
#ifdef USE_TLS
    if(ip->tls_status == TLS_ACTIVE)
        tls_write(ip, msg, strlen(msg));
    else
#endif
        socket_write(ip->socket, msg, strlen(msg));
    remove_interactive(ip->ob, MY_TRUE);

    /* Unset mutex */
    in_fatal = MY_FALSE;
} /* comm_fatal() */

/*-------------------------------------------------------------------------*/
static void
set_socket_nonblocking (SOCKET_T new_socket)

/* Set the <new_socket> into non-blocking mode.
 * Abort on error.
 */

{
    int tmp;

#ifndef __BEOS__

    tmp = 1;

# ifdef USE_IOCTL_FIONBIO
    if (socket_ioctl(new_socket, FIONBIO, &tmp) == -1) {
        perror("ioctl socket FIONBIO");
        abort();
    }
# else /* !USE_IOCTL_FIONBIO */
# ifdef USE_FCNTL_O_NDELAY
    if (fcntl(new_socket, F_SETFL, O_NDELAY) == -1) {
# else
    if (fcntl(new_socket, F_SETFL, FNDELAY) == -1) {
# endif
        perror("fcntl socket FNDELAY");
        abort();
    }
# endif /* !USE_IOCTL_FIONBIO */

#else /* if __BEOS__ */

    /* BeOS up to R4 uses different filedescriptors for files and sockets;
     * so a fcntl() modifies the regular file with the number <new_socket>,
     * but not the socket itself. setsockopt() comes to our rescue.
     * TODO: Add setsockopt() to configure to test?
     */
    tmp = 1;
    if (setsockopt(new_socket, SOL_SOCKET, SO_NONBLOCK, &tmp, sizeof tmp))
        perror("setsockopt SO_NONBLOCK");

#endif /* if !__BEOS__ */

} /* set_socket_nonblocking() */

/*-------------------------------------------------------------------------*/
static void
set_close_on_exec (SOCKET_T new_socket)

/* Set that <new_socket> is closed when the driver performs an exec()
 * (i.e. when starting the ERQ).
 * Failure is acceptable as this is just a nicety.
 */

{
#ifdef HAVE_FCNTL
    fcntl(new_socket, F_SETFD, 1L);
#endif
} /* set_close_on_exec() */

/*-------------------------------------------------------------------------*/
static void
set_socket_own (SOCKET_T new_socket)

/* Enable OOB communication on <new_socket>: the driver is set to
 * receive SIGIO and SIGURG signals, and OOBINLINE is enabled.
 * Failure is acceptable as both facilities are not available everywhere.
 */

{
#if defined(F_SETOWN) && defined(USE_FCNTL_SETOWN)
    if (0 > fcntl(new_socket, F_SETOWN, getpid()))
    {
        perror("fcntl SETOWN");
    }
#endif
#if defined(SO_OOBINLINE) && defined(USE_OOBINLINE)
    {
        int on = 1;
        if (0 > setsockopt(new_socket, SOL_SOCKET, SO_OOBINLINE, (char *)&on, sizeof on))
        {
            perror("setsockopt SO_OOBINLINE");
        }
    }
#endif
    new_socket = 0; /* Prevent 'not used' warning */
} /* set_socket_own() */

/*-------------------------------------------------------------------------*/
void
initialize_host_name (const char *hname)

/* This function is called at an early stage of the driver startup in order
 * to initialise the global host_name with a useful value (specifically so
 * that we can open the debug.log file).
 * If <hname> is given, the hostname is parsed from the string, otherwise it
 * is queried from the system.
 *
 * The value set in this function will later be overwritten by the
 * call to initialize_host_ip_number().
 *
 * exit() on failure.
 */

{
    char *domain;

    /* Get the (possibly qualified) hostname */
    if (hname != NULL)
    {
        if (strlen(hname) > MAXHOSTNAMELEN)
        {
            fprintf(stderr, "%s Given hostname '%s' too long.\n"
                          , time_stamp(), hname);
            exit(1);
        }
        else
            strcpy(host_name, hname);
    }
    else
    {
        if (gethostname(host_name, sizeof host_name) == -1) {
            herror("gethostname");
            exit(1);
        }
    }

    /* Cut off the domain name part of the hostname, if any.
     */
    domain = strchr(host_name, '.');
    if (domain)
        *domain = '\0';
} /* initialize_host_name() */

/*-------------------------------------------------------------------------*/
void
initialize_host_ip_number (const char *hname, const char * haddr)

/* Initialise the globals host_ip_number and host_ip_addr_template.
 * If <hname> or <haddr> are given, the hostname/hostaddr are parsed
 * from the strings, otherwise they are queried from the system.
 *
 * Open the UDP port if requested so that it can be used in inaugurate_master().
 * exit() on failure.
 */

{
    char *domain;
    length_t tmp;

    /* Get the (possibly qualified) hostname */
    if (hname != NULL)
    {
        if (strlen(hname) > MAXHOSTNAMELEN)
        {
            fprintf(stderr, "%s Given hostname '%s' too long.\n"
                          , time_stamp(), hname);
            exit(1);
        }
        else
            strcpy(host_name, hname);
    }
    else
    {
        if (gethostname(host_name, sizeof host_name) == -1) {
            herror("gethostname");
            exit(1);
        }
    }

    /* Get the host address */
    memset(&host_ip_addr_template, 0, sizeof host_ip_addr_template);
    if (haddr != NULL)
    {
#ifndef USE_IPV6
        host_ip_number.s_addr = inet_addr(haddr);
        host_ip_addr_template.sin_family = AF_INET;
        host_ip_addr_template.sin_addr = host_ip_number;
#else
        host_ip_number = inet6_addr(haddr);
        host_ip_addr_template.sin_family = AF_INET6;
        host_ip_addr_template.sin_addr = host_ip_number;
#endif

        /* Find the domain part of the hostname */
        domain = strchr(host_name, '.');
    }
    else
    {
        struct hostent *hp;

        hp = gethostbyname(host_name);
        if (!hp) {
            fprintf(stderr, "%s gethostbyname: unknown host '%s'.\n"
                          , time_stamp(), host_name);
            exit(1);
        }
        memcpy(&host_ip_addr_template.sin_addr, hp->h_addr, (size_t)hp->h_length);
        host_ip_addr_template.sin_family = (unsigned short)hp->h_addrtype;
        host_ip_number = host_ip_addr_template.sin_addr;

        /* Now set the template to the proper _ANY value */
        memset(&host_ip_addr_template.sin_addr, 0, sizeof(host_ip_addr_template.sin_addr));
#ifndef USE_IPV6
        host_ip_addr_template.sin_addr.s_addr = INADDR_ANY;
        host_ip_addr_template.sin_family = AF_INET;
#else
        host_ip_addr_template.sin_addr = in6addr_any;
        host_ip_addr_template.sin_family = AF_INET6;
#endif

        /* Find the domain part of the hostname */
        if (hname == NULL)
            domain = strchr(hp->h_name, '.');
        else
            domain = strchr(host_name, '.');
    }

#ifndef USE_IPV6
#ifdef VERBOSE
    printf("%s Hostname '%s' address '%s'\n"
          , time_stamp(), host_name, inet_ntoa(host_ip_number));
#endif
    debug_message("%s Hostname '%s' address '%s'\n"
                 , time_stamp(), host_name, inet_ntoa(host_ip_number));
#else
#ifdef VERBOSE
    printf("%s Hostname '%s' address '%s'\n"
          , time_stamp(), host_name, inet6_ntoa(host_ip_number));
#endif
    debug_message("%s Hostname '%s' address '%s'\n"
                 , time_stamp(), host_name, inet6_ntoa(host_ip_number));
#endif

    /* Put the domain name part of the hostname into domain_name, then
     * strip it off the host_name[] (as only query_host_name() is going
     * to need it).
     * Note that domain might not point into host_name[] here, so we
     * can't just stomp '\0' in there.
     */
    if (domain)
    {
        domain_name = strdup(domain+1);
    }
    else
        domain_name = strdup("unknown");

    domain = strchr(host_name, '.');
    if (domain)
        *domain = '\0';

    /* Initialize udp at an early stage so that the master object can use
     * it in inaugurate_master() , and the port number is known.
     */
    if (udp_port != -1)
    {
        struct sockaddr_in host_ip_addr;

        memcpy(&host_ip_addr, &host_ip_addr_template, sizeof(host_ip_addr));

        host_ip_addr.sin_port = htons((u_short)udp_port);
        debug_message("%s UDP recv-socket requested for port: %d\n"
                     , time_stamp(), udp_port);
        udp_s = socket(host_ip_addr.sin_family, SOCK_DGRAM, 0);
        if (udp_s == -1)
        {
            perror("socket(udp_socket)");
            exit(1);
        }
        tmp = 1;
        if (setsockopt (udp_s, SOL_SOCKET, SO_REUSEADDR,
                        (char *) &tmp, sizeof (tmp)) < 0)
        {
            perror ("setsockopt(udp_s, SO_REUSEADDR)");
            exit(1);
        }

        /* Bind the UDP socket to an address.
         * First, try the given port number, if that one is in use
         * already, let bind() select one. If that one is in use, too,
         * close the socket again and pretend that we never had one.
         * Other errors abort the driver.
         */
        for(;;) {
            if (-1 == bind(udp_s, (struct sockaddr *)&host_ip_addr
                          , sizeof(host_ip_addr)))
            {
                if (errno == EADDRINUSE) {
#ifdef VERBOSE
                    fprintf(stderr, "%s UDP port %d already bound!\n"
                                  , time_stamp(), udp_port);
#endif
                    debug_message("%s UDP port %d already bound!\n"
                                  , time_stamp(), udp_port);
                    if (host_ip_addr.sin_port) {
                        host_ip_addr.sin_port = 0;
                        continue;
                    }
                    close(udp_s);
                    udp_s = -1;
                } else {
                    perror("udp-bind");
                    exit(1);
                }
            }
            break;
        }
    }

    /* If we got the UDP socket, get query it's real address and
     * initialise it.
     */
    if (udp_s >= 0) {
        struct sockaddr_in host_ip_addr;

        tmp = sizeof(host_ip_addr);
        if (!getsockname(udp_s, (struct sockaddr *)&host_ip_addr, &tmp))
        {
            int oldport = udp_port;

            udp_port = ntohs(host_ip_addr.sin_port);
            if (oldport != udp_port)
                debug_message("%s UDP recv-socket on port: %d\n"
                             , time_stamp(), udp_port);
        }
        set_socket_nonblocking(udp_s);
        set_close_on_exec(udp_s);
        if (socket_number(udp_s) >= min_nfds)
            min_nfds = socket_number(udp_s)+1;
    }

} /* initialize_host_ip_number() */


/*-------------------------------------------------------------------------*/
static void
ignore_handler (int signo)

/* Signal handler for ignored signals: it just reinitializes the signal
 * handler for this signal. It is used for OS where a signal(,SIG_IGN)
 * is implemented with a one-shot handler (e.g. Linux).
 */

{
#ifdef DEBUG
    if (signo != SIGPIPE) /* the only ignored signal so far */
    {
        fprintf(stderr, "%s Error: OS passes signo %d instead of SIGPIPE (%d) to handler.\n", time_stamp(), signo, SIGPIPE);
        signo = SIGPIPE;
    }
#endif
    signal(signo, (RETSIGTYPE(*)(int))ignore_handler);
}

/*-------------------------------------------------------------------------*/
static void
urgent_data_handler (int signo)

/* Signal handler for SIGURG/SIGIO: set the urgent_data flag and
 * note the time.
 */

{
    if (d_flag)
        write(2, "received urgent data\n", 21);
    urgent_data = MY_TRUE;
    urgent_data_time = current_time;
    signal(signo, (RETSIGTYPE(*)(int))urgent_data_handler);
}

/*-------------------------------------------------------------------------*/
void
prepare_ipc(void)

/* Open all login sockets on driver startup, exit() on a failure.
 */

{
    length_t tmp;
    int i;

#ifdef ERQ_DEMON
    /* Initialize the IP name lookup table */
    memset(iptable, 0, sizeof(iptable));
#endif

    for (i = 0; i < MAX_OUTCONN; i++)
        outconn[i].status = ocNotUsed;

    /* Initialize the telnet machine unless mudlib_telopts() already
     * did that.
     */
    if (!telopts_do[0])
      init_telopts();

    /* Loop over all given port numbers.
     * Remember: positive number are actual port numbers to be opened,
     * negative numbers are the fd numbers of already existing sockets.
     */
    for (i = 0; i < numports; i++)
    {
        struct sockaddr_in host_ip_addr;

        memcpy(&host_ip_addr, &host_ip_addr_template, sizeof(host_ip_addr));

        if (port_numbers[i] > 0)
        {
            /* Real port number */

            host_ip_addr.sin_port = htons((u_short)port_numbers[i]);
            sos[i] = socket(host_ip_addr.sin_family, SOCK_STREAM, 0);
            if ((int)sos[i] == -1) {
                perror("socket");
                exit(1);
            }
            tmp = 1;
            if (setsockopt(sos[i], SOL_SOCKET, SO_REUSEADDR
                          , (char *) &tmp, sizeof (tmp)) < 0) {
                perror ("setsockopt");
                exit (1);
            }
            if (bind(sos[i], (struct sockaddr *)&host_ip_addr, sizeof host_ip_addr) == -1) {
                if (errno == EADDRINUSE) {
#ifdef VERBOSE
                    fprintf(stderr, "%s Port %d already bound!\n"
                                  , time_stamp(), port_numbers[i]);
#endif
                    debug_message("%s Port %d already bound!\n"
                                 , time_stamp(), port_numbers[i]);
                    exit(errno);
                } else {
                    perror("bind");
                    exit(1);
                }
            }
        }
        else {

            /* Existing socket */

            sos[i] = -port_numbers[i];
            tmp = sizeof(host_ip_addr);
            if (!getsockname(sos[i], (struct sockaddr *)&host_ip_addr, &tmp))
                port_numbers[i] = ntohs(host_ip_addr.sin_port);
        }

        /* Initialise the socket */
        if (listen(sos[i], 5) == -1) {
            perror("listen");
            exit(1);
        }
#ifndef USE_PTHREADS
        set_socket_nonblocking(sos[i]);
#endif
        set_close_on_exec(sos[i]);

        if (socket_number(sos[i]) >= min_nfds)
            min_nfds = socket_number(sos[i])+1;
    } /* for(i = 0..numports) */

    /* We handle SIGPIPEs ourself */
#if defined(__linux__)
    signal(SIGPIPE, (RETSIGTYPE(*)(int))ignore_handler);
#else
    signal(SIGPIPE, SIG_IGN);
#endif

#if defined(SIGURG)
    signal(SIGURG, (RETSIGTYPE(*)(int))urgent_data_handler);
#endif
#if defined(SIGIO)
    signal(SIGIO, (RETSIGTYPE(*)(int))urgent_data_handler);
#endif
} /* prepare_ipc() */

/*-------------------------------------------------------------------------*/
void
ipc_remove (void)

/* Called when the driver is shutting down, this function closes all
 * open sockets.
 */

{
    int i;

    printf("%s Shutting down ipc...\n", time_stamp());
    for (i = 0; i < numports; i++)
        socket_close(sos[i]);

    if (udp_s >= 0)
        socket_close(udp_s);

#ifdef ERQ_DEMON
    shutdown_erq_demon();
#endif

} /* ipc_remove() */

/*-------------------------------------------------------------------------*/
void
interactive_lock (interactive_t *ip)

/* Lock the interactive <ip> for the current thread.
 */

{
#ifdef USE_PTHREADS
    pthread_mutex_lock(&ip->write_mutex);
#else
#  ifdef __MWERKS__
#      pragma unused(ip)
#  endif
#endif
} /* interactive_lock() */

/*-------------------------------------------------------------------------*/
void
interactive_unlock (interactive_t *ip)

/* Unlock the interactive <ip>.
 */

{
#ifdef USE_PTHREADS
    pthread_mutex_unlock(&ip->write_mutex);
#else
#  ifdef __MWERKS__
#      pragma unused(ip)
#  endif
#endif
} /* interactive_unlock() */

/*-------------------------------------------------------------------------*/
void
interactive_cleanup (interactive_t *ip)

/* Free all pending 'written' buffers for the interactive <ip>.
 * Locking must be handled by the caller.
 */

{
#ifdef USE_PTHREADS
    struct write_buffer_s *tmp;

    for (tmp = ip->written_first; tmp != NULL; tmp = ip->written_first)
    {
        ip->written_first = tmp->next;
        switch (tmp->errorno) {
          case 0:
            /* No error happened. */
            break;

          case EINTR:
            dprintf1(2, "%s comm: write EINTR. Message discarded.\n"
                      , (p_int)time_stamp());
            break;

          case EWOULDBLOCK:
            dprintf1(2, "%s comm: write EWOULDBLOCK. Message discarded.\n"
                      , (p_int)time_stamp());
            break;

          case EMSGSIZE:
            dprintf1(2, "%s comm: write EMSGSIZE.\n", (p_int)time_stamp());
            break;

          case EINVAL:
            dprintf1(2, "%s comm: write EINVAL.\n", (p_int)time_stamp());
            break;

          case ENETUNREACH:
            dprintf1(2, "%s comm: write ENETUNREACH.\n", (p_int)time_stamp());
            break;

          case EHOSTUNREACH:
            dprintf1(2, "%s comm: write EHOSTUNREACH.\n", (p_int)time_stamp());
            break;

          case EPIPE:
            dprintf1(2, "%s comm: write EPIPE detected\n", (p_int)time_stamp());
            break;

          default:
            {
              dprintf2(2, "%s comm: write: unexpected errno %d\n"
                        , (p_int)time_stamp(), (p_int)tmp->errorno);
              break;
            }
        } /* switch (ip->errorno) */

        free(tmp);
    } /* for (tmp) */
#else
#  ifdef __MWERKS__
#      pragma unused(ip)
#  endif
#endif
} /* interactive_cleanup() */

/*-------------------------------------------------------------------------*/
void
comm_cleanup_interactives (void)

/* Remove all pending 'written' buffers from all interactive structures.
 * This function handles the locking.
 */

{
#ifdef USE_PTHREADS
    size_t i;

    for (i = 0; i < sizeof(all_players)/sizeof(all_players[0]); i++)
    {
        interactive_t * ip = all_players[i];
        if (ip && ip->written_first != NULL)
        {
            interactive_lock(ip);
            interactive_cleanup(ip);
            interactive_unlock(ip);
        }
    }
#endif
} /* comm_cleanup_interactives() */

/*-------------------------------------------------------------------------*/
#ifdef USE_PTHREADS

static int
thread_socket_write( SOCKET_T s UNUSED, char *msg, size_t size
                   , interactive_t *ip, Bool docompress
#ifndef USE_MCCP
                                                        UNUSED
#endif /* USE_MCCP */
                   )

/* Stand in for socket_write(): take the data to be written and append
 * it to the buffer list of <ip>. <docompress> denotes the MCCP compression
 * setting.
 */

{
#ifdef __MWERKS__
#    pragma unused(s)
#    ifndef USE_MCCP
#        pragma unused(docompress)
#    endif /* USE_MCCP */
#endif

    struct write_buffer_s *b;

    if (size == 0)
        return 0;

    /* Get a new buffer for the data to be written */
    b = malloc(sizeof(struct write_buffer_s) + size - 1);
    if (!b)
        outofmem(sizeof(struct write_buffer_s) + size - 1, "thread_socket_write()");

    b->length = size;
    b->next = NULL;
#ifdef USE_MCCP
    if (ip->out_compress)   
        b->compress = docompress;
    else
#endif
        b->compress = 0;


    memcpy(b->buffer, msg, size);

    /* Chain in the new buffer */

    pthread_mutex_lock(&ip->write_mutex);

    if(ip->write_first)
        ip->write_last = ip->write_last->next = b;
    else
        ip->write_first = ip->write_last = b;
    ip->write_size += size;

    /* Make sure that the amount of data pending never exceeds
     * the maximum.
     */
    while (pthread_write_max_size != 0
        && ip->write_size >= (unsigned long)pthread_write_max_size)
    {
        struct write_buffer_s *tmp = ip->write_first;
        ip->write_first = tmp->next;
        ip->write_size -= tmp->length;
        free(tmp);
    }

    /* While we have the structure locked, remove pending
     * written buffers.
     */
    interactive_cleanup(ip);

    pthread_mutex_unlock(&ip->write_mutex);
    pthread_cond_signal(&ip->write_cond);

    errno = 0;
    return size;
} /* thread_socket_write() */

/*-------------------------------------------------------------------------*/
static void
thread_write_buf (interactive_t * ip, struct write_buffer_s *buf)

/* Write the buffer <buf> to interactive <ip>, handling MCCP and other
 * things if necessary.
 * This function is called from the main write thread as well as the
 * thread cleanup.
 */

{
#ifdef USE_MCCP
    int length;

    buf->errorno = 0;
    if (buf->compress)
    {
        int status;
        ip->out_compress->next_in = (unsigned char *) buf->buffer;
        ip->out_compress->avail_in = buf->length;
        ip->out_compress->avail_out = COMPRESS_BUF_SIZE -
          (ip->out_compress->next_out -
           ip->out_compress_buf);
        
        status = deflate(ip->out_compress, Z_SYNC_FLUSH);
        
        if (status != Z_OK)
            debug_message("%s MCCP compression error: %d\n"
                         , time_stamp(), status);
        length = ip->out_compress->next_out - ip->out_compress_buf;
    }
    if (buf->compress)
    {
#ifdef USE_TLS
        if ((ip->tls_status == TLS_INACTIVE
             ? socket_write(ip->socket, ip->out_compress_buf, length)
             : tls_write(ip, ip->out_compress_buf, length)) == -1)
#else
        if (socket_write(ip->socket, ip->out_compress_buf, length) == -1)
#endif
        { 
            buf->errorno = errno;
        } /* if socket_write() == -1 */
        
        /* we update the compressed buffer here */
        ip->out_compress->next_out = ip->out_compress_buf;
    }
    else
    {
#ifdef USE_TLS
        if ((ip->tls_status == TLS_INACTIVE
             ? socket_write(ip->socket, buf->buffer, buf->length)
             : tls_write(ip, buf->buffer, buf->length)) == -1)
#else
        if (socket_write(ip->socket, buf->buffer, buf->length) == -1)
#endif
        {
            buf->errorno = errno;
        } /* if socket_write() == -1 */
    }
    if (buf->compress && !ip->compressing)
    {
        /* Compression has been turned off for this interactive,
         * now get rid of all residual data.
         */
        end_compress(ip, MY_FALSE);
    }
#else /* USE_MCCP */
    buf->errorno = 0;
#ifdef USE_TLS
    if ((ip->tls_status == TLS_INACTIVE
         ? socket_write(ip->socket, buf->buffer, buf->length)
         : tls_write(ip, buf->buffer, buf->length)) == -1)
#else
    if (socket_write(ip->socket, buf->buffer, buf->length) == -1)
#endif
    {
        buf->errorno = errno;
    } /* if socket_write() == -1 */
#endif /* USE_MCCP */
} /* thread_write_buf() */

/*-------------------------------------------------------------------------*/
static void
writer_thread_cleanup(void *arg)

/* The given thread is canceled - move all pending buffers into the
 * written list.
 */

{
    interactive_t * ip = (interactive_t *) arg;
    struct write_buffer_s *buf;

    if (ip->write_current)
    {
        if (ip->flush_on_cleanup)
            thread_write_buf(ip, ip->write_current);
        else
            ip->write_current->errorno = 0;
        ip->write_current->next = ip->written_first;
        ip->written_first = ip->write_current;
        ip->write_current = NULL;
    }

    buf = ip->write_first;
    while (buf)
    {
        struct write_buffer_s *next = buf->next;
        if (ip->flush_on_cleanup)
            thread_write_buf(ip, buf);
        else
            buf->errorno = 0;
        buf->next = ip->written_first;
        ip->written_first = buf;
        buf = next;
    }
    ip->write_first = ip->write_last = NULL;

} /* writer_thread_cleanup() */

/*-------------------------------------------------------------------------*/
static void
writer_thread_locked_cleanup (void *arg)

/* The given thread is canceled - move all pending buffers into the
 * written list, protecting the operation with a lock.
 */

{
    interactive_t * ip = (interactive_t *) arg;
    pthread_mutex_lock(&ip->write_mutex);
    writer_thread_cleanup(arg);
    pthread_mutex_unlock(&ip->write_mutex);
} /* writer_thread_locked_cleanup() */

/*-------------------------------------------------------------------------*/
void *
writer_thread (void *arg)

/* The thread to write the pending data for the given interactive <arg>.
 * The buffer to be written is removed from the chain before the write
 * is attempted, so that a block here won't block add_message().
 */

{
    interactive_t *ip = (interactive_t *) arg;
    int oldvalue;

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldvalue); /* make us cancelable */
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &oldvalue);
    pthread_cleanup_push(writer_thread_locked_cleanup, ip);
      /* MacOS X: pthread_cleanup_push() is a macro which opens a new scope
       * and uses scope-local variable to store the cleanup handler.
       * To close the scope, pthread_cleanup_pop() needs to be 'invoked'
       * below.
       */

    while (MY_TRUE)
    {
        struct write_buffer_s * buf;

        /* cancellation point */
        pthread_testcancel();

        /* mutex protected getting of first write_buffer */
        pthread_mutex_lock(&ip->write_mutex);
        if  (!ip->write_first)
        {
            /* if no first write_buffer -> wait on signal from mainthread */
            pthread_cond_wait(&ip->write_cond, &ip->write_mutex);
        }

        /* another cancellation point */
        pthread_testcancel();

        if (ip->write_first)
        {
            /* We have to move the buffer out of the to-write list,
             * so that thread_socket_write() won't remove it if the
             * data limit is reached. On the other hand a GC might
             * happen while we're still printing, erasing the
             * written list.
             */
            buf = ip->write_first;
            ip->write_first = buf->next;
              /* If this was the last buffer, .write_first will become
               * NULL and the next call to thread_socket_write() will
               * set both .write_first and .write_last.
               */
            ip->write_size -= buf->length;
            ip->write_current = buf;
        }
        else
        {
            buf = NULL;
        }
        pthread_mutex_unlock(&ip->write_mutex);

        if (buf)
        {
            thread_write_buf(ip, buf);

            pthread_mutex_lock(&ip->write_mutex);

            ip->write_current = NULL;

            /* Originally we didn't free(buf) here because smalloc
             * wasn't threadsafe. We use malloc() now for the buffers,
             * so it would be safe now, but since the code to clean up
             * the buffers is already in the main thread, we keep it that
             * way. It makes for nicer diagnostic messages for failed
             * writes anyway.
             */
            buf->next = ip->written_first;
            ip->written_first = buf;

            pthread_mutex_unlock(&ip->write_mutex);
        }
    } /* while forever */

    /* Remove the thread cleanup handler */
    pthread_cleanup_pop(0);

    return NULL;
} /* writer_thread() */

#endif /* USE_PTHREADS */

/*-------------------------------------------------------------------------*/
void
add_message (const char *fmt, ...)

/* Send a message to the current command_giver. The message is composed
 * from the <fmt> string and the following arguments using the normal
 * printf() semantics.
 *
 * The format string "%s" is special in that it bypasses the normal
 * printf() handling and uses the given char* argument directly as data
 * source, allowing to send strings of arbitrary length.
 * The format string FMT_STRING accepts a string_t argument of arbitrary length.
 * The format string FMT_BINARY accepts a char* as text argument, followed
 * by a size_t with the string length. The text may contain any character.
 *
 * All other format strings compose the message to send in a local buffer
 * and are therefore subject to a length restriction.
 *
 * This function also does the telnet, snooping, and shadow handling.
 * If an interactive player is shadowed, object.c::shadow_catch_message()
 * is called to give the shadows the opportunity to intercept the message.
 *
 * All messages are accumulated in interactive.message_buf, which is
 * flushed when it is full. This flush can be forced by passing the
 * special 'string' message_flush (which is actually just a NULL pointer)
 * as <fmt> string to this function.
 *
 * Messages which can't be send (e.g. because the command_giver was
 * destructed or disconnected) are printed on stdout, preceeded by ']'.
 *
 * If an error other than EINTR occured while sending the data to
 * the network, the message is discarded and the socket is marked
 * for disconnection.
 *
 * Note that add_message() might be called recursively.
 */

{
    char  buff[MAX_TEXT + MAX_TEXT/2];
      /* Composition buffer for the final message.
       * We hope that it's big enough, but to be sure the code will
       * check for buffer overruns.
       * Message is composed starting from buff[1] on, buff[0] is
       * set to '%' for easier snooper-message generation.
       */
    int   length;
    int   min_length;
      /* When accumulating data in ip.message_buf[], this is the
       * threshold over which the buffer will be written to the
       * socket.
       * TODO: Actually, it is used just as a flag for flush/non-flush.
       */
    string_t *srcstr;
      /* If not NULL, this string was passed in to be printed.
       * source will point to the first real character of it.
       */
    int   old_message_length;  /* accumulated message length so far */
    char *source;              /* Pointer to the final message to add */
    size_t srclen;             /* Length of the message in source/srcstr */
    char *end;                 /* One char past the end of .message_buf[] */
    char *dest;                /* First free char in .message_buf[] */
    va_list va;
    interactive_t *ip;       /* The interactive user */
#ifdef USE_SNOOPING
    object_t      *snooper;  /* Snooper of <ip> */
#endif
    int   n;

    source = NULL;
    srcstr = NULL;
    srclen = 0;
    length = 0;

    va_start(va, fmt);

    /* Test if the command_giver is a real, living, undestructed user,
     * and not disconnected, closing or actually a new ERQ demon.
     * If the command_giver fails the test, the message is printed
     * to stdout and the function returns.
     */
    if ( command_giver == NULL
     || (   command_giver->flags & O_DESTRUCTED
         && fmt != message_flush )
     || !(O_SET_INTERACTIVE(ip, command_giver))
     || (ip->do_close && fmt != message_flush && !sending_telnet_command)
       )
    {
        putchar(']');
        if ( fmt == FMT_STRING )
        {
            /* Make sure to print embedded '\0' characters as well */

            size_t len;

            srcstr = va_arg(va, string_t *);
            source = get_txt(srcstr);
            srclen = mstrsize(srcstr);

            for ( len = 0; len < srclen; )
            {
                if (*source == '\0')
                {
                    putc('\0', stdout);
                    source++;
                    len++;
                }
                else
                {
                    size_t slen;

                    fputs(source, stdout);
                    slen = strlen(source);
                    source += slen;
                    len += slen;
                }
            }
        }
        else if ( fmt != message_flush )
        {
            vprintf(fmt, va);
        }
        fflush(stdout);
        va_end(va);
        return;
    }

    /* First, if a previous call had to discard the message, inform the user.
     */
    if (ip->msg_discarded)
    {
        ip->msg_discarded = MY_FALSE;
        add_message("%s", "\n*** Text lost in transmission ***\n");
        /* msg_discarded might be TRUE again now */
    }

    old_message_length = ip->message_length;

    /* --- Compose the final message --- */

    /* Create the final message and handle snoopers and shadows.
     */

    min_length = MAX_SOCKET_PACKET_SIZE-1;
      /* Allow some wiggle room for source characters like NL which
       * expand into two characters.
       */

    if ( fmt == message_flush )
    {
        /* Just flush, nothing to add */

        min_length = 1;
        source = "";
        srclen = 0;
        srcstr = NULL;
    }
    else /* add the message */
    {
#ifdef COMM_STAT
        add_message_calls++;
#endif

        /* Compose the final message in buff[] (while checking for overruns)
         * and point source to it.
         * Recognize the special formats '%s', FMT_STRING and FMT_BINARY
         * to bypass buff[] for messages of arbitrary length and content.
         */

        if (fmt == FMT_STRING)
        {
            srcstr = va_arg(va, string_t *);
            va_end(va);

            source = get_txt(srcstr);
            srclen = mstrsize(srcstr);
        }
        else if (fmt == FMT_BINARY)
        {
            source = va_arg(va, char *);
            srclen = va_arg(va, size_t);
            va_end(va);
            srcstr = NULL;
        }
        else if (fmt[0] == '%' && fmt[1] == 's' && !fmt[2])
        {
            source = va_arg(va, char *);
            va_end(va);
            srclen = strlen(source);
            srcstr = NULL;
        }
        else
        {
            size_t len;
            len = vsnprintf(buff+1, sizeof(buff)-1, fmt,va);
            va_end(va);
            /* old sprintf() implementations returned -1 if the output was
             * truncated. Since size_t is an unsigned type, the check for 
             * len == -1 is implicitly included by >= sizeof(...)-1, because
             * -1 will be wrapped to SIZE_T_MAX which is the maximum sizeof()
             * can return and can never be valid as return value here. */
            if (len >= sizeof(buff)-1)
            {
                char err[] = "\n*** Message truncated ***\n";
                debug_message("%s Message too long (Length: %zu): '%.200s...'\n"
                             , time_stamp(), len, buff);
                (void)strcpy(buff+(sizeof(buff)-sizeof(err)), err);
            }
            source = buff+1;
            srclen = strlen(buff+1);
            srcstr = NULL;
        }

        /* If we're not sending a telnet command with this message,
         * pass on the new data to any snooper and/or shadow
         */

#if defined(USE_SHADOWING) || defined(USE_SNOOPING)
        if (!sending_telnet_command)
        {

#ifdef USE_SHADOWING
            /* If there's a shadow successfully handling the
             * message, return.
             * This may cause a recursive call to add_message()!.
             */

            if (shadow_catch_message(command_giver, source))
            {
                return;
            }
#endif

#ifdef USE_SNOOPING
            /* If there's a snooper, send it the new message prepended
             * with a '%'.
             * For interactive snoopers this means a recursion with
             * the command_giver set to the snooper, for non-interactive
             * snoopers it's a simple call to tell_npc(), with an
             * adaption of the global trace_level to this users trace
             * settings.
             */

            if ( NULL != (snooper = ip->snoop_by)
             && !(snooper->flags & O_DESTRUCTED))
            {
                buff[0] = '%';
                if (O_IS_INTERACTIVE(snooper))
                {
                    object_t *save;

                    save = command_giver;
                    command_giver = snooper;
                    if (source != buff+1)
                    {
                        if (srcstr != NULL)
                        {
                            add_message("%s", "%");
                            add_message(FMT_STRING, srcstr);
                        }
                        else if (srclen >= sizeof buff - 1)
                        {
                            add_message("%s", "%");
                            add_message("%s", source);
                        }
                        else
                        {
                            strcpy(buff+1, source);
                            add_message("%s", buff);
                        }
                    }
                    else
                    {
                        add_message("%s", buff);
                    }
                    command_giver = save;
                }
                else
                {
                    trace_level |= ip->trace_level;
                    if (source != buff+1)
                    {
                        if (srcstr != NULL)
                        {
                            tell_npc(snooper, STR_PERCENT);
                            tell_npc(snooper, srcstr);
                        }
                        else if (srclen >= sizeof buff - 1)
                        {
                            tell_npc(snooper, STR_PERCENT);
                            tell_npc_str(snooper, source);
                        }
                        else
                        {
                            strcpy(buff+1, source);
                            tell_npc_str(snooper, buff);
                        }
                    } else
                    {
                        tell_npc_str(snooper, buff);
                    }
                }
            } /* if (snooper) */
#endif
        } /* if (!sending_telnet_command */
#endif /* defined(USE_SHADOWING) || defined(USE_SNOOPING) */
    } /* if (flush or not) */

#ifdef DEBUG
    if (d_flag > 1)
        debug_message("%s [%s (%zu)]: %s"
                     , time_stamp(), get_txt(command_giver->name)
                     , srclen, source);
#endif

    /* --- Send the final message --- */

    /* Append the final message to the .message_buf[], taking
     * care of all the necessary charset and telnet translations.
     */

    dest = &ip->message_buf[old_message_length];
    end  = &ip->message_buf[sizeof ip->message_buf];

    /* This loop advances source until it reaches the end.
     * Every character encountered is copied, translated or fed
     * into the telnet machine.
     */

#ifdef DEBUG_TELNET
if (sending_telnet_command)
{
    char *cp;
    long left;
    printf("%s TDEBUG: '%s' Sending telnet (%zu bytes): "
          , time_stamp(), get_txt(ip->ob->name), strlen(source));
    for (cp = source, left = srclen; left > 0; cp++, left--)
        printf(" %02x", (unsigned char)*cp);
    printf("\n");
}
#endif

    do /* while (srclen != 0) */
    {
        int    retries;   /* Number of retries left when sending data */
        ptrdiff_t chunk;  /* Current size of data in .message_buf[] */
        char   c;         /* Currently processed character */

        while (srclen != 0 && dest != end)
        {
            c = *source++;
            srclen--;

            /* Process the character:
             *  - copy it if the corresponding .charset bit is set,
             *    or if it's part of a telnet command.
             *  - translate a '\n' into '\r\n'
             *  - double an IAC if quote_iac is active.
             *  - stop this loop if the source is exhausted or
             *    if the buffer is full.
             */
            if (ip->charset[(c&0xff)>>3] & 1<<(c&7)
             || (c && sending_telnet_command)
               )
            {
                *dest++ = c;
            }
            else if (c == '\n')
            {
                if (dest + 1 == end)
                {
                    /* Not enough space in the buffer - revisit this char
                     * on the next time around */
                    source--;
                    srclen++;
                    break;
                }

                /* Insert CR before NL */
                *dest++ = '\r';
                *dest++ = c;
            }
            else if ( (unsigned char)c == IAC && ip->quote_iac)
            {
                if (dest + 1 == end)
                {
                    /* Not enough space in the buffer - revisit this char
                     * on the next time around */
                    source--;
                    srclen++;
                    break;
                }

                *dest++ = c;
                *dest++ = c;
            }

            /* Other characters are silently dropped */
        } /* while() */

        /* Check how much data there is in .message_buf[].
         * If it is enough, send it, else terminate the outer loop
         * (because *source must be exhausted for this to happen).
         */
        chunk = dest - ip->message_buf;
        if (chunk < min_length)
        {
            break;
        }

        /* Write .message_buf[] to the network. */

#if !defined(USE_PTHREADS) && defined(USE_MCCP)
        if (ip->out_compress)
        {
            ip->out_compress->next_in = (unsigned char *) ip->message_buf;
            ip->out_compress->avail_in = chunk;
            
            ip->out_compress->avail_out = COMPRESS_BUF_SIZE -
              (ip->out_compress->next_out -
               ip->out_compress_buf);
            
            {
              int status = deflate(ip->out_compress, Z_SYNC_FLUSH);
              
              if (status != Z_OK)
                return;
            }
            
            /* ok.. perhaps i should take care that all data in message_buf
             * is compressed, but i guess there is no chance that 1024 byte
             * compressed won't fit into the 8192 byte buffer
             */
            
            length = ip->out_compress->next_out - ip->out_compress_buf;
        }
#endif /* USE_MCCP && !USE_PTHREADS */

        /* now sending the buffer... */
        
        for (retries = 6;;)
        {
           
#if defined(USE_PTHREADS)

            if ((n = (int)thread_socket_write(ip->socket, ip->message_buf, (size_t)chunk, ip, MY_TRUE)) != -1)
            {
                break;
            }

#elif defined(USE_MCCP)
            if (ip->out_compress) /* here we choose the correct buffer */             
            {
#ifdef USE_TLS
                if ((n = (ip->tls_status == TLS_INACTIVE ?
                          (int)socket_write(ip->socket, ip->out_compress_buf, (size_t)length):
                          (int)tls_write(ip, (char *) ip->out_compress_buf, (size_t)length))) != -1)
#else
                if ((n = (int)socket_write(ip->socket, ip->out_compress_buf, (size_t)length)) != -1)
#endif
                {
                    break;
                }
            }
            else 
#endif
#if !defined(USE_PTHREADS)
            {
#ifdef USE_TLS
                if ((n = (ip->tls_status == TLS_INACTIVE ? 
                          (int)socket_write(ip->socket, ip->message_buf, (size_t)chunk):
                          (int)tls_write(ip, ip->message_buf, (size_t)chunk))) != -1)
#else
                if ((n = (int)socket_write(ip->socket, ip->message_buf, (size_t)chunk)) != -1)
#endif
                {
                    break;
                }
            }
#endif
            switch (errno) {
              case EINTR:
                if (--retries)
                    continue;
                ip->msg_discarded = MY_TRUE;
                fprintf(stderr,
                  "%s comm: write EINTR. Message discarded.\n", time_stamp());
                if (old_message_length)
                    remove_flush_entry(ip);
                return;

              case EWOULDBLOCK:
                ip->msg_discarded = MY_TRUE;
#ifdef USE_LDMUD_COMPATIBILITY
                if (d_flag)
#endif
                    fprintf(stderr,
                      "%s comm: write EWOULDBLOCK. Message discarded.\n", time_stamp());
                if (old_message_length)
                    remove_flush_entry(ip);
                return;

              case EMSGSIZE:
                fprintf(stderr, "%s comm: write EMSGSIZE.\n", time_stamp());
                return;

              case EINVAL:
                fprintf(stderr, "%s comm: write EINVAL.\n", time_stamp());
                break;

              case ENETUNREACH:
                fprintf(stderr, "%s comm: write ENETUNREACH.\n", time_stamp());
                break;

              case EHOSTUNREACH:
                fprintf(stderr, "%s comm: write EHOSTUNREACH.\n", time_stamp());
                break;

              case EPIPE:
                fprintf(stderr, "%s comm: write EPIPE detected\n", time_stamp());
                break;

              default:
                {
                  int e = errno;
                  perror("write");
                  fprintf(stderr, "%s comm: write: unknown errno %d\n"
                                , time_stamp(), e);
                }
            }
            if (old_message_length)
                remove_flush_entry(ip);
            ip->do_close = FLAG_DO_CLOSE;
            return;

        } /* for (retries) */

#ifdef COMM_STAT
        inet_packets++;
        inet_volume += n;
#endif

#if defined(USE_MCCP) && !defined(USE_PTHREADS)
        if (ip->out_compress)
        {
            /* we update the compressed buffer here */
            ip->out_compress->next_out = ip->out_compress_buf + length - n;
            if (n != length)
                fprintf(stderr, "%s write socket (compressed): wrote %ld, "
                                "should be %td.\n"
                        , time_stamp(), (long)n, chunk);
        }
        else
#endif
        if (n != chunk)
            fprintf(stderr, "%s write socket: wrote %ld, should be %td.\n"
                    , time_stamp(), (long)n, chunk);

        /* Continue with the processing of source */
        dest = &ip->message_buf[0];
    } while (srclen != 0);

    /* --- Final touches --- */

    ip->message_length = length = dest - ip->message_buf;

    /* Update the list of interactives with pending data */

    if ( length && !old_message_length )
    {
        /* Buffer became 'dirty': add this interactive to the list.
         */
        if ( NULL != (ip->next_player_for_flush = first_player_for_flush) )
        {
            O_GET_INTERACTIVE(first_player_for_flush)->
              previous_player_for_flush =
                command_giver;
        }
        ip->previous_player_for_flush = NULL;
        first_player_for_flush = command_giver;
    }
    if ( !length && old_message_length ) /* buffer has become empty */
    {
        remove_flush_entry(ip);
    }
} /* add_message() */

/*-------------------------------------------------------------------------*/
static INLINE void
reset_input_buffer (interactive_t *ip)

/* When returning from CHARMODE to LINEMODE, the input buffer variables
 * need to be reset. This function takes care of it.
 * Note that there might be additional lines pending between .tn_end and
 * .text_end.
 */

{
    if (ip->command_start)
    {
        DTN(("reset input buffer: cmd_start %hd, tn_start %hd, tn_end %hd, text_end %hd\n", 
             ip->command_start, ip->tn_start, ip->tn_end, ip->text_end));
        ip->tn_start -= ip->command_start;
        ip->tn_end -= ip->command_start;
        ip->text_end -= ip->command_start;
        if (ip->tn_start < 0)
            ip->tn_start = 0;
        if (ip->tn_end < 0)
            ip->tn_end = 0;
        if (ip->text_end <= 0)
            ip->text_end = 0;
        else
        {
            move_memory( ip->text, ip->text + ip->command_start
                       , ip->text_end
                       );
        }
        if (ip->command_end)
            ip->command_end = ip->tn_end;
        ip->command_start = 0;
    }
} /* reset_input_buffer() */

/*-------------------------------------------------------------------------*/
static void
remove_flush_entry (interactive_t *ip)

/* Remove the given interactive <ip> from the list of 'dirty' interactives
 * and make sure it is really clean. The function is safe to call for
 * interactives not in the list.
 *
 * This function is called after an interactive sent all pending data (or
 * failing while doing so).
 */

{
    ip->message_length = 0;

    /* To make it safe for calling the function even for interactives
     * not in the flush list, we check that <ip> is either in the middle
     * or at the end of the flush list (one or both of the .previous
     * and .next pointers is !NULL), or if .previous is NULL, that it is
     * the first entry in the list.
     */

    if ( ip->previous_player_for_flush )
    {
        O_GET_INTERACTIVE(ip->previous_player_for_flush)->next_player_for_flush
          = ip->next_player_for_flush;
    }
    else if (first_player_for_flush == ip->ob)
    {
        first_player_for_flush = ip->next_player_for_flush;
    }

    if ( ip->next_player_for_flush )
    {
        O_GET_INTERACTIVE(ip->next_player_for_flush)->previous_player_for_flush
          = ip->previous_player_for_flush;
    }

    ip->previous_player_for_flush = NULL;
    ip->next_player_for_flush = NULL;
} /* remove_flush_entry() */

/*-------------------------------------------------------------------------*/
void
flush_all_player_mess (void)

/* Flush all pending data from the interactives. Usually called before
 * every input loop, after a user logged in, or after an LPC runtime
 * error was processed.
 */

{
    object_t *p, *np;
    object_t *save = command_giver;

    for ( p = first_player_for_flush; p != NULL; p = np)
    {
        np = O_GET_INTERACTIVE(p)->next_player_for_flush;
          /* add_message() will clobber (p)->next_player_for_flush! */
        command_giver = p;
        add_message(message_flush);
    }
    command_giver = save;
} /* flush_all_player_mess() */

/*-------------------------------------------------------------------------*/
Bool
get_message (char *buff, size_t *len)

/* Get a message from a user, or wait until it is time for the next
 * heartbeat/callout. You can tell this apart by the result:
 *
 *   true: a user message was received and placed into buff; the user
 *         object is set as command_giver.
 *   false: it is just time to call the heart_beat.
 *
 * In both cases, time_to_call_heart_beat is set if a heartbeat is due.
 *
 * Internally, get_message() scans the array of interactive users in
 * search for one with a complete message in its incoming buffer. If
 * an earlier select() marked the socket for the current user as pending
 * with data, this data is read into the buffer before the check for
 * a message is performed. get_message() returns for the first user found
 * with a complete message. Since get_message() keeps its own
 * status of which user was looked at last, the next call to get_message()
 * will continue the scan where it left off.
 *
 * If no user has a complete message, a call to select() waits for more
 * incoming data. If this succeeds (and no heartbeat requires an
 * immediate return), the cycle begins again. If a heart_beat is due
 * even before select() executed, the waiting time for select() is
 * set to 0 so that only the status of the sockets is recorded and
 * get_message returns (almost) immediately.
 *
 * Normally, users can give only one command per cycle. The exception
 * is when they are editing, then they can give up to ALLOWED_ED_CMDS.
 *
 * Heartbeats are detected by checking the backend variable comm_time_-
 * to_call_heart_beat, which is set by the SIGALRM handler. If it is
 * true, get_message() sets the variable time_to_call_heart_beat to
 * inform the backend and returns.
 *
 * If a heart_beat occurs during the reading and returning of player
 * commands, the comm_time_c_h_b variable is set, but not evaluated.
 * This evaluation happens only when a select() is performed (therefore
 * the second variable time_to_c_h_b). This way every user can issure
 * at least one command in one backend cycle, even if that takes longer
 * than one heart_beat time. This makes it also legal for comm_to_c_h_b
 * to be set upon entering get_message().
 *
 * For short latency, the UDP socket is checked on every call to
 * get_message(), even if a previous select() did not mark it as ready
 * (this is disabled under BeOS and Windows).
 */

{
    /* State information: */
    static fd_set readfds, writefds;
      /* List of sockets with pending data.
       * You can ignore a 'could be used uninitialized' warning.
       */
    static int NextCmdGiver = -1;
      /* Index of current user to check */
#ifdef USE_SOCKET_LIMITS
    static int CmdsGiven = 0;
      /* Number of commands the current user gave in this cycle. */
#endif

#   define StartCmdGiver  (max_player)
#   define IncCmdGiver    NextCmdGiver--

    int    i;
    interactive_t * ip = NULL;
    fd_set exceptfds;


    /* The endless loop */

    while(MY_TRUE)
    {
        struct sockaddr_in addr;
        length_t length; /* length of <addr> */
        struct timeval timeout;

        /* --- select() on the sockets and handle ERQ --- */

        /* This also removes users which connection is marked
         * as to be closed.
         */

        if (NextCmdGiver < 0)
        {
            int nfds;     /* number of fds for select() */
            int res;      /* result from select() */
            int twait;    /* wait time in seconds for select() */
            int retries;  /* retries of select() after EINTR */

            flush_all_player_mess();
            twait = comm_time_to_call_heart_beat ? 0 : 1;
              /* If the heart_beat is due, just check the state
               * of the sockets, but don't wait.
               */

            /* Set up fd-sets. */

            FD_ZERO(&readfds);
            FD_ZERO(&writefds);
            for (i = 0; i < numports; i++) {
                FD_SET(sos[i], &readfds);
            } /* for */
            nfds = min_nfds;
            for (i = max_player + 1; --i >= 0;)
            {
                ip = all_players[i];
                if (!ip)
                    continue;

                if (ip->do_close)
                {
                    ip->do_close &= FLAG_PROTO_ERQ;
                    remove_interactive(ip->ob, MY_FALSE);
                    continue;
                }

                if (ip->tn_state == TS_READY)
                {
                    /* If telnet is ready for commands, react quickly. */
                    twait = 0;
                }
                else
                {
                    FD_SET(ip->socket, &readfds);
                    if (socket_number(ip->socket) >= nfds)
                        nfds = socket_number(ip->socket)+1;
                }
            } /* for (all players) */
#ifdef ERQ_DEMON
            if (erq_demon >= 0)
            {
                FD_SET(erq_demon, &readfds);
            }
#endif
            if (udp_s >= 0)
            {
                FD_SET(udp_s, &readfds);
            }

#ifdef USE_PGSQL
            pg_setfds(&readfds, &writefds, &nfds);
#endif

            /* select() until time is up or there is data */

            for (retries = 6;;)
            {
                check_alarm();
                timeout.tv_sec = twait;
                timeout.tv_usec = 0;
                res = socket_select(nfds, &readfds, &writefds, 0, &timeout);
                if (res == -1)
                {
                    /* BeOS <= PR2 returns errno -1 instead of EINTR :-( */
                    if (errno == EINTR || errno == -1)
                    {
                        /* We got an alarm, probably need heart_beat.
                         * But finish the select call since we already have
                         * prepared readfds.
                         */
                        if (comm_time_to_call_heart_beat)
                            twait = 0;
                        if (--retries >= 0)
                            continue;
                    }
                    else
                    {
                        perror("select");
                    }

                    /* Despite the failure, pretend select() suceeded with
                     * zero sockets to read, and process heart_beat / buffered
                     * commands.
                     */
                    FD_ZERO(&readfds);
                }
                break;
            } /* for (retries) */

            /* If we got a SIGIO/SIGURG, telnet wants to synch with us.
             */
            if (urgent_data)
            {
                DTN(("telnet wants to sync\n"));
                check_alarm();
                urgent_data = MY_FALSE;
                timeout.tv_sec = 0;
                timeout.tv_usec = 0;
                memset((char *)&exceptfds, 255, (size_t)(nfds + 7) >> 3);
                if (socket_select(nfds, 0, 0, &exceptfds, &timeout) > 0)
                {
                    for (i = max_player + 1; --i >= 0;)
                    {
                        ip = all_players[i];
                        if (!ip)
                            continue;
                        if (FD_ISSET(ip->socket, &exceptfds))
                        {
                            DTN(("ts_data = TS_SYNCH\n"));
                            ip->ts_data = TS_SYNCH;
                            switch (ip->tn_state)
                            {
                              case TS_DATA:
                              case TS_READY:
                                ip->tn_state = TS_SYNCH;
                                ip->gobble_char = '\0';
                                DTN(("tn_state = TS_SYNCH\n"));
                            }
                        }
                    } /* for (all players) */
                }
                /* Maybe the data didn't arrive yet, so try again later.
                 * But don't waste time doing it for too long.
                 */
                else if (current_time - urgent_data_time < 600)
                {
                    urgent_data = MY_TRUE;
                }
            } /* if (urgent_data) */

#ifdef USE_PGSQL
            pg_process_all();
#endif

            /* Initialise the user scan */
#ifdef USE_SOCKET_LIMITS
            CmdsGiven = 0;
#endif
            NextCmdGiver = StartCmdGiver;

#ifdef ERQ_DEMON

            /* --- Handle data from the ERQ ---
             * TODO: This should be a function on its own.
             * TODO: Define the erq messages as structs.
             */
            if (erq_demon >= 0 && FD_ISSET(erq_demon, &readfds))
            {
                mp_int l;
                mp_int msglen;  /* Length of the current erq message */
                mp_int rest;
                int32  handle;
                char  *rp;      /* Read pointer into buf_from_erq[] */

                FD_CLR(erq_demon, &readfds);

                /* Try six times to read data from the ERQ, appending
                 * it to what is already in buf_from_erq[].
                 */
                retries = 6;
                do {
                    l = socket_read(
                      erq_demon,
                      input_from_erq,
                      (size_t)(&buf_from_erq[sizeof buf_from_erq] - input_from_erq)
                    );
                } while(l < 0 && errno == EINTR && --retries >= 0);

                /* If there is no data, stop the erq, else handle it. */

                if (l <= 0)
                {
#ifdef DEBUG_ERQ
                    fprintf(stderr, "%s read %"PRIdMPINT" bytes from erq demon\n"
                                  , time_stamp(), l);
                    if (l)
                        perror("");
#endif /* DEBUG_ERQ */
                    stop_erq_demon(MY_TRUE);
                }
                else
                {
                    input_from_erq += l;
                    l = input_from_erq - &buf_from_erq[0];
                    rp = buf_from_erq;

                    /* l is the amount of data left to consider,
                     * rp points to the data to be considered next.
                     *
                     * Loop while there are messages left in the buffer.
                     */

                    for (; l >= 8 && l >= (msglen = read_32(rp))
                         ; rp += msglen, l -= msglen)
                    {
                        Bool keep_handle;

                        /* Is the message length valid?
                         * TODO: use sizeof(struct) here
                         */
                        if (msglen < 8) {
#ifdef DEBUG_ERQ
                            fprintf( stderr
                                   , "%s invalid length of message from"
                                     "erq demon: %"PRIdMPINT" bytes\n"
                                   , time_stamp(), msglen);
#endif /* DEBUG_ERQ */
                            stop_erq_demon(MY_TRUE);
                            break;
                        }

                        handle = read_32(rp+4); /* get the handle */

                        if (handle == ERQ_HANDLE_KEEP_HANDLE
                         && msglen >= 8)
                        {
                            /* _KEEP_HANDLE wrapper are used when
                             * more messages for the (real) handle
                             * are to be expected. The real message
                             * follows after the _KEEP_HANDLE.
                             */
                            handle = read_32(rp+8); /* the real handle */
                            keep_handle = MY_TRUE;
                            msglen -= 4;  /* adjust the message parameters */
                            l -= 4;
                            rp += 4;
                        }
                        else if (handle == ERQ_HANDLE_RLOOKUP)
                        {
                            /* The result of a hostname lookup. */

                            if (msglen < 13 || rp[msglen-1]) {
#ifdef DEBUG
                              if (msglen == 12) {
                                if (d_flag > 1)
                                  debug_message("%s Host lookup failed\n"
                                               , time_stamp());
                              } else {
                                debug_message("%s Bogus reverse name lookup.\n"
                                             , time_stamp());
                              }
#endif
                            } else {
                                uint32 naddr;
                                struct in_addr net_addr;

                                memcpy((char*)&naddr, rp+8, sizeof(naddr));
#ifndef USE_IPV6
                                net_addr.s_addr = naddr;
#else
                                CREATE_IPV6_MAPPED(&net_addr, naddr);
#endif
                                add_ip_entry(net_addr, rp+12);
                            }
                            continue;
                        }
#ifdef USE_IPV6
                        else if (handle == ERQ_HANDLE_RLOOKUPV6)
                        {
                            /* The result of a hostname lookup. */

                            if (msglen < 9 || rp[msglen-1]) {
#ifdef DEBUG
                                debug_message("%s Bogus reverse name lookup.\n"
                                             , time_stamp());
#else
                                NOOP;
#endif
                            } else {
                                char * space;

                                space = strchr(rp+8, ' ');

                                if (space == NULL)
                                {
                                    debug_message("%s IP6 Host lookup failed: %s\n"
                                                 , time_stamp(), rp+8);
                                }
                                else if (strlen(space+1))
                                {
                                    *space = '\0';
                                    update_ip_entry(rp+8, space+1);
                                }
                            }
                            continue;
                        }
#endif /* USE_IPV6 */
                        else
                        {
                            /* remove the callback handle after processing
                             * the message.
                             */
                            keep_handle = MY_FALSE;
                        }

                        /* We have an ERQ message for a user supplied
                         * handle - execute it (after some sanity checks).
                         */

                        rest = msglen - 8;
                        if ((uint32)handle < MAX_PENDING_ERQ
                         && pending_erq[handle].fun.type != T_INVALID
                         && (   rest <= (mp_int)max_array_size
                             || !max_array_size
                             || pending_erq[handle].string_arg
                            )
                           )
                        {
                            svalue_t *erqp = &pending_erq[handle].fun;
                            object_t *ob;
                            wiz_list_t *user;
                            int num_arg;

                            command_giver = 0;
                            current_interactive = 0;
                            ob = !CLOSURE_MALLOCED(erqp->x.closure_type)
                                 ? erqp->u.ob
                                 : erqp->u.lambda->ob;
                            if (pending_erq[handle].string_arg)
                            {
                                string_t * str;

                                str = new_n_mstring(rp + 8, rest);
                                push_string(inter_sp, str);

                                num_arg = 1;
                            }
                            else
                            {
                                unsigned char *cp;
                                vector_t *v;
                                svalue_t *svp;

                                current_object = ob;
                                v = allocate_array(rest);
                                current_object = NULL;
                                push_array(inter_sp, v);
                                push_number(inter_sp, rest);
                                cp = (unsigned char *)rp + 8;
                                for (svp = v->item; --rest >=0; svp++)
                                {
                                    svp->u.number = *cp++;
                                }

                                num_arg = 2;
                            }

                            user = ob->user;
                            if (user->last_call_out != current_time)
                            {
                                user->last_call_out = current_time;
                                CLEAR_EVAL_COST;
                            } else {
                                assigned_eval_cost = eval_cost = user->call_out_cost;
                            }
                            RESET_LIMITS;
                            secure_callback_lambda(erqp, num_arg);
                            user->call_out_cost = eval_cost;
                            if (!keep_handle || (ob->flags & O_DESTRUCTED))
                            {
                                free_svalue(erqp);
                                erqp->type = T_INVALID;
                                erqp->u.generic = (void *)free_erq;
                                free_erq = &pending_erq[handle];
                            }
                        } /* if(valid handle) */

                        /* Messages for invalid handles are no error: e.g. the
                         * object could have gone away unexpectantly before
                         * the erq had time to answer.
                         */

                    } /* for (l,rp in buf_from_erq) */

                    /* Delete the processed data from the buffer */
                    if (rp != buf_from_erq)
                    {
                        move_memory(buf_from_erq, rp, (size_t)l);
                        input_from_erq = &buf_from_erq[l];
                    }
                } /* if (read data from erq) */
            } /* if (erq socket ready) */

#endif /* ERQ_DEMON */

            /* --- Try to get a new player --- */
            for (i = 0; i < numports; i++)
            {
                if (FD_ISSET(sos[i], &readfds))
                {
                    SOCKET_T new_socket;

                    length = sizeof addr;
                    new_socket = accept(sos[i], (struct sockaddr *)&addr
                                              , &length);
                    if ((int)new_socket != -1)
                        new_player( NULL, new_socket, &addr, (size_t)length
                                  , port_numbers[i]);
                    else if ((int)new_socket == -1
                      && errno != EWOULDBLOCK && errno != EINTR
                      && errno != EAGAIN && errno != EPROTO )
                    {
                        /* EBADF would be a valid cause for an abort,
                         * same goes for ENOTSOCK, EOPNOTSUPP, EFAULT.
                         * However, don't abort() because that tends to
                         * leave Mud admins baffled (and would opens the
                         * door for DoS attacks).
                         */
                        int errorno = errno;
#ifdef VERBOSE
                        fprintf( stderr
                               , "%s comm: Can't accept on socket %d "
                                 "(port %d): %s\n"
                               , time_stamp(), sos[i], port_numbers[i]
                               , strerror(errorno)
                               );
#endif
                        debug_message("%s comm: Can't accept on socket %d "
                                      "(port %d): %s\n"
                                     , time_stamp(), sos[i], port_numbers[i]
                                     , strerror(errorno)
                                     );
                        /* TODO: Was: perror(); abort(); */
                    }
                }
            } /* for */
            /* check for alarm signal (heart beat) */
            if (comm_time_to_call_heart_beat)
            {
                time_to_call_heart_beat = MY_TRUE;
                return MY_FALSE;
            }
        } /* if (no NextCmdGiver) */

        /* See if we got any udp messages.
         * We don't test readfds so that we can accept udp messages with
         * short latency. But for the same reason, it was necessary to
         * include the descriptor number in the set to be selected on.
         * Note for BeOS and Cygwin: since making sockets non-blocking
         *   is a bit tricky, we check if the socket is actually ready,
         *   to prevent freezing.
         * TODO: Always use the readfds information.
         */
#if !defined(__BEOS__) && !defined(CYGWIN)
        if (udp_s >= 0)
#else
        if (udp_s >= 0 && FD_ISSET(udp_s, &readfds))
#endif
        {
            char udp_buf[1024+1], *st;
            int cnt;

            length = sizeof addr;
            cnt = recvfrom(udp_s, udp_buf, sizeof(udp_buf)-1, 0
                          , (struct sockaddr *)&addr, &length);
            if (cnt != -1)
            {
                string_t *udp_data;

                udp_data = new_n_mstring(udp_buf, cnt);
                if (!udp_data)
                {
                    debug_message("%s Out of memory (%d bytes) for UDP message.\n"
                                 , time_stamp(), cnt);
                }
                else
                {
                    command_giver = NULL;
                    current_interactive = NULL;
                    current_object = NULL;
                    trace_level = 0;
#ifndef USE_IPV6
                    st = inet_ntoa(addr.sin_addr);
#else
                    st = inet6_ntoa(addr.sin_addr);
#endif
                    push_c_string(inter_sp, st);
                    push_string(inter_sp, udp_data); /* adopts the ref */
                    push_number(inter_sp, ntohs(addr.sin_port));
                    RESET_LIMITS;
                    callback_master(STR_RECEIVE_UDP, 3);
                    CLEAR_EVAL_COST;
                }
            }
        } /* if (upd_s) */

        /* --- The Scan for User Commands --- */

        for (; NextCmdGiver >= 0; IncCmdGiver)
        {
#ifdef USE_SNOOPING
            object_t *snooper;
#endif

            ip = all_players[NextCmdGiver];

            if (ip == 0)
                continue;

#ifdef USE_TLS
            /* Special case for setting up a TLS connection: don't
             * attempt IO if the connection is still being set up.
             * fippo suggests we should check here if the socket
             * is readable, otherwise there is nothing to handshake.
             * Currently we peek each second, we can optimize that.
             */
            if (ip->tls_status == TLS_HANDSHAKING)
            {
/*		printf("tls: socket readable %d, writable %d\n", FD_ISSET(ip->socket, &readfds), 
								 FD_ISSET(ip->socket, &writefds)); */
                tls_continue_handshake(ip);
                continue;
            }
#endif

#ifdef USE_SOCKET_LIMITS
            /* Skip players which have reached the ip->maxNumCmds limit
             * for this second. We let the data accumulate on the socket.
             */
            if (ip->last_time == current_time
             && ip->maxNumCmds >= 0
             && ip->numCmds >= ip->maxNumCmds)
                continue;
#endif

            /* Get the data (if any), at max enough to fill .text[] */

            if (FD_ISSET(ip->socket, &readfds)) {
                int l;

                l = MAX_TEXT - ip->text_end;

                /* In CHARMODE with combine-charset, the driver gets
                 * confused when receiving MAX_TEXT or more combinable
                 * characters.
                 * I couldn't quite figure out what and why, but
                 * reading less than MAX_TEXT characters works around
                 * the issue.
                 */
#ifndef SIMULATE_CHARMODE
                if ((ip->noecho & (CHARMODE_REQ|CHARMODE)) == (CHARMODE_REQ|CHARMODE))
#else
                if (ip->noecho & (CHARMODE_REQ|CHARMODE))
#endif
                {
                    l -= 2;
                }

                DTN(("text_end %hd, can read %d chars\n", ip->text_end, l));

#ifdef USE_TLS
                if (ip->tls_status != TLS_INACTIVE)
                    l = tls_read(ip, ip->text + ip->text_end, (size_t)l);
                else
#endif
                    l = socket_read(ip->socket, ip->text + ip->text_end, (size_t)l);
                DTN(("# chars read: %d\n", l));
                /* TODO: Check if the block below can be simplified by using
                 * TODO::strerror().*/
                if (l == -1) {
                    if (errno == ENETUNREACH) {
                        debug_message("%s Net unreachable detected.\n"
                                     , time_stamp());
                        remove_interactive(ip->ob, MY_FALSE);
                        continue;
                    }
                    if (errno == EHOSTUNREACH) {
                        debug_message("%s Host unreachable detected.\n"
                                     , time_stamp());
                        remove_interactive(ip->ob, MY_FALSE);
                        continue;
                    }
                    if (errno == ETIMEDOUT) {
                        debug_message("%s Connection timed out detected.\n"
                                     , time_stamp());
                        remove_interactive(ip->ob, MY_FALSE);
                        continue;
                    }
                    if (errno == ECONNRESET) {
                        debug_message("%s Connection reset by peer detected.\n"
                                     , time_stamp());
                        remove_interactive(ip->ob, MY_FALSE);
                        continue;
                    }
                    if (errno == ECONNREFUSED) {
                        debug_message("%s Connection refused detected.\n"
                                     , time_stamp());
                        remove_interactive(ip->ob, MY_FALSE);
                        continue;
                    }
                    if (errno == EWOULDBLOCK) {
                        debug_message("%s read would block socket %d!\n"
                                     , time_stamp(), ip->socket);
                        remove_interactive(ip->ob, MY_FALSE);
                        continue;
                    }
                    if (errno == EMSGSIZE) {
                        debug_message("%s read EMSGSIZE\n", time_stamp());
                        continue;
                    }
                    if (errno == ESHUTDOWN) {
                        debug_message("%s Connection to socket %d lost.\n"
                                     , time_stamp(), ip->socket);
                        remove_interactive(ip->ob, MY_FALSE);
                        continue;
                    }
                    if (errno == EBADF) {
                        if (ip->ob)
                            debug_message("%s Socket %d (ip %p '%s') is a bad descriptor.\n"
                                         , time_stamp(), ip->socket, ip
                                         , get_txt(ip->ob->name));
                        else
                            debug_message("%s Socket %d (ip %p) is a bad descriptor.\n"
                                         , time_stamp(), ip->socket, ip);
                        remove_interactive(ip->ob, MY_FALSE);
                        continue;
                    }
                    perror("read");
                    debug_message("%s Unexpected errno %d\n"
                                 , time_stamp(), errno);
                    remove_interactive(ip->ob, MY_FALSE);
                    continue;
                }
                if (l == 0) {
                    if (ip->closing)
                        comm_fatal(ip, "Tried to read from closing socket.\n");
                        /* This will forcefully disconnect the user */
                    else
                        remove_interactive(ip->ob, MY_FALSE);
                    continue;
                }
#ifdef COMM_STAT
                inet_packets_in++;
                inet_volume_in += l;
#endif

#ifdef HAS_PSYC
		// If we're receiving the first bytes call a peek function which
		// could enable_binary depending on the content of the buffer
		// We only need this until we give up the old PSYC syntax.
		if (!ip->connected) {
		    put_c_n_string(++inter_sp, ip->text, l);
		    // we shouldn't look up the string in the string table
		    // for each connection.. but it's just temporary code
		    sapply(new_tabled("connection_peek"), ip->ob, 1);
		    ip->connected = MY_TRUE;
		}
#endif
		/* Experimental support for
		 * binary data streams, by fippo 2008
		 */
		if (ip->is_binary) {
		     /* webdav attack caused something near memcpy to crash..
		      * buff isn't larger than ip->text but
		      * maybe l is larger than buff?
		      */
		    if (l > MAX_TEXT) {
			debug_message("%s Incoming socket overflow. Dropped %d bytes.\n"
			    , time_stamp(), l);
		    } else {
			memcpy(buff, ip->text, l);
			*len = (size_t) l;
		    }
                    FD_CLR(ip->socket, &readfds);
		    command_giver = ip->ob;
		    return MY_TRUE;
		}
                ip->text_end += l;

                /* Here would be the place to send data through an
                 * outportal instead of returning it.
                 */

                telnet_neg(ip);
            } /* if (cmdgiver socket ready) */

            /* if ip->text[0] does not hold a valid character, the outcome
             * of the comparison to input_escape does not matter.
             */

            /* ----- CHARMODE -----
             * command_start is 0 at the beginning. Received chars start at
             * text[0].  After the first character is processed, command_start
             * will be 1.  Chars are in text[1] then. Only after a
             * full_newline is command_start reset to 0. This is important for
             * bang-escape, the first char in a 'line' is stored in text[0],
             * subsequent chars are in text[1].
             *
             * chars_ready is the number of chars in the text buffer. If the
             * user is slow this will be 1. If the user pastes data it could
             * be more.  The chars are processed then one at a time (or if
             * combine-charset is used that many until a non-combinable char
             * is reached).
             *
             * The processed char(s) are copied to buff and handled in the
             * backend.
             *
             * If telnet_neg() returned state READY, we want to process the
             * string end marker (which represents the \r\n) also and have to
             * add 1 to strlen() for the chars_ready.
             *
             * The remark above 'if (destix > 0 && !buff[destix-1])' is not
             * quite true (anymore). Because we process the string terminating
             * \0 as a char, we will have a destix > 0 always - even if we got
             * a new line.  Mind, that buff[destix-1] is always buff[0] in
             * that 'if', because newlines are never combinable and we always
             * start with a new buffer for it!
             *
#ifndef SIMULATE_CHARMODE
             * TODO: I dont think that it is neccesary to disable charmode if
             * TODO:: the client refuses to use it. The disadvantage of the
             * TODO:: present behaviour is a confused lpc object (which could
             * TODO:: not know if it gets linemode-lines). The charmode code
             * TODO:: does work with clients in linemode.
#endif
             */

#ifndef SIMULATE_CHARMODE
            if ((ip->noecho & (CHARMODE_REQ|CHARMODE)) == (CHARMODE_REQ|CHARMODE))
#else
            if (ip->noecho & (CHARMODE_REQ|CHARMODE))
#endif
            {
                DTN(("CHARMODE_REQ\n"));
                if (ip->text[0] != input_escape
                 || find_no_bang(ip) & IGNORE_BANG )
                {
                    /* Unescaped input.
                     * Puts the next character(s) (addressed by
                     * .command_start) into buff[0] and return the data.
                     */

                    int destix;  /* Save index */
                    Bool end_of_line = MY_FALSE;

                    DTN(("  Unescaped input\n"));

                    if (ip->tn_state != TS_READY)
                    {
                        /* .text[] contains an incomplete negotiation.
                         * Set .chars_ready the amount of pure data available
                         * and temporarily suspend the telnet machine.
                         */
                        length = (TN_START_VALID(ip->tn_state)
                                   ? ip->tn_start
                                   : ip->command_end
                                 ) - ip->command_start;
                        DTN(("    incomplete negotiation: length %ld\n"
                           , (long)length));
                        if (!length)
                            continue;
                        if (length < 0)
                        {
                            comm_fatal(ip, "comm: data length < 0: %ld\n", (long)length);
                            continue;
                        }
                        DTN(("    save machine state %hhd, set to %d (READY)\n"
                          , ip->tn_state, TS_READY));
                        ip->save_tn_state = ip->tn_state;
                        ip->chars_ready = length;
                        ip->tn_state = TS_READY;
                    }
                    else if (!ip->chars_ready)
                    {
                        /* Empty input: we received an end of line.
                         * The telnet machine is already suspended, but
                         * we have to set the state for it to return to.
                         * At the moment it is TS_INVALID, so the next
                         * character received would be thrown away.
                         */
                        DTN(("    Empty input: save machine state %d (DATA)\n"
                          , TS_DATA));
                        length = strlen(ip->text + ip->command_start) + 1;
                        ip->chars_ready = length;
                        ip->save_tn_state = TS_DATA;
                        end_of_line = MY_TRUE;
                        /* tn_state is TS_READY */
                    }

                    /* Copy as many characters from the text[] into
                     * the buff[] as possible.
                     */
                    DTN(("  %"PRId32" chars ready\n", ip->chars_ready));
                    if (end_of_line)
                    {
                        DTN(("    faking NL\n"));
                        buff[0] = '\n';
                        destix = 1;
                    }
                    else for (destix = 0; destix < ip->chars_ready; )
                    {
                        char ch;

                        ch = ip->text[ip->command_start++];
                        buff[destix++] = ch;
                        if (!(ip->combine_cset[(ch&0xff) / 8] & (1 << (ch % 8)))
                         || !ch
                           )
                        {
                            /* This character can't be combined (or it is the
                             * end of the line).
                             * If it is not the first character encountered,
                             * undo the previous store; in either case break
                             * the loop.
                             */
                            if (destix != 1)
                            {
                                destix--;
                                ip->command_start--;
                            }
                            break;
                        }
                    }

                    /* destix is now the number of characters stored in
                     * buff[], and is at least 1.
                     */

                    if (!buff[destix-1])
                    {
                        /* End of line. Reinitialise the telnet machine
                         */
                        DTN(("    end of line: reinit telnet machine\n"));
                        destix--;
                        ip->command_start = 0;
                        ip->tn_state = TS_DATA;
                        telnet_neg(ip);
                    }

                    buff[destix] = '\0';

                    if (!end_of_line)
                        ip->chars_ready -= destix;
                    DTN(("  %"PRId32" chars left ready\n", ip->chars_ready));
                    if (!ip->chars_ready)
                    {
                        /* All the pure data was read, now restore the
                         * old telnet machine state.
                         * Leave the first char in to make input escape
                         * possible
                         */
                        DTN(("    restore old telnet machine state %hhd\n"
                            , ip->save_tn_state));
                        ip->tn_state = ip->save_tn_state;
                        ip->save_tn_state = TS_INVALID;
                        ip->tn_start -= ip->command_start - 1;
                        ip->command_end -= ip->command_start - 1;

                        if (ip->command_start && ip->command_end > 0)
                        {
                            move_memory( ip->text, ip->text+ip->command_start
                                       , ip->command_end
                                       );
                        }

                        ip->command_start = 1;

                        /* When receiving a pure data line in charmode, starting
                         * with the second char, these two values may become
                         * negative. We have to correct them then to point
                         * to ip->command_start.
                         */
                        DTN(("    tn_start %hd, command_end %hd\n", ip->tn_start, ip->command_end));
                        if (ip->tn_start < 1)
                            ip->tn_start = 1;
                        if (ip->command_end < 1)
                            ip->command_end = 1;

                        ip->text_end = ip->tn_end = ip->command_end;
                    }

                    command_giver = ip->ob;
                    trace_level = ip->trace_level;
                    IncCmdGiver;
#ifdef USE_SOCKET_LIMITS
                    CmdsGiven = 0;

                    if (ip->last_time != current_time)
                    {
                        ip->last_time = current_time;
                        ip->numCmds = 0;
                    }
                    else
                        ip->numCmds++;
#endif

                    DTN(("--- return with char command %02x '%c' length %d ---\n", buff[0], buff[0], destix));

                    return MY_TRUE;
                }
                else if (ip->tn_state != TS_READY)
                {
                    DT(("'%s'   Escaped input\n", get_txt(ip->ob->name)));
                    length = (TN_START_VALID(ip->tn_state)
                              ? ip->tn_start
                              : ip->command_end
                             ) - ip->command_start;
                    DTN(("  data length %ld\n", (long)length));
                    if (length < 0)
                    {
                        comm_fatal(ip, "comm: data length < 0: %ld\n", (long)length);
                        continue;
                    }
                    if (length > ip->chars_ready)
                    {
#ifdef USE_PTHREADS
                        thread_socket_write(ip->socket, ip->text + ip->chars_ready
                                    , (size_t)(length - ip->chars_ready), ip, MY_FALSE);
#else
#ifdef USE_TLS
                        if (ip->tls_status != TLS_INACTIVE)
                            tls_write(ip, ip->text + ip->chars_ready
                                        , (size_t)(length - ip->chars_ready));
                        else
#endif /* USE_TLS */
                          socket_write(ip->socket, ip->text + ip->chars_ready
                                      , (size_t)(length - ip->chars_ready));
#endif
                        ip->chars_ready = length;
                    }
                }
            } /* if (CHARMODE_REQ) */

            /* The telnet negotiation produces the commands starting at
             * the beginning of .text[] and terminated with a '\0'. Whenever
             * a command is complete, the tn_state is TS_READY.
             */
            DTN(("tn complete, telnet machine state: %hhd\n", ip->tn_state));
            if (ip->tn_state == TS_READY)
            {
                /* We have a command: copy it into buff, handle a
                 * possible snooper and return.
                 */

                DTN(("telnet machine ready\n"));
                /* buffer overflows here are impossible even with strcpy(),
                 * because buff is allocated in backend() as MAX_TEXT+4 and
                 * ip->text is allocated as MAX_TEXT+2. Ok, as long as nobody
                 * changes buff in backend() withour changing ip->text ... */
                strcpy(buff, ip->text);
                command_giver = ip->ob;
                trace_level = ip->trace_level;
                ip->chars_ready = 0; /* for escaped charmode */

                /* Reinitialize the telnet machine, possibly already
                 * producing the next command in .text[].
                 */
                ip->tn_state = TS_DATA;
                telnet_neg(ip);

#ifdef USE_SOCKET_LIMITS
                /* If the user is not in ed, don't let him issue another command
                 * before the poll comes again.
                 */
                if (O_GET_SHADOW(ip->ob)->ed_buffer
                 && CmdsGiven < ALLOWED_ED_CMDS)
                {
                    CmdsGiven++;
                    FD_CLR(ip->socket, &readfds);
                }
                else
                {
                    IncCmdGiver;
                    CmdsGiven = 0;
                }
#else
		FD_CLR(ip->socket, &readfds);
#endif

#ifdef USE_SNOOPING
                /* Manage snooping - should the snooper see type ahead?
                 * Well, he doesn't here.
                 */
                if (NULL != (snooper = ip->snoop_by)
                 && !(snooper->flags & O_DESTRUCTED)
                 && !(ip->noecho & NOECHO_REQ)
                   )
                {
                    if (O_IS_INTERACTIVE(snooper))
                    {
                        command_giver = snooper;
                        add_message("%% %s\n", buff);
                    }
                    else
                    {
                        /* buff is limited to MAX_TEXT+4. Additionally,
                         * get_message() is usually not called recursively or
                         * from a very high stack depth, so alloca() is 
                         * probably ok. */
                        char *snoop_message = alloca(strlen(buff) + 4);
                        sprintf(snoop_message, "%% %s\n", buff);
                        tell_npc_str(snooper, snoop_message);
                    }
                    command_giver = ip->ob;
                }
#endif

#ifdef USE_SOCKET_LIMITS
                if (ip->last_time != current_time)
                {
                    ip->last_time = current_time;
                    ip->numCmds = 0;
                }
                else
                    ip->numCmds++;
#endif

#ifndef SIMULATE_CHARMODE
                if ((ip->noecho & (CHARMODE_REQ|CHARMODE)) == CHARMODE_REQ)
                {
                    DTN(("   clear CHARMODE as it was refused anyway\n"));
                    ip->noecho &= ~(CHARMODE_REQ|CHARMODE|CHARMODE_ACK);
                    reset_input_buffer(ip);
                }
#endif /* SIMULATE_CHARMODE */

                DTN(("--- return with line command ---\n"));
                DTN(("--- '%s'\n", buff));
                return MY_TRUE;
            } /* if (have a command) */

        } /* for (NextCmdGiver) */

        /* If we got here, we couldn't find any commands:
         * loop and select (on timeout) again.
         */

    } /* while(forever) */

    /* NOTREACHED */
#   undef StartCmdGiver
#   undef IncCmdGiver

} /* get_message() */

/*-------------------------------------------------------------------------*/
void
remove_interactive (object_t *ob, Bool force)

/* Remove the interactive user <ob> immediately.
 * If <force> is true, the user is removed under all circumstances and
 * without even flushing the outgoing buffer.
 * This function should not be called from within a LPC command execution.
 */

{
    object_t *save = command_giver;
    int i;
    interactive_t *interactive;
    object_t * curobj_save = current_object;
    int save_privilege;

    interactive = O_GET_INTERACTIVE(ob);

    /* Proper call? */
    for (i = 0; i < MAX_PLAYERS && all_players[i] != interactive; i++) NOOP;
    if (i >= MAX_PLAYERS)
    {
        fatal("Could not find and remove player %s\n", get_txt(ob->name));
        abort();
    }
    if (interactive->closing && !force)
        fatal("Double call to remove_interactive()\n");

    interactive->closing = MY_TRUE;
    current_object = ob;
    save_privilege = malloc_privilege;

    /* If the object is not destructed, inform the master */

    if ( !(ob->flags & O_DESTRUCTED) )
    {
        int numRemaining = interactive->text_end - interactive->command_start;

        command_giver = NULL;
        current_interactive = NULL;
        push_ref_object(inter_sp, ob, "remove_interactive");

        if (numRemaining > 0)
        {
            string_t * remaining = NULL;
            memsafe( remaining = new_n_mstring(interactive->text+interactive->command_start, numRemaining)
                   , numRemaining, "buffer for remaining data from socket");
            push_string(inter_sp, remaining);
        }
        else
            push_ref_string(inter_sp, STR_EMPTY);

        malloc_privilege = MALLOC_MASTER;
        callback_master(STR_DISCONNECT, 2);
        /* master might have used exec() */
        ob = interactive->ob;
    }

    interactive->catch_tell_activ = MY_FALSE;

#ifdef USE_SNOOPING
    /* Untie eventual snooping relations */

    if (interactive->snoop_by)
    {
        if (O_IS_INTERACTIVE(interactive->snoop_by))
        {
            O_GET_INTERACTIVE(interactive->snoop_by)->snoop_on = NULL;
        }
        else
        {
            free_object(interactive->snoop_by, "remove_interactive");
        }
        interactive->snoop_by = NULL;
    }
    if (interactive->snoop_on)
    {
        interactive->snoop_on->snoop_by = NULL;
        interactive->snoop_on = NULL;
    }
#endif

    command_giver = ob;

#ifdef ERQ_DEMON
    /* If this object is disconnected because it was used to connect
     * a new ERQ, put the connection into place and greet the ERQ.
     */
    if (interactive->do_close & FLAG_PROTO_ERQ
     && interactive->socket == erq_proto_demon
     && !force)
    {
        static unsigned char erq_welcome[] = { IAC, TELOPT_BINARY };

        add_message(message_flush);
        erq_demon = interactive->socket;
        erq_proto_demon = -1;
        socket_write(erq_demon, erq_welcome, sizeof erq_welcome);
    }
    else
#endif
    {
        if (!force)
        {
            /* Say goodbye to the user. */
            trace_level |= interactive->trace_level;
            add_message(message_flush);
        }

        remove_flush_entry(interactive); /* To be sure */

#ifdef USE_PTHREADS
        /* Cancel the thread, then in case it is waiting on the
         * condition, signal the condition as well. This way when
         * the thread reaches the cancellation point after the
         * condition, it will stop.
         */
        if (!force)
        {
            set_socket_nonblocking(interactive->socket);
            interactive->flush_on_cleanup = MY_TRUE;
        }
        pthread_cancel(interactive->write_thread);
        pthread_cond_signal(&interactive->write_cond);
        pthread_join(interactive->write_thread, NULL);
          /* buffer list is returned by thread */
        interactive_cleanup(interactive);
#endif
#ifdef USE_MCCP
        if (interactive->out_compress)
            end_compress(interactive, MY_TRUE);
            /* Always force the compression end as we won't get
             * a second chance at it.
             */
#endif
#ifdef USE_TLS
        tls_deinit_connection(interactive);
#endif
        shutdown(interactive->socket, 2);
        socket_close(interactive->socket);
    } /* if (erq or user) */

#if defined(ACCESS_CONTROL)
    release_host_access(interactive->access_class);
      /* One user less in this class */
#endif

    num_player--;

    /* Release all associated resources */

    while (interactive->input_to)
    {
        input_to_t * it = interactive->input_to;

        interactive->input_to = it->next;
        free_input_to(it);
    }

    if (interactive->modify_command)
    {
        free_object(interactive->modify_command, "remove_interactive");
    }

#ifdef USE_MCCP
    if (interactive->out_compress_buf)
       xfree(interactive->out_compress_buf);
    if (interactive->out_compress)
       xfree(interactive->out_compress);
#endif
#ifdef USE_PTHREADS
    pthread_mutex_destroy(&interactive->write_mutex);
    pthread_cond_destroy(&interactive->write_cond);
#endif
    free_svalue(&interactive->prompt);

    if (interactive->trace_prefix)
        free_mstring(interactive->trace_prefix);

    /* Unlink the interactive structure from the shadow sentence
     * of the object.
     */
    O_GET_INTERACTIVE(ob) = NULL;
    check_shadow_sent(ob);

    xfree(interactive);
    if (i < MAX_PLAYERS)
        all_players[i] = NULL;
    while (max_player && !all_players[max_player])
        max_player--;
    free_object(ob, "remove_interactive");

    command_giver = check_object(save);
    current_object = curobj_save;
    malloc_privilege = save_privilege;
} /* remove_interactive() */

#ifdef ACCESS_CONTROL

/*-------------------------------------------------------------------------*/
void
refresh_access_data(void (*add_entry)(struct sockaddr_in *, int, long*) )

/* Called from access_check after the ACCESS_FILE has been (re)read, this
 * function has to call the passed callback function add_entry for every
 * user currently logged in.
 */

{
    interactive_t **user, *this;
    int n;

    user = all_players;
    for (n = max_player + 2; --n; user++)
    {
        this = *user;
        if (this)
        {
            struct sockaddr_in addr;
            int port;
            length_t length;

            length = sizeof(addr);
            getsockname(this->socket, (struct sockaddr *)&addr, &length);
            port = ntohs(addr.sin_port);
            (*add_entry)(&this->addr, port, &this->access_class);
        }
    }
}

#endif /* ACCESS_CONTROL */

/*-------------------------------------------------------------------------*/
static INLINE void
set_default_conn_charset (char charset[32])

/* Set the default connection charset bitmask in <charset>.
 */

{
    memset(charset, 255, 32);
    charset['\n'/8] &= ~(1 << '\n' % 8);
    charset['\0'/8] &= ~(1 << '\0' % 8);
} /* set_default_conn_charset() */

/*-------------------------------------------------------------------------*/
static INLINE void
set_default_combine_charset (char charset[32])

/* Set the default combine charset bitmask in <charset>.
 */

{
    memset(charset, 0, 32);
    charset['\n'/8] &= ~(1 << '\n' % 8);
    charset['\0'/8] &= ~(1 << '\0' % 8);
} /* set_default_combine_charset() */

/*-------------------------------------------------------------------------*/
static void
new_player ( object_t *ob, SOCKET_T new_socket
           , struct sockaddr_in *addr, size_t addrlen
#if !defined(ACCESS_CONTROL) && !defined(USE_AUTHLOCAL)
           , int login_port UNUSED
#else
           , int login_port
#endif
           )

/* Accept (or reject) a new connection on <new_socket> from <addr> (length
 * of structure is <addrlen>), accepted on port <login_port>.
 *
 * Called when get_message() detects a new connection on one of the
 * login ports, this function checks if the user may access the mud.
 *
 * If yes and <ob> is NULL, a new interactive structure is generated and
 * bound to the master, then master->connect() is called. This call is
 * expected to return an object and the interactive structure is rebound to
 * that object.
 * If yes and <ob> is an object, a new interactive structure is generated
 * and bound to <ob>
 *
 * Finally, logon() is called in the newly-interactive object.
 * Alternatively if no <ob> is given, master->connect() may exec() the
 * connection away from the master, in which case no further action will be
 * taken after the return from that call.
 *
 * If the connection can't be accepted for some reason, a failure
 * message will be send back to the user and the socket will be
 * closed.
 */

{
#if defined(__MWERKS__) && !defined(ACCESS_CONTROL) && !defined(USE_AUTHLOCAL)
#    pragma unused(login_port)
#endif

    int   i;             /* Index of free slot in all_players[] */
    char *message;       /* Failure message */
    svalue_t *ret;       /* LPC call results */
    interactive_t *new_interactive;
                         /* The new interactive structure */
#ifdef ACCESS_CONTROL
    long class;     /* Access class */
#endif

    /* Set some useful socket options */
#ifndef USE_PTHREADS
    set_socket_nonblocking(new_socket);
#endif
    set_close_on_exec(new_socket);
    set_socket_own(new_socket);

#ifdef ACCESS_CONTROL
    /* Check for access restrictions for this connection */
    message = allow_host_access(addr, login_port, &class);
#ifdef ACCESS_LOG
    {
        FILE *log_file = fopen (ACCESS_LOG, "a");

        if (log_file) {
            FCOUNT_WRITE(log_file);
            fprintf(log_file, "%s %s: %s\n"
                   , time_stamp()
#ifndef USE_IPV6
                   , inet_ntoa(addr->sin_addr)
#else
                   , inet6_ntoa(addr->sin_addr)
#endif
                   , message ? "denied" : "granted");
            fclose(log_file);
        }
    }
#endif
    if (message)
    {
        socket_write(new_socket, message, strlen(message));
        socket_write(new_socket, "\r\n", 2);
        socket_close(new_socket);
        return;
    }
#endif /* ACCESS_CONTROL */

#ifdef DEBUG
    if (d_flag)
        debug_message("%s New player at socket %d.\n"
                     , time_stamp(), new_socket);
#endif

    /* Look for an empty slot in all_players[] */
    for (i = 0; i < MAX_PLAYERS && all_players[i] != NULL; i++) NOOP;
    if (i >= MAX_PLAYERS)
    {
        /* calling closures here would need special error handling */
        if (driver_hook[H_NO_IPC_SLOT].type == T_STRING)
        {
            string_t *msg;

            msg = driver_hook[H_NO_IPC_SLOT].u.str;
            socket_write(new_socket, get_txt(msg), mstrsize(msg));
        }
        else
        {
	    /* always define an H_NO_IPC_SLOT !! */
            message = "We are full. Come back later.\r\n";
            socket_write(new_socket, message, strlen(message));
        }
        socket_close(new_socket);
        debug_message("%s Out of IPC slots for new connection.\n"
                     , time_stamp());
        return;
    }

    /* The master must be loaded and free to accept a login */
    assert_master_ob_loaded();
    if (O_IS_INTERACTIVE(master_ob))
    {
        message = "Cannot accept connections. Come back later.\r\n";
        socket_write(new_socket, message, strlen(message));
        socket_close(new_socket);
        debug_message("%s Master still busy with previous new connection.\n"
                     , time_stamp());
        return;
    }

    command_giver = master_ob;
    trace_level = 0;
    new_interactive = xalloc(sizeof (interactive_t));
    if (!new_interactive)
    {
        message = "Cannot accept connection (out of memory). Come back later.\r\n";
        socket_write(new_socket, message, strlen(message));
        socket_close(new_socket);
        debug_message("%s Out of memory (%zu bytes) for new connection.\n"
                     , time_stamp(), sizeof(interactive_t));
        return;
    }

    if (ob && O_IS_INTERACTIVE(ob))
    {
        /* The caller provided an object to connect to. But since
         * it is already interactive, we have to terminate that
         * old connection.
         */
        remove_interactive(ob, MY_FALSE);
    }

    /* Link the interactive to the master */

    assert_shadow_sent(master_ob);
    O_GET_INTERACTIVE(master_ob) = new_interactive;
    master_ob->flags |= O_ONCE_INTERACTIVE;
    new_interactive->ob = ref_object(master_ob, "new_player");
    new_interactive->outgoing_conn = (ob != NULL);

    /* Initialize the rest of the interactive structure */

#ifdef USE_MCCP
    new_interactive->compressing = 0;
    new_interactive->out_compress = NULL;
    new_interactive->out_compress_buf=NULL;
#endif
#ifdef USE_TLS
    new_interactive->tls_status = TLS_INACTIVE;
    new_interactive->tls_session = NULL;
    new_interactive->tls_cb = NULL;
# ifdef HAS_PSYC
	 /* give TLS 4 seconds to start. in fact even 1 second should
	  * be enough as the TLS init packet is sent immediately with
	  * the TCP 3-way handshake completion.
	  */
    new_interactive->tls_autodetect = ob == NULL? 4 : 0;
# endif
    new_interactive->tls_want_peer_cert = MY_FALSE;
#endif
    new_interactive->input_to = NULL;
    put_number(&new_interactive->prompt, 0);
    new_interactive->modify_command = NULL;
    new_interactive->msg_discarded = MY_FALSE;
    new_interactive->set_input_to = MY_FALSE;
    new_interactive->closing = MY_FALSE;
    new_interactive->tn_enabled = MY_TRUE;
    new_interactive->is_binary = MY_FALSE;
    new_interactive->connected = MY_FALSE;
    new_interactive->do_close = 0;
    new_interactive->noecho = 0;
    new_interactive->gobble_char = 0;
    new_interactive->catch_tell_activ = MY_TRUE;
    new_interactive->text_end = 0;
    new_interactive->command_start = 0;
    new_interactive->command_end = 0;
    new_interactive->chars_ready = 0;
    new_interactive->save_tn_state = TS_INVALID;
    new_interactive->tn_start = 0;
    new_interactive->tn_end = 0;
    new_interactive->tn_state = TS_DATA;
    new_interactive->ts_data = TS_DATA;
#ifdef USE_SNOOPING
    new_interactive->snoop_on = NULL;
    new_interactive->snoop_by = NULL;
#endif
    new_interactive->last_time = current_time;
#ifdef USE_SOCKET_LIMITS
    new_interactive->numCmds = 0;
    new_interactive->maxNumCmds = -1;
#endif
    new_interactive->trace_level = 0;
    new_interactive->trace_prefix = NULL;
    new_interactive->message_length = 0;
    set_default_conn_charset(new_interactive->charset);
    set_default_combine_charset(new_interactive->combine_cset);
    new_interactive->text[0] = '\0';
    memcpy(&new_interactive->addr, addr, addrlen);
#if defined(ACCESS_CONTROL)
    new_interactive->access_class = class;
#endif
    new_interactive->socket = new_socket;
    new_interactive->next_player_for_flush = NULL;
    new_interactive->previous_player_for_flush = NULL;

#ifdef USE_PTHREADS
    new_interactive->flush_on_cleanup = MY_FALSE;
    pthread_mutex_init(&new_interactive->write_mutex, NULL);
    {
        pthread_mutexattr_t mutexattr;
        pthread_mutexattr_init(&mutexattr);
        pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_ERRORCHECK);
        pthread_mutex_init(&new_interactive->write_mutex, &mutexattr);
        pthread_mutexattr_destroy(&mutexattr);
    }
    pthread_cond_init(&new_interactive->write_cond, NULL);
    new_interactive->write_first = new_interactive->write_last = NULL;
    new_interactive->write_size = 0;
    new_interactive->write_current = NULL;
    new_interactive->written_first = NULL;
    pthread_create(&new_interactive->write_thread, NULL, writer_thread, new_interactive);
      /* We pthread_join() on this thread when the connection is closed */
#endif

    /* Add the new interactive structure to the list of users */

    all_players[i] = new_interactive;
    if (i > max_player)
        max_player = i;
    num_player++;

    current_interactive = master_ob;

    if (!ob)
    {
#ifdef USE_AUTHLOCAL
	uid_t uid;

	uid = getUID(addr->sin_addr.s_addr, ntohs(addr->sin_port), login_port);
# ifdef VERBOSE
	if (uid) debug_message("%s authlocal from port %d to %d is uid %d.\n",
	     time_stamp(), addr->sin_port, login_port, uid);
# endif
	push_number(inter_sp, uid);
#else
	push_number(inter_sp, 0);
#endif

        /* The caller did not provide an object to connect to.
         * Call master->connect() and evaluate the result.
         */
        ret = callback_master(STR_CONNECT, 1);
        if (new_interactive != O_GET_INTERACTIVE(master_ob))
            return;
        if (ret == NULL
         || ret->type != T_OBJECT
         || (ob = ret->u.ob, O_IS_INTERACTIVE(ob)))
        {
            remove_interactive(master_ob, MY_FALSE);
            return;
        }
        command_giver = master_ob;
        add_message(message_flush);
    }

    /* ob is now a non-interactive object, either passed in from the caller
     * or returned from connect().
     * Relink the interactive from the master to this as the user object.
     */

    O_GET_INTERACTIVE(master_ob) = NULL;
    master_ob->flags &= ~O_ONCE_INTERACTIVE;
    check_shadow_sent(master_ob);
    free_object(master_ob, "new_player");

    assert_shadow_sent(ob);
    O_GET_INTERACTIVE(ob) = new_interactive;
    new_interactive->ob = ref_object(ob, "new_player");
    ob->flags |= O_ONCE_INTERACTIVE;

    /* Prepare to call logon() in the new user object.
     */
    command_giver = ob;
    current_interactive = ob;
#ifdef USE_SNOOPING
    if (new_interactive->snoop_on)
    {
        new_interactive->snoop_on->snoop_by = ob;
    }
#endif
#ifdef ERQ_DEMON
    (void) lookup_ip_entry(new_interactive->addr.sin_addr, MY_TRUE);
    /* TODO: We could pass the retrieved hostname right to login */
#endif
#ifdef USE_TLS
    /* If we're using secure connections and the connect() triggered
     * a handshake which is still going on, we call logon() as
     * the default TLS callback. This way, logon() is callled only
     * if the connection could be established, secure or not.
     */
    if (new_interactive->tls_status != TLS_HANDSHAKING)
    {
        /* Connection not secure, or already established: logon. */
        logon_object(ob);
    }
    else if (new_interactive->tls_cb == NULL)
    {
        callback_t * cb;

        /* Connection in TLS handshake, but not callback: set a callback
         * to the logon function.
         */
        if (find_function(STR_LOGON, current_interactive->prog) < 0)
        {
            errorf("Could not find %s() on the player %s\n", get_txt(STR_LOGON), get_txt(current_interactive->name));
            /* NOTREACHED */
        }

        xallocate(cb, sizeof(*cb), "logon tls-callback structure");
        setup_function_callback(cb, current_interactive, STR_LOGON, 0, NULL, MY_TRUE);
        new_interactive->tls_cb = cb;

    }
    /* else: Connection in TLS handshake and callback set by connect(). */
#else
    logon_object(ob);
#endif /* USE_TLS */
    if (!(ob->flags & O_DESTRUCTED))
        print_prompt();
    flush_all_player_mess();
} /* new_player() */

/*-------------------------------------------------------------------------*/
void
set_noecho (interactive_t *ip, char noecho, Bool local_change, Bool external)

/* Change the input mode <i>->noecho to the given <noecho>, performing all
 * necessary telnet negotiations. If the driverhook H_NOECHO is set,
 * the hook function is expected to do all the negotiations.
 * If <local_change> is TRUE, the driver will not send out any telnet
 * commands for CHARMODE/LINEMODE changes.
 */

{
    char old, confirm;
    object_t *ob;

    if (!ip->tn_enabled)
    {
        DTN(("set_noecho(%02hhx) old %02hhx: TN disabled\n"
            , noecho, ip->noecho));
    }

    old = ip->noecho;

    confirm = (char)(
      noecho | CHARMODE_REQ_TO_CHARMODE(noecho & (NOECHO_REQ|CHARMODE_REQ)));
    DTN(("set_noecho(%02hhx%s) old %02hhx %s\n"
       , noecho, local_change ? " local" : "", old, decode_noecho(old)));
    DTN(("  -> confirm: %02hhx %s\n"
       , confirm, decode_noecho(confirm)));
    DTN(("           -> %02hhx %s\n"
       , confirm | NOECHO_ACKSHIFT(confirm)
       , decode_noecho(confirm | NOECHO_ACKSHIFT(confirm))
       ));

    ip->noecho = confirm;

    confirm |= NOECHO_ACKSHIFT(confirm);
    if (((confirm ^ old) & (NOECHO_MASK|CHARMODE_MASK)) && ip->tn_enabled )
    {
        DTN(("set_noecho(): Mode changes\n"));
        ob = ip->ob;
        if (!(ob->flags & O_DESTRUCTED))
        {
            if (driver_hook[H_NOECHO].type == T_STRING
             || driver_hook[H_NOECHO].type == T_CLOSURE
               )
            {
                DTN(("set_noecho():   calling H_NOECHO\n"));
                push_number(inter_sp, noecho);
                push_ref_valid_object(inter_sp, ob, "set_no_echo");
                push_number(inter_sp,  local_change ? 1 : 0);
                if (driver_hook[H_NOECHO].type == T_STRING)
                    secure_apply_ob(driver_hook[H_NOECHO].u.str, ob, 3, external);
                else 
                {
                    if (driver_hook[H_NOECHO].x.closure_type == CLOSURE_LAMBDA)
                    {
                        free_object(driver_hook[H_NOECHO].u.lambda->ob
                                   , "set_noecho");
                        driver_hook[H_NOECHO].u.lambda->ob
                          = ref_object(ob, "set_noecho");
                    }
                    secure_call_lambda(&driver_hook[H_NOECHO], 3, external);
                }
                if (~confirm & old & CHARMODE_MASK)
                {
                    if (ip->save_tn_state != TS_INVALID)
                    {
                        DT(("'%s' set_noecho():     0 chars ready, "
                            "saved state %hhd\n", get_txt(ip->ob->name)
                                              , ip->save_tn_state));
                        ip->chars_ready = 0;
                        ip->tn_state = ip->save_tn_state;
                    }
                    reset_input_buffer(ip);
                }
            }
            else
            {
                object_t *save;

                save = command_giver;
                command_giver = ob;
#ifdef SAVE_NOECHO
                ip->noecho &= ~NOECHO_DELAYED;
#endif
                if (~confirm & old & NOECHO)
                {
                    DTN(("set_noecho():   WONT TELOPT_ECHO\n"));
                    send_wont(TELOPT_ECHO);
                }
                else if (confirm & ~old & NOECHO_MASK)
                {
#ifdef SAVE_NOECHO
                    if (confirm & ~old & CHARMODE_MASK)
                    {
                        ip->noecho |= NOECHO_DELAYED;
                        ip->noecho &= ~(NOECHO | NOECHO_REQ);
                        DTN(("set_noecho():   delaying WILL TELOPT_ECHO\n"));
                    }
                    else
                    {
#endif
                        DTN(("set_noecho():   WILL TELOPT_ECHO\n"));
                        send_will(TELOPT_ECHO);
#ifdef SAVE_NOECHO
                    }
#endif
                }
                else /* No change in NOECHO mode */ if (confirm & NOECHO)
                {
                    /* Since we stay in NOECHO mode, we need the ACK flag set. */
                    DTN(("set_noecho():   Staying in NOECHO mode\n"));
                    ip->noecho |= NOECHO_ACKSHIFT(NOECHO);
                }

                if (ip->supress_go_ahead && !(confirm & (NOECHO|CHARMODE)))
                {
                    DTN(("set_noecho():   WONT TELOPT_SGA\n"));
                    ip->supress_go_ahead = MY_FALSE;
                    send_wont(TELOPT_SGA);
                }
                /* Only using SGA for charmode is supported hardcoded.
                 * To make more sophisticated negotiations, e.g. using LINEMODE,
                 * use the H_NOECHO hook.
                 */
                if ((~confirm & old & CHARMODE_MASK)
                ||  ((~confirm & old & NOECHO_STALE) && (old & CHARMODE_MASK))
                   )
                {
                    if(~confirm & old & CHARMODE_MASK)
                    {
                        DTN(("set_noecho():   turn off charmode\n"));
                        if ((old & CHARMODE) && !local_change)
                        {
                            DTN(("set_noecho():     DONT TELOPT_SGA\n"));
                            send_dont(TELOPT_SGA);
                        }
                        if (ip->save_tn_state != TS_INVALID)
                        {
                            DTN(("set_noecho():     0 chars ready, saved state %hhd\n", ip->save_tn_state));
                            ip->chars_ready = 0;
                            ip->tn_state = ip->save_tn_state;
                        }
                    }

                    reset_input_buffer(ip);
                }
                else if (confirm & ~old & CHARMODE_MASK)
                {
                    DTN(("set_noecho():   turn on charmode\n"));
                    if (!local_change)
                    {
                        DTN(("set_noecho():     DO+WILL TELOPT_SGA\n"));
                        send_do(TELOPT_SGA);
                        /* some telnet implementations (Windows' telnet is one) mix
                         * up DO and WILL SGA, thus we send WILL SGA as well.
                         */
                        send_will(TELOPT_SGA);
                        ip->supress_go_ahead = MY_TRUE;
                    }
                    else
                        /* Since there won't be any telnet negotiation
                         * we can assume that CHARMODE is acknowledged.
                         */
                        ip->noecho |= NOECHO_ACKSHIFT(CHARMODE);

                }
                else /* No change in CHARMODE mode */ if (confirm & CHARMODE)
                {
                    /* Since we stay in CHARMODE mode, we need the ACK flag set. */
                    DTN(("set_noecho():   Staying in CHARMODE mode\n"));
                    ip->noecho |= NOECHO_ACKSHIFT(CHARMODE);
                }

                command_giver = save;
            }
        } /* if (!(ob->flags & O_DESTRUCTED)) */
    }
    else
    {
        /* No change in modes.
         * However, if we stay in NOECHO/CHARMODE, we need to set
         * the ACK flags.
         */
        if (confirm & CHARMODE)
        {
            /* Since we stay in CHARMODE mode, we need the ACK flag set. */
            DTN(("set_noecho():   Staying in CHARMODE mode\n"));
            ip->noecho |= NOECHO_ACKSHIFT(CHARMODE);
        }
        if (confirm & NOECHO)
        {
            /* Since we stay in NOECHO mode, we need the ACK flag set. */
            DTN(("set_noecho():   Staying in NOECHO mode\n"));
            ip->noecho |= NOECHO_ACKSHIFT(NOECHO);
        }
    }

} /* set_noecho() */

/*-------------------------------------------------------------------------*/
int
find_no_bang (interactive_t *ip)

/* Find the most recent input_to in *<ip> which specified "IGNORE_BANG" and
 * return its full "noecho" flags. This may be the ip->noecho itself!
 * If there is none, return 0.
 */

{
    input_to_t *it;

    if (ip->noecho & IGNORE_BANG)
        return ip->noecho;

    for (it = ip->input_to; it; it = it->next)
        if (it->noecho & IGNORE_BANG)
            return it->noecho;
    return 0;
} /* find_no_bang() */

/*-------------------------------------------------------------------------*/
Bool
call_function_interactive (interactive_t *i, char *str, size_t len)

/* Perform a pending input_to() for this user <i> and the input <str>
 * Return TRUE if an input_to() was pending and executed, and FALSE
 * if the input was not processed.
 *
 * This function is called by the backend as part of the input processing.
 */

{
    static input_to_t current_it;
      /* Current input_to, static so that longjmp() won't clobber it. */

    struct error_recovery_info error_recovery_info;

    input_to_t *it;
    object_t   *ob;   /* object holding <function> */

    it = i->input_to;

    /* _Are_ there an input_to() pending? */
    if (!it)
        return MY_FALSE;

    /* Yes, there are. Check if we have to handle input escape. */
    if (*str == input_escape && str[1])
    {
        input_to_t * prev;

        for (prev = NULL
            ; it && !(it->noecho & IGNORE_BANG)
            ; prev = it, it = it->next)
            NOOP;

        if (it)
        {
            /* Move this 'IGNORE_BANG' input_to to the top of list
             * since it's the one we're going to execute.
             */
            if (prev)
            {
                prev->next = it->next;
                it->next = i->input_to;
                i->input_to = it;
            }

            if (!(i->noecho & NOECHO) != !(it->noecho & NOECHO_REQ)) {
                /* !message for ECHO-context  while in NOECHO - simulate the
                 * echo by sending the (remaining) raw data we got.
                 */
                add_message("%s\n", str + i->chars_ready);
                i->chars_ready = 0;
            }

            /* Don't hide the leading input escape */
        }
        else
        {
            /* Bang-input but no matching input_to(): return */
            return MY_FALSE;
        }
    }

    /* We got the right input_to_t. Check if it's still valid. */
    ob = callback_object(&(it->fun));
    if (!ob)
    {
        /* Sorry, the object has selfdestructed ! */
        set_noecho(i, it->next ? it->next->noecho : 0
                    , it->next ? it->next->local : MY_FALSE
                    , MY_TRUE);
        i->input_to = it->next;
        free_input_to(it);
        return MY_FALSE;
    }

#ifdef USE_SWAP
    if (O_PROG_SWAPPED(ob)
     && load_ob_from_swap(ob) < 0)
    {
        set_noecho(i, it->next ? it->next->noecho : 0
                    , it->next ? it->next->local : MY_FALSE
                    , MY_TRUE);
        i->input_to = it->next;
        free_input_to(it);
        errorf("Out of memory: unswap object '%s'.\n", get_txt(ob->name));
        return MY_FALSE;
    }
#endif

    /* if there is a series of noecho/charmode input, we should only
     * negotiate when we know that the state actually should change.
     * In other words: should the input_to function request NOECHO
     * again, the NOECHO_STALE bit will be cleared and we will not
     * turn NOECHO off after the call.
     */
    if (i->noecho)
    {
        i->noecho |= NOECHO_STALE;
    }

    /* Clear the input_to() reference in case the function called
     * sets up a new one.
     */
    current_it = *it;
    i->input_to = it->next;
    xfree(it);
    free_svalue(&current_it.prompt); /* Don't need this anymore */

    /* Activate the local error recovery context */

    error_recovery_info.rt.last = rt_context;
    error_recovery_info.rt.type = ERROR_RECOVERY_BACKEND;
    rt_context = (rt_context_t *)&error_recovery_info.rt;

    if (setjmp(error_recovery_info.con.text))
    {
        /* An error occured: free the remaining data,
         * restore the error stack and return
         */

        clear_state();
        debug_message("%s Error in input_to().\n", time_stamp());
        free_callback(&(current_it.fun));
        rt_context = error_recovery_info.rt.last;
        return MY_TRUE;
    }

    /* Call the input_to() function with the newly input string */

#ifdef HAS_PSYC	// enable_binary i suppose
    if (len == 0) {
        push_c_string(inter_sp, str);
    } else {
	push_c_n_string(inter_sp, str, len);
    }
#else
    push_c_string(inter_sp, str);
#endif
    (void)backend_callback(&(current_it.fun), 1);

    rt_context = error_recovery_info.rt.last;

    /* If NOECHO is no longer needed, turn it off. */

    if (i->noecho & NOECHO_STALE)
    {
        set_noecho(i, i->input_to ? i->input_to->noecho : 0
                    , i->input_to ? i->input_to->local : MY_FALSE
                    , MY_TRUE);
    }

    /* Done */
    return MY_TRUE;
} /* call_function_interactive() */

/*-------------------------------------------------------------------------*/
static Bool
set_call ( object_t *ob, input_to_t *it, char noecho
         , Bool local_change, Bool append)

/* Set a a new input_to <it> with the flags <noecho> (mainly really NOECHO,
 * but also IGNORE_BANG or not) to the interactive object <ob>.
 * If <local_change> is TRUE, the driver will not send out any telnet
 * commands for CHARMODE/LINEMODE changes.
 * If <append> is TRUE, the call is appended to the list of existing
 * input_to's, if any.
 * Return TRUE on success.
 *
 * Called for efun input_to().
 */

{
    interactive_t *ip;

    if (ob == NULL || it == NULL)
        return MY_FALSE;
    if (!(O_SET_INTERACTIVE(ip, ob))
     || ip->closing || (!append && ip->set_input_to))
    {
        return MY_FALSE;
    }

    it->noecho = noecho;
    it->local = local_change;

    if (!append || ip->input_to == NULL)
    {
        it->next = ip->input_to;
        ip->input_to = it;
    }
    else
    {
        input_to_t * ptr = ip->input_to;

        while (ptr->next != NULL)
            ptr = ptr->next;

        ptr->next = it;
        it->next = NULL;
    }

    ip->set_input_to = MY_TRUE;

    if (noecho || ip->noecho)
        set_noecho(ip, noecho, local_change, MY_FALSE);
    return MY_TRUE;
} /* set_call() */

/*-------------------------------------------------------------------------*/
void
remove_all_players (void)

/* Destruct all user objects. This is first tried by calling master->remove()
 * for every object. If this doesn't destruct the user object,
 * destruct() is used.
 * The function is called when the game is shut down.
 */

{
    int i;

    for (i = 0; i < MAX_PLAYERS; i++) {
        if (all_players[i] == 0 || (all_players[i]->ob->flags & O_DESTRUCTED))
            continue;
        command_giver = all_players[i]->ob;
        trace_level |= all_players[i]->trace_level;
        RESET_LIMITS;
        CLEAR_EVAL_COST;
        push_ref_object(inter_sp, all_players[i]->ob, "remove_all_players");
        (void)callback_master(STR_REMOVE_PL, 1);
        if ( !(all_players[i]->ob->flags & O_DESTRUCTED) ) {
            destruct(all_players[i]->ob);
        }
    }
}

/*-------------------------------------------------------------------------*/
static void
print_prompt_string (string_t *prompt)

/* Print the string <prompt> to the current command_giver.
 * This function checks if the driver hook H_PRINT_PROMPT is set and in that
 * case passes the string through the set function. If it is not set,
 * the prompt is printed via add_message().
#ifdef USE_TLS
 * The prompt is not printed at all if the interactive is currently
 * negotiating the TLS handshake.
#endif
 */

{
    svalue_t *hook = &driver_hook[H_PRINT_PROMPT];

#ifdef USE_TLS
    {
        interactive_t *ip = O_GET_INTERACTIVE(command_giver);
        if (NULL != ip && TLS_HANDSHAKING == ip->tls_status)
            return;
    }
#endif /* USE_TLS */

    if (hook->type == T_CLOSURE)
    {
        object_t *ob;

        /* Needed for clean error recovery */

        previous_ob = 0;
        current_object = command_giver;

        /* Check if the object the closure is bound to still exists.
         * If not, erase the hook, print the prompt using add_message(),
         * then throw an error.
         */
        ob = !CLOSURE_MALLOCED(hook->x.closure_type)
             ? hook->u.ob
             : hook->u.lambda->ob;

        if (ob->flags & O_DESTRUCTED)
        {
            free_svalue(hook);
            put_number(hook, 0);
            current_object = NULL; /* So that catch_tell() can see it */
            add_message(FMT_STRING, prompt);
            errorf("H_PRINT_PROMPT for %s was a closure bound to a "
                   "now-destructed object - hook removed.\n", 
                   get_txt(command_giver->name));
            /* NOTREACHED */
        }

        push_ref_string(inter_sp, prompt);
        call_lambda(hook, 1);
        free_svalue(inter_sp--);
    }
    else if (hook->type == T_STRING)
    {
        push_ref_string(inter_sp, prompt);
        (void)sapply(hook->u.str, command_giver, 1);
    }
    else
    {
        current_object = NULL; /* So that catch_tell() can see it */
        add_message(FMT_STRING, prompt);
    }
} /* print_prompt_string() */

/*-------------------------------------------------------------------------*/
void
print_prompt (void)

/* Print the prompt of the current command_giver, unless disabled
 * by input_to. If the prompt is set to a closure, the closure
 * is called and expected to return the actual prompt string or
 * to print the prompt itself.
 */

{
    interactive_t *ip;
    svalue_t *prompt = NULL;
    object_t * save_current = current_object;
    object_t * save_previous = previous_ob;
    Bool usingDefaultPrompt = MY_FALSE;

#ifdef DEBUG
    if (command_giver == 0)
        fatal("command_giver == 0.\n");
#endif

    if (!(O_SET_INTERACTIVE(ip, command_giver)))
        fatal("print_prompt() of non-interactive object\n");

    if (ip->input_to != NULL)
    {
        prompt = &ip->input_to->prompt;
    }
    else
#ifdef USE_BUILTIN_EDITOR
    if (NULL == (prompt = get_ed_prompt(ip)))
#endif
    {
        prompt = &ip->prompt;
        if (prompt->type != T_CLOSURE && prompt->type != T_STRING)
        {
            prompt = &driver_hook[H_DEFAULT_PROMPT];
            usingDefaultPrompt = MY_TRUE;
        }
    }

    if (prompt->type == T_CLOSURE)
    {
        object_t *ob;

        /* Needed for clean error recovery */

        previous_ob = 0;
        current_object = command_giver;

        /* Check if the object the closure is bound to still exists.
         * If not, restore the prompt to the default (this also works with
         * the default prompt driver hook), then throw an error.
         */
        ob = !CLOSURE_MALLOCED(prompt->x.closure_type)
             ? prompt->u.ob
             : prompt->u.lambda->ob;

        if (ob && ob->flags & O_DESTRUCTED)
        {
            free_svalue(prompt);
            put_ref_string(prompt, STR_DEFAULT_PROMPT);
            print_prompt_string(prompt->u.str);
            errorf("Prompt of %s was a closure bound to a now-destructed "
                   "object - default prompt restored.\n", 
                   get_txt(command_giver->name));
            /* NOTREACHED */
        }

        call_lambda(prompt, 0);
        prompt = inter_sp;
        if (prompt->type != T_STRING)
        {
            free_svalue(prompt);
        }
        else
        {
            /* beware: print_prompt_string() might cause an error.
             * Thus, the LPC stack has to include the prompt to free it then.
             */
            print_prompt_string(prompt->u.str);
            free_svalue(prompt);
        }
        inter_sp--;
    }
    else if (prompt->type == T_STRING)
    {
        print_prompt_string(prompt->u.str);
    }
    else if (usingDefaultPrompt)
    {
        /* No prompt nor default prompt given, and it's not an input_to:
         * print the usual prompt.
         */
        print_prompt_string(STR_DEFAULT_PROMPT);
    }

    current_object = save_current;
    previous_ob = save_previous;
} /* print_prompt() */

/*-------------------------------------------------------------------------*/
#ifdef USE_SNOOPING
static int
set_snoop (object_t *me, object_t *you)

/* Set a snoop from <me> on the IO of <you>. If <you> is NULL, an
 * existing snoop is terminated. <me> need not to be an interactive
 * user.
 *
 * Return 1 on success, -1 if a snooping loop would be caused, 0 for
 * any other failure.
 *
 * The function calls master->valid_snoop() to test if the snoop
 * is allowed.
 */

{
    interactive_t *on = NULL; /* interactive struct of <you> */
    interactive_t *by = NULL; /* interactive struct of <me> */
    interactive_t *tmp;
    svalue_t *ret;

    /* Stop if people managed to quit before we got this far */
    if (me->flags & O_DESTRUCTED)
        return 0;
    if (you && (you->flags & O_DESTRUCTED))
        return 0;

    /* Check for permissions with valid_snoop in master */
    push_ref_object(inter_sp, me, "snoop");
    if (you == NULL)
        push_number(inter_sp, 0);
    else
        push_ref_object(inter_sp, you, "snoop");
    ret = apply_master(STR_VALID_SNOOP, 2);

    if (!ret || ret->type != T_NUMBER || ret->u.number == 0)
        return 0;

    if (me->flags & O_DESTRUCTED)
        return 0;

    /* Test is <me> is able to snoop anyway.
     * Set <by> to <me>'s interactive struct if yes.
     */
    if (O_SET_INTERACTIVE(by, me) && by->closing)
        return 0;

    if (you)
    {
        /* Test if <you> can be snooped at all.
         * Set <on> to <you>'s interactive struct if yes.
         */
        if (you->flags & O_DESTRUCTED)
            return 0;
        if (!(O_SET_INTERACTIVE(on, you)) || on->closing)
            return 0;
    }
    else
    {
        /* Stop snoop.
         * For this, set <on> to the interactive struct of the snoops
         * victim. If <by> is NULL, <me> is propably a netdead user
         * or a NPC and we have to scan the list of users for the victim.
         */

        if (!by)
        {
            int i;

            for (i = max_player+1;;)
            {
                if (--i < 0)
                    return 0;
                if (NULL != (on = all_players[i]) && on->snoop_by == me)
                    break;
            }
            if (on->closing)
                return 0;
            free_object(me, "set_snoop");
        }
        else
        {
            on = by->snoop_on;
            if (!on || on->closing)
                return 0;
            by->snoop_on = NULL;
        }
        on->snoop_by = NULL;
        return 1;
    }

    /* If we come here, a snoop on <you> by <me> is possible.
     * Now protect against snooping loops.
     */

    for (tmp = on; tmp; tmp = tmp->snoop_on)
    {
        if (tmp == by)
            return -1;
    }

    /* Terminate previous snoop, if any */
    if (on->snoop_by)
    {
        interactive_t *ip;

        if (O_SET_INTERACTIVE(ip, on->snoop_by))
        {
            if (ip->closing)
                return 0;
            ip->snoop_on = 0;
        }
        else
        {
            free_object(on->snoop_by, "set_snoop");
        }
        on->snoop_by = NULL;
    }

    /* Initialise the new snoop */
    if (by)
    {
        if (by->snoop_on)
        {
            if (by->snoop_on->closing)
                return 0;
            by->snoop_on->snoop_by = 0;
            by->snoop_on = 0;
        }
        by->snoop_on = on;
    }
    else
    {
        ref_object(me, "set_snoop");
    }

    on->snoop_by = me;
    return 1;
} /* set_snoop() */
#endif

/*=========================================================================*/
/*                      Telnet Support
 */

/* Note: when stored in char variables, IAC can be equal to EOF.
 * This can cause sprintf(), which is used in add_message(), to abort
 * output after EOF. Therefore, don't try to send anything after the IAC
 * in the same call to add_message().
 */

/*-------------------------------------------------------------------------*/
static INLINE void
send_telnet_option (char action, char option)

/* Send IAC <action> <option> */

{
    char msg[3];

    msg[0] = IAC;
    msg[1] = action;
    msg[2] = option;
    SEND_TELNET_COMMAND(
      add_message(FMT_BINARY, msg, 3);
      add_message(message_flush);
    )
} /* send_telnet_option() */

/*-------------------------------------------------------------------------*/
static void
send_wont (int option)

/* Send IAC WONT <option> */

{
    DTF(("%s TDEBUG: send IAC WONT %02x\n", time_stamp(), option));
    send_telnet_option(WONT, option);
}

/*-------------------------------------------------------------------------*/
static void
send_dont (int option)

/* Send IAC DONT <option> */

{
    DTF(("%s TDEBUG: send IAC DONT %02x\n", time_stamp(), option));
    send_telnet_option(DONT, option);
}

/*-------------------------------------------------------------------------*/
static void
send_will (int option)

/* Send IAC WILL <option> */

{
    DTF(("%s TDEBUG: send IAC WILL %02x\n", time_stamp(), option));
    send_telnet_option(WILL, option);
}

/*-------------------------------------------------------------------------*/
static void
send_do (int option)

/* Send IAC DO <option> */

{
    DTF(("%s TDEBUG: send IAC DO %02x\n", time_stamp(), option));
    send_telnet_option(DO, option);
}

/*-------------------------------------------------------------------------*/
static void
reply_nil (int option UNUSED)

/* Dummy function which does nothing. */

{
#ifdef __MWERKS__
#    pragma unused(option)
#endif
}

/*-------------------------------------------------------------------------*/
static void
reply_to_do_echo (int option)

/* Send IAC WONT <option> if we don't want noecho mode.
 * If we requested WILL ECHO this is the client's reply. Set NOECHO_ACK. Send
 * no reply.  Send WILL ECHO if we want noecho but where told not to echo
 * (reactivate noecho mode).
 */

{
    interactive_t *ip = O_GET_INTERACTIVE(command_giver);

    DTN(("reply to DO ECHO\n"));
    if (ip->noecho & NOECHO_MASK) {
        if ( !(ip->noecho & NOECHO) ) {
            /* We were previously told not to echo */
            send_will(option);
        }
        else DTN(("  we don't need to say WILL\n"));
        /* If we already said that we will echo, be quiet */
        ip->noecho |= NOECHO_MASK;
    } else {
        send_wont(option);
    }
}

/*-------------------------------------------------------------------------*/
static void
reply_to_dont_echo (int option)

/* If we requested WONT ECHO this is the client's reply. Do nothing.
 * If client requests us to not echo while we want to, send WONT ECHO and
 * delete NOECHO flag. The client may turn the option on again later.
 */

{
    interactive_t *ip = O_GET_INTERACTIVE(command_giver);

    DTN(("reply to DONT ECHO\n"));
    if (ip->noecho & NOECHO_MASK) {
        if (!~(ip->noecho | ~NOECHO_MASK)) {
            /* We were granted the option before */
            send_wont(option);
        }
        else DTN(("  we don't need to say WONT\n"));
        ip->noecho = (char)((ip->noecho & ~NOECHO) | NOECHO_ACK);
    }
}

/*-------------------------------------------------------------------------*/
static void
reply_to_do_sga (int option)

/* Send IAC WILL <option> if Suppress Go Ahead is not already active and
 * mark it as active, send IAC WONT <option> if neither in NOECHO or
 * CHARMODE mode.
 */

{
    interactive_t *ip = O_GET_INTERACTIVE(command_giver);

    DTN(("reply to DO SGA\n"));
    if (ip->noecho & (NOECHO_MASK|CHARMODE_MASK)) {
        if (!ip->supress_go_ahead) {
            ip->supress_go_ahead = MY_TRUE;
            send_will(option);
        }
        else DTN(("  we don't need to say WILL\n"));
    } else {
        send_wont(option);
    }
}

/*-------------------------------------------------------------------------*/
static void
reply_to_dont_sga (int option)

/* Send IAC WONT <option> if Suppress Go Ahead is active and mark it as
 * inactive.
 */

{
    interactive_t *ip = O_GET_INTERACTIVE(command_giver);

    DTN(("reply to DONT SGA\n"));
    if (ip->supress_go_ahead) {
        ip->supress_go_ahead = 0;
        send_wont(option);
    }
}

/*-------------------------------------------------------------------------*/
static void
reply_to_will_sga (int option)

/* Send IAC DO <option> if CHARMODE is requested but not active yet,
 * send IAC DONT <option> if CHARMODE is neither requested nor active.
 */

{
    interactive_t *ip = O_GET_INTERACTIVE(command_giver);

    DTN(("reply to WILL SGA\n"));
    if (ip->noecho & CHARMODE_MASK) {
        if ( !(ip->noecho & CHARMODE) ) {
            send_do(option);
        }
        else DTN(("  we don't need to say DO\n"));
        DTN(("  noecho: %02hhx -> %02hhx\n", ip->noecho, (char)(ip->noecho  | CHARMODE_MASK)));
        ip->noecho |= CHARMODE_MASK;
    } else {
        send_dont(option);
    }
#ifdef SAVE_NOECHO
    if (ip->noecho & NOECHO_DELAYED)
    {
        DT(("'%s' set_noecho():   sending delayed WILL TELOPT_ECHO\n",
            get_txt(ip->ob->name)));
        ip->noecho &= ~NOECHO_DELAYED;
        if (!(ip->noecho & NOECHO_MASK))
        {
            send_will(TELOPT_ECHO);
            ip->noecho |= NOECHO_REQ | NOECHO;
        }
        else DT(("'%s'   we don't need to say WILL\n", get_txt(ip->ob->name)));
    }
#endif /* SAVE_NOECHO */
} /* reply_to_will_sga() */

/*-------------------------------------------------------------------------*/
static void
reply_to_wont_sga (int option)

/* Send IAC DONT <option> if CHARMODE was granted before.
 */

{
    interactive_t *ip = O_GET_INTERACTIVE(command_giver);

    DTN(("reply to WONT SGA\n"));
    if (ip->noecho & CHARMODE_MASK) {
        if (!~(ip->noecho | ~CHARMODE_MASK)) {
            /* We were granted the option before */
            send_dont(option);
        }
        else DTN(("  we don't need to say DONT\n"));
        DTN(("  noecho: %02hhx -> %02hhx\n", ip->noecho, 
             (unsigned char)((ip->noecho & ~CHARMODE) | CHARMODE_ACK)));
        ip->noecho = (char)((ip->noecho & ~CHARMODE) | CHARMODE_ACK);
          /* Don't reset CHARMODE_REQ here: this WONT can be the answer
           * to the DO SGA we sent before, and the client can still answer
           * with DO SGA to the WILL SGA we sent as well (Windows' telnet
           * for example does this).
           * Besides, the variables are now set up to treat the input
           * in charmode, and changing the flag without the variables
           * will do Bad Things(tm).
           */
    }
} /* reply_to_wont_sga() */

/*-------------------------------------------------------------------------*/
static void
mccp_telnet_neg (int option)
{
    interactive_t *ip = O_GET_INTERACTIVE (command_giver);
    
     switch (ip->tn_state)
     {
     case TS_WILL:
         DTF(("MCCP NEG (%d) STATE (WILL)\n", option));
         break;
     case TS_WONT:
         DTF(("MCCP NEG (%d) STATE (WONT)\n", option));
         break;
     case TS_DO:
         DTF(("MCCP NEG (%d) STATE (DO)\n", option));
#ifdef USE_MCCP
         if (!ip->compressing)
             start_compress(ip, option);
#endif
         break;
     case TS_DONT:
         DTF(("MCCP NEG (%d) STATE (DONT)\n", option));
#ifdef USE_MCCP
         if (ip->compressing==option)
             end_compress(ip, MY_FALSE);
#endif
        break;
     default:
         DTF(("MCCP NEG (%d) STATE (%hhd)\n", option, ip->tn_state));
     }
} /* mccp_telnet_neg() */

/*-------------------------------------------------------------------------*/
static svalue_t *
h_telnet_neg (int n)

/* Call the H_TELNET_NEG driverhook with <n> arguments on the interpreter
 * stack. Return the result from that call, or NULL if the hook isn't
 * set. The arguments are removed from the stack in any case.
 */

{
    svalue_t *svp;

    RESET_LIMITS;
    CLEAR_EVAL_COST;
    if (driver_hook[H_TELNET_NEG].type == T_STRING)
    {
        svp =
          secure_apply(driver_hook[H_TELNET_NEG].u.str, command_giver, n);
    }
    else if (driver_hook[H_TELNET_NEG].type == T_CLOSURE)
    {
        if (driver_hook[H_TELNET_NEG].x.closure_type == CLOSURE_LAMBDA)
        {
            free_object(driver_hook[H_TELNET_NEG].u.lambda->ob, "h_telnet_neg");
            driver_hook[H_TELNET_NEG].u.lambda->ob = ref_object(command_giver, "h_telnet_neg");
        }
        svp = secure_callback_lambda(&driver_hook[H_TELNET_NEG], n);
    }
    else
    {
        while (--n >= 0)
            pop_stack();
        svp = NULL;
    }
    return svp;
}

/*-------------------------------------------------------------------------*/
static void
reply_h_telnet_neg (int option)

/* Call the H_TELNET_NEG driver hook with <tn_state> <option> as
 * arguments. If the hook is not defined, send WONT <option> if
 * the state is TS_DO, or send DONT <option> if the state is TS_WILL.
 */

{
    interactive_t *ip = O_GET_INTERACTIVE(command_giver);
    int i = 0;

    switch(ip->tn_state) {
      case TS_DO:
        DTN(("reply to telnet_neg: DO %02x\n", option));
        i = DO;
        break;
      case TS_DONT:
        DTN(("reply to telnet_neg: DONT %02x\n", option));
        i = DONT;
        break;
      case TS_WILL:
        DTN(("reply to telnet_neg: WILL %02x\n", option));
        i = WILL;
        break;
      case TS_WONT:
        DTN(("reply to telnet_neg: WONT %02x\n", option));
        i = WONT;
        break;
      default:
        debug_message("%s Invalid tn_state %hhd for interactive '%s'\n"
             , time_stamp(), ip->tn_state, get_txt(ip->ob->name));
        break;
    }
    push_number(inter_sp, i);
    push_number(inter_sp, option);
    if (!h_telnet_neg(2)) {
        DTN(("  using default methods\n"));
        switch(ip->tn_state) {
          case TS_DO:
            DTN(("    -> WONT %02x\n", option));
            send_wont(option);
            break;
          case TS_WILL:
            DTN(("    -> DONT %02x\n", option));
            send_dont(option);
            break;
        }
    }
} /* reply_h_telnet_neg() */

/*-------------------------------------------------------------------------*/
void
init_telopts (void)

/* Initialise the telopts_xxx[] tables.
 * The default setting is such that requests are ignored or rejected.
 */

{
    int i;

    for (i = NTELOPTS; --i >= 0; ) {
        telopts_do[i] = send_wont;
    }
    for (i = NTELOPTS; --i >= 0; ) {
        telopts_dont[i] = reply_nil;
    }
    for (i = NTELOPTS; --i >= 0; ) {
        telopts_will[i] = send_dont;
    }
    for (i = NTELOPTS; --i >= 0; ) {
        telopts_wont[i] = reply_nil;
    }

    telopts_do[TELOPT_ECHO] = reply_to_do_echo;
    telopts_dont[TELOPT_ECHO] = reply_to_dont_echo;

    telopts_do[TELOPT_TM] = reply_h_telnet_neg;
    telopts_dont[TELOPT_TM] = reply_h_telnet_neg;
    telopts_will[TELOPT_TM] = reply_h_telnet_neg;
    telopts_wont[TELOPT_TM] = reply_h_telnet_neg;

    telopts_do[TELOPT_NEWENV] = reply_h_telnet_neg;
    telopts_dont[TELOPT_NEWENV] = reply_h_telnet_neg;
    telopts_will[TELOPT_NEWENV] = reply_h_telnet_neg;
    telopts_wont[TELOPT_NEWENV] = reply_h_telnet_neg;

    telopts_do[TELOPT_ENVIRON] = reply_h_telnet_neg;
    telopts_dont[TELOPT_ENVIRON] = reply_h_telnet_neg;
    telopts_will[TELOPT_ENVIRON] = reply_h_telnet_neg;
    telopts_wont[TELOPT_ENVIRON] = reply_h_telnet_neg;

    telopts_do[TELOPT_XDISPLOC] = reply_h_telnet_neg;
    telopts_dont[TELOPT_XDISPLOC] = reply_h_telnet_neg;
    telopts_will[TELOPT_XDISPLOC] = reply_h_telnet_neg;
    telopts_wont[TELOPT_XDISPLOC] = reply_h_telnet_neg;

    telopts_do[TELOPT_LINEMODE] = reply_h_telnet_neg;
    telopts_dont[TELOPT_LINEMODE] = reply_h_telnet_neg;
    telopts_will[TELOPT_LINEMODE] = reply_h_telnet_neg;
    telopts_wont[TELOPT_LINEMODE] = reply_h_telnet_neg;

    telopts_do[TELOPT_NAWS] = reply_h_telnet_neg;
    telopts_dont[TELOPT_NAWS] = reply_h_telnet_neg;
    telopts_will[TELOPT_NAWS] = reply_h_telnet_neg;
    telopts_wont[TELOPT_NAWS] = reply_h_telnet_neg;

    telopts_do[TELOPT_TTYPE] = reply_h_telnet_neg;
    telopts_dont[TELOPT_TTYPE] = reply_h_telnet_neg;
    telopts_will[TELOPT_TTYPE] = reply_h_telnet_neg;
    telopts_wont[TELOPT_TTYPE] = reply_h_telnet_neg;

    telopts_do[TELOPT_TSPEED] = reply_h_telnet_neg;
    telopts_dont[TELOPT_TSPEED] = reply_h_telnet_neg;
    telopts_will[TELOPT_TSPEED] = reply_h_telnet_neg;
    telopts_wont[TELOPT_TSPEED] = reply_h_telnet_neg;

    telopts_do[TELOPT_BINARY] = reply_h_telnet_neg;
    telopts_dont[TELOPT_BINARY] = reply_h_telnet_neg;
    telopts_will[TELOPT_BINARY] = reply_h_telnet_neg;
    telopts_wont[TELOPT_BINARY] = reply_h_telnet_neg;

    /* Tinyfugue can do bad things to your health */
    telopts_do[TELOPT_EOR] = reply_h_telnet_neg;
    telopts_dont[TELOPT_EOR] = reply_h_telnet_neg;
    telopts_will[TELOPT_EOR] = reply_h_telnet_neg;
    telopts_wont[TELOPT_EOR] = reply_h_telnet_neg;

    /* Go Ahead does not make any sense when coupling multiple
     * interactive users. It is debatable if we are sending
     * Go Ahead every time it is appropriate (i.e. , never),
     * or we supress it all the time.
     * Unfortunately, SGA is also often associated with
     * character-at-a-time mode - the RFC even mandates this
     * double meaning - which we certainly don't want.
     * It might cause problems when we reject Supress Go Ahead
     * when some stupid client thinks that the ECHO option need
     * be coupled with SGA .
     * Thus, reject SGA in general, but not while
     * ip->noecho & NOECHO_MASK is true.
     */
    telopts_do[TELOPT_SGA] = reply_to_do_sga;
    telopts_dont[TELOPT_SGA] = reply_to_dont_sga;
    telopts_will[TELOPT_SGA] = reply_to_will_sga;
    telopts_wont[TELOPT_SGA] = reply_to_wont_sga;

    /* Mud specific protocols */

    telopts_do[TELOPT_COMPRESS] = mccp_telnet_neg;
    telopts_dont[TELOPT_COMPRESS] = mccp_telnet_neg;
    telopts_will[TELOPT_COMPRESS] = mccp_telnet_neg;
    telopts_wont[TELOPT_COMPRESS] = mccp_telnet_neg;

    telopts_do[TELOPT_COMPRESS2] = mccp_telnet_neg;
    telopts_dont[TELOPT_COMPRESS2] = mccp_telnet_neg;
    telopts_will[TELOPT_COMPRESS2] = mccp_telnet_neg;
    telopts_wont[TELOPT_COMPRESS2] = mccp_telnet_neg;

    telopts_do[TELOPT_MSP] = reply_h_telnet_neg;
    telopts_dont[TELOPT_MSP] = reply_h_telnet_neg;
    telopts_will[TELOPT_MSP] = reply_h_telnet_neg;
    telopts_wont[TELOPT_MSP] = reply_h_telnet_neg;

    telopts_do[TELOPT_MXP] = reply_h_telnet_neg;
    telopts_dont[TELOPT_MXP] = reply_h_telnet_neg;
    telopts_will[TELOPT_MXP] = reply_h_telnet_neg;
    telopts_wont[TELOPT_MXP] = reply_h_telnet_neg;

    telopts_do[TELOPT_STARTTLS] = reply_h_telnet_neg;
    telopts_dont[TELOPT_STARTTLS] = reply_h_telnet_neg;
    telopts_will[TELOPT_STARTTLS] = reply_h_telnet_neg;
    telopts_wont[TELOPT_STARTTLS] = reply_h_telnet_neg;
} /* init_telopts() */

/*-------------------------------------------------------------------------*/
void
mudlib_telopts (void)

/* Set all telopts_xxx[] entries to reply_h_telnet_neg().
 * This means that the mudlib does all the telnet negotiation.
 * It is called whenever driver hook H_NOECHO is set.
 */

{
    int i;

    DT(("All telnet options set to the mudlib.\n"));
    for (i = NTELOPTS; --i >= 0; ) {
        telopts_do[i] = telopts_dont[i] =
          telopts_will[i] = telopts_wont[i] = reply_h_telnet_neg;
    }
} /* mudlib_telopts() */

/*-------------------------------------------------------------------------*/
static INLINE Bool
is_charmode (interactive_t * ip)

/* Return TRUE if <ip> has charmode enabled, and FALSE if not.
 */

{
    return (ip->noecho & CHARMODE_REQ)
            && (   ip->text[0] != input_escape
                || find_no_bang(ip) & IGNORE_BANG
               )
               
    ;
} /* is_charmode() */

/*-------------------------------------------------------------------------*/
static void
telnet_neg (interactive_t *ip)

/* Process the data read from the socket, performing any telnet negotiations
 * necessary, and extract the 'pure' command text. When the function returns,
 * all new data in .text[] has been used and .text_end set back as far
 * as possible.
 *
 * The start state for the telnet machine is TS_DATA, and whenever a command
 * text has been completed, it assumes the TS_READY state.
 *
 * The function tn_end and goes on until it reaches text_end or a full newline.
 *
 * When it returns:
 *   tn_end is set to the first unprocessed character.
 * When a full newline is found:
 *   Processed commands start at command_start and are \0 terminated strings
 *    state is set to READY
 * else
 *   Processed commands start at command_start and end at command_end-1
 *   state is set to DATA (or something else if we got a fragmented
 *     telnet negotiation).
 *
 * text_end could move a bit to the start of text if we deleted chars
 * from the raw input string (e.g. because it was an IAC).
 *
 * If gobble_char is set, that char is removed from a fresh text packet.
 * Removing of unwanted chars inside of a packet is done at the appropriate
 * place (case '\r':). There is no gobbling of <LN><CR> sequences in
 * character mode (why not?). Code would have to be added at case '\n':
 * to gobble them in-packet.
 *
 * Example:
 * text = "say hello\r\nsay hi\r\n";
 * 
 * Output would be:
 * text = "say hello\0\nsay hi\r\n";
 * 
 * command_start = 0
 * command_end = 0
 * tn_end = 11 (s of 2nd say)
 * text_end stays at 19 (first unused char in text)
 * state = TS_READY
 *
 * After a second call of telnet_neg (almost always done by get_message())
 * will pre process the second command:
 *
 * text = "say hi\0lo\0\nsay hi\r\n";
 * 
 * command_start = 0
 * command_end = 0
 * tn_end = 7
 * text_end = 7
 * state = READY
 */

{
    fd_set exceptfds;
    char *from;   /* Next char to process */
    char *to;     /* Where to store the extracted command text */
    int   state;
    int   ch;     /* Current character */
    char *first;  /* Begin of the last pure command text */
    char *end;    /* End of data in text[] */

    first = ip->text;
    from = &first[ip->tn_end];
    end = &first[ip->text_end];

    DTN(("telnet_neg: state %hhd\n", ip->tn_state));

    /* Gobble the character *from if gobble_char is set.
     * Also test for the end of current buffer content.
     *
     * If we want to gobble NL, we also gobble NUL
     * (used for CR NL and CR NUL digraphs)
     */
    for (;;)
    {
        if (from >= end) {
#if 0 /* cannot happen with the current calling pattern */
            if (ip->state == TS_READY) return;
#endif
            ip->text_end = ip->tn_end = ip->command_end;
            return;
        }
        if (ip->gobble_char) {
            DTN(("t_n: gobble char %02hhx (in buf: %02x)\n"
               , ip->gobble_char, *from));
/* we had a crash around here:
 *
#0  telnet_neg (ip=0x8df9918) at comm.c:5472
#1  0x080631a7 in get_message (
    buff=0xbfd61d16 "\"�\"T\225�\207̥dchȫn Rosa und Jungs wollen nur Fu�\237ba7)��*�>EG\224s�\215X��\215'P\206\027�\017��1�jN#\001\220\216\223\030U\\,�Oa\024F
#2  0x08052b6a in backend () at backend.c:622
#3  0x080a5809 in main (argc=151587082, argv=0x9090a0d) at main.c:672
 *
 * apparently at the time of compilation, line 5472 was the from == comparison
 * below. strange or funny, we never touched this part of code. so it must be
 * a quite old bug. anyway...  FIXME.

 * same crash again, 2008-12-10. this time i look at it more closely.
 *
#0  telnet_neg (ip=0xa5a01a8) at comm.c:5533
#1  0x080632b5 in get_message (
    buff=0xbf96e3b2 "\002\002P�G\vC\002��\226�\001\001\200", len=0xbf976488)
    at comm.c:3180
#2  0x08053026 in backend () at backend.c:629
#3  0x080a43a4 in main (argc=1936024636, argv=0x70697263) at main.c:681
 *
(gdb) print from
$1 = 0x2acc67ca <Address 0x2acc67ca out of bounds>
(gdb) print end
$2 = 0x729e22c5 <Address 0x729e22c5 out of bounds>
(gdb) print first
No symbol "first" in current context.
(gdb) print ip
$5 = (interactive_t *) 0xa5a01a8
(gdb) print ip->gobble_char
$6 = 101 'e'
(gdb) print *from
Cannot access memory at address 0x2acc67ca
(gdb) print ip->text
$7 = "da hat sich verletzt.</description>\n<category>Sport</category>\n<pubDate>Tue, 09 Dec 2008 11:01:33 +0100</pubDate>\n<guid>http://www.spiegel.de/sport/fussball/0,1518,595301,00.html</guid>\n<content:encod"...
(gdb) print ip->tn_end
$8 = 544367982
(gdb) print ip->text_end
$9 = 1749295209
(gdb) print &ip->text
$10 = (char (*)[32770]) 0xa5a025c
(gdb) print &(ip->text)[ip->tn_end]
$12 = 0x2acc67ca <Address 0x2acc67ca out of bounds>

 * alright. so here we have a case where tn_end and text_end have
 * completely absurd values. how did they get into there. we could add
 * some crazy paranoid checks here, but that's bad for performance and
 * bad anyway. disabling telnet in net/http/fetch will be helpful, but
 * i'd prefer to understand how we got into this problem.
 */
            if (*from == ip->gobble_char
                || (*from == '\0' && ip->gobble_char == '\n')
               )
            {
                from++;
            }
            ip->gobble_char = '\0';
            continue;
        }
        break;
    }
    to = &first[ip->command_end];

    /* The processing loop */

    do {
        ch = (*from++ & 0xff);
        DTN(("t_n: processing %02hhx '%c'\n"
            , (unsigned char)ch, ch));
        switch(ip->tn_state)
        {
        case TS_READY:
            DTN(("t_n: still in TS_READY - return\n"));
            /* Previous command hasn't been read yet - don't clobber it! */
            return;

        ts_data:
            /* Most state functions end with a jump here to check if they
             * exhausted their input.
             */
            if (from >= end)
            {
                ip->text_end = ip->tn_end = ip->command_end = (short)(to - first);
                 DTN(("t_n: (ts_data) from >= end by %td, text_end := %hd\n (max %d)"
                     , (ptrdiff_t)(from-end), ip->text_end, MAX_TEXT));
                *to = '\0';
                if (ip->text_end >= MAX_TEXT)
                {
                    /* this looks like a super-long command.
                     * Return the text so far as partial command and restart
                     * input from the beginning.
                     * In charmode, we must not reset command_end, otherwise
                     * it might fall under command_start.
                     */
                    ip->tn_state = TS_READY;
                    ip->tn_end = 0;
                    ip->text_end = 0;
                    if (!(ip->noecho & (CHARMODE_REQ|CHARMODE)))
                    {
                        ip->command_end = 0;
                    }
                    return;
                }
                return;
            }
            ch = (*from++ & 0xff);
            DTN(("t_n: (ts_data) processing %02hhx '%c'\n"
                , (unsigned char)ch, ch));
            /* FALLTHROUGH */

        case TS_DATA: /* --- Copy/interpret plain data --- */
            switch(ch)
            {
            case IAC:
            new_iac:
                  if (ip->tn_enabled)
                  {
                      state = TS_IAC;
        change_state:
                      DTN(("t_n: new state %d\n", state));
                      ip->tn_state = (char)state;
                      continue;
                  }
                  /* FALLTHROUGH if !tn_enabled */

            case '\b':        /* Backspace */
            case 0x7f:        /* Delete */
                /* In Linemode, just move to one char back.
                 * In Charmode with escaped input, write the data gathered
                 * so far and add a rubout sequence ('\b \b').
                 * In Charmode with unescaped input, just pass it on to
                 * the mudlib.
                 *
                 * If telnet is disabled, fallthrough to the general
                 * data handling.
                 */
                if (ip->tn_enabled)
                {
                    if ( !(ip->noecho & CHARMODE_REQ) )
                    {
                        if (to > first)
                            to--;
                        goto ts_data;
                    }

                    if (ip->text[0] == input_escape
                     && ! (find_no_bang(ip) & IGNORE_BANG) )
                    {
#ifdef USE_PTHREADS
                        if (to > &ip->text[ip->chars_ready])
                        {
                            thread_socket_write(ip->socket, &ip->text[ip->chars_ready],
                              (size_t)(to - &ip->text[ip->chars_ready]), ip, MY_FALSE);
                            ip->chars_ready = to - ip->text;
                        }
                        if (to > first) {
                            thread_socket_write(ip->socket, "\b \b", 3, ip, MY_FALSE);
                            to--;
                            ip->chars_ready--;
                        }
#else
                        if (to > &ip->text[ip->chars_ready])
                        {
#ifdef USE_TLS
                            if (ip->tls_status != TLS_INACTIVE)
                                tls_write(ip, &ip->text[ip->chars_ready]
                                         , (size_t)(to - &ip->text[ip->chars_ready]));
                            else
#endif
                                socket_write(ip->socket, &ip->text[ip->chars_ready],
                                  (size_t)(to - &ip->text[ip->chars_ready]));
                            ip->chars_ready = to - ip->text;
                        }
                        if (to > first) {
#ifdef USE_TLS
                            if (ip->tls_status != TLS_INACTIVE)
                                tls_write(ip, "\b \b", 3);
                            else
#endif
                                socket_write(ip->socket, "\b \b", 3);
                            to--;
                            ip->chars_ready--;
                        }
#endif
                        goto ts_data;
                    }
                } /* if (ip->tn_enabled) */
                /* FALLTHROUGH */

            default:
                *to++ = (char)ch;
                /* FALLTHROUGH */

            case '\0':
                /* In Charmode, we should return the \0 (as with CR and LF),
                 * but for the caller the \0 has magical properties.
                 */
                goto ts_data;

            case '\r':
                /* In Charmode we have to return the \r.
                 */
                if (is_charmode(ip))
                {
                    *to++ = (char)ch;
                    goto ts_data;
                }

                if (from >= end)
                {
                    /* This might be a fragmented CR NL, CR NUL, or
                     * a broken client that ends lines with CR only.
                     * We proceed as full newline now, but gobble
                     * NL or NUL if they are sent afterwards.
                     */
                    ip->gobble_char = '\n';
                }
                else
                {
                    ch = (*from++ & 0xff);
                    /* gobble following NL and NUL */
                    if (ch && ch != '\n')
                        from--;
                }

        full_newline:
                /* Proper line end found: set telnet machine into TS_READY,
                 * terminate the command with \0 and return.
                 */
                {
                    ip->tn_state = TS_READY;
                    ip->command_end = 0;
                    ip->tn_end = (short)(from - first);

                    /* Even in charmode we append the NUL in case the client
                     * refused to use charmode, because then get_message()
                     * will treat the data as if in linemode and expect
                     * a trailing NUL.
                     */
                    *to = '\0';
                    return;
                }

            case '\n':
                /* In Charmode we have to return the \n.
                 */
                if (is_charmode(ip))
                {
                    *to++ = (char)ch;
                    goto ts_data;
                }

                ip->gobble_char = '\r';
                goto full_newline;
            } /* switch(ch) */

            /* NOTREACHED */

        ts_iac:
        case TS_IAC:
            DTN(("t_n: state IAC\n"));
            /* Begin a telnet negotiation */
            switch(ch)
            {
            case IAC:
                DTN(("t_n: got IAC\n"));
                *to++ = ch;
                ip->tn_state = state = TS_DATA;
                goto ts_data;
            case WILL:
                DTN(("t_n: got WILL\n"));
                state = TS_WILL;
                goto change_state;
            case WONT:
                DTN(("t_n: got WONT\n"));
                state = TS_WONT;
                goto change_state;
            case DO:
                DTN(("t_n: got DO\n"));
                state = TS_DO;
                goto change_state;
            case DONT:
                DTN(("t_n: got DONT\n"));
                state = TS_DONT;
                goto change_state;
            case SB:
                DTN(("t_n: got SB\n"));
                ip->tn_start = (short)(to - first);
                state = TS_SB;
                goto change_state;
            case DM:
                DTN(("t_n: got DM\n"));
            data_mark:
                if (ip->ts_data == TS_SYNCH)
                {
                    struct timeval timeout;

                    FD_ZERO(&exceptfds);
                    FD_SET(ip->socket, &exceptfds);
                    timeout.tv_sec = 0;
                    timeout.tv_usec = 0;
                    if (! socket_select(ip->socket + 1, 0, 0, &exceptfds,
                        &timeout))
                    {
#ifdef DEBUG_TELNET
                        if (d_flag)
                            debug_message("%s Synch operation finished.\n"
                                         , time_stamp());
#endif
                        ip->ts_data = TS_DATA;
                    }
                }
                break;
            case NOP:
                DTN(("t_n: got NOP\n")); break;
            case GA:
                DTN(("t_n: got GA\n")); break;
            default:
                DTN(("t_n: got %02hhx\n", ch)); break;
                break;
            } /* switch(ch) */
            state = ip->ts_data;
            goto change_state;

        case TS_WILL:
            command_giver = ip->ob;
            if (ch < NTELOPTS) {
                DTN(("t_n: state WILL got %s (%02hhx)\n"
                   , telopts[ch], ch));
#ifdef DEBUG_TELNET
                if (d_flag)
                    debug_message("%s Will %s\n", time_stamp(), telopts[ch]);
#endif
                (*telopts_will[ch])(ch);
            } else {
                debug_message("%s Unknown telnet option Will %hhd\n"
                             , time_stamp(), ch);
                send_dont(ch);
            }
            state = ip->ts_data;
            goto change_state;

        case TS_WONT:
            command_giver = ip->ob;
            if (ch < NTELOPTS) {
                DTN(("t_n: state WONT got %s (%02hhx)\n"
                   , telopts[ch], ch));
#ifdef DEBUG_TELNET
                if (d_flag)
                    debug_message("%s Wont %s\n", time_stamp(), telopts[ch]);
#endif
                (*telopts_wont[ch])(ch);
            } else {
                debug_message("%s Unknown telnet option Wont %hhd\n"
                             , time_stamp(), ch);
            }
            state = ip->ts_data;
            goto change_state;

        case TS_DO:
            command_giver = ip->ob;
            if (ch < NTELOPTS) {
                DTN(("t_n: state DO got %s (%02hhx)\n"
                   , telopts[ch], ch));
#ifdef DEBUG_TELNET
                if (d_flag)
                    debug_message("%s Do %s\n", time_stamp(), telopts[ch]);
#endif
                (*telopts_do[ch])(ch);
            } else {
                debug_message("%s Unknown telnet option Do %hhd\n"
                             , time_stamp(), ch);
                send_wont(ch);
            }
            state = ip->ts_data;
            goto change_state;

        case TS_DONT:
            command_giver = ip->ob;
            if (ch < NTELOPTS) {
                DTN(("t_n: state DONT got %s (%02hhx)\n"
                   , telopts[ch], ch));
#ifdef DEBUG_TELNET
                if (d_flag)
                    debug_message("%s Dont %s\n", time_stamp(), telopts[ch]);
#endif
                (*telopts_dont[ch])(ch);
            } else {
                debug_message("%s Unknown telnet option Dont %hhd\n"
                             , time_stamp(), ch);
            }
            state = ip->ts_data;
            goto change_state;

        case TS_SB:
            DTN(("t_n: state TS_SB got %02hhx\n", ch));
            if (ch == IAC) {
                state = TS_SB_IAC;
                goto change_state;
            }
            *to++ = (char)ch;
            continue;

        case TS_SB_IAC:
          {
            mp_int size;
            vector_t *v;

            DTN(("t_n: state TS_SB_IAC got %02hhx\n", ch));
            if (ch == IAC) {
                DTN(("t_n: that is: state TS_SB_IAC got IAC\n"));
                *to++ = (char)ch;
                state = TS_SB;
                goto change_state;
            } else if ((ch == SE || ch == SB)
                && (  (size = (to - first) - ip->tn_start - 1) <= (mp_int)max_array_size
                    || !max_array_size)
                && size >= 0
                && (current_object = ip->ob,  v = allocate_array(size)) )
            {
                unsigned char *str;
                svalue_t *svp;

                str = (unsigned char *)&ip->text[ip->tn_start];
                DTN(("t_n: that is: state TS_SB_IAC got useful SE or SB: neg SB %02hhx (%"PRIdMPINT" bytes)\n", *str, size));
                push_number(inter_sp, SB);
                push_number(inter_sp, *str++);
                svp = v->item;
                while (--size >= 0) {
                    svp->u.number = *str++;
                    svp++;
                }
                push_array(inter_sp, v);
                command_giver = ip->ob;
                h_telnet_neg(3);
            }
            to = &first[ip->tn_start];
            if (ch != SE)
                goto ts_iac;
            state = ip->ts_data;
            goto change_state;
          }

        case TS_SYNCH:
            DTN(("t_n: state TS_SYNCH got %02hhx\n", ch));
            if (ch == IAC) goto new_iac;
            if (ch == DM) goto data_mark;
            continue;

        default:
#ifdef DEBUG_TELNET
            if (d_flag)
                debug_message("%s Bad state: 0x%hhx\n", time_stamp(), ip->tn_state);
#endif
            state = TS_DATA;
            goto change_state;
        } /* switch (ip->tn_state) */

    } while(from < end);

    /* We used all the new data in .text[] but found no complete command.
     * Reset all pointers necessary to read new data.
     */

    ip->text_end = ip->tn_end = ip->command_end = (short)(to - first);
    if (ip->text_end == MAX_TEXT)
    {
        /* telnet negotiation shouldn't have such large data chunks.
         * Ignore all data altogether and return to text mode.
         */
        ip->text_end = ip->tn_end = ip->command_end = 0;
        ip->tn_start = ip->command_start = 0;
        ip->tn_state = TS_DATA;
    }
} /* telnet_neg() */

/* End of Telnet support */
/*=========================================================================*/
/*                      ERQ Support
 */

#ifdef ERQ_DEMON
/*-------------------------------------------------------------------------*/
void
start_erq_demon (const char *suffix, size_t suffixlen)

/* Start the ERQ demon from the path 'ERQFILE<suffix>' and setup
 * the pending_erq[] array.
 */

{
    erq_callback_t *erqp;
    char path[MAXPATHLEN+1];
    int sockets[2];
    int pid;
    char c;

    /* Create the freelist in pending_erq[] */
    pending_erq[0].fun.type = T_INVALID;
    pending_erq[0].fun.u.generic = NULL;

    erqp = pending_erq + 1;
    while (erqp < &pending_erq[MAX_PENDING_ERQ])
    {
        erqp->fun.u.generic = (void *)(erqp - 1);
        erqp->fun.type = T_INVALID;
        erqp++;
    }
    free_erq = &pending_erq[MAX_PENDING_ERQ-1];

    /* Create the sockets to talk to the ERQ */
/* TODO: Add tests to configure if the system really implements AF_UNIX or socketpair() */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0)
    {
        perror("socketpair");
        return;
    }

    (void)signal(SIGCLD, SIG_IGN); /* don't create zombie processes */

#ifdef USE_LDMUD_COMPATIBILITY
#ifdef VERBOSE
    printf("%s Attempting to start erq '%s%s'.\n"
          , time_stamp(), erq_file, suffix);
#endif
    debug_message("%s Attempting to start erq '%s%s'.\n"
                 , time_stamp(), erq_file, suffix);
#endif

    if ((pid = fork()) == 0)
    {
        /* Child */
        dup2(sockets[0], 0);
        dup2(sockets[0], 1);
        close(sockets[0]);
        close(sockets[1]);

        if (strlen(erq_file) + 1 + suffixlen <= sizeof path)
        {
            sprintf(path, "%s%.*s", erq_file, (int)suffixlen, suffix);
            if (erq_args)
                execv((char *)path, erq_args);
            else
                execl((char *)path, "erq", "--forked", (char*)0);
        }
        write(1, "0", 1);  /* indicate failure back to the driver */
        _exit(1);
    }

    close(sockets[0]);
    if (pid == -1) {
        close(sockets[1]);
        return;
    }

    /* Read the first character from the ERQ. If it's '0', the ERQ
     * didn't start.
     */
    read(sockets[1], &c, 1);
    if (c == '0') {
        close(sockets[1]);

#ifdef USE_LDMUD_COMPATIBILITY
#ifdef VERBOSE
        printf("%s Failed to start erq.\n", time_stamp());
#endif
        debug_message("%s Failed to start erq.\n", time_stamp());
#else
	debug_message("%s Failed to start erq from '%s%s'.\n"
		     , time_stamp(), erq_file, suffix);
#endif
        return;
    }

    /* ERQ is up and running */
    erq_demon = sockets[1];
    set_socket_nonblocking(erq_demon);
    if (socket_number(erq_demon) >= min_nfds)
        min_nfds = socket_number(erq_demon)+1;
} /* start_erq_demon() */

/*-------------------------------------------------------------------------*/
static void
shutdown_erq_demon (void)

/* Close the connection to the ERQ.
 * This method is to be used directly only on game shutdown, otherwise
 * use stop_erq_demon() instead.
 */

{
    if (erq_demon < 0)
        return;

    socket_close(erq_demon);
    erq_demon = FLAG_NO_ERQ;
    erq_pending_len = 0;
    input_from_erq = &buf_from_erq[0];
} /* shutdown_erq_demon() */

/*-------------------------------------------------------------------------*/
static void
stop_erq_demon (Bool notify)

/* Close the connection to the ERQ and inform all pending requests
 * about this. If <notify> is set, the hook H_ERQ_STOP is called.
 */

{
    erq_callback_t *erqp;
    int i;

    if (erq_demon < 0)
        return;

    shutdown_erq_demon();

    /* Inform all pending requests about the loss.
     */
    erqp = pending_erq;
    i = MAX_PENDING_ERQ;
    do {
        if (erqp->fun.type == T_CLOSURE)
        {
            *++inter_sp = erqp->fun;
            erqp->fun.type = T_INVALID;
            erqp->fun.u.generic = (void *)free_erq;
            free_erq = erqp;
            CLEAR_EVAL_COST;
            RESET_LIMITS;
            callback_master(STR_STALE_ERQ, 1);
        }
        erqp++;
    } while (--i);

    /* If desired, call H_ERQ_STOP to notify the situation.
     */
    if (notify)
    {
        RESET_LIMITS;
        CLEAR_EVAL_COST;
        if (driver_hook[H_ERQ_STOP].type == T_CLOSURE) {
            secure_callback_lambda(&driver_hook[H_ERQ_STOP], 0);
        }
    }
} /* stop_erq_demon() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_attach_erq_demon (svalue_t *sp)

/* EFUN: attach_erq_demon()
 *
 *   int attach_erq_demon(object ob, int do_close)
 *   int attach_erq_demon(string name, int do_close)
 *
 * In the first form, take away the connection from <ob> and store it as
 * _the_ erq connection. <ob> thus becomes a normal non-interactive object.
 * In the second form, try to start the ERQ demon from the path
 * 'ERQFILE<name>' (ERQFILE defaults to BINDIR/erq). <name> must not
 * contain '/..' sequences.
 *
 * If there is already an ERQ demon connected to the driver, the function
 * will fail unless <do_close> is set to 1 or any other odd integer; in
 * this case the connection to the old ERQ will be closed first.
 *
 * Return svalue.number 1 on success, 0 else.
 */

{
    object_t *ob;
    interactive_t *ip;
    string_t *suffix;

    /* Test for the first form: (object ob, int do_close) */
    if (sp[-1].type == T_OBJECT)
    {
        ob = sp[-1].u.ob;
        if (!O_SET_INTERACTIVE(ip, ob))
        {
            errorf("Bad arg 1 to attach_erq_demon(): object is not interactive.\n");
            /* NOTREACHED */
            return sp;
        }

        sp--;
        deref_object(ob, "attach_erq_demon");
        put_number(sp, 0);
        /* we need to read sp[1] below, thus don't overwrite it now. */
        if (privilege_violation4(STR_ATTACH_ERQ_DEMON,
            ob, 0, sp[1].u.number, sp+1))
        {
            if (erq_demon != FLAG_NO_ERQ) {
                if (sp[1].u.number & 1) {
                    stop_erq_demon(0);
                    erq_demon = FLAG_ERQ_STOP;
                } else {
                    return sp;
                }
            }
            erq_proto_demon = ip->socket;
            ip->do_close = FLAG_PROTO_ERQ;
            sp->u.number = 1;
        }
        return sp;
    }

    /* Otherwise the argument is a string */

    suffix = sp[-1].u.str;
    if (mstrstr(suffix, "/.."))
    {
        errorf("Bad arg 1 to attach_erq_demon(): illegal path.\n");
        /* NOTREACHED */
        return sp;
    }

    {
        int n;

        sp--;
        n = 0;
        if (privilege_violation4(STR_ATTACH_ERQ_DEMON,
            0, suffix, sp[1].u.number, sp+1))
        {
            if (erq_demon != FLAG_NO_ERQ)
            {
                if (sp[1].u.number & 1) {
                    stop_erq_demon(0);
                } else {
                    goto return_result;
                }
                erq_proto_demon = -1;
            }
            start_erq_demon(get_txt(suffix), mstrsize(suffix));
            n = 1;
        }
return_result:
        free_svalue(sp);
        put_number(sp, n);
        return sp;
    }

    /* NOTREACHED */
    return NULL;
} /* f_attach_erq_demon() */

/*-------------------------------------------------------------------------*/
static Bool
send_erq (int handle, int request, const char *arg, size_t arglen)

/* Send compose an ERQ message out of <handle>, <request> and <arg>
 * and send it to the ERQ. If all the data can't be sent now, the
 * next call to send_erq() will send the rest.
 *
 * Return FALSE if the data couldn't be sent, TRUE on success.
 *
 * SOCK_SEQPACKET is not portable enough, thus make special provisions
 * to deliver messages in an atomic fashion.
 */

{
    static char buf[ERQ_MAX_SEND], *pending;
    long wrote;

    if (erq_demon < 0)
        return MY_FALSE;

    /* Try to send the pending data */
    if (erq_pending_len)
    {
        wrote = socket_write(erq_demon, pending, erq_pending_len);
        if (wrote > 0) {
            pending += wrote;
            erq_pending_len -= wrote;
        }
        if (erq_pending_len)
            return MY_FALSE;
    }

    if (arglen + 9 > sizeof buf)
        return MY_FALSE;

    /* Create the message and add it to buf[] */
    erq_pending_len = arglen + 9;
    *(uint32*)buf = htonl(erq_pending_len);
    *(uint32*)(buf+4) = htonl(handle);
    buf[8] = (char)request;
    memcpy(buf + 9, arg, arglen);

    /* Send as much of buf[] as possible */
    pending = buf;
    wrote = socket_write(erq_demon, buf, erq_pending_len);
    if (wrote > 0) {
        pending += wrote;
        erq_pending_len -= wrote;
    }

    return MY_TRUE;
} /* send_erq() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_send_erq (svalue_t *sp)

/* EFUN: send_erq()
 *
 *   int send_erq(int request, string|int* data, closure callback)
 *
 * Send a request of type <request> and the data <data> to the ERQ>
 * If <callback> is set to a closure, it will be called with the
 * response from the ERQ.
 *
 * The function returns svalue.number 1 on success, and 0 on failure.
 *
 * The function causes a privilege violation "erq".
 */

{
    char  *arg;
    size_t arglen;
    erq_callback_t *new_erq;
    int i;
    p_int erq_request;

    /* Set arg with the data to send. */

    if (sp[-1].type == T_STRING) {
        arg = get_txt(sp[-1].u.str);
        arglen = mstrsize(sp[-1].u.str);
    }
    else /* it's a pointer */
    {
        vector_t *v;
        svalue_t *svp;
        char *cp;
        p_int j;

        v = sp[-1].u.vec;
        arglen = VEC_SIZE(v);
        cp = arg = xalloc(arglen);
        if (!arg) {
            errorf("Out of memory (%zu bytes) in send_erq() for allocating "
                   "temporary buffer.\n", arglen);
        }
        svp = &v->item[0];
        for (j = (p_int)arglen; --j >= 0; )
            *cp++ = (char)(*svp++).u.number;
    }

    erq_request = sp[-2].u.number;

    /* Test if this call is allowed. */

    if (!privilege_violation4(STR_SEND_ERQ, 0, STR_EMPTY
                             , erq_request & (~ERQ_CB_STRING)
                             , sp))
    {
        goto failure;
    }

    /* Store the callback closure. If none is given, use the
     * default callback.
     */

    new_erq = NULL;

    if (sp->type == T_NUMBER) { /* it's the number 0 */
        new_erq = &pending_erq[MAX_PENDING_ERQ];
        new_erq->fun.u.generic = (void *)free_erq;
    }
    else if (sp->type == T_CLOSURE
          && sp->x.closure_type != CLOSURE_UNBOUND_LAMBDA)
    {
        new_erq = free_erq;
    }

    /* Send the request and make up the result. */

    if (new_erq
     && 0 != (i = send_erq(new_erq - pending_erq, erq_request & (~ERQ_CB_STRING)
                          , arg, arglen))
       )
    {
        free_erq = (erq_callback_t *)new_erq->fun.u.generic;
        new_erq->fun = *sp;
        new_erq->string_arg = (erq_request & ERQ_CB_STRING) != 0;
    }
    else
    {
failure:
        i = 0;
        free_svalue(sp);
    }
    /* cleanup */
    if (sp[-1].type != T_STRING) {
        /* free arg only if sp-1 is not a string */
        xfree(arg);
    }
    free_svalue(--sp);
    (*--sp).u.number = i;

    return sp;
} /* f_send_erq() */

/*-------------------------------------------------------------------------*/
static long
read_32 (char *str)

/* Read a 32 bit value from a possibly unaligned network byte order
 * representation.
 */

{
    unsigned char *p = (unsigned char *)str;

    return (long)p[0]<<24 | (long)p[1]<<16 | (long)p[2]<<8 | p[3];
}

/*-------------------------------------------------------------------------*/
static void
add_ip_entry (struct in_addr addr, const char *name)

/* Add a new IP address <addr>/hostname <name> pair to the cache iptable[].
 * If the <addr> already exists in the table, replace the old tabled name
 * with the new one.
 */

{
    int i, ix;
    Bool new_entry;

    ix = -1;
    new_entry = MY_FALSE;
    for (i = 0; i < IPSIZE; i++)
    {
        if (!memcmp(&(iptable[i].addr.s_addr), &addr.s_addr, sizeof(iptable[i].addr.s_addr)))
        {
            ix = i;
            break;
        }
    }

    if (ix < 0)
    {
        ix = ipcur;
        new_entry = MY_TRUE;
    }

    iptable[ix].addr = addr;
    if (iptable[ix].name)
        free_mstring(iptable[ix].name);
    iptable[ix].name = new_tabled(name);

    if (new_entry)
        ipcur = (ipcur+1) % IPSIZE;
} /* add_ip_entry() */

/*-------------------------------------------------------------------------*/
#ifdef USE_IPV6

static void
update_ip_entry (const char *oldname, const char *newname)

/* Change the IP name <oldname> in the iptable[] to <newname>.
 * If the <oldname> is not in the table, nothing happens.
 */

{
    int i, ix;
    Bool new_entry;

    ix = -1;
    new_entry = MY_FALSE;
    for (i = 0; i < IPSIZE; i++)
    {
        if (iptable[i].name
         && !strncmp(get_txt(iptable[i].name), oldname, mstrsize(iptable[i].name))
           )
        {
            free_mstring(iptable[i].name);
            iptable[i].name = new_tabled(newname);
        }
    }
} /* update_ip_entry() */

/*-------------------------------------------------------------------------*/
static int
open_ipv6_conn( const char *hostname, const unsigned short int port
              , struct sockaddr_in * pTarget)

/* Create a non-blocking IPv6/IPv4 tcp connnection to the given
 * <hostname>:<port>. The <hostname> is first interpreted as IPv6
 * address, and if that fails, as IPv4 address.
 *
 * Result is the socket (with the connnection possibly still in process
 * of being opened), or -1 on a failure.
 *
 * The completed sockaddr_in is passed back in *<pTarget> as well.
 *
 * WARNING: Not threadsafe!
 */

{
    int sock;
    int con = 0;
    int fd_flags;
#ifdef __BEOS__
    const int bos = 1;
#endif
    struct hostent *h;
    struct protoent *p;
    struct sockaddr_in6 addr;

    p = getprotobyname("TCP");
    if(!p) return -1;

    sock = socket(AF_INET6, SOCK_STREAM, p->p_proto);
    if(sock == -1)
    {
        perror("socket");
        return -1;
    }
    endprotoent();
#ifdef __BEOS__
    if (setsockopt(sock, SOL_SOCKET, SO_NONBLOCK, &bos, sizeof(bos)))
#else
        fd_flags = fcntl(sock, F_GETFL, 0);
#  if defined(O_NONBLOCK)
    fd_flags |= O_NONBLOCK;
#  elif defined(O_NDELAY)
    fd_flags |= O_NDELAY;
#  elif defined(FNDELAY)
    fd_flags |= O_FNDELAY;
#  endif
    if (fcntl(sock, F_SETFL, fd_flags) == -1)
#endif
    {
        perror("setsockopt/fcntl");
        close(sock);
        return -1;
    }
    addr.sin6_port=htons(port);
    addr.sin6_family=AF_INET6;
    addr.sin6_flowinfo=0;
    addr.sin6_scope_id=IPV6_ADDR_SCOPE_GLOBAL;

    h = gethostbyname2(hostname, AF_INET6);
    if(h)
    {
        memcpy(&addr.sin6_addr, h->h_addr, h->h_length);
        con = connect(sock, (struct sockaddr *) &addr, sizeof(addr));
        perror("con");
    }
    else if(!h || (con && con != EINPROGRESS))
    {
        h = gethostbyname2(hostname, AF_INET);
        if(h)
        {
            CREATE_IPV6_MAPPED(&addr.sin6_addr, *((u_int32_t *)h->h_addr_list[0]));
            con = connect(sock, (struct sockaddr *) &addr, sizeof(addr));
        }
    }
    endhostent();

    if (pTarget)
        memcpy(pTarget, &addr, sizeof(*pTarget));
 
    return (!con || (con == -1 && errno == EINPROGRESS))
           ? sock
           : (close(sock),-1);
} /* open_ipv6_conn() */

#endif /* USE_IPV6 */

/*-------------------------------------------------------------------------*/
static string_t *
lookup_ip_entry (struct in_addr addr, Bool useErq)

/* Lookup the IP address <addr> and return an uncounted pointer to
 * a shared string with the hostname. The function looks first in the
 * iptable[], then, if not found there and <useErq> is true, asks the ERQ.
 * If the hostname can not be found, NULL is returned.
 */

{
    int i;
    string_t *ipname;
    struct in_addr tmp;

    /* Search for the address backwards from the last added entry,
     * hoping that its one of the more recently added ones.
     */
    i = ipcur;
    do {
        i--;
        if (i < 0)
            i += IPSIZE;

        if (!memcmp(&(iptable[i].addr.s_addr), &addr.s_addr, sizeof(iptable[i].addr.s_addr))
         && iptable[i].name)
        {
            return iptable[i].name;
        }
    } while (i != ipcur );

    /* The address is new to us.
     * Add a temporary entry into the iptable[] to bridge
     * the time until the erq has finished the lookup.
     * This also handles the case of an unresolvable hostname.
     */

    iptable[ipcur].addr = addr;
    if (iptable[ipcur].name)
        free_mstring(iptable[ipcur].name);

    memcpy(&tmp, &addr, sizeof(tmp));
#ifndef USE_IPV6
    ipname = new_tabled(inet_ntoa(tmp));
#else
    ipname = new_tabled(inet6_ntoa(tmp));
#endif

    iptable[ipcur].name = ipname;

    ipcur = (ipcur+1) % IPSIZE;

    /* If we have the erq and may use it, lookup the real hostname */
    if (erq_demon >= 0 && useErq)
    {
#ifndef USE_IPV6
        send_erq(ERQ_HANDLE_RLOOKUP, ERQ_RLOOKUP, (char *)&addr.s_addr, sizeof(addr.s_addr));
#else
        send_erq(ERQ_HANDLE_RLOOKUPV6, ERQ_RLOOKUPV6, get_txt(ipname)
                , mstrsize(ipname));
#endif
    }

    return iptable[ipcur].name;
}

#endif /* ERQ_DEMON */

/* End of ERQ Support */
/*=========================================================================*/

/*-------------------------------------------------------------------------*/
void
remove_stale_player_data (void)

/* GC and statistics support: Remove all input_to and prompt infos
 * referencing destructed objects.
 */

{
    int i;

    for(i = 0 ; i < MAX_PLAYERS; i++)
    {
        input_to_t * it, * prev;
        object_t *ob;

        if (all_players[i] == NULL)
            continue;

        /* Remove stale input_to data */
        for ( prev = NULL, it = all_players[i]->input_to; it != NULL; )
        {
            input_to_t *tmp;
            ob = callback_object(&(it->fun));
            if (ob)
            {
                prev = it;
                it = it->next;
            }
            else
            {
                /* The object has selfdestructed */

                if (prev == NULL)
                {
                    set_noecho(all_players[i]
                              , it->next ? it->next->noecho : 0
                              , it->next ? it->next->local : MY_FALSE
                              , MY_TRUE);
                    all_players[i]->input_to = it->next;
                }
                else
                {
                    prev->next = it->next;
                }

                tmp = it;
                it = it->next;

                free_input_to(tmp);
            }
        }

#ifdef USE_SNOOPING
        /* Remove stale snooping monsters */
        ob = all_players[i]->snoop_by;
        if (ob && !O_IS_INTERACTIVE(ob) && !check_object(ob))
        {
            free_object(ob, "remove_stale_player_data");
            all_players[i]->snoop_by = NULL;
        }
#endif

        /* Remove a stale modify_command object */
        ob = all_players[i]->modify_command;
        if (ob && !check_object(ob))
        {
            free_object(ob, "remove_stale_player_data");
            all_players[i]->modify_command = NULL;
        }
    } /* for (i) */
} /* remove_stale_player_data() */

/*-------------------------------------------------------------------------*/
size_t
show_comm_status (strbuf_t * sbuf, Bool verbose UNUSED)

/* Return the amount of memory used by the comm module.
 */

{
#if defined(__MWERKS__)
#    pragma unused(verbose)
#endif
    size_t sum;
    int i;

    remove_stale_player_data();

    sum = 0;

    for (i = 0; i <= max_player; i++)
    {
        interactive_t *pl;
        input_to_t *it;

        pl = all_players[i];
        if (!pl)
            continue;

        sum += sizeof(*pl);

        for (it = pl->input_to; it != NULL; it = it->next)
            sum += sizeof(*it);

#ifdef USE_BUILTIN_EDITOR
        sum += ed_buffer_size(O_GET_EDBUFFER(pl->ob));
#endif
#ifdef USE_PTHREADS
        {
            struct write_buffer_s *buf;

            interactive_lock(pl);
            for (buf = pl->write_first; buf != NULL; buf = buf->next)
            {
                sum += sizeof(*buf) - 1 + buf->length;
            }
            for (buf = pl->written_first; buf != NULL; buf = buf->next)
            {
                sum += sizeof(*buf) - 1 + buf->length;
            }
            if ((buf = pl->write_current) != NULL)
            {
                sum += sizeof(*buf) - 1 + buf->length;
            }
            interactive_unlock(pl);
        }
#endif
    }

    if (sbuf)
        strbuf_addf(sbuf, "Comm structures\t\t\t\t %9zu\n", sum);
    return sum;
} /* show_comm_status() */

#ifdef GC_SUPPORT

/*-------------------------------------------------------------------------*/
void
clear_comm_refs (void)

/* GC support: Clear all refs the module might have.
 */

{
    int i;

    for (i = 0; i < MAX_OUTCONN; i++)
    {
        if (outconn[i].status != ocNotUsed)
        {
            if (outconn[i].curr_obj)
                clear_object_ref(outconn[i].curr_obj);
	    num_pending_outconns--;
        }
    }

#ifdef ERQ_DEMON
    for (i = sizeof (pending_erq) / sizeof (*pending_erq); --i >= 0;)
    {
        clear_ref_in_vector(&pending_erq[i].fun, 1);
    }
#endif /* ERQ_DEMON */
} /* clear_comm_refs() */

/*-------------------------------------------------------------------------*/
void
count_comm_refs (void)

/* GC support: count any ref the module has.
 */

{
    int i;

    for (i = 0; i < MAX_OUTCONN; i++)
    {
        if (outconn[i].status != ocNotUsed)
        {
            if (outconn[i].curr_obj)
            {
                svalue_t sv;

                put_object(&sv, outconn[i].curr_obj);
                count_ref_in_vector(&sv, 1);
            }
        }
    }

#ifdef ERQ_DEMON
    for(i = 0; i < IPSIZE; i++) {
        if (iptable[i].name)
            count_ref_from_string(iptable[i].name);
    }

    for (i = sizeof (pending_erq) / sizeof (*pending_erq); --i >= 0;)
    {
        count_ref_in_vector(&pending_erq[i].fun, 1);
    }
#endif /* ERQ_DEMON */
} /* count_comm_refs() */

#endif /* GC_SUPPORT */


/*=========================================================================*/

/*-------------------------------------------------------------------------*/
static svalue_t *
query_ip_name (svalue_t *sp, Bool lookup)

/* Lookup the IP address (<lookup> is false) or IP hostname (<lookup> is
 * true) of object <sp> and return it. If <sp> is the number 0 or a
 * non-interactive object, the number 0 is returned.
 *
 * The hostname is read from the iptable[], so if it hasn't been
 * resolved yet, we return the number in any case.
 *
 * If <sp> is a reference to an interactive object, it will be replaced
 * on return with an array of integers with the full sockaddr_in:
 *    array[0.. 1]: sin_family
 *    array[2.. 3]: sin_port
 *    array[4.. 7]: sin_addr
 *    array[8..15]: undefined (ideally 0).
 *
 * The function is used to implement the efuns query_ip_number() and
 * query_ip_name().
 */

{
    object_t *ob;
    int i;
    interactive_t *ip;
    string_t *str;

    /* Set <ob> to the object passed on the stack. */

    if (sp->type != T_OBJECT)
    {
        svalue_t *svp;

        if (sp->type == T_NUMBER && !sp->u.number)
            return sp;
        svp = sp;
        while (svp->type == T_LVALUE || svp->type == T_PROTECTED_LVALUE)
            svp = svp->u.lvalue;
        if (svp->type != T_OBJECT)
        {
            errorf("Bad arg 1 to query_ip_number(): expected object/object&, got %s&.\n"
                 , typename(svp->type));
            /* NOTREACHED */
        }
        ob = svp->u.ob;
    }
    else
    {
        ob = sp->u.ob;
        deref_object(ob, "query_ip_name");
        sp->type = T_INVALID;
    }

    /* Return 0 for non-interactive objects */
    if (!(O_SET_INTERACTIVE(ip, ob)))
    {
        free_svalue(sp);
        put_number(sp, 0);
        return sp;
    }

    /* If the object was passed as reference, replace it with an array
     * with the full sockaddr_in.
     */
    if (sp->type == T_LVALUE)
    {
        svalue_t array, *svp;
        vector_t *v;
        char *cp;

        v = allocate_array(sizeof ip->addr);
        if (v)
        {
            put_array(&array, v);
            i = sizeof ip->addr;
            svp = v->item;
            cp = (char *)&ip->addr;
            do {
                svp->u.number = *cp++;
                svp++;
            } while(--i);
            transfer_svalue(sp, &array);
        }
        else
        {
            assign_svalue(sp, &const0);
        }

        return sp;
    }

    /* If the hostname is requested and we indeed have it in our table,
     * return it.
     */
    if (lookup)
    {
#ifdef ERQ_DEMON
        string_t * hname;

        hname = lookup_ip_entry(ip->addr.sin_addr, MY_FALSE);
        if (hname)
        {
            put_ref_string(sp, hname);
            return sp;
        }
#else
        /* The if(lookup) gets rid of a 'lookup unused' warning. */
#endif
    }

    /* Return the IP address as string.
     */

#ifndef USE_IPV6
    str = new_mstring(inet_ntoa(ip->addr.sin_addr));
#else
    str = new_mstring(inet6_ntoa(ip->addr.sin_addr));
#endif
    if (!str)
    {
        inter_sp = sp - 1;
        errorf("Out of memory for IP address\n");
    }
    put_string(sp, str);
    return sp;
} /* query_ip_number() */

/*-------------------------------------------------------------------------*/
char *
query_host_name (void)

/* Return the hostname (and just the hostname, not the full domain name).
 * The result is a pointer to a static array!
 * Called by lex.c, main.c and swap.c .
 */

{
    return host_name;
} /* query_host_name() */

/*-------------------------------------------------------------------------*/
char *
get_host_ip_number (void)

/* Return the IP address of the host.
 * The result is a newly allocated string.
 * Called by lex.c .
 */

{
#ifndef USE_IPV6
    char buf[INET_ADDRSTRLEN+3];

    sprintf(buf, "\"%s\"", inet_ntoa(host_ip_number));
#else
    char buf[INET6_ADDRSTRLEN+3];

    sprintf(buf, "\"%s\"", inet6_ntoa(host_ip_number));
#endif
    return string_copy(buf);
} /* query_host_ip_number() */

#ifdef USE_SNOOPING
/*-------------------------------------------------------------------------*/
svalue_t *
f_query_snoop (svalue_t *sp)

/* EFUN: query_snoop()
 *
 *   object query_snoop(object victim)
 *
 * Return the object which is snooping <victim>, or 0 if there is none.
 * The call must be allowed by master->valid_query_snoop().
 */

{
    svalue_t *arg1;
    object_t *ob;

    /* Do some test and set ob to the snooper (if any) */
    switch (0) /* try {...} */
    {
      default:
        ob = sp->u.ob;
        if ((ob->flags & (O_DESTRUCTED|O_SHADOW)) != O_SHADOW
         || O_GET_SHADOW(ob)->ip == NULL)
        {
            zero_object_svalue(sp);
            return sp;
        }
        inter_sp = sp;
        assert_master_ob_loaded();
        if (current_object != master_ob)
        {
            assign_eval_cost();
            arg1 = apply_master(STR_VALID_QSNOOP, 1);
            if (arg1 == 0 || arg1->type != T_NUMBER || !arg1->u.number)
            {
                ob = NULL;
                break;
            }
        }
        else
        {
            deref_object(ob, "query_snoop");
        }
        ob = O_GET_INTERACTIVE(ob)->snoop_by;
    }

    /* Return the result */
    if (ob)
        put_ref_object(sp, ob, "query_snoop");
    else
        put_number(sp, 0);
    return sp;
} /* f_query_snoop() */
#endif

/*-------------------------------------------------------------------------*/
svalue_t *
f_query_idle (svalue_t *sp)

/* EFUN: query_idle()
 *
 *   int query_idle(object ob)
 *
 * Return how many seconds a user object has been idle.
 */

{
    int i;
    object_t *ob;

    ob = sp->u.ob;
    if (!O_IS_INTERACTIVE(ob))
    {
        inter_sp = sp;
        errorf("query_idle() of non-interactive object.\n");
        return sp;
    }

    i = current_time - O_GET_INTERACTIVE(ob)->last_time;
    deref_object(ob, "query_idle");
    put_number(sp, i);
    return sp;
} /* f_query_idle() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_remove_interactive (svalue_t *sp)

/* EFUN: remove_interactive()
 *
 *   void remove_interactive(object ob)
 *
 * Close the connection to the interactive object ob.
 *
 * In fact, the connection is only flushed and marked for closing,
 * as a remove_interactive() here can upset some other code.
 * The actual remove will be done by get_message().
 */

{
    interactive_t *victim;

    if (O_SET_INTERACTIVE(victim, sp->u.ob)
     && !victim->closing
     && !victim->do_close)
    {
        if (victim->message_length) {
            command_giver = victim->ob;
            add_message(message_flush);

            /* message_flush takes always directly effect on the
             * socket. No apply() is involved.
             */
        }
        victim->do_close = FLAG_DO_CLOSE;
    }
    free_svalue(sp);
    return sp - 1;
} /* f_remove_interactive() */

/*-------------------------------------------------------------------------*/
#ifdef USE_PARANOIA

void
count_comm_extra_refs (void)

/* Count all the refs to verify the normal refcounting. */

{
    int i;

#ifdef ERQ_DEMON
    for (i = sizeof(pending_erq) / sizeof(*pending_erq); --i >= 0; )
        count_extra_ref_in_vector(&pending_erq[i].fun, 1);
#endif /* ERQ_DEMON */

    for (i = 0; i < MAX_PLAYERS; i++)
    {
        object_t *ob;
        input_to_t *it;

        if (all_players[i] == 0)
            continue;
        all_players[i]->ob->extra_ref++;
#ifdef USE_SNOOPING
        if ( NULL != (ob = all_players[i]->snoop_by) ) {
            interactive_t *ip;

	    /* this was current_object in ldmud distro */
            if (!(O_SET_INTERACTIVE(ip, ob)))
            {
                /* snooping monster */
                ob->extra_ref++;
            }
        } /* end of snoop-processing */
#endif

        for ( it = all_players[i]->input_to; it; it = it->next)
        {
            count_callback_extra_refs(&(it->fun));
            count_extra_ref_in_vector(&it->prompt, 1);
        }
        if ( NULL != (ob = all_players[i]->modify_command) )
            count_extra_ref_in_object(ob);
        count_extra_ref_in_vector(&all_players[i]->prompt, 1);
    }
} /* count_comm_extra_refs() */

#endif /* DEBUG */

/*-------------------------------------------------------------------------*/
svalue_t *
f_send_udp (svalue_t *sp)

/* EFUN: send_udp()
 *
 *   int send_udp(string host, int port, string message)
 *   int send_udp(string host, int port, int * message)
 *
 * Sends The message in an UDP packet to the given host and port
 * number. Causes a privilege violation.
 * The message can be given either as string, or as array of
 * bytes. The latter variant allows to send binary data as well.
 * Returns 1 on success, 0 on failure.
 *
 * Note: On some machines a failed send_imp() will not be registered
 * until the next send_imp() - the latter one might return '0' even
 * if itself was successful.
 */

{
    char *to_host = NULL;
    int to_port;
    char *msg;
    size_t msglen;
#ifndef USE_IPV6
    int ip1, ip2, ip3, ip4;
#endif /* USE_IPV6 */
    struct sockaddr_in name;
    struct hostent *hp;
    int ret = 0;
    svalue_t *firstarg; /* store the first argument */
    
    firstarg = sp-2;  /* hostname */
    
    switch(0) { default: /* try {...} */

        /* Set msg/msglen to the data of the message to send */

        if (sp->type == T_STRING)
        {
            msg = get_txt(sp->u.str);
            msglen = mstrsize(sp->u.str);
        }
        else /* it's an array */
        {
            vector_t *v;
            svalue_t *svp;
            char *cp;
            p_int j;

            v = sp->u.vec;
            msglen = VEC_SIZE(v);
            /* allocate memory and push error handler onto stack */
            cp = msg = xalloc_with_error_handler(msglen);
            if (!msg) {
                errorf("Out of memory (%zu bytes) in send_udp() for "
                       "temporary buffer.\n", msglen);
            }
            sp = inter_sp;
            
            svp = &v->item[0];
            for (j = (p_int)msglen; --j >= 0; )
                *cp++ = (char)(*svp++).u.number;
        }

        /* Is this call valid? */

        if (!privilege_violation(STR_SEND_UDP, firstarg, firstarg + 2))
            break;

        if (udp_s < 0)
            break;

        /* Determine the destination address */

        {
            size_t adrlen;

            adrlen = mstrsize(firstarg->u.str);
            /* as there are no runtime error raised below, we just xallocate
             * and don't bother with an error handler. */
            to_host = xalloc(adrlen+1);
            if (!to_host)
            {
                errorf("Out of memory (%zu bytes) in send_udp() for host address\n"
                     , (adrlen+1));
                /* NOTREACHED */
            }
            memcpy(to_host, get_txt(firstarg->u.str), adrlen);
            to_host[adrlen] = '\0';
        }
        to_port = (sp-1)->u.number;

#ifndef USE_IPV6
        if (sscanf(to_host, "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4) == 4)
        {
            name.sin_addr.s_addr = inet_addr(to_host);
            name.sin_family = AF_INET;
        }
        else
        {
            /* TODO: Uh-oh, blocking DNS in the execution thread */
            hp = gethostbyname(to_host);
            if (hp == 0)
                break;
            memcpy(&name.sin_addr, hp->h_addr, (size_t)hp->h_length);
            name.sin_family = AF_INET;
        }

#else /* USE_IPV6 */

        /* TODO: Uh-oh, blocking DNS in the execution thread */
        hp = gethostbyname2(to_host, AF_INET6);
        if (hp == 0) hp = gethostbyname2(to_host, AF_INET);
        if (hp == 0) break;
        memcpy(&name.sin_addr, hp->h_addr, (size_t)hp->h_length);

        if (hp->h_addrtype == AF_INET)
        {
            CREATE_IPV6_MAPPED(&name.sin_addr, (u_int32_t)hp->h_addr_list[0]);
        }
        name.sin_family = AF_INET6;
#endif /* USE_IPV6 */

        name.sin_port = htons(to_port);

        /* Send the message. */
#ifndef SENDTO_BROKEN
        if (sendto(udp_s, msg, msglen, 0,
               (struct sockaddr *)&name, sizeof(name)) != (int)msglen)
#endif
            break;
        ret = 1;
    }
    /* Cleanup - an allocated buffer for the message will be on the stack 
     * above the arguments, therefore clean everything from the first argument
     * (including) to sp.
     */
    sp = pop_n_elems((sp-firstarg)+1, sp);
    xfree(to_host);
    
    /*Return the result */
    sp++;
    put_number(sp, ret);
    return sp;
    
} /* f_send_udp() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_set_buffer_size (svalue_t *sp)

/* EFUN: set_buffer_size()
 *
 *   int set_buffer_size(int size)
 *
 * Changes the socket buffer size for this_interactive() to size,
 * up to a preconfigured maximum, result is the old buffer size
 * (or -1 on systems which aren't able to change the socket
 * buffer).
 * Modifying the buffer size may result in a better IO
 * throughput, but can also worsen it.
 */

{
    int new;

    /* Get the desired buffer size */

    if (sp->u.number > SET_BUFFER_SIZE_MAX)
    {
        errorf("Bad arg 1 to set_buffer_size(): value %"PRIdPINT" exceeds maximum %ld\n"
             , sp->u.number, (long) SET_BUFFER_SIZE_MAX);
        /* NOTREACHED */
        return sp;
    }
    new = sp->u.number;

    sp->u.number = -1; /* Default result */

#ifdef SO_SNDBUF
    {
        int old;
        length_t optlen;
        interactive_t *ip;

        if (!(O_SET_INTERACTIVE(ip, current_object))
         || ip->do_close)
        {
            return sp;
        }


        optlen = sizeof old;
        if (getsockopt(ip->socket, SOL_SOCKET, SO_SNDBUF, (char *)&old, &optlen) < 0)
            return sp;
        if (setsockopt(ip->socket, SOL_SOCKET, SO_SNDBUF, (char *)&new, sizeof new) < 0)
            return sp;
        sp->u.number = old;
    }
#endif /* SO_SNDBUF */

    return sp;
} /* f_set_buffer_size() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_binary_message (svalue_t *sp)

/* EFUN: binary_message()
 *
 *   int binary_message(int *|string message, int flags)
 *
 * Flush output and send output directly with write WITHOUT IAC QUOTING.
 * The message may contain zeroes if given as int *.
 * The messages goes to this_object(), but only if interactive.
 * Returned value: number of characters actually written.
 * Any 'allowed charset' setting is ignored.
 *
 * Flag settings are interpreted bitwise and may be ored
 * together:
 *
 *   Bit 0 (value 1): when set, add_message() is used instead of
 *     write(). Thus no previous flushing of the buffer is
 *     needed, but the output is not immediate, nor can the
 *     number of bytes actually sent be determined - the return
 *     value is undefined.
 *   Bit 1 (value 2): The buffer is flushed _after_ adding the
 *     message. Useful only in conjunction with Bit 0.
 *
 * The idea behind the flag settings is that sending command
 * codes for colours and other things needs to bypass the allowed
 * charset filters, but isn't important enough to waste bandwith
 * on a synchronous transmission.
 *
#ifdef USE_MCCP
 * If the client uses MCCP compression add_message ist always used
 * with flushing buffer _after_ the Message.
#endif
 */

{
    string_t *msg;
    mp_int wrote = 0, i;
    svalue_t *svp;
    interactive_t *ip;
    object_t *save_command_giver;

    /* Set message to the data to be sent, and size to its length. */

    if (sp[-1].type == T_POINTER)
    {
        char *p;
        size_t size;

        size = VEC_SIZE(sp[-1].u.vec);
        msg = alloc_mstring(size);
        if (!msg)
          errorf("Out of memory (%zu bytes) in binary_message()",size);
        for (i = (mp_int)size, svp = sp[-1].u.vec->item, p = get_txt(msg)
            ; --i >= 0; svp++)
        {
            if (svp->type != T_NUMBER)
            {
                free_mstring(msg);
                errorf("Bad arg 1 to binary_message(): got %s*, "
                      "expected string/int*.\n", typename(svp->type));
                /* NOTREACHED */
                return sp;
            }
            *p++ = (char)svp->u.number;
        }
    }
    else /* it's a string */
    {
        msg = ref_mstring(sp[-1].u.str);
    }

    /* Send the message */

    i = 0;
    if (O_SET_INTERACTIVE(ip, current_object)
     && !ip->do_close)
    {
        save_command_giver = command_giver;
        command_giver = current_object;

#ifdef USE_MCCP
        if ((sp->u.number & 1)||ip->out_compress)
#else
        if (sp->u.number & 1)
#endif
        {
            /* Write before flush... */

            sending_telnet_command = MY_TRUE; /* turn of IAC quoting */

            add_message(FMT_STRING, msg);

            sending_telnet_command = MY_FALSE;

#ifdef USE_MCCP
            if ((sp->u.number & 2)||ip->out_compress)
                add_message(message_flush);
#else
            if (sp->u.number & 2)
                add_message(message_flush);
#endif /* USE_MCCP */
            wrote = mstrsize(msg);
        }
        else
        {
            /* Flush, then write. */
            add_message(message_flush);

            /* Since all pending data was flushed, we can write directly
             * to the socket now.
             */

            for (i = 6; i > 0; i--) {
#ifdef USE_PTHREADS
                wrote = (mp_int)thread_socket_write(ip->socket, get_txt(msg)
                                                   , mstrsize(msg), ip, MY_TRUE);
#else
#ifdef USE_TLS
                if (ip->tls_status != TLS_INACTIVE)
                    wrote = (mp_int)tls_write(ip, get_txt(msg), mstrsize(msg));
                else
#endif /* USE_TLS */
                    wrote = (mp_int)socket_write(ip->socket, get_txt(msg), mstrsize(msg));
#endif
                if (wrote != -1 || errno != EINTR || i != 1)
                    break;
            }
            if (wrote == -1)
            {
                /* TODO: Use strerror()? */
                switch(errno)
                {
                case EINTR:
                    fprintf(stderr
                           , "%s comm: write EINTR. Message discarded.\n"
                           , time_stamp());
                    break;
                case EWOULDBLOCK:
                    fprintf(stderr,
                      "%s comm: write EWOULDBLOCK. Message discarded.\n"
                           , time_stamp());
                    break;
                case EMSGSIZE:
                    fprintf(stderr, "%s comm: write EMSGSIZE.\n"
                           , time_stamp());
                    break;
                default:
                    perror("write");
                    ip->do_close = FLAG_DO_CLOSE;
                    break;
                }
            }
        } /* if (type of write) */

        command_giver = save_command_giver;
    } /* end if interactive */

    sp--;
    free_mstring(msg);
    free_svalue(sp);
    put_number(sp, wrote);
    return sp;
} /* f_binary_message() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_exec (svalue_t *sp)

/* EFUN exec()
 *
 *   object exec(object new, object old)
 *
 * Switch the network connection from <old> to <new>. If <new> is already
 * interactive, its connection will be switched to <old>.
 *
 * It is used to load different "user objects" or to reconnect
 * link dead users.
 *
 * If <old> was command_giver, <news> will be the new command_giver.
 *
 * The call is validated by master->valid_exec() and returns 0 on
 * failure, and 1 on success.
 */

{
    int rc;
    object_t *ob;
    object_t *obfrom;

    rc = 0;

    ob = sp[-1].u.ob;
    obfrom = sp[0].u.ob;

    do {
        svalue_t *v;
        interactive_t *stale_interactive, *ip;
        object_t *save_command;

        /* Ask the master if this exec() is ok. */
        push_ref_string(inter_sp, current_prog->name);
          /* TODO: FinalFrontier suggests 'current_object->prog->name' */
        push_ref_object(inter_sp, ob, "exec");
        push_ref_object(inter_sp, obfrom, "exec");
        v = apply_master(STR_VALID_EXEC, 3);
        if (!v || v->type != T_NUMBER || v->u.number == 0)
            break;

        /* stale_interactive becomes the former interactive _if_ it
         * still is an interactive_t.
         */
        if (!(O_SET_INTERACTIVE(stale_interactive, ob)))
        {
            stale_interactive = NULL;
        }

        if (!(O_SET_INTERACTIVE(ip, obfrom)))
            errorf("Bad argument 2 to exec(): not interactive.\n");

        /* When we have to have an out of memory error, have it before pointers
         * get changed.
         */
        assert_shadow_sent(ob);

        save_command = command_giver;

        /* If <ob> has a connection, flush it */
        if (stale_interactive)
        {
            if (stale_interactive->message_length)
            {
                command_giver = ob;
                add_message(message_flush);
            }
        }

        /* Flush the connection of <obfrom> */

        if (ip->message_length) {
            command_giver = obfrom;
            add_message(message_flush);
        }
        command_giver = save_command;

#ifdef USE_SNOOPING
        /* Switch a possible snooper */
        if (ip->snoop_on)
            ip->snoop_on->snoop_by = ob;
#endif

        /* Switch the interactive */

        O_GET_INTERACTIVE(ob) = ip;
        O_GET_INTERACTIVE(obfrom) = NULL;
        ob->flags |= O_ONCE_INTERACTIVE;
        ip->ob = ob;
        ip->catch_tell_activ = MY_TRUE;

        if (stale_interactive)
        {
            /* Tie <ob>s stale connection to <obfrom>. */

            O_GET_INTERACTIVE(obfrom) = stale_interactive;
            stale_interactive->ob = obfrom;
#ifdef USE_SNOOPING
            if (stale_interactive->snoop_on)
                stale_interactive->snoop_on->snoop_by = obfrom;
#endif
            stale_interactive->catch_tell_activ = MY_TRUE;
        }
        else
        {
            /* Clean up <obfrom> after the loss of connection */

            obfrom->flags &= ~O_ONCE_INTERACTIVE;
            check_shadow_sent(obfrom);

            ref_object(ob, "exec");
            free_object(obfrom, "exec");
        }

        /* If this_player() or this_interactive() point to one of the
         * involved objects, switch it too.
         */
        if (obfrom == command_giver)
            command_giver = ob;
        else if (ob == command_giver)
            command_giver = obfrom;

        if (obfrom == current_interactive)
            current_interactive = ob;
        else if (ob == current_interactive)
            current_interactive = obfrom;

        rc = 1;
    }while(0);

    free_svalue(sp--);
    free_svalue(sp); /* object might have been destructed */
    put_number(sp, rc);

    return sp;
} /* f_exec() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_interactive (svalue_t *sp)

/* EFUN interactive()
 *
 *   int interactive(object ob)
 *
 * Return non-zero if ob, or when the argument is omitted, this
 * object(), is an interactive user. Will return 1 if the
 * object is interactive, else 0.
 */

{
    int i;
    object_t *ob;
    interactive_t *ip;

    ob = sp->u.ob;
    (void)O_SET_INTERACTIVE(ip, ob);
    i = ip && !ip->do_close;
    deref_object(ob, "interactive");
    put_number(sp, i);

    return sp;
} /* f_interactive() */

/*-------------------------------------------------------------------------*/
svalue_t *
v_input_to (svalue_t *sp, int num_arg)

/* EFUN input_to()
 *
 *   void input_to(string fun)
 *   void input_to(string fun, int flag, ...)
 *
 * Enable next line of user input to be sent to the local
 * function fun as an argument. The input line will not be
 * parsed, only when it starts with a "!" (like a kind of shell
 * escape) (this feature may be disabled).
 * The function <fun> may be static, but must not be private (or
 * it won't be found).
 *
 * Note that fun is not called immediately but after pressing the
 * RETURN key.
 *
 * If input_to() is called more than once in the same execution,
 * only the first call has any effect.
 *
 * The optional 3rd and following args will be passed as second and
 * subsequent args to the function fun. (This feature is was
 * added only recently, to avoid the need for global variables)
 */

{
    svalue_t *arg;       /* Pointer to the arguments of the efun */
    svalue_t *extra_arg; /* Pointer to the extra arguments of the efun */
    int iflags;          /* The flags passed to input_to() */
    int flags;           /* The flags as required for .noecho */
    input_to_t *it;
    int extra;           /* Number of extra arguments */
    int error_index;

    arg = sp - num_arg + 1;

    /* Extract the arguments */

    iflags = 0;
    extra = 0;
    extra_arg = arg + 1;

    if (arg[0].type != T_CLOSURE && arg[0].type != T_STRING)
    {
        vefun_bad_arg(1, sp);
        /* NOTREACHED */
        return NULL;
    }

    if (num_arg > 1)
    {
        if (arg[1].type != T_NUMBER)
        {
            vefun_bad_arg(2, sp);
            /* NOTREACHED */
            return NULL;
        }

        iflags = arg[1].u.number;
        extra = num_arg - 2;
        extra_arg = arg + 2;
    }

    {
        interactive_t *ip;

        if (command_giver != NULL
         && O_SET_INTERACTIVE(ip, command_giver)
         && !ip->tn_enabled
           )
        {
            if (!(iflags & INPUT_NOECHO) != !(ip->noecho & NOECHO_MASK))
            {
                warnf("input_to(): Change in NOECHO mode requested for object '%s' "
                      "with telnet disabled.\n"
                     , get_txt(command_giver->name)
                     );
            }

            if (!(iflags & INPUT_CHARMODE) != !(ip->noecho & CHARMODE_MASK)
             && (iflags & INPUT_NO_TELNET) == 0
               )
            {
                warnf("input_to(): Change in CHARMODE mode requested for object '%s' "
                      "with telnet disabled.\n"
                     , get_txt(command_giver->name)
                     );
            }
        }
    }

    /* Setup the flags required for 'noecho' */
    flags =   ((iflags & INPUT_NOECHO)      ? NOECHO_REQ   : 0)
            | ((iflags & INPUT_CHARMODE)    ? CHARMODE_REQ : 0)
            | ((iflags & INPUT_IGNORE_BANG) ? IGNORE_BANG  : 0)
          ;

    /* Check the arguments */

    if (iflags & INPUT_PROMPT)
    {
        if (num_arg <= 2)
        {
            errorf("Missing prompt argument to input_to().\n");
            /* NOTREACHED */
        }

        if (arg[2].type != T_STRING && arg[2].type != T_CLOSURE)
        {
            vefun_bad_arg(3, sp);
            /* NOTREACHED */
        }
    }

    if ((flags & IGNORE_BANG)
     && !privilege_violation4(STR_INPUT_TO, command_giver, 0, flags, sp))
    {
        do
        {
            free_svalue(sp--);
        } while (--num_arg);
        
        put_number(arg, 0); /* arg should equal sp+1 */
        return arg;
    }

    /* There is a chance that the privilege_violation() method destructed
     * the current object or the command_giver - return as if the call was
     * denied.
     */
    if (!check_object(current_object) || !check_object(command_giver))
    {
	do
	{
	    free_svalue(sp--);
	} while (--num_arg);
	
	put_number(arg, 0); /* arg should equal sp+1 */
	return arg;
    }

    /* Allocate and setup the input_to structure */

    xallocate(it, sizeof *it, "new input_to");
    init_empty_callback(&(it->fun));
    put_number(&(it->prompt), 0);

    /* If SET_PROMPT was specified, collect it */

    if (iflags & INPUT_PROMPT)
    {
        transfer_svalue(&(it->prompt), arg+2);
        extra--;
        extra_arg++;
    }

    /* Parse the extra args for the call */

    if (arg[0].type == T_STRING)
    {
        error_index = setup_function_callback(&(it->fun), current_object
                                             , arg[0].u.str
                                             , extra, extra_arg
                                             , MY_TRUE
                                             );
        free_string_svalue(arg);
    }
    else if (arg[0].type == T_CLOSURE)
        error_index = setup_closure_callback(&(it->fun), arg
                                            , extra, extra_arg
                                            , MY_TRUE
                                            );
    else
        error_index = 1;

    if (error_index >= 0)
    {
        free_input_to(it);
        vefun_bad_arg(error_index + num_arg - extra + 1, arg-1);
        /* NOTREACHED */
        return arg-1;
    }

    /* At this point the call back should be bound to an existing object - but
     * as a sanity check we test it.
     */
    if (NULL == callback_object(&(it->fun)))
    {
        free_input_to(it);
	put_number(arg, 0); /* arg should equal sp+1 */
	return arg;
    }

    /* Try setting the input_to. On success, return 1. */

    if (set_call( command_giver, it, (char)flags
                , (iflags & INPUT_NO_TELNET) != 0
                , (iflags & INPUT_APPEND) != 0
                )
       )
    {
        put_number(arg, 1);
        return arg;
    }

    /* input_to() was not allowed - return 0. */

    free_input_to(it);
    put_number(arg, 0);
    return arg;
} /* v_input_to() */

/*-------------------------------------------------------------------------*/
static void
free_input_to (input_to_t *it)

/* Deallocate the input_to structure <it> and all referenced memory.
 */

{
    free_callback(&(it->fun));
    free_svalue(&(it->prompt));
    xfree(it);
} /* free_input_to() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_query_input_pending (svalue_t *sp)

/* EFUN query_input_pending()
 *
 *   object query_input_pending(object ob)
 *
 * If ob is interactive and currently has an input_to() pending,
 * the object that has called the input_to() is returned,
 * else 0.
 */

{
    object_t *ob, *cb;
    interactive_t *ip;

    ob = sp->u.ob;
    if (O_SET_INTERACTIVE(ip, ob) && ip->input_to)
    {
        cb = callback_object(&(ip->input_to->fun));
        if (cb)
            sp->u.ob = ref_object(cb, "query_input_pending");
        else
            put_number(sp, 0);
    }
    else
    {
        put_number(sp, 0);
    }

    deref_object(ob, "query_input_pending");

    return sp;
} /* f_query_input_pending() */

/*-------------------------------------------------------------------------*/
svalue_t *
v_find_input_to (svalue_t *sp, int num_arg)

/* EFUN: find_input_to()
 *
 *   int find_input_to (object player, string|closure|object fun)
 *   int find_input_to (object player, object ob, string fun)
 *
 * Find the input_to most recently added to the interactive <player> object
 * matching the <fun> argument:
 *  - <fun> is a string: the input_to functionname has to match
 *  - <fun> is an object: the object the input_to function is bound to has
 *                        to match
 *  - <fun> is a closure: the input_to closure has to match.
 * If both <ob> and <fun> are specified, both the object and the function name
 * have to match.
 *
 * Return -1 if not found, or the position in the input_to stack (0 being
 * _least_ recently added input_to).
 */

{
    svalue_t *arg;  /* Pointer to the arguments of the efun */
    int       rc;   /* Resultvalue */

    arg = sp - num_arg + 1;

    if (num_arg > 2)
    {
        if (arg[1].type == T_OBJECT && num_arg > 2 && arg[2].type != T_STRING)
        {
            vefun_bad_arg(3, sp);
            /* NOTREACHED */
            return NULL;
        }

        if (arg[1].type != T_OBJECT
           )
        {
            vefun_bad_arg(2, sp);
            /* NOTREACHED */
            return NULL;
        }
    }

    /* Process the command, terminating out when possible */
    do {
        input_to_t    *it;
        interactive_t *ip;

        /* Get the interactive object.
         * If there is none, or if it is closing down or doesn't have
         * an input_to set, fail.
         */
        if (!(O_SET_INTERACTIVE(ip, arg[0].u.ob))
         || ip->closing || ip->input_to == NULL
           )
        {
            rc = -1;
            break;
        }

        /* Search for the right input_to */

        for ( it = ip->input_to
            ; it != NULL
            ; it = it->next)
        {
            Bool found = MY_FALSE;

            switch (arg[1].type)
            {
            case T_STRING:
                if (!it->fun.is_lambda
                 && mstreq(it->fun.function.named.name, arg[1].u.str))
                    found = MY_TRUE;
                break;

            case T_OBJECT:
                if (num_arg > 2)
                {
                    if (callback_object(&(it->fun)) == arg[1].u.ob
                     && !it->fun.is_lambda
                     && it->fun.function.named.name == arg[2].u.str
                       )
                        found = MY_TRUE;
                }
                else
                {
                    if (callback_object(&(it->fun)) == arg[1].u.ob)
                        found = MY_TRUE;
                }
                break;

            case T_CLOSURE:
                if (it->fun.is_lambda
                 && closure_eq(&(it->fun.function.lambda), arg+1))
                    found = MY_TRUE;
                break;

            default:
                /* TODO: Wouldn't errorf() suffice? */
                fatal("Unsupported argument type %d\n", arg[1].type);
                break;
            }

            if (found)
                break;
        }

        if (it != NULL)
        {
            /* We found the input_to: now count at which position it is */
            for ( rc = 0
                ; it->next != NULL
                ; it = it->next, rc++) NOOP ;
            break;
        }

        /* At this point, we didn't find the input_to */
        rc = -1;
    } while (0);

    /* Return the result */
    sp = pop_n_elems(num_arg, sp);
    sp++;
    put_number(sp, rc);

    return sp;
} /* f_find_input_to() */

/*-------------------------------------------------------------------------*/
svalue_t *
v_remove_input_to (svalue_t *sp, int num_arg)

/* EFUN: remove_input_to()
 *
 *   int remove_input_to (object player)
 *   int remove_input_to (object player, string|closure|object fun)
 *   int remove_input_to (object player, object ob, string fun)
 *
 * Remove a pending input_to from the interactive <player> object.
 * If the optional <fun> is not given, the most recently added input_to
 * is removed.
 *
 * If the optional <fun> is given, the efun tries to find and remove the
 * most recently added input_to matching the <fun> argument:
 *  - <fun> is a string: the input_to functionname has to match
 *  - <fun> is an object: the object the input_to function is bound to has
 *                        to match
 *  - <fun> is a closure: the input_to closure has to match.
 * If both <ob> and <fun> are specified, both the object and the function name
 * have to match.
 *
 * Return 1 on success, or 0 on failure (no input_to found, object is not
 * interactive or has no input_to pending).
 */

{
    svalue_t      *arg;  /* Pointer to the arguments of the efun */
    int            rc;   /* Resultvalue */
    interactive_t *ip;
    Bool           removedFirst;

    /* Get the arguments */
    arg = sp - num_arg + 1;

    if (num_arg > 2)
    {
        if (arg[1].type == T_OBJECT && arg[2].type != T_STRING)
        {
            vefun_bad_arg(3, sp);
            /* NOTREACHED */
            return NULL;
        }
        if (arg[1].type != T_OBJECT)
        {
            vefun_bad_arg(2, sp);
            /* NOTREACHED */
            return NULL;
        }
    }


    /* Process the command, bailing out whenever necessary */
    do {
        input_to_t * prev;
        input_to_t    *it;

        removedFirst = MY_FALSE;

        /* Get the interactive object.
         * If there is none, or if it is closing down or doesn't have
         * an input_to set, fail.
         */
        if (!(O_SET_INTERACTIVE(ip, arg[0].u.ob))
         || ip->closing || ip->input_to == NULL
           )
        {
            rc = 0;
            break;
        }

        /* If no filter argument has been given, just remove
         * the first input to.
         */
        if (num_arg < 2)
        {
            it = ip->input_to;
            ip->input_to = it->next;
            free_input_to(it);
            ip->set_input_to = (ip->input_to != NULL);
            removedFirst = MY_TRUE;
            rc = 1;
            break;
        }

        /* There is a filter argument: search for the right input_to */

        for (prev = NULL, it = ip->input_to
            ; it != NULL
            ; prev = it, it = it->next)
        {
            Bool found = MY_FALSE;

            switch (arg[1].type)
            {
            case T_STRING:
                if (!it->fun.is_lambda
                 && mstreq(it->fun.function.named.name, arg[1].u.str))
                    found = MY_TRUE;
                break;

            case T_OBJECT:
                if (num_arg > 2)
                {
                    if (callback_object(&(it->fun)) == arg[1].u.ob
                     && !it->fun.is_lambda
                     && it->fun.function.named.name == arg[2].u.str
                       )
                        found = MY_TRUE;
                }
                else
                {
                    if (callback_object(&(it->fun)) == arg[1].u.ob)
                        found = MY_TRUE;
                }
                break;

            case T_CLOSURE:
                if (it->fun.is_lambda
                 && closure_eq(&(it->fun.function.lambda), arg+1))
                    found = MY_TRUE;
                break;

            default:
                /* TODO: Wouldn't errorf() suffice? */
                fatal("Unsupported argument type %d\n", arg[1].type);
                break;
            }

            if (found)
                break;
        }

        if (it != NULL)
        {
            /* We found the input_to: remove it */
            if (prev == NULL)
            {
                ip->input_to = it->next;
                removedFirst = MY_TRUE;
            }
            else
                prev->next = it->next;

            free_input_to(it);
            ip->set_input_to = (ip->input_to != NULL);
            rc = 1;
            break;
        }

        /* At this point, nothing worked: failure */
        rc = 0;
    } while (0);

    if (rc && removedFirst)
    {
        if (ip->noecho)
            ip->noecho |= NOECHO_STALE;
        set_noecho(ip, ip->input_to ? ip->input_to->noecho : ip->noecho
                     , ip->input_to ? ip->input_to->local : MY_FALSE
                     , MY_FALSE
                  );
    }

    /* Return the result */
    sp = pop_n_elems(num_arg, sp);
    push_number(sp, rc);

    return sp;
} /* v_remove_input_to() */

/*-------------------------------------------------------------------------*/
svalue_t  *
f_input_to_info (svalue_t *sp)

/* EFUN: input_to_info()
 *
 *   mixed * input_to_info (object player)
 *
 * Construct an array of all input_to's pending for this interactive <player>.
 * The first entry in the array is the least recently added input_to, the
 * last element the most recently added one.
 * Every item in the array is itself an array of 2 or more entries:
 *  0:   The object (only if the function is a string).
 *  1:   The function (string or closure).
 *  2..: The argument(s).
 */
{
    vector_t      *v;
    int            num_pending;
    input_to_t    *it;
    interactive_t *ip;

    /* Get the interactive object.
     * If there is none, or if it is closing down or doesn't have
     * an input_to set, the efun will return the empty array.
     */
    if (!(O_SET_INTERACTIVE(ip, sp->u.ob))
     || ip->closing || ip->input_to == NULL
       )
    {
        num_pending = 0;
    }
    else
    {
        /* Count the number of pending input_tos.
         */
        for ( num_pending = 0, it = ip->input_to
            ; it != NULL
            ; it = it->next, num_pending++) NOOP ;
    }

    /* Allocate the result arrray and fill it in */
    v = allocate_array(num_pending);

    if (num_pending > 0)
    {
        int i;

        for (i = num_pending, it = ip->input_to
            ; --i >= 0
            ; it = it->next
            )
        {
            vector_t *vv;
            object_t *ob;

            ob = callback_object(&(it->fun));
            if (!ob)
                continue;

            /* Get the subarray */

            vv = allocate_array(2 + it->fun.num_arg);

            if (it->fun.is_lambda)
            {
                if (it->fun.function.lambda.x.closure_type == CLOSURE_LFUN)
                    put_ref_object( vv->item
                                  , it->fun.function.lambda.u.lambda->function.lfun.ob
                                  , "input_to_info");
                else
                    put_ref_object(vv->item, ob, "input_to_info");
                assign_svalue_no_free(&vv->item[1], &it->fun.function.lambda);
            }
            else
            {
                put_ref_object(vv->item, ob, "input_to_info");
                put_ref_string(vv->item + 1, it->fun.function.named.name);
            }

            if (it->fun.num_arg > 0)
            {
                svalue_t *source, *dest;
                int nargs;

                nargs = it->fun.num_arg;
                if (nargs > 1)
                    source = it->fun.arg.u.lvalue;
                else
                    source = &(it->fun.arg);
                dest = &vv->item[2];
                do {
                    assign_svalue_no_free(dest++, source++);
                } while (--nargs);
            }

            put_array(v->item + i, vv);
        }
    }

    /* Return the result */
    free_svalue(sp);
    put_array(sp, v);

    return sp;
} /* f_input_to_info() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_query_ip_name (svalue_t *sp)

/* EFUN query_ip_name()
 *
 *   string query_ip_name(object ob)
 *
 * Give the ip-name for user the current user or for the optional
 * argument ob. An asynchronous process 'erq' is used to find
 * out these names in parallel. If there are any failures to find
 * the ip-name, then the ip-number is returned instead.
 */

{
    return query_ip_name(sp, MY_TRUE);
} /* f_query_ip_name() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_query_ip_number (svalue_t *sp)

/* EFUN query_ip_number()
 *
 *   string query_ip_number(object  ob)
 *   string query_ip_number(mixed & ob)
 *
 * Give the ip-number for the current user or the optional
 * argument ob.
 *
 * If ob is given as reference (and it must be a valid object
 * then), it will upon return be set to the struct sockaddr_in of
 * the queried object, represented by an array of integers, one
 * integer per address byte:
 *   ob[0.. 1]: sin_family
 *   ob[2.. 3]: sin_port
 *   ob[4.. 7]: sin_addr
 *   ob[8..15]: undefined.
 */

{
    return query_ip_name(sp, MY_FALSE);
} /* f_query_ip_number() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_query_mud_port (svalue_t *sp)

/* EFUN: query_mud_port()
 *
 * Returns the port number the parser uses for user connections.
 *
 *   int query_mud_port(void)
 *
 * If no argument is given, the port for this_player() is
 * returned. If this_player() is not existing or not interactive,
 * the first port number open for connections is returned.
 *
 *   int query_mud_port(object user)
 *   int query_mud_port(int num)
 *
 * If an user object is given, the port used for its connection
 * is returned.
 * If a positive number is given, the <num>th port number the
 * parser uses for connections is returned (given that there are
 * that many ports).
 * If -1 is given, the number of ports open for connections is
 * returned.
 */

{
    object_t *ob;
    interactive_t *ip;
    struct sockaddr_in addr;
    length_t length;

    length = sizeof(addr);

    if (sp->type == T_NUMBER)
    {
        if (sp->u.number < -1 || sp->u.number >= numports)
        {
            errorf("Bad arg 1 to query_mud_port(): value %"PRIdPINT" out of range.\n"
                 , sp->u.number);
            /* NOTREACHED */
        }
        sp->u.number = sp->u.number < 0 ? numports : port_numbers[sp->u.number];
        return sp;
    }

    ob = sp->u.ob;
    deref_object(ob, "query_ip_port");

    if ( !(O_SET_INTERACTIVE(ip, ob))) {
        put_number(sp, port_numbers[0]);
        return sp;
    }

    getsockname(ip->socket, (struct sockaddr *)&addr, &length);
    put_number(sp, ntohs(addr.sin_port));
    return sp;
} /* f_query_mud_port() */

/*-------------------------------------------------------------------------*/


static inline void translate_bit(char c, int i, int length, string_t *rc, unsigned int bitno)
/* static helper function to translatin bits to characters in get_charset */
{
  if (c & (1 << bitno))
    get_txt(rc)[length++] = (char)(i * 8 + bitno);
} /* translate_bit */

static void
get_charset (svalue_t * sp, p_int mode, char charset[32])

/* Translate the <charset> into an svalue and store it into <sp>:
 *   <mode> == CHARSET_VECTOR: result is a bitvector array
 *   <mode> == CHARSET_STRING: result is a string.
 */

{
    put_number(sp, 0);
    switch (mode)
    {
    default:
        fatal("(get_charset): Illegal mode value %"PRIdPINT"\n", mode);
        /* NOTREACHED */
        break;

    case CHARSET_VECTOR:
      {
        vector_t * rc;
        int i;

        rc = allocate_uninit_array(32);
        if (!rc)
        {
            outofmemory("result array");
            /* NOTREACHED */
            break;
        }

        for (i = 0; i < 32; i++)
            put_number(rc->item+i, (unsigned char)charset[i]);

        put_array(sp, rc);
        break;
      }

    case CHARSET_STRING:
      {
        string_t * rc;
        int length, i;

        /* Count the number of bits set in the charset */
        for (i = length = 0; i < 32; i++)
        {
            char c = charset[i];
            length +=   ((c & 0x80) ? 1 : 0)
                      + ((c & 0x40) ? 1 : 0)
                      + ((c & 0x20) ? 1 : 0)
                      + ((c & 0x10) ? 1 : 0)
                      + ((c & 0x08) ? 1 : 0)
                      + ((c & 0x04) ? 1 : 0)
                      + ((c & 0x02) ? 1 : 0)
                      + ((c & 0x01) ? 1 : 0);
        }

        rc = alloc_mstring(length);
        if (!rc)
        {
            outofmemory("result string");
            /* NOTREACHED */
            break;
        }

        /* Translate the bits into characters */
        for (i = length = 0; i < 32; i++)
        {
            char c = charset[i];

            translate_bit(c, i, length, rc, 0);
            translate_bit(c, i, length, rc, 1);
            translate_bit(c, i, length, rc, 2);
            translate_bit(c, i, length, rc, 3);
            translate_bit(c, i, length, rc, 4);
            translate_bit(c, i, length, rc, 5);
            translate_bit(c, i, length, rc, 6);
            translate_bit(c, i, length, rc, 7);
        }

        put_string(sp, rc);
        break;
      } /* case CHARSET_STRING */
    } /* switch(mode) */
} /* get_charset() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_get_combine_charset (svalue_t *sp)

/* TEFUN: get_combine_charset()
 *
 *   mixed get_combine_charset (int mode)
 *
 * Return the combine charset of the current interactive in the form requested
 * by <mode>:
 *   <mode> == CHARSET_VECTOR: return as bitvector
 *   <mode> == CHARSET_STRING: return as string
 *
 * The bitvector is interpreted as an array of 8-bit-values and might
 * contain up to 32 elements. Character n is "combinable"
 * if sizeof(bitvector) > n/8 && bitvector[n/8] & (1 << n%8) .
 *
 * If there is no current interactive, the function returns 0. 
 */

{
    p_int mode;
    interactive_t *ip;

    mode = sp->u.number;
    if (mode != CHARSET_VECTOR && mode != CHARSET_STRING)
    {
        errorf("Bad arg 1 to get_combine_charset(): %"PRIdPINT", "
              "expected CHARSET_VECTOR (%d) or CHARSET_STRING (%d)\n"
             , mode, CHARSET_VECTOR, CHARSET_STRING);
        /* NOTREACHED */
        return sp;
    }

    if (current_interactive && O_SET_INTERACTIVE(ip, current_interactive))
        get_charset(sp, mode, ip->combine_cset);
    else
        put_number(sp, 0);

    return sp;
} /* f_get_combine_charset() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_set_combine_charset (svalue_t *sp)

/* EFUN: set_combine_charset()
 *
 *   void set_combine_charset (int* bitvector)
 *   void set_combine_charset (string chars)
 *   void set_combine_charset (0)
 *
 * Set the set of characters which can be combined into a single string
 * when received en-bloc in charmode from the current interactive user.
 * Non-combinable characters and single received characters are returned
 * in separate strings as usual. The function must be called with the
 * interactive user being the command giver.
 *
 * The newline '\n' and the NUL character '\0' are always non-combinable.
 *
 * The charset can be given either directly as a string, or indirectly
 * as a bitvector. If the charset is given as the number 0, the default
 * charset is re-established.
 *
 * The bitvector is interpreted as an array of 8-bit-values and might
 * contain up to 32 elements. Character n is "combinable"
 * if sizeof(bitvector) > n/8 && bitvector[n/8] & (1 << n%8) .
 */

{
    mp_int i;
    svalue_t *svp;
    char *p;
    interactive_t *ip;

    i = 0;
    if (sp->type == T_POINTER && (i = (mp_int)VEC_SIZE(sp->u.vec)) > 32)
    {
        errorf("Bad arg 1 to set_combine_charset(): int[] too long (%"PRIdMPINT")\n"
             , i);
        /* NOTREACHED */
        return sp;
    }

    if (current_interactive && O_SET_INTERACTIVE(ip, current_interactive))
    {
        if (sp->type == T_NUMBER)
        {
            set_default_combine_charset(ip->combine_cset);
        }
        else if (sp->type == T_STRING)
        {
            memset(ip->combine_cset, 0, sizeof ip->combine_cset);
            for ( i = mstrsize(sp->u.str), p = get_txt(sp->u.str)
                ; i > 0
                ; i--, p++)
                ip->combine_cset[(*p & 0xff) / 8] |= 1 << (*p % 8);
        }
        else
        {
            /* i was set in the typecheck above */
            for ( svp = sp->u.vec->item, p = ip->combine_cset
                ; --i >= 0
                ; svp++, p++)
            {
                if (svp->type == T_NUMBER)
                    *p = (char)svp->u.number;
            }
            memset(p, 0, (size_t)(&ip->combine_cset[sizeof ip->combine_cset] - p));
        }

//        ip->combine_cset['\n'/8] &= ~(1 << '\n' % 8);
        ip->combine_cset['\0'/8] &= ~(1 << '\0' % 8);
    }
    free_svalue(sp);
    sp--;
    return sp;
} /* f_set_combine_charset() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_get_connection_charset (svalue_t *sp)

/* TEFUN: get_connection_charset()
 *
 *   mixed get_connection_charset (int mode)
 *
 * Return the combine charset of the current interactive in the form requested
 * by <mode>:
 *   <mode> == CHARSET_VECTOR: return as bitvector
 *   <mode> == CHARSET_STRING: return as string
 *
 * Alternatively, the status of the IAC quoting can be returned:
 *   <mode> == CHARSET_QUOTE_IAC: return 0 if IACs are not quoted,
 *                                return 1 if they are.
 *
 * The bitvector is interpreted as an array of 8-bit-values and might
 * contain up to 32 elements. Character n is "combinable"
 * if sizeof(bitvector) > n/8 && bitvector[n/8] & (1 << n%8) .
 *
 * If there is no current interactive, the function returns 0. 
 */

{
    p_int mode;
    interactive_t *ip;

    mode = sp->u.number;
    if (mode != CHARSET_VECTOR && mode != CHARSET_STRING
     && mode != CHARSET_QUOTE_IAC)
    {
        errorf("Bad arg 1 to get_connection_charset(): %"PRIdPINT", "
              "expected CHARSET_VECTOR (%d), _STRING (%d), "
              "or _QUOTE_IAC (%d)\n"
             , mode, CHARSET_VECTOR, CHARSET_STRING, CHARSET_QUOTE_IAC);
        /* NOTREACHED */
        return sp;
    }

    if (current_interactive && O_SET_INTERACTIVE(ip, current_interactive))
    {
        if (mode == CHARSET_QUOTE_IAC)
            put_number(sp, ip->quote_iac != 0);
        else
            get_charset(sp, mode, ip->charset);
    }
    else
        put_number(sp, 0);

    return sp;
} /* f_get_connection_charset() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_set_connection_charset (svalue_t *sp)

/* EFUN: set_connection_charset()
 *
 *   void set_connection_charset (int* bitvector, int quote_iac)
 *   void set_connection_charset (string charset, int quote_iac)
 *   void set_connection_charset (0, int quote_iac)
 *
 * Set the set of characters that can be output to the interactive user
 * (this does not apply to binary_message() ). The function must be called
 * by the interactive user object itself.
 *
 * The charset can be given either directly as a string, or indirectly
 * as a bitvector. If the charset is given as 0, the default connection
 * charset is re-established.
 *
 * The bitvector is interpreted as an array of 8-bit-values and might
 * contain up to 32 elements. Character n is allowed to be output
 * if sizeof(bitvector) > n/8 && bitvector[n/8] & (1 << n%8) .
 *
 * If quote_iac is 0 and char 255 is allowed to be output, IAC
 * will be output unmodified.
 * If quote_iac is 1 and char 255 is allowed to be output,
 * char 255 will be quoted so that it is not interpreted as IAC
 * by the telnet protocol.
 */

{
    mp_int i;
    svalue_t *svp;
    char *p;
    interactive_t *ip;

    i = 0;
    if (sp[-1].type == T_POINTER && (i = (mp_int)VEC_SIZE(sp[-1].u.vec)) > 32)
    {
        errorf("Bad arg 1 to set_connection_charset(): array too big (%"
             PRIdMPINT")\n", i);
        /* NOTREACHED */
        return sp;
    }

    if (O_SET_INTERACTIVE(ip, current_object))
    {
        if (sp[-1].type == T_NUMBER)
        {
            set_default_conn_charset(ip->charset);
        }
        else if (sp[-1].type == T_STRING)
        {
            memset(ip->charset, 0, sizeof ip->charset);
            for ( i = mstrsize((sp-1)->u.str), p = get_txt(sp[-1].u.str)
                ; i > 0
                ; i--, p++)
                ip->charset[(*p & 0xff) / 8] |= 1 << (*p % 8);
        }
        else
        {
            /* i was set in the typecheck above */
            for ( svp = sp[-1].u.vec->item, p = ip->charset
                ; --i >= 0
                ; svp++, p++)
            {
                if (svp->type == T_NUMBER)
                    *p = (char)svp->u.number;
            }
            memset(p, 0, (size_t)(&ip->charset[sizeof ip->charset] - p));
        }

        ip->charset['\n'/8] &= ~(1 << '\n' % 8);
        ip->charset['\0'/8] &= ~(1 << '\0' % 8);

        if ( 0 != (ip->quote_iac = (char)sp->u.number) )
        {
            if (ip->charset[IAC/8] & (1 << IAC % 8))
                ip->charset[IAC/8] &= ~(1 << IAC % 8);
            else
                ip->quote_iac = MY_FALSE;
        }
    }
    sp--;
    free_svalue(sp);
    sp--;
    return sp;
} /* f_set_connection_charset() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_set_prompt (svalue_t *sp)

/* EFUN set_prompt()
 *
 *       string set_prompt(mixed prompt, object ob)
 *
 * Set the prompt given by the first argument for the interactive object
 * instead of the default ``> ''. If the second argument is omitted,
 * this_player() is used as default. The first arg can be a string or a
 * closure. If the <prompt> arg is 0, the prompt is not changed.
 * TODO: Remove the acceptance of -1 here.
 *
 * The result returned is the old prompt.
 */

{
    svalue_t *prompt;
    interactive_t *ip;

    /* Make sure the object is interactive */
    if (!(O_SET_INTERACTIVE(ip, sp->u.ob))
     || ip->closing)
    {
        errorf("Bad arg 2 to set_prompt(): object not interactive.\n");
        return sp;
    }

    /* Get the address of the prompt svalue */
    prompt = &O_GET_INTERACTIVE(sp->u.ob)->prompt;

    free_object_svalue(sp);
    sp--;

    if (sp->type == T_STRING || sp->type == T_CLOSURE)
    {
        if (sp->type == T_CLOSURE && sp->x.closure_type == CLOSURE_UNBOUND_LAMBDA)
        {
            inter_sp = sp;
            errorf("Bad arg 1 for set_prompt(): lambda closure not bound\n");
            /* NOTREACHED */
        }

        if (sp->type == T_STRING)
        {
            string_t *str = make_tabled_from(sp->u.str);

            if (!str)
            {
                inter_sp = sp;
                errorf("(set_prompt) Out of memory (%zu bytes) for prompt\n"
                     , mstrsize(sp->u.str));
            }
            else
            {
                free_mstring(sp->u.str);
                sp->u.str = str;
            }
        }

        /* Three-way exchange to set the new prompt and put
         * the old one onto the stack.
         */
        sp[1] = *prompt;
        *prompt = *sp;
        *sp = sp[1];
    }
    else /* It's a number */
    {
        if (sp->u.number == 0 || sp->u.number == -1)
            assign_svalue(sp, prompt);
        else
        {
            errorf("Bad int arg 1 to set_prompt(): got %"PRIdPINT", expected 0 or -1.\n"
                 , sp->u.number);
            /* NOTREACHED */
            return sp;
        }
    }

    return sp;
} /* f_set_prompt() */

/*-------------------------------------------------------------------------*/
#ifdef USE_SNOOPING
svalue_t *
v_snoop (svalue_t *sp, int num_arg)

/* EFUN snoop()
 *
 *   object snoop(object snooper)
 *   object snoop(object snooper, object snoopee)
 *
 * Starts a snoop from 'snooper' on 'snoopee', or if 'snoopee' is not
 * given, terminates any snoop from 'snooper'.
 * On success, 'snoopee' is returned, else 0.
 *
 * The snoop is checked with the master object for validity.
 * It will also fail if the 'snoopee' is being snooped already or
 * if a snoop would result in a recursive snoop action.
 */

{
    int i;

    if (num_arg == 1)
    {
        i = set_snoop(sp->u.ob, 0);
    }
    else
    {
        i = set_snoop((sp-1)->u.ob, sp->u.ob);
        free_svalue(sp--);
    }
    free_svalue(sp);
    put_number(sp, i);

    return sp;
} /* v_snoop() */
#endif

/*-------------------------------------------------------------------------*/
svalue_t *
f_users (svalue_t *sp)

/* EFUN users()
 *
 * Return a (possibly empty) vector of all interactive user objects.
 */

{
    object_t *ob;
    int n, num;
    vector_t *ret;
    interactive_t **user;
    svalue_t *svp;

    /* Count the active users */
    num = 0;
    user = all_players;
    for (n = max_player + 2; --n; user++)
    {
        if (*user && !((*user)->ob->flags & O_DESTRUCTED))
            num++;
    }

    /* Get the result array and fill it */

    ret = allocate_array(num);
    svp = ret->item;
    user = all_players;
    for (n = max_player + 2; --n; user++)
    {
        if (*user && !((ob = (*user)->ob)->flags & O_DESTRUCTED))
        {
            put_ref_object(svp, ob, "users");
            svp++;
        }
    }

    push_array(sp, ret);

    return sp;
} /* f_users() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_query_udp_port (svalue_t *sp)

/* EFUN query_udp_port()
 *
 *   int query_udp_port(void)
 *
 * Returns the port number that is used for the inter mud
 * protocol.
 */

{
    push_number(sp, udp_port);

    return sp;
} /* f_query_udp_port() */

/*-------------------------------------------------------------------------*/
#ifdef USE_SOCKET_LIMITS

svalue_t *
f_get_max_commands (svalue_t *sp)

/* TEFUN: get_max_commands()
 *
 *   int get_max_commands ()
 *   int get_max_commands (object obj)
 *
 * Return the max number of commands (read: line resp. char inputs) the
 * interactive <obj> (default is the current interactive) is allowed to
 * execute per second. A negative result means 'unlimited'.
 * For non-interactive objects the result is 0.
 */

{
    p_int rc;
    interactive_t *ip;

    rc = 0;
    if (O_SET_INTERACTIVE(ip, sp->u.ob))
        rc = ip->maxNumCmds;

    free_svalue(sp);
    put_number(sp, rc);

    return sp;
} /* f_get_max_commands() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_set_max_commands (svalue_t *sp)

/* TEFUN: set_max_commands()
 *
 *   void set_max_commands (int num)
 *   void set_max_commands (int num, object obj)
 *
 * Set the max number of commands (read: line resp. char inputs) the
 * interactive <obj> (default is the current interactive) is allowed to
 * execute per second to <num>. A negative result means 'unlimited'.
 * For non-interactive objects the function raises an error.
 *
 * The function raises a privilege_violation ("set_max_commands", obj, num).
 * If the privilege is denied, the call is ignored.
 */

{
    p_int num;
    interactive_t *ip;

    num = sp[-1].u.number;
    if (num < 0)
        num = -1;

    if (!O_SET_INTERACTIVE(ip, sp->u.ob))
    {
        errorf("Bad arg 2 to set_max_commands(): Object is not interactive.\n");
        /* NOTREACHED */
    }

    if (privilege_violation4(STR_SET_MAX_CMDS, sp->u.ob, NULL, num, sp))
        ip->maxNumCmds = num;

    free_svalue(sp--);
    free_svalue(sp--);
    return sp;
} /* f_set_max_commands() */
#endif

/*-------------------------------------------------------------------------*/
svalue_t *
f_enable_telnet (svalue_t *sp)

/* TEFUN: enable_telnet()
 *
 *   int enable_telnet (int num)
 *   int enable_telnet (int num, object obj)
 *
 * Enable or disable the telnet machine for the interactive object <obj>.
 * Return the previous state of the telnet machine as result.
 *
 * <num> > 0 : enable telnet machine (default)
 *       = 0 : disable telnet machine
 *       < 0 : just query the current state of the telnet machine.
 * <obj> : the interactive object, default is the current interactive.
 *         For non-interactive objects the function raises an error.
 *
 * The function raises a privilege_violation ("enable_telnet", obj, num)
 * if <num> is >= 0. If the privilege is denied, the call is ignored.
 *
 * WARNING: Careless use of this efun can cause great confusion for both
 * driver and clients!
 */

{
    p_int num;
    p_int rc;
    interactive_t *ip;

    num = sp[-1].u.number;
    if (num < 0)
        num = -1;

    if (!O_SET_INTERACTIVE(ip, sp->u.ob))
    {
        errorf("Bad arg 2 to enable_telnet(): Object '%s' is not interactive.\n"
             , get_txt(sp->u.ob->name)
             );
        /* NOTREACHED */
        return sp; /* flow control hint */
    }

    rc = (ip->tn_enabled != 0);
    if (num >= 0
     && privilege_violation4(STR_ENABLE_TELNET, sp->u.ob, NULL, num, sp))
        ip->tn_enabled = (num != 0);

    free_svalue(sp--);
    free_svalue(sp);

    put_number(sp, rc);
    return sp;
} /* f_enable_telnet() */
 
/*-------------------------------------------------------------------------*/

svalue_t *
f_enable_binary (svalue_t *sp)

/* TEFUN: enable_binary()
 *
 *   int enable_binary (object obj)
 *
 * enable binary socket read for obj. Only do this if you know 
 * what you're doing. fippo 2008.
 */

{
    p_int rc;
    interactive_t *ip;

    if (!O_SET_INTERACTIVE(ip, sp->u.ob))
    {
        errorf("Bad arg 1 to enable_binary(): Object '%s' is not interactive.\n"
             , get_txt(sp->u.ob->name)
             );
        /* NOTREACHED */
        return sp; /* flow control hint */
    }

    rc = (ip->is_binary != MY_FALSE);
    //if (privilege_violation4(STR_ENABLE_TELNET, sp->u.ob, NULL, 1, sp))
    ip->is_binary = MY_TRUE;

    free_svalue(sp);

    put_number(sp, rc);
    return sp;
} /* f_enable_binary() */
 
/*-------------------------------------------------------------------------*/
void
check_for_out_connections (void)

/* Check the list of pending outgoing connections if the connections
 * are still pending.
 * Activate those who succeeded, remove those which failed.
 *
 * To be called regularily from the backend.
 */

{
    int i, ret;
    object_t *user;

    for (i = 0; i < MAX_OUTCONN; i++)
    {
        if (outconn[i].status == ocNotUsed)
            continue;

        if (!outconn[i].curr_obj) /* shouldn't happen */
        {
            socket_close(outconn[i].socket);
            outconn[i].status = ocNotUsed;
	    num_pending_outconns--;
            continue;
        }

        if (outconn[i].status == ocLoggingOn)
        {
            /* LPC logon threw an error - clean up */
            debug_message("%s Error in net_connect(): logon "
                          "object '%s' threw an error.\n"
                         , time_stamp()
                         , outconn[i].curr_obj
                           ? get_txt(outconn[i].curr_obj->name)
                           : "<null>"
                         );

            outconn[i].status = ocNotUsed;
	    num_pending_outconns--;
            if (outconn[i].curr_obj)
            {
                if (O_IS_INTERACTIVE(outconn[i].curr_obj))
                    remove_interactive(outconn[i].curr_obj, MY_FALSE);
                free_object(outconn[i].curr_obj, "net_connect");
            }
            socket_close(outconn[i].socket);
            continue;
        }

        if (outconn[i].curr_obj && (outconn[i].curr_obj->flags & O_DESTRUCTED))
        {
            socket_close(outconn[i].socket);
            free_object(outconn[i].curr_obj, "net_connect");
            outconn[i].status = ocNotUsed;
	    num_pending_outconns--;
            continue;
        }

        ret = connect(outconn[i].socket, (struct sockaddr*) &outconn[i].target
                     , sizeof(outconn[i].target));
        if (ret == -1)
        {
            switch(errno)
            {
            case EALREADY: /* still trying */
                continue;
            case EISCONN: /* we are connected! */
                break;
            default:
                /* Error with connection, call logon() with the failure flag
                 */
                outconn[i].status = ocLoggingOn;
                push_number(inter_sp, -1);
                logon_object(outconn[i].curr_obj);

                outconn[i].status = ocNotUsed;
		num_pending_outconns--;
                free_object(outconn[i].curr_obj, "net_connect");
                socket_close(outconn[i].socket);

                continue;
            }
        }

        /* connection successful */
        outconn[i].status = ocLoggingOn;

        user = command_giver;
        new_player( outconn[i].curr_obj, outconn[i].socket
                  , &outconn[i].target, sizeof(outconn[i].target), 0);
        command_giver = user;

        free_object(outconn[i].curr_obj, "net_connect");
        outconn[i].status = ocNotUsed;
	num_pending_outconns--;
    }
} /* check_for_out_connections() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_net_connect (svalue_t *sp)

/* EFUN net_connect()
 *   
 *   int net_connect(string host, int port)
 *
 * Open a non-blocking TCP network connection to <host> and <port>.
 * On success, the connection is bound to the current object and the
 * lfun logon() is called in the object.
 *
 * If the connection can't be established immediately, the efun returns
 * 'success' and the driver will check in the background for the progress
 * of the connection. When it is established, logon() will be called in
 * the object. If the connection fails, logon(-1) will be called in the
 * object.
 *
 * The efun raises a privilege violation ("net_connect", host, port).
 *
 * Return 0 on success, and a Unix ERRNO on failure.
 */

{
    char * host;
    int    port;
    int    rc;

    /* get the arguments */
    
    host = get_txt(sp[-1].u.str);
    port = sp->u.number;

    if (!privilege_violation4(STR_NET_CONNECT, NULL, sp[-1].u.str, port, sp))
    {
        sp = pop_n_elems(2, sp);
        push_number(sp, -1);
        return sp;
    }

    /* Try the connection */
    rc = 0;
    do{
        int d, n, ret;
        object_t *user;
        struct sockaddr_in target;

#ifndef USE_IPV6

        struct hostent *h;

#endif

        /* Find a place to store the pending connection,
         * store the index in n
         */
        Bool stored = MY_FALSE;
        for (n = 0; n < MAX_OUTCONN; n++)
        {
            if (outconn[n].status == ocNotUsed)
            {
                stored = MY_TRUE;
                break;
            }
        }

        if (!stored)
        {
	    /* TODO: This should not be an EMFILE. We should have a custom
	     * error code here so the mudlib can distinguish a real
	     * EMFILE from a temporary exhaustion of outconn.	--lynX
	     */
            rc = EMFILE;
            break;
        }

#ifndef USE_IPV6

        /* Attempt the connection */

        target.sin_port = htons(port);
        /* TODO: Uh-oh, blocking DNS in the execution thread.
         * TODO:: Better would be to start an ERQ lookup and fill in the
         * TODO:: data in the background.
         */
        h = gethostbyname(host);
        target.sin_addr.s_addr = h ? ** (uint32 **) (h -> h_addr_list)
                                   : inet_addr(host);
        if (!target.sin_addr.s_addr)
        {
            rc = -1;
            break;
        }

        target.sin_family = h ? h -> h_addrtype : AF_INET;
        d = socket (target.sin_family, SOCK_STREAM, 0);
        if (d == -1) {
            perror ("socket");
            rc = errno;
            break;
        }

        set_socket_nonblocking(d);

        /* On multihomed machines it is important to bind the socket to
         * the proper IP address.
         */
# if 0
        ret = bind(d, (struct sockaddr *) &host_ip_addr_template, sizeof(host_ip_addr_template));
        if (ret == -1) {
            perror("bind during net_connect");
            rc = errno;
            break;
        } 
# else
	/* only do it if an ip address has actually been passed
	 * this enables servers running under dynamic dns	--lynX
	 *	(http://mantis.bearnip.com/view.php?id=425)
	 */
        if (host_ip_addr_template.sin_addr.s_addr != 0) {
            ret = bind(d, (struct sockaddr *) &host_ip_addr_template, sizeof(host_ip_addr_template));
            if (ret == -1) {
                perror("bind during net_connect");
                rc = errno;
                break;
            }
        }
# endif
        ret = connect(d, (struct sockaddr *) &target, sizeof(target));
#else
        d = ret = open_ipv6_conn(host, port, &target);
#endif
        if (ret == -1 && errno != EINPROGRESS)
        {
            /* error with connection */
            perror("net_connect");
            socket_close(d);
            rc = errno;
            break;
        }

        rc = 0;

        /* Store the connection in the outconn[] table even if
         * we can complete it immediately. For the reason see below.
         */
        outconn[n].socket = d;
        outconn[n].target = target;
        outconn[n].curr_obj = ref_object(current_object, "net_connect");

        if (errno == EINPROGRESS)
        {
            /* Can't complete right now */
            outconn[n].status = ocUsed;
	    num_pending_outconns++;
	    extra_jobs_to_do = MY_TRUE;
            break;
        }

        /* Attempt the logon. By setting the outconn[].status to
         * ocLoggingOn, any subsequent call to check_for_out_connections()
         * will clean up for us.
         */
        outconn[n].status = ocLoggingOn;

        user = command_giver;
        inter_sp = sp;
        new_player(current_object, d, &target, sizeof(target), 0);
        command_giver = user;

        /* All done - clean up */
        outconn[n].status = ocNotUsed;
        free_object(outconn[n].curr_obj, "net_connect");
    }while(0);

    /* Return the result */
    sp = pop_n_elems(2, sp);
    push_number(sp, rc);

    return sp;
} /* f_net_connect() */

/***************************************************************************/
