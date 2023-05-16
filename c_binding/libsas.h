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

#endif /* End of _LIBSAS_H_ */
