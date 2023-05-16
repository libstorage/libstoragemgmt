# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2015-2023 Red Hat, Inc.
#
# Author: Gris Ge <fge@redhat.com>

import subprocess
import os


def cmd_exec(cmds):
    """
    Execute provided command and return the STDOUT as string.
    Raise ExecError if command return code is not zero
    """
    cmd_popen = subprocess.Popen(cmds,
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE,
                                 env={"PATH": os.getenv("PATH")},
                                 universal_newlines=True)
    str_stdout = "".join(list(cmd_popen.stdout)).strip()
    str_stderr = "".join(list(cmd_popen.stderr)).strip()
    errno = cmd_popen.wait()
    if errno != 0:
        raise ExecError(" ".join(cmds), errno, str_stdout, str_stderr)
    return str_stdout


class ExecError(Exception):

    def __init__(self, cmd, errno, stdout, stderr, *args, **kwargs):
        Exception.__init__(self, *args, **kwargs)
        self.cmd = cmd
        self.errno = errno
        self.stdout = stdout
        self.stderr = stderr

    def __str__(self):
        return "cmd: '%s', errno: %d, stdout: '%s', stderr: '%s'" % \
            (self.cmd, self.errno, self.stdout, self.stderr)
