/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2016-2023 Red Hat, Inc.
 *
 * Author: Gris Ge <fge@redhat.com>
 */

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libfc.h"
#include "libstoragemgmt/libstoragemgmt_error.h"
#include "utils.h"

#define _SYSFS_FC_HOST_SPEED_PATH_STR_MAX_LEN 128
/* ^ The max host number is 4294967295 which has 14 digits.
 *   The sysfs path is "/sys/class/fc_host/host<host_no>/speed"
 *   Hence we got max 45 char count, The 128 should works for a long time.
 */

int _fc_host_speed_get(char *err_msg, unsigned int host_no,
                       uint32_t *link_speed) {
    int rc = LSM_ERR_OK;
    char sysfs_path[_SYSFS_FC_HOST_SPEED_PATH_STR_MAX_LEN];

    assert(link_speed != NULL);

    *link_speed = LSM_DISK_LINK_SPEED_UNKNOWN;

    if (host_no == UINT_MAX) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "BUG: _fc_host_speed_get(): "
                                  "Got unknown(UINT_MAX) fc host number");
        goto out;
    }

    snprintf(sysfs_path, _SYSFS_FC_HOST_SPEED_PATH_STR_MAX_LEN,
             "/sys/class/fc_host/host%u/speed", host_no);

    _good(_sysfs_host_speed_get(err_msg, sysfs_path, link_speed), rc, out);

out:
    return rc;
}
