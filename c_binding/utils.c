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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libstoragemgmt/libstoragemgmt_error.h"
#include "utils.h"

#define _SYSFS_HOST_SPEED_PATH_STR_MAX_LEN 128
/* ^ The max host number is 4294967295 which has 14 digits.
 *   The sysfs path is "/sys/class/iscsi_host/host<host_no>/port_speed"
 *   Hence we got max 45 char count, The 128 should works for a long time.
 */

#define _SYSFS_HOST_SPEED_BUFF_MAX 128
/* ^ The max FC and iSCSI speed in linux kernel is "100 Gbit" when I coding this
 *   line, hence 128 should works for a long time
 */

int _check_null_ptr(char *err_msg, int arg_count, ...) {
    int rc = LSM_ERR_OK;
    va_list arg;
    int i = 0;
    void *ptr = NULL;

    assert(err_msg != NULL);

    va_start(arg, arg_count);

    for (; i < arg_count; ++i) {
        ptr = va_arg(arg, void *);
        if (ptr == NULL) {
            _lsm_err_msg_set(err_msg, "Got NULL pointer in arguments");
            rc = LSM_ERR_INVALID_ARGUMENT;
            goto out;
        }
    }

out:
    va_end(arg);
    return rc;
}

void _be_raw_to_hex(uint8_t *raw, size_t len, char *out) {
    size_t i = 0;

    assert(raw != NULL);
    assert(out != NULL);

    for (; i < len; ++i) {
        snprintf(out + (i * 2), 3, "%02x", raw[i]);
    }
    out[len * 2] = 0;
}

bool _file_exists(const char *path) {
    int fd = -1;

    assert(path != NULL);

    fd = open(path, O_RDONLY);
    if ((fd == -1) && (errno == ENOENT))
        return false;

    if (fd >= 0) {
        close(fd);
    }
    return true;
}

int _read_file(const char *path, uint8_t *buff, ssize_t *size,
               ssize_t max_size) {
    int fd = -1;
    int rc = 0;
    int errno_copy = 0;

    assert(path != NULL);
    assert(buff != NULL);
    assert(size != NULL);

    *size = 0;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return errno;
    *size = read(fd, buff, max_size);
    errno_copy = errno;
    close(fd);

    if (*size < 0) {
        rc = errno_copy;
        buff[0] = '\0';
    } else {
        if (*size >= (max_size - 1)) {
            rc = EFBIG;
            buff[max_size - 1] = '\0';
        } else {
            buff[*size] = '\0';
        }
    }
    return rc;
}

char *_trim_spaces(char *beginning) {
    size_t len = 0;
    unsigned int leading_space_count = 0;
    char *end;

    assert(beginning != NULL);

    len = strlen(beginning);

    if (!len)
        return NULL;

    while (*beginning == ' ') {
        beginning++;
        leading_space_count++;
    }

    /* The string is composed entirely of spaces */
    if (leading_space_count >= len)
        return NULL;

    end = beginning + len - 1;

    while (*end == ' ') {
        *end = '\0';
        end--;
    }

    return beginning;
}

int _sysfs_host_speed_get(char *err_msg, const char *sysfs_path,
                          uint32_t *link_speed) {
    int rc = LSM_ERR_OK;
    char strerr_buff[1024];
    uint8_t buff[_SYSFS_HOST_SPEED_BUFF_MAX];
    int file_rc = 0;
    ssize_t file_size = 0;
    char *num_str = NULL;
    char *postfix = NULL;
    long int speed_raw = 0;

    assert(sysfs_path != NULL);
    assert(link_speed != NULL);

    *link_speed = LSM_DISK_LINK_SPEED_UNKNOWN;

    file_rc = _read_file(sysfs_path, buff, &file_size,
                         _SYSFS_HOST_SPEED_PATH_STR_MAX_LEN);
    if (file_rc == ENOENT) {
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg, "No support: no %s file", sysfs_path);
        goto out;
    } else if (file_rc != 0) {
        rc = LSM_ERR_LIB_BUG;
        char *err_str = error_to_str(file_rc, strerr_buff, sizeof(strerr_buff));
        _lsm_err_msg_set(err_msg,
                         "BUG: Unknown error %d(%s) from "
                         "_read_file().",
                         file_rc, err_str);
        goto out;
    }

    /* Remove the trailing \n */
    buff[file_size - 1] = '\0';

    if (strcmp((char *)buff, "Unknown") == 0)
        goto out;
    /* 'Unknown' is used by iSCSI host */

    if (strcmp((char *)buff, "Not Negotiated") == 0)
        goto out;
    /* 'Not Negotiated' is used by FC host */

    num_str = strtok_r((char *)buff, " ", &postfix);
    if ((num_str == NULL) || (postfix == NULL)) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg,
                         "BUG: _sysfs_host_speed_get(): Invalid "
                         "format of SCSI host speed '%s'",
                         (char *)buff);
        goto out;
    }

    speed_raw = strtol(num_str, NULL /* end ptr */, 10 /* Base 10 */);
    if ((speed_raw < 0) || (speed_raw >= INT_MAX)) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg,
                         "BUG: _sysfs_host_speed_get(): Invalid "
                         "format of SCSI host speed '%s'",
                         (char *)buff);
        goto out;
    }

    if ((strcmp(postfix, "Gbps") == 0) || (strcmp(postfix, "Gbit") == 0)) {
        *link_speed = (speed_raw * 1000) & UINT32_MAX;
    } else if (strcmp(postfix, "Mbps") == 0) {
        *link_speed = speed_raw & UINT32_MAX;
    } else {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg,
                         "BUG: _sysfs_host_speed_get(): Invalid "
                         "format of iscsi host speed '%s'",
                         (char *)buff);
        goto out;
    }

out:
    return rc;
}

char *error_to_str(int errnum, char *buf, size_t buflen) {
    const char *non_mem = "Unable to translate errno to text, newlocale fail";
    if (buf && buflen >= 1024) {
        locale_t const locale = newlocale(LC_MESSAGES_MASK, "", (locale_t)0);
        if (!locale) {
            strncpy(buf, non_mem, buflen - 1);
            buf[buflen - 1] = '\0';
        } else {
            char *s = strerror_l(errnum, locale);
            strncpy(buf, s, buflen - 1);
            buf[buflen - 1] = '\0';
            freelocale(locale);
        }
        return buf;
    }
    return NULL;
}
