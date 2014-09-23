# Copyright (C) 2011-2014 Red Hat, Inc.
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
#
# Author: Gris Ge <fge@redhat.com>

# This class handle DMTF CIM constants and convert to LSM type.

from lsm import (System, Pool, Disk)
from pywbem import Uint16


class DMTF(object):
    # CIM_ManagedSystemElement['OperationalStatus']
    OP_STATUS_UNKNOWN = 0
    OP_STATUS_OTHER = 1
    OP_STATUS_OK = 2
    OP_STATUS_DEGRADED = 3
    OP_STATUS_STRESSED = 4
    OP_STATUS_PREDICTIVE_FAILURE = 5
    OP_STATUS_ERROR = 6
    OP_STATUS_NON_RECOVERABLE_ERROR = 7
    OP_STATUS_STARTING = 8
    OP_STATUS_STOPPING = 9
    OP_STATUS_STOPPED = 10
    OP_STATUS_IN_SERVICE = 11
    OP_STATUS_NO_CONTACT = 12
    OP_STATUS_LOST_COMMUNICATION = 13
    OP_STATUS_ABORTED = 14
    OP_STATUS_DORMANT = 15
    OP_STATUS_SUPPORTING_ENTITY_IN_ERROR = 16
    OP_STATUS_COMPLETED = 17
    OP_STATUS_POWER_MODE = 18

    _OP_STATUS_STR_CONV = {
        OP_STATUS_UNKNOWN: "UNKNOWN",
        OP_STATUS_OTHER: "OTHER",
        OP_STATUS_OK: "OK",
        OP_STATUS_DEGRADED: "DEGRADED",
        OP_STATUS_STRESSED: "STRESSED",
        OP_STATUS_PREDICTIVE_FAILURE: "PREDICTIVE_FAILURE",
        OP_STATUS_ERROR: "ERROR",
        OP_STATUS_NON_RECOVERABLE_ERROR: "NON_RECOVERABLE_ERROR",
        OP_STATUS_STARTING: "STARTING",
        OP_STATUS_STOPPING: "STOPPING",
        OP_STATUS_STOPPED: "STOPPED",
        OP_STATUS_IN_SERVICE: "IN_SERVICE",
        OP_STATUS_NO_CONTACT: "NO_CONTACT",
        OP_STATUS_LOST_COMMUNICATION: "LOST_COMMUNICATION",
        OP_STATUS_ABORTED: "ABORTED",
        OP_STATUS_DORMANT: "DORMANT",
        OP_STATUS_SUPPORTING_ENTITY_IN_ERROR: "SUPPORTING_ENTITY_IN_ERROR",
        OP_STATUS_COMPLETED: "COMPLETED",
        OP_STATUS_POWER_MODE: "POWER_MODE",
    }

    @staticmethod
    def dmtf_op_status_to_str(dmtf_op_status):
        """
        Just convert integer to string. NOT ALLOWING provide a list.
        Return emtpy string is not found.
        """
        try:
            return DMTF._OP_STATUS_STR_CONV[dmtf_op_status]
        except KeyError:
            return ''

    _LSM_SYS_OP_STATUS_CONV = {
        OP_STATUS_OK: System.STATUS_OK,
        OP_STATUS_ERROR: System.STATUS_ERROR,
        OP_STATUS_DEGRADED: System.STATUS_DEGRADED,
        OP_STATUS_NON_RECOVERABLE_ERROR: System.STATUS_ERROR,
        OP_STATUS_PREDICTIVE_FAILURE: System.STATUS_PREDICTIVE_FAILURE,
        OP_STATUS_SUPPORTING_ENTITY_IN_ERROR: System.STATUS_ERROR,
    }

    @staticmethod
    def _dmtf_op_status_list_conv(conv_dict, dmtf_op_status_list,
                                  unknown_value, other_value):
        status = 0
        status_info_list = []
        for dmtf_op_status in dmtf_op_status_list:
            if dmtf_op_status in conv_dict.keys():
                status |= conv_dict[dmtf_op_status]
            else:
                if dmtf_op_status in DMTF._OP_STATUS_STR_CONV.keys():
                    status |= other_value
                    status_info_list.append(
                        DMTF.dmtf_op_status_to_str(dmtf_op_status))
                    continue
        if status == 0:
            status = unknown_value
        return status, " ".join(status_info_list)

    @staticmethod
    def cim_sys_status_of(dmtf_op_status_list):
        """
        Convert CIM_ComputerSystem['OperationalStatus']
        """
        return DMTF._dmtf_op_status_list_conv(
            DMTF._LSM_SYS_OP_STATUS_CONV, dmtf_op_status_list,
            System.STATUS_UNKNOWN, System.STATUS_OTHER)

    _LSM_POOL_OP_STATUS_CONV = {
        OP_STATUS_OK: Pool.STATUS_OK,
        OP_STATUS_ERROR: Pool.STATUS_ERROR,
        OP_STATUS_DEGRADED: Pool.STATUS_DEGRADED,
        OP_STATUS_NON_RECOVERABLE_ERROR: Pool.STATUS_ERROR,
        OP_STATUS_SUPPORTING_ENTITY_IN_ERROR: Pool.STATUS_ERROR,
    }

    @staticmethod
    def cim_pool_status_of(dmtf_op_status_list):
        """
        Convert CIM_StoragePool['OperationalStatus'] to LSM
        """
        return DMTF._dmtf_op_status_list_conv(
            DMTF._LSM_POOL_OP_STATUS_CONV, dmtf_op_status_list,
            Pool.STATUS_UNKNOWN, Pool.STATUS_OTHER)

    EMC_DISK_STATUS_REMOVED = 32768

    _LSM_DISK_OP_STATUS_CONV = {
        OP_STATUS_UNKNOWN: Disk.STATUS_UNKNOWN,
        OP_STATUS_OK: Disk.STATUS_OK,
        OP_STATUS_PREDICTIVE_FAILURE: Disk.STATUS_PREDICTIVE_FAILURE,
        OP_STATUS_ERROR: Disk.STATUS_ERROR,
        OP_STATUS_NON_RECOVERABLE_ERROR: Disk.STATUS_ERROR,
        OP_STATUS_STARTING: Disk.STATUS_STARTING,
        OP_STATUS_STOPPING: Disk.STATUS_STOPPING,
        OP_STATUS_STOPPED: Disk.STATUS_STOPPED,
    }

    @staticmethod
    def cim_disk_status_of(dmtf_op_status_list):
        """
        Convert CIM_DiskDrive['OperationalStatus'] to LSM
        Only return status, no status_info
        """
        return DMTF._dmtf_op_status_list_conv(
            DMTF._LSM_DISK_OP_STATUS_CONV, dmtf_op_status_list,
            Disk.STATUS_UNKNOWN, Disk.STATUS_OTHER)[0]

    # CIM_StorageHardwareID['IDType']
    ID_TYPE_OTHER = Uint16(1)
    ID_TYPE_WWPN = Uint16(2)
    ID_TYPE_ISCSI = Uint16(5)

    TGT_PORT_USAGE_FRONTEND_ONLY = Uint16(2)
    TGT_PORT_USAGE_UNRESTRICTED = Uint16(4)
    # CIM_FCPort['PortDiscriminator']
    FC_PORT_PORT_DISCRIMINATOR_FCOE = Uint16(10)
    # CIM_NetworkPort['LinkTechnology']
    NET_PORT_LINK_TECH_ETHERNET = Uint16(2)
    # CIM_iSCSIProtocolEndpoint['Role']
    ISCSI_TGT_ROLE_TARGET = Uint16(3)
    # CIM_SCSIProtocolController['NameFormat']
    SPC_NAME_FORMAT_ISCSI = Uint16(3)
    # CIM_IPProtocolEndpoint['IPv6AddressType']
    IPV6_ADDR_TYPE_GUA = Uint16(6)
    # GUA: Global Unicast Address.
    #      2000::/3
    IPV6_ADDR_TYPE_6TO4 = Uint16(7)
    # IPv6 to IPv4 transition
    #      ::ffff:0:0/96
    #      ::ffff:0:0:0/96
    #      64:ff9b::/96     # well-known prefix
    #      2002::/16        # 6to4
    IPV6_ADDR_TYPE_ULA = Uint16(8)
    # ULA: Unique Local Address, aka Site Local Unicast.
    #      fc00::/7

    # CIM_GroupMaskingMappingService.CreateGroup('Type')
    MASK_GROUP_TYPE_INIT = Uint16(2)
    MASK_GROUP_TYPE_TGT = Uint16(3)
    MASK_GROUP_TYPE_DEV = Uint16(4)

    # CIM_GroupMaskingMappingCapabilities['SupportedDeviceGroupFeatures']
    #   Allowing empty DeviceMaskingGroup associated to SPC
    GMM_CAP_DEV_MG_ALLOW_EMPTY_W_SPC = Uint16(5)

    # CIM_GroupMaskingMappingCapabilities['SupportedAsynchronousActions']
    # and 'SupportedSynchronousActions'. They are using the same value map.
    GMM_CAP_DELETE_SPC = Uint16(24)
    GMM_CAP_DELETE_GROUP = Uint16(20)

    # CIM_StorageConfigurationCapabilities['SupportedStorageElementTypes']
    SCS_CAP_SUP_ST_VOLUME = Uint16(2)
    SCS_CAP_SUP_THIN_ST_VOLUME = Uint16(5)

    # CIM_StorageConfigurationCapabilities['SupportedAsynchronousActions']
    # and also for 'SupportedSynchronousActions'
    SCS_CAP_VOLUME_CREATE = Uint16(5)
    SCS_CAP_VOLUME_DELETE = Uint16(6)
    SCS_CAP_VOLUME_MODIFY = Uint16(7)

    # DSP 1033  Profile Registration
    INTEROP_NAMESPACES = ['interop', 'root/interop', 'root/PG_Interop']
    DEFAULT_NAMESPACE = 'interop'


# DMTF CIM 2.37.0 experimental CIM_StoragePool['Usage']
DMTF_POOL_USAGE_UNRESTRICTED = 2
DMTF_POOL_USAGE_RESERVED_FOR_SYSTEM = 3
DMTF_POOL_USAGE_DELTA = 4
DMTF_POOL_USAGE_SPARE = 8

# DMTF CIM 2.29.1 CIM_StorageConfigurationCapabilities
# ['SupportedStorageElementFeatures']
DMTF_SUPPORT_VOL_CREATE = 3
DMTF_SUPPORT_ELEMENT_EXPAND = 12
DMTF_SUPPORT_ELEMENT_REDUCE = 13

# DMTF CIM 2.37.0 experimental CIM_StorageConfigurationCapabilities
# ['SupportedStorageElementTypes']
DMTF_ELEMENT_THICK_VOLUME = 2
DMTF_ELEMENT_THIN_VOLUME = 5

# DMTF CIM 2.29.1 CIM_StorageConfigurationCapabilities
# ['SupportedStoragePoolFeatures']
DMTF_ST_POOL_FEATURE_INEXTS = 2
DMTF_ST_POOL_FEATURE_SINGLE_INPOOL = 3
DMTF_ST_POOL_FEATURE_MULTI_INPOOL = 4

# DMTF CIM 2.38.0+ CIM_StorageSetting['ThinProvisionedPoolType']
DMTF_THINP_POOL_TYPE_ALLOCATED = Uint16(7)

# DMTF Disk Type
DMTF_DISK_TYPE_UNKNOWN = 0
DMTF_DISK_TYPE_OTHER = 1
DMTF_DISK_TYPE_HDD = 2
DMTF_DISK_TYPE_SSD = 3
DMTF_DISK_TYPE_HYBRID = 4

_DMTF_DISK_TYPE_2_LSM = {
    DMTF_DISK_TYPE_UNKNOWN: Disk.TYPE_UNKNOWN,
    DMTF_DISK_TYPE_OTHER: Disk.TYPE_OTHER,
    DMTF_DISK_TYPE_HDD: Disk.TYPE_HDD,
    DMTF_DISK_TYPE_SSD: Disk.TYPE_SSD,
    DMTF_DISK_TYPE_HYBRID: Disk.TYPE_HYBRID,
}


def dmtf_disk_type_2_lsm_disk_type(dmtf_disk_type):
    if dmtf_disk_type in _DMTF_DISK_TYPE_2_LSM.keys():
        return _DMTF_DISK_TYPE_2_LSM[dmtf_disk_type]
    else:
        return Disk.TYPE_UNKNOWN


DMTF_STATUS_UNKNOWN = 0
DMTF_STATUS_OTHER = 1
DMTF_STATUS_OK = 2
DMTF_STATUS_DEGRADED = 3
DMTF_STATUS_STRESSED = 4
DMTF_STATUS_PREDICTIVE_FAILURE = 5
DMTF_STATUS_ERROR = 6
DMTF_STATUS_NON_RECOVERABLE_ERROR = 7
DMTF_STATUS_STARTING = 8
DMTF_STATUS_STOPPING = 9
DMTF_STATUS_STOPPED = 10
DMTF_STATUS_IN_SERVICE = 11
DMTF_STATUS_NO_CONTACT = 12
DMTF_STATUS_LOST_COMMUNICATION = 13
DMTF_STATUS_ABORTED = 14
DMTF_STATUS_DORMANT = 15
DMTF_STATUS_SUPPORTING_ENTITY_IN_ERROR = 16
DMTF_STATUS_COMPLETED = 17
DMTF_STATUS_POWER_MODE = 18