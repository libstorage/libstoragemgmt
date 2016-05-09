/*
 * Copyright (C) 2014 Red Hat, Inc.
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
 *
 */

#ifndef LIBSTORAGEMGMT_DISK_H
#define LIBSTORAGEMGMT_DISK_H

#include "libstoragemgmt_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Free the memory for a disk record
 * @param d     Disk memory to free
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_disk_record_free(lsm_disk *d);

/**
 * Copy a disk record
 * @param d     Disk record to copy
 * @return Copy of disk record
 */
lsm_disk LSM_DLL_EXPORT *lsm_disk_record_copy(lsm_disk *d);

/**
 * Free an array of disk records
 * @param disk      Array of disk records
 * @param size      Size of disk array
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_disk_record_array_free(lsm_disk *disk[],
                                              uint32_t size);

/**
 * Returns the disk id
 * Note: Return value is valid as long as disk pointer is valid.  It gets
 * freed when record is freed.
 * @param d     Disk record of interest
 * @return String id
 */
const char LSM_DLL_EXPORT *lsm_disk_id_get(lsm_disk *d);

/**
 * Returns the disk name
 * Note: Return value is valid as long as disk pointer is valid.  It gets
 * freed when record is freed.
 * @param d     Disk record of interest
 * @return Disk name
 */
const char LSM_DLL_EXPORT *lsm_disk_name_get(lsm_disk *d);

/**
 * Returns the disk type (enumeration)
 * Note: Return value is valid as long as disk pointer is valid.  It gets
 * freed when record is freed.
 * @param d     Disk record of interest
 * @return Disk type
 */
lsm_disk_type LSM_DLL_EXPORT lsm_disk_type_get(lsm_disk *d);

/**
 * Returns number of blocks for disk
 * Note: Return value is valid as long as disk pointer is valid.  It gets
 * freed when record is freed.
 * @param d     Disk record of interest
 * @return Number of logical blocks
 */
uint64_t LSM_DLL_EXPORT lsm_disk_number_of_blocks_get(lsm_disk *d);

/**
 * Returns the block size
 * Note: Return value is valid as long as disk pointer is valid.  It gets
 * freed when record is freed.
 * @param d     Disk record of interest
 * @return Block size in bytes
 */
uint64_t LSM_DLL_EXPORT lsm_disk_block_size_get(lsm_disk *d);

/**
 * Returns the disk status
 * Note: Return value is valid as long as disk pointer is valid.  It gets
 * freed when record is freed.
 * @param d     Disk record of interest
 * @return Status of the disk
 */
uint64_t LSM_DLL_EXPORT lsm_disk_status_get(lsm_disk *d);

/**
 * New in version 1.3.
 * Retrieves a disk's location.
 * Do not free returned string, free the struct lsm_disk instead.
 * @param d     Pointer to the disk of interest.
 * @return Disk location string. Return NULL if invalid argument, no support or
 * bug.
 */
LSM_DLL_EXPORT const char *lsm_disk_location_get(lsm_disk *d);

/**
 * New in version 1.3.
 * Retrieves a disk's rotation speed(revolutions per minute).
 * @param d     Pointer to the disk of interest.
 * @return Disk rotation speed - revolutions per minute(RPM).
 * @retval >1
 *              Normal rotational disk.
 * @retval LSM_DISK_RPM_NO_SUPPORT
 *              Not supported by plugin.
 * @retval LSM_DISK_RPM_NON_ROTATING_MEDIUM
 *              Non-rotating medium (e.g., SSD).
 * @retval LSM_DISK_RPM_ROTATING_UNKNOWN_SPEED
 *              Rotational disk with unknown speed.
 * @retval LSM_DISK_RPM_UNKNOWN
 *              Bug or invalid argument.
 */
int32_t LSM_DLL_EXPORT lsm_disk_rpm_get(lsm_disk *d);

/**
 * New in version 1.3.
 * Retrieves a disk link type.
 * @param d             Pointer to the disk of interest.
 * @return Disk link type - lsm_disk_link_type
 * @retval LSM_DISK_LINK_TYPE_NO_SUPPORT
 *              Plugin does not support this property.
 * @retval LSM_DISK_LINK_TYPE_UNKNOWN
 *              Given 'd' argument is NULL or plugin failed to detect link type.
 * @retval LSM_DISK_LINK_TYPE_FC
 *              Fibre Channel
 * @retval LSM_DISK_LINK_TYPE_SSA
 *              Serial Storage Architecture, Old IBM tech.
 * @retval LSM_DISK_LINK_TYPE_SBP
 *              Serial Bus Protocol, used by IEEE 1394.
 * @retval LSM_DISK_LINK_TYPE_SRP
 *              SCSI RDMA Protocol
 * @retval LSM_DISK_LINK_TYPE_ISCSI
 *              Internet Small Computer System Interface
 * @retval LSM_DISK_LINK_TYPE_SAS
 *              Serial Attached SCSI
 * @retval LSM_DISK_LINK_TYPE_ADT
 *              Automation/Drive Interface Transport Protocol, often used by
 *              Tape.
 * @retval LSM_DISK_LINK_TYPE_ATA
 *              PATA/IDE or SATA.
 * @retval LSM_DISK_LINK_TYPE_USB
 *              USB disk
 * @retval LSM_DISK_LINK_TYPE_SOP
 *              SCSI over PCI-E
 * @retval LSM_DISK_LINK_TYPE_PCIE
 *              PCI-E, e.g. NVMe
 */
lsm_disk_link_type LSM_DLL_EXPORT lsm_disk_link_type_get(lsm_disk *d);

/**
 * Returns the system id
 * Note: Return value is valid as long as disk pointer is valid.  It gets
 * freed when record is freed.
 * @param d     Disk record of interest
 * @return Which system the disk belongs too.
 */
const char LSM_DLL_EXPORT *lsm_disk_system_id_get(lsm_disk *d);

/**
 * News in version 1.3. Only available for direct attached storage system.
 * Returns the SCSI VPD83 NAA ID of disk. The VPD83 NAA ID could be used in
 * 'lsm_local_disk_vpd83_search()' when physical disk is exposed to OS directly
 * (also known as system HBA mode). Please be advised the capability
 * LSM_CAP_DISK_VPD83_GET only means plugin could query VPD83 for HBA mode disk,
 * for those physical disks acting as RAID member, plugin might return NULL as
 * their VPD83 NAA ID.
 * Note: Return value is valid as long as disk pointer is valid.  It gets
 * freed when record is freed.
 * @param d     Disk record of interest
 * @return string pointer of vpd83 NAA ID. NULL if not support or error.
 */
const char LSM_DLL_EXPORT *lsm_disk_vpd83_get(lsm_disk *d);

#ifdef __cplusplus
}
#endif
#endif                          /* LIBSTORAGEMGMT_DISK_H */
