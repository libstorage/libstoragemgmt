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

#include "libsas.h"
#include "libsg.h"
#include "utils.h"

#include "libstoragemgmt/libstoragemgmt_error.h"

#include <assert.h>
#include <endian.h>
#include <stdint.h>
#include <string.h>

#define _SAS_SPEED_UNKNOWN 0x0
#define _SAS_SPEED_1_5     0x8
#define _SAS_SPEED_3_0     0x9
#define _SAS_SPEED_6_0     0xa
#define _SAS_SPEED_12_0    0xb
#define _SAS_SPEED_22_5    0xc

#pragma pack(push, 1)
struct _sas_phy_ctrl_dicov_hdr {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t page_code : 6;
    uint8_t spf : 1;
    uint8_t ps : 1;
#else
    uint8_t ps : 1;
    uint8_t spf : 1;
    uint8_t page_code : 6;
#endif
    uint8_t sub_page_code;
    uint16_t len_be;
    uint8_t reserved_1;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t protocol_id : 4;
    uint8_t reserved_2 : 4;
#else
    uint8_t reserved_2 : 4;
    uint8_t protocol_id : 4;
#endif
    uint8_t gen_code;
    uint8_t num_of_phys;
};

struct _sas_phy_mode_dp {
    uint8_t reserved_1;
    uint8_t phy_id;
    uint16_t reserved_2;
    uint8_t we_dont_care_0;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t negotiated_logical_link_rate : 4;
    uint8_t we_dont_care_1 : 4;
#else
    uint8_t we_dont_care_1 : 4;
    uint8_t negotiated_logical_link_rate : 4;
#endif
    uint8_t we_dont_care_2[2];
    uint8_t sas_addr[8];
    uint8_t we_dont_care_3[32];
};
#pragma pack(pop)

int _sas_cur_speed_get(char *err_msg, uint8_t *mode_sense_data,
                       const char *sas_addr, uint32_t *link_speed) {
    struct _sas_phy_ctrl_dicov_hdr *phy_header = NULL;
    uint16_t len = 0;
    struct _sas_phy_mode_dp *phy_dp = NULL;
    uint8_t i = 0;
    uint8_t *end_p = NULL;
    char cur_sas_addr[_SG_T10_SPL_SAS_ADDR_LEN];
    uint8_t link_rate = 0;
    int rc = LSM_ERR_OK;

    assert(mode_sense_data != NULL);
    assert(sas_addr != NULL);
    assert(link_speed != NULL);

    *link_speed = LSM_DISK_LINK_SPEED_UNKNOWN;

    phy_header = (struct _sas_phy_ctrl_dicov_hdr *)mode_sense_data;

    len = be16toh(phy_header->len_be);
    if (len >= _SG_T10_SPC_MODE_SENSE_MAX_LEN - 4)
        /* Corrupted MODE SENSE data */
        return rc;

    end_p = mode_sense_data + len + 4;

    for (i = 0; i < phy_header->num_of_phys; ++i) {
        if (mode_sense_data + sizeof(struct _sas_phy_ctrl_dicov_hdr) +
                sizeof(struct _sas_phy_mode_dp) * i >=
            end_p)
            /* Corrupted MODE SENSE data */
            return rc;
        phy_dp =
            (struct _sas_phy_mode_dp *)(mode_sense_data +
                                        sizeof(struct _sas_phy_ctrl_dicov_hdr) +
                                        sizeof(struct _sas_phy_mode_dp) * i);
        _be_raw_to_hex(phy_dp->sas_addr, _SG_T10_SPL_SAS_ADDR_LEN_BITS,
                       cur_sas_addr);
        if (strcmp(sas_addr, cur_sas_addr) == 0) {
            link_rate = phy_dp->negotiated_logical_link_rate;
            break;
        }
    }
    switch (link_rate) {
    case _SAS_SPEED_1_5:
        *link_speed = 1500;
        break;
    case _SAS_SPEED_3_0:
        *link_speed = 3000;
        break;
    case _SAS_SPEED_6_0:
        *link_speed = 6000;
        break;
    case _SAS_SPEED_12_0:
        *link_speed = 12000;
        break;
    case _SAS_SPEED_22_5:
        *link_speed = 22500;
        break;
    default:
        rc = LSM_ERR_LIB_BUG;
        /* Yes, we treat _SAS_SPEED_UNKNOWN as bug, in that case, the OS
         * should not have /dev/sdX, hence it's a bug. */
        _lsm_err_msg_set(err_msg, "BUG: Got unexpected SAS speed code 0x%02x",
                         link_rate);
    }

    return rc;
}
