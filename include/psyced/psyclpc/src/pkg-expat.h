#ifndef PKG_EXPAT_H__
#define PKG_EXPAT_H__ 1

#include "driver.h"

#ifdef USE_EXPAT

#ifndef HAS_EXPAT
#error "pkg-expat configured even though the machine doesn't support expat."
#endif

#include <unistd.h>
#include "typedefs.h"
#include "simulate.h"
#include <expat.h>

#include "xalloc.h"

/* --- Prototypes --- */
extern void expat_ElementStart(void *, const char *, const char **);
extern void expat_ElementEnd(void *, const char *);
extern void expat_ElementText(void *, const char *, int);
extern svalue_t *f_expat_parse(svalue_t *);
extern svalue_t *f_expat_init(svalue_t *);
extern svalue_t *f_expat_last_error(svalue_t *);
extern svalue_t *f_expat_position(svalue_t *);

#endif /* USE_EXPAT */

#endif /* PKG_EXPAT_H__ */
