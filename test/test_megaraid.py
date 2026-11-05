import importlib.util
import json
import os
import sys
import types
import unittest
from unittest import mock

# Import utils directly to avoid megaraid_plugin.__init__ pulling in lsm (C extension).
_utils_path = os.path.join(os.path.dirname(__file__), '..', 'plugin',
                           'megaraid_plugin', 'utils.py')
_spec = importlib.util.spec_from_file_location("megaraid_utils", _utils_path)
_mod = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mod)
select_disk_model = _mod.select_disk_model
ExecError = _mod.ExecError


class _LsmError(Exception):

    def __init__(self, code, msg=''):
        Exception.__init__(self, msg)
        self.code = code


class _ErrorNumber(object):
    PLUGIN_BUG = 2
    NO_SUPPORT = 153
    INVALID_ARGUMENT = 101


def _load_megaraid():
    """Import megaraid.py against a stand-in 'lsm' (no C extension)."""
    lsm = types.ModuleType('lsm')
    lsm.LsmError = _LsmError
    lsm.ErrorNumber = _ErrorNumber
    lsm.IPlugin = object
    lsm.VERSION = '0'
    for name in ('uri_parse', 'search_property', 'size_human_2_size_bytes',
                 'Capabilities', 'System', 'Client', 'Disk', 'Pool', 'Volume',
                 'Battery', 'int_div'):
        setattr(lsm, name, mock.MagicMock())
    pkg = types.ModuleType('megaraid_plugin')
    pkg.__path__ = []
    stubs = {'lsm': lsm, 'megaraid_plugin': pkg, 'megaraid_plugin.utils': _mod}
    path = os.path.join(os.path.dirname(__file__), '..', 'plugin',
                        'megaraid_plugin', 'megaraid.py')
    with mock.patch.dict(sys.modules, stubs):
        spec = importlib.util.spec_from_file_location('megaraid_test', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    return module


_megaraid = _load_megaraid()


def _storcli_failure(description, err_msg=None):
    """storcli JSON failure reply, with a Detailed Status if err_msg set."""
    status = {'Status': 'Failure', 'Description': description}
    if err_msg is not None:
        status['Detailed Status'] = [{'ErrMsg': err_msg, 'ErrCd': 255}]
    return json.dumps({'Controllers': [{'Command Status': status}]})


class TestDiskModelSelection(unittest.TestCase):
    """Test the disk model fallback logic used in MegaRAID disk discovery."""

    def test_model_number_populated(self):
        attr = {'Model Number': 'Samsung SSD 990', 'Model': 'old-model'}
        self.assertEqual(select_disk_model(attr), 'Samsung SSD 990')

    def test_model_number_empty_falls_back_to_model(self):
        attr = {'Model Number': '', 'Model': 'Seagate ST8000'}
        self.assertEqual(select_disk_model(attr), 'Seagate ST8000')

    def test_model_number_none_falls_back_to_model(self):
        attr = {'Model Number': None, 'Model': 'WDC WD4003'}
        self.assertEqual(select_disk_model(attr), 'WDC WD4003')

    def test_model_number_absent_falls_back_to_model(self):
        attr = {'Model': 'HGST HUH72'}
        self.assertEqual(select_disk_model(attr), 'HGST HUH72')

    def test_both_absent(self):
        attr = {}
        self.assertEqual(select_disk_model(attr), '')

    def test_model_number_only(self):
        attr = {'Model Number': 'Intel SSDPE2'}
        self.assertEqual(select_disk_model(attr), 'Intel SSDPE2')


class TestStorcliFailureHandling(unittest.TestCase):
    """Classification of storcli failures, whatever the exit code."""

    # (name, reply) pairs storcli uses for a device the controller lacks
    ABSENT = [
        ('bbu on capacitor controller', _storcli_failure('None',
                                                         'use /cx/cv')),
        ('redirect in description only', _storcli_failure('use /cx/cv')),
        ('HBA-mode unsupported', _storcli_failure('Un-supported Command')),
        ('HBA-mode unsupported detail',
         _storcli_failure('None', 'Un-supported command')),
    ]

    # Failures callers must still see as ExecError on non-zero exit
    OTHER = [
        ('internal error', _storcli_failure('None', 'internal error')),
        ('pool not found', _storcli_failure('None', 'Not found')),
        ('non-JSON output', ''),
    ]

    def setUp(self):
        self.mega = _megaraid.MegaRAID()
        self.mega._storcli_bin = 'storcli64'

    def _run(self, func, **cmd_exec):
        with mock.patch.object(_megaraid, 'cmd_exec', **cmd_exec):
            return func(['/c0/bbu', 'show', 'all'])

    def _exit(self, reply, errno):
        if errno:
            return {'side_effect': ExecError('storcli', errno, reply, '')}
        return {'return_value': reply}

    def test_absent_device(self):
        for name, reply in self.ABSENT:
            for errno in (0, 255):
                with self.subTest(name=name, errno=errno):
                    exit_ = self._exit(reply, errno)
                    self.assertIsNone(
                        self._run(self.mega._storcli_exec_optional, **exit_))
                    with self.assertRaises(_LsmError) as caught:
                        self._run(self.mega._storcli_exec, **exit_)
                    self.assertEqual(caught.exception.code,
                                     _ErrorNumber.NO_SUPPORT)

    def test_other_failure_nonzero_exit_stays_exec_error(self):
        for name, reply in self.OTHER:
            with self.subTest(name=name):
                with self.assertRaises(ExecError):
                    self._run(self.mega._storcli_exec_optional,
                              **self._exit(reply, 255))

    def test_other_failure_zero_exit_is_plugin_bug(self):
        for name, reply in self.OTHER[:2] + [
            ('description only', _storcli_failure('boom'))
        ]:
            with self.subTest(name=name):
                with self.assertRaises(_LsmError) as caught:
                    self._run(self.mega._storcli_exec_optional,
                              **self._exit(reply, 0))
                self.assertEqual(caught.exception.code,
                                 _ErrorNumber.PLUGIN_BUG)


if __name__ == '__main__':
    unittest.main()
