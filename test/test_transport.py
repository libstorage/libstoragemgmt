# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2011-2023 Red Hat, Inc.

import importlib.util
import os
import sys
import types
import unittest

# Import python_binding/lsm/_transport.py (and the modules it needs) directly,
# bypassing lsm/__init__.py which pulls in the compiled _clib C extension.
_lsm_src_dir = os.path.join(os.path.dirname(__file__), '..', 'python_binding',
                            'lsm')


def _load(name, filename):
    path = os.path.join(_lsm_src_dir, filename)
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


_lsm_pkg = types.ModuleType("lsm")
_lsm_pkg.__path__ = [_lsm_src_dir]
sys.modules.setdefault("lsm", _lsm_pkg)

_load("lsm._common", "_common.py")
_load("lsm._data", "_data.py")
_transport = _load("lsm._transport", "_transport.py")

TransPort = _transport.TransPort
LsmError = _transport.LsmError


class TestTransport(_transport._TestTransport):
    """Runs TransPort's own unittest suite (defined in _transport.py) under
    pytest, without requiring the compiled lsm C extension."""


if __name__ == "__main__":
    unittest.main()
