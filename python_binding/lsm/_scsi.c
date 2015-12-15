/*
 * Copyright (C) 2015 Red Hat, Inc.
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
    "Version:\n"
    "    1.3\n"
    "Usage:\n"
    "    Find out the /dev/sdX paths for given SCSI VPD page 0x83 NAA type\n"
    "    ID. Considering multipath, certain VPD83 might have multiple disks\n"
    "    associated.\n"
    "Parameters:\n"
    "    vpd83 (string)\n"
    "        The VPD83 NAA type ID.\n"
    "Returns:\n"
    "    sd_path (list of string)\n"
    "        Empty list is not found. The string format is '/dev/sd[a-z]+'.\n"
    "\n"
    "SpecialExceptions:\n"
    "    N/A\n"
    "Capability:\n"
    "    N/A\n"
    "        No capability required from plugin as this is a library level\n"
    "        method.";

static const char vpd83_of_disk_path_docstring[] =
    "Version:\n"
    "    1.3\n"
    "Usage:\n"
    "    Query the SCSI VPD83 NAA ID of given scsi disk path\n"
    "Parameters:\n"
    "    sd_path (string)\n"
    "        The SCSI disk path, example '/dev/sdb'. Empty string is failure\n"
    "Returns:\n"
    "    vpd83 (string)\n"
    "        String of VPD83 NAA ID. Empty string if not supported.\n"
    "         The string format regex is:\n"
    "        (?:^6[0-9a-f]{31})|(?:^[235][0-9a-f]{15})$\n"
    "SpecialExceptions:\n"
    "    N/A\n"
    "Capability:\n"
    "    N/A\n"
    "        No capability required from plugin as this is a library level\n"
    "        method.";

static PyObject *disk_paths_of_vpd83(PyObject *self, PyObject *args,
                                     PyObject *kwargs);
static PyObject *vpd83_of_disk_path(PyObject *self, PyObject *args,
                                    PyObject *kwargs);

/*
 * TODO: Support METH_VARARGS | METH_KEYWORDS
 */
static PyMethodDef _scsi_methods[] = {
    {"disk_paths_of_vpd83",  (PyCFunction) disk_paths_of_vpd83,
     METH_VARARGS | METH_KEYWORDS, disk_paths_of_vpd83_docstring},
    {"vpd83_of_disk_path",  (PyCFunction) vpd83_of_disk_path,
     METH_VARARGS | METH_KEYWORDS, vpd83_of_disk_path_docstring},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};


static PyObject *disk_paths_of_vpd83(PyObject *self, PyObject *args,
                                     PyObject *kwargs)
{
    static const char *kwlist[] = {"vpd83", NULL};
    const char *vpd83 = NULL;
    PyObject *rc_list = NULL;
    lsm_string_list *sd_path_list = NULL;
    lsm_error *lsm_err = NULL;
    int rc = LSM_ERR_OK;
    uint8_t i = 0;
    const char *sd_path = NULL;
    PyObject *sd_path_obj = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s", (char **) kwlist,
                                     &vpd83))
        return NULL;

    rc = lsm_scsi_disk_paths_of_vpd83(vpd83, &sd_path_list, &lsm_err);
    /* In python API definition, we don't raise error in function, only
     * return empty list.
     */
    lsm_error_free(lsm_err);

    rc_list = PyList_New(0 /* No pre-allocation */);
    if (rc_list == NULL)
        return NULL;

    if ((rc != LSM_ERR_OK) || (sd_path_list == NULL))
        return rc_list;

    for (; i < lsm_string_list_size(sd_path_list); ++i) {
        sd_path = lsm_string_list_elem_get(sd_path_list, i);
        if (sd_path == NULL)
            continue;
        sd_path_obj = PyString_FromString(sd_path);
        if (sd_path_obj == NULL) {
            Py_DECREF(rc_list);
            rc_list = NULL;
            goto out;
        }
        PyList_Append(rc_list, sd_path_obj);
        /* ^ PyList_Append will increase the reference count of sd_path_obj */
        Py_DECREF(sd_path_obj);
    }

 out:
    lsm_string_list_free(sd_path_list);

    return rc_list;
}

static PyObject *vpd83_of_disk_path(PyObject *self, PyObject *args,
                                    PyObject *kwargs)
{
    static const char *kwlist[] = {"sd_path", NULL};
    const char *vpd83 = NULL;
    PyObject *rc_vpd83 = NULL;
    const char *sd_path = NULL;
    lsm_error *lsm_err = NULL;
    int rc = LSM_ERR_OK;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s", (char **) kwlist,
                                     &sd_path))
        return NULL;

    rc = lsm_scsi_vpd83_of_disk_path(sd_path, &vpd83, &lsm_err);
    if ((rc != LSM_ERR_OK) || (vpd83 == NULL)) {
        /* TODO(Gris Ge): Raise exception for error. */
        lsm_error_free(lsm_err);
        return PyString_FromString("");
    }

    rc_vpd83 = PyString_FromString(vpd83);

    free((char *) vpd83);
    return rc_vpd83;
}

PyMODINIT_FUNC init_scsi(void)
{
        (void) Py_InitModule("_scsi", _scsi_methods);
}
