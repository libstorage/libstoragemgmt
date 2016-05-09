/*
 * Copyright (C) 2015-2016 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Gris Ge <fge@redhat.com>
 */

#include <Python.h>
#include <stdint.h>
#include <stdbool.h>

#include <libstoragemgmt/libstoragemgmt.h>

#define _alloc_check(ptr, flag_no_mem, out) \
    do { \
        if (ptr == NULL) { \
            flag_no_mem = true; \
            goto out; \
        } \
    } while(0)

#define _NO_NEED_TO_FREE(x)

#define _wrapper(func_name, c_func_name, arg_type, arg, c_rt_type, \
                 c_rt_default, py_rt_conv_func, c_rt_free_func) \
static PyObject *func_name(PyObject *self, PyObject *args, PyObject *kwargs) \
{ \
    static const char *kwlist[] = {# arg, NULL}; \
    c_rt_type c_rt = c_rt_default; \
    arg_type arg = NULL; \
    lsm_error *lsm_err = NULL; \
    int rc = LSM_ERR_OK; \
    PyObject *rc_list = NULL; \
    PyObject *rc_obj = NULL; \
    PyObject *err_msg_obj = NULL; \
    PyObject *err_no_obj = NULL; \
    bool flag_no_mem = false; \
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s", (char **) kwlist, \
                                     &arg)) \
        return NULL; \
    rc = c_func_name(arg, &c_rt, &lsm_err); \
    err_no_obj = PyInt_FromLong(rc); \
    _alloc_check(err_no_obj, flag_no_mem, out); \
    rc_list = PyList_New(3 /* rc_obj, errno, err_str*/); \
    _alloc_check(rc_list, flag_no_mem, out); \
    rc_obj = py_rt_conv_func(c_rt); \
    _alloc_check(rc_obj, flag_no_mem, out); \
    if (rc != LSM_ERR_OK) { \
        err_msg_obj = PyString_FromString(lsm_error_message_get(lsm_err)); \
        lsm_error_free(lsm_err); \
        lsm_err = NULL; \
        _alloc_check(err_msg_obj, flag_no_mem, out); \
        goto out; \
    } else { \
        err_msg_obj = PyString_FromString(""); \
        _alloc_check(err_msg_obj, flag_no_mem, out); \
    } \
 out: \
    if (lsm_err != NULL) \
        lsm_error_free(lsm_err); \
    c_rt_free_func(c_rt); \
    if (flag_no_mem == true) { \
        Py_XDECREF(rc_list); \
        Py_XDECREF(err_no_obj); \
        Py_XDECREF(err_msg_obj); \
        Py_XDECREF(rc_obj); \
        return PyErr_NoMemory(); \
    } \
    PyList_SET_ITEM(rc_list, 0, rc_obj); \
    PyList_SET_ITEM(rc_list, 1, err_no_obj); \
    PyList_SET_ITEM(rc_list, 2, err_msg_obj); \
    return rc_list; \
}

#define _wrapper_no_output(func_name, c_func_name, arg_type, arg) \
static PyObject *func_name(PyObject *self, PyObject *args, PyObject *kwargs); \
static PyObject *func_name(PyObject *self, PyObject *args, PyObject *kwargs) \
{ \
    static const char *kwlist[] = {# arg, NULL}; \
    arg_type arg = NULL; \
    lsm_error *lsm_err = NULL; \
    int rc = LSM_ERR_OK; \
    PyObject *rc_list = NULL; \
    PyObject *rc_obj = Py_None; \
    PyObject *err_msg_obj = NULL; \
    PyObject *err_no_obj = NULL; \
    bool flag_no_mem = false; \
    _alloc_check(rc_obj, flag_no_mem, out); \
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s", (char **) kwlist, \
                                     &disk_path)) \
        return NULL; \
    rc = c_func_name(arg, &lsm_err); \
    err_no_obj = PyInt_FromLong(rc); \
    _alloc_check(err_no_obj, flag_no_mem, out); \
    rc_list = PyList_New(3 /* rc_obj, errno, err_str*/); \
    _alloc_check(rc_list, flag_no_mem, out); \
    if (rc != LSM_ERR_OK) { \
        err_msg_obj = PyString_FromString(lsm_error_message_get(lsm_err)); \
        lsm_error_free(lsm_err); \
        lsm_err = NULL; \
        _alloc_check(err_msg_obj, flag_no_mem, out); \
        goto out; \
    } else { \
        err_msg_obj = PyString_FromString(""); \
        _alloc_check(err_msg_obj, flag_no_mem, out); \
    } \
 out: \
    if (lsm_err != NULL) \
        lsm_error_free(lsm_err); \
    if (flag_no_mem == true) { \
        Py_XDECREF(rc_list); \
        Py_XDECREF(err_no_obj); \
        Py_XDECREF(err_msg_obj); \
        Py_XDECREF(rc_obj); \
        return PyErr_NoMemory(); \
    } \
    PyList_SET_ITEM(rc_list, 0, rc_obj); \
    PyList_SET_ITEM(rc_list, 1, err_no_obj); \
    PyList_SET_ITEM(rc_list, 2, err_msg_obj); \
    return rc_list; \
}

static const char local_disk_vpd83_search_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Find out the /dev/sdX paths for given SCSI VPD page 0x83 NAA type\n"
    "    ID. Considering multipath, certain VPD83 might have multiple disks\n"
    "    associated.\n"
    "Parameters:\n"
    "    vpd83 (string)\n"
    "        The VPD83 NAA type ID.\n"
    "Returns:\n"
    "    [disk_paths, rc, err_msg]\n"
    "        disk_paths (list of string)\n"
    "            Empty list is not found. The string format: '/dev/sd[a-z]+'.\n"
    "        rc (integer)\n"
    "            Error code, lsm.ErrorNumber.OK if no error\n"
    "        err_msg (string)\n"
    "            Error message, empty if no error.\n";

static const char local_disk_vpd83_get_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Query the SCSI VPD83 NAA ID of given disk path\n"
    "Parameters:\n"
    "    disk_path (string)\n"
    "        The SCSI disk path, example '/dev/sdb'. Empty string is failure\n"
    "Returns:\n"
    "    [vpd83, rc, err_msg]\n"
    "        vpd83 (string)\n"
    "            String of VPD83 NAA ID. Empty string if not supported.\n"
    "            The string format regex is:\n"
    "            (?:^6[0-9a-f]{31})|(?:^[235][0-9a-f]{15})$\n"
    "        rc (integer)\n"
    "            Error code, lsm.ErrorNumber.OK if no error\n"
    "        err_msg (string)\n"
    "            Error message, empty if no error.\n";

static const char local_disk_rpm_get_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Query the rotation speed of given disk path\n"
    "Parameters:\n"
    "    disk_path (string)\n"
    "        The disk path, example '/dev/sdb'. Empty string is failure\n"
    "Returns:\n"
    "    [rpm, rc, err_msg]\n"
    "        rpm (int)\n"
    "              revolutions per minute (RPM).\n"
    "        rc (integer)\n"
    "            Error code, lsm.ErrorNumber.OK if no error\n"
    "        err_msg (string)\n"
    "            Error message, empty if no error.\n";

static const char local_disk_list_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Query local disk paths. Currently only SCSI, ATA and NVMe disks will\n"
    "    be included\n"
    "Parameters:\n"
    "    N/A\n"
    "Returns:\n"
    "    [disk_paths, rc, err_msg]\n"
    "        disk_paths (list of string)\n"
    "            Empty list is not found. The string format: '/dev/sd[a-z]+'\n"
    "            or '/dev/nvme[0-9]+n[0-9]+'.\n"
    "        rc (integer)\n"
    "            Error code, lsm.ErrorNumber.OK if no error\n"
    "        err_msg (string)\n"
    "            Error message, empty if no error.\n";

static const char local_disk_link_type_get_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Query the link type of given disk path\n"
    "Parameters:\n"
    "    disk_path (string)\n"
    "        The disk path, example '/dev/sdb'. Empty string is failure\n"
    "Returns:\n"
    "    [link_type, rc, err_msg]\n"
    "        link_type (int)\n"
    "              Link type.\n"
    "        rc (integer)\n"
    "            Error code, lsm.ErrorNumber.OK if no error\n"
    "        err_msg (string)\n"
    "            Error message, empty if no error.\n";


static const char local_disk_ident_led_on_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Enable the identification LED for given disk.\n"
    "Parameters:\n"
    "    disk_path (string)\n"
    "        The disk path, example '/dev/sdb'. Empty string is failure\n"
    "Returns:\n"
    "    [None, rc, err_msg]\n"
    "        None \n"
    "        rc (integer)\n"
    "            Error code, lsm.ErrorNumber.OK if no error\n"
    "        err_msg (string)\n"
    "            Error message, empty if no error.\n";

static const char local_disk_ident_led_off_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Clear the identification LED for given disk.\n"
    "Parameters:\n"
    "    disk_path (string)\n"
    "        The disk path, example '/dev/sdb'. Empty string is failure\n"
    "Returns:\n"
    "    [None, rc, err_msg]\n"
    "        None \n"
    "        rc (integer)\n"
    "            Error code, lsm.ErrorNumber.OK if no error\n"
    "        err_msg (string)\n"
    "            Error message, empty if no error.\n";

static const char local_disk_fault_led_on_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Enable the fault LED for given disk.\n"
    "Parameters:\n"
    "    disk_path (string)\n"
    "        The disk path, example '/dev/sdb'. Empty string is failure\n"
    "Returns:\n"
    "    [None, rc, err_msg]\n"
    "        None \n"
    "        rc (integer)\n"
    "            Error code, lsm.ErrorNumber.OK if no error\n"
    "        err_msg (string)\n"
    "            Error message, empty if no error.\n";

static const char local_disk_fault_led_off_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Clear the fault LED for given disk.\n"
    "Parameters:\n"
    "    disk_path (string)\n"
    "        The disk path, example '/dev/sdb'. Empty string is failure\n"
    "Returns:\n"
    "    [None, rc, err_msg]\n"
    "        None \n"
    "        rc (integer)\n"
    "            Error code, lsm.ErrorNumber.OK if no error\n"
    "        err_msg (string)\n"
    "            Error message, empty if no error.\n";

static PyObject *local_disk_vpd83_search(PyObject *self, PyObject *args,
                                     PyObject *kwargs);
static PyObject *local_disk_vpd83_get(PyObject *self, PyObject *args,
                                    PyObject *kwargs);
static PyObject *local_disk_rpm_get(PyObject *self, PyObject *args,
                                    PyObject *kwargs);
static PyObject *local_disk_list(PyObject *self, PyObject *args,
                                 PyObject *kwargs);
static PyObject *local_disk_link_type_get(PyObject *self, PyObject *args,
                                          PyObject *kwargs);
static PyObject *_lsm_string_list_to_pylist(lsm_string_list *str_list);
static PyObject *_c_str_to_py_str(const char *str);

_wrapper_no_output(local_disk_ident_led_on, lsm_local_disk_ident_led_on,
                   const char *, disk_path);
_wrapper_no_output(local_disk_ident_led_off, lsm_local_disk_ident_led_off,
                   const char *, disk_path);
_wrapper_no_output(local_disk_fault_led_on, lsm_local_disk_fault_led_on,
                   const char *, disk_path);
_wrapper_no_output(local_disk_fault_led_off, lsm_local_disk_fault_led_off,
                   const char *, disk_path);

static PyMethodDef _methods[] = {
    {"_local_disk_vpd83_search",  (PyCFunction) local_disk_vpd83_search,
     METH_VARARGS | METH_KEYWORDS, local_disk_vpd83_search_docstring},
    {"_local_disk_vpd83_get",  (PyCFunction) local_disk_vpd83_get,
     METH_VARARGS | METH_KEYWORDS, local_disk_vpd83_get_docstring},
    {"_local_disk_rpm_get",  (PyCFunction) local_disk_rpm_get,
     METH_VARARGS | METH_KEYWORDS, local_disk_rpm_get_docstring},
    {"_local_disk_list",  (PyCFunction) local_disk_list,
     METH_NOARGS, local_disk_list_docstring},
    {"_local_disk_link_type_get",  (PyCFunction) local_disk_link_type_get,
     METH_VARARGS | METH_KEYWORDS, local_disk_link_type_get_docstring},
    {"_local_disk_ident_led_on",  (PyCFunction) local_disk_ident_led_on,
     METH_VARARGS | METH_KEYWORDS, local_disk_ident_led_on_docstring},
    {"_local_disk_ident_led_off",  (PyCFunction) local_disk_ident_led_off,
     METH_VARARGS | METH_KEYWORDS, local_disk_ident_led_off_docstring},
    {"_local_disk_fault_led_on",  (PyCFunction) local_disk_fault_led_on,
     METH_VARARGS | METH_KEYWORDS, local_disk_fault_led_on_docstring},
    {"_local_disk_fault_led_off",  (PyCFunction) local_disk_fault_led_off,
     METH_VARARGS | METH_KEYWORDS, local_disk_fault_led_off_docstring},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static PyObject *_lsm_string_list_to_pylist(lsm_string_list *str_list)
{
    PyObject *rc_list = NULL;
    PyObject *str_obj = NULL;
    uint32_t i = 0;

    rc_list = PyList_New(lsm_string_list_size(str_list));
    if (rc_list == NULL)
        return NULL;

    for (; i < lsm_string_list_size(str_list); ++i) {
        str_obj = _c_str_to_py_str(lsm_string_list_elem_get(str_list, i));

        if (str_obj == NULL) {
            Py_XDECREF(rc_list);
            return NULL;
        }
        PyList_SET_ITEM(rc_list, i, str_obj); \
    }
    return rc_list;
}

static PyObject *_c_str_to_py_str(const char *str)
{
    if (str == NULL)
        return PyString_FromString("");
    else
        return PyString_FromString(str);
}

_wrapper(local_disk_vpd83_search, lsm_local_disk_vpd83_search,
         const char *, vpd83, lsm_string_list *, NULL,
         _lsm_string_list_to_pylist, lsm_string_list_free);
_wrapper(local_disk_vpd83_get, lsm_local_disk_vpd83_get,
         const char *, disk_path, char *, NULL,
         _c_str_to_py_str, free);
_wrapper(local_disk_rpm_get, lsm_local_disk_rpm_get,
         const char *, disk_path, int32_t, LSM_DISK_RPM_UNKNOWN,
         PyInt_FromLong, _NO_NEED_TO_FREE);
_wrapper(local_disk_link_type_get, lsm_local_disk_link_type_get,
         const char *, disk_path, lsm_disk_link_type,
         LSM_DISK_LINK_TYPE_UNKNOWN, PyInt_FromLong, _NO_NEED_TO_FREE);

static PyObject *local_disk_list(PyObject *self, PyObject *args,
                                 PyObject *kwargs)
{
    lsm_error *lsm_err = NULL;
    int rc = LSM_ERR_OK;
    lsm_string_list *disk_paths = NULL;
    PyObject *rc_list = NULL;
    PyObject *rc_obj = NULL;
    PyObject *err_msg_obj = NULL;
    PyObject *err_no_obj = NULL;
    bool flag_no_mem = false;

    rc = lsm_local_disk_list(&disk_paths, &lsm_err);
    err_no_obj = PyInt_FromLong(rc);
    _alloc_check(err_no_obj, flag_no_mem, out);
    rc_list = PyList_New(3 /* rc_obj, errno, err_str*/);
    _alloc_check(rc_list, flag_no_mem, out);
    rc_obj = _lsm_string_list_to_pylist(disk_paths);
    _alloc_check(rc_obj, flag_no_mem, out);
    if (rc != LSM_ERR_OK) {
        err_msg_obj = PyString_FromString(lsm_error_message_get(lsm_err));
        lsm_error_free(lsm_err);
        lsm_err = NULL;
        _alloc_check(err_msg_obj, flag_no_mem, out);
        goto out;
    } else {
        err_msg_obj = PyString_FromString("");
        _alloc_check(err_msg_obj, flag_no_mem, out);
    }
 out:
    if (lsm_err != NULL)
        lsm_error_free(lsm_err);
    if (disk_paths != NULL)
        lsm_string_list_free(disk_paths);
    if (flag_no_mem == true) {
        Py_XDECREF(rc_list);
        Py_XDECREF(err_no_obj);
        Py_XDECREF(err_msg_obj);
        Py_XDECREF(rc_obj);
        return PyErr_NoMemory();
    }
    PyList_SET_ITEM(rc_list, 0, rc_obj);
    PyList_SET_ITEM(rc_list, 1, err_no_obj);
    PyList_SET_ITEM(rc_list, 2, err_msg_obj);
    return rc_list;
}

PyMODINIT_FUNC init_clib(void)
{
        (void) Py_InitModule("_clib", _methods);
}
