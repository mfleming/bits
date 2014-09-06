/*
Copyright (c) 2013, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "Python.h"
#include "pyunconfig.h"

#include "efimodule.h"

#include <grub/efi/efi.h>

static PyObject *call0(PyObject *self, PyObject *args)
{
    unsigned long func;
    if (!PyArg_ParseTuple(args, "k:_call0", &func))
        return NULL;
    return Py_BuildValue("k", efi_call_0(((unsigned long (*)(void))func)));
}

static PyObject *call1(PyObject *self, PyObject *args)
{
    unsigned long func, arg1;
    if (!PyArg_ParseTuple(args, "kk:_call1", &func, &arg1))
        return NULL;
    return Py_BuildValue("k", efi_call_1(((unsigned long (*)(unsigned long))func), arg1));
}

static PyObject *call2(PyObject *self, PyObject *args)
{
    unsigned long func, arg1, arg2;
    if (!PyArg_ParseTuple(args, "kkk:_call2", &func, &arg1, &arg2))
        return NULL;
    return Py_BuildValue("k", efi_call_2(((unsigned long (*)(unsigned long, unsigned long))func), arg1, arg2));
}

static PyObject *call3(PyObject *self, PyObject *args)
{
    unsigned long func, arg1, arg2, arg3;
    if (!PyArg_ParseTuple(args, "kkkk:_call3", &func, &arg1, &arg2, &arg3))
        return NULL;
    return Py_BuildValue("k", efi_call_3(((unsigned long (*)(unsigned long, unsigned long, unsigned long))func), arg1, arg2, arg3));
}

static PyObject *call4(PyObject *self, PyObject *args)
{
    unsigned long func, arg1, arg2, arg3, arg4;
    if (!PyArg_ParseTuple(args, "kkkkk:_call4", &func, &arg1, &arg2, &arg3, &arg4))
        return NULL;
    return Py_BuildValue("k", efi_call_4(((unsigned long (*)(unsigned long, unsigned long, unsigned long, unsigned long))func), arg1, arg2, arg3, arg4));
}

static PyObject *call5(PyObject *self, PyObject *args)
{
    unsigned long func, arg1, arg2, arg3, arg4, arg5;
    if (!PyArg_ParseTuple(args, "kkkkkk:_call5", &func, &arg1, &arg2, &arg3, &arg4, &arg5))
        return NULL;
    return Py_BuildValue("k", efi_call_5(((unsigned long (*)(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long))func), arg1, arg2, arg3, arg4, arg5));
}

static PyObject *call6(PyObject *self, PyObject *args)
{
    unsigned long func, arg1, arg2, arg3, arg4, arg5, arg6;
    if (!PyArg_ParseTuple(args, "kkkkkkk:_call6", &func, &arg1, &arg2, &arg3, &arg4, &arg5, &arg6))
        return NULL;
    return Py_BuildValue("k", efi_call_6(((unsigned long (*)(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long))func), arg1, arg2, arg3, arg4, arg5, arg6));
}

static PyObject *call7(PyObject *self, PyObject *args)
{
    unsigned long func, arg1, arg2, arg3, arg4, arg5, arg6, arg7;
    if (!PyArg_ParseTuple(args, "kkkkkkkk:_call7", &func, &arg1, &arg2, &arg3, &arg4, &arg5, &arg6, &arg7))
        return NULL;
    return Py_BuildValue("k", efi_call_7(((unsigned long (*)(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long))func), arg1, arg2, arg3, arg4, arg5, arg6, arg7));
}

static PyObject *call10(PyObject *self, PyObject *args)
{
    unsigned long func, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10;
    if (!PyArg_ParseTuple(args, "kkkkkkkkkkk:_call10", &func, &arg1, &arg2, &arg3, &arg4, &arg5, &arg6, &arg7, &arg8, &arg9, &arg10))
        return NULL;
    return Py_BuildValue("k", efi_call_10(((unsigned long (*)(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long))func), arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10));
}

static PyMethodDef efiMethods[] = {
    {"_call0", call0, METH_VARARGS, "_call0(func) -> result"},
    {"_call1", call1, METH_VARARGS, "_call1(func, arg1) -> result"},
    {"_call2", call2, METH_VARARGS, "_call2(func, arg1, arg2) -> result"},
    {"_call3", call3, METH_VARARGS, "_call3(func, arg1, arg2, arg3) -> result"},
    {"_call4", call4, METH_VARARGS, "_call4(func, arg1, arg2, arg3, arg4) -> result"},
    {"_call5", call5, METH_VARARGS, "_call5(func, arg1, arg2, arg3, arg4, arg5) -> result"},
    {"_call6", call6, METH_VARARGS, "_call6(func, arg1, arg2, arg3, arg4, arg5, arg6) -> result"},
    {"_call7", call7, METH_VARARGS, "_call7(func, arg1, arg2, arg3, arg4, arg5, arg6, arg7) -> result"},
    {"_call10", call10, METH_VARARGS, "_call10(func, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10) -> result"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

PyMODINIT_FUNC init_efi(void)
{
    PyObject *m = Py_InitModule("_efi", efiMethods);
    PyModule_AddObject(m, "_system_table", Py_BuildValue("k", (unsigned long)grub_efi_system_table));
    PyModule_AddObject(m, "_image_handle", Py_BuildValue("k", (unsigned long)grub_efi_image_handle));
}
