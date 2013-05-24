/*---------------------------------------------------------------------------
 * fippo's lightweight wrapper for the expat xml parser
 *---------------------------------------------------------------------------
 */
#include "driver.h"

#ifdef USE_EXPAT
#include<stdio.h>
#include<sys/time.h>

#include "machine.h"
#include "typedefs.h"
#include "my-alloca.h"

#include "actions.h"
#include "array.h"
#include "gcollect.h"
#include "instrs.h"
#include "interpret.h"
#include "main.h"
#include "mapping.h"
#include "mstrings.h"
#include "simulate.h"
#include "stdstrings.h"
#include "xalloc.h"
#include "closure.h"
#include "object.h"
#include "mapping.h"

#include "pkg-expat.h"

#include<expat.h>
#include<stdio.h>

// TODO: these are copied from pkg-tls.c
static void *
expat_xalloc (size_t size)

/* Wrapper function so that expat will use the driver's allocator.
 * The wrapper is required as 'pxalloc' itself is a macro.
 */

{
    return pxalloc(size);
} /* expat_xalloc() */

/*-------------------------------------------------------------------------*/
static void *
expat_rexalloc (void *old, size_t size)

/* Wrapper function so that expat will use the driver's allocator.
 * The wrapper is required as 'prexalloc' itself is a macro.
 */

{
    return prexalloc(old, size);
} /* expat_rexalloc() */

/*-------------------------------------------------------------------------*/
static void
expat_xfree (void *p)

/* Wrapper function so that expat will use the driver's allocator.
 * The wrapper is not exactly required for pfree(),  but it keeps things
 * consistent.
 */

{
    return pfree(p);
} /* expat_xfree() */

/* memory handling suite */
const XML_Memory_Handling_Suite xml_memsuite = { 
    expat_xalloc, 
    expat_rexalloc, 
    expat_xfree 
};

void expat_ElementStart(void *data, const char *el, const char **attrs) 
{
    int i;
    int num_attributes;

    mapping_t *attributes;
    svalue_t attr_key, *attr_value;


    num_attributes = XML_GetSpecifiedAttributeCount(current_object->xml_parser) / 2;

    attributes = allocate_mapping(num_attributes, 1);
    
    for(i = 0; attrs[i]; i += 2) 
    {
	/* name -> value */
	put_c_string(&attr_key, attrs[i]);
	attr_value = get_map_lvalue(attributes, &attr_key);
	put_c_string(attr_value, attrs[i+1]);
	free_svalue(&attr_key);
    }
    /* call lpcish onElementStart with element name and params */
    push_c_string(inter_sp, el);
    push_mapping(inter_sp, attributes);

    secure_apply(new_mstring("xml_onStartTag"), current_object, 2);
}

void expat_ElementEnd(void *data, const char *el) 
{
    /* call lpcish onElementEnd */
    push_c_string(inter_sp, el);
    secure_apply(new_mstring("xml_onEndTag"), current_object, 1);
}

void expat_ElementText(void *data, const char *txt, int txtlen) 
{
    /* txt is not 0-terminated! */
    string_t *mtxt;

    memsafe(mtxt = new_n_mstring(txt, txtlen), txtlen, "string");
    /* call lpcish onText */
    push_string(inter_sp, mtxt);

    secure_apply(new_mstring("xml_onText"), current_object, 1);
}

svalue_t *
f_expat_parse(svalue_t *sp)
/*
 * int expat_parse(string document)
 * parses the string document
 * returns 1 when finished successfully, 0 if there was a parse error 
 */
{
    int ret;
    int err;

    if (current_object->xml_parser == NULL) 
    {
	errorf("%s expat_parse called without initialization.\n", time_stamp());
	/* NOTREACHED */
    }
    current_object->xml_error = MY_FALSE;
    /* we parse en block */
    err = XML_Parse(current_object->xml_parser, get_txt(sp->u.str), mstrsize(sp->u.str), 0);
    if (!err)
    {
	/* user should fetch the error with expat_lastError() */
	current_object->xml_error = MY_TRUE;
	ret = 0;
    } 
    else 
    {
	ret = 1;
    }
    /* free the document string */
    free_svalue(sp);
    put_number(sp, ret);
    return sp;
}
/*
 * void expat_init([string namespace separator [, string encoding]])
 * initializes the objects XML parser. 
 * only the first character of the namespace separator string is used.
 *
 * calling expat_init a second time will reset the parser.
 */
svalue_t *
v_expat_init(svalue_t *sp, int num_arg)
{
    char *separator = NULL; /* namespace separator */
    char *encoding = NULL;
    if (num_arg > 1)
    {
        separator = get_txt(sp->u.str);
	free_svalue(sp--);
    }
    if (num_arg > 0)
    {
	encoding = get_txt(sp->u.str);
	free_svalue(sp--);
    }
    if (1) {
	current_object->xml_parser = XML_ParserCreate_MM(encoding, &xml_memsuite, separator);
    } 
    else 
    {
	printf("parser reset\n"); 
	XML_ParserReset(current_object->xml_parser, encoding);
    }

    XML_SetCharacterDataHandler(current_object->xml_parser, 
				expat_ElementText);
    XML_SetElementHandler(current_object->xml_parser, 
			  expat_ElementStart, expat_ElementEnd);
    return sp;
}

svalue_t *
f_expat_last_error(svalue_t *sp)
{
    if (current_object->xml_error == MY_FALSE) 
    {
	push_number(sp, 0);
    } 
    else 
    {
	vector_t *ret;
	int errorcode;
	ret = allocate_array(5);
	errorcode = XML_GetErrorCode(current_object->xml_parser);
	put_c_string(&ret->item[0], XML_ErrorString(errorcode));
	put_number(&ret->item[1], errorcode);
	put_number(&ret->item[2], XML_GetErrorLineNumber(current_object->xml_parser));
	put_number(&ret->item[3], XML_GetErrorColumnNumber(current_object->xml_parser));
	put_number(&ret->item[4], XML_GetErrorByteIndex(current_object->xml_parser));

	sp++;
	put_array(sp, ret);
    }
    return sp;
}

svalue_t *
f_expat_position(svalue_t *sp) {
    if (current_object->xml_parser == NULL) 
	push_number(sp, -1);
    else 
	push_number(sp, XML_GetCurrentByteIndex(current_object->xml_parser));
    return sp;
}

svalue_t * 
f_expat_eventlen(svalue_t *sp) {
    if (current_object->xml_parser == NULL) 
	push_number(sp, -1);
    else 
	push_number(sp, XML_GetCurrentByteCount(current_object->xml_parser));
    return sp;

}

#endif /* USE_EXPAT */
