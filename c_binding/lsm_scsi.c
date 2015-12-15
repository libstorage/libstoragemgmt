/*
 * Copyright (C) 2015 Red Hat, Inc.
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

#include "libstoragemgmt/libstoragemgmt.h"
#include "libstoragemgmt/libstoragemgmt_error.h"
#include "libstoragemgmt/libstoragemgmt_plug_interface.h"

#define _MAX_VPD83_PAGE_LEN 0xff + 4
#define _MAX_VPD83_NAA_ID_LEN 33
/* Max one is 6h IEEE Registered Extended ID which it 32 bits hex string.
 */
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

#define _T10_VPD83_NAA_235_ID_LEN 8
#define _T10_VPD83_NAA_6_ID_LEN 16
#define _T10_VPD83_PAGE_CODE 0x83
#define _T10_VPD83_DESIGNATOR_TYPE_NAA 0x3
#define _T10_VPD83_NAA_TYPE_2 0x2
#define _T10_VPD83_NAA_TYPE_3 0x3
#define _T10_VPD83_NAA_TYPE_5 0x5
#define _T10_VPD83_NAA_TYPE_6 0x6

#define _LSM_ERR_MSG_LEN 255

#pragma pack(1)
/*
 * Table 589 — Device Identification VPD page
 */
struct t10_vpd83_header {
    uint8_t dev_type : 5;
    /* ^ PERIPHERAL DEVICE TYPE */
    uint8_t qualifier : 3;
    /* PERIPHERAL QUALIFIER */
    uint8_t page_code;
    uint8_t page_len_msb;
    uint8_t page_len_lsb;
};

/*
 * Table 590 — Designation descriptor
 */
struct t10_vpd83_id_header {
    uint8_t code_set        : 4;
    uint8_t protocol_id     : 4;
    uint8_t designator_type : 4;
    uint8_t association     : 2;
    uint8_t reserved_1      : 1;
    uint8_t piv             : 1;
    uint8_t reserved_2;
    uint8_t len;
};

struct t10_vpd83_naa_header {
    uint8_t data_msb : 4;
    uint8_t naa_type : 4;
};

#pragma pack()

#define _lsm_err_msg_clear(err_msg) memset(err_msg, 0, _LSM_ERR_MSG_LEN)

#define _lsm_err_msg_set(err_msg, format, ...) \
    snprintf(err_msg, _LSM_ERR_MSG_LEN, format, ##__VA_ARGS__)

#define _lsm_string_list_foreach(l, i, d) \
    for(i = 0; \
        (l != NULL) && (i < lsm_string_list_size(l)) && \
        (d = lsm_string_list_elem_get(l, i)); \
        ++i)

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
        _lsm_err_msg_set(err_msg, "Failed to open %s, error: %d, %s",
                         path, errno, strerror(errno));
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

    *size = 0;
    memset(buff, 0, max_size);

    fd = open(sys_fs_path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT)
            return LSM_ERR_NO_SUPPORT;

        _lsm_err_msg_set(err_msg, "_sysfs_read_file(): Failed to open %s, "
                         "error: %d, %s", sys_fs_path, errno, strerror(errno));
        return LSM_ERR_LIB_BUG;
    }
    *size = read(fd, buff, max_size);
    close(fd);

    if (*size < 0) {
        _lsm_err_msg_set(err_msg, "Failed to read %s, error: %d, %s",
                         sys_fs_path, errno, strerror(errno));
        return LSM_ERR_LIB_BUG;
    }
    return LSM_ERR_OK;
}

/*
 * Parse _SYSFS_VPD83_PATH_FORMAT file for VPD83 NAA ID.
 * When no such sysfs file found, return LSM_ERR_NO_SUPPORT.
 * When VPD83 page does not have NAA ID, return LSM_ERR_OK and vpd83 as empty
 * string.
 *
 * Input *vpd83 should be char[_MAX_VPD83_NAA_ID_LEN], assuming caller did
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
    uint8_t *end_p = NULL;
    uint8_t *p = NULL;
    struct t10_vpd83_header *vpd83_header = NULL;
    uint32_t vpd83_len = 0;
    struct t10_vpd83_id_header *id_header = NULL;
    struct t10_vpd83_naa_header *naa_header = NULL;
    int rc = LSM_ERR_OK;
    uint8_t buff[_MAX_VPD83_PAGE_LEN];
    char sysfs_path[_MAX_SYSFS_VPD83_PATH_STR_LEN];
    char sysfs_blk_path[_MAX_SYSFS_BLK_PATH_STR_LEN];

    memset(vpd83, 0, _MAX_VPD83_NAA_ID_LEN);

    if (sd_name == NULL) {
        _lsm_err_msg_set(err_msg, "_sysfs_vpd83_naa_of_sd_name(): "
                         "Input sd_name argument is NULL");
        rc = LSM_ERR_LIB_BUG;
        goto out;
    }

    /*
     * Check the existence of disk vis /sys/block/sdX folder.
     */
    snprintf(sysfs_blk_path, _MAX_SYSFS_BLK_PATH_STR_LEN,
             _SYSFS_BLK_PATH_FORMAT, sd_name);
    if (_file_is_exist(err_msg, sysfs_blk_path) == false) {
        rc = LSM_ERR_NOT_FOUND_DISK;
        goto out;
    }

    snprintf(sysfs_path, _MAX_SYSFS_VPD83_PATH_STR_LEN,
             _SYSFS_VPD83_PATH_FORMAT, sd_name);

    rc = _sysfs_read_file(err_msg, sysfs_path, buff, &read_size,
                          _MAX_VPD83_PAGE_LEN);

    if (rc != LSM_ERR_OK)
        goto out;

    /* Return NULL and LSM_ERR_OK when got invalid sysfs file */
    /* Read size is smaller than VPD83 header */
    if (read_size < sizeof(struct t10_vpd83_header))
        goto out;

    vpd83_header = (struct t10_vpd83_header*) buff;

    /* Incorrect page code */
    if (vpd83_header->page_code != _T10_VPD83_PAGE_CODE)
        goto out;

    vpd83_len = (((uint32_t) vpd83_header->page_len_msb) << 8) +
        vpd83_header->page_len_lsb + sizeof(struct t10_vpd83_header);

    end_p = buff + vpd83_len - 1;
    p = buff + sizeof(struct t10_vpd83_header);


    while(p <= end_p) {
        /* Corrupted data: facing data end */
        if (p + sizeof(struct t10_vpd83_id_header) > end_p)
            goto out;

        id_header = (struct t10_vpd83_id_header *) p;
        /* Skip non-NAA ID */
        if (id_header->designator_type != _T10_VPD83_DESIGNATOR_TYPE_NAA)
            goto next_one;

        naa_header = (struct t10_vpd83_naa_header *)
            ((uint8_t *)id_header + sizeof(struct t10_vpd83_id_header));

        switch(naa_header->naa_type) {
        case _T10_VPD83_NAA_TYPE_2:
        case _T10_VPD83_NAA_TYPE_3:
        case _T10_VPD83_NAA_TYPE_5:
            _be_raw_to_hex((uint8_t *) naa_header, _T10_VPD83_NAA_235_ID_LEN,
                           vpd83);
            break;
        case _T10_VPD83_NAA_TYPE_6:
            _be_raw_to_hex((uint8_t *) naa_header, _T10_VPD83_NAA_6_ID_LEN,
                           vpd83);
            break;
        default:
            /* Skip for Unknown NAA ID type */
            goto next_one;
        }
        /* Quit when found first NAA ID */
        if (vpd83[0] != 0)
            break;

     next_one:
        p = (uint8_t *) id_header + id_header->len +
            sizeof(struct t10_vpd83_id_header);
        continue;
    }

 out:
    return rc;
}

/*
 * Try to parse /sys/block/sda/device/vpd_pg83 for VPD83 NAA ID first.
 * This sysfs file is missing in some older kernel(like RHEL6), we use udev
 * ID_WWN_WITH_EXTENSION property instead, even through ID_WWN_WITH_EXTENSION
 * does not mean VPD83 NAA ID, this is the only workaround I could found for
 * old system without root privilege.
 * Input *vpd83 should be char[_MAX_VPD83_NAA_ID_LEN], assuming caller did
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

    memset(vpd83, 0, _MAX_VPD83_NAA_ID_LEN);
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

    snprintf(vpd83, _MAX_VPD83_NAA_ID_LEN, wwn);

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

    *sd_name_list = lsm_string_list_alloc(0 /* no pre-allocation */);
    if (*sd_name_list == NULL) {
        return LSM_ERR_NO_MEMORY;
    }

    dir = opendir(_SYS_BLOCK_PATH);
    if (dir == NULL) {
        _lsm_err_msg_set(err_msg, "Cannot open %s: error (%d)%s",
                         _SYS_BLOCK_PATH, errno, strerror(errno));
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
                             _SYS_BLOCK_PATH, errno, strerror(errno));
            rc = LSM_ERR_LIB_BUG;
        }
    }

    if (rc != LSM_ERR_OK) {
        lsm_string_list_free(*sd_name_list);
        *sd_name_list = NULL;
    }
    return rc;
}

int lsm_scsi_disk_paths_of_vpd83(const char *vpd83,
                                 lsm_string_list **sd_path_list,
                                 lsm_error **lsm_err)
{
    int rc = LSM_ERR_OK;
    lsm_string_list *sd_name_list = NULL;
    uint32_t i = 0;
    const char *sd_name = NULL;
    char tmp_vpd83[_MAX_VPD83_NAA_ID_LEN];
    char sd_path[_MAX_SD_PATH_STR_LEN];
    bool sysfs_support = true;
    char err_msg[_LSM_ERR_MSG_LEN];

    _lsm_err_msg_clear(err_msg);

    rc = _check_null_ptr(err_msg, 3 /* argument count */, vpd83, sd_path_list,
                         lsm_err);

    if (rc != LSM_ERR_OK) {
        /* set output pointers to NULL if possible when facing error in case
         * application use output memory.
         */
        if (sd_path_list != NULL)
            *sd_path_list = NULL;

        goto out;
    }

    if (strlen(vpd83) >= _MAX_VPD83_NAA_ID_LEN) {
        _lsm_err_msg_set(err_msg, "Provided vpd83 string exceeded the maximum "
                         "string length for SCSI VPD83 NAA ID %d, current %zd",
                         _MAX_VPD83_NAA_ID_LEN - 1, strlen(vpd83));
        rc = LSM_ERR_INVALID_ARGUMENT;
        goto out;
    }


    *lsm_err = NULL;
    *sd_path_list = lsm_string_list_alloc(0 /* no pre-allocation */);
    if (*sd_path_list == NULL) {
        rc = LSM_ERR_NO_MEMORY;
        goto out;
    }

    rc = _sysfs_get_all_sd_names(err_msg, &sd_name_list);
    if (rc != LSM_ERR_OK)
        goto out;

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
        if (strncmp(vpd83, tmp_vpd83, _MAX_VPD83_NAA_ID_LEN) == 0) {
            snprintf(sd_path, _MAX_SD_PATH_STR_LEN, _SD_PATH_FORMAT, sd_name);

            if (lsm_string_list_append(*sd_path_list, sd_path) != 0) {
                rc = LSM_ERR_NO_MEMORY;
                goto out;
            }
        }
    }

 out:
    if (sd_name_list != NULL)
        lsm_string_list_free(sd_name_list);

    if (rc == LSM_ERR_OK) {
        /* clean sd_path_list if nothing found */
        if (lsm_string_list_size(*sd_path_list) == 0) {
            lsm_string_list_free(*sd_path_list);
            *sd_path_list = NULL;
        }
    } else {
        /* Error found, clean up */

        if (lsm_err != NULL)
            *lsm_err = LSM_ERROR_CREATE_PLUGIN_MSG(rc, err_msg);

        if ((sd_path_list != NULL) && (*sd_path_list != NULL)) {
            lsm_string_list_free(*sd_path_list);
            *sd_path_list = NULL;
        }
    }

    return rc;
}

int lsm_scsi_vpd83_of_disk_path(const char *sd_path, const char **vpd83,
                                lsm_error **lsm_err)
{
    char tmp_vpd83[_MAX_VPD83_NAA_ID_LEN];
    const char *sd_name = NULL;
    int rc = LSM_ERR_OK;
    char err_msg[_LSM_ERR_MSG_LEN];

    _lsm_err_msg_clear(err_msg);

    rc = _check_null_ptr(err_msg, 3 /* arg_count */, sd_path, vpd83, lsm_err);

    if (rc != LSM_ERR_OK) {
        if (vpd83 != NULL)
            *vpd83 = NULL;

        goto out;
    }

    if ((strlen(sd_path) <= strlen("/dev/")) ||
        (strncmp(sd_path, "/dev/", strlen("/dev/")) != 0)) {

        _lsm_err_msg_set(err_msg, "Invalid sd_path, should start with /dev/");
        rc = LSM_ERR_INVALID_ARGUMENT;
        goto out;
    }

    sd_name = sd_path + strlen("/dev/");
    if (strlen(sd_name) > _MAX_SD_NAME_STR_LEN) {
        rc = LSM_ERR_INVALID_ARGUMENT;
        _lsm_err_msg_set(err_msg, "Illegal sd_path string, the SCSI disk name "
                         "part(sdX) exceeded the max length %d, current %d",
                         _MAX_SD_NAME_STR_LEN - 1, strlen(sd_name));
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
            free((char *) *vpd83);
            *vpd83= NULL;
        }
    }

    return rc;
}
