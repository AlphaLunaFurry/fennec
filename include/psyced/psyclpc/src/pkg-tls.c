/*------------------------------------------------------------------
 * Wrapper for the gnutls resp. OpenSSL module.
 *
#ifdef HAS_GNUTLS
 * GNUTLS provides less functionality than OpenSSL
#endif
 *------------------------------------------------------------------
 */

#include "driver.h"
#include "machine.h"

#ifdef USE_TLS

/* #define DEBUG **/

#include <stdio.h>

#if defined(HAS_OPENSSL)
#  include <openssl/ssl.h>
#  include <openssl/rand.h>
#  include <openssl/err.h>
#  include <openssl/x509.h>
#  include <openssl/x509v3.h>
#  include <sys/utsname.h>
#  include <openssl/opensslconf.h>

#  include <openssl/sha.h>
#  include <openssl/md5.h>
#  include <openssl/ripemd.h>

#  include <openssl/hmac.h>
#  include <openssl/evp.h>

#elif defined(HAS_GNUTLS)
#  include <gnutls/gnutls.h>
#  if defined(USE_PTHREADS) && defined(GCRY_THREAD_OPTION_PTHREAD_IMPL)
#    include <pthread.h>
     GCRY_THREAD_OPTION_PTHREAD_IMPL;
#  endif
#endif

#include "pkg-tls.h"

#include "actions.h"
#include "array.h"
#include "comm.h"
#include "interpret.h"
#include "main.h"
#include "mstrings.h"
#include "object.h"
#include "sha1.h"
#include "svalue.h"
#include "xalloc.h"
     
#include "../mudlib/sys/tls.h"

/*-------------------------------------------------------------------------*/

#define DH_BITS 1024

/*-------------------------------------------------------------------------*/
/* Variables */

char * tls_keyfile = NULL;
char * tls_certfile = NULL;
char * tls_trustfile = NULL;
char * tls_trustdirectory = NULL;
char * tls_crlfile = NULL;
char * tls_crldirectory = NULL;
  /* The filenames of the x509 key and cert file, set by the argument
   * parser. If not set, the package will use defaults.
   */

static Bool tls_available = MY_FALSE;
  /* Set to TRUE when the TLS layer has been initialised successfully.
   */

#ifdef HAS_OPENSSL

static SSL_CTX * context = NULL;
  /* The SSL program context. */

static DH *dhe1024 = NULL;
  /* The Diffie-Hellmann parameters. */

#elif defined(HAS_GNUTLS)

static gnutls_certificate_server_credentials x509_cred;
  /* The x509 credentials. */

static gnutls_dh_params dh_params;
  /* The Diffie-Hellmann parameters */

#endif /* SSL Package */

#ifdef HAS_OPENSSL

/*-------------------------------------------------------------------------*/
static Bool
set_dhe1024 (void)

/* Set the Diffie-Hellmann parameters.
 * Return MY_TRUE on success, and MY_FALSE on error.
 */

{
    DSA *dsaparams;
    DH *dhparams;

    if (dhe1024 != NULL)
        return MY_TRUE;

    dsaparams = DSA_generate_parameters(1024, NULL, 0, NULL, NULL, 0, NULL);

    if (dsaparams == NULL)
        return MY_FALSE;

    dhparams = DSA_dup_DH(dsaparams);
    DSA_free(dsaparams);
    if (dhparams == NULL)
        return MY_FALSE;

    dhe1024 = dhparams;

    return MY_TRUE;
} /* set_dhe1024() */
                                                  
/*-------------------------------------------------------------------------*/
static int
tls_verify_callback(int preverify_ok, X509_STORE_CTX *ctx) 

/* This function will be called if the client did present a certificate
 * always returns MY_TRUE so that the handshake will succeed
 * and the verification status can later be checked on mudlib level
 * see also: SSL_set_verify(3)
 */

{
    if (d_flag)
    {
        char buf[512];
        printf("%s tls_verify_callback(%d, ...)\n", time_stamp(), preverify_ok);

        X509_NAME_oneline(X509_get_issuer_name(ctx->current_cert), buf, sizeof buf);
        printf("depth %d: %s\n", X509_STORE_CTX_get_error_depth(ctx), buf);
    }
#if 0
    if (!preverify_ok) 
    {
	int err;
	int depth;

	/* ignore purpose flag on leaf certs */
	err = X509_STORE_CTX_get_error(ctx);
	depth = X509_STORE_CTX_get_error_depth(ctx);
	if (err == X509_V_ERR_INVALID_PURPOSE && depth == 0) 
	{
	    SSL *ssl;
	    ssl = X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx());

	    preverify_ok = 1;
	    err = X509_V_OK;
	    SSL_set_verify_result(ssl, X509_V_OK);
	    X509_STORE_CTX_set_error(ctx, X509_V_OK);
	}

    }
#endif
    return MY_TRUE;
} /* tls_verify_callback() */

#elif defined(HAS_GNUTLS)

/*-------------------------------------------------------------------------*/
static int
generate_dh_params (void)

/* GnuTLS: Generate Diffie Hellman parameters and store them in the global
 * <dh_params>.  They are for use with DHE kx algorithms. These should be
 * discarded and regenerated once a day, once a week or once a month. Depends
 * on the security requirements.
 *
 * tls_available must be TRUE.
 */

{
#if HAS_GNUTLS_VERSION < 8
    gnutls_datum prime, generator;

    gnutls_dh_params_init( &dh_params);
    gnutls_dh_params_generate( &prime, &generator, DH_BITS);
    gnutls_dh_params_set( dh_params, prime, generator, DH_BITS);

    free( prime.data);
    free( generator.data);
#else
    gnutls_dh_params_init( &dh_params);
    gnutls_dh_params_generate2( dh_params, DH_BITS);
#endif
    return 0;
} /* generate_dh_params() */

/*-------------------------------------------------------------------------*/
static void
initialize_tls_session (gnutls_session *session)

/* GnuTLS: Initialise a TLS <session>.
 * tls_available must be TRUE.
 */

{
    gnutls_init(session, GNUTLS_SERVER);

    /* avoid calling all the priority functions, since the defaults
     * are adequate.
     */
    gnutls_set_default_priority( *session);   
    
    gnutls_credentials_set( *session, GNUTLS_CRD_CERTIFICATE, x509_cred);

    gnutls_dh_set_prime_bits( *session, DH_BITS);
} /* initialize_tls_session() */

/*-------------------------------------------------------------------------*/
static void *
tls_xalloc (size_t size)

/* Wrapper function so that (gnu)tls will use the driver's allocator.
 * The wrapper is required as 'pxalloc' itself is a macro.
 */

{
    return pxalloc(size);
} /* tls_xalloc() */

/*-------------------------------------------------------------------------*/
static void *
tls_rexalloc (void *old, size_t size)

/* Wrapper function so that (gnu)tls will use the driver's allocator.
 * The wrapper is required as 'prexalloc' itself is a macro.
 */

{
    return prexalloc(old, size);
} /* tls_rexalloc() */

/*-------------------------------------------------------------------------*/
static void
tls_xfree (void *p)

/* Wrapper function so that (gnu)tls will use the driver's allocator.
 * The wrapper is not exactly required for pfree(),  but it keeps things
 * consistent.
 */

{
    return pfree(p);
} /* tls_xfree() */

#endif /* SSL Package */ 

/*-------------------------------------------------------------------------*/
void
tls_global_init (void)

/* Initialise the TLS package; to be called once at program startup.
 */

{
    char * keyfile = tls_keyfile ? tls_keyfile : TLS_DEFAULT_KEYFILE;
    char * certfile = tls_certfile ? tls_certfile : TLS_DEFAULT_CERTFILE;
#ifdef HAS_OPENSSL
    char * trustfile = tls_trustfile ? tls_trustfile : NULL;
    char * trustdirectory = tls_trustdirectory ? tls_trustdirectory : TLS_DEFAULT_TRUSTDIRECTORY;
#elif defined(HAS_GNUTLS)
    /* GNUTLS doesn't support certificate checking yet. */
#endif

#ifdef HAS_OPENSSL

#ifdef VERBOSE
    printf("%s TLS: (OpenSSL) x509 keyfile '%s', certfile '%s'\n"
          , time_stamp(), keyfile, certfile);
#endif
    debug_message("%s TLS: (OpenSSL) Keyfile '%s', Certfile '%s'\n"
                 , time_stamp(), keyfile, certfile);
    if (trustfile != NULL && trustdirectory != NULL)
    {
#ifdef VERBOSE
        printf("%s TLS: (OpenSSL) trusted x509 certificates from '%s' and directory '%s'.\n"
              , time_stamp(), trustfile, trustdirectory);
#endif
        debug_message("%s TLS: (OpenSSL) trusted x509 certificates from '%s' and directory '%s'.\n"
                     , time_stamp(), trustfile, trustdirectory);
    }
    else if (trustfile != NULL)
    {
#ifdef VERBOSE
        printf("%s TLS: (OpenSSL) trusted x509 certificates from '%s'.\n"
              , time_stamp(), trustfile);
#endif
        debug_message("%s TLS: (OpenSSL) trusted x509 certificates from '%s'.\n"
                     , time_stamp(), trustfile);
    }
    else if (trustdirectory != NULL)
    {
#ifdef VERBOSE
        printf("%s TLS: (OpenSSL) trusted x509 certificates from directory '%s'.\n"
              , time_stamp(), trustdirectory);
#endif
        debug_message("%s TLS: (OpenSSL) trusted x509 certificates from directory '%s'.\n"
                     , time_stamp(), trustdirectory);
    }
    else
    {
#ifdef VERBOSE
        printf("%s TLS: (OpenSSL) Trusted x509 certificates locations not specified.\n"
              , time_stamp());
#endif
        debug_message("%s TLS: (OpenSSL) trusted x509 certificates locations not specified.\n"
                     , time_stamp());
    }

    SSL_load_error_strings();
    ERR_load_BIO_strings();
    if (!SSL_library_init())
    {
#ifdef VERBOSE
        printf("%s TLS: Initialising the SSL library failed.\n"
              , time_stamp());
#endif
        debug_message("%s TLS: Initialising the SSL library failed.\n"
                     , time_stamp());
        return;
    }

    /* SSL uses the rand(3) generator from libcrypto(), which needs
     * to be seeded.
     */
    {
        struct {
            struct utsname uname;
            int uname_1;
            int uname_2;
            uid_t uid;
            uid_t euid;
            gid_t gid;
            gid_t egid;
        } data1;

        struct {
            pid_t pid;
            time_t time;
            void *stack;
        } data2;

        data1.uname_1 = uname(&data1.uname);
        data1.uname_2 = errno; /* Let's hope that uname fails randomly :-) */

        data1.uid = getuid();
        data1.euid = geteuid();
        data1.gid = getgid();
        data1.egid = getegid();

        RAND_seed((const void *)&data1, sizeof data1);

        data2.pid = getpid();
        data2.time = time(NULL);
        data2.stack = (void *)&data2;

        RAND_seed((const void *)&data2, sizeof data2);
    }

    /* note: outgoing connections override this with TLSv1_client_method
     */
    context = SSL_CTX_new (SSLv23_method());
    if (!context)
    {
#ifdef VERBOSE
        printf("%s TLS: Can't get SSL context:\n"
              , time_stamp());
#endif
        debug_message("%s TLS: Can't get SSL context:\n"
                     , time_stamp());
        
        goto ssl_init_err;
    }

    SSL_CTX_set_mode(context, SSL_MODE_ENABLE_PARTIAL_WRITE);
    SSL_CTX_set_session_id_context(context, (unsigned char*) "ldmud", 5);

    if (!SSL_CTX_use_PrivateKey_file(context, keyfile, SSL_FILETYPE_PEM))
    {
#ifdef VERBOSE
        printf("%s TLS: Error setting x509 keyfile:\n"
              , time_stamp());
#endif
        debug_message("%s TLS: Error setting x509 keyfile:\n"
              , time_stamp());
        goto ssl_init_err;
    }

    if (!SSL_CTX_use_certificate_chain_file(context, certfile))
    {
#ifdef VERBOSE
        printf("%s TLS: Error setting x509 certfile:\n"
              , time_stamp());
#endif
        debug_message("%s TLS: Error setting x509 certfile:\n"
              , time_stamp());
        goto ssl_init_err;
    }

    if (tls_crlfile != NULL || tls_crldirectory != NULL)
    {
	X509_STORE *store = X509_STORE_new();
	if (store != NULL)
        {
	    if (tls_crlfile != NULL)
            {
		X509_LOOKUP *lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file());
		if (lookup != NULL) 
		    X509_LOOKUP_load_file(lookup, tls_crlfile, X509_FILETYPE_PEM);
	    }
	    if (tls_crldirectory != NULL)
            {
		X509_LOOKUP *lookup = X509_STORE_add_lookup(store, X509_LOOKUP_hash_dir());
		if (lookup != NULL) 
		    X509_LOOKUP_add_dir(lookup, tls_crldirectory, X509_FILETYPE_PEM);
	    }
#if OPENSSL_VERSION_NUMBER >= 0x00907000L
	    X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
	    SSL_CTX_set_cert_store(context, store);
	    if (tls_crlfile != NULL && tls_crldirectory != NULL)
            {
		printf("%s TLS: (OpenSSL) CRLs from '%s' and '%s'.\n"
		       , time_stamp(), tls_crlfile, tls_crldirectory);
		debug_message("%s TLS: (OpenSSL) CRLs from '%s' and '%s'.\n"
		       , time_stamp(), tls_crlfile, tls_crldirectory);
	    }
            else if (tls_crlfile != NULL)
            {
		printf("%s TLS: (OpenSSL) CRLs from '%s'.\n"
		       , time_stamp(), tls_crlfile);
		debug_message("%s TLS: (OpenSSL) CRLs from '%s'.\n"
		       , time_stamp(), tls_crlfile);
	    }
            else if (tls_crldirectory != NULL)
            {
		printf("%s TLS: (OpenSSL) CRLs from '%s'.\n"
		       , time_stamp(), tls_crldirectory);
		debug_message("%s TLS: (OpenSSL) CRLs from '%s'.\n"
		       , time_stamp(), tls_crldirectory);
	    }
            else
            {
		printf("%s TLS: (OpenSSL) CRL checking disabled.\n"
		       , time_stamp());
		debug_message("%s TLS: (OpenSSL) CRL checking disabled.\n"
		       , time_stamp());
	    }
#else
	    printf("%s TLS: Warning: Your OpenSSL version does not support "
		   "Certificate revocation list checking\n"
		  , time_stamp());
	    debug_message("%s TLS: Warning: Your OpenSSL version does not "
			  "support Certificate revocation list checking\n"
		  , time_stamp());
#endif
	}
        else
        {
	    printf("%s TLS: Warning: There was a problem getting the "
		   "storage context from OpenSSL. Certificate revocation "
		   "list checking is not enabled.\n"
		  , time_stamp());
	    debug_message("%s TLS: Warning: There was a problem getting the "
			  "storage context from OpenSSL. Certificate revocation "
			  "list checking is not enabled.\n"
		  , time_stamp());
	}
    }

    if (!SSL_CTX_load_verify_locations(context, trustfile, trustdirectory))
    {
#ifdef VERBOSE
        printf("%s TLS: Error preparing x509 verification certificates\n",
               time_stamp());
#endif
        debug_message("%s TLS: Error preparing x509 verification certificates\n",
               time_stamp());
    }
    
    if (SSL_CTX_check_private_key(context) != 1)
    {
#ifdef VERBOSE
        printf("%s TLS: Private key does not match certificate public key:\n"
              , time_stamp());
#endif
        debug_message("%s TLS: Private key does not match certificate public "
                      "key:\n"
              , time_stamp());

        goto ssl_init_err;
    }

    if (!set_dhe1024()
     || !SSL_CTX_set_tmp_dh(context, dhe1024)
       )
    {
#ifdef VERBOSE
        printf("%s TLS: Error setting Diffie-Hellmann parameters:\n"
              , time_stamp());
#endif
        debug_message("%s TLS: Error setting Diffie-Hellmann parameters:\n"
              , time_stamp());
        goto ssl_init_err;
    }

    /* Avoid small subgroup attacks */
    /* do not do SSLv2 */
    SSL_CTX_set_options(context, SSL_OP_SINGLE_DH_USE);
    SSL_CTX_set_options(context, SSL_OP_NO_SSLv2);

    /* OpenSSL successfully initialised */
    tls_available = MY_TRUE;
    return;

    /* General error handling for the initialisation */
ssl_init_err:
    {
        unsigned long err;

        while (0 != (err = ERR_get_error()))
        {
            char * errstring = ERR_error_string(err, NULL);
#ifdef VERBOSE
            printf("%s TLS: SSL %s.\n"
                  , time_stamp(), errstring);
#endif
            debug_message("%s TLS: SSL %s.\n"
                         , time_stamp(), errstring);
        }

        if (dhe1024 != NULL)
        {
            DH_free(dhe1024);
            dhe1024 = NULL;
        }

        if (context != NULL)
        {
            SSL_CTX_free(context);
            context = NULL;
        }
        return;
    }

#elif defined(HAS_GNUTLS)

    int f;

  
    /* In order to be able to identify gnutls allocations as such, we redirect
     * all allocations through the driver's allocator. The wrapper functions
     * make sure that the allocations are annotated properly with this source
     * file.
     */
    gnutls_global_set_mem_functions(tls_xalloc,
                                    tls_xalloc,
                                    NULL,
                                    tls_rexalloc,
                                    tls_xfree);

#  if defined(USE_PTHREADS) && defined(GCRY_THREAD_OPTION_PTHREAD_IMPL)
    gcry_control (GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);
#endif

    gnutls_global_init();

    gnutls_certificate_allocate_credentials(&x509_cred);

#ifdef VERBOSE
    printf("%s TLS: (GnuTLS) x509 keyfile '%s', certfile '%s'\n"
          , time_stamp(), keyfile, certfile);
#endif
    debug_message("%s TLS: (GnuTLS) Keyfile '%s', Certfile '%s'\n"
                 , time_stamp(), keyfile, certfile);

    f = gnutls_certificate_set_x509_key_file(x509_cred, certfile, keyfile, GNUTLS_X509_FMT_PEM);
    if (f < 0)
    {
#ifdef VERBOSE
        printf("%s TLS: Error setting x509 keyfile: %s\n"
              , time_stamp(), gnutls_strerror(f));
#endif
        debug_message("%s TLS: Error setting x509 keyfile: %s\n"
                     , time_stamp(), gnutls_strerror(f));
    }
    else
    {
       printf("%s TLS: x509 keyfile and certificate set.\n", time_stamp());
        generate_dh_params();

        gnutls_certificate_set_dh_params( x509_cred, dh_params);

        tls_available = MY_TRUE;
    }
#endif /* SSL Package */

} /* tls_global_init() */

/*-------------------------------------------------------------------------*/
void
tls_global_deinit (void)

/* Clean up the TLS package on program termination.
 */

{
#ifdef HAS_OPENSSL

    if (dhe1024 != NULL)
    {
        DH_free(dhe1024);
        dhe1024 = NULL;
    }
    if (context != NULL)
    {
        SSL_CTX_free(context);
        context = NULL;
    }

#elif defined(HAS_GNUTLS)

    if (tls_available)
    {
        gnutls_certificate_free_credentials(x509_cred);
    }

    gnutls_global_deinit();
#endif /* SSL Package */

    tls_available = MY_FALSE;

} /* tls_global_deinit() */

/*-------------------------------------------------------------------------*/
int
tls_read (interactive_t *ip, char *buffer, int length)

/* Read up to <length> bytes data for the TLS connection of <ip>
 * and store it in <buffer>.
 * Return then number of bytes read, or a negative number if an error
 * occured.
 */

{
    int ret = -11;

#ifdef HAS_OPENSSL
    int err;

    do {
        ret = SSL_read(ip->tls_session, buffer, length);
    } while  (ret < 0 && (err = SSL_get_error(ip->tls_session, ret))
              && (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE));

    if (ret == 0)
    {
        tls_deinit_connection(ip);
    }
    else if (ret < 0)
    {
        ret = SSL_get_error(ip->tls_session, ret);
	debug_message("%s TLS: Received corrupted data (%d, errno %d). "
		      "Closing the connection.\n"
		     , time_stamp(), ret, errno);
	switch (ret) {
	case SSL_ERROR_SYSCALL:
	case SSL_ERROR_SSL:
	    ERR_print_errors_fp(stderr);
	    break;
	default:
	    break;
	}
        ip->tls_status = TLS_BROKEN;
	tls_deinit_connection(ip);
    }

#elif defined(HAS_GNUTLS)
    do {
           ret = gnutls_record_recv(ip->tls_session, buffer, length);
    } while ( ret < 0 && (ret == GNUTLS_E_INTERRUPTED || ret == GNUTLS_E_AGAIN) );

    if (ret == 0)
    {
        tls_deinit_connection(ip);
    }
    else if (ret < 0)
    {
        debug_message("%s GnuTLS: Error in receiving data (%s). "
                      "Closing the connection.\n"
                     , time_stamp(), gnutls_strerror(ret));
        tls_deinit_connection(ip);
    }
#endif /* SSL Package */

    return (ret < 0 ? -1 : ret);
} /* tls_read() */

/*-------------------------------------------------------------------------*/
int
tls_write (interactive_t *ip, char *buffer, int length)

/* Write <length> bytes from <buffer> to the TLS connection of <ip>
 * Return the number of bytes written, or a negative number if an error
 * occured.
 */

{
    int ret = -1;

#ifdef HAS_OPENSSL

    int err;

    do {
        ret = SSL_write(ip->tls_session, buffer, length);
    } while  (ret < 0 && (err = SSL_get_error(ip->tls_session, ret))
              && (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE));

    if (ret == 0)
    {
        tls_deinit_connection(ip);
    }
    else if (ret < 0)
    {
        ret = SSL_get_error(ip->tls_session, ret);
        debug_message("%s TLS: Sending data failed (%d). "
                      "Closing the connection.\n"
                     , time_stamp(), ret);
        tls_deinit_connection(ip);
    }

#elif defined(HAS_GNUTLS)

    ret = gnutls_record_send( ip->tls_session, buffer, length );
    if (ret < 0)
    {
        debug_message("%s GnuTLS: Error in sending data (%s). "
                      "Closing the connection.\n"
                     , time_stamp(), gnutls_strerror(ret));
        tls_deinit_connection(ip);
    }
#endif /* SSL Package */

    return (ret<0 ? -1 : ret);
} /* tls_write() */

/*-------------------------------------------------------------------------*/
/* To protect the tls callback during it's execution, it is pushed onto
 * the stack as an T_ERROR_HANDLER value for guaranteed cleanup.
 */

typedef struct tls_cb_handler_s
{
    svalue_t     val;
    callback_t * cb;
} tls_cb_handler_t;

/*-------------------------------------------------------------------------*/
static void
handle_tls_cb_error (svalue_t * arg)

{
    tls_cb_handler_t * data = (tls_cb_handler_t *)arg;
    free_callback(data->cb);
    xfree(data->cb);
    xfree(arg);
} /* handle_tls_cb_error() */

/*-------------------------------------------------------------------------*/
int
tls_continue_handshake (interactive_t *ip)

/* Continue the TLS initialisation handshake for interactive <ip>.
 * Return a negative error code if the connection can not be set up.
 * Return 0 if the connection is still begin set up.
 * Return 1 if the connection is now active (or if no secure connection
 * had been requested).
 *
 * If a callback is set for <ip> and connection comes out of the handshaking
 * state, it will be called with the result code.
 */

{
    int ret = 1;

#ifdef USE_PARANOIA
    if (ip->tls_status != TLS_HANDSHAKING)
        return 1;
#endif
#ifdef HAS_OPENSSL
# ifdef HAS_PSYC
    /* detect non-tls data on a tls connection
     * heuristic taken from jabberds jadc2s
     */
    if (ip->tls_autodetect) {
	char peek_buf = 0x44;

	if (recv(ip->socket, &peek_buf, 1, MSG_PEEK) < 1) {
	     /*	peek failure: this part of code is run each 
		second until any character has appeared on the
		socket. until now a non-tls connect to an autodetect
		socket could cause a crash, or at least a runtime
		openssl error. here's the fix. even better:
		this implements a countdown. if after a certain
		number of seconds no SSL/TLS has shown up on the
		socket we pass 0x44 to the switch below which
		deallocates the tls session and goes for plaintext. --lynX
	     */
#  ifdef DEBUG
	    debug_message("TLS autodetect %d: could not peek from socket\n",
		ip->tls_autodetect);
#  endif
	    if (--ip->tls_autodetect) return 0;
	}
#  ifdef DEBUG
	debug_message("TLS autodetect called off. ret = %d, peek byte is %x\n",
	    ret, (int)peek_buf);
#  endif
	/* disable autodetect on established plaintext connections */
	ip->tls_autodetect = 0;

	switch(peek_buf & 0xff) {
	case 0x16: /* TLSv1 most likely */
	case 0x80: /* SSLv2 most likely */
	case 0x00: /* SSLv2 most likely */
	    break;
	case 0x44: /* special marker for autodetect timeout */
	default:
	    SSL_free(ip->tls_session);
	    ip->tls_session = NULL;
	    ip->tls_status = TLS_INACTIVE;
	    ret = ERR_TLS_NOT_DETECTED;
	    break;
	}
    }
# endif

    if (ret == 1)
    {
        int n;

        if ((n = SSL_do_handshake(ip->tls_session)) < 0)
            ret = SSL_get_error(ip->tls_session, n);
        else
            ret = 0;

        if (ret != SSL_ERROR_WANT_READ && ret != SSL_ERROR_WANT_WRITE)
        {
            if (ret != 0)
            {
                /* Setup failed */
                SSL_free(ip->tls_session);
                ip->tls_session = NULL;
                ip->tls_status = TLS_INACTIVE;
                ret = - ret;
            }
            else
            {
                /* Setup finished */
                /* TODO: Check SSL_in_init() at this place? */
                ip->tls_status = TLS_ACTIVE;
                ret = 1;
            }
        }
        else
            ret = 0;
    }

#elif defined(HAS_GNUTLS)

    ret = gnutls_handshake(ip->tls_session);

    if (ret != GNUTLS_E_AGAIN && ret != GNUTLS_E_INTERRUPTED)
    {
        if (ret < 0)
        {
            /* Setup failed */
            gnutls_deinit(ip->tls_session);
            ip->tls_session = NULL;
            ip->tls_status = TLS_INACTIVE;
        }
        else
        {
            /* Setup finished */
            ip->tls_status = TLS_ACTIVE;
            ret = 1;
        }
    }
    else
        ret = 0;

#endif /* SSL Package */

    /* If the connection is no longer in handshake, execute the callback */
    if (ip->tls_cb != NULL && ret != 0)
    {
        tls_cb_handler_t * handler;
	object_t * para;
	para = command_giver;
	command_giver = ip->ob;

        xallocate( handler, sizeof(*handler)
                 , "TLS: Callback handler protector");
        handler->cb = ip->tls_cb;
        ip->tls_cb = NULL;
	/* Protect the callback during its execution. */

        push_error_handler (handle_tls_cb_error, (svalue_t *)handler);

        push_number(inter_sp, ret < 0 ? ret : 0);
        push_ref_object(inter_sp, ip->ob, "tls_handshake");

        (void)apply_callback(handler->cb, 2);

        free_svalue(inter_sp); inter_sp--; /* free the callback */
	command_giver = para;
    }

    return ret;
} /* tls_continue_handshake() */

/*-------------------------------------------------------------------------*/
svalue_t *
v_tls_init_connection (svalue_t *sp, int num_arg)

/* EFUN tls_init_connection()
 *
 *   int tls_init_connection(object ob)
 *   int tls_init_connection(object ob, string fun, string|object fob, mixed extra...)
 *   int tls_init_connection(object ob, closure fun, mixed extra...)
 *
 * tls_init_connection() tries to start a TLS secured connection to the
 * interactive object <ob> (or this_object() if <ob> is not given).
 * Result:
 *   errorcode < 0: unsuccessful, use tls_error() to get an useful
 *                  description of the error
 *      number > 0: the secure connection is still being set up in the
 *                   background
 *     number == 0: the secure connection is active.
 *
 * If the callback <fun>/<fun>:<fob> is specified, it will be called once
 * the fate of the secure connection has been determined. The first argument
 * will be the return code from the handshake (errorcode < 0 on failure,
 * or 0 on success), followed by the interactive object <ob> and any <extra>
 * arguments.
 */

{
    svalue_t * argp = sp - num_arg + 1;
    long ret;
    object_t * obj;
    interactive_t *ip;

    if (!tls_available)
        errorf("tls_init_connection(): TLS layer hasn't been initialized.\n");

    if (num_arg > 0)
    {
        obj = argp->u.ob;
        put_number(argp, 0); /* remove obj reference from the stack */
    }
    else
    {
        obj = ref_object(current_object, "tls_init_connection");
    }

    if (!O_SET_INTERACTIVE(ip, obj))
    {
        free_object(obj, "tls_init_connection");
        errorf("Bad arg 1 to tls_init_connection(): "
              "object not interactive.\n");
    }

    free_object(obj, "tls_init_connection");
      /* ip has another reference to obj, so this is safe to do */

    if (ip->tls_status != TLS_INACTIVE)
        errorf("tls_init_connection(): Interactive already has a secure "
              "connection.\n");

    /* Extract the callback information from the stack */
    if (num_arg > 1)
    {
        /* Extract the callback information from the stack */
        int error_index;
        callback_t * cb;

        inter_sp = sp;

        memsafe(cb = xalloc(sizeof *cb) , sizeof *cb , "callback structure");

        assign_eval_cost();

        error_index = setup_efun_callback(cb, argp+1, num_arg-1);

        if (error_index >= 0)
        {
            /* The callback values have already been removed. */
            
            xfree(cb);
            inter_sp = sp = argp;
            vefun_bad_arg(error_index+2, argp);
            /* NOTREACHED */
            return argp;
        }

        /* Callback creation successful */
        ip->tls_cb = cb;

        inter_sp = sp = argp;
    }

    /* Flush the connection */

    {
        object_t * save_c_g = command_giver;
        command_giver = obj;
        add_message(message_flush);
        command_giver = save_c_g;
    }

    ret = 0;

    do {

#ifdef HAS_OPENSSL

        SSL * session = SSL_new(context);

        if (session == NULL)
        {
            ret = - ERR_get_error();
            break;
        }

        if (!SSL_set_fd(session, ip->socket))
        {
            SSL_free(session);
            ret = - ERR_get_error();
            break;
        }
        if (ip->outgoing_conn) 
	{
#if 1
	    /* circumvent a bug in google talk or java ssl
	     * see also http://twistedmatrix.com/trac/changeset/25471
	     */
	    SSL_set_options(session, SSL_OP_NO_SSLv2 | SSL_OP_NO_TICKET);
#endif
	    /* simply using the contexts default would send old 
	     * handshakes not containing new features like compression
	     */
	    if (SSL_set_ssl_method(session, TLSv1_client_method()) != 1) {
		ret = - ERR_get_error();
		break;
	    }
            SSL_set_connect_state(session);
	}
        else
        {
            SSL_set_accept_state(session);
            /* request a client certificate */
	    if (ip->tls_want_peer_cert) {
		SSL_set_verify( session, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE
			      , tls_verify_callback);
	    } else {
		SSL_set_verify( session, SSL_VERIFY_NONE 
			      , tls_verify_callback);
	    }
        }
        ip->tls_session = session;
        
#elif defined(HAS_GNUTLS)

        initialize_tls_session(&ip->tls_session);
        gnutls_transport_set_ptr(ip->tls_session, (gnutls_transport_ptr)(ip->socket));

#endif /* SSL Package */

        ip->tls_status = TLS_HANDSHAKING;
	ret = tls_continue_handshake(ip);

        /* Adjust the return value of tls_continue_handshake() */
        if (ret == 1)
            ret = 0;
        else if (ret == 0)
            ret = 1;

    } while(0);

    put_number(sp, ret);
    return sp;
} /* f_tls_init_connection() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_tls_check_certificate(svalue_t *sp)

/* EFUN tls_check_certificate()
 *
 *   mixed *tls_check_certificate(object obj);
 *   mixed *tls_check_certificate(object obj, int extra);
 * 
 * tls_check_certificate() checks the certificate of the secured
 * connection bound to <obj> (default is the current object).  If
 * <obj> is not interactive, or if TLS is not available, an error
 * is thrown.
 * 
 * If <obj> doesn't have a secure connection up and running, an
 * error is thrown.
 * Otherwise, the result is an array with these values:
 * 
 *   int [0]      : Result code of SSL_get_verify_result (see man 1 verify
 *                  subsection DIAGNOSTICS for possible values)
 *   array [1]          : array with 3*n entries of extra x509 data.
 *                       structure is:
 *                       3*i    : numerical form of object name, e.g. "2.5.4.3"
 *                       3*i + 1: long or short name if available, e.g. "commonName"
 *                       3*i + 2: value
 *   array [2]          : if extra is set:
 *                       array with 3*n entries of x509 extension data
 *                       data structure is:
 *                       3*i    : numerical form of extension name
 *                       3*i + 1: long or short name of extension name if available
 *                       3*i + 2: array of strings with the data structure of [1]
 *
 *   string [3]         :  if extra is bigger than 1, this contains the PEM 
 *                         encoded certificate. It is 0 if there was an error
 *
 *   string [4]         :  if extra is bigger than 2, this contains the SHA1
 *                         fingerprint of the certificate. It is 0 if there was 
 *                         an error. Note well that it is not in hex form!
 *
 * Note: a x509 certificate can have more than one object with the same name
 *
 * See associated documentation for code that generates more convient mapping
 * data structures
 */

{
    vector_t *v = NULL;
#ifdef HAS_OPENSSL
    X509 *peer;
    X509_NAME *subject;
    interactive_t *ip;
    int more;
  
    /* more information requested */
    more = sp->u.number;
    free_svalue(sp--);

    if (!tls_available)
        errorf("tls_check_certificate(): TLS layer hasn't been initialized.\n");

    if (!O_SET_INTERACTIVE(ip, sp->u.ob))
        errorf("Bad arg 1 to tls_check_certificate(): "
              "object not interactive.\n");

    if (ip->tls_status != TLS_ACTIVE) 
        errorf("tls_check_certificate(): object doesn't have a secure connection.\n");

    if (more < 0 || more > 2)
        errorf("tls_check_certificate(): invalid flag passed as second argument.\n");

    peer = SSL_get_peer_certificate(ip->tls_session);
    if (peer != NULL)
    {
        int i, j, len;
	// used by OBJ_obj2txt - anything larger than 4096 bytes will be trimmed
        char buf[4096]; 
        vector_t *extra = NULL;

        v = allocate_array(2 + more);

        /* the result of SSL verification, the most important thing here
         * see verify(1) for more details
         */
        put_number(&(v->item[0]), SSL_get_verify_result(ip->tls_session));

        subject = X509_get_subject_name(peer);

        j = X509_NAME_entry_count(subject);
        extra = allocate_array(3 * j);

        /* iterate all objects in the certificate */
        for (i = 0; i < j; i++)
        {
            X509_NAME_ENTRY *entry;
            ASN1_OBJECT *ob;

            entry = X509_NAME_get_entry(subject, i);
            ob = X509_NAME_ENTRY_get_object(entry);

            len = OBJ_obj2txt(buf, sizeof buf, ob, 1);
	    if (len > sizeof buf) len = sizeof buf;
            put_c_n_string(&(extra->item[3 * i]), buf, len);

            len = OBJ_obj2txt(buf, sizeof buf, ob, 0);
            put_c_n_string(&(extra->item[3 * i + 1]), buf, len);
            
            put_c_n_string(&(extra->item[3 * i + 2]), 
			   (char *)ASN1_STRING_data(X509_NAME_ENTRY_get_data(entry)),
			   ASN1_STRING_length(X509_NAME_ENTRY_get_data(entry)));
        }
        put_array(&(v->item[1]), extra);

        /* also get all information from extensions like subjectAltName */
        if (more >= 1)
        {
            vector_t *extensions = NULL;
            vector_t *extension = NULL;

            j = X509_get_ext_count(peer);
            extensions = allocate_array(3 * j);
            for (i = X509_get_ext_by_NID(peer, NID_subject_alt_name, -1)
                ; i != -1
                ; i = X509_get_ext_by_NID(peer, NID_subject_alt_name, i))
            {
                int iter, count;

                X509_EXTENSION *ext = NULL;
                STACK_OF(GENERAL_NAME) *ext_vals = NULL;

                ext = X509_get_ext(peer, i);
                if (ext == NULL) {
                    break;
                }
                /* extension name */
                len = OBJ_obj2txt(buf, sizeof buf, ext->object, 1);
		if (len > sizeof buf) len = sizeof buf;
                put_c_n_string(&(extensions->item[3 * i]), (char *)buf, len);

                len = OBJ_obj2txt(buf, sizeof buf, ext->object, 0);
		if (len > sizeof buf) len = sizeof buf;
                put_c_n_string(&(extensions->item[3 * i + 1]), (char *)buf, len);

                /* extension values */
                ext_vals = X509V3_EXT_d2i(ext);
                if (ext_vals == NULL) {
                    break;
                }

                count = sk_GENERAL_NAME_num(ext_vals);
                extension = allocate_array(3 * count);

                put_array(&(extensions->item[3 * i + 2]), extension);
                for (iter = 0; iter < count; iter++) {
                    GENERAL_NAME *ext_val = NULL;
                    ASN1_STRING *value = NULL;

                    ext_val = sk_GENERAL_NAME_value(ext_vals, iter);

                    switch(ext_val->type) {
                    case GEN_OTHERNAME:
                        value = ext_val->d.otherName->value->value.asn1_string;

                        len = OBJ_obj2txt(buf, sizeof buf, ext_val->d.otherName->type_id, 1);
			if (len > sizeof buf) len = sizeof buf;
                        put_c_n_string(&(extension->item[3 * iter]), buf, len);
                        len = OBJ_obj2txt(buf, sizeof buf, ext_val->d.otherName->type_id, 0);
			if (len > sizeof buf) len = sizeof buf;
                        put_c_n_string(&(extension->item[3 * iter + 1]), buf, len);
                        put_c_n_string(&(extension->item[3 * iter + 2]), 
				       (char*)ASN1_STRING_data(value),
				       ASN1_STRING_length(value));
                        break;
                    case GEN_DNS:
                        value = ext_val->d.dNSName;
                        put_c_n_string(&(extension->item[3 * iter]), "dNSName", 7);
                        put_c_n_string(&(extension->item[3 * iter + 1]), "dNSName", 7);
                        put_c_n_string(&(extension->item[3 * iter + 2]), 
				       (char*)ASN1_STRING_data(value),
				       ASN1_STRING_length(value));
                        break;
                    case GEN_EMAIL:
                        value = ext_val->d.rfc822Name;
                        put_c_n_string(&(extension->item[3 * iter]), "rfc822Name", 10);
                        put_c_n_string(&(extension->item[3 * iter + 1]), "rfc822Name", 10);
                        put_c_n_string(&(extension->item[3 * iter + 2]), 
				       (char*)ASN1_STRING_data(value),
				       ASN1_STRING_length(value));
                        break;
                    case GEN_URI:
                        value = ext_val->d.uniformResourceIdentifier;
                        put_c_n_string(&(extension->item[3 * iter]), "uniformResourceIdentifier", 25);
                        put_c_n_string(&(extension->item[3 * iter + 1]), "uniformResourceIdentifier", 25);
                        put_c_n_string(&(extension->item[3 * iter + 2]), 
				     (char*)ASN1_STRING_data(value),
				     ASN1_STRING_length(value));
                        break;

                    /* TODO: the following are unimplemented 
                     *                 and the structure is getting ugly 
                     */
                    case GEN_X400:
                    case GEN_DIRNAME:
                    case GEN_EDIPARTY:
                    case GEN_IPADD:
                    case GEN_RID:
                    default:
                        break;
                    }
                }
            }
            put_array(&(v->item[2]), extensions);
        }
	/* extract the certificate as PEM */
	if (more >= 2) 
	{
	    BIO *bio = BIO_new(BIO_s_mem());
	    char *tmp;
	    long l;
	    int l2;
	    l2 = PEM_write_bio_X509(bio, peer);
	    l = BIO_get_mem_data(bio, &tmp);
	    tmp[l] = 0;
	    if (l > 0)
		put_c_n_string(&(v->item[3]), tmp, l);
	    else 
		put_number(&(v->item[3]), 0);
	    BIO_free(bio);
	}
        X509_free(peer);
    }
#elif defined(HAS_GNUTLS)
    errorf( "%s TLS: Gnu TLS does not provide certificate checking yet."
          , time_stamp());
#endif

    free_svalue(sp);

    if (v != NULL)
        put_array(sp, v);
    else
        put_number(sp, 0);
    return sp;
} /* tls_check_certificate() */

/*-------------------------------------------------------------------------*/
void
tls_deinit_connection (interactive_t *ip)

/* Close the TLS connection for the interactive <ip> if there is one.
 */

{
#ifdef HAS_OPENSSL
    if (ip->tls_status != TLS_INACTIVE) {
	/* when an SSL_ERROR_SYSCALL is produced, calling
	 * SSL_shutdown may lead to a crash. SSL_set_shutdown
	 * seems to be safer.		--lynX
	 *
	 * update july: worse than that, SSL_shutdown has crashed
	 * on me even when SSL_read returned an innocent '0'.
	 * let's try SSL_set_shutdown by default. probably we
	 * don't need that TLS_BROKEN flag after all.
	 */
/*	if (ip->tls_status == TLS_BROKEN) { */
		SSL_set_shutdown(ip->tls_session,
		    SSL_SENT_SHUTDOWN|SSL_RECEIVED_SHUTDOWN);
/*	} else {
		SSL_shutdown(ip->tls_session);
	} */
        SSL_free(ip->tls_session);
        ip->tls_session = NULL;
    }
#elif defined(HAS_GNUTLS)
    if (ip->tls_status != TLS_INACTIVE) {
        gnutls_bye( ip->tls_session, GNUTLS_SHUT_WR);
        gnutls_deinit(ip->tls_session);
        ip->tls_session = NULL;
    }
#endif /* SSL Package */
    if (ip->tls_cb != NULL) {
        free_callback(ip->tls_cb);
        xfree(ip->tls_cb);
        ip->tls_cb = NULL;
    }
    ip->tls_status = TLS_INACTIVE;
} /* tls_deinit_connection() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_tls_deinit_connection(svalue_t *sp)

/* EFUN tls_deinit_connection()
 *
 *      void tls_deinit_connection(object ob)
 *
 * tls_deinit_connection() shuts down a TLS connection to the interactive
 * object <ob> (or this_object() if <ob> is not given) but the connection is
 * not closed.
 */

{
    interactive_t *ip;

    if (!O_SET_INTERACTIVE(ip, sp->u.ob))
        errorf("Bad arg 1 to tls_deinit_connection(): "
              "object not interactive.\n");

    /* Flush the connection */

    {
        object_t * save_c_g = command_giver;
        command_giver = sp->u.ob;
        add_message(message_flush);
        command_giver = save_c_g;
    }

    tls_deinit_connection(ip);

    free_svalue(sp--);
    return sp;
} /* f_tls_deinit_connection() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_tls_error(svalue_t *sp)

/* EFUN tls_error()
 *
 *     string tls_error(int errorno)
 *
 * tls_error() returns a string describing the error behind the
 * error number <errorno>.
 */

{
    string_t *s;
    const char *text;
    int err = sp->u.number;

#ifdef HAS_PSYC
    if (err == ERR_TLS_NOT_DETECTED) text = "Unencrypted connection detected";
    else
#endif
    {
#ifdef HAS_OPENSSL
	text = ERR_error_string(-err, NULL);
#elif defined(HAS_GNUTLS)
	text = gnutls_strerror(err);
#endif /* SSL Package */
    }

    if (text)
    {
        memsafe(s = new_mstring(text), strlen(text), "tls_error()");
        free_svalue(sp);
        put_string(sp, s);
    }
    else
    {
        free_svalue(sp);
        put_number(sp, 0);
    }

    return sp;
} /* f_tls_error() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_tls_query_connection_state (svalue_t *sp)

/* EFUN tls_query_connection_state()
 *
 *      int tls_query_connection_state(object ob)
 *
 * tls_query_connection_state() returns a positive number if <ob>'s connection
 * is TLS secured, 0 if it's unsecured, and a negative number if the
 * TLS connection setup is still being set-up.
 * Returns 0 for non-interactive objects.
 */

{
    interactive_t *ip;
    Bool rc;

    if (!O_SET_INTERACTIVE(ip, sp->u.ob))
        rc = 0;
    else if (ip->tls_status == TLS_HANDSHAKING)
        rc = -1;
    else if (ip->tls_status == TLS_INACTIVE)
        rc = 0;
    else
        rc = 1;
    free_svalue(sp);
    put_number(sp, rc);
    return sp;
} /* f_tls_query_connection_state() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_tls_query_connection_info (svalue_t *sp)

/* EFUN tls_query_connection_info()
 *
 *
 *       #include <sys/ tls.h>
 *       int *tls_query_connection_info (object ob)
 *
 * If <ob> does not have a TLS connection, or if the TLS connection is
 * still being set up, the efun returns 0.
 *
 * If <ob> has a TLS connection, tls_query_connection_info() returns an array
 * that contains some parameters of <ob>'s connection:
 *
 *    int|string [TLS_CIPHER]: the cipher used
 *    int        [TLS_COMP]:   the compression used
 *    int        [TLS_KX]:     the key-exchange used
 *    int        [TLS_MAC]:    the digest algorithm used
 *    int|string [TLS_PROT]:   the protocol used
 *    string     [TLS_SESSION]: the session id
 *
 * To translate these numbers into strings, <tls.h> offers a number of macros:
 *
 *    TLS_xxx_TABLE: a literal array of strings describing the value in
 *        question.
 *    TLS_xxx_NAME(x): a macro translating the numeric result value into a
 *        string.
 *
 *    xxx: CIPHER, COMP, KX, MAC, PROT
 */

{
    interactive_t *ip;

    if (O_SET_INTERACTIVE(ip, sp->u.ob) && ip->tls_status == TLS_ACTIVE)
    {
        vector_t * rc;
        rc = allocate_array(TLS_INFO_MAX);
#ifdef HAS_OPENSSL
        put_c_string(&(rc->item[TLS_CIPHER])
                    , SSL_get_cipher(ip->tls_session));
        put_number(&(rc->item[TLS_COMP]), 0);
        put_number(&(rc->item[TLS_KX]), 0);
        put_number(&(rc->item[TLS_MAC]), 0);
        put_c_string(&(rc->item[TLS_PROT])
                    , SSL_get_version(ip->tls_session));
	/* warning: this session id is binary .. maybe fix it someday */
	put_c_string(&(rc->item[TLS_SESSION])
		    , (char*) ip->tls_session->session->session_id);
#elif defined(HAS_GNUTLS)
        put_number(&(rc->item[TLS_CIPHER])
                  , gnutls_cipher_get(ip->tls_session));
        put_number(&(rc->item[TLS_COMP])
                  , gnutls_compression_get(ip->tls_session));
        put_number(&(rc->item[TLS_KX])
                  , gnutls_kx_get(ip->tls_session));
        put_number(&(rc->item[TLS_MAC])
                  , gnutls_mac_get(ip->tls_session));
        put_number(&(rc->item[TLS_PROT])
                  , gnutls_protocol_get_version(ip->tls_session));
        put_c_string(&(rc->item[TLS_SESSION])
	          , "");
#endif /* SSL Package */
        free_svalue(sp);
        put_array(sp, rc);
    }
    else
    {
        free_svalue(sp);
        put_number(sp, 0);
    }

    return sp;
} /* tls_query_connection_info() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_tls_available (svalue_t *sp)

/* EFUN tls_available()
 *
 *       int tls_available ()
 *
 * If the global TLS Initialisation could not been set up, tls_available()
 * returns 0, otherwise 1.
 */

{
  sp++;
  put_number(sp, tls_available == MY_TRUE ? 1 : 0);
  return sp;
} /* f_tls_available() */


/*-------------------------------------------------------------------------*/
svalue_t *
f_tls_want_peer_certificate (svalue_t *sp)
/* EFUN tls_want_peer_certificate
 *
 * 	void tls_want_peer_certificate(obj = this_object())
 *
 * Specify if a subsequent call to tls_init_connection should request
 * a peer certificate.
 */
{
    interactive_t *ip;
  
    if (!tls_available)
        errorf("tls_want_peer_certificate(): TLS layer hasn't been initialized.\n");

    if (!O_SET_INTERACTIVE(ip, sp->u.ob))
        errorf("Bad arg 1 to tls_want_peer_certificate(): "
              "object not interactive.\n");
    ip->tls_want_peer_cert = MY_TRUE;

    free_svalue(sp--);
    return sp;
}

/*------------------------------------------------------------------
 * Interface to the openssl cryptography api
 *------------------------------------------------------------------
 */
#ifdef HAS_OPENSSL

static void
get_digest (int num, const EVP_MD **md, int *len)

/* Determine the proper digest descriptor <*md> and length <*len>
 * from the designator <num>, which is one of the TLS_HASH_ constants.
 *
 * Return NULL for <*md> if the desired digest isn't available.
 */

{
    switch(num)
    {
#ifndef OPENSSL_NO_SHA1
# ifdef SHA_DIGEST_LENGTH
    case TLS_HASH_SHA1:
	(*len) = SHA_DIGEST_LENGTH;
	(*md) = EVP_sha1();
	break;
# endif
#endif
#ifndef OPENSSL_NO_SHA256
# ifdef SHA224_DIGEST_LENGTH
    case TLS_HASH_SHA224:
	(*len) = SHA224_DIGEST_LENGTH;
	(*md) = EVP_sha224();
	break;
# endif
# ifdef SHA256_DIGEST_LENGTH
    case TLS_HASH_SHA256:
	(*len) = SHA256_DIGEST_LENGTH;
	(*md) = EVP_sha256();
	break;
# endif
#endif
#ifndef OPENSSL_NO_SHA512
# ifdef SHA384_DIGEST_LENGTH
    case TLS_HASH_SHA384:
	(*len) = SHA384_DIGEST_LENGTH;
	(*md) = EVP_sha384();
	break;
# endif
# ifdef SHA512_DIGEST_LENGTH
    case TLS_HASH_SHA512:
	(*len) = SHA512_DIGEST_LENGTH;
	(*md) = EVP_sha512();
	break;
# endif
#endif
#ifndef OPENSSL_NO_MD5
# ifdef MD5_DIGEST_LENGTH
    case TLS_HASH_MD5:
	(*len) = MD5_DIGEST_LENGTH;
	(*md) = EVP_md5();
	break;
# endif
#endif
#ifndef OPENSSL_NO_RIPEMD
# ifdef RIPEMD160_DIGEST_LENGTH
    case TLS_HASH_RIPEMD160:
	(*len) = RIPEMD160_DIGEST_LENGTH;
	(*md) = EVP_ripemd160();
	break;
# endif
#endif
    default:
	(*md) = NULL;
	break;
    }
} /* get_digest() */
#endif /* HAS_OPENSSL */

/*-------------------------------------------------------------------------*/
svalue_t *
v_hash(svalue_t *sp, int num_arg)

/* EFUN hash()
 *
 *   string hash(int method, string arg [, int iterations ] )
 *   string hash(int method, int *  arg [, int iterations ] )
 *
 * Calculate the hash from <arg> as determined by <method>. The
 * hash is calculated with <iterations> iterations, default is 1 iteration.
 *
 * <method> is one of the TLS_HASH_ constants defined in tls.h; not
 * all recognized methods may be supported for a given driven.
 */

{
#ifdef HAS_GNUTLS
    errorf("GnuTLS does not provide the hash API.\n");
    /* NOTREACHED */
#else
    EVP_MD_CTX ctx;
    const EVP_MD *md = NULL;
    char *tmp;
    string_t *digest;
    int i, hashlen;
    unsigned int len;
    p_int iterations;

    if (num_arg == 3)
    {
        iterations = sp->u.number;
        sp--;
    }
    else
        iterations = 1;

    if (iterations < 1)
    {
        errorf("Bad argument 3 to hash(): expected a number > 0, but got %ld\n"
              , (long) iterations);
        /* NOTREACHED */
        return sp;
    }

    if (sp->type == T_POINTER)
    {
        string_t * arg;
        char * argp;

        memsafe(arg = alloc_mstring(VEC_SIZE(sp->u.vec)), VEC_SIZE(sp->u.vec)
               , "hash argument string");
        argp = get_txt(arg);

        for (i = 0; i < VEC_SIZE(sp->u.vec); i++)
        {
            if (sp->u.vec->item[i].type != T_NUMBER)
            {
                free_mstring(arg);
                errorf("Bad argument 2 to hash(): got mixed*, expected string/int*.\n");
                /* NOTREACHED */
            }
            argp[i] = (char)sp->u.vec->item[i].u.number & 0xff;
        }

        free_svalue(sp);
        put_string(sp, arg);
    }

    get_digest(sp[-1].u.number, &md, &hashlen);

    if (md == NULL)
    {
        errorf("Bad argument 1 to hash(): hash function %d unknown or unsupported by OpenSSL\n", (int) sp[-1].u.number);
    }

    memsafe(tmp = xalloc(hashlen), hashlen, "hash result");

    EVP_DigestInit(&ctx, md);
    EVP_DigestUpdate(&ctx, (unsigned char *)get_txt(sp->u.str), 
		     mstrsize(sp->u.str));
    EVP_DigestFinal(&ctx, (unsigned char*)tmp, &len);

    while (--iterations > 0)
    {
        EVP_DigestInit(&ctx, md);
        EVP_DigestUpdate(&ctx, tmp, len);
	EVP_DigestFinal(&ctx, (unsigned char*)tmp, &len);
    }

    memsafe(digest = alloc_mstring(2 * len), 2 & len, "hex hash result");
    for (i = 0; i < len; i++)
        sprintf(get_txt(digest)+2*i, "%02x", tmp[i] & 0xff);
    free_svalue(sp--);
    free_svalue(sp);
    put_string(sp, digest);
#endif

    return sp;
} /* v_hash() */

/*-------------------------------------------------------------------------*/
svalue_t *
f_hmac(svalue_t *sp)

/* EFUN hmac()
 *
 *   string hmac(int method, string key, string arg)
 *   string hmac(int method, string key, int * arg)
 *
 * Calculate the Hashed Message Authenication Code for <arg> based
 * on the digest <method> and the password <key>. Return the HMAC.
 *
 * <method> is one of the TLS_HASH_ constants defined in tls.h; not
 * all recognized methods may be supported for a given driven.
 */

{
#ifdef HAS_GNUTLS
    errorf("GnuTLS does not provide the hash API\n");
    /* NOTREACHED */
#elif defined(OPENSSL_NO_HMAC)
    errorf("OpenSSL wasn't configured to provide the hmac() method.");
    /* NOTREACHED */
#else
    HMAC_CTX ctx;
    const EVP_MD *md = NULL;
    char *tmp;
    string_t *digest;
    int i, hashlen;
    unsigned int len;

    if (sp->type == T_POINTER)
    {
        string_t * arg;
        char * argp;

        memsafe(arg = alloc_mstring(VEC_SIZE(sp->u.vec)), VEC_SIZE(sp->u.vec)
               , "hash argument string");
        argp = get_txt(arg);

        for (i = 0; i < VEC_SIZE(sp->u.vec); i++)
        {
            if (sp->u.vec->item[i].type != T_NUMBER)
            {
                free_mstring(arg);
                errorf("Bad argument 2 to hash(): got mixed*, expected string/int*.\n");
                /* NOTREACHED */
            }
            argp[i] = (char)sp->u.vec->item[i].u.number & 0xff;
        }

        free_svalue(sp);
        put_string(sp, arg);
    }

    get_digest(sp[-2].u.number, &md, &hashlen);

    if (md == NULL)
    {
        errorf("Bad argument 1 to hmac(): hash function %d unknown or unsupported by OpenSSL\n", (int) sp[-2].u.number);
    }

    memsafe(tmp = xalloc(hashlen), hashlen, "hash result");

    HMAC_Init(&ctx, get_txt(sp[-1].u.str), mstrsize(sp[-1].u.str), md);
    HMAC_Update(&ctx, (unsigned char*)get_txt(sp->u.str), mstrsize(sp->u.str));
    HMAC_Final(&ctx, (unsigned char*)tmp, &len);

    memsafe(digest = alloc_mstring(2 * hashlen)
           , 2 & hashlen, "hmac result");
    for (i = 0; i < len; i++)
        sprintf(get_txt(digest)+2*i, "%02x", tmp[i] & 0xff);

    free_svalue(sp--);
    free_svalue(sp--);
    free_svalue(sp);
    put_string(sp, digest);
#endif

    return sp;
} /* f_hmac */

/***************************************************************************/
#endif /* USE_TLS */
