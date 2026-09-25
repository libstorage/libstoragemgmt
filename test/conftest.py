# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2026 Red Hat, Inc.
"""
plugin_test.py is a specification-based integration script that drives the real
"lsm" package (the compiled _clib extension and all) against a running
daemon; it only matches pytest's ``*_test.py`` discovery pattern by
accident.  "lsm" is not installed on a stock system and is only put on
sys.path by runtests.sh's golang/root install step, which runs after the
standalone unit test suites, so collecting the whole test/ directory
before that step aborts on plugin_test.py's "import lsm" every time.

Skip it only when "lsm" is genuinely absent -- find_spec() asks without
importing, so an ImportError raised from our own code still fails loudly.
"""

from importlib.util import find_spec

collect_ignore = ['plugin_test.py'] if find_spec('lsm') is None else []
