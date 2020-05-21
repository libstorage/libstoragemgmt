/*
 * Copyright (C) 2016 Red Hat, Inc.
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
 */
#ifndef _LIB_UTILS_H_
#define _LIB_UTILS_H_

#include "libstoragemgmt/libstoragemgmt_error.h"

#include <stdbool.h>
#include <stdio.h>

#define _LSM_ERR_MSG_LEN 4096

#define _good(rc, rc_val, out)                                                 \
    do {                                                                       \
        rc_val = rc;                                                           \
        if (rc_val != LSM_ERR_OK)                                              \
            goto out;                                                          \
    } while (0)

#define _lsm_string_list_foreach(l, i, d)                                      \
    for (i = 0; (l != NULL) && (i < lsm_string_list_size(l)) &&                \
                (d = lsm_string_list_elem_get(l, i));                          \
         ++i)

#define _lsm_err_msg_clear(err_msg) memset(err_msg, 0, _LSM_ERR_MSG_LEN)

#define _alloc_null_check(err_msg, ptr, rc, goto_out)                          \
    do {                                                                       \
        if (ptr == NULL) {                                                     \
            rc = LSM_ERR_NO_MEMORY;                                            \
            _lsm_err_msg_set(err_msg, "No memory");                            \
            goto goto_out;                                                     \
        }                                                                      \
    } while (0)

#define _lsm_err_msg_set(err_msg, format, ...)                                 \
    snprintf(err_msg, _LSM_ERR_MSG_LEN, format, ##__VA_ARGS__)

/*
 * Preconditions:
 *  err_msg != NULL
 *
 * Check whether pointers are NULL.
 * If found any NULL pointer, set error message and return
 * LSM_ERR_INVALID_ARGUMENT.
 * Return LSM_ERR_OK if no NULL pointer.
 */
LSM_DLL_LOCAL int _check_null_ptr(char *err_msg, int arg_count, ...);

/*
 * Preconditions:
 *  raw != NULL
 *  out != NULL
 *
 * Input big-endian uint8_t array, output has hex string.
 * No check on output memory size or boundary, make sure before invoke.
 */
LSM_DLL_LOCAL void _be_raw_to_hex(uint8_t *raw, size_t len, char *out);

/*
 * Preconditions:
 *  path != NULL
 *
 * Try to open provided path file or folder, return true if file could be open,
 * or false.
 */
LSM_DLL_LOCAL bool _file_exists(const char *path);

/*
 * Preconditions:
 *  path != NULL
 *  buff != NULL
 *  size != NULL
 *
 * Return the errno of open() if failed.
 */
LSM_DLL_LOCAL int _read_file(const char *path, uint8_t *buff, ssize_t *size,
                             ssize_t max_size);

/*
 * Preconditions:
 * beginning != NULL
 *
 * Return NULL if failed.
 */
LSM_DLL_LOCAL char *_trim_spaces(char *beginning);

/*
 * Preconditions:
 * sysfs_path != NULL
 * link_speed != NULL
 *
 * Only support FC and iSCSI SCSI host yet.
 * Return lsm error number if failed.
 */
LSM_DLL_LOCAL int _sysfs_host_speed_get(char *err_msg, const char *sysfs_path,
                                        uint32_t *link_speed);

/**
 * Convert an errno to a string representation.
 */
LSM_DLL_LOCAL char *error_to_str(int errnum, char *buf, size_t buflen);

#endif /* End of _LIB_UTILS_H_ */
