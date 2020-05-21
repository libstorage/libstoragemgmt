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
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libiscsi.h"
#include "libstoragemgmt/libstoragemgmt_error.h"
#include "utils.h"

#define _SYSFS_ISCSI_HOST_SPEED_PATH_STR_MAX_LEN 128
/* ^ The max host number is 4294967295 which has 14 digits.
 *   The sysfs path is "/sys/class/scsi_host/host<host_no>/port_speed"
 *   Hence we got max 45 char count, The 128 should works for a long time.
 */

int _iscsi_host_speed_get(char *err_msg, unsigned int host_no,
                          uint32_t *link_speed) {
    int rc = LSM_ERR_OK;
    char sysfs_path[_SYSFS_ISCSI_HOST_SPEED_PATH_STR_MAX_LEN];

    assert(link_speed != NULL);

    *link_speed = LSM_DISK_LINK_SPEED_UNKNOWN;

    if (host_no == UINT_MAX) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "BUG: _iscsi_host_speed_get(): "
                                  "Got unknown(UINT_MAX) iSCSI host number");
        goto out;
    }

    snprintf(sysfs_path, _SYSFS_ISCSI_HOST_SPEED_PATH_STR_MAX_LEN,
             "/sys/class/iscsi_host/host%u/port_speed", host_no);

    _good(_sysfs_host_speed_get(err_msg, sysfs_path, link_speed), rc, out);

out:
    return rc;
}
