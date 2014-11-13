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
This module intends to provide independent methods related to lsm.Volume and
CIM_StorageVolume.
"""

import re
import sys

from lsm import md5, Volume, LsmError, ErrorNumber
from lsm.plugin.smispy.utils import (
    merge_list, cim_path_to_path_str, path_str_to_cim_path)
from lsm.plugin.smispy import dmtf


def cim_vol_id_pros():
    """
    Return the property of CIM_StorageVolume required to generate
    lsm.Volume.id
    """
    return ['SystemName', 'DeviceID']


def vol_id_of_cim_vol(cim_vol):
    """
    Get lsm.Volume.id from CIM_StorageVolume['DeviceID'] and ['SystemName']
    """
    if 'SystemName' not in cim_vol or 'DeviceID' not in cim_vol:
        raise LsmError(
            ErrorNumber.PLUGIN_BUG,
            "vol_id_of_cim_vol(): Got cim_vol with no "
            "SystemName or DeviceID property: %s, %s" %
            (cim_vol.path, cim_vol.items()))

    return md5("%s%s" % (cim_vol['SystemName'], cim_vol['DeviceID']))


def cim_vol_pros():
    """
    Return the PropertyList required for creating new lsm.Volume.
    """
    props = ['ElementName', 'NameFormat',
             'NameNamespace', 'BlockSize', 'NumberOfBlocks', 'Name',
             'OtherIdentifyingInfo', 'IdentifyingDescriptions', 'Usage',
             'OtherNameFormat', 'OtherNameNamespace']
    props.extend(cim_vol_id_pros())
    return props


def cim_vol_of_cim_pool_path(smis_common, cim_pool_path, property_list=None):
    """
    Use this association to get a list of CIM_StorageVolume:
        CIM_StoragePool
            |
            |   CIM_AllocatedFromStoragePool
            |
            v
        CIM_StorageVolume
    CIM_StorageVolume['Usage'] == dmtf.VOL_USAGE_SYS_RESERVED will be filtered
    out.
    Return a list of CIM_StorageVolume.
    """
    if property_list is None:
        property_list = ['Usage']
    else:
        property_list = merge_list(property_list, ['Usage'])

    cim_vols = smis_common.Associators(
        cim_pool_path,
        AssocClass='CIM_AllocatedFromStoragePool',
        ResultClass='CIM_StorageVolume',
        PropertyList=property_list)

    needed_cim_vols = []
    for cim_vol in cim_vols:
        if 'Usage' not in cim_vol or \
           cim_vol['Usage'] != dmtf.VOL_USAGE_SYS_RESERVED:
            needed_cim_vols.append(cim_vol)
    return needed_cim_vols


def _vpd83_in_cim_vol_name(cim_vol):
    """
    We require NAA Type 3 VPD83 address:
    Only this is allowed when storing VPD83 in cim_vol["Name"]:
     * NameFormat = NAA(9), NameNamespace = VPD83Type3(2)
    """
    if not ('NameFormat' in cim_vol and
            'NameNamespace' in cim_vol and
            'Name' in cim_vol):
        return None
    name_format = cim_vol['NameFormat']
    name_space = cim_vol['NameNamespace']
    name = cim_vol['Name']
    if not (name_format and name_space and name):
        return None

    if name_format == dmtf.VOL_NAME_FORMAT_NNA and \
       name_space == dmtf.VOL_NAME_SPACE_VPD83_TYPE3:
        return name


def _vpd83_in_cim_vol_otherinfo(cim_vol):
    """
    IdentifyingDescriptions[] shall contain "NAA;VPD83Type3".
    Will return the vpd_83 value if found
    """
    if not ("IdentifyingDescriptions" in cim_vol and
            "OtherIdentifyingInfo" in cim_vol):
        return None

    id_des = cim_vol["IdentifyingDescriptions"]
    other_info = cim_vol["OtherIdentifyingInfo"]
    if not (isinstance(cim_vol["IdentifyingDescriptions"], list) and
            isinstance(cim_vol["OtherIdentifyingInfo"], list)):
        return None

    index = 0
    len_id_des = len(id_des)
    len_other_info = len(other_info)
    while index < min(len_id_des, len_other_info):
        if dmtf.VOL_OTHER_INFO_NAA_VPD83_TYPE3H == id_des[index]:
            return other_info[index]
        index += 1
    return None


def _vpd83_netapp(cim_vol):
    """
    Workaround for NetApp, they use OtherNameNamespace and
    OtherNameFormat.
    """
    if 'OtherNameFormat' in cim_vol and \
       cim_vol['OtherNameFormat'] == 'NAA' and \
       'OtherNameNamespace' in cim_vol and \
       cim_vol['OtherNameNamespace'] == 'VPD83Type3' and \
       'OtherIdentifyingInfo' in cim_vol and \
       isinstance(cim_vol["OtherIdentifyingInfo"], list) and \
       len(cim_vol['OtherIdentifyingInfo']) == 1:
        return cim_vol['OtherIdentifyingInfo'][0]


def _vpd83_of_cim_vol(cim_vol):
    """
    Extract VPD83 string from CIMInstanceName and convert to LSM format:
        ^6[a-f0-9]{31}$
    """
    vpd_83 = _vpd83_in_cim_vol_name(cim_vol)
    if vpd_83 is None:
        vpd_83 = _vpd83_in_cim_vol_otherinfo(cim_vol)
    if vpd_83 is None:
        vpd_83 = _vpd83_netapp(cim_vol)

    if vpd_83 and re.match('^6[a-fA-F0-9]{31}$', vpd_83):
        return vpd_83.lower()
    else:
        return ''


def cim_vol_to_lsm_vol(cim_vol, pool_id, sys_id):
    """
    Takes a CIMInstance that represents a volume and returns a lsm Volume
    """

    # This is optional (User friendly name)
    if 'ElementName' in cim_vol:
        user_name = cim_vol["ElementName"]
    else:
        #Better fallback value?
        user_name = cim_vol['DeviceID']

    vpd_83 = _vpd83_of_cim_vol(cim_vol)

    admin_state = Volume.ADMIN_STATE_ENABLED

    plugin_data = cim_path_to_path_str(cim_vol.path)

    return Volume(
        vol_id_of_cim_vol(cim_vol), user_name, vpd_83,
        cim_vol["BlockSize"], cim_vol["NumberOfBlocks"], admin_state, sys_id,
        pool_id, plugin_data)


def lsm_vol_to_cim_vol_path(smis_common, lsm_vol):
    """
    Convert lsm.Volume to CIMInstanceName of CIM_StorageVolume using
    lsm.Volume.plugin_data
    """
    if not lsm_vol.plugin_data:
        raise LsmError(
            ErrorNumber.PLUGIN_BUG,
            "Got lsm.Volume instance with empty plugin_data")
    if smis_common.system_list and \
       lsm_vol.system_id not in smis_common.system_list:
        raise LsmError(
            ErrorNumber.NOT_FOUND_SYSTEM,
            "System filtered in URI")

    return path_str_to_cim_path(lsm_vol.plugin_data)


def volume_name_exists(smis_common, volume_name):
    """
    Try to minimize time to search.
    :param volume_name:    Volume ElementName
    :return: True if volume exists with 'name', else False
    """
    all_cim_vols = smis_common.EnumerateInstances(
        'CIM_StorageVolume', PropertyList=['ElementName'])
    for exist_cim_vol in all_cim_vols:
        if volume_name == exist_cim_vol['ElementName']:
            return True
    return False


def volume_create_error_handler(smis_common, method_data):
    """
    When we got CIMError, we check whether we got a duplicate volume name.
    The method_data is the requested volume name.
    """
    if volume_name_exists(smis_common, method_data):
        raise LsmError(ErrorNumber.NAME_CONFLICT,
                       "Volume with name '%s' already exists!" % method_data)

    (error_type, error_msg, error_trace) = sys.exc_info()
    raise error_type, error_msg, error_trace
