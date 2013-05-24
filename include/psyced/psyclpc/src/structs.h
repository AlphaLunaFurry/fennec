#ifndef STRUCTS_H_
#define STRUCTS_H_ 1

#include "driver.h"

#ifdef USE_STRUCTS

#include "typedefs.h"

#include "exec.h"
#include "hash.h"
#include "svalue.h"

/* --- Types --- */

/* --- struct struct_s: a LPC struct instance
 *
 * The LPC struct contains just the actual struct data; the structure
 * of the struct is stored in a separate struct_def_t instance and linked
 * to from the data structure.
 *
 * Like arrays a LPC struct is allocated large enough to hold all member
 * svalues.
 */

struct struct_s
{
    struct_type_t * type;  /* The type object */
    p_int           ref;   /* Number of references */
    wiz_list_t    * user;  /* Who made the struct */
    svalue_t        member[1 /* .type->num_members */ ];
      /* The struct member values */
};


/* --- struct struct_member_s: description of one struct member
 */

struct struct_member_s
{
    string_t * name;  /* Tabled name of the struct member */
    vartype_t  type;  /* The compile type of the member (see exec.h) */
};


/* --- struct struct_type_s: LPC struct typeobject
 *
 * All programs using structs link to these typestructure, as do
 * the struct instances themselves. The typestructures are organized
 * in a hash table for lookups, and thus contain the necessary structures
 * for it.
 *
 * The member descriptors are allocated in a separate block to allow
 * the use of struct prototypes.
 */

struct struct_type_s
{
    struct_type_t * next;      /* Next type in hash chain (uncounted) */
    whash_t         hash;      /* Hash value of this type */

    string_t      * name;      /* Tabled name of the struct */
    p_int           ref;       /* Number of references to this structure */
    struct_type_t * base;      /* Counted link to the base structure,
                                * or NULL if none
                                */
    string_t      * prog_name; /* Tabled name of the defining program.
                                * NULL for struct prototypes.
                                */
    int32           prog_id;   /* ID number of the defining program */
    string_t      * unique_name; /* The unique name of this struct,
                                  * composed from .name, .prog_name and
                                  * .prog_id. It is created the first
                                  * time it is queried.
                                  */
    unsigned short  num_members;  /* Number of data members */
    struct_member_t * member;
      /* The description of the struct members, including those from the
       * base structure (if any).
       * The descriptors are in order of appearance in the struct; if
       * larger structs become the norm, we might have to think about
       * a fast name->index lookup mechanism.
       * If the struct doesn't have any members, this pointer is NULL.
       */
};

/* --- Macros --- */

/* p_int struct_ref(struct *t)
 * p_int struct_t_ref(struct_type_t *t)
 *   Return the number of references to struct(type) <t>.
 */
#define struct_ref(t)   ((t)->type->ref)
#define struct_t_ref(t) ((t)->ref)


/* unsigned short struct_size(struct *t)
 * unsigned short struct_t_size(struct_type_t *t)
 *   Return the number of elements in struct(type) <t>.
 */
#define struct_size(t)   ((t)->type->num_members)
#define struct_t_size(t) ((t)->num_members)


/* string_t * struct_name(struct *t)
 * string_t * struct_t_name(struct_type_t *t)
 *   Return an uncounted reference to the struct name.
 */
#define struct_name(t)   ((t)->type->name)
#define struct_t_name(t) ((t)->name)


/* string_t * struct_pname(struct *t)
 * string_t * struct_t_pname(struct_type_t *t)
 *   Return an uncounted reference to the struct's prog_name.
 */
#define struct_pname(t)   ((t)->type->prog_name)
#define struct_t_pname(t) ((t)->prog_name)

/* int32 struct_pid(struct *t)
 * int32 struct_t_pid(struct_type_t *t)
 *   Return the ID of the struct's definint program.
 */
#define struct_pid(t)   ((t)->type->prog_id)
#define struct_t_pid(t) ((t)->prog_id)


/* struct_t *ref_struct(struct_t *t)
 *   Add another ref to struct <t> and return <t>.
 *
 * struct_type_t *ref_struct_type(struct_type_t *t)
 *   Add another ref to struct typeobject <t> and return <t>.
 */

#define ref_struct(t) ((t)->ref++, (t))
#define ref_struct_type(t) ((t)->ref++, (t))


/* void free_struct(struct_t *m)
 *   Subtract one ref from struct <t>, and free the struct
 *   fully if the refcount reaches zero.
 *
 * void free_struct_type(struct_type_t *m)
 *   Subtract one ref from struct typeobject <t>, and free the typeobject
 *   fully if the refcount reaches zero.
 */

#define free_struct(t) MACRO( if (--((t)->ref) <= 0) struct_free(t); )
#define free_struct_type(t) MACRO( if (--((t)->ref) <= 0) struct_free_type(t); )


/* p_int deref_struct(struct_t *t)
 *   Subtract one ref from struct <t>, but don't check if it needs to
 *   be freed. Result is the number of refs left.
 *
 * p_int deref_struct_type(struct_type_t *t)
 *   Subtract one ref from struct typeobject <t>, but don't check if it needs
 *   to be freed. Result is the number of refs left.
 */

#define deref_struct(t) (--(t)->ref)
#define deref_struct_type(t) (--(t)->ref)


/* void free_struct_member_data(struct_member_t *v)
 *   Free all data associated with struct member <v>.
 */

#define free_struct_member_data(v) \
    do { if ((v)->name) free_mstring((v)->name); free_vartype_data(&((v)->type)); } while(0)


#endif /* USE_STRUCTS */


/* --- Prototypes --- */

extern struct_t * struct_new (struct_type_t *pSType);
extern struct_type_t * struct_new_prototype ( string_t *name );
extern struct_type_t * struct_fill_prototype ( struct_type_t   *type
                                             , string_t        *prog_name
                                             , int32            prog_id
                                             , struct_type_t   *base
                                             , int              num_members
                                             , struct_member_t *member);
extern struct_type_t * struct_new_type ( string_t        *name
                                       , string_t        *prog_name
                                       , int32            prog_id
                                       , struct_type_t   *base
                                       , int              num_members
                                       , struct_member_t *member);
extern struct_t * struct_new_anonymous (int num_members);
extern void struct_free_empty (struct_t *pStruct);
extern void struct_free (struct_t *pStruct);
extern void struct_free_type (struct_type_t *pSType);
extern struct_type_t * struct_lookup_type ( struct_type_t * pSType );
extern void struct_publish_type ( struct_type_t * pSType );
extern Bool struct_type_equivalent (struct_type_t * pSType1, struct_type_t *pSType2);
extern void struct_type_update ( struct_type_t * pSType
                               , struct_type_t * pOld
                               , struct_type_t * pNew);
extern struct_type_t * struct_find (string_t *name, program_t * prog);
extern int struct_find_member ( struct_type_t * ptype, string_t * name );
extern void struct_check_for_destr ( struct_t * pStruct );
extern mp_int total_struct_size (strbuf_t *sbuf, Bool verbose);
extern void   struct_dinfo_status(svalue_t *svp, int value);
extern string_t * struct_t_unique_name (struct_type_t *pSType);
#define struct_unique_name(pStruct) struct_t_unique_name(pStruct->type)

#ifdef GC_SUPPORT

extern void clear_struct_type_ref (struct_type_t * pSType);
extern void clear_struct_ref (struct_t * pStruct);
extern void clear_tabled_struct_refs (void);
extern void count_struct_type_ref (struct_type_t * pSType);
extern void count_struct_ref (struct_t * pStruct);
extern void remove_unreferenced_structs (void);

#endif /* GC_SUPPORT */

extern svalue_t * x_map_struct (svalue_t *sp, int num_arg);
extern svalue_t * f_struct_info(svalue_t * sp);
extern svalue_t * f_baseof(svalue_t *sp);

#endif /* STRUCTS_H_ */
