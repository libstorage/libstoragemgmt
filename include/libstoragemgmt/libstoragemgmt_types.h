/*
 * Copyright (C) 2011 Red Hat, Inc.
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Author: tasleson
 */

#ifndef LIBSTORAGEMGMT_TYPES_H
#define	LIBSTORAGEMGMT_TYPES_H

#ifdef	__cplusplus
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
 * @page enum-notes Enumeration design notes
 * The enum values have been created so that if a user inadvertently uses the
 * wrong enum type (casting) for a given function we will be able to detect it
 * in the library and return an invalid input type.  The strategy is to make
 * each enum unique for the entire library.  Perhaps this is more trouble than
 * it is worth :-).
 */

/**
 * Different types of replications that can be created
 */
typedef enum {
    LSM_VOLUME_REPLICATE_SNAPSHOT = 0x0001,
    LSM_VOLUME_REPLICATE_CLONE    = 0x0002,
    LSM_VOLUME_REPLICATE_MIRROR   = 0x0003
} lsmReplicationType;

/**
 * Different types of provisioning.
 */
typedef enum {
    LSM_PROVISION_THIN = 0x0010,        /**< Thin provisioning */
    LSM_PROVISION_FULL = 0x0020,        /**< Thick provisioning */
    LSM_PROVISION_DEFAULT = 0x0030,     /**< Default provisioning */
} lsmProvisionType;

/**
 * Different types of Volume access
 */
typedef enum {
    LSM_VOLUME_ACCESS_READ_ONLY = 0x0100,
    LSM_VOLUME_ACCESS_READ_WRITE = 0x0200,
    LSM_VOLUME_ACCESS_NONE = 0x0300
} lsmAccessType;

/**
 * Different states that a volume can be in
 */
typedef enum {
    LSM_VOLUME_STATUS_ONLINE = 0x1000,  /**< Volume is ready to be used */
    LSM_VOLUME_STATUS_OFFLINE = 0x2000, /**< Volume is offline, no access */
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
    LSM_INITIATOR_WWN = 0x00010000,
    LSM_INITIATOR_ISCSI = 0x0020000,
} lsmInitiatorTypes;

#ifdef	__cplusplus
}
#endif

#endif	/* LIBSTORAGEMGMT_TYPES_H */

