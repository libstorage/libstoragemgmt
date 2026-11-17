# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2026 Red Hat, Inc.
"""
Regression tests for two packaging hardening fixes:

  a2bad671 ("packaging: Add systemd resource limits to lsmd service"):
      an unprivileged local user who can reach lsmd's IPC socket can make it
      fork workers without bound; TasksMax et al. cap that blast radius.

  b13a8bed ("daemon: Restore home dir for sysusers libstoragemgmt account"):
      the sysusers.d line for the libstoragemgmt account lost its HomeDir
      column in a migration, so it defaulted to '/' - a hardening playbook
      that chowns a service account's home dir can then reassign ownership
      of '/' itself. HomeDir has to name an account-private directory
      (/run/lsm, matching the tmpfiles.d entry) and the shell has to be an
      explicit nologin.

These parse the packaging source files directly (not an installed copy),
and only go on to ask systemd itself to validate them when it is present -
skipping that part cleanly otherwise, since neither systemd-analyze nor
systemd-sysusers is a hard build/test dependency of this project.
"""

import os
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest

_PACKAGING_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..',
                              'packaging')

SERVICE_FILE = os.path.join(_PACKAGING_DIR, 'daemon', 'libstoragemgmt.service')
SYSUSERS_FILE = os.path.join(_PACKAGING_DIR, 'daemon', 'user_group',
                             'libstoragemgmt.conf')
TMPFILES_FILE = os.path.join(_PACKAGING_DIR, 'daemon', 'libstoragemgmt.conf')

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


def _parse_tmpfiles_dir(path, target_dir):
    """Returns True if tmpfiles.d file 'path' has a 'D' (or 'd') line
    creating exactly 'target_dir'."""
    with open(path) as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line or line.startswith('#'):
                continue
            fields = line.split()
            if len(fields) >= 2 and fields[0] in ('d', 'D') and \
                    fields[1] == target_dir:
                return True
    return False


def _parse_sysusers_line(path, account_name):
    """Returns the sysusers.d fields for 'account_name' as a dict with keys
    type, name, id, gecos, home_dir, shell - honoring the quoting sysusers.d
    allows around the (space-containing) GECOS field. Fields past GECOS are
    optional in the format; missing ones come back as None."""
    with open(path) as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line or line.startswith('#'):
                continue
            fields = shlex.split(line)
            if len(fields) >= 2 and fields[1] == account_name:
                names = ('type', 'name', 'id', 'gecos', 'home_dir', 'shell')
                return dict(zip(names, fields + [None] * len(names)))
    return None


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


class TestSysusersHomeDir(unittest.TestCase):

    ACCOUNT = 'libstoragemgmt'

    def setUp(self):
        self.assertTrue(os.path.isfile(SYSUSERS_FILE),
                        "%s not found" % SYSUSERS_FILE)
        self.fields = _parse_sysusers_line(SYSUSERS_FILE, self.ACCOUNT)
        self.assertIsNotNone(
            self.fields, "no sysusers.d line for account %r in %s" %
            (self.ACCOUNT, SYSUSERS_FILE))

    def test_home_dir_is_not_empty_or_root(self):
        home_dir = self.fields['home_dir']
        self.assertTrue(
            home_dir, "HomeDir is empty/missing - sysusers.d then defaults "
            "the account's home to '/'")
        self.assertNotEqual(
            home_dir, '/',
            "HomeDir is '/' - a hardening playbook that chowns a service "
            "account's home dir would reassign ownership of '/' itself")

    def test_home_dir_matches_tmpfiles_directory(self):
        """HomeDir should name the directory this package actually creates
        and owns for the daemon (via tmpfiles.d), not a hardcoded literal
        that could drift from it."""
        self.assertTrue(os.path.isfile(TMPFILES_FILE),
                        "%s not found" % TMPFILES_FILE)
        home_dir = self.fields['home_dir']
        self.assertTrue(
            _parse_tmpfiles_dir(TMPFILES_FILE, home_dir),
            "HomeDir %r in %s has no matching 'D' entry in %s" %
            (home_dir, SYSUSERS_FILE, TMPFILES_FILE))

    def test_shell_is_explicit_nologin(self):
        shell = self.fields['shell']
        self.assertTrue(shell, "Shell is empty/missing")
        self.assertEqual(
            os.path.basename(shell), 'nologin',
            "Shell %r is not an explicit nologin shell" % (shell, ))

    def test_systemd_sysusers_dry_run_accepts_file(self):
        systemd_sysusers = shutil.which('systemd-sysusers')
        if not systemd_sysusers:
            self.skipTest("systemd-sysusers not available")

        proc = subprocess.run([systemd_sysusers, '--dry-run', SYSUSERS_FILE],
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT,
                              text=True)
        self.assertEqual(
            proc.returncode, 0,
            "systemd-sysusers --dry-run rejected the file:\n%s" % proc.stdout)


if __name__ == "__main__":
    unittest.main()
