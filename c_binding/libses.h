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

#ifndef _LIBSES_H_
#define _LIBSES_H_

#include <stdbool.h>
#include <stdint.h>

#include "libstoragemgmt/libstoragemgmt_common.h"
#include "libstoragemgmt/libstoragemgmt_error.h"

#define _SES_CTRL_SET   1
#define _SES_CTRL_CLEAR 2

#define _SES_DEV_CTRL_RQST_IDENT 1
#define _SES_DEV_CTRL_RQST_FAULT 2

#pragma pack(push, 1)
/*
 * Holding the share properties of `Device Slot status element` and
 * `Array Device Slot status element`.
 */
struct _ses_dev_slot_status {
    uint8_t common_status;
    uint8_t diff_between_dev_slot_and_array_dev_slot;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t report : 1;
    uint8_t ident : 1;
    uint8_t rmv : 1;
    uint8_t ready_to_insert : 1;
    uint8_t enc_bypass_b : 1;
    uint8_t enc_bypass_a : 1;
    uint8_t do_not_remove : 1;
    uint8_t app_bypass_a : 1;

    uint8_t dev_bypass_b : 1;
    uint8_t dev_bypass_a : 1;
    uint8_t bypass_b : 1;
    uint8_t bypass_a : 1;
    uint8_t dev_off : 1;
    uint8_t fault_reqstd : 1;
    uint8_t fault_sensed : 1;
    uint8_t app_bypass_b : 1;
#else
    uint8_t app_bypass_a : 1;
    uint8_t do_not_remove : 1;
    uint8_t enc_bypass_a : 1;
    uint8_t enc_bypass_b : 1;
    uint8_t ready_to_insert : 1;
    uint8_t rmv : 1;
    uint8_t ident : 1;
    uint8_t report : 1;

    uint8_t app_bypass_b : 1;
    uint8_t fault_sensed : 1;
    uint8_t fault_reqstd : 1;
    uint8_t dev_off : 1;
    uint8_t bypass_a : 1;
    uint8_t bypass_b : 1;
    uint8_t dev_bypass_a : 1;
    uint8_t dev_bypass_b : 1;
#endif
};
#pragma pack(pop)

/*
 * err_msg:     Should be 'char err_msg[_LSM_ERR_MSG_LEN]'.
 * tp_sas_addr: Target port SAS address.
 * ctrl_value:  Should be _SES_DEV_CTRL_RQST_IDENT or _SES_DEV_CTRL_RQST_FAULT.
 * ctrl_type:   _SES_CTRL_SET or _SES_CTRL_CLEAR.
 *
 */
LSM_DLL_LOCAL int _ses_dev_slot_ctrl(char *err_msg, const char *tp_sas_addr,
                                     int ctrl_value, int ctrl_type);

/*
 * err_msg:     Should be 'char err_msg[_LSM_ERR_MSG_LEN]'.
 * tp_sas_addr: Target port SAS address.
 * status:      Should be struct _ses_slot_status.
 */
LSM_DLL_LOCAL int _ses_status_get(char *err_msg, const char *tp_sas_addr,
                                  struct _ses_dev_slot_status *status);

#endif /* End of _LIBSES_H_ */
