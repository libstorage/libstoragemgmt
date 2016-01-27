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

#include <libstoragemgmt/libstoragemgmt.h>

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

/*
 * TODO: Support METH_VARARGS | METH_KEYWORDS
 */
static PyMethodDef _scsi_methods[] = {
    {"_disk_paths_of_vpd83",  (PyCFunction) disk_paths_of_vpd83,
     METH_VARARGS | METH_KEYWORDS, disk_paths_of_vpd83_docstring},
    {"_vpd83_of_disk_path",  (PyCFunction) vpd83_of_disk_path,
     METH_VARARGS | METH_KEYWORDS, vpd83_of_disk_path_docstring},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};


static PyObject *disk_paths_of_vpd83(PyObject *self, PyObject *args,
                                     PyObject *kwargs)
{
    static const char *kwlist[] = {"vpd83", NULL};
    const char *vpd83 = NULL;
    lsm_string_list *sd_path_list = NULL;
    lsm_error *lsm_err = NULL;
    int rc = LSM_ERR_OK;
    uint8_t i = 0;
    const char *sd_path = NULL;
    PyObject *sd_path_obj = NULL;
    PyObject *rc_list = NULL;
    PyObject *sd_paths_obj = NULL;
    PyObject *err_msg_obj = NULL;
    PyObject *err_no_obj = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s", (char **) kwlist,
                                     &vpd83))
        return NULL;

    rc = lsm_scsi_disk_paths_of_vpd83(vpd83, &sd_path_list, &lsm_err);
    /* In python API definition, we don't raise error in function, only
     * return empty list.
     */

    if ((rc != LSM_ERR_OK)) {
        sd_paths_obj = PyList_New(0);
        err_no_obj = PyInt_FromLong(rc);
        err_msg_obj = PyString_FromString(lsm_error_message_get(lsm_err));
        lsm_error_free(lsm_err);
        goto out;
    }

    err_no_obj = PyInt_FromLong(LSM_ERR_OK);
    err_msg_obj = PyString_FromString("");
    sd_paths_obj = PyList_New(0 /* no preallocation */);
    if (sd_paths_obj == NULL) {
        goto out;
    }

    if (sd_path_list == NULL) {
        goto out;
    }

    for (; i < lsm_string_list_size(sd_path_list); ++i) {
        sd_path = lsm_string_list_elem_get(sd_path_list, i);
        if (sd_path == NULL)
            continue;
        sd_path_obj = PyString_FromString(sd_path);
        if (sd_path_obj == NULL) {
            /* No memory */
            Py_XDECREF(sd_paths_obj);
            Py_XDECREF(err_msg_obj);
            Py_XDECREF(err_no_obj);
            lsm_string_list_free(sd_path_list);
            return PyErr_NoMemory();
        }
        PyList_Append(sd_paths_obj, sd_path_obj);
        /* ^ PyList_Append will increase the reference count of sd_path_obj */
        Py_DECREF(sd_path_obj);
    }

 out:
    rc_list = PyList_New(3 /* list, errno, err_str*/);

    if ((rc_list == NULL) || (err_no_obj == NULL) || (err_msg_obj == NULL) ||
        (sd_paths_obj == NULL)) {
        Py_XDECREF(rc_list);
        Py_XDECREF(err_no_obj);
        Py_XDECREF(err_msg_obj);
        Py_XDECREF(sd_paths_obj);
        return PyErr_NoMemory();
    }

    if (sd_path_list != NULL)
        lsm_string_list_free(sd_path_list);

    PyList_SET_ITEM(rc_list, 0, sd_paths_obj);
    PyList_SET_ITEM(rc_list, 1, err_no_obj);
    PyList_SET_ITEM(rc_list, 2, err_msg_obj);

    return rc_list;
}

static PyObject *vpd83_of_disk_path(PyObject *self, PyObject *args,
                                    PyObject *kwargs)
{
    static const char *kwlist[] = {"sd_path", NULL};
    const char *vpd83 = NULL;
    const char *sd_path = NULL;
    lsm_error *lsm_err = NULL;
    int rc = LSM_ERR_OK;
    PyObject *rc_list = NULL;
    PyObject *vpd83_obj = NULL;
    PyObject *err_msg_obj = NULL;
    PyObject *err_no_obj = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s", (char **) kwlist,
                                     &sd_path))
        return NULL;

    rc = lsm_scsi_vpd83_of_disk_path(sd_path, &vpd83, &lsm_err);

    err_no_obj = PyInt_FromLong(rc);

    if (vpd83 == NULL)
        vpd83_obj = PyString_FromString("");
    else
        vpd83_obj = PyString_FromString(vpd83);

    if (rc != LSM_ERR_OK) {
        err_msg_obj = PyString_FromString(lsm_error_message_get(lsm_err));
        lsm_error_free(lsm_err);
        goto out;
    } else {
        err_msg_obj = PyString_FromString("");
    }

    free((char *) vpd83);

 out:
    rc_list = PyList_New(3 /* vpd83, errno, err_str*/);

    if ((rc_list == NULL) || (err_no_obj == NULL) || (err_msg_obj == NULL) ||
        (vpd83_obj == NULL)) {
        Py_XDECREF(rc_list);
        Py_XDECREF(err_no_obj);
        Py_XDECREF(err_msg_obj);
        Py_XDECREF(vpd83_obj);
        return PyErr_NoMemory();
    }

    PyList_SET_ITEM(rc_list, 0, vpd83_obj);
    PyList_SET_ITEM(rc_list, 1, err_no_obj);
    PyList_SET_ITEM(rc_list, 2, err_msg_obj);

    return rc_list;
}

PyMODINIT_FUNC init_scsi_clib(void)
{
        (void) Py_InitModule("_scsi_clib", _scsi_methods);
}
