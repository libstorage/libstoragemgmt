/*
 * Copyright (C) 2016 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
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

#ifndef _LIBSES_H_
#define _LIBSES_H_

#include <stdint.h>
#include <stdbool.h>

#include "libstoragemgmt/libstoragemgmt_common.h"
#include "libstoragemgmt/libstoragemgmt_error.h"

#define _SES_CTRL_SET                       1
#define _SES_CTRL_CLEAR                     2

#define _SES_DEV_CTRL_RQST_IDENT            1
#define _SES_DEV_CTRL_RQST_FAULT            2

/*
 * err_msg:     Should be 'char err_msg[_LSM_ERR_MSG_LEN]'.
 * tp_sas_addr: Target port SAS address.
 * ctrl_value:  Should be _SES_DEV_CTRL_RQST_IDENT or _SES_DEV_CTRL_RQST_FAULT.
 * ctrl_type:   _SES_CTRL_SET or _SES_CTRL_CLEAR.
 *
 */
LSM_DLL_LOCAL int _ses_dev_slot_ctrl(char *err_msg, const char *tp_sas_addr,
                                     int ctrl_value, int ctrl_type);

#endif  /* End of _LIBSES_H_ */
