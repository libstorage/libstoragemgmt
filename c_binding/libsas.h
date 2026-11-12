/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * Author: Gris Ge <fge@redhat.com>
 */

#ifndef _LIBSAS_H_
#define _LIBSAS_H_

#include "libstoragemgmt/libstoragemgmt_common.h"
#include <stdint.h>

/* SPL-4 Table "NEGOTIATED LOGICAL LINK RATE field" */
#define _SAS_SPEED_UNKNOWN 0x0
#define _SAS_SPEED_1_5     0x8
#define _SAS_SPEED_3_0     0x9
#define _SAS_SPEED_6_0     0xa
#define _SAS_SPEED_12_0    0xb
#define _SAS_SPEED_22_5    0xc

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Preconditions:
 *  mode_sense_data != NULL
 *  mode_sense_data is uint8_t[_SG_T10_SPC_MODE_SENSE_MAX_LEN] retrieved by
 *  _sg_io_mode_sense on page 0x19 subpage 0x01.
 *  sas_addr != NULL
 *  sas_addr is the SAS address of the disk.
 *  link_speed != NULL
 * Return:
 *  LSM_ERR_XXX
 */
LSM_DLL_LOCAL int _sas_cur_speed_get(char *err_msg, uint8_t *mode_sense_data,
                                     const char *sas_addr,
                                     uint32_t *link_speed);

#ifdef __cplusplus
}
#endif

#endif /* End of _LIBSAS_H_ */
