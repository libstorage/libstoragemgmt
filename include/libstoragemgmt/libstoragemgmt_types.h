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
typedef uint64_t lsmFlag_t;

#define LSM_FLAG_RSVD 0

/**
 * Opaque data type for a connection.
 */
typedef struct _lsmConnect lsmConnect;

/**
 * Opaque data type for a block based storage unit
 */
typedef struct _lsmVolume lsmVolume;

/**
 * Opaque data type for a storage pool which is used as a base for Volumes etc.
 * to be created from.
 */
typedef struct _lsmPool lsmPool;

/**
 * Opaque data type for an initiator.
 */
typedef struct _lsmInitiator lsmInitiator;

/**
 * Opaque data type for storage capabilities.
 */
typedef struct _lsmStorageCapabilities lsmStorageCapabilities;

/**
 * Access group
 */
typedef struct _lsmAccessGroup lsmAccessGroup;

/**
 * Opaque data type for file system
 */
typedef struct _lsmFileSystem lsmFileSystem;


/**
 * Opaque data type for nfs exports
 */
typedef struct _lsmNfsExport lsmNfsExport;

/**
 * Opaque data type for block ranges (regions to replicate)
 */
typedef struct _lsmBlockRange lsmBlockRange;

/**
 * Opaque data type for systems.
 */
typedef struct _lsmSystem lsmSystem;

/**
 * Opaque data type for string collection
 */
typedef struct _lsmStringList lsmStringList;

/**
 * Opaque data type for file systems
 */
typedef struct _lsmFs lsmFs;

/**
 * Opaque data type for snapshot
 */
typedef struct _lsmSs lsmSs;

/**< \enum lsmReplicationType Different types of replications that can be created */
typedef enum {
    LSM_VOLUME_REPLICATE_UNKNOWN        = -1,       /**< Unknown replicate */
    LSM_VOLUME_REPLICATE_SNAPSHOT       = 1,        /**< Space efficient read only copy*/
    LSM_VOLUME_REPLICATE_CLONE          = 2,        /**< Space efficient copy */
    LSM_VOLUME_REPLICATE_COPY           = 3,        /**< Full bitwise copy */
    LSM_VOLUME_REPLICATE_MIRROR_SYNC    = 4,        /**< Mirrors always in sync */
    LSM_VOLUME_REPLICATE_MIRROR_ASYNC   = 5         /**< Mirror partner updated with delay */
} lsmReplicationType;

/**< \enum lsmProvisionType Different types of provisioning */
typedef enum {
    LSM_PROVISION_UNKNOWN = -1,     /**< Unknown */
    LSM_PROVISION_THIN = 1,         /**< Thin provisioning */
    LSM_PROVISION_FULL = 2,         /**< Thick provisioning */
    LSM_PROVISION_DEFAULT = 3       /**< Default provisioning */
} lsmProvisionType;

/**< \enum lsmAccessType Different types of Volume access */
typedef enum {
    LSM_VOLUME_ACCESS_READ_ONLY = 1,    /**< Read only access */
    LSM_VOLUME_ACCESS_READ_WRITE = 2,   /**< Read write access */
    LSM_VOLUME_ACCESS_NONE = 3          /**< No access */
} lsmAccessType;

/**< \enum lsmVolumeStatusType Different states that a volume can be in */
typedef enum {
    LSM_VOLUME_STATUS_ONLINE = 1,   /**< Volume is ready to be used */
    LSM_VOLUME_STATUS_OFFLINE = 2   /**< Volume is offline, no access */
} lsmVolumeStatusType;

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
#define LSM_SYSTEM_STATUS_UNKNOWN               0x00000000  /**< System status unknown */
#define LSM_SYSTEM_STATUS_OK                    0x00000001  /**< System status OK */
#define LSM_SYSTEM_STATUS_DEGRADED              0x00000002  /**< System is degraded */
#define LSM_SYSTEM_STATUS_ERROR                 0x00000004  /**< System has error(s) */
#define LSM_SYSTEM_STATUS_PREDICTIVE_FAILURE    0x00000008  /**< System has predictive failure(s) */
#define LSM_SYSTEM_STATUS_VENDOR_SPECIFIC       0x00000010  /**< Vendor specific status code */

/**< \enum lsmInitiatorType Different types of initiator IDs */
typedef enum {
    LSM_INITIATOR_OTHER = 1,                    /**< Other or unspecified */
    LSM_INITIATOR_PORT_WWN = 2,                 /**< World wide port name */
    LSM_INITIATOR_NODE_WWN = 3,                 /**< World wide node name */
    LSM_INITIATOR_HOSTNAME = 4,                 /**< Host name */
    LSM_INITIATOR_ISCSI = 5                     /**< iSCSI IQN */
} lsmInitiatorType;

/**< \enum lsmJobType Different types of jobs */
typedef enum {
    LSM_JOB_VOL_CREATE  = 1,                    /**< Volume create */
    LSM_JOB_VOL_RESIZE = 2,                     /**< Volume re-size */
    LSM_JOB_VOL_REPLICATE = 3                   /**< Volume replicate */
} lsmJobType;

/**< \enum lsmJobStatus Job states */
typedef enum {
    LSM_JOB_INPROGRESS = 1,                     /**< Job is in progress */
    LSM_JOB_COMPLETE = 2,                       /**< Job is complete */
    LSM_JOB_STOPPED = 3,                        /**< Job is stopped */
    LSM_JOB_ERROR = 4                           /**< Job has errored */
} lsmJobStatus;

#ifdef  __cplusplus
}
#endif

#endif  /* LIBSTORAGEMGMT_TYPES_H */

