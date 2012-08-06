/*
 * Copyright (C) 2011-2012 Red Hat, Inc.
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

/*Note: Domain is 0..255 */
typedef enum {
    LSM_CAPABILITY_UNSUPPORTED          = 0,
    LSM_CAPABILITY_SUPPORTED            = 1,
    LSM_CAPABILITY_SUPPORTED_OFFLINE    = 2,
    LSM_CAPABILITY_NO_IMPLEMENTED       = 3,
    LSM_CAPABILITY_UNKNOWN              = 4
} lsmCapabilityValueType;

typedef enum {
    LSM_CAP_BLOCK_SUPPORT                           = 0,
    LSM_CAP_FS_SUPPORT                              = 1,

    LSM_CAP_VOLUMES                                 = 20,
    LSM_CAP_VOLUME_CREATE                           = 21,
    LSM_CAP_VOLUME_RESIZE                           = 22,

    LSM_CAP_VOLUME_REPLICATE                        = 23,
    LSM_CAP_VOLUME_REPLICATE_CLONE                  = 24,
    LSM_CAP_VOLUME_REPLICATE_COPY                   = 25,
    LSM_CAP_VOLUME_REPLICATE_MIRROR_ASYNC           = 26,
    LSM_CAP_VOLUME_REPLICATE_MIRROR_SYNC            = 27,
    LSM_CAP_VOLUME_COPY_RANGE_BLOCK_SIZE            = 28,
    LSM_CAP_VOLUME_COPY_RANGE                       = 29,
    LSM_CAP_VOLUME_COPY_RANGE_CLONE                 = 30,
    LSM_CAP_VOLUME_COPY_RANGE_COPY                  = 31,

    LSM_CAP_VOLUME_DELETE                           = 33,

    LSM_CAP_VOLUME_ONLINE                           = 34,
    LSM_CAP_VOLUME_OFFLINE                          = 35,

    LSM_CAP_ACCESS_GROUP_GRANT                      = 36,
    LSM_CAP_ACCESS_GROUP_REVOKE                     = 37,
    LSM_CAP_ACCESS_GROUP_LIST                       = 38,
    LSM_CAP_ACCESS_GROUP_CREATE                     = 39,
    LSM_CAP_ACCESS_GROUP_DELETE                     = 40,
    LSM_CAP_ACCESS_GROUP_ADD_INITIATOR              = 41,
    LSM_CAP_ACCESS_GROUP_DEL_INITIATOR              = 42,

    LSM_CAP_VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP      = 43,
    LSM_CAP_ACCESS_GROUPS_GRANTED_TO_VOLUME         = 44,

    LSM_CAP_VOLUME_CHILD_DEPENDENCY                 = 45,
    LSM_CAP_VOLUME_CHILD_DEPENDENCY_RM              = 46,

    LSM_CAP_INITIATORS                              = 47,
    LSM_CAP_INITIATORS_GRANTED_TO_VOLUME            = 48,

    LSM_CAP_VOLUME_INITIATOR_GRANT                  = 50,
    LSM_CAP_VOLUME_INITIATOR_REVOKE                 = 51,
    LSM_CAP_VOLUME_ACCESSIBLE_BY_INITIATOR          = 52,
    LSM_CAP_VOLUME_ISCSI_CHAP_AUTHENTICATION        = 53,

    LSM_CAP_FS                                      = 100,
    LSM_CAP_FS_DELETE                               = 101,
    LSM_CAP_FS_RESIZE                               = 102,
    LSM_CAP_FS_CREATE                               = 103,
    LSM_CAP_FS_CLONE                                = 104,
    LSM_CAP_FILE_CLONE                              = 105,
    LSM_CAP_FS_SNAPSHOTS                            = 106,
    LSM_CAP_FS_SNAPSHOT_CREATE                      = 107,
    LSM_CAP_FS_SNAPSHOT_CREATE_SPECIFIC_FILES       = 108,
    LSM_CAP_FS_SNAPSHOT_DELETE                      = 109,
    LSM_CAP_FS_SNAPSHOT_REVERT                      = 110,
    LSM_CAP_FS_SNAPSHOT_REVERT_SPECIFIC_FILES       = 111,
    LSM_CAP_FS_CHILD_DEPENDENCY                     = 112,
    LSM_CAP_FS_CHILD_DEPENDENCY_RM                  = 113,
    LSM_CAP_FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES   = 114,

    LSM_CAP_EXPORT_AUTH                             = 120,
    LSM_CAP_EXPORTS                                 = 121,
    LSM_CAP_EXPORT_FS                               = 122,
    LSM_CAP_EXPORT_REMOVE                           = 123

} lsmCapabilityType;

void LSM_DLL_EXPORT lsmCapabilityRecordFree(lsmStorageCapabilitiesPtr cap);

lsmCapabilityValueType LSM_DLL_EXPORT lsmCapabilityGet(
                                        lsmStorageCapabilitiesPtr cap,
                                        lsmCapabilityType t);


#ifdef  __cplusplus
}
#endif

#endif