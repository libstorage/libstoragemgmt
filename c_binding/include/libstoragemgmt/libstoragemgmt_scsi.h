/*
 * Copyright (C) 2015 Red Hat, Inc.
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

#ifndef LIBSTORAGEMGMT_SCSI_H
#define LIBSTORAGEMGMT_SCSI_H

#include "libstoragemgmt_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Find out the scsi disk paths of given SCSI VPD page 0x83 NAA ID.
 * New in version 1.3.
 * @param[in] vpd83 String. The VPD83 ID retrieved from LSM volume or disk.
 *                  for supported strip sizes.
 * @param[out] sd_path_list
 *                  Output pointer of lsm_string_list. The format of scsi
 *                  disk path will be "/dev/sd[a-z]+".
 *                  NULL if no found or got error.
 *                  Memory should be freed by lsm_string_list_free().
 * @param[out] lsm_err
 *                  Output pointer of lsm_error. Error message could be
 *                  retrieved via lsm_error_message_get(). Memory should be
 *                  freed by lsm_error_free().
 * @return LSM_ERR_OK                   on success.
 *         LSM_ERR_INVALID_ARGUMENT     when any argument is NULL.
 *         LSM_ERR_NO_MEMORY            when no memory.
 *         LSM_ERR_LIB_BUG              when something unexpected happens.
 */
int LSM_DLL_EXPORT lsm_scsi_disk_paths_of_vpd83(const char *vpd83,
                                                lsm_string_list **sd_path_list,
                                                lsm_error **lsm_err);

#ifdef __cplusplus
}
#endif
#endif                          /* LIBSTORAGEMGMT_SCSI_H */
