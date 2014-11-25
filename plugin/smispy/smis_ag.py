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
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#
# Author: Gris Ge <fge@redhat.com>

"""
This module intend to provide independent methods for lsm.AccessGroup and
volume masking/unmasking.
"""

from pywbem import CIMError, CIM_ERR_NOT_FOUND

from lsm import AccessGroup, md5, LsmError, ErrorNumber

from lsm.plugin.smispy.smis_common import SmisCommon
from lsm.plugin.smispy import dmtf
from lsm.plugin.smispy.utils import cim_path_to_path_str, path_str_to_cim_path

_CIM_INIT_PROS = ['StorageID', 'IDType']


def _init_id_and_type_of(cim_inits):
    """
    Retrieve AccessGroup.init_ids and AccessGroup.init_type from
    a list of CIM_StorageHardwareID.
    """
    init_ids = []
    init_type = AccessGroup.INIT_TYPE_UNKNOWN
    init_types = []
    for cim_init in cim_inits:
        if cim_init['IDType'] == dmtf.ID_TYPE_WWPN:
            init_ids.append(init_id_of_cim_init(cim_init))
            init_types.append(AccessGroup.INIT_TYPE_WWPN)
        if cim_init['IDType'] == dmtf.ID_TYPE_ISCSI:
            init_ids.append(init_id_of_cim_init(cim_init))
            init_types.append(AccessGroup.INIT_TYPE_ISCSI_IQN)
        # Skip if not a iscsi initiator IQN or WWPN.
        continue

    init_type_dict = {}
    for cur_init_type in init_types:
        init_type_dict[cur_init_type] = 1

    if len(init_type_dict) == 1:
        init_type = init_types[0]
    elif len(init_type_dict) == 2:
        init_type = AccessGroup.INIT_TYPE_ISCSI_WWPN_MIXED
    return (init_ids, init_type)


def cim_spc_pros():
    """
    Return the property of CIM_SCSIProtocolController required to generate
    lsm.AccessGroup
    'EMCAdapterRole' is for EMC VNX only.
    """
    return ['DeviceID', 'ElementName', 'StorageID', 'EMCAdapterRole',
            'SystemName']


def cim_init_mg_pros():
    """
    Return the property of CIM_InitiatorMaskingGroup required to generate
    lsm.AccessGroup
    """
    return ['ElementName', 'InstanceID']


def cim_init_of_cim_spc_path(smis_common, cim_spc_path):
    """
    Return a list of CIM_StorageHardwareID associated to cim_spc.
    Only contain ['StorageID', 'IDType'] property.
    Two ways to get StorageHardwareID from SCSIProtocolController:
     * Method A (defined in SNIA SMIS 1.6):
            CIM_SCSIProtocolController
                     |
                     | CIM_AssociatedPrivilege
                     v
            CIM_StorageHardwareID

     * Method B (defined in SNIA SMIS 1.3, 1.4, 1.5 and 1.6):
            CIM_SCSIProtocolController
                    |
                    | CIM_AuthorizedTarget
                    v
            CIM_AuthorizedPrivilege
                    |
                    | CIM_AuthorizedSubject
                    v
            CIM_StorageHardwareID
    """
    cim_inits = []
    if smis_common.profile_check(SmisCommon.SNIA_MASK_PROFILE,
                                 SmisCommon.SMIS_SPEC_VER_1_6,
                                 raise_error=False):
        try:
            cim_inits = smis_common.Associators(
                cim_spc_path,
                AssocClass='CIM_AssociatedPrivilege',
                ResultClass='CIM_StorageHardwareID',
                PropertyList=_CIM_INIT_PROS)
        except CIMError as cim_error:
            if cim_error[0] == CIM_ERR_NOT_FOUND:
                pass
            else:
                raise

    if len(cim_inits) == 0:
        cim_aps_path = smis_common.AssociatorNames(
            cim_spc_path,
            AssocClass='CIM_AuthorizedTarget',
            ResultClass='CIM_AuthorizedPrivilege')

        for cim_ap_path in cim_aps_path:
            cim_inits.extend(smis_common.Associators(
                cim_ap_path,
                AssocClass='CIM_AuthorizedSubject',
                ResultClass='CIM_StorageHardwareID',
                PropertyList=_CIM_INIT_PROS))
    return cim_inits


def cim_spc_to_lsm_ag(smis_common, cim_spc, system_id):
    """
    Convert CIM_SCSIProtocolController to lsm.AccessGroup
    """
    ag_id = md5(cim_spc['DeviceID'])
    ag_name = cim_spc['ElementName']
    cim_inits = cim_init_of_cim_spc_path(smis_common, cim_spc.path)
    (init_ids, init_type) = _init_id_and_type_of(cim_inits)
    plugin_data = cim_path_to_path_str(cim_spc.path)
    return AccessGroup(
        ag_id, ag_name, init_ids, init_type, system_id, plugin_data)


def cim_init_of_cim_init_mg_path(smis_common, cim_init_mg_path):
    """
    Use this association to get a list of CIM_StorageHardwareID:
        CIM_InitiatorMaskingGroup
                |
                | CIM_MemberOfCollection
                v
        CIM_StorageHardwareID
    Only contain ['StorageID', 'IDType'] property.
    """
    return smis_common.Associators(
        cim_init_mg_path,
        AssocClass='CIM_MemberOfCollection',
        ResultClass='CIM_StorageHardwareID',
        PropertyList=_CIM_INIT_PROS)


def cim_init_mg_to_lsm_ag(smis_common, cim_init_mg, system_id):
    """
    Convert CIM_InitiatorMaskingGroup to lsm.AccessGroup
    """
    ag_name = cim_init_mg['ElementName']
    ag_id = md5(cim_init_mg['InstanceID'])
    cim_inits = cim_init_of_cim_init_mg_path(smis_common, cim_init_mg.path)
    (init_ids, init_type) = _init_id_and_type_of(cim_inits)
    plugin_data = cim_path_to_path_str(cim_init_mg.path)
    return AccessGroup(
        ag_id, ag_name, init_ids, init_type, system_id, plugin_data)


def lsm_ag_to_cim_spc_path(smis_common, lsm_ag):
    """
    Convert lsm.AccessGroup to CIMInstanceName of CIM_SCSIProtocolController
    using lsm.AccessGroup.plugin_data.
    This method does not check whether plugin_data is cim_spc or cim_init_mg,
    caller should make sure that.
    """
    if not lsm_ag.plugin_data:
        raise LsmError(
            ErrorNumber.PLUGIN_BUG,
            "Got lsm.AccessGroup instance with empty plugin_data")
    if smis_common.system_list and \
       lsm_ag.system_id not in smis_common.system_list:
        raise LsmError(
            ErrorNumber.NOT_FOUND_SYSTEM,
            "System filtered in URI")

    return path_str_to_cim_path(lsm_ag.plugin_data)


def lsm_ag_to_cim_init_mg_path(smis_common, lsm_ag):
    """
    Convert lsm.AccessGroup to CIMInstanceName of CIM_InitiatorMaskingGroup
    using lsm.AccessGroup.plugin_data.
    This method does not check whether plugin_data is cim_spc or cim_init_mg,
    caller should make sure that.
    """
    return lsm_ag_to_cim_spc_path(smis_common, lsm_ag)


def init_id_of_cim_init(cim_init):
    """
    Return CIM_StorageHardwareID['StorageID']
    """
    if 'StorageID' in cim_init:
        return cim_init['StorageID']
    raise LsmError(
        ErrorNumber.PLUGIN_BUG,
        "init_id_of_cim_init() got cim_init without 'StorageID' %s: %s" %
        (cim_init.path, cim_init.items()))


def lsm_init_id_to_snia(lsm_init_id):
    """
    If lsm_init_id is a WWPN, convert it to SNIA format:
        [0-9A-F]{16}
    If not, return original directly.
    """
    val, init_type, init_id = AccessGroup.initiator_id_verify(lsm_init_id)
    if val and init_type == AccessGroup.INIT_TYPE_WWPN:
        return lsm_init_id.replace(':', '').upper()
    return lsm_init_id


def cim_init_path_check_or_create(smis_common, system_id, init_id, init_type):
    """
    Check whether CIM_StorageHardwareID exists, if not, create new one.
    """
    cim_inits = smis_common.EnumerateInstances(
        'CIM_StorageHardwareID',
        PropertyList=_CIM_INIT_PROS)

    if len(cim_inits):
        for cim_init in cim_inits:
            if init_id_of_cim_init(cim_init) == init_id:
                return cim_init.path

    # Create new one
    dmtf_id_type = None
    if init_type == AccessGroup.INIT_TYPE_WWPN:
        dmtf_id_type = dmtf.ID_TYPE_WWPN
    elif init_type == AccessGroup.INIT_TYPE_ISCSI_IQN:
        dmtf_id_type = dmtf.ID_TYPE_ISCSI
    else:
        raise LsmError(
            ErrorNumber.PLUGIN_BUG,
            "cim_init_path_check_or_create(): Got invalid init_type: %d" %
            init_type)

    cim_hwms = smis_common.cim_hwms_of_sys_id(system_id)
    in_params = {
        'StorageID': init_id,
        'IDType': dmtf_id_type,
    }
    return smis_common.invoke_method_wait(
        'CreateStorageHardwareID', cim_hwms.path, in_params,
        out_key='HardwareID', expect_class='CIM_StorageHardwareID')


def cim_vols_masked_to_cim_spc_path(smis_common, cim_spc_path,
                                   property_list=None):
    """
    Use this association to find out masked volume for certain cim_spc:
        CIM_SCSIProtocolController
                |
                |   CIM_ProtocolControllerForUnit
                v
        CIM_StorageVolume
    Return a list of CIMInstance
    """
    if property_list is None:
        property_list = []

    return smis_common.Associators(
        cim_spc_path,
        AssocClass='CIM_ProtocolControllerForUnit',
        ResultClass='CIM_StorageVolume',
        PropertyList=property_list)
