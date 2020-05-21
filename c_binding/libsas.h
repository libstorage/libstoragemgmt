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
