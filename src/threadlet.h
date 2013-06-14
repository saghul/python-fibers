#ifndef PYTHREADLET_H
#define PYTHREADLET_H

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
typedef struct _threadlet {
    PyObject_HEAD
    PyObject *ts_dict;
    PyObject *dict;
    PyObject *weakreflist;
    struct _threadlet *parent;
    stacklet_thread_handle thread_h;
    stacklet_handle stacklet_h;
    Bool initialized;
    PyObject *target;
    PyObject *args;
    PyObject *kwargs;
    struct {
	struct _frame *frame;
	int recursion_depth;
        PyObject *exc_type;
        PyObject *exc_value;
        PyObject *exc_traceback;
    } ts;
} Threadlet;

static PyTypeObject ThreadletType;


/* Some helper stuff */
#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)

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

/* Add a type to a module */
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

#endif

