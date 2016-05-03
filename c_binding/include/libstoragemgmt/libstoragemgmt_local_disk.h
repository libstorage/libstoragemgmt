/*
 * Copyright (C) 2015-2016 Red Hat, Inc.
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
 * Author: Gris Ge <fge@redhat.com>
 *
 */

#ifndef LIBSTORAGEMGMT_LOCAL_DISK_H
#define LIBSTORAGEMGMT_LOCAL_DISK_H

#include "libstoragemgmt_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * New in version 1.3.
 * Search all the disk paths of given SCSI VPD 0x83 page NAA type ID.
 * For any ATA and other non-SCSI protocol disks supporting VPD 0x83 pages NAA
 * ID, their disk path will also be included.
 * @param[in] vpd83
 *                  String. The SCSI VPD 0x83 page NAA type ID.
 * @param[out] disk_path_list
 *                  Output pointer of lsm_string_list. The format of
 *                  disk path will be like "/dev/sdb" for SCSI or ATA disk.
 *                  NULL if no found or got error.
 *                  Memory should be freed by lsm_string_list_free().
 * @param[out] lsm_err
 *                  Output pointer of lsm_error. Error message could be
 *                  retrieved via lsm_error_message_get(). Memory should be
 *                  freed by lsm_error_free().
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK               on success or not found.
 * @retval LSM_ERR_INVALID_ARGUMENT when any argument is NULL.
 * @retval LSM_ERR_NO_MEMORY        when no memory.
 * @retval LSM_ERR_LIB_BUG          when something unexpected happens.
 */
int LSM_DLL_EXPORT lsm_local_disk_vpd83_search(const char *vpd83,
                                               lsm_string_list **disk_path_list,
                                               lsm_error **lsm_err);

/**
 * New in version 1.3.
 * Query the SCSI VPD 0x83 page NAA type ID of given disk path.
 * @param[in]  disk_path
 *                  String. The path of disk path, example "/dev/sdb".
 * @param[out] vpd83
 *                  Output pointer of SCSI VPD83 NAA ID. The format is:
 *                  (?:^6[0-9a-f]{31})|(?:^[235][0-9a-f]{15})$
 *                  NULL when error. Memory should be freed by free().
 * @param[out] lsm_err
 *                  Output pointer of lsm_error. Error message could be
 *                  retrieved via lsm_error_message_get(). Memory should be
 *                  freed by lsm_error_free().
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK               on success or not found.
 * @retval LSM_ERR_INVALID_ARGUMENT when any argument is NULL
 * @retval LSM_ERR_NO_MEMORY        when no memory.
 * @retval LSM_ERR_LIB_BUG          when something unexpected happens.
 * @retval LSM_ERR_NOT_FOUND_DISK   when provided disk path not found.
 */
int LSM_DLL_EXPORT lsm_local_disk_vpd83_get(const char *disk_path,
                                            char **vpd83,
                                            lsm_error **lsm_err);

/**
 * New in version 1.3.
 * Query the disk rotation speed - revolutions per minute(RPM) of given disk
 * path.
 * Requires permission to open disk path(root user or disk group).
 * @param[in]  disk_path
 *                      String. The path of disk block, example: "/dev/sdb",
 *                      "/dev/nvme0n1".
 * @param[out] rpm      Output pointer of int32_t.
 *                          -1 (LSM_DISK_RPM_UNKNOWN):
 *                              Unknown RPM
 *                           0 (LSM_DISK_RPM_NON_ROTATING_MEDIUM):
 *                              Non-rotating medium (e.g., SSD)
 *                           1 (LSM_DISK_RPM_ROTATING_UNKNOWN_SPEED):
 *                              Rotational disk with unknown speed
 *                          >1:
 *                              Normal rotational disk (e.g., HDD)
 * @param[out] lsm_err
 *                      Output pointer of lsm_error. Error message could be
 *                      retrieved via lsm_error_message_get(). Memory should be
 *                      freed by lsm_error_free().
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK               on success or not found.
 * @retval LSM_ERR_INVALID_ARGUMENT when any argument is NULL.
 * @retval LSM_ERR_LIB_BUG          when something unexpected happens.
 * @retval LSM_ERR_NOT_FOUND_DISK   when provided disk path not found.
 * @retval LSM_ERR_PERMISSION_DENIED no sufficient permission to access
 *                                   provided disk path.
 */
int LSM_DLL_EXPORT lsm_local_disk_rpm_get(const char *disk_path, int32_t *rpm,
                                          lsm_error **lsm_err);

/**
 * New in version 1.3.
 * Query local disk paths. Currently, only SCSI, ATA and NVMe disks will be
 * included.
 * @param[out]  disk_paths
 *                      lsm_string_list pointer.
 *                      The disk_path string format is '/dev/sd[a-z]+' for SCSI
 *                      and ATA disks, '/dev/nvme[0-9]+n[0-9]+' for NVMe disks.
 *                      Empty lsm_string_list but not NULL will be returned
 *                      if no disk found.
 *                      Memory should be freed by lsm_string_list_free().
 * @param[out] lsm_err
 *                      Output pointer of lsm_error. Error message could be
 *                      retrieved via lsm_error_message_get(). Memory should be
 *                      freed by lsm_error_free().
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK               on success or not found.
 * @retval LSM_ERR_INVALID_ARGUMENT when any argument is NULL.
 * @retval LSM_ERR_LIB_BUG          when something unexpected happens.
 */
int LSM_DLL_EXPORT lsm_local_disk_list(lsm_string_list **disk_paths,
                                       lsm_error **lsm_err);

/**
 * New in version 1.3.
 * Query the disk link type of given disk path.
 * For SATA disks connected to SAS SES enclosure, will return
 * LSM_SCSI_LINK_TYPE_ATA.
 * Require permission to open /dev/sdX(root user or disk group).
 * @param[in]  disk_path    String. The path of disk, example "/dev/sdb".
 * @param[out] link_type    Output pointer of lsm_disk_link_type.
 *                          LSM_DISK_LINK_TYPE_UNKNOWN when error.
 *                          Possible values are:
 *                              LSM_DISK_LINK_TYPE_UNKNOWN
 *                                  Unknown
 *                              LSM_DISK_LINK_TYPE_FC
 *                                  Fibre Channel
 *                              LSM_DISK_LINK_TYPE_SSA
 *                                  Serial Storage Architecture, Old IBM tech.
 *                              LSM_DISK_LINK_TYPE_SBP
 *                                  Serial Bus Protocol, used by IEEE 1394.
 *                              LSM_DISK_LINK_TYPE_SRP
 *                                  SCSI RDMA Protocol
 *                              LSM_DISK_LINK_TYPE_ISCSI
 *                                  Internet Small Computer System Interface
 *                              LSM_DISK_LINK_TYPE_SAS
 *                                  Serial Attached SCSI
 *                              LSM_DISK_LINK_TYPE_ADT
 *                                  Automation/Drive Interface Transport
 *                                  Protocol, often used by Tape.
 *                              LSM_DISK_LINK_TYPE_ATA
 *                                  PATA/IDE or SATA.
 *                              LSM_DISK_LINK_TYPE_USB
 *                                  USB disk
 *                              LSM_DISK_LINK_TYPE_SOP
 *                                  SCSI over PCI-E
 *                              LSM_DISK_LINK_TYPE_PCIE
 *                                  PCI-E, e.g. NVMe
 *
 * @param[out] lsm_err  Output pointer of lsm_error. Error message could be
 *                      retrieved via lsm_error_message_get(). Memory should
 *                      be freed by lsm_error_free().
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK               on success or not found.
 * @retval LSM_ERR_INVALID_ARGUMENT when any argument is NULL.
 * @retval LSM_ERR_LIB_BUG          when something unexpected happens.
 * @retval LSM_ERR_NOT_FOUND_DISK   when provided disk path not found.
 * @retval LSM_ERR_PERMISSION_DENIED insufficient permission to access
 *                                   provided disk path.
 */
int LSM_DLL_EXPORT lsm_local_disk_link_type_get(const char *disk_path,
                                                lsm_disk_link_type *link_type,
                                                lsm_error **lsm_err);

/**
 * New in version 1.3.
 * Turn on the identification LED for specified disk.
 * Require read and write access to specified disk path.
 * @param[in]  disk_path    String. The path of disk path, example "/dev/sdb".
 * @param[out] lsm_err      Output pointer of lsm_error. Error message could be
 *                          retrieved via lsm_error_message_get(). Memory should
 *                          be freed by lsm_error_free().
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK                   On success.
 * @retval LSM_ERR_INVALID_ARGUMENT     When any argument is NULL.
 * @retval LSM_ERR_NO_MEMORY            When no memory.
 * @retval LSM_ERR_LIB_BUG              When something unexpected happens.
 * @retval LSM_ERR_NOT_FOUND_DISK       When provided disk path not found.
 * @retval LSM_ERR_NO_SUPPORT           Action is not supported.
 * @retval LSM_ERR_PERMISSION_DENIED    Insufficient permission to access
 *                                      provided disk path.
 */
int LSM_DLL_EXPORT lsm_local_disk_ident_led_on(const char *disk_path,
                                               lsm_error **lsm_err);
/**
 * New in version 1.3.
 * Turn off the identification LED for specified disk.
 * Require read and write access to specified disk path.
 * @param[in]  disk_path    String. The path of disk path, example "/dev/sdb".
 * @param[out] lsm_err      Output pointer of lsm_error. Error message could be
 *                          retrieved via lsm_error_message_get().
 *                          Memory should be freed by lsm_error_free().
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK                   On success.
 * @retval LSM_ERR_INVALID_ARGUMENT     When any argument is NULL.
 * @retval LSM_ERR_NO_MEMORY            When no memory.
 * @retval LSM_ERR_LIB_BUG              When something unexpected happens.
 * @retval LSM_ERR_NOT_FOUND_DISK       When provided disk path not found.
 * @retval LSM_ERR_NO_SUPPORT           Action is not supported.
 * @retval LSM_ERR_PERMISSION_DENIED    Insufficient permission to access
 *                                      provided disk path.
 */
int LSM_DLL_EXPORT lsm_local_disk_ident_led_off(const char *disk_path,
                                                  lsm_error **lsm_err);

/**
 * New in version 1.3.
 * Turn on the fault LED for specified disk.
 * Require read and write access to specified disk path.
 * @param[in]  disk_path    String. The path of disk path, example "/dev/sdb".
 * @param[out] lsm_err      Output pointer of lsm_error. Error message could be
 *                          retrieved via lsm_error_message_get(). Memory should
 *                          be freed by lsm_error_free().
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK                   On success.
 * @retval LSM_ERR_INVALID_ARGUMENT     When any argument is NULL.
 * @retval LSM_ERR_NO_MEMORY            When no memory.
 * @retval LSM_ERR_LIB_BUG              When something unexpected happens.
 * @retval LSM_ERR_NOT_FOUND_DISK       When provided disk path not found.
 * @retval LSM_ERR_NO_SUPPORT           Action is not supported.
 * @retval LSM_ERR_PERMISSION_DENIED    Insufficient permission to access
 *                                      provided disk path.
 */
int LSM_DLL_EXPORT lsm_local_disk_fault_led_on(const char *disk_path,
                                               lsm_error **lsm_err);
/**
 * New in version 1.3.
 * Turn off the fault LED for specified disk.
 * Require read and write access to specified disk path.
 * @param[in]  disk_path    String. The path of disk path, example "/dev/sdb".
 * @param[out] lsm_err      Output pointer of lsm_error. Error message could be
 *                          retrieved via lsm_error_message_get(). Memory should
 *                          be freed by lsm_error_free().
 * @retval LSM_ERR_OK                   On success.
 * @retval LSM_ERR_INVALID_ARGUMENT     When any argument is NULL.
 * @retval LSM_ERR_NO_MEMORY            When no memory.
 * @retval LSM_ERR_LIB_BUG              When something unexpected happens.
 * @retval LSM_ERR_NOT_FOUND_DISK       When provided disk path not found.
 * @retval LSM_ERR_NO_SUPPORT           Action is not supported.
 * @retval LSM_ERR_PERMISSION_DENIED    Insufficient permission to access
 *                                      provided disk path.
 */
int LSM_DLL_EXPORT lsm_local_disk_fault_led_off(const char *disk_path,
                                                lsm_error **lsm_err);

#ifdef __cplusplus
}
#endif
#endif                          /* LIBSTORAGEMGMT_LOCAL_DISK_H */
