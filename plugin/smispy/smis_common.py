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
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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

from dmtf import DMTF
from lsm import LsmError, ErrorNumber
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
    with hightest version number.
    """
    profile_dict = {}
    root_blk_cim_rp = None
    namespace_check_list = DMTF.INTEROP_NAMESPACES

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
    SNIA_BLK_ROOT_PROFILE = 'Array'
    SNIA_BLK_SRVS_PROFILE = 'Block Services'
    SNIA_DISK_LITE_PROFILE = 'Disk Drive Lite'
    SNIA_MULTI_SYS_PROFILE = 'Multiple Computer System'
    SNIA_MASK_PROFILE = 'Masking and Mapping'
    SNIA_GROUP_MASK_PROFILE = 'Group Masking and Mapping'
    SNIA_FC_TGT_PORT_PROFILE = 'FC Target Ports'
    SNIA_ISCSI_TGT_PORT_PROFILE = 'iSCSI Target Ports'
    SMIS_SPEC_VER_1_4 = '1.4'
    SMIS_SPEC_VER_1_5 = '1.5'
    SMIS_SPEC_VER_1_6 = '1.6'
    SNIA_REG_ORG_CODE = Uint16(11)
    _MEGARAID_NAMESPACE = 'root/LsiMr13'
    _NETAPP_E_NAMESPACE = 'root/LsiArray13'
    _PRODUCT_MEGARAID = 'LSI MegaRAID'
    _PRODUCT_NETAPP_E = 'NetApp-E'

    def __init__(self, url, username, password,
                 namespace=DMTF.DEFAULT_NAMESPACE,
                 no_ssl_verify=False, debug=False):
        self._wbem_conn = None
        self._profile_dict = {}
        self.root_blk_cim_rp = None    # For root_cim_
        self._vendor_product = None     # For vendor workaround codes.

        if namespace is None:
            namespace = DMTF.DEFAULT_NAMESPACE

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

        self._wbem_conn.debug = debug

        if namespace.lower() == SmisCommon._MEGARAID_NAMESPACE.lower():
        # Skip profile register check on MegaRAID for better performance.
        # MegaRAID SMI-S profile support status will not change for a while.
            self._profile_dict = {
                # Provide a fake profile support status to pass the check.
                SmisCommon.SNIA_BLK_ROOT_PROFILE: \
                    SmisCommon.SMIS_SPEC_VER_1_4,
                SmisCommon.SNIA_BLK_SRVS_PROFILE: \
                    SmisCommon.SMIS_SPEC_VER_1_4,
                SmisCommon.SNIA_DISK_LITE_PROFILE: \
                    SmisCommon.SMIS_SPEC_VER_1_4,
            }
            self._vendor_product = SmisCommon._PRODUCT_MEGARAID
        else:
            (self._profile_dict, self.root_blk_cim_rp) = \
                _profile_register_load(self._wbem_conn)

        if namespace.lower() == SmisCommon._NETAPP_E_NAMESPACE.lower():
            self._vendor_product = SmisCommon._PRODUCT_NETAPP_E

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

    def get_class_instance(self, class_name, prop_name, prop_value,
                            raise_error=True, property_list=None):
        """
        Gets an instance of a class that optionally matches a specific
        property name and value
        """
        instances = None
        if property_list is None:
            property_list = [prop_name]
        else:
            property_list = merge_list(property_list, [prop_name])

        try:
            cim_xxxs = self.EnumerateInstances(
                class_name, PropertyList=property_list)
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

    def get_cim_service_path(self, cim_sys_path, class_name):
        """
        Return None if not supported
        """
        try:
            cim_srvs = self.AssociatorNames(
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
        if self._wbem_conn.default_namespace in DMTF.INTEROP_NAMESPACES:
            # We have to enumerate in vendor namespace
            self._wbem_conn.default_namespace = self._vendor_namespace()
        params['LocalOnly']=False
        return self._wbem_conn.EnumerateInstances(
            ClassName, namespace, **params)

    def EnumerateInstanceNames(self, ClassName, namespace=None, **params):
        if self._wbem_conn.default_namespace in DMTF.INTEROP_NAMESPACES:
            # We have to enumerate in vendor namespace
            self._wbem_conn.default_namespace = self._vendor_namespace()
        params['LocalOnly']=False
        return self._wbem_conn.EnumerateInstanceNames(
            ClassName, namespace, **params)

    def Associators(self, ObjectName, **params):
        return self._wbem_conn.Associators(ObjectName, **params)

    def AssociatorNames(self, ObjectName, **params):
        return self._wbem_conn.AssociatorNames(ObjectName, **params)

    def GetInstance(self, InstanceName, **params):
        params['LocalOnly']=False
        return self._wbem_conn.GetInstance(InstanceName, **params)

    def InvokeMethod(self, MethodName, ObjectName, **params):
        return self._wbem_conn.InvokeMethod(MethodName, ObjectName, **params)

    def DeleteInstance(self, InstanceName, **params):
        return self._wbem_conn.DeleteInstance(InstanceName, **params)

    def References(self, ObjectName, **params):
        return self._wbem_conn.References(ObjectName, **params)

    @property
    def last_request(self):
        return self._wbem_conn.last_request

    @property
    def last_reply(self):
        return self._wbem_conn.last_reply

    def is_megaraid(self):
        return self._vendor_product == SmisCommon._PRODUCT_MEGARAID

    def is_netappe(self):
        return self._vendor_product == SmisCommon._PRODUCT_NETAPP_E
