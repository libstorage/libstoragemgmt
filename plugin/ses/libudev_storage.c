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

/* TODO(Gris Ge): introduce udev_st_err_msg */

#include <libstoragemgmt/libstoragemgmt.h>
#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libudev_storage.h"
#include "ptr_list.h"
#include "my_mem.h"

/*
 * SPC-5 Table 139 — PERIPHERAL DEVICE TYPE field
 */
#define _T10_SCSI_DEV_TYPE_SES "13"  /* 0x0d  */

#define _STRDUP_UDEV_PRO(output, udev_dev, key, tmp_char) \
    { \
        \
        tmp_char = udev_device_get_property_value(udev_dev, key); \
        if (tmp_char != NULL) \
            output = _trim_tailling_space(strdup_or_die(tmp_char)); \
        else \
            output = NULL; \
    }


static struct UDEV_ST_disk *_udev_st_disk_new(void);
static struct UDEV_ST_ses *_udev_st_ses_new(void);
static void _udev_st_disk_free(struct UDEV_ST_disk *disk);
static void _udev_st_ses_free(struct UDEV_ST_ses *ses);
static char *_trim_tailling_space(char *str);

int udev_st_ses_list_get(struct UDEV_ST_ses ***ses_list, uint32_t *ses_count)
{
    struct udev *udev = NULL;
    struct udev_enumerate *enumerate_sg = NULL;
    struct udev_list_entry *sg_udevices = NULL;
    struct udev_list_entry *sg_udev_list = NULL;
    struct udev_list_entry *sg_udev_list_entry = NULL;
    struct udev_device *sg_udev = NULL;
    struct udev_device *pci_udev = NULL;
    const char *sg_udev_path = NULL;
    const char *sg_path = NULL;
    const char *hw_driver = NULL;
    const char *pci_slot_name = NULL;
    struct pointer_list *all_ses = NULL;
    struct UDEV_ST_ses *tmp_ses = NULL;
    uint32_t i = 0;
    int rc = UDEV_ST_OK;

    if ((ses_list == NULL) || (ses_count == NULL))
        return UDEV_ST_ERR_INVALID_ARGUMENT;

    *ses_count = 0;
    *ses_list = NULL;

    udev = udev_new();
    if (udev == NULL)
        exit(EXIT_FAILURE);

    enumerate_sg = udev_enumerate_new(udev);
    if (enumerate_sg == NULL)
        exit(EXIT_FAILURE);

    if (udev_enumerate_add_match_subsystem(enumerate_sg,
                                           "scsi_generic") != 0) {
        rc = UDEV_ST_ERR_BUG;
        goto out;
    }

    if (udev_enumerate_add_match_sysattr(enumerate_sg,
                                         "device/type",
                                         _T10_SCSI_DEV_TYPE_SES) != 0) {
        rc = UDEV_ST_ERR_BUG;
        goto out;
    }
    if (udev_enumerate_scan_devices(enumerate_sg) != 0) {
        rc = UDEV_ST_ERR_BUG;
        goto out;
    }
    sg_udevices = udev_enumerate_get_list_entry(enumerate_sg);

    all_ses = ptr_list_new();

    udev_list_entry_foreach(sg_udev_list_entry, sg_udevices) {
        sg_udev_path = udev_list_entry_get_name(sg_udev_list_entry);
        if (sg_udev_path == NULL)
            continue;
        sg_udev = udev_device_new_from_syspath(udev, sg_udev_path);
        if (sg_udev == NULL) {
            rc = UDEV_ST_ERR_BUG;
            goto out;
        }
        sg_path = udev_device_get_property_value(sg_udev, "DEVNAME");
        if (sg_path == NULL) {
            udev_device_unref(sg_udev);
            continue;
        }

        pci_udev = udev_device_get_parent_with_subsystem_devtype(sg_udev, "pci",
                                                                 NULL);
        if (pci_udev == NULL) {
            rc = UDEV_ST_ERR_BUG;
            goto out;
        }

        hw_driver = udev_device_get_property_value(pci_udev, "DRIVER");
        if (hw_driver == NULL) {
            rc = UDEV_ST_ERR_BUG;
            goto out;
        }

        pci_slot_name = udev_device_get_property_value(pci_udev,
                                                       "PCI_SLOT_NAME");
        if (pci_slot_name == NULL) {
            rc = UDEV_ST_ERR_BUG;
            goto out;
        }

        tmp_ses = _udev_st_ses_new();
        tmp_ses->sg_path = strdup_or_die(sg_path);
        tmp_ses->hw_driver = strdup_or_die(hw_driver);
        tmp_ses->pci_slot_name = strdup_or_die(pci_slot_name);

        ptr_list_add(all_ses, tmp_ses);

        udev_device_unref(sg_udev);
    }

    ptr_list_2_array(all_ses, (void ***) ses_list, ses_count);

out:
    if (udev != NULL) {
        udev_unref(udev);
    }
    if (enumerate_sg != NULL) {
        udev_enumerate_unref(enumerate_sg);
    }
    if (rc != 0) {
        if (all_ses != NULL) {
            ptr_list_for_each(all_ses, i, tmp_ses) {
                _udev_st_ses_free(tmp_ses);
            }
        }
    }
    ptr_list_free(all_ses);

    return rc;
}

void udev_st_ses_list_free(struct UDEV_ST_ses **ses_list, uint32_t ses_count)
{
    uint32_t i = 0;

    if (ses_list == NULL || ses_count == 0)
        return;

    for (;i < ses_count; ++i) {
        free((char *) ses_list[i]);
    }
    free((char **) ses_list);
}

int udev_st_disk_list_get(struct UDEV_ST_disk ***disk_list,
                          uint32_t *disk_count)
{
    struct udev *udev = NULL;
    struct udev_enumerate *enumerate_blk = NULL;
    struct udev_list_entry *blk_udevices = NULL;
    struct udev_list_entry *blk_udev_list = NULL;
    struct udev_list_entry *blk_udev_list_entry = NULL;
    struct udev_device *blk_udev = NULL;
    struct udev_device *sd_udev = NULL;
    const char *blk_udev_path = NULL;
    const char *sd_path = NULL;
    struct pointer_list *all_disks = NULL;
    struct UDEV_ST_disk *tmp_disk = NULL;
    const char *c = NULL;
    const char *sas_address = NULL;
    const char *sector_size_str = 0;
    const char *sector_count_str = 0;
    const char *wwn = NULL;
    int rc = 0;
    uint32_t i = 0;

    udev = udev_new();
    if (udev == NULL)
        exit(EXIT_FAILURE);

    enumerate_blk = udev_enumerate_new(udev);
    if (enumerate_blk == NULL)
        exit(EXIT_FAILURE);

    if (udev_enumerate_add_match_subsystem(enumerate_blk,
                                           "block") != 0) {
        rc = UDEV_ST_ERR_BUG;
        goto out;
    }

    /* Filter out the patitions */
    if (udev_enumerate_add_match_property(enumerate_blk, "DEVTYPE", "disk")
        != 0) {
        rc = UDEV_ST_ERR_BUG;
        goto out;
    }


    if (udev_enumerate_scan_devices(enumerate_blk) != 0) {
        rc = UDEV_ST_ERR_BUG;
        goto out;
    }
    all_disks = ptr_list_new();

    blk_udevices = udev_enumerate_get_list_entry(enumerate_blk);

    udev_list_entry_foreach(blk_udev_list_entry, blk_udevices) {
        blk_udev_path = udev_list_entry_get_name(blk_udev_list_entry);
        if (blk_udev_path == NULL)
            continue;
        blk_udev = udev_device_new_from_syspath(udev, blk_udev_path);
        if (blk_udev == NULL) {
            rc = UDEV_ST_ERR_BUG;
            goto out;
        }
        sd_path = udev_device_get_devnode(blk_udev);
        if ((sd_path == NULL) ||
            (strncmp(sd_path, "/dev/sd", sizeof("/dev/sd") - 1) != 0)) {
            udev_device_unref(blk_udev);
            continue;
        }

        tmp_disk = _udev_st_disk_new();
        ptr_list_add(all_disks, tmp_disk);

        tmp_disk->sd_path = _trim_tailling_space(strdup_or_die(sd_path));

        wwn = udev_device_get_property_value(blk_udev, "ID_WWN_WITH_EXTENSION");
        if ((wwn != NULL) && (strlen(wwn) > strlen("0x"))) {
            tmp_disk->wwn = strdup_or_die(wwn + strlen("0x"));
        }

        _STRDUP_UDEV_PRO(tmp_disk->serial, blk_udev, "ID_SERIAL", c);
        _STRDUP_UDEV_PRO(tmp_disk->model, blk_udev, "ID_MODEL", c);
        _STRDUP_UDEV_PRO(tmp_disk->vendor, blk_udev, "ID_VENDOR", c);

        sector_count_str = udev_device_get_sysattr_value(blk_udev, "size");
        if (sector_count_str == NULL)
            tmp_disk->sector_count = 0;
        else
            tmp_disk->sector_count =
                strtoull(sector_count_str, NULL, 10 /* base */) & UINT64_MAX;

        sector_size_str =
            udev_device_get_sysattr_value(blk_udev, "queue/logical_block_size");

        if (sector_size_str == NULL)
            tmp_disk->sector_size = 0;
        else
            tmp_disk->sector_size =
                strtoull(sector_size_str, NULL, 10 /* base */) & UINT64_MAX;

        sd_udev = udev_device_get_parent_with_subsystem_devtype(blk_udev,
                                                                "scsi",
                                                                "scsi_device");

        if (sd_udev == NULL)
            continue;

        if (tmp_disk->vendor == NULL) {
            c = udev_device_get_sysattr_value(sd_udev, "vendor");
            if (c != NULL)
                tmp_disk->vendor = _trim_tailling_space(strdup_or_die(c));
        }

        sas_address = udev_device_get_sysattr_value(sd_udev, "sas_address");
        if ((sas_address != NULL) && (strlen(sas_address) > strlen("0x")))
            tmp_disk->sas_address =
                _trim_tailling_space(strdup_or_die(sas_address + strlen("0x")));

        udev_device_unref(blk_udev);
    }
    ptr_list_2_array(all_disks, (void ***) disk_list, disk_count);

out:
    if (udev != NULL) {
        udev_unref(udev);
    }
    if (enumerate_blk != NULL) {
        udev_enumerate_unref(enumerate_blk);
    }
    if (rc != 0 ) {
        ptr_list_for_each(all_disks, i, tmp_disk) {
            if (tmp_disk != NULL)
                _udev_st_disk_free(tmp_disk);
        }
    }

    ptr_list_free(all_disks);
    return rc;
}

void udev_st_disk_list_free(struct UDEV_ST_disk **disk_list,
                            uint32_t disk_count)
{
    uint32_t i = 0;

    if (disk_list == NULL || disk_count == 0)
        return;

    for (;i < disk_count; ++i) {
        if (disk_list[i] != NULL)
            _udev_st_disk_free(disk_list[i]);
    }
    free(disk_list);
}

static struct UDEV_ST_disk *_udev_st_disk_new()
{
    struct UDEV_ST_disk *disk = malloc_or_die(sizeof(struct UDEV_ST_disk));

    disk->type = UDEV_ST_DISK_TYPE_UNKNOWN;
    disk->wwn = NULL;
    disk->sd_path = NULL;
    disk->serial = NULL;
    disk->model = NULL;
    disk->sas_address = NULL;
    disk->vendor = NULL;
    disk->sector_size = 0;
    disk->sector_count = 0;
    return disk;
}

static void _udev_st_disk_free(struct UDEV_ST_disk *disk)
{
    if (disk == NULL)
        return;

    free(disk->wwn);
    free(disk->sd_path);
    free(disk->serial);
    free(disk->model);
    free(disk->sas_address);
    free(disk->vendor);
    free(disk);
}

static struct UDEV_ST_ses *_udev_st_ses_new()
{
    struct UDEV_ST_ses *ses = malloc_or_die(sizeof(struct UDEV_ST_ses));

    ses->sg_path = NULL;
    ses->hw_driver = NULL;
    ses->pci_slot_name = NULL;
    return ses;
}

static void _udev_st_ses_free(struct UDEV_ST_ses *ses)
{
    if (ses == NULL)
        return;

    free(ses->sg_path);
    free(ses->hw_driver);
    free(ses->pci_slot_name);
    free(ses);
}

struct UDEV_ST_disk *udev_st_disk_of_sas_address(struct UDEV_ST_disk **disks,
                                                 uint32_t disk_count,
                                                 const char *sas_address)
{
    uint32_t i = 0;
    struct UDEV_ST_disk *disk = NULL;

    if ((disks == NULL) || (sas_address == NULL))
        return NULL;

    for (; i < disk_count; ++i) {
        if ((disks[i] != NULL) && (disks[i]->sas_address != NULL) &&
            (strcmp(disks[i]->sas_address, sas_address) == 0)) {
            disk = disks[i];
            break;
        }
    }
    return disk;
}

static char *_trim_tailling_space(char *str) {
    char *end = NULL;
    if ((str == NULL) || (*str == '\0'))
        return;
    end = str + strlen(str) - 1;
    while(end > str && *end == ' ') --end;
    *(end + 1) = 0;
    return str;
}
