# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2011-2023 Red Hat, Inc.

import os
import sys
import unittest

# Make the shared stub bootstrap importable however we're invoked (pytest
# from any directory, or running this file directly).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import lsm_stub

# Import python_binding/lsm/_transport.py (and the modules it needs)
# directly, bypassing lsm/__init__.py which pulls in the compiled _clib C
# extension.
lsm_stub.install()
_transport = lsm_stub.load('lsm._transport', '_transport.py')

TransPort = _transport.TransPort
LsmError = _transport.LsmError


class TestTransport(_transport._TestTransport):
    """Runs TransPort's own unittest suite (defined in _transport.py) under
    pytest, without requiring the compiled lsm C extension."""


if __name__ == "__main__":
    unittest.main()
