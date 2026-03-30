/*****************************************************************************
*       Copyright (C) 2015 Jeroen Demeyer <jdemeyer@cage.ugent.be>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 2 of the License, or
* (at your option) any later version.
*                  http://www.gnu.org/licenses/
*****************************************************************************/

/* Tuple (None, None, None), initialized as needed */
static PyObject* NoneNoneNone;

/* All args flags of a PyMethod */
#define METH_ALLARGS (METH_VARARGS|METH_KEYWORDS|METH_NOARGS|METH_O)

/* Given an unbound method "desc" (this is not checked!) with only a
 * single "self" argument, call "desc(self)" without checking "self".
 * This can in particular be used to call any method as class or
 * static method. */
static CYTHON_INLINE PyObject* PyMethodDescr_CallSelf(PyMethodDescrObject* desc, PyObject* self)
{
    PyMethodDef* meth = desc->d_method;
    if (meth == NULL)
    {
        PyErr_SetString(PyExc_TypeError,
                "PyMethodDescr_CallSelf missing method implementation");
        return NULL;
    }

    /*
     * Call the underlying method implementation with *zero* arguments
     * beyond `self`.
     *
     * Cython can generate `METH_FASTCALL|METH_KEYWORDS` wrappers even for
     * methods that accept no extra args. In that case we must use the
     * fastcall calling convention (vectorcall), not the legacy
     * `PyCFunction(self, args)` convention.
     */
    if (meth->ml_flags & METH_FASTCALL)
    {
        if (meth->ml_flags & METH_KEYWORDS)
            return ((_PyCFunctionFastWithKeywords)meth->ml_meth)(self, NULL, 0, NULL);
        return ((_PyCFunctionFast)meth->ml_meth)(self, NULL, 0);
    }

    /* Fallback for non-fastcall methods (e.g. METH_NOARGS). */
    return meth->ml_meth(self, NULL);
}

/*
 * This function calls PyType_Ready(t) and then calls
 * t.__getmetaclass__(None) (if that method exists) which should
 * return the metaclass for t. Then type(t) is set to this metaclass
 * and metaclass.__init__(t, None, None, None) is called.
 */
static CYTHON_INLINE int Sage_PyType_Ready(PyTypeObject* t)
{
    int r = PyType_Ready(t);
    if (r < 0)
        return r;

    /* Set or get metaclass (the type of t) */
    PyTypeObject* metaclass;

    PyObject* getmetaclass;
    getmetaclass = PyObject_GetAttrString((PyObject*)t, "__getmetaclass__");
    if (getmetaclass)
    {
        /* Call getmetaclass with self=None.
         * We bypass Python's descriptor binding rules by calling the
         * underlying PyMethodDef directly (see PyMethodDescr_CallSelf()). */
        metaclass = (PyTypeObject*)(PyMethodDescr_CallSelf((PyMethodDescrObject*)getmetaclass, Py_None));
        Py_DECREF(getmetaclass);
        if (!metaclass)
            return -1;

        if (!PyType_Check(metaclass))
        {
            PyErr_SetString(PyExc_TypeError,
                    "__getmetaclass__ did not return a type");
            return -1;
        }

        /* Now, set t.__class__ to metaclass.
         * `Py_TYPE()` is not a valid lvalue on some Python versions/compilers;
         * `Py_SET_TYPE()` is the supported way to update the type. */
        Py_SET_TYPE(t, metaclass);
        PyType_Modified(t);
    }
    else
    {
        /* No __getmetaclass__ method: read metaclass... */
        PyErr_Clear();
        metaclass = Py_TYPE(t);
    }

    /* Now call metaclass.__init__(t, None, None, None) unless
     * we would be calling type.__init__ */
    initproc init = metaclass->tp_init;
    if (init == NULL || init == PyType_Type.tp_init)
        return 0;

    /* Safety check: since we didn't call tp_new of metaclass,
     * we cannot safely call tp_init if the size of the structure
     * differs. */
    if (metaclass->tp_basicsize != PyType_Type.tp_basicsize)
    {
        PyErr_SetString(PyExc_TypeError,
                "metaclass is not compatible with 'type' (you cannot use cdef attributes in Cython metaclasses)");
        return -1;
    }

    /* Initialize a tuple (None, None, None) */
    if (!NoneNoneNone)
    {
        NoneNoneNone = PyTuple_Pack(3, Py_None, Py_None, Py_None);
        if (!NoneNoneNone) return -1;
    }
    return init((PyObject*)t, NoneNoneNone, NULL);
}


/* Use the above function in Cython code instead of the default
 * PyType_Ready() function */
#define PyType_Ready(t)  Sage_PyType_Ready(t)
