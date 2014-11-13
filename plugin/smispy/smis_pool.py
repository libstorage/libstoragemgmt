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

from utils import merge_list, path_str_to_cim_path, cim_path_to_path_str
import dmtf
from lsm import LsmError, ErrorNumber, Pool


def cim_pools_of_cim_sys_path(smis_common, cim_sys_path, property_list=None):
    """
    Use this association to get a list of CIM_StoragePool:
            CIM_ComputerSystem
                 |
                 | (CIM_HostedStoragePool)
                 |
                 v
            CIM_StoragePool
    As 'Block Services Package' is mandatory for 'Array' profile which already
    checked by plugin_register(), we don't do any profile check here.
    Primordial pool will be eliminated from return list.
    These pools will be eliminated also:
        * Spare pool with CIM_StoragePool['Usage'] == dmtf.POOL_USAGE_SPARE
        * IBM ArrayPool(IBMTSDS_ArrayPool)
        * IBM ArraySitePool(IBMTSDS_ArraySitePool)
    """
    cim_pools = []

    if property_list is None:
        property_list = ['Primordial', 'Usage']
    else:
        property_list = merge_list(property_list, ['Primordial', 'Usage'])

    cim_pools = smis_common.Associators(
        cim_sys_path,
        AssocClass='CIM_HostedStoragePool',
        ResultClass='CIM_StoragePool',
        PropertyList=property_list)

    rc = []
    for cim_pool in cim_pools:
        if 'Primordial' in cim_pool and cim_pool['Primordial']:
            continue
        if 'Usage' in cim_pool and cim_pool['Usage'] == dmtf.POOL_USAGE_SPARE:
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
        rc.append(cim_pool)

    return rc


def cim_pool_id_pros():
    """
    Return a list of CIM_StoragePool properties required to generate
    lsm.Pool.id
    """
    return ['InstanceID']


def pool_id_of_cim_pool(cim_pool):
    if 'InstanceID' in cim_pool:
        return cim_pool['InstanceID']
    else:
        raise LsmError(
            ErrorNumber.PLUGIN_BUG,
            "pool_id_of_cim_pool(): Got CIM_StoragePool with no 'InstanceID' "
            "property: %s, %s" % (cim_pool.items(), cim_pool.path))


def cim_pool_pros():
    """
    Return a list of CIM_StoragePool properties required to generate lsm.Pool.
    """
    pool_pros = cim_pool_id_pros()
    pool_pros.extend(['ElementName', 'TotalManagedSpace',
                      'RemainingManagedSpace', 'Usage',
                      'OperationalStatus'])
    return pool_pros


def _pool_element_type(smis_common, cim_pool):
    """
    Return a set (Pool.element_type, Pool.unsupported)
    Using CIM_StorageConfigurationCapabilities
    'SupportedStorageElementFeatures' and 'SupportedStorageElementTypes'
    property.
    For MegaRAID, just return (Pool.ELEMENT_TYPE_VOLUME, 0)
    """
    if smis_common.is_megaraid():
        return Pool.ELEMENT_TYPE_VOLUME | Pool.ELEMENT_TYPE_VOLUME_FULL, 0

    element_type = 0
    unsupported = 0

    # check whether current pool support create volume or not.
    cim_sccs = smis_common.Associators(
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
            supported_types = cim_scc['SupportedStorageElementTypes']

            if dmtf.SUPPORT_VOL_CREATE in supported_features:
                element_type = Pool.ELEMENT_TYPE_VOLUME
                if dmtf.ELEMENT_THIN_VOLUME in supported_types:
                    element_type |= Pool.ELEMENT_TYPE_VOLUME_THIN
                if dmtf.ELEMENT_THICK_VOLUME in supported_types:
                    element_type |= Pool.ELEMENT_TYPE_VOLUME_FULL
            if dmtf.SUPPORT_ELEMENT_EXPAND not in supported_features:
                unsupported |= Pool.UNSUPPORTED_VOLUME_GROW
            if dmtf.SUPPORT_ELEMENT_REDUCE not in supported_features:
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

        if usage == dmtf.POOL_USAGE_UNRESTRICTED:
            element_type |= Pool.ELEMENT_TYPE_VOLUME
        if usage == dmtf.POOL_USAGE_RESERVED_FOR_SYSTEM or \
                usage > dmtf.POOL_USAGE_DELTA:
            element_type |= Pool.ELEMENT_TYPE_SYS_RESERVED
        if usage == dmtf.POOL_USAGE_DELTA:
            # We blitz all the other elements types for this designation
            element_type = Pool.ELEMENT_TYPE_DELTA

    return element_type, unsupported


_LSM_POOL_OP_STATUS_CONV = {
    dmtf.OP_STATUS_OK: Pool.STATUS_OK,
    dmtf.OP_STATUS_ERROR: Pool.STATUS_ERROR,
    dmtf.OP_STATUS_DEGRADED: Pool.STATUS_DEGRADED,
    dmtf.OP_STATUS_NON_RECOVERABLE_ERROR: Pool.STATUS_ERROR,
    dmtf.OP_STATUS_SUPPORTING_ENTITY_IN_ERROR: Pool.STATUS_ERROR,
}


def _pool_status_of_cim_pool(dmtf_op_status_list):
    """
    Convert CIM_StoragePool['OperationalStatus'] to LSM
    """
    return dmtf.op_status_list_conv(
        _LSM_POOL_OP_STATUS_CONV, dmtf_op_status_list,
        Pool.STATUS_UNKNOWN, Pool.STATUS_OTHER)


def cim_pool_to_lsm_pool(smis_common, cim_pool, system_id):
    """
    Return a Pool object base on information of cim_pool.
    Assuming cim_pool already holding correct properties.
    """
    status_info = ''
    pool_id = pool_id_of_cim_pool(cim_pool)
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
        (status, status_info) = _pool_status_of_cim_pool(
            cim_pool['OperationalStatus'])

    element_type, unsupported = _pool_element_type(smis_common, cim_pool)

    plugin_data = cim_path_to_path_str(cim_pool.path)

    return Pool(pool_id, name, element_type, unsupported,
                total_space, free_space,
                status, status_info, system_id, plugin_data)


def lsm_pool_to_cim_pool_path(smis_common, lsm_pool):
    """
    Convert lsm.Pool to CIMInstanceName of CIM_StoragePool using
    lsm.Pool.plugin_data
    """
    if not lsm_pool.plugin_data:
        raise LsmError(
            ErrorNumber.PLUGIN_BUG,
            "Got lsm.Pool instance with empty plugin_data")
    if smis_common.system_list and \
       lsm_pool.system_id not in smis_common.system_list:
        raise LsmError(
            ErrorNumber.NOT_FOUND_SYSTEM,
            "System filtered in URI")

    return path_str_to_cim_path(lsm_pool.plugin_data)


def pool_id_of_cim_vol(smis_common, cim_vol_path):
    """
    Find out the lsm.Pool.id of CIM_StorageVolume
    """
    property_list = cim_pool_id_pros()
    cim_pools = smis_common.Associators(
        cim_vol_path,
        AssocClass='CIM_AllocatedFromStoragePool',
        ResultClass='CIM_StoragePool',
        PropertyList=property_list)
    if len(cim_pools) != 1:
        raise LsmError(
            ErrorNumber.PLUGIN_BUG,
            "pool_id_of_cim_vol(): Got unexpected count(%d) of cim_pool " %
            len(cim_pools) +
            "associated to cim_vol: %s, %s" % (cim_vol_path, cim_pools))
    return pool_id_of_cim_pool(cim_pools[0])
