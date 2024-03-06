/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2015-2023 Red Hat, Inc.
 * (C) Copyright (C) 2017 Hewlett Packard Enterprise Development LP
 *
 * Author: Gris Ge <fge@redhat.com>
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdbool.h>
#include <stdint.h>

#include <libstoragemgmt/libstoragemgmt.h>

/*
 *  Following directions from here: http://python3porting.com/cextensions.html
 * https://docs.python.org/3/howto/cporting.html
 *
 * General documentation on extending python with C.
 * https://docs.python.org/3/extending/extending.html
 */

#if PY_MAJOR_VERSION > 2
#define PyInt_FromLong PyLong_FromLong
#endif

#define _alloc_check(ptr, flag_no_mem, out)                                    \
    do {                                                                       \
        if (ptr == NULL) {                                                     \
            flag_no_mem = true;                                                \
            goto out;                                                          \
        }                                                                      \
    } while (0)

#define _NO_NEED_TO_FREE(x)
#define _UNUSED(x) (void)(x)

#define _wrapper(func_name, c_func_name, arg_type, arg, c_rt_type,             \
                 c_rt_default, py_rt_conv_func, c_rt_free_func)                \
    static PyObject *func_name(PyObject *self, PyObject *args,                 \
                               PyObject *kwargs) {                             \
        static const char *kwlist[] = {#arg, NULL};                            \
        c_rt_type c_rt = c_rt_default;                                         \
        arg_type arg = NULL;                                                   \
        lsm_error *lsm_err = NULL;                                             \
        int rc = LSM_ERR_OK;                                                   \
        PyObject *rc_list = NULL;                                              \
        PyObject *rc_obj = NULL;                                               \
        PyObject *err_msg_obj = NULL;                                          \
        PyObject *err_no_obj = NULL;                                           \
        bool flag_no_mem = false;                                              \
        _UNUSED(self);                                                         \
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s", (char **)kwlist,   \
                                         &arg))                                \
            return NULL;                                                       \
        rc = c_func_name(arg, &c_rt, &lsm_err);                                \
        err_no_obj = PyInt_FromLong(rc);                                       \
        _alloc_check(err_no_obj, flag_no_mem, out);                            \
        rc_list = PyList_New(3 /* rc_obj, errno, err_str*/);                   \
        _alloc_check(rc_list, flag_no_mem, out);                               \
        rc_obj = py_rt_conv_func(c_rt);                                        \
        _alloc_check(rc_obj, flag_no_mem, out);                                \
        if (rc != LSM_ERR_OK) {                                                \
            err_msg_obj =                                                      \
                PyUnicode_FromString(lsm_error_message_get(lsm_err));          \
            lsm_error_free(lsm_err);                                           \
            lsm_err = NULL;                                                    \
            _alloc_check(err_msg_obj, flag_no_mem, out);                       \
            goto out;                                                          \
        } else {                                                               \
            err_msg_obj = PyUnicode_FromString("");                            \
            _alloc_check(err_msg_obj, flag_no_mem, out);                       \
        }                                                                      \
    out:                                                                       \
        if (lsm_err != NULL)                                                   \
            lsm_error_free(lsm_err);                                           \
        c_rt_free_func(c_rt);                                                  \
        if (flag_no_mem == true) {                                             \
            Py_XDECREF(rc_list);                                               \
            Py_XDECREF(err_no_obj);                                            \
            Py_XDECREF(err_msg_obj);                                           \
            Py_XDECREF(rc_obj);                                                \
            return PyErr_NoMemory();                                           \
        }                                                                      \
        PyList_SET_ITEM(rc_list, 0, rc_obj);                                   \
        PyList_SET_ITEM(rc_list, 1, err_no_obj);                               \
        PyList_SET_ITEM(rc_list, 2, err_msg_obj);                              \
        return rc_list;                                                        \
    }

#define _wrapper_no_output(func_name, c_func_name, arg_type, arg)              \
    static PyObject *func_name(PyObject *self, PyObject *args,                 \
                               PyObject *kwargs);                              \
    static PyObject *func_name(PyObject *self, PyObject *args,                 \
                               PyObject *kwargs) {                             \
        static const char *kwlist[] = {#arg, NULL};                            \
        arg_type arg = NULL;                                                   \
        lsm_error *lsm_err = NULL;                                             \
        int rc = LSM_ERR_OK;                                                   \
        PyObject *rc_list = NULL;                                              \
        PyObject *rc_obj = Py_None;                                            \
        PyObject *err_msg_obj = NULL;                                          \
        PyObject *err_no_obj = NULL;                                           \
        bool flag_no_mem = false;                                              \
        _UNUSED(self);                                                         \
        _alloc_check(rc_obj, flag_no_mem, out);                                \
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s", (char **)kwlist,   \
                                         &disk_path))                          \
            return NULL;                                                       \
        rc = c_func_name(arg, &lsm_err);                                       \
        err_no_obj = PyInt_FromLong(rc);                                       \
        _alloc_check(err_no_obj, flag_no_mem, out);                            \
        rc_list = PyList_New(3 /* rc_obj, errno, err_str*/);                   \
        _alloc_check(rc_list, flag_no_mem, out);                               \
        if (rc != LSM_ERR_OK) {                                                \
            err_msg_obj =                                                      \
                PyUnicode_FromString(lsm_error_message_get(lsm_err));          \
            lsm_error_free(lsm_err);                                           \
            lsm_err = NULL;                                                    \
            _alloc_check(err_msg_obj, flag_no_mem, out);                       \
            goto out;                                                          \
        } else {                                                               \
            err_msg_obj = PyUnicode_FromString("");                            \
            _alloc_check(err_msg_obj, flag_no_mem, out);                       \
        }                                                                      \
    out:                                                                       \
        if (lsm_err != NULL)                                                   \
            lsm_error_free(lsm_err);                                           \
        if (flag_no_mem == true) {                                             \
            Py_XDECREF(rc_list);                                               \
            Py_XDECREF(err_no_obj);                                            \
            Py_XDECREF(err_msg_obj);                                           \
            Py_XDECREF(rc_obj);                                                \
            return PyErr_NoMemory();                                           \
        }                                                                      \
        PyList_SET_ITEM(rc_list, 0, rc_obj);                                   \
        PyList_SET_ITEM(rc_list, 1, err_no_obj);                               \
        PyList_SET_ITEM(rc_list, 2, err_msg_obj);                              \
        return rc_list;                                                        \
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

static const char local_disk_serial_num_get_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Query the SCSI VPD80 serial number of given disk path\n"
    "Parameters:\n"
    "    disk_path (string)\n"
    "        The SCSI disk path, example '/dev/sdb'. Empty string is failure\n"
    "Returns:\n"
    "    [serial_num, rc, err_msg]\n"
    "        serial_num (string)\n"
    "            String of VPD80 serial number.\n"
    "            Empty string if not supported.\n"
    "        rc (integer)\n"
    "            Error code, lsm.ErrorNumber.OK if no error\n"
    "        err_msg (string)\n"
    "            Error message, empty if no error.\n";

static const char local_disk_health_status_get_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Query the health status of a given disk path\n"
    "Parameters:\n"
    "    disk_path (string)\n"
    "        The SCSI disk path, example '/dev/sdb'. Empty string is failure\n"
    "Returns:\n"
    "    [health_status, rc, err_msg]\n"
    "        health_status (int)\n"
    "            health status.\n"
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

static const char local_disk_led_status_get_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Get LED status for given disk.\n"
    "Parameters:\n"
    "    disk_path (string)\n"
    "        The disk path, example '/dev/sdb'. Empty string is failure\n"
    "Returns:\n"
    "    [led_status, rc, err_msg]\n"
    "        led_status (integer)\n"
    "            Disk LED status which is a bit map.\n"
    "        rc (integer)\n"
    "            Error code, lsm.ErrorNumber.OK if no error\n"
    "        err_msg (string)\n"
    "            Error message, empty if no error.\n";

static const char local_disk_link_speed_get_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Get the link speed for given disk.\n"
    "Parameters:\n"
    "    disk_path (string)\n"
    "        The disk path, example '/dev/sdb'. Empty string is failure\n"
    "Returns:\n"
    "    [link_speeds, rc, err_msg]\n"
    "        link_speeds (list of string)\n"
    "            Empty list is not support. The string is like: '3.0 Gbps'\n"
    "            or special strings(check libstoragemgmt_types.h for detail):\n"
    "             * LSM_DISK_LINK_SPEED_UNKNOWN -- 'UNKNOWN'\n"
    "             * LSM_DISK_LINK_SPEED_DISABLED -- 'DISABLED'\n"
    "             * LSM_DISK_LINK_SPEED_DISCONNECTED-- 'DISCONNECTED'\n"
    "        rc (integer)\n"
    "            Error code, lsm.ErrorNumber.OK if no error\n"
    "        err_msg (string)\n"
    "            Error message, empty if no error.\n";

static const char led_slot_handle_get_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Get a handle for LED slots support\n"
    "Parameters:\n"
    "     N/A\n"
    "Returns:\n"
    "    [handle, rc, err_msg]\n"
    "        handle (integer)\n"
    "             Pointer address of the handle\n"
    "        rc (integer)\n"
    "            Error code, lsm.ErrorNumber.OK if no error\n"
    "        err_msg (string)\n"
    "            Error message, empty if no error.\n";

static const char led_slot_handle_free_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Frees resources for LED slot handle\n"
    "Parameters:\n"
    "     Handle(unsigned long long)\n"
    "Returns:\n"
    "     N/A";

static const char led_slot_iterator_get_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Get an iterator for LED slots support using a handle\n"
    "Parameters:\n"
    "     handle (unsigned long long)\n"
    "Returns:\n"
    "    [handle, rc, err_msg]\n"
    "        handle (integer)\n"
    "             Pointer address of the iterator\n"
    "        rc (integer)\n"
    "            Error code, lsm.ErrorNumber.OK if no error\n"
    "        err_msg (string)\n"
    "            Error message, empty if no error.\n";

static const char led_slot_iterator_free_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Frees resources for LED slot iterator\n"
    "Parameters:\n"
    "     iterator(unsigned long long)\n"
    "Returns:\n"
    "     N/A";

static const char led_slot_iterator_reset_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Resets the slot iterator, so that it can be used again\n"
    "Parameters:\n"
    "     iterator(unsigned long long)\n"
    "Returns:\n"
    "     N/A";

static const char led_slot_iterator_next_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Increments the slot iterator using a handle and slot iterator\n"
    "Parameters:\n"
    "     handle (unsigned long long)\n"
    "     slot_iterator (unsigned long long)\n"
    "Returns:\n"
    "    Updated slot iterator or 0 when iterator is complete\n";

static const char led_slot_status_get_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Returns the state of the LED for the given slot\n"
    "Parameters:\n"
    "     slot (unsigned long long)\n"
    "Returns:\n"
    "    slot status, see led_status_get for more details\n";

static const char led_slot_status_set_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Set the state for the specified slot\n"
    "Parameters:\n"
    "     handle (unsigned long long)\n"
    "     slot (unsigned long long)\n"
    "     led_stat (unsigned long)\n"
    "Returns:\n"
    "    [None, rc, err_msg]\n"
    "        None\n"
    "        rc (integer)\n"
    "            Error code, lsm.ErrorNumber.OK if no error\n"
    "        err_msg (string)\n"
    "            Error message, empty if no error.\n";

static const char led_slot_id_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Returns the slot identifier for selected slot\n"
    "Parameters:\n"
    "     slot (unsigned long long)\n"
    "Returns:\n"
    "    slot id (string)\n";

static const char led_slot_device_docstring[] =
    "INTERNAL USE ONLY!\n"
    "\n"
    "Usage:\n"
    "    Returns the slot device node for selected slot\n"
    "    Note: Not all slots have a device"
    "Parameters:\n"
    "     slot (unsigned long long)\n"
    "Returns:\n"
    "    device node (string) if present or None\n";

static PyObject *local_disk_serial_num_get(PyObject *self, PyObject *args,
                                           PyObject *kwargs);

static PyObject *local_disk_vpd83_search(PyObject *self, PyObject *args,
                                         PyObject *kwargs);
static PyObject *local_disk_vpd83_get(PyObject *self, PyObject *args,
                                      PyObject *kwargs);
static PyObject *local_disk_health_status_get(PyObject *self, PyObject *args,
                                              PyObject *kwargs);
static PyObject *local_disk_rpm_get(PyObject *self, PyObject *args,
                                    PyObject *kwargs);
static PyObject *local_disk_list(PyObject *self, PyObject *args,
                                 PyObject *kwargs);
static PyObject *local_disk_link_type_get(PyObject *self, PyObject *args,
                                          PyObject *kwargs);
static PyObject *local_disk_link_speed_get(PyObject *self, PyObject *args,
                                           PyObject *kwargs);
static PyObject *_lsm_string_list_to_pylist(lsm_string_list *str_list);
static PyObject *_c_str_to_py_str(const char *str);
static PyObject *local_disk_led_status_get(PyObject *self, PyObject *args,
                                           PyObject *kwargs);

static PyObject *led_slot_handle_get(PyObject *self, PyObject *args,
                                     PyObject *kwargs);

static PyObject *led_slot_handle_free(PyObject *self, PyObject *args,
                                      PyObject *kwargs);

static PyObject *led_slot_iterator_get(PyObject *self, PyObject *args,
                                       PyObject *kwargs);

static PyObject *led_slot_iterator_free(PyObject *self, PyObject *args,
                                        PyObject *kwargs);

static PyObject *led_slot_iterator_reset(PyObject *self, PyObject *args,
                                         PyObject *kwargs);

static PyObject *led_slot_iterator_next(PyObject *self, PyObject *args,
                                        PyObject *kwargs);

static PyObject *led_slot_status_get(PyObject *self, PyObject *args,
                                     PyObject *kwargs);

static PyObject *led_slot_status_set(PyObject *self, PyObject *args,
                                     PyObject *kwargs);

static PyObject *led_slot_id(PyObject *self, PyObject *args, PyObject *kwargs);

static PyObject *led_slot_device(PyObject *self, PyObject *args,
                                 PyObject *kwargs);

_wrapper_no_output(local_disk_ident_led_on, lsm_local_disk_ident_led_on,
                   const char *, disk_path);
_wrapper_no_output(local_disk_ident_led_off, lsm_local_disk_ident_led_off,
                   const char *, disk_path);
_wrapper_no_output(local_disk_fault_led_on, lsm_local_disk_fault_led_on,
                   const char *, disk_path);
_wrapper_no_output(local_disk_fault_led_off, lsm_local_disk_fault_led_off,
                   const char *, disk_path);

static PyMethodDef _methods[] = {
    {"_local_disk_serial_num_get", (PyCFunction)local_disk_serial_num_get,
     METH_VARARGS | METH_KEYWORDS, local_disk_serial_num_get_docstring},
    {"_local_disk_vpd83_search", (PyCFunction)local_disk_vpd83_search,
     METH_VARARGS | METH_KEYWORDS, local_disk_vpd83_search_docstring},
    {"_local_disk_vpd83_get", (PyCFunction)local_disk_vpd83_get,
     METH_VARARGS | METH_KEYWORDS, local_disk_vpd83_get_docstring},
    {"_local_disk_health_status_get", (PyCFunction)local_disk_health_status_get,
     METH_VARARGS | METH_KEYWORDS, local_disk_health_status_get_docstring},
    {"_local_disk_rpm_get", (PyCFunction)local_disk_rpm_get,
     METH_VARARGS | METH_KEYWORDS, local_disk_rpm_get_docstring},
    {"_local_disk_list", (PyCFunction)local_disk_list, METH_NOARGS,
     local_disk_list_docstring},
    {"_local_led_slot_handle_get", (PyCFunction)led_slot_handle_get,
     METH_NOARGS, led_slot_handle_get_docstring},
    {"_local_led_slot_handle_free", (PyCFunction)led_slot_handle_free,
     METH_VARARGS | METH_KEYWORDS, led_slot_handle_free_docstring},
    {"_local_led_slot_iterator_get", (PyCFunction)led_slot_iterator_get,
     METH_VARARGS | METH_KEYWORDS, led_slot_iterator_get_docstring},
    {"_local_led_slot_iterator_free", (PyCFunction)led_slot_iterator_free,
     METH_VARARGS | METH_KEYWORDS, led_slot_iterator_free_docstring},
    {"_local_led_slot_iterator_reset", (PyCFunction)led_slot_iterator_reset,
     METH_VARARGS | METH_KEYWORDS, led_slot_iterator_reset_docstring},
    {"_local_led_slot_iterator_next", (PyCFunction)led_slot_iterator_next,
     METH_VARARGS | METH_KEYWORDS, led_slot_iterator_next_docstring},
    {"_local_led_slot_status_get", (PyCFunction)led_slot_status_get,
     METH_VARARGS | METH_KEYWORDS, led_slot_status_get_docstring},
    {"_local_led_slot_status_set", (PyCFunction)led_slot_status_set,
     METH_VARARGS | METH_KEYWORDS, led_slot_status_set_docstring},
    {"_local_led_slot_status_get", (PyCFunction)led_slot_status_get,
     METH_VARARGS | METH_KEYWORDS, led_slot_iterator_next_docstring},
    {"_local_led_slot_id", (PyCFunction)led_slot_id,
     METH_VARARGS | METH_KEYWORDS, led_slot_id_docstring},
    {"_local_led_slot_device", (PyCFunction)led_slot_device,
     METH_VARARGS | METH_KEYWORDS, led_slot_device_docstring},
    {"_local_disk_link_type_get", (PyCFunction)local_disk_link_type_get,
     METH_VARARGS | METH_KEYWORDS, local_disk_link_type_get_docstring},
    {"_local_disk_ident_led_on", (PyCFunction)local_disk_ident_led_on,
     METH_VARARGS | METH_KEYWORDS, local_disk_ident_led_on_docstring},
    {"_local_disk_ident_led_off", (PyCFunction)local_disk_ident_led_off,
     METH_VARARGS | METH_KEYWORDS, local_disk_ident_led_off_docstring},
    {"_local_disk_fault_led_on", (PyCFunction)local_disk_fault_led_on,
     METH_VARARGS | METH_KEYWORDS, local_disk_fault_led_on_docstring},
    {"_local_disk_fault_led_off", (PyCFunction)local_disk_fault_led_off,
     METH_VARARGS | METH_KEYWORDS, local_disk_fault_led_off_docstring},
    {"_local_disk_led_status_get", (PyCFunction)local_disk_led_status_get,
     METH_VARARGS | METH_KEYWORDS, local_disk_led_status_get_docstring},
    {"_local_disk_link_speed_get", (PyCFunction)local_disk_link_speed_get,
     METH_VARARGS | METH_KEYWORDS, local_disk_link_speed_get_docstring},
    {NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject *_lsm_string_list_to_pylist(lsm_string_list *str_list) {
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
        PyList_SET_ITEM(rc_list, i, str_obj);
    }
    return rc_list;
}

static PyObject *_c_str_to_py_str(const char *str) {
    if (str == NULL)
        return PyUnicode_FromString("");
    return PyUnicode_FromString(str);
}

_wrapper(local_disk_serial_num_get, lsm_local_disk_serial_num_get, const char *,
         disk_path, char *, NULL, _c_str_to_py_str, free);
_wrapper(local_disk_vpd83_search, lsm_local_disk_vpd83_search, const char *,
         vpd83, lsm_string_list *, NULL, _lsm_string_list_to_pylist,
         lsm_string_list_free);
_wrapper(local_disk_vpd83_get, lsm_local_disk_vpd83_get, const char *,
         disk_path, char *, NULL, _c_str_to_py_str, free);
_wrapper(local_disk_health_status_get, lsm_local_disk_health_status_get,
         const char *, disk_path, int32_t, LSM_DISK_HEALTH_STATUS_UNKNOWN,
         PyInt_FromLong, _NO_NEED_TO_FREE);
_wrapper(local_disk_rpm_get, lsm_local_disk_rpm_get, const char *, disk_path,
         int32_t, LSM_DISK_RPM_UNKNOWN, PyInt_FromLong, _NO_NEED_TO_FREE);
_wrapper(local_disk_link_type_get, lsm_local_disk_link_type_get, const char *,
         disk_path, lsm_disk_link_type, LSM_DISK_LINK_TYPE_UNKNOWN,
         PyInt_FromLong, _NO_NEED_TO_FREE);
_wrapper(local_disk_led_status_get, lsm_local_disk_led_status_get, const char *,
         disk_path, uint32_t, LSM_DISK_LED_STATUS_UNKNOWN, PyInt_FromLong,
         _NO_NEED_TO_FREE);
_wrapper(local_disk_link_speed_get, lsm_local_disk_link_speed_get, const char *,
         disk_path, uint32_t, LSM_DISK_LINK_SPEED_UNKNOWN, PyInt_FromLong,
         _NO_NEED_TO_FREE);

static PyObject *local_disk_list(PyObject *self, PyObject *args,
                                 PyObject *kwargs) {
    lsm_error *lsm_err = NULL;
    int rc = LSM_ERR_OK;
    lsm_string_list *disk_paths = NULL;
    PyObject *rc_list = NULL;
    PyObject *rc_obj = NULL;
    PyObject *err_msg_obj = NULL;
    PyObject *err_no_obj = NULL;
    bool flag_no_mem = false;

    _UNUSED(self);
    _UNUSED(args);
    _UNUSED(kwargs);
    rc = lsm_local_disk_list(&disk_paths, &lsm_err);
    err_no_obj = PyInt_FromLong(rc);
    _alloc_check(err_no_obj, flag_no_mem, out);
    rc_list = PyList_New(3 /* rc_obj, errno, err_str*/);
    _alloc_check(rc_list, flag_no_mem, out);
    rc_obj = _lsm_string_list_to_pylist(disk_paths);
    _alloc_check(rc_obj, flag_no_mem, out);
    if (rc != LSM_ERR_OK) {
        err_msg_obj = PyUnicode_FromString(lsm_error_message_get(lsm_err));
        lsm_error_free(lsm_err);
        lsm_err = NULL;
        _alloc_check(err_msg_obj, flag_no_mem, out);
        goto out;
    } else {
        err_msg_obj = PyUnicode_FromString("");
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

static PyObject *led_slot_handle_get(PyObject *self, PyObject *args,
                                     PyObject *kwargs) {
    int rc = LSM_ERR_OK;
    lsm_flag flags = LSM_ERR_OK;
    lsm_led_handle *handle = NULL;
    PyObject *rc_list = NULL;
    PyObject *rc_obj = NULL;
    PyObject *err_msg_obj = NULL;
    PyObject *err_no_obj = NULL;
    bool flag_no_mem = false;

    _UNUSED(self);
    _UNUSED(args);
    _UNUSED(kwargs);

    rc = lsm_led_handle_get(&handle, flags);
    err_no_obj = PyInt_FromLong(rc);
    _alloc_check(err_no_obj, flag_no_mem, out);
    rc_list = PyList_New(3 /* rc_obj, errno, err_str*/);
    _alloc_check(rc_list, flag_no_mem, out);

    err_msg_obj = PyUnicode_FromString("");
    _alloc_check(err_msg_obj, flag_no_mem, out);

    if (rc == LSM_ERR_OK) {
        rc_obj = PyLong_FromLongLong((unsigned long long)handle);
    } else {
        rc_obj = PyLong_FromLongLong(0ull);
    }
    _alloc_check(rc_obj, flag_no_mem, out);

out:
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

static PyObject *led_slot_handle_free(PyObject *self, PyObject *args,
                                      PyObject *kwargs) {
    unsigned long long handle_address = 0;
    lsm_led_handle *handle = NULL;

    _UNUSED(self);
    _UNUSED(kwargs);

    if (!PyArg_ParseTuple(args, "K", &handle_address)) {
        return NULL;
    }
    handle = (lsm_led_handle *)handle_address;
    lsm_led_handle_free(handle);
    Py_RETURN_NONE;
}

static PyObject *led_slot_iterator_get(PyObject *self, PyObject *args,
                                       PyObject *kwargs) {
    lsm_error *lsm_err = NULL;
    int rc = LSM_ERR_OK;
    lsm_flag flags = LSM_ERR_OK;
    lsm_led_handle *handle = NULL;
    lsm_led_slot_itr *slot_itr = NULL;
    unsigned long long handle_address = 0;
    PyObject *rc_list = NULL;
    PyObject *rc_obj = NULL;
    PyObject *err_msg_obj = NULL;
    PyObject *err_no_obj = NULL;
    bool flag_no_mem = false;

    _UNUSED(self);
    _UNUSED(kwargs);

    if (!PyArg_ParseTuple(args, "K", &handle_address)) {
        return NULL;
    }

    handle = (lsm_led_handle *)handle_address;
    rc = lsm_led_slot_iterator_get(handle, &slot_itr, &lsm_err, flags);
    err_no_obj = PyInt_FromLong(rc);
    _alloc_check(err_no_obj, flag_no_mem, out);
    rc_list = PyList_New(3 /* rc_obj, errno, err_str*/);
    _alloc_check(rc_list, flag_no_mem, out);

    if (rc != LSM_ERR_OK) {
        err_msg_obj = PyUnicode_FromString(lsm_error_message_get(lsm_err));
        lsm_error_free(lsm_err);
        lsm_err = NULL;
        _alloc_check(err_msg_obj, flag_no_mem, out);
        goto out;
    } else {
        rc_obj = PyLong_FromLongLong((unsigned long long)slot_itr);
        _alloc_check(rc_obj, flag_no_mem, out);
        err_msg_obj = PyUnicode_FromString("");
        _alloc_check(err_msg_obj, flag_no_mem, out);
    }

out:
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

static PyObject *led_slot_iterator_free(PyObject *self, PyObject *args,
                                        PyObject *kwargs) {
    unsigned long long handle_addr = 0;
    unsigned long long itr_addr = 0;
    lsm_led_handle *handle = NULL;
    lsm_led_slot_itr *itr = NULL;

    _UNUSED(self);
    _UNUSED(kwargs);

    if (!PyArg_ParseTuple(args, "KK", &handle_addr, &itr_addr)) {
        return NULL;
    }

    handle = (lsm_led_handle *)handle_addr;
    itr = (lsm_led_slot_itr *)itr_addr;
    lsm_led_slot_iterator_free(handle, itr);
    Py_RETURN_NONE;
}

static PyObject *led_slot_iterator_reset(PyObject *self, PyObject *args,
                                         PyObject *kwargs) {
    unsigned long long handle_addr = 0;
    unsigned long long itr_addr = 0;
    lsm_led_handle *handle = NULL;
    lsm_led_slot_itr *itr = NULL;

    _UNUSED(self);
    _UNUSED(kwargs);

    if (!PyArg_ParseTuple(args, "KK", &handle_addr, &itr_addr)) {
        return NULL;
    }

    handle = (lsm_led_handle *)handle_addr;
    itr = (lsm_led_slot_itr *)itr_addr;
    lsm_led_slot_iterator_reset(handle, itr);
    Py_RETURN_NONE;
}

static PyObject *led_slot_iterator_next(PyObject *self, PyObject *args,
                                        PyObject *kwargs) {
    PyObject *rc_obj = NULL;
    unsigned long long handle_addr = 0;
    unsigned long long itr_addr = 0;
    lsm_led_handle *handle = NULL;
    lsm_led_slot_itr *itr = NULL;
    lsm_led_slot *slot = NULL;

    bool flag_no_mem = false;

    _UNUSED(self);
    _UNUSED(kwargs);

    if (!PyArg_ParseTuple(args, "KK", &handle_addr, &itr_addr)) {
        return NULL;
    }

    handle = (lsm_led_handle *)handle_addr;
    itr = (lsm_led_slot_itr *)itr_addr;

    slot = lsm_led_slot_next(handle, itr);

    if (slot)
        rc_obj = PyLong_FromLongLong((unsigned long long)slot);
    else {
        rc_obj = PyLong_FromLongLong(0ull);
    }

    _alloc_check(rc_obj, flag_no_mem, out);

out:
    if (flag_no_mem) {
        return PyErr_NoMemory();
    }

    return rc_obj;
}

static PyObject *led_slot_status_get(PyObject *self, PyObject *args,
                                     PyObject *kwargs) {

    PyObject *rc_obj = NULL;
    unsigned long long slot_addr = 0;
    lsm_led_slot *slot = NULL;
    uint32_t state = 0;
    bool flag_no_mem = false;

    _UNUSED(self);
    _UNUSED(kwargs);

    if (!PyArg_ParseTuple(args, "K", &slot_addr)) {
        return NULL;
    }

    slot = (lsm_led_slot *)slot_addr;
    state = lsm_led_slot_status_get(slot);

    rc_obj = PyLong_FromLongLong(state);
    _alloc_check(rc_obj, flag_no_mem, out);

out:
    if (flag_no_mem) {
        return PyErr_NoMemory();
    }

    return rc_obj;
}

static PyObject *led_slot_status_set(PyObject *self, PyObject *args,
                                     PyObject *kwargs) {
    lsm_error *lsm_err = NULL;
    lsm_flag flag = 0;
    int rc = LSM_ERR_OK;
    PyObject *rc_list = NULL;
    PyObject *err_msg_obj = NULL;
    unsigned long long handle_addr = 0;
    unsigned long long slot_addr = 0;
    unsigned long long state = 0;
    lsm_led_handle *handle = NULL;
    lsm_led_slot *slot = NULL;
    PyObject *err_no_obj = NULL;
    bool flag_no_mem = false;

    _UNUSED(self);
    _UNUSED(kwargs);

    if (!PyArg_ParseTuple(args, "KKK", &handle_addr, &slot_addr, &state)) {
        return NULL;
    }

    handle = (lsm_led_handle *)handle_addr;
    slot = (lsm_led_slot *)slot_addr;

    rc = lsm_led_slot_status_set(handle, slot, state, &lsm_err, flag);

    err_no_obj = PyInt_FromLong(rc);
    _alloc_check(err_no_obj, flag_no_mem, out);
    rc_list = PyList_New(3 /* None, errno, err_str*/);
    _alloc_check(rc_list, flag_no_mem, out);

    if (rc != LSM_ERR_OK) {
        err_msg_obj = PyUnicode_FromString(lsm_error_message_get(lsm_err));
        lsm_error_free(lsm_err);
        lsm_err = NULL;
        _alloc_check(err_msg_obj, flag_no_mem, out);
        goto out;
    } else {
        err_msg_obj = PyUnicode_FromString("");
        _alloc_check(err_msg_obj, flag_no_mem, out);
    }
out:
    if (lsm_err != NULL) {
        lsm_error_free(lsm_err);
    }

    if (flag_no_mem) {
        Py_XDECREF(rc_list);
        Py_XDECREF(err_no_obj);
        Py_XDECREF(err_msg_obj);
        return PyErr_NoMemory();
    }

    PyList_SET_ITEM(rc_list, 0, Py_None);
    PyList_SET_ITEM(rc_list, 1, err_no_obj);
    PyList_SET_ITEM(rc_list, 2, err_msg_obj);
    return rc_list;
}

static PyObject *led_slot_id(PyObject *self, PyObject *args, PyObject *kwargs) {

    PyObject *rc_obj = NULL;
    unsigned long long slot_addr = 0;
    lsm_led_slot *slot = NULL;
    bool flag_no_mem = false;
    const char *id = NULL;

    _UNUSED(self);
    _UNUSED(kwargs);

    if (!PyArg_ParseTuple(args, "K", &slot_addr)) {
        return NULL;
    }

    slot = (lsm_led_slot *)slot_addr;

    id = lsm_led_slot_id(slot);
    if (id) {
        rc_obj = PyUnicode_FromString(id);
    } else {
        rc_obj = PyUnicode_FromString("");
    }

    _alloc_check(rc_obj, flag_no_mem, out);

out:
    if (flag_no_mem) {
        return PyErr_NoMemory();
    }

    return rc_obj;
}

static PyObject *led_slot_device(PyObject *self, PyObject *args,
                                 PyObject *kwargs) {

    PyObject *rc_obj = Py_None;
    unsigned long long slot_addr = 0;
    lsm_led_slot *slot = NULL;
    bool flag_no_mem = false;
    const char *device_node = NULL;

    _UNUSED(self);
    _UNUSED(kwargs);

    if (!PyArg_ParseTuple(args, "K", &slot_addr)) {
        return NULL;
    }

    slot = (lsm_led_slot *)slot_addr;
    device_node = lsm_led_slot_device(slot);
    if (device_node) {
        rc_obj = PyUnicode_FromString(device_node);
    }

    _alloc_check(rc_obj, flag_no_mem, out);

out:
    if (flag_no_mem) {
        return PyErr_NoMemory();
    }

    return rc_obj;
}

#if PY_MAJOR_VERSION >= 3
#define MOD_DEF(name, methods)                                                 \
    static struct PyModuleDef moduledef = {PyModuleDef_HEAD_INIT,              \
                                           name,                               \
                                           NULL,                               \
                                           -1,                                 \
                                           methods,                            \
                                           NULL,                               \
                                           NULL,                               \
                                           NULL,                               \
                                           NULL};                              \
    return PyModule_Create(&moduledef);
#else
#define MOD_DEF(name, methods) Py_InitModule(name, methods);
#endif

#if PY_MAJOR_VERSION >= 3
PyMODINIT_FUNC PyInit__clib(void)
#else
PyMODINIT_FUNC init_clib(void)
#endif
{
    MOD_DEF("_clib", _methods);
}
