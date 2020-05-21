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
 * lsm_disk_record_free - Free the lsm_disk memory.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory resources for the specified lsm_disk.
 *
 * @d:
 *      Record to release
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When not a valid lsm_disk pointer.
 *
 */
int LSM_DLL_EXPORT lsm_disk_record_free(lsm_disk *d);

/**
 * lsm_disk_record_copy - Duplicates a lsm_disk record.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Duplicates a lsm_disk record.
 *
 * @d:
 *      Pointer of lsm_disk to duplicate.
 *
 * Return:
 *      Pointer of lsm_disk. NULL on memory allocation failure. Should be
 *      freed by lsm_disk_record_free().
 */
lsm_disk LSM_DLL_EXPORT *lsm_disk_record_copy(lsm_disk *d);

/**
 * lsm_disk_record_array_free - Free the memory of lsm_disk array.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory resources for an array of lsm_disk.
 *
 * @disk:
 *      Array to release memory for.
 * @size:
 *      Number of elements.
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When not a valid lsm_disk pointer.
 *
 */
int LSM_DLL_EXPORT lsm_disk_record_array_free(lsm_disk *disk[], uint32_t size);

/**
 * lsm_disk_id_get - Retrieves the ID of the disk.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the ID for the disk.
 *      Note: Address returned is valid until lsm_disk gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @d:
 *      Disk to retrieve id for.
 *
 * Return:
 *      string. NULL if argument 'd' is NULL or not a valid lsm_disk pointer.
 */
const char LSM_DLL_EXPORT *lsm_disk_id_get(lsm_disk *d);

/**
 * lsm_disk_name_get - Retrieves the name for the lsm_disk.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the name for the lsm_disk.
 *      Note: Address returned is valid until lsm_disk gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @d:
 *      Disk to retrieve name for.
 *
 * Return:
 *      string. NULL if argument 'd' is NULL or not a valid lsm_disk pointer.
 */
const char LSM_DLL_EXPORT *lsm_disk_name_get(lsm_disk *d);

/**
 * lsm_disk_type_get - Retrieves the disk type.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the type for the disk. Possible values are:
 *          * LSM_DISK_TYPE_UNKNOWN
 *              Unknown of argument 'd' is NULL.
 *          * LSM_DISK_TYPE_OTHER
 *              Vendor specific.
 *          * LSM_DISK_TYPE_ATA
 *              IDE/ATA disk.
 *          * LSM_DISK_TYPE_SATA
 *              SATA disk.
 *          * LSM_DISK_TYPE_SAS
 *              SAS disk.
 *          * LSM_DISK_TYPE_FC
 *              FC disk.
 *          * LSM_DISK_TYPE_SOP
 *              SCSI over PCI-E for Solid State Storage.
 *          * LSM_DISK_TYPE_SCSI
 *              SCSI disk.
 *          * LSM_DISK_TYPE_LUN
 *              LUN from external storage array.
 *          * LSM_DISK_TYPE_NL_SAS
 *              NL_SAS disk (SATA disk using SAS interface).
 *          * LSM_DISK_TYPE_HDD
 *              Failback value for hard disk drive(rotational).
 *          * LSM_DISK_TYPE_SSD
 *              Solid State Disk.
 *          * LSM_DISK_TYPE_HYBRID
 *              Combination of HDD and SSD.
 *
 * @d:
 *      Disk to retrieve type for.
 *
 * Return:
 *      string. NULL if argument 'd' is NULL or not a valid lsm_disk pointer.
 */
lsm_disk_type LSM_DLL_EXPORT lsm_disk_type_get(lsm_disk *d);

/**
 * lsm_disk_number_of_blocks_get -  Retrieves the block count for the disk.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the block count for the disk.
 *
 * @d:
 *      Disk to retrieve block count for.
 *
 * Return:
 *      uint64_t. 0 if argument 'd' is NULL or not a valid lsm_disk pointer.
 */
uint64_t LSM_DLL_EXPORT lsm_disk_number_of_blocks_get(lsm_disk *d);

/**
 * lsm_disk_block_size_get -  Retrieves the block size for the disk.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the block size in bytes for the disk.
 *
 * @d:
 *      Disk to retrieve block size for.
 *
 * Return:
 *      uint64_t. 0 if argument 'd' is NULL or not a valid lsm_disk pointer.
 */
uint64_t LSM_DLL_EXPORT lsm_disk_block_size_get(lsm_disk *d);

/**
 * lsm_disk_status_get - Retrieves status of specified disk.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves status of the specified disk.
 *
 * @d:
 *      Disk to retrieve status for.
 *
 * Return:
 *      uint64_t. Status of the specified disk which is a bit sensitive field.
 *      Possible values are:
 *          * LSM_DISK_STATUS_UNKNOWN
 *              Plugin failed to query out the status of disk.
 *          * LSM_DISK_STATUS_OK
 *              Everything is OK.
 *          * LSM_DISK_STATUS_OTHER
 *              Vendor specific status.
 *          * LSM_DISK_STATUS_PREDICTIVE_FAILURE
 *              Disk is still functional but will fail soon.
 *          * LSM_DISK_STATUS_ERROR
 *              Error make disk not functional.
 *          * LSM_DISK_STATUS_REMOVED
 *              Disk was removed by administrator.
 *          * LSM_DISK_STATUS_STARTING
 *              Disk is starting up.
 *          * LSM_DISK_STATUS_STOPPING
 *              Disk is shutting down.
 *          * LSM_DISK_STATUS_STOPPED
 *              Disk is stopped by administrator.
 *          * LSM_DISK_STATUS_INITIALIZING
 *              Disk is not functional yet, internal storage system is
 *              initializing this disk, it could be:
 *                  * Initialising new disk.
 *                  * Zeroing disk.
 *                  * Scrubbing disk data.
 *          * LSM_DISK_STATUS_MAINTENANCE_MODE
 *              In maintenance for bad sector scan, integrity check and etc It
 *              might be combined with LSM_DISK_STATUS_OK or
 *              LSM_DISK_STATUS_STOPPED for online maintenance or offline
 *              maintenance.
 *          * LSM_DISK_STATUS_SPARE_DISK
 *              Disk is configured as spare disk.
 *          * LSM_DISK_STATUS_RECONSTRUCT
 *              Disk is reconstructing its data.
 *          * LSM_DISK_STATUS_FREE
 *              New in version 1.2, indicate the whole disk is not holding any
 *              data or acting as a dedicate spare disk. This disk could be
 *              assigned as a dedicated spare disk or used for creating pool.
 *              If any spare disk(like those on NetApp ONTAP) does not require
 *              any explicit action when assigning to pool, it should be treated
 *              as free disk and marked as
 *              LSM_DISK_STATUS_FREE|LSM_DISK_STATUS_SPARE_DISK.
 *
 */
uint64_t LSM_DLL_EXPORT lsm_disk_status_get(lsm_disk *d);

/**
 * lsm_disk_location_get - Retrieves the location for the disk.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Retrieve the disk location.
 *      Note: Address returned is valid until lsm_disk gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * Capability:
 *      LSM_CAP_DISK_LOCATION
 *
 * @d:
 *      Disk to retrieve location for.
 *
 * Return:
 *      string. NULL if argument 'd' is NULL or not a valid lsm_disk pointer
 *      or not supported.
 */
LSM_DLL_EXPORT const char *lsm_disk_location_get(lsm_disk *d);

/**
 * lsm_disk_rpm_get - Retrieves the rotation speed for the disk.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Retrieves the disk rotation speed - revolutions per minute(RPM).
 *
 * Capability:
 *      LSM_CAP_DISK_RPM
 *
 * @d:
 *      Disk to retrieve rotation speed for.
 *
 * Return:
 *      int32_t. Disk rotation speed. Possible values:
 *          * >1
 *              Normal rotational disk.
 *          * LSM_DISK_RPM_NO_SUPPORT
 *              Not supported by plugin.
 *          * LSM_DISK_RPM_NON_ROTATING_MEDIUM
 *              Non-rotating medium (e.g., SSD).
 *          * LSM_DISK_RPM_ROTATING_UNKNOWN_SPEED
 *              Rotational disk with unknown speed.
 *          * LSM_DISK_RPM_UNKNOWN
 *              Bug or invalid argument or not supported.
 */
int32_t LSM_DLL_EXPORT lsm_disk_rpm_get(lsm_disk *d);

/**
 * lsm_disk_link_type_get - Retrieves the link type for the disk.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Retrieves the disk physical link type.
 *
 * Capability:
 *      LSM_CAP_DISK_LINK_TYPE
 *
 * @d:
 *      Disk to retrieve link type for.
 *
 * Return:
 *      lsm_disk_link_type. Disk link type. Possible values:
 *          * LSM_DISK_LINK_TYPE_NO_SUPPORT
 *              Plugin does not support this property.
 *          * LSM_DISK_LINK_TYPE_UNKNOWN
 *              Given 'd' argument is NULL or plugin failed to detect link type.
 *          * LSM_DISK_LINK_TYPE_FC
 *              Fibre Channel
 *          * LSM_DISK_LINK_TYPE_SSA
 *              Serial Storage Architecture, Old IBM tech.
 *          * LSM_DISK_LINK_TYPE_SBP
 *              Serial Bus Protocol, used by IEEE 1394.
 *          * LSM_DISK_LINK_TYPE_SRP
 *              SCSI RDMA Protocol
 *          * LSM_DISK_LINK_TYPE_ISCSI
 *              Internet Small Computer System Interface
 *          * LSM_DISK_LINK_TYPE_SAS
 *              Serial Attached SCSI
 *          * LSM_DISK_LINK_TYPE_ADT
 *              Automation/Drive Interface Transport Protocol, often used by
 *              Tape.
 *          * LSM_DISK_LINK_TYPE_ATA
 *              PATA/IDE or SATA.
 *          * LSM_DISK_LINK_TYPE_USB
 *              USB disk
 *          * LSM_DISK_LINK_TYPE_SOP
 *              SCSI over PCI-E
 *          * LSM_DISK_LINK_TYPE_PCIE
 *              PCI-E, e.g. NVMe
 */
lsm_disk_link_type LSM_DLL_EXPORT lsm_disk_link_type_get(lsm_disk *d);

/**
 * lsm_disk_system_id_get - Retrieve the system ID for the disk.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieve the system id for the specified disk.
 *      Note: Address returned is valid until lsm_disk gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @d:
 *      Disk to retrieve system ID for.
 *
 * Return:
 *      string. NULL if argument 'd' is NULL or not a valid lsm_disk pointer.
 */
const char LSM_DLL_EXPORT *lsm_disk_system_id_get(lsm_disk *d);

/**
 * lsm_disk_vpd83_get - Retrieve the SCSI VPD 0x83 ID for the disk.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Retrieve the system id for the specified disk.
 *      Only available for direct attached storage system.
 *      Returns the SCSI VPD83 NAA ID of disk. The VPD83 NAA ID could be used in
 *      lsm_local_disk_vpd83_search when physical disk is exposed to OS
 *      directly (also known as system HBA mode). Please be advised the
 *      capability LSM_CAP_DISK_VPD83_GET only means plugin could query VPD83
 *      for HBA mode disk, for those physical disks acting as RAID member,
 *      plugin might return NULL as their VPD83 NAA ID.
 *      Note: Address returned is valid until lsm_disk gets freed, copy return
 *      value if you need longer scope. Do not free returned string.
 *
 * @d:
 *      Disk to retrieve system ID for.
 *
 * Return:
 *      string. NULL if argument 'd' is NULL or not a valid lsm_disk pointer.
 */
const char LSM_DLL_EXPORT *lsm_disk_vpd83_get(lsm_disk *d);

#ifdef __cplusplus
}
#endif
#endif /* LIBSTORAGEMGMT_DISK_H */
