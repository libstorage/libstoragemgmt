/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * Unit tests for the SCSI sense data parsing in libsg.c. These run on canned
 * buffers, so they need no root, no hardware and no lsmd.
 */

#include <check.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsas.h"
#include "libsg.h"
#include "libstoragemgmt/libstoragemgmt_error.h"
#include "libstoragemgmt/libstoragemgmt_types.h"
#include "utils.h"

/* Offsets of ADDITIONAL SENSE CODE, SPC-5 rev 07 Tables 28 and 30 */
#define SENSE_FIXED_ASC_OFFSET 12
#define SENSE_DP_ASC_OFFSET    2

/* Distinct values so reading the wrong offset gives a different answer. */
#define SENTINEL_FIXED_ASC 0xa1
#define SENTINEL_DP_ASC    0xb2

/* Bit 7 of byte 0 is not part of the response code in either format: it is
 * VALID in fixed format (SPC-5 rev 07 Table 28) and reserved in descriptor
 * format.
 */
#define SENSE_VALID_BIT 0x80

#define NOT_FOUND (-1)

struct asc_case {
    const char *name;
    uint8_t response_code; /* byte 0 */
    int expect_rc;
    uint8_t expect_asc; /* only checked when expect_rc == 0 */
};

static const struct asc_case asc_cases[] = {
    {"current fixed", _T10_SPC_SENSE_REPORT_TYPE_CUR_INFO_FIXED, 0,
     SENTINEL_FIXED_ASC},
    {"deferred fixed", _T10_SPC_SENSE_REPORT_TYPE_DEF_ERR_FIXED, 0,
     SENTINEL_FIXED_ASC},
    {"current descriptor", _T10_SPC_SENSE_REPORT_TYPE_CUR_INFO_DP, 0,
     SENTINEL_DP_ASC},
    {"deferred descriptor", _T10_SPC_SENSE_REPORT_TYPE_DEF_ERR_DP, 0,
     SENTINEL_DP_ASC},
    {"fixed with VALID bit",
     SENSE_VALID_BIT | _T10_SPC_SENSE_REPORT_TYPE_CUR_INFO_FIXED, 0,
     SENTINEL_FIXED_ASC},
    {"descriptor with bit 7 set",
     SENSE_VALID_BIT | _T10_SPC_SENSE_REPORT_TYPE_CUR_INFO_DP, 0,
     SENTINEL_DP_ASC},
    {"all zero", 0x00, NOT_FOUND, 0},
    {"vendor specific", 0x7f, NOT_FOUND, 0},
};

START_TEST(test_sense_data_asc_get) {
    uint8_t sense[_T10_SPC_SENSE_DATA_MAX_LENGTH];
    uint8_t asc = 0;
    const struct asc_case *c = NULL;
    int rc = 0;
    size_t i = 0;

    for (i = 0; i < sizeof(asc_cases) / sizeof(asc_cases[0]); i++) {
        c = &asc_cases[i];
        memset(sense, 0, sizeof(sense));
        sense[0] = c->response_code;
        sense[SENSE_DP_ASC_OFFSET] = SENTINEL_DP_ASC;
        sense[SENSE_FIXED_ASC_OFFSET] = SENTINEL_FIXED_ASC;
        asc = 0;

        rc = _sense_data_asc_get(sense, &asc);
        ck_assert_msg(rc == c->expect_rc, "%s: rc %d, expected %d", c->name, rc,
                      c->expect_rc);
        if (c->expect_rc == 0)
            ck_assert_msg(asc == c->expect_asc,
                          "%s: asc 0x%02x, expected 0x%02x", c->name, asc,
                          c->expect_asc);
    }
}
END_TEST

struct interpret_case {
    const char *name;
    uint8_t asc;
    int32_t expect;
};

static const struct interpret_case interpret_cases[] = {
    {"impending failure", _T10_SPC_ASC_IMPENDING_FAILURE,
     LSM_DISK_HEALTH_STATUS_FAIL},
    {"warning", _T10_SPC_ASC_WARNING, LSM_DISK_HEALTH_STATUS_WARN},
    {"no additional sense", 0x00, LSM_DISK_HEALTH_STATUS_GOOD},
    {"other", 0xff, LSM_DISK_HEALTH_STATUS_GOOD},
};

START_TEST(test_info_excep_interpret_asc) {
    const struct interpret_case *c = NULL;
    int32_t got = 0;
    size_t i = 0;

    for (i = 0; i < sizeof(interpret_cases) / sizeof(interpret_cases[0]); i++) {
        c = &interpret_cases[i];
        got = _sg_info_excep_interpret_asc(c->asc);
        ck_assert_msg(got == c->expect, "%s: asc 0x%02x gave %d, expected %d",
                      c->name, c->asc, got, c->expect);
    }
}
END_TEST

/* SPL-4 Phy Control And Discover mode subpage layout */
#define SAS_PHY_PS_BIT           0x80
#define SAS_PHY_SPF_BIT          0x40
#define SAS_PHY_PAGE_HDR_LEN     8
#define SAS_PHY_DESC_LEN         48
#define SAS_PHY_PAGE_LEN_FIXED   (SAS_PHY_PAGE_HDR_LEN - 4) /* after byte 3 */
#define SAS_PHY_PROTOCOL_ID_SAS  6
#define SAS_PHY_HDR_PROTO_OFFSET 5
#define SAS_PHY_HDR_GEN_OFFSET   6
#define SAS_PHY_HDR_NPHYS_OFFSET 7
#define SAS_PHY_DESC_ID_OFFSET   1
#define SAS_PHY_DESC_RATE_OFFSET 5
#define SAS_PHY_DESC_ADDR_OFFSET 8
#define SAS_ADDR_RAW_LEN         8
/* The rate shares its byte with other fields; fill them so a reader of the
 * whole byte gets the wrong answer.
 */
#define SAS_PHY_DESC_RATE_OTHER_BITS 0xa0
#define SAS_PHY_HDR_ID_LEN           2 /* page code byte and subpage code */
#define SAS_PHY_VALID_BYTE0 (SAS_PHY_SPF_BIT | _SCSI_MODE_SENSE_PSP_PAGE_CODE)

#define SAS_ADDR_A "5000c50012345678"
#define SAS_ADDR_B "5000c500abcdef01"
#define SAS_ADDR_C "5000c50000000099"

struct sas_phy {
    const char *addr; /* hex string as _sas_cur_speed_get() expects */
    uint8_t rate;     /* NEGOTIATED LOGICAL LINK RATE */
};

/* Build a Phy Control And Discover page into a zeroed
 * _SG_T10_SPC_MODE_SENSE_MAX_LEN buffer, the way _sg_io_mode_sense() hands
 * it over.
 */
static void sas_phy_page_build(uint8_t *buf, const struct sas_phy *phys,
                               uint8_t num_phys) {
    uint16_t page_len = SAS_PHY_PAGE_LEN_FIXED + SAS_PHY_DESC_LEN * num_phys;
    uint8_t *dp = NULL;
    unsigned int byte = 0;
    uint8_t i = 0;
    size_t j = 0;

    memset(buf, 0, _SG_T10_SPC_MODE_SENSE_MAX_LEN);
    buf[0] = SAS_PHY_SPF_BIT | _SCSI_MODE_SENSE_PSP_PAGE_CODE;
    buf[1] = _SCSI_MODE_SENSE_SAS_PHY_SUB_PAGE_CODE;
    buf[2] = (page_len >> 8) & UINT8_MAX;
    buf[3] = page_len & UINT8_MAX;
    buf[SAS_PHY_HDR_PROTO_OFFSET] = SAS_PHY_PROTOCOL_ID_SAS;
    buf[SAS_PHY_HDR_GEN_OFFSET] = 1;
    buf[SAS_PHY_HDR_NPHYS_OFFSET] = num_phys;

    for (i = 0; i < num_phys; i++) {
        dp = buf + SAS_PHY_PAGE_HDR_LEN + SAS_PHY_DESC_LEN * i;
        dp[SAS_PHY_DESC_ID_OFFSET] = i;
        dp[SAS_PHY_DESC_RATE_OFFSET] =
            SAS_PHY_DESC_RATE_OTHER_BITS | phys[i].rate;
        for (j = 0; j < SAS_ADDR_RAW_LEN; j++) {
            ck_assert_int_eq(sscanf(phys[i].addr + j * 2, "%2x", &byte), 1);
            dp[SAS_PHY_DESC_ADDR_OFFSET + j] = byte;
        }
    }
}

static const struct sas_phy two_phys[] = {
    {SAS_ADDR_A, _SAS_SPEED_6_0},
    {SAS_ADDR_B, _SAS_SPEED_12_0},
};

struct sas_speed_case {
    const char *name;
    const struct sas_phy *phys;
    uint8_t num_phys;
    const char *want_addr;
    int expect_rc;
    uint32_t expect_speed; /* LSM_DISK_LINK_SPEED_UNKNOWN when rc != OK */
    /* Replaces bytes 0 and 1 of the built page; NULL keeps the valid ones */
    const uint8_t *hdr_id;
};

#define HDR_ID(byte0, byte1) ((const uint8_t[SAS_PHY_HDR_ID_LEN]){byte0, byte1})

static const struct sas_phy one_bad_rate[] = {{SAS_ADDR_A, _SAS_SPEED_UNKNOWN}};

#define ONE_RATE_CASE(label, code, speed)                                      \
    {label,      (const struct sas_phy[]){{SAS_ADDR_A, code}},                 \
     1,          SAS_ADDR_A,                                                   \
     LSM_ERR_OK, speed,                                                        \
     NULL}

static const struct sas_speed_case sas_speed_cases[] = {
    {"two phys, second phy asked", two_phys, 2, SAS_ADDR_B, LSM_ERR_OK, 12000,
     NULL},
    {"two phys, first phy asked", two_phys, 2, SAS_ADDR_A, LSM_ERR_OK, 6000,
     NULL},
    {"two phys, address matches neither", two_phys, 2, SAS_ADDR_C,
     LSM_ERR_NO_SUPPORT, LSM_DISK_LINK_SPEED_UNKNOWN, NULL},
    {"subpage with zero phys", NULL, 0, SAS_ADDR_A, LSM_ERR_NO_SUPPORT,
     LSM_DISK_LINK_SPEED_UNKNOWN, NULL},
    {"matched phy with rate code 0", one_bad_rate, 1, SAS_ADDR_A,
     LSM_ERR_LIB_BUG, LSM_DISK_LINK_SPEED_UNKNOWN, NULL},
    ONE_RATE_CASE("rate 1.5 Gbps", _SAS_SPEED_1_5, 1500),
    ONE_RATE_CASE("rate 3 Gbps", _SAS_SPEED_3_0, 3000),
    ONE_RATE_CASE("rate 6 Gbps", _SAS_SPEED_6_0, 6000),
    ONE_RATE_CASE("rate 12 Gbps", _SAS_SPEED_12_0, 12000),
    ONE_RATE_CASE("rate 22.5 Gbps", _SAS_SPEED_22_5, 22500),
    /* Phys that would match, behind a header that is not the phy subpage */
    {"PS bit set on a valid page", two_phys, 2, SAS_ADDR_B, LSM_ERR_OK, 12000,
     HDR_ID(SAS_PHY_PS_BIT | SAS_PHY_VALID_BYTE0,
            _SCSI_MODE_SENSE_SAS_PHY_SUB_PAGE_CODE)},
    {"wrong page code", two_phys, 2, SAS_ADDR_B, LSM_ERR_NO_SUPPORT,
     LSM_DISK_LINK_SPEED_UNKNOWN,
     HDR_ID(SAS_PHY_SPF_BIT | (_SCSI_MODE_SENSE_PSP_PAGE_CODE - 1),
            _SCSI_MODE_SENSE_SAS_PHY_SUB_PAGE_CODE)},
    {"wrong subpage code", two_phys, 2, SAS_ADDR_B, LSM_ERR_NO_SUPPORT,
     LSM_DISK_LINK_SPEED_UNKNOWN,
     HDR_ID(SAS_PHY_VALID_BYTE0, _SCSI_MODE_SENSE_SAS_PHY_SUB_PAGE_CODE + 1)},
    {"SPF clear", two_phys, 2, SAS_ADDR_B, LSM_ERR_NO_SUPPORT,
     LSM_DISK_LINK_SPEED_UNKNOWN,
     HDR_ID(_SCSI_MODE_SENSE_PSP_PAGE_CODE,
            _SCSI_MODE_SENSE_SAS_PHY_SUB_PAGE_CODE)},
};

START_TEST(test_sas_cur_speed_get) {
    static uint8_t buf[_SG_T10_SPC_MODE_SENSE_MAX_LEN];
    char err_msg[_LSM_ERR_MSG_LEN];
    const struct sas_speed_case *c = NULL;
    uint32_t speed = 0;
    int rc = 0;
    size_t i = 0;

    for (i = 0; i < sizeof(sas_speed_cases) / sizeof(sas_speed_cases[0]); i++) {
        c = &sas_speed_cases[i];
        sas_phy_page_build(buf, c->phys, c->num_phys);
        if (c->hdr_id != NULL)
            memcpy(buf, c->hdr_id, SAS_PHY_HDR_ID_LEN);
        memset(err_msg, 0, sizeof(err_msg));
        speed = 0xdeadbeef;

        rc = _sas_cur_speed_get(err_msg, buf, c->want_addr, &speed);
        ck_assert_msg(rc == c->expect_rc, "%s: rc %d, expected %d (%s)",
                      c->name, rc, c->expect_rc, err_msg);
        ck_assert_msg(speed == c->expect_speed,
                      "%s: speed %" PRIu32 ", expected %" PRIu32, c->name,
                      speed, c->expect_speed);
    }
}
END_TEST

/* Replies that are not the phy subpage at all, as _sg_io_mode_sense() hands
 * them over. None has a phy to find, so all of them are "no support".
 *
 * The HPE MSA 2050 SAS array (multipath LUNs) really answers a request for
 * page 0x19 subpage 0x01 this way: the short-format page 0x19 (SPF clear,
 * page length 6, protocol SAS) with no phy descriptors.
 */
#define RAW_REPLY_MAX_LEN 8

struct sas_raw_case {
    const char *name;
    uint8_t reply[RAW_REPLY_MAX_LEN];
};

static const struct sas_raw_case sas_raw_cases[] = {
    {"HPE MSA 2050 short-format page 0x19",
     {0x19, 0x06, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"all zero", {0}},
};

START_TEST(test_sas_cur_speed_get_not_phy_subpage) {
    static uint8_t buf[_SG_T10_SPC_MODE_SENSE_MAX_LEN];
    char err_msg[_LSM_ERR_MSG_LEN];
    const struct sas_raw_case *c = NULL;
    uint32_t speed = 0;
    int rc = 0;
    size_t i = 0;

    for (i = 0; i < sizeof(sas_raw_cases) / sizeof(sas_raw_cases[0]); i++) {
        c = &sas_raw_cases[i];
        memset(buf, 0, sizeof(buf));
        memcpy(buf, c->reply, sizeof(c->reply));
        memset(err_msg, 0, sizeof(err_msg));
        speed = 0xdeadbeef;

        rc = _sas_cur_speed_get(err_msg, buf, SAS_ADDR_A, &speed);
        ck_assert_msg(rc == LSM_ERR_NO_SUPPORT, "%s: rc %d, expected %d (%s)",
                      c->name, rc, LSM_ERR_NO_SUPPORT, err_msg);
        ck_assert_msg(speed == LSM_DISK_LINK_SPEED_UNKNOWN,
                      "%s: speed %" PRIu32 " on error", c->name, speed);
        /* The header check has to be what refused it, not the phy search */
        ck_assert_msg(strstr(err_msg, "none match") == NULL,
                      "%s: fell through to the phy search (%s)", c->name,
                      err_msg);
    }
}
END_TEST

static Suite *libsg_suite(void) {
    Suite *s = suite_create("libsg");
    TCase *tc = tcase_create("sense_data");

    tcase_add_test(tc, test_sense_data_asc_get);
    tcase_add_test(tc, test_info_excep_interpret_asc);
    suite_add_tcase(s, tc);

    tc = tcase_create("sas_speed");
    tcase_add_test(tc, test_sas_cur_speed_get);
    tcase_add_test(tc, test_sas_cur_speed_get_not_phy_subpage);
    suite_add_tcase(s, tc);
    return s;
}

int main(void) {
    SRunner *sr = srunner_create(libsg_suite());
    int number_failed = 0;

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
