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

#ifndef _LIBATA_H_
#define _LIBATA_H_

#include <stdint.h>
#include "libstoragemgmt/libstoragemgmt_common.h"

#define _ATA_IDENTIFY_DEVICE_DATA_LEN   512

/*
 * Preconditions:
 *  id_dev_data != NULL
 *  id_dev_data is uint8_t[_LIBATA_IDENTIFY_DEVICE_DATA_LEN]
 *  link_speed != NULL;
 * Return:
 *  LSM_ERR_XXX.
 */
LSM_DLL_LOCAL int _ata_cur_speed_get(char *err_msg, uint8_t *id_dev_data,
                                     uint32_t *link_speed);

#endif  /* End of _LIBATA_H_ */
