## Copyright (C) 2014 Red Hat, Inc.
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; If not, see <http://www.gnu.org/licenses/>.
#
# Author: Gris Ge <fge@redhat.com>

from utils import merge_list
import dmtf
from lsm import System, LsmError, ErrorNumber


def cim_sys_id_pros():
    """
    Return the property of CIM_ComputerSystem required to generate
    lsm.System.id
    """
    return ['Name']


def sys_id_of_cim_sys(cim_sys):
    if 'Name' in cim_sys:
        return cim_sys['Name']
    else:
        raise LsmError(
            ErrorNumber.PLUGIN_BUG,
            "sys_id_of_cim_sys(): Got a CIM_ComputerSystem does not have "
            "'Name' property: %s, %s" % (cim_sys.items(), cim_sys.path))


def sys_id_of_cim_vol(cim_vol):
    if 'SystemName' in cim_vol:
        return cim_vol['SystemName']
    else:
        raise LsmError(
            ErrorNumber.PLUGIN_BUG,
            "sys_id_of_cim_vol(): Got a CIM_StorageVolume does not have "
            "'SystemName' property: %s, %s" % (cim_vol.items(), cim_vol.path))


def root_cim_sys(smis_common, property_list=None):
    """
    Use this association to find out the root CIM_ComputerSystem:
        CIM_RegisteredProfile       # Root Profile('Array') in interop
                 |
                 | CIM_ElementConformsToProfile
                 v
        CIM_ComputerSystem          # vendor namespace
    """
    id_pros = cim_sys_id_pros()
    if property_list is None:
        property_list = id_pros
    else:
        property_list = merge_list(property_list, id_pros)

    cim_syss = []
    if smis_common.is_megaraid():
        cim_syss = smis_common.EnumerateInstances(
            'CIM_ComputerSystem', PropertyList=property_list)
    else:
        cim_syss = smis_common.Associators(
            smis_common.root_blk_cim_rp.path,
            ResultClass='CIM_ComputerSystem',
            AssocClass='CIM_ElementConformsToProfile',
            PropertyList=property_list)

        if len(cim_syss) == 0:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "Current SMI-S provider does not provide "
                           "the root CIM_ComputerSystem associated "
                           "to 'Array' CIM_RegisteredProfile.")

    # System URI Filtering
    if smis_common.system_list:
        needed_cim_syss = []
        for cim_sys in cim_syss:
            if sys_id_of_cim_sys(cim_sys) in smis_common.system_list:
                needed_cim_syss.extend([cim_sys])
        return needed_cim_syss
    else:
        return cim_syss


def cim_sys_pros():
    """
    Return a list of properties required to create a LSM System
    """
    cim_sys_properties = cim_sys_id_pros()
    cim_sys_properties.extend(['ElementName', 'OperationalStatus'])
    return cim_sys_properties


_LSM_SYS_OP_STATUS_CONV = {
    dmtf.OP_STATUS_UNKNOWN: System.STATUS_UNKNOWN,
    dmtf.OP_STATUS_OK: System.STATUS_OK,
    dmtf.OP_STATUS_ERROR: System.STATUS_ERROR,
    dmtf.OP_STATUS_DEGRADED: System.STATUS_DEGRADED,
    dmtf.OP_STATUS_NON_RECOVERABLE_ERROR: System.STATUS_ERROR,
    dmtf.OP_STATUS_PREDICTIVE_FAILURE: System.STATUS_PREDICTIVE_FAILURE,
    dmtf.OP_STATUS_SUPPORTING_ENTITY_IN_ERROR: System.STATUS_ERROR,
}


def _sys_status_of_cim_sys(cim_sys):
    """
    Convert CIM_ComputerSystem['OperationalStatus']
    """
    if 'OperationalStatus' not in cim_sys:
        raise LsmError(
            ErrorNumber.PLUGIN_BUG,
            "sys_status_of_cim_sys(): Got a CIM_ComputerSystem with no "
            "OperationalStatus: %s, %s" % (cim_sys.items(), cim_sys.path))

    return dmtf.op_status_list_conv(
        _LSM_SYS_OP_STATUS_CONV, cim_sys['OperationalStatus'],
        System.STATUS_UNKNOWN, System.STATUS_OTHER)


def cim_sys_to_lsm_sys(cim_sys):
    status = System.STATUS_UNKNOWN
    status_info = ''

    if 'OperationalStatus' in cim_sys:
        (status, status_info) = _sys_status_of_cim_sys(cim_sys)

    sys_id = sys_id_of_cim_sys(cim_sys)
    sys_name = cim_sys['ElementName']

    return System(sys_id, sys_name, status, status_info)


def cim_sys_of_sys_id(smis_common, sys_id, property_list=None):
    """
    Find out the CIM_ComputerSystem for given lsm.System.id using
    root_cim_sys()
    """
    id_pros = cim_sys_id_pros()
    if property_list is None:
        property_list = id_pros
    else:
        property_list = merge_list(property_list, id_pros)

    cim_syss = root_cim_sys(smis_common, property_list)
    for cim_sys in cim_syss:
        if sys_id_of_cim_sys(cim_sys) == sys_id:
            return cim_sys
    raise LsmError(
        ErrorNumber.NOT_FOUND_SYSTEM,
        "Not found System")
