#ifndef ACTIONS_H__
#define ACTIONS_H__ 1

#include "driver.h"

#include "typedefs.h"  /* object_t */

/* --- Variables --- */

extern object_t *command_giver;
extern p_int alloc_action_sent;

/* --- Prototypes --- */

extern void free_action_temporaries(void);
extern void free_action_sent(action_t *p);
extern void remove_action_sent(object_t *ob, object_t *player);
extern void remove_shadow_action_sent(object_t *ob, object_t *player);
extern void remove_environment_sent(object_t *player);
extern void remove_shadow_actions (object_t *shadow, object_t *target);

extern void restore_command_context (rt_context_t *context);
extern Bool execute_command (char *str, object_t *ob);
extern svalue_t *v_add_action(svalue_t *sp, int num_arg);
extern svalue_t *v_command(svalue_t *sp, int num_arg);
extern svalue_t *f_disable_commands(svalue_t *sp);
extern svalue_t *f_enable_commands(svalue_t *sp);
extern svalue_t *f_execute_command(svalue_t *sp);
extern svalue_t *f_living(svalue_t *sp);
extern svalue_t *f_notify_fail(svalue_t *sp);
extern svalue_t *f_query_notify_fail(svalue_t *sp);
extern svalue_t *f_query_actions(svalue_t *sp);
extern svalue_t *f_query_verb(svalue_t *sp);
extern svalue_t *f_query_command(svalue_t *sp);
extern svalue_t *f_command_stack_depth(svalue_t *sp);
extern svalue_t *f_command_stack(svalue_t *sp);
extern svalue_t *f_set_modify_command(svalue_t *sp);
extern svalue_t *f_set_this_player(svalue_t *sp);
extern svalue_t *f_remove_action(svalue_t *sp);
extern svalue_t *f_match_command(svalue_t * sp);

#endif /* ACTIONS_H__ */
