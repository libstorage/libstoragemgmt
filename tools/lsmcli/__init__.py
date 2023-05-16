# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2011-2023 Red Hat, Inc.

try:
    from .cmdline import cmd_line_wrapper
except ImportError:
    from cmdline import cmd_line_wrapper
