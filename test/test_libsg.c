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

/* CVE-2026-93433: SPC-5 rev 07 7.7.17 Unit Serial Number VPD page. The page
 * header is 4 bytes (byte 0 PERIPHERAL QUALIFIER/DEVICE TYPE, byte 1 PAGE
 * CODE, bytes 2-3 PAGE LENGTH, big-endian), followed by PAGE LENGTH bytes of
 * serial number data. _sg_parse_vpd_80() must clamp the copy to
 * serial_num_max_len - 1 and NUL-terminate, regardless of what PAGE LENGTH
 * claims; before d6ac37ed + f2675e29 it trusted PAGE LENGTH outright and
 * overflowed the caller's stack buffer (the only caller,
 * _sysfs_serial_num_of_sd_name() in lsm_local_disk.cpp, passes a
 * uint8_t[_LSM_MAX_SERIAL_NUM_LEN] = uint8_t[253] destination).
 */
#define VPD80_HDR_LEN          4 /* device qualifier/type, page code, length */
#define VPD80_PAGE_CODE_OFFSET 1
#define VPD80_PAGE_LEN_OFFSET  2
#define VPD80_DATA_OFFSET      VPD80_HDR_LEN

/* Matches _LSM_MAX_SERIAL_NUM_LEN in lsm_local_disk.cpp, the only caller. */
#define VPD80_DEST_LEN_NORMAL 253
/* A small destination exercises the clamp without a 253-byte fixture. */
#define VPD80_DEST_LEN_SMALL 8
/* Bytes kept past the destination to catch any write past its end. */
#define VPD80_GUARD_LEN  16
#define VPD80_GUARD_BYTE 0xa5

/* Source serial bytes for the truncation cases: never 0x00, so these also
 * cover a device serial with no embedded NUL terminator.
 */
#define VPD80_PATTERN_LEN 0x1000
static uint8_t vpd80_pattern[VPD80_PATTERN_LEN];

static const uint8_t vpd80_short_serial[] = {'S', 'N', '1', '2', '3', '4', '5'};

struct vpd80_case {
    const char *name;
    uint8_t page_code;
    uint16_t page_len;          /* PAGE LENGTH field, big-endian on the wire */
    const uint8_t *serial_data; /* bytes placed right after the header */
    size_t serial_data_len;     /* how many of serial_data to place */
    uint16_t dest_len;          /* serial_num_max_len passed in */
    int expect_rc;
    const uint8_t *expect_content; /* ignored unless expect_rc == LSM_ERR_OK */
    size_t expect_content_len;     /* bytes before the NUL terminator */
};

static const struct vpd80_case vpd80_cases[] = {
    {"page_len far past a small dest", _SG_T10_SPC_VPD_UNIT_SN,
     VPD80_PATTERN_LEN, vpd80_pattern, VPD80_PATTERN_LEN, VPD80_DEST_LEN_SMALL,
     LSM_ERR_OK, vpd80_pattern, VPD80_DEST_LEN_SMALL - 1},
    {"normal short serial", _SG_T10_SPC_VPD_UNIT_SN, sizeof(vpd80_short_serial),
     vpd80_short_serial, sizeof(vpd80_short_serial), VPD80_DEST_LEN_NORMAL,
     LSM_ERR_OK, vpd80_short_serial, sizeof(vpd80_short_serial)},
    {"serial exactly dest_len - 1, no NUL in source", _SG_T10_SPC_VPD_UNIT_SN,
     VPD80_DEST_LEN_NORMAL - 1, vpd80_pattern, VPD80_DEST_LEN_NORMAL - 1,
     VPD80_DEST_LEN_NORMAL, LSM_ERR_OK, vpd80_pattern,
     VPD80_DEST_LEN_NORMAL - 1},
    {"page_len == dest_len, one over", _SG_T10_SPC_VPD_UNIT_SN,
     VPD80_DEST_LEN_NORMAL, vpd80_pattern, VPD80_DEST_LEN_NORMAL,
     VPD80_DEST_LEN_NORMAL, LSM_ERR_OK, vpd80_pattern,
     VPD80_DEST_LEN_NORMAL - 1},
    {"page_len 0xffff: header + length overflows the VPD max",
     _SG_T10_SPC_VPD_UNIT_SN, 0xffff, NULL, 0, VPD80_DEST_LEN_NORMAL,
     LSM_ERR_LIB_BUG, NULL, 0},
    {"page_len 0xfffd: header + length overflows the VPD max",
     _SG_T10_SPC_VPD_UNIT_SN, 0xfffd, NULL, 0, VPD80_DEST_LEN_NORMAL,
     LSM_ERR_LIB_BUG, NULL, 0},
    {"wrong page code", _SG_T10_SPC_VPD_DI, 4, NULL, 0, VPD80_DEST_LEN_NORMAL,
     LSM_ERR_NO_SUPPORT, NULL, 0},
};

/* Builds a VPD page into a zeroed _SG_T10_SPC_VPD_MAX_LEN buffer, the way
 * _sg_io_vpd() hands it over.
 */
static void vpd80_page_build(uint8_t *buf, uint8_t page_code, uint16_t page_len,
                             const uint8_t *serial_data,
                             size_t serial_data_len) {
    memset(buf, 0, _SG_T10_SPC_VPD_MAX_LEN);
    buf[VPD80_PAGE_CODE_OFFSET] = page_code;
    buf[VPD80_PAGE_LEN_OFFSET] = (page_len >> 8) & UINT8_MAX;
    buf[VPD80_PAGE_LEN_OFFSET + 1] = page_len & UINT8_MAX;
    if (serial_data != NULL && serial_data_len > 0) {
        ck_assert_uint_le(VPD80_DATA_OFFSET + serial_data_len,
                          _SG_T10_SPC_VPD_MAX_LEN);
        memcpy(buf + VPD80_DATA_OFFSET, serial_data, serial_data_len);
    }
}

START_TEST(test_parse_vpd_80) {
    static uint8_t page[_SG_T10_SPC_VPD_MAX_LEN];
    static uint8_t dest_arena[VPD80_DEST_LEN_NORMAL + VPD80_GUARD_LEN];
    char err_msg[_LSM_ERR_MSG_LEN];
    const struct vpd80_case *c = NULL;
    int rc = 0;
    size_t i = 0;
    size_t j = 0;

    for (i = 0; i < VPD80_PATTERN_LEN; i++)
        vpd80_pattern[i] = (uint8_t)((i % 0xfe) + 1); /* never 0x00 */

    for (i = 0; i < sizeof(vpd80_cases) / sizeof(vpd80_cases[0]); i++) {
        c = &vpd80_cases[i];
        vpd80_page_build(page, c->page_code, c->page_len, c->serial_data,
                         c->serial_data_len);
        memset(dest_arena, VPD80_GUARD_BYTE, sizeof(dest_arena));
        memset(err_msg, 0, sizeof(err_msg));

        rc = _sg_parse_vpd_80(err_msg, page, dest_arena, c->dest_len);
        ck_assert_msg(rc == c->expect_rc, "%s: rc %d, expected %d (%s)",
                      c->name, rc, c->expect_rc, err_msg);

        for (j = c->dest_len; j < sizeof(dest_arena); j++)
            ck_assert_msg(dest_arena[j] == VPD80_GUARD_BYTE,
                          "%s: guard byte %zu clobbered (0x%02x)", c->name, j,
                          dest_arena[j]);

        if (c->expect_rc == LSM_ERR_OK) {
            ck_assert_msg(memcmp(dest_arena, c->expect_content,
                                 c->expect_content_len) == 0,
                          "%s: serial content mismatch", c->name);
            ck_assert_msg(dest_arena[c->expect_content_len] == '\0',
                          "%s: no NUL terminator at offset %zu", c->name,
                          c->expect_content_len);
        } else {
            for (j = 0; j < c->dest_len; j++)
                ck_assert_msg(dest_arena[j] == 0,
                              "%s: dest byte %zu not zeroed on error (0x%02x)",
                              c->name, j, dest_arena[j]);
        }
    }
}
END_TEST

static Suite *libsg_suite(void) {
    Suite *s = suite_create("libsg");
    TCase *tc = tcase_create("vpd80");

    tcase_add_test(tc, test_parse_vpd_80);
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
