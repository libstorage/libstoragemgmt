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

#include "libata.h"
#include "utils.h"

#include "libstoragemgmt/libstoragemgmt_error.h"
#include "libstoragemgmt/libstoragemgmt_types.h"

#include <stdint.h>
#include <assert.h>

/*
 * Serial ATA Additional Capabilities
 */
#define _ATA_SATA_ADD_CAP_WORD                      77
#define _ATA_SPEED_UNKNOWN              0
#define _ATA_SPEED_GEN1_0               1
/* SATA revision 1.0 -- 1.5 Gbps */
#define _ATA_SPEED_GEN2_0               2
/* SATA revision 2.0 -- 3 Gbps */
#define _ATA_SPEED_GEN3_0               3
/* SATA revision 3.0 -- 6 Gbps */

#pragma pack(push, 1)
struct _ata_sata_add_cap {
    uint8_t zero            : 1;
    uint8_t cur_speed       : 3;
    uint8_t we_dont_care_0  : 8;
    uint8_t we_dont_care_1  : 4;
};
#pragma pack(pop)


int _ata_cur_speed_get(char *err_msg, uint8_t *id_dev_data,
                       uint32_t *link_speed)
{
    int rc = LSM_ERR_OK;
    struct _ata_sata_add_cap *add_cap = NULL;

    assert(id_dev_data != NULL);
    assert(link_speed != NULL);

    add_cap = (struct _ata_sata_add_cap *)
        (id_dev_data + _ATA_SATA_ADD_CAP_WORD * 2);

    *link_speed = LSM_DISK_LINK_SPEED_UNKNOWN;

    switch(add_cap->cur_speed) {
    case _ATA_SPEED_UNKNOWN:
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg, "No support: specified disk does not "
                         "expose SATA speed information in 'Serial ATA "
                         "Capabilities' word");
        break;
    case _ATA_SPEED_GEN1_0:
        *link_speed = 1500;
        break;
    case _ATA_SPEED_GEN2_0:
        *link_speed = 3000;
        break;
    case _ATA_SPEED_GEN3_0:
        *link_speed = 6000;
        break;
    default:
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg,
                         "BUG: Got unexpected ATA speed code 0x%02x",
                         add_cap->cur_speed);
    }

    return rc;
}
