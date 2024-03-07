/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2015-2023 Red Hat, Inc.
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
 * lsm_local_disk_vpd83_search - Search disks by VPD83 string.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Search all the disk paths of specified SCSI VPD 0x83 page NAA type ID.
 *      For any ATA and other non-SCSI protocol disks supporting VPD 0x83 pages
 *      NAA ID, their disk path will also be included.
 *
 * @vpd83:
 *      String. The SCSI VPD 0x83 page NAA type ID.
 * @disk_path_list:
 *      Output pointer of &lsm_string_list. The format of
 *      disk path will be like "/dev/sdb" for SCSI or ATA disk.
 *      NULL if no found or got error.
 *      Memory should be freed by lsm_string_list_free().
 * @lsm_err:
 *      Output pointer of &lsm_error. Error message could be
 *      retrieved via lsm_error_message_get(). Memory should be
 *      freed by lsm_error_free().
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL.
 *          * LSM_ERR_NO_MEMORY
 *              When no memory.
 *          * LSM_ERR_LIB_BUG
 *              When something unexpected happens.
 *
 */
int LSM_DLL_EXPORT lsm_local_disk_vpd83_search(const char *vpd83,
                                               lsm_string_list **disk_path_list,
                                               lsm_error **lsm_err);

/**
 * lsm_local_disk_serial_num_get - Query serial number.
 * Version:
 *      1.4
 *
 * Description:
 *      Query the serial number of specified disk path.
 *      For SCSI/SAS/SATA/ATA disks, it will be extracted from SCSI VPD 0x80
 *      page.
 *
 * @disk_path:
 *      String. The path of disk path, example "/dev/sdb".
 * @serial_num:
 *      Output pointer of SCSI VPD80 serial number.
 *      NULL when error. Memory should be freed by free().
 * @lsm_err:
 *      Output pointer of lsm_error. Error message could be
 *      retrieved via lsm_error_message_get(). Memory should be
 *      freed by lsm_error_free().
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL
 *          * LSM_ERR_NO_MEMORY
 *              When no memory.
 *          * LSM_ERR_LIB_BUG
 *              When something unexpected happens.
 *          * LSM_ERR_NOT_FOUND_DISK
 *              When provided disk path not found.
 *
 */
int LSM_DLL_EXPORT lsm_local_disk_serial_num_get(const char *disk_path,
                                                 char **serial_num,
                                                 lsm_error **lsm_err);

/**
 * lsm_local_disk_vpd83_get - Query scsi VPD 0x83 NAA ID.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Query the SCSI VPD 0x83 page NAA type ID of specified disk path.
 *
 * @disk_path:
 *      String. The path of disk path, example "/dev/sdb".
 * @vpd83:
 *      Output pointer of SCSI VPD83 NAA ID. The format is:
 *          (?:^6[0-9a-f]{31})|(?:^[235][0-9a-f]{15})$
 *      Set to NULL when error. Memory should be freed by free().
 * @lsm_err:
 *      Output pointer of lsm_error. Error message could be retrieved via
 *      lsm_error_message_get(). Memory should be freed by lsm_error_free().
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL
 *          * LSM_ERR_NO_MEMORY
 *              When no memory.
 *          * LSM_ERR_LIB_BUG
 *              When something unexpected happens.
 *          * LSM_ERR_NOT_FOUND_DISK
 *              When provided disk path not found.
 *
 */
int LSM_DLL_EXPORT lsm_local_disk_vpd83_get(const char *disk_path, char **vpd83,
                                            lsm_error **lsm_err);

/**
 * lsm_local_disk_health_status_get - Query the health status of local disk.
 *
 * Version:
 *      1.5
 *
 * Description:
 *      Query the health status of the specified disk path.
 *
 * @disk_path:
 *      String. The disk path, example "/dev/sdc".
 * @health_status:
 *      Output pointer of int32_t. Possible values are:
 *          * LSM_DISK_HEALTH_STATUS_UNKNOWN
 *              Unsupported or failed to retrieved health status.
 *          * LSM_DISK_HEALTH_STATUS_FAIL
 *          * LSM_DISK_HEALTH_STATUS_WARN
 *          * LSM_DISK_HEALTH_STATUS_GOOD
 * @lsm_err:
 *      Output pointer of lsm_error. Error message could be retrieved via
 *      lsm_error_message_get(). Memory should be freed by lsm_error_free().
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL
 *          * LSM_ERR_NO_MEMORY
 *              When no memory.
 *          * LSM_ERR_LIB_BUG
 *              When something unexpected happens.
 *          * LSM_ERR_NOT_FOUND_DISK
 *              When provided disk path not found.
 */
int LSM_DLL_EXPORT lsm_local_disk_health_status_get(const char *disk_path,
                                                    int32_t *health_status,
                                                    lsm_error **lsm_err);

/**
 * lsm_local_disk_rpm_get - Query disk rotation speed.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Query the disk rotation speed - revolutions per minute(RPM) of
 *      specified disk path. Requires permission to open disk path(root user or
 *      disk group).
 *
 *      Possible values of rpm are:
 *
 *          * -1(LSM_DISK_RPM_UNKNOWN):
 *              Unknown RPM.
 *          * 0(LSM_DISK_RPM_NON_ROTATING_MEDIUM):
 *              Non-rotating medium (e.g., SSD).
 *
 *          * 1(LSM_DISK_RPM_ROTATING_UNKNOWN_SPEED):
 *              Rotational disk with unknown speed.
 *          * >1:
 *              Normal rotational disk (e.g., HDD).
 *
 * @disk_path:
 *      String. The path of disk block, example: "/dev/sdb", "/dev/nvme0n1".
 * @rpm:
 *      Output pointer of int32_t.
 * @lsm_err:
 *      Output pointer of lsm_error. Error message could be retrieved via
 *      lsm_error_message_get(). Memory should be freed by lsm_error_free().
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL
 *          * LSM_ERR_LIB_BUG
 *              When something unexpected happens.
 *          * LSM_ERR_NOT_FOUND_DISK
 *              When provided disk path not found.
 *          * LSM_ERR_PERMISSION_DENIED
 *              No sufficient permission to access provided disk path.
 *
 */
int LSM_DLL_EXPORT lsm_local_disk_rpm_get(const char *disk_path, int32_t *rpm,
                                          lsm_error **lsm_err);

/**
 * lsm_local_disk_list - Query local disks.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Query local disk paths. Currently, only SCSI, SAS, ATA and NVMe disks
 *      will be included.
 *
 * @disk_paths:
 *      lsm_string_list pointer.
 *      The disk_path string format is "/dev/sd[a-z]+" for SCSI and ATA disks,
 *      "/dev/nvme[0-9]+n[0-9]+" for NVMe disks. Empty lsm_string_list but not
 *      NULL will be returned if no disk found. Memory should be freed by
 *      lsm_string_list_free().
 * @lsm_err:
 *      Output pointer of lsm_error. Error message could be retrieved via
 *      lsm_error_message_get(). Memory should be freed by lsm_error_free().
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL
 *          * LSM_ERR_NO_MEMORY
 *              When no memory.
 *          * LSM_ERR_LIB_BUG
 *              When something unexpected happens.
 *
 */
int LSM_DLL_EXPORT lsm_local_disk_list(lsm_string_list **disk_paths,
                                       lsm_error **lsm_err);

/**
 * lsm_local_disk_link_type_get - Query disk link type.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Query the disk link type of specified disk path.
 *      For SATA disks connected to SAS SES enclosure, will return
 *      LSM_SCSI_LINK_TYPE_ATA.
 *      Require permission to open /dev/sdX(root user or disk group).
 *
 *      Possible value of lsm_disk_link_type:
 *       * LSM_DISK_LINK_TYPE_UNKNOWN
 *          When error or unknown.
 *       * LSM_DISK_LINK_TYPE_FC
 *           Fibre Channel.
 *       * LSM_DISK_LINK_TYPE_SSA
 *           Serial Storage Architecture, Old IBM tech.
 *       * LSM_DISK_LINK_TYPE_SBP
 *           Serial Bus Protocol, used by IEEE 1394.
 *       * LSM_DISK_LINK_TYPE_SRP
 *           SCSI RDMA Protocol.
 *       * LSM_DISK_LINK_TYPE_ISCSI
 *           Internet Small Computer System Interface.
 *       * LSM_DISK_LINK_TYPE_SAS
 *           Serial Attached SCSI.
 *       * LSM_DISK_LINK_TYPE_ADT
 *           Automation/Drive Interface Transport Protocol, often used by Tape.
 *       * LSM_DISK_LINK_TYPE_ATA
 *           PATA/IDE or SATA.
 *       * LSM_DISK_LINK_TYPE_USB
 *           USB disk.
 *       * LSM_DISK_LINK_TYPE_SOP
 *           SCSI over PCI-E.
 *       * LSM_DISK_LINK_TYPE_PCIE
 *           PCI-E, e.g. NVMe.
 *
 * @disk_path:
 *      String. The path of disk, example "/dev/sdb".:
 * @link_type:
 *      Output pointer of lsm_disk_link_type.
 * @lsm_err:
 *      Output pointer of lsm_error. Error message could be:
 *      retrieved via lsm_error_message_get(). Memory should
 *      be freed by lsm_error_free().
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL
 *          * LSM_ERR_NO_MEMORY
 *              When no memory.
 *          * LSM_ERR_LIB_BUG
 *              When something unexpected happens.
 *          * LSM_ERR_NOT_FOUND_DISK
 *              When provided disk path not found.
 *          * LSM_ERR_PERMISSION_DENIED
 *              Insufficient permission to access provided disk path.
 */
int LSM_DLL_EXPORT lsm_local_disk_link_type_get(const char *disk_path,
                                                lsm_disk_link_type *link_type,
                                                lsm_error **lsm_err);

/**
 * lsm_local_disk_ident_led_on - Turn on the disk identification LED.
 * Version:
 *      1.3
 *
 * Description:
 *      Turn on the identification LED for specified disk.
 *      Require read and write access to specified disk path.
 *
 * @disk_path:
 *      String. The path of disk path, example "/dev/sdb".
 * @lsm_err:
 *      Output pointer of lsm_error. Error message could be:
 *      retrieved via lsm_error_message_get(). Memory should
 *      be freed by lsm_error_free().
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL
 *          * LSM_ERR_NO_MEMORY
 *              When no memory.
 *          * LSM_ERR_LIB_BUG
 *              When something unexpected happens.
 *          * LSM_ERR_NOT_FOUND_DISK
 *              When provided disk path not found.
 *          * LSM_ERR_PERMISSION_DENIED
 *              Insufficient permission to access provided disk path.
 *          * LSM_ERR_NO_SUPPORT
 *              Action is not supported.
 *
 */
int LSM_DLL_EXPORT lsm_local_disk_ident_led_on(const char *disk_path,
                                               lsm_error **lsm_err);
/**
 * lsm_local_disk_ident_led_off - Turn off the disk identification LED.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Turn off the identification LED for specified disk.
 *      Require read and write access to specified disk path.
 *
 * @disk_path:
 *      String. The path of disk path, example "/dev/sdb".
 * @lsm_err:
 *      Output pointer of lsm_error. Error message could be retrieved via
 *      lsm_error_message_get().  Memory should be freed by lsm_error_free().
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL
 *          * LSM_ERR_NO_MEMORY
 *              When no memory.
 *          * LSM_ERR_LIB_BUG
 *              When something unexpected happens.
 *          * LSM_ERR_NOT_FOUND_DISK
 *              When provided disk path not found.
 *          * LSM_ERR_PERMISSION_DENIED
 *              Insufficient permission to access provided disk path.
 *          * LSM_ERR_NO_SUPPORT
 *              Action is not supported.
 *
 */
int LSM_DLL_EXPORT lsm_local_disk_ident_led_off(const char *disk_path,
                                                lsm_error **lsm_err);

/**
 * lsm_local_disk_fault_led_on - Turn on the disk fault LED.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Turn on the fault LED for specified disk.
 *      Require read and write access to specified disk path.
 *
 * @disk_path:
 *      String. The path of disk path, example "/dev/sdb".
 * @lsm_err:
 *      Output pointer of lsm_error. Error message could be retrieved via
 *      lsm_error_message_get(). Memory should be freed by lsm_error_free().
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL
 *          * LSM_ERR_NO_MEMORY
 *              When no memory.
 *          * LSM_ERR_LIB_BUG
 *              When something unexpected happens.
 *          * LSM_ERR_NOT_FOUND_DISK
 *              When provided disk path not found.
 *          * LSM_ERR_PERMISSION_DENIED
 *              Insufficient permission to access provided disk path.
 *          * LSM_ERR_NO_SUPPORT
 *              Action is not supported.
 *
 */
int LSM_DLL_EXPORT lsm_local_disk_fault_led_on(const char *disk_path,
                                               lsm_error **lsm_err);
/**
 * lsm_local_disk_fault_led_off - Turn off the disk fault LED.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Turn off the fault LED for specified disk.
 *      Require read and write access to specified disk path.
 *
 * @disk_path:
 *      String. The path of disk path, example "/dev/sdb".
 * @lsm_err:
 *      Output pointer of lsm_error. Error message could be
 *      retrieved via lsm_error_message_get(). Memory should
 *      be freed by lsm_error_free().
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL
 *          * LSM_ERR_NO_MEMORY
 *              When no memory.
 *          * LSM_ERR_LIB_BUG
 *              When something unexpected happens.
 *          * LSM_ERR_NOT_FOUND_DISK
 *              When provided disk path not found.
 *          * LSM_ERR_PERMISSION_DENIED
 *              Insufficient permission to access provided disk path.
 *          * LSM_ERR_NO_SUPPORT
 *              Action is not supported.
 *
 */
int LSM_DLL_EXPORT lsm_local_disk_fault_led_off(const char *disk_path,
                                                lsm_error **lsm_err);

/**
 * lsm_local_disk_led_status_get - Query disk LED status.
 * Version:
 *      1.4
 *
 * Description:
 *      Query the disk LED status of specified disk path.
 *      Require permission to open specified disk path(root user or disk group).
 *
 *      The output led_status is a bit sensitive field:
 *          * LSM_DISK_LED_STATUS_UNKNOWN
 *          * LSM_DISK_LED_STATUS_IDENT_ON
 *          * LSM_DISK_LED_STATUS_IDENT_OFF
 *          * LSM_DISK_LED_STATUS_IDENT_UNKNOWN
 *          * LSM_DISK_LED_STATUS_FAULT_ON
 *          * LSM_DISK_LED_STATUS_FAULT_OFF
 *          * LSM_DISK_LED_STATUS_FAULT_UNKNOWN
 *
 * @disk_path:
 *      String. The path of disk, example "/dev/sdb".
 * @led_status:
 *      Output pointer of uint32_t. which is a bit sensitive field.
 * @lsm_err:
 *      Output pointer of lsm_error. Error message could be retrieved via
 *      lsm_error_message_get(). Memory should be freed by lsm_error_free().
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL
 *          * LSM_ERR_LIB_BUG
 *              When something unexpected happens.
 *          * LSM_ERR_NOT_FOUND_DISK
 *              When provided disk path not found.
 *          * LSM_ERR_PERMISSION_DENIED
 *              Insufficient permission to access provided disk path.
 *          * LSM_ERR_NO_SUPPORT
 *              Action is not supported.
 *
 */
int LSM_DLL_EXPORT lsm_local_disk_led_status_get(const char *disk_path,
                                                 uint32_t *led_status,
                                                 lsm_error **lsm_err);
/**
 * Version:
 *      1.4
 *
 * Description:
 *      Query the current negotiated disk link speed.
 *      Requires permission to open disk path(root user or disk group).
 *      The output speed is in Mbps. For example, 3.0 Gbps will get 3000.
 *      Set to 0(LSM_DISK_LINK_SPEED_UNKNOWN) if error.
 *
 * @disk_path:
 *      String. The path of block device, example: "/dev/sdb", "/dev/nvme0n1".
 * @link_speed:
 *      Output pointer of link speed in Mbps.
 * @lsm_err:
 *      Output pointer of lsm_error. Error message could be retrieved via
 *      lsm_error_message_get(). Memory should be freed by lsm_error_free().
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL
 *          * LSM_ERR_LIB_BUG
 *              When something unexpected happens.
 *          * LSM_ERR_NOT_FOUND_DISK
 *              When provided disk path not found.
 *          * LSM_ERR_PERMISSION_DENIED
 *              Insufficient permission to access provided disk path.
 *          * LSM_ERR_NO_SUPPORT
 *              Action is not supported.
 *
 */
int LSM_DLL_EXPORT lsm_local_disk_link_speed_get(const char *disk_path,
                                                 uint32_t *link_speed,
                                                 lsm_error **lsm_err);

/**
 * lsm_led_handle_get - Retrieves a handle for the LED slots API
 * Version:
 *      1.10
 *
 * Description:
 *    Return a handle to be used for LED slots API addition.
 *    This handle is used by most of the API slots methods.
 *    Make sure to call lsm_led_handle_free() to release resources.
 *
 * @handle:
 *      lsm_led_handle.  Opaque handle pointer to be initialized by this
 * function.
 * @flags:
 *      lsm_flag. Used for future expandability , currently must be set to 0.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL
 *          * LSM_ERR_LIB_BUG
 *              When something unexpected happens.
 *          * LSM_ERR_PERMISSION_DENIED
 *              Insufficient permission to initialize handle
 *          * LSM_ERR_NO_SUPPORT
 *              Action is not supported.
 *
 */
int LSM_DLL_EXPORT lsm_led_handle_get(lsm_led_handle **handle, lsm_flag flags);

/**
 * lsm_led_handle_free - Frees the resources associated with the slots API
 * handle. Version: 1.10
 *
 * Description:
 *       Frees the resources for a handle that is used for LED slots.
 *
 * @handle:
 *      lsm_led_handle.  Opaque handle pointer that was obtained by
 *      lsm_led_handle_get()
 *
 * Return:
 *      void
 */
void LSM_DLL_EXPORT lsm_led_handle_free(lsm_led_handle *handle);

/**
 * lsm_led_slot_iterator_get - obtain an slot iterator
 * Version:
 *      1.10
 *
 * Description:
 *       Returns a slot iterator which is used to iterator over the
 *       available slots.
 *
 * @handle:
 *      lsm_led_handle.  Opaque handle pointer to be initialized by this
 *                       function.
 * @slot_itr:
 *      lsm_led_slot_itr. Returned slots iterator.
 *
 * @lsm_err:
 *      lsm_error. Output pointer of lsm_error. Error message could be retrieved
 * via lsm_error_message_get(). Memory should be freed by lsm_error_free().
 *
 * @flags:
 *      lsm_flag. Bitfield for providing future behavior modifications, requires
 *      0 for version 1.10
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL
 *          * LSM_ERR_LIB_BUG
 *              When something unexpected happens.
 *          * LSM_ERR_PERMISSION_DENIED
 *              Insufficient permission to initialize handle
 *          * LSM_ERR_NO_SUPPORT
 *              Action is not supported.
 *
 */
int LSM_DLL_EXPORT lsm_led_slot_iterator_get(lsm_led_handle *handle,
                                             lsm_led_slot_itr **slot_itr,
                                             lsm_error **lsm_err,
                                             lsm_flag flags);

/**
 * lsm_led_slot_iterator_free - Frees the resources obtained by
 * lsm_led_slot_iterator_get() Version: 1.10
 *
 * Description:
 *       Frees the resources for the iterator.
 *
 * @handle:
 *      lsm_led_handle.  Opaque handle pointer that was obtained by
 *      lsm_led_handle_get()
 * @slot_itr:
 *      lsm_led_slot_itr. Slots iterator that will have its resources freed.
 *
 * Return:
 *      void
 */
void LSM_DLL_EXPORT lsm_led_slot_iterator_free(lsm_led_handle *handle,
                                               lsm_led_slot_itr *slot_itr);

/**
 * lsm_led_slot_iterator_reset - Reset the iterator to allow restarting
 * iteration Version: 1.10
 *
 * Description:
 *       Resets the iterator so that iteration can be done again.
 *
 * @handle:
 *      lsm_led_handle.  Opaque handle pointer that was obtained by
 *      lsm_led_handle_get
 * @slot_itr:
 *      lsm_led_slot_itr. Slots iterator that will be reset.
 *
 * Return:
 *      void
 */
void LSM_DLL_EXPORT lsm_led_slot_iterator_reset(lsm_led_handle *handle,
                                                lsm_led_slot_itr *slot_itr);

/**
 * lsm_led_slot_next - Advances iterator to next slot
 * Version:
 *      1.10
 *
 * Description:
 *       Moves iterator to next LED slot
 *
 * @handle:
 *      lsm_led_handle.  Opaque handle pointer that was obtained by
 *      lsm_led_handle_get().
 * @slot_itr:
 *      lsm_led_slot_itr. Slots iterator that will be reset.
 *
 * Return:
 *      lsm_led_slot or NULL when iteration has completed.  Do not store a copy
 * of the returned slot pointer.  Its only valid until lsm_led_slot_next() is
 * called again or iterator is freed.  Do not call free on the returned pointer.
 * Any resources will be reclaimed when lsm_led_slot_iterator_free() is called.
 */
lsm_led_slot LSM_DLL_EXPORT *lsm_led_slot_next(lsm_led_handle *handle,
                                               lsm_led_slot_itr *slot_itr);

/**
 * lsm_led_slot_status_get - Return the current LED status
 * Version:
 *      1.10
 *
 * Description:
 *      Query the status of LED at specified slot.  Requires permission to open
 *      hardware (root)
 *
 * @slot:
 *      lsm_led_slot.  Opaque slot pointer.
 *
 * Return:
 *      The return value is a bit sensitive field:
 *          * LSM_DISK_LED_STATUS_UNKNOWN
 *          * LSM_DISK_LED_STATUS_IDENT_ON
 *          * LSM_DISK_LED_STATUS_IDENT_OFF
 *          * LSM_DISK_LED_STATUS_IDENT_UNKNOWN
 *          * LSM_DISK_LED_STATUS_FAULT_ON
 *          * LSM_DISK_LED_STATUS_FAULT_OFF
 *          * LSM_DISK_LED_STATUS_FAULT_UNKNOWN
 */
uint32_t LSM_DLL_EXPORT lsm_led_slot_status_get(lsm_led_slot *slot);

/**
 * lsm_led_slot_status_set - Sets the LED for the specified slot
 * Version:
 *      1.10
 *
 * Description:
 *       Set the status of LED at specified slot.  Requires permission to r/w
 hardware (root)
 *
 * @handle:
 *      lsm_led_handle. Opaque led slots API pointer.
 * @slot:
 *      lsm_led_slot.  Opaque slot pointer.
 * @led_status:
 *   uint32_t. Bitfield of desired LED state.  Please note that not all LED
 hardware supports both
 *   identification and fault LEDs.  Using this API, please specify what you
 would like regardless
 *   of support and the hardware will adhere to your request as best it can.
 *   LSM_DISK_LED_STATUS_IDENT_ON => Implies fault off,
 *   LSM_DISK_LED_STATUS_FAULT_ON => Implies ident and fault on
 *   LSM_DISK_LED_STATUS_IDENT_OFF => Implies both ident and fault are off,
 *   LSM_DISK_LED_STATUS_FAULT_OFF => Implies both ident and fault are off,
 *   LSM_DISK_LED_STATUS_IDENT_OFF|LSM_DISK_LED_STATUS_FAULT_OFF,
 *   LSM_DISK_LED_STATUS_IDENT_ON|LSM_DISK_LED_STATUS_FAULT_OFF,
 *   LSM_DISK_LED_STATUS_FAULT_ON|LSM_DISK_LED_STATUS_IDENT_OFF,
 *   LSM_DISK_LED_STATUS_IDENT_ON|LSM_DISK_LED_STATUS_FAULT_ON
 * @lsm_err:
 *      Output pointer of lsm_error. Error message could be retrieved via
 *      lsm_error_message_get(). Memory should be freed by lsm_error_free().
 *
 * @flags:
 *      Bitfield for providing future behavior modifications, requires 0 for
 * version 1.10
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL, incorrect, or has an incorrect value
 *          * LSM_ERR_LIB_BUG
 *              When something unexpected happens.
 *          * LSM_ERR_PERMISSION_DENIED
 *              Insufficient permission to write to hardware
 *          * LSM_ERR_NO_SUPPORT
 *              Action is not supported.
 */
int LSM_DLL_EXPORT lsm_led_slot_status_set(lsm_led_handle *handle,
                                           lsm_led_slot *slot,
                                           uint32_t led_status,
                                           lsm_error **lsm_err, lsm_flag flags);

/**
 * lsm_led_slot_id - return the specified slot identifier
 * Version:
 *      1.10
 *
 * Description:
 *       Returns the slot identifier for the specified slot.
 *
 * @slot:
 *      lsm_led_slot.  Opaque slot pointer obtained from  lsm_led_slot_next()
 *
 * Return:
 *      string representing slot identifier. Pointer valid until
 * lsm_led_slot_next gets called or slots iterator gets freed.  Make a copy if
 * you need a longer life span, eg. strdup(). Do not call free on returned
 * value.
 */
const char LSM_DLL_EXPORT *lsm_led_slot_id(lsm_led_slot *slot);

/**
 * lsm_led_slot_device - return the specified slot device node
 * Version:
 *      1.10
 *
 * Description:
 *       Returns the device node for the specified slot.
 *
 * @slot:
 *      lsm_led_slot.  Opaque slot pointer obtained from lsm_led_slot_next()
 *
 * Notes: Returned value may be NULL, not all slots have a device node!
 *
 * Return:
 *      string representing device node, may be NULL as not all slots have a
 * device node. Pointer valid until lsm_led_slot_next gets called or slots
 * iterator gets freed.  Make a copy eg. strdup() if you need a longer life
 * span. Do not call free on returned value.
 */
const char LSM_DLL_EXPORT *lsm_led_slot_device(lsm_led_slot *slot);

#ifdef __cplusplus
}
#endif
#endif /* LIBSTORAGEMGMT_LOCAL_DISK_H */
