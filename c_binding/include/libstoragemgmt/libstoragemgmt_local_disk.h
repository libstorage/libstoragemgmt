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
 * Search all the disk paths of given SCSI VPD 0x83 page NAA type ID.
 * For any ATA and other non-SCSI protocol disks supporting VPD 0x83 pages NAA
 * ID, their disk path will also be included.
 * New in version 1.3.
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
 * @return LSM_ERR_OK                   on success or not found.
 *         LSM_ERR_INVALID_ARGUMENT     when any argument is NULL.
 *         LSM_ERR_NO_MEMORY            when no memory.
 *         LSM_ERR_LIB_BUG              when something unexpected happens.
 */
int LSM_DLL_EXPORT lsm_local_disk_vpd83_search(const char *vpd83,
                                               lsm_string_list **disk_path_list,
                                               lsm_error **lsm_err);

/**
 * Query the SCSI VPD 0x83 page NAA type ID of given disk path.
 * New in version 1.3.
 * @param[in]  sd_path  String. The path of disk path, example "/dev/sdb".
 * @param[out] vpd83    Output pointer of SCSI VPD83 NAA ID. The format is:
 *                          (?:^6[0-9a-f]{31})|(?:^[235][0-9a-f]{15})$
 *                      NULL when error. Memory should be freed by free().
 * @param[out] lsm_err
 *                  Output pointer of lsm_error. Error message could be
 *                  retrieved via lsm_error_message_get(). Memory should be
 *                  freed by lsm_error_free().
 * @return LSM_ERR_OK                   on success.
 *         LSM_ERR_INVALID_ARGUMENT     when any argument is NULL or
 *                                      illegal sd_path.
 *         LSM_ERR_NO_MEMORY            when no memory.
 *         LSM_ERR_LIB_BUG              when something unexpected happens.
 *         LSM_ERR_NOT_FOUND_DISK       When provided disk path not found.
 */
int LSM_DLL_EXPORT lsm_local_disk_vpd83_get(const char *sd_path,
                                            const char **vpd83,
                                            lsm_error **lsm_err);

#ifdef __cplusplus
}
#endif
#endif                          /* LIBSTORAGEMGMT_LOCAL_DISK_H */
