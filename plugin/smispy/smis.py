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
# Author: tasleson
#         Gris Ge <fge@redhat.com>

from string import split
import time
import traceback
import copy
import os
import datetime
import sys
import re

import pywbem
from pywbem import CIMError

from lsm import (IStorageAreaNetwork, error, uri_parse, LsmError, ErrorNumber,
                 JobStatus, md5, Pool, Volume, AccessGroup, System,
                 Capabilities, Disk, VERSION, TargetPort,
                 search_property)

from dmtf import DMTF

## Variable Naming scheme:
#   cim_xxx         CIMInstance
#   cim_xxx_path    CIMInstanceName
#   cim_sys         CIM_ComputerSystem  (root or leaf)
#   cim_pool        CIM_StoragePool
#   cim_scs         CIM_StorageConfigurationService
#   cim_vol         CIM_StorageVolume
#   cim_rp          CIM_RegisteredProfile
#   cim_init        CIM_StorageHardwareID
#   cim_spc         CIM_SCSIProtocolController
#   cim_init_mg     CIM_InitiatorMaskingGroup
#   cim_fc_tgt      CIM_FCPort
#   cim_iscsi_pg    CIM_iSCSIProtocolEndpoint   # iSCSI portal group
#   cim_iscsi_node  CIM_SCSIProtocolController
#   cim_tcp         CIM_TCPProtocolEndpoint,
#   cim_ip          CIM_IPProtocolEndpoint
#   cim_eth         CIM_EthernetPort
#   cim_pe          CIM_SCSIProtocolEndpoint
#   cim_gmm         CIM_GroupMaskingMappingService
#   cim_ccs         CIM_ControllerConfigurationService
#
#   sys             Object of LSM System
#   pool            Object of LSM Pool
#   vol             Object of LSM Volume

## Method Naming schme:
#   _cim_xxx()
#       Return CIMInstance without any Associators() call.
#   _cim_xxx_of(cim_yyy)
#       Return CIMInstance associated to cim_yyy
#   _adj_cim_xxx()
#       Retrun CIMInstance with 'adj' only
#   _cim_xxx_of_id(some_id)
#       Return CIMInstance for given ID

# Terminology
#   SPC             CIM_SCSIProtocolController
#   BSP             SNIA SMI-S 'Block Services Package' profile
#   Group M&M       SNIA SMI-S 'Group Masking and Mapping' profile


def handle_cim_errors(method):
    def cim_wrapper(*args, **kwargs):
        try:
            return method(*args, **kwargs)
        except LsmError as lsm:
            raise
        except CIMError as ce:
            error_code, desc = ce

            if error_code == 0:
                if 'Socket error' in desc:
                    if 'Errno 111' in desc:
                        raise LsmError(ErrorNumber.NETWORK_CONNREFUSED,
                                       'Connection refused')
                    if 'Errno 113' in desc:
                        raise LsmError(ErrorNumber.NETWORK_HOSTDOWN,
                                       'Host is down')
                elif 'SSL error' in desc:
                    raise LsmError(ErrorNumber.TRANSPORT_COMMUNICATION,
                                   desc)
                elif 'The web server returned a bad status line':
                    raise LsmError(ErrorNumber.TRANSPORT_COMMUNICATION,
                                   desc)
                elif 'HTTP error' in desc:
                    raise LsmError(ErrorNumber.TRANSPORT_COMMUNICATION,
                                   desc)
            raise LsmError(ErrorNumber.PLUGIN_BUG, desc)
        except pywbem.cim_http.AuthError as ae:
            raise LsmError(ErrorNumber.PLUGIN_AUTH_FAILED, "Unauthorized user")
        except pywbem.cim_http.Error as te:
            raise LsmError(ErrorNumber.NETWORK_ERROR, str(te))
        except Exception as e:
            error("Unexpected exception:\n" + traceback.format_exc())
            raise LsmError(ErrorNumber.PLUGIN_BUG, str(e),
                           traceback.format_exc())
    return cim_wrapper


def _spec_ver_str_to_num(spec_ver_str):
    """
    Convert version string stored in CIM_RegisteredProfile to a integer.
    Example:
        "1.5.1" -> 1,005,001
    """
    tmp_list = [0, 0, 0]
    tmp_list = spec_ver_str.split(".")
    if len(tmp_list) == 2:
        tmp_list.extend([0])
    if len(tmp_list) == 3:
        return (int(tmp_list[0]) * 10 ** 6 +
                int(tmp_list[1]) * 10 ** 3 +
                int(tmp_list[2]))
    return None


def _merge_list(list_a, list_b):
    return list(set(list_a + list_b))


def _hex_string_format(hex_str, length, every):
    hex_str = hex_str.lower()
    return ':'.join(hex_str[i:i + every] for i in range(0, length, every))


def _lsm_init_id_to_snia(lsm_init_id):
    """
    If lsm_init_id is a WWPN, convert it to SNIA format:
        [0-9A-F]{16}
    If not, return directly.
    """
    val, init_type, init_id = AccessGroup.initiator_id_verify(lsm_init_id)
    if val and init_type == AccessGroup.INIT_TYPE_WWPN:
        return lsm_init_id.replace(':', '').upper()
    return lsm_init_id


def _dmtf_init_type_to_lsm(cim_init):
    if 'IDType' in cim_init:
        if cim_init['IDType'] == DMTF.ID_TYPE_WWPN:
            return AccessGroup.INIT_TYPE_WWPN
        elif cim_init['IDType'] == DMTF.ID_TYPE_ISCSI:
            return AccessGroup.INIT_TYPE_ISCSI_IQN
    return AccessGroup.INIT_TYPE_UNKNOWN


def _lsm_tgt_port_type_of_cim_fc_tgt(cim_fc_tgt):
    """
    We are assuming we got CIM_FCPort. Caller should make sure of that.
    Return TargetPool.PORT_TYPE_FC as fallback
    """
    # In SNIA SMI-S 1.6.1 public draft 2, 'PortDiscriminator' is mandatroy
    # for FCoE target port.
    if 'PortDiscriminator' in cim_fc_tgt and \
       cim_fc_tgt['PortDiscriminator'] and \
       DMTF.FC_PORT_PORT_DISCRIMINATOR_FCOE in cim_fc_tgt['PortDiscriminator']:
        return TargetPort.TYPE_FCOE
    if 'LinkTechnology' in cim_fc_tgt and \
       cim_fc_tgt['LinkTechnology'] == DMTF.NET_PORT_LINK_TECH_ETHERNET:
        return TargetPort.TYPE_FCOE
    return TargetPort.TYPE_FC


def _lsm_init_type_to_dmtf(init_type):
    if init_type == AccessGroup.INIT_TYPE_WWPN:
        return DMTF.ID_TYPE_WWPN
    if init_type == AccessGroup.INIT_TYPE_ISCSI_IQN:
        return DMTF.ID_TYPE_ISCSI
    raise LsmError(ErrorNumber.NO_SUPPORT,
                   "Does not support provided init_type: %d" % init_type)


class SNIA(object):
    BLK_ROOT_PROFILE = 'Array'
    BLK_SRVS_PROFILE = 'Block Services'
    DISK_LITE_PROFILE = 'Disk Drive Lite'
    MULTI_SYS_PROFILE = 'Multiple Computer System'
    MASK_PROFILE = 'Masking and Mapping'
    GROUP_MASK_PROFILE = 'Group Masking and Mapping'
    FC_TGT_PORT_PROFILE = 'FC Target Ports'
    ISCSI_TGT_PORT_PROFILE = 'iSCSI Target Ports'
    SMIS_SPEC_VER_1_4 = '1.4'
    SMIS_SPEC_VER_1_5 = '1.5'
    SMIS_SPEC_VER_1_6 = '1.6'
    REG_ORG_CODE = pywbem.Uint16(11)


class Smis(IStorageAreaNetwork):
    """
    SMI-S plug-ing which exposes a small subset of the overall provided
    functionality of SMI-S
    """

    # SMI-S job 'JobState' enumerations
    (JS_NEW, JS_STARTING, JS_RUNNING, JS_SUSPENDED, JS_SHUTTING_DOWN,
     JS_COMPLETED,
     JS_TERMINATED, JS_KILLED, JS_EXCEPTION) = (2, 3, 4, 5, 6, 7, 8, 9, 10)

    # SMI-S job 'OperationalStatus' enumerations
    (JOB_OK, JOB_ERROR, JOB_STOPPED, JOB_COMPLETE) = (2, 6, 10, 17)

    # SMI-S invoke return values we are interested in
    # Reference: Page 54 in 1.5 SMI-S block specification
    (INVOKE_OK,
     INVOKE_NOT_SUPPORTED,
     INVOKE_TIMEOUT,
     INVOKE_FAILED,
     INVOKE_INVALID_PARAMETER,
     INVOKE_IN_USE,
     INVOKE_ASYNC,
     INVOKE_SIZE_NOT_SUPPORTED) = (0, 1, 3, 4, 5, 6, 4096, 4097)

    # SMI-S replication enumerations
    (SYNC_TYPE_MIRROR, SYNC_TYPE_SNAPSHOT, SYNC_TYPE_CLONE) = (6, 7, 8)

    # DMTF 2.29.1 (which SNIA SMI-S 1.6 based on)
    # CIM_StorageVolume['NameFormat']
    VOL_NAME_FORMAT_OTHER = 1
    VOL_NAME_FORMAT_VPD83_NNA6 = 2
    VOL_NAME_FORMAT_VPD83_NNA5 = 3
    VOL_NAME_FORMAT_VPD83_TYPE2 = 4
    VOL_NAME_FORMAT_VPD83_TYPE1 = 5
    VOL_NAME_FORMAT_VPD83_TYPE0 = 6
    VOL_NAME_FORMAT_SNVM = 7
    VOL_NAME_FORMAT_NODE_WWN = 8
    VOL_NAME_FORMAT_NNA = 9
    VOL_NAME_FORMAT_EUI64 = 10
    VOL_NAME_FORMAT_T10VID = 11

    # CIM_StorageVolume['NameNamespace']
    VOL_NAME_SPACE_OTHER = 1
    VOL_NAME_SPACE_VPD83_TYPE3 = 2
    VOL_NAME_SPACE_VPD83_TYPE2 = 3
    VOL_NAME_SPACE_VPD83_TYPE1 = 4
    VOL_NAME_SPACE_VPD80 = 5
    VOL_NAME_SPACE_NODE_WWN = 6
    VOL_NAME_SPACE_SNVM = 7

    JOB_RETRIEVE_NONE = 0
    JOB_RETRIEVE_VOLUME = 1
    JOB_RETRIEVE_POOL = 2

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
    DMTF_THINP_POOL_TYPE_ALLOCATED = pywbem.Uint16(7)

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

    @staticmethod
    def dmtf_disk_type_2_lsm_disk_type(dmtf_disk_type):
        if dmtf_disk_type in Smis._DMTF_DISK_TYPE_2_LSM.keys():
            return Smis._DMTF_DISK_TYPE_2_LSM[dmtf_disk_type]
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

    # DSP 1033  Profile Registration
    DMTF_INTEROP_NAMESPACES = ['interop', 'root/interop']
    SMIS_DEFAULT_NAMESPACE = 'interop'

    IAAN_WBEM_HTTP_PORT = 5988
    IAAN_WBEM_HTTPS_PORT = 5989

    MASK_TYPE_NO_SUPPORT = 0
    MASK_TYPE_MASK = 1
    MASK_TYPE_GROUP = 2

    _INVOKE_MAX_LOOP_COUNT = 60
    _INVOKE_CHECK_INTERVAL = 5

    class RepSvc(object):

        class Action(object):
            CREATE_ELEMENT_REPLICA = 2

        class RepTypes(object):
            # SMI-S replication service capabilities
            SYNC_MIRROR_LOCAL = 2
            ASYNC_MIRROR_LOCAL = 3
            SYNC_MIRROR_REMOTE = 4
            ASYNC_MIRROR_REMOTE = 5
            SYNC_SNAPSHOT_LOCAL = 6
            ASYNC_SNAPSHOT_LOCAL = 7
            SYNC_SNAPSHOT_REMOTE = 8
            ASYNC_SNAPSHOT_REMOTE = 9
            SYNC_CLONE_LOCAL = 10
            ASYNC_CLONE_LOCAL = 11
            SYNC_CLONE_REMOTE = 12
            ASYNC_CLONE_REMOTE = 13

    class CopyStates(object):
        INITIALIZED = 2
        UNSYNCHRONIZED = 3
        SYNCHRONIZED = 4
        INACTIVE = 8

    class CopyTypes(object):
        ASYNC = 2           # Async. mirror
        SYNC = 3            # Sync. mirror
        UNSYNCASSOC = 4     # lsm Clone
        UNSYNCUNASSOC = 5   # lsm Copy

    class Synchronized(object):
        class SyncState(object):
            INITIALIZED = 2
            PREPAREINPROGRESS = 3
            PREPARED = 4
            RESYNCINPROGRESS = 5
            SYNCHRONIZED = 6
            FRACTURE_IN_PROGRESS = 7
            QUIESCEINPROGRESS = 8
            QUIESCED = 9
            RESTORE_IN_PROGRESSS = 10
            IDLE = 11
            BROKEN = 12
            FRACTURED = 13
            FROZEN = 14
            COPY_IN_PROGRESS = 15

    # SMI-S mode for mirror updates
    (CREATE_ELEMENT_REPLICA_MODE_SYNC,
     CREATE_ELEMENT_REPLICA_MODE_ASYNC) = (2, 3)

    # SMI-S volume 'OperationalStatus' enumerations
    (VOL_OP_STATUS_OK, VOL_OP_STATUS_DEGRADED, VOL_OP_STATUS_ERR,
     VOL_OP_STATUS_STARTING,
     VOL_OP_STATUS_DORMANT) = (2, 3, 6, 8, 15)

    # SMI-S ExposePaths device access enumerations
    (EXPOSE_PATHS_DA_READ_WRITE, EXPOSE_PATHS_DA_READ_ONLY) = (2, 3)

    def __init__(self):
        self._c = None
        self.tmo = 0
        self.system_list = None
        self.cim_rps = []
        self.cim_root_profile_dict = dict()
        self.fallback_mode = True    # Means we cannot use profile register
        self.all_vendor_namespaces = []
        self.debug_path = None

    def _get_cim_instance_by_id(self, class_type, requested_id,
                                property_list=None, raise_error=True):
        """
        Find out the CIM_XXXX Instance which holding the requested_id
        Return None when error and raise_error is False
        """
        class_name = Smis._cim_class_name_of(class_type)
        error_numer = Smis._not_found_error_of_class(class_type)
        id_pros = Smis._property_list_of_id(class_type, property_list)

        if property_list is None:
            property_list = id_pros
        else:
            property_list = _merge_list(property_list, id_pros)

        cim_xxxs = self._enumerate(class_name, property_list)
        org_requested_id = requested_id
        if class_type == 'Job':
            (requested_id, ignore) = self._parse_job_id(requested_id)
        for cim_xxx in cim_xxxs:
            if self._id(class_type, cim_xxx) == requested_id:
                return cim_xxx
        if raise_error is False:
            return None

        raise LsmError(error_numer,
                       "Cannot find %s Instance with " % class_name +
                       "%s ID '%s'" % (class_type, org_requested_id))

    def _get_class_instance(self, class_name, prop_name, prop_value,
                            raise_error=True, property_list=None):
        """
        Gets an instance of a class that optionally matches a specific
        property name and value
        """
        instances = None
        if property_list is None:
            property_list = [prop_name]
        else:
            property_list = _merge_list(property_list, [prop_name])

        try:
            cim_xxxs = self._enumerate(class_name, property_list)
        except CIMError as ce:
            error_code = tuple(ce)[0]

            if error_code == pywbem.CIM_ERR_INVALID_CLASS and \
               raise_error is False:
                return None
            else:
                raise

        for cim_xxx in cim_xxxs:
            if prop_name in cim_xxx and cim_xxx[prop_name] == prop_value:
                return cim_xxx

        if raise_error:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "Unable to find class instance %s " % class_name +
                           "with property %s " % prop_name +
                           "with value %s" % prop_value)
        return None

    def _pi(self, msg, retrieve_data, rc, out):
        """
        Handle the the process of invoking an operation.
        """
        # Check to see if operation is done
        if rc == Smis.INVOKE_OK:
            if retrieve_data == Smis.JOB_RETRIEVE_VOLUME:
                return None, self._new_vol_from_name(out)
            elif retrieve_data == Smis.JOB_RETRIEVE_POOL:
                return None, self._new_pool_from_name(out)
            else:
                return None, None

        elif rc == Smis.INVOKE_ASYNC:
            # We have an async operation
            job_id = self._job_id(out['Job'], retrieve_data)
            return job_id, None
        elif rc == Smis.INVOKE_NOT_SUPPORTED:
            raise LsmError(
                ErrorNumber.NO_SUPPORT,
                'SMI-S error code indicates operation not supported')
        else:
            # When debugging issues with providers it's helpful to have the
            # xml request/reply to give to provider developers.
            try:
                if self.debug_path is not None:
                    if not os.path.exists(self.debug_path):
                        os.makedirs(self.debug_path)

                    if os.path.isdir(self.debug_path):
                        debug_fn = "%s_%s" % \
                                   (msg, datetime.datetime.now().isoformat())
                        debug_full = os.path.join(self.debug_path, debug_fn)

                        # Dump the request & reply to a file
                        with open(debug_full, 'w') as d:
                            d.write("REQ:\n%s\n\nREPLY:\n%s\n" %
                                    (self._c.last_request, self._c.last_reply))

            except Exception:
                tb = traceback.format_exc()
                raise LsmError(ErrorNumber.PLUGIN_BUG,
                               'Error: ' + msg + " rc= " + str(rc) +
                               ' Debug data exception: ' + str(tb))

            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           'Error: ' + msg + " rc= " + str(rc))

    @handle_cim_errors
    def plugin_register(self, uri, password, timeout, flags=0):
        """
        Called when the plug-in runner gets the start request from the client.
        Checkout interop support status via:
            1. Enumerate CIM_RegisteredProfile in 'interop' namespace.
            2. if nothing found, then
               Enumerate CIM_RegisteredProfile in 'root/interop' namespace.
            3. if nothing found, then
               Enumerate CIM_RegisteredProfile in userdefined namespace.
        """
        protocol = 'http'
        port = Smis.IAAN_WBEM_HTTP_PORT
        u = uri_parse(uri, ['scheme', 'netloc', 'host'], None)

        if u['scheme'].lower() == 'smispy+ssl':
            protocol = 'https'
            port = Smis.IAAN_WBEM_HTTPS_PORT

        if 'port' in u:
            port = u['port']

        url = "%s://%s:%s" % (protocol, u['host'], port)

        # System filtering
        self.system_list = None

        namespace = None
        if 'namespace' in u['parameters']:
            namespace = u['parameters']['namespace']
            self.all_vendor_namespaces = [namespace]
        else:
            namespace = Smis.SMIS_DEFAULT_NAMESPACE

        if 'systems' in u['parameters']:
            self.system_list = split(u['parameters']["systems"], ":")

        if namespace is not None:
            self._c = pywbem.WBEMConnection(url, (u['username'], password),
                                            namespace)
            if "no_ssl_verify" in u["parameters"] \
               and u["parameters"]["no_ssl_verify"] == 'yes':
                try:
                    self._c = pywbem.WBEMConnection(
                        url,
                        (u['username'], password),
                        namespace,
                        no_verification=True)
                except TypeError:
                    # pywbem is not holding fix from
                    # https://bugzilla.redhat.com/show_bug.cgi?id=1039801
                    pass

        self.tmo = timeout

        if 'debug_path' in u['parameters']:
            self.debug_path = u['parameters']['debug_path']
            self._c.debug = True

        if 'force_fallback_mode' in u['parameters'] and \
           u['parameters']['force_fallback_mode'] == 'yes':
            return

        # Checking profile registration support status unless
        # force_fallback_mode is enabled in URI.
        namespace_check_list = Smis.DMTF_INTEROP_NAMESPACES
        if 'namespace' in u['parameters'] and \
           u['parameters']['namespace'] not in namespace_check_list:
            namespace_check_list.extend([u['parameters']['namespace']])

        for interop_namespace in Smis.DMTF_INTEROP_NAMESPACES:
            try:
                self.cim_rps = self._c.EnumerateInstances(
                    'CIM_RegisteredProfile',
                    namespace=interop_namespace,
                    PropertyList=['RegisteredName', 'RegisteredVersion',
                                  'RegisteredOrganization'],
                    LocalOnly=False)
            except CIMError as e:
                if e[0] == pywbem.CIM_ERR_NOT_SUPPORTED or \
                   e[0] == pywbem.CIM_ERR_INVALID_NAMESPACE or \
                   e[0] == pywbem.CIM_ERR_INVALID_CLASS:
                    pass
                else:
                    raise
            if len(self.cim_rps) != 0:
                break

        if len(self.cim_rps) >= 1:
            self.fallback_mode = False
            self.all_vendor_namespaces = []
            # Support 'Array' profile is step 0 for this whole plugin.
            # We find out all 'Array' CIM_RegisteredProfile and stored
            # them into self.cim_root_profile_dict
            if not self._profile_is_supported(
                    SNIA.BLK_ROOT_PROFILE,
                    SNIA.SMIS_SPEC_VER_1_4,
                    strict=False):
                raise LsmError(ErrorNumber.NO_SUPPORT,
                               "Target SMI-S provider does not support "
                               "SNIA SMI-S SPEC %s '%s' profile" %
                               (SNIA.SMIS_SPEC_VER_1_4,
                                SNIA.BLK_ROOT_PROFILE))

    def time_out_set(self, ms, flags=0):
        self.tmo = ms

    def time_out_get(self, flags=0):
        return self.tmo

    def plugin_unregister(self, flags=0):
        self._c = None

    def _bsp_cap_set(self, cim_sys_path, cap):
        """
        Set capabilities for these methods:
            volumes()
            volume_create()
            volume_resize()
            volume_delete()
        """
        if self.fallback_mode:
            # pools() is mandatory, we will try pools() related methods first
            try:
                self._cim_pools_of(cim_sys_path)
            except CIMError as e:
                if e[0] == pywbem.CIM_ERR_NOT_SUPPORTED or \
                   e[0] == pywbem.CIM_ERR_INVALID_CLASS:
                    raise LsmError(ErrorNumber.NO_SUPPORT,
                                   "Target SMI-S provider does not support "
                                   "CIM_StoragePool querying which is "
                                   "mandatory for pools() method")
                else:
                    raise

        # CIM_StorageConfigurationService is optional.
        cim_scs_path = self._get_cim_service_path(
            cim_sys_path, 'CIM_StorageConfigurationService')

        if cim_scs_path is None:
            return

        # These methods are mandatory for CIM_StorageConfigurationService:
        #   CreateOrModifyElementFromStoragePool()
        #   ReturnToStoragePool()
        # But SNIA never defined which function of
        # CreateOrModifyElementFromStoragePool() is mandatory.
        # Hence we check CIM_StorageConfigurationCapabilities
        # which is mandatory if CIM_StorageConfigurationService is supported.
        cim_scs_cap = self._c.Associators(
            cim_scs_path,
            AssocClass='CIM_ElementCapabilities',
            ResultClass='CIM_StorageConfigurationCapabilities',
            PropertyList=['SupportedAsynchronousActions',
                          'SupportedSynchronousActions',
                          'SupportedStorageElementTypes'])[0]

        element_types = cim_scs_cap['SupportedStorageElementTypes']
        sup_actions = []
        if cim_scs_cap['SupportedSynchronousActions']:
            sup_actions.extend(cim_scs_cap['SupportedSynchronousActions'])
        if cim_scs_cap['SupportedAsynchronousActions']:
            sup_actions.extend(cim_scs_cap['SupportedAsynchronousActions'])

        if DMTF.SCS_CAP_SUP_ST_VOLUME in element_types or \
           DMTF.SCS_CAP_SUP_THIN_ST_VOLUME in element_types:
            cap.set(Capabilities.VOLUMES)
            if DMTF.SCS_CAP_SUP_THIN_ST_VOLUME in element_types:
                cap.set(Capabilities.VOLUME_THIN)

        if DMTF.SCS_CAP_VOLUME_CREATE in sup_actions:
            cap.set(Capabilities.VOLUME_CREATE)

        if DMTF.SCS_CAP_VOLUME_DELETE in sup_actions:
            cap.set(Capabilities.VOLUME_DELETE)

        if DMTF.SCS_CAP_VOLUME_MODIFY in sup_actions:
            cap.set(Capabilities.VOLUME_RESIZE)

        return

    def _disk_cap_set(self, cim_sys_path, cap):
        if self.fallback_mode:
            try:
                # Assuming provider support disk drive when systems under it
                # support it.
                self._enumerate('CIM_DiskDrive')
            except CIMError as e:
                if e[0] == pywbem.CIM_ERR_NOT_SUPPORTED or \
                   e[0] == pywbem.CIM_ERR_INVALID_CLASS:
                    return
                else:
                    raise
        else:
            if not self._profile_is_supported(SNIA.DISK_LITE_PROFILE,
                                              SNIA.SMIS_SPEC_VER_1_4,
                                              strict=False,
                                              raise_error=False):
                return

        cap.set(Capabilities.DISKS)
        return

    def _rs_supported_capabilities(self, system, cap):
        """
        Interrogate the supported features of the replication service
        """
        rs = self._get_class_instance("CIM_ReplicationService", 'SystemName',
                                      system.id, raise_error=False)
        if rs:
            rs_cap = self._c.Associators(
                rs.path,
                AssocClass='CIM_ElementCapabilities',
                ResultClass='CIM_ReplicationServiceCapabilities')[0]

            s_rt = rs_cap['SupportedReplicationTypes']

            if self.RepSvc.Action.CREATE_ELEMENT_REPLICA in s_rt or \
                    self.RepSvc.Action.CREATE_ELEMENT_REPLICA in s_rt:
                cap.set(Capabilities.VOLUME_REPLICATE)

            # Mirror support is not working and is not supported at this time.
            # if self.RepSvc.RepTypes.SYNC_MIRROR_LOCAL in s_rt:
            #    cap.set(Capabilities.DeviceID)

            # if self.RepSvc.RepTypes.ASYNC_MIRROR_LOCAL \
            #    in s_rt:
            #    cap.set(Capabilities.VOLUME_REPLICATE_MIRROR_ASYNC)

            if self.RepSvc.RepTypes.SYNC_SNAPSHOT_LOCAL in s_rt or \
                    self.RepSvc.RepTypes.ASYNC_SNAPSHOT_LOCAL in s_rt:
                cap.set(Capabilities.VOLUME_REPLICATE_CLONE)

            if self.RepSvc.RepTypes.SYNC_CLONE_LOCAL in s_rt or \
               self.RepSvc.RepTypes.ASYNC_CLONE_LOCAL in s_rt:
                cap.set(Capabilities.VOLUME_REPLICATE_COPY)
        else:
            # Try older storage configuration service

            rs = self._get_class_instance("CIM_StorageConfigurationService",
                                          'SystemName',
                                          system.id, raise_error=False)

            if rs:
                rs_cap = self._c.Associators(
                    rs.path,
                    AssocClass='CIM_ElementCapabilities',
                    ResultClass='CIM_StorageConfigurationCapabilities')[0]

                if rs_cap is not None and 'SupportedCopyTypes' in rs_cap:
                    sct = rs_cap['SupportedCopyTypes']

                    if sct and len(sct):
                        cap.set(Capabilities.VOLUME_REPLICATE)

                    # Mirror support is not working and is not supported at
                    # this time.

                    # if Smis.CopyTypes.ASYNC in sct:
                    #    cap.set(Capabilities.VOLUME_REPLICATE_MIRROR_ASYNC)

                    # if Smis.CopyTypes.SYNC in sct:
                    #    cap.set(Capabilities.VOLUME_REPLICATE_MIRROR_SYNC)

                        if Smis.CopyTypes.UNSYNCASSOC in sct:
                            cap.set(Capabilities.VOLUME_REPLICATE_CLONE)

                        if Smis.CopyTypes.UNSYNCUNASSOC in sct:
                            cap.set(Capabilities.VOLUME_REPLICATE_COPY)

    def _mask_map_cap_set(self, cim_sys_path, cap):
        """
        In SNIA SMI-S 1.4rev6 'Masking and Mapping' profile:
        CIM_ControllerConfigurationService is mandatory
        and it's ExposePaths() and HidePaths() are mandatory

        For fallback mode, once we found CIM_ControllerConfigurationService,
        we assume they are supporting 1.4rev6 'Masking and Mapping' profile.
        Fallback mode means target provider does not support interop, but
        they still need to follow at least SNIA SMI-S 1.4rev6
        """
        if self.fallback_mode:
            cim_ccs_path = self._get_cim_service_path(
                cim_sys_path, 'CIM_ControllerConfigurationService')
            if cim_ccs_path is None:
                return

        elif not self._profile_is_supported(SNIA.MASK_PROFILE,
                                            SNIA.SMIS_SPEC_VER_1_4,
                                            strict=False,
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

        if self._fc_tgt_is_supported(cim_sys_path):
            cap.set(Capabilities.ACCESS_GROUP_INITIATOR_ADD_WWPN)
        if self._iscsi_tgt_is_supported(cim_sys_path):
            cap.set(Capabilities.ACCESS_GROUP_INITIATOR_ADD_ISCSI_IQN)
        return

    def _common_capabilities(self, system):
        cap = Capabilities()

        self._rs_supported_capabilities(system, cap)
        return cap

    def _tgt_cap_set(self, cim_sys_path, cap):

        # LSI MegaRAID actually not support FC Target and iSCSI target,
        # They expose empty list of CIM_FCPort
        if cim_sys_path.classname == 'LSIESG_MegaRAIDHBA':
            return

        flag_fc_support = False
        flag_iscsi_support = False
        if self.fallback_mode:
            flag_fc_support = True
            flag_iscsi_support = True
            # CIM_FCPort is the contral class of FC Targets profile
            try:
                self._cim_fc_tgt_of(cim_sys_path)
            except CIMError as e:
                if e[0] == pywbem.CIM_ERR_NOT_SUPPORTED or \
                   e[0] == pywbem.CIM_ERR_INVALID_CLASS:
                    flag_fc_support = False

            try:
                self._cim_iscsi_pg_of(cim_sys_path)
            except CIMError as e:
                if e[0] == pywbem.CIM_ERR_NOT_SUPPORTED or \
                   e[0] == pywbem.CIM_ERR_INVALID_CLASS:
                    flag_iscsi_support = False
        else:
            flag_fc_support = self._profile_is_supported(
                SNIA.FC_TGT_PORT_PROFILE,
                SNIA.SMIS_SPEC_VER_1_4,
                strict=False,
                raise_error=False)
            # One more check for NetApp Typo:
            #   NetApp:     'FC Target Port'
            #   SMI-S:      'FC Target Ports'
            # Bug reported.
            if not flag_fc_support:
                flag_fc_support = self._profile_is_supported(
                    'FC Target Port',
                    SNIA.SMIS_SPEC_VER_1_4,
                    strict=False,
                    raise_error=False)
            flag_iscsi_support = self._profile_is_supported(
                SNIA.ISCSI_TGT_PORT_PROFILE,
                SNIA.SMIS_SPEC_VER_1_4,
                strict=False,
                raise_error=False)

        if flag_fc_support or flag_iscsi_support:
            cap.set(Capabilities.TARGET_PORTS)
        return

    def _group_mask_map_cap_set(self, cim_sys_path, cap):
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
        if self._fc_tgt_is_supported(cim_sys_path):
            cap.set(Capabilities.ACCESS_GROUP_INITIATOR_ADD_WWPN)
            cap.set(Capabilities.ACCESS_GROUP_CREATE_WWPN)
        if self._iscsi_tgt_is_supported(cim_sys_path):
            cap.set(Capabilities.ACCESS_GROUP_INITIATOR_ADD_ISCSI_IQN)
            cap.set(Capabilities.ACCESS_GROUP_CREATE_ISCSI_IQN)

        # RemoveMembers is also mandatory
        cap.set(Capabilities.ACCESS_GROUP_INITIATOR_DELETE)

        cim_gmm_cap_pros = [
            'SupportedAsynchronousActions',
            'SupportedSynchronousActions',
            'SupportedDeviceGroupFeatures']

        cim_gmm_cap = self._c.Associators(
            cim_sys_path,
            AssocClass='CIM_ElementCapabilities',
            ResultClass='CIM_GroupMaskingMappingCapabilities',
            PropertyList=cim_gmm_cap_pros)[0]

        # if empty dev group in spc is allowed, RemoveMembers() is enough
        # to do volume_unamsk(). RemoveMembers() is mandatory.
        if DMTF.GMM_CAP_DEV_MG_ALLOW_EMPTY_W_SPC in \
           cim_gmm_cap['SupportedDeviceGroupFeatures']:
            cap.set(Capabilities.VOLUME_UNMASK)

        # DeleteMaskingView() is optional, this is required by volume_unmask()
        # when empty dev group in spc not allowed.
        elif ((DMTF.GMM_CAP_DELETE_SPC in
               cim_gmm_cap['SupportedSynchronousActions']) or
              (DMTF.GMM_CAP_DELETE_SPC in
               cim_gmm_cap['SupportedAsynchronousActions'])):
            cap.set(Capabilities.VOLUME_UNMASK)

        # DeleteGroup is optional, this is required by access_group_delete()
        if ((DMTF.GMM_CAP_DELETE_GROUP in
             cim_gmm_cap['SupportedSynchronousActions']) or
            (DMTF.GMM_CAP_DELETE_GROUP in
             cim_gmm_cap['SupportedAsynchronousActions'])):
            cap.set(Capabilities.ACCESS_GROUP_DELETE)
        return None

    @handle_cim_errors
    def capabilities(self, system, flags=0):

        cim_sys = self._get_cim_instance_by_id(
            'System', system.id, raise_error=True)

        cap = Capabilities()

        # 'Block Services Package' profile
        self._bsp_cap_set(cim_sys.path, cap)

        # 'Disk Drive Lite' profile
        self._disk_cap_set(cim_sys.path, cap)

        # 'Masking and Mapping' and 'Group Masking and Mapping' profiles
        mask_type = self._mask_type()
        if cim_sys.path.classname == 'Clar_StorageSystem':
            mask_type = Smis.MASK_TYPE_MASK

        if mask_type == Smis.MASK_TYPE_GROUP:
            self._group_mask_map_cap_set(cim_sys.path, cap)
        else:
            self._mask_map_cap_set(cim_sys.path, cap)

        # 'FC Target Ports' and 'iSCSI Target Ports' profiles
        self._tgt_cap_set(cim_sys.path, cap)

        self._rs_supported_capabilities(system, cap)
        return cap

    @handle_cim_errors
    def plugin_info(self, flags=0):
        return "Generic SMI-S support", VERSION

    @staticmethod
    def _job_completed_ok(status):
        """
        Given a concrete job instance, check the operational status.  This
        is a little convoluted as different SMI-S proxies return the values in
        different positions in list :-)
        """
        rc = False
        op = status['OperationalStatus']

        if (len(op) > 1 and
            ((op[0] == Smis.JOB_OK and op[1] == Smis.JOB_COMPLETE) or
             (op[0] == Smis.JOB_COMPLETE and op[1] == Smis.JOB_OK))):
            rc = True

        return rc

    @handle_cim_errors
    def job_status(self, job_id, flags=0):
        """
        Given a job id returns the current status as a tuple
        (status (enum), percent_complete(integer), volume (None or Volume))
        """
        completed_item = None

        props = ['JobState', 'PercentComplete', 'ErrorDescription',
                 'OperationalStatus']
        cim_job_pros = self._property_list_of_id('Job', props)

        cim_job = self._get_cim_instance_by_id('Job', job_id, cim_job_pros)

        job_state = cim_job['JobState']

        if job_state in (Smis.JS_NEW, Smis.JS_STARTING, Smis.JS_RUNNING):
            status = JobStatus.INPROGRESS

            pc = cim_job['PercentComplete']
            if pc > 100:
                percent_complete = 100
            else:
                percent_complete = pc

        elif job_state == Smis.JS_COMPLETED:
            status = JobStatus.COMPLETE
            percent_complete = 100

            if Smis._job_completed_ok(cim_job):
                (ignore, retrieve_data) = self._parse_job_id(job_id)
                if retrieve_data == Smis.JOB_RETRIEVE_VOLUME:
                    completed_item = self._new_vol_from_job(cim_job)
                elif retrieve_data == Smis.JOB_RETRIEVE_POOL:
                    completed_item = self._new_pool_from_job(cim_job)
            else:
                status = JobStatus.ERROR

        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           str(cim_job['ErrorDescription']))

        return status, percent_complete, completed_item

    @staticmethod
    def _cim_class_name_of(class_type):
        if class_type == 'Volume':
            return 'CIM_StorageVolume'
        if class_type == 'System':
            return 'CIM_ComputerSystem'
        if class_type == 'Pool':
            return 'CIM_StoragePool'
        if class_type == 'Disk':
            return 'CIM_DiskDrive'
        if class_type == 'Job':
            return 'CIM_ConcreteJob'
        if class_type == 'AccessGroup':
            return 'CIM_SCSIProtocolController'
        if class_type == 'Initiator':
            return 'CIM_StorageHardwareID'
        raise LsmError(ErrorNumber.PLUGIN_BUG,
                       "Smis._cim_class_name_of() got unknown " +
                       "class_type %s" % class_type)

    @staticmethod
    def _not_found_error_of_class(class_type):
        if class_type == 'Volume':
            return ErrorNumber.NOT_FOUND_VOLUME
        if class_type == 'System':
            return ErrorNumber.NOT_FOUND_SYSTEM
        if class_type == 'Pool':
            return ErrorNumber.NOT_FOUND_POOL
        if class_type == 'Job':
            return ErrorNumber.NOT_FOUND_JOB
        if class_type == 'AccessGroup':
            return ErrorNumber.NOT_FOUND_ACCESS_GROUP
        if class_type == 'Initiator':
            return ErrorNumber.INVALID_ARGUMENT
        raise LsmError(ErrorNumber.PLUGIN_BUG,
                       "Smis._cim_class_name_of() got unknown " +
                       "class_type %s" % class_type)

    @staticmethod
    def _property_list_of_id(class_type, extra_properties=None):
        """
        Return a PropertyList which the ID of current class is basing on
        """
        rc = []
        if class_type == 'Volume':
            rc = ['SystemName', 'DeviceID']
        elif class_type == 'System':
            rc = ['Name']
        elif class_type == 'Pool':
            rc = ['InstanceID']
        elif class_type == 'SystemChild':
            rc = ['SystemName']
        elif class_type == 'Disk':
            rc = ['SystemName', 'DeviceID']
        elif class_type == 'Job':
            rc = ['InstanceID']
        elif class_type == 'Initiator':
            rc = ['StorageID']
        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "Smis._property_list_of_id() got unknown " +
                           "class_type %s" % class_type)

        if extra_properties:
            rc = _merge_list(rc, extra_properties)
        return rc

    def _sys_id_child(self, cim_xxx):
        """
        Find out the system id of Pool/Volume/Disk/AccessGroup/Initiator
        Currently, we just use SystemName of cim_xxx
        """
        return self._id('SystemChild', cim_xxx)

    def _sys_id(self, cim_sys):
        """
        Return CIM_ComputerSystem['Name']
        """
        return self._id('System', cim_sys)

    def _pool_id(self, cim_pool):
        """
        Return CIM_StoragePool['InstanceID']
        """
        return self._id('Pool', cim_pool)

    def _vol_id(self, cim_vol):
        """
        Return the MD5 hash of CIM_StorageVolume['SystemName'] and
        ['DeviceID']
        """
        return self._id('Volume', cim_vol)

    def _disk_id(self, cim_disk):
        """
        Return the MD5 hash of CIM_DiskDrive['SystemName'] and ['DeviceID']
        """
        return self._id('Disk', cim_disk)

    def _job_id(self, cim_job, retrieve_data):
        """
        Return the MD5 has of CIM_ConcreteJob['InstanceID'] in conjunction
        with '@%s' % retrieve_data
        retrieve_data should be JOB_RETRIEVE_NONE or JOB_RETRIEVE_VOLUME or etc
        """
        return "%s@%d" % (self._id('Job', cim_job), int(retrieve_data))

    def _init_id(self, cim_init):
        """
        Retrive Initiator ID from CIM_StorageHardwareID
        """
        return self._id('Initiator', cim_init)

    def _id(self, class_type, cim_xxx):
        """
        Return the ID of certain class.
        When ID is based on two or more properties, we use MD5 hash of them.
        If not, return the property value.
        """
        property_list = Smis._property_list_of_id(class_type)
        for key in property_list:
            if key not in cim_xxx:
                cim_xxx = self._c.GetInstance(cim_xxx.path,
                                              PropertyList=property_list,
                                              LocalOnly=False)
                break

        id_str = ''
        for key in property_list:
            if key not in cim_xxx:
                cim_class_name = ''
                if class_type == 'SystemChild':
                    cim_class_name = str(cim_xxx.classname)
                else:
                    cim_class_name = Smis._cim_class_name_of(class_type)
                raise LsmError(ErrorNumber.NO_SUPPORT,
                               "%s %s " % (cim_class_name, cim_xxx.path) +
                               "does not have property %s " % str(key) +
                               "calculate out %s id" % class_type)
            else:
                id_str += cim_xxx[key]
        if len(property_list) == 1 and class_type != 'Job':
            return id_str
        else:
            return md5(id_str)

    @staticmethod
    def _parse_job_id(job_id):
        """
        job_id is assembled by a md5 string and retrieve_data
        This method will split it and return (md5_str, retrieve_data)
        """
        tmp_list = job_id.split('@', 2)
        md5_str = tmp_list[0]
        retrieve_data = Smis.JOB_RETRIEVE_NONE
        if len(tmp_list) == 2:
            retrieve_data = int(tmp_list[1])
        return (md5_str, retrieve_data)

    def _get_pool_from_vol(self, cim_vol):
        """
         Takes a CIMInstance that represents a volume and returns the pool
         id for that volume.
        """
        property_list = Smis._property_list_of_id('Pool')
        cim_pool = self._c.Associators(
            cim_vol.path,
            AssocClass='CIM_AllocatedFromStoragePool',
            ResultClass='CIM_StoragePool',
            PropertyList=property_list)[0]
        return self._pool_id(cim_pool)

    @staticmethod
    def _get_vol_other_id_info(cv):
        other_id = None

        if 'OtherIdentifyingInfo' in cv \
                and cv["OtherIdentifyingInfo"] is not None \
                and len(cv["OtherIdentifyingInfo"]) > 0:

            other_id = cv["OtherIdentifyingInfo"]

            if isinstance(other_id, list):
                other_id = other_id[0]

            # This is not what we are looking for if the field has this value
            if other_id is not None and other_id == "VPD83Type3":
                other_id = None

        return other_id

    def _cim_vol_pros(self):
        """
        Retrun the PropertyList required for creating new LSM Volume.
        """
        props = ['ElementName', 'NameFormat',
                 'NameNamespace', 'BlockSize', 'NumberOfBlocks', 'Name',
                 'OtherIdentifyingInfo', 'IdentifyingDescriptions', 'Usage']
        cim_vol_pros = self._property_list_of_id("Volume", props)
        return cim_vol_pros

    def _new_vol(self, cv, pool_id=None, sys_id=None):
        """
        Takes a CIMInstance that represents a volume and returns a lsm Volume
        """

        # This is optional (User friendly name)
        if 'ElementName' in cv:
            user_name = cv["ElementName"]
        else:
            #Better fallback value?
            user_name = cv['DeviceID']

        vpd_83 = Smis._vpd83_in_cv_name(cv)
        if vpd_83 is None:
            vpd_83 = Smis._vpd83_in_cv_otherinfo(cv)

        if vpd_83 is None:
            vpd_83 = Smis._vpd83_in_cv_ibm_xiv(cv)

        if vpd_83 and re.match('^[a-fA-F0-9]{32}$', vpd_83):
            vpd_83 = vpd_83.lower()
        else:
            vpd_83 = ''

        #This is a fairly expensive operation, so it's in our best interest
        #to not call this very often.
        if pool_id is None:
            #Go an retrieve the pool id
            pool_id = self._get_pool_from_vol(cv)

        if sys_id is None:
            sys_id = cv['SystemName']

        admin_state = Volume.ADMIN_STATE_ENABLED

        return Volume(self._vol_id(cv), user_name, vpd_83, cv["BlockSize"],
                      cv["NumberOfBlocks"], admin_state, sys_id, pool_id)

    @staticmethod
    def _vpd83_in_cv_name(cv):
        """
        Explanation of these Format is in:
          SMI-S 1.6 r4 SPEC part1 7.6.2 Table 2 Page 38, PDF Page 60:
              Table 2 - Standard Formats for StorageVolume Names
        Only these combinations is allowed when storing VPD83 in cv["Name"]:
         * NameFormat = NAA(9), NameNamespace = VPD83Type3(1)
            SCSI VPD page 83, type 3h, Association=0, NAA 0101b/0110b/0010b
            NAA name with first nibble of 2/5/6.
            Formatted as 16 or 32 un-separated upper case hex digits
         * NameFormat = NAA(9), NameNamespace = VPD83Type3(2)
            SCSI VPD page 83, type 3h, Association=0, NAA 0001b
            NAA name with first nibble of 1. Formatted as 16 un-separated
            upper case hex digits
         * NameFormat = EUI64(10), NameNamespace = VPD83Type2(3)
            SCSI VPD page 83, type 2h, Association=0
            Formatted as 16, 24, or 32 un-separated upper case hex digits
         * NameFormat = T10VID(11), NameNamespace = VPD83Type1(4)
            SCSI VPD page 83, type 1h, Association=0
            Formatted as 1 to 252 bytes of ASCII.
        Will return vpd_83 if found.
        """
        if not ('NameFormat' in cv and
                'NameNamespace' in cv and
                'Name' in cv):
            return None
        nf = cv['NameFormat']
        nn = cv['NameNamespace']
        name = cv['Name']
        if not (nf and nn and name):
            return None
        # SNIA might missly said VPD83Type3(1), it should be
        # VOL_NAME_FORMAT_OTHER(1) based on DMTF.
        # Will remove the Smis.VOL_NAME_FORMAT_OTHER condition if confirmed as
        # SNIA document fault.
        if (nf == Smis.VOL_NAME_FORMAT_NNA and
                nn == Smis.VOL_NAME_FORMAT_OTHER) or \
           (nf == Smis.VOL_NAME_FORMAT_NNA and
                nn == Smis.VOL_NAME_SPACE_VPD83_TYPE3) or \
           (nf == Smis.VOL_NAME_FORMAT_EUI64 and
                nn == Smis.VOL_NAME_SPACE_VPD83_TYPE2) or \
           (nf == Smis.VOL_NAME_FORMAT_T10VID and
                nn == Smis.VOL_NAME_SPACE_VPD83_TYPE1):
            return name

    @staticmethod
    def _vpd83_in_cv_otherinfo(cv):
        """
        In SNIA SMI-S 1.6 r4 part 1 section 7.6.2: "Standard Formats for
        Logical Unit Names" it allow VPD83 stored in 'OtherIdentifyingInfo'
        Quote:
            Storage volumes may have multiple standard names. A page 83
            logical unit identifier shall be placed in the Name property with
            NameFormat and Namespace set as specified in Table 2. Each
            additional name should be placed in an element of
            OtherIdentifyingInfo. The corresponding element in
            IdentifyingDescriptions shall contain a string from the Values
            lists from NameFormat and NameNamespace, separated by a
            semi-colon. For example, an identifier from SCSI VPD page 83 with
            type 3, association 0, and NAA 0101b - the corresponding entry in
            IdentifyingDescriptions[] shall be "NAA;VPD83Type3".
        Will return the vpd_83 value if found
        """
        vpd83_namespaces = ['NAA;VPD83Type1', 'NAA;VPD83Type3',
                            'EUI64;VPD83Type2', 'T10VID;VPD83Type1']
        if not ("IdentifyingDescriptions" in cv and
                "OtherIdentifyingInfo" in cv):
            return None
        id_des = cv["IdentifyingDescriptions"]
        other_info = cv["OtherIdentifyingInfo"]
        if not (isinstance(cv["IdentifyingDescriptions"], list) and
                isinstance(cv["OtherIdentifyingInfo"], list)):
            return None

        index = 0
        len_id_des = len(id_des)
        len_other_info = len(other_info)
        while index < min(len_id_des, len_other_info):
            if [1 for x in vpd83_namespaces if x == id_des[index]]:
                return other_info[index]
            index += 1
        return None

    @staticmethod
    def _vpd83_in_cv_ibm_xiv(cv):
        """
        IBM XIV IBM.2810-MX90014 is not following SNIA standard.
        They are using NameFormat=NodeWWN(8) and
        NameNamespace=NodeWWN(6) and no otherinfo indicated the
        VPD 83 info.
        Its cv["Name"] is equal to VPD 83, will use it.
        """
        if not "CreationClassName" in cv:
            return None
        if cv["CreationClassName"] == "IBMTSDS_SEVolume":
            if "Name" in cv and cv["Name"]:
                return cv["Name"]

    def _new_vol_from_name(self, out):
        """
        Given a volume by CIMInstanceName, return a lsm Volume object
        """
        instance = None

        if 'TheElement' in out:
            instance = self._c.GetInstance(out['TheElement'],
                                           LocalOnly=False)
        elif 'TargetElement' in out:
            instance = self._c.GetInstance(out['TargetElement'],
                                           LocalOnly=False)

        return self._new_vol(instance)

    def _new_pool_from_name(self, out):
        """
        For SYNC CreateOrModifyElementFromStoragePool action.
        The new CIM_StoragePool is stored in out['Pool']
        """
        pool_pros = self._new_pool_cim_pool_pros()

        if 'Pool' in out:
            cim_new_pool = self._c.GetInstance(
                out['Pool'],
                PropertyList=pool_pros, LocalOnly=False)
            return self._new_pool(cim_new_pool)
        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "Got not new Pool from out of InvokeMethod" +
                           "when CreateOrModifyElementFromStoragePool")

    def _cim_spc_pros(self):
        """
        Return a list of properties required to build new AccessGroup.
        """
        cim_spc_pros = ['DeviceID']
        cim_spc_pros.extend(self._property_list_of_id('SystemChild'))
        cim_spc_pros.extend(['ElementName', 'StorageID'])
        cim_spc_pros.extend(['EMCAdapterRole'])  # EMC specific, used to
                                                 # filter out the mapping SPC.
        return cim_spc_pros

    def _cim_inits_to_lsm(self, cim_inits):
        """
        Retrive AccessGroup.init_ids and AccessGroup.init_type from
        a list of CIM_StorageHardwareID.
        """
        init_ids = []
        init_type = AccessGroup.INIT_TYPE_UNKNOWN
        init_types = []
        for cim_init in cim_inits:
            init_type = _dmtf_init_type_to_lsm(cim_init)
            if init_type == AccessGroup.INIT_TYPE_WWPN:
                init_ids.append(self._init_id(cim_init))
                init_types.append(init_type)
            elif init_type == AccessGroup.INIT_TYPE_ISCSI_IQN:
                init_ids.append(self._init_id(cim_init))
                init_types.append(init_type)
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

    def _cim_spc_to_lsm(self, cim_spc, system_id=None):
        if system_id is None:
            system_id = self._sys_id_child(cim_spc)
        ag_id = md5(cim_spc['DeviceID'])
        ag_name = cim_spc['ElementName']
        ag_init_ids = []
        cim_init_pros = self._property_list_of_id('Initiator')
        cim_init_pros.extend(['IDType'])
        cim_inits = self._cim_init_of_spc(cim_spc.path, cim_init_pros)
        (init_ids, init_type) = self._cim_inits_to_lsm(cim_inits)
        sys_id = self._sys_id_child(cim_spc)
        return AccessGroup(ag_id, ag_name, init_ids, init_type, sys_id)

    def _new_vol_from_job(self, job):
        """
        Given a concrete job instance, return referenced volume as lsm volume
        """
        for a in self._c.Associators(job.path,
                                     AssocClass='CIM_AffectedJobElement',
                                     ResultClass='CIM_StorageVolume'):
            return self._new_vol(self._c.GetInstance(a.path, LocalOnly=False))
        return None

    def _new_pool_from_job(self, cim_job):
        """
        Given a CIMInstance of CIM_ConcreteJob, return a LSM Pool
        """
        pool_pros = self._new_pool_cim_pool_pros()
        cim_pools = self._c.Associators(cim_job.path,
                                        AssocClass='CIM_AffectedJobElement',
                                        ResultClass='CIM_StoragePool',
                                        PropertyList=pool_pros)
        return self._new_pool(cim_pools[0])

    @handle_cim_errors
    def volumes(self, search_key=None, search_value=None, flags=0):
        """
        Return all volumes.
        We are basing on "Block Services Package" profile version 1.4 or
        later:
            CIM_ComputerSystem
                 |
                 |  (CIM_HostedStoragePool)
                 |
                 v
            CIM_StoragePool
                 |
                 | (CIM_AllocatedFromStoragePool)
                 |
                 v
            CIM_StorageVolume
        As 'Block Services Package' is mandatory for 'Array' profile, we
        don't check support status here as startup() already checked 'Array'
        profile.
        """
        rc = []
        cim_sys_pros = self._property_list_of_id("System")
        cim_syss = self._root_cim_syss(cim_sys_pros)
        cim_vol_pros = self._cim_vol_pros()
        for cim_sys in cim_syss:
            sys_id = self._sys_id(cim_sys)
            pool_pros = self._property_list_of_id('Pool')
            for cim_pool in self._cim_pools_of(cim_sys.path, pool_pros):
                pool_id = self._pool_id(cim_pool)
                cim_vols = self._c.Associators(
                    cim_pool.path,
                    AssocClass='CIM_AllocatedFromStoragePool',
                    ResultClass='CIM_StorageVolume',
                    PropertyList=cim_vol_pros)
                for cim_vol in cim_vols:
                    # Exclude those volumes which are reserved for system
                    if 'Usage' in cim_vol:
                        if cim_vol['Usage'] != 3:
                            vol = self._new_vol(cim_vol, pool_id, sys_id)
                            rc.extend([vol])
                    else:
                        vol = self._new_vol(cim_vol, pool_id, sys_id)
                        rc.extend([vol])
        return search_property(rc, search_key, search_value)

    def _cim_pools_of(self, cim_sys_path, property_list=None):
        if property_list is None:
            property_list = ['Primordial']
        else:
            property_list = _merge_list(property_list, ['Primordial'])

        cim_pools = self._c.Associators(cim_sys_path,
                                        AssocClass='CIM_HostedStoragePool',
                                        ResultClass='CIM_StoragePool',
                                        PropertyList=property_list)

        return [p for p in cim_pools if not p["Primordial"]]

    def _new_pool_cim_pool_pros(self):
        """
        Return a list of properties for creating new pool.
        """
        pool_pros = self._property_list_of_id('Pool')
        pool_pros.extend(['ElementName', 'TotalManagedSpace',
                          'RemainingManagedSpace', 'Usage',
                          'OperationalStatus'])
        return pool_pros

    @handle_cim_errors
    def pools(self, search_key=None, search_value=None, flags=0):
        """
        We are basing on "Block Services Package" profile version 1.4 or
        later:
            CIM_ComputerSystem
                 |
                 | (CIM_HostedStoragePool)
                 |
                 v
            CIM_StoragePool
        As 'Block Services Package' is mandatory for 'Array' profile, we
        don't check support status here as startup() already checked 'Array'
        profile.
        """
        rc = []
        cim_pool_pros = self._new_pool_cim_pool_pros()

        cim_sys_pros = self._property_list_of_id("System")
        cim_syss = self._root_cim_syss(cim_sys_pros)

        for cim_sys in cim_syss:
            system_id = self._sys_id(cim_sys)
            for cim_pool in self._cim_pools_of(cim_sys.path, cim_pool_pros):
                # Skip spare storage pool.
                if 'Usage' in cim_pool and \
                   cim_pool['Usage'] == Smis.DMTF_POOL_USAGE_SPARE:
                    continue
                # Skip IBM ArrayPool and ArraySitePool
                # ArrayPool is holding RAID info.
                # ArraySitePool is holding 8 disks. Predefined by array.
                # ArraySite --(1to1 map) --> Array --(1to1 map)--> Rank

                # By design when user get a ELEMENT_TYPE_POOL only pool,
                # user can assume he/she can allocate spaces from that pool
                # to create a new pool with ELEMENT_TYPE_VOLUME or
                # ELEMENT_TYPE_FS ability.

                # If we expose them out, we will have two kind of pools
                # (ArrayPool and ArraySitePool) having element_type &
                # ELEMENT_TYPE_POOL, but none of them can create a
                # ELEMENT_TYPE_VOLUME pool.
                # Only RankPool can create a ELEMENT_TYPE_VOLUME pool.

                # We are trying to hide the detail to provide a simple
                # abstraction.
                if cim_pool.classname == 'IBMTSDS_ArrayPool' or \
                   cim_pool.classname == 'IBMTSDS_ArraySitePool':
                    continue

                pool = self._new_pool(cim_pool, system_id)
                if pool:
                    rc.extend([pool])
                else:
                    raise LsmError(ErrorNumber.PLUGIN_BUG,
                                   "Failed to retrieve pool information " +
                                   "from CIM_StoragePool: %s" % cim_pool.path)
        return search_property(rc, search_key, search_value)

    def _sys_id_of_cim_pool(self, cim_pool):
        """
        Find out the system ID for certain CIM_StoragePool.
        Will return '' if failed.
        """
        sys_pros = self._property_list_of_id('System')
        cim_syss = self._c.Associators(cim_pool.path,
                                       ResultClass='CIM_ComputerSystem',
                                       PropertyList=sys_pros)
        if len(cim_syss) == 1:
            return self._sys_id(cim_syss[0])
        return ''

    @handle_cim_errors
    def _new_pool(self, cim_pool, system_id=''):
        """
        Return a Pool object base on information of cim_pool.
        Assuming cim_pool already holding correct properties.
        """
        if not system_id:
            system_id = self._sys_id_of_cim_pool(cim_pool)

        status_info = ''
        pool_id = self._pool_id(cim_pool)
        name = ''
        total_space = Pool.TOTAL_SPACE_NOT_FOUND
        free_space = Pool.FREE_SPACE_NOT_FOUND
        status = Pool.STATUS_OK
        if 'ElementName' in cim_pool:
            name = cim_pool['ElementName']
        if 'TotalManagedSpace' in cim_pool:
            total_space = cim_pool['TotalManagedSpace']
        if 'RemainingManagedSpace' in cim_pool:
            free_space = cim_pool['RemainingManagedSpace']
        if 'OperationalStatus' in cim_pool:
            (status, status_info) = DMTF.cim_pool_status_of(
                cim_pool['OperationalStatus'])

        element_type, unsupported = self._pool_element_type(cim_pool)

        return Pool(pool_id, name, element_type, unsupported,
                    total_space, free_space,
                    status, status_info, system_id)

    @staticmethod
    def _cim_sys_to_lsm(cim_sys):
        # In the case of systems we are assuming that the System Name is
        # unique.
        status = System.STATUS_UNKNOWN
        status_info = ''

        if 'OperationalStatus' in cim_sys:
            (status, status_info) = \
                DMTF.cim_sys_status_of(cim_sys['OperationalStatus'])

        return System(cim_sys['Name'], cim_sys['ElementName'], status,
                      status_info)

    def _cim_sys_pros(self):
        """
        Return a list of properties required to create a LSM System
        """
        cim_sys_pros = self._property_list_of_id('System',
                                                 ['ElementName',
                                                  'OperationalStatus'])
        return cim_sys_pros

    @handle_cim_errors
    def systems(self, flags=0):
        """
        Return the storage arrays accessible from this plug-in at this time

        As 'Block Services Package' is mandatory for 'Array' profile, we
        don't check support status here as startup() already checked 'Array'
        profile.
        """
        cim_sys_pros = self._cim_sys_pros()
        cim_syss = self._root_cim_syss(cim_sys_pros)

        return [Smis._cim_sys_to_lsm(s) for s in cim_syss]

    def _check_for_dupe_vol(self, volume_name, original_exception):
        """
        Throw original exception or NAME_CONFLICT if volume name found
        :param volume_name: Volume to check for
        :original_exception Info grabbed from sys.exec_info
        :return:
        """
        report_original = True

        # Check to see if we already have a volume with this name.  If we
        # do we will assume that this is the cause of it.  We will hide
        # any errors that happen during this check and just report the
        # original error if we can't determine if we have a duplicate
        # name.

        try:
            # TODO: Add ability to search by volume name to 'volumes'
            volumes = self.volumes()
            for v in volumes:
                if v.name == volume_name:
                    report_original = False
        except Exception:
            # Don't report anything here on a failing
            pass

        if report_original:
            raise original_exception[1], None, original_exception[2]
        else:
            raise LsmError(ErrorNumber.NAME_CONFLICT,
                           "Volume with name '%s' already exists!" %
                           volume_name)

    @handle_cim_errors
    def volume_create(self, pool, volume_name, size_bytes, provisioning,
                      flags=0):
        """
        Create a volume.
        """
        if provisioning != Volume.PROVISION_DEFAULT:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "Unsupported provisioning")

        # Get the Configuration service for the system we are interested in.
        scs = self._get_class_instance('CIM_StorageConfigurationService',
                                       'SystemName', pool.system_id)
        sp = self._get_cim_instance_by_id('Pool', pool.id)

        in_params = {'ElementName': volume_name,
                     'ElementType': pywbem.Uint16(2),
                     'InPool': sp.path,
                     'Size': pywbem.Uint64(size_bytes)}

        try:
            return self._pi("volume_create", Smis.JOB_RETRIEVE_VOLUME,
                            *(self._c.InvokeMethod(
                                'CreateOrModifyElementFromStoragePool',
                                scs.path, **in_params)))
        except CIMError:
            self._check_for_dupe_vol(volume_name, sys.exc_info())

    def _poll(self, msg, job):
        if job:
            while True:
                (s, percent, i) = self.job_status(job)

                if s == JobStatus.INPROGRESS:
                    time.sleep(0.25)
                elif s == JobStatus.COMPLETE:
                    self.job_free(job)
                    return i
                else:
                    raise LsmError(
                        ErrorNumber.PLUGIN_BUG,
                        msg + ", job error code= " + str(s))

    def _detach(self, vol, sync):
        rs = self._get_class_instance("CIM_ReplicationService", 'SystemName',
                                      vol.system_id, raise_error=False)

        if rs:
            in_params = {'Operation': pywbem.Uint16(8),
                         'Synchronization': sync.path}

            job_id = self._pi("_detach", Smis.JOB_RETRIEVE_NONE,
                              *(self._c.InvokeMethod(
                                  'ModifyReplicaSynchronization', rs.path,
                                  **in_params)))[0]

            self._poll("ModifyReplicaSynchronization, detach", job_id)

    @staticmethod
    def _cim_name_match(a, b):
        if a['DeviceID'] == b['DeviceID'] \
                and a['SystemName'] == b['SystemName'] \
                and a['SystemCreationClassName'] == \
                b['SystemCreationClassName']:
            return True
        else:
            return False

    def _deal_volume_associations(self, vol, lun):
        """
        Check a volume to see if it has any associations with other
        volumes and deal with them.
        """
        lun_path = lun.path

        try:
            ss = self._c.References(lun_path,
                                    ResultClass='CIM_StorageSynchronized')
        except pywbem.CIMError as e:
            if e[0] == pywbem.CIM_ERR_INVALID_CLASS:
                return
            else:
                raise

        if len(ss):
            for s in ss:
                # TODO: Need to see if detach is a supported operation in
                # replication capabilities.
                #
                # TODO: Theory of delete.  Some arrays will automatically
                # detach a clone, check
                # ReplicationServiceCapabilities.GetSupportedFeatures() and
                # look for "Synchronized clone target detaches automatically".
                # If not automatic then detach manually.  However, we have
                # seen arrays that don't report detach automatically that
                # don't need a detach.
                #
                # This code needs to be re-investigated to work with a wide
                # range of array vendors.

                if 'SyncState' in s and 'CopyType' in s:
                    if s['SyncState'] == \
                            Smis.Synchronized.SyncState.SYNCHRONIZED and \
                            (s['CopyType'] != Smis.CopyTypes.UNSYNCASSOC):
                        if 'SyncedElement' in s:
                            item = s['SyncedElement']

                            if Smis._cim_name_match(item, lun_path):
                                self._detach(vol, s)

                        if 'SystemElement' in s:
                            item = s['SystemElement']

                            if Smis._cim_name_match(item, lun_path):
                                self._detach(vol, s)

    @handle_cim_errors
    def volume_delete(self, volume, flags=0):
        """
        Delete a volume
        """
        scs = self._get_class_instance('CIM_StorageConfigurationService',
                                       'SystemName', volume.system_id)
        lun = self._get_cim_instance_by_id('Volume', volume.id)

        self._deal_volume_associations(volume, lun)

        in_params = {'TheElement': lun.path}

        # Delete returns None or Job number
        return self._pi("volume_delete", Smis.JOB_RETRIEVE_NONE,
                        *(self._c.InvokeMethod('ReturnToStoragePool',
                                               scs.path,
                                               **in_params)))[0]

    @handle_cim_errors
    def volume_resize(self, volume, new_size_bytes, flags=0):
        """
        Re-size a volume
        """
        scs = self._get_class_instance('CIM_StorageConfigurationService',
                                       'SystemName', volume.system_id)
        lun = self._get_cim_instance_by_id('Volume', volume.id)

        in_params = {'ElementType': pywbem.Uint16(2),
                     'TheElement': lun.path,
                     'Size': pywbem.Uint64(new_size_bytes)}

        return self._pi("volume_resize", Smis.JOB_RETRIEVE_VOLUME,
                        *(self._c.InvokeMethod(
                            'CreateOrModifyElementFromStoragePool',
                            scs.path, **in_params)))

    def _get_supported_sync_and_mode(self, system_id, rep_type):
        """
        Converts from a library capability to a suitable array capability

        returns a tuple (sync, mode)
        """
        rc = [None, None]

        rs = self._get_class_instance("CIM_ReplicationService", 'SystemName',
                                      system_id, raise_error=False)

        if rs:
            rs_cap = self._c.Associators(
                rs.path,
                AssocClass='CIM_ElementCapabilities',
                ResultClass='CIM_ReplicationServiceCapabilities')[0]

            s_rt = rs_cap['SupportedReplicationTypes']

            if rep_type == Volume.REPLICATE_COPY:
                if self.RepSvc.RepTypes.SYNC_CLONE_LOCAL in s_rt:
                    rc[0] = Smis.SYNC_TYPE_CLONE
                    rc[1] = Smis.CREATE_ELEMENT_REPLICA_MODE_SYNC
                elif self.RepSvc.RepTypes.ASYNC_CLONE_LOCAL in s_rt:
                    rc[0] = Smis.SYNC_TYPE_CLONE
                    rc[1] = Smis.CREATE_ELEMENT_REPLICA_MODE_ASYNC

            elif rep_type == Volume.REPLICATE_MIRROR_ASYNC:
                if self.RepSvc.RepTypes.ASYNC_MIRROR_LOCAL in s_rt:
                    rc[0] = Smis.SYNC_TYPE_MIRROR
                    rc[1] = Smis.CREATE_ELEMENT_REPLICA_MODE_ASYNC

            elif rep_type == Volume.REPLICATE_MIRROR_SYNC:
                if self.RepSvc.RepTypes.SYNC_MIRROR_LOCAL in s_rt:
                    rc[0] = Smis.SYNC_TYPE_MIRROR
                    rc[1] = Smis.CREATE_ELEMENT_REPLICA_MODE_SYNC

            elif rep_type == Volume.REPLICATE_CLONE:
                if self.RepSvc.RepTypes.SYNC_CLONE_LOCAL in s_rt:
                    rc[0] = Smis.SYNC_TYPE_SNAPSHOT
                    rc[1] = Smis.CREATE_ELEMENT_REPLICA_MODE_SYNC
                elif self.RepSvc.RepTypes.ASYNC_CLONE_LOCAL in s_rt:
                    rc[0] = Smis.SYNC_TYPE_SNAPSHOT
                    rc[1] = Smis.CREATE_ELEMENT_REPLICA_MODE_ASYNC

        if rc[0] is None:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "Replication type not supported")

        return tuple(rc)

    @handle_cim_errors
    def volume_replicate(self, pool, rep_type, volume_src, name, flags=0):
        """
        Replicate a volume
        """
        if rep_type == Volume.REPLICATE_MIRROR_ASYNC \
                or rep_type == Volume.REPLICATE_MIRROR_SYNC:
            raise LsmError(ErrorNumber.NO_SUPPORT, "Mirroring not supported")

        rs = self._get_class_instance("CIM_ReplicationService", 'SystemName',
                                      volume_src.system_id, raise_error=False)

        if pool is not None:
            cim_pool = self._get_cim_instance_by_id('Pool', pool.id)
        else:
            cim_pool = None

        lun = self._get_cim_instance_by_id('Volume', volume_src.id)

        if rs:
            method = 'CreateElementReplica'

            sync, mode = self._get_supported_sync_and_mode(
                volume_src.system_id, rep_type)

            in_params = {'ElementName': name,
                         'SyncType': pywbem.Uint16(sync),
                         #'Mode': pywbem.Uint16(mode),
                         'SourceElement': lun.path,
                         'WaitForCopyState':
                         pywbem.Uint16(Smis.CopyStates.SYNCHRONIZED)}

        else:
            # Check for older support via storage configuration service

            method = 'CreateReplica'

            # Check for storage configuration service
            rs = self._get_class_instance("CIM_StorageConfigurationService",
                                          'SystemName', volume_src.system_id,
                                          raise_error=False)

            ct = Volume.REPLICATE_CLONE
            if rep_type == Volume.REPLICATE_CLONE:
                ct = Smis.CopyTypes.UNSYNCASSOC
            elif rep_type == Volume.REPLICATE_COPY:
                ct = Smis.CopyTypes.UNSYNCUNASSOC
            elif rep_type == Volume.REPLICATE_MIRROR_ASYNC:
                ct = Smis.CopyTypes.ASYNC
            elif rep_type == Volume.REPLICATE_MIRROR_SYNC:
                ct = Smis.CopyTypes.SYNC

            in_params = {'ElementName': name,
                         'CopyType': pywbem.Uint16(ct),
                         'SourceElement': lun.path}
        if rs:

            if cim_pool is not None:
                in_params['TargetPool'] = cim_pool.path

            try:

                return self._pi("volume_replicate", Smis.JOB_RETRIEVE_VOLUME,
                                *(self._c.InvokeMethod(method,
                                rs.path, **in_params)))
            except CIMError:
                self._check_for_dupe_vol(name, sys.exc_info())

        raise LsmError(ErrorNumber.NO_SUPPORT,
                       "volume-replicate not supported")

    def _get_cim_service_path(self, cim_sys_path, class_name):
        """
        Return None if not supported
        """
        try:
            cim_srvs = self._c.AssociatorNames(
                cim_sys_path,
                AssocClass='CIM_HostedService',
                ResultClass=class_name)
        except CIMError as ce:
            if ce[0] == pywbem.CIM_ERR_NOT_SUPPORTED:
                return None
            else:
                raise
        if len(cim_srvs) == 1:
            return cim_srvs[0]
        elif len(cim_srvs) == 0:
            return None
        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "_get_cim_service_path(): Got unexpected(not 1) "
                           "count of %s from cim_sys %s: %s" %
                           (class_name, cim_sys_path, cim_srvs))

    def _cim_dev_mg_path_create(self, cim_gmm_path, name, cim_vol_path,
                                vol_id):
        rc = Smis.INVOKE_FAILED
        out = None

        in_params = {
            'GroupName': name,
            'Members': [cim_vol_path],
            'Type': DMTF.MASK_GROUP_TYPE_DEV}

        cim_dev_mg_path = None
        try:
            (rc, out) = self._c.InvokeMethod('CreateGroup', cim_gmm_path,
                                             **in_params)
        except CIMError as ce:
            if ce[0] == pywbem.CIM_ERR_FAILED:
                cim_dev_mg_path = self._check_exist_cim_dev_mg(
                    name, cim_gmm_path, cim_vol_path, vol_id)
                if cim_dev_mg_path is None:
                    raise
            else:
                raise
        if cim_dev_mg_path is None:
            cim_dev_mg_path = self._wait_invoke(
                rc, out, out_key='MaskingGroup',
                expect_class='CIM_TargetMaskingGroup')

        return cim_dev_mg_path

    def _cim_tgt_mg_path_create(self, cim_sys_path, cim_gmm_path, name,
                                init_type):
        """
        Create CIM_TargetMaskingGroup
        Currently, LSM does not support target ports masking
        we will mask to all target ports.
        Return CIMInstanceName of CIM_TargetMaskingGroup
        """
        rc = Smis.INVOKE_FAILED
        out = None

        in_params = {
            'GroupName': name,
            'Type': DMTF.MASK_GROUP_TYPE_TGT}

        if init_type == AccessGroup.INIT_TYPE_WWPN:
            cim_fc_tgts = self._cim_fc_tgt_of(cim_sys_path)
            all_cim_fc_peps_path = []
            all_cim_fc_peps_path.extend(
                [self._cim_pep_path_of_fc_tgt(x.path) for x in cim_fc_tgts])
            in_params['Members'] = all_cim_fc_peps_path

        elif init_type == AccessGroup.INIT_TYPE_ISCSI_IQN:
            cim_iscsi_pgs = self._cim_iscsi_pg_of(cim_sys_path)
            in_params['Members'] = [x.path for x in cim_iscsi_pgs]
        else:
            # Already checked at the begining of this method
            pass

        cim_tgt_mg_path = None
        try:
            (rc, out) = self._c.InvokeMethod('CreateGroup', cim_gmm_path,
                                             **in_params)
        except CIMError as ce:
            if ce[0] == pywbem.CIM_ERR_FAILED:
                cim_tgt_mg_path = self._check_exist_cim_tgt_mg(name)
                if cim_tgt_mg_path is None:
                    raise
            else:
                raise

        if cim_tgt_mg_path is None:
            cim_tgt_mg_path = self._wait_invoke(
                rc, out, out_key='MaskingGroup',
                expect_class='CIM_TargetMaskingGroup')

        return cim_tgt_mg_path

    def _cim_spc_path_create(self, cim_gmm_path, cim_init_mg_path,
                             cim_tgt_mg_path, cim_dev_mg_path, name):
        in_params = {
            'ElementName': name,
            'InitiatorMaskingGroup': cim_init_mg_path,
            'TargetMaskingGroup': cim_tgt_mg_path,
            'DeviceMaskingGroup': cim_dev_mg_path,
        }

        (rc, out) = self._c.InvokeMethod('CreateMaskingView', cim_gmm_path,
                                         **in_params)

        return self._wait_invoke(
            rc, out, out_key='ProtocolController',
            expect_class='CIM_SCSIProtocolController')

    def _volume_mask_group(self, access_group, volume, flags=0):
        """
        Grant access to a volume to an group
        Use GroupMaskingMappingService.AddMembers() for Group Masking
        Use ControllerConfigurationService.ExposePaths() for Masking.
        Currently, LSM does not have a way to control which target port to
        mask.
        If CIM_TargetMaskingGroup already defined for current
        CIM_InitiatorMaskingGroup, we use that.
        If No CIM_TargetMaskingGroup exist, we create one with all possible
        target ports(all FC and FCoE port for access_group.init_type == WWPN,
        and the same to iSCSI)
        """
        cim_sys = self._get_cim_instance_by_id(
            'System', access_group.system_id, raise_error=True)

        cim_init_mg = self._cim_init_mg_of_id(access_group.id,
                                              raise_error=True)

        cim_inits = self._cim_init_of_init_mg(cim_init_mg.path)
        if len(cim_inits) == 0:
            raise LsmError(ErrorNumber.EMPTY_ACCESS_GROUP,
                           "Access group %s is empty(no member), " %
                           access_group.id +
                           "will not do volume_mask()")

        if access_group.init_type != AccessGroup.INIT_TYPE_WWPN and \
           access_group.init_type != AccessGroup.INIT_TYPE_ISCSI_IQN:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "SMI-S plugin only support iSCSI and FC/FCoE "
                           "access group volume masking, but got "
                           "access group init_type: %d" %
                           access_group.init_type)

        cim_vol = self._get_cim_instance_by_id(
            'Volume', volume.id, ['Name'],
            raise_error=True)

        cim_gmm_path = self._get_cim_service_path(
            cim_sys.path, 'CIM_GroupMaskingMappingService')

        cim_spcs_path = self._c.AssociatorNames(
            cim_init_mg.path,
            AssocClass='CIM_AssociatedInitiatorMaskingGroup',
            ResultClass='CIM_SCSIProtocolController')

        if len(cim_spcs_path) == 0:
            # We have to create the SPC and dev_mg now.
            cim_tgt_mg_path = self._cim_tgt_mg_path_create(
                cim_sys.path, cim_gmm_path, access_group.name,
                access_group.init_type)
            cim_dev_mg_path = self._cim_dev_mg_path_create(
                cim_gmm_path, access_group.name, cim_vol.path, volume.id)
            # Done when SPC created.
            self._cim_spc_path_create(
                cim_gmm_path, cim_init_mg.path, cim_tgt_mg_path,
                cim_dev_mg_path, access_group.name)
        else:
            # CIM_InitiatorMaskingGroup might have multiple SPC when having
            # many tgt_mg. It's seldom use, but possible.
            for cim_spc_path in cim_spcs_path:
                # Check whether already masked
                cim_vol_pros = self._property_list_of_id('Volume')
                cim_vols = self._c.Associators(
                    cim_spc_path,
                    AssocClass='CIM_ProtocolControllerForUnit',
                    ResultClass='CIM_StorageVolume',
                    PropertyList=cim_vol_pros)
                for cur_cim_vol in cim_vols:
                    if self._vol_id(cur_cim_vol) == volume.id:
                        # Masked.
                        return None

                # spc one-one map to dev_mg is mandatory in 1.5r6
                cim_dev_mg_path = self._c.AssociatorNames(
                    cim_spc_path,
                    AssocClass='CIM_AssociatedDeviceMaskingGroup',
                    ResultClass='CIM_DeviceMaskingGroup')[0]
                in_params = {
                    'MaskingGroup': cim_dev_mg_path,
                    'Members': [cim_vol.path],
                }
                (rc, out) = self._c.InvokeMethod(
                    'AddMembers',
                    cim_gmm_path, **in_params)
                self._wait_invoke(rc, out)
        return None

    @handle_cim_errors
    def volume_mask(self, access_group, volume, flags=0):
        """
        Grant access to a volume to an group
        """
        mask_type = self._mask_type(raise_error=True)
        # Workaround for EMC VNX/CX
        if mask_type == Smis.MASK_TYPE_GROUP:
            cim_sys = self._get_cim_instance_by_id(
                'System', volume.system_id, raise_error=True)
            if cim_sys.path.classname == 'Clar_StorageSystem':
                mask_type = Smis.MASK_TYPE_MASK

        if mask_type == Smis.MASK_TYPE_GROUP:
            return self._volume_mask_group(access_group, volume, flags)
        return self._volume_mask_old(access_group, volume, flags)

    def _volume_mask_old(self, access_group, volume, flags):

        cim_spc = self._cim_spc_of_id(access_group.id, raise_error=True)

        cim_inits = self._cim_init_of_spc(cim_spc.path)
        if len(cim_inits) == 0:
            raise LsmError(ErrorNumber.EMPTY_ACCESS_GROUP,
                           "Access group %s is empty(no member), " %
                           access_group.id +
                           "will not do volume_mask()")

        cim_sys = self._get_cim_instance_by_id(
            'System', volume.system_id, raise_error=True)
        cim_css_path = self._get_cim_service_path(
            cim_sys.path, 'CIM_ControllerConfigurationService')

        cim_vol = self._get_cim_instance_by_id(
            'Volume', volume.id, ['Name'], raise_error=True)

        da = Smis.EXPOSE_PATHS_DA_READ_WRITE

        in_params = {'LUNames': [cim_vol['Name']],
                     'ProtocolControllers': [cim_spc.path],
                     'DeviceAccesses': [pywbem.Uint16(da)]}

        (rc, out) = self._c.InvokeMethod(
            'ExposePaths',
            cim_css_path, **in_params)
        self._wait_invoke(rc, out)
        return None

    def _volume_unmask_group(self, access_group, volume):
        """
        Use CIM_GroupMaskingMappingService.RemoveMembers() against
        CIM_DeviceMaskingGroup
        If SupportedDeviceGroupFeatures does not allow empty
        DeviceMaskingGroup in SPC, we remove SPC and DeviceMaskingGroup.
        """
        cim_vol = self._get_cim_instance_by_id(
            'Volume', volume.id, raise_error=True)
        cim_sys = self._get_cim_instance_by_id(
            'System', volume.system_id, raise_error=True)

        cim_gmm_cap = self._c.Associators(
            cim_sys.path,
            AssocClass='CIM_ElementCapabilities',
            ResultClass='CIM_GroupMaskingMappingCapabilities',
            PropertyList=['SupportedDeviceGroupFeatures',
                          'SupportedSynchronousActions',
                          'SupportedAsynchronousActions'])[0]

        flag_empty_dev_in_spc = False

        if DMTF.GMM_CAP_DEV_MG_ALLOW_EMPTY_W_SPC in \
           cim_gmm_cap['SupportedDeviceGroupFeatures']:
            flag_empty_dev_in_spc = True

        if flag_empty_dev_in_spc is False:
            if ((DMTF.GMM_CAP_DELETE_SPC not in
                 cim_gmm_cap['SupportedSynchronousActions']) and
                (DMTF.GMM_CAP_DELETE_SPC not in
                 cim_gmm_cap['SupportedAsynchronousActions'])):
                raise LsmError(
                    ErrorNumber.NO_SUPPORT,
                    "volume_unmask() not supported. It requires one of these "
                    "1. support of DeleteMaskingView(). 2. allowing empty "
                    "DeviceMaskingGroup in SPC. But target SMI-S provider "
                    "does not support any of these")

        cim_gmm_path = self._get_cim_service_path(
            cim_sys.path, 'CIM_GroupMaskingMappingService')

        cim_spcs_path = self._c.AssociatorNames(
            cim_vol.path,
            AssocClass='CIM_ProtocolControllerForUnit',
            ResultClass='CIM_SCSIProtocolController')
        if len(cim_spcs_path) == 0:
            # Already unmasked
            return None

        flag_init_mg_found = False
        cur_cim_init_mg = None
        # Seaching for CIM_DeviceMaskingGroup
        for cim_spc_path in cim_spcs_path:
            cim_init_mgs = self._c.Associators(
                cim_spc_path,
                AssocClass='CIM_AssociatedInitiatorMaskingGroup',
                ResultClass='CIM_InitiatorMaskingGroup',
                PropertyList=['InstanceID'])
            for cim_init_mg in cim_init_mgs:
                if md5(cim_init_mg['InstanceID']) == access_group.id:
                    flag_init_mg_found = True
                    break

            if flag_init_mg_found:
                cim_dev_mgs_path = self._c.AssociatorNames(
                    cim_spc_path,
                    AssocClass='CIM_AssociatedDeviceMaskingGroup',
                    ResultClass='CIM_DeviceMaskingGroup')

                for cim_dev_mg_path in cim_dev_mgs_path:
                    if flag_empty_dev_in_spc is False:
                        # We have to check whether this volume is the last
                        # one in the DeviceMaskingGroup, if so, we have to
                        # delete the SPC
                        cur_cim_vols_path = self._c.AssociatorNames(
                            cim_dev_mg_path,
                            AssocClass='CIM_OrderedMemberOfCollection',
                            ResultClass='CIM_StorageVolume')
                        if len(cur_cim_vols_path) == 1:
                            # Now, delete SPC
                            in_params = {
                                'ProtocolController': cim_spc_path,
                            }
                            (rc, out) = self._c.InvokeMethod(
                                'DeleteMaskingView',
                                cim_gmm_path, **in_params)
                            self._wait_invoke(rc, out)

                    in_params = {
                        'MaskingGroup': cim_dev_mg_path,
                        'Members': [cim_vol.path],
                    }
                    (rc, out) = self._c.InvokeMethod(
                        'RemoveMembers',
                        cim_gmm_path, **in_params)
                    self._wait_invoke(rc, out)

        return None

    @handle_cim_errors
    def volume_unmask(self, access_group, volume, flags=0):
        mask_type = self._mask_type(raise_error=True)
        # Workaround for EMC VNX/CX
        if mask_type == Smis.MASK_TYPE_GROUP:
            cim_sys = self._get_cim_instance_by_id(
                'System', volume.system_id, raise_error=True)
            if cim_sys.path.classname == 'Clar_StorageSystem':
                mask_type = Smis.MASK_TYPE_MASK

        if mask_type == Smis.MASK_TYPE_GROUP:
            return self._volume_unmask_group(access_group, volume)
        return self._volume_unmask_old(access_group, volume)

    def _volume_unmask_old(self, access_group, volume):
        cim_sys = self._get_cim_instance_by_id(
            'System', access_group.system_id, raise_error=True)
        cim_ccs_path = self._get_cim_service_path(
            cim_sys.path, 'CIM_ControllerConfigurationService')

        cim_vol = self._get_cim_instance_by_id(
            'Volume', volume.id,
            property_list=['Name'], raise_error=True)

        cim_spc = self._cim_spc_of_id(access_group.id, raise_error=True)

        hide_params = {'LUNames': [cim_vol['Name']],
                       'ProtocolControllers': [cim_spc.path]}

        (rc, out) = self._c.InvokeMethod('HidePaths', cim_ccs_path,
                                         **hide_params)
        self._wait_invoke(rc, out)

        return None

    def _is_access_group(self, cim_spc):
        rc = True
        _SMIS_EMC_ADAPTER_ROLE_MASKING = 'MASK_VIEW'

        if 'EMCAdapterRole' in cim_spc:
            # Currently SNIA does not define LUN mapping.
            # EMC is using their specific way for LUN mapping which
            # expose their frontend ports as a SPC(SCSIProtocolController).
            # which we shall filter out.
            emc_adp_roles = cim_spc['EMCAdapterRole'].split(' ')
            if _SMIS_EMC_ADAPTER_ROLE_MASKING not in emc_adp_roles:
                rc = False
        return rc

    def _cim_spc_of_id(self, access_group_id, property_list=None,
                       raise_error=False):
        """
        Return CIMInstance of CIM_SCSIProtocolController
        Return None if not found.
        Raise error if not found and raise_error is True
        """
        if property_list is None:
            property_list = ['DeviceID']
        else:
            property_list = _merge_list(property_list, ['DeviceID'])

        cim_spcs = self._enumerate('CIM_SCSIProtocolController', property_list)

        for cim_spc in cim_spcs:
            if md5(cim_spc['DeviceID']) == access_group_id:
                return cim_spc

        if raise_error is True:
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "AccessGroup %s not found" % access_group_id)
        return None

    def _cim_spc_of(self, cim_sys_path, property_list=None):
        """
        Return a list of CIM_SCSIProtocolController.
        Following SNIA SMIS 'Masking and Mapping Profile':
            CIM_ComputerSystem
                |
                | CIM_HostedService
                v
            CIM_ControllerConfigurationService
                |
                | CIM_ConcreteDependency
                v
            CIM_SCSIProtocolController
        """
        cim_ccss_path = []
        rc_cim_spcs = []

        if property_list is None:
            property_list = []

        try:
            cim_ccss_path = self._c.AssociatorNames(
                cim_sys_path,
                AssocClass='CIM_HostedService',
                ResultClass='CIM_ControllerConfigurationService')
        except CIMError as ce:
            error_code = tuple(ce)[0]
            if error_code == pywbem.CIM_ERR_INVALID_CLASS or \
               error_code == pywbem.CIM_ERR_INVALID_PARAMETER:
                raise LsmError(ErrorNumber.NO_SUPPORT,
                               'AccessGroup is not supported ' +
                               'by this array')
        cim_ccs_path = None
        if len(cim_ccss_path) == 1:
            cim_ccs_path = cim_ccss_path[0]
        elif len(cim_ccss_path) == 0:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           'AccessGroup is not supported by this array')
        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "Got %d instance of " % len(cim_ccss_path) +
                           "ControllerConfigurationService from %s" %
                           cim_sys_path + " in _cim_spc_of()")
        cim_spcs = self._c.Associators(
            cim_ccs_path,
            AssocClass='CIM_ConcreteDependency',
            ResultClass='CIM_SCSIProtocolController',
            PropertyList=property_list)
        for cim_spc in cim_spcs:
            if self._is_access_group(cim_spc):
                rc_cim_spcs.append(cim_spc)
        return rc_cim_spcs

    def _cim_init_of_spc(self, cim_spc_path, property_list=None):
        """
        Take CIM_SCSIProtocolController and return a list of
        CIM_StorageHardwareID, both are CIMInstance.
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

        Method A defined in SNIA SMIS 1.6 deprecated the Method B and Method A
        saved 1 query which provide better performance.
        Hence we try method A.
        Maybe someday, we will stop trying after knowing array's supported
        SMIS version.
        """
        cim_inits = []
        if property_list is None:
            property_list = []

        if (not self.fallback_mode and
            self._profile_is_supported(SNIA.MASK_PROFILE,
                                       SNIA.SMIS_SPEC_VER_1_6,
                                       strict=False,
                                       raise_error=False)):
            return self._c.Associators(
                cim_spc_path,
                AssocClass='CIM_AssociatedPrivilege',
                ResultClass='CIM_StorageHardwareID',
                PropertyList=property_list)
        else:
            cim_aps_path = self._c.AssociatorNames(
                cim_spc_path,
                AssocClass='CIM_AuthorizedTarget',
                ResultClass='CIM_AuthorizedPrivilege')
            for cim_ap_path in cim_aps_path:
                cim_inits.extend(self._c.Associators(
                    cim_ap_path,
                    AssocClass='CIM_AuthorizedSubject',
                    ResultClass='CIM_StorageHardwareID',
                    PropertyList=property_list))
            return cim_inits

    def _cim_init_of_init_mg(self, cim_init_mg_path, property_list=None):
        """
        Use this association:
            CIM_InitiatorMaskingGroup
                    |
                    | CIM_MemberOfCollection
                    v
            CIM_StorageHardwareID
        """
        if property_list is None:
            property_list = []
        return self._c.Associators(
            cim_init_mg_path,
            AssocClass='CIM_MemberOfCollection',
            ResultClass='CIM_StorageHardwareID',
            PropertyList=property_list)

    @handle_cim_errors
    def volumes_accessible_by_access_group(self, access_group, flags=0):
        mask_type = self._mask_type(raise_error=True)
        cim_vols = []
        cim_vol_pros = self._cim_vol_pros()

        # Workaround for EMC VNX/CX
        if mask_type == Smis.MASK_TYPE_GROUP:
            cim_sys = self._get_cim_instance_by_id(
                'System', access_group.system_id, raise_error=True)
            if cim_sys.path.classname == 'Clar_StorageSystem':
                mask_type = Smis.MASK_TYPE_MASK

        if mask_type == Smis.MASK_TYPE_GROUP:
            cim_init_mg = self._cim_init_mg_of_id(
                access_group.id, raise_error=True)

            cim_spcs_path = self._c.AssociatorNames(
                cim_init_mg.path,
                AssocClass='CIM_AssociatedInitiatorMaskingGroup',
                ResultClass='CIM_SCSIProtocolController')

            for cim_spc_path in cim_spcs_path:
                cim_vols.extend(
                    self._c.Associators(
                        cim_spc_path,
                        AssocClass='CIM_ProtocolControllerForUnit',
                        ResultClass='CIM_StorageVolume',
                        PropertyList=cim_vol_pros))
        else:
            cim_spc = self._cim_spc_of_id(access_group.id, raise_error=True)
            cim_vols = self._c.Associators(
                cim_spc.path,
                AssocClass='CIM_ProtocolControllerForUnit',
                ResultClass='CIM_StorageVolume',
                PropertyList=cim_vol_pros)

        return [self._new_vol(v) for v in cim_vols]

    @handle_cim_errors
    def access_groups_granted_to_volume(self, volume, flags=0):
        rc = []
        mask_type = self._mask_type(raise_error=True)
        cim_vol = self._get_cim_instance_by_id(
            'Volume', volume.id, raise_error=True)

        # Workaround for EMC VNX/CX
        if mask_type == Smis.MASK_TYPE_GROUP:
            cim_sys = self._get_cim_instance_by_id(
                'System', volume.system_id, raise_error=True)
            if cim_sys.path.classname == 'Clar_StorageSystem':
                mask_type = Smis.MASK_TYPE_MASK

        cim_spc_pros = None
        if mask_type == Smis.MASK_TYPE_GROUP:
            cim_spc_pros = []
        else:
            cim_spc_pros = self._cim_spc_pros()

        cim_spcs = self._c.Associators(
            cim_vol.path,
            AssocClass='CIM_ProtocolControllerForUnit',
            ResultClass='CIM_SCSIProtocolController',
            PropertyList=cim_spc_pros)

        if mask_type == Smis.MASK_TYPE_GROUP:
            cim_init_mg_pros = self._cim_init_mg_pros()
            for cim_spc in cim_spcs:
                cim_init_mgs = self._c.Associators(
                    cim_spc.path,
                    AssocClass='CIM_AssociatedInitiatorMaskingGroup',
                    ResultClass='CIM_InitiatorMaskingGroup',
                    PropertyList=cim_init_mg_pros)
                rc.extend(list(self._cim_init_mg_to_lsm(x, volume.system_id)
                               for x in cim_init_mgs))
        else:
            for cim_spc in cim_spcs:
                if self._is_access_group(cim_spc):
                    rc.extend(
                        [self._cim_spc_to_lsm(cim_spc, volume.system_id)])

        return rc

    def _cim_init_mg_of_id(self, access_group_id, property_list=None,
                           raise_error=False):
        """
        Return CIMInstance of CIM_InitiatorMaskingGroup
        Return None if not found.
        Raise error if not found and raise_error is True
        """
        if property_list is None:
            property_list = ['InstanceID']
        else:
            property_list = _merge_list(property_list, ['InstanceID'])

        cim_init_mgs = self._enumerate(
            'CIM_InitiatorMaskingGroup', property_list)

        for cim_init_mg in cim_init_mgs:
            if md5(cim_init_mg['InstanceID']) == access_group_id:
                return cim_init_mg

        if raise_error is True:
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "AccessGroup %s not found" % access_group_id)
        return None

    def _cim_init_mg_of(self, cim_sys_path, property_list=None):
        """
        There is no CIM_ComputerSystem association to
        CIM_InitiatorMaskingGroup, we use this association:
            CIM_ComputerSystem
                    |
                    | CIM_HostedService
                    v
            CIM_GroupMaskingMappingService
                    |
                    | CIM_ServiceAffectsElement
                    v
            CIM_InitiatorMaskingGroup
        """
        if property_list is None:
            property_list = []

        cim_gmm_path = self._get_cim_service_path(
            cim_sys_path, 'CIM_GroupMaskingMappingService')

        return self._c.Associators(
            cim_gmm_path,
            AssocClass='CIM_ServiceAffectsElement',
            ResultClass='CIM_InitiatorMaskingGroup',
            PropertyList=property_list)

    def _mask_type(self, raise_error=False):
        """
        Return Smis.MASK_TYPE_NO_SUPPORT, MASK_TYPE_MASK or MASK_TYPE_GROUP
        For fallback_mode, return MASK_TYPE_MASK
        if 'Group Masking and Mapping' profile is supported, return
        MASK_TYPE_GROUP

        If raise_error == False, just return Smis.MASK_TYPE_NO_SUPPORT
        or, raise NO_SUPPORT error.
        """
        if self.fallback_mode:
            return Smis.MASK_TYPE_MASK
        if self._profile_is_supported(SNIA.GROUP_MASK_PROFILE,
                                      SNIA.SMIS_SPEC_VER_1_5,
                                      strict=False,
                                      raise_error=False):
            return Smis.MASK_TYPE_GROUP
        if self._profile_is_supported(SNIA.MASK_PROFILE,
                                      SNIA.SMIS_SPEC_VER_1_4,
                                      strict=False,
                                      raise_error=False):
            return Smis.MASK_TYPE_MASK
        if raise_error:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "Target SMI-S provider does not support "
                           "%s version %s or %s version %s" %
                           (SNIA.MASK_PROFILE, SNIA.SMIS_SPEC_VER_1_4,
                            SNIA.GROUP_MASK_PROFILE, SNIA.SMIS_SPEC_VER_1_5))
        return Smis.MASK_TYPE_NO_SUPPORT

    @handle_cim_errors
    def access_groups(self, search_key=None, search_value=None, flags=0):
        rc = []
        mask_type = self._mask_type(raise_error=True)

        cim_sys_pros = self._property_list_of_id('System')
        cim_syss = self._root_cim_syss(cim_sys_pros)

        cim_spc_pros = self._cim_spc_pros()
        for cim_sys in cim_syss:
            if cim_sys.path.classname == 'Clar_StorageSystem':
                # Workaround for EMC VNX/CX.
                # Even they claim support of Group M&M via
                # CIM_RegisteredProfile, but actually they don't support it.
                mask_type = Smis.MASK_TYPE_MASK

            system_id = self._sys_id(cim_sys)
            if mask_type == Smis.MASK_TYPE_GROUP:
                cim_init_mg_pros = self._cim_init_mg_pros()
                cim_init_mgs = self._cim_init_mg_of(cim_sys.path,
                                                    cim_init_mg_pros)
                rc.extend(list(self._cim_init_mg_to_lsm(x, system_id)
                               for x in cim_init_mgs))
            elif mask_type == Smis.MASK_TYPE_MASK:
                cim_spcs = self._cim_spc_of(cim_sys.path, cim_spc_pros)
                rc.extend(
                    list(self._cim_spc_to_lsm(cim_spc, system_id)
                         for cim_spc in cim_spcs))
            else:
                raise LsmError(ErrorNumber.PLUGIN_BUG,
                               "_get_cim_spc_by_id(): Got invalid mask_type: "
                               "%s" % mask_type)

        return search_property(rc, search_key, search_value)

    def _cim_init_path_check_or_create(self, cim_sys_path, init_id, init_type):
        """
        Check whether CIM_StorageHardwareID exists, if not, create new one.
        """
        cim_init = self._get_cim_instance_by_id(
            'Initiator', init_id, raise_error=False)

        if cim_init:
            return cim_init.path

        # Create new one
        dmtf_id_type = _lsm_init_type_to_dmtf(init_type)
        if dmtf_id_type is None:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "SMI-S Plugin does not support init_type %d" %
                           init_type)

        return self._cim_init_path_create(
            cim_sys_path, init_id, dmtf_id_type)

    def _cim_init_path_create(self, cim_sys_path, init_id, dmtf_id_type):
        """
        Create a CIM_StorageHardwareID.
        Return CIMInstanceName
        Raise error if failed. Return if pass.
        """
        cim_hw_srv_path = self._get_cim_service_path(
            cim_sys_path, 'CIM_StorageHardwareIDManagementService')

        in_params = {'StorageID': init_id,
                     'IDType': pywbem.Uint16(dmtf_id_type)}

        (rc, out) = self._c.InvokeMethod('CreateStorageHardwareID',
                                         cim_hw_srv_path, **in_params)
        # CreateStorageHardwareID does not allow ASYNC
        return self._wait_invoke(
            rc, out, out_key='HardwareID',
            expect_class='CIM_StorageHardwareID')

    def _ag_init_add_group(self, access_group, init_id, init_type):
        cim_sys = self._get_cim_instance_by_id(
            'System', access_group.system_id, raise_error=True)

        if cim_sys.path.classname == 'Clar_StorageSystem':
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "EMC VNX/CX require WWNN defined when adding a "
                           "new initiator which is not supported by LSM yet. "
                           "Please do it via EMC vendor specific tools.")

        cim_init_mg = self._cim_init_mg_of_id(access_group.id)

        cim_init_pros = self._property_list_of_id('Initiator')
        cim_init_pros.extend(['IDType'])
        exist_cim_inits = self._cim_init_of_init_mg(
            cim_init_mg.path, cim_init_pros)

        # Check whether already added.
        for exist_cim_init in exist_cim_inits:
            if self._init_id(exist_cim_init) == init_id:
                return copy.deepcopy(access_group)

        cim_init_path = self._cim_init_path_check_or_create(
            cim_sys.path, init_id, init_type)

        cim_gmm_path = self._get_cim_service_path(
            cim_sys.path, 'CIM_GroupMaskingMappingService')

        in_params = {
            'MaskingGroup': cim_init_mg.path,
            'Members': [cim_init_path],
        }
        (rc, out) = self._c.InvokeMethod('AddMembers',
                                         cim_gmm_path, **in_params)

        new_cim_init_mg_path = self._wait_invoke(
            rc, out, out_key='MaskingGroup',
            expect_class='CIM_InitiatorMaskingGroup')
        cim_init_mg_pros = self._cim_init_mg_pros()
        new_cim_init_mg = self._c.GetInstance(
            new_cim_init_mg_path, PropertyList=cim_init_mg_pros,
            LocalOnly=False)
        return self._cim_init_mg_to_lsm(
            new_cim_init_mg, access_group.system_id)

    @handle_cim_errors
    def access_group_initiator_add(self, access_group, init_id, init_type,
                                   flags=0):
        init_id = _lsm_init_id_to_snia(init_id)
        mask_type = self._mask_type(raise_error=True)

        if mask_type == Smis.MASK_TYPE_GROUP:
            return self._ag_init_add_group(access_group, init_id, init_type)
        else:
            return self._ag_init_add_old(access_group, init_id, init_type)

    def _ag_init_add_old(self, access_group, init_id, init_type):
        # CIM_StorageHardwareIDManagementService.CreateStorageHardwareID()
        # is mandatory since 1.4rev6
        cim_sys = self._get_cim_instance_by_id(
            'System', access_group.system_id, raise_error=True)

        if cim_sys.path.classname == 'Clar_StorageSystem':
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "EMC VNX/CX require WWNN defined when adding "
                           "new initiator which is not supported by LSM yet. "
                           "Please do it via EMC vendor specific tools. "
                           "EMC VNX does not support adding iSCSI IQN neither")

        cim_spc = self._cim_spc_of_id(access_group.id, raise_error=True)

        cim_init_pros = self._property_list_of_id('Initiator')
        cim_init_pros.extend(['IDType'])
        exist_cim_inits = self._cim_init_of_spc(cim_spc.path, cim_init_pros)

        for exist_cim_init in exist_cim_inits:
            if self._init_id(exist_cim_init) == init_id:
                return copy.deepcopy(access_group)

        # Check to see if we have this initiator already, if not we
        # create it and then add to the view.

        self._cim_init_path_check_or_create(cim_sys.path, init_id, init_type)

        cim_ccs_path = self._get_cim_service_path(
            cim_sys.path, 'CIM_ControllerConfigurationService')

        in_params = {'InitiatorPortIDs': [init_id],
                     'ProtocolControllers': [cim_spc.path]}

        (rc, out) = self._c.InvokeMethod('ExposePaths',
                                         cim_ccs_path, **in_params)
        cim_spc_path = self._wait_invoke(
            rc, out, out_key='ProtocolControllers', flag_out_array=True,
            expect_class='CIM_SCSIProtocolController')

        cim_spc_pros = self._cim_spc_pros()
        cim_spc = self._c.GetInstance(
            cim_spc_path, PropertyList=cim_spc_pros, LocalOnly=False)
        return self._cim_spc_to_lsm(cim_spc, access_group.system_id)

    def _ag_init_del_group(self, access_group, init_id):
        """
        Call CIM_GroupMaskingMappingService.RemoveMembers() against
        CIM_InitiatorMaskingGroup.
        """
        cim_sys = self._get_cim_instance_by_id(
            'System', access_group.system_id, raise_error=True)

        cim_init_mg_pros = self._cim_init_mg_pros()
        cim_init_mg = self._cim_init_mg_of_id(
            access_group.id, raise_error=True, property_list=cim_init_mg_pros)

        cim_init_pros = self._property_list_of_id('Initiator')
        cur_cim_inits = self._cim_init_of_init_mg(
            cim_init_mg.path, property_list=cim_init_pros)

        cim_init = None
        for cur_cim_init in cur_cim_inits:
            if self._init_id(cur_cim_init) == init_id:
                cim_init = cur_cim_init
                break

        if cim_init is None:
            raise LsmError(ErrorNumber.NO_STATE_CHANGE,
                "Initiator %s does not exist in defined access group %s" %
                (init_id, access_group.id))

        if len(cur_cim_inits) == 1:
            raise LsmError(ErrorNumber.LAST_INIT_IN_ACCESS_GROUP,
                "Refuse to remove last initiator from access group")

        cim_gmm_path = self._get_cim_service_path(
            cim_sys.path, 'CIM_GroupMaskingMappingService')

        # RemoveMembers from InitiatorMaskingGroup
        in_params = {
            'MaskingGroup': cim_init_mg.path,
            'Members': [cim_init.path],
        }

        (rc, out) = self._c.InvokeMethod(
            'RemoveMembers',
            cim_gmm_path, **in_params)
        self._wait_invoke(rc, out)
        return self._cim_init_mg_to_lsm(
            cim_init_mg, access_group.system_id)

    @handle_cim_errors
    def access_group_initiator_delete(self, access_group, init_id, init_type,
                                      flags=0):
        init_id = _lsm_init_id_to_snia(init_id)
        mask_type = self._mask_type(raise_error=True)

        if mask_type == Smis.MASK_TYPE_GROUP:
            return self._ag_init_del_group(access_group, init_id)
        else:
            return self._ag_init_del_old(access_group, init_id)

    def _ag_init_del_old(self, access_group, init_id):
        cim_sys = self._get_cim_instance_by_id(
            'System', access_group.system_id, raise_error=True)

        cim_spc = self._cim_spc_of_id(access_group.id, raise_error=True)

        cim_ccs_path = self._get_cim_service_path(
            cim_sys.path, 'CIM_ControllerConfigurationService')

        hide_params = {'InitiatorPortIDs': [init_id],
                       'ProtocolControllers': [cim_spc.path]}
        (rc, out) = self._c.InvokeMethod(
            'HidePaths', cim_ccs_path, **hide_params)

        self._wait_invoke(rc, out)
        return None

    @handle_cim_errors
    def job_free(self, job_id, flags=0):
        """
        Frees the resources given a job number.
        """
        cim_job = self._get_cim_instance_by_id('Job', job_id,
                                               ['DeleteOnCompletion'])

        # See if we should delete the job
        if not cim_job['DeleteOnCompletion']:
            try:
                self._c.DeleteInstance(cim_job.path)
            except CIMError:
                pass

    def _enumerate(self, class_name, property_list=None):
        """
        Please do the filter of "sytems=" in URI by yourself.
        """
        if len(self.all_vendor_namespaces) == 0:
            # We need to find out the vendor spaces.
            # We do it here to save plugin_register() time.
            # Only non-fallback mode can goes there.
            cim_syss = self._root_cim_syss()
            all_vendor_namespaces = []
            for cim_sys in cim_syss:
                if cim_sys.path.namespace not in all_vendor_namespaces:
                    all_vendor_namespaces.extend([cim_sys.path.namespace])
            self.all_vendor_namespaces = all_vendor_namespaces
        rc = []
        e_args = dict(LocalOnly=False)
        if property_list is not None:
            e_args['PropertyList'] = property_list
        for vendor_namespace in self.all_vendor_namespaces:
            rc.extend(self._c.EnumerateInstances(class_name, vendor_namespace,
                                                 **e_args))
        return rc

    @handle_cim_errors
    def disks(self, search_key=None, search_value=None, flags=0):
        """
        return all object of data.Disk.
        We are using "Disk Drive Lite Subprofile" v1.4 of SNIA SMI-S for these
        classes to create LSM Disk:
            CIM_PhysicalPackage
            CIM_DiskDrive
            CIM_StorageExtent (Primordial)
        Due to 'Multiple Computer System' profile, disks might assocated to
        sub ComputerSystem. To improve profromance of listing disks, we will
        use EnumerateInstances(). Which means we have to filter the results
        by ourself in case URI contain 'system=xxx'.
        """
        rc = []
        if not self.fallback_mode:
            self._profile_is_supported(SNIA.DISK_LITE_PROFILE,
                                       SNIA.SMIS_SPEC_VER_1_4,
                                       strict=False,
                                       raise_error=True)
        cim_disk_pros = Smis._new_disk_cim_disk_pros(flags)
        cim_disks = self._enumerate('CIM_DiskDrive', cim_disk_pros)
        for cim_disk in cim_disks:
            if self.system_list:
                if self._sys_id_child(cim_disk) not in self.system_list:
                    continue
            cim_ext_pros = Smis._new_disk_cim_ext_pros(flags)
            cim_ext = self._pri_cim_ext_of_cim_disk(cim_disk.path,
                                                    cim_ext_pros)

            rc.extend([self._new_disk(cim_disk, cim_ext)])
        return search_property(rc, search_key, search_value)

    @staticmethod
    def _new_disk_cim_disk_pros(flag=0):
        """
        Return all CIM_DiskDrive Properties needed to create a Disk object.
        """
        pros = ['OperationalStatus', 'Name', 'SystemName',
                'Caption', 'InterconnectType', 'DiskType']
        return pros

    @staticmethod
    def _new_disk_cim_ext_pros(flag=0):
        """
        Return all CIM_StorageExtent Properties needed to create a Disk
        object.
        """
        return ['BlockSize', 'NumberOfBlocks']

    def _new_disk(self, cim_disk, cim_ext):
        """
        Takes a CIM_DiskDrive and CIM_StorageExtent, returns a lsm Disk
        Assuming cim_disk and cim_ext already contained the correct
        properties.
        """
        status = Disk.STATUS_UNKNOWN
        name = ''
        block_size = Disk.BLOCK_SIZE_NOT_FOUND
        num_of_block = Disk.BLOCK_COUNT_NOT_FOUND
        disk_type = Disk.TYPE_UNKNOWN
        status_info = ''
        sys_id = self._sys_id_child(cim_disk)

        # These are mandatory
        # we do not check whether they follow the SNIA standard.
        if 'OperationalStatus' in cim_disk:
            status = DMTF.cim_disk_status_of(cim_disk['OperationalStatus'])
        if 'Name' in cim_disk:
            name = cim_disk["Name"]
        if 'BlockSize' in cim_ext:
            block_size = cim_ext['BlockSize']
        if 'NumberOfBlocks' in cim_ext:
            num_of_block = cim_ext['NumberOfBlocks']

        # SNIA SMI-S 1.4 or even 1.6 does not define anyway to find out disk
        # type.
        # Currently, EMC is following DMTF define to do so.
        if 'InterconnectType' in cim_disk:  # DMTF 2.31 CIM_DiskDrive
            disk_type = cim_disk['InterconnectType']
            if 'Caption' in cim_disk:
                # EMC VNX introduced NL_SAS disk.
                if cim_disk['Caption'] == 'NL_SAS':
                    disk_type = Disk.TYPE_NL_SAS

        if disk_type == Disk.TYPE_UNKNOWN and 'DiskType' in cim_disk:
            disk_type = \
                Smis.dmtf_disk_type_2_lsm_disk_type(cim_disk['DiskType'])

        # LSI way for checking disk type
        if not disk_type and cim_disk.classname == 'LSIESG_DiskDrive':
            cim_pes = self._c.Associators(
                cim_disk.path,
                AssocClass='CIM_SAPAvailableForElement',
                ResultClass='CIM_ProtocolEndpoint',
                PropertyList=['CreationClassName'])
            if cim_pes and cim_pes[0]:
                if 'CreationClassName' in cim_pes[0]:
                    ccn = cim_pes[0]['CreationClassName']
                    if ccn == 'LSIESG_TargetSATAProtocolEndpoint':
                        disk_type = Disk.TYPE_SATA
                    if ccn == 'LSIESG_TargetSASProtocolEndpoint':
                        disk_type = Disk.TYPE_SAS

        new_disk = Disk(self._disk_id(cim_disk), name, disk_type, block_size,
                        num_of_block, status, sys_id)

        return new_disk

    def _pri_cim_ext_of_cim_disk(self, cim_disk_path, property_list=None):
        """
        Usage:
            Find out the Primordial CIM_StorageExtent of CIM_DiskDrive
            In SNIA SMI-S 1.4 rev.6 Block book, section 11.1.1 'Base Model'
            quote:
            A disk drive is modeled as a single MediaAccessDevice (DiskDrive)
            That shall be linked to a single StorageExtent (representing the
            storage in the drive) by a MediaPresent association. The
            StorageExtent class represents the storage of the drive and
            contains its size.
        Parameter:
            cim_disk_path   # CIM_InstanceName of CIM_DiskDrive
            property_list   # a List of properties needed on returned
                            # CIM_StorageExtent
        Returns:
            cim_pri_ext     # The CIM_Instance of Primordial CIM_StorageExtent
        Exceptions:
            LsmError
                ErrorNumber.LSM_PLUGIN_BUG  # Failed to find out pri cim_ext
        """
        if property_list is None:
            property_list = ['Primordial']
        else:
            property_list = _merge_list(property_list, ['Primordial'])

        cim_exts = self._c.Associators(
            cim_disk_path,
            AssocClass='CIM_MediaPresent',
            ResultClass='CIM_StorageExtent',
            PropertyList=property_list)
        cim_exts = [p for p in cim_exts if p["Primordial"]]
        if cim_exts and cim_exts[0]:
            # As SNIA commanded, only _ONE_ Primordial CIM_StorageExtent for
            # each CIM_DiskDrive
            return cim_exts[0]
        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "Failed to find out Primordial " +
                           "CIM_StorageExtent for CIM_DiskDrive %s " %
                           cim_disk_path)

    def _cim_disk_of_pri_ext(self, cim_pri_ext_path, pros_list=None):
        """
        Follow this procedure to find out CIM_DiskDrive from Primordial
        CIM_StorageExtent:
                CIM_StorageExtent (Primordial)
                      ^
                      |
                      | CIM_MediaPresent
                      |
                      v
                CIM_DiskDrive
        """
        if pros_list is None:
            pros_list = []
        cim_disks = self._c.Associators(
            cim_pri_ext_path,
            AssocClass='CIM_MediaPresent',
            ResultClass='CIM_DiskDrive',
            PropertyList=pros_list)
        if len(cim_disks) == 1:
            return cim_disks[0]
        elif len(cim_disks) == 2:
            return None
        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "Found two or more CIM_DiskDrive associated to " +
                           "requested CIM_StorageExtent %s" %
                           cim_pri_ext_path)

    def _pool_element_type(self, cim_pool):

        element_type = 0
        unsupported = 0

        # check whether current pool support create volume or not.
        cim_sccs = self._c.Associators(
            cim_pool.path,
            AssocClass='CIM_ElementCapabilities',
            ResultClass='CIM_StorageConfigurationCapabilities',
            PropertyList=['SupportedStorageElementFeatures',
                          'SupportedStorageElementTypes'])
        # Associate StorageConfigurationCapabilities to StoragePool
        # is experimental in SNIA 1.6rev4, Block Book PDF Page 68.
        # Section 5.1.6 StoragePool, StorageVolume and LogicalDisk
        # Manipulation, Figure 9 - Capabilities Specific to a StoragePool
        if len(cim_sccs) == 1:
            cim_scc = cim_sccs[0]
            if 'SupportedStorageElementFeatures' in cim_scc:
                supported_features = cim_scc['SupportedStorageElementFeatures']

                if Smis.DMTF_SUPPORT_VOL_CREATE in supported_features:
                    element_type |= Pool.ELEMENT_TYPE_VOLUME
                if Smis.DMTF_SUPPORT_ELEMENT_EXPAND not in supported_features:
                    unsupported |= Pool.UNSUPPORTED_VOLUME_GROW
                if Smis.DMTF_SUPPORT_ELEMENT_REDUCE not in supported_features:
                    unsupported |= Pool.UNSUPPORTED_VOLUME_SHRINK

        else:
            # IBM DS 8000 does not support StorageConfigurationCapabilities
            # per pool yet. They has been informed. Before fix, use a quick
            # workaround.
            # TODO: Currently, we don't have a way to detect
            #       Pool.ELEMENT_TYPE_POOL
            #       but based on knowing definition of each vendor.
            if cim_pool.classname == 'IBMTSDS_VirtualPool' or \
               cim_pool.classname == 'IBMTSDS_ExtentPool':
                element_type = Pool.ELEMENT_TYPE_VOLUME
            elif cim_pool.classname == 'IBMTSDS_RankPool':
                element_type = Pool.ELEMENT_TYPE_POOL
            elif cim_pool.classname == 'LSIESG_StoragePool':
                element_type = Pool.ELEMENT_TYPE_VOLUME

        if 'Usage' in cim_pool:
            usage = cim_pool['Usage']

            if usage == Smis.DMTF_POOL_USAGE_UNRESTRICTED:
                element_type |= Pool.ELEMENT_TYPE_VOLUME
            if usage == Smis.DMTF_POOL_USAGE_RESERVED_FOR_SYSTEM or \
                    usage > Smis.DMTF_POOL_USAGE_DELTA:
                element_type |= Pool.ELEMENT_TYPE_SYS_RESERVED
            if usage == Smis.DMTF_POOL_USAGE_DELTA:
                # We blitz all the other elements types for this designation
                element_type = Pool.ELEMENT_TYPE_DELTA

        return element_type, unsupported

    def _profile_is_supported(self, profile_name, spec_ver, strict=False,
                              raise_error=False):
        """
        Usage:
            Check whether we support certain profile at certain SNIA
            specification version.
            When strict == False(default), profile spec version later or equal
            than  require spec_ver will also be consider as found.
            When strict == True, only defined spec_version is allowed.
            Require self.cim_rps containing all CIM_RegisteredProfile
            Will raise LsmError(ErrorNumber.NO_SUPPORT, 'xxx') if raise_error
            is True when nothing found.
        Parameter:
            profile_name    # SNIA.XXXX_PROFILE
            spec_ver        # SNIA.SMIS_SPEC_VER_XXX
            strict          # False or True. If True, only defined
                            # spec_version is consider as supported
                            # If false, will return the maximum version of
                            # spec.
            raise_error     # Raise LsmError if not found
        Returns:
            None            # Not supported.
                or
            spec_int        # Integer. Converted by _spec_ver_str_to_num()
        """
        req_ver = _spec_ver_str_to_num(spec_ver)

        max_spec_ver_str = None
        max_spec_ver = None
        for cim_rp in self.cim_rps:
            if 'RegisteredName' not in cim_rp or \
               'RegisteredVersion' not in cim_rp:
                continue
            if cim_rp['RegisteredName'] == profile_name:
                # check spec version
                cur_ver = _spec_ver_str_to_num(cim_rp['RegisteredVersion'])

                if strict and cur_ver == req_ver:
                    return cur_ver
                elif cur_ver >= req_ver:
                    if max_spec_ver is None or \
                       cur_ver > max_spec_ver:
                        max_spec_ver = cur_ver
                        max_spec_ver_str = cim_rp['RegisteredVersion']
        if (strict or max_spec_ver is None) and raise_error:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "SNIA SMI-S %s '%s' profile is not supported" %
                           (spec_ver, profile_name))

        return max_spec_ver

    def _root_cim_syss(self, property_list=None):
        """
        For fallback mode, this just enumerate CIM_ComputerSystem.
        We require vendor to implement profile registration when using
        "Multiple System Profile".
        For normal mode, this just find out the root CIM_ComputerSystem
        via:

                CIM_RegisteredProfile       # Root Profile('Array') in interop
                      |
                      | CIM_ElementConformsToProfile
                      v
                CIM_ComputerSystem          # vendor namespace

        We also assume no matter which version of root profile can lead to
        the same CIM_ComputerSystem instance.
        As CIM_ComputerSystem has no property indicate SNIA SMI-S version,
        this is assumption should work. Tested on EMC SMI-S provider which
        provide 1.4, 1.5, 1.6 root profile.
        """
        cim_scss_path = []
        id_pros = self._property_list_of_id('System', property_list)
        if property_list is None:
            property_list = id_pros
        else:
            property_list = _merge_list(property_list, id_pros)

        cim_syss = []
        if self.fallback_mode:
        # Fallback mode:
        # Find out the root CIM_ComputerSystem using the fallback method:
        #       CIM_StorageConfigurationService     # Enumerate
        #               |
        #               |   CIM_HostedService
        #               v
        #       CIM_ComputerSystem
        # If CIM_StorageConfigurationService is not support neither,
        # we enumerate CIM_ComputerSystem.
            try:
                cim_scss_path = self._c.EnumerateInstanceNames(
                    'CIM_StorageConfigurationService')
            except CIMError as e:
                # If array does not support CIM_StorageConfigurationService
                # we use CIM_ComputerSystem which is mandatory.
                # We might get some non-storage array listed as system.
                # but we would like to take that risk instead of
                # skipping basic support of old SMIS provider.
                if e[0] == pywbem.CIM_ERR_INVALID_CLASS:
                    cim_syss = self._c.EnumerateInstances(
                        'CIM_ComputerSystem',
                        PropertyList=property_list,
                        LocalOnly=False)
                else:
                    raise

            if not cim_syss:
                for cim_scs_path in cim_scss_path:
                    cim_tmp = None
                    # CIM_ComputerSystem is one-one map to
                    # CIM_StorageConfigurationService
                    cim_tmp = self._c.Associators(
                        cim_scs_path,
                        AssocClass='CIM_HostedService',
                        ResultClass='CIM_ComputerSystem',
                        PropertyList=property_list)
                    if cim_tmp and cim_tmp[0]:
                        cim_syss.extend([cim_tmp[0]])
        else:
            for cim_rp in self.cim_rps:
                if cim_rp['RegisteredName'] == SNIA.BLK_ROOT_PROFILE and\
                   cim_rp['RegisteredOrganization'] == SNIA.REG_ORG_CODE:
                    cim_syss = self._c.Associators(
                        cim_rp.path,
                        ResultClass='CIM_ComputerSystem',
                        AssocClass='CIM_ElementConformsToProfile',
                        PropertyList=property_list)
                    # Any version of 'Array' profile can get us to root
                    # CIM_ComputerSystem. startup() already has checked the
                    # 1.4 version
                    break
            if len(cim_syss) == 0:
                raise LsmError(ErrorNumber.NO_SUPPORT,
                               "Current SMI-S provider does not provide "
                               "the root CIM_ComputerSystem associated "
                               "to 'Array' CIM_RegisteredProfile. Try "
                               "add 'force_fallback_mode=yes' into URI")

        # System URI Filtering
        if self.system_list:
            needed_cim_syss = []
            for cim_sys in cim_syss:
                if self._sys_id(cim_sys) in self.system_list:
                    needed_cim_syss.extend([cim_sys])
            return needed_cim_syss
        else:
            return cim_syss

    def _fc_tgt_is_supported(self, cim_sys_path):
        """
        Return True if FC Target Port 1.4+ profile is supported.
        For fallback_mode, we call self._cim_fc_tgt_of() and do try-except
        """
        if self.fallback_mode:
            try:
                self._cim_fc_tgt_of(cim_sys_path)
            except CIMError as e:
                if e[0] == pywbem.CIM_ERR_NOT_SUPPORTED or \
                   e[0] == pywbem.CIM_ERR_INVALID_CLASS:
                    return False
            return True

        flag_fc_support = self._profile_is_supported(
            SNIA.FC_TGT_PORT_PROFILE,
            SNIA.SMIS_SPEC_VER_1_4,
            strict=False,
            raise_error=False)
        # One more check for NetApp Typo:
        #   NetApp:     'FC Target Port'
        #   SMI-S:      'FC Target Ports'
        # Bug reported.
        if not flag_fc_support:
            flag_fc_support = self._profile_is_supported(
                'FC Target Port',
                SNIA.SMIS_SPEC_VER_1_4,
                strict=False,
                raise_error=False)
        if flag_fc_support:
            return True
        else:
            return False

    def _iscsi_tgt_is_supported(self, cim_sys_path):
        """
        Return True if FC Target Port 1.4+ profile is supported.
        For fallback_mode, we call self._cim_iscsi_pg_of() and do try-except
        For fallback_mode:
        Even CIM_EthernetPort is the contral class of iSCSI Target
        Ports profile, but that class is optional. :(
        We use CIM_iSCSIProtocolEndpoint as it's a start point we are
        using in our code of target_ports().
        """
        if self.fallback_mode:
            try:
                self._cim_iscsi_pg_of(cim_sys_path)
            except CIMError as e:
                if e[0] == pywbem.CIM_ERR_NOT_SUPPORTED or \
                   e[0] == pywbem.CIM_ERR_INVALID_CLASS:
                    return False
            return True

        if self._profile_is_supported(SNIA.ISCSI_TGT_PORT_PROFILE,
                                      SNIA.SMIS_SPEC_VER_1_4,
                                      strict=False,
                                      raise_error=False):
            return True
        return False

    def _multi_sys_is_supported(self):
        """
        Return True if Multiple ComputerSystem 1.4+ profile is supported.
        For fallback_mode, always return True.
        Return False else.
        """
        if self.fallback_mode:
            return True
        flag_multi_sys_support = self._profile_is_supported(
            SNIA.MULTI_SYS_PROFILE,
            SNIA.SMIS_SPEC_VER_1_4,
            strict=False,
            raise_error=False)
        if flag_multi_sys_support:
            return True
        else:
            return False

    @staticmethod
    def _is_frontend_fc_tgt(cim_fc_tgt):
        """
        Check CIM_FCPort['UsageRestriction'] for frontend port.
        """
        dmtf_usage = cim_fc_tgt['UsageRestriction']
        if dmtf_usage == DMTF.TGT_PORT_USAGE_FRONTEND_ONLY or \
           dmtf_usage == DMTF.TGT_PORT_USAGE_UNRESTRICTED:
            return True
        return False

    def _cim_fc_tgt_of(self, cim_sys_path, property_list=None):
        """
        Get all CIM_FCPort (frontend only) from CIM_ComputerSystem and its
        leaf CIM_ComputerSystem
        """
        rc = []
        if property_list is None:
            property_list = ['UsageRestriction']
        else:
            property_list = _merge_list(property_list, ['UsageRestriction'])
        all_cim_syss_path = [cim_sys_path]
        if self._multi_sys_is_supported():
            all_cim_syss_path.extend(
                self._leaf_cim_syss_path_of(cim_sys_path))
        for cur_cim_sys_path in all_cim_syss_path:
            cur_cim_fc_tgts = self._c.Associators(
                cur_cim_sys_path,
                AssocClass='CIM_SystemDevice',
                ResultClass='CIM_FCPort',
                PropertyList=property_list)
            for cim_fc_tgt in cur_cim_fc_tgts:
                if Smis._is_frontend_fc_tgt(cim_fc_tgt):
                    rc.extend([cim_fc_tgt])
        return rc

    @staticmethod
    def _cim_fc_tgt_to_lsm(cim_fc_tgt, system_id):
        """
        Convert CIM_FCPort to Lsm.TargetPort
        """
        port_id = md5(cim_fc_tgt['DeviceID'])
        port_type = _lsm_tgt_port_type_of_cim_fc_tgt(cim_fc_tgt)
        # SNIA define WWPN string as upper, no spliter, 16 digits.
        # No need to check.
        wwpn = _hex_string_format(cim_fc_tgt['PermanentAddress'], 16, 2)
        port_name = cim_fc_tgt['ElementName']
        plugin_data = None
        return TargetPort(port_id, port_type, wwpn, wwpn, wwpn, port_name,
                          system_id, plugin_data)

    def _iscsi_node_name_of(self, cim_iscsi_pg_path):
        """
            CIM_iSCSIProtocolEndpoint
                    |
                    |
                    v
            CIM_SAPAvailableForElement
                    |
                    |
                    v
            CIM_SCSIProtocolController  # iSCSI Node

        """
        cim_spcs = self._c.Associators(
            cim_iscsi_pg_path,
            ResultClass='CIM_SCSIProtocolController',
            AssocClass='CIM_SAPAvailableForElement',
            PropertyList=['Name', 'NameFormat'])
        cim_iscsi_nodes = []
        for cim_spc in cim_spcs:
            if cim_spc.classname == 'Clar_MappingSCSIProtocolController':
                # EMC has vendor specific class which contain identical
                # properties of SPC for iSCSI node.
                continue
            if cim_spc['NameFormat'] == DMTF.SPC_NAME_FORMAT_ISCSI:
                cim_iscsi_nodes.extend([cim_spc])

        if len(cim_iscsi_nodes) == 0:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "_iscsi_node_of(): No iSCSI node "
                           "CIM_SCSIProtocolController associated to %s"
                           % cim_iscsi_pg_path)
        if len(cim_iscsi_nodes) > 1:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "_iscsi_node_of(): Got two or more iSCSI node "
                           "CIM_SCSIProtocolController associated to %s: %s"
                           % (cim_iscsi_pg_path, cim_iscsi_nodes))
        return cim_iscsi_nodes[0]['Name']

    def _cim_iscsi_pg_of(self, cim_sys_path, property_list=None):
        """
        Get all CIM_iSCSIProtocolEndpoint(Target only) from CIM_ComputerSystem
        and its leaf CIM_ComputerSystem
        """
        rc = []
        if property_list is None:
            property_list = ['Role']
        else:
            property_list = _merge_list(property_list, ['Role'])
        all_cim_syss_path = [cim_sys_path]
        if self._multi_sys_is_supported():
            all_cim_syss_path.extend(
                self._leaf_cim_syss_path_of(cim_sys_path))
        for cur_cim_sys_path in all_cim_syss_path:
            cur_cim_iscsi_pgs = self._c.Associators(
                cur_cim_sys_path,
                AssocClass='CIM_HostedAccessPoint',
                ResultClass='CIM_iSCSIProtocolEndpoint',
                PropertyList=property_list)
            for cim_iscsi_pg in cur_cim_iscsi_pgs:
                if cim_iscsi_pg['Role'] == DMTF.ISCSI_TGT_ROLE_TARGET:
                    rc.extend([cim_iscsi_pg])
        return rc

    def _cim_iscsi_pg_to_lsm(self, cim_iscsi_pg, system_id):
        """
        Return a list of TargetPort CIM_iSCSIProtocolEndpoint
        Associations:
            CIM_SCSIProtocolController  # iSCSI Node
                    ^
                    |   CIM_SAPAvailableForElement
                    |
            CIM_iSCSIProtocolEndpoint   # iSCSI Portal Group
                    |
                    |   CIM_BindsTo
                    v
            CIM_TCPProtocolEndpoint     # Need TCP port, default is 3260
                    |
                    |   CIM_BindsTo
                    v
            CIM_IPProtocolEndpoint      # Need IPv4 and IPv6 address
                    |
                    |   CIM_DeviceSAPImplementation
                    v
            CIM_EthernetPort            # Need MAC address (Optional)
        Assuming there is storage array support iSER
        (iSCSI over RDMA of Infinity Band),
        this method is only for iSCSI over TCP.
        """
        rc = []
        port_type = TargetPort.TYPE_ISCSI
        plugin_data = None
        cim_tcps = self._c.Associators(
            cim_iscsi_pg.path,
            ResultClass='CIM_TCPProtocolEndpoint',
            AssocClass='CIM_BindsTo',
            PropertyList=['PortNumber'])
        if len(cim_tcps) == 0:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "_cim_iscsi_pg_to_lsm():  "
                           "No CIM_TCPProtocolEndpoint associated to %s"
                           % cim_iscsi_pg.path)
        iscsi_node_name = self._iscsi_node_name_of(cim_iscsi_pg.path)

        for cim_tcp in cim_tcps:
            tcp_port = cim_tcp['PortNumber']
            cim_ips = self._c.Associators(
                cim_tcp.path,
                ResultClass='CIM_IPProtocolEndpoint',
                AssocClass='CIM_BindsTo',
                PropertyList=['IPv4Address', 'IPv6Address', 'SystemName',
                              'EMCPortNumber', 'IPv6AddressType'])
            for cim_ip in cim_ips:
                ipv4_addr = ''
                ipv6_addr = ''
                # 'IPv4Address', 'IPv6Address' are optional in SMI-S 1.4.
                if 'IPv4Address' in cim_ip and cim_ip['IPv4Address']:
                    ipv4_addr = cim_ip['IPv4Address']
                if 'IPv6Address' in cim_ip and cim_ip['IPv6Address']:
                    ipv6_addr = cim_ip['IPv6Address']
                # 'IPv6AddressType' is not listed in SMI-S but in DMTF CIM
                # Schema
                # Only allow IPv6 Global Unicast Address, 6to4, and Unique
                # Local Address.
                if 'IPv6AddressType' in cim_ip and cim_ip['IPv6AddressType']:
                    ipv6_addr_type = cim_ip['IPv6AddressType']
                    if ipv6_addr_type != DMTF.IPV6_ADDR_TYPE_GUA and \
                       ipv6_addr_type != DMTF.IPV6_ADDR_TYPE_6TO4 and \
                       ipv6_addr_type != DMTF.IPV6_ADDR_TYPE_ULA:
                        ipv6_addr = ''

                # NetApp is using this kind of IPv6 address
                # 0000:0000:0000:0000:0000:0000:0a10:29d5
                # even when IPv6 is not enabled on their array.
                # It's not a legal IPv6 address anyway. No need to do
                # vendor check.
                if ipv6_addr[0:29] == '0000:0000:0000:0000:0000:0000':
                    ipv6_addr = ''

                if ipv4_addr is None and ipv6_addr is None:
                    continue
                cim_eths = self._c.Associators(
                    cim_ip.path,
                    ResultClass='CIM_EthernetPort',
                    AssocClass='CIM_DeviceSAPImplementation',
                    PropertyList=['PermanentAddress', 'ElementName'])
                nics = []
                # NetApp ONTAP cluster-mode show one IP bonded to multiple
                # ethernet,
                # Not suer it's their BUG or real ethernet channel bonding.
                # Waiting reply.
                if len(cim_eths) == 0:
                    nics = [('', '')]
                else:
                    for cim_eth in cim_eths:
                        mac_addr = ''
                        port_name = ''
                        if 'PermanentAddress' in cim_eth and \
                           cim_eth["PermanentAddress"]:
                            mac_addr = cim_eth["PermanentAddress"]
                        # 'ElementName' is optional in CIM_EthernetPort
                        if 'ElementName' in cim_eth and cim_eth["ElementName"]:
                            port_name = cim_eth['ElementName']
                        nics.extend([(mac_addr, port_name)])
                for nic in nics:
                    mac_address = nic[0]
                    port_name = nic[1]
                    if mac_address:
                        # Convert to lsm require form
                        mac_address = _hex_string_format(mac_address, 12, 2)

                    if ipv4_addr:
                        network_address = "%s:%s" % (ipv4_addr, tcp_port)
                        port_id = md5("%s:%s:%s" % (mac_address,
                                                    network_address,
                                                    iscsi_node_name))
                        rc.extend(
                            [TargetPort(port_id, port_type, iscsi_node_name,
                                        network_address, mac_address,
                                        port_name, system_id, plugin_data)])
                    if ipv6_addr:
                        # DMTF or SNIA did defined the IPv6 string format.
                        # we just guess here.
                        if len(ipv6_addr) == 39:
                            ipv6_addr = ipv6_addr.replace(':', '')
                            if len(ipv6_addr) == 32:
                                ipv6_addr = _hex_string_format(
                                    ipv6_addr, 32, 4)

                        network_address = "[%s]:%s" % (ipv6_addr, tcp_port)
                        port_id = md5("%s:%s:%s" % (mac_address,
                                                    network_address,
                                                    iscsi_node_name))
                        rc.extend(
                            [TargetPort(port_id, port_type, iscsi_node_name,
                                        network_address, mac_address,
                                        port_name, system_id, plugin_data)])
        return rc

    def _leaf_cim_syss_path_of(self, cim_sys_path):
        """
        Return a list of CIMInstanceName of leaf CIM_ComputerSystem
        """
        max_loop_count = 10   # There is no storage array need 10 layer of
                              # Computer
        loop_counter = max_loop_count
        rc = []
        leaf_cim_syss_path = []
        try:
            leaf_cim_syss_path = self._c.AssociatorNames(
                cim_sys_path,
                ResultClass='CIM_ComputerSystem',
                AssocClass='CIM_ComponentCS',
                Role='GroupComponent',
                ResultRole='PartComponent')
        except CIMError as ce:
            error_code = tuple(ce)[0]
            if error_code == pywbem.CIM_ERR_INVALID_CLASS or \
               error_code == pywbem.CIM_ERR_NOT_SUPPORTED:
                return []

        if len(leaf_cim_syss_path) > 0:
            rc = leaf_cim_syss_path
            for cim_sys_path in leaf_cim_syss_path:
                rc.extend(self._leaf_cim_syss_path_of(cim_sys_path))

        return rc

    @handle_cim_errors
    def target_ports(self, search_key=None, search_value=None, flags=0):
        rc = []

        cim_fc_tgt_pros = ['UsageRestriction', 'ElementName', 'SystemName',
                           'PermanentAddress', 'PortDiscriminator',
                           'LinkTechnology', 'DeviceID']

        cim_syss = self._root_cim_syss(
            property_list=self._property_list_of_id('System'))
        for cim_sys in cim_syss:
            system_id = self._sys_id(cim_sys)
            flag_fc_support = self._fc_tgt_is_supported(cim_sys.path)
            flag_iscsi_support = self._iscsi_tgt_is_supported(cim_sys.path)

            # Assuming: if one system does not support target_ports(),
            # all systems from the same provider will not support
            # target_ports().
            if flag_fc_support is False and flag_iscsi_support is False:
                raise LsmError(ErrorNumber.NO_SUPPORT,
                               "Target SMI-S provider does not support any of"
                               "these profiles: '%s %s', '%s %s'"
                               % (SNIA.SMIS_SPEC_VER_1_4,
                                  SNIA.FC_TGT_PORT_PROFILE,
                                  SNIA.SMIS_SPEC_VER_1_4,
                                  SNIA.ISCSI_TGT_PORT_PROFILE))

            if flag_fc_support:
                # CIM_FCPort might be not belong to root cim_sys
                # In that case, CIM_FCPort['SystemName'] will not be
                # the name of root CIM_ComputerSystem.
                cim_fc_tgt_pros = ['UsageRestriction', 'ElementName',
                                   'SystemName', 'PermanentAddress',
                                   'PortDiscriminator', 'LinkTechnology',
                                   'DeviceID']
                cim_fc_tgts = self._cim_fc_tgt_of(cim_sys.path,
                                                  cim_fc_tgt_pros)
                rc.extend(
                    list(
                        Smis._cim_fc_tgt_to_lsm(x, system_id)
                        for x in cim_fc_tgts))

            if flag_iscsi_support:
                cim_iscsi_pgs = self._cim_iscsi_pg_of(cim_sys.path)
                for cim_iscsi_pg in cim_iscsi_pgs:
                    rc.extend(
                        self._cim_iscsi_pg_to_lsm(cim_iscsi_pg, system_id))

        # NetApp is sharing CIM_TCPProtocolEndpoint which
        # cause duplicate TargetPort. It's a long story, they heard my
        # bug report.
        if len(cim_syss) >= 1 and \
           cim_syss[0].classname == 'ONTAP_StorageSystem':
            id_list = []
            new_rc = []
            # We keep the original list order by not using dict.values()
            for lsm_tp in rc:
                if lsm_tp.id not in id_list:
                    id_list.extend([lsm_tp.id])
                    new_rc.extend([lsm_tp])
            rc = new_rc

        return search_property(rc, search_key, search_value)

    def _cim_init_mg_pros(self):
        return ['ElementName', 'InstanceID']

    def _cim_init_mg_to_lsm(self, cim_init_mg, system_id):
        ag_name = cim_init_mg['ElementName']
        ag_id = md5(cim_init_mg['InstanceID'])
        cim_init_pros = self._property_list_of_id('Initiator')
        cim_init_pros.extend(['IDType'])
        cim_inits = self._cim_init_of_init_mg(cim_init_mg.path, cim_init_pros)
        (init_ids, init_type) = self._cim_inits_to_lsm(cim_inits)
        return AccessGroup(ag_id, ag_name, init_ids, init_type, system_id)

    def _wait_invoke(self, rc, out, out_key=None, expect_class=None,
                     flag_out_array=False,):
        """
        Return out[out_key] if found rc == INVOKE_OK.
        For rc == INVOKE_ASYNC, we check every Smis.INVOKE_CHECK_INTERVAL
        seconds until done. Then return assocition via CIM_AffectedJobElement
        Return CIM_InstanceName
        Assuming only one CIM_InstanceName will get.
        """
        if rc == Smis.INVOKE_OK:
            if out_key is None:
                return None
            if out_key in out:
                if flag_out_array:
                    if len(out[out_key]) != 1:
                        raise LsmError(ErrorNumber.PLUGIN_BUG,
                                       "_wait_invoke(), %s is not length 1: %s"
                                       % (out_key, out.items()))
                    return out[out_key][0]
                return out[out_key]
            else:
                raise LsmError(ErrorNumber.PLUGIN_BUG,
                               "_wait_invoke(), %s not exist in out %s" %
                               (out_key, out.items()))
        elif rc == Smis.INVOKE_ASYNC:
            cim_job_path = out['Job']
            loop_counter = 0
            job_pros = ['JobState', 'PercentComplete', 'ErrorDescription',
                        'OperationalStatus']
            cim_xxxs_path = []
            while(loop_counter <= Smis._INVOKE_MAX_LOOP_COUNT):
                cim_job = self._c.GetInstance(cim_job_path,
                                              PropertyList=job_pros,
                                              LocalOnly=False)
                job_state = cim_job['JobState']
                if job_state in (Smis.JS_NEW, Smis.JS_STARTING,
                                 Smis.JS_RUNNING):
                    loop_counter += 1
                    time.sleep(Smis._INVOKE_CHECK_INTERVAL)
                    continue
                elif job_state == Smis.JS_COMPLETED:
                    if expect_class is None:
                        return None
                    cim_xxxs_path = self._c.AssociatorNames(
                        cim_job.path,
                        AssocClass='CIM_AffectedJobElement',
                        ResultClass=expect_class)
                else:
                    raise LsmError(ErrorNumber.PLUGIN_BUG,
                                   "_wait_invoke(): Got unknown job state "
                                   "%d: %s" % (job_state, cim_job.items()))
                if len(cim_xxxs_path) != 1:
                    raise LsmError(ErrorNumber.PLUGIN_BUG,
                                   "_wait_invoke(): got unexpect(not 1) "
                                   "return from CIM_AffectedJobElement: "
                                   "%s, out: %s, job: %s" %
                                   (cim_xxxs_path, out.items(),
                                    cim_job.items()))
                return cim_xxxs_path[0]
        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "_wait_invoke(): Got unexpected rc code "
                           "%d, out: %s" % (rc, out.items()))

    def _cim_pep_path_of_fc_tgt(self, cim_fc_tgt_path):
        """
        Return CIMInstanceName of CIM_SCSIProtocolEndpoint of CIM_FCPort
        In 1.4r6, it's one-to-one map.
        """
        return self._c.AssociatorNames(
            cim_fc_tgt_path,
            AssocClass='CIM_DeviceSAPImplementation',
            ResultClass='CIM_SCSIProtocolEndpoint')[0]

    def _check_exist_cim_tgt_mg(self, name):
        """
        We should do more checks[1] in stead of use it directly.
        But considering EMC VMAX is the only support vendor, make it quick
        and works could be priority 1.
        We can improve this for any bug report.

        [1] At least check whether CIM_TargetMaskingGroup is already used
            by other SPC.
        """
        cim_tgt_mgs = self._enumerate(
            class_name='CIM_TargetMaskingGroup',
            property_list=['ElementName'])
        for cim_tgt_mg in cim_tgt_mgs:
            if cim_tgt_mg['ElementName'] == name:
                return cim_tgt_mg.path

        return None

    def _check_exist_cim_dev_mg(self, name, cim_gmm_path, cim_vol_path,
                                vol_id):
        """
        This is buggy check, but it works on EMC VMAX which is only supported
        platform of Group Masking and Mapping.
        When found CIM_DeviceMaskingGroup, make sure cim_vol is included.
        """
        cim_dev_mgs = self._enumerate(
            class_name='CIM_DeviceMaskingGroup',
            property_list=['ElementName'])
        cim_dev_mg = None
        for tmp_cim_dev_mg in cim_dev_mgs:
            if tmp_cim_dev_mg['ElementName'] == name:
                cim_dev_mg = tmp_cim_dev_mg
                break
        if cim_dev_mg:
            # Check whether cim_vol included.
            cim_vol_pros = self._property_list_of_id('Volume')
            cim_vols = self._c.Associators(
                cim_dev_mg.path,
                AssocClass='CIM_OrderedMemberOfCollection',
                ResultClass='CIM_StorageVolume',
                PropertyList=cim_vol_pros)
            for cim_vol in cim_vols:
                if self._vol_id(cim_vol) == vol_id:
                    return cim_dev_mg.path

            # We should add this volume to found DeviceMaskingGroup
            in_params = {
                'MaskingGroup': cim_dev_mg.path,
                'Members': [cim_vol_path],
            }
            (rc, out) = self._c.InvokeMethod(
                'AddMembers',
                cim_gmm_path, **in_params)
            self._wait_invoke(rc, out)
            return cim_dev_mg.path

        return None

    @handle_cim_errors
    def access_group_create(self, name, init_id, init_type, system,
                            flags=0):
        """
        Using 1.5.0 'Group Masking and Mapping' profile.
        Actually, only EMC VMAX/DMX support this now(July 2014).
        Steps:
            0. Check exist SPC of init_id for duplication call and
               confliction.
            1. Create CIM_InitiatorMaskingGroup
        """
        org_init_id = init_id
        init_id = _lsm_init_id_to_snia(init_id)
        if self.fallback_mode:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "access_group_create() is not supported in "
                           "fallback mode")

        self._profile_is_supported(SNIA.GROUP_MASK_PROFILE,
                                   SNIA.SMIS_SPEC_VER_1_5,
                                   strict=False,
                                   raise_error=True)

        if init_type != AccessGroup.INIT_TYPE_WWPN and \
           init_type != AccessGroup.INIT_TYPE_ISCSI_IQN:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "SMI-S plugin only support creating FC/FCoE WWPN "
                           "and iSCSI AccessGroup")

        cim_sys = self._get_cim_instance_by_id(
            'System', system.id, raise_error=True)
        if cim_sys.path.classname == 'Clar_StorageSystem':
            # EMC VNX/CX does not support Group M&M, which incorrectly exposed
            # in CIM_RegisteredProfile
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "access_group_create() is not supported by "
                           "EMC VNX/CX which lacks the support of SNIA 1.5+ "
                           "Group Masking and Mapping profile")

        flag_fc_support = self._fc_tgt_is_supported(cim_sys.path)
        flag_iscsi_support = self._iscsi_tgt_is_supported(cim_sys.path)

        if init_type == AccessGroup.INIT_TYPE_WWPN and not flag_fc_support:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "Target SMI-S provider does not support "
                           "FC target port, which not allow creating "
                           "WWPN access group")

        if init_type == AccessGroup.INIT_TYPE_ISCSI_IQN and \
           not flag_iscsi_support:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "Target SMI-S provider does not support "
                           "iSCSI target port, which not allow creating "
                           "iSCSI IQN access group")

        cim_init_path = self._cim_init_path_check_or_create(
            cim_sys.path, init_id, init_type)

        # Create CIM_InitiatorMaskingGroup
        cim_gmm_path = self._get_cim_service_path(
            cim_sys.path, 'CIM_GroupMaskingMappingService')

        in_params = {'GroupName': name,
                     'Members': [cim_init_path],
                     'Type': DMTF.MASK_GROUP_TYPE_INIT}

        cim_init_mg_pros = self._cim_init_mg_pros()

        try:
            (rc, out) = self._c.InvokeMethod(
                'CreateGroup', cim_gmm_path, **in_params)

            cim_init_mg_path = self._wait_invoke(
                rc, out, out_key='MaskingGroup',
                expect_class='CIM_InitiatorMaskingGroup')

        except (LsmError, CIMError):
            # Check possible failure
            # 1. Initiator already exist in other group.
            #    If that group hold the same name as requested.
            #    We consider as a duplicate call, return the exist one.
            exist_cim_init_mgs = self._c.Associators(
                cim_init_path,
                AssocClass='CIM_MemberOfCollection',
                ResultClass='CIM_InitiatorMaskingGroup',
                PropertyList=cim_init_mg_pros)

            if len(exist_cim_init_mgs) != 0:
                for exist_cim_init_mg in exist_cim_init_mgs:
                    if exist_cim_init_mg['ElementName'] == name:
                        return self._cim_init_mg_to_lsm(
                            exist_cim_init_mg, system.id)

                # Name does not match.
                raise LsmError(ErrorNumber.EXISTS_INITIATOR,
                               "Initiator %s " % org_init_id +
                               "already exist in other access group "
                               "with name %s and ID: %s" %
                               (exist_cim_init_mgs[0]['ElementName'],
                                md5(exist_cim_init_mgs[0]['InstanceID'])))
            # 2. Requested name used by other group.
            #    Since 1) already checked whether any group containing
            #    requested init_id, now, it's surelly a confliction.
            exist_cim_init_mgs = self._cim_init_mg_of(
                cim_sys.path, property_list=['ElementName'])
            for exist_cim_init_mg in exist_cim_init_mgs:
                if exist_cim_init_mg['ElementName'] == name:
                    raise LsmError(ErrorNumber.NAME_CONFLICT,
                                   "Requested name %s is used by " % name +
                                   "another access group, but not containing "
                                   "requested initiator %s" % org_init_id)
            raise

        cim_init_mg = self._c.GetInstance(
            cim_init_mg_path, PropertyList=cim_init_mg_pros, LocalOnly=False)
        return self._cim_init_mg_to_lsm(cim_init_mg, system.id)

    def access_group_delete(self, access_group, flags=0):
        if self.fallback_mode:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "access_group_create() is not supported in "
                           "fallback mode")

        self._profile_is_supported(SNIA.GROUP_MASK_PROFILE,
                                   SNIA.SMIS_SPEC_VER_1_5,
                                   strict=False,
                                   raise_error=True)

        cim_init_mg = self._cim_init_mg_of_id(
            access_group.id, raise_error=True)

        # Check whether still have volume masked.
        cim_spcs_path = self._c.AssociatorNames(
            cim_init_mg.path,
            AssocClass='CIM_AssociatedInitiatorMaskingGroup',
            ResultClass='CIM_SCSIProtocolController')

        for cim_spc_path in cim_spcs_path:
            if len(self._c.AssociatorNames(
                    cim_spc_path,
                    AssocClass='CIM_ProtocolControllerForUnit',
                    ResultClass='CIM_StorageVolume')) >= 1:
                raise LsmError(ErrorNumber.IS_MASKED,
                               "Access Group %s has volume masked" %
                               access_group.id)

        cim_gmm_path = self._c.AssociatorNames(
            cim_init_mg.path,
            AssocClass='CIM_ServiceAffectsElement',
            ResultClass='CIM_GroupMaskingMappingService')[0]

        in_params = {
            'MaskingGroup': cim_init_mg.path,
            'Force': True,
        }

        (rc, out) = self._c.InvokeMethod('DeleteGroup', cim_gmm_path,
                                         **in_params)

        self._wait_invoke(rc, out)
        return None
