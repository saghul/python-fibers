#ifndef PYFIBERS_H
#define PYFIBERS_H

/* python */
#define PY_SSIZE_T_CLEAN
#include "Python.h"

#if PY_MAJOR_VERSION >= 3
#endif

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
	struct _frame *frame;
	int recursion_depth;
#if PY_VERSION_HEX >= 0x03070000
        _PyErr_StackItem exc_state;
#else
        PyObject *exc_type;
        PyObject *exc_value;
        PyObject *exc_traceback;
#endif
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

/* Add an object to a module */
/*
static int
MyPyModule_AddObject(PyObject *module, const char *name, PyObject *value)
{
    Py_INCREF(value);
    if (PyModule_AddObject(module, name, value)) {
        Py_DECREF(value);
        return -1;
    }
    return 0;
}
*/

#endif

