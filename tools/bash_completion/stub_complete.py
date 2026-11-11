#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 Red Hat, Inc.
#
# What a stub lsmcli of the completion tests runs for 'lsmcli __complete': the
# real completion of lsmcli, but with the values it would look up through the
# library asked of the stub instead, as 'stub list --type <type> -t<sep> ...',
# so a test controls them and sees how they were asked for. Needs the lsm
# module on the PYTHONPATH, see stage_lsm.sh.

import os
import subprocess
import sys

from lsm.lsmcli import completion

SEP = "\x1f"


def fetch_rows(list_type, wrapper_uri=None, plugin_exe=None):
    # As the real one: there is no look-up without a URI to look up.
    if not (wrapper_uri or completion.default_uri()):
        return []
    cmd = [
        os.environ["LSM_TEST_STUB"], "list", "--type", list_type, "-t" + SEP
    ]
    # Only the URI of a wrapper in front of lsmcli is passed, never one from
    # the line, and no filter.
    if wrapper_uri:
        cmd += ["-u", wrapper_uri]
    out = subprocess.run(cmd,
                         stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL,
                         stdin=subprocess.DEVNULL,
                         check=False).stdout
    text = out.decode("utf-8", "surrogateescape")
    return [line.split(SEP) for line in text.split("\n") if line]


completion.fetch_rows = fetch_rows
args = sys.argv[1:]
at = args.index("__complete")
sys.exit(completion.main(args[:at], args[at + 1:]))
