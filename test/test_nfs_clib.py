# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2026 Red Hat, Inc.
"""
Regression tests for 52d1d9b0 ("Fix use-after-free and resource leaks in
nfs_clib.c"), parts 2 and 3 - both in list_mount_paths() (the C
implementation behind plugin/nfs_plugin/nfs_clib's list_mounts()):

  2. endmntent(f) was only called on the error path, so the FILE* opened by
     setmntent() leaked on every single successful call.
  3. The PyObject reference for each mount point string was never released
     after being appended to the result list, leaking one reference per
     mount point per call (the object itself is never freed, since its
     refcount never reaches zero).

part 1 (the py_in_list() use-after-free) is covered separately by
test_nfs_clib_py_in_list.c, which can reach that 'static' function directly;
these two are only reachable through the public list_mounts() entry point.
"""

import os
import resource
import sys
import unittest

# nfs_clib is a compiled extension (built by plugin/nfs_plugin/Makefile.am);
# find it next to this test's own build directory rather than requiring it
# to be installed.
_HERE = os.path.dirname(os.path.abspath(__file__))
_NFS_CLIB_DIRS = [
    os.path.join(_HERE, '..', 'plugin', 'nfs_plugin', '.libs'),
    os.path.join(_HERE, '..', '..', 'plugin', 'nfs_plugin', '.libs'),
]

for _d in _NFS_CLIB_DIRS:
    if os.path.isdir(_d):
        sys.path.insert(0, _d)

try:
    import nfs_clib
except ImportError as e:
    nfs_clib = None
    _import_error = e


@unittest.skipIf(nfs_clib is None,
                 "nfs_clib extension not built (NFS plug-in disabled?)")
class TestNfsClibListMounts(unittest.TestCase):

    def test_repeated_calls_do_not_leak_file_descriptors(self):
        """Every successful list_mounts() call used to leak the FILE* from
        setmntent() (endmntent() lived only on the error path). A low
        RLIMIT_NOFILE turns that leak into a hard failure within a handful
        of calls; the fixed code must survive far more than that."""
        soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
        try:
            resource.setrlimit(resource.RLIMIT_NOFILE, (64, hard))
            for i in range(300):
                result = nfs_clib.list_mounts()
                self.assertIsInstance(result, list,
                                      "list_mounts() failed on call %d" % i)
        finally:
            resource.setrlimit(resource.RLIMIT_NOFILE, (soft, hard))

    def test_result_strings_are_not_over_referenced(self):
        """Each mount point string used to keep one extra, never-released
        reference (the one list_mount_paths() itself held after appending
        it to the result list, and never gave up).

        sys.getrefcount()'s absolute return value is not a portable
        constant to assert against: it depends on implementation details
        such as whether a value was bound via a global or a local (observed
        directly: the identical loop gave 3 at module scope and 2 inside a
        function on this interpreter), so instead this compares the
        suspect string's refcount against a same-shape "control" string
        indexed out of a second, equivalent, definitely-not-leaked list.
        Both are fetched the same way (list[index], into a same-scope local
        variable) immediately before comparison, so any CPython-version- or
        scope-dependent overhead in the count applies equally to both sides;
        only a real extra reference on the nfs_clib side shows up as a
        difference.

        '/' is excluded: short, common strings like it are immortalized by
        CPython (their refcount is a sentinel, not a real count), which
        would make this comparison meaningless for it."""
        result = nfs_clib.list_mounts()
        self.assertTrue(result, "expected at least one mount point")

        # A generator expression (rather than a plain for loop) so no
        # leftover loop variable in this frame keeps its own alias of the
        # chosen string once a match is found - that alias would itself
        # look exactly like the leak this test is checking for.
        index = next(
            (i for i, candidate in enumerate(result) if candidate != '/'),
            None)
        if index is None:
            self.skipTest("only '/' was mounted; nothing non-immortal to "
                          "check")

        path = result[index]
        control_list = ['not-a-real-mount-point-%d' % index]
        control = control_list[0]

        self.assertEqual(sys.getrefcount(path), sys.getrefcount(control),
                         "%r has an extra, leaked reference" % (path, ))


if __name__ == "__main__":
    unittest.main()
