# Copyright (C) 2014 Red Hat, Inc.
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
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

# This file stores:
# 1. Constants of SNIA SMI-S.
# 2. Methods shared by smis_sys.py and etc:
#   * Job control
#   * Profile register
#   * WBEM actions: enumerate, associations, getinstance and etc.

from pywbem import Uint16, CIMError
import pywbem
import os
import datetime
import time
import sys

import dmtf
from lsm import LsmError, ErrorNumber, md5
from utils import (merge_list)


def _profile_register_load(wbem_conn):
    """
    Check CIM_RegisteredProfile in interop namespace.
    Return (profile_dict, root_blk_cim_rp)
    The 'profile_dict' is a dictionary like this:
        {
            # profile_name: max_version
            'Array': 1.4,
            'Block Service Profile': 1.4,
        }
    The 'root_blk_cim_rp' is the 'Array' profile of CIM_RegisteredProfile
    with highest version number.
    """
    profile_dict = {}
    root_blk_cim_rp = None
    namespace_check_list = dmtf.INTEROP_NAMESPACES

    cim_rps = []
    for namespace in namespace_check_list:
        try:
            cim_rps = wbem_conn.EnumerateInstances(
                'CIM_RegisteredProfile',
                namespace=namespace,
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
        if len(cim_rps) != 0:
            break

    if len(cim_rps) >= 1:
        for cim_rp in cim_rps:
            if cim_rp['RegisteredOrganization'] != \
               SmisCommon.SNIA_REG_ORG_CODE:
                continue
            profile_name = cim_rp['RegisteredName']
            profile_ver = cim_rp['RegisteredVersion']
            profile_ver_num = _profile_spec_ver_to_num(profile_ver)
            if profile_name in profile_dict.keys():
                exist_ver_num = _profile_spec_ver_to_num(
                    profile_dict[profile_name])
                if exist_ver_num >= profile_ver_num:
                    continue
            if profile_name == SmisCommon.SNIA_BLK_ROOT_PROFILE:
                root_blk_cim_rp = cim_rp
            profile_dict[profile_name] = profile_ver
    else:
        raise LsmError(
            ErrorNumber.NO_SUPPORT,
            "Target SMI-S provider does not support DMTF DSP1033 profile "
            "register which is mandatory for LSM")

    return profile_dict, root_blk_cim_rp


def _profile_check(profile_dict, profile_name, spec_ver,
                   raise_error=False):
    """
    Check whether we support certain profile at certain SNIA
    specification version.
    Profile spec version later or equal than require spec_ver will also be
    consider as found.
    Require profile_dict provided by SmisCommon.profile_register_load()
    Will raise LsmError(ErrorNumber.NO_SUPPORT, 'xxx') if raise_error
    is True when nothing found.
    """
    request_ver_num = _profile_spec_ver_to_num(spec_ver)
    if profile_name not in profile_dict.keys():
        if raise_error:
            raise LsmError(
                ErrorNumber.NO_SUPPORT,
                "SNIA SMI-S %s '%s' profile is not supported by " %
                (profile_name, spec_ver) +
                "target SMI-S provider")
        return False

    support_ver_num = _profile_spec_ver_to_num(profile_dict[profile_name])
    if support_ver_num < request_ver_num:
        if raise_error:
            raise LsmError(
                ErrorNumber.NO_SUPPORT,
                "SNIA SMI-S %s '%s' profile is not supported by " %
                (profile_name, spec_ver) +
                "target SMI-S provider. Only version %s is supported" %
                profile_dict[profile_name])
        else:
            return False
    return True


def _profile_spec_ver_to_num(spec_ver_str):
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


class SmisCommon(object):
    # Even many CIM_XXX_Service in DMTF shared the same return value
    # definition as SNIA do, but there is no DMTF standard motioned
    # InvokeMethod() should follow that list of return value.
    # We use SNIA definition here.
    # SNIA 1.6 rev4 Block book, BSP 5.5.3.12 Return Values section.
    SNIA_INVOKE_OK = 0
    SNIA_INVOKE_NOT_SUPPORTED = 1
    SNIA_INVOKE_FAILED = 4
    SNIA_INVOKE_ASYNC = 4096

    SNIA_BLK_ROOT_PROFILE = 'Array'
    SNIA_BLK_SRVS_PROFILE = 'Block Services'
    SNIA_DISK_LITE_PROFILE = 'Disk Drive Lite'
    SNIA_MULTI_SYS_PROFILE = 'Multiple Computer System'
    SNIA_MASK_PROFILE = 'Masking and Mapping'
    SNIA_GROUP_MASK_PROFILE = 'Group Masking and Mapping'
    SNIA_FC_TGT_PORT_PROFILE = 'FC Target Ports'
    SNIA_ISCSI_TGT_PORT_PROFILE = 'iSCSI Target Ports'
    SNIA_SPARE_DISK_PROFILE = 'Disk Sparing'
    SMIS_SPEC_VER_1_1 = '1.1'
    SMIS_SPEC_VER_1_4 = '1.4'
    SMIS_SPEC_VER_1_5 = '1.5'
    SMIS_SPEC_VER_1_6 = '1.6'
    SNIA_REG_ORG_CODE = Uint16(11)
    _MEGARAID_NAMESPACE = 'root/LsiMr13'
    _NETAPP_E_NAMESPACE = 'root/LsiArray13'
    _PRODUCT_MEGARAID = 'LSI MegaRAID'
    _PRODUCT_NETAPP_E = 'NetApp-E'

    JOB_RETRIEVE_NONE = 0
    JOB_RETRIEVE_VOLUME = 1
    JOB_RETRIEVE_VOLUME_CREATE = 2

    IAAN_WBEM_HTTP_PORT = 5988
    IAAN_WBEM_HTTPS_PORT = 5989

    _INVOKE_MAX_LOOP_COUNT = 60
    _INVOKE_CHECK_INTERVAL = 5

    def __init__(self, url, username, password,
                 namespace=dmtf.DEFAULT_NAMESPACE,
                 no_ssl_verify=False, debug_path=None, system_list=None):
        self._wbem_conn = None
        self._profile_dict = {}
        self.root_blk_cim_rp = None    # For root_cim_
        self._vendor_product = None     # For vendor workaround codes.
        self.system_list = system_list
        self._debug_path = debug_path

        if namespace is None:
            namespace = dmtf.DEFAULT_NAMESPACE

        self._wbem_conn = pywbem.WBEMConnection(
            url, (username, password), namespace)
        if no_ssl_verify:
            try:
                self._wbem_conn = pywbem.WBEMConnection(
                    url, (username, password), namespace,
                    no_verification=True)
            except TypeError:
                # pywbem is not holding fix from
                # https://bugzilla.redhat.com/show_bug.cgi?id=1039801
                pass

        if debug_path is not None:
            self._wbem_conn.debug = True

        if namespace.lower() == SmisCommon._MEGARAID_NAMESPACE.lower():
            # Skip profile register check on MegaRAID for better performance.
            # MegaRAID SMI-S profile support status will not change for a
            # while.
            self._profile_dict = {
                # Provide a fake profile support status to pass the check.
                SmisCommon.SNIA_BLK_ROOT_PROFILE: SmisCommon.SMIS_SPEC_VER_1_4,
                SmisCommon.SNIA_BLK_SRVS_PROFILE: SmisCommon.SMIS_SPEC_VER_1_4,
                SmisCommon.SNIA_DISK_LITE_PROFILE:
                SmisCommon.SMIS_SPEC_VER_1_4,
            }
            self._vendor_product = SmisCommon._PRODUCT_MEGARAID
        else:
            (self._profile_dict, self.root_blk_cim_rp) = \
                _profile_register_load(self._wbem_conn)

        if namespace.lower() == SmisCommon._NETAPP_E_NAMESPACE.lower():
            self._vendor_product = SmisCommon._PRODUCT_NETAPP_E
            # NetApp-E indicates they support 1.0 version of FC/iSCSI target
            # But 1.0 does not define thoese profiles. Forcly change
            # support version to 1.4
            self._profile_dict[SmisCommon.SNIA_FC_TGT_PORT_PROFILE] = \
                SmisCommon.SMIS_SPEC_VER_1_4
            self._profile_dict[SmisCommon.SNIA_ISCSI_TGT_PORT_PROFILE] = \
                SmisCommon.SMIS_SPEC_VER_1_4
            # NetApp-E indicates support of Mask and Mapping 1.2. But
            # SNIA website link for 1.2 broken. Change it to 1.4.
            self._profile_dict[SmisCommon.SNIA_MASK_PROFILE] = \
                SmisCommon.SMIS_SPEC_VER_1_4

        # Check 'Array' 1.4 support status.
        _profile_check(
            self._profile_dict, SmisCommon.SNIA_BLK_ROOT_PROFILE,
            SmisCommon.SMIS_SPEC_VER_1_4, raise_error=True)

    def profile_check(self, profile_name, spec_ver, raise_error=False):
        """
        Usage:
            Check whether we support certain profile at certain SNIA
            specification version or later version.
            Will raise LsmError(ErrorNumber.NO_SUPPORT, 'xxx') if raise_error
            is True when nothing found.
        Parameter:
            profile_name    # SmisCommon.SNIA_XXXX_PROFILE
            spec_ver        # SmisCommon.SMIS_SPEC_VER_XXX
            raise_error     # Raise LsmError if not found
        Returns:
            True
                or
            False
        """
        return _profile_check(
            self._profile_dict, profile_name, spec_ver, raise_error)

    def _vendor_namespace(self):
        if self.root_blk_cim_rp:
            cim_syss_path = self._wbem_conn.AssociatorNames(
                self.root_blk_cim_rp.path,
                ResultClass='CIM_ComputerSystem',
                AssocClass='CIM_ElementConformsToProfile')
            if len(cim_syss_path) == 0:
                raise LsmError(
                    ErrorNumber.NO_SUPPORT,
                    "Target SMI-S provider does not support any "
                    "CIM_ComputerSystem for SNIA SMI-S '%s' profile" %
                    SmisCommon.SNIA_BLK_ROOT_PROFILE)
            return cim_syss_path[0].namespace
        else:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "_vendor_namespace(): self.root_blk_cim_rp not set yet")

    def EnumerateInstances(self, ClassName, namespace=None, **params):
        if self._wbem_conn.default_namespace in dmtf.INTEROP_NAMESPACES:
            # We have to enumerate in vendor namespace
            self._wbem_conn.default_namespace = self._vendor_namespace()
        params['LocalOnly'] = False
        return self._wbem_conn.EnumerateInstances(
            ClassName, namespace, **params)

    def EnumerateInstanceNames(self, ClassName, namespace=None, **params):
        if self._wbem_conn.default_namespace in dmtf.INTEROP_NAMESPACES:
            # We have to enumerate in vendor namespace
            self._wbem_conn.default_namespace = self._vendor_namespace()
        params['LocalOnly'] = False
        return self._wbem_conn.EnumerateInstanceNames(
            ClassName, namespace, **params)

    def Associators(self, ObjectName, **params):
        return self._wbem_conn.Associators(ObjectName, **params)

    def AssociatorNames(self, ObjectName, **params):
        return self._wbem_conn.AssociatorNames(ObjectName, **params)

    def GetInstance(self, InstanceName, **params):
        params['LocalOnly'] = False
        return self._wbem_conn.GetInstance(InstanceName, **params)

    def DeleteInstance(self, InstanceName, **params):
        return self._wbem_conn.DeleteInstance(InstanceName, **params)

    def References(self, ObjectName, **params):
        return self._wbem_conn.References(ObjectName, **params)

    def is_megaraid(self):
        return self._vendor_product == SmisCommon._PRODUCT_MEGARAID

    def is_netappe(self):
        return self._vendor_product == SmisCommon._PRODUCT_NETAPP_E

    @staticmethod
    def cim_job_pros():
        return ['InstanceID']

    def cim_job_of_job_id(self, job_id, property_list=None):
        """
        Return CIM_ConcreteJob for given job_id.
        """
        if property_list is None:
            property_list = SmisCommon.cim_job_pros()
        else:
            property_list = merge_list(
                property_list, SmisCommon.cim_job_pros())

        cim_jobs = self.EnumerateInstances(
            'CIM_ConcreteJob',
            PropertyList=property_list)
        real_job_id = SmisCommon.parse_job_id(job_id)[0]
        for cim_job in cim_jobs:
            if md5(cim_job['InstanceID']) == real_job_id:
                return cim_job

        raise LsmError(
            ErrorNumber.NOT_FOUND_JOB,
            "Job %s not found" % job_id)

    @staticmethod
    def _job_id_of_cim_job(cim_job, retrieve_data, method_data):
        """
        Return the MD5 has of CIM_ConcreteJob['InstanceID'] in conjunction
        with '@%s' % retrieve_data
        retrieve_data should be SmisCommon.JOB_RETRIEVE_NONE or
        SmisCommon.JOB_RETRIEVE_VOLUME or etc
        method_data is any string a method would like store for error
        handling by job_status().
        """
        return "%s@%d@%s" % (
            md5(cim_job['InstanceID']), int(retrieve_data), str(method_data))

    @staticmethod
    def parse_job_id(job_id):
        """
        job_id is assembled by a md5 string, retrieve_data and method_data
        This method will split it and return
        (md5_str, retrieve_data, method_data)
        """
        tmp_list = job_id.split('@', 3)
        md5_str = tmp_list[0]
        retrieve_data = SmisCommon.JOB_RETRIEVE_NONE
        method_data = None
        if len(tmp_list) == 3:
            retrieve_data = int(tmp_list[1])
            method_data = tmp_list[2]
        return md5_str, retrieve_data, method_data

    def _dump_wbem_xml(self, file_prefix):
        """
        When debugging issues with providers it's helpful to have
        the xml request/reply to give to provider developers.
        """
        try:
            if self._debug_path is not None:
                if not os.path.exists(self._debug_path):
                    os.makedirs(self._debug_path)

                if os.path.isdir(self._debug_path):
                    debug_fn = "%s_%s" % (
                        file_prefix, datetime.datetime.now().isoformat())
                    debug_full = os.path.join(
                        self._debug_path, debug_fn)

                    # Dump the request & reply to a file
                    with open(debug_full, 'w') as d:
                        d.write("REQUEST:\n%s\n\nREPLY:\n%s\n" %
                                (self._wbem_conn.last_request,
                                 self._wbem_conn.last_reply))
        except Exception:
            # Lets not bother to try and report that we couldn't log the debug
            # data when we are most likely already in a bad spot
            pass

    def invoke_method(self, cmd, cim_path, in_params, out_handler=None,
                      error_handler=None, retrieve_data=None,
                      method_data=None):
        """
        cmd
            A string of command, example:
                'CreateOrModifyElementFromStoragePool'
        cim_path
            the CIMInstanceName, example:
                CIM_StorageConfigurationService.path
        in_params
            A dictionary of input parameter, example:
                {'ElementName': volume_name,
                 'ElementType': dmtf_element_type,
                 'InPool': cim_pool_path,
                 'Size': pywbem.Uint64(size_bytes)}
        out_handler
            A reference to a method to parse output, example:
                self._new_vol_from_name
        error_handler
            A reference to a method to handle all exceptions.
        retrieve_data
            SmisCommon.JOB_RETRIEVE_XXX, it will be used only
            when a ASYNC job has been created.
        method_data
            A string which will be stored in job_id, it could be used by
            job_status() to do error checking.
        """
        if retrieve_data is None:
            retrieve_data = SmisCommon.JOB_RETRIEVE_NONE
        try:
            (rc, out) = self._wbem_conn.InvokeMethod(
                cmd, cim_path, **in_params)

            # Check to see if operation is done
            if rc == SmisCommon.SNIA_INVOKE_OK:
                if out_handler is None:
                    return None, None
                else:
                    return None, out_handler(out)

            elif rc == SmisCommon.SNIA_INVOKE_ASYNC:
                # We have an async operation
                job_id = SmisCommon._job_id_of_cim_job(
                    out['Job'], retrieve_data, method_data)
                return job_id, None
            elif rc == SmisCommon.SNIA_INVOKE_NOT_SUPPORTED:
                raise LsmError(
                    ErrorNumber.NO_SUPPORT,
                    'SMI-S error code indicates operation not supported')
            else:
                self._dump_wbem_xml(cmd)
                raise LsmError(ErrorNumber.PLUGIN_BUG,
                               "Error: %s rc= %s" % (cmd, str(rc)))

        except Exception:
            exc_info = sys.exc_info()
            # Make sure to save off current exception as we could cause
            # another when trying to dump debug data.
            self._dump_wbem_xml(cmd)
            if error_handler is not None:
                error_handler(self, method_data, exc_info)
            else:
                raise

    def invoke_method_wait(self, cmd, cim_path, in_params,
                           out_key=None, expect_class=None,
                           flag_out_array=False):
        """
        InvokeMethod and wait it until done.
        Return a CIMInstanceName from out[out_key] or from cim_job:
            CIM_ConcreteJob
                |
                | CIM_AffectedJobElement
                v
            CIMInstanceName # expect_class
        If flag_out_array is True, return the first element of out[out_key].
        """
        cim_job = dict()
        (rc, out) = self._wbem_conn.InvokeMethod(cmd, cim_path, **in_params)

        try:
            if rc == SmisCommon.SNIA_INVOKE_OK:
                if out_key is None:
                    return None
                if out_key in out:
                    if flag_out_array:
                        if len(out[out_key]) == 1:
                            return out[out_key][0]
                        else:
                            raise LsmError(
                                ErrorNumber.PLUGIN_BUG,
                                "invoke_method_wait(), output contains %d " %
                                len(out[out_key]) +
                                "elements: %s" % out[out_key])
                    return out[out_key]
                else:
                    raise LsmError(ErrorNumber.PLUGIN_BUG,
                                   "invoke_method_wait(), %s not exist "
                                   "in out %s" % (out_key, out.items()))

            elif rc == SmisCommon.SNIA_INVOKE_ASYNC:
                cim_job_path = out['Job']
                loop_counter = 0
                job_pros = ['JobState', 'ErrorDescription',
                            'OperationalStatus']
                cim_xxxs_path = []
                while loop_counter <= SmisCommon._INVOKE_MAX_LOOP_COUNT:
                    cim_job = self.GetInstance(cim_job_path,
                                               PropertyList=job_pros)
                    job_state = cim_job['JobState']
                    if job_state in (dmtf.JOB_STATE_NEW,
                                     dmtf.JOB_STATE_STARTING,
                                     dmtf.JOB_STATE_RUNNING):
                        loop_counter += 1
                        time.sleep(SmisCommon._INVOKE_CHECK_INTERVAL)
                        continue
                    elif job_state == dmtf.JOB_STATE_COMPLETED:
                        if not SmisCommon.cim_job_completed_ok(cim_job):
                            raise LsmError(
                                ErrorNumber.PLUGIN_BUG,
                                str(cim_job['ErrorDescription']))
                        if expect_class is None:
                            return None
                        cim_xxxs_path = self.AssociatorNames(
                            cim_job.path,
                            AssocClass='CIM_AffectedJobElement',
                            ResultClass=expect_class)
                        break
                    else:
                        raise LsmError(
                            ErrorNumber.PLUGIN_BUG,
                            "invoke_method_wait(): Got unknown job state "
                            "%d: %s" % (job_state, cim_job.items()))

                if loop_counter > SmisCommon._INVOKE_MAX_LOOP_COUNT:
                    raise LsmError(
                        ErrorNumber.TIMEOUT,
                        "The job generated by %s() failed to finish in %ds" %
                        (cmd,
                         SmisCommon._INVOKE_CHECK_INTERVAL *
                         SmisCommon._INVOKE_MAX_LOOP_COUNT))

                if len(cim_xxxs_path) == 1:
                    return cim_xxxs_path[0]
                else:
                    raise LsmError(
                        ErrorNumber.PLUGIN_BUG,
                        "invoke_method_wait(): got unexpected(not 1) "
                        "return from CIM_AffectedJobElement: "
                        "%s, out: %s, job: %s" %
                        (cim_xxxs_path, out.items(), cim_job.items()))
            else:
                self._dump_wbem_xml(cmd)
                raise LsmError(
                    ErrorNumber.PLUGIN_BUG,
                    "invoke_method_wait(): Got unexpected rc code "
                    "%d, out: %s" % (rc, out.items()))
        except Exception:
            exc_info = sys.exc_info()
            # Make sure to save off current exception as we could cause
            # another when trying to dump debug data.
            self._dump_wbem_xml(cmd)
            raise exc_info[0], exc_info[1], exc_info[2]

    def _cim_srv_of_sys_id(self, srv_name, sys_id, raise_error):
        property_list = ['SystemName']

        try:
            cim_srvs = self.EnumerateInstances(
                srv_name,
                PropertyList=property_list)
            for cim_srv in cim_srvs:
                if cim_srv['SystemName'] == sys_id:
                    return cim_srv
        except CIMError:
            if raise_error:
                raise
            else:
                return None

        if raise_error:
            raise LsmError(
                ErrorNumber.NO_SUPPORT,
                "Cannot find any '%s' for requested system ID" % srv_name)
        return None

    def cim_scs_of_sys_id(self, sys_id, raise_error=True):
        """
        Return a CIMInstance of CIM_StorageConfigurationService for given
        system id.
        Using 'SystemName' property as system id of a service which is defined
        by DMTF CIM_Service.
        """
        return self._cim_srv_of_sys_id(
            'CIM_StorageConfigurationService', sys_id, raise_error)

    def cim_rs_of_sys_id(self, sys_id, raise_error=True):
        """
        Return a CIMInstance of CIM_ReplicationService for given system id.
        Using 'SystemName' property as system id of a service which is defined
        by DMTF CIM_Service.
        """
        return self._cim_srv_of_sys_id(
            'CIM_ReplicationService', sys_id, raise_error)

    def cim_gmms_of_sys_id(self, sys_id, raise_error=True):
        """
        Return a CIMInstance of CIM_GroupMaskingMappingService for given system
        id.
        Using 'SystemName' property as system id of a service which is defined
        by DMTF CIM_Service.
        """
        return self._cim_srv_of_sys_id(
            'CIM_GroupMaskingMappingService', sys_id, raise_error)

    def cim_ccs_of_sys_id(self, sys_id, raise_error=True):
        """
        Return a CIMInstance of CIM_ControllerConfigurationService for given
        system id.
        Using 'SystemName' property as system id of a service which is defined
        by DMTF CIM_Service.
        """
        return self._cim_srv_of_sys_id(
            'CIM_ControllerConfigurationService', sys_id, raise_error)

    def cim_hwms_of_sys_id(self, sys_id, raise_error=True):
        """
        Return a CIMInstance of CIM_StorageHardwareIDManagementService for
        given system id.
        Using 'SystemName' property as system id of a service which is defined
        by DMTF CIM_Service.
        """
        return self._cim_srv_of_sys_id(
            'CIM_StorageHardwareIDManagementService', sys_id, raise_error)

    @staticmethod
    def cim_job_completed_ok(status):
        """
        Given a concrete job instance, check the operational status.  This
        is a little convoluted as different SMI-S proxies return the values in
        different positions in list :-)
        """
        rc = False
        op = status['OperationalStatus']

        if (len(op) > 1 and
            ((op[0] == dmtf.OP_STATUS_OK and
              op[1] == dmtf.OP_STATUS_COMPLETED) or
             (op[0] == dmtf.OP_STATUS_COMPLETED and
              op[1] == dmtf.OP_STATUS_OK))):
            rc = True

        return rc
