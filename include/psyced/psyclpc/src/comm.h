#ifndef COMM_H__
#define COMM_H__ 1

#include "driver.h"
#include "typedefs.h"
#include <sys/types.h>
#ifdef USE_PTHREADS
#    include <pthread.h>
#endif
#ifdef USE_MCCP
#    include <zlib.h>
#endif

#include "simulate.h"   /* callback_t for input_to_t */
#include "svalue.h"
#include "pkg-tls.h"

/* TODO: Make the following a separate "my-socket.h" include, also
 * TODO:: to be used in access_check.h instead of comm.h.
 */
#ifdef SOCKET_HEADER
#    include SOCKET_HEADER
#endif

#if !defined (SOCKET_LIB) && !defined(SOCKET_INC)
#    include <sys/socket.h>
#    ifdef _AIX
#        include <sys/socketvar.h>
#    endif
#    include <netinet/in.h>
#    include <arpa/inet.h>
#    define SOCKET_T int
#endif /* SOCKET_LIB */

#ifndef MAX_SOCKET_PACKET_SIZE
#    if defined(sgi) && defined(mips) && defined(unix)
         /* Work around for an irix kernel bug that can leave the
          * process unresponsive and unkillable.
          */
#        define MAX_SOCKET_PACKET_SIZE   200
#    else
#        define MAX_SOCKET_PACKET_SIZE	MAX_TEXT /* was: 1024 = Wild guess. */
#    endif /* irix */
#endif /* MAX_SOCKET_PACKET_SIXE */


/* --- IPv6 --- */

#ifdef USE_IPV6

/* For IPv6 we defined macros for the 'old' sockaddr member names
 * which expand into the ipv6 names.
 */

#define sockaddr_in sockaddr_in6

#define sin_port    sin6_port
#define sin_addr    sin6_addr
#define sin_family  sin6_family
#define s_addr      s6_addr
#define in_addr     in6_addr

#if defined(__APPLE__) && defined(__MACH__) && !defined(s6_addr32)

/* These are non-standard defines, and some OS don't offer them
 * by default: OS-X
 */

#  define s6_addr8  __u6_addr.__u6_addr8
#  define s6_addr16 __u6_addr.__u6_addr16
#  define s6_addr32 __u6_addr.__u6_addr32
#endif

#endif /* USE_IPV6 */


/* --- Macros --- */

/* 'Format string' to use with add_message() when sending
 * a string_t* string to the player.
 */

#define FMT_STRING ((const char *)&add_message)

/* 'Format string' to use with add_message() when sending
 * a string with binary characters and of fixed size
 * to the player.
 */

#define FMT_BINARY (FMT_STRING+1)

/* --- Telnet Handling --- */

#define SEND_TELNET_COMMAND(TEXT) {\
        sending_telnet_command = MY_TRUE;\
        TEXT\
        sending_telnet_command = MY_FALSE;\
}
  /* Use this macro to safely send telnet commands with TEXT
   */

#ifdef DEBUG_TELNET

#define DT(x) printf("%s TDEBUG: ", time_stamp()); printf x
#define DTN(x) printf("%s TDEBUG: '%s' ", time_stamp(), get_txt(ip->ob->name)); printf x
#define DTF(x) printf x

#else

#define DT(x)
#define DTN(x)
#define DTF(x)

#endif

/* --- Types --- */

/* --- struct write_buffer_s: async write datastructure
 *
 * This data structure holds all the information for pending messages
 * which are to be written by a background thread. The structure is
 * allocated to the necessary length to hold the full message.
 * The instances are kept in a linked list from the interactive_t
 * structure.
 */
#ifdef USE_PTHREADS
struct write_buffer_s
{
    struct write_buffer_s *next;
    size_t length;
    Bool   compress; /* should the buffer get compressed by mccp? */
    int    errorno; /* After writing, the errno */
    char buffer[1 /* .length */ ];
};
#endif

/* --- struct input_to_s: input_to() datastructure
 *
 * input-to structures describe a pending input_to() for a given
 * interactive object. Every object can have one input-to pending, the
 * pointer to the structure is stored in the interactive sentence structure.
 */

struct input_to_s {
    input_to_t *next;
    svalue_t    prompt;     /* the prompt, may be 0 */
    char        noecho;     /* the requested "noecho" state */
    Bool        local;      /* TRUE if a CHARMODE change is local only */
    callback_t  fun;        /* The function to call, and its args */
};

/* --- struct interactive_s: an interactive connection
 *
 * The structure is linked to by a shadow sentence of the interactive
 * object.
 *
 * When changing struct members, take care that you don't introduce
 * unnecessary padding.
 *
 * If the CBool members should ever be changed to C99's _Bool, take care of
 * checking who is relying on a specific size (e.g. some printf())
 *
 */

struct interactive_s {
    SOCKET_T socket;            /* The socket structure */
    object_t *ob;               /* Points back to the associated object */
    input_to_t *input_to;       /* != NULL: defines function to be
                                   called with next input line */
    object_t *modify_command;   /* modify_command() handler() */
    svalue_t prompt;            /* The prompt to print. */
    struct sockaddr_in addr;    /* Address of connected user */

    CBool msg_discarded;        /* True if an earlier msg had been discarded */
    CBool set_input_to;         /* True if input_to was set in this cycle */
    CBool closing;              /* True when closing this socket. */
    CBool tn_enabled;           /* True: telnet machine enabled */
    CBool is_binary;		/* True: socket reads binary */
    CBool connected;            /* Set to true after first bytes arrived */
    char do_close;              /* Bitflags: Close this down; Proto-ERQ. */
    char noecho;                /* Input mode bitflags */

    char tn_state;              /* current state of telnet machine */
    char save_tn_state;         /* saved state of telnet machine */
    CBool supress_go_ahead;     /* Keep track of the WILL SGA negotiation state
                                 * as some clients mix that up with DO SGA.
                                 * Other than that, this is of no concern.
                                 */
    CBool outgoing_conn;        /* TRUE if the connection was created by 
                                 * net_connect().
                                 */

    int text_end;             /* first free char in buffer */
    int command_start;        /* used for charmode */
    int command_end;          /* where we are up to in player cmd buffer */
    int tn_start;             /* first char of pending telnet neg */
    int tn_end;               /* first char to check for telnet negotiation */
    int32 chars_ready;          /* amount of pure data available. In charmode
                                 * this is the amount of data already echoed
                                 * back to the sender. */
#ifdef USE_SNOOPING
    interactive_t *snoop_on;    /* whom we're snooping */
    object_t *snoop_by;         /* by whom we're snooped */
#endif
    mp_int last_time;           /* Time of last command executed */
    long  numCmds;              /* Number of commands executed with the same
                                 * .last_time */
    long  maxNumCmds;           /* Maximum number of commands (char or line)
                                 * to execute per second. A value < 0
                                 * means 'unlimited'.
                                 */
    int trace_level;            /* Trace flags. 0 means no tracing */
    string_t *trace_prefix;     /* Trace only objects which have this string
                                   as name prefix. NULL traces everything. */
    int message_length;         /* Current length of message in message_buf[] */

    object_t *next_player_for_flush;
    object_t *previous_player_for_flush;
      /* Double linked list of all active user objects with data pending
       * in message_buf[].
       */

    long access_class;
      /* represents a "cluster" where this player comes from
       */
    char charset[32];
      /* A bitflag array: every non-zero flag allows the corresponding
       * character to be sent. Characters whose flag is 0 are excluded
       * from the sent data.
       */
    char combine_cset[32];
      /* A bitflag array: all characters with their corresponding flag
       * set to non-zero flag may be combined into one string in
       * char-mode when received en-bloc anyway. Characters whose flag
       * is 0 are always returned in charmode in separate strings.
       * TODO: The code for these two thingies assume 8 Bits per character.
       */
    CBool quote_iac;
    CBool catch_tell_activ;
    char gobble_char;           /* Char to ignore at the next telnet_neg() */
    char ts_data;               /* Telnet suboption? */

    char text[MAX_TEXT+2];
      /* The receive buffer. It can contain two extra characters:
       * a '\r' that is recognized only after another character is read,
       * and a terminating '\0'.
       */

    char message_buf[MAX_SOCKET_PACKET_SIZE];
      /* The send buffer. */

#ifdef USE_MCCP
     unsigned char   compressing;     
     z_stream      * out_compress;    
     unsigned char * out_compress_buf;
#endif   

#ifdef USE_PTHREADS
    /* The data exchange with the writer thread happens through two
     * lists: write_first/write_last hands of data to write to
     * the thread, written_first receives the written buffers.
     * Reason for this 2-way exchange is that the writer thread
     * must not call xfree().
     * TODO: These two lists + one extra can be combined into
     * TODO:: one list, plus two roving pointers into it.
     * TODO:: In fact, once ptmalloc is really complete, the buffers
     * TODO:: can be discarded immediately and the written_first list
     * TODO:: becomes unnecessary.
     */
    Bool                   flush_on_cleanup;
      /* If set to TRUE at the time the thread is cancelled, all remaining
       * pending data is sent to the socket (which is made non-blocking for
       * this) as part of the cleanup.
       */
    pthread_mutex_t        write_mutex;
    pthread_cond_t         write_cond;
    pthread_t              write_thread;
    struct write_buffer_s *write_first;  /* List of buffers to write */
    struct write_buffer_s *write_last;
    unsigned long          write_size;
    struct write_buffer_s *write_current; /* Buffer currently written */
    struct write_buffer_s *written_first; /* List of written buffers */
#endif

#ifdef USE_TLS
    tls_session_t          tls_session;
    int                    tls_status;
#     define TLS_INACTIVE     0  /* Session is not secure */
#     define TLS_HANDSHAKING  1  /* TLS is being negotiated */
#     define TLS_ACTIVE       2  /* Session is secure */
#     define TLS_BROKEN       3  /* Error has occurred */
    CBool		   tls_want_peer_cert;
# ifdef HAS_PSYC
    int			   tls_autodetect;
# endif
    callback_t            *tls_cb;
#endif
};

/* --- Bitflags and masks for interactive.noecho ---
 *
 * 'noecho' is a historical misnomer as it actually represents several
 * input modes: echo/noecho, linemode/charmode, and ignore '!' escape.
 * Echo and Charmode additionally distinguish between 'required'
 * and 'granted' ('want yes' and 'yes').
 *
 * xxx_REQ is set when the lib wants this state (independend from telnet state)
 * xxx     is set when we sent out the appropirate negotiations
 * xxx_ACK is set when the client agrees
 *
 *     xxx   xxx_ACK    telnet state
 *      0       0        NO
 *      1       0        WANT YES
 *      1       1        YES
 *      0       1        WANT NO (not implemented)
 */

#define CHARMODE_REQ_TO_CHARMODE(x) ((x) << 2)
  /* Transform {CHARMODE, NOECHO}_REQ into {CHARMODE, NOECHO}
   */
#define NOECHO_ACKSHIFT(x)   ((x) << 2)
#define NOECHO_ACKRSHIFT(x)  ((x) >> 2)
  /* TODO: ???
   */

#define IGNORE_BANG    128
  /* Disable input escape with leading '!'.
   */

#define NOECHO_REQ        1
  /* noecho requested
   */
#define NOECHO         /* 4 */ CHARMODE_REQ_TO_CHARMODE(NOECHO_REQ)
  /* noecho active (requested via telnet negotiation)
   */
#define NOECHO_ACK    /* 16 */ NOECHO_ACKSHIFT(NOECHO)
  /* noecho negotiation complete (acknowledged)
   * TODO: We need a _NACK flag, too, for when the negotiation is complete
   * TODO:: but the client refused to go into NOECHO. For the time being
   * TODO:: we might use (NOECHO_REQ|NOECHO) == (NOECHO_REQ) as check.
   */
#define NOECHO_STALE     64
  /* Set prior to performing a noecho input, this bit causes the deactivation
   * of noecho if still set after the input (ie if noecho was not requested
   * again).
   */
#define NOECHO_MASK      (NOECHO|NOECHO_ACK)
  /* Mask for active noecho states.
   */

#define CHARMODE_REQ      2
  /* Charmode requested
   * The driver orients its behaviour according to this flag so that it
   * can act correctly even before the negotiation is complete.
   * Moreover, this flag serves as indicator that the input buffer
   * variables are set up to 'charmode', so it should not be reset except
   * through a call to set_noecho().
   */
#define CHARMODE       /* 8 */ CHARMODE_REQ_TO_CHARMODE(CHARMODE_REQ)
  /* Charmode active (requested via telnet negotiation)
   */
#define CHARMODE_ACK  /* 32 */ NOECHO_ACKSHIFT(CHARMODE)
  /* Charmode negotiation complete (acknowledged)
   * TODO: We need a _NACK flag, too, for when the negotiation is complete
   * TODO:: but the client refused to go into CHARMODE. For the time being
   * TODO:: we can use (CHARMODE_REQ|CHARMODE) == CHARMODE_REQ as check.
   */
#define NOECHO_DELAYED  128
  /* If NOECHO and CHARMODE is requested, we first try to establish
   * CHARMODE and set this flag. If CHARMODE is acknoledged we try to
   * negotiate for NOECHO. This modus operandi is enabled with SAVE_NOECHO.
   * pro: If the user has a linemode-only client she is not left in
   *      useless NOECHO mode
   * con: The NOECHO is delayed by the round trip time for the negotiation
   *      which could be imense on very slow lines.
   * TODO: Need to defined SAVE_NOECHO in comm.c for this.
   */
#define CHARMODE_MASK    (CHARMODE|CHARMODE_ACK)
  /* Mask for active charmode states.
   */

/* --- Variables --- */

extern int num_pending_outconns;    /* new! optimization! --lynX 2007 */

extern Bool sending_telnet_command;
extern interactive_t *all_players[MAX_PLAYERS];
extern int num_player;
extern char *message_flush;
extern char *domain_name;

extern long pthread_write_max_size;

#ifdef COMM_STAT
extern unsigned long add_message_calls;
extern unsigned long inet_packets;
extern unsigned long inet_volume;
extern unsigned long inet_packets_in;
extern unsigned long inet_volume_in;
#endif

/* --- Prototypes --- */

extern void initialize_host_name (const char *hname);
extern void initialize_host_ip_number(const char *, const char *);
extern void  prepare_ipc(void);
extern void  ipc_remove(void);
#ifdef USE_PTHREADS
extern void interactive_lock (interactive_t *ip);
extern void interactive_unlock (interactive_t *ip);
extern void interactive_cleanup (interactive_t *ip);
#else
extern void interactive_lock (interactive_t *ip UNUSED);
extern void interactive_unlock (interactive_t *ip UNUSED);
extern void interactive_cleanup (interactive_t *ip UNUSED);
#endif /* USE_PTHREADS */
extern void comm_cleanup_interactives (void);
extern void  add_message VARPROT((const char *, ...), printf, 1, 2);
extern void  flush_all_player_mess(void);
extern Bool get_message(char *buff, size_t *len);
extern void remove_interactive(object_t *ob, Bool force);
extern void set_noecho(interactive_t *i, char noecho, Bool local_change, Bool external);
extern int  find_no_bang (interactive_t *ip);
extern Bool call_function_interactive(interactive_t *i, char *str, size_t len);
extern void remove_all_players(void);
extern void  print_prompt(void);
extern void  init_telopts(void);
extern void  mudlib_telopts(void);
extern svalue_t *v_input_to (svalue_t *sp, int num_arg);

extern svalue_t *f_query_udp_port(svalue_t *sp);

#ifdef ERQ_DEMON
extern void  start_erq_demon(const char *suffix, size_t suffixlen);
extern svalue_t *f_attach_erq_demon(svalue_t *sp);
extern svalue_t *f_send_erq(svalue_t *sp);
#endif

extern size_t show_comm_status (strbuf_t * sbuf, Bool verbose);
extern void remove_stale_player_data (void);
extern void check_for_out_connections (void);

#ifdef GC_SUPPORT
extern void  clear_comm_refs(void);
extern void  count_comm_refs(void);
#endif /* GC_SUPPORT */

extern char *query_host_name(void);
extern char *get_host_ip_number(void);
extern svalue_t *f_query_idle(svalue_t *sp);
extern svalue_t *f_remove_interactive(svalue_t *sp);

#ifdef DEBUG
extern void  count_comm_extra_refs(void);
#endif /* DEBUG */

extern svalue_t *f_send_udp(svalue_t *sp);

extern svalue_t *f_binary_message(svalue_t *sp);
extern svalue_t *f_exec(svalue_t *sp);
extern svalue_t *f_interactive(svalue_t *sp);
extern svalue_t *f_query_input_pending(svalue_t *sp);
extern svalue_t *f_query_ip_name(svalue_t *sp);
extern svalue_t *f_query_ip_number(svalue_t *sp);
extern svalue_t *f_query_mud_port(svalue_t *sp);
extern svalue_t *v_find_input_to (svalue_t *sp, int num_arg);
extern svalue_t *v_remove_input_to (svalue_t *sp, int num_arg);
extern svalue_t *f_input_to_info (svalue_t *sp);
extern svalue_t *f_set_buffer_size(svalue_t *sp);
extern svalue_t *f_get_combine_charset(svalue_t *sp);
extern svalue_t *f_set_combine_charset(svalue_t *sp);
extern svalue_t *f_get_connection_charset(svalue_t *sp);
extern svalue_t *f_set_connection_charset(svalue_t *sp);
extern svalue_t *f_set_prompt(svalue_t *sp);
extern svalue_t *f_users(svalue_t *sp);
extern svalue_t *f_get_max_commands (svalue_t *sp);
extern svalue_t *f_set_max_commands (svalue_t *sp);
extern svalue_t *f_enable_telnet (svalue_t *sp);
extern svalue_t *f_net_connect (svalue_t *sp);

#if defined(ACCESS_CONTROL)
extern void refresh_access_data(void (*add_entry)(struct sockaddr_in *, int, long*) );
#endif /* ACCESS_CONTROL */

#ifdef USE_SNOOPING
extern svalue_t *f_query_snoop(svalue_t *sp);
extern svalue_t *v_snoop(svalue_t *sp, int num_arg);
#endif

#endif /* COMM_H__ */
