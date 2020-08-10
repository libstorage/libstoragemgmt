#include <Python.h>
#include <mntent.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/statvfs.h>

#include <libstoragemgmt/libstoragemgmt.h>

#define _BUFF_SIZE       1024
#define LSF_LOCAL_MOUNTS "/proc/self/mounts"

#if PY_MAJOR_VERSION > 2
#define PyInt_FromLong PyLong_FromLong
#endif

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

#define _UNUSED(x) (void)(x)

static PyObject *find_fsid_bypath(PyObject *self, PyObject *args) {
    const char *path = NULL;
    PyObject *err = NULL;
    char *out = NULL;
    PyObject *result = NULL;

    _UNUSED(self);

    if (!PyArg_ParseTuple(args, "s", &path))
        return NULL;

    struct statvfs st;

    if (statvfs(path, &st) == -1) {
        err = PyExc_OSError;
        PyErr_SetFromErrno(err);
        return NULL;
    }

    if (st.f_fsid == 0) {
        err = PyExc_OSError;
        PyErr_SetString(err, "No FSID found");
        return NULL;
    }

    if (asprintf(&out, "%lx", st.f_fsid) < 0) {
        err = PyExc_MemoryError;
        PyErr_SetFromErrno(err);
        return NULL;
    }

    result = PyUnicode_FromString(out);
    free(out);

    if (result == NULL) {
        err = PyExc_MemoryError;
        PyErr_SetString(err, "Error allocating String");
    }

    return result;
}

static bool py_in_list(const char *needle, PyObject *haystack) {
    if (haystack == NULL)
        return false;
    if (needle == NULL)
        return false;
    if (!PyList_Check(haystack))
        return false;

    Py_ssize_t len = PyList_Size(haystack);
    Py_ssize_t i = 0;
    for (i = 0; i < len; i++) {
        PyObject *item = PyList_GetItem(haystack, i);
        if (item == NULL)
            continue;
        PyObject *bytes = PyUnicode_AsEncodedString(item, "utf-8", "ignore");
        if (bytes == NULL)
            continue;
        const char *value = PyBytes_AS_STRING(bytes);
        Py_DECREF(bytes);
        if (value == NULL)
            continue;
        if (strcmp(needle, value) == 0)
            return true;
    }
    return false;
}

static PyObject *list_mount_paths(PyObject *self, PyObject *args) {
    FILE *f = NULL;
    PyObject *result = NULL;
    PyObject *err = NULL;

    _UNUSED(self);
    _UNUSED(args);

    /* open the system mount tab list */
    if ((f = setmntent(LSF_LOCAL_MOUNTS, "r")) == NULL) {
        /* on error, throw OSError exception */
        err = PyExc_OSError;
        PyErr_SetFromErrno(err);
        goto out;
    }

    result = PyList_New(0);
    if (!result) {
        err = PyExc_MemoryError;
        PyErr_SetString(err, "Error allocating list");
        goto out;
    }

    char buff[_BUFF_SIZE];
    struct mntent me;
    while (getmntent_r(f, &me, buff, _BUFF_SIZE) != NULL) {
        struct statvfs st;

        // not a suitable / valid mount point
        if (statvfs(me.mnt_dir, &st) == -1)
            continue;
        if (st.f_fsid == 0)
            continue;

        // check for duplicates
        if (py_in_list(me.mnt_dir, result))
            continue;

        // add to the list
        PyObject *str_obj = PyUnicode_FromString(me.mnt_dir);
        if (str_obj == NULL) {
            err = PyExc_MemoryError;
            PyErr_SetString(err, "Error allocating String");
            goto out;
        }

        if (PyList_Append(result, str_obj) == -1) {
            // flag that an exception was thrown from below
            err = PyExc_Exception;
            goto out;
        }
    }

out:
    if (err) {
        Py_XDECREF(result);
        if (f) {
            endmntent(f);
        }
        return NULL;
    }

    return result;
}

static PyMethodDef _methods[] = {
    {"get_fsid", find_fsid_bypath, METH_VARARGS,
     "Find Filesystem ID for given path."},
    {"list_mounts", list_mount_paths, METH_VARARGS, "List mounted filesystems"},
    {NULL, NULL, 0, NULL} /* Sentinel */
};

#if PY_MAJOR_VERSION >= 3
PyMODINIT_FUNC PyInit_nfs_clib(void)
#else
PyMODINIT_FUNC initnfs_clib(void)

#endif
{
    MOD_DEF("nfs_clib", _methods);
}
