#ifdef HAS_PSYC
# ifndef PKG_PSYC_H

/* pkg-psyc takes and produces PSYC packets in form
 * of an array of mapping, mapping, string and string
 * or int* where necessary.
 */

# include <psyc/packet.h>
# include <psyc/parse.h>

# include "array.h"
# include "xalloc.h"

# define PACKET_ROUTING 0
# define PACKET_ENTITY  1
# define PACKET_METHOD  2
# define PACKET_BODY    3

# define MAX_LIST_SIZE 1024

# define PSYC_PARSE_ERROR_AMOUNT 1
# define PSYC_PARSE_ERROR_DEGREE 2
# define PSYC_PARSE_ERROR_DATE 3
# define PSYC_PARSE_ERROR_TIME 4
# define PSYC_PARSE_ERROR_FLAG 5
# define PSYC_PARSE_ERROR_LIST 6
# define PSYC_PARSE_ERROR_LIST_TOO_LARGE 7

typedef struct psyc_state_s {
	psycParseState *parser;
	vector_t *packet;
	// tmp storage for incomplete modifier/body
	char oper;
	string_t *name;
	string_t *value;
	size_t value_len;
	// tmp storage for remaining unparsed bytes at the end of the buffer
	char *remaining;
	size_t remaining_len;
} psyc_state_t;

typedef struct psyc_modifier_s {
	psycHeader *header;
	p_int num_values;
	psycModifierFlag flag;
} psyc_modifier_t;

static inline void
psyc_free_state (psyc_state_t *ps) {
	if (!ps)
	    return;
	if (ps->name)
	    pfree((void *) ps->name);
	if (ps->value)
	    pfree((void *) ps->value);
	if (ps->remaining)
	    pfree((void *) ps->remaining);
	if (ps->parser)
	    pfree((void *) ps->parser);
	if (ps->packet)
	    free_array(ps->packet);
	memset(ps, 0, sizeof(psyc_state_t));
}

# define PKG_PSYC_H
# endif
#endif
