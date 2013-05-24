/*------------------------------------------------------------------
 * Glue for libpsyc.
 *------------------------------------------------------------------
 * test LPC code:
 *

#define CHECK_PARSE_RET if (ret != 0) raise_error(sprintf("FAIL: psyc_parse returned %d\n", ret))

string s, s2;

create() {
	mixed p, r;
	int ret;

	s = "=_context\ttest\n\n:_topic\ttesting\n_notice_test_libpsyc\nJust [_topic] libpsyc.\n|\n";
	ret = psyc_parse(s+s+s); CHECK_PARSE_RET;

	ret = psyc_parse("=_context\ttest\n\n:_topic\ttest"); CHECK_PARSE_RET;
	ret = psyc_parse("ing\n_notice_test_libpsyc\nJust [_topic] libpsyc.\n|\n"); CHECK_PARSE_RET;

	p = ({ ([ "_context": "test"; '=' ]), ([ "_topic": "testing" ]),
	       "_notice_test_libpsyc", "Just [_topic] libpsyc." });
	r = psyc_render(p);
	debug_message(sprintf(">> psyc_render returned:\n%s\n", r));
	if (r != s) raise_error(sprintf(">> FAIL, expected:\n%s\n", s));

	s  = "=_context\ttest\n\n:_amount\t4404\n:_list\t|4|4|0|4\n_notice_test_libpsyc\nJust [_topic] libpsyc.\n|\n";
	s2 = "=_context\ttest\n\n:_list\t|4|4|0|4\n:_amount\t4404\n_notice_test_libpsyc\nJust [_topic] libpsyc.\n|\n";
	p = ({ ([ "_context": "test"; '=' ]), ([ "_amount": 4404, "_list": ({4,4,0,4}) ]),
	       "_notice_test_libpsyc", "Just [_topic] libpsyc." });
	r = psyc_render(p);

	debug_message(sprintf(">> psyc_render returned:\n%s\n", r));
	if (r != s && r != s2) raise_error(sprintf(">> FAIL, expected:\n%s- or -\n%s\n", s, s2));

	s  = "=_context\ttest\n\n:_time_foo\t59\n:_time_bar\t-41\n_notice_test_libpsyc\nJust [_topic] libpsyc.\n|\n";
	s2 = "=_context\ttest\n\n:_time_bar\t-41\n:_time_foo\t59\n_notice_test_libpsyc\nJust [_topic] libpsyc.\n|\n";
	ret = psyc_parse(s); CHECK_PARSE_RET;

	s  = "=_context\ttest\n\n:_date_foo\t59\n:_date_bar\t-41\n_notice_test_libpsyc\nJust [_topic] libpsyc.\n|\n";
	s2 = "=_context\ttest\n\n:_date_bar\t-41\n:_date_foo\t59\n_notice_test_libpsyc\nJust [_topic] libpsyc.\n|\n";
	ret = psyc_parse(s); CHECK_PARSE_RET;

	s  = "=_context\ttest\n\n:_list_foo\t|foo|bar|baz\n:_list_bar\t\n_notice_test_libpsyc\nJust [_topic] libpsyc.\n|\n";
	s2 = "=_context\ttest\n\n:_list_bar\t\n:_list_foo\t|foo|bar|baz\n_notice_test_libpsyc\nJust [_topic] libpsyc.\n|\n";
	ret = psyc_parse(s); CHECK_PARSE_RET;

	p = ({ ([ "_context": "test"; '=' ]),
	       ([ "_list_foo": ({ "foo", "bar", "baz" }), "_list_bar": ({}) ]),
	       "_notice_test_libpsyc", "Just [_topic] libpsyc." });
	r = psyc_render(p);

	debug_message(sprintf(">> psyc_render returned:\n%s\n", r));
	if (r != s) raise_error(sprintf(">> FAIL, expected:\n%s\n", s));

	debug_message(sprintf(">> SUCCESS\n"));
}

psyc_dispatch(mixed p) {
	debug_message(sprintf(">> psyc_parse returned:\n%O\n", p));

	mixed r = psyc_render(p);
	debug_message(sprintf(">> psyc_render returned:\n%s", r));
	if (r != s && r != s2) raise_error(sprintf(">> FAIL, expected:\n%s- or -\n%s\n", s, s2));
}

 */

#include "machine.h"

#ifdef HAS_PSYC

# include "array.h"
# include "efuns.h"
# include "interpret.h"
# include "mapping.h"
# include "mstrings.h"
# include "object.h"
# include "pkg-psyc.h"
# include "simulate.h"
# include "xalloc.h"

# include <stdio.h>
# include <unistd.h>
# include <fcntl.h>

# include <psyc.h>
# include <psyc/parse.h>
# include <psyc/render.h>

void
fill_header_from_mapping (svalue_t *key, svalue_t *val, void *extra) {
    psyc_modifier_t *m = extra;
    char oper = 0;
    char *name, *value;
    size_t namelen, valuelen, i;
    uint8_t type;
    svalue_t vsp, *lval;

    psycList list;
    psycString *elems = NULL;

    if (key->type != T_STRING) {
	errorf("fill_header_from_mapping: key type %d not supported\n", key->type);
	return; // not reached
    }

    name = get_txt(key->u.str);
    namelen = mstrsize(key->u.str);
    type = psyc_getVarType2(name, namelen);

    if (m->num_values > 1)
	oper = val[1].u.number;
    if (!oper)
	oper = C_GLYPH_OPERATOR_SET;

    switch (val->type) {
	case T_STRING:
	    value = get_txt(val->u.str);
	    valuelen = mstrsize(val->u.str);
	    break;

	case T_NUMBER:
	case T_OBJECT:
	    vsp.type = val->type;
	    switch (val->type) {
		case T_NUMBER:
		    if (type == PSYC_TYPE_DATE)
			vsp.u.number = val->u.number - PSYC_EPOCH;
		    else
			vsp.u.number = val->u.number;
		    break;
		case T_OBJECT:
		    vsp.u.ob = val->u.ob;
		    break;
	    }

	    f_to_string(&vsp);	// generates an mstring
	    value = get_txt(vsp.u.str);
	    valuelen = mstrsize(vsp.u.str);
	    break;

	case T_POINTER:
	    if (VEC_SIZE(val->u.vec)) {
		elems = pxalloc(sizeof(psycString) * VEC_SIZE(val->u.vec));
		if (!elems) {
		    errorf("Out of memory in fill_header_from_mapping for elems\n");
		    return; // not reached
		}

		for (i = 0; i < VEC_SIZE(val->u.vec); i++) {
		    lval = &(val->u.vec->item[i]);
		    switch (lval->type) {
			case T_STRING:
			    elems[i] = (psycString){mstrsize(lval->u.str), get_txt(lval->u.str)};
			    break;
			case T_NUMBER:
			case T_OBJECT:
			    vsp.type = lval->type;
			    switch (lval->type) {
				case T_NUMBER:
				    vsp.u.number = lval->u.number;
				    break;
				case T_OBJECT:
				    vsp.u.ob = lval->u.ob;
				    break;
			    }

			    f_to_string(&vsp);
			    elems[i] = (psycString){mstrsize(vsp.u.str), get_txt(vsp.u.str)};
			    break;
			default:
			    errorf("fill_header_from_mapping: list value type %d not supported\n", lval->type);
			    return; // not reached
		    }
		}
	    }

	    list = psyc_newList(elems, VEC_SIZE(val->u.vec), PSYC_LIST_CHECK_LENGTH);
	    valuelen = list.length;
	    value = pxalloc(valuelen);
	    if (!value) {
		errorf("Out of memory in fill_header_from_mapping for list value\n");
		return; // not reached
	    }

	    psyc_renderList(&list, value, valuelen);
	    break;

	default:
	    errorf("fill_header_from_mapping: value type %d not supported\n", val->type);
	    return; // not reached
    }

    m->header->modifiers[m->header->lines++] =
	psyc_newModifier2(oper, name, namelen, value, valuelen, m->flag);
}

/*-------------------------------------------------------------------------*/
// old: string  psyc_render(mapping, mapping, string, int* | string);
// new: string  psyc_render(mixed*);

svalue_t *
f_psyc_render(svalue_t *sp) {
    uint8_t i;
    vector_t *v;
    string_t *out;
    char *meth, *body;
    size_t mlen, blen;
    mapping_t *map;

    psycPacket packet;
    psycHeader headers[2];

    // unless (sp->type == T_POINTER) return sp;
    v = sp->u.vec;
    if (VEC_SIZE(v) == PACKET_BODY + 1) {
	for (i = PACKET_ROUTING; i <= PACKET_ENTITY; i++) {
	    headers[i].lines = 0;
	    if (v->item[i].type == T_MAPPING) {
		map = v->item[i].u.map;
		if (!MAP_SIZE(map)) continue;

		headers[i].modifiers = malloc(sizeof(psycModifier) * MAP_SIZE(v->item[i].u.map));
		if (!headers[i].modifiers) {
		    errorf("Out of memory in psyc_render for modifier table.\n");
		    return sp; // not reached
		}

		walk_mapping(map, &fill_header_from_mapping,
		             &(psyc_modifier_t) {
		                 &headers[i], map->num_values,
		                 i == PACKET_ROUTING ?
		                     PSYC_MODIFIER_ROUTING :
		                     PSYC_MODIFIER_CHECK_LENGTH
		             });
	    }
	    // else ... ignoring possibly invalid args
	}
    } else {
	errorf("Wrong number of elements (%" PRIdMPINT ") "
	       "in array argument to psyc_render()\n", VEC_SIZE(v));
	return sp; // not reached
    }

    if (v->item[PACKET_METHOD].type == T_STRING) {
	meth = get_txt(v->item[PACKET_METHOD].u.str);
	mlen = mstrsize(v->item[PACKET_METHOD].u.str);
    } else {
	meth = NULL;
	mlen = 0;
    }

    if (v->item[PACKET_BODY].type == T_STRING) {
	body = get_txt(v->item[PACKET_BODY].u.str);
	blen = mstrsize(v->item[PACKET_BODY].u.str);
    } else {
	body = NULL;
	blen = 0;
    }

    packet = psyc_newPacket2(headers[PACKET_ROUTING].modifiers,
                             headers[PACKET_ROUTING].lines,
                             headers[PACKET_ENTITY].modifiers,
                             headers[PACKET_ENTITY].lines,
                             meth, mlen, body, blen,
                             PSYC_PACKET_CHECK_LENGTH);

#ifdef DEBUG
    printf("rendering... packet.length = %ld\n", packet.length);
#endif
    // alloc_mstring creates an *untabled* string suitable for tmp data 
    memsafe(out = alloc_mstring(packet.length), packet.length, "f_psyc_render");
    psyc_render(&packet, get_txt(out), packet.length);

    free_svalue(sp);
    put_string(sp, out);
    // stack should take care of freeing the string after use
    return sp;

} /* f_psyc_render */

/*-------------------------------------------------------------------------*/
// int  psyc_parse(int* | string);

static string_t *psyc_dispatch_callback = NULL;
static string_t *psyc_error_callback = NULL;

svalue_t *
f_psyc_parse (svalue_t *sp) {
    char *buffer = NULL;
    svalue_t *sv;
    vector_t *v, *list;
    mapping_t *map;
    char oper = 0;
    psycString name = {0,0}, value = {0,0}, elems[MAX_LIST_SIZE], elem;
    psycParseListState listState;
    int ret, retl, type = -1, error = 0;
    size_t size, i;
    ssize_t n;
    time_t timmy;

    if (!psyc_dispatch_callback)
      psyc_dispatch_callback = new_tabled("psyc_dispatch");

    if (!psyc_error_callback)
      psyc_error_callback = new_tabled("psyc_error");

    assert_shadow_sent(current_object);
    psyc_state_t *state = O_GET_PSYC_STATE(current_object);
    if (!state) {
	state = pxalloc(sizeof(psyc_state_t));
	if (!state) {
	    errorf("Out of memory for psyc state struct.\n");
	    return sp; // not reached
	}
	O_GET_PSYC_STATE(current_object) = state;
	memset(state, 0, sizeof(psyc_state_t));

	state->parser = pxalloc(sizeof(psycParseState));
	if (!state->parser) {
	    errorf("Out of memory for psyc parse state struct.\n");
	    return sp; // not reached
	}
	psyc_initParseState(state->parser);
    }
    v = state->packet;

    if (sp->type == T_POINTER) {
	errorf("\npsyc_parse got %ld int* bytes... not supported yet\n",
	       VEC_SIZE(sp->u.vec));
	return sp; // not reached
    } else if (sp->type == T_STRING) {
#ifdef DEBUG
	printf("\npsyc_parse got a %ld bytes long string...\n", mstrsize(sp->u.str));
#endif
	if (state->remaining) {
	    // there are remaining bytes from the previous call to psyc_parse,
	    // copy them together with the newly arrived data
	    buffer = pxalloc(state->remaining_len + mstrsize(sp->u.str));
	    if (!buffer) {
		errorf("Out of memory for psyc_parse buffer.\n");
		return sp; // not reached
	    }
	    memcpy(buffer, state->remaining, state->remaining_len);
	    memcpy(buffer + state->remaining_len, get_txt(sp->u.str),
		   mstrsize(sp->u.str));
	    psyc_setParseBuffer2(state->parser, buffer,
				 state->remaining_len + mstrsize(sp->u.str));
	    pfree(state->remaining);
	    state->remaining = NULL;
	    state->remaining_len = 0;
	} else {
	    psyc_setParseBuffer2(state->parser, get_txt(sp->u.str),
				 mstrsize(sp->u.str));
	}
    } else {
	errorf("\npsyc_parse got type %d, not supported\n", sp->type);
	return sp; // not reached
    }

    do {
	ret = psyc_parse(state->parser, &oper, &name, &value);
#ifdef DEBUG
	printf("#%2d %c%.*s = %.*s\n", ret, oper ? oper : ' ',
	       (int)name.length, name.ptr, (int)value.length, value.ptr);
#endif
	if (!state->packet) {
	    state->packet = allocate_array(4);
	    if (!state->packet) {
		errorf("Out of memory for psyc_parse array.\n");
		return sp; // not reached
	    }
	    v = state->packet;

	    map = allocate_mapping(0, 2);	// empty mapping
	    if (!map) {
		errorf("Out of memory for psyc_parse routing header.\n");
		return sp; // not reached
	    }
	    put_mapping(&v->item[PACKET_ROUTING], map);
	    map = allocate_mapping(0, 2);	// empty mapping
	    if (!map) {
		errorf("Out of memory for psyc_parse entity header.\n");
		return sp; // not reached
	    }
	    put_mapping(&v->item[PACKET_ENTITY], map);
	}

	switch (ret) {
	    case PSYC_PARSE_ENTITY_START: case PSYC_PARSE_BODY_START:
		// save oper, name & value in state at the start of
		// incomplete entity or body
		state->oper = oper;
		state->name = mstring_alloc_string(name.length);
		memcpy(get_txt(state->name), name.ptr, name.length);
		if (!state->name) {
		    errorf("Out of memory for name.\n");
		    return sp; // not reached
		}

		// allocate memory for the total length of the value
		state->value_len = 0;
		state->value = mstring_alloc_string(psyc_getParseValueLength(state->parser));
		if (!state->value) {
		    errorf("Out of memory for value.\n");
		    return sp; // not reached
		}

		// fall thru
	    case PSYC_PARSE_ENTITY_CONT:  case PSYC_PARSE_BODY_CONT:
	    case PSYC_PARSE_ENTITY_END:   case PSYC_PARSE_BODY_END:
		// append value to tmp buffer in state
		memcpy(get_txt(state->value) + state->value_len, value.ptr, value.length);
		state->value_len += value.length;
	}

	if (ret == PSYC_PARSE_ENTITY_END || ret == PSYC_PARSE_BODY_END) {
	    // incomplete entity or body parsing done,
	    // set oper/name/value to the ones saved in state
	    oper = state->oper;
	    name.ptr = get_txt(state->name);
	    name.length = mstrsize(state->name);
	    value.ptr = get_txt(state->value);
	    value.length = mstrsize(state->value);
	}

	switch (ret) {
	    case PSYC_PARSE_ROUTING:
		sv = pxalloc(sizeof(svalue_t));

		// new_n_tabled fetches a reference of a probably existing
		// shared string
		put_string(sv, new_n_tabled(name.ptr, name.length));
		sv = get_map_lvalue(v->item[PACKET_ROUTING].u.map, sv);
		put_number(&sv[1], oper);
		// strings are capable of containing 0 so we can do this
		// for binary data too. let's use a tabled string even
		// for values of routing variables as they repeat a lot
		put_string(sv, new_n_tabled(value.ptr, value.length));
		break;

	    case PSYC_PARSE_ENTITY_START:
	    case PSYC_PARSE_ENTITY_CONT:
		break;

	    case PSYC_PARSE_ENTITY_END:
	    case PSYC_PARSE_ENTITY:
		sv = pxalloc(sizeof(svalue_t));

		if (ret == PSYC_PARSE_ENTITY)
		    put_string(sv, new_n_tabled(name.ptr, name.length));
		else // PSYC_PARSE_ENTITY_END
		    put_string(sv, make_tabled(state->name));

		sv = get_map_lvalue(v->item[PACKET_ENTITY].u.map, sv);
		put_number(&sv[1], oper);

		type = psyc_getVarType(&name);

		switch (type) {
		    case PSYC_TYPE_DATE: // number + PSYC_EPOCH
			if (psyc_parseDate(&value, &timmy))
			    put_number(sv, timmy);
			else
			    error = PSYC_PARSE_ERROR_DATE;
			break;
		    case PSYC_TYPE_TIME: // number
			if (psyc_parseTime(&value, &timmy))
			    put_number(sv, timmy);
			else
			    error = PSYC_PARSE_ERROR_TIME;
			break;
		    case PSYC_TYPE_AMOUNT: // number
			if (psyc_parseNumber(&value, &n))
			    put_number(sv, n);
			else
			    error = PSYC_PARSE_ERROR_AMOUNT;
			break;
		    case PSYC_TYPE_DEGREE: // first digit
			if (value.length && value.ptr[0] >= '0' && value.ptr[0] <= '9')
			    put_number(sv, value.ptr[0] - '0');
			else
			    error = PSYC_PARSE_ERROR_DEGREE;
			break;
		    case PSYC_TYPE_FLAG: // 0 or 1
			if (value.length && value.ptr[0] >= '0' && value.ptr[0] <= '1')
			    put_number(sv, value.ptr[0] - '0');
			else
			    error = PSYC_PARSE_ERROR_FLAG;
			break;
		    case PSYC_TYPE_LIST: // array
			size = 0;
			if (value.length) {
			    psyc_initParseListState(&listState);
			    psyc_setParseListBuffer(&listState, value);
			    elem = (psycString){0, 0};
			    do {
				retl = psyc_parseList(&listState, &elem);
				switch (retl) {
				    case PSYC_PARSE_LIST_END:
					retl = 0;
				    case PSYC_PARSE_LIST_ELEM:
					if (size >= MAX_LIST_SIZE) {
					    error = PSYC_PARSE_ERROR_LIST_TOO_LARGE;
					    break;
					}
					elems[size++] = elem;
					break;
				    default:
					error = PSYC_PARSE_ERROR_LIST;
				}
			    } while (retl > 0 && !error);
			}
			if (error) break;

			list = allocate_array(size);
			for (i = 0; i < size; i++)
			    put_string(&list->item[i], new_n_tabled(elems[i].ptr,
			                                            elems[i].length));

			put_array(sv, list);
			break;
		    default: // string
			if (ret == PSYC_PARSE_ENTITY)
			    // is it good to put entity variable values into the
			    // shared string table? probably yes.. but it's a guess
			    //t_string(sv, new_n_mstring(value.ptr, value.length));
			    put_string(sv, new_n_tabled(value.ptr, value.length));
			else // PSYC_PARSE_ENTITY_END
			    put_string(sv, state->value);
		}
		break;

	    case PSYC_PARSE_BODY_START:
	    case PSYC_PARSE_BODY_CONT:
		break;

	    case PSYC_PARSE_BODY_END:
		put_string(&v->item[PACKET_METHOD], make_tabled(state->name));
		put_string(&v->item[PACKET_BODY], state->value);
		break;

	    case PSYC_PARSE_BODY:
		// new_n_tabled gets the shared string for the method
		put_string(&v->item[PACKET_METHOD],
			   new_n_tabled(name.ptr, name.length));

		// allocate an untabled string for the packet body
		put_string(&v->item[PACKET_BODY],
			   new_n_mstring(value.ptr, value.length));
		break;

	    case PSYC_PARSE_COMPLETE:
		put_array(inter_sp, v);
		sapply(psyc_dispatch_callback, current_object, 1);
		state->packet = NULL;
		break;

	    case PSYC_PARSE_INSUFFICIENT:
		// insufficient data, save remaining bytes
		state->remaining_len = psyc_getParseRemainingLength(state->parser);
		if (state->remaining_len) {
		    state->remaining = pxalloc(state->remaining_len);
		    memcpy(state->remaining,
			   psyc_getParseRemainingBuffer(state->parser),
			   state->remaining_len);
		} else
		    state->remaining = NULL;

		ret = 0;
		break;

	    default:
		error = ret;
	}

	switch (ret) {
	    case PSYC_PARSE_BODY_END:
	    case PSYC_PARSE_ENTITY_END:
		// reset tmp buffers in state when incomplete
		// entity or body parsing is finished
		state->oper = 0;
		state->name = NULL;
		state->value = NULL;
	}
    } while (ret && !error);

    if (buffer)
	pfree(buffer);

    free_svalue(sp);
    put_number(sp, error);
    return sp;
} /* f_psyc_parse */

#endif /* HAS_PSYC */
