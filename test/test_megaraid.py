import importlib.util
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


def _load_megaraid():
    """
    Import megaraid.py with a stand-in 'lsm', so the storcli handling can be
    exercised without the compiled C extension.
    """

    class LsmError(Exception):

        def __init__(self, code, msg='', *args):
            Exception.__init__(self, msg)
            self.code = code
            self.msg = msg

    class ErrorNumber(object):
        PLUGIN_BUG = 2
        NO_SUPPORT = 10
        INVALID_ARGUMENT = 101
        NOT_FOUND_SYSTEM = 203

    lsm = types.ModuleType('lsm')
    lsm.LsmError = LsmError
    lsm.ErrorNumber = ErrorNumber
    lsm.IPlugin = type('IPlugin', (object,), {})
    lsm.VERSION = '0'
    for name in ('uri_parse', 'search_property', 'size_human_2_size_bytes',
                 'Capabilities', 'System', 'Client', 'Disk', 'Pool', 'Volume',
                 'Battery', 'int_div'):
        setattr(lsm, name, mock.MagicMock())

    pkg = types.ModuleType('megaraid_plugin')
    pkg.__path__ = []

    saved = {k: sys.modules.get(k) for k in
             ('lsm', 'megaraid_plugin', 'megaraid_plugin.utils')}
    sys.modules['lsm'] = lsm
    sys.modules['megaraid_plugin'] = pkg
    sys.modules['megaraid_plugin.utils'] = _mod
    try:
        path = os.path.join(os.path.dirname(__file__), '..', 'plugin',
                            'megaraid_plugin', 'megaraid.py')
        spec = importlib.util.spec_from_file_location('megaraid_under_test',
                                                      path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    finally:
        for key, value in saved.items():
            if value is None:
                sys.modules.pop(key, None)
            else:
                sys.modules[key] = value
    return module, LsmError, ErrorNumber


_megaraid, _LsmError, _ErrorNumber = _load_megaraid()


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


def _storcli_failure(err_msg, description='None'):
    """A storcli JSON failure reply carrying a 'Detailed Status'."""
    return ('{"Controllers":[{"Command Status":{'
            '"Controller":0,"Status":"Failure","Description":"%s",'
            '"Detailed Status":[{"Ctrl":0,"Status":"Failed","Property":"-",'
            '"ErrMsg":"%s","ErrCd":255}]}}]}' % (description, err_msg))


def _storcli_no_detail(description):
    """A storcli JSON failure reply with only a 'Description'."""
    return ('{"Controllers":[{"Command Status":{'
            '"Controller":0,"Status":"Failure","Description":"%s"}}]}' %
            description)


class TestStorcliFailureHandling(unittest.TestCase):
    """
    storcli exits non-zero for some failures, so the reply has to be read
    from the ExecError rather than the exit code being taken at face value.
    """

    def setUp(self):
        self.mega = _megaraid.MegaRAID()
        self.mega._storcli_bin = '/nonexistent/storcli64'

    def _run(self, optional, stdout, errno=255):
        err = ExecError('storcli', errno, stdout, '')
        with mock.patch.object(_megaraid, 'cmd_exec', side_effect=err):
            call = (self.mega._storcli_exec_optional if optional
                    else self.mega._storcli_exec)
            return call(["/c0/bbu", "show", "all"])

    def test_bbu_on_capacitor_controller_is_optional(self):
        # A controller with a capacitor answers /cx/bbu with 'use /cx/cv'.
        # There is no battery to report, so the caller gets nothing back
        # rather than an error.
        self.assertIsNone(self._run(True, _storcli_failure('use /cx/cv')))

    def test_bbu_redirect_is_no_support(self):
        with self.assertRaises(_LsmError) as caught:
            self._run(False, _storcli_failure('use /cx/cv'))
        self.assertEqual(caught.exception.code, _ErrorNumber.NO_SUPPORT)

    def test_unsupported_command_is_optional(self):
        # HBA-mode controllers reject RAID queries outright.
        self.assertIsNone(
            self._run(True, _storcli_failure('Un-supported Command',
                                             description='Un-supported '
                                                         'Command')))

    def test_description_only_redirect_is_optional(self):
        # The same redirect, but reported without a 'Detailed Status'.
        self.assertIsNone(
            self._run(True, _storcli_no_detail('use /cx/cv')))

    def test_unrelated_failure_stays_an_exec_error(self):
        # disks(), pool_member_info() and volume_raid_create() all catch
        # ExecError to retry or to give a better error than storcli did.
        # Only an absent device may be turned into an LsmError, or those
        # handlers stop running.
        with self.assertRaises(ExecError):
            self._run(True, _storcli_failure('internal error'))

    def test_pool_not_found_stays_an_exec_error(self):
        # pool_member_info() reads this reply to answer NOT_FOUND_POOL.
        reply = ('{"Controllers":[{"Command Status":{"Controller":0,'
                 '"Status":"Failure","Description":"None",'
                 '"Detailed Status":[{"Status":"Not found",'
                 '"ErrMsg":"Not found","ErrCd":1}]}}]}')
        with self.assertRaises(ExecError) as caught:
            self._run(True, reply)
        self.assertIn('Not found', caught.exception.stdout)

    def test_non_json_failure_still_raises(self):
        # Nothing to classify, so the original error has to surface.
        with self.assertRaises(ExecError):
            self._run(True, '')

    def test_zero_exit_failure_is_a_plugin_bug(self):
        # storcli can report a failure while exiting zero; that path is
        # unchanged and still has nothing to do with ExecError.
        with mock.patch.object(_megaraid, 'cmd_exec',
                               return_value=_storcli_failure('internal '
                                                             'error')):
            with self.assertRaises(_LsmError) as caught:
                self.mega._storcli_exec(["/c0/bbu", "show", "all"])
        self.assertEqual(caught.exception.code, _ErrorNumber.PLUGIN_BUG)


if __name__ == '__main__':
    unittest.main()
