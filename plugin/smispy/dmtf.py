# Copyright (C) 2011-2016 Red Hat, Inc.
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

# This class handle DMTF CIM constants and convert to LSM type.

from pywbem import Uint16


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
POOL_USAGE_UNRESTRICTED = 2
POOL_USAGE_RESERVED_FOR_SYSTEM = 3
POOL_USAGE_DELTA = 4
POOL_USAGE_SPARE = 8

# DMTF CIM 2.29.1 CIM_StorageConfigurationCapabilities
# ['SupportedStorageElementFeatures']
SUPPORT_VOL_CREATE = 3
SUPPORT_ELEMENT_EXPAND = 12
SUPPORT_ELEMENT_REDUCE = 13

# DMTF CIM 2.37.0 experimental CIM_StorageConfigurationCapabilities
# ['SupportedStorageElementTypes']
ELEMENT_THICK_VOLUME = Uint16(2)
ELEMENT_THIN_VOLUME = Uint16(5)

# DMTF CIM 2.29.1 CIM_StorageConfigurationCapabilities
# ['SupportedStoragePoolFeatures']
ST_POOL_FEATURE_INEXTS = 2
ST_POOL_FEATURE_SINGLE_INPOOL = 3
ST_POOL_FEATURE_MULTI_INPOOL = 4

# DMTF CIM 2.38.0+ CIM_StorageSetting['ThinProvisionedPoolType']
THINP_POOL_TYPE_ALLOCATED = Uint16(7)

# DMTF Disk Type
DISK_TYPE_UNKNOWN = 0
DISK_TYPE_OTHER = 1
DISK_TYPE_HDD = 2
DISK_TYPE_SSD = 3
DISK_TYPE_HYBRID = 4

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


def _op_status_to_str(dmtf_op_status):
    """
    Just convert integer to string. NOT ALLOWING provide a list.
    Return emtpy string is not found.
    """
    try:
        return _OP_STATUS_STR_CONV[dmtf_op_status]
    except KeyError:
        return ''


def op_status_list_conv(conv_dict, dmtf_op_status_list,
                        unknown_value, other_value):
    status = 0
    status_info_list = []
    for dmtf_op_status in dmtf_op_status_list:
        if dmtf_op_status in conv_dict.keys():
            status |= conv_dict[dmtf_op_status]
        else:
            if dmtf_op_status in _OP_STATUS_STR_CONV.keys():
                status |= other_value
                status_info_list.append(_op_status_to_str(dmtf_op_status))
                continue
    if status == 0:
        status = unknown_value
    return status, " ".join(status_info_list)

# CIM_ConcreteJob['JobState']
JOB_STATE_NEW = 2
JOB_STATE_STARTING = 3
JOB_STATE_RUNNING = 4
JOB_STATE_COMPLETED = 7

# CIM_Synchronized['SyncType'] also used by
# CIM_ReplicationService.CreateElementReplica() 'SyncType' parameter.
SYNC_TYPE_MIRROR = Uint16(6)
SYNC_TYPE_SNAPSHOT = Uint16(7)
SYNC_TYPE_CLONE = Uint16(8)

# CIM_Synchronized['Mode'] also used by
# CIM_ReplicationService.CreateElementReplica() 'Mode' parameter.
REPLICA_MODE_SYNC = Uint16(2)
REPLICA_MODE_ASYNC = Uint16(3)

# CIM_StorageVolume['NameFormat']
VOL_NAME_FORMAT_NNA = 9

# CIM_StorageVolume['NameNamespace']
VOL_NAME_SPACE_VPD83_TYPE3 = 2

# CIM_ReplicationServiceCapabilities['SupportedAsynchronousActions']
# or CIM_ReplicationServiceCapabilities['SupportedSynchronousActions']
REPLICA_CAP_ACTION_CREATE_ELEMENT = 2

# CIM_ReplicationServiceCapabilities['SupportedReplicationTypes']
REPLICA_CAP_TYPE_SYNC_MIRROR_LOCAL = 2
REPLICA_CAP_TYPE_ASYNC_MIRROR_LOCAL = 3

REPLICA_CAP_TYPE_SYNC_SNAPSHOT_LOCAL = 6
REPLICA_CAP_TYPE_ASYNC_SNAPSHOT_LOCAL = 7

REPLICA_CAP_TYPE_SYNC_CLONE_LOCAL = 10
REPLICA_CAP_TYPE_ASYNC_CLONE_LOCAL = 11

# CIM_Synchronized['CopyState']
COPY_STATE_SYNC = Uint16(4)

# CIM_StorageConfigurationCapabilities['SupportedCopyTypes']
ST_CONF_CAP_COPY_TYPE_ASYNC = Uint16(2)
ST_CONF_CAP_COPY_TYPE_SYNC = Uint16(3)
ST_CONF_CAP_COPY_TYPE_UNSYNC_ASSOC = Uint16(4)
ST_CONF_CAP_COPY_TYPE_UNSYNC_UNASSOC = Uint16(5)

# CIM_StorageSynchronized['SyncState']
ST_SYNC_STATE_SYNCHRONIZED = 6

# CIM_ControllerConfigurationService.ExposePaths(DeviceAccesses)
CTRL_CONF_SRV_DA_RW = Uint16(2)

VOL_OTHER_INFO_NAA_VPD83_TYPE3H = 'NAA;VPD83Type3'

VOL_USAGE_SYS_RESERVED = Uint16(3)
