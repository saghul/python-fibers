
#include <stddef.h>
#include "fibers.h"

typedef struct {
    Fiber *origin;
    PyObject *value;
} FiberGlobalState;

static volatile FiberGlobalState _global_state;

static PyObject* main_fiber_key;
static PyObject* current_fiber_key;

static PyObject* PyExc_FiberError;


/*
 * Create main Fiber. There is always a main Fiber for a given (real) thread,
 * and it's parent is always NULL.
 */
static Fiber *
fiber_create_main(void)
{
    Fiber *t_main;
    PyObject *dict = PyThreadState_GetDict();
    PyTypeObject *cls = (PyTypeObject *)&FiberType;

    ASSERT(dict != NULL);

    /* create the main Fiber for this thread */
    t_main = (Fiber *)cls->tp_new(cls, NULL, NULL);
    if (!t_main) {
        return NULL;
    }
    Py_INCREF(dict);
    t_main->ts_dict = dict;
    t_main->parent = NULL;
    t_main->thread_h = stacklet_newthread();
    t_main->stacklet_h = NULL;
    t_main->initialized = True;
    t_main->is_main = True;
    return t_main;
}


/*
 * Get the current Fiber reference on the current thread. The first time this
 * function is called on a given (real) thread, the main Fiber is created.
 */
static Fiber *
get_current(void)
{
    Fiber *current;
    PyObject *tstate_dict;

    /* get current Fiber from the active thread-state */
    tstate_dict = PyThreadState_GetDict();
    if (tstate_dict == NULL) {
        if (!PyErr_Occurred()) {
            PyErr_NoMemory();
        }
        return NULL;
    }
    current = (Fiber *)PyDict_GetItem(tstate_dict, current_fiber_key);
    if (current == NULL) {
        current = fiber_create_main();
        if (current == NULL) {
            return NULL;
        }
        /* Keep a reference to the main fiber in the thread dict. The main
         * fiber is special because we don't require the user to keep a
         * reference to it. It should be deleted when the thread exits. */
        if (PyDict_SetItem(tstate_dict, main_fiber_key, (PyObject *) current) < 0) {
            Py_DECREF(current);
            return NULL;
        }
        /* current starts out as main */
        if (PyDict_SetItem(tstate_dict, current_fiber_key, (PyObject *) current) < 0) {
            Py_DECREF(current);
            return NULL;
        }
        /* return a borrowed ref. refcount should be 2 after this */
        Py_DECREF(current);
    }

    ASSERT(current != NULL);
    return current;
}


/*
 * Get current Fiber
 */
static PyObject *
fibers_func_current(PyObject *obj)
{
    Fiber *current;

    UNUSED_ARG(obj);

    if (!(current = get_current())) {
        return NULL;
    }
    Py_INCREF(current);
    return (PyObject *) current;
}


static int
Fiber_tp_init(Fiber *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"target", "args", "kwargs", "parent", NULL};

    PyObject *target, *t_args, *t_kwargs;
    Fiber *current, *parent;
    target = t_args = t_kwargs = NULL;
    parent = NULL;

    if (self->initialized) {
        PyErr_SetString(PyExc_RuntimeError, "object was already initialized");
        return -1;
    }

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|OOOO!:__init__", kwlist, &target, &t_args, &t_kwargs, &FiberType, &parent)) {
        return -1;
    }

    if (!(current = get_current())) {
        return -1;
    }

    if (parent) {
        /* check if parent is on the same (real) thread */
        if (parent->ts_dict != current->ts_dict) {
            PyErr_SetString(PyExc_FiberError, "parent cannot be on a different thread");
            return -1;
        }
        if (parent->stacklet_h == EMPTY_STACKLET_HANDLE) {
            PyErr_SetString(PyExc_ValueError, "parent must not have ended");
            return -1;
        }
    } else {
        parent = current;
    }

    if (target) {
        if (!PyCallable_Check(target)) {
            PyErr_SetString(PyExc_TypeError, "if specified, target must be a callable");
            return -1;
        }
        if (t_args) {
            if (!PyTuple_Check(t_args)) {
                PyErr_SetString(PyExc_TypeError, "args must be a tuple");
                return -1;
            }
        } else {
            t_args = PyTuple_New(0);
        }
        if (t_kwargs) {
            if (!PyDict_Check(t_kwargs)) {
                PyErr_SetString(PyExc_TypeError, "kwargs must be a dict");
                return -1;
            }
        }
    }

    Py_XINCREF(target);
    Py_XINCREF(t_args);
    Py_XINCREF(t_kwargs);
    self->target = target;
    self->args = t_args;
    self->kwargs = t_kwargs;

    Py_INCREF(parent);
    self->parent = parent;
    self->thread_h = parent->thread_h;
    self->ts_dict = parent->ts_dict;
    Py_INCREF(self->ts_dict);

    self->initialized = True;
    return 0;
}


static PyObject *
Fiber_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    Fiber *self = (Fiber *)PyType_GenericNew(type, args, kwargs);
    if (!self) {
        return NULL;
    }
    self->dict = NULL;
    self->ts_dict = NULL;
    self->weakreflist = NULL;
    self->parent = NULL;
    self->thread_h = NULL;
    self->stacklet_h = NULL;
    self->initialized = False;
    self->is_main = False;
    return (PyObject *)self;
}


static stacklet_handle
stacklet__callback(stacklet_handle h, void *arg)
{
    Fiber *origin, *self, *target;
    PyObject *result, *value;
    PyThreadState *tstate;
    stacklet_handle target_h;

    self = get_current();
    ASSERT(self != NULL);
    origin = _global_state.origin;
    value = _global_state.value;

    /* save the handle to switch back to the fiber that created us */
    origin->stacklet_h = h;

    /* set current thread state before starting this new Fiber */
    tstate = PyThreadState_Get();
    ASSERT(tstate != NULL);
    tstate->frame = NULL;
    tstate->exc_type = NULL;
    tstate->exc_value = NULL;
    tstate->exc_traceback = NULL;
    self->ts.recursion_depth = tstate->recursion_depth;
    self->ts.frame = NULL;
    self->ts.exc_type = NULL;
    self->ts.exc_value = NULL;
    self->ts.exc_traceback = NULL;

    if (value == NULL) {
        /* pending exception, user called throw on a non-started Fiber,
         * propagate to parent */
        result = NULL;
    } else if (self->target) {
        result = PyObject_Call(self->target, self->args, self->kwargs);
    } else {
        result = Py_None;
        Py_INCREF(Py_None);
    }

    /* cleanup target and arguments */
    Py_XDECREF(self->target);
    Py_XDECREF(self->args);
    Py_XDECREF(self->kwargs);
    self->target = NULL;
    self->args = NULL;
    self->kwargs = NULL;

    /* this Fiber has finished, select the parent as the next one to be run  */
    target_h = NULL;
    target = self->parent;
    while(target) {
        if (target->stacklet_h && target->stacklet_h != EMPTY_STACKLET_HANDLE) {
            _global_state.value = result;
            _global_state.origin = self;
            target_h = target->stacklet_h;
            break;
        }
        target = target->parent;
    }

    ASSERT(target_h);
    return target_h;
}


static PyObject *
do_switch(Fiber *self, PyObject *value)
{
    PyThreadState *tstate;
    stacklet_handle stacklet_h;
    Fiber *origin, *current;
    PyObject *result;

    /* save state */
    current = get_current();
    ASSERT(current != NULL);
    tstate = PyThreadState_Get();
    ASSERT(tstate != NULL);
    ASSERT(tstate->dict != NULL);
    current->ts.recursion_depth = tstate->recursion_depth;
    current->ts.frame = tstate->frame;
    current->ts.exc_type = tstate->exc_type;
    current->ts.exc_value = tstate->exc_value;
    current->ts.exc_traceback = tstate->exc_traceback;
    ASSERT(current->stacklet_h == NULL);

    /* _global_state is to pass values across a switch. Its contents are only
     * valid immediately before and after a switch. For any other purpose, the
     * current fiber is identified by current_fiber_key in the thread state
     * dictionary. */
    _global_state.origin = current;
    _global_state.value = value;

    /* make the target fiber the new current one. */
    if (PyDict_SetItem(tstate->dict, current_fiber_key, (PyObject *) self) < 0) {
        return NULL;
    }

    /* switch to existing, or create new fiber */
    if (self->stacklet_h == NULL) {
        stacklet_h = stacklet_new(self->thread_h, stacklet__callback, NULL);
    } else {
        stacklet_h = stacklet_switch(self->stacklet_h);
    }

    /* need to store the handle of the stacklet that switched to us, so that
     * later it can be resumed again. (stacklet_h can also be
     * EMPTY_STACKLET_HANDLE in which case the stacklet exited) */
    ASSERT(stacklet_h != NULL);
    origin = _global_state.origin;
    origin->stacklet_h = stacklet_h;
    current->stacklet_h = NULL;  /* handle is valid only once */
    result = _global_state.value;

    /* back to the fiber that did the switch. this may drop the refcount on
     * origin to zero. */
    if (PyDict_SetItem(tstate->dict, current_fiber_key, (PyObject *) current) < 0) {
        return NULL;
    }

    /* restore state */
    tstate->recursion_depth = current->ts.recursion_depth;
    tstate->frame = current->ts.frame;
    tstate->exc_type = current->ts.exc_type;
    tstate->exc_value = current->ts.exc_value;
    tstate->exc_traceback = current->ts.exc_traceback;
    current->ts.frame = NULL;
    current->ts.exc_type = NULL;
    current->ts.exc_value = NULL;
    current->ts.exc_traceback = NULL;

    return result;
}


static PyObject *
Fiber_func_switch(Fiber *self, PyObject *args)
{
    Fiber *current;
    PyObject *value = Py_None;

    if (!PyArg_ParseTuple(args, "|O:switch", &value)) {
        return NULL;
    }

    if (!(current = get_current())) {
        return NULL;
    }

    if (self == current) {
        PyErr_SetString(PyExc_FiberError, "cannot switch from a Fiber to itself");
        return NULL;
    }

    if (self->stacklet_h == EMPTY_STACKLET_HANDLE) {
        PyErr_SetString(PyExc_FiberError, "Fiber has ended");
        return NULL;
    }

    if (self->thread_h != current->thread_h) {
        PyErr_SetString(PyExc_FiberError, "cannot switch to a Fiber on a different thread");
        return NULL;
    }

    if (self->stacklet_h == NULL && value != Py_None) {
        PyErr_SetString(PyExc_ValueError, "cannot specify a value when the Fiber wasn't started");
        return NULL;
    }
    Py_INCREF(value);

    return do_switch(self, value);
}


static PyObject *
Fiber_func_throw(Fiber *self, PyObject *args)
{
    Fiber *current;
    PyObject *typ, *val, *tb;

    val = tb = NULL;

    if (!PyArg_ParseTuple(args, "O|OO:throw", &typ, &val, &tb)) {
	return NULL;
    }

    /* First, check the traceback argument, replacing None, with NULL */
    if (tb == Py_None) {
        tb = NULL;
    } else if (tb != NULL && !PyTraceBack_Check(tb)) {
	PyErr_SetString(PyExc_TypeError, "throw() third argument must be a traceback object");
	return NULL;
    }

    Py_INCREF(typ);
    Py_XINCREF(val);
    Py_XINCREF(tb);

    if (PyExceptionClass_Check(typ)) {
        PyErr_NormalizeException(&typ, &val, &tb);
    } else if (PyExceptionInstance_Check(typ)) {
        /* Raising an instance. The value should be a dummy. */
        if (val && val != Py_None) {
	    PyErr_SetString(PyExc_TypeError, "instance exceptions cannot have a separate value");
	    goto error;
	} else {
	    /* Normalize to raise <class>, <instance> */
	    Py_XDECREF(val);
            val = typ;
            typ = PyExceptionInstance_Class(typ);
	    Py_INCREF(typ);
	}
    } else {
	/* Not something you can raise. throw() fails. */
        PyErr_Format(PyExc_TypeError, "exceptions must be classes, or instances, not %s", Py_TYPE(typ)->tp_name);
	goto error;
    }

    if (!(current = get_current())) {
        goto error;
    }

    if (self == current) {
        PyErr_SetString(PyExc_FiberError, "cannot throw from a Fiber to itself");
        goto error;
    }

    if (self->stacklet_h == EMPTY_STACKLET_HANDLE) {
        PyErr_SetString(PyExc_FiberError, "Fiber has ended");
        goto error;
    }

    if (self->thread_h != current->thread_h) {
        PyErr_SetString(PyExc_FiberError, "cannot switch to a Fiber on a different thread");
        return NULL;
    }

    /* set error and do a switch with NULL as the value */
    PyErr_Restore(typ, val, tb);

    return do_switch(self, NULL);

error:
    /* Didn't use our arguments, so restore their original refcounts */
    Py_DECREF(typ);
    Py_XDECREF(val);
    Py_XDECREF(tb);
    return NULL;
}


static PyObject *
Fiber_func_is_alive(Fiber *self)
{
    Fiber *current;

    if (!(current = get_current())) {
        return NULL;
    }

    /* self->stacklet_h is only valid when self is not currently running */
    return PyBool_FromLong(current == self || self->stacklet_h != EMPTY_STACKLET_HANDLE);
}


static PyObject *
Fiber_func_getstate(Fiber *self)
{
    PyErr_Format(PyExc_TypeError, "cannot serialize '%s' object", Py_TYPE(self)->tp_name);
    return NULL;
}


static PyObject *
Fiber_dict_get(Fiber *self, void* c)
{
    UNUSED_ARG(c);

    if (self->dict == NULL) {
        self->dict = PyDict_New();
        if (self->dict == NULL) {
            return NULL;
        }
    }
    Py_INCREF(self->dict);
    return self->dict;
}


static int
Fiber_dict_set(Fiber *self, PyObject* val, void* c)
{
    PyObject* tmp;
    UNUSED_ARG(c);

    if (val == NULL) {
        PyErr_SetString(PyExc_AttributeError, "__dict__ may not be deleted");
        return -1;
    }
    if (!PyDict_Check(val)) {
        PyErr_SetString(PyExc_TypeError, "__dict__ must be a dictionary");
        return -1;
    }
    tmp = self->dict;
    Py_INCREF(val);
    self->dict = val;
    Py_XDECREF(tmp);
    return 0;
}


static PyObject *
Fiber_parent_get(Fiber *self, void* c)
{
	PyObject *result;
	UNUSED_ARG(c);

        if (self->parent) {
            result = (PyObject *)self->parent;
        } else {
            result = Py_None;
        }
	Py_INCREF(result);
	return result;
}


static int
Fiber_parent_set(Fiber *self, PyObject *val, void* c)
{
    Fiber *p, *nparent;
    UNUSED_ARG(c);

    if (val == NULL) {
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!PyObject_TypeCheck(val, &FiberType)) {
        PyErr_SetString(PyExc_TypeError, "parent must be a Fiber");
        return -1;
    }

    nparent = (Fiber *)val;
    for (p = nparent; p != NULL; p = p->parent) {
        if (p == self) {
            PyErr_SetString(PyExc_ValueError, "cyclic parent chain");
            return -1;
        }
    }

    if (nparent->stacklet_h == EMPTY_STACKLET_HANDLE) {
        PyErr_SetString(PyExc_ValueError, "parent must not have ended");
        return -1;
    }

    if (nparent->thread_h != self->thread_h) {
        PyErr_SetString(PyExc_ValueError, "parent cannot be on a different thread");
        return -1;
    }

    p = self->parent;
    self->parent = nparent;
    Py_INCREF(nparent);
    Py_XDECREF(p);

    return 0;
}


static int
Fiber_tp_traverse(Fiber *self, visitproc visit, void *arg)
{
    Py_VISIT(self->target);
    Py_VISIT(self->args);
    Py_VISIT(self->kwargs);
    Py_VISIT(self->dict);
    Py_VISIT(self->ts_dict);
    Py_VISIT(self->parent);
    Py_VISIT(self->ts.frame);
    Py_VISIT(self->ts.exc_type);
    Py_VISIT(self->ts.exc_value);
    Py_VISIT(self->ts.exc_traceback);
    return 0;
}


static int
Fiber_tp_clear(Fiber *self)
{
    Py_CLEAR(self->target);
    Py_CLEAR(self->args);
    Py_CLEAR(self->kwargs);
    Py_CLEAR(self->dict);
    Py_CLEAR(self->ts_dict);
    Py_CLEAR(self->parent);
    Py_CLEAR(self->ts.frame);
    Py_CLEAR(self->ts.exc_type);
    Py_CLEAR(self->ts.exc_value);
    Py_CLEAR(self->ts.exc_traceback);
    return 0;
}


static void
Fiber_tp_dealloc(Fiber *self)
{
    if (self->stacklet_h != NULL && self->stacklet_h != EMPTY_STACKLET_HANDLE) {
        stacklet_destroy(self->stacklet_h);
        self->stacklet_h = NULL;
    }
    if (self->is_main) {
        stacklet_deletethread(self->thread_h);
        self->thread_h = NULL;
    }
    if (self->weakreflist != NULL) {
        PyObject_ClearWeakRefs((PyObject *)self);
    }
    Py_TYPE(self)->tp_clear((PyObject *)self);
    Py_TYPE(self)->tp_free((PyObject *)self);
}


static PyMethodDef
Fiber_tp_methods[] = {
    { "current", (PyCFunction)fibers_func_current, METH_CLASS|METH_NOARGS, "Returns the current Fiber" },
    { "is_alive", (PyCFunction)Fiber_func_is_alive, METH_NOARGS, "Returns true if the Fiber can still be switched to" },
    { "switch", (PyCFunction)Fiber_func_switch, METH_VARARGS, "Switch execution to this Fiber" },
    { "throw", (PyCFunction)Fiber_func_throw, METH_VARARGS, "Switch execution and raise the specified exception to this Fiber" },
    { "__getstate__", (PyCFunction)Fiber_func_getstate, METH_NOARGS, "Serialize the Fiber object, not really" },
    { NULL }
};


static PyGetSetDef Fiber_tp_getsets[] = {
    {"__dict__", (getter)Fiber_dict_get, (setter)Fiber_dict_set, "Instance dictionary", NULL},
    {"parent", (getter)Fiber_parent_get, (setter)Fiber_parent_set, "Fiber parent or None if it's the main Fiber", NULL},
    {NULL}
};


static PyTypeObject FiberType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "fibers._cfibers.Fiber",                                        /*tp_name*/
    sizeof(Fiber),                                                  /*tp_basicsize*/
    0,                                                              /*tp_itemsize*/
    (destructor)Fiber_tp_dealloc,                                   /*tp_dealloc*/
    0,                                                              /*tp_print*/
    0,                                                              /*tp_getattr*/
    0,                                                              /*tp_setattr*/
    0,                                                              /*tp_compare*/
    0,                                                              /*tp_repr*/
    0,                                                              /*tp_as_number*/
    0,                                                              /*tp_as_sequence*/
    0,                                                              /*tp_as_mapping*/
    0,                                                              /*tp_hash */
    0,                                                              /*tp_call*/
    0,                                                              /*tp_str*/
    0,                                                              /*tp_getattro*/
    0,                                                              /*tp_setattro*/
    0,                                                              /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,  /*tp_flags*/
    0,                                                              /*tp_doc*/
    (traverseproc)Fiber_tp_traverse,                                /*tp_traverse*/
    (inquiry)Fiber_tp_clear,                                        /*tp_clear*/
    0,                                                              /*tp_richcompare*/
    offsetof(Fiber, weakreflist),	                            /*tp_weaklistoffset*/
    0,                                                              /*tp_iter*/
    0,                                                              /*tp_iternext*/
    Fiber_tp_methods,                                               /*tp_methods*/
    0,                                                              /*tp_members*/
    Fiber_tp_getsets,                                               /*tp_getsets*/
    0,                                                              /*tp_base*/
    0,                                                              /*tp_dict*/
    0,                                                              /*tp_descr_get*/
    0,                                                              /*tp_descr_set*/
    offsetof(Fiber, dict),                                          /*tp_dictoffset*/
    (initproc)Fiber_tp_init,                                        /*tp_init*/
    0,                                                              /*tp_alloc*/
    Fiber_tp_new,                                                   /*tp_new*/
};


static PyMethodDef
fibers_methods[] = {
    { "current", (PyCFunction)fibers_func_current, METH_NOARGS, "Get the current Fiber" },
    { NULL }
};


#if PY_MAJOR_VERSION >= 3
static PyModuleDef fibers_module = {
    PyModuleDef_HEAD_INIT,
    "fibers._cfibers",        /*m_name*/
    NULL,                     /*m_doc*/
    -1,                       /*m_size*/
    fibers_methods  ,         /*m_methods*/
};
#endif


/* Module */
PyObject *
init_fibers(void)
{
    PyObject *fibers;

    /* Main module */
#if PY_MAJOR_VERSION >= 3
    fibers = PyModule_Create(&fibers_module);
#else
    fibers = Py_InitModule("fibers._cfibers", fibers_methods);
#endif

    /* keys for per-thread dictionary */
#if PY_MAJOR_VERSION >= 3
    main_fiber_key = PyUnicode_InternFromString("__fibers_main");
    current_fiber_key = PyUnicode_InternFromString("__fibers_current");
#else
    main_fiber_key = PyString_InternFromString("__fibers_main");
    current_fiber_key = PyString_InternFromString("__fibers_current");
#endif
    if ((current_fiber_key == NULL) || (main_fiber_key == NULL)) {
        goto fail;
    }

    /* Exceptions */
    PyExc_FiberError = PyErr_NewException("fibers._cfibers.error", NULL, NULL);
    MyPyModule_AddType(fibers, "error", (PyTypeObject *)PyExc_FiberError);

    /* Types */
    MyPyModule_AddType(fibers, "Fiber", &FiberType);

    return fibers;

fail:
#if PY_MAJOR_VERSION >= 3
    Py_DECREF(fibers);
#endif
    return NULL;

}


#if PY_MAJOR_VERSION >= 3
PyMODINIT_FUNC
PyInit__cfibers(void)
{
    return init_fibers();
}
#else
PyMODINIT_FUNC
init_cfibers(void)
{
    init_fibers();
}
#endif

