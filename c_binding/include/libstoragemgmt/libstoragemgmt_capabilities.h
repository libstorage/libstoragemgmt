/*
 * Copyright (C) 2011-2014 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Author: tasleson
 */

#ifndef LSM_CAPABILITIES_H
#define LSM_CAPABILITIES_H

#include "libstoragemgmt_common.h"

#ifdef  __cplusplus
extern "C" {
#endif

/** @file libstoragemgmt_capabilities.h*/

/*Note: Domain is 0..255 */
/** \enum lsm_capability_value_type Possible values for supported feature*/
typedef enum {
    LSM_CAPABILITY_UNSUPPORTED          = 0,        /**< Feature is not supported */
    LSM_CAPABILITY_SUPPORTED            = 1         /**< Feature is supported */
} lsm_capability_value_type;

/** \enum lsm_capability_value_type Capabilities supported by array */
typedef enum {
    LSM_CAP_BLOCK_SUPPORT                           = 0,        /**< Array supports block ops */
    LSM_CAP_FS_SUPPORT                              = 1,        /**< Array supports file system ops */

    LSM_CAP_VOLUMES                                 = 20,       /**< List volumes */
    LSM_CAP_VOLUME_CREATE                           = 21,       /**< Create volumes */
    LSM_CAP_VOLUME_RESIZE                           = 22,       /**< Resize volumes */

    LSM_CAP_VOLUME_REPLICATE                        = 23,       /**< Replication is supported */
    LSM_CAP_VOLUME_REPLICATE_CLONE                  = 24,       /**< Can make a space efficient copy of volume */
    LSM_CAP_VOLUME_REPLICATE_COPY                   = 25,       /**< Can make a bitwise copy of volume */
    LSM_CAP_VOLUME_REPLICATE_MIRROR_ASYNC           = 26,       /**< Mirror data with delay */
    LSM_CAP_VOLUME_REPLICATE_MIRROR_SYNC            = 27,       /**< Mirror data and always in sync */
    LSM_CAP_VOLUME_COPY_RANGE_BLOCK_SIZE            = 28,       /**< Size of a block for range operations */
    LSM_CAP_VOLUME_COPY_RANGE                       = 29,       /**< Sub volume replication support */
    LSM_CAP_VOLUME_COPY_RANGE_CLONE                 = 30,       /**< Can space efficient copy a region(s) of a volume*/
    LSM_CAP_VOLUME_COPY_RANGE_COPY                  = 31,       /**< Can copy a region(s) of a volume */

    LSM_CAP_VOLUME_DELETE                           = 33,       /**< Can delete a volume */

    LSM_CAP_VOLUME_ONLINE                           = 34,       /**< Put volume online */
    LSM_CAP_VOLUME_OFFLINE                          = 35,       /**< Take volume offline */

    LSM_CAP_VOLUME_MASK                             = 36,       /**< Grant an access group to a volume */
    LSM_CAP_VOLUME_UNMASK                           = 37,       /**< Revoke access for an access group */
    LSM_CAP_ACCESS_GROUPS                           = 38,       /**< List access groups */
    LSM_CAP_ACCESS_GROUP_CREATE                     = 39,       /**< Create an access group */
    LSM_CAP_ACCESS_GROUP_DELETE                     = 40,       /**< Delete an access group */
    LSM_CAP_ACCESS_GROUP_INITIATOR_ADD              = 41,       /**< Add an initiator to an access group */
    LSM_CAP_ACCESS_GROUP_INITIATOR_DELETE           = 42,       /**< Remove an initiator from an access group */

    LSM_CAP_VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP      = 43,       /**< Retrieve a list of volumes accessible by an access group */
    LSM_CAP_ACCESS_GROUPS_GRANTED_TO_VOLUME         = 44,       /**< Retrieve a list of what access groups are accessible for a given volume */

    LSM_CAP_VOLUME_CHILD_DEPENDENCY                 = 45,       /**< Used to determine if a volume has any dependencies */
    LSM_CAP_VOLUME_CHILD_DEPENDENCY_RM              = 46,       /**< Removes dependendies */

    LSM_CAP_VOLUME_ISCSI_CHAP_AUTHENTICATION        = 53,       /**< If you can configure iSCSI chap authentication */

    LSM_CAP_VOLUME_THIN                             = 55,       /**< Thin provisioned volumes are supported */

    LSM_CAP_FS                                      = 100,      /**< List file systems */
    LSM_CAP_FS_DELETE                               = 101,      /**< Delete a file system */
    LSM_CAP_FS_RESIZE                               = 102,      /**< Resize a file system */
    LSM_CAP_FS_CREATE                               = 103,      /**< Create a file system */
    LSM_CAP_FS_CLONE                                = 104,      /**< Clone a file system */
    LSM_CAP_FILE_CLONE                              = 105,      /**< Clone a file on a file system */
    LSM_CAP_FS_SNAPSHOTS                            = 106,      /**< List FS snapshots */
    LSM_CAP_FS_SNAPSHOT_CREATE                      = 107,      /**< Create a snapshot */
    LSM_CAP_FS_SNAPSHOT_CREATE_SPECIFIC_FILES       = 108,      /**< Create snapshots for one or more specific files */
    LSM_CAP_FS_SNAPSHOT_DELETE                      = 109,      /**< Delete a snapshot */
    LSM_CAP_FS_SNAPSHOT_REVERT                      = 110,      /**< Revert the state of a FS to the specified snapshot */
    LSM_CAP_FS_SNAPSHOT_REVERT_SPECIFIC_FILES       = 111,      /**< Revert the state of a list of files to a specified snapshot */
    LSM_CAP_FS_CHILD_DEPENDENCY                     = 112,      /**< Determine if a child dependency exists for the specified file */
    LSM_CAP_FS_CHILD_DEPENDENCY_RM                  = 113,      /**< Remove any dependencies the file system may have */
    LSM_CAP_FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES   = 114,      /**< Remove any dependencies for specific files */

    LSM_CAP_EXPORT_AUTH                             = 120,      /**< Get a list of supported client authentication types */
    LSM_CAP_EXPORTS                                 = 121,      /**< List exported file systems */
    LSM_CAP_EXPORT_FS                               = 122,      /**< Export a file system */
    LSM_CAP_EXPORT_REMOVE                           = 123,      /**< Remove an export */
    LSM_CAP_EXPORT_CUSTOM_PATH                      = 124,      /**< Plug-in allows user to define custome export path */

    LSM_CAP_POOL_CREATE                             = 130,      /**< Pool create support */
    LSM_CAP_POOL_CREATE_FROM_DISKS                  = 131,      /**< Pool create from disks */
    LSM_CAP_POOL_CREATE_FROM_VOLUMES                = 132,      /**< Pool create from volumes */
    LSM_CAP_POOL_CREATE_FROM_POOL                   = 133,      /**< Pool create from pool */

    LSM_CAP_POOL_CREATE_DISK_RAID_0                 = 140,
    LSM_CAP_POOL_CREATE_DISK_RAID_1                 = 141,
    LSM_CAP_POOL_CREATE_DISK_RAID_JBOD              = 142,
    LSM_CAP_POOL_CREATE_DISK_RAID_3                 = 143,
    LSM_CAP_POOL_CREATE_DISK_RAID_4                 = 144,
    LSM_CAP_POOL_CREATE_DISK_RAID_5                 = 145,
    LSM_CAP_POOL_CREATE_DISK_RAID_6                 = 146,
    LSM_CAP_POOL_CREATE_DISK_RAID_10                = 147,
    LSM_CAP_POOL_CREATE_DISK_RAID_50                = 148,
    LSM_CAP_POOL_CREATE_DISK_RAID_51                = 149,
    LSM_CAP_POOL_CREATE_DISK_RAID_60                = 150,
    LSM_CAP_POOL_CREATE_DISK_RAID_61                = 151,
    LSM_CAP_POOL_CREATE_DISK_RAID_15                = 152,
    LSM_CAP_POOL_CREATE_DISK_RAID_16                = 153,
    LSM_CAP_POOL_CREATE_DISK_RAID_NOT_APPLICABLE    = 154,

    LSM_CAP_POOL_DELETE                             = 200,       /**< Pool delete support */

    LSM_CAP_POOLS_QUICK_SEARCH                      = 210,      /**< Seach occurs on array */
    LSM_CAP_VOLUMES_QUICK_SEARCH                    = 211,      /**< Seach occurs on array */
    LSM_CAP_DISKS_QUICK_SEARCH                      = 212,      /**< Seach occurs on array */
    LSM_CAP_ACCESS_GROUPS_QUICK_SEARCH              = 213,      /**< Seach occurs on array */
    LSM_CAP_FS_QUICK_SEARCH                         = 214,      /**< Seach occurs on array */
    LSM_CAP_NFS_EXPORTS_QUICK_SEARCH                = 215       /**< Seach occurs on array */

} lsm_capability_type;

/**
 * Free the memory used by the storage capabilities data structure
 * @param cap   Valid storage capability data structure.
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_capability_record_free(lsm_storage_capabilities *cap);

/**
 * Return the capability for the specified feature.
 * @param cap   Valid pointer to capability data structure
 * @param t     Which capability you are interested in
 * @return Value of supported enumerated type.
 */
lsm_capability_value_type LSM_DLL_EXPORT lsm_capability_get(
                                        lsm_storage_capabilities *cap,
                                        lsm_capability_type t);

/**
 * Boolean version of capability support
 * @param cap
 * @param t
 * @return Non-zero if supported, 0 if not supported
 */
int LSM_DLL_EXPORT lsm_capability_supported(lsm_storage_capabilities *cap,
                                        lsm_capability_type t);


#ifdef  __cplusplus
}
#endif

#endif
