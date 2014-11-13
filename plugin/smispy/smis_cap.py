# Copyright (C) 2014 Red Hat, Inc.
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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
# USA

from lsm import Capabilities, LsmError, ErrorNumber
import dmtf
from smis_common import SmisCommon

MASK_TYPE_NO_SUPPORT = 0
MASK_TYPE_MASK = 1
MASK_TYPE_GROUP = 2


def _rs_supported_capabilities(smis_common, system_id, cap):
    """
    Interrogate the supported features of the replication service
    """
    cim_rs = smis_common.cim_rs_of_sys_id(system_id, raise_error=False)
    if cim_rs:
        rs_cap = smis_common.Associators(
            cim_rs.path,
            AssocClass='CIM_ElementCapabilities',
            ResultClass='CIM_ReplicationServiceCapabilities',
            PropertyList=['SupportedReplicationTypes',
                          'SupportedAsynchronousActions',
                          'SupportedSynchronousActions'])[0]

        s_rt = rs_cap['SupportedReplicationTypes']
        async_actions = rs_cap['SupportedAsynchronousActions']
        sync_actions = rs_cap['SupportedSynchronousActions']

        if dmtf.REPLICA_CAP_ACTION_CREATE_ELEMENT in async_actions or \
           dmtf.REPLICA_CAP_ACTION_CREATE_ELEMENT in sync_actions:
            cap.set(Capabilities.VOLUME_REPLICATE)
        else:
            return

        if dmtf.REPLICA_CAP_TYPE_SYNC_SNAPSHOT_LOCAL in s_rt or \
           dmtf.REPLICA_CAP_TYPE_ASYNC_SNAPSHOT_LOCAL in s_rt:
            cap.set(Capabilities.VOLUME_REPLICATE_CLONE)

        if dmtf.REPLICA_CAP_TYPE_SYNC_CLONE_LOCAL in s_rt or \
           dmtf.REPLICA_CAP_TYPE_ASYNC_CLONE_LOCAL in s_rt:
            cap.set(Capabilities.VOLUME_REPLICATE_COPY)
    else:
        # Try older storage configuration service

        cim_scs = smis_common.cim_scs_of_sys_id(system_id, raise_error=False)

        if cim_scs:
            cim_sc_cap = smis_common.Associators(
                cim_scs.path,
                AssocClass='CIM_ElementCapabilities',
                ResultClass='CIM_StorageConfigurationCapabilities',
                PropertyList=['SupportedCopyTypes'])[0]

            if cim_sc_cap is not None and 'SupportedCopyTypes' in cim_sc_cap:
                sct = cim_sc_cap['SupportedCopyTypes']

                if sct and len(sct):
                    cap.set(Capabilities.VOLUME_REPLICATE)

                    if dmtf.ST_CONF_CAP_COPY_TYPE_UNSYNC_ASSOC in sct:
                        cap.set(Capabilities.VOLUME_REPLICATE_CLONE)

                    if dmtf.ST_CONF_CAP_COPY_TYPE_UNSYNC_UNASSOC in sct:
                        cap.set(Capabilities.VOLUME_REPLICATE_COPY)


def _bsp_cap_set(smis_common, system_id, cap):
    """
    Set capabilities for these methods:
        volumes()
        volume_create()
        volume_resize()
        volume_delete()
    """
    # CIM_StorageConfigurationService is optional.
    cim_scs = smis_common.cim_scs_of_sys_id(system_id, raise_error=False)

    if cim_scs is None:
        return

    # These methods are mandatory for CIM_StorageConfigurationService:
    #   CreateOrModifyElementFromStoragePool()
    #   ReturnToStoragePool()
    # But SNIA never defined which function of
    # CreateOrModifyElementFromStoragePool() is mandatory.
    # Hence we check CIM_StorageConfigurationCapabilities
    # which is mandatory if CIM_StorageConfigurationService is supported.
    cim_scs_cap = smis_common.Associators(
        cim_scs.path,
        AssocClass='CIM_ElementCapabilities',
        ResultClass='CIM_StorageConfigurationCapabilities',
        PropertyList=['SupportedAsynchronousActions',
                      'SupportedSynchronousActions',
                      'SupportedStorageElementTypes'])[0]

    element_types = cim_scs_cap['SupportedStorageElementTypes']
    sup_actions = []

    if 'SupportedSynchronousActions' in cim_scs_cap:
        if cim_scs_cap['SupportedSynchronousActions']:
            sup_actions.extend(cim_scs_cap['SupportedSynchronousActions'])

    if 'SupportedAsynchronousActions' in cim_scs_cap:
        if cim_scs_cap['SupportedAsynchronousActions']:
            sup_actions.extend(cim_scs_cap['SupportedAsynchronousActions'])

    if dmtf.SCS_CAP_SUP_ST_VOLUME in element_types or \
       dmtf.SCS_CAP_SUP_THIN_ST_VOLUME in element_types:
        cap.set(Capabilities.VOLUMES)
        if dmtf.SCS_CAP_SUP_THIN_ST_VOLUME in element_types:
            cap.set(Capabilities.VOLUME_THIN)

    if dmtf.SCS_CAP_VOLUME_CREATE in sup_actions:
        cap.set(Capabilities.VOLUME_CREATE)

    if dmtf.SCS_CAP_VOLUME_DELETE in sup_actions:
        cap.set(Capabilities.VOLUME_DELETE)

    if dmtf.SCS_CAP_VOLUME_MODIFY in sup_actions:
        cap.set(Capabilities.VOLUME_RESIZE)

    return


def _disk_cap_set(smis_common, cim_sys_path, cap):
    if not smis_common.profile_check(SmisCommon.SNIA_DISK_LITE_PROFILE,
                                     SmisCommon.SMIS_SPEC_VER_1_4,
                                     raise_error=False):
        return

    cap.set(Capabilities.DISKS)
    return


def _group_mask_map_cap_set(smis_common, cim_sys_path, cap):
    """
    We set caps for these methods recording to 1.5+ Group M&M profile:
        access_groups()
        access_groups_granted_to_volume()
        volumes_accessible_by_access_group()
        access_group_initiator_add()
        access_group_initiator_delete()
        volume_mask()
        volume_unmask()
        access_group_create()
        access_group_delete()
    """
    # These are mandatory in SNIA SMI-S.
    # We are not in the position of SNIA SMI-S certification.
    cap.set(Capabilities.ACCESS_GROUPS)
    cap.set(Capabilities.ACCESS_GROUPS_GRANTED_TO_VOLUME)
    cap.set(Capabilities.VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP)
    cap.set(Capabilities.VOLUME_MASK)
    if fc_tgt_is_supported(smis_common):
        cap.set(Capabilities.ACCESS_GROUP_INITIATOR_ADD_WWPN)
        cap.set(Capabilities.ACCESS_GROUP_CREATE_WWPN)
    if iscsi_tgt_is_supported(smis_common):
        cap.set(Capabilities.ACCESS_GROUP_INITIATOR_ADD_ISCSI_IQN)
        cap.set(Capabilities.ACCESS_GROUP_CREATE_ISCSI_IQN)

    # RemoveMembers is also mandatory
    cap.set(Capabilities.ACCESS_GROUP_INITIATOR_DELETE)

    cim_gmm_cap_pros = [
        'SupportedAsynchronousActions',
        'SupportedSynchronousActions',
        'SupportedDeviceGroupFeatures']

    cim_gmm_cap = smis_common.Associators(
        cim_sys_path,
        AssocClass='CIM_ElementCapabilities',
        ResultClass='CIM_GroupMaskingMappingCapabilities',
        PropertyList=cim_gmm_cap_pros)[0]

    # if empty dev group in spc is allowed, RemoveMembers() is enough
    # to do volume_unmask(). RemoveMembers() is mandatory.
    if dmtf.GMM_CAP_DEV_MG_ALLOW_EMPTY_W_SPC in \
       cim_gmm_cap['SupportedDeviceGroupFeatures']:
        cap.set(Capabilities.VOLUME_UNMASK)

    # DeleteMaskingView() is optional, this is required by volume_unmask()
    # when empty dev group in spc not allowed.
    elif ((dmtf.GMM_CAP_DELETE_SPC in
           cim_gmm_cap['SupportedSynchronousActions']) or
          (dmtf.GMM_CAP_DELETE_SPC in
           cim_gmm_cap['SupportedAsynchronousActions'])):
        cap.set(Capabilities.VOLUME_UNMASK)

    # DeleteGroup is optional, this is required by access_group_delete()
    if ((dmtf.GMM_CAP_DELETE_GROUP in
         cim_gmm_cap['SupportedSynchronousActions']) or
        (dmtf.GMM_CAP_DELETE_GROUP in
         cim_gmm_cap['SupportedAsynchronousActions'])):
        cap.set(Capabilities.ACCESS_GROUP_DELETE)
    return None


def _mask_map_cap_set(smis_common, cim_sys_path, cap):
    """
    In SNIA SMI-S 1.4rev6 'Masking and Mapping' profile:
    CIM_ControllerConfigurationService is mandatory
    and it's ExposePaths() and HidePaths() are mandatory
    """
    if not smis_common.profile_check(SmisCommon.SNIA_MASK_PROFILE,
                                     SmisCommon.SMIS_SPEC_VER_1_4,
                                     raise_error=False):
        return

    cap.set(Capabilities.ACCESS_GROUPS)
    cap.set(Capabilities.VOLUME_MASK)
    cap.set(Capabilities.VOLUME_UNMASK)
    cap.set(Capabilities.ACCESS_GROUP_INITIATOR_DELETE)
    cap.set(Capabilities.ACCESS_GROUPS_GRANTED_TO_VOLUME)
    cap.set(Capabilities.VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP)

    # EMC VNX does not support CreateStorageHardwareID for iSCSI
    # and require WWNN for WWPN. Hence both are not supported.
    if cim_sys_path.classname == 'Clar_StorageSystem':
        return

    if fc_tgt_is_supported(smis_common):
        cap.set(Capabilities.ACCESS_GROUP_INITIATOR_ADD_WWPN)
    if iscsi_tgt_is_supported(smis_common):
        cap.set(Capabilities.ACCESS_GROUP_INITIATOR_ADD_ISCSI_IQN)
    return


def _tgt_cap_set(smis_common, cim_sys_path, cap):

    # LSI MegaRAID actually not support FC Target and iSCSI target,
    # They expose empty list of CIM_FCPort
    if cim_sys_path.classname == 'LSIESG_MegaRAIDHBA':
        return

    flag_fc_support = smis_common.profile_check(
        SmisCommon.SNIA_FC_TGT_PORT_PROFILE,
        SmisCommon.SMIS_SPEC_VER_1_4,
        raise_error=False)
    # One more check for NetApp Typo:
    #   NetApp:     'FC Target Port'
    #   SMI-S:      'FC Target Ports'
    # Bug reported.
    if not flag_fc_support:
        flag_fc_support = smis_common.profile_check(
            'FC Target Port',
            SmisCommon.SMIS_SPEC_VER_1_4,
            raise_error=False)
    flag_iscsi_support = smis_common.profile_check(
        SmisCommon.SNIA_ISCSI_TGT_PORT_PROFILE,
        SmisCommon.SMIS_SPEC_VER_1_4,
        raise_error=False)

    if flag_fc_support or flag_iscsi_support:
        cap.set(Capabilities.TARGET_PORTS)
    return


def mask_type(smis_common, raise_error=False):
    """
    Return MASK_TYPE_NO_SUPPORT, MASK_TYPE_MASK or MASK_TYPE_GROUP
    if 'Group Masking and Mapping' profile is supported, return
    MASK_TYPE_GROUP

    If raise_error == False, just return MASK_TYPE_NO_SUPPORT
    or, raise NO_SUPPORT error.
    """
    if smis_common.profile_check(SmisCommon.SNIA_GROUP_MASK_PROFILE,
                                 SmisCommon.SMIS_SPEC_VER_1_5,
                                 raise_error=False):
        return MASK_TYPE_GROUP
    if smis_common.profile_check(SmisCommon.SNIA_MASK_PROFILE,
                                 SmisCommon.SMIS_SPEC_VER_1_4,
                                 raise_error=False):
        return MASK_TYPE_MASK
    if raise_error:
        raise LsmError(ErrorNumber.NO_SUPPORT,
                       "Target SMI-S provider does not support "
                       "%s version %s or %s version %s" %
                       (SmisCommon.SNIA_MASK_PROFILE,
                        SmisCommon.SMIS_SPEC_VER_1_4,
                        SmisCommon.SNIA_GROUP_MASK_PROFILE,
                        SmisCommon.SMIS_SPEC_VER_1_5))
    return MASK_TYPE_NO_SUPPORT


def fc_tgt_is_supported(smis_common):
    """
    Return True if FC Target Port 1.4+ profile is supported.
    """
    flag_fc_support = smis_common.profile_check(
        SmisCommon.SNIA_FC_TGT_PORT_PROFILE,
        SmisCommon.SMIS_SPEC_VER_1_4,
        raise_error=False)
    # One more check for NetApp Typo:
    #   NetApp:     'FC Target Port'
    #   SMI-S:      'FC Target Ports'
    # Bug reported.
    if not flag_fc_support:
        flag_fc_support = smis_common.profile_check(
            'FC Target Port',
            SmisCommon.SMIS_SPEC_VER_1_4,
            raise_error=False)
    if flag_fc_support:
        return True
    else:
        return False


def iscsi_tgt_is_supported(smis_common):
    """
    Return True if FC Target Port 1.4+ profile is supported.
    We use CIM_iSCSIProtocolEndpoint as it's a start point we are
    using in our code of target_ports().
    """
    if smis_common.profile_check(SmisCommon.SNIA_ISCSI_TGT_PORT_PROFILE,
                                 SmisCommon.SMIS_SPEC_VER_1_4,
                                 raise_error=False):
        return True
    return False


def multi_sys_is_supported(smis_common):
    """
    Return True if Multiple ComputerSystem 1.4+ profile is supported.
    Return False else.
    """
    flag_multi_sys_support = smis_common.profile_check(
        SmisCommon.SNIA_MULTI_SYS_PROFILE,
        SmisCommon.SMIS_SPEC_VER_1_4,
        raise_error=False)
    if flag_multi_sys_support:
        return True
    else:
        return False


def get(smis_common, cim_sys, system):
    cap = Capabilities()

    if smis_common.is_netappe():
        _rs_supported_capabilities(smis_common, system.id, cap)

        #TODO We need to investigate why our interrogation code doesn't
        #work.
        #The array is telling us one thing, but when we try to use it, it
        #doesn't work
        return cap

     # 'Block Services Package' profile
    _bsp_cap_set(smis_common, system.id, cap)

    # 'Disk Drive Lite' profile
    _disk_cap_set(smis_common, cim_sys.path, cap)

    # 'Masking and Mapping' and 'Group Masking and Mapping' profiles
    mt = mask_type(smis_common)
    if cim_sys.path.classname == 'Clar_StorageSystem':
        mt = MASK_TYPE_MASK

    if mask_type == MASK_TYPE_GROUP:
        _group_mask_map_cap_set(smis_common, cim_sys.path, cap)
    else:
        _mask_map_cap_set(smis_common, cim_sys.path, cap)

    # 'FC Target Ports' and 'iSCSI Target Ports' profiles
    _tgt_cap_set(smis_common, cim_sys.path, cap)

    _rs_supported_capabilities(smis_common, system.id, cap)
    return cap
