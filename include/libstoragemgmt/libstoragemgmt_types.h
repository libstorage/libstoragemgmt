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

/**
 * Opaque data type for a connection.
 */
typedef struct _lsmConnect lsmConnect;
typedef lsmConnect *lsmConnectPtr;

/**
 * Opaque data type for a block based storage unit
 */
typedef struct _lsmVolume lsmVolume;
typedef lsmVolume *lsmVolumePtr;

/**
 * Opaque data type for a storage pool which is used as a base for Volumes etc.
 * to be created from.
 */
typedef struct _lsmPool lsmPool;
typedef lsmPool *lsmPoolPtr;

/**
 * Opaque data type for an initiator.
 */
typedef struct _lsmInitiator lsmInitiator;
typedef lsmInitiator *lsmInitiatorPtr;

/**
 * Opaque data type for storage capabilities.
 */
typedef struct _lsmStorageCapabilities lsmStorageCapabilities;
typedef lsmStorageCapabilities *lsmStorageCapabilitiesPtr;

/**
 * Access group
 */
typedef struct _lsmAccessGroup lsmAccessGroup;
typedef lsmAccessGroup *lsmAccessGroupPtr;

/**
 * Opaque data type for file system
 */
typedef struct _lsmFileSystem lsmFileSystem;
typedef lsmFileSystem *lsmFileSystemPtr;

/**
 * Opaque data type for snapshots
 */
typedef struct _lsmSnapShot lsmSnapShot;
typedef lsmFileSystem *lsmSnapShotPtr;

/**
 * Opaque data type for nfs exports
 */
typedef struct _lsmNfsExport lsmNfsExport;
typedef lsmNfsExport *lsmNfsExportPtr;

/**
 * Opaque data type for block ranges (regions to replicate)
 */
typedef struct _lsmBlockRange lsmBlockRange;
typedef lsmBlockRange *lsmBlockRangePtr;

/**
 * Opaque data type for systems.
 */
typedef struct _lsmSystem lsmSystem;
typedef lsmSystem *lsmSystemPtr;

/**
 * Opaque data type for string collection
 */
typedef struct _lsmStringList lsmStringList;
typedef lsmStringList *lsmStringListPtr;

/**
 * Opaque data type for file systems
 */
typedef struct _lsmFs lsmFs;
typedef lsmFs *lsmFsPtr;

/**
 * Opaque data type for snapshot
 */
typedef struct _lsmSs lsmSs;
typedef lsmSs *lsmSsPtr;

/**
 * Different types of replications that can be created
 */
typedef enum {
    LSM_VOLUME_REPLICATE_UNKNOWN = -1,
    LSM_VOLUME_REPLICATE_SNAPSHOT = 1,
    LSM_VOLUME_REPLICATE_CLONE    = 2,
    LSM_VOLUME_REPLICATE_COPY     = 3,
    LSM_VOLUME_REPLICATE_MIRROR   = 4,
} lsmReplicationType;

/**
 * Different types of provisioning.
 */
typedef enum {
    LSM_PROVISION_UNKNOWN = -1,
    LSM_PROVISION_THIN = 1,        /**< Thin provisioning */
    LSM_PROVISION_FULL = 2,        /**< Thick provisioning */
    LSM_PROVISION_DEFAULT = 3,     /**< Default provisioning */
} lsmProvisionType;

/**
 * Different types of Volume access
 */
typedef enum {
    LSM_VOLUME_ACCESS_READ_ONLY = 1,
    LSM_VOLUME_ACCESS_READ_WRITE = 2,
    LSM_VOLUME_ACCESS_NONE = 3,
} lsmAccessType;

/**
 * Different states that a volume can be in
 */
typedef enum {
    LSM_VOLUME_STATUS_ONLINE = 1,  /**< Volume is ready to be used */
    LSM_VOLUME_STATUS_OFFLINE = 2, /**< Volume is offline, no access */
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
 * Different types of initiator IDs
 */
typedef enum {
    LSM_INITIATOR_OTHER = 1,
    LSM_INITIATOR_PORT_WWN = 2,
    LSM_INITIATOR_NODE_WWN = 3,
    LSM_INITIATOR_HOSTNAME = 4,
    LSM_INITIATOR_ISCSI = 5,
} lsmInitiatorType;

/**
 * Different types of jobs.
 */
typedef enum {
    LSM_JOB_VOL_CREATE  = 1,
    LSM_JOB_VOL_RESIZE = 2,
    LSM_JOB_VOL_REPLICATE = 3,
} lsmJobType;

typedef enum {
    LSM_JOB_INPROGRESS = 1,
    LSM_JOB_COMPLETE = 2,
    LSM_JOB_STOPPED = 3,
    LSM_JOB_ERROR = 4
} lsmJobStatus;

#ifdef  __cplusplus
}
#endif

#endif  /* LIBSTORAGEMGMT_TYPES_H */

