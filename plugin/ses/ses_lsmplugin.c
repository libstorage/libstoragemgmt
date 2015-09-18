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

#include <libstoragemgmt/libstoragemgmt.h>
#include <libstoragemgmt/libstoragemgmt_plug_interface.h>

#include <stdio.h> // code_debug for printf
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>

#include "libses.h"
#include "libudev_storage.h"
#include "ptr_list.h"

#ifdef  __cplusplus
extern "C" {
#endif

#define _SPLITER ","
#define _STR_IF_NOT_NULL_ELSE_EMPTY(x) x ? x : ""
#define _SPLITER_IF_NOT_NULL(x) x ? _SPLITER : ""
#define _COMMON_MAX_STR_LEN 256

static char plugin_name[] = "SCSI Enclosure Service Plugin";
static char plugin_version[] = "1.0.0";

int plugin_register(lsm_plugin_ptr c, const char *uri,
                    const char *password, uint32_t timeout, lsm_flag flags);

int plugin_unregister( lsm_plugin_ptr c, lsm_flag flags );

int systems(lsm_plugin_ptr c, lsm_system **systems[], uint32_t *system_count,
            lsm_flag flags);

int time_out_set(lsm_plugin_ptr c, uint32_t timeout, lsm_flag flags );

int time_out_get(lsm_plugin_ptr c, uint32_t *timeout, lsm_flag flags );

int capabilities(lsm_plugin_ptr c, lsm_system *sys,
                 lsm_storage_capabilities **cap, lsm_flag flags);

int job_status(lsm_plugin_ptr c, const char *job, lsm_job_status *status,
               uint8_t *percent_complete, lsm_data_type *type, void **value,
               lsm_flag flags);

int job_free(lsm_plugin_ptr c, char *job_id, lsm_flag flags);

int pools(lsm_plugin_ptr c, const char *search_key, const char *search_value,
          lsm_pool **pool_array[], uint32_t *count, lsm_flag flags);

static int list_disks(lsm_plugin_ptr c, const char *search_key,
                      const char *search_value, lsm_disk **disks[],
                      uint32_t *count, lsm_flag flags);

static struct lsm_mgmt_ops_v1 mgm_ops = {
    time_out_set,
    time_out_get,
    capabilities,
    job_status,
    job_free,
    pools,
    systems,
};

static struct lsm_san_ops_v1 san_ops = {
    NULL,   /* list_volumes */
    list_disks,
    NULL,    /* volume_create */
    NULL,    /* volume_replicate */
    NULL,    /* volume_replicate_range_bs */
    NULL,    /* volume_replicate_range */
    NULL,    /* volume_resize */
    NULL,    /* volume_delete */
    NULL,    /* volume_enable_disable */
    NULL,    /* volume_enable_disable */
    NULL,    /* iscsi_chap_auth */
    NULL,    /* access_group_list */
    NULL,    /* access_group_create */
    NULL,    /* access_group_delete */
    NULL,    /* access_group_initiator_add */
    NULL,    /* access_group_initiator_delete */
    NULL,    /* volume_mask */
    NULL,    /* volume_unmask */
    NULL,    /* vol_accessible_by_ag */
    NULL,    /* ag_granted_to_volume */
    NULL,    /* volume_dependency */
    NULL,    /* volume_dependency_rm */
    NULL,    /* list_targets */
};

static struct lsm_fs_ops_v1 fs_ops = {
};

static struct lsm_nas_ops_v1 nas_ops = {
};

static struct lsm_ops_v1_2 ops_v1_2 ={
};

/*
 * Treat each "enclosure services process" as an system.
 * For Dual-domain SAS, each physical card will have two enclosure service
 * process in each subenclosure.
 */
int systems(lsm_plugin_ptr c, lsm_system **systems[], uint32_t *system_count,
            lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct UDEV_ST_ses **udev_ses_list = NULL;
    uint32_t udev_ses_count = 0;
    uint32_t i;
    uint32_t j;
    struct SES_enclosure *ses_enc = NULL;
    int ses_rc = 0;
    char sys_id[255];
    char sys_name[255];
    char *err_msg = NULL;
    uint32_t sys_status = LSM_SYSTEM_STATUS_UNKNOWN;
    char status_info[] = "";
    struct pointer_list *all_systems = NULL;
    lsm_system *new_sys = NULL;
    lsm_system *tmp_lsm_sys = NULL;
    const char *tmp_sys_id = NULL;
    bool is_dup_sys = false;
    struct UDEV_ST_disk **disk_list = NULL;
    uint32_t disk_count = NULL;

    if (system_count == NULL || system_count == NULL)
        return lsm_log_error_basic(c, LSM_ERR_INVALID_ARGUMENT,
                                   "Got NULL pointer in parameter");

    rc = udev_st_ses_list_get(&udev_ses_list, &udev_ses_count);
    if (rc != UDEV_ST_OK)
        return lsm_log_error_basic(c, LSM_ERR_PLUGIN_BUG,
                                   "udev_st_ses_list_get() failed");

    *system_count = 0;
    *systems = NULL;
    all_systems = ptr_list_new();

    for (i = 0; i < udev_ses_count; ++i) {
        ses_rc = ses_enclosure_get(udev_ses_list[i]->sg_path, &ses_enc);
        if (ses_rc != SES_OK) {
            //TODO(Gris Ge): Convert SES_ERR_XXX to LSM_ERR_XXX
            printf("Got error %d\n", ses_rc);
            continue;
        }

        is_dup_sys = false;
        ptr_list_for_each(all_systems, j, tmp_lsm_sys) {
            tmp_sys_id = lsm_system_id_get(tmp_lsm_sys);
            if ((tmp_sys_id != NULL) && (ses_enc->id != NULL) &&
                (strcmp(tmp_sys_id, ses_enc->id) == 0)) {
                is_dup_sys = true;
                break;
            }
        }
        if (is_dup_sys == true)
            continue;

        snprintf(sys_name, _COMMON_MAX_STR_LEN,
                 "PCI-%s(%s) %s %s rev %s esp count %" PRIu8 "",
                 udev_ses_list[i]->pci_slot_name,
                 udev_ses_list[i]->hw_driver,
                 ses_enc->vendor, ses_enc->product, ses_enc->rev,
                 ses_enc->esp_count);

        new_sys = lsm_system_record_alloc(ses_enc->id, sys_name, sys_status,
                                          status_info,
                                          udev_ses_list[i]->sg_path);

        if (new_sys == NULL) {
            rc = LSM_ERR_NO_MEMORY;
            err_msg = "No memory";
            goto out;
        }
        lsm_system_mode_set(new_sys, LSM_SYSTEM_MODE_HBA);

        ptr_list_add(all_systems, new_sys);

        ses_enclosure_free(ses_enc);
        ses_enc = NULL;
    }

    ptr_list_2_array(all_systems, (void ***) systems, system_count);

    udev_st_disk_list_get(&disk_list, &disk_count);

 out:
    if (rc != LSM_ERR_OK) {
        ptr_list_for_each(all_systems, i, tmp_lsm_sys) {
            if (tmp_lsm_sys != NULL)
                lsm_system_record_free(tmp_lsm_sys);
        }
        lsm_log_error_basic(c, rc, err_msg);
        *systems = NULL;
        *system_count = 0;
    }
    udev_st_ses_list_free(udev_ses_list, udev_ses_count);

    ptr_list_free(all_systems);

    return rc;
}

int time_out_set(lsm_plugin_ptr c, uint32_t timeout, lsm_flag flags)
{
    return lsm_log_error_basic(c, LSM_ERR_NO_SUPPORT, "No support");
}

int time_out_get(lsm_plugin_ptr c, uint32_t *timeout, lsm_flag flags)
{
    return lsm_log_error_basic(c, LSM_ERR_NO_SUPPORT, "No support");
}

int capabilities(lsm_plugin_ptr c, lsm_system *sys,
                 lsm_storage_capabilities **cap, lsm_flag flags)
{
    int rc = LSM_ERR_NO_MEMORY;
    *cap = lsm_capability_record_alloc(NULL);

    return rc;
}

int job_status(lsm_plugin_ptr c, const char *job, lsm_job_status *status,
               uint8_t *percent_complete, lsm_data_type *type, void **value,
               lsm_flag flags)
{
    return lsm_log_error_basic(c, LSM_ERR_NO_SUPPORT, "No support");
}

int job_free(lsm_plugin_ptr c, char *job_id, lsm_flag flags)
{
    return lsm_log_error_basic(c, LSM_ERR_NO_SUPPORT, "No support");
}

int pools(lsm_plugin_ptr c, const char *search_key, const char *search_value,
          lsm_pool **pool_array[], uint32_t *count, lsm_flag flags)
{
    return lsm_log_error_basic(c, LSM_ERR_NO_SUPPORT, "No support");
}

int plugin_register(lsm_plugin_ptr c, const char *uri,
                    const char *password, uint32_t timeout, lsm_flag flags)
{
    if (geteuid() != 0 /* root */) {
        return lsm_log_error_basic(c, LSM_ERR_INVALID_ARGUMENT,
                                   "Require root privilege");
    }
    lsm_register_plugin_v1_2(c, NULL /* plugin private data */, &mgm_ops,
                             &san_ops, &fs_ops, &nas_ops, &ops_v1_2);
}

int plugin_unregister(lsm_plugin_ptr c, lsm_flag flags)
{
    return LSM_ERR_OK;
}

static int list_disks(lsm_plugin_ptr c, const char *search_key,
                      const char *search_value, lsm_disk **disks[],
                      uint32_t *count, lsm_flag flags)
{
    struct UDEV_ST_ses **udev_ses_list = NULL;
    uint32_t udev_ses_count = 0;
    int rc = LSM_ERR_OK;
    int ses_rc = SES_OK;
    int udev_st_rc = UDEV_ST_OK;
    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t l = 0;
    struct SES_disk **ses_disks = NULL;
    uint32_t ses_disk_count = 0;
    struct pointer_list *all_disks = NULL;
    lsm_disk *tmp_lsm_disk = NULL;
    lsm_disk *tmp_lsm_disk2 = NULL;
    char err_msg[_COMMON_MAX_STR_LEN];
    const char *disk_id = NULL;
    char name[_COMMON_MAX_STR_LEN];
    lsm_disk_type type = LSM_DISK_TYPE_UNKNOWN;
    uint64_t sec_size = 0;
    uint64_t sec_count = 0;
    uint64_t status = LSM_DISK_STATUS_UNKNOWN;
    const char *sys_id = NULL;
    const char *sas_address = NULL;
    struct UDEV_ST_disk **udev_disk_list = NULL;
    struct UDEV_ST_disk *udev_disk = NULL;
    uint32_t udev_disk_count = 0;
    bool flag_dup = false;
    struct SES_enclosure *ses_enc = NULL;

    if ((disks == NULL) || (count == NULL))
        return lsm_log_error_basic(c, LSM_ERR_INVALID_ARGUMENT,
                                   "Got NULL pointer in parameter");
    *disks = NULL;
    *count = 0;

    all_disks = ptr_list_new();

    udev_st_rc = udev_st_ses_list_get(&udev_ses_list, &udev_ses_count);
    if (udev_st_rc != UDEV_ST_OK) {
        //TODO(Gris Ge): Convert UDEV_ST_ERR_* to LSM_ERR_XXX
        return lsm_log_error_basic(c, LSM_ERR_PLUGIN_BUG,
                                   "udev_st_ses_list_get() failed");
    }
    if (udev_ses_count == 0)
        return LSM_ERR_OK;

    // TODO Get all disks from udev.
    if (udev_st_disk_list_get(&udev_disk_list, &udev_disk_count) != 0)
        return lsm_log_error_basic(c, LSM_ERR_PLUGIN_BUG,
                                   "Failed to retrieve udev disks");
    if (udev_disk_count == 0) {
        udev_st_ses_list_free(udev_ses_list, udev_ses_count);
        return LSM_ERR_OK;
    }

    for (i = 0; i < udev_ses_count; ++i) {
        ses_rc = ses_disk_list_get(udev_ses_list[i]->sg_path, &ses_disks,
                                   &ses_disk_count);
        if (ses_rc != SES_OK) {
            //TODO(Gris Ge): Convert SES_ERR_XXX to LSM_ERR_XXX
            printf("ses_disk_list_get() got error %d: %s\n", ses_rc,
                   ses_err_msg);
            continue;
        }
        ses_rc = ses_enclosure_get(udev_ses_list[i]->sg_path, &ses_enc);
        if (ses_rc != SES_OK) {
            //TODO(Gris Ge): Convert SES_ERR_XXX to LSM_ERR_XXX
            printf("ses_disk_list_get() got error %d: %s\n", ses_rc,
                   ses_err_msg);
            continue;
        }
        if ((ses_enc == NULL) || (ses_enc->id == NULL))
            continue;
        sys_id = ses_enc->id;
        for (j = 0; j < ses_disk_count; ++j) {
            udev_disk = NULL;
            switch(ses_disks[j]->link_type) {
            case SES_DISK_LINK_TYPE_SAS:
                type = LSM_DISK_TYPE_SAS;
                goto sas_or_sata;
            case SES_DISK_LINK_TYPE_SATA:
                type = LSM_DISK_TYPE_SATA;
             sas_or_sata:
                udev_disk = udev_st_disk_of_sas_address(udev_disk_list,
                                                        udev_disk_count,
                                                        ses_disks[j]->id);
                break;
            case SES_DISK_LINK_TYPE_FC:
                type = LSM_DISK_TYPE_FC;
                break;
            case SES_DISK_LINK_TYPE_NVME:
                type = LSM_DISK_TYPE_SOP;
                break;
            default:
                rc = LSM_ERR_PLUGIN_BUG;
                sprintf(err_msg, "Unknown struct ses_disk->link_type %d",
                        ses_disks[i]->link_type);
                goto out;
            }
            /* plugin_info is "<enclosure_id>:<item_index>", the "item_index"
             * is the "II" in manpage of sg_ses(8) INDEXES section.
             */
            if ((udev_disk == NULL) ||
                (udev_disk->serial == NULL) ||
                (strlen(udev_disk->serial) == 0)) {
                continue;
            }
            /* Check duplication */
            flag_dup = false;
            ptr_list_for_each(all_disks, l, tmp_lsm_disk2) {
                disk_id = lsm_disk_id_get(tmp_lsm_disk2);
                if ((disk_id != NULL) &&
                    (strcmp(disk_id, udev_disk->serial) == 0)) {
                    flag_dup = true;
                    break;
                }
            }
            if (flag_dup == true)
                continue;
            disk_id = udev_disk->serial;

            snprintf(name, _COMMON_MAX_STR_LEN, "%s%s%s",
                     _STR_IF_NOT_NULL_ELSE_EMPTY(udev_disk->vendor),
                     _SPLITER_IF_NOT_NULL(udev_disk->vendor),
                     _STR_IF_NOT_NULL_ELSE_EMPTY(udev_disk->model));
            sec_size = udev_disk->sector_size;
            sec_count = udev_disk->sector_count;
            tmp_lsm_disk = lsm_disk_record_alloc(disk_id, name, type, sec_size,
                                                 sec_count, status, sys_id);

            if (tmp_lsm_disk == NULL) {
                rc = LSM_ERR_NO_MEMORY;
                goto out;
            }

            ptr_list_add(all_disks, tmp_lsm_disk);
        }
        ses_enclosure_free(ses_enc);
        ses_enc = NULL;
    }
    ptr_list_2_array(all_disks, (void ***) disks, count);

 out:
    udev_st_ses_list_free(udev_ses_list, udev_ses_count);

    if (rc != 0) {
        *disks = NULL;
        *count = 0;
        if (all_disks != NULL) {
            ptr_list_for_each(all_disks, i, tmp_lsm_disk) {
                if (tmp_lsm_disk != NULL)
                    lsm_disk_record_free(tmp_lsm_disk);
            }
        }
    }

    ses_enclosure_free(ses_enc);

    if (all_disks != NULL)
        ptr_list_free(all_disks);

    if (udev_disk_list != NULL)
        udev_st_disk_list_free(udev_disk_list, udev_disk_count);

    return rc;
}


int main(int argc, char *argv[])
{
    return lsm_plugin_init_v1(argc, argv, plugin_register, plugin_unregister,
                              plugin_name, plugin_version);
}

#ifdef  __cplusplus
}
#endif
