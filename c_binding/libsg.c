/*
 * Copyright (C) 2016-2017 Red Hat, Inc.
 * (C) Copyright (C) 2017 Hewlett Packard Enterprise Development LP
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

/* For strerror_r() */
#define _GNU_SOURCE

#include "libsg.h"
#include "utils.h"

#include "libstoragemgmt/libstoragemgmt_error.h"

#include <scsi/sg.h>
#include <scsi/scsi.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <endian.h>
#include <scsi/scsi.h>  /* For SCSI_IOCTL_GET_BUS_NUMBER */
#include <limits.h>

/* SGIO timeout: 1 second
 * TODO(Gris Ge): Raise LSM_ERR_TIMEOUT error for this
 */
#define _SG_IO_TMO                                      1000

/* SPC-5 rev7 Table 142 - INQUIRY command */
#define _T10_SPC_INQUIRY_CMD_LEN                        6
/* SPC-5 rev7 Table 219 - RECEIVE DIAGNOSTIC RESULTS command */
#define _T10_SPC_RECV_DIAG_CMD_LEN                      6
/* SPC-5 rev7 Table 269 - SEND DIAGNOSTIC command */
#define _T10_SPC_SEND_DIAG_CMD_LEN                      6
/* SPC-5 rev7 Table 534 - Supported VPD Pages VPD page */
#define _T10_SPC_MODE_SENSE_CMD_LEN                     10
/* SPC-5 rev12 Table 171 - MODE SENSE(10) command */
#define _T10_SPC_LOG_SENSE_CMD_LEN                      10
/* SPC-5 rev07 - LOG SENSE command */
#define _T10_SPC_REQUEST_SENSE_CMD_LEN                  6
/* SPC-5 rev07 - REQUEST SENSE command */
#define _T10_SPC_VPD_SUP_VPD_PGS_LIST_OFFSET            4

/* SPC-5 rev7 4.4.2.1 Descriptor format sense data overview
 * Quote:
 * The ADDITIONAL SENSE LENGTH field indicates the number of additional sense
 * bytes that follow. The additional sense length shall be less than or equal to
 * 244 (i.e., limiting the total length of the sense data to 252 bytes).
 */
#define _T10_SPC_SENSE_DATA_MAX_LENGTH                       252

/* The max length of char[] required to hold hex dump of sense data */
#define _T10_SPC_SENSE_DATA_STR_MAX_LENGTH  \
    _T10_SPC_SENSE_DATA_MAX_LENGTH * 2 + 1

/* SPC-5 rev07 Table 300 - Summary of log page codes */
#define _T10_SPC_INFO_EXCEP_PAGE_CODE                        0x2f

/* SPC-5 rev07 Table 151 - Page control (PC) field */
#define PAGE_CONTROL_CUMULATIVE_VALS                         0x01

/* SPC-5 rev07 Table E.13 - Mode pages codes */
#define INFO_EXCEP_CONTROL_PAGE                              0x1c

/* SBC - Method of reporting informational exceptions (MRIE) field */
#define MRIE_REPORT_INFO_EXCEP_ON_REQUEST                    0x6

/* SPC-5 rev07 Table 49 - ASC and ASCQ assignments */
#define _T10_SPC_ASC_WARNING                                 0x0b
#define _T10_SPC_ASC_IMPENDING_FAILURE                       0x5d
#define _T10_SPC_ASCQ_ATA_PASSTHROUGH                        0x1d

/* SPC-5 rev07 Table 30 - DESCRIPTOR TYPE field */
#define _T10_ATA_STATUS_RETURN                               0x09

/*
 * SPC-5 rev7 Table 27 - Sense data response codes
 */
#define _T10_SPC_SENSE_REPORT_TYPE_CUR_INFO_FIXED       0x70
#define _T10_SPC_SENSE_REPORT_TYPE_DEF_ERR_FIXED        0x71
#define _T10_SPC_SENSE_REPORT_TYPE_CUR_INFO_DP          0x72
#define _T10_SPC_SENSE_REPORT_TYPE_DEF_ERR_DP           0x73

/*
 * SPC-5 rev7 Table 48 - Sense key descriptions
 */
#define _T10_SPC_SENSE_KEY_NO_SENSE                     0x0
#define _T10_SPC_SENSE_KEY_RECOVERED_ERROR              0x1
#define _T10_SPC_SENSE_KEY_ILLEGAL_REQUEST              0x5
#define _T10_SPC_SENSE_KEY_COMPLETED                    0xf

const char * const _T10_SPC_SENSE_KEY_STR[] = {
    "NO SENSE",
    "RECOVERED ERROR",
    "NOT READY",
    "MEDIUM ERROR",
    "HARDWARE ERROR",
    "ILLEGAL REQUEST",
    "UNIT ATTENTION",
    "DATA PROTECT",
    "BLANK CHECK",
    "VENDOR SPECIFIC",
    "COPY ABORTED",
    "ABORTED COMMAND",
    "RESERVED",
    "VOLUME OVERFLOW",
    "MISCOMPARE",
    "COMPLETED",
};

/* The offset of ADDITIONAL SENSE LENGTH */
#define _T10_SPC_SENSE_DATA_LEN_OFFSET                  8

#define _SG_IO_NO_DATA                                  0
#define _SG_IO_SEND_DATA                                1
#define _SG_IO_RECV_DATA                                2

#pragma pack(push, 1)
/*
 * SPC-5 rev 7 Table 589 - Device Identification VPD page
 */
struct _sg_t10_vpd83_header {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t dev_type : 5;
    uint8_t qualifier : 3;
#else
    uint8_t qualifier : 3;
    uint8_t dev_type : 5;
#endif
    uint8_t page_code;
    uint16_t page_len_be;
};

struct _sg_t10_vpd80_header {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t dev_type : 5;
    uint8_t qualifier : 3;
#else
    uint8_t qualifier : 3;
    uint8_t dev_type : 5;
#endif
    uint8_t page_code;
    uint16_t page_len_be;
};

struct _sg_t10_vpd00 {
    uint8_t ignore;
    uint8_t page_code;
    uint16_t page_len_be;
    uint8_t supported_vpd_list_begin;
};


struct _sg_t10_sense_header {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t response_code : 7;
    uint8_t we_dont_care_0 : 1;
#else
    uint8_t we_dont_care_0 : 1;
    uint8_t response_code : 7;
#endif
};

/*
 * SPC-5 rev 7 Table 47 - Fixed format sense data
 */
struct _sg_t10_sense_fixed {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t response_code : 7;
    uint8_t valid : 1;
#else
    uint8_t valid : 1;
    uint8_t response_code : 7;
#endif
    uint8_t obsolete;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t sense_key : 4;
    uint8_t we_dont_care_0 : 4;
#else
    uint8_t we_dont_care_0 : 4;
    uint8_t sense_key : 4;
#endif
    uint8_t we_dont_care_1[4];
    uint8_t len;
    uint8_t we_dont_care_2[4];
    uint8_t asc;        /* ADDITIONAL SENSE CODE */
    uint8_t ascq;       /* ADDITIONAL SENSE CODE QUALIFIER */
    /* We don't care the rest of data */
};

/*
 * SPC-5 rev 7 Table 47 - Fixed format sense data
 * with embedded ATA registers
 */
struct _sg_t10_sense_ata_fixed {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t response_code : 7;
    uint8_t valid : 1;
#else
    uint8_t valid : 1;
    uint8_t response_code : 7;
#endif
    uint8_t obsolete;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t sense_key : 4;
    uint8_t we_dont_care_0 : 4;
#else
    uint8_t we_dont_care_0 : 4;
    uint8_t sense_key : 4;
#endif
    uint8_t error;
    uint8_t status;
    uint8_t device;
    uint8_t count;
    uint8_t len;
    uint8_t we_dont_care_1;
    uint8_t lba_low;
    uint8_t lba_mid;
    uint8_t lba_high;
    uint8_t asc;        /* ADDITIONAL SENSE CODE */
    uint8_t ascq;       /* ADDITIONAL SENSE CODE QUALIFIER */
};

/*
 * SPC-5 rev 7 Table 28 - Descriptor format sense data
 */
struct _sg_t10_sense_dp {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t response_code : 7;
    uint8_t reserved : 1;
    uint8_t sense_key : 4;
    uint8_t we_dont_care_0 : 4;
#else
    uint8_t reserved : 1;
    uint8_t response_code : 7;
    uint8_t we_dont_care_0 : 4;
    uint8_t sense_key : 4;
#endif
    uint8_t asc;        /* ADDITIONAL SENSE CODE */
    uint8_t ascq;       /* ADDITIONAL SENSE CODE QUALIFIER */
    uint8_t we_dont_care_1[3];
    uint8_t len;
    /* We don't care the rest of data */
};

struct _sg_t10_ata_status_return_dp_hdr {
    uint8_t descriptor_code;
    uint8_t additional_desc_len;
};

struct _sg_t10_ata_status_return_dp {
    uint8_t descriptor_code;
    uint8_t additional_desc_len;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t rsvd : 7;
    uint8_t extend : 1;
#else
    uint8_t extend : 1;
    uint8_t rsvd : 7;
#endif
    uint8_t error;
    uint8_t reserved_count;
    uint8_t count;
    uint8_t reserved_lba_low;
    uint8_t lba_low;
    uint8_t reserved_lba_mid;
    uint8_t lba_mid;
    uint8_t reserved_lba_high;
    uint8_t lba_high;
    uint8_t device;
    uint8_t status;
};

struct _sg_t10_mode_para_hdr {
    uint16_t mode_data_len_be;
    uint8_t we_dont_care_0[4];
    uint16_t block_dp_header_len_be;
};

struct _sg_t10_log_para_hdr {
    uint8_t we_dont_care_0[2];
    uint16_t log_data_len_be;
};

struct _sg_t10_info_excep_mode_page_0_hdr {
    uint8_t dont_care[3];
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t reserved : 4;
    uint8_t mrie : 4;
#else
    uint8_t mrie : 4;
    uint8_t reserved : 4;
#endif
};

struct _sg_t10_info_excep_general_log_hdr {
    uint8_t dont_care[4];
    uint8_t asc;
    uint8_t ascq;
};

#pragma pack(pop)

/*
 * Return 0 if pass, return -1 means got sense_data, return errno of ioctl
 * error if ioctl failed.
 * The 'sense_data' should be uint8_t[_T10_SPC_SENSE_DATA_MAX_LENGTH].
 */
static int _sg_io(int fd, uint8_t *cdb, uint8_t cdb_len, uint8_t *data,
                  ssize_t data_len, uint8_t *sense_data, int direction);

static struct _sg_t10_vpd83_dp *_sg_t10_vpd83_dp_new(void);

static int _sg_io_open(char *err_msg, const char *disk_path, int *fd,
                       int oflag);

static int _fill_ata_regs_desc(uint8_t additional_sense_len,
                               uint8_t *sense_data_desc_list_index,
                               uint8_t *ata_output_regs);

static int _fill_ata_regs_fixed(uint8_t *ata_output_regs, uint8_t *sense_data);

/*
 * The 'sense_key' is the output pointer.
 * Return 0 if sense_key is _T10_SPC_SENSE_KEY_NO_SENSE or
 * _T10_SPC_SENSE_KEY_RECOVERED_ERROR, return -1 otherwise.
 */
static int _check_sense_data(char *err_msg, uint8_t *sense_data,
                                 uint8_t *sense_key);

static int _sg_io(int fd, uint8_t *cdb, uint8_t cdb_len, uint8_t *data,
                  ssize_t data_len, uint8_t *sense_data, int direction)
{
    int rc = 0;
    struct sg_io_hdr io_hdr;

    assert(cdb != NULL);
    assert(cdb_len != 0);

    memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
    memset(sense_data, 0, _T10_SPC_SENSE_DATA_MAX_LENGTH);
    if (direction == _SG_IO_RECV_DATA)
        memset(data, 0, (size_t) data_len);
    io_hdr.interface_id = 'S';  /* 'S' for SCSI generic */
    io_hdr.cmdp = cdb;
    io_hdr.cmd_len = cdb_len;
    io_hdr.sbp = sense_data;
    io_hdr.mx_sb_len = _T10_SPC_SENSE_DATA_MAX_LENGTH;
    if (direction == _SG_IO_RECV_DATA)
        io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    else if (direction == _SG_IO_SEND_DATA)
        io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
    else if (direction == _SG_IO_NO_DATA)
        io_hdr.dxfer_direction = SG_DXFER_NONE;

    if (data != NULL)
        io_hdr.dxferp = (unsigned char *) data;
    io_hdr.dxfer_len = data_len;
    io_hdr.timeout = _SG_IO_TMO;

    if (ioctl(fd, SG_IO, &io_hdr) != 0)
        rc = errno;

    if (io_hdr.sb_len_wr != 0)
        /* It might possible we got "NO SENSE", so we does not zero the data */
        return -1;

    if ((rc != 0) && (data != NULL))
        memset(data, 0, (size_t) data_len);

    return rc;
}

int _sg_io_vpd(char *err_msg, int fd, uint8_t page_code, uint8_t *data)
{
    int rc = LSM_ERR_OK;
    uint8_t vpd_00_data[_SG_T10_SPC_VPD_MAX_LEN];
    uint8_t cdb[_T10_SPC_INQUIRY_CMD_LEN];
    uint8_t sense_data[_T10_SPC_SENSE_DATA_MAX_LENGTH];
    int ioctl_errno = 0;
    int rc_vpd_00 = 0;
    char strerr_buff[_LSM_ERR_MSG_LEN];
    uint8_t sense_key = _T10_SPC_SENSE_KEY_NO_SENSE;
    char sense_err_msg[_LSM_ERR_MSG_LEN];

    assert(err_msg != NULL);
    assert(fd >= 0);
    assert(data != NULL);

    memset(sense_err_msg, 0, _LSM_ERR_MSG_LEN);

    /* SPC-5 Table 142 - INQUIRY command */
    cdb[0] = INQUIRY;                           /* OPERATION CODE */
    cdb[1] = 1;                                 /* EVPD */
    /* VPD INQUIRY requires EVPD == 1 */;
    cdb[2] = page_code & UINT8_MAX;             /* PAGE CODE */
    cdb[3] = (_SG_T10_SPC_VPD_MAX_LEN >> 8 )& UINT8_MAX;
                                                /* ALLOCATION LENGTH, MSB */
    cdb[4] = _SG_T10_SPC_VPD_MAX_LEN & UINT8_MAX;
                                                /* ALLOCATION LENGTH, LSB */
    cdb[5] = 0;                                 /* CONTROL */
    /* We have no use case need for handling auto contingent allegiance(ACA)
     * yet.
     */

    ioctl_errno = _sg_io(fd, cdb, _T10_SPC_INQUIRY_CMD_LEN, data,
                         _SG_T10_SPC_VPD_MAX_LEN, sense_data, _SG_IO_RECV_DATA);

    if (ioctl_errno != 0) {
        if (page_code == _SG_T10_SPC_VPD_SUP_VPD_PGS) {
            _lsm_err_msg_set(err_msg, "Not a SCSI compatible device");
            rc = LSM_ERR_NO_SUPPORT;
            goto out;
        }
        if (_check_sense_data(sense_err_msg, sense_data, &sense_key) != 0) {
            if (sense_key == _T10_SPC_SENSE_KEY_ILLEGAL_REQUEST) {
                /* Check whether provided page is supported */
                rc_vpd_00 = _sg_io_vpd(err_msg, fd, _SG_T10_SPC_VPD_SUP_VPD_PGS,
                                       vpd_00_data);
                if (rc_vpd_00 != 0) {
                    rc = LSM_ERR_NO_SUPPORT;
                    goto out;
                }
                if (_sg_is_vpd_page_supported(vpd_00_data, page_code) == true) {
                    /* Current VPD page is supported, then it's a library bug */
                    rc = LSM_ERR_LIB_BUG;
                    _lsm_err_msg_set(err_msg,
                                     "BUG: VPD page 0x%02x is supported, "
                                     "but failed with error %d(%s), %s",
                                     page_code, ioctl_errno,
                                     strerror_r(ioctl_errno, strerr_buff,
                                                _LSM_ERR_MSG_LEN),
                                                sense_err_msg);
                    goto out;
                } else {
                    rc = LSM_ERR_NO_SUPPORT;
                    _lsm_err_msg_set(err_msg, "SCSI VPD 0x%02x page is "
                                     "not supported", page_code);
                    goto out;
                }
            } else {
                rc = LSM_ERR_LIB_BUG;
                _lsm_err_msg_set(err_msg,
                                 "BUG: Unexpected failure of _sg_io_vpd(): %s",
                                 sense_err_msg);
                goto out;
            }
        }
        /* NVMe disk does not support SCSI VPD page 0x00 or SCSI sense data
         * which fallback to here */
        if (ioctl_errno == ENOTTY) {
            _lsm_err_msg_set(err_msg, "SCSI VPD page 0x%02x is not supported",
                             page_code);
            rc = LSM_ERR_NO_SUPPORT;
            goto out;
        }
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "BUG: Unexpected failure of _sg_io_vpd(): "
                         "error %d(%s), with no error in SCSI sense data",
                         ioctl_errno,
                         strerror_r(ioctl_errno, strerr_buff, _LSM_ERR_MSG_LEN)
                         );
    }

 out:

    return rc;
}

bool _sg_is_vpd_page_supported(uint8_t *vpd_0_data, uint8_t page_code)
{
    uint16_t supported_list_len = 0;
    uint16_t i = 0;
    struct _sg_t10_vpd00 *vpd00 = NULL;

    assert(vpd_0_data != NULL);

    vpd00 = (struct _sg_t10_vpd00 *) vpd_0_data;

    supported_list_len = be16toh(vpd00->page_len_be);

    for (; i < supported_list_len; ++i) {
        if (i + _T10_SPC_VPD_SUP_VPD_PGS_LIST_OFFSET >= _SG_T10_SPC_VPD_MAX_LEN)
            break;
        if (page_code == vpd_0_data[i + _T10_SPC_VPD_SUP_VPD_PGS_LIST_OFFSET])
            return true;
    }
    return false;
}

int _sg_parse_vpd_80(char *err_msg, uint8_t *vpd_data, uint8_t *serial_num,
                     uint16_t serial_num_max_len)
{
    int rc = LSM_ERR_OK;
    int len = 0;
    struct _sg_t10_vpd80_header *vpd80_header = NULL;
    uint8_t *p = NULL;
    uint8_t *end_p = NULL;
    uint16_t vpd80_len = 0;
    uint16_t serial_num_len = 0;

    assert(err_msg != NULL);
    assert(vpd_data != NULL);
    assert(serial_num != NULL);
    assert(serial_num_max_len != 0);

    memset(serial_num, 0, serial_num_max_len);

    vpd80_header = (struct _sg_t10_vpd80_header*) vpd_data;

    if (vpd80_header->page_code != _SG_T10_SPC_VPD_UNIT_SN) {
        /* Some DELL virtual floppy scsi disk return STANDARD INQUIRY data
         * on any VPD query with no sense error. Since SCSI SPC-4 or later
         * does not clarify this action, we treat it as no support
         */
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg, "Malformed SCSI data: VPD page code "
                         "'0x%02x', should be 0x80", vpd80_header->page_code);
        goto out;
    }

    serial_num_len = be16toh(vpd80_header->page_len_be);
    vpd80_len = serial_num_len + sizeof(struct _sg_t10_vpd83_header);

    end_p = vpd_data + vpd80_len - 1;

    if (end_p >= vpd_data + _SG_T10_SPC_VPD_MAX_LEN) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "BUG: Got invalid VPD UNIT SN page response, "
                         "data length exceeded the maximum size of a legal VPD "
                         "page");
        goto out;
    }

    p = vpd_data + sizeof(struct _sg_t10_vpd83_header);

    //add extra character to allow for terminating NULL
    serial_num_len += 1;
    len = snprintf((char *) serial_num, serial_num_len, "%s", (char *) p);

    if ((uint16_t) len >= serial_num_len) {
        memset(serial_num, 0, serial_num_max_len);
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "BUG: VPD UNIT SN was truncated when copied.");
    }

 out:
    return rc;
}

int _sg_parse_vpd_83(char *err_msg, uint8_t *vpd_data,
                     struct _sg_t10_vpd83_dp ***dps, uint16_t *dp_count)
{
    int rc = LSM_ERR_OK;
    struct _sg_t10_vpd83_header *vpd83_header = NULL;
    uint8_t *p = NULL;
    uint8_t *end_p = NULL;
    struct _sg_t10_vpd83_dp_header * dp_header = NULL;
    struct _sg_t10_vpd83_dp *dp = NULL;
    uint16_t i = 0;
    uint16_t vpd83_len = 0;

    assert(err_msg != NULL);
    assert(vpd_data != NULL);
    assert(dps != NULL);
    assert(dp_count != NULL);

    *dps = NULL;
    *dp_count = 0;

    vpd83_header = (struct _sg_t10_vpd83_header*) vpd_data;

    if (vpd83_header->page_code != _SG_T10_SPC_VPD_DI) {
        /* Some DELL virtual floppy scsi disk return STANDARD INQUIRY data
         * on any VPD query with no sense error. Since SCSI SPC-4 or later
         * does not clarify this action, we treat it as no support
         */
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg, "Malformed SCSI data: VPD page code "
                         "'0x%02x', should be 0x83", vpd83_header->page_code);
        goto out;
    }

    vpd83_len = be16toh(vpd83_header->page_len_be) +
        sizeof(struct _sg_t10_vpd83_header);

    end_p = vpd_data + vpd83_len - 1;
    if (end_p >= vpd_data + _SG_T10_SPC_VPD_MAX_LEN) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "BUG: Got invalid VPD DI page response, "
                         "data length exceeded the maximum size of a legal VPD "
                         "data");
        goto out;
    }
    p = vpd_data + sizeof(struct _sg_t10_vpd83_header);

    /* First loop finds out how many IDs we have */
    while(p <= end_p) {
        if (p + sizeof(struct _sg_t10_vpd83_dp_header) > end_p) {
            rc = LSM_ERR_LIB_BUG;
            _lsm_err_msg_set(err_msg, "BUG: Illegal VPD 0x83 page data, "
                             "got partial designation descriptor.");
            goto out;
        }
        ++i;

        dp_header = (struct _sg_t10_vpd83_dp_header *) p;

        p += dp_header->designator_len + sizeof(struct _sg_t10_vpd83_dp_header);
        continue;
    }

    if (i == 0)
        goto out;

    *dps = (struct _sg_t10_vpd83_dp **)
        malloc(sizeof(struct _sg_t10_vpd83_dp*) * i);

    if (*dps == NULL) {
        rc = LSM_ERR_NO_MEMORY;
        goto out;
    }

    p = vpd_data + sizeof(struct _sg_t10_vpd83_header);

    while(*dp_count < i) {
        dp = _sg_t10_vpd83_dp_new();
        if (dp == NULL) {
            rc = LSM_ERR_NO_MEMORY;
            goto out;
        }
        (*dps)[*dp_count] = dp;
        ++*dp_count;

        dp_header = (struct _sg_t10_vpd83_dp_header *) p;
        memcpy(&dp->header, dp_header, sizeof(struct _sg_t10_vpd83_dp_header));
        memcpy(dp->designator, p + sizeof(struct _sg_t10_vpd83_dp_header),
               dp_header->designator_len);

        p += dp_header->designator_len + sizeof(struct _sg_t10_vpd83_dp_header);
        continue;
    }

 out:
    if (rc != LSM_ERR_OK) {
        if (*dps != NULL) {
            _sg_t10_vpd83_dp_array_free(*dps, *dp_count);
            *dps = NULL;
            *dp_count = 0;
        }
    }
    return rc;
}

static struct _sg_t10_vpd83_dp *_sg_t10_vpd83_dp_new(void)
{
    struct _sg_t10_vpd83_dp *dp = NULL;

    dp = (struct _sg_t10_vpd83_dp *) malloc(sizeof(struct _sg_t10_vpd83_dp));

    if (dp != NULL) {
        memset(dp, 0, sizeof(struct _sg_t10_vpd83_dp));
    }
    return dp;
}

static int _sg_io_open(char *err_msg, const char *disk_path, int *fd, int oflag)
{
    int rc = LSM_ERR_OK;
    char strerr_buff[_LSM_ERR_MSG_LEN];

    assert(err_msg != NULL);
    assert(disk_path != NULL);
    assert(fd != NULL);

    *fd = open(disk_path, oflag);
    if (*fd < 0) {
        switch(errno) {
        case ENOENT:
            rc = LSM_ERR_NOT_FOUND_DISK;
            _lsm_err_msg_set(err_msg, "Disk %s not found", disk_path);
            goto out;
        case EACCES:
            rc = LSM_ERR_PERMISSION_DENIED;
            _lsm_err_msg_set(err_msg, "Permission denied: Cannot open %s "
                             "with %d flag", disk_path, oflag);
            goto out;
        default:
            rc = LSM_ERR_LIB_BUG;
            _lsm_err_msg_set(err_msg, "BUG: Failed to open %s, error: %d, %s",
                             disk_path, errno,
                             strerror_r(errno, strerr_buff, _LSM_ERR_MSG_LEN));
        }
    }
 out:
    return rc;
}

static int _check_sense_data(char *err_msg, uint8_t *sense_data,
                             uint8_t *sense_key)
{
    int rc = -1;
    struct _sg_t10_sense_header *sense_hdr = NULL;
    struct _sg_t10_sense_fixed *sense_fixed = NULL;
    struct _sg_t10_sense_dp *sense_dp = NULL;
    uint8_t len = 0;
    char sense_data_str[_T10_SPC_SENSE_DATA_STR_MAX_LENGTH];
    uint8_t i = 0;
    uint8_t asc = 0;
    uint8_t ascq = 0;

    assert(sense_data != NULL);
    assert(sense_key != NULL);

    memset(sense_data_str, 0, _T10_SPC_SENSE_DATA_STR_MAX_LENGTH);

    *sense_key = _T10_SPC_SENSE_KEY_NO_SENSE;

    sense_hdr = (struct _sg_t10_sense_header*) sense_data;

    switch(sense_hdr->response_code) {
    case _T10_SPC_SENSE_REPORT_TYPE_CUR_INFO_FIXED:
    case _T10_SPC_SENSE_REPORT_TYPE_DEF_ERR_FIXED:
        sense_fixed = (struct _sg_t10_sense_fixed *) sense_data;
        *sense_key = sense_fixed->sense_key;
        len = sense_fixed->len + _T10_SPC_SENSE_DATA_LEN_OFFSET;
        asc = sense_fixed->asc;
        ascq = sense_fixed->ascq;
        break;
    case _T10_SPC_SENSE_REPORT_TYPE_CUR_INFO_DP:
    case _T10_SPC_SENSE_REPORT_TYPE_DEF_ERR_DP:
        sense_dp = (struct _sg_t10_sense_dp *) sense_data;
        *sense_key = sense_dp->sense_key;
        len = sense_dp->len + _T10_SPC_SENSE_DATA_LEN_OFFSET;
        asc = sense_dp->asc;
        ascq = sense_dp->ascq;
        break;
    case 0:
        /* In case we got all zero sense data */
        rc = 0;
        goto out;
    default:
        _lsm_err_msg_set(err_msg, "Got unknown sense data response code %02x",
                         sense_hdr->response_code);
        goto out;
    }
    /* TODO(Gris Ge): Handle ADDITIONAL SENSE CODE field and ADDITIONAL SENSE
     * CODE QUALIFIER which is quit a large work(19 pages of PDF):
     *  SPC-5 rev7 Table 49 - ASC and ASCQ assignments
     */

    for (; i < len; ++i)
        sprintf(&sense_data_str[i * 2], "%02x", sense_data[i]);

    switch(*sense_key) {
    case _T10_SPC_SENSE_KEY_NO_SENSE:
    case _T10_SPC_SENSE_KEY_RECOVERED_ERROR:
    case _T10_SPC_SENSE_KEY_COMPLETED:
        /* No error */
        rc = 0;
        goto out;
    default:
    /* As sense_key is 4 bytes and we covered all 16 values in
     * _T10_SPC_SENSE_KEY_STR, there will be no out of index error.
     */
        _lsm_err_msg_set(err_msg, "Got SCSI sense data, key %s(0x%02x), "
                         "ADDITIONAL SENSE CODE 0x%02x, ADDITIONAL SENSE CODE "
                         "QUALIFIER 0x%02x, all sense data in hex: %s",
                         _T10_SPC_SENSE_KEY_STR[*sense_key], *sense_key,
                         asc, ascq, sense_data_str);
    }

 out:
    return rc;
}

int _sg_io_open_ro(char *err_msg, const char *disk_path, int *fd)
{
    return _sg_io_open(err_msg, disk_path, fd, O_RDONLY|O_NONBLOCK);
}

void _sg_t10_vpd83_dp_array_free(struct _sg_t10_vpd83_dp **dps,
                                 uint16_t dp_count)
{
    uint16_t i = 0;

    assert(dps != NULL);

    for (; i < dp_count; ++i) {
        free(dps[i]);
    }
    free(dps);
}

int _sg_io_open_rw(char *err_msg, const char *disk_path, int *fd)
{
    return _sg_io_open(err_msg, disk_path, fd, O_RDWR|O_NONBLOCK);
}

int _sg_io_recv_diag(char *err_msg, int fd, uint8_t page_code, uint8_t *data)
{
    int rc = LSM_ERR_OK;
    uint8_t cdb[_T10_SPC_RECV_DIAG_CMD_LEN];
    int ioctl_errno = 0;
    char strerr_buff[_LSM_ERR_MSG_LEN];
    uint8_t sense_data[_T10_SPC_SENSE_DATA_MAX_LENGTH];
    uint8_t sense_key = _T10_SPC_SENSE_KEY_NO_SENSE;
    char sense_err_msg[_LSM_ERR_MSG_LEN];

    assert(err_msg != NULL);
    assert(fd >= 0);
    assert(data != NULL);

    memset(sense_err_msg, 0, _LSM_ERR_MSG_LEN);

    /* SPC-5 rev7, Table 219 - RECEIVE DIAGNOSTIC RESULTS command */
    cdb[0] = RECEIVE_DIAGNOSTIC;                /* OPERATION CODE */
    cdb[1] = 1;                                 /* PCV */
    /* We have no use case for PCV = 0 yet.
     * When PCV == 0, it means page code is invalid, just retrieve the result
     * of recent SEND DIAGNOSTIC.
     */
    cdb[2] = page_code & UINT8_MAX;             /* PAGE CODE */
    cdb[3] = (_SG_T10_SPC_RECV_DIAG_MAX_LEN >> 8 ) & UINT8_MAX;
                                                /* ALLOCATION LENGTH, MSB */
    cdb[4] = _SG_T10_SPC_RECV_DIAG_MAX_LEN & UINT8_MAX;
                                                /* ALLOCATION LENGTH, LSB */
    cdb[5] = 0;                                 /* CONTROL */
    /* We have no use case need for handling auto contingent allegiance(ACA)
     * yet.
     */

    ioctl_errno = _sg_io(fd, cdb, _T10_SPC_RECV_DIAG_CMD_LEN, data,
                         _SG_T10_SPC_RECV_DIAG_MAX_LEN, sense_data,
                         _SG_IO_RECV_DATA);
    if (ioctl_errno != 0) {
        rc = LSM_ERR_LIB_BUG;
        /* TODO(Gris Ge): Check 'Supported Diagnostic Pages diagnostic page' */
        _check_sense_data(sense_err_msg, sense_data, &sense_key);

        _lsm_err_msg_set(err_msg, "Got error from SGIO RECEIVE_DIAGNOSTIC "
                         "for page code 0x%02x: error %d(%s), %s", page_code,
                         ioctl_errno,
                         strerror_r(ioctl_errno, strerr_buff, _LSM_ERR_MSG_LEN),
                         sense_err_msg);
        goto out;
    }

 out:

    return rc;
}

int _sg_io_send_diag(char *err_msg, int fd, uint8_t *data, uint16_t data_len)
{
    int rc = LSM_ERR_OK;
    uint8_t cdb[_T10_SPC_SEND_DIAG_CMD_LEN];
    int ioctl_errno = 0;
    char strerr_buff[_LSM_ERR_MSG_LEN];
    uint8_t sense_data[_T10_SPC_SENSE_DATA_MAX_LENGTH];
    uint8_t sense_key = _T10_SPC_SENSE_KEY_NO_SENSE;
    char sense_err_msg[_LSM_ERR_MSG_LEN];

    assert(err_msg != NULL);
    assert(fd >= 0);
    assert(data != NULL);
    assert(data_len > 0);

    memset(sense_err_msg, 0, _LSM_ERR_MSG_LEN);

    /* SPC-5 rev7, Table 219 - RECEIVE DIAGNOSTIC RESULTS command */
    cdb[0] = SEND_DIAGNOSTIC;                   /* OPERATION CODE */
    cdb[1] = 0x10;                              /* SELF-TEST, PF, DEVOFFL,
                                                   UNITOFFL */
    /* Only set PF(Page format) bit and leave others as 0
     * Check SPC-5 rev 7 Table 271 - The meanings of the SELF - TEST CODE
     * field, the PF bit, the SELF TEST bit, and the PARAMETER LIST LENGTH
     * field.
     */
    cdb[2] = 0;                                 /* Reserved */
    cdb[3] = data_len >> 8;                     /* PARAMETER LIST LENGTH, MSB */
    cdb[4] = data_len & UINT8_MAX;              /* PARAMETER LIST LENGTH, LSB */
    cdb[5] = 0;                                 /* CONTROL */
    /* We have no use case need for handling auto contingent allegiance(ACA)
     * yet.
     */

    ioctl_errno = _sg_io(fd, cdb, _T10_SPC_SEND_DIAG_CMD_LEN, data,
                         data_len, sense_data, _SG_IO_SEND_DATA);
    if (ioctl_errno != 0) {
        rc = LSM_ERR_LIB_BUG;
        /* TODO(Gris Ge): No idea why this could fail */
        _check_sense_data(sense_err_msg, sense_data, &sense_key);

        _lsm_err_msg_set(err_msg, "Got error from SGIO SEND_DIAGNOSTIC "
                         "for error %d(%s), %s", ioctl_errno,
                         strerror_r(ioctl_errno, strerr_buff, _LSM_ERR_MSG_LEN),
                         sense_err_msg);
    }

    return rc;
}

/* Find out the target port address via SCSI VPD device Identification
 * page:
 *  SPC-5 rev7 Table 487 - ASSOCIATION field
 */
int _sg_tp_sas_addr_of_disk(char *err_msg, int fd, char *tp_sas_addr)
{
    int rc = LSM_ERR_OK;
    struct _sg_t10_vpd83_dp **dps = NULL;
    uint8_t vpd_di_data[_SG_T10_SPC_VPD_MAX_LEN];
    uint16_t dp_count = 0;
    uint16_t i = 0;
    struct _sg_t10_vpd83_naa_header *naa_header = NULL;

    assert(err_msg != NULL);
    assert(fd >= 0);
    assert(tp_sas_addr != NULL);

    _good(_sg_io_vpd(err_msg, fd, _SG_T10_SPC_VPD_DI, vpd_di_data), rc, out);
    _good(_sg_parse_vpd_83(err_msg, vpd_di_data, &dps, &dp_count), rc, out);

    memset(tp_sas_addr, 0, _SG_T10_SPL_SAS_ADDR_LEN);

    for (; i < dp_count; ++i) {
        if ((dps[i]->header.association != _SG_T10_SPC_ASSOCIATION_TGT_PORT) ||
            (dps[i]->header.piv != 1) ||
            (dps[i]->header.designator_type !=
             _SG_T10_SPC_VPD_DI_DESIGNATOR_TYPE_NAA) ||
            (dps[i]->header.protocol_id != _SG_T10_SPC_PROTOCOL_ID_SAS))
            continue;

        naa_header = (struct _sg_t10_vpd83_naa_header *) dps[i]->designator;
        _be_raw_to_hex((uint8_t *) naa_header,
                       _SG_T10_SPL_SAS_ADDR_LEN_BITS, tp_sas_addr);

        break;
    }

    if (tp_sas_addr[0] == '\0') {
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg,
                         "Given disk does not expose SCSI target port "
                         "SAS address via SCSI Device Identification VPD page");
    }

 out:
    if (dps != NULL)
        _sg_t10_vpd83_dp_array_free(dps, dp_count);

    return rc;
}

int _sg_io_mode_sense(char *err_msg, int fd, uint8_t page_code,
                      uint8_t sub_page_code, uint8_t *data)
{
    int rc = LSM_ERR_OK;
    uint8_t tmp_data[_SG_T10_SPC_MODE_SENSE_MAX_LEN];
    uint8_t cdb[_T10_SPC_MODE_SENSE_CMD_LEN];
    uint8_t sense_data[_T10_SPC_SENSE_DATA_MAX_LENGTH];
    int ioctl_errno = 0;
    char strerr_buff[_LSM_ERR_MSG_LEN];
    uint8_t sense_key = _T10_SPC_SENSE_KEY_NO_SENSE;
    char sense_err_msg[_LSM_ERR_MSG_LEN];
    struct _sg_t10_mode_para_hdr *mode_hdr = NULL;
    uint16_t block_dp_len = 0;
    uint16_t mode_data_len = 0;

    assert(err_msg != NULL);
    assert(fd >= 0);
    assert(data != NULL);

    memset(sense_err_msg, 0, _LSM_ERR_MSG_LEN);
    memset(data, 0, _SG_T10_SPC_MODE_SENSE_MAX_LEN);

    /* SPC-5 Table 171 — MODE SENSE(10) command */
    cdb[0] = MODE_SENSE_10;                     /* OPERATION CODE */
    cdb[1] = 0;                                 /* disable block descriptors
                                                 * and long LBA accepted
                                                 */
    /* We don't need block descriptors or long LBA accepted */;
    cdb[2] = page_code & UINT8_MAX;             /* PAGE CODE and Page control*/
    /* The page control is 0 means 'current values' */
    cdb[3] = sub_page_code & UINT8_MAX;         /* Subpage code */
    cdb[4] = 0;                                 /* Reserved */
    cdb[5] = 0;                                 /* Reserved */
    cdb[6] = 0;                                 /* Reserved */
    cdb[7] = (_SG_T10_SPC_MODE_SENSE_MAX_LEN >> 8) & UINT8_MAX;
                                                /* ALLOCATION LENGTH MSB */
    cdb[8] = _SG_T10_SPC_MODE_SENSE_MAX_LEN & UINT8_MAX;
                                                /* ALLOCATION LENGTH LSB */
    cdb[9] = 0;                                 /* CONTROL */
    /* We have no use case need for handling auto contingent allegiance(ACA)
     * yet.
     */

    ioctl_errno = _sg_io(fd, cdb, _T10_SPC_MODE_SENSE_CMD_LEN, tmp_data,
                         _SG_T10_SPC_MODE_SENSE_MAX_LEN, sense_data,
                         _SG_IO_RECV_DATA);

    if (ioctl_errno == 0) {
        mode_hdr = (struct _sg_t10_mode_para_hdr *) tmp_data;
        mode_data_len = be16toh(mode_hdr->mode_data_len_be);
        if ((mode_data_len == 0) ||
            (mode_data_len >= _SG_T10_SPC_MODE_SENSE_MAX_LEN)) {
            rc = LSM_ERR_LIB_BUG;
            _lsm_err_msg_set(err_msg, "BUG: Got illegal SCSI mode page return: "
                             "invalid MODE DATA LENGTH %" PRIu16 "\n",
                             mode_data_len);
            goto out;
        }
        block_dp_len = be16toh(mode_hdr->block_dp_header_len_be);
        if ((block_dp_len >= _SG_T10_SPC_MODE_SENSE_MAX_LEN -
             sizeof(struct _sg_t10_mode_para_hdr))) {
            rc = LSM_ERR_LIB_BUG;
            _lsm_err_msg_set(err_msg, "BUG: Got illegal SCSI mode page return: "
                             "invalid BLOCK DESCRIPTOR LENGTH %" PRIu16 "\n",
                             block_dp_len);
            goto out;
        }
        memcpy(data,
               tmp_data + sizeof(struct _sg_t10_mode_para_hdr) + block_dp_len,
               sizeof(mode_hdr->mode_data_len_be) + mode_data_len -
               sizeof(struct _sg_t10_mode_para_hdr) - block_dp_len);
        goto out;
    }

    if (_check_sense_data(sense_err_msg, sense_data, &sense_key) != 0) {
        if (sense_key == _T10_SPC_SENSE_KEY_ILLEGAL_REQUEST) {
            rc = LSM_ERR_NO_SUPPORT;
            _lsm_err_msg_set(err_msg, "SCSI MODE SENSE 0x%02x page and "
                             "sub page 0x%02x is not supported", page_code,
                             sub_page_code);
            goto out;
        } else {
            rc = LSM_ERR_LIB_BUG;
            _lsm_err_msg_set(err_msg, "BUG: Unexpected failure of "
                             "_sg_io_mode_sense(): %s",
                             sense_err_msg);
            goto out;
        }
    }
    rc = LSM_ERR_LIB_BUG;
    _lsm_err_msg_set(err_msg, "BUG: Unexpected failure of "
                     "_sg_io_mode_sense(): error %d(%s), with no error in "
                     "SCSI sense data",
                     ioctl_errno,
                     strerror_r(ioctl_errno, strerr_buff, _LSM_ERR_MSG_LEN)
                     );

 out:
    return rc;
}

int _sg_host_no(char *err_msg, int fd, unsigned int *host_no)
{
    int rc = LSM_ERR_OK;
    int ioctl_errno = 0;
    char strerr_buff[_LSM_ERR_MSG_LEN];

    assert(err_msg != NULL);
    assert(fd >= 0);
    assert(host_no != NULL);

    *host_no = UINT_MAX;

    if (ioctl(fd, SCSI_IOCTL_GET_BUS_NUMBER, host_no) != 0) {
        ioctl_errno = errno;
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "IOCTL SCSI_IOCTL_GET_BUS_NUMBER failed: "
                         "%d, %s", ioctl_errno,
                         strerror_r(ioctl_errno, strerr_buff,
                                    _LSM_ERR_MSG_LEN));
        goto out;
    }

 out:
    return rc;
}

int _fill_ata_regs_fixed(uint8_t *ata_output_regs, uint8_t *sense_data)
{
    struct _ata_registers_output_28_bit *output_registers = NULL;
    struct _sg_t10_sense_ata_fixed *ata_sense_data = NULL;

    assert(ata_output_regs != NULL);
    assert(sense_data != NULL);

    output_registers = (struct _ata_registers_output_28_bit *)
                       ata_output_regs;
    ata_sense_data = (struct _sg_t10_sense_ata_fixed *) sense_data;

    output_registers->error = ata_sense_data->error;
    output_registers->status = ata_sense_data->status;
    output_registers->device = ata_sense_data->device;
    output_registers->count = ata_sense_data->count;
    output_registers->lba_low = ata_sense_data->lba_low;
    output_registers->lba_mid = ata_sense_data->lba_mid;
    output_registers->lba_high = ata_sense_data->lba_high;

    return 0;
}

int _fill_ata_regs_desc(uint8_t additional_sense_len,
                        uint8_t *sense_data_desc_list_index,
                        uint8_t *ata_output_regs)
{
    uint8_t i = 0;
    struct _ata_registers_output_28_bit *output_registers = NULL;
    struct _sg_t10_ata_status_return_dp_hdr *current_status_desc = NULL;
    struct _sg_t10_ata_status_return_dp *ata_status_desc = NULL;

    assert(ata_output_regs != NULL);
    assert(sense_data_desc_list_index != NULL);

    output_registers = (struct _ata_registers_output_28_bit *)
                       ata_output_regs;
    current_status_desc = (struct _sg_t10_ata_status_return_dp_hdr *)
                          sense_data_desc_list_index;

    while (i < additional_sense_len) {

        if (current_status_desc->descriptor_code == _T10_ATA_STATUS_RETURN)
            break;

        i = i + current_status_desc->additional_desc_len +
            sizeof(struct _sg_t10_ata_status_return_dp_hdr);
        current_status_desc = current_status_desc +
                          current_status_desc->additional_desc_len +
                          sizeof(struct _sg_t10_ata_status_return_dp_hdr);
    }

    if (i > additional_sense_len)
        return -1;

    ata_status_desc = (struct _sg_t10_ata_status_return_dp *)
                      current_status_desc;

    output_registers->error = ata_status_desc->error;
    output_registers->count = ata_status_desc->count;
    output_registers->lba_low = ata_status_desc->lba_low;
    output_registers->lba_mid = ata_status_desc->lba_mid;
    output_registers->lba_high = ata_status_desc->lba_high;
    output_registers->device = ata_status_desc->device;
    output_registers->status = ata_status_desc->status;

    return 0;
}

int _get_ata_output_regs(uint8_t *sense_data, uint8_t *ata_output_regs)
{
    int rc = -1;
    struct _sg_t10_sense_header *sense_hdr = NULL;
    struct _sg_t10_sense_fixed *sense_fixed = NULL;
    struct _sg_t10_sense_dp *sense_dp = NULL;
    uint8_t asc = 0;
    uint8_t ascq = 0;

    assert(sense_data != NULL);
    assert(ata_output_regs != NULL);

    sense_hdr = (struct _sg_t10_sense_header*) sense_data;

    switch(sense_hdr->response_code) {
    case _T10_SPC_SENSE_REPORT_TYPE_CUR_INFO_FIXED:
    case _T10_SPC_SENSE_REPORT_TYPE_DEF_ERR_FIXED:
        sense_fixed = (struct _sg_t10_sense_fixed *) sense_data;
        asc = sense_fixed->asc;
        ascq = sense_fixed->ascq;
        if ((asc == 0) && (ascq == _T10_SPC_ASCQ_ATA_PASSTHROUGH)) {
            rc = _fill_ata_regs_fixed(ata_output_regs, sense_data);
        }
        break;
    case _T10_SPC_SENSE_REPORT_TYPE_CUR_INFO_DP:
    case _T10_SPC_SENSE_REPORT_TYPE_DEF_ERR_DP:
        sense_dp = (struct _sg_t10_sense_dp *) sense_data;
	if (sense_dp->len > 0) {
            rc = _fill_ata_regs_desc(sense_dp->len, &sense_data[8],
                                     ata_output_regs);
        }
        break;
    default:
        goto out;
    }

 out:
    return rc;
}

int _sg_io_ata_passthrough(char *err_msg, int fd, bool need_output_reg,
                           uint8_t direction, uint8_t *ata_cmd, uint8_t *data,
                           ssize_t data_len, uint8_t *ata_output_regs)
{
    int rc = LSM_ERR_OK;
    uint8_t cdb[ATA_PASS_THROUGH_12_LEN];
    uint8_t cmd_len = 0;
    uint8_t sense_data[_T10_SPC_SENSE_DATA_MAX_LENGTH];
    int ioctl_errno = 0;
    char sense_err_msg[_LSM_ERR_MSG_LEN];
    char strerr_buff[_LSM_ERR_MSG_LEN];
    uint8_t sense_key = _T10_SPC_SENSE_KEY_NO_SENSE;
    struct _ata_registers_input_28_bit *input_registers = NULL;

    assert(err_msg != NULL);
    assert(fd >= 0);
    assert(ata_cmd != NULL);
    assert(ata_output_regs != NULL);

    memset(cdb, 0, ATA_PASS_THROUGH_12_LEN);
    memset(sense_data, 0, _T10_SPC_SENSE_DATA_MAX_LENGTH);
    memset(sense_err_msg, 0, _LSM_ERR_MSG_LEN);

    //set BYT_BLOK bit to 1 for all commands
    cdb[2] = 1 << 2;

    //set CK_COND bit
    if (need_output_reg)
        cdb[2] = cdb[2] | (1 << 5);

    //set PROTOCOL value as defined in Table 2, and
    //set T_LENGTH and T_DIR as appropriate.
    if (direction == ATA_NON_DATA_COMMAND) {
        cdb[1] = 3 << 1;
        cdb[2] = cdb[2] | (1 << 3);
    } else if (direction == ATA_DATA_IN_COMMAND) {
        cdb[1] = 4 << 1;
        cdb[2] = cdb[2] | (1 << 3) | 2;
    } else if (direction == ATA_DATA_OUT_COMMAND) {
        cdb[1] = 5 << 1;
        cdb[2] = cdb[2] | 2;
    }

    input_registers = (struct _ata_registers_input_28_bit *) ata_cmd;

    cdb[0] = ATA_PASS_THROUGH_12;
    cdb[3] = input_registers->feature;
    cdb[4] = input_registers->count;
    cdb[5] = input_registers->lba_low;
    cdb[6] = input_registers->lba_mid;
    cdb[7] = input_registers->lba_high;
    cdb[8] = input_registers->device;
    cdb[9] = input_registers->command;
    cmd_len = ATA_PASS_THROUGH_12_LEN;

    ioctl_errno = _sg_io(fd, cdb, cmd_len, data, data_len, sense_data,
                         _SG_IO_NO_DATA);

    if (ioctl_errno) {
        rc = _get_ata_output_regs(sense_data, ata_output_regs);

        if (rc) {
            _check_sense_data(sense_err_msg, sense_data, &sense_key);

            _lsm_err_msg_set(err_msg, "Got error from SGIO ATA PASSTHROUGH: "
                             "error %d(%s), %s", ioctl_errno,
                             strerror_r(ioctl_errno, strerr_buff,
                                        _LSM_ERR_MSG_LEN),
                             sense_err_msg);
        }
    }

    return rc;
}

int _sg_log_sense(char *err_msg, int fd, uint8_t page_code,
                  uint8_t sub_page_code, uint8_t *data)
{
    int rc = LSM_ERR_OK;
    uint8_t tmp_data[_T10_SPC_LOG_SENSE_MAX_LEN];
    uint8_t cdb[_T10_SPC_LOG_SENSE_CMD_LEN];
    uint8_t sense_data[_T10_SPC_SENSE_DATA_MAX_LENGTH];
    int ioctl_errno = 0;
    char strerr_buff[_LSM_ERR_MSG_LEN];
    uint8_t sense_key = _T10_SPC_SENSE_KEY_NO_SENSE;
    char sense_err_msg[_LSM_ERR_MSG_LEN];
    struct _sg_t10_log_para_hdr *log_hdr = NULL;
    uint16_t log_data_len = 0;

    assert(err_msg != NULL);
    assert(fd >= 0);
    assert(data != NULL);

    memset(sense_err_msg, 0, _LSM_ERR_MSG_LEN);
    memset(cdb, 0, _T10_SPC_LOG_SENSE_CMD_LEN);

    cdb[0] = LOG_SENSE;
    cdb[2] = (PAGE_CONTROL_CUMULATIVE_VALS << 6) | (page_code & 0x3f);
    cdb[3] = sub_page_code & UINT8_MAX;
    cdb[7] = (_T10_SPC_LOG_SENSE_MAX_LEN >> 8) & UINT8_MAX;
    cdb[8] = _T10_SPC_LOG_SENSE_MAX_LEN & UINT8_MAX;

    ioctl_errno = _sg_io(fd, cdb, _T10_SPC_LOG_SENSE_CMD_LEN, tmp_data,
                         _T10_SPC_LOG_SENSE_MAX_LEN, sense_data,
                         _SG_IO_RECV_DATA);

    if (ioctl_errno) {
        rc = LSM_ERR_LIB_BUG;
        _check_sense_data(sense_err_msg, sense_data, &sense_key);

        if (sense_key == _T10_SPC_SENSE_KEY_ILLEGAL_REQUEST) {
            rc = LSM_ERR_NO_SUPPORT;
            goto out;
        }

        _lsm_err_msg_set(err_msg, "Got error from SGIO LOG SENSE "
                         "with error %d(%s), %s", ioctl_errno,
                         strerror_r(ioctl_errno, strerr_buff, _LSM_ERR_MSG_LEN),
                         sense_err_msg);
        goto out;
    }

    log_hdr = (struct _sg_t10_log_para_hdr *) tmp_data;
    log_data_len = be16toh(log_hdr->log_data_len_be);
    if ((log_data_len == 0) ||
        (log_data_len >= _T10_SPC_LOG_SENSE_MAX_LEN)) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "BUG: Got illegal SCSI log page return: "
                         "invalid LOG DATA LENGTH %" PRIu16 "\n",
                         log_data_len);
        goto out;
    }
    memcpy(data, tmp_data + sizeof(struct _sg_t10_log_para_hdr), log_data_len);

out:

    return rc;
}

int _sg_request_sense(char *err_msg, int fd, uint8_t *returned_sense_data)
{
    int rc = LSM_ERR_OK;
    uint8_t request_sense[_T10_SPC_REQUEST_SENSE_MAX_LEN];
    uint8_t cdb[_T10_SPC_REQUEST_SENSE_CMD_LEN];
    uint8_t sense_data[_T10_SPC_SENSE_DATA_MAX_LENGTH];
    int ioctl_errno = 0;
    uint8_t sense_key = _T10_SPC_SENSE_KEY_NO_SENSE;
    char sense_err_msg[_LSM_ERR_MSG_LEN];
    char strerr_buff[_LSM_ERR_MSG_LEN];

    assert(err_msg != NULL);
    assert(fd >= 0);
    assert(returned_sense_data != NULL);

    memset(sense_err_msg, 0, _LSM_ERR_MSG_LEN);
    memset(cdb, 0, _T10_SPC_REQUEST_SENSE_CMD_LEN);

    cdb[0] = REQUEST_SENSE;
    cdb[4] = _T10_SPC_REQUEST_SENSE_MAX_LEN & UINT8_MAX;

    ioctl_errno = _sg_io(fd, cdb, _T10_SPC_REQUEST_SENSE_CMD_LEN, request_sense,
                         _T10_SPC_REQUEST_SENSE_MAX_LEN,
                         sense_data, _SG_IO_RECV_DATA);

    if (ioctl_errno) {
        rc = LSM_ERR_LIB_BUG;
        _check_sense_data(sense_err_msg, sense_data, &sense_key);

        if (sense_key == _T10_SPC_SENSE_KEY_ILLEGAL_REQUEST) {
            rc = LSM_ERR_NO_SUPPORT;
            goto out;
        }

        _lsm_err_msg_set(err_msg, "Got error from SGIO REQUEST SENSE: "
                         "error %d(%s) %s", ioctl_errno,
                         strerror_r(ioctl_errno, strerr_buff,
                                    _LSM_ERR_MSG_LEN),
                         sense_err_msg);
        goto out;
    }

    memcpy(returned_sense_data, sense_data, _T10_SPC_SENSE_DATA_MAX_LENGTH);

out:

    return rc;
}

int32_t _sg_info_excep_interpret_asc(uint8_t asc)
{
    if (asc == _T10_SPC_ASC_IMPENDING_FAILURE)
        return LSM_DISK_HEALTH_STATUS_FAIL;
    else if (asc == _T10_SPC_ASC_WARNING)
        return LSM_DISK_HEALTH_STATUS_WARN;
    else
        return LSM_DISK_HEALTH_STATUS_GOOD;
}

/*
 * Query a SAS drive to get its health status.
 * This method will attempt to:
 * 1. Retrieve the MRIE value of the SAS drive
 * 2. Depending on the MRIE value, we will either submit a REQUEST SENSE
 *    command or a LOG SENSE command.
 * 3. Return the health status to the caller.
 *
 * Input *health_status should be a pointer to an int32_t value.
 * Return LSM_ERR_NO_MEMORY or LSM_ERR_NO_SUPPORT or LSM_ERR_LIB_BUG or
 * LSM_ERR_NOT_FOUND_DISK.
 */
int _sg_sas_health_status(char *err_msg, int fd, int32_t *health_status)
{
    int rc = LSM_ERR_OK;
    uint8_t info_excep_mode_page[_SG_T10_SPC_MODE_SENSE_MAX_LEN];
    uint8_t info_excep_log_page[_T10_SPC_LOG_SENSE_MAX_LEN];
    uint8_t asc = 0;
    uint8_t requested_sense[_T10_SPC_SENSE_DATA_MAX_LENGTH];
    struct _sg_t10_sense_fixed *sense_fixed = NULL;
    struct _sg_t10_info_excep_mode_page_0_hdr *ie_mode_hdr = NULL;
    struct _sg_t10_info_excep_general_log_hdr *ie_log_hdr = NULL;

    _good(_sg_io_mode_sense(err_msg, fd, INFO_EXCEP_CONTROL_PAGE, 0,
                            info_excep_mode_page), rc, out);
    ie_mode_hdr = (struct _sg_t10_info_excep_mode_page_0_hdr *)
                   info_excep_mode_page;

    if (ie_mode_hdr->mrie == MRIE_REPORT_INFO_EXCEP_ON_REQUEST) {
        _good(_sg_request_sense(err_msg, fd, requested_sense), rc, out);
        sense_fixed = (struct _sg_t10_sense_fixed *) requested_sense;
        asc = sense_fixed->asc;
    } else {
        _good(_sg_log_sense(err_msg, fd, _T10_SPC_INFO_EXCEP_PAGE_CODE, 0,
                            info_excep_log_page), rc, out);
        // SPC5 rev07 - Table 349 - Informational Exceptions General log parameter
        ie_log_hdr = (struct _sg_t10_info_excep_general_log_hdr *)
                     info_excep_log_page;
        asc = ie_log_hdr->asc;
    }

    *health_status = _sg_info_excep_interpret_asc(asc);

out:
    return rc;
}

/*
 * Query a SATA drive attached via SAS to get its health status.
 *
 * Input *health_status should be a pointer to an int32_t value.
 * Return LSM_ERR_NO_MEMORY or LSM_ERR_NO_SUPPORT or LSM_ERR_LIB_BUG or
 * LSM_ERR_NOT_FOUND_DISK.
 */
int _sg_ata_passthrough_health_status(char *err_msg, int fd,
                                      int32_t *health_status)
{
    int rc = LSM_ERR_OK;
    uint8_t ata_cmd[_ATA_REGISTER_INPUT_28_BIT_LENGTH];
    uint8_t ata_output_regs[_ATA_REGISTER_OUTPUT_28_BIT_LENGTH];


    memset(ata_cmd, 0, _ATA_REGISTER_INPUT_28_BIT_LENGTH);
    memset(ata_output_regs, 0, _ATA_REGISTER_OUTPUT_28_BIT_LENGTH);

    _ata_smart_status_fill_registers(ata_cmd, ATA_SMART_COMMAND,
                                     ATA_SMART_RETURN_STATUS_SUBCOMMAND,
                                     SMART_STATUS_LBA_HIGH_DEFAULT,
                                     SMART_STATUS_LBA_MID_DEFAULT, 0, 0, 0);

    rc = _sg_io_ata_passthrough(err_msg, fd, true, ATA_NON_DATA_COMMAND,
                                ata_cmd, NULL, 0, ata_output_regs);

    if (!rc)
        *health_status = _ata_smart_status_interpret_output_regs(ata_output_regs);
    else
        rc = LSM_ERR_NO_SUPPORT;

    return rc;
}
