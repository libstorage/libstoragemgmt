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

#define LSM_FLAG_RSVD 0

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
 * Opaque data type for file system
 */
typedef struct _lsm_file_system lsm_file_system;


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
typedef struct _lsm_ss lsm_ss;

/**
 * Opaque data type for disk
 */
typedef struct _lsm_disk lsm_disk;

/**
 * Optional data type
 */
typedef struct _lsm_optional_data lsm_optional_data;

/**< \enum lsm_replication_type Different types of replications that can be created */
typedef enum {
    LSM_VOLUME_REPLICATE_UNKNOWN        = -1,       /**< Unknown replicate */
    LSM_VOLUME_REPLICATE_SNAPSHOT       = 1,        /**< Space efficient read only copy*/
    LSM_VOLUME_REPLICATE_CLONE          = 2,        /**< Space efficient copy */
    LSM_VOLUME_REPLICATE_COPY           = 3,        /**< Full bitwise copy */
    LSM_VOLUME_REPLICATE_MIRROR_SYNC    = 4,        /**< Mirrors always in sync */
    LSM_VOLUME_REPLICATE_MIRROR_ASYNC   = 5         /**< Mirror partner updated with delay */
} lsm_replication_type;

/**< \enum lsm_provision_type Different types of provisioning */
typedef enum {
    LSM_PROVISION_UNKNOWN = -1,     /**< Unknown */
    LSM_PROVISION_THIN = 1,         /**< Thin provisioning */
    LSM_PROVISION_FULL = 2,         /**< Thick provisioning */
    LSM_PROVISION_DEFAULT = 3       /**< Default provisioning */
} lsm_provision_type;

/**< \enum lsm_access_type Different types of Volume access */
typedef enum {
    LSM_VOLUME_ACCESS_READ_ONLY = 1,    /**< Read only access */
    LSM_VOLUME_ACCESS_READ_WRITE = 2,   /**< Read write access */
    LSM_VOLUME_ACCESS_NONE = 3          /**< No access */
} lsm_access_type;

/**< \enum lsm_volume_status_type Different states that a volume can be in */
typedef enum {
    LSM_VOLUME_STATUS_ONLINE = 1,   /**< Volume is ready to be used */
    LSM_VOLUME_STATUS_OFFLINE = 2   /**< Volume is offline, no access */
} lsm_volume_status_type;

/**
 * Different states for a volume to be in.
 * Bit field, can be in multiple states at the same time.
 */
#define LSM_VOLUME_OP_STATUS_UNKNOWN    0x0     /**< Unknown status */
#define LSM_VOLUME_OP_STATUS_OK         0x1     /**< Volume is functioning properly */
#define LSM_VOLUME_OP_STATUS_DEGRADED   0x2     /**< Volume is functioning but not optimal */
#define LSM_VOLUME_OP_STATUS_ERROR      0x4     /**< Volume is non-functional */
#define LSM_VOLUME_OP_STATUS_STARTING   0x8     /**< Volume in the process of becomming ready */
#define LSM_VOLUME_OP_STATUS_DORMANT    0x10    /**< Volume is inactive or quiesced */

/**
 * Different states a system status can be in.
 * Bit field, can be in multiple states at the same time.
 */
#define LSM_SYSTEM_STATUS_UNKNOWN               0x00000001  /**< Unknown */
#define LSM_SYSTEM_STATUS_OK                    0x00000002  /**< OK */
#define LSM_SYSTEM_STATUS_ERROR                 0x00000004  /**< Error(s) exist */
#define LSM_SYSTEM_STATUS_DEGRADED              0x00000008  /**< Degraded */
#define LSM_SYSTEM_STATUS_PREDICTIVE_FAILURE    0x00000010  /**< System has predictive failure(s) */
#define LSM_SYSTEM_STATUS_STRESSED              0x00000020  /**< Temp or excessive IO */
#define LSM_SYSTEM_STATUS_STARTING              0x00000040  /**< Booting */
#define LSM_SYSTEM_STATUS_STOPPING              0x00000080  /**< Shutting down */
#define LSM_SYSTEM_STATUS_STOPPED               0x00000100  /**< Stopped by admin */
#define LSM_SYSTEM_STATUS_OTHER                 0x00000200  /**< Vendor specific */

/**< \enum lsm_initiator_type Different types of initiator IDs */
typedef enum {
    LSM_INITIATOR_OTHER = 1,                    /**< Other or unspecified */
    LSM_INITIATOR_PORT_WWN = 2,                 /**< World wide port name */
    LSM_INITIATOR_NODE_WWN = 3,                 /**< World wide node name */
    LSM_INITIATOR_HOSTNAME = 4,                 /**< Host name */
    LSM_INITIATOR_ISCSI = 5,                    /**< iSCSI IQN */
    LSM_INITIATOR_SAS = 7                       /**< SAS ID */
} lsm_initiator_type;

/**< \enum lsm_job_type Different types of jobs */
typedef enum {
    LSM_JOB_VOL_CREATE  = 1,                    /**< Volume create */
    LSM_JOB_VOL_RESIZE = 2,                     /**< Volume re-size */
    LSM_JOB_VOL_REPLICATE = 3                   /**< Volume replicate */
} lsm_job_type;

/**< \enum lsm_job_status Job states */
typedef enum {
    LSM_JOB_INPROGRESS = 1,                     /**< Job is in progress */
    LSM_JOB_COMPLETE = 2,                       /**< Job is complete */
    LSM_JOB_STOPPED = 3,                        /**< Job is stopped */
    LSM_JOB_ERROR = 4                           /**< Job has errored */
} lsm_job_status;

typedef enum {
    LSM_DISK_TYPE_UNKNOWN = 0,
    LSM_DISK_TYPE_OTHER = 1,
    LSM_DISL_TYPE_NOT_APPLICABLE = 2,
    LSM_DISK_TYPE_ATA = 3,
    LSM_DISK_TYPE_SATA = 4,
    LSM_DISK_TYPE_SAS = 5,
    LSM_DISK_TYPE_FC = 6,
    LSM_DISK_TYPE_SOP = 7,
    LSM_DISK_TYPE_SCSI = 8
} lsm_disk_type;

#define LSM_DISK_RETRIEVE_FULL_INFO                 0x02

#define LSM_DISK_STATUS_UNKNOWN                     0x0000000000000001
#define LSM_DISK_STATUS_OK                          0x0000000000000002
#define LSM_DISK_STATUS_PREDICTIVE_FAILURE          0x0000000000000004
#define LSM_DISK_STATUS_ERROR                       0x0000000000000008
#define LSM_DISK_STATUS_OFFLINE                     0x0000000000000010
#define LSM_DISK_STATUS_STARTING                    0x0000000000000020
#define LSM_DISK_STATUS_STOPPING                    0x0000000000000040
#define LSM_DISK_STATUS_STOPPED                     0x0000000000000080
#define LSM_DISK_STATUS_INITIALIZING                0x0000000000000100
#define LSM_DISK_STATUS_RECONSTRUCTING              0x0000000000000200


#define LSM_POOL_STATUS_UNKNOWN                     0x0000000000000001
#define LSM_POOL_STATUS_OK                          0x0000000000000002
#define LSM_POOL_STATUS_OTHER                       0x0000000000000004
#define LSM_POOL_STATUS_STRESSED                    0x0000000000000008
#define LSM_POOL_STATUS_DEGRADED                    0x0000000000000010
#define LSM_POOL_STATUS_ERROR                       0x0000000000000020
#define LSM_POOL_STATUS_OFFLINE                     0x0000000000000040
#define LSM_POOL_STATUS_STARTING                    0x0000000000000080
#define LSM_POOL_STATUS_STOPPING                    0x0000000000000100
#define LSM_POOL_STATUS_STOPPED                     0x0000000000000200
#define LSM_POOL_STATUS_READ_ONLY                   0x0000000000000400
#define LSM_POOL_STATUS_DORMAT                      0x0000000000000800
#define LSM_POOL_STATUS_RECONSTRUCTING              0x0000000000001000
#define LSM_POOL_STATUS_VERIFYING                   0x0000000000002000
#define LSM_POOL_STATUS_INITIALIZING                0x0000000000004000
#define LSM_POOL_STATUS_GROWING                     0x0000000000008000
#define LSM_POOL_STATUS_SHRINKING                   0x0000000000010000
#define LSM_POOL_STATUS_DESTROYING                  0x0000000000020000

typedef enum {
    LSM_POOL_MEMBER_TYPE_UNKNOWN = 0,
    LSM_POOL_MEMBER_TYPE_DISK = 1,
    LSM_POOL_MEMBER_TYPE_POOL = 2,
    LSM_POOL_MEMBER_TYPE_VOLUME = 3,
    LSM_POOL_MEMBER_TYPE_DISK_MIX = 10,
    LSM_POOL_MEMBER_TYPE_DISK_ATA = 11,
    LSM_POOL_MEMBER_TYPE_DISK_SATA = 12,
    LSM_POOL_MEMBER_TYPE_DISK_SAS = 13,
    LSM_POOL_MEMBER_TYPE_DISK_FC = 14,
    LSM_POOL_MEMBER_TYPE_DISK_SOP = 15,
    LSM_POOL_MEMBER_TYPE_DISK_SCSI = 16,
    LSM_POOL_MEMBER_TYPE_DISK_NL_SAS = 17,
    LSM_POOL_MEMBER_TYPE_DISK_HDD = 18,
    LSM_POOL_MEMBER_TYPE_DISK_SSD = 19,
    LSM_POOL_MEMBER_TYPE_DISK_HYBRID = 110
} lsm_pool_member_type;

typedef enum {
    LSM_POOL_RAID_TYPE_0 = 0,
    LSM_POOL_RAID_TYPE_1 = 1,
    LSM_POOL_RAID_TYPE_3 = 3,
    LSM_POOL_RAID_TYPE_4 = 4,
    LSM_POOL_RAID_TYPE_5 = 5,
    LSM_POOL_RAID_TYPE_6 = 6,
    LSM_POOL_RAID_TYPE_10 = 10,
    LSM_POOL_RAID_TYPE_15 = 15,
    LSM_POOL_RAID_TYPE_16 = 16,
    LSM_POOL_RAID_TYPE_50 = 50,
    LSM_POOL_RAID_TYPE_60 = 60,
    LSM_POOL_RAID_TYPE_51 = 51,
    LSM_POOL_RAID_TYPE_61 = 61,
    LSM_POOL_RAID_TYPE_JBOD = 20,
    LSM_POOL_RAID_TYPE_UNKNOWN = 21,
    LSM_POOL_RAID_TYPE_NOT_APPLICABLE = 22,
    LSM_POOL_RAID_TYPE_MIXED = 23
} lsm_pool_raid_type;

#ifdef  __cplusplus
}
#endif

#endif  /* LIBSTORAGEMGMT_TYPES_H */

