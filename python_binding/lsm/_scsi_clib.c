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

#define _wrapper(func_name, c_func_name, arg_type, arg, c_rt_type, \
                 c_rt_default, py_rt_conv_func) \
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
        _alloc_check(err_msg_obj, flag_no_mem, out); \
        goto out; \
    } else { \
        err_msg_obj = PyString_FromString(""); \
        _alloc_check(err_msg_obj, flag_no_mem, out); \
    } \
 out: \
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

static const char disk_paths_of_vpd83_docstring[] =
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
    "    [sd_paths, rc, err_msg]\n"
    "        sd_paths (list of string)\n"
    "            Empty list is not found. The string format: '/dev/sd[a-z]+'.\n"
    "        rc (integer)\n"
    "            Error code, lsm.ErrorNumber.OK if no error\n"
    "        err_msg (string)\n"
    "            Error message, empty if no error.\n";

static const char vpd83_of_disk_path_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Query the SCSI VPD83 NAA ID of given scsi disk path\n"
    "Parameters:\n"
    "    sd_path (string)\n"
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

static PyObject *disk_paths_of_vpd83(PyObject *self, PyObject *args,
                                     PyObject *kwargs);
static PyObject *vpd83_of_disk_path(PyObject *self, PyObject *args,
                                    PyObject *kwargs);
static PyObject *_lsm_string_list_to_pylist(lsm_string_list *str_list);
static PyObject *_c_str_to_py_str(const char *str);

static PyMethodDef _scsi_methods[] = {
    {"_disk_paths_of_vpd83",  (PyCFunction) disk_paths_of_vpd83,
     METH_VARARGS | METH_KEYWORDS, disk_paths_of_vpd83_docstring},
    {"_vpd83_of_disk_path",  (PyCFunction) vpd83_of_disk_path,
     METH_VARARGS | METH_KEYWORDS, vpd83_of_disk_path_docstring},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

/*
 * Provided lsm_string_list will be freed in this function.
 */
static PyObject *_lsm_string_list_to_pylist(lsm_string_list *str_list)
{
    PyObject *rc_list = NULL;
    PyObject *str_obj = NULL;
    uint32_t i = 0;

    rc_list = PyList_New(lsm_string_list_size(str_list));
    if (rc_list == NULL) {
        lsm_string_list_free(str_list);
        return PyErr_NoMemory();
    }


    for (; i < lsm_string_list_size(str_list); ++i) {
        str_obj = _c_str_to_py_str(lsm_string_list_elem_get(str_list, i));

        if (str_obj == NULL) {
            /* No memory */
            Py_XDECREF(rc_list);
            lsm_string_list_free(str_list);
            return PyErr_NoMemory();
        }
        PyList_SET_ITEM(rc_list, i, str_obj); \
    }
    lsm_string_list_free(str_list);
    return rc_list;
}

static PyObject *_c_str_to_py_str(const char *str)
{
    if (str == NULL)
        return PyString_FromString("");
    return PyString_FromString(str);
}

_wrapper(disk_paths_of_vpd83, lsm_scsi_disk_paths_of_vpd83,
         const char *, vpd83, lsm_string_list *, NULL,
         _lsm_string_list_to_pylist);
_wrapper(vpd83_of_disk_path, lsm_scsi_vpd83_of_disk_path,
         const char *, disk_path, const char *, NULL,
         _c_str_to_py_str);

PyMODINIT_FUNC init_scsi_clib(void)
{
        (void) Py_InitModule("_scsi_clib", _scsi_methods);
}
