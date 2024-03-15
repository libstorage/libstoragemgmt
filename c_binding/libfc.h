/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2016-2023 Red Hat, Inc.
 *
 * Author: Gris Ge <fge@redhat.com>
 */

#ifndef _LIBFC_H_
#define _LIBFC_H_

#include "libstoragemgmt/libstoragemgmt_common.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Retrieve FC host speed via /sys/class/fc_host/host<host_no>/speed
 * Preconditions:
 *  err_msg != NULL
 *  link_speed != NULL
 * Return:
 *  LSM_ERR_OK or other LSM error code.
 */
LSM_DLL_LOCAL int _fc_host_speed_get(char *err_msg, unsigned int host_no,
                                     uint32_t *link_speed);

#ifdef __cplusplus
}
#endif
#endif /* End of _LIBFC_H_ */
