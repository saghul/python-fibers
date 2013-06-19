
#include "threadlet.h"

typedef struct {
    Threadlet *current;
    Threadlet *origin;
    Threadlet *destination;
    PyObject *value;
} ThreadletGlobalState;

static ThreadletGlobalState _global_state;

static PyObject* ts_curkey;

static PyObject* PyExc_ThreadletError;
static PyObject* PyExc_ThreadletExit;


/*
 * Create main threadlet. The is always a main threadlet for a given (real) thread,
 * and it's parent is always NULL.
 */
static Threadlet *
threadlet_create_main(void)
{
    Threadlet *t_main;
    PyObject *dict = PyThreadState_GetDict();
    PyTypeObject *cls = (PyTypeObject *)&ThreadletType;

    if (dict == NULL) {
        if (!PyErr_Occurred()) {
            PyErr_NoMemory();
        }
        return NULL;
    }

    /* create the main threadlet for this thread */
    t_main = (Threadlet *)cls->tp_new(cls, NULL, NULL);
    if (!t_main) {
        return NULL;
    }
    Py_INCREF(dict);
    t_main->ts_dict = dict;
    t_main->parent = NULL;
    t_main->thread_h = stacklet_newthread();
    t_main->stacklet_h = NULL;
    t_main->initialized = True;
    return t_main;
}


/*
 * Update the current threadlet reference on the current thread. The first time this
 * function is called on a given (real) thread, the main threadlet is created.
 */
static Threadlet *
update_current(void)
{
	Threadlet *current, *previous;
	PyObject *exc, *val, *tb;
	PyThreadState *tstate;

restart:
	/* save current exception */
	PyErr_Fetch(&exc, &val, &tb);

	/* get current threadlet from the active thread-state */
	tstate = PyThreadState_GET();
	if (tstate->dict && (current = (Threadlet *) PyDict_GetItem(tstate->dict, ts_curkey))) {
	    /* found - remove it, to avoid keeping a ref */
	    Py_INCREF(current);
	    PyDict_DelItem(tstate->dict, ts_curkey);
	} else {
            /* first time we see this thread-state, create main threadlet */
            current = threadlet_create_main();
            if (current == NULL) {
                Py_XDECREF(exc);
                Py_XDECREF(val);
                Py_XDECREF(tb);
                return NULL;
	    }
	    if (_global_state.current == NULL) {
	        _global_state.current = current;
	    }
        }
	assert(current->ts_dict == tstate->dict);

	Py_INCREF(current);
retry:
	previous = _global_state.current;
	_global_state.current = current;

        /* save previous as the current threadlet of its own (real) thread */
        if (PyDict_SetItem(previous->ts_dict, ts_curkey, (PyObject*) previous) < 0) {
            Py_DECREF(previous);
            Py_DECREF(current);
            Py_XDECREF(exc);
            Py_XDECREF(val);
            Py_XDECREF(tb);
            return NULL;
        }
        Py_DECREF(previous);

	if (_global_state.current != current) {
            /* some Python code executed above and there was a thread switch,
             * so the global current points to some other threadlet again. We need to
             * delete ts_curkey and retry. */
            PyDict_DelItem(tstate->dict, ts_curkey);
            goto retry;
	}

	/* release the extra reference */
        Py_DECREF(current);

	/* restore current exception */
	PyErr_Restore(exc, val, tb);

	/* thread switch could happen during PyErr_Restore, in that
	   case there's nothing to do except restart from scratch. */
	if (_global_state.current->ts_dict != tstate->dict) {
            goto restart;
        }

	return current;
}


/*
 * sanity check, see if there is a current threadlet or create one
 */
#define CHECK_STATE  ((_global_state.current && _global_state.current->ts_dict == PyThreadState_GET()->dict) || update_current())


/*
 * Get current threadlet.
 */
static PyObject *
threadlet_func_get_current(PyObject *obj)
{
    UNUSED_ARG(obj);

    if (!CHECK_STATE) {
        return NULL;
    }
    Py_INCREF(_global_state.current);
    return (PyObject *)_global_state.current;
}


static int
Threadlet_tp_init(Threadlet *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"target", "args", "kwargs", "parent", NULL};

    PyObject *target, *t_args, *t_kwargs;
    Threadlet *parent;
    target = t_args = t_kwargs = NULL;
    parent = NULL;

    if (self->initialized) {
        PyErr_SetString(PyExc_RuntimeError, "object was already initialized");
        return -1;
    }

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|OOOO!:__init__", kwlist, &target, &t_args, &t_kwargs, &ThreadletType, &parent)) {
        return -1;
    }

    if (!CHECK_STATE) {
        return -1;
    }

    if (parent) {
        /* check if parent is on the same (real) thread */
        if (parent->ts_dict != PyThreadState_GET()->dict) {
            PyErr_SetString(PyExc_ValueError, "parent cannot be on a different thread");
            return -1;
        }
    } else {
        parent = _global_state.current;
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
Threadlet_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    Threadlet *self = (Threadlet *)PyType_GenericNew(type, args, kwargs);
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
    return (PyObject *)self;
}


static stacklet_handle
stacklet__callback(stacklet_handle h, void *arg)
{
    Threadlet *origin, *self, *parent;
    PyObject *result;
    PyObject *exc, *val, *tb;
    PyThreadState *tstate;

    origin = _global_state.origin;
    self = _global_state.destination;
    origin->stacklet_h = h;
    _global_state.current = self;

    /* set current thread state before starting this new threadlet */
    tstate = PyThreadState_GET();
    tstate->frame = NULL;
    tstate->exc_type = NULL;
    tstate->exc_value = NULL;
    tstate->exc_traceback = NULL;
    self->ts.recursion_depth = tstate->recursion_depth;
    self->ts.frame = NULL;
    self->ts.exc_type = NULL;
    self->ts.exc_value = NULL;
    self->ts.exc_traceback = NULL;

    /* keep this threadlet alive while it runs */
    Py_INCREF(self);

    if (self->target) {
        result = PyObject_Call(self->target, self->args, self->kwargs);
    } else {
        result = Py_None;
        Py_INCREF(Py_None);
    }

    /* catch and ignore ThreadletExit exception */
    if (result == NULL && PyErr_ExceptionMatches(PyExc_ThreadletExit)) {
        PyErr_Fetch(&exc, &val, &tb);
        if (val == NULL) {
            val = Py_None;
            Py_INCREF(Py_None);
        }
        result = val;
        Py_DECREF(exc);
        Py_XDECREF(tb);
    }

    _global_state.value = result;
    _global_state.origin = origin;
    _global_state.destination = self;

    /* this threadlet has finished, select the parent as the next one to be run  */
    for (parent = self->parent; parent != NULL; parent = parent->parent) {
        _global_state.current = parent;
        Py_DECREF(self);
        return parent->stacklet_h;
    }

    return origin->stacklet_h;
}


static PyObject *
stacklet__post_switch(stacklet_handle h)
{
    Threadlet *origin = _global_state.origin;
    Threadlet *self = _global_state.destination;
    PyObject *result = _global_state.value;

    _global_state.origin = NULL;
    _global_state.destination = NULL;
    _global_state.value = NULL;

    if (h == EMPTY_STACKLET_HANDLE) {
        /* the current threadlet has ended, the reference to current is updated in
         * stacklet__callback, right after the Python function has returned */
        self->stacklet_h = h;
    } else {
        self->stacklet_h = origin->stacklet_h;
        origin->stacklet_h = h;
        _global_state.current = self;
    }

    return result;
}


static PyObject *
do_switch(Threadlet *self, PyObject *value)
{
    PyThreadState *tstate;
    stacklet_handle stacklet_h;
    Threadlet *current;
    PyObject *result;

    /* save state */
    current = _global_state.current;
    tstate = PyThreadState_GET();
    current->ts.recursion_depth = tstate->recursion_depth;
    current->ts.frame = tstate->frame;
    current->ts.exc_type = tstate->exc_type;
    current->ts.exc_value = tstate->exc_value;
    current->ts.exc_traceback = tstate->exc_traceback;

    _global_state.origin = current;
    _global_state.destination = self;
    _global_state.value = value;

    Py_INCREF(current);

    if (self->stacklet_h == NULL) {
        stacklet_h = stacklet_new(self->thread_h, stacklet__callback, NULL);
    } else {
        stacklet_h = stacklet_switch(self->stacklet_h);
    }

    result = stacklet__post_switch(stacklet_h);

    /* restore state */
    tstate = PyThreadState_GET();
    tstate->recursion_depth = current->ts.recursion_depth;
    tstate->frame = current->ts.frame;
    tstate->exc_type = current->ts.exc_type;
    tstate->exc_value = current->ts.exc_value;
    tstate->exc_traceback = current->ts.exc_traceback;
    current->ts.frame = NULL;
    current->ts.exc_type = NULL;
    current->ts.exc_value = NULL;
    current->ts.exc_traceback = NULL;

    Py_DECREF(current);

    return result;
}


static PyObject *
Threadlet_func_switch(Threadlet *self, PyObject *args)
{
    Threadlet *current;
    PyObject *value = Py_None;

    if (!PyArg_ParseTuple(args, "|O:switch", &value)) {
        return NULL;
    }

    if (!CHECK_STATE) {
        return NULL;
    }

    current = _global_state.current;
    if (self == current) {
        PyErr_SetString(PyExc_RuntimeError, "cannot switch from a threadlet to itself");
        return NULL;
    }

    if (self->stacklet_h == EMPTY_STACKLET_HANDLE) {
        PyErr_SetString(PyExc_ThreadletError, "threadlet has ended");
        return NULL;
    }

    if (self->thread_h != current->thread_h) {
        PyErr_SetString(PyExc_ThreadletError, "cannot switch to a threadlet on a different thread");
        return NULL;
    }

    if (self->stacklet_h == NULL && value != Py_None) {
        PyErr_SetString(PyExc_ValueError, "cannot specify a value when the threadlet wasn't started");
        return NULL;
    }
    Py_INCREF(value);
    /* TODO: need to decref? when? */

    return do_switch(self, value);
}


static PyObject *
Threadlet_func_throw(Threadlet *self, PyObject *args)
{
    Threadlet *current;
    PyObject *typ, *val, *tb;

    typ = PyExc_ThreadletExit;
    val = tb = NULL;

    if (!PyArg_ParseTuple(args, "|OOO:throw", &typ, &val, &tb)) {
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

    if (!CHECK_STATE) {
        goto error;
    }

    current = _global_state.current;
    if (self == current) {
        PyErr_SetString(PyExc_RuntimeError, "cannot throw from a threadlet to itself");
        goto error;
    }

    if (self->stacklet_h == EMPTY_STACKLET_HANDLE) {
        PyErr_SetString(PyExc_ThreadletError, "threadlet has ended");
        goto error;
    }

    if (self->thread_h != current->thread_h) {
        PyErr_SetString(PyExc_ThreadletError, "cannot switch to a threadlet on a different thread");
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
Threadlet_func_getstate(Threadlet *self)
{
    PyErr_Format(PyExc_TypeError, "cannot serialize '%s' object", Py_TYPE(self)->tp_name);
    return NULL;
}


static PyObject *
Threadlet_dict_get(Threadlet *self, void* c)
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
Threadlet_dict_set(Threadlet *self, PyObject* val, void* c)
{
    PyObject* tmp;
    UNUSED_ARG(c);

    if (val == NULL) {
        PyErr_SetString(PyExc_TypeError, "__dict__ may not be deleted");
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
Threadlet_parent_get(Threadlet *self, void* c)
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
Threadlet_parent_set(Threadlet *self, PyObject *val, void* c)
{
    Threadlet *p, *nparent;
    UNUSED_ARG(c);

    if (val == NULL) {
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!PyObject_TypeCheck(val, &ThreadletType)) {
        PyErr_SetString(PyExc_TypeError, "parent must be a threadlet");
        return -1;
    }

    nparent = (Threadlet *)val;
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


static PyObject *
Threadlet_frame_get(Threadlet *self, void* c)
{
	PyObject *result;
	UNUSED_ARG(c);

	if (self->ts.frame != NULL) {
	    result = (PyObject *)self->ts.frame;
	} else {
	    result = Py_None;
	}
	Py_INCREF(result);
	return result;
}


static int
Threadlet_tp_traverse(Threadlet *self, visitproc visit, void *arg)
{
    Py_VISIT(self->dict);
    Py_VISIT(self->ts_dict);
    Py_VISIT(self->parent);
    return 0;
}


static int
Threadlet_tp_clear(Threadlet *self)
{
    Py_CLEAR(self->dict);
    Py_CLEAR(self->ts_dict);
    Py_CLEAR(self->parent);
    return 0;
}


static void
Threadlet_tp_dealloc(Threadlet *self)
{
    if (self->stacklet_h && self->stacklet_h != EMPTY_STACKLET_HANDLE) {
        stacklet_destroy(self->stacklet_h);
    }
    /* TODO: dealloc thread handle. when? */
    if (self->weakreflist != NULL) {
        PyObject_ClearWeakRefs((PyObject *)self);
    }
    Py_TYPE(self)->tp_clear((PyObject *)self);
    Py_TYPE(self)->tp_free((PyObject *)self);
}


static PyMethodDef
Threadlet_tp_methods[] = {
    { "switch", (PyCFunction)Threadlet_func_switch, METH_VARARGS, "Switch execution to this threadlet" },
    { "throw", (PyCFunction)Threadlet_func_throw, METH_VARARGS, "Switch execution and raise the specified exception to this threadlet" },
    { "__getstate__", (PyCFunction)Threadlet_func_getstate, METH_NOARGS, "Serialize the threadlet object, not really" },
    { NULL }
};


static PyGetSetDef Threadlet_tp_getsets[] = {
    {"__dict__", (getter)Threadlet_dict_get, (setter)Threadlet_dict_set, "Instance dictionary", NULL},
    {"parent", (getter)Threadlet_parent_get, (setter)Threadlet_parent_set, "Threadlet parent or None if it's the main threadlet", NULL},
    {"frame", (getter)Threadlet_frame_get, NULL, "Current top frame or None", NULL},
    {NULL}
};


static PyTypeObject ThreadletType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "threadlet.Threadlet",                                          /*tp_name*/
    sizeof(Threadlet),                                              /*tp_basicsize*/
    0,                                                              /*tp_itemsize*/
    (destructor)Threadlet_tp_dealloc,                               /*tp_dealloc*/
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
    (traverseproc)Threadlet_tp_traverse,                            /*tp_traverse*/
    (inquiry)Threadlet_tp_clear,                                    /*tp_clear*/
    0,                                                              /*tp_richcompare*/
    offsetof(Threadlet, weakreflist),	                            /*tp_weaklistoffset*/
    0,                                                              /*tp_iter*/
    0,                                                              /*tp_iternext*/
    Threadlet_tp_methods,                                           /*tp_methods*/
    0,                                                              /*tp_members*/
    Threadlet_tp_getsets,                                           /*tp_getsets*/
    0,                                                              /*tp_base*/
    0,                                                              /*tp_dict*/
    0,                                                              /*tp_descr_get*/
    0,                                                              /*tp_descr_set*/
    offsetof(Threadlet, dict),                                      /*tp_dictoffset*/
    (initproc)Threadlet_tp_init,                                    /*tp_init*/
    0,                                                              /*tp_alloc*/
    Threadlet_tp_new,                                               /*tp_new*/
};


static PyMethodDef
threadlet_methods[] = {
    { "get_current", (PyCFunction)threadlet_func_get_current, METH_NOARGS, "Get the current threadlet" },
    { NULL }
};


#if PY_MAJOR_VERSION >= 3
static PyModuleDef threadlet_module = {
    PyModuleDef_HEAD_INIT,
    "threadlet",              /*m_name*/
    NULL,                     /*m_doc*/
    -1,                       /*m_size*/
    threadlet_methods  ,      /*m_methods*/
};
#endif


/* Module */
PyObject *
init_threadlet(void)
{
    PyObject *threadlet;

    /* Main module */
#if PY_MAJOR_VERSION >= 3
    threadlet = PyModule_Create(&threadlet_module);
#else
    threadlet = Py_InitModule("threadlet", threadlet_methods);
#endif

    /* keys for per-thread dictionary */
#if PY_MAJOR_VERSION >= 3
    ts_curkey = PyUnicode_InternFromString("__threadlet_ts_curkey");
#else
    ts_curkey = PyString_InternFromString("__threadlet_ts_curkey");
#endif
    if (ts_curkey == NULL) {
        goto fail;
    }

    /* Exceptions */
    PyExc_ThreadletError = PyErr_NewException("threadlet.error", NULL, NULL);
    MyPyModule_AddType(threadlet, "error", (PyTypeObject *)PyExc_ThreadletError);
    PyExc_ThreadletExit = PyErr_NewException("threadlet.ThreadletExit", PyExc_BaseException, NULL);
    MyPyModule_AddType(threadlet, "ThreadletExit", (PyTypeObject *)PyExc_ThreadletExit);

    /* Types */
    MyPyModule_AddType(threadlet, "Threadlet", &ThreadletType);

    /* Module version (the MODULE_VERSION macro is defined by setup.py) */
    PyModule_AddStringConstant(threadlet, "__version__", STRINGIFY(MODULE_VERSION));

    return threadlet;

fail:
#if PY_MAJOR_VERSION >= 3
    Py_DECREF(threadlet);
#endif
    return NULL;

}


#if PY_MAJOR_VERSION >= 3
PyMODINIT_FUNC
PyInit_threadlet(void)
{
    return init_threadlet();
}
#else
PyMODINIT_FUNC
initthreadlet(void)
{
    init_threadlet();
}
#endif

