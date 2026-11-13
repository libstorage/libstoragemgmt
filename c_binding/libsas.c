/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2016-2023 Red Hat, Inc.
 *
 * Author: Gris Ge <fge@redhat.com>
 */

#include "libsas.h"
#include "libsg.h"
#include "utils.h"

#include "libstoragemgmt/libstoragemgmt_error.h"

#include <assert.h>
#include <endian.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#pragma pack(push, 1)
struct _sas_phy_ctrl_dicov_hdr {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t page_code : 6;
    uint8_t spf       : 1;
    uint8_t ps        : 1;
#else
    uint8_t ps        : 1;
    uint8_t spf       : 1;
    uint8_t page_code : 6;
#endif
    uint8_t sub_page_code;
    uint16_t len_be;
    uint8_t reserved_1;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t protocol_id : 4;
    uint8_t reserved_2  : 4;
#else
    uint8_t reserved_2  : 4;
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
    uint8_t we_dont_care_1               : 4;
#else
    uint8_t we_dont_care_1               : 4;
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
    bool matched = false;
    int rc = LSM_ERR_OK;

    assert(mode_sense_data != NULL);
    assert(sas_addr != NULL);
    assert(link_speed != NULL);

    *link_speed = LSM_DISK_LINK_SPEED_UNKNOWN;

    phy_header = (struct _sas_phy_ctrl_dicov_hdr *)mode_sense_data;

    /* Some devices (e.g. HPE MSA 2050) do not implement the Phy Control And
     * Discover subpage and answer with the short-format page 0x19 instead.
     */
    if (phy_header->page_code != _SCSI_MODE_SENSE_PSP_PAGE_CODE ||
        !phy_header->spf ||
        phy_header->sub_page_code != _SCSI_MODE_SENSE_SAS_PHY_SUB_PAGE_CODE) {
        /* A short-format page has no subpage code: that byte is its length */
        if (phy_header->spf) {
            _lsm_err_msg_set(err_msg,
                             "Device does not report SAS phy information: got "
                             "mode page 0x%02x subpage 0x%02x instead of page "
                             "0x%02x subpage 0x%02x",
                             phy_header->page_code, phy_header->sub_page_code,
                             _SCSI_MODE_SENSE_PSP_PAGE_CODE,
                             _SCSI_MODE_SENSE_SAS_PHY_SUB_PAGE_CODE);
        } else {
            _lsm_err_msg_set(err_msg,
                             "Device does not report SAS phy information: got "
                             "short-format mode page 0x%02x instead of page "
                             "0x%02x subpage 0x%02x",
                             phy_header->page_code,
                             _SCSI_MODE_SENSE_PSP_PAGE_CODE,
                             _SCSI_MODE_SENSE_SAS_PHY_SUB_PAGE_CODE);
        }
        return LSM_ERR_NO_SUPPORT;
    }

    len = be16toh(phy_header->len_be);
    if (len >= _SG_T10_SPC_MODE_SENSE_MAX_LEN - 4) {
        _lsm_err_msg_set(err_msg, "Got invalid SAS phy page length %" PRIu16,
                         len);
        return LSM_ERR_LIB_BUG;
    }

    end_p = mode_sense_data + len + 4;

    for (i = 0; i < phy_header->num_of_phys; ++i) {
        phy_dp =
            (struct _sas_phy_mode_dp *)(mode_sense_data +
                                        sizeof(struct _sas_phy_ctrl_dicov_hdr) +
                                        sizeof(struct _sas_phy_mode_dp) * i);
        if ((uint8_t *)(phy_dp + 1) > end_p) {
            /* Corrupted MODE SENSE data */
            _lsm_err_msg_set(err_msg,
                             "SAS phy page claims %" PRIu8 " phy(s) but only "
                             "%" PRIu16 " byte(s) of data",
                             phy_header->num_of_phys, len);
            return LSM_ERR_LIB_BUG;
        }
        _be_raw_to_hex(phy_dp->sas_addr, _SG_T10_SPL_SAS_ADDR_LEN_BITS,
                       cur_sas_addr);
        if (strcmp(sas_addr, cur_sas_addr) == 0) {
            link_rate = phy_dp->negotiated_logical_link_rate;
            matched = true;
            break;
        }
    }
    if (!matched) {
        _lsm_err_msg_set(err_msg,
                         "Device does not report SAS phy information for SAS "
                         "address %s: %" PRIu8 " phy(s) listed, none match",
                         sas_addr, phy_header->num_of_phys);
        return LSM_ERR_NO_SUPPORT;
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
