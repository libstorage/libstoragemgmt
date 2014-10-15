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

#ifndef LIBSTORAGEMGMT_TYPES_H
#define LIBSTORAGEMGMT_TYPES_H


#ifdef __cplusplus
    #define __STDC_FORMAT_MACROS
    #define __STDC_LIMIT_MACROS
#endif
#include <inttypes.h>

#ifdef  __cplusplus
extern "C" {
#endif

/** @file libstoragemgmt_types.h */

/* Just incase we want to change the flag to a different type */
typedef uint64_t lsm_flag;

#define LSM_CLIENT_FLAG_RSVD 0

/**
 * Opaque data type for a connection.
 */
typedef struct _lsm_connect lsm_connect;

/**
 * Opaque data type for a block based storage unit
 */
typedef struct _lsm_volume lsm_volume;

/**
 * Opaque data type for a storage pool which is used as a base for Volumes etc.
 * to be created from.
 */
typedef struct _lsm_pool lsm_pool;

/**
 * Opaque data type for an initiator.
 */
typedef struct _lsm_initiator lsm_initiator;

/**
 * Opaque data type for storage capabilities.
 */
typedef struct _lsm_storage_capabilities lsm_storage_capabilities;

/**
 * Access group
 */
typedef struct _lsm_access_group lsm_access_group;

/**
 * Opaque data type for nfs exports
 */
typedef struct _lsm_nfs_export lsm_nfs_export;

/**
 * Opaque data type for block ranges (regions to replicate)
 */
typedef struct _lsm_block_range lsm_block_range;

/**
 * Opaque data type for systems.
 */
typedef struct _lsm_system lsm_system;

/**
 * Opaque data type for string collection
 */
typedef struct _lsm_string_list lsm_string_list;

/**
 * Opaque data type for file systems
 */
typedef struct _lsm_fs lsm_fs;

/**
 * Opaque data type for snapshot
 */
typedef struct _lsm_fs_ss lsm_fs_ss;

/**
 * Opaque data type for disk
 */
typedef struct _lsm_disk lsm_disk;

/**
 * Optional data type
 */
typedef struct _lsm_hash lsm_hash;

/**
 * Opaque data type for Target ports
 */
typedef struct _lsm_target_port lsm_target_port;

/**< \enum lsm_replication_type Different types of replications that can be created */
typedef enum {
    LSM_VOLUME_REPLICATE_UNKNOWN        = -1,       /**< Unknown replicate */
    LSM_VOLUME_REPLICATE_CLONE          = 2,        /**< Space efficient copy */
    LSM_VOLUME_REPLICATE_COPY           = 3,        /**< Full bitwise copy */
    LSM_VOLUME_REPLICATE_MIRROR_SYNC    = 4,        /**< Mirrors always in sync */
    LSM_VOLUME_REPLICATE_MIRROR_ASYNC   = 5         /**< Mirror partner updated with delay */
} lsm_replication_type;

/**< \enum lsm_volume_provision_type Different types of provisioning */
typedef enum {
    LSM_VOLUME_PROVISION_UNKNOWN = -1,     /**< Unknown */
    LSM_VOLUME_PROVISION_THIN = 1,         /**< Thin provisioning */
    LSM_VOLUME_PROVISION_FULL = 2,         /**< Thick provisioning */
    LSM_VOLUME_PROVISION_DEFAULT = 3       /**< Default provisioning */
} lsm_volume_provision_type;

/**
 * Admin state for volume, enabled or disabled
 */
#define LSM_VOLUME_ADMIN_STATE_ENABLED      0x1     /**< Volume accessible */
#define LSM_VOLUME_ADMIN_STATE_DISABLED     0x0     /**< Volume unaccessible */

/**
 * Different states a system status can be in.
 * Bit field, can be in multiple states at the same time.
 */
#define LSM_SYSTEM_STATUS_UNKNOWN               0x00000001  /**< Unknown */
#define LSM_SYSTEM_STATUS_OK                    0x00000002  /**< OK */
#define LSM_SYSTEM_STATUS_ERROR                 0x00000004  /**< Error(s) exist */
#define LSM_SYSTEM_STATUS_DEGRADED              0x00000008  /**< Degraded */
#define LSM_SYSTEM_STATUS_PREDICTIVE_FAILURE    0x00000010  /**< System has predictive failure(s) */
#define LSM_SYSTEM_STATUS_OTHER                 0x00000020  /**< Vendor specific */


typedef enum {
    LSM_ACCESS_GROUP_INIT_TYPE_UNKNOWN = 0,     /**< Unknown */
    LSM_ACCESS_GROUP_INIT_TYPE_OTHER = 1,       /**< Something not seen before */
    LSM_ACCESS_GROUP_INIT_TYPE_WWPN = 2,        /**< Port name */
    LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN = 5,   /**< ISCSI IQN */
    LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_WWPN_MIXED = 7 /**< More than 1 type */
} lsm_access_group_init_type;

/**< \enum lsm_job_status Job states */
typedef enum {
    LSM_JOB_INPROGRESS = 1,                     /**< Job is in progress */
    LSM_JOB_COMPLETE = 2,                       /**< Job is complete */
    LSM_JOB_ERROR = 3                           /**< Job has errored */
} lsm_job_status;

typedef enum {
    LSM_DISK_TYPE_UNKNOWN = 0,
    LSM_DISK_TYPE_OTHER = 1,
    LSM_DISK_TYPE_ATA = 3,
    LSM_DISK_TYPE_SATA = 4,
    LSM_DISK_TYPE_SAS = 5,
    LSM_DISK_TYPE_FC = 6,
    LSM_DISK_TYPE_SOP = 7,
    LSM_DISK_TYPE_SCSI = 8,
    LSM_DISK_TYPE_LUN = 9,
    LSM_DISK_TYPE_NL_SAS = 51,
    LSM_DISK_TYPE_HDD = 52,
    LSM_DISK_TYPE_SSD = 53,
    LSM_DISK_TYPE_HYBRID = 54,
} lsm_disk_type;


#define LSM_DISK_STATUS_UNKNOWN                     0x0000000000000001
#define LSM_DISK_STATUS_OK                          0x0000000000000002
#define LSM_DISK_STATUS_OTHER                       0x0000000000000004
#define LSM_DISK_STATUS_PREDICTIVE_FAILURE          0x0000000000000008
#define LSM_DISK_STATUS_ERROR                       0x0000000000000010
#define LSM_DISK_STATUS_REMOVED                     0x0000000000000020
#define LSM_DISK_STATUS_STARTING                    0x0000000000000040
#define LSM_DISK_STATUS_STOPPING                    0x0000000000000080
#define LSM_DISK_STATUS_STOPPED                     0x0000000000000100
#define LSM_DISK_STATUS_INITIALIZING                0x0000000000000200
#define LSM_DISK_STATUS_MAINTENANCE_MODE            0x0000000000000400
#define LSM_DISK_STATUS_SPARE_DISK                  0x0000000000000800
#define LSM_DISK_STATUS_RECONSTRUCT                 0x0000000000001000

#define LSM_DISK_BLOCK_SIZE_NOT_FOUND               -1
#define LSM_DISK_BLOCK_COUNT_NOT_FOUND              -1

#define LSM_POOL_STATUS_UNKNOWN                     0x0000000000000001
#define LSM_POOL_STATUS_OK                          0x0000000000000002
#define LSM_POOL_STATUS_OTHER                       0x0000000000000004
#define LSM_POOL_STATUS_DEGRADED                    0x0000000000000010
#define LSM_POOL_STATUS_ERROR                       0x0000000000000020
#define LSM_POOL_STATUS_STOPPED                     0x0000000000000200
#define LSM_POOL_STATUS_RECONSTRUCTING              0x0000000000001000
#define LSM_POOL_STATUS_VERIFYING                   0x0000000000002000
#define LSM_POOL_STATUS_INITIALIZING                0x0000000000004000
#define LSM_POOL_STATUS_GROWING                     0x0000000000008000

#define LSM_POOL_ELEMENT_TYPE_POOL                  0x0000000000000002
#define LSM_POOL_ELEMENT_TYPE_VOLUME                0x0000000000000004
#define LSM_POOL_ELEMENT_TYPE_FS                    0x0000000000000008
#define LSM_POOL_ELEMENT_TYPE_DELTA                 0x0000000000000010
#define LSM_POOL_ELEMENT_TYPE_VOLUME_FULL           0x0000000000000020
#define LSM_POOL_ELEMENT_TYPE_VOLUME_THIN           0x0000000000000040
#define LSM_POOL_ELEMENT_TYPE_SYS_RESERVED          0x0000000000000400

#define LSM_POOL_UNSUPPORTED_VOLUME_GROW			0x0000000000000001
#define LSM_POOL_UNSUPPORTED_VOLUME_SHRINK			0x0000000000000002

typedef enum {
    LSM_TARGET_PORT_TYPE_OTHER = 1,
    LSM_TARGET_PORT_TYPE_FC = 2,
    LSM_TARGET_PORT_TYPE_FCOE = 3,
    LSM_TARGET_PORT_TYPE_ISCSI = 4
} lsm_target_port_type;

#ifdef  __cplusplus
}
#endif

#endif  /* LIBSTORAGEMGMT_TYPES_H */
