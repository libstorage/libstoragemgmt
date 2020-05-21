/*
 * Copyright (C) 2011-2013 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: tasleson
 */

#ifndef LSM_CONVERT_HPP
#define LSM_CONVERT_HPP

#include "lsm_datatypes.hpp"
#include "lsm_ipc.hpp"

/**
 * Class names for serialized json
 */
const char CLASS_NAME_SYSTEM[] = "System";
const char CLASS_NAME_POOL[] = "Pool";
const char CLASS_NAME_VOLUME[] = "Volume";
const char CLASS_NAME_BLOCK_RANGE[] = "BlockRange";
const char CLASS_NAME_ACCESS_GROUP[] = "AccessGroup";
const char CLASS_NAME_FILE_SYSTEM[] = "FileSystem";
const char CLASS_NAME_DISK[] = "Disk";
const char CLASS_NAME_FS_SNAPSHOT[] = "FsSnapshot";
const char CLASS_NAME_FS_EXPORT[] = "NfsExport";
const char CLASS_NAME_CAPABILITIES[] = "Capabilities";
const char CLASS_NAME_TARGET_PORT[] = "TargetPort";
const char CLASS_NAME_BATTERY[] = "Battery";

#define IS_CLASS(x, name) is_expected_object(x, name)

#define IS_CLASS_SYSTEM(x)       IS_CLASS(x, CLASS_NAME_SYSTEM)
#define IS_CLASS_POOL(x)         IS_CLASS(x, CLASS_NAME_POOL)
#define IS_CLASS_VOLUME(x)       IS_CLASS(x, CLASS_NAME_VOLUME)
#define IS_CLASS_BLOCK_RANGE(x)  IS_CLASS(x, CLASS_NAME_BLOCK_RANGE)
#define IS_CLASS_ACCESS_GROUP(x) IS_CLASS(x, CLASS_NAME_ACCESS_GROUP)
#define IS_CLASS_FILE_SYSTEM(x)  IS_CLASS(x, CLASS_NAME_FILE_SYSTEM)
#define IS_CLASS_FS_SNAPSHOT(x)  IS_CLASS(x, CLASS_NAME_FS_SNAPSHOT)
#define IS_CLASS_FS_EXPORT(x)    IS_CLASS(x, CLASS_NAME_FS_EXPORT)

/**
 * Checks to see if a value is an expected object instance
 * @param obj           Value to check
 * @param class_name    Class name to check
 * @return boolean, true if matches
 */
bool LSM_DLL_LOCAL is_expected_object(Value &obj, std::string class_name);

/**
 * Converts an array of Values to a lsm_string_list
 * @param list      List represented as an vector of strings.
 * @return lsm_string_list pointer, NULL on error.
 */
lsm_string_list LSM_DLL_LOCAL *value_to_string_list(Value &list);

/**
 * Converts a lsm_string_list to a Value
 * @param sl        String list to convert
 * @return Value
 */
Value LSM_DLL_LOCAL string_list_to_value(lsm_string_list *sl);

/**
 * Converts a volume to a volume.
 * @param vol Value to convert.
 * @return lsm_volume *, else NULL on error
 */
lsm_volume LSM_DLL_LOCAL *value_to_volume(Value &vol);

/**
 * Converts a lsm_volume *to a Value
 * @param vol lsm_volume to convert
 * @return Value
 */
Value LSM_DLL_LOCAL volume_to_value(lsm_volume *vol);

/**
 * Converts a vector of volume values to an array
 * @param volume_values     Vector of values that represents volumes
 * @param volumes           An array of volume pointers
 * @param count             Number of volumes
 * @return LSM_ERR_OK on success, else error reason
 */
int LSM_DLL_LOCAL value_array_to_volumes(Value &volume_values,
                                         lsm_volume **volumes[],
                                         uint32_t *count);

/**
 * Converts a Value to a lsm_disk
 * @param disk  Value representing a disk
 * @return lsm_disk pointer, else NULL on error
 */
lsm_disk LSM_DLL_LOCAL *value_to_disk(Value &disk);

/**
 * Converts a lsm_disk to a value
 * @param disk  lsm_disk to convert to value
 * @return Value
 */
Value LSM_DLL_LOCAL disk_to_value(lsm_disk *disk);

/**
 * Converts a vector of disk values to an array.
 * @param[in] disk_values       Vector of values that represents disks
 * @param[out] disks            An array of disk pointers
 * @param[out] count            Number of disks
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_LOCAL value_array_to_disks(Value &disk_values, lsm_disk **disks[],
                                       uint32_t *count);

/**
 * Converts a value to a pool
 * @param pool To convert to lsm_pool *
 * @return lsm_pool *, else NULL on error.
 */
lsm_pool LSM_DLL_LOCAL *value_to_pool(Value &pool);

/**
 * Converts a lsm_pool * to Value
 * @param pool Pool pointer to convert
 * @return Value
 */
Value LSM_DLL_LOCAL pool_to_value(lsm_pool *pool);

/**
 * Converts a value to a system
 * @param system to convert to lsm_system *
 * @return lsm_system pointer, else NULL on error
 */
lsm_system LSM_DLL_LOCAL *value_to_system(Value &system);

/**
 * Converts a lsm_system * to a Value
 * @param system pointer to convert to Value
 * @return Value
 */
Value LSM_DLL_LOCAL system_to_value(lsm_system *system);

/**
 * Converts a Value to a lsm_access_group
 * @param group to convert to lsm_access_group*
 * @return lsm_access_group *, NULL on error
 */
lsm_access_group LSM_DLL_LOCAL *value_to_access_group(Value &group);

/**
 * Converts a lsm_access_group to a Value
 * @param group     Group to convert
 * @return Value, null value type on error.
 */
Value LSM_DLL_LOCAL access_group_to_value(lsm_access_group *group);

/**
 * Converts an access group list to an array of access group pointers
 * @param[in] group         Value representing a std::vector of access groups
 * @param[out] ag_list      Access group array
 * @param[out] count        Number of items in the returned array.
 * @return LSM_ERR_OK on success, else error reason
 */
int LSM_DLL_LOCAL value_array_to_access_groups(Value &group,
                                               lsm_access_group **ag_list[],
                                               uint32_t *count);

/**
 * Converts an array of lsm_access_group to Value(s)
 * @param group             Pointer to an array of lsm_access_group
 * @param count             Number of items in array.
 * @return std::vector of Values representing access groups
 */
Value LSM_DLL_LOCAL access_group_list_to_value(lsm_access_group **group,
                                               uint32_t count);

/**
 * Converts a Value to a lsm_block_range
 * @param br        Value representing a block range
 * @return lsm_block_range *
 */
lsm_block_range LSM_DLL_LOCAL *value_to_block_range(Value &br);

/**
 * Converts a lsm_block_range to a Value
 * @param br        lsm_block_range to convert
 * @return Value, null value type on error
 */
Value LSM_DLL_LOCAL block_range_to_value(lsm_block_range *br);

/**
 * Converts a Value to an array of lsm_block_range
 * @param[in] brl           Value representing block range(s)
 * @param[out] count        Number of items in the resulting array
 * @return NULL on memory allocation failure, else array of lsm_block_range
 */
lsm_block_range LSM_DLL_LOCAL **value_to_block_range_list(Value &brl,
                                                          uint32_t *count);

/**
 * Converts an array of lsm_block_range to Value
 * @param brl           An array of lsm_block_range
 * @param count         Number of items in input
 * @return Value
 */
Value LSM_DLL_LOCAL block_range_list_to_value(lsm_block_range **brl,
                                              uint32_t count);

/**
 * Converts a value to a lsm_fs *
 * @param fs        Value representing a FS to be converted
 * @return lsm_fs pointer or NULL on error.
 */
lsm_fs LSM_DLL_LOCAL *value_to_fs(Value &fs);

/**
 * Converts a lsm_fs pointer to a Value
 * @param fs        File system pointer to convert
 * @return Value
 */
Value LSM_DLL_LOCAL fs_to_value(lsm_fs *fs);

/**
 * Converts a value to a lsm_ss *
 * @param ss        Value representing a snapshot to be converted
 * @return lsm_ss pointer or NULL on error.
 */
lsm_fs_ss LSM_DLL_LOCAL *value_to_ss(Value &ss);

/**
 * Converts a lsm_ss pointer to a Value
 * @param ss        Snapshot pointer to convert
 * @return Value
 */
Value LSM_DLL_LOCAL ss_to_value(lsm_fs_ss *ss);

/**
 * Converts a value to a lsm_nfs_export *
 * @param exp        Value representing a nfs export to be converted
 * @return lsm_nfs_export pointer or NULL on error.
 */
lsm_nfs_export LSM_DLL_LOCAL *value_to_nfs_export(Value &exp);

/**
 * Converts a lsm_nfs_export pointer to a Value
 * @param exp        NFS export pointer to convert
 * @return Value
 */
Value LSM_DLL_LOCAL nfs_export_to_value(lsm_nfs_export *exp);

/**
 * Converts a Value to a lsm_storage_capabilities
 * @param exp       Value representing a storage capabilities
 * @return lsm_storage_capabilities pointer or NULL on error
 */
lsm_storage_capabilities LSM_DLL_LOCAL *value_to_capabilities(Value &exp);

/**
 * Converts a lsm_storage_capabilities to a value
 * @param cap       lsm_storage_capabilities to convert to value
 * @return Value
 */
Value LSM_DLL_LOCAL capabilities_to_value(lsm_storage_capabilities *cap);

/**
 * Convert a Value representation to lsm_target_port
 * @param tp    Value to convert to lsm_target_port
 * @return lsm_target_port pointer or NULL on errors
 */
lsm_target_port LSM_DLL_LOCAL *value_to_target_port(Value &tp);

/**
 * Converts a lsm_target_port to a value
 * @param tp       lsm_target_port to convert to value
 * @return Value
 */
Value LSM_DLL_LOCAL target_port_to_value(lsm_target_port *tp);

/**
 * Converts a value to array of uint32.
 */
int LSM_DLL_LOCAL values_to_uint32_array(Value &value, uint32_t **uint32_array,
                                         uint32_t *count);

/**
 * Converts an array of uint32 to a value.
 */
Value LSM_DLL_LOCAL uint32_array_to_value(uint32_t *uint32_array,
                                          uint32_t count);

/**
 * Converts a Value to a lsm_battery
 * @param battery  Value representing a battery
 * @return lsm_battery pointer, else NULL on error
 */
lsm_battery LSM_DLL_LOCAL *value_to_battery(Value &battery);

/**
 * Converts a lsm_battery to a value
 * @param battery  lsm_battery to convert to value
 * @return Value
 */
Value LSM_DLL_LOCAL battery_to_value(lsm_battery *battery);

/**
 * Converts a vector of battery values to an array.
 * @param[in]  battery_values       Vector of values that represents batteries.
 * @param[out] bs                   An array of battery pointers
 * @param[out] count                Number of batteries
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_LOCAL value_array_to_batteries(Value &battery_values,
                                           lsm_battery **bs[], uint32_t *count);

#endif
