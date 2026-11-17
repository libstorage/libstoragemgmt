# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2026 Red Hat, Inc.
"""
Regression test for a2bad671 ("packaging: Add systemd resource limits to
lsmd service"): an unprivileged local user who can reach lsmd's IPC socket
can make it fork workers without bound (lsmd forks one per connection);
TasksMax and friends cap that blast radius.

This parses the packaging source file directly (not an installed copy), and
only goes on to ask systemd itself to validate it when systemd-analyze is
present - skipping that part cleanly otherwise, since it is not a hard
build/test dependency of this project.
"""

import os
import re
import shutil
import subprocess
import tempfile
import unittest

_PACKAGING_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..',
                              'packaging')

SERVICE_FILE = os.path.join(_PACKAGING_DIR, 'daemon', 'libstoragemgmt.service')

# The [Service] directives a2bad671 added, each required to be present with
# some non-empty value; the exact values are a deployment tuning choice, not
# part of the security property this guards (a cap exists at all).
REQUIRED_SERVICE_LIMITS = (
    'TasksMax',
    'MemoryHigh',
    'MemoryMax',
    'CPUQuota',
    'LimitNOFILE',
)


def _read_ini_like_sections(path):
    """Minimal systemd-unit-file reader: {section: {key: value}}, last
    assignment of a key in a section wins (as it does for these directives
    in real systemd unit parsing)."""
    sections = {}
    current = None
    with open(path) as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line or line.startswith('#') or line.startswith(';'):
                continue
            m = re.match(r'^\[(.+)\]$', line)
            if m:
                current = m.group(1)
                sections.setdefault(current, {})
                continue
            if current is not None and '=' in line:
                key, _, value = line.partition('=')
                sections[current][key.strip()] = value.strip()
    return sections


class TestLsmdServiceResourceLimits(unittest.TestCase):

    def setUp(self):
        self.assertTrue(os.path.isfile(SERVICE_FILE),
                        "%s not found" % SERVICE_FILE)
        self.sections = _read_ini_like_sections(SERVICE_FILE)
        self.assertIn('Service', self.sections,
                      "libstoragemgmt.service has no [Service] section")

    def test_resource_limits_present_in_service_section(self):
        service = self.sections['Service']
        missing = [k for k in REQUIRED_SERVICE_LIMITS if not service.get(k)]
        self.assertEqual(
            missing, [],
            "libstoragemgmt.service is missing (or has an empty) %s in "
            "[Service]" % missing)

    def test_systemd_analyze_verify_accepts_unit(self):
        """Full syntactic/semantic validation from systemd itself, when
        available. ExecStart's binary need not actually be installed for
        this test to be meaningful, so it is substituted with something
        that always exists."""
        systemd_analyze = shutil.which('systemd-analyze')
        if not systemd_analyze:
            self.skipTest("systemd-analyze not available")

        with open(SERVICE_FILE) as f:
            content = f.read()
        content = re.sub(r'(?m)^ExecStart=.*$', 'ExecStart=/bin/true', content)

        with tempfile.NamedTemporaryFile(mode='w',
                                         suffix='-libstoragemgmt.service',
                                         delete=False) as tmp:
            tmp.write(content)
            tmp_path = tmp.name

        try:
            proc = subprocess.run([systemd_analyze, 'verify', tmp_path],
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT,
                                  text=True)
            self.assertEqual(
                proc.returncode, 0,
                "systemd-analyze verify rejected the service unit:\n%s" %
                proc.stdout)
        finally:
            os.unlink(tmp_path)


if __name__ == "__main__":
    unittest.main()
