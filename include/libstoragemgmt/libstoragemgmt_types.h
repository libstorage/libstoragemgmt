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
typedef struct lsmConnect *lsmConnectPtr;

/**
 * Opaque data type for a block based storage unit
 */
typedef struct lsmVolume *lsmVolumePtr;

/**
 * Opaque data type for a storage pool which is used as a base for Volumes etc.
 * to be created from.
 */
typedef struct lsmPool *lsmPoolPtr;

/**
 * Opaque data type for an initiator.
 */
typedef struct lsmInitiator *lsmInitiatorPtr;

/**
 * Opaque data type for storage capabilities.
 */
typedef struct lsmStorageCapabilities *lsmStorageCapabilitiesPtr;

/**
 * Access group
 */
typedef struct lsmAccessGroup *lsmAccessGroupPtr;

/**
 * @page enum-notes Enumeration design notes
 * The enum values have been created so that if a user inadvertently uses the
 * wrong enum type for a given function we will be able to detect it in the
 * library and return an invalid input type.  The strategy is to make each enum
 * unique for the entire library.
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
    LSM_PROVISION_THIN = 0x0010,
    LSM_PROVISION_FULL = 0x0020
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



#ifdef	__cplusplus
}
#endif

#endif	/* LIBSTORAGEMGMT_TYPES_H */

