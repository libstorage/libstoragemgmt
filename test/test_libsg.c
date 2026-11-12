/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * Unit tests for the SCSI sense data parsing in libsg.c. These run on canned
 * buffers, so they need no root, no hardware and no lsmd.
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "libsg.h"
#include "libstoragemgmt/libstoragemgmt_types.h"

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

static Suite *libsg_suite(void) {
    Suite *s = suite_create("libsg");
    TCase *tc = tcase_create("sense_data");

    tcase_add_test(tc, test_sense_data_asc_get);
    tcase_add_test(tc, test_info_excep_interpret_asc);
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
