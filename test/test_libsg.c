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

static Suite *libsg_suite(void) {
    Suite *s = suite_create("libsg");
    TCase *tc = tcase_create("vpd80");

    tcase_add_test(tc, test_parse_vpd_80);
    suite_add_tcase(s, tc);

    tc = tcase_create("vpd83");
    tcase_add_test(tc, test_parse_vpd_83_bounds);
    suite_add_tcase(s, tc);

    tc = tcase_create("sg_io_fake_device");
    tcase_add_test(tc, test_request_sense_returns_device_data);
    tcase_add_test(tc, test_io_vpd_zeroes_whole_buffer);
    tcase_add_test(tc, test_sas_health_status_short_log_page);
    tcase_add_test(tc, test_io_mode_sense_length_checks);
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
