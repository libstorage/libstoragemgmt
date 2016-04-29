/*
 * Copyright (C) 2016 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
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
#define _T10_SPC_VPD_SUP_VPD_PGS_LIST_OFFSET            4

#define _SG_IO_SEND_DATA                                1
#define _SG_IO_RECV_DATA                                2

#pragma pack(push, 1)
/*
 * SPC-5 rev 7 Table 589 - Device Identification VPD page
 */
struct _sg_t10_vpd83_header {
    uint8_t dev_type : 5;
    uint8_t qualifier : 3;
    uint8_t page_code;
    uint16_t page_len_be;
};

struct _sg_t10_vpd00 {
    uint8_t ignore;
    uint8_t page_code;
    uint16_t page_len_be;
    uint8_t supported_vpd_list_begin;
};

#pragma pack(pop)

/*
 * Return 0 if pass, return errno if failure.
 *
 */
static int _sg_io(int fd, uint8_t *cdb, uint8_t cdb_len, uint8_t *data,
                  ssize_t data_len, int direction);

static struct _sg_t10_vpd83_dp *_sg_t10_vpd83_dp_new(void);

static int _sg_io_open(char *err_msg, const char *disk_path, int *fd,
                       int oflag);

static int _sg_io(int fd, uint8_t *cdb, uint8_t cdb_len, uint8_t *data,
                  ssize_t data_len, int direction)
{
    int rc = 0;
    struct sg_io_hdr io_hdr;

    assert(cdb != NULL);
    assert(cdb_len != 0);
    assert(data != NULL);
    assert(data_len != 0);

    memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
    if (direction == _SG_IO_RECV_DATA)
        memset(data, 0, (size_t) data_len);
    io_hdr.interface_id = 'S';  /* 'S' for SCSI generic */
    io_hdr.cmdp = cdb;
    io_hdr.cmd_len = cdb_len;
    io_hdr.sbp = NULL;  /* TODO(Gris Ge): Handle sense data for error message */
    io_hdr.mx_sb_len = 0;
    if (direction == _SG_IO_RECV_DATA)
        io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    else
        io_hdr.dxfer_direction = SG_DXFER_TO_DEV;

    io_hdr.dxferp = (unsigned char *) data;
    io_hdr.dxfer_len = data_len;
    io_hdr.timeout = _SG_IO_TMO;

    if (ioctl(fd, SG_IO, &io_hdr) != 0)
        rc = errno;

    if (rc != 0)
        memset(data, 0, (size_t) data_len);

    return rc;
}

int _sg_io_vpd(char *err_msg, int fd, uint8_t page_code, uint8_t *data)
{
    int rc = LSM_ERR_OK;
    uint8_t vpd_00_data[_SG_T10_SPC_VPD_MAX_LEN];
    uint8_t cdb[_T10_SPC_INQUIRY_CMD_LEN];
    int ioctl_errno = 0;
    int rc_vpd_00 = 0;
    char strerr_buff[_LSM_ERR_MSG_LEN];

    assert(err_msg != NULL);
    assert(fd >= 0);
    assert(data != NULL);

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
                         _SG_T10_SPC_VPD_MAX_LEN, _SG_IO_RECV_DATA);
    if (ioctl_errno != 0) {
        if (page_code == _SG_T10_SPC_VPD_SUP_VPD_PGS) {
            _lsm_err_msg_set(err_msg, "Not a SCSI compatible device");
            return LSM_ERR_NO_SUPPORT;
        }
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
                             "but failed with error %d(%s)",
                             page_code, ioctl_errno,
                             strerror_r(ioctl_errno, strerr_buff,
                                        _LSM_ERR_MSG_LEN));
            goto out;

        }

        rc = LSM_ERR_NO_SUPPORT;

        _lsm_err_msg_set(err_msg, "SCSI VPD 0x%02x page is not supported",
                         page_code);
        goto out;
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
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "BUG: Got incorrect VPD page code '%02x', "
                         "should be 0x83", vpd83_header->page_code);
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

    assert(err_msg != NULL);
    assert(fd >= 0);
    assert(data != NULL);

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
                         _SG_T10_SPC_RECV_DIAG_MAX_LEN, _SG_IO_RECV_DATA);
    if (ioctl_errno != 0) {
        rc = LSM_ERR_LIB_BUG;
        /* TODO(Gris Ge): Check 'Supported Diagnostic Pages diagnostic page' */

        _lsm_err_msg_set(err_msg, "Got error from SGIO RECEIVE_DIAGNOSTIC "
                         "for page code 0x%02x: %d(%s)", page_code, ioctl_errno,
                         strerror_r(ioctl_errno, strerr_buff,
                                    _LSM_ERR_MSG_LEN));
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

    assert(err_msg != NULL);
    assert(fd >= 0);
    assert(data != NULL);
    assert(data_len > 0);

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
                         data_len, _SG_IO_SEND_DATA);
    if (ioctl_errno != 0) {
        rc = LSM_ERR_LIB_BUG;
        /* TODO(Gris Ge): No idea why this could fail */

        _lsm_err_msg_set(err_msg, "Got error from SGIO SEND_DIAGNOSTIC "
                         "for %d(%s)", ioctl_errno,
                         strerror_r(ioctl_errno, strerr_buff,
                                    _LSM_ERR_MSG_LEN));
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
