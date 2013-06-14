/********** A really minimal coroutine package for C **********/
#ifndef _STACKLET_H_
#define _STACKLET_H_

#include <stddef.h>
#include <stdlib.h>


/* A "stacklet handle" is an opaque pointer to a suspended stack.
 * Whenever we suspend the current stack in order to switch elsewhere,
 * stacklet.c passes to the target a 'stacklet_handle' argument that points
 * to the original stack now suspended.  The handle must later be passed
 * back to this API once, in order to resume the stack.  It is only
 * valid once.
 */
typedef struct stacklet_s *stacklet_handle;

#define EMPTY_STACKLET_HANDLE  ((stacklet_handle) -1)

typedef struct stacklet_thread_s *stacklet_thread_handle;

struct stacklet_s {
    /* The portion of the real stack claimed by this paused tealet. */
    char *stack_start;                /* the "near" end of the stack */
    char *stack_stop;                 /* the "far" end of the stack */

    /* The amount that has been saved away so far, just after this struct.
     * There is enough allocated space for 'stack_stop - stack_start'
     * bytes.
     */
    ptrdiff_t stack_saved;            /* the amount saved */

    /* Internally, some stacklets are arranged in a list, to handle lazy
     * saving of stacks: if the stacklet has a partially unsaved stack,
     * this points to the next stacklet with a partially unsaved stack,
     * creating a linked list with each stacklet's stack_stop higher
     * than the previous one.  The last entry in the list is always the
     * main stack.
     */
    struct stacklet_s *stack_prev;

    stacklet_thread_handle stack_thrd;  /* the thread where the stacklet is */
};

struct stacklet_thread_s {
    struct stacklet_s *g_stack_chain_head;  /* NULL <=> running main */
    char *g_current_stack_stop;
    char *g_current_stack_marker;
    struct stacklet_s *g_source;
    struct stacklet_s *g_target;
};


stacklet_thread_handle stacklet_newthread(void);
void stacklet_deletethread(stacklet_thread_handle thrd);


/* The "run" function of a stacklet.  The first argument is the handle
 * of the stack from where we come.  When such a function returns, it
 * must return a (non-empty) stacklet_handle that tells where to go next.
 */
typedef stacklet_handle (*stacklet_run_fn)(stacklet_handle, void *);

/* Call 'run(source, run_arg)' in a new stack. See stacklet_switch()
 * for the return value.
 */
stacklet_handle stacklet_new(stacklet_thread_handle thrd, stacklet_run_fn run, void *run_arg);

/* Switch to the target handle, resuming its stack.  This returns:
 *  - if we come back from another call to stacklet_switch(), the source handle
 *  - if we come back from a run() that finishes, EMPTY_STACKLET_HANDLE
 *  - if we run out of memory, NULL
 * Don't call this with an already-used target, with EMPTY_STACKLET_HANDLE,
 * or with a stack handle from another thread (in multithreaded apps).
 */
stacklet_handle stacklet_switch(stacklet_handle target);

/* Delete a stack handle without resuming it at all.
 * (This works even if the stack handle is of a different thread)
 */
void stacklet_destroy(stacklet_handle target);


#endif /* _STACKLET_H_ */
