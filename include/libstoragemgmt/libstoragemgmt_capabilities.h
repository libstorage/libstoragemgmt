/*
 * Copyright (C) 2011-2013 Red Hat, Inc.
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
/** \enum lsmCapabilityValueType Possible values for supported feature*/
typedef enum {
    LSM_CAPABILITY_UNSUPPORTED          = 0,		/**< Feature is not supported */
    LSM_CAPABILITY_SUPPORTED            = 1,		/**< Feature is supported */
    LSM_CAPABILITY_SUPPORTED_OFFLINE    = 2,		/**< Feature is supported when offlined */
    LSM_CAPABILITY_NO_IMPLEMENTED       = 3,		/**< Feature is not implemented */
    LSM_CAPABILITY_UNKNOWN              = 4			/**< Feature status unknown */
} lsmCapabilityValueType;

/** \enum lsmCapabilityType Capabilities supported by array */
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

    LSM_CAP_ACCESS_GROUP_GRANT                      = 36,       /**< Grant an access group to a volume */
    LSM_CAP_ACCESS_GROUP_REVOKE                     = 37,       /**< Revoke access for an access group */
    LSM_CAP_ACCESS_GROUP_LIST                       = 38,       /**< List access groups */
    LSM_CAP_ACCESS_GROUP_CREATE                     = 39,       /**< Create an access group */
    LSM_CAP_ACCESS_GROUP_DELETE                     = 40,       /**< Delete an access group */
    LSM_CAP_ACCESS_GROUP_ADD_INITIATOR              = 41,       /**< Add an initiator to an access group */
    LSM_CAP_ACCESS_GROUP_DEL_INITIATOR              = 42,       /**< Remove an initiator from an access group */

    LSM_CAP_VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP      = 43,       /**< Retrieve a list of volumes accessible by an access group */
    LSM_CAP_ACCESS_GROUPS_GRANTED_TO_VOLUME         = 44,       /**< Retrieve a list of what access groups are accessible for a given volume */

    LSM_CAP_VOLUME_CHILD_DEPENDENCY                 = 45,       /**< Used to determine if a volume has any dependencies */
    LSM_CAP_VOLUME_CHILD_DEPENDENCY_RM              = 46,       /**< Removes dependendies */

    LSM_CAP_INITIATORS                              = 47,       /**< List initiators */
    LSM_CAP_INITIATORS_GRANTED_TO_VOLUME            = 48,       /**< List initiators granted to a volume */

    LSM_CAP_VOLUME_INITIATOR_GRANT                  = 50,       /**< Grant a initiator to a volume */
    LSM_CAP_VOLUME_INITIATOR_REVOKE                 = 51,       /**< Remove access to a volume for a specified initiator */
    LSM_CAP_VOLUME_ACCESSIBLE_BY_INITIATOR          = 52,       /**< List what volumes are accessible by an initiator */
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
    LSM_CAP_EXPORT_CUSTOM_PATH                      = 124       /**< Plug-in allows user to define custome export path */

} lsmCapabilityType;

/**
 * Free the memory used by the storage capabilities data structure
 * @param cap	Valid storage capability data structure.
 */
void LSM_DLL_EXPORT lsmCapabilityRecordFree(lsmStorageCapabilities *cap);

/**
 * Return the capability for the specified feature.
 * @param cap	Valid pointer to capability data structure
 * @param t		Which capability you are interested in
 * @return Value of supported enumerated type.
 */
lsmCapabilityValueType LSM_DLL_EXPORT lsmCapabilityGet(
                                        lsmStorageCapabilities *cap,
                                        lsmCapabilityType t);


#ifdef  __cplusplus
}
#endif

#endif
