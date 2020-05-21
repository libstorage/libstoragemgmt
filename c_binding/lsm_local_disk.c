/*
 * Copyright (C) 2015-2016 Red Hat, Inc.
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

#include <assert.h>
#include <dirent.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <libudev.h>
#include <limits.h>
#include <math.h> /* For log10() */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libata.h"
#include "libfc.h"
#include "libiscsi.h"
#include "libsas.h"
#include "libses.h"
#include "libsg.h"
#include "libstoragemgmt/libstoragemgmt.h"
#include "libstoragemgmt/libstoragemgmt_error.h"
#include "libstoragemgmt/libstoragemgmt_plug_interface.h"
#include "utils.h"

#define _LSM_MAX_SERIAL_NUM_LEN 253
/* ^ Max is 252 bytes */
#define _LSM_MAX_VPD83_ID_LEN 33
/* ^ Max one is 6h IEEE Registered Extended ID which it 32 bits hex string. */
#define _SYS_BLOCK_PATH      "/sys/block"
#define _MAX_SD_NAME_STR_LEN 128
/* The linux kernel support INT_MAX(2147483647) scsi disks at most which will
 * be named as sd[a-z]{1,7}, the 7 here means `math.log(2147483647, 26) + 1`.
 * Hence, 128 bits might be enough for a quit a while.
 */

#define _SD_PATH_FORMAT      "/dev/%s"
#define _MAX_SD_PATH_STR_LEN 128 + _MAX_SD_NAME_STR_LEN

#define _SYSFS_VPD80_PATH_FORMAT      "/sys/block/%s/device/vpd_pg80"
#define _MAX_SYSFS_VPD80_PATH_STR_LEN 128 + _MAX_SD_NAME_STR_LEN

#define _SYSFS_VPD83_PATH_FORMAT      "/sys/block/%s/device/vpd_pg83"
#define _MAX_SYSFS_VPD83_PATH_STR_LEN 128 + _MAX_SD_NAME_STR_LEN

#define _SYSFS_BLK_PATH_FORMAT      "/sys/block/%s"
#define _MAX_SYSFS_BLK_PATH_STR_LEN 128 + _MAX_SD_NAME_STR_LEN
#define _SYSFS_SAS_ADDR_LEN         _SG_T10_SPL_SAS_ADDR_LEN + 2
/* ^ Only Linux sysfs entry /sys/block/sdx/device/sas_address which
 *   format is '0x<hex_addr>\0'
 */

#define _SCSI_MODE_SENSE_PSP_PAGE_CODE 0x19
/* ^ SCSI MODE SENSE page 19h Protocol Specific Port */
#define _SCSI_MODE_SENSE_SAS_PHY_SUB_PAGE_CODE 0x01
/* ^ SCSI MODE SENSE SPL-4: Phy Control And Discover subpage 01h */

#define _SCSI_MODE_SENSE_SUB_PAGE_FMT 0x01
/* ^ SPC-5 rev12 Table 458 - Sub_page mode page format Protocol Specific Port
 *   mode page
 */

#define _SCSI_MODE_SENSE_PAGE_0_FMT 0x00
/* ^ SPC-5 rev12 Table 457 - Page_0 mode page format Protocol Specific Port mode
 *   page
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

struct t10_proto_port_mode_page_0_hdr {
    uint8_t we_dont_care_0[2];
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t protocol_id : 4;
    uint8_t we_dont_care_1 : 4;
#else
    uint8_t we_dont_care_1 : 4;
    uint8_t protocol_id : 4;
#endif
};
/* ^ SPC-5 rev12 Table 457 - Page_0 mode page format Protocol Specific Port mode
 *   page
 */

struct t10_proto_port_mode_sub_page_hdr {
    uint8_t we_dont_care_0[5];
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t protocol_id : 4;
    uint8_t we_dont_care_1 : 4;
#else
    uint8_t we_dont_care_1 : 4;
    uint8_t protocol_id : 4;
#endif
};
/* ^ SPC-5 rev12 Table 458 - Sub_page mode page format Protocol Specific Port
 *   mode page
 */

#pragma pack(pop)

static int _sysfs_serial_num_of_sd_name(char *err_msg, const char *sd_name,
                                        uint8_t *serial_num);
static int _sysfs_vpd_pg80_data_get(char *err_msg, const char *sd_name,
                                    uint8_t *vpd_data, ssize_t *read_size);
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
 * Legal here means:
 *  * sysfs file content has strlen as _SYSFS_SAS_ADDR_LEN
 *  * sysfs file content start with '0x'.
 */
static void _sysfs_sas_addr_get(const char *blk_name, char *tp_sas_addr);

static int _ses_ctrl(const char *disk_path, lsm_error **lsm_err, int action,
                     int action_type);

/*
 * `tp_sas_addr` should be char[_SG_T10_SPL_SAS_ADDR_LEN]
 */
static int _sas_addr_get(char *err_msg, const char *disk_path,
                         char *tp_sas_addr);

/*
 * Retrieve the content of /sys/block/sda/device/vpd_pg80 file.
 * No argument checker here, assume all non-NULL and vpd_data is
 * char[_SG_T10_SPC_VPD_MAX_LEN]
 */
static int _sysfs_vpd_pg80_data_get(char *err_msg, const char *sd_name,
                                    uint8_t *vpd_data, ssize_t *read_size) {
    int file_rc = 0;
    char sysfs_path[_MAX_SYSFS_VPD80_PATH_STR_LEN];
    char sysfs_blk_path[_MAX_SYSFS_BLK_PATH_STR_LEN];
    char strerr_buff[_LSM_ERR_MSG_LEN];

    memset(vpd_data, 0, _SG_T10_SPC_VPD_MAX_LEN);

    /*
     * Check the existence of disk vis /sys/block/sdX folder.
     */
    snprintf(sysfs_blk_path, _MAX_SYSFS_BLK_PATH_STR_LEN,
             _SYSFS_BLK_PATH_FORMAT, sd_name);
    if (!_file_exists(sysfs_blk_path)) {
        _lsm_err_msg_set(err_msg, "Disk %s not found", sd_name);
        return LSM_ERR_NOT_FOUND_DISK;
    }

    snprintf(sysfs_path, _MAX_SYSFS_VPD80_PATH_STR_LEN,
             _SYSFS_VPD80_PATH_FORMAT, sd_name);

    file_rc =
        _read_file(sysfs_path, vpd_data, read_size, _SG_T10_SPC_VPD_MAX_LEN);
    if (file_rc != 0) {
        if (file_rc == ENOENT) {
            _lsm_err_msg_set(err_msg, "File '%s' not exist", sysfs_path);
            return LSM_ERR_NO_SUPPORT;
        } else if (file_rc == EINVAL) {
            _lsm_err_msg_set(err_msg,
                             "Read error on File '%s': "
                             "invalid argument",
                             sysfs_path);
            return LSM_ERR_NO_SUPPORT;
        } else {
            _lsm_err_msg_set(
                err_msg,
                "BUG: Unknown error %d(%s) from "
                "_read_file().",
                file_rc, error_to_str(file_rc, strerr_buff, _LSM_ERR_MSG_LEN));
            return LSM_ERR_LIB_BUG;
        }
    }
    return LSM_ERR_OK;
}

/*
 * Retrieve the content of /sys/block/sda/device/vpd_pg83 file.
 * No argument checker here, assume all non-NULL and vpd_data is
 * char[_SG_T10_SPC_VPD_MAX_LEN]
 */
static int _sysfs_vpd_pg83_data_get(char *err_msg, const char *sd_name,
                                    uint8_t *vpd_data, ssize_t *read_size) {
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
    if (!_file_exists(sysfs_blk_path)) {
        _lsm_err_msg_set(err_msg, "Disk %s not found", sd_name);
        return LSM_ERR_NOT_FOUND_DISK;
    }

    snprintf(sysfs_path, _MAX_SYSFS_VPD83_PATH_STR_LEN,
             _SYSFS_VPD83_PATH_FORMAT, sd_name);

    file_rc =
        _read_file(sysfs_path, vpd_data, read_size, _SG_T10_SPC_VPD_MAX_LEN);
    if (file_rc != 0) {
        if (file_rc == ENOENT) {
            _lsm_err_msg_set(err_msg, "File '%s' not exist", sysfs_path);
            return LSM_ERR_NO_SUPPORT;
        } else if (file_rc == EINVAL) {
            _lsm_err_msg_set(err_msg,
                             "Read error on File '%s': "
                             "invalid argument",
                             sysfs_path);
            return LSM_ERR_NO_SUPPORT;
        } else {
            _lsm_err_msg_set(
                err_msg,
                "BUG: Unknown error %d(%s) from "
                "_read_file().",
                file_rc, error_to_str(file_rc, strerr_buff, _LSM_ERR_MSG_LEN));
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
                                       char *vpd83) {
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

    _good(_sysfs_vpd_pg83_data_get(err_msg, sd_name, vpd_data, &read_size), rc,
          out);

    _good(_sg_parse_vpd_83(err_msg, vpd_data, &dps, &dp_count), rc, out);

    for (; i < dp_count; ++i) {
        if ((dps[i]->header.designator_type ==
             _SG_T10_SPC_VPD_DI_DESIGNATOR_TYPE_NAA) &&
            (dps[i]->header.association ==
             _SG_T10_SPC_VPD_DI_ASSOCIATION_LUN)) {
            naa_header = (struct _sg_t10_vpd83_naa_header *)dps[i]->designator;
            switch (naa_header->naa_type) {
            case _SG_T10_SPC_VPD_DI_NAA_TYPE_2:
            case _SG_T10_SPC_VPD_DI_NAA_TYPE_3:
            case _SG_T10_SPC_VPD_DI_NAA_TYPE_5:
                _be_raw_to_hex((uint8_t *)naa_header,
                               _SG_T10_SPC_VPD_DI_NAA_235_ID_LEN, vpd83);
                break;
            case _SG_T10_SPC_VPD_DI_NAA_TYPE_6:
                _be_raw_to_hex((uint8_t *)naa_header,
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
 * Parse _SYSFS_VPD80_PATH_FORMAT file for VPD80 serial number.
 * When no such sysfs file found, return LSM_ERR_NO_SUPPORT.
 * When VPD80 page does not have a serial number, return LSM_ERR_OK and
 * serial_num as an empty string.
 *
 * Input *serial_num should be char[_LSM_MAX_SERIAL_NUM_LEN], assuming caller
 * did the check.
 * The maximum *sd_name strlen is (_MAX_SD_NAME_STR_LEN - 1), assuming caller
 * did the check.
 * Return LSM_ERR_NO_MEMORY or LSM_ERR_NO_SUPPORT or LSM_ERR_LIB_BUG or
 *        LSM_ERR_NOT_FOUND_DISK
 */
static int _sysfs_serial_num_of_sd_name(char *err_msg, const char *sd_name,
                                        uint8_t *serial_num) {
    ssize_t read_size = 0;
    int rc = LSM_ERR_OK;
    uint8_t vpd_data[_SG_T10_SPC_VPD_MAX_LEN];

    if (sd_name == NULL) {
        _lsm_err_msg_set(err_msg, "_sysfs_serial_num_of_sd_name(): "
                                  "Input sd_name argument is NULL");
        rc = LSM_ERR_LIB_BUG;
        goto out;
    }

    _good(_sysfs_vpd_pg80_data_get(err_msg, sd_name, vpd_data, &read_size), rc,
          out);

    _good(_sg_parse_vpd_80(err_msg, vpd_data, serial_num,
                           _LSM_MAX_SERIAL_NUM_LEN),
          rc, out);

    if (serial_num[0] == '\0') {
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg, "SCSI VPD 80 serial number is not supported");
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
 * Input *vpd83 should be char[_LSM_MAX_VPD83_ID_LEN], assuming caller did
 * the check.
 * The maximum *sd_name strlen is (_MAX_SD_NAME_STR_LEN - 1), assuming caller
 * did the check.
 */
static int _udev_vpd83_of_sd_name(char *err_msg, const char *sd_name,
                                  char *vpd83) {
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
    if (wwn == NULL) {
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg,
                         "SCSI VPD 83 NAA logical unit ID is not supported");
        goto out;
    }

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
                                lsm_error **lsm_err) {
    int rc = LSM_ERR_OK;
    uint32_t i = 0;
    const char *disk_path = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_string_list *disk_paths = NULL;
    char *tmp_vpd83 = NULL;
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
        _lsm_err_msg_set(err_msg,
                         "Provided vpd83 string exceeded the maximum "
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
        if (lsm_local_disk_vpd83_get(disk_path, &tmp_vpd83, &tmp_lsm_err) !=
            LSM_ERR_OK) {
            lsm_error_free(tmp_lsm_err);
            continue;
        }
        if (tmp_vpd83 == NULL) {
            rc = LSM_ERR_LIB_BUG;
            _lsm_err_msg_set(err_msg,
                             "BUG: lsm_local_disk_vpd83_get() on "
                             "'%s',return NULL for vpd83 and LSM_ERR_OK",
                             disk_path);
            goto out;
        }
        if (strncmp(vpd83, tmp_vpd83, _LSM_MAX_VPD83_ID_LEN) == 0) {
            if (lsm_string_list_append(*disk_path_list, disk_path) != 0) {
                rc = LSM_ERR_NO_MEMORY;
                goto out;
            }
        }
        free(tmp_vpd83);
        tmp_vpd83 = NULL;
    }

out:
    if (disk_paths != NULL)
        lsm_string_list_free(disk_paths);

    free(tmp_vpd83);

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

int lsm_local_disk_serial_num_get(const char *disk_path, char **serial_num,
                                  lsm_error **lsm_err) {
    uint8_t tmp_serial_num[_LSM_MAX_SERIAL_NUM_LEN];
    char *trimmed_serial_num = NULL;
    const char *sd_name = NULL;
    int rc = LSM_ERR_OK;
    char err_msg[_LSM_ERR_MSG_LEN];

    _lsm_err_msg_clear(err_msg);

    rc = _check_null_ptr(err_msg, 3 /* arg_count */, disk_path, serial_num,
                         lsm_err);

    if (rc != LSM_ERR_OK) {
        if (serial_num != NULL)
            *serial_num = NULL;

        goto out;
    }

    *serial_num = NULL;
    *lsm_err = NULL;

    if (!_file_exists(disk_path)) {
        rc = LSM_ERR_NOT_FOUND_DISK;
        _lsm_err_msg_set(err_msg, "Disk %s not found", disk_path);
        goto out;
    }

    if (strncmp(disk_path, "/dev/sd", strlen("/dev/sd")) != 0) {
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg, "we only support disk path start with "
                                  "'/dev/sd' today");
        goto out;
    }

    sd_name = disk_path + strlen("/dev/");

    rc = _sysfs_serial_num_of_sd_name(err_msg, sd_name, tmp_serial_num);
    if (rc != LSM_ERR_OK)
        goto out;

    if (tmp_serial_num[0] != '\0') {
        // ensure that the string being trimmed is NULL terminated
        tmp_serial_num[_LSM_MAX_SERIAL_NUM_LEN - 1] = '\0';

        trimmed_serial_num = _trim_spaces((char *)tmp_serial_num);
        if (trimmed_serial_num == NULL) {
            rc = LSM_ERR_NO_SUPPORT;
            _lsm_err_msg_set(err_msg, "failed to trim vpd80 "
                                      "serial number field");
            goto out;
        }

        *serial_num = strdup(trimmed_serial_num);
        if (*serial_num == NULL)
            rc = LSM_ERR_NO_MEMORY;
    } else {
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg, "no characters in vpd80 serial "
                                  "number field");
    }

out:
    if (rc != LSM_ERR_OK) {
        if (lsm_err != NULL)
            *lsm_err = LSM_ERROR_CREATE_PLUGIN_MSG(rc, err_msg);

        if (serial_num != NULL) {
            free(*serial_num);
            *serial_num = NULL;
        }
    }

    return rc;
}

int lsm_local_disk_vpd83_get(const char *disk_path, char **vpd83,
                             lsm_error **lsm_err) {
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

    if (!_file_exists(disk_path)) {
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
            *vpd83 = NULL;
        }
    }

    return rc;
}

int lsm_local_disk_rpm_get(const char *disk_path, int32_t *rpm,
                           lsm_error **lsm_err) {
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
    _good(_sg_io_vpd(err_msg, fd, _SG_T10_SBC_VPD_BLK_DEV_CHA, vpd_data), rc,
          out);

    bdc = (struct t10_sbc_vpd_bdc *)vpd_data;
    if (bdc->pg_code != _SG_T10_SBC_VPD_BLK_DEV_CHA) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg,
                         "Got corrupted SCSI SBC "
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

int lsm_local_disk_list(lsm_string_list **disk_paths, lsm_error **lsm_err) {
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
        _lsm_err_msg_set(err_msg,
                         "udev_enumerate_scan_subsystems() failed "
                         "with %d",
                         udev_rc);
        goto out;
    }
    udev_rc = udev_enumerate_add_match_property(udev_enum, "DEVTYPE", "disk");
    if (udev_rc != 0) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg,
                         "udev_enumerate_add_match_property() failed "
                         "with %d",
                         udev_rc);
        goto out;
    }

    udev_rc = udev_enumerate_scan_devices(udev_enum);
    if (udev_rc != 0) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg,
                         "udev_enumerate_scan_devices() failed "
                         "with %d",
                         udev_rc);
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
 *  * Query VPD supported pages, get ATA information page support and
 *    check the device id page.
 *  * Based on that data, decide what type of device it is.
 *  * Request health status the appropriate way.
 */
int lsm_local_disk_health_status_get(const char *disk_path,
                                     int32_t *health_status,
                                     lsm_error **lsm_err) {
    int fd = -1;
    char err_msg[_LSM_ERR_MSG_LEN];
    int rc = LSM_ERR_OK;
    lsm_disk_link_type link_type = LSM_DISK_LINK_TYPE_NO_SUPPORT;

    _lsm_err_msg_clear(err_msg);

    _good(_check_null_ptr(err_msg, 3, disk_path, health_status, lsm_err), rc,
          out);

    *lsm_err = NULL;

    rc = lsm_local_disk_link_type_get(disk_path, &link_type, lsm_err);

    if (rc != LSM_ERR_OK) {
        _lsm_err_msg_set(err_msg, "%s", lsm_error_message_get(*lsm_err));
        lsm_error_free(*lsm_err);
        goto out;
    }

    _good(_sg_io_open_ro(err_msg, disk_path, &fd), rc, out);

    if (link_type == LSM_DISK_LINK_TYPE_ATA) {
        _good(_sg_ata_health_status(err_msg, fd, health_status), rc, out);
    } else if (link_type == LSM_DISK_LINK_TYPE_SAS) {
        _good(_sg_sas_health_status(err_msg, fd, health_status), rc, out);
    } else {
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg, "Device link type %d is not supported yet",
                         link_type);
        goto out;
    }

out:
    if (rc != LSM_ERR_OK) {
        if (lsm_err != NULL)
            *lsm_err = LSM_ERROR_CREATE_PLUGIN_MSG(rc, err_msg);
        if (health_status != NULL)
            *health_status = LSM_DISK_HEALTH_STATUS_UNKNOWN;
    }

    if (fd >= 0)
        close(fd);

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
 *  * As fallback, we use 'Protocol Specific Port mode page' seeking for
 *    'PROTOCOL IDENTIFIER' also.
 */
int lsm_local_disk_link_type_get(const char *disk_path,
                                 lsm_disk_link_type *link_type,
                                 lsm_error **lsm_err) {
    unsigned char vpd_sup_data[_SG_T10_SPC_VPD_MAX_LEN];
    unsigned char vpd_di_data[_SG_T10_SPC_VPD_MAX_LEN];
    int fd = -1;
    char err_msg[_LSM_ERR_MSG_LEN];
    int rc = LSM_ERR_OK;
    struct _sg_t10_vpd83_dp **dps = NULL;
    uint16_t dp_count = 0;
    uint8_t protocol_id = _SG_T10_SPC_PROTOCOL_ID_OBSOLETE;
    uint16_t i = 0;
    uint8_t protocol_mode_page[_SG_T10_SPC_MODE_SENSE_MAX_LEN];
    int tmp_rc = LSM_ERR_OK;
    struct t10_proto_port_mode_page_0_hdr *page_0_hdr = NULL;
    struct t10_proto_port_mode_sub_page_hdr *sub_page_hdr = NULL;

    _lsm_err_msg_clear(err_msg);

    _good(_check_null_ptr(err_msg, 3 /* arg_count */, disk_path, link_type,
                          lsm_err),
          rc, out);

    *link_type = LSM_DISK_LINK_TYPE_NO_SUPPORT;
    *lsm_err = NULL;

    _good(_sg_io_open_ro(err_msg, disk_path, &fd), rc, out);
    _good(_sg_io_vpd(err_msg, fd, _SG_T10_SPC_VPD_SUP_VPD_PGS, vpd_sup_data),
          rc, out);

    if (_sg_is_vpd_page_supported(vpd_sup_data, _SG_T10_SPC_VPD_ATA_INFO) ==
        true) {
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

    /* Use MODE SENSE(10) to query 'Protocol Specific Port mode page' as
     * fallback.
     */
    if (*link_type == LSM_DISK_LINK_TYPE_NO_SUPPORT) {
        /* Try subpage format first as hpsa return subpage format even when
         * request page 0 mode.
         * TODO(Gris Ge): sg_modes does not impact by this issue, it
         *                can detect the return data's format. Need to
         *                study their workflow.
         */
        tmp_rc = _sg_io_mode_sense(err_msg, fd, _SCSI_MODE_SENSE_PSP_PAGE_CODE,
                                   _SCSI_MODE_SENSE_SUB_PAGE_FMT,
                                   protocol_mode_page);
        if (tmp_rc == LSM_ERR_OK) {
            sub_page_hdr =
                (struct t10_proto_port_mode_sub_page_hdr *)protocol_mode_page;
            *link_type = sub_page_hdr->protocol_id;
        } else if (tmp_rc == LSM_ERR_NO_SUPPORT) {
            tmp_rc = _sg_io_mode_sense(
                err_msg, fd, _SCSI_MODE_SENSE_PSP_PAGE_CODE,
                _SCSI_MODE_SENSE_PAGE_0_FMT, protocol_mode_page);
            if (tmp_rc == LSM_ERR_OK) {
                page_0_hdr =
                    (struct t10_proto_port_mode_page_0_hdr *)protocol_mode_page;
                *link_type = page_0_hdr->protocol_id;
            } else if (tmp_rc != LSM_ERR_NO_SUPPORT) {
                rc = LSM_ERR_LIB_BUG;
                goto out;
            }
        } else {
            rc = LSM_ERR_LIB_BUG;
            goto out;
        }
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

int lsm_local_disk_ident_led_on(const char *disk_path, lsm_error **lsm_err) {
    return _ses_ctrl(disk_path, lsm_err, _SES_DEV_CTRL_RQST_IDENT,
                     _SES_CTRL_SET);
}

int lsm_local_disk_ident_led_off(const char *disk_path, lsm_error **lsm_err) {
    return _ses_ctrl(disk_path, lsm_err, _SES_DEV_CTRL_RQST_IDENT,
                     _SES_CTRL_CLEAR);
}

int lsm_local_disk_fault_led_on(const char *disk_path, lsm_error **lsm_err) {
    return _ses_ctrl(disk_path, lsm_err, _SES_DEV_CTRL_RQST_FAULT,
                     _SES_CTRL_SET);
}

int lsm_local_disk_fault_led_off(const char *disk_path, lsm_error **lsm_err) {
    return _ses_ctrl(disk_path, lsm_err, _SES_DEV_CTRL_RQST_FAULT,
                     _SES_CTRL_CLEAR);
}

static int _ses_ctrl(const char *disk_path, lsm_error **lsm_err, int action,
                     int action_type) {
    int rc = LSM_ERR_OK;
    char err_msg[_LSM_ERR_MSG_LEN];
    char tp_sas_addr[_SG_T10_SPL_SAS_ADDR_LEN];

    _lsm_err_msg_clear(err_msg);

    _good(_check_null_ptr(err_msg, 2 /* arg_count */, disk_path, lsm_err), rc,
          out);

    _good(_sas_addr_get(err_msg, disk_path, tp_sas_addr), rc, out);

    /* SEND DIAGNOSTIC
     * SES-3, 6.1.3 Enclosure Control diagnostic page
     * SES-3, Table 78 — Device Slot control element
     */
    _good(_ses_dev_slot_ctrl(err_msg, tp_sas_addr, action, action_type), rc,
          out);

out:
    if (rc != LSM_ERR_OK) {
        if (lsm_err != NULL)
            *lsm_err = LSM_ERROR_CREATE_PLUGIN_MSG(rc, err_msg);
    }
    return rc;
}

static void _sysfs_sas_addr_get(const char *blk_name, char *tp_sas_addr) {
    char sysfs_sas_addr[_SYSFS_SAS_ADDR_LEN];
    char *sysfs_sas_path = NULL;
    ssize_t read_size = -1;
    int tmp_rc = 0;

    assert(blk_name != NULL);
    assert(tp_sas_addr != NULL);

    memset(sysfs_sas_addr, 0, _SYSFS_SAS_ADDR_LEN);
    memset(tp_sas_addr, 0, _SG_T10_SPL_SAS_ADDR_LEN);

    sysfs_sas_path = (char *)malloc(sizeof(char) *
                                    (strlen("/sys/block//device/sas_address") +
                                     strlen(blk_name) + 1 /* trailing \0 */));
    if (sysfs_sas_path == NULL)
        goto out;

    sprintf(sysfs_sas_path, "/sys/block/%s/device/sas_address", blk_name);
    if (!_file_exists(sysfs_sas_path))
        goto out;

    tmp_rc = _read_file(sysfs_sas_path, (uint8_t *)sysfs_sas_addr, &read_size,
                        _SYSFS_SAS_ADDR_LEN);
    /* As sysfs entry has trailing '\n', we should get EFBIG here */
    if (tmp_rc != EFBIG)
        goto out;

    if (strncmp(sysfs_sas_addr, "0x", strlen("0x")) != 0)
        goto out;

    memcpy(tp_sas_addr, sysfs_sas_addr + strlen("0x"),
           _SG_T10_SPL_SAS_ADDR_LEN);

out:
    free(sysfs_sas_path);
}

static int _sas_addr_get(char *err_msg, const char *disk_path,
                         char *tp_sas_addr) {
    int rc = LSM_ERR_OK;
    int fd = -1;

    assert(disk_path != NULL);
    assert(tp_sas_addr != NULL);

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

out:
    if (fd >= 0)
        close(fd);
    return rc;
}

int LSM_DLL_EXPORT lsm_local_disk_led_status_get(const char *disk_path,
                                                 uint32_t *led_status,
                                                 lsm_error **lsm_err) {
    int rc = LSM_ERR_OK;
    char err_msg[_LSM_ERR_MSG_LEN];
    char tp_sas_addr[_SG_T10_SPL_SAS_ADDR_LEN];
    struct _ses_dev_slot_status status;

    _lsm_err_msg_clear(err_msg);

    _good(_check_null_ptr(err_msg, 3 /* arg_count */, disk_path, led_status,
                          lsm_err),
          rc, out);

    _good(_sas_addr_get(err_msg, disk_path, tp_sas_addr), rc, out);

    _good(_ses_status_get(err_msg, tp_sas_addr, &status), rc, out);

    *led_status = 0;

    if (status.fault_reqstd || status.fault_sensed)
        *led_status |= LSM_DISK_LED_STATUS_FAULT_ON;
    else
        *led_status |= LSM_DISK_LED_STATUS_FAULT_OFF;

    if (status.ident)
        *led_status |= LSM_DISK_LED_STATUS_IDENT_ON;
    else
        *led_status |= LSM_DISK_LED_STATUS_IDENT_OFF;

out:
    if (rc != LSM_ERR_OK) {
        if (led_status != NULL)
            *led_status = LSM_DISK_LED_STATUS_UNKNOWN;
        if (lsm_err != NULL)
            *lsm_err = LSM_ERROR_CREATE_PLUGIN_MSG(rc, err_msg);
    }
    return rc;
}

int lsm_local_disk_link_speed_get(const char *disk_path, uint32_t *link_speed,
                                  lsm_error **lsm_err) {
    int rc = LSM_ERR_OK;
    int tmp_rc = LSM_ERR_OK;
    lsm_error *tmp_lsm_err = NULL;
    lsm_disk_link_type link_type = LSM_DISK_LINK_TYPE_UNKNOWN;
    int fd = -1;
    char err_msg[_LSM_ERR_MSG_LEN];
    uint8_t vpd_data[_SG_T10_SPC_VPD_MAX_LEN];
    struct _sg_t10_vpd_ata_info *ata_info = NULL;
    uint8_t sas_mode_sense[_SG_T10_SPC_MODE_SENSE_MAX_LEN];
    char sas_addr[_SG_T10_SPL_SAS_ADDR_LEN];
    unsigned int host_no = UINT_MAX;

    _lsm_err_msg_clear(err_msg);
    rc = _check_null_ptr(err_msg, 3 /* argument count */, disk_path, link_speed,
                         lsm_err);

    if (rc != LSM_ERR_OK) {
        /* set output pointers to NULL if possible when facing error in case
         * application use output memory.
         */
        if (link_speed != NULL)
            *link_speed = LSM_DISK_LINK_SPEED_UNKNOWN;

        goto out;
    }

    /* Workflow:
     *  * Use lsm_local_disk_link_type_get() to find out link type:
     *      * SATA
     *          check vpd89(ATA Information VPD page) for
     *          "IDENTIFY DEVICE data" ACS word 77 CURRENT NEGOTIATED SERIAL ATA
     *          SIGNAL SPEED.
     *      * SAS
     *          SCSI MODE SENSE page 19h Protocol Specific Port,
     *          subpage 01h Phy Control And Discover
     *      * FC
     *          Use SCSI_IOCTL_GET_BUS_NUMBER IOCTL to get SCSI host number,
     *          then check file: /sys/class/fc_host/host9/speed
     */

    tmp_rc = lsm_local_disk_link_type_get(disk_path, &link_type, &tmp_lsm_err);
    if (tmp_rc != LSM_ERR_OK) {
        rc = tmp_rc;
        _lsm_err_msg_set(err_msg, "%s", lsm_error_message_get(tmp_lsm_err));
        lsm_error_free(tmp_lsm_err);
        goto out;
    }

    switch (link_type) {
    case LSM_DISK_LINK_TYPE_ATA:
        /* Check VPD 0x89(ATA Information VPD page) which is mandatory page */
        _good(_sg_io_open_ro(err_msg, disk_path, &fd), rc, out);
        _good(_sg_io_vpd(err_msg, fd, _SG_T10_SPC_VPD_ATA_INFO, vpd_data), rc,
              out);
        ata_info = (struct _sg_t10_vpd_ata_info *)vpd_data;
        _good(
            _ata_cur_speed_get(err_msg, ata_info->ata_id_dev_data, link_speed),
            rc, out);
        break;
    case LSM_DISK_LINK_TYPE_SAS:
        _good(_sas_addr_get(err_msg, disk_path, sas_addr), rc, out);
        _good(_sg_io_open_ro(err_msg, disk_path, &fd), rc, out);
        _good(_sg_io_mode_sense(err_msg, fd, _SCSI_MODE_SENSE_PSP_PAGE_CODE,
                                _SCSI_MODE_SENSE_SAS_PHY_SUB_PAGE_CODE,
                                sas_mode_sense),
              rc, out);
        _good(_sas_cur_speed_get(err_msg, sas_mode_sense, sas_addr, link_speed),
              rc, out);
        break;
    case LSM_DISK_LINK_TYPE_FC:
        _good(_sg_io_open_ro(err_msg, disk_path, &fd), rc, out);
        _good(_sg_host_no(err_msg, fd, &host_no), rc, out);
        _good(_fc_host_speed_get(err_msg, host_no, link_speed), rc, out);
        break;
    case LSM_DISK_LINK_TYPE_ISCSI:
        _good(_sg_io_open_ro(err_msg, disk_path, &fd), rc, out);
        _good(_sg_host_no(err_msg, fd, &host_no), rc, out);
        _good(_iscsi_host_speed_get(err_msg, host_no, link_speed), rc, out);
        break;
    default:
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg, "Disk link type %d is not supported yet",
                         link_type);
        goto out;
    }

out:
    if (rc != LSM_ERR_OK) {
        if (lsm_err != NULL)
            *lsm_err = LSM_ERROR_CREATE_PLUGIN_MSG(rc, err_msg);
        if (link_speed != NULL) {
            *link_speed = LSM_DISK_LINK_SPEED_UNKNOWN;
        }
    }

    if (fd >= 0)
        close(fd);

    return rc;
}
