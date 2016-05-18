/*
 * Copyright (C) 2016 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
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

#include "utils.h"

#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

int _check_null_ptr(char *err_msg, int arg_count, ...)
{
    int rc = LSM_ERR_OK;
    va_list arg;
    int i = 0;
    void *ptr = NULL;

    assert(err_msg != NULL);

    va_start(arg, arg_count);

    for (; i < arg_count; ++i) {
        ptr = va_arg(arg, void*);
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

void _be_raw_to_hex(uint8_t *raw, size_t len, char *out)
{
    size_t i = 0;

    assert(raw != NULL);
    assert(out != NULL);

    for (; i < len; ++i) {
        snprintf(out + (i * 2), 3, "%02x", raw[i]);
    }
    out[len * 2] = 0;

}

bool _file_exists(const char *path)
{
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

int _read_file(const char *path, uint8_t *buff, ssize_t *size, size_t max_size)
{
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
