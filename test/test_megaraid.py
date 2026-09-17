import importlib.util
import os
import unittest

# Import utils directly to avoid megaraid_plugin.__init__ pulling in lsm (C extension).
_utils_path = os.path.join(os.path.dirname(__file__), '..', 'plugin',
                           'megaraid_plugin', 'utils.py')
_spec = importlib.util.spec_from_file_location("megaraid_utils", _utils_path)
_mod = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mod)
select_disk_model = _mod.select_disk_model


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


if __name__ == '__main__':
    unittest.main()
