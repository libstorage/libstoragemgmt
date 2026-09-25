# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2026 Red Hat, Inc.
"""
Shared bootstrap for the standalone python unit tests.

The modules under test live in python_binding/lsm, but importing the real
``lsm`` package drags in the compiled ``_clib`` C extension, which these
tests deliberately do without.  So we register a stub ``lsm`` package
pointing at the source directory and load the individual modules from
their files.

Every test module must end up sharing one stub, because whichever module
pytest imports first is the one that owns ``sys.modules['lsm']``; a
second module populating attributes on a private copy leaves the real
entry empty and the module under test fails to import from it.  Hence a
single idempotent ``install()`` that creates the stub once and always
returns the object actually registered in ``sys.modules``.
"""

import importlib.util
import os
import sys
import types

LSM_SRC_DIR = os.path.abspath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), '..',
                 'python_binding', 'lsm'))


def _from_source_tree(mod):
    """True if mod was loaded out of python_binding/lsm.

    The stub package itself has no __file__, so it is recognised by the
    __path__ we gave it.
    """
    path = getattr(mod, '__file__', None)
    if path is None:
        return getattr(mod, '__path__', None) == [LSM_SRC_DIR]
    return os.path.abspath(path).startswith(LSM_SRC_DIR + os.sep)


def _reuse(name, mod):
    """Return an already-registered module, refusing an installed copy.

    libstoragemgmt is usually installed system wide, so without this an
    earlier "import lsm" anywhere in the process would quietly hand these
    tests the installed library - and its _clib extension - instead of the
    working tree, and they would pass against the wrong code.
    """
    if not _from_source_tree(mod):
        raise RuntimeError(
            "%s is already loaded from %s; these tests must run against "
            "%s.  Run them in a process that has not imported the installed "
            "lsm package." %
            (name, getattr(mod, '__file__', 'an unknown '
                           'location'), LSM_SRC_DIR))
    return mod


def load(name, filename):
    """Load python_binding/lsm/<filename> as module <name>, once."""
    existing = sys.modules.get(name)
    if existing is not None:
        return _reuse(name, existing)

    path = os.path.join(LSM_SRC_DIR, filename)
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


def install():
    """Create or reuse the stub ``lsm`` package and populate it.

    Returns whatever ``lsm`` object is registered in ``sys.modules``;
    individual modules come from ``load()``, which is idempotent too.
    """
    pkg = sys.modules.get('lsm')
    if pkg is None:
        pkg = types.ModuleType('lsm')
        pkg.__path__ = [LSM_SRC_DIR]
        sys.modules['lsm'] = pkg
    else:
        _reuse('lsm', pkg)

    common = load('lsm._common', '_common.py')
    load('lsm._data', '_data.py')
    load('lsm._transport', '_transport.py')

    # _pluginrunner.py does "from lsm import LsmError, error, ErrorNumber",
    # so the stub package has to expose those names.
    pkg.LsmError = common.LsmError
    pkg.error = common.error
    pkg.ErrorNumber = common.ErrorNumber

    # It also does "from lsm.lsmcli import cmd_line_wrapper"; stub that out
    # so we don't drag in the CLI (and the C extension) it lives beside.
    lsmcli = sys.modules.get('lsm.lsmcli')
    if lsmcli is None:
        lsmcli = types.ModuleType('lsm.lsmcli')
        lsmcli.cmd_line_wrapper = lambda *args, **kwargs: None
        sys.modules['lsm.lsmcli'] = lsmcli
    pkg.lsmcli = lsmcli

    return pkg
