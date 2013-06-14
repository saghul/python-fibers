
/********** A really minimal coroutine package for C **********
 * By Armin Rigo (slightly modified by saghul)
 */

#include <assert.h>
#include <string.h>

#include "stacklet.h"
#include "slp_platformselect.h"


void *(*_stacklet_switchstack)(void*(*)(void*, void*),
                               void*(*)(void*, void*),
                               void*) = NULL;

void (*_stacklet_initialstub)(struct stacklet_thread_s *,
                              stacklet_run_fn,
                              void *) = NULL;


/************************************************************/

static void g_save(struct stacklet_s* g, char* stop)
{
    /* Save more of g's stack into the heap -- at least up to 'stop'

       In the picture below, the C stack is on the left, growing down,
       and the C heap on the right.  The area marked with xxx is the logical
       stack of the stacklet 'g'.  It can be half in the C stack (its older
       part), and half in the heap (its newer part).

       g->stack_stop |________|
                     |xxxxxxxx|
                     |xxx __ stop       .........
                     |xxxxxxxx|    ==>  :       :
                     |________|         :_______:
                     |        |         |xxxxxxx|
                     |        |         |xxxxxxx|
      g->stack_start |        |         |_______| g+1

     */
    ptrdiff_t sz1 = g->stack_saved;
    ptrdiff_t sz2 = stop - g->stack_start;
    assert(stop <= g->stack_stop);

    if (sz2 > sz1) {
        char *c = (char *)(g + 1);
        memcpy(c+sz1, g->stack_start+sz1, sz2-sz1);
        g->stack_saved = sz2;
    }
}

/* Allocate and store in 'g_source' a new stacklet, which has the C
 * stack from 'old_stack_pointer' to 'g_current_stack_stop'.  It is
 * initially completely unsaved, so it is attached to the head of the
 * chained list of 'stack_prev'.
 */
static int g_allocate_source_stacklet(void *old_stack_pointer, struct stacklet_thread_s *thrd)
{
    struct stacklet_s *stacklet;
    ptrdiff_t stack_size = (thrd->g_current_stack_stop -
                            (char *)old_stack_pointer);

    thrd->g_source = malloc(sizeof(struct stacklet_s) + stack_size);
    if (thrd->g_source == NULL)
        return -1;

    stacklet = thrd->g_source;
    stacklet->stack_start = old_stack_pointer;
    stacklet->stack_stop  = thrd->g_current_stack_stop;
    stacklet->stack_saved = 0;
    stacklet->stack_prev  = thrd->g_stack_chain_head;
    stacklet->stack_thrd  = thrd;
    thrd->g_stack_chain_head = stacklet;
    return 0;
}

/* Save more of the C stack away, up to 'target_stop'.
 */
static void g_clear_stack(struct stacklet_s *g_target, struct stacklet_thread_s *thrd)
{
    struct stacklet_s *current = thrd->g_stack_chain_head;
    char *target_stop = g_target->stack_stop;

    /* save and unlink tealets that are completely within
       the area to clear. */
    while (current != NULL && current->stack_stop <= target_stop) {
        struct stacklet_s *prev = current->stack_prev;
        current->stack_prev = NULL;
        if (current != g_target) {
            /* don't bother saving away g_target, because
               it would be immediately restored */
            g_save(current, current->stack_stop);
        }
        current = prev;
    }

    /* save a partial stack */
    if (current != NULL && current->stack_start < target_stop)
        g_save(current, target_stop);

    thrd->g_stack_chain_head = current;
}

/* This saves the current state in a new stacklet that gets stored in
 * 'g_source', and save away enough of the stack to allow a jump to
 * 'g_target'.
 */
static void *g_save_state(void *old_stack_pointer, void *rawthrd)
{
    struct stacklet_thread_s *thrd = (struct stacklet_thread_s *)rawthrd;
    if (g_allocate_source_stacklet(old_stack_pointer, thrd) < 0)
        return NULL;
    g_clear_stack(thrd->g_target, thrd);
    return thrd->g_target->stack_start;
}

/* This saves the current state in a new stacklet that gets stored in
 * 'g_source', but returns NULL, to not do any restoring yet.
 */
static void *g_initial_save_state(void *old_stack_pointer, void *rawthrd)
{
    struct stacklet_thread_s *thrd = (struct stacklet_thread_s *)rawthrd;
    if (g_allocate_source_stacklet(old_stack_pointer, thrd) == 0)
        g_save(thrd->g_source, thrd->g_current_stack_marker);
    return NULL;
}

/* Save away enough of the stack to allow a jump to 'g_target'.
 */
static void *g_destroy_state(void *old_stack_pointer, void *rawthrd)
{
    struct stacklet_thread_s *thrd = (struct stacklet_thread_s *)rawthrd;
    thrd->g_source = EMPTY_STACKLET_HANDLE;
    g_clear_stack(thrd->g_target, thrd);
    return thrd->g_target->stack_start;
}

/* Restore the C stack by copying back from the heap in 'g_target',
 * and free 'g_target'.
 */
static void *g_restore_state(void *new_stack_pointer, void *rawthrd)
{
    /* Restore the heap copy back into the C stack */
    struct stacklet_thread_s *thrd = (struct stacklet_thread_s *)rawthrd;
    struct stacklet_s *g = thrd->g_target;
    ptrdiff_t stack_saved = g->stack_saved;

    assert(new_stack_pointer == g->stack_start);
    memcpy(g->stack_start, g+1, stack_saved);
    thrd->g_current_stack_stop = g->stack_stop;
    free(g);
    return EMPTY_STACKLET_HANDLE;
}

static void g_initialstub(struct stacklet_thread_s *thrd, stacklet_run_fn run, void *run_arg)
{
    struct stacklet_s *result;

    /* The following call returns twice! */
    result = (struct stacklet_s *) _stacklet_switchstack(g_initial_save_state,
                                                         g_restore_state,
                                                         thrd);
    if (result == NULL && thrd->g_source != NULL) {
        /* First time it returns.  Only g_initial_save_state() has run
           and has created 'g_source'.  Call run(). */
        thrd->g_current_stack_stop = thrd->g_current_stack_marker;
        result = run(thrd->g_source, run_arg);

        /* Then switch to 'result'. */
        thrd->g_target = result;
        _stacklet_switchstack(g_destroy_state, g_restore_state, thrd);

        assert(!"stacklet: we should not return here");
        abort();
    }
    /* The second time it returns. */
}


/************************************************************/

stacklet_thread_handle stacklet_newthread(void)
{
    struct stacklet_thread_s *thrd;

    if (_stacklet_switchstack == NULL) {
        /* set up the following global with an indirection, which is needed
           to prevent any inlining */
        _stacklet_initialstub = g_initialstub;
        _stacklet_switchstack = slp_switch;
    }

    thrd = malloc(sizeof(struct stacklet_thread_s));
    if (thrd != NULL)
        memset(thrd, 0, sizeof(struct stacklet_thread_s));
    return thrd;
}

void stacklet_deletethread(stacklet_thread_handle thrd)
{
    free(thrd);
}

stacklet_handle stacklet_new(stacklet_thread_handle thrd, stacklet_run_fn run, void *run_arg)
{
    long stackmarker;
    assert((char *)NULL < (char *)&stackmarker);
    if (thrd->g_current_stack_stop <= (char *)&stackmarker)
        thrd->g_current_stack_stop = ((char *)&stackmarker) + 1;

    thrd->g_current_stack_marker = (char *)&stackmarker;
    _stacklet_initialstub(thrd, run, run_arg);
    return thrd->g_source;
}

stacklet_handle stacklet_switch(stacklet_handle target)
{
    long stackmarker;
    stacklet_thread_handle thrd = target->stack_thrd;
    if (thrd->g_current_stack_stop <= (char *)&stackmarker)
        thrd->g_current_stack_stop = ((char *)&stackmarker) + 1;

    thrd->g_target = target;
    _stacklet_switchstack(g_save_state, g_restore_state, thrd);
    return thrd->g_source;
}

void stacklet_destroy(stacklet_handle target)
{
    if (target->stack_prev != NULL) {
        /* 'target' appears to be in the chained list 'unsaved_stack',
           so remove it from there.  Note that if 'thrd' was already
           deleted, it means that we left the thread and all stacklets
           still in the thread should be fully copied away from the
           stack --- so should have stack_prev == NULL.  In this case
           we don't even read 'stack_thrd', already deallocated. */
        stacklet_thread_handle thrd = target->stack_thrd;
        struct stacklet_s **pp = &thrd->g_stack_chain_head;
        for (; *pp != NULL; pp = &(*pp)->stack_prev)
            if (*pp == target) {
                *pp = target->stack_prev;
                break;
            }
    }
    free(target);
}

