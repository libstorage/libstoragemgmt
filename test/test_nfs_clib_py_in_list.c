/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * Regression test for 52d1d9b0 ("Fix use-after-free and resource leaks in
 * nfs_clib.c"), part 1: py_in_list() in plugin/nfs_plugin/nfs_clib.c used
 * to Py_DECREF(bytes) - which, at refcount 0, deallocates it immediately -
 * before reading *and comparing* the C string pointer (PyBytes_AS_STRING())
 * it had just borrowed from that object. With CPython's normal pymalloc
 * allocator the freed buffer is usually still readable (recycled lazily),
 * so the bug rarely misbehaves; this is exactly what PYTHONMALLOC=debug
 * exists to catch; its debug allocator overwrites freed memory with 0xDD
 * ("forbidden bytes") instead, so a read through the dangling pointer finds
 * garbage rather than the original string, and strcmp() stops matching a
 * needle that is genuinely in the list.
 *
 * py_in_list() is 'static', so it is not reachable from a separately built
 * and linked test binary; this test #includes nfs_clib.c directly (building
 * it a second time, under a plain `int main()`, alongside the real
 * nfs_clib.la extension module) to call it directly with a controlled list,
 * independent of the host's actual mount table.
 */

/* Python.h must be the first include in a translation unit (it sets
 * feature-test macros like _POSIX_C_SOURCE that a standard header pulled in
 * first would have already set to a different value), so nfs_clib.c - which
 * includes it - comes before any other header here. */
#include "../plugin/nfs_plugin/nfs_clib.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* Builds a Python list of str objects from a NULL-terminated C array. */
static PyObject *build_list(const char *const *items) {
    PyObject *list = PyList_New(0);
    size_t i;

    for (i = 0; items[i] != NULL; i++) {
        PyObject *s = PyUnicode_FromString(items[i]);
        PyList_Append(list, s);
        Py_DECREF(s);
    }
    return list;
}

static void test_needle_present_is_found(void) {
    const char *items[] = {"/mnt/a", "/mnt/b", "/mnt/needle", "/mnt/c", NULL};
    PyObject *list = build_list(items);

    CHECK(py_in_list("/mnt/needle", list) == true,
          "a needle present in the list must be found");
    CHECK(py_in_list("/mnt/a", list) == true, "the first entry must be found");
    CHECK(py_in_list("/mnt/c", list) == true, "the last entry must be found");

    Py_DECREF(list);
}

static void test_needle_absent_is_not_found(void) {
    const char *items[] = {"/mnt/a", "/mnt/b", NULL};
    PyObject *list = build_list(items);

    CHECK(py_in_list("/mnt/does-not-exist", list) == false,
          "a needle absent from the list must not be found");

    Py_DECREF(list);
}

/* The bug is use-after-free, not a size issue, but a longer set of distinct
 * strings makes pymalloc's normal (non-debug) pool reuse more likely to
 * paper over a reintroduced bug, so this also guards against the test
 * itself only "accidentally" passing. */
static void test_many_entries_all_found(void) {
    char buf[64][32];
    const char *items[65];
    int i;

    for (i = 0; i < 64; i++) {
        snprintf(buf[i], sizeof(buf[i]), "/mnt/point-%02d", i);
        items[i] = buf[i];
    }
    items[64] = NULL;

    PyObject *list = build_list(items);

    for (i = 0; i < 64; i++) {
        CHECK(py_in_list(items[i], list) == true, items[i]);
    }

    Py_DECREF(list);
}

static void test_empty_and_null_inputs(void) {
    PyObject *empty = PyList_New(0);

    CHECK(py_in_list("anything", empty) == false,
          "an empty list must never match");
    CHECK(py_in_list(NULL, empty) == false, "a NULL needle must not match");
    CHECK(py_in_list("anything", NULL) == false,
          "a NULL haystack must not match");

    Py_DECREF(empty);
}

int main(void) {
    /* Read by CPython during Py_Initialize(); must be set first. On the
     * pre-fix code this is what turns the use-after-free from "usually
     * silently fine" into a reliable, detectable failure below. */
    setenv("PYTHONMALLOC", "debug", 1);

    Py_Initialize();

    test_needle_present_is_found();
    test_needle_absent_is_not_found();
    test_many_entries_all_found();
    test_empty_and_null_inputs();

    Py_Finalize();

    if (failures == 0) {
        printf("All py_in_list() tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d py_in_list() test(s) failed\n", failures);
    return 1;
}
