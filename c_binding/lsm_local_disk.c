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
#include <assert.h>
#include <endian.h>

#include "libstoragemgmt/libstoragemgmt.h"
#include "libstoragemgmt/libstoragemgmt_error.h"
#include "libstoragemgmt/libstoragemgmt_plug_interface.h"
#include "utils.h"
#include "libsg.h"
#include "libses.h"

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
#define _SYSFS_SAS_ADDR_LEN                     _SG_T10_SPL_SAS_ADDR_LEN + 2
/* ^ Only Linux sysfs entry /sys/block/sdx/device/sas_address which
 *   format is '0x<hex_addr>\0'
 */

#pragma pack(push, 1)
struct t10_sbc_vpd_bdc {
    uint8_t we_dont_care_0;
    uint8_t pg_code;
    uint16_t len_be;
    uint16_t medium_rotation_rate_be;
    uint8_t we_dont_care_1[58];
};
/* ^ SBC-4 rev 09 Table 236 - Block Device Characteristics VPD page */

#pragma pack(pop)

static int _sysfs_vpd83_naa_of_sd_name(char *err_msg, const char *sd_name,
                                       char *vpd83);
static int _udev_vpd83_of_sd_name(char *err_msg, const char *sd_name,
                                  char *vpd83);
static int _sysfs_vpd_pg83_data_get(char *err_msg, const char *sd_name,
                                    uint8_t *vpd_data, ssize_t *read_size);
/*
 * Use /sys/block/sdx/device/sas_address to retrieve sas address of certain
 * disk.
 * 'tp_sas_addr' should be char[_SG_T10_SPL_SAS_ADDR_LEN].
 * Return 0 if file exists and the SAS address is legal, return -1 otherwise.
 * Legal here means:
 *  * sysfs file content has strlen as _SYSFS_SAS_ADDR_LEN
 *  * sysfs file content start with '0x'.
 */
static int _sysfs_sas_addr_get(const char *blk_name, char *tp_sas_addr);

static int _ses_ctrl(const char *disk_path, lsm_error **lsm_err,
                     int action, int action_type);

static const char *_sd_name_of(const char *disk_path);

/*
 * Retrieve the content of /sys/block/sda/device/vpd_pg83 file.
 * No argument checker here, assume all non-NULL and vpd_data is
                * char[_SG_T10_SPC_VPD_MAX_LEN]
 */
static int _sysfs_vpd_pg83_data_get(char *err_msg, const char *sd_name,
                                    uint8_t *vpd_data, ssize_t *read_size)
{
    int file_rc = 0;
    char sysfs_path[_MAX_SYSFS_VPD83_PATH_STR_LEN];
    char sysfs_blk_path[_MAX_SYSFS_BLK_PATH_STR_LEN];
    char strerr_buff[_LSM_ERR_MSG_LEN];

    memset(vpd_data, 0, _SG_T10_SPC_VPD_MAX_LEN);

    /*
     * Check the existence of disk vis /sys/block/sdX folder.
     */
    snprintf(sysfs_blk_path, _MAX_SYSFS_BLK_PATH_STR_LEN,
             _SYSFS_BLK_PATH_FORMAT, sd_name);
    if (! _file_exists(sysfs_blk_path)) {
        _lsm_err_msg_set(err_msg, "Disk %s not found", sd_name);
        return LSM_ERR_NOT_FOUND_DISK;
    }

    snprintf(sysfs_path, _MAX_SYSFS_VPD83_PATH_STR_LEN,
             _SYSFS_VPD83_PATH_FORMAT, sd_name);

    file_rc = _read_file(sysfs_path, vpd_data, read_size,
                         _SG_T10_SPC_VPD_MAX_LEN);
    if (file_rc != 0) {
        if (errno == ENOENT) {
            _lsm_err_msg_set(err_msg, "File '%s' not exist", sysfs_path);
            return LSM_ERR_NO_SUPPORT;
        } else {
            _lsm_err_msg_set(err_msg, "BUG: Unknown error %d(%s) from "
                             "_read_file().", file_rc,
                             strerror_r(file_rc, strerr_buff,
                                        _LSM_ERR_MSG_LEN));
            return LSM_ERR_LIB_BUG;
        }
    }
    return LSM_ERR_OK;
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
    struct _sg_t10_vpd83_naa_header *naa_header = NULL;
    int rc = LSM_ERR_OK;
    uint8_t vpd_data[_SG_T10_SPC_VPD_MAX_LEN];
    struct _sg_t10_vpd83_dp **dps = NULL;
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

    _good(_sg_parse_vpd_83(err_msg, vpd_data, &dps, &dp_count), rc, out);

    for (; i < dp_count; ++i) {
        if ((dps[i]->header.designator_type ==
             _SG_T10_SPC_VPD_DI_DESIGNATOR_TYPE_NAA)  &&
            (dps[i]->header.association ==
             _SG_T10_SPC_VPD_DI_ASSOCIATION_LUN)) {
            naa_header = (struct _sg_t10_vpd83_naa_header *) dps[i]->designator;
            switch(naa_header->naa_type) {
            case _SG_T10_SPC_VPD_DI_NAA_TYPE_2:
            case _SG_T10_SPC_VPD_DI_NAA_TYPE_3:
            case _SG_T10_SPC_VPD_DI_NAA_TYPE_5:
                _be_raw_to_hex((uint8_t *) naa_header,
                               _SG_T10_SPC_VPD_DI_NAA_235_ID_LEN, vpd83);
                break;
            case _SG_T10_SPC_VPD_DI_NAA_TYPE_6:
                _be_raw_to_hex((uint8_t *) naa_header,
                               _SG_T10_SPC_VPD_DI_NAA_6_ID_LEN, vpd83);
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
        _sg_t10_vpd83_dp_array_free(dps, dp_count);
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

    snprintf(vpd83, _LSM_MAX_VPD83_ID_LEN, "%s", wwn);

 out:
    if (udev != NULL)
        udev_unref(udev);

    if (sd_udev != NULL)
        udev_device_unref(sd_udev);

    return rc;
}

int lsm_local_disk_vpd83_search(const char *vpd83,
                                lsm_string_list **disk_path_list,
                                lsm_error **lsm_err)
{
    int rc = LSM_ERR_OK;
    uint32_t i = 0;
    const char *sd_name = NULL;
    const char *disk_path = NULL;
    char tmp_vpd83[_LSM_MAX_VPD83_ID_LEN];
    bool sysfs_support = true;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_string_list *disk_paths = NULL;
    lsm_error *tmp_lsm_err = NULL;

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

    rc = lsm_local_disk_list(&disk_paths, &tmp_lsm_err);
    if (rc != LSM_ERR_OK) {
        snprintf(err_msg, _LSM_ERR_MSG_LEN, "%s",
                 lsm_error_message_get(tmp_lsm_err));
        lsm_error_free(tmp_lsm_err);
        goto out;
    }

    _lsm_string_list_foreach(disk_paths, i, disk_path) {
        sd_name = _sd_name_of(disk_path);
        if (sd_name == NULL)
            continue;

        if (sysfs_support == true) {
            rc = _sysfs_vpd83_naa_of_sd_name(err_msg, sd_name, tmp_vpd83);
            if (rc == LSM_ERR_NO_SUPPORT) {
                sysfs_support = false;
            } else if (rc == LSM_ERR_NOT_FOUND_DISK) {
                /* In case disk got removed after lsm_local_disk_list() */
                continue;
            }
            else if (rc != LSM_ERR_OK)
                break;
        }
        /* Try udev way if got NO_SUPPORT from sysfs way. */
        if (sysfs_support == false) {
            rc = _udev_vpd83_of_sd_name(err_msg, sd_name, tmp_vpd83);
            if (rc == LSM_ERR_NOT_FOUND_DISK)
                /* In case disk got removed after lsm_local_disk_list() */
                continue;
            else if (rc != LSM_ERR_OK)
                break;
        }
        if (strncmp(vpd83, tmp_vpd83, _LSM_MAX_VPD83_ID_LEN) == 0) {
            if (lsm_string_list_append(*disk_path_list, disk_path) != 0) {
                rc = LSM_ERR_NO_MEMORY;
                goto out;
            }
        }
    }

 out:
    if (disk_paths != NULL)
        lsm_string_list_free(disk_paths);

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

static const char *_sd_name_of(const char *disk_path)
{
    assert(disk_path != NULL);

    if (strncmp(disk_path, "/dev/sd", strlen("/dev/sd")) == 0)
        return disk_path + strlen("/dev/");
    return NULL;
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

    *vpd83 = NULL;
    *lsm_err = NULL;

    if (! _file_exists(disk_path)) {
        rc = LSM_ERR_NOT_FOUND_DISK;
        _lsm_err_msg_set(err_msg, "Disk %s not found", disk_path);
        goto out;
    }

    if (strncmp(disk_path, "/dev/sd", strlen("/dev/sd")) != 0) {
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg, "Only support disk path start with "
                         "'/dev/sd' yet");
        goto out;
    }

    sd_name = disk_path + strlen("/dev/");

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

int lsm_local_disk_rpm_get(const char *disk_path, int32_t *rpm,
                           lsm_error **lsm_err)
{
    uint8_t vpd_data[_SG_T10_SPC_VPD_MAX_LEN];
    int fd = -1;
    char err_msg[_LSM_ERR_MSG_LEN];
    int rc = LSM_ERR_OK;
    struct t10_sbc_vpd_bdc *bdc = NULL;

    rc = _check_null_ptr(err_msg, 3 /* arg_count */, disk_path, rpm, lsm_err);
    if (rc != LSM_ERR_OK) {
        goto out;
    }

    _lsm_err_msg_clear(err_msg);

    _good(_sg_io_open_ro(err_msg, disk_path, &fd), rc, out);
    _good(_sg_io_vpd(err_msg, fd, _SG_T10_SBC_VPD_BLK_DEV_CHA,  vpd_data),
          rc, out);

    bdc = (struct t10_sbc_vpd_bdc *) vpd_data;
    if (bdc->pg_code != _SG_T10_SBC_VPD_BLK_DEV_CHA) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "Got corrupted SCSI SBC "
                         "Device Characteristics VPD page, expected page code "
                         "is %d but got %" PRIu8 "",
                         _SG_T10_SBC_VPD_BLK_DEV_CHA, bdc->pg_code);
        goto out;
    }

    *rpm = be16toh(bdc->medium_rotation_rate_be);
    if (((*rpm >= 2) && (*rpm <= 0x400)) || (*rpm == 0xffff) ||
        (*rpm == _SG_T10_SBC_MEDIUM_ROTATION_NO_SUPPORT))
        *rpm = LSM_DISK_RPM_NO_SUPPORT;

    if (*rpm == _SG_T10_SBC_MEDIUM_ROTATION_SSD)
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

int lsm_local_disk_list(lsm_string_list **disk_paths, lsm_error **lsm_err)
{
    struct udev *udev = NULL;
    struct udev_enumerate *udev_enum = NULL;
    struct udev_list_entry *udev_devs = NULL;
    struct udev_list_entry *udev_list = NULL;
    struct udev_device *udev_dev = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    int udev_rc = 0;
    const char *udev_path = NULL;
    const char *disk_path = NULL;
    int rc = LSM_ERR_OK;

    _lsm_err_msg_clear(err_msg);

    rc = _check_null_ptr(err_msg, 2 /* argument count */, disk_paths, lsm_err);
    if (rc != LSM_ERR_OK) {
        /* set output pointers to NULL if possible when facing error in case
         * application use output memory.
         */
        if (disk_paths != NULL)
            *disk_paths = NULL;
        goto out;
    }

    *disk_paths = lsm_string_list_alloc(0 /* no pre-allocation */);
    if (*disk_paths == NULL) {
        rc = LSM_ERR_NO_MEMORY;
        goto out;
    }

    udev = udev_new();
    if (udev == NULL) {
        rc = LSM_ERR_NO_MEMORY;
        goto out;
    }
    udev_enum = udev_enumerate_new(udev);
    if (udev_enum == NULL) {
        rc = LSM_ERR_NO_MEMORY;
        goto out;
    }

    udev_rc = udev_enumerate_add_match_subsystem(udev_enum, "block");
    if (udev_rc != 0) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "udev_enumerate_scan_subsystems() failed "
                         "with %d", udev_rc);
        goto out;
    }
    udev_rc = udev_enumerate_add_match_property(udev_enum, "DEVTYPE", "disk");
    if (udev_rc != 0) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "udev_enumerate_add_match_property() failed "
                         "with %d", udev_rc);
        goto out;
    }

    udev_rc = udev_enumerate_scan_devices(udev_enum);
    if (udev_rc != 0) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "udev_enumerate_scan_devices() failed "
                         "with %d", udev_rc);
        goto out;
    }

    udev_devs = udev_enumerate_get_list_entry(udev_enum);
    if (udev_devs == NULL)
        goto out;

    udev_list_entry_foreach(udev_list, udev_devs) {
        udev_path = udev_list_entry_get_name(udev_list);
        if (udev_path == NULL)
            continue;
        udev_dev = udev_device_new_from_syspath(udev, udev_path);
        if (udev_dev == NULL) {
            rc = LSM_ERR_NO_MEMORY;
            goto out;
        }
        disk_path = udev_device_get_devnode(udev_dev);
        if (disk_path == NULL) {
            udev_device_unref(udev_dev);
            continue;
        }
        if ((strncmp(disk_path, "/dev/sd", strlen("/dev/sd")) == 0) ||
            (strncmp(disk_path, "/dev/nvme", strlen("/dev/nvme")) == 0)) {

            if (_file_exists(disk_path)) {
                rc = lsm_string_list_append(*disk_paths, disk_path);
                if (rc != LSM_ERR_OK) {
                    udev_device_unref(udev_dev);
                    goto out;
                }
            }
        }
        udev_device_unref(udev_dev);
    }


 out:
    if (udev != NULL)
        udev_unref(udev);

    if (udev_enum != NULL)
        udev_enumerate_unref(udev_enum);

    if (rc != LSM_ERR_OK) {
        if ((disk_paths != NULL) && (*disk_paths != NULL)) {
            lsm_string_list_free(*disk_paths);
            *disk_paths = NULL;
        }
        if (lsm_err != NULL)
            *lsm_err = LSM_ERROR_CREATE_PLUGIN_MSG(rc, err_msg);
    }
    return rc;
}

/* Workflow:
 *  * Query VPD supported pages, if ATA Information page is supported, then
 *     we got a ATA.
 *    # We check this first as when SATA disk connected to a SAS enclosure
 *    # then its VPD device id page will include SAS PROTOCOL IDENTIFIER as
 *    # target port.
 *
 *  * Check VPD device ID page, seeking ASSOCIATION == 01b,
 *    check PROTOCOL IDENTIFIER
 */
int lsm_local_disk_link_type_get(const char *disk_path,
                                 lsm_disk_link_type *link_type,
                                 lsm_error **lsm_err)
{
    unsigned char vpd_sup_data[_SG_T10_SPC_VPD_MAX_LEN];
    unsigned char vpd_di_data[_SG_T10_SPC_VPD_MAX_LEN];
    int fd = -1;
    char err_msg[_LSM_ERR_MSG_LEN];
    int rc = LSM_ERR_OK;
    struct _sg_t10_vpd83_dp **dps = NULL;
    uint16_t dp_count = 0;
    uint8_t protocol_id = _SG_T10_SPC_PROTOCOL_ID_OBSOLETE;
    uint16_t i = 0;

    _lsm_err_msg_clear(err_msg);

    _good(_check_null_ptr(err_msg, 3 /* arg_count */, disk_path, link_type,
                         lsm_err),
          rc, out);

    *link_type = LSM_DISK_LINK_TYPE_NO_SUPPORT;
    *lsm_err = NULL;

    _good(_sg_io_open_ro(err_msg, disk_path, &fd), rc, out);
    _good(_sg_io_vpd(err_msg, fd, _SG_T10_SPC_VPD_SUP_VPD_PGS, vpd_sup_data),
          rc, out);

    if (_sg_is_vpd_page_supported(vpd_sup_data,
                                  _SG_T10_SPC_VPD_ATA_INFO) == true) {
        *link_type = LSM_DISK_LINK_TYPE_ATA;
        goto out;
    }

    _good(_sg_io_vpd(err_msg, fd, _SG_T10_SPC_VPD_DI, vpd_di_data), rc, out);

    _good(_sg_parse_vpd_83(err_msg, vpd_di_data, &dps, &dp_count), rc, out);

    for (; i < dp_count; ++i) {
        if ((dps[i]->header.association != _SG_T10_SPC_ASSOCIATION_TGT_PORT) ||
            (dps[i]->header.piv != 1))
            continue;
        protocol_id = dps[i]->header.protocol_id;
        if ((protocol_id == _SG_T10_SPC_PROTOCOL_ID_OBSOLETE) ||
            (protocol_id >= _SG_T10_SPC_PROTOCOL_ID_RESERVED)) {
            rc = LSM_ERR_LIB_BUG;
            _lsm_err_msg_set(err_msg, "Got unknown protocol ID: %02x",
                             protocol_id);
            goto out;
        }
        *link_type = protocol_id;
        break;
    }

 out:
    if (fd >= 0)
        close(fd);

    if (dps != NULL)
        _sg_t10_vpd83_dp_array_free(dps, dp_count);

    if (rc != LSM_ERR_OK) {
        if (lsm_err != NULL)
            *lsm_err = LSM_ERROR_CREATE_PLUGIN_MSG(rc, err_msg);
        if (link_type != NULL)
            *link_type = LSM_DISK_LINK_TYPE_UNKNOWN;
    }

    return rc;
}

int lsm_local_disk_ident_led_on(const char *disk_path, lsm_error **lsm_err)
{
    return _ses_ctrl(disk_path, lsm_err, _SES_DEV_CTRL_RQST_IDENT,
                     _SES_CTRL_SET);
}

int lsm_local_disk_ident_led_off(const char *disk_path, lsm_error **lsm_err)
{
    return _ses_ctrl(disk_path, lsm_err, _SES_DEV_CTRL_RQST_IDENT,
                     _SES_CTRL_CLEAR);
}

int lsm_local_disk_fault_led_on(const char *disk_path, lsm_error **lsm_err)
{
    return _ses_ctrl(disk_path, lsm_err, _SES_DEV_CTRL_RQST_FAULT,
                     _SES_CTRL_SET);
}

int lsm_local_disk_fault_led_off(const char *disk_path, lsm_error **lsm_err)
{
    return _ses_ctrl(disk_path, lsm_err, _SES_DEV_CTRL_RQST_FAULT,
                     _SES_CTRL_CLEAR);
}

static int _ses_ctrl(const char *disk_path, lsm_error **lsm_err,
                     int action, int action_type)
{
    int rc = LSM_ERR_OK;
    char err_msg[_LSM_ERR_MSG_LEN];
    char tp_sas_addr[_SG_T10_SPL_SAS_ADDR_LEN];
    int fd = -1;

    _lsm_err_msg_clear(err_msg);

    _good(_check_null_ptr(err_msg, 2 /* arg_count */, disk_path, lsm_err),
          rc, out);

    memset(tp_sas_addr, 0, _SG_T10_SPL_SAS_ADDR_LEN);

    /* TODO(Gris Ge): Add support of NVMe enclosure */

    /* Try use sysfs first to get SAS address. */
    if ((strlen(disk_path) > strlen("/dev/")) &&
        (strncmp(disk_path, "/dev/", strlen("/dev/")) == 0) &&
        (strncmp(disk_path + strlen("/dev/"), "sd", strlen("sd")) == 0))
        _sysfs_sas_addr_get(disk_path + strlen("/dev/"), tp_sas_addr);

    if (tp_sas_addr[0] == '\0') {
        _good(_sg_io_open_ro(err_msg, disk_path, &fd), rc, out);
        _good(_sg_tp_sas_addr_of_disk(err_msg, fd, tp_sas_addr), rc, out);
    }

    /* SEND DIAGNOSTIC
     * SES-3, 6.1.3 Enclosure Control diagnostic page
     * SES-3, Table 78 — Device Slot control element
     */
    _good(_ses_dev_slot_ctrl(err_msg, tp_sas_addr, action, action_type),
          rc, out);

 out:
    if (rc != LSM_ERR_OK) {
        if (lsm_err != NULL)
            *lsm_err = LSM_ERROR_CREATE_PLUGIN_MSG(rc, err_msg);
    }
    if (fd >= 0)
        close(fd);
    return rc;
}

static int _sysfs_sas_addr_get(const char *blk_name, char *tp_sas_addr)
{
    int rc = -1;
    char sysfs_sas_addr[_SYSFS_SAS_ADDR_LEN];
    char *sysfs_sas_path = NULL;
    ssize_t read_size = -1;

    assert(blk_name != NULL);
    assert(tp_sas_addr != NULL);

    memset(sysfs_sas_addr, 0, _SYSFS_SAS_ADDR_LEN);
    memset(tp_sas_addr, 0, _SG_T10_SPL_SAS_ADDR_LEN);

    sysfs_sas_path = (char *)
        malloc(sizeof(char) * (strlen("/sys/block//device/sas_address") +
                               strlen(blk_name) + 1 /* trailing \0 */));
    if (sysfs_sas_path == NULL)
        goto out;

    sprintf(sysfs_sas_path, "/sys/block/%s/device/sas_address", blk_name);

    if (! _file_exists(sysfs_sas_path) ||
        (_read_file(sysfs_sas_path, (uint8_t *) sysfs_sas_addr, &read_size,
                    _SYSFS_SAS_ADDR_LEN) != 0) ||
        (read_size != _SYSFS_SAS_ADDR_LEN) ||
        (strlen(sysfs_sas_addr) != _SYSFS_SAS_ADDR_LEN) ||
        (strncmp(sysfs_sas_addr, "0x", strlen("0x")) != 0))
        goto out;

    memcpy(tp_sas_addr, sysfs_sas_addr + strlen("0x"),
           _SG_T10_SPL_SAS_ADDR_LEN);
    tp_sas_addr[_SG_T10_SPL_SAS_ADDR_LEN - 1] = '\0';
    /* ^ Replace trailing \n as \0 */

    rc = 0;

 out:
    free(sysfs_sas_path);

    return rc;
}
