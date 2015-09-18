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

#ifndef _LIBHPSA_SES_UDEV_STORAGE_H_
#define _LIBHPSA_SES_UDEV_STORAGE_H_

#include <stdint.h>

#define UDEV_ST_OK 0
#define UDEV_ST_ERR_BUG 1
#define UDEV_ST_ERR_INVALID_ARGUMENT 2

enum udev_st_disk_type {
    UDEV_ST_DISK_TYPE_UNKNOWN,
    UDEV_ST_DISK_TYPE_SATA_SES,
    UDEV_ST_DISK_TYPE_SAS,
};

struct UDEV_ST_disk {
    char *sd_path;
    /* ^ devnode */
    enum udev_st_disk_type type;
    char *wwn;
    /* ^ ID_WWN_WITH_EXTENSION without leading 0x */
    char *sas_address;
    /* ^ sysfs sas_address value without leading 0x */
    char *serial;
    /* ^ ID_SERIAL */
    char *model;
    /* ^ ID_MODEL */
    char *vendor;
    /* ^ ID_VENDOR */
    uint64_t sector_size;
    uint64_t sector_count;
};

struct UDEV_ST_ses {
    char *sg_path;
    char *hw_driver;
    /* ^ like mpt2sas */
    char *pci_slot_name;
    /* ^ PCI_SLOT_NAME like 09:00.0 */
};

int udev_st_ses_list_get(struct UDEV_ST_ses ***ses_list, uint32_t *ses_count);

void udev_st_ses_list_free(struct UDEV_ST_ses **ses_list, uint32_t ses_count);

int udev_st_disk_list_get(struct UDEV_ST_disk ***disk_list,
                          uint32_t *disk_count);

void udev_st_disk_list_free(struct UDEV_ST_disk **disk_list,
                            uint32_t disk_count);

struct UDEV_ST_disk *udev_st_disk_of_sas_address(struct UDEV_ST_disk **disks,
                                                 uint32_t disk_count,
                                                 const char *sas_address);

#endif  /* End of _LIBHPSA_SES_UDEV_STORAGE_H_  */
