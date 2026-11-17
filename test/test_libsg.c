/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * Unit tests for the SCSI sense data parsing in libsg.c. These run on canned
 * buffers, so they need no root, no hardware and no lsmd.
 */

#include <check.h>
#include <errno.h>
#include <inttypes.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "libsas.h"
#include "libsg.h"
#include "libstoragemgmt/libstoragemgmt_error.h"
#include "libstoragemgmt/libstoragemgmt_types.h"
#include "utils.h"

/* test_libsg is linked with -Wl,--wrap=ioctl (test/Makefile.am) so the
 * SG_IO paths in libsg.c (_sg_io_vpd, _sg_io_mode_sense, _sg_request_sense,
 * ...) can be driven with canned device responses instead of real hardware.
 * Only the v3 (struct sg_io_hdr) ioctl interface is in play: every SG_IO
 * call site in libsg.c goes through _sg_io_v3(), never _sg_io_v4().
 *
 * A test selects a response by calling fake_ioctl_reset() and then filling
 * in what it wants returned, before calling into the libsg.c function under
 * test with any fd value (it is never actually used).
 */
extern int __real_ioctl(int fd, unsigned long request, ...);

#define FAKE_IOCTL_DXFER_MAX 256

static uint8_t fake_ioctl_dxfer[FAKE_IOCTL_DXFER_MAX];
static size_t fake_ioctl_dxfer_len;
/* LOG SENSE replies get their own slot so a test can serve a MODE SENSE
 * reply and a LOG SENSE reply to the same _sg_sas_health_status() call.
 */
static uint8_t fake_ioctl_log_dxfer[FAKE_IOCTL_DXFER_MAX];
static size_t fake_ioctl_log_dxfer_len;
static uint8_t fake_ioctl_sense[_T10_SPC_SENSE_DATA_MAX_LENGTH];
static size_t fake_ioctl_sense_len;
/* 0 means the ioctl() call itself succeeds (a real device still may return
 * CHECK CONDITION via fake_ioctl_sense_len, same as _sg_io_v3 expects).
 */
static int fake_ioctl_errno;

static void fake_ioctl_reset(void) {
    memset(fake_ioctl_dxfer, 0, sizeof(fake_ioctl_dxfer));
    fake_ioctl_dxfer_len = 0;
    memset(fake_ioctl_log_dxfer, 0, sizeof(fake_ioctl_log_dxfer));
    fake_ioctl_log_dxfer_len = 0;
    memset(fake_ioctl_sense, 0, sizeof(fake_ioctl_sense));
    fake_ioctl_sense_len = 0;
    fake_ioctl_errno = 0;
}

int __wrap_ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    void *arg = NULL;
    struct sg_io_hdr *hdr = NULL;
    size_t n = 0;

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);

    if (request != SG_IO)
        return __real_ioctl(fd, request, arg);

    hdr = (struct sg_io_hdr *)arg;

    if (fake_ioctl_errno != 0) {
        errno = fake_ioctl_errno;
        return -1;
    }

    if ((hdr->dxferp != NULL) && (hdr->dxfer_direction == SG_DXFER_FROM_DEV)) {
        const uint8_t *src = fake_ioctl_dxfer;
        size_t src_len = fake_ioctl_dxfer_len;

        if ((hdr->cmdp != NULL) && (hdr->cmd_len > 0) &&
            (hdr->cmdp[0] == LOG_SENSE)) {
            src = fake_ioctl_log_dxfer;
            src_len = fake_ioctl_log_dxfer_len;
        }
        n = src_len;
        if (n > hdr->dxfer_len)
            n = hdr->dxfer_len;
        memcpy(hdr->dxferp, src, n);
    }

    if ((hdr->sbp != NULL) && (fake_ioctl_sense_len > 0)) {
        n = fake_ioctl_sense_len;
        if (n > hdr->mx_sb_len)
            n = hdr->mx_sb_len;
        memcpy(hdr->sbp, fake_ioctl_sense, n);
        hdr->sb_len_wr = (unsigned char)n;
    } else {
        hdr->sb_len_wr = 0;
    }

    hdr->status = 0;
    return 0;
}

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

/* f3c7fe3e + f2675e29: _sg_parse_vpd_83() must reject a designation
 * descriptor (SPC-5 rev 7 Table 590) whose DESIGNATOR LENGTH runs past the
 * end of the page data (f3c7fe3e), and a page too short to hold even the
 * 4-byte descriptor header (f2675e29), rather than trusting either and
 * walking (and later memcpy-ing) past where the page actually ends.
 */
#define VPD83_PAGE_CODE_OFFSET 1
#define VPD83_PAGE_LEN_OFFSET  2
#define VPD83_HDR_LEN          4 /* dev type/qualifier, page code, length */
#define VPD83_DP_OFFSET        VPD83_HDR_LEN
#define VPD83_DP_HDR_LEN       4 /* code_set/.../piv, reserved, desig_len */
#define VPD83_DP_DESIGNATOR_LEN_OFFSET                                         \
    3 /* relative to the descriptor start                                      \
       */

#define VPD83_GUARD_LEN  24
#define VPD83_GUARD_BYTE 0xcc

static void vpd83_page_build(uint8_t *page, uint16_t page_len,
                             uint8_t designator_len) {
    memset(page, 0, _SG_T10_SPC_VPD_MAX_LEN);
    page[VPD83_PAGE_CODE_OFFSET] = _SG_T10_SPC_VPD_DI;
    page[VPD83_PAGE_LEN_OFFSET] = (page_len >> 8) & UINT8_MAX;
    page[VPD83_PAGE_LEN_OFFSET + 1] = page_len & UINT8_MAX;
    page[VPD83_DP_OFFSET + VPD83_DP_DESIGNATOR_LEN_OFFSET] = designator_len;
    /* Data past the page (VPD83_HDR_LEN + page_len) is not part of this
     * page at all; mark it so a read that strays past the page boundary is
     * unmistakable in the parsed designator.
     */
    ck_assert_uint_le(VPD83_HDR_LEN + page_len + VPD83_GUARD_LEN,
                      _SG_T10_SPC_VPD_MAX_LEN);
    memset(page + VPD83_HDR_LEN + page_len, VPD83_GUARD_BYTE, VPD83_GUARD_LEN);
}

struct vpd83_case {
    const char *name;
    uint16_t page_len;
    uint8_t designator_len;
};

static const struct vpd83_case vpd83_cases[] = {
    /* 4-byte descriptor header + 6 bytes of data, but DESIGNATOR LENGTH
     * claims far more than page_len allows.
     */
    {"designator_len past page data", 10, 20},
    /* Page data too short to hold even the 4-byte descriptor header. */
    {"partial descriptor header", 2, 0},
};

START_TEST(test_parse_vpd_83_bounds) {
    static uint8_t page[_SG_T10_SPC_VPD_MAX_LEN];
    char err_msg[_LSM_ERR_MSG_LEN];
    const struct vpd83_case *c = NULL;
    struct _sg_t10_vpd83_dp **dps = NULL;
    uint16_t dp_count = 0xffff;
    int rc = 0;
    size_t i = 0;

    for (i = 0; i < sizeof(vpd83_cases) / sizeof(vpd83_cases[0]); i++) {
        c = &vpd83_cases[i];
        vpd83_page_build(page, c->page_len, c->designator_len);

        memset(err_msg, 0, sizeof(err_msg));
        dps = NULL;
        dp_count = 0xffff;
        rc = _sg_parse_vpd_83(err_msg, page, &dps, &dp_count);

        ck_assert_msg(rc == LSM_ERR_LIB_BUG,
                      "%s: rc %d, dp_count %" PRIu16 " (%s)", c->name, rc,
                      dp_count, err_msg);
        ck_assert_msg(dps == NULL, "%s: dps not NULL", c->name);
        ck_assert_msg(dp_count == 0, "%s: dp_count %" PRIu16, c->name,
                      dp_count);
    }

    /* Belt and suspenders: even if a future change let an oversized
     * designator_len through, the guard bytes placed past the page must
     * never surface in a designator.
     */
    vpd83_page_build(page, vpd83_cases[0].page_len,
                     vpd83_cases[0].designator_len);
    rc = _sg_parse_vpd_83(err_msg, page, &dps, &dp_count);
    if (rc == LSM_ERR_OK && dp_count > 0) {
        for (i = 0; i < dps[0]->header.designator_len; i++)
            ck_assert_msg(dps[0]->designator[i] != VPD83_GUARD_BYTE,
                          "designator[%zu] leaked a byte past the page "
                          "(0x%02x)",
                          i, dps[0]->designator[i]);
        _sg_t10_vpd83_dp_array_free(dps, dp_count);
    }
}
END_TEST

/* aad71dcc + 328ed4ed: _check_sense_data() must clamp the ADDITIONAL SENSE
 * LENGTH derived length to _T10_SPC_SENSE_DATA_MAX_LENGTH before hex-dumping
 * sense_data, rather than trusting a device-reported length that can run
 * past the end of the buffer.
 */
#define SENSE_RESPONSE_CODE_OFFSET 0
#define SENSE_KEY_OFFSET           2
#define SENSE_LEN_OFFSET           7
#define SENSE_ASC_OFFSET           12
#define SENSE_ASCQ_OFFSET          13

/* SPC-5 rev 07 Table 48 - Sense key descriptions (not exported: the parser
 * only needs the three "no error" keys and one "has error" key to test).
 */
#define SENSE_KEY_NO_SENSE        0x0
#define SENSE_KEY_RECOVERED_ERROR 0x1
#define SENSE_KEY_ILLEGAL_REQUEST 0x5
#define SENSE_KEY_COMPLETED       0xf

#define SENSE_UNKNOWN_RESPONSE_CODE 0x55

/* Bytes kept right after the sense_data buffer to catch any read past it. */
#define SENSE_GUARD_LEN    8
#define SENSE_GUARD_BYTE   0xee
#define SENSE_GUARD_NEEDLE "eeeeee"

struct sense_check_case {
    const char *name;
    uint8_t response_code;
    uint8_t sense_key;
    uint8_t len_field; /* ADDITIONAL SENSE LENGTH, byte 7 */
    uint8_t asc;
    uint8_t ascq;
    int expect_rc;
    int expect_msg_untouched;
    const char *expect_substr; /* ignored when expect_msg_untouched */
    /* 0 to skip: otherwise, byte at _T10_SPC_SENSE_DATA_MAX_LENGTH - 1 is
     * set to this and must show up as the last hex pair in the dump.
     * Guards against 328ed4ed's uint8_t overflow of 'len', which (with the
     * aad71dcc clamp alone, but the narrow type) truncates len to near 0 and
     * skips the hex dump loop entirely instead of running it out to the
     * clamp.
     */
    uint8_t last_byte_marker;
};

static const struct sense_check_case sense_check_cases[] = {
    {"all-zero sense data", 0x00, 0, 0, 0, 0, 0, 1, NULL, 0},
    {"unknown response code", SENSE_UNKNOWN_RESPONSE_CODE, 0, 0, 0, 0, -1, 0,
     "unknown sense data response code 55", 0},
    {"exempt key: no sense", _T10_SPC_SENSE_REPORT_TYPE_CUR_INFO_FIXED,
     SENSE_KEY_NO_SENSE, 0, 0, 0, 0, 1, NULL, 0},
    {"exempt key: recovered error", _T10_SPC_SENSE_REPORT_TYPE_CUR_INFO_FIXED,
     SENSE_KEY_RECOVERED_ERROR, 0, 0, 0, 0, 1, NULL, 0},
    {"exempt key: completed", _T10_SPC_SENSE_REPORT_TYPE_CUR_INFO_FIXED,
     SENSE_KEY_COMPLETED, 0, 0, 0, 0, 1, NULL, 0},
    {"illegal request, short length", _T10_SPC_SENSE_REPORT_TYPE_CUR_INFO_FIXED,
     SENSE_KEY_ILLEGAL_REQUEST, 5, 0x11, 0x22, -1, 0,
     "ADDITIONAL SENSE CODE 0x11", 0},
    /* CVE regression (aad71dcc/328ed4ed): len_field 248 means a claimed
     * total length of 256 (248 + the 8-byte fixed header), one past what
     * the loop counter 'i' (uint8_t) can represent. Without the aad71dcc
     * clamp, 'i' wraps from 255 back to 0 and the loop never reaches 'len'
     * again, hanging instead of returning. Without 328ed4ed (len back to
     * uint8_t), 256 truncates to 0 even with the clamp applied, and the hex
     * dump loop never runs at all, silently dropping the last_byte_marker
     * byte it should have included.
     */
    {"length wraps the uint8_t loop counter without the fixes",
     _T10_SPC_SENSE_REPORT_TYPE_CUR_INFO_FIXED, SENSE_KEY_ILLEGAL_REQUEST, 248,
     0x11, 0x22, -1, 0, "ADDITIONAL SENSE CODE 0x11", 0x99},
};

static void sense_fixed_build(uint8_t *arena,
                              const struct sense_check_case *c) {
    memset(arena, 0, _T10_SPC_SENSE_DATA_MAX_LENGTH);
    memset(arena + _T10_SPC_SENSE_DATA_MAX_LENGTH, SENSE_GUARD_BYTE,
           SENSE_GUARD_LEN);
    arena[SENSE_RESPONSE_CODE_OFFSET] = c->response_code;
    arena[SENSE_KEY_OFFSET] = c->sense_key & 0x0f;
    arena[SENSE_LEN_OFFSET] = c->len_field;
    arena[SENSE_ASC_OFFSET] = c->asc;
    arena[SENSE_ASCQ_OFFSET] = c->ascq;
    if (c->last_byte_marker != 0)
        arena[_T10_SPC_SENSE_DATA_MAX_LENGTH - 1] = c->last_byte_marker;
}

#define SENSE_ERR_MSG_SENTINEL 'Z'

START_TEST(test_check_sense_data) {
    static uint8_t arena[_T10_SPC_SENSE_DATA_MAX_LENGTH + SENSE_GUARD_LEN];
    char err_msg[_LSM_ERR_MSG_LEN / 2];
    const struct sense_check_case *c = NULL;
    uint8_t sense_key = 0;
    int rc = 0;
    size_t i = 0;
    size_t j = 0;

    for (i = 0; i < sizeof(sense_check_cases) / sizeof(sense_check_cases[0]);
         i++) {
        c = &sense_check_cases[i];
        sense_fixed_build(arena, c);
        memset(err_msg, SENSE_ERR_MSG_SENTINEL, sizeof(err_msg));
        sense_key = 0xff;

        rc = _check_sense_data(err_msg, arena, &sense_key);
        ck_assert_msg(rc == c->expect_rc, "%s: rc %d, expected %d", c->name, rc,
                      c->expect_rc);

        if (c->expect_msg_untouched) {
            for (j = 0; j < sizeof(err_msg); j++)
                ck_assert_msg(err_msg[j] == SENSE_ERR_MSG_SENTINEL,
                              "%s: err_msg written to at byte %zu", c->name, j);
        } else {
            ck_assert_msg(strstr(err_msg, c->expect_substr) != NULL,
                          "%s: err_msg missing '%s' (%s)", c->name,
                          c->expect_substr, err_msg);
            ck_assert_msg(strstr(err_msg, SENSE_GUARD_NEEDLE) == NULL,
                          "%s: err_msg leaked guard bytes past the sense "
                          "data buffer (%s)",
                          c->name, err_msg);
            if (c->last_byte_marker != 0) {
                char marker_hex[3];
                snprintf(marker_hex, sizeof(marker_hex), "%02x",
                         c->last_byte_marker);
                ck_assert_msg(strstr(err_msg, marker_hex) != NULL,
                              "%s: hex dump missing last_byte_marker %s, "
                              "the loop did not run out to the clamp (%s)",
                              c->name, marker_hex, err_msg);
            }
        }
    }
}
END_TEST

/* abd55816 + f2675e29: _extract_ata_sense_data() must clamp the end of the
 * descriptor list to _T10_SPC_SENSE_DATA_MAX_LENGTH (abd55816) and refuse a
 * truncated descriptor header or ATA status descriptor (f2675e29), instead
 * of trusting the device-reported ADDITIONAL SENSE LENGTH to find where the
 * descriptor list actually ends.
 */
#define ATA_SENSE_LEN_OFFSET 7
#define ATA_SENSE_DP_OFFSET  8    /* sense_data_dp_list_begin, SPC-5 Table 28 */
#define ATA_DP_HDR_LEN       2    /* descriptor_code + len, SPC-5 Table 29 */
#define ATA_STATUS_DP_CODE   0x09 /* SAT-4 Table 176 */
#define ATA_STATUS_DP_LEN    0x0c /* SAT-4 Table 176, length after header */
#define ATA_STATUS_DP_TOTAL  (ATA_DP_HDR_LEN + ATA_STATUS_DP_LEN)
#define ATA_NON_ATA_DP_CODE  0xaa /* anything other than ATA_STATUS_DP_CODE */

/* Offsets of the fields that matter inside an ATA status descriptor,
 * relative to its own descriptor_code byte (SAT-4 rev 04 Table 176).
 */
#define ATA_DP_LBA_MID_OFFSET  9
#define ATA_DP_LBA_HIGH_OFFSET 11
#define ATA_DP_STATUS_OFFSET   13

#define ATA_SENSE_OUT_SENTINEL 0xff /* caller's pre-call value */
#define ATA_SENSE_GUARD_BYTE   0x5a /* distinct from any real test value */

START_TEST(test_extract_ata_sense_data) {
    static uint8_t arena[_T10_SPC_SENSE_DATA_MAX_LENGTH + SENSE_GUARD_LEN];
    char err_msg[_LSM_ERR_MSG_LEN];
    uint8_t status = 0;
    uint8_t lba_mid = 0;
    uint8_t lba_high = 0;
    int rc = 0;

    /* Normal case: one ATA status descriptor, fully inside the buffer. */
    memset(arena, 0, sizeof(arena));
    arena[ATA_SENSE_LEN_OFFSET] = ATA_DP_HDR_LEN + ATA_STATUS_DP_TOTAL;
    arena[ATA_SENSE_DP_OFFSET] = ATA_STATUS_DP_CODE;
    arena[ATA_SENSE_DP_OFFSET + 1] = ATA_STATUS_DP_LEN;
    arena[ATA_SENSE_DP_OFFSET + ATA_DP_LBA_MID_OFFSET] = 0x11;
    arena[ATA_SENSE_DP_OFFSET + ATA_DP_LBA_HIGH_OFFSET] = 0x22;
    arena[ATA_SENSE_DP_OFFSET + ATA_DP_STATUS_OFFSET] = 0x33;
    status = ATA_SENSE_OUT_SENTINEL;
    lba_mid = ATA_SENSE_OUT_SENTINEL;
    lba_high = ATA_SENSE_OUT_SENTINEL;
    memset(err_msg, 0, sizeof(err_msg));
    rc = _extract_ata_sense_data(err_msg, arena, &status, &lba_mid, &lba_high);
    ck_assert_msg(rc == LSM_ERR_OK, "normal: rc %d (%s)", rc, err_msg);
    ck_assert_msg(status == 0x33, "normal: status 0x%02x", status);
    ck_assert_msg(lba_mid == 0x11, "normal: lba_mid 0x%02x", lba_mid);
    ck_assert_msg(lba_high == 0x22, "normal: lba_high 0x%02x", lba_high);

    /* Truncated descriptor header: ADDITIONAL SENSE LENGTH claims only 1
     * byte follows, not enough for even the 2-byte descriptor header.
     */
    memset(arena, 0, sizeof(arena));
    arena[ATA_SENSE_LEN_OFFSET] = 1;
    arena[ATA_SENSE_DP_OFFSET] = ATA_NON_ATA_DP_CODE;
    status = ATA_SENSE_OUT_SENTINEL;
    memset(err_msg, 0, sizeof(err_msg));
    rc = _extract_ata_sense_data(err_msg, arena, &status, &lba_mid, &lba_high);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "truncated header: rc %d (%s)", rc, err_msg);
    ck_assert_msg(status == ATA_SENSE_OUT_SENTINEL,
                  "truncated header: status touched (0x%02x)", status);

    /* Zero-length descriptor: not truncated, but len == 0 is still illegal. */
    memset(arena, 0, sizeof(arena));
    arena[ATA_SENSE_LEN_OFFSET] = 10;
    arena[ATA_SENSE_DP_OFFSET] = ATA_NON_ATA_DP_CODE;
    arena[ATA_SENSE_DP_OFFSET + 1] = 0;
    status = ATA_SENSE_OUT_SENTINEL;
    memset(err_msg, 0, sizeof(err_msg));
    rc = _extract_ata_sense_data(err_msg, arena, &status, &lba_mid, &lba_high);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "zero-length descriptor: rc %d (%s)", rc, err_msg);
    ck_assert_msg(status == ATA_SENSE_OUT_SENTINEL,
                  "zero-length descriptor: status touched (0x%02x)", status);

    /* CVE-shaped regression for abd55816: ADDITIONAL SENSE LENGTH (250)
     * claims the descriptor list runs to offset 258, 6 bytes past the
     * 252-byte buffer. A leading non-ATA descriptor (offsets 8..239) is
     * sized so the next descriptor starts at offset 240; an ATA status
     * descriptor planted there would end at offset 254, 2 bytes into the
     * guard. The claimed length makes that look like enough room; the
     * actual buffer does not have it. Expect a refusal, not a read into
     * the guard.
     */
    memset(arena, 0, _T10_SPC_SENSE_DATA_MAX_LENGTH);
    memset(arena + _T10_SPC_SENSE_DATA_MAX_LENGTH, ATA_SENSE_GUARD_BYTE,
           SENSE_GUARD_LEN);
    arena[ATA_SENSE_LEN_OFFSET] = 250;
    arena[ATA_SENSE_DP_OFFSET] = ATA_NON_ATA_DP_CODE;
    arena[ATA_SENSE_DP_OFFSET + 1] =
        240 - ATA_SENSE_DP_OFFSET - ATA_DP_HDR_LEN; /* lands next dp at 240 */
    arena[240] = ATA_STATUS_DP_CODE;
    arena[241] = ATA_STATUS_DP_LEN;
    arena[240 + ATA_DP_LBA_MID_OFFSET] = 0x11;
    arena[240 + ATA_DP_LBA_HIGH_OFFSET] = 0x22;
    /* offset 240 + ATA_DP_STATUS_OFFSET (253) is inside the guard */
    status = ATA_SENSE_OUT_SENTINEL;
    lba_mid = ATA_SENSE_OUT_SENTINEL;
    lba_high = ATA_SENSE_OUT_SENTINEL;
    memset(err_msg, 0, sizeof(err_msg));
    rc = _extract_ata_sense_data(err_msg, arena, &status, &lba_mid, &lba_high);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "over-length descriptor list: rc %d, status 0x%02x (%s)", rc,
                  status, err_msg);
    ck_assert_msg(status != ATA_SENSE_GUARD_BYTE,
                  "over-length descriptor list: status read from the guard "
                  "(0x%02x)",
                  status);
}
END_TEST

/* 515e355c: _sg_request_sense() must return what the device sent back for
 * the REQUEST SENSE command (the SG_IO dxferp buffer), not the SG_IO
 * transport-level sense buffer (sbp), which is all zero on a plain success.
 */
START_TEST(test_request_sense_returns_device_data) {
    static uint8_t returned_sense_data[_T10_SPC_SENSE_DATA_MAX_LENGTH];
    char err_msg[_LSM_ERR_MSG_LEN];
    int rc = 0;
    size_t i = 0;

    fake_ioctl_reset();
    for (i = 0; i < _T10_SPC_REQUEST_SENSE_MAX_LEN; i++)
        fake_ioctl_dxfer[i] = (uint8_t)(i + 1); /* never 0x00 */
    fake_ioctl_dxfer_len = _T10_SPC_REQUEST_SENSE_MAX_LEN;

    memset(err_msg, 0, sizeof(err_msg));
    memset(returned_sense_data, 0, sizeof(returned_sense_data));
    rc = _sg_request_sense(err_msg, 0, returned_sense_data);

    ck_assert_msg(rc == LSM_ERR_OK, "rc %d (%s)", rc, err_msg);
    for (i = 0; i < _T10_SPC_SENSE_DATA_MAX_LENGTH; i++)
        ck_assert_msg(returned_sense_data[i] == (uint8_t)(i + 1),
                      "byte %zu is 0x%02x, expected 0x%02x (came from the "
                      "transport sense buffer, not the REQUEST SENSE data)",
                      i, returned_sense_data[i], (uint8_t)(i + 1));
}
END_TEST

/* 75cde6d3: _sg_io_vpd() must zero the whole caller buffer up front, not
 * just the 'data_len' bytes it asks the device for. The VPD 0x80 parser
 * trusts the device-reported PAGE LENGTH to decide how much of the buffer
 * is real, so anything past data_len that is not zeroed reads back as
 * whatever the caller's stack held before the call.
 */
START_TEST(test_io_vpd_zeroes_whole_buffer) {
    static uint8_t vpd_data[_SG_T10_SPC_VPD_MAX_LEN];
    char err_msg[_LSM_ERR_MSG_LEN];
    /* VPD 0x80 header (SPC-5 Table 584): byte 0 qualifier/type, byte 1 page
     * code, bytes 2-3 PAGE LENGTH. The device only has 252 bytes allocated
     * to send (_SG_T10_SPC_VPD_SMALL_MAX_LEN), but reports a PAGE LENGTH far
     * past that, as a device with a long serial number legitimately would.
     */
    const uint16_t claimed_page_len = 1000;
    const size_t residue_offset =
        300; /* past data_len, inside claimed_page_len */
    int rc = 0;

    ck_assert_uint_lt(residue_offset, (size_t)claimed_page_len + 4);

    memset(vpd_data, 0xab, sizeof(vpd_data)); /* simulate stack residue */

    fake_ioctl_reset();
    fake_ioctl_dxfer[1] = _SG_T10_SPC_VPD_UNIT_SN;
    fake_ioctl_dxfer[2] = (claimed_page_len >> 8) & UINT8_MAX;
    fake_ioctl_dxfer[3] = claimed_page_len & UINT8_MAX;
    fake_ioctl_dxfer_len = _SG_T10_SPC_VPD_SMALL_MAX_LEN;

    memset(err_msg, 0, sizeof(err_msg));
    rc = _sg_io_vpd(err_msg, 0, _SG_T10_SPC_VPD_UNIT_SN, vpd_data);

    ck_assert_msg(rc == LSM_ERR_OK, "rc %d (%s)", rc, err_msg);
    ck_assert_msg(vpd_data[residue_offset] == 0,
                  "byte %zu is 0x%02x, not zeroed: parser will read stack "
                  "residue instead of empty data",
                  residue_offset, vpd_data[residue_offset]);
}
END_TEST

/* 75cde6d3 (LOG SENSE half): _sg_log_sense() must zero the whole caller
 * buffer, not just copy the log parameters the device returned. When MRIE
 * is not "report on request", _sg_sas_health_status() reads the ASC at a
 * fixed offset of the Informational Exceptions log page, so a short log
 * page must parse as "no additional sense" (healthy) rather than as
 * whatever the caller's stack held before the call.
 */
#define LOG_SENSE_PAGE_LEN_OFFSET 2 /* LOG DATA LENGTH, SPC-5 Table 208 hdr */
#define LOG_SENSE_DATA_OFFSET     4
#define LOG_PARA_HDR_LEN          4 /* struct _sg_t10_log_para_hdr */
/* MRIE (SPC-5 Table 89) of 0x2 selects the LOG SENSE path in
 * _sg_sas_health_status().
 */
#define MRIE_REPORT_INFO_EXCEP_PERIODICALLY 0x2
/* Byte holding the MRIE field inside an Informational Exceptions control
 * mode page (SPC-5 Table 90): byte 3 of the page, low nibble.
 */
#define IE_MODE_PAGE_MRIE_OFFSET 3

/* Dirties the stack well below the current frame with _T10_SPC_ASC_
 * IMPENDING_FAILURE bytes, so a read of never-written stack by a callee is
 * a deterministic failure verdict instead of a coin flip.
 */
static volatile uint32_t dirty_stack_sink;

static void dirty_stack_with_impending_failure(unsigned int depth) {
    uint8_t dirt[64 * 1024];

    memset(dirt, _T10_SPC_ASC_IMPENDING_FAILURE, sizeof(dirt));
    dirty_stack_sink += dirt[depth % sizeof(dirt)];
    if (depth > 0)
        dirty_stack_with_impending_failure(depth - 1);
}

START_TEST(test_sas_health_status_short_log_page) {
    char err_msg[_LSM_ERR_MSG_LEN];
    int32_t health_status = 0;
    int rc = 0;

    dirty_stack_with_impending_failure(8);

    fake_ioctl_reset();
    /* MODE SENSE(10) reply for the Informational Exceptions control mode
     * page: MODE DATA LENGTH 10, no block descriptor, and the page itself
     * starting at byte 8 (what _sg_io_mode_sense() copies out).
     */
    fake_ioctl_dxfer[0] = 0;
    fake_ioctl_dxfer[1] = 10;
    fake_ioctl_dxfer[6] = 0;
    fake_ioctl_dxfer[7] = 0;
    fake_ioctl_dxfer[8 + IE_MODE_PAGE_MRIE_OFFSET] =
        MRIE_REPORT_INFO_EXCEP_PERIODICALLY;
    fake_ioctl_dxfer_len = 8 + 4;

    /* LOG SENSE reply with LOG DATA LENGTH 4: _sg_log_sense() copies only
     * those 4 bytes, so the ASC at offset LOG_PARA_HDR_LEN of the caller's
     * buffer is beyond what the device answered and must read back as
     * zeroed, not as stack residue.
     */
    fake_ioctl_log_dxfer[LOG_SENSE_PAGE_LEN_OFFSET] = 0;
    fake_ioctl_log_dxfer[LOG_SENSE_PAGE_LEN_OFFSET + 1] = LOG_PARA_HDR_LEN;
    fake_ioctl_log_dxfer_len = LOG_SENSE_DATA_OFFSET + LOG_PARA_HDR_LEN;

    memset(err_msg, 0, sizeof(err_msg));
    rc = _sg_sas_health_status(err_msg, 0, &health_status);

    ck_assert_msg(rc == LSM_ERR_OK, "rc %d (%s)", rc, err_msg);
    ck_assert_msg(health_status == LSM_DISK_HEALTH_STATUS_GOOD,
                  "health status %d, expected GOOD: the ASC past the log "
                  "parameters was stack residue, not zeros",
                  health_status);
}
END_TEST

/* 31b55484 + 3836b5bb: _sg_io_mode_sense() must reject a MODE DATA LENGTH /
 * BLOCK DESCRIPTOR LENGTH combination from the device before using them to
 * size the final memcpy(), rather than letting a bad combination underflow
 * the copy size (31b55484) or walk one byte past the buffer handed to the
 * device (3836b5bb's off-by-one in the MODE DATA LENGTH range check).
 */
struct mode_sense_case {
    const char *name;
    uint16_t mode_data_len;
    uint16_t block_dp_len;
    int expect_rc;
};

static const struct mode_sense_case mode_sense_cases[] = {
    {"normal, no block descriptor", 10, 0, LSM_ERR_OK},
    /* 31b55484: MODE DATA LENGTH (8) is too small to cover even the fixed
     * header remainder (6) plus the claimed BLOCK DESCRIPTOR LENGTH (4):
     * the memcpy size computation 2 + mode_data_len - 8 - block_dp_len
     * underflows to a huge size_t without the fix.
     */
    {"MODE DATA LENGTH too small for BLOCK DESCRIPTOR LENGTH", 8, 4,
     LSM_ERR_LIB_BUG},
    /* 3836b5bb: MODE DATA LENGTH one below the buffer size. The old check
     * (mode_data_len >= MAX) let this through; the valid range is really
     * mode_data_len <= MAX - sizeof(mode_data_len_be), i.e. MAX - 2.
     */
    {"MODE DATA LENGTH one past the off-by-one boundary",
     _SG_T10_SPC_MODE_SENSE_MAX_LEN - 1, 0, LSM_ERR_LIB_BUG},
};

START_TEST(test_io_mode_sense_length_checks) {
    static uint8_t data[_SG_T10_SPC_MODE_SENSE_MAX_LEN];
    char err_msg[_LSM_ERR_MSG_LEN];
    const struct mode_sense_case *c = NULL;
    const uint8_t normal_payload[] = {0x11, 0x22, 0x33, 0x44};
    int rc = 0;
    size_t i = 0;

    for (i = 0; i < sizeof(mode_sense_cases) / sizeof(mode_sense_cases[0]);
         i++) {
        c = &mode_sense_cases[i];

        fake_ioctl_reset();
        /* struct _sg_t10_mode_para_hdr: mode_data_len_be, 4 reserved bytes,
         * block_dp_header_len_be.
         */
        fake_ioctl_dxfer[0] = (c->mode_data_len >> 8) & UINT8_MAX;
        fake_ioctl_dxfer[1] = c->mode_data_len & UINT8_MAX;
        fake_ioctl_dxfer[6] = (c->block_dp_len >> 8) & UINT8_MAX;
        fake_ioctl_dxfer[7] = c->block_dp_len & UINT8_MAX;
        fake_ioctl_dxfer_len = 8;
        if (c->expect_rc == LSM_ERR_OK) {
            memcpy(fake_ioctl_dxfer + 8, normal_payload,
                   sizeof(normal_payload));
            fake_ioctl_dxfer_len += sizeof(normal_payload);
        }

        memset(err_msg, 0, sizeof(err_msg));
        memset(data, 0, sizeof(data));
        rc = _sg_io_mode_sense(err_msg, 0, 0, 0, data);

        ck_assert_msg(rc == c->expect_rc, "%s: rc %d, expected %d (%s)",
                      c->name, rc, c->expect_rc, err_msg);
        if (c->expect_rc == LSM_ERR_OK)
            ck_assert_msg(
                memcmp(data, normal_payload, sizeof(normal_payload)) == 0,
                "%s: mode page data mismatch", c->name);
    }
}
END_TEST

/* 7f6f9bfd: _sysfs_host_speed_get() must not touch buff[file_size - 1] when
 * the sysfs file is empty (file_size == 0), which underflows to buff[-1].
 * An empty port_speed/speed file is a real (if degenerate) sysfs state, not
 * just a hostile input.
 */
START_TEST(test_sysfs_host_speed_get_empty_file) {
    char path[] = "/tmp/test_libsg_host_speed_XXXXXX";
    char err_msg[_LSM_ERR_MSG_LEN];
    uint32_t link_speed = 0xdeadbeef;
    int fd = -1;
    int rc = 0;

    fd = mkstemp(path);
    ck_assert_msg(fd >= 0, "mkstemp failed: %s", strerror(errno));
    close(fd);

    memset(err_msg, 0, sizeof(err_msg));
    rc = _sysfs_host_speed_get(err_msg, path, &link_speed);
    unlink(path);

    /* An empty file has no usable speed string; LSM_ERR_LIB_BUG (not a
     * crash) is the correct outcome.
     */
    ck_assert_msg(rc == LSM_ERR_LIB_BUG, "rc %d (%s)", rc, err_msg);
    ck_assert_msg(link_speed == LSM_DISK_LINK_SPEED_UNKNOWN,
                  "link_speed 0x%" PRIx32 " on error", link_speed);
}
END_TEST

/* 2ca209e5: _trim_spaces() must compute the end-of-string pointer before
 * skipping leading spaces, not after. Using the post-skip 'beginning' with
 * the pre-skip length overshoots the real end of the (now shorter) string
 * by exactly the number of leading spaces skipped, and the trailing-space
 * trim loop then reads and writes past the NUL terminator.
 */
START_TEST(test_trim_spaces_no_overread) {
    /* "   hi" (3 leading spaces, no trailing spaces) NUL-terminated, then
     * 2 guard bytes set to ' ' (what the overshot end pointer lands on)
     * and a final sentinel byte that must never be touched either way.
     */
    char buf[9] = {' ', ' ', ' ', 'h', 'i', '\0', ' ', ' ', 'Z'};
    char *result = NULL;

    result = _trim_spaces(buf);

    ck_assert_msg(result != NULL, "unexpected NULL");
    ck_assert_msg(strcmp(result, "hi") == 0, "got '%s', expected 'hi'", result);
    ck_assert_msg(buf[6] == ' ',
                  "guard byte 6 is 0x%02x, _trim_spaces wrote past the "
                  "string's end",
                  buf[6]);
    ck_assert_msg(buf[7] == ' ',
                  "guard byte 7 is 0x%02x, _trim_spaces wrote past the "
                  "string's end",
                  buf[7]);
    ck_assert_msg(buf[8] == 'Z', "sentinel byte touched (0x%02x)", buf[8]);
}
END_TEST

START_TEST(test_trim_spaces_all_spaces) {
    char buf[4] = "   ";

    ck_assert_msg(_trim_spaces(buf) == NULL,
                  "all-space string should trim to NULL");
}
END_TEST

static Suite *libsg_suite(void) {
    Suite *s = suite_create("libsg");
    TCase *tc = tcase_create("vpd80");

    tcase_add_test(tc, test_parse_vpd_80);
    suite_add_tcase(s, tc);

    tc = tcase_create("vpd83");
    tcase_add_test(tc, test_parse_vpd_83_bounds);
    suite_add_tcase(s, tc);

    tc = tcase_create("sense_data_bounds");
    tcase_add_test(tc, test_check_sense_data);
    tcase_add_test(tc, test_extract_ata_sense_data);
    suite_add_tcase(s, tc);

    tc = tcase_create("sg_io_fake_device");
    tcase_add_test(tc, test_request_sense_returns_device_data);
    tcase_add_test(tc, test_io_vpd_zeroes_whole_buffer);
    tcase_add_test(tc, test_sas_health_status_short_log_page);
    tcase_add_test(tc, test_io_mode_sense_length_checks);
    suite_add_tcase(s, tc);

    tc = tcase_create("utils");
    tcase_add_test(tc, test_sysfs_host_speed_get_empty_file);
    tcase_add_test(tc, test_trim_spaces_no_overread);
    tcase_add_test(tc, test_trim_spaces_all_spaces);
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
