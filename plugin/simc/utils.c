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
#include <sqlite3.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils.h"

static int _str_to_ll(char *err_msg, const char *str, long long int *val);

int _get_db_from_plugin_ptr(char *err_msg, lsm_plugin_ptr c, sqlite3 **db) {
    struct _simc_private_data *pri_data = NULL;
    int rc = LSM_ERR_OK;

    pri_data = lsm_private_data_get(c);
    if ((pri_data == NULL) || (pri_data->db == NULL)) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg, "BUG: Got NULL db pointer");
        *db = NULL;
    } else {
        *db = pri_data->db;
    }

    return rc;
}

void _md5(const char *data, char *out_hash) {
    int i = 0;
    unsigned char digest[MD5_DIGEST_LENGTH];

    assert(data != NULL);
    assert(out_hash != NULL);

    MD5((const unsigned char *)data, strlen(data), digest);

    for (; i < MD5_DIGEST_LENGTH; ++i)
        sprintf(out_hash + i * 2, "%02x", (unsigned int)digest[i]);
}

/*
 * Copy from c_binding/utils.c, will remove if that was exposed out.
 */
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

/*
 * Copy from c_binding/utils.c, will remove if that was exposed out.
 */
int _check_null_ptr(char *err_msg, int arg_count, ...) {
    int rc = LSM_ERR_OK;
    va_list arg;
    int i = 0;
    void *ptr = NULL;

    va_start(arg, arg_count);

    for (; i < arg_count; ++i) {
        ptr = va_arg(arg, void *);
        if (ptr == NULL) {
            _lsm_err_msg_set(err_msg, "Got NULL pointer as argument %d", i);
            rc = LSM_ERR_INVALID_ARGUMENT;
            goto out;
        }
    }

out:
    va_end(arg);
    return rc;
}

static int _str_to_ll(char *err_msg, const char *str, long long int *val) {
    int tmp_errno = 0;
    assert(str != NULL);
    assert(val != NULL);

    *val = strtoll(str, NULL, 10 /* base */);
    tmp_errno = errno;
    if ((tmp_errno != 0) && (*val == LONG_MAX)) {
        _lsm_err_msg_set(err_msg,
                         "BUG: Failed to convert string to number: "
                         "'%s', error %d",
                         str, tmp_errno);
        return LSM_ERR_PLUGIN_BUG;
    }
    return LSM_ERR_OK;
}

int _str_to_uint32(char *err_msg, const char *str, uint32_t *val) {
    int rc = LSM_ERR_OK;
    long long int tmp_val = 0;

    rc = _str_to_ll(err_msg, str, &tmp_val);

    if (rc != LSM_ERR_OK)
        *val = UINT32_MAX;
    else
        *val = tmp_val & UINT32_MAX;

    return rc;
}

int _str_to_uint64(char *err_msg, const char *str, uint64_t *val) {
    int rc = LSM_ERR_OK;
    long long int tmp_val = 0;

    rc = _str_to_ll(err_msg, str, &tmp_val);

    if (rc != LSM_ERR_OK)
        *val = UINT64_MAX;
    else
        *val = tmp_val & UINT64_MAX;

    return rc;
}

int _str_to_int(char *err_msg, const char *str, int *val) {
    int rc = LSM_ERR_OK;
    long long int tmp_val = 0;

    rc = _str_to_ll(err_msg, str, &tmp_val);

    if (rc != LSM_ERR_OK)
        *val = INT_MAX;
    else
        *val = tmp_val & INT_MAX;

    return rc;
}

const char *_random_vpd(char *buff) {
    int fd = -1;
    ssize_t cur_got = 0;
    size_t got = 0;
    size_t i = 0;
    size_t needed = (_VPD_83_LEN - 1) / 2 - 1;
    /* Skip the first two digits. We will use '50'. */
    /* Assuming _VPD_83_LEN is odd number */
    uint8_t raw_data[(_VPD_83_LEN - 1) / 2];

    assert(buff != NULL);

    memset(buff, 0, _VPD_83_LEN);

    /* Coverity said we should not use rand() */
    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return buff;

    while (got < needed) {
        cur_got = read(fd, raw_data + got, needed - got);
        if (cur_got < 0) {
            close(fd);
            return buff;
        }
        got += cur_got;
    }
    close(fd);

    buff[0] = '5';
    buff[1] = '0';

    for (i = 0; i < needed; ++i)
        sprintf(&buff[i * 2 + 2], "%02x", raw_data[i]);

    return buff;
}
