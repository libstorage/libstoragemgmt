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
 *  LSM_ERR_OK
 *      *link_speed is the negotiated speed in Mbps of the phy whose SAS
 *      address is sas_addr.
 *  LSM_ERR_NO_SUPPORT
 *      The device did not return the Phy Control And Discover subpage (e.g.
 *      it answered with the short-format page 0x19), or no phy in it has
 *      sas_addr. Not a library bug.
 *  LSM_ERR_LIB_BUG
 *      The matching phy reports a link rate code that is not a known speed.
 *  *link_speed is LSM_DISK_LINK_SPEED_UNKNOWN on any non-OK return.
 */
LSM_DLL_LOCAL int _sas_cur_speed_get(char *err_msg, uint8_t *mode_sense_data,
                                     const char *sas_addr,
                                     uint32_t *link_speed);

#ifdef __cplusplus
}
#endif

#endif /* End of _LIBSAS_H_ */
