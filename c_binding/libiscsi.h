/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2016-2023 Red Hat, Inc.
 *
 * Author: Gris Ge <fge@redhat.com>
 */

#ifndef _LIBISCSI_H_
#define _LIBISCSI_H_

#include "libstoragemgmt/libstoragemgmt_common.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Retrieve iSCSI host speed via /sys/class/scsi_host/host<host_no>/port_speed
 * Preconditions:
 *  err_msg != NULL
 *  link_speed != NULL
 * Return:
 *  LSM_ERR_OK or other LSM error code.
 */
LSM_DLL_LOCAL int _iscsi_host_speed_get(char *err_msg, unsigned int host_no,
                                        uint32_t *link_speed);

#ifdef __cplusplus
}
#endif

#endif /* End of _LIBISCSI_H_ */
