/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2021-2023 Red Hat, Inc.
 *
 * Author: Tony Asleson <tasleson@redhat.com>
 */

#ifndef _LIBNVME_H_
#define _LIBNVME_H_

#include "libstoragemgmt/libstoragemgmt_common.h"

#ifdef __cplusplus
extern "C" {
#endif

LSM_DLL_LOCAL int _nvme_health_status(char *err_msg, int fd,
                                      int32_t *health_status);

#ifdef __cplusplus
}
#endif

#endif
