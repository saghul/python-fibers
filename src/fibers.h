#ifndef PYFIBERS_H
#define PYFIBERS_H

/* python */
#define PY_SSIZE_T_CLEAN
#include "Python.h"

/* stacklet */
#include "stacklet.h"

/* Custom types */
typedef int Bool;
#define True  1
#define False 0

#define UNUSED_ARG(arg)  (void)arg

/* Python types */
typedef struct _fiber {
    PyObject_HEAD
    PyObject *ts_dict;
    PyObject *dict;
    PyObject *weakreflist;
    struct _fiber *parent;
    stacklet_thread_handle thread_h;
    stacklet_handle stacklet_h;
    Bool initialized;
    Bool is_main;
    PyObject *target;
    PyObject *args;
    PyObject *kwargs;
    struct {
#if PY_MINOR_VERSION >= 11
        _PyCFrame *cframe;
        _PyStackChunk *datastack_chunk;
        PyObject **datastack_top;
        PyObject **datastack_limit;
#endif
        struct _frame *frame;
        int recursion_depth;
        _PyErr_StackItem exc_state;
    } ts;
} Fiber;

static PyTypeObject FiberType;


/* Some helper stuff */
#ifdef _MSC_VER
    #define INLINE __inline
#else
    #define INLINE inline
#endif

#define ASSERT(x)                                                           \
    do {                                                                    \
        if (!(x)) {                                                         \
            fprintf (stderr, "%s:%u: Assertion `" #x "' failed.\n",         \
                     __FILE__, __LINE__);                                   \
            abort();                                                        \
        }                                                                   \
    } while(0)                                                              \


/* Add a type to a module */
static int
MyPyModule_AddType(PyObject *module, const char *name, PyTypeObject *type)
{
    if (PyType_Ready(type)) {
        return -1;
    }
    Py_INCREF(type);
    if (PyModule_AddObject(module, name, (PyObject *)type)) {
        Py_DECREF(type);
        return -1;
    }
    return 0;
}

#endif
