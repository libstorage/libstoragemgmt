/*
 * Copyright (C) 2015-2016 Red Hat, Inc.
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

#define _GNU_SOURCE
/* ^ For strerror_r() */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libudev.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <scsi/sg.h>
#include <scsi/scsi.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <endian.h>

#include "libstoragemgmt/libstoragemgmt.h"
#include "libstoragemgmt/libstoragemgmt_error.h"
#include "libstoragemgmt/libstoragemgmt_plug_interface.h"

#define _T10_SBC_VPD_BLK_DEV_CHA                        0xb1
/* ^ SBC-4 rev9 Table 236 - Block Device Characteristics VPD page */
#define _T10_SBC_VPD_BLK_DEV_CHA_MAX_PAGE_LEN            0x3c + 4
/* ^ SBC-4 rev9 Table 236 Block Device Characteristics VPD page */
#define _T10_SPC_VPD_SUP_VPD_PGS                         0x00
/* ^ SPC-5 rev7 7.7.16 Supported VPD Pages VPD page */
#define _T10_SPC_VPD_SUP_VPD_PGS_MAX_PAGE_LEN            0xff + 1 + 4
/* ^ There are 256(0xff + 1) page codes, each only take 1 byte, we only
 *   need 256 bytes to store them. Addition 4 bytes for VPD 0x00 page header.
 *   Please refer to "SPC-5 rev7 Table 534 - Supported VPD Pages VPD page"
 *   for detail.
 */
#define _T10_SPC_VPD_SUP_VPD_PGS_LIST_OFFSET            4
/* ^ SPC-5 rev7 Table 534 - Supported VPD Pages VPD page */
#define _T10_SPC_INQUERY_CMD_LEN                        6
/* ^ SPC-5 rev7 Table 142 - INQUIRY command */
#define _T10_SBC_MEDIUM_ROTATION_NO_SUPPORT             0
/* ^ SPC-5 rev7 Table 237 - MEDIUM ROTATION RATE field */
#define _T10_SBC_MEDIUM_ROTATION_SSD                    1
/* ^ SPC-5 rev7 Table 237 - MEDIUM ROTATION RATE field */

#define _VPD_QUERY_TMO                                  1000
/* ^ VPD timeout: 1 second */
/*TODO(Gris Ge): Raise LSM_ERR_TIMEOUT error for this */

#define _T10_SPC_VPD_DI                         0x83
#define _T10_SPC_VPD_DI_MAX_LEN                 0xff + 4
#define _T10_SPC_VPD_DI_NAA_235_ID_LEN          8
#define _T10_SPC_VPD_DI_NAA_6_ID_LEN            16
#define _T10_SPC_VPD_DI_DESIGNATOR_TYPE_NAA     0x3
#define _T10_SPC_VPD_DI_NAA_TYPE_2              0x2
#define _T10_SPC_VPD_DI_NAA_TYPE_3              0x3
#define _T10_SPC_VPD_DI_NAA_TYPE_5              0x5
#define _T10_SPC_VPD_DI_NAA_TYPE_6              0x6
#define _T10_SPC_VPD_DI_ASSOCIATION_LUN         0
/* ^ SPC-5 rev7 7.7.6 Device Identification VPD page */

#define _LSM_MAX_VPD83_ID_LEN                   33
/* ^ Max one is 6h IEEE Registered Extended ID which it 32 bits hex string. */
#define _SYS_BLOCK_PATH "/sys/block"
#define _MAX_SD_NAME_STR_LEN 128
/* The linux kernel support INT_MAX(2147483647) scsi disks at most which will
 * be named as sd[a-z]{1,7}, the 7 here means `math.log(2147483647, 26) + 1`.
 * Hence, 128 bits might be enough for a quit a while.
 */

#define _SD_PATH_FORMAT "/dev/%s"
#define _MAX_SD_PATH_STR_LEN 128 + _MAX_SD_NAME_STR_LEN

#define _SYSFS_VPD83_PATH_FORMAT "/sys/block/%s/device/vpd_pg83"
#define _MAX_SYSFS_VPD83_PATH_STR_LEN  128 + _MAX_SD_NAME_STR_LEN

#define _SYSFS_BLK_PATH_FORMAT "/sys/block/%s"
#define _MAX_SYSFS_BLK_PATH_STR_LEN 128 + _MAX_SD_NAME_STR_LEN
#define _LSM_ERR_MSG_LEN 255

#pragma pack(push, 1)
/*
 * Table 589 - Device Identification VPD page
 */
struct t10_vpd83_header {
    uint8_t dev_type : 5;
    /* ^ PERIPHERAL DEVICE TYPE */
    uint8_t qualifier : 3;
    /* PERIPHERAL QUALIFIER */
    uint8_t page_code;
    uint16_t page_len;
};

/*
 * Table 590 - Designation descriptor
 */
struct t10_vpd83_dp_header {
    uint8_t code_set        : 4;
    uint8_t protocol_id     : 4;
    uint8_t designator_type : 4;
    uint8_t association     : 2;
    uint8_t reserved_1      : 1;
    uint8_t piv             : 1;
    uint8_t reserved_2;
    uint8_t designator_len;
};

struct t10_vpd83_dp {
    struct t10_vpd83_dp_header header;
    uint8_t designator[0xff];
};
/* ^ SPC-5 rev7 Table 486 - Designation descriptor. */

struct t10_vpd83_naa_header {
    uint8_t data_msb : 4;
    uint8_t naa_type : 4;
};

struct t10_sbc_vpd_bdc {
    uint8_t we_dont_care_0;
    uint8_t pg_code;
    uint16_t len_be;
    uint16_t medium_rotation_rate_be;
    uint8_t we_dont_care_1[58];
};
/* ^ SBC-4 rev 09 Table 236 - Block Device Characteristics VPD page */

#pragma pack(pop)

#define _lsm_err_msg_clear(err_msg) memset(err_msg, 0, _LSM_ERR_MSG_LEN)

#define _lsm_err_msg_set(err_msg, format, ...) \
    snprintf(err_msg, _LSM_ERR_MSG_LEN, format, ##__VA_ARGS__)

#define _lsm_string_list_foreach(l, i, d) \
    for(i = 0; \
        (l != NULL) && (i < lsm_string_list_size(l)) && \
        (d = lsm_string_list_elem_get(l, i)); \
        ++i)

#define _good(rc, rc_val, out) \
        do { \
                rc_val = rc; \
                if (rc_val != LSM_ERR_OK) \
                        goto out; \
        } while(0)

/*
 * All the 'err_msg' used in these static functions are expected to be
 * pointer of char[_LSM_ERR_MSG_LEN].
 */
static void _be_raw_to_hex(uint8_t *raw, size_t len, char *out);
static int _sysfs_read_file(char *err_msg, const char *sys_fs_path,
                            uint8_t *buff, ssize_t *size, size_t max_size);
static int _sysfs_vpd83_naa_of_sd_name(char *err_msg, const char *sd_name,
                                       char *vpd83);
static int _udev_vpd83_of_sd_name(char *err_msg, const char *sd_name,
                                  char *vpd83);
static int _sysfs_get_all_sd_names(char *err_msg,
                                   lsm_string_list **sd_name_list);
static int _check_null_ptr(char *err_msg, int arg_count, ...);
static bool _file_is_exist(char *err_msg, const char *path);
static int _parse_vpd_83(char *err_msg, uint8_t *vpd_data,
                         uint16_t vpd_data_len, struct t10_vpd83_dp ***dps,
                         uint16_t *dp_count);
static struct t10_vpd83_dp *_t10_vpd83_dp_new(void);
static void _t10_vpd83_dp_array_free(struct t10_vpd83_dp **dps,
                                    uint16_t dp_count);
static int _sysfs_vpd_pg83_data_get(char *err_msg, const char *sd_name,
                                    uint8_t *vpd_data, ssize_t *read_size);
static int _sg_io_ioctl_vpd(char *err_msg, int fd, uint8_t page_cde,
                            uint8_t *vpd_data, uint16_t vpd_data_len);
static int _sg_io_ioctl_open(char *err_msg, const char *sd_path, int *fd);
static bool _is_vpd_page_supported(uint8_t *vpd_0_data, uint16_t vpd_0_len,
                                   uint8_t page_code);

/*
 * Assume input path is not NULL or empty string.
 * Try to open provided path file or folder, return true if file could be open,
 * or false.
 */
static bool _file_is_exist(char *err_msg, const char *path)
{
    int fd = 0;

    fd = open(path, O_RDONLY);
    if (fd == -1) {
        return false;
    }
    close(fd);
    return true;
}

/*
 * Check whether input pointer is NULL.
 * If found NULL pointer, set error message and return LSM_ERR_INVALID_ARGUMENT.
 * Return LSM_ERR_OK if no NULL pointer.
 */
static int _check_null_ptr(char *err_msg, int arg_count, ...)
{
    int rc = LSM_ERR_OK;
    va_list arg;
    int i = 0;
    void *ptr = NULL;

    va_start(arg, arg_count);

    for (; i < arg_count; ++i) {
        ptr = va_arg(arg, void*);
        if (ptr == NULL) {
            _lsm_err_msg_set(err_msg, "Got NULL pointer in arguments");
            rc = LSM_ERR_INVALID_ARGUMENT;
            goto out;
        }
    }

 out:
    va_end(arg);
    return rc;
}

/*
 * Input big-endian uint8_t array, output has hex string.
 * No check on output memory size or boundary, make sure before invoke.
 */
static void _be_raw_to_hex(uint8_t *raw, size_t len, char *out)
{
    size_t i = 0;

    for (; i < len; ++i) {
        snprintf(out + (i * 2), 3, "%02x", raw[i]);
    }
    out[len * 2] = 0;

}

static int _sysfs_read_file(char *err_msg, const char *sys_fs_path,
                            uint8_t *buff, ssize_t *size, size_t max_size)
{
    int fd = -1;
    char strerr_buff[_LSM_ERR_MSG_LEN];

    *size = 0;
    memset(buff, 0, max_size);

    fd = open(sys_fs_path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT)
            return LSM_ERR_NO_SUPPORT;

        _lsm_err_msg_set(err_msg, "_sysfs_read_file(): Failed to open %s, "
                         "error: %d, %s", sys_fs_path, errno,
                         strerror_r(errno, strerr_buff, _LSM_ERR_MSG_LEN));
        return LSM_ERR_LIB_BUG;
    }
    *size = read(fd, buff, max_size);
    close(fd);

    if (*size < 0) {
        _lsm_err_msg_set(err_msg, "Failed to read %s, error: %d, %s",
                         sys_fs_path, errno,
                         strerror_r(errno, strerr_buff, _LSM_ERR_MSG_LEN));
        return LSM_ERR_LIB_BUG;
    }
    return LSM_ERR_OK;
}

/*
 * Retrieve the content of /sys/block/sda/device/vpd_pg83 file.
 * No argument checker here, assume all non-NULL and vpd_data is
                * char[_T10_SPC_VPD_DI_MAX_LEN]
 */
static int _sysfs_vpd_pg83_data_get(char *err_msg, const char *sd_name,
                                    uint8_t *vpd_data, ssize_t *read_size)
{
    char sysfs_path[_MAX_SYSFS_VPD83_PATH_STR_LEN];
    char sysfs_blk_path[_MAX_SYSFS_BLK_PATH_STR_LEN];

    memset(vpd_data, 0, _T10_SPC_VPD_DI_MAX_LEN);

    /*
     * Check the existence of disk vis /sys/block/sdX folder.
     */
    snprintf(sysfs_blk_path, _MAX_SYSFS_BLK_PATH_STR_LEN,
             _SYSFS_BLK_PATH_FORMAT, sd_name);
    if (_file_is_exist(err_msg, sysfs_blk_path) == false) {
        _lsm_err_msg_set(err_msg, "Disk %s not found", sd_name);
        return LSM_ERR_NOT_FOUND_DISK;
    }

    snprintf(sysfs_path, _MAX_SYSFS_VPD83_PATH_STR_LEN,
             _SYSFS_VPD83_PATH_FORMAT, sd_name);

    return _sysfs_read_file(err_msg, sysfs_path, vpd_data, read_size,
                            _T10_SPC_VPD_DI_MAX_LEN);
}

/*
 * Parse _SYSFS_VPD83_PATH_FORMAT file for VPD83 NAA ID.
 * When no such sysfs file found, return LSM_ERR_NO_SUPPORT.
 * When VPD83 page does not have NAA ID, return LSM_ERR_OK and vpd83 as empty
 * string.
 *
 * Input *vpd83 should be char[_LSM_MAX_VPD83_ID_LEN], assuming caller did
 * the check.
 * The maximum *sd_name strlen is (_MAX_SD_NAME_STR_LEN - 1), assuming caller
 * did the check.
 * Return LSM_ERR_NO_MEMORY or LSM_ERR_NO_SUPPORT or LSM_ERR_LIB_BUG or
 *        LSM_ERR_NOT_FOUND_DISK
 */
static int _sysfs_vpd83_naa_of_sd_name(char *err_msg, const char *sd_name,
                                       char *vpd83)
{
    ssize_t read_size = 0;
    struct t10_vpd83_naa_header *naa_header = NULL;
    int rc = LSM_ERR_OK;
    uint8_t vpd_data[_T10_SPC_VPD_DI_MAX_LEN];
    struct t10_vpd83_dp **dps = NULL;
    uint16_t dp_count = 0;
    uint16_t i = 0;

    memset(vpd83, 0, _LSM_MAX_VPD83_ID_LEN);

    if (sd_name == NULL) {
        _lsm_err_msg_set(err_msg, "_sysfs_vpd83_naa_of_sd_name(): "
                         "Input sd_name argument is NULL");
        rc = LSM_ERR_LIB_BUG;
        goto out;
    }

    _good(_sysfs_vpd_pg83_data_get(err_msg, sd_name, vpd_data, &read_size),
          rc, out);

    _good(_parse_vpd_83(err_msg, vpd_data, read_size, &dps, &dp_count),
          rc, out);

    for (; i < dp_count; ++i) {
        if ((dps[i]->header.designator_type ==
             _T10_SPC_VPD_DI_DESIGNATOR_TYPE_NAA)  &&
            (dps[i]->header.association == _T10_SPC_VPD_DI_ASSOCIATION_LUN)) {
            naa_header = (struct t10_vpd83_naa_header *) dps[i]->designator;
            switch(naa_header->naa_type) {
            case _T10_SPC_VPD_DI_NAA_TYPE_2:
            case _T10_SPC_VPD_DI_NAA_TYPE_3:
            case _T10_SPC_VPD_DI_NAA_TYPE_5:
                _be_raw_to_hex((uint8_t *) naa_header,
                               _T10_SPC_VPD_DI_NAA_235_ID_LEN, vpd83);
                break;
            case _T10_SPC_VPD_DI_NAA_TYPE_6:
                _be_raw_to_hex((uint8_t *) naa_header,
                               _T10_SPC_VPD_DI_NAA_6_ID_LEN, vpd83);
                break;
            default:
                rc = LSM_ERR_LIB_BUG;
                _lsm_err_msg_set(err_msg, "BUG: Got unknown NAA type ID %02x",
                                 naa_header->naa_type);
                goto out;
            }
        }
    }
    if (vpd83[0] == '\0') {
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg,
                         "SCSI VPD 83 NAA logical unit ID is not supported");
    }

 out:
    if (dps != NULL)
        _t10_vpd83_dp_array_free(dps, dp_count);
    return rc;
}

/*
 * Try to parse /sys/block/sda/device/vpd_pg83 for VPD83 NAA ID first.
 * This sysfs file is missing in some older kernel(like RHEL6), we use udev
 * ID_WWN_WITH_EXTENSION property instead, even through ID_WWN_WITH_EXTENSION
 * does not mean VPD83 NAA ID, this is the only workaround I could found for
 * old system without root privilege.
 * Input *vpd83 should be char[_LSM_MAX_VPD83_ID_LEN], assuming caller did
 * the check.
 * The maximum *sd_name strlen is (_MAX_SD_NAME_STR_LEN - 1), assuming caller
 * did the check.
 */
static int _udev_vpd83_of_sd_name(char *err_msg, const char *sd_name,
                                  char *vpd83)
{
    struct udev *udev = NULL;
    struct udev_device *sd_udev = NULL;
    int rc = LSM_ERR_OK;
    const char *wwn = NULL;
    char sys_path[_MAX_SYSFS_BLK_PATH_STR_LEN];

    memset(vpd83, 0, _LSM_MAX_VPD83_ID_LEN);
    snprintf(sys_path, _MAX_SYSFS_BLK_PATH_STR_LEN, _SYSFS_BLK_PATH_FORMAT,
             sd_name);

    udev = udev_new();
    if (udev == NULL) {
        rc = LSM_ERR_NO_MEMORY;
        goto out;
    }

    sd_udev = udev_device_new_from_syspath(udev, sys_path);
    if (sd_udev == NULL) {
        _lsm_err_msg_set(err_msg, "Provided disk not found");
        rc = LSM_ERR_NOT_FOUND_DISK;
        goto out;
    }
    wwn = udev_device_get_property_value(sd_udev, "ID_WWN_WITH_EXTENSION");
    if (wwn == NULL)
        goto out;

    if (strncmp(wwn, "0x", strlen("0x")) == 0)
        wwn += strlen("0x");

    snprintf(vpd83, _LSM_MAX_VPD83_ID_LEN, wwn);

 out:
    if (udev != NULL)
        udev_unref(udev);

    if (sd_udev != NULL)
        udev_device_unref(sd_udev);

    return rc;
}

static int _sysfs_get_all_sd_names(char *err_msg,
                                   lsm_string_list **sd_name_list)
{
    DIR *dir = NULL;
    struct dirent *dp = NULL;
    int rc = LSM_ERR_OK;
    char strerr_buff[_LSM_ERR_MSG_LEN];

    *sd_name_list = lsm_string_list_alloc(0 /* no pre-allocation */);
    if (*sd_name_list == NULL) {
        return LSM_ERR_NO_MEMORY;
    }

    dir = opendir(_SYS_BLOCK_PATH);
    if (dir == NULL) {
        _lsm_err_msg_set(err_msg, "Cannot open %s: error (%d)%s",
                         _SYS_BLOCK_PATH, errno,
                         strerror_r(errno, strerr_buff, _LSM_ERR_MSG_LEN));
        rc = LSM_ERR_LIB_BUG;
        goto out;
    }

    do {
        if ((dp = readdir(dir)) != NULL) {
            if (strncmp(dp->d_name, "sd", strlen("sd")) != 0)
                continue;
            if (strlen(dp->d_name) >= _MAX_SD_NAME_STR_LEN) {
                _lsm_err_msg_set(err_msg, "BUG: Got a SCSI disk name exceeded "
                                 "the maximum string length %d, current %zd",
                                 _MAX_SD_PATH_STR_LEN, strlen(dp->d_name));
                rc = LSM_ERR_LIB_BUG;
                goto out;
            }
            if (lsm_string_list_append(*sd_name_list, dp->d_name) != 0) {
                rc = LSM_ERR_NO_MEMORY;
                goto out;
            }
        }
    } while(dp != NULL);

 out:
    if (dir != NULL) {
        if (closedir(dir) != 0) {
            _lsm_err_msg_set(err_msg, "Failed to close dir %s: error (%d)%s",
                             _SYS_BLOCK_PATH, errno,
                             strerror_r(errno, strerr_buff, _LSM_ERR_MSG_LEN));
            rc = LSM_ERR_LIB_BUG;
        }
    }

    if (rc != LSM_ERR_OK) {
        lsm_string_list_free(*sd_name_list);
        *sd_name_list = NULL;
    }
    return rc;
}

static struct t10_vpd83_dp *_t10_vpd83_dp_new(void)
{
    struct t10_vpd83_dp *dp = NULL;

    dp = (struct t10_vpd83_dp *) malloc(sizeof(struct t10_vpd83_dp));

    if (dp != NULL) {
        memset(dp, 0, sizeof(struct t10_vpd83_dp));
    }
    return dp;
}

/*
 * Assuming input pointer is not NULL, caller should do that.
 */
static void _t10_vpd83_dp_array_free(struct t10_vpd83_dp **dps,
                                    uint16_t dp_count)
{
    uint16_t i = 0;
    for (; i < dp_count; ++i) {
        free(dps[i]);
    }
    free(dps);
}

/*
 * Assuming input pointer is not NULL, caller should do that.
 * The memory of output pointer 'dps' should be manually freed.
 *
 */
static int _parse_vpd_83(char *err_msg, uint8_t *vpd_data,
                         uint16_t vpd_data_len, struct t10_vpd83_dp ***dps,
                         uint16_t *dp_count)
{
    int rc = LSM_ERR_OK;
    struct t10_vpd83_header *vpd83_header = NULL;
    uint8_t *p = NULL;
    uint8_t *end_p = NULL;
    struct t10_vpd83_dp_header * dp_header = NULL;
    struct t10_vpd83_dp *dp = NULL;
    uint16_t i = 0;
    uint32_t vpd83_len = 0;

    if (vpd_data_len < sizeof(struct t10_vpd83_header)) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "BUG: VPD 83 data len '%" PRIu16
                         "' is less than struct t10_vpd83_header size '%zu'",
                         vpd_data_len, sizeof(struct t10_vpd83_header));
        goto out;
    }

    *dps = NULL;
    *dp_count = 0;

    vpd83_header = (struct t10_vpd83_header*) vpd_data;

    if (vpd83_header->page_code != _T10_SPC_VPD_DI) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "BUG: Got incorrect VPD page code '%02x', "
                         "should be 0x83", vpd83_header->page_code);
        goto out;
    }

    vpd83_len = ntohs(vpd83_header->page_len) + sizeof(struct t10_vpd83_header);

    end_p = vpd_data + vpd83_len - 1;
    p = vpd_data + sizeof(struct t10_vpd83_header);

    /* First loop find out how many id we got */
    while(p <= end_p) {
        if (p + sizeof(struct t10_vpd83_dp_header) > end_p) {
            rc = LSM_ERR_LIB_BUG;
            _lsm_err_msg_set(err_msg, "BUG: Illegal VPD 0x83 page data, "
                             "got partial designation descriptor.");
            goto out;
        }
        ++i;

        dp_header = (struct t10_vpd83_dp_header *) p;

        p += dp_header->designator_len + sizeof(struct t10_vpd83_dp_header);
        continue;
    }

    if (i == 0)
        goto out;

    *dps = (struct t10_vpd83_dp **) malloc(sizeof(struct t10_vpd83_dp*) * i);

    if (*dps == NULL) {
        rc = LSM_ERR_NO_MEMORY;
        goto out;
    }

    p = vpd_data + sizeof(struct t10_vpd83_header);

    while(*dp_count < i) {
        dp = _t10_vpd83_dp_new();
        if (dp == NULL) {
            rc = LSM_ERR_NO_MEMORY;
            goto out;
        }
        (*dps)[*dp_count] = dp;
        ++*dp_count;

        dp_header = (struct t10_vpd83_dp_header *) p;
        memcpy(&dp->header, dp_header, sizeof(struct t10_vpd83_dp_header));
        memcpy(dp->designator, p + sizeof(struct t10_vpd83_dp_header),
               dp_header->designator_len);

        p += dp_header->designator_len + sizeof(struct t10_vpd83_dp_header);
        continue;
    }

 out:
    if (rc != LSM_ERR_OK) {
        if (*dps != NULL) {
            _t10_vpd83_dp_array_free(*dps, *dp_count);
            *dps = NULL;
            *dp_count = 0;
        }
    }
    return rc;
}

int lsm_local_disk_vpd83_search(const char *vpd83,
                                lsm_string_list **disk_path_list,
                                lsm_error **lsm_err)
{
    int rc = LSM_ERR_OK;
    lsm_string_list *sd_name_list = NULL;
    uint32_t i = 0;
    const char *sd_name = NULL;
    char tmp_vpd83[_LSM_MAX_VPD83_ID_LEN];
    char sd_path[_MAX_SD_PATH_STR_LEN];
    bool sysfs_support = true;
    char err_msg[_LSM_ERR_MSG_LEN];

    _lsm_err_msg_clear(err_msg);

    rc = _check_null_ptr(err_msg, 3 /* argument count */, vpd83, disk_path_list,
                         lsm_err);

    if (rc != LSM_ERR_OK) {
        /* set output pointers to NULL if possible when facing error in case
         * application use output memory.
         */
        if (disk_path_list != NULL)
            *disk_path_list = NULL;

        goto out;
    }

    if (strlen(vpd83) >= _LSM_MAX_VPD83_ID_LEN) {
        _lsm_err_msg_set(err_msg, "Provided vpd83 string exceeded the maximum "
                         "string length for SCSI VPD83 NAA ID %d, current %zd",
                         _LSM_MAX_VPD83_ID_LEN - 1, strlen(vpd83));
        rc = LSM_ERR_INVALID_ARGUMENT;
        goto out;
    }


    *lsm_err = NULL;
    *disk_path_list = lsm_string_list_alloc(0 /* no pre-allocation */);
    if (*disk_path_list == NULL) {
        rc = LSM_ERR_NO_MEMORY;
        goto out;
    }

    _good(_sysfs_get_all_sd_names(err_msg, &sd_name_list), rc, out);

    _lsm_string_list_foreach(sd_name_list, i, sd_name) {
        if (sd_name == NULL)
            continue;
        if (sysfs_support == true) {
            rc = _sysfs_vpd83_naa_of_sd_name(err_msg, sd_name, tmp_vpd83);
            if (rc == LSM_ERR_NO_SUPPORT) {
                sysfs_support = false;
            } else if (rc == LSM_ERR_NOT_FOUND_DISK) {
                /* In case disk got removed after _sysfs_get_all_sd_names() */
                continue;
            }
            else if (rc != LSM_ERR_OK)
                break;
        }
        /* Try udev way if got NO_SUPPORT from sysfs way. */
        if (sysfs_support == false) {
            rc = _udev_vpd83_of_sd_name(err_msg, sd_name, tmp_vpd83);
            if (rc == LSM_ERR_NOT_FOUND_DISK)
                /* In case disk got removed after _sysfs_get_all_sd_names() */
                continue;
            else if (rc != LSM_ERR_OK)
                break;
        }
        if (strncmp(vpd83, tmp_vpd83, _LSM_MAX_VPD83_ID_LEN) == 0) {
            snprintf(sd_path, _MAX_SD_PATH_STR_LEN, _SD_PATH_FORMAT, sd_name);

            if (lsm_string_list_append(*disk_path_list, sd_path) != 0) {
                rc = LSM_ERR_NO_MEMORY;
                goto out;
            }
        }
    }

 out:
    if (sd_name_list != NULL)
        lsm_string_list_free(sd_name_list);

    if (rc == LSM_ERR_OK) {
        /* clean disk_path_list if nothing found */
        if (lsm_string_list_size(*disk_path_list) == 0) {
            lsm_string_list_free(*disk_path_list);
            *disk_path_list = NULL;
        }
    } else {
        /* Error found, clean up */

        if (lsm_err != NULL)
            *lsm_err = LSM_ERROR_CREATE_PLUGIN_MSG(rc, err_msg);

        if ((disk_path_list != NULL) && (*disk_path_list != NULL)) {
            lsm_string_list_free(*disk_path_list);
            *disk_path_list = NULL;
        }
    }

    return rc;
}

int lsm_local_disk_vpd83_get(const char *disk_path, char **vpd83,
                             lsm_error **lsm_err)
{
    char tmp_vpd83[_LSM_MAX_VPD83_ID_LEN];
    const char *sd_name = NULL;
    int rc = LSM_ERR_OK;
    char err_msg[_LSM_ERR_MSG_LEN];

    _lsm_err_msg_clear(err_msg);

    rc = _check_null_ptr(err_msg, 3 /* arg_count */, disk_path, vpd83, lsm_err);

    if (rc != LSM_ERR_OK) {
        if (vpd83 != NULL)
            *vpd83 = NULL;

        goto out;
    }

    if ((strlen(disk_path) <= strlen("/dev/")) ||
        (strncmp(disk_path, "/dev/", strlen("/dev/")) != 0)) {

        _lsm_err_msg_set(err_msg, "Invalid disk_path, should start with /dev/");
        rc = LSM_ERR_INVALID_ARGUMENT;
        goto out;
    }

    sd_name = disk_path + strlen("/dev/");
    if (strlen(sd_name) > _MAX_SD_NAME_STR_LEN) {
        rc = LSM_ERR_INVALID_ARGUMENT;
        _lsm_err_msg_set(err_msg, "Illegal disk_path string, the SCSI disk name "
                         "part(sdX) exceeded the max length %d, current %zd",
                         _MAX_SD_NAME_STR_LEN - 1, strlen(sd_name));
        goto out;
    }
    if (strncmp(sd_name, "sd", strlen("sd")) != 0) {
        rc = LSM_ERR_INVALID_ARGUMENT;
        _lsm_err_msg_set(err_msg, "Illegal disk_path string, should start with "
                         "'/dev/sd' as we only support SCSI or ATA disk "
                         "right now");
        goto out;
    }

    *vpd83 = NULL;
    *lsm_err = NULL;

    rc = _sysfs_vpd83_naa_of_sd_name(err_msg, sd_name, tmp_vpd83);
    if (rc == LSM_ERR_NO_SUPPORT)
        /* Try udev if kernel does not expose vpd83 */
        rc = _udev_vpd83_of_sd_name(err_msg, sd_name, tmp_vpd83);

    if (rc != LSM_ERR_OK)
        goto out;

    if (tmp_vpd83[0] != '\0') {
        *vpd83 = strdup(tmp_vpd83);
        if (*vpd83 == NULL)
            rc = LSM_ERR_NO_MEMORY;
    }

 out:
    if (rc != LSM_ERR_OK) {
        if (lsm_err != NULL)
            *lsm_err = LSM_ERROR_CREATE_PLUGIN_MSG(rc, err_msg);

        if (vpd83 != NULL) {
            free(*vpd83);
            *vpd83= NULL;
        }
    }

    return rc;
}

/*
 * Preconditions:
 *  vpd_0_len > 3
 *  vpd_0_data not NULL
 */
static bool _is_vpd_page_supported(uint8_t *vpd_0_data, uint16_t vpd_0_len,
                                   uint8_t page_code)
{
    uint16_t supported_list_len = 0;
    uint16_t i = 0;

    assert(vpd_0_len > 3);
    assert(vpd_0_data != NULL);

    supported_list_len = (vpd_0_data[2] << 8) + vpd_0_data[3];

    for (; (i < supported_list_len) &&
           ((i + _T10_SPC_VPD_SUP_VPD_PGS_LIST_OFFSET) < vpd_0_len); ++i) {
        if (page_code == vpd_0_data[i + _T10_SPC_VPD_SUP_VPD_PGS_LIST_OFFSET])
            return true;
    }
    return false;
}

/*
 * Preconditions:
 *  err_msg != NULL
 *  fd >= 0
 *  vpd_data != NULL
 *  vpd_data_len > 0
 */
static int _sg_io_ioctl_vpd(char *err_msg, int fd, uint8_t page_code,
                            uint8_t *vpd_data, uint16_t vpd_data_len)
{
    int rc = LSM_ERR_OK;
    uint8_t vpd_00_data[_T10_SPC_VPD_SUP_VPD_PGS_MAX_PAGE_LEN];
    uint8_t cdb[_T10_SPC_INQUERY_CMD_LEN];
    struct sg_io_hdr io_hdr;
    int ioctl_rc = 0;
    int ioctl_errno = 0;
    int rc_vpd_00 = 0;
    char strerr_buff[_LSM_ERR_MSG_LEN];

    assert(err_msg != NULL);
    assert(fd >= 0);
    assert(vpd_data != NULL);
    assert(vpd_data_len > 0);

    /* SPC-5 Table 142 - INQUIRY command */
    cdb[0] = INQUIRY;
    /* ^ OPERATION CODE */
    cdb[1] = 1;
    /* ^  EVPD, VPD INQUIRY require EVPD == 1 */;
    cdb[2] = page_code & UINT8_MAX;
    /* ^ PAGE CODE */
    cdb[3] = (vpd_data_len >> 8 )& UINT8_MAX;
    /* ^ ALLOCATION LENGTH, MSB */
    cdb[4] = vpd_data_len & UINT8_MAX;
    /* ^ ALLOCATION LENGTH, LSB */
    cdb[5] = 0;
    /* ^ CONTROL, no need to handle auto contingent allegiance(ACA) */

    memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
    memset(vpd_data, 0, (size_t) vpd_data_len);
    io_hdr.interface_id = 'S'; /* 'S' for SCSI generic */
    io_hdr.cmd_len = _T10_SPC_INQUERY_CMD_LEN;
    io_hdr.sbp = NULL; /* No need to parse sense data */
    io_hdr.mx_sb_len = 0;
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = vpd_data_len;
    io_hdr.dxferp = vpd_data;
    io_hdr.cmdp = cdb;
    io_hdr.timeout = _VPD_QUERY_TMO;

    ioctl_rc = ioctl(fd, SG_IO, &io_hdr);
    if (ioctl_rc != 0) {
        ioctl_errno = errno;
        if (page_code == _T10_SPC_VPD_SUP_VPD_PGS) {
            _lsm_err_msg_set(err_msg, "Not a SCSI compatible device");
            return LSM_ERR_NO_SUPPORT;
        }
        /* Check whether provided page is supported */
        rc_vpd_00 = _sg_io_ioctl_vpd(err_msg, fd, _T10_SPC_VPD_SUP_VPD_PGS,
                                     vpd_00_data,
                                     _T10_SPC_VPD_SUP_VPD_PGS_MAX_PAGE_LEN);
        if (rc_vpd_00 != 0) {
            rc = LSM_ERR_NO_SUPPORT;
            goto out;
        }

        if (_is_vpd_page_supported(vpd_00_data,
                                   _T10_SPC_VPD_SUP_VPD_PGS_MAX_PAGE_LEN,
                                   page_code) == true) {
            /* Current VPD page is supported, then it's a library bug */
            rc = LSM_ERR_LIB_BUG;
            _lsm_err_msg_set(err_msg,
                             "BUG: VPD page 0x%02x is supported, "
                             "but failed with error %d(%s)", ioctl_rc,
                             strerror_r(ioctl_errno, strerr_buff, _LSM_ERR_MSG_LEN));
            goto out;

        }

        rc = LSM_ERR_NO_SUPPORT;

        _lsm_err_msg_set(err_msg, "SCSI VPD 0x%02x page is not supported",
                         page_code);
        goto out;
    }

 out:
    if (rc != LSM_ERR_OK)
        memset(vpd_data, 0, (size_t) vpd_data_len);

    return rc;
}

/*
 * Preconditions:
 *  err_msg != NULL
 *  sd_path != NULL
 *  fd != NULL
 */
static int _sg_io_ioctl_open(char *err_msg, const char *sd_path, int *fd)
{
    int rc = LSM_ERR_OK;
    char strerr_buff[_LSM_ERR_MSG_LEN];

    assert(err_msg != NULL);
    assert(sd_path != NULL);
    assert(fd != NULL);

    *fd = open(sd_path, O_RDONLY|O_NONBLOCK);
    if (*fd < 0) {
        switch(errno) {
        case ENOENT:
            rc = LSM_ERR_NOT_FOUND_DISK;
            _lsm_err_msg_set(err_msg, "Disk %s not found", sd_path);
            goto out;
        case EACCES:
            rc = LSM_ERR_PERMISSION_DENIED;
            _lsm_err_msg_set(err_msg, "Permission denied: Cannot open %s "
                             "with O_RDONLY and O_NONBLOCK flag", sd_path);
            goto out;
        default:
            rc = LSM_ERR_LIB_BUG;
            _lsm_err_msg_set(err_msg, "BUG: Failed to open %s, error: %d, %s",
                             sd_path, errno,
                             strerror_r(errno, strerr_buff, _LSM_ERR_MSG_LEN));
        }
    }
 out:
    return rc;
}

int lsm_local_disk_rpm_get(const char *sd_path, int32_t *rpm,
                           lsm_error **lsm_err)
{
    uint8_t vpd_data[_T10_SBC_VPD_BLK_DEV_CHA_MAX_PAGE_LEN];
    int fd = -1;
    char err_msg[_LSM_ERR_MSG_LEN];
    int rc = LSM_ERR_OK;
    struct t10_sbc_vpd_bdc *bdc = NULL;

    rc = _check_null_ptr(err_msg, 3 /* arg_count */, sd_path, rpm, lsm_err);
    if (rc != LSM_ERR_OK) {
        goto out;
    }

    _lsm_err_msg_clear(err_msg);

    rc = _sg_io_ioctl_open(err_msg, sd_path, &fd);
    if (rc != LSM_ERR_OK)
        goto out;

    rc = _sg_io_ioctl_vpd(err_msg, fd, _T10_SBC_VPD_BLK_DEV_CHA,  vpd_data,
                          _T10_SBC_VPD_BLK_DEV_CHA_MAX_PAGE_LEN);
    if (rc != LSM_ERR_OK)
        goto out;
    bdc = (struct t10_sbc_vpd_bdc *) vpd_data;
    if (bdc->pg_code != _T10_SBC_VPD_BLK_DEV_CHA) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "Got corrupted SCSI SBC "
                         "Device Characteristics VPD page, expected page code "
                         "is %d but got % " PRIu8 "",
                         _T10_SBC_VPD_BLK_DEV_CHA, bdc->pg_code);
        goto out;
    }

    *rpm = be16toh(bdc->medium_rotation_rate_be);
    if (((*rpm >= 2) && (*rpm <= 0x400)) || (*rpm == 0xffff) ||
        (*rpm == _T10_SBC_MEDIUM_ROTATION_NO_SUPPORT))
        *rpm = LSM_DISK_RPM_UNKNOWN;

    if (*rpm == _T10_SBC_MEDIUM_ROTATION_SSD)
        *rpm = LSM_DISK_RPM_NON_ROTATING_MEDIUM;

 out:
    if (fd >= 0)
        close(fd);

    if (rc != LSM_ERR_OK) {
        if (lsm_err != NULL)
            *lsm_err = LSM_ERROR_CREATE_PLUGIN_MSG(rc, err_msg);
        if (rpm != NULL)
            *rpm = LSM_DISK_RPM_UNKNOWN;
    }

    return rc;
}
