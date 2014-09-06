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

#include <grub/mm.h>

#include "acpica.h"
#include "acpimodule.h"

static PyObject *acpi_object_to_python(ACPI_OBJECT *obj)
{
    if (obj == NULL)
        return Py_BuildValue("");

    switch(obj->Type) {
    case ACPI_TYPE_ANY:
        return Py_BuildValue("");
    case ACPI_TYPE_INTEGER:
        return Py_BuildValue("IK", ACPI_TYPE_INTEGER, obj->Integer.Value);
    case ACPI_TYPE_STRING:
        return Py_BuildValue("Is#", ACPI_TYPE_STRING, obj->String.Pointer, obj->String.Length);
    case ACPI_TYPE_BUFFER:
        return Py_BuildValue("Is#", ACPI_TYPE_BUFFER, obj->Buffer.Pointer, obj->Buffer.Length);
    case ACPI_TYPE_PACKAGE:
        {
            U32 ndx;
            PyObject *pkg = PyTuple_New(obj->Package.Count);
            for (ndx = 0; ndx < obj->Package.Count; ndx++) {
                PyObject *elem = acpi_object_to_python(&obj->Package.Elements[ndx]);
                if (!elem) {
                    Py_XDECREF(pkg);
                    return NULL;
                }
                PyTuple_SET_ITEM(pkg, ndx, elem);
            }
            return Py_BuildValue("IN", ACPI_TYPE_PACKAGE, pkg);
        }
    case ACPI_TYPE_POWER:
        return Py_BuildValue("I(II)", ACPI_TYPE_POWER, obj->PowerResource.SystemLevel, obj->PowerResource.ResourceOrder);
    case ACPI_TYPE_PROCESSOR:
        return Py_BuildValue("I(IkI)", ACPI_TYPE_PROCESSOR, obj->Processor.ProcId, obj->Processor.PblkAddress, obj->Processor.PblkLength);
    case ACPI_TYPE_LOCAL_REFERENCE:
        {
            ACPI_BUFFER Path = { .Length = ACPI_ALLOCATE_BUFFER, .Pointer = NULL };
            PyObject *ret;

            if (obj->Reference.Handle)
                if (ACPI_FAILURE(AcpiGetName(obj->Reference.Handle, ACPI_FULL_PATHNAME, &Path)))
                    return PyErr_Format(PyExc_RuntimeError,
                                        "Could not get name from ACPI local reference");

            ret = Py_BuildValue("I(IN)", ACPI_TYPE_LOCAL_REFERENCE, obj->Reference.ActualType, Path.Pointer ? Py_BuildValue("s", Path.Pointer) : Py_BuildValue(""));
            ACPI_FREE(Path.Pointer);
            return ret;
        }
    default:
        return PyErr_Format(PyExc_RuntimeError,
                            "Unable to parse the ACPI object returned from acpi_object_to_python on unhandled ACPI_OBJECT_TYPE %u.",
                            obj->Type);
    }
}

static void free_acpi_objects(ACPI_OBJECT *objs, U32 count)
{
    if (objs) {
        U32 i;
        for (i = 0; i < count; i++)
            if (objs[i].Type == ACPI_TYPE_PACKAGE)
                free_acpi_objects(objs[i].Package.Elements, objs[i].Package.Count);
        grub_free(objs);
    }
}

static bool acpi_objects_from_python(PyObject *pyobj, ACPI_OBJECT **objs, U32 *count);

static bool acpi_object_from_python(PyObject *pyobj, ACPI_OBJECT *obj)
{
    PyObject *value;

    if (pyobj == Py_None) {
        obj->Type = ACPI_TYPE_ANY;
        return true;
    }
    if (!PyArg_ParseTuple(pyobj, "IO:acpi_object_from_python", &obj->Type, &value))
        return false;
    switch (obj->Type) {
    case ACPI_TYPE_INTEGER:
        obj->Integer.Value = PyInt_AsUnsignedLongLongMask(value);
        return true;
    case ACPI_TYPE_STRING:
        {
            Py_ssize_t length;
            if (PyString_AsStringAndSize(value, &obj->String.Pointer, &length) < 0)
                return false;
            if (length > GRUB_UINT_MAX) {
                PyErr_Format(PyExc_RuntimeError, "Python object provided as ACPI string had > 4G of data");
                return false;
            }
            obj->String.Length = length;
            return true;
        }
    case ACPI_TYPE_BUFFER:
        {
            Py_ssize_t length;
            if (PyString_AsStringAndSize(value, (char **)&obj->Buffer.Pointer, &length) < 0)
                return false;
            if (length > GRUB_UINT_MAX) {
                PyErr_Format(PyExc_RuntimeError, "Python object provided as ACPI buffer had > 4G of data");
                return false;
            }
            obj->Buffer.Length = length;
            return true;
        }
    case ACPI_TYPE_PACKAGE:
        return acpi_objects_from_python(value, &obj->Package.Elements, &obj->Package.Count);
    case ACPI_TYPE_POWER:
        return PyArg_ParseTuple(value, "II", &obj->PowerResource.SystemLevel, &obj->PowerResource.ResourceOrder);
    case ACPI_TYPE_PROCESSOR:
        return PyArg_ParseTuple(value, "IkI", &obj->Processor.ProcId, &obj->Processor.PblkAddress, &obj->Processor.PblkLength);
    default:
        PyErr_Format(PyExc_RuntimeError,
                     "Python object provided as ACPI method parameter used unhandled ACPI_OBJECT_TYPE %u.",
                     obj->Type);
        return false;
    }
}

static bool acpi_objects_from_python(PyObject *pyobj, ACPI_OBJECT **objs, U32 *count)
{
    U32 i;

    if (!PyTuple_Check(pyobj))
        return false;

    *count = PyTuple_Size(pyobj);
    if (*count) {
        *objs = grub_zalloc(*count * sizeof(**objs));
        if (!*objs)
            return false;
    } else
        *objs = NULL;

    for (i = 0; i < *count; i++) {
        if (!acpi_object_from_python(PyTuple_GetItem(pyobj, i), &(*objs)[i])) {
            free_acpi_objects(*objs, *count);
            return false;
        }
    }

    return true;
}

static PyObject *bits_acpi_eval(PyObject *self, PyObject *args)
{
    char *pathname;
    PyObject *acpi_args_tuple;
    ACPI_OBJECT_LIST acpi_args;
    ACPI_BUFFER results = { .Length = ACPI_ALLOCATE_BUFFER, .Pointer = NULL };
    PyObject *ret;

    if (!PyArg_ParseTuple(args, "sO", &pathname, &acpi_args_tuple))
        return NULL;

    if (!acpi_objects_from_python(acpi_args_tuple, &acpi_args.Pointer, &acpi_args.Count))
        return NULL;

    if (acpica_init() != GRUB_ERR_NONE) {
        free_acpi_objects(acpi_args.Pointer, acpi_args.Count);
        return PyErr_Format(PyExc_RuntimeError, "ACPICA module failed to initialize.");
    }

    if (ACPI_FAILURE(AcpiEvaluateObject(NULL, pathname, &acpi_args, &results))) {
        free_acpi_objects(acpi_args.Pointer, acpi_args.Count);
        return Py_BuildValue("");
    }

    free_acpi_objects(acpi_args.Pointer, acpi_args.Count);
    ret = acpi_object_to_python(results.Pointer);

    ACPI_FREE(results.Pointer);
    return Py_BuildValue("N", ret);
}

static PyObject *bits_acpi_get_object_info(PyObject *self, PyObject *args)
{
    char *pathname;
    ACPI_HANDLE handle = 0;
    ACPI_DEVICE_INFO *info = NULL;
    PyObject *ret;
    ACPI_DEVICE_INFO *addr;

    if (!PyArg_ParseTuple(args, "s", &pathname))
        return NULL;

    if (acpica_init() != GRUB_ERR_NONE)
        return PyErr_Format(PyExc_RuntimeError, "ACPICA module failed to initialize.");

    if (ACPI_FAILURE(AcpiGetHandle(NULL, pathname, &handle)) || (handle == 0))
        return PyErr_Format(PyExc_RuntimeError, "Couldn't get object handle for \"%s\"", pathname);

    if (ACPI_FAILURE(AcpiGetObjectInfo(handle, &info)))
        return PyErr_Format(PyExc_RuntimeError, "Couldn't get object info for \"%s\"", pathname);

    ret = Py_BuildValue("(s#k)", info, info->InfoSize, info);
    ACPI_FREE(info);
    return ret;
}

static PyObject *bits_acpi_get_table(PyObject *self, PyObject *args)
{
    char *signature;
    U32 instance = 1;
    ACPI_TABLE_HEADER *table_header;

    if (!PyArg_ParseTuple(args, "s|I", &signature, &instance))
        return NULL;

    if (acpica_init() != GRUB_ERR_NONE)
        return PyErr_Format(PyExc_RuntimeError, "ACPICA module failed to initialize.");

    if (ACPI_FAILURE(AcpiGetTable(signature, instance, &table_header)))
        return Py_BuildValue("");

    return Py_BuildValue("s#", table_header, table_header->Length);
}

static PyObject *bits_acpi_get_root_pointer(PyObject *self, PyObject *args)
{
    ACPI_TABLE_RSDP *rsdp;

    if (acpica_init() != GRUB_ERR_NONE)
        return PyErr_Format(PyExc_RuntimeError, "ACPICA module failed to initialize.");

    rsdp = (ACPI_TABLE_RSDP *)AcpiOsGetRootPointer();

    if (rsdp)
        return Py_BuildValue("k", (unsigned long)rsdp);

    return Py_BuildValue("");
}

static PyObject *bits_acpi_get_rsdp(PyObject *self, PyObject *args)
{
    ACPI_TABLE_RSDP *rsdp;
    Py_ssize_t length = 0;

    if (acpica_init() != GRUB_ERR_NONE)
        return PyErr_Format(PyExc_RuntimeError, "ACPICA module failed to initialize.");

    rsdp = (ACPI_TABLE_RSDP *)AcpiOsGetRootPointer();

    if (rsdp) {
        if (rsdp->Revision == 0)
            length = sizeof(ACPI_RSDP_COMMON);
        else if (rsdp->Revision >= 2)
            length = rsdp->Length;
        if (length)
            return Py_BuildValue("s#", rsdp, length);
    }
    return Py_BuildValue("");
}

static PyObject *bits_acpi_get_rsdt(PyObject *self, PyObject *args)
{
    ACPI_TABLE_RSDP *rsdp;
    ACPI_TABLE_RSDT *rsdt;

    if (acpica_init() != GRUB_ERR_NONE)
        return PyErr_Format(PyExc_RuntimeError, "ACPICA module failed to initialize.");

    rsdp = (ACPI_TABLE_RSDP *)AcpiOsGetRootPointer();

    if (rsdp) {
        rsdt = (ACPI_TABLE_RSDT *)(unsigned long)rsdp->RsdtPhysicalAddress;
        if (rsdt)
            return Py_BuildValue("s#", rsdt, (Py_ssize_t)rsdt->Header.Length);
    }
    return Py_BuildValue("");
}

static PyObject *bits_acpi_get_xsdt(PyObject *self, PyObject *args)
{
    ACPI_TABLE_RSDP *rsdp;
    ACPI_TABLE_XSDT *xsdt;

    if (acpica_init() != GRUB_ERR_NONE)
        return PyErr_Format(PyExc_RuntimeError, "ACPICA module failed to initialize.");

    rsdp = (ACPI_TABLE_RSDP *)AcpiOsGetRootPointer();

    if (rsdp) {
        if (rsdp->Revision >= 2) {
#if defined(GRUB_TARGET_CPU_I386)
            if (rsdp->XsdtPhysicalAddress > GRUB_UINT_MAX)
                return PyErr_Format(PyExc_RuntimeError, "XSDT located above 4G; cannot access on 32-bit");
#endif
            xsdt = (ACPI_TABLE_XSDT *)(unsigned long)rsdp->XsdtPhysicalAddress;
            if (xsdt)
                return Py_BuildValue("s#", xsdt, (Py_ssize_t)xsdt->Header.Length);
        }
    }
    return Py_BuildValue("");
}

static PyObject *bits_acpi_get_table_by_index(PyObject *self, PyObject *args)
{
    U32 index;
    ACPI_TABLE_HEADER *table_header;

    if (!PyArg_ParseTuple(args, "I", &index))
        return NULL;

    if (acpica_init() != GRUB_ERR_NONE)
        return PyErr_Format(PyExc_RuntimeError, "ACPICA module failed to initialize.");

    if (ACPI_FAILURE(AcpiGetTableByIndex(index, &table_header)))
        return Py_BuildValue("");

    return Py_BuildValue("s#", table_header, table_header->Length);
}

struct find_processor_context {
    bool init_cpu;
    U32 caps;
    PyObject *cpupath_list;
    PyObject *devpath_list;
};

static grub_err_t call_osc(ACPI_HANDLE cpu_handle, U32 caps)
{
    grub_err_t ret;
    ACPI_STATUS Status;
    ACPI_BUFFER Results = { .Length = ACPI_ALLOCATE_BUFFER, .Pointer = NULL };
    ACPI_OBJECT_LIST Params;
    ACPI_OBJECT Obj[4];
    U32 osc_buffer[2];
    U8 OSC_UUID[16] = {0x16, 0xA6, 0x77, 0x40, 0x0C, 0x29, 0xBE, 0x47, 0x9E, 0xBD, 0xD8, 0x70, 0x58, 0x71, 0x39, 0x53};

    /* Initialize the parameter list */
    Params.Count = 4;
    Params.Pointer = Obj;

    /* Initialize the parameter objects */
    // Intel-specific UUID
    Obj[0].Type = ACPI_TYPE_BUFFER;
    Obj[0].Buffer.Length = 16;
    Obj[0].Buffer.Pointer = OSC_UUID;

    // Revision ID
    Obj[1].Type = ACPI_TYPE_INTEGER;
    Obj[1].Integer.Value = 1;

    // Count of DWORDS
    Obj[2].Type = ACPI_TYPE_INTEGER;
    Obj[2].Integer.Value = 2;

    // Capabilities buffer
    Obj[3].Type = ACPI_TYPE_BUFFER;
    Obj[3].Buffer.Length = 8;
    Obj[3].Buffer.Pointer = (U8 *)osc_buffer;

    osc_buffer[0] = 0;
    osc_buffer[1] = caps; // Capabilities DWORD

    Status = AcpiEvaluateObject(cpu_handle, "_OSC", &Params, &Results);
    if (Status == AE_OK)
        ret = GRUB_ERR_NONE;
    else if (Status == AE_NOT_FOUND)
        ret = grub_error(GRUB_ERR_TEST_FAILURE, "false");
    else
        ret = grub_error(GRUB_ERR_IO, "Evaluating _OSC failed (0x%x %s)\n", Status, AcpiFormatException(Status));

    ACPI_FREE(Results.Pointer);

    return ret;
}

static grub_err_t call_pdc(ACPI_HANDLE cpu_handle, U32 caps)
{
    ACPI_STATUS Status;
    ACPI_OBJECT_LIST Params;
    ACPI_OBJECT Obj;
    U32 pdc_buffer[3];

    /* Initialize the parameter list */
    Params.Count = 1;
    Params.Pointer = &Obj;

    /* Initialize the parameter objects */
    Obj.Type = ACPI_TYPE_BUFFER;
    Obj.Buffer.Length = 12;
    Obj.Buffer.Pointer = (UINT8 *)pdc_buffer;

    /* Initialize the DWORD array */
    pdc_buffer[0] = 1; // Revision
    pdc_buffer[1] = 1; // Count
    pdc_buffer[2] = caps; // Capabilities DWORD

    Status = AcpiEvaluateObject(cpu_handle, "_PDC", &Params, NULL);
    if (Status == AE_OK)
        return GRUB_ERR_NONE;
    else if (Status == AE_NOT_FOUND)
        return grub_error(GRUB_ERR_TEST_FAILURE, "false");
    else
        return grub_error(GRUB_ERR_IO, "Evaluating _PDC failed (0x%x %s)\n", Status, AcpiFormatException(Status));
}

static ACPI_STATUS find_processor(ACPI_HANDLE ObjHandle, UINT32 NestingLevel ACPI_UNUSED_VAR, void *Context, void **ReturnValue ACPI_UNUSED_VAR)
{
    struct find_processor_context *fpc = Context;
    ACPI_BUFFER Path = { .Length = ACPI_ALLOCATE_BUFFER, .Pointer = NULL };
    PyObject *cpupath;

    if (!IsEnabledProcessor(ObjHandle))
        goto out;

    if (ACPI_FAILURE(AcpiGetName(ObjHandle, ACPI_FULL_PATHNAME, &Path))) {
        grub_printf("Couldn't get object name\n");
        goto out;
    }

    if (fpc->init_cpu)
        if (call_osc(ObjHandle, fpc->caps) != GRUB_ERR_NONE)
            call_pdc(ObjHandle, fpc->caps);

    cpupath = Py_BuildValue("s", Path.Pointer);
    PyList_Append(fpc->cpupath_list, cpupath);
    Py_XDECREF(cpupath);

out:
    ACPI_FREE(Path.Pointer);
    return AE_OK;
}

static ACPI_STATUS find_processor_dev(ACPI_HANDLE ObjHandle, UINT32 NestingLevel ACPI_UNUSED_VAR, void *Context, void **ReturnValue ACPI_UNUSED_VAR)
{
    struct find_processor_context *fpc = Context;
    ACPI_BUFFER Path = { .Length = ACPI_ALLOCATE_BUFFER, .Pointer = NULL };
    PyObject *cpupath;

    if (!IsEnabledProcessorDev(ObjHandle))
        goto out;

    if (ACPI_FAILURE(AcpiGetName(ObjHandle, ACPI_FULL_PATHNAME, &Path))) {
        grub_printf("Couldn't get object name\n");
        goto out;
    }

    if (fpc->init_cpu)
        if (call_osc(ObjHandle, fpc->caps) != GRUB_ERR_NONE)
            call_pdc(ObjHandle, fpc->caps);

    cpupath = Py_BuildValue("s", Path.Pointer);
    PyList_Append(fpc->devpath_list, cpupath);
    Py_XDECREF(cpupath);

out:
    ACPI_FREE(Path.Pointer);
    return AE_OK;
}

static PyObject *bits_acpi_cpupaths(PyObject *self, PyObject *args)
{
    struct find_processor_context fpc = { .init_cpu = false, .caps = 0xfbf, .cpupath_list = NULL };

    if (acpica_init() != GRUB_ERR_NONE)
        return PyErr_Format(PyExc_RuntimeError, "ACPICA module failed to initialize.");

    // Before getting any input parameters, change the value of the capabilities DWORD
    // to match the value used in prior ACPI CPU initialization
    if (acpica_cpus_initialized)
        fpc.caps = acpica_cpus_init_caps;

    if (!PyArg_ParseTuple(args, "|I", &fpc.caps))
        return NULL;

    if (acpica_cpus_initialized) {
        if (fpc.caps != acpica_cpus_init_caps)
            return PyErr_Format(PyExc_ValueError, "Attempt to change current Capabilities DWORD from 0x%x to 0x%x; changing capabilities requires ACPI shutdown (acpi_terminate) and restart.",
                                acpica_cpus_init_caps, fpc.caps);
    } else {
        acpica_cpus_initialized = true;
        acpica_cpus_init_caps = fpc.caps;
        fpc.init_cpu = true;
    }

    fpc.cpupath_list = PyList_New(0);

    AcpiWalkNamespace(ACPI_TYPE_PROCESSOR, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX, find_processor, NULL, &fpc, NULL);

    fpc.devpath_list = PyList_New(0);

    AcpiWalkNamespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX, find_processor_dev, NULL, &fpc, NULL);

    return Py_BuildValue("(NN)", fpc.cpupath_list, fpc.devpath_list);
}

struct find_object_context {

    char *needle;
    PyObject *objpath_list;
};

static ACPI_STATUS find_object(ACPI_HANDLE ObjHandle, UINT32 NestingLevel ACPI_UNUSED_VAR, void *Context, void **ReturnValue ACPI_UNUSED_VAR)
{
    struct find_object_context *foc = Context;
    ACPI_BUFFER Path = { .Length = ACPI_ALLOCATE_BUFFER, .Pointer = NULL };
    PyObject *objpath;

    if (ACPI_FAILURE(AcpiGetName(ObjHandle, ACPI_FULL_PATHNAME, &Path))) {
        grub_printf("Couldn't get object name\n");
        goto out;
    }

    if (grub_strstr(Path.Pointer, foc->needle) == NULL)
        goto out;

    objpath = Py_BuildValue("s", Path.Pointer);
    PyList_Append(foc->objpath_list, objpath);
    Py_XDECREF(objpath);

out:
    ACPI_FREE(Path.Pointer);
    return AE_OK;
}

static PyObject *bits_acpi_install_interface(PyObject *self, PyObject *args)
{
    char *interface_name;

    if (!PyArg_ParseTuple(args, "s", &interface_name))
        return NULL;

    if (acpica_init() != GRUB_ERR_NONE)
        return PyErr_Format(PyExc_RuntimeError, "ACPICA module failed to initialize.");

    if (ACPI_FAILURE(AcpiInstallInterface(interface_name)))
        return PyErr_Format(PyExc_RuntimeError, "AcpiInstallInterface failed.");

    return Py_BuildValue("");
}

static PyObject *bits_acpi_objpaths(PyObject *self, PyObject *args)
{
    struct find_object_context foc = { .objpath_list = NULL };

    if (acpica_init() != GRUB_ERR_NONE)
        return PyErr_Format(PyExc_RuntimeError, "ACPICA module failed to initialize.");

    if (!PyArg_ParseTuple(args, "s", &foc.needle))
        return NULL;

    foc.objpath_list = PyList_New(0);

    AcpiWalkNamespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX, find_object, NULL, &foc, NULL);

    return foc.objpath_list;
}

static PyObject *bits_acpi_remove_interface(PyObject *self, PyObject *args)
{
    char *interface_name;

    if (!PyArg_ParseTuple(args, "s", &interface_name))
        return NULL;

    if (acpica_init() != GRUB_ERR_NONE)
        return PyErr_Format(PyExc_RuntimeError, "ACPICA module failed to initialize.");

    if (ACPI_FAILURE(AcpiRemoveInterface(interface_name)))
        return PyErr_Format(PyExc_RuntimeError, "AcpiRemoveInterface failed.");

    return Py_BuildValue("");
}

void acpica_terminate(void);

static PyObject *bits_acpi_terminate(PyObject *self, PyObject *args)
{
    acpica_terminate();

    return Py_BuildValue("");
}

static PyMethodDef acpiMethods[] = {
    {"_cpupaths",  bits_acpi_cpupaths, METH_VARARGS, "_cpupaths([capabilities]) -> tuple(list of cpu namepaths, list of device namepaths)"},
    {"_eval",  bits_acpi_eval, METH_VARARGS, "_eval(\"\\PATH._TO_.EVAL\") -> result"},
    {"_get_object_info",  bits_acpi_get_object_info, METH_VARARGS, "_get_object_info() -> (infostr, address)"},
    {"_get_root_pointer", bits_acpi_get_root_pointer, METH_NOARGS, "_get_root_pointer() -> address of the ACPI Root Pointer"},
    {"_get_rsdp",  bits_acpi_get_rsdp, METH_NOARGS, "_get_rsdp() -> str"},
    {"_get_rsdt",  bits_acpi_get_rsdt, METH_NOARGS, "_get_rsdt() -> str"},
    {"_get_xsdt",  bits_acpi_get_xsdt, METH_NOARGS, "_get_xsdt() -> str"},
    {"_get_table",  bits_acpi_get_table, METH_VARARGS, "_get_table(signature[, instance=1]) -> str"},
    {"_get_table_by_index",  bits_acpi_get_table_by_index, METH_VARARGS, "_get_table_by_index(index) -> str"},
    {"_install_interface", bits_acpi_install_interface, METH_VARARGS, "_install_interface(\"interface_name\")"},
    {"_objpaths",  bits_acpi_objpaths, METH_VARARGS, "_objpaths(\"objectname\") -> list of obj namepaths"},
    {"_remove_interface", bits_acpi_remove_interface, METH_VARARGS, "_remove_interface(\"interface_name\")"},
    {"_terminate", bits_acpi_terminate, METH_NOARGS, "_terminate() -> Perform ACPICA module terminate"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

PyMODINIT_FUNC init_acpi_module(void)
{
    (void) Py_InitModule("_acpi", acpiMethods);
}
