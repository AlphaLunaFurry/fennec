/*------------------------------------------------------------------
 * Wrapper for the pcre modules.
 *
 * Compile the pcre modules into one file.
 * To make this possible the pcre/internal.h had to be augmented with
 * protection against multiple inclusion.
 *------------------------------------------------------------------
 */

#include "driver.h"

#include "pkg-pcre.h"

#if defined(USE_BUILTIN_PCRE)

#include "interpret.h"
#include "simulate.h"

/* Provide a definition for NEWLINE */
#define NEWLINE '\n'

/* DEBUG has a different meaning for pcre than for us */
#ifdef DEBUG
#    undef DEBUG
#endif

/* activated UTF8 support --lynX 2008 */
#define SUPPORT_UTF8

#include "pcre/pcre.c"
#include "pcre/get.c"
#include "pcre/maketables.c"
#include "pcre/study.c"

#endif /* USE_BUILTIN_PCRE */
