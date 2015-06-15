/*
 * Copyright (C) 2011-2014 Red Hat, Inc.
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
 * Author: tasleson
 *
 */

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include <string.h>
#define _XOPEN_SOURCE
#include <unistd.h>
#include <crypt.h>
#include <glib.h>
#include <assert.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <openssl/md5.h>

#include "libstoragemgmt/libstoragemgmt_targetport.h"

#ifdef  __cplusplus
extern "C" {
#endif

static char name[] = "Compiled plug-in example";
static char version[] = "0.2.0";
static char sys_id[] = "sim-01";

#define BS 512
#define MAX_SYSTEMS 1
#define MAX_FS 32
#define MAX_EXPORT 32

/**
 * Creates a md5 string (DO NOT FREE RETURN VALUE as the string is static)
 * @param data      Data to generate md5
 * @return Pointer to string which contains the string digest
 */
char *md5(const char *data)
{
    int i = 0;
    MD5_CTX c;
    unsigned char digest[16];
    static char digest_str[33];

    MD5_Init(&c);
    MD5_Update(&c, data, strlen(data));
    MD5_Final(digest, &c);

    for (i = 0; i < sizeof(digest); ++i) {
        sprintf(&digest_str[i * 2], "%02x", (unsigned int) digest[i]);
    } return digest_str;
}

/**
 * Removes an item from an array, shifting the elements and clearing the space
 * that was occupied at the end, use with caution :-)
 * @param array         Base address for the array
 * @param remove_index  Element index to remove
 * @param num_elems     Number of elements currently in the array
 * @param elem_size     Size of each array element
 */
void remove_item(void *array, int remove_index, int num_elems,
                 size_t elem_size)
{
    if (array && (num_elems > 0) && (remove_index < num_elems) && elem_size) {
        /*Are we at the end?, clear that which is at the end */
        if (remove_index + 1 == num_elems) {
            memset(array + (elem_size * (num_elems - 1)), 0, elem_size);
            return;
        }

        /* Calculate the position of the one after that we want to remove */
        void *src_addr = (void *) (array + ((remove_index + 1) * elem_size));

        /* Calculate the destination */
        void *dest_addr = (void *) (array + (remove_index * elem_size));

        /* Shift the memory */
        memmove(dest_addr, src_addr, ((num_elems - 1) - remove_index) *
                elem_size);
        /* Clear that which was at the end */
        memset(array + (elem_size * (num_elems - 1)), 0, elem_size);
    }
}

struct allocated_volume {
    lsm_volume *v;
    lsm_pool *p;
};

struct allocated_fs {
    lsm_fs *fs;
    lsm_pool *p;
    GHashTable *ss;
    GHashTable *exports;
};

struct allocated_ag {
    lsm_access_group *ag;
    lsm_access_group_init_type ag_type;
};

struct plugin_data {
    uint32_t tmo;
    uint32_t num_systems;
    lsm_system *system[MAX_SYSTEMS];

    GHashTable *access_groups;
    GHashTable *group_grant;
    GHashTable *fs;
    GHashTable *jobs;
    GHashTable *pools;
    GHashTable *volumes;
    GHashTable *disks;
};

struct allocated_job {
    int polls;
    lsm_data_type type;
    void *return_data;
};

struct allocated_job *alloc_allocated_job(lsm_data_type type,
                                          void *return_data)
{
    struct allocated_job *rc = malloc(sizeof(struct allocated_job));
    if (rc) {
        rc->polls = 0;
        rc->type = type;
        rc->return_data = return_data;
    }
    return rc;
}

void free_allocated_job(void *j)
{
    struct allocated_job *job = j;

    if (job && job->return_data) {
        switch (job->type) {
        case (LSM_DATA_TYPE_ACCESS_GROUP):
            lsm_access_group_record_free((lsm_access_group *)
                                         job->return_data);
            break;
        case (LSM_DATA_TYPE_BLOCK_RANGE):
            lsm_block_range_record_free((lsm_block_range *)
                                        job->return_data);
            break;
        case (LSM_DATA_TYPE_FS):
            lsm_fs_record_free((lsm_fs *) job->return_data);
            break;
        case (LSM_DATA_TYPE_NFS_EXPORT):
            lsm_nfs_export_record_free((lsm_nfs_export *) job->return_data);
            break;
        case (LSM_DATA_TYPE_POOL):
            lsm_pool_record_free((lsm_pool *) job->return_data);
            break;
        case (LSM_DATA_TYPE_SS):
            lsm_fs_ss_record_free((lsm_fs_ss *) job->return_data);
            break;
        case (LSM_DATA_TYPE_STRING_LIST):
            lsm_string_list_free((lsm_string_list *) job->return_data);
            break;
        case (LSM_DATA_TYPE_SYSTEM):
            lsm_system_record_free((lsm_system *) job->return_data);
            break;
        case (LSM_DATA_TYPE_VOLUME):
            lsm_volume_record_free((lsm_volume *) job->return_data);
            break;
        default:
            break;
        }
        job->return_data = NULL;
    }
    free(job);
}

struct allocated_ag *alloc_allocated_ag(lsm_access_group * ag,
                                        lsm_access_group_init_type i) {
    struct allocated_ag *aag =
        (struct allocated_ag *) malloc(sizeof(struct allocated_ag));
    if (aag) {
        aag->ag = ag;
        aag->ag_type = i;
    }
    return aag;
}

void free_allocated_ag(void *v)
{
    if (v) {
        struct allocated_ag *aag = (struct allocated_ag *) v;
        lsm_access_group_record_free(aag->ag);
        free(aag);
    }
}

void free_pool_record(void *p)
{
    if (p) {
        lsm_pool_record_free((lsm_pool *) p);
    }
}

void free_fs_record(struct allocated_fs *fs)
{
    if (fs) {
        g_hash_table_destroy(fs->ss);
        g_hash_table_destroy(fs->exports);
        lsm_fs_record_free(fs->fs);
        fs->p = NULL;
        free(fs);
    }
}

static void free_ss(void *s)
{
    lsm_fs_ss_record_free((lsm_fs_ss *) s);
}

static void free_export(void *exp)
{
    lsm_nfs_export_record_free((lsm_nfs_export *) exp);
}

static struct allocated_fs *alloc_fs_record()
{
    struct allocated_fs *rc = (struct allocated_fs *)
        malloc(sizeof(struct allocated_fs));
    if (rc) {
        rc->fs = NULL;
        rc->p = NULL;
        rc->ss =
            g_hash_table_new_full(g_str_hash, g_str_equal, free, free_ss);
        rc->exports =
            g_hash_table_new_full(g_str_hash, g_str_equal, free,
                                  free_export);

        if (!rc->ss || !rc->exports) {
            if (rc->ss) {
                g_hash_table_destroy(rc->ss);
            }

            if (rc->exports) {
                g_hash_table_destroy(rc->exports);
            }

            free(rc);
            rc = NULL;
        }
    }
    return rc;
}

static int create_job(struct plugin_data *pd, char **job, lsm_data_type t,
                      void *new_value, void **returned_value)
{
    static int job_num = 0;
    int rc = LSM_ERR_JOB_STARTED;
    char job_id[64];
    char *key = NULL;

    /* Make this random */
    if (0) {
        if (returned_value) {
            *returned_value = new_value;
        }
        *job = NULL;
        rc = LSM_ERR_OK;
    } else {
        snprintf(job_id, sizeof(job_id), "JOB_%d", job_num);
        job_num += 1;

        if (returned_value) {
            *returned_value = NULL;
        }

        *job = strdup(job_id);
        key = strdup(job_id);

        struct allocated_job *value = alloc_allocated_job(t, new_value);
        if (*job && key && value) {
            g_hash_table_insert(pd->jobs, key, value);
        } else {
            free(*job);
            *job = NULL;
            free(key);
            key = NULL;
            free_allocated_job(value);
            value = NULL;
            rc = LSM_ERR_NO_MEMORY;
        }
    }
    return rc;
}

static int tmo_set(lsm_plugin_ptr c, uint32_t timeout, lsm_flag flags)
{
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    if (pd) {
        pd->tmo = timeout;
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

static int tmo_get(lsm_plugin_ptr c, uint32_t * timeout, lsm_flag flags)
{
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    if (pd) {
        *timeout = pd->tmo;
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

static int vol_accessible_by_ag(lsm_plugin_ptr c,
                                lsm_access_group * group,
                                lsm_volume ** volumes[],
                                uint32_t * count, lsm_flag flags);

static int ag_granted_to_volume(lsm_plugin_ptr c,
                                lsm_volume * volume,
                                lsm_access_group ** groups[],
                                uint32_t * count, lsm_flag flags);

static int cap(lsm_plugin_ptr c, lsm_system * system,
               lsm_storage_capabilities ** cap, lsm_flag flags)
{
    int rc = LSM_ERR_NO_MEMORY;
    *cap = lsm_capability_record_alloc(NULL);

    if (*cap) {
        rc = lsm_capability_set_n(*cap, LSM_CAP_SUPPORTED,
                                  LSM_CAP_VOLUMES,
                                  LSM_CAP_VOLUME_CREATE,
                                  LSM_CAP_VOLUME_RESIZE,
                                  LSM_CAP_VOLUME_REPLICATE,
                                  LSM_CAP_VOLUME_REPLICATE_CLONE,
                                  LSM_CAP_VOLUME_REPLICATE_COPY,
                                  LSM_CAP_VOLUME_REPLICATE_MIRROR_ASYNC,
                                  LSM_CAP_VOLUME_REPLICATE_MIRROR_SYNC,
                                  LSM_CAP_VOLUME_COPY_RANGE_BLOCK_SIZE,
                                  LSM_CAP_VOLUME_COPY_RANGE,
                                  LSM_CAP_VOLUME_COPY_RANGE_CLONE,
                                  LSM_CAP_VOLUME_COPY_RANGE_COPY,
                                  LSM_CAP_VOLUME_DELETE,
                                  LSM_CAP_VOLUME_ENABLE,
                                  LSM_CAP_VOLUME_DISABLE,
                                  LSM_CAP_VOLUME_MASK,
                                  LSM_CAP_VOLUME_UNMASK,
                                  LSM_CAP_ACCESS_GROUPS,
                                  LSM_CAP_ACCESS_GROUP_CREATE_ISCSI_IQN,
                                  LSM_CAP_VOLUME_ISCSI_CHAP_AUTHENTICATION,
                                  LSM_CAP_ACCESS_GROUP_CREATE_WWPN,
                                  LSM_CAP_ACCESS_GROUP_INITIATOR_ADD_WWPN,
                                  LSM_CAP_ACCESS_GROUP_INITIATOR_DELETE,
                                  LSM_CAP_ACCESS_GROUP_DELETE,
                                  LSM_CAP_VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP,
                                  LSM_CAP_ACCESS_GROUPS_GRANTED_TO_VOLUME,
                                  LSM_CAP_VOLUME_CHILD_DEPENDENCY,
                                  LSM_CAP_VOLUME_CHILD_DEPENDENCY_RM,
                                  LSM_CAP_FS,
                                  LSM_CAP_FS_DELETE,
                                  LSM_CAP_FS_RESIZE,
                                  LSM_CAP_FS_CREATE,
                                  LSM_CAP_FS_CLONE,
                                  LSM_CAP_FILE_CLONE,
                                  LSM_CAP_FS_SNAPSHOTS,
                                  LSM_CAP_FS_SNAPSHOT_CREATE,
                                  LSM_CAP_FS_SNAPSHOT_DELETE,
                                  LSM_CAP_FS_SNAPSHOT_RESTORE,
                                  LSM_CAP_FS_SNAPSHOT_RESTORE_SPECIFIC_FILES,
                                  LSM_CAP_FS_CHILD_DEPENDENCY,
                                  LSM_CAP_FS_CHILD_DEPENDENCY_RM,
                                  LSM_CAP_FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES,
                                  LSM_CAP_EXPORT_AUTH,
                                  LSM_CAP_EXPORTS,
                                  LSM_CAP_EXPORT_FS,
                                  LSM_CAP_EXPORT_REMOVE,
                                  LSM_CAP_VOLUME_RAID_INFO,
                                  LSM_CAP_POOL_MEMBER_INFO, -1);

        if (LSM_ERR_OK != rc) {
            lsm_capability_record_free(*cap);
            *cap = NULL;
        }
    }
    return rc;
}

static int job_status(lsm_plugin_ptr c, const char *job_id,
                      lsm_job_status * status, uint8_t * percent_complete,
                      lsm_data_type * t, void **value, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    if (pd) {
        struct allocated_job *val = (struct allocated_job *)
            g_hash_table_lookup(pd->jobs, job_id);
        if (val) {
            *status = LSM_JOB_INPROGRESS;

            val->polls += 34;

            if ((val->polls) >= 100) {
                *t = val->type;
                *value = lsm_data_type_copy(val->type, val->return_data);
                *status = LSM_JOB_COMPLETE;
                *percent_complete = 100;
            } else {
                *percent_complete = val->polls;
            }

        } else {
            rc = LSM_ERR_NOT_FOUND_JOB;
        }
    } else {
        rc = LSM_ERR_INVALID_ARGUMENT;
    }

    return rc;
}

static int list_pools(lsm_plugin_ptr c, const char *search_key,
                      const char *search_value, lsm_pool ** pool_array[],
                      uint32_t * count, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    *count = g_hash_table_size(pd->pools);

    if (*count) {
        *pool_array = lsm_pool_record_array_alloc(*count);
        if (*pool_array) {
            uint32_t i = 0;
            char *k = NULL;
            lsm_pool *p = NULL;
            GHashTableIter iter;
            g_hash_table_iter_init(&iter, pd->pools);
            while (g_hash_table_iter_next
                   (&iter, (gpointer) & k, (gpointer) & p)) {
                (*pool_array)[i] = lsm_pool_record_copy(p);
                if (!(*pool_array)[i]) {
                    rc = LSM_ERR_NO_MEMORY;
                    lsm_pool_record_array_free(*pool_array, i);
                    *count = 0;
                    *pool_array = NULL;
                    break;
                }
                ++i;
            }
        } else {
            rc = lsm_log_error_basic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
        }
    }

    if (LSM_ERR_OK == rc) {
        lsm_plug_pool_search_filter(search_key, search_value, *pool_array,
                                    count);
    }

    return rc;
}

static int list_systems(lsm_plugin_ptr c, lsm_system ** systems[],
                        uint32_t * system_count, lsm_flag flags)
{
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    if (pd) {
        *system_count = pd->num_systems;
        *systems = lsm_system_record_array_alloc(MAX_SYSTEMS);

        if (*systems) {
            (*systems)[0] = lsm_system_record_copy(pd->system[0]);

            if ((*systems)[0]) {
                return LSM_ERR_OK;
            } else {
                lsm_system_record_array_free(*systems, pd->num_systems);
            }
        }
        return LSM_ERR_NO_MEMORY;
    } else {
        return LSM_ERR_INVALID_ARGUMENT;
    }
}

static int job_free(lsm_plugin_ptr c, char *job_id, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    if (pd) {
        if (!g_hash_table_remove(pd->jobs, job_id)) {
            rc = LSM_ERR_NOT_FOUND_JOB;
        }
    } else {
        rc = LSM_ERR_INVALID_ARGUMENT;
    }

    return rc;
}

static struct lsm_mgmt_ops_v1 mgm_ops = {
    tmo_set,
    tmo_get,
    cap,
    job_status,
    job_free,
    list_pools,
    list_systems,
};

static int list_volumes(lsm_plugin_ptr c, const char *search_key,
                        const char *search_value,
                        lsm_volume ** vols[], uint32_t * count,
                        lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    *count = g_hash_table_size(pd->volumes);

    if (*count) {
        *vols = lsm_volume_record_array_alloc(*count);
        if (*vols) {
            uint32_t i = 0;
            char *k = NULL;
            struct allocated_volume *vol;
            GHashTableIter iter;
            g_hash_table_iter_init(&iter, pd->volumes);
            while (g_hash_table_iter_next
                   (&iter, (gpointer) & k, (gpointer) & vol)) {
                (*vols)[i] = lsm_volume_record_copy(vol->v);
                if (!(*vols)[i]) {
                    rc = LSM_ERR_NO_MEMORY;
                    lsm_volume_record_array_free(*vols, i);
                    *count = 0;
                    *vols = NULL;
                    break;
                }
                ++i;
            }
        } else {
            rc = lsm_log_error_basic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
        }
    }

    if (LSM_ERR_OK == rc) {
        lsm_plug_volume_search_filter(search_key, search_value, *vols,
                                      count);
    }

    return rc;
}

static int list_disks(lsm_plugin_ptr c, const char *search_key,
                      const char *search_value, lsm_disk ** disks[],
                      uint32_t * count, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    *count = g_hash_table_size(pd->disks);


    if (*count) {
        *disks = lsm_disk_record_array_alloc(*count);
        if (*disks) {
            uint32_t i = 0;
            char *k = NULL;
            lsm_disk *disk;
            GHashTableIter iter;
            g_hash_table_iter_init(&iter, pd->disks);
            while (g_hash_table_iter_next
                   (&iter, (gpointer) & k, (gpointer) & disk)) {
                (*disks)[i] = lsm_disk_record_copy(disk);
                if (!(*disks)[i]) {
                    rc = LSM_ERR_NO_MEMORY;
                    lsm_disk_record_array_free(*disks, i);
                    *count = 0;
                    *disks = NULL;
                    break;
                }
                ++i;
            }
        } else {
            rc = lsm_log_error_basic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
        }
    }

    if (LSM_ERR_OK == rc) {
        lsm_plug_disk_search_filter(search_key, search_value, *disks,
                                    count);
    }

    return rc;
}

static int list_targets(lsm_plugin_ptr c, const char *search_key,
                        const char *search_value, lsm_target_port ** tp[],
                        uint32_t * count, lsm_flag flags)
{
    uint32_t i = 0;
    const char p0[] = "50:0a:09:86:99:4b:8d:c5";
    const char p1[] = "50:0a:09:86:99:4b:8d:c6";
    int rc = LSM_ERR_OK;

    *count = 5;
    *tp = lsm_target_port_record_array_alloc(*count);

    if (*tp) {
        (*tp)[0] = lsm_target_port_record_alloc("TGT_PORT_ID_01",
                                                LSM_TARGET_PORT_TYPE_FC, p0,
                                                p0, p0, "FC_a_0b", sys_id,
                                                NULL);

        (*tp)[1] = lsm_target_port_record_alloc("TGT_PORT_ID_02",
                                                LSM_TARGET_PORT_TYPE_FCOE,
                                                p1, p1, p1, "FC_a_0c",
                                                sys_id, NULL);

        (*tp)[2] = lsm_target_port_record_alloc("TGT_PORT_ID_03",
                                        LSM_TARGET_PORT_TYPE_ISCSI,
                                        "iqn.1986-05.com.example:sim-tgt-03",
                                        "sim-iscsi-tgt-3.example.com:3260",
                                        "a4:4e:31:47:f4:e0",
                                        "iSCSI_c_0d", sys_id, NULL);

        (*tp)[3] = lsm_target_port_record_alloc("TGT_PORT_ID_04",
                                        LSM_TARGET_PORT_TYPE_ISCSI,
                                        "iqn.1986-05.com.example:sim-tgt-03",
                                        "10.0.0.1:3260",
                                        "a4:4e:31:47:f4:e1",
                                        "iSCSI_c_0e", sys_id, NULL);

        (*tp)[4] = lsm_target_port_record_alloc("TGT_PORT_ID_05",
                                        LSM_TARGET_PORT_TYPE_ISCSI,
                                        "iqn.1986-05.com.example:sim-tgt-03",
                                        "[2001:470:1f09:efe:a64e:31ff::1]:3260",
                                        "a4:4e:31:47:f4:e1",
                                        "iSCSI_c_0e", sys_id, NULL);

        for (i = 0; i < *count; ++i) {
            if (!(*tp)[i]) {
                rc = lsm_log_error_basic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
                lsm_target_port_record_array_free(*tp, *count);
                *count = 0;
                break;
            }
        }
    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
        *count = 0;
    }

    if (LSM_ERR_OK == rc) {
        lsm_plug_target_port_search_filter(search_key, search_value, *tp,
                                           count);
    }

    return rc;
}

static uint64_t pool_allocate(lsm_pool * p, uint64_t size)
{
    uint64_t rounded_size = 0;
    uint64_t free_space = lsm_pool_free_space_get(p);

    rounded_size = (size / BS) * BS;

    if (free_space >= rounded_size) {
        free_space -= rounded_size;
        lsm_pool_free_space_set(p, free_space);
    } else {
        rounded_size = 0;
    }
    return rounded_size;
}

void pool_deallocate(lsm_pool * p, uint64_t size)
{
    uint64_t free_space = lsm_pool_free_space_get(p);

    free_space += size;
    lsm_pool_free_space_set(p, free_space);
}

static lsm_pool *find_pool(struct plugin_data *pd, const char *pool_id)
{
    return (lsm_pool *) g_hash_table_lookup(pd->pools, pool_id);
}

static struct allocated_volume *find_volume(struct plugin_data *pd,
                                            const char *vol_id)
{
    struct allocated_volume *rc = g_hash_table_lookup(pd->volumes, vol_id);
    return rc;
}

static struct allocated_volume *find_volume_name(struct plugin_data *pd,
                                                 const char *name)
{
    struct allocated_volume *found = NULL;
    char *k = NULL;
    struct allocated_volume *vol;
    GHashTableIter iter;
    g_hash_table_iter_init(&iter, pd->volumes);
    while (g_hash_table_iter_next(&iter, (gpointer) & k, (gpointer) & vol)) {
        if (strcmp(lsm_volume_name_get(vol->v), name) == 0) {
            found = vol;
            break;
        }
    }
    return found;
}

static int volume_create(lsm_plugin_ptr c, lsm_pool * pool,
                         const char *volume_name, uint64_t size,
                         lsm_volume_provision_type provisioning,
                         lsm_volume ** new_volume, char **job,
                         lsm_flag flags)
{
    int rc = LSM_ERR_OK;

    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    lsm_pool *p = find_pool(pd, lsm_pool_id_get(pool));

    if (p) {
        if (!find_volume_name(pd, volume_name)) {
            uint64_t allocated_size = pool_allocate(p, size);
            if (allocated_size) {
                char *id = md5(volume_name);

                /* We create one to return and a copy to store in memory */

                lsm_volume *v = lsm_volume_record_alloc(id, volume_name,
                                   "60a980003246694a412b45673342616e",
                                    BS, allocated_size/BS, 0, sys_id,
                                    lsm_pool_id_get(pool), NULL);

                lsm_volume *to_store = lsm_volume_record_copy(v);
                struct allocated_volume *av =
                    malloc(sizeof(struct allocated_volume));

                if (v && av && to_store) {
                    av->v = to_store;
                    av->p = p;

                    /*
                     * Make a copy of the key, as we may replace the volume,
                     * but leave the key.
                     */
                    g_hash_table_insert(pd->volumes, (gpointer)
                                        strdup(lsm_volume_id_get(to_store)),
                                        (gpointer) av);

                    rc = create_job(pd, job, LSM_DATA_TYPE_VOLUME, v,
                                    (void **) new_volume);

                } else {
                    free(av);
                    lsm_volume_record_free(v);
                    lsm_volume_record_free(to_store);
                    rc = lsm_log_error_basic(c, LSM_ERR_NO_MEMORY,
                                             "Check for leaks");
                }

            } else {
                rc = lsm_log_error_basic(c, LSM_ERR_NOT_ENOUGH_SPACE,
                                         "Insufficient space in pool");
            }

        } else {
            rc = lsm_log_error_basic(c, LSM_ERR_NAME_CONFLICT,
                                     "Existing volume " "with name");
        }
    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_POOL,
                                 "Pool not found!");
    }
    return rc;
}

static int volume_replicate(lsm_plugin_ptr c, lsm_pool * pool,
                            lsm_replication_type rep_type,
                            lsm_volume * volume_src, const char *name,
                            lsm_volume ** new_replicant, char **job,
                            lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    lsm_pool *pool_to_use = NULL;

    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    if (pool) {
        pool_to_use = find_pool(pd, lsm_pool_id_get(pool));
    } else {
        pool_to_use = find_pool(pd, lsm_volume_pool_id_get(volume_src));
    }

    if (!pool_to_use) {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_POOL,
                                 "Pool not found!");
    } else {
        if (find_volume(pd, lsm_volume_id_get(volume_src))) {
            rc = volume_create(c, pool_to_use, name,
                               lsm_volume_number_of_blocks_get(volume_src) *
                               BS, LSM_VOLUME_PROVISION_DEFAULT,
                               new_replicant, job, flags);
        } else {
            rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                     "Volume not found!");
        }
    }
    return rc;
}

static int volume_replicate_range_bs(lsm_plugin_ptr c, lsm_system * system,
                                     uint32_t * bs, lsm_flag flags)
{
    *bs = BS;
    return LSM_ERR_OK;
}

static int volume_replicate_range(lsm_plugin_ptr c,
                                  lsm_replication_type rep_type,
                                  lsm_volume * source,
                                  lsm_volume * dest,
                                  lsm_block_range ** ranges,
                                  uint32_t num_ranges, char **job,
                                  lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    struct allocated_volume *src_v =
        find_volume(pd, lsm_volume_id_get(source));
    struct allocated_volume *dest_v =
        find_volume(pd, lsm_volume_id_get(dest));

    if (!src_v || !dest_v) {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                 "Src or dest volumes not found!");
    } else {
        rc = create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);
    }

    return rc;
}

static int volume_resize(lsm_plugin_ptr c, lsm_volume * volume,
                         uint64_t new_size, lsm_volume ** resized_volume,
                         char **job, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    struct allocated_volume *av =
        find_volume(pd, lsm_volume_id_get(volume));

    if (av) {
        lsm_volume *v = av->v;
        lsm_pool *p = av->p;
        uint64_t curr_size = lsm_volume_number_of_blocks_get(v) * BS;

        pool_deallocate(p, curr_size);
        uint64_t resized_size = pool_allocate(p, new_size);
        if (resized_size) {
            lsm_volume *vp = lsm_volume_record_alloc(lsm_volume_id_get(v),
                                                    lsm_volume_name_get(v),
                                                    lsm_volume_vpd83_get(v),
                                                    lsm_volume_block_size_get(v),
                                                    resized_size/BS, 0, sys_id,
                                                    lsm_volume_pool_id_get(volume),
                                                    NULL);

            if( vp ) {
                av->v = vp;
                lsm_volume_record_free(v);
                rc = create_job(pd, job, LSM_DATA_TYPE_VOLUME,
                                lsm_volume_record_copy(vp),
                                (void **) resized_volume);
            } else {
                pool_deallocate(p, resized_size);
                pool_allocate(p, curr_size);
                rc = lsm_log_error_basic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
            }

        } else {
            /*Could not accommodate re-sized, go back */
            pool_allocate(p, curr_size);
            rc = lsm_log_error_basic(c, LSM_ERR_NOT_ENOUGH_SPACE,
                                     "Insufficient space in pool");
        }

    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                 "volume not found!");
    }
    return rc;
}

static int _volume_delete(lsm_plugin_ptr c, const char *volume_id)
{
    int rc = LSM_ERR_OK;
    GHashTableIter iter;
    char *k = NULL;
    GHashTable *v = NULL;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    struct allocated_volume *av = find_volume(pd, volume_id);

    if (av) {
        lsm_volume *vp = av->v;
        pool_deallocate(av->p, lsm_volume_number_of_blocks_get(vp) * BS);

        g_hash_table_remove(pd->volumes, volume_id);

        g_hash_table_iter_init(&iter, pd->group_grant);
        while (g_hash_table_iter_next
               (&iter, (gpointer) & k, (gpointer) & v)) {
            if (g_hash_table_lookup(v, volume_id)) {
                g_hash_table_remove(v, volume_id);
            }
        }

    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                 "volume not found!");
    }
    return rc;
}

static int volume_delete(lsm_plugin_ptr c, lsm_volume * volume,
                         char **job, lsm_flag flags)
{
    lsm_access_group **groups = NULL;
    uint32_t count = 0;

    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    // Check to see if this volume is masked to any access groups, if it is we
    // will return an IS_MASKED error code.
    int rc = ag_granted_to_volume(c, volume, &groups, &count,
                                  LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_OK == rc) {
        lsm_access_group_record_array_free(groups, count);
        groups = NULL;

        if (!count) {

            rc = _volume_delete(c, lsm_volume_id_get(volume));

            if (LSM_ERR_OK == rc) {
                rc = create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);
            }
        } else {
            rc = lsm_log_error_basic(c, LSM_ERR_IS_MASKED,
                                     "Volume is masked!");
        }
    }
    return rc;
}

static int volume_raid_info(lsm_plugin_ptr c, lsm_volume * volume,
                            lsm_volume_raid_type * raid_type,
                            uint32_t * strip_size, uint32_t * disk_count,
                            uint32_t * min_io_size, uint32_t * opt_io_size,
                            lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    struct allocated_volume *av =
        find_volume(pd, lsm_volume_id_get(volume));

    if (!av) {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                 "volume not found!");
    }

    *raid_type = LSM_VOLUME_RAID_TYPE_UNKNOWN;
    *strip_size = LSM_VOLUME_STRIP_SIZE_UNKNOWN;
    *disk_count = LSM_VOLUME_DISK_COUNT_UNKNOWN;
    *min_io_size = LSM_VOLUME_MIN_IO_SIZE_UNKNOWN;
    *opt_io_size = LSM_VOLUME_OPT_IO_SIZE_UNKNOWN;
    return rc;
}

static int pool_member_info(lsm_plugin_ptr c, lsm_pool * pool,
                            lsm_volume_raid_type * raid_type,
                            lsm_pool_member_type * member_type,
                            lsm_string_list ** member_ids, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    lsm_pool *p = find_pool(pd, lsm_pool_id_get(pool));

    if (!p) {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_POOL,
                                 "Pool not found!");
    }

    *raid_type = LSM_VOLUME_RAID_TYPE_UNKNOWN;
    *member_type = LSM_POOL_MEMBER_TYPE_UNKNOWN;
    *member_ids = NULL;
    return rc;
}

static int volume_raid_create_cap_get(lsm_plugin_ptr c, lsm_system * system,
                                      uint32_t ** supported_raid_types,
                                      uint32_t * supported_raid_type_count,
                                      uint32_t ** supported_strip_sizes,
                                      uint32_t * supported_strip_size_count,
                                      lsm_flag flags)
{
    return LSM_ERR_NO_SUPPORT;
}

static int volume_raid_create(lsm_plugin_ptr c, const char *name,
                              lsm_volume_raid_type raid_type,
                              lsm_disk * disks[], uint32_t disk_count,
                              uint32_t strip_size, lsm_volume ** new_volume,
                              lsm_flag flags)
{
    return LSM_ERR_NO_SUPPORT;
}

static struct lsm_ops_v1_2 ops_v1_2 = {
    volume_raid_info,
    pool_member_info,
    volume_raid_create_cap_get,
    volume_raid_create,
};

static int volume_enable_disable(lsm_plugin_ptr c, lsm_volume * v,
                                 lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    struct allocated_volume *av = find_volume(pd, lsm_volume_id_get(v));

    if (!av) {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                 "volume not found!");
    }
    return rc;
}

static int access_group_list(lsm_plugin_ptr c,
                             const char *search_key,
                             const char *search_value,
                             lsm_access_group ** groups[],
                             uint32_t * group_count, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    *group_count = g_hash_table_size(pd->access_groups);

    if (*group_count) {
        *groups = lsm_access_group_record_array_alloc(*group_count);
        if (*groups) {
            int i = 0;
            char *key = NULL;
            struct allocated_ag *val = NULL;
            GHashTableIter iter;

            g_hash_table_iter_init(&iter, pd->access_groups);

            while (g_hash_table_iter_next(&iter, (gpointer) & key,
                                          (gpointer) & val)) {
                (*groups)[i] = lsm_access_group_record_copy(val->ag);
                if (!(*groups)[i]) {
                    rc = LSM_ERR_NO_MEMORY;
                    lsm_access_group_record_array_free(*groups, i);
                    *group_count = 0;
                    groups = NULL;
                    break;
                }
                ++i;
            }
        } else {
            rc = LSM_ERR_NO_MEMORY;
        }
    }

    if (LSM_ERR_OK == rc) {
        lsm_plug_access_group_search_filter(search_key, search_value,
                                            *groups, group_count);
    }

    return rc;
}

static int _find_dup_init(struct plugin_data *pd, const char *initiator_id)
{
    GList *all_aags = g_hash_table_get_values(pd->access_groups);
    guint y;
    int rc = 1;
    for (y = 0; y < g_list_length(all_aags); ++y) {
        struct allocated_ag *cur_aag =
            (struct allocated_ag *) g_list_nth_data(all_aags, y);
        if (cur_aag) {
            lsm_string_list *inits =
                lsm_access_group_initiator_id_get(cur_aag->ag);
            int i;
            for (i = 0; i < lsm_string_list_size(inits); ++i) {
                const char *cur_init_id =
                    lsm_string_list_elem_get(inits, i);
                if (strcmp(initiator_id, cur_init_id) == 0) {
                    rc = 0;
                    break;
                }
            }
            if (rc == 0) {
                break;
            } else {
                cur_aag = (struct allocated_ag *) g_list_next(all_aags);
            }
        }
    }
    g_list_free(all_aags);
    return rc;
}

static int access_group_create(lsm_plugin_ptr c,
                               const char *name,
                               const char *initiator_id,
                               lsm_access_group_init_type id_type,
                               lsm_system * system,
                               lsm_access_group ** access_group,
                               lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    lsm_access_group *ag = NULL;
    struct allocated_ag *aag = NULL;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    char *id = strdup(md5(name));

    struct allocated_ag *find = (struct allocated_ag *)
        g_hash_table_lookup(pd->access_groups, id);

    if (!find) {
        // check initiator conflict
        if (_find_dup_init(pd, initiator_id) == 0) {
            rc = lsm_log_error_basic(c, LSM_ERR_EXISTS_INITIATOR,
                                     "Requested initiator is used by other access group");
        } else {
            lsm_string_list *initiators = lsm_string_list_alloc(1);
            if (initiators && id &&
                (LSM_ERR_OK ==
                 lsm_string_list_elem_set(initiators, 0, initiator_id))) {
                ag = lsm_access_group_record_alloc(id, name, initiators,
                                                   id_type,
                                                   lsm_system_id_get
                                                   (system), NULL);
                aag = alloc_allocated_ag(ag, id_type);
                if (ag && aag) {
                    g_hash_table_insert(pd->access_groups, (gpointer) id,
                                        (gpointer) aag);
                    *access_group = lsm_access_group_record_copy(ag);
                } else {
                    free_allocated_ag(aag);
                    lsm_access_group_record_free(ag);
                    rc = lsm_log_error_basic(c, LSM_ERR_NO_MEMORY,
                                             "ENOMEM");
                }
            } else {
                rc = lsm_log_error_basic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
            }
            /* Initiators is copied when allocating a group record */
            lsm_string_list_free(initiators);
        }
    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NAME_CONFLICT,
                                 "access group with same id found");
    }

    /*
     *  If we were not successful free memory for id string, id is on the heap
     * because it is passed to the hash table.
     */
    if (LSM_ERR_OK != rc) {
        free(id);
    }

    return rc;
}

static int access_group_delete(lsm_plugin_ptr c,
                               lsm_access_group * group, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    lsm_volume **volumes = NULL;
    uint32_t count = 0;

    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    const char *id = lsm_access_group_id_get(group);

    rc = vol_accessible_by_ag(c, group, &volumes, &count,
                              LSM_CLIENT_FLAG_RSVD);
    lsm_volume_record_array_free(volumes, count);
    volumes = NULL;

    if (rc == LSM_ERR_OK) {
        if (count) {
            rc = lsm_log_error_basic(c, LSM_ERR_IS_MASKED,
                                     "access group has masked volumes!");
        } else {
            gboolean r =
                g_hash_table_remove(pd->access_groups, (gpointer) id);

            if (!r) {
                rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_ACCESS_GROUP,
                                         "access group not found");
            } else {
                g_hash_table_remove(pd->group_grant, id);
            }

            if (!g_hash_table_size(pd->access_groups)) {
                assert(g_hash_table_size(pd->group_grant) == 0);
            }
        }
    }

    return rc;
}

static int access_group_initiator_add(lsm_plugin_ptr c,
                                      lsm_access_group * group,
                                      const char *initiator_id,
                                      lsm_access_group_init_type id_type,
                                      lsm_access_group **
                                      updated_access_group,
                                      lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    struct allocated_ag *find = (struct allocated_ag *)
        g_hash_table_lookup(pd->access_groups,
                            lsm_access_group_id_get(group));

    if (find) {
        lsm_string_list *inits =
            lsm_access_group_initiator_id_get(find->ag);
        rc = lsm_string_list_append(inits, initiator_id);

        if (LSM_ERR_OK == rc) {
            *updated_access_group = lsm_access_group_record_copy(find->ag);
            if (!*updated_access_group) {
                rc = LSM_ERR_NO_MEMORY;
            }
        }
    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_ACCESS_GROUP,
                                 "access group not found");
    }
    return rc;
}

static int access_group_initiator_delete(lsm_plugin_ptr c,
                                         lsm_access_group * group,
                                         const char *initiator_id,
                                         lsm_access_group_init_type id_type,
                                         lsm_access_group **
                                         updated_access_group,
                                         lsm_flag flags)
{
    int rc = LSM_ERR_INVALID_ARGUMENT;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    struct allocated_ag *find = (struct allocated_ag *)
        g_hash_table_lookup(pd->access_groups,
                            lsm_access_group_id_get(group));

    if (find) {
        uint32_t i;
        lsm_string_list *inits =
            lsm_access_group_initiator_id_get(find->ag);

        for (i = 0; i < lsm_string_list_size(inits); ++i) {
            if (strcmp(initiator_id, lsm_string_list_elem_get(inits, i)) ==
                0) {
                lsm_string_list_delete(inits, i);
                rc = LSM_ERR_OK;
                break;
            }
        }

        if (LSM_ERR_OK == rc) {
            *updated_access_group = lsm_access_group_record_copy(find->ag);
            if (!*updated_access_group) {
                rc = LSM_ERR_NO_MEMORY;
            }
        }

    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_ACCESS_GROUP,
                                 "access group not found");
    }
    return rc;
}

static int volume_mask(lsm_plugin_ptr c,
                       lsm_access_group * group,
                       lsm_volume * volume, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    struct allocated_ag *find = (struct allocated_ag *)
        g_hash_table_lookup(pd->access_groups,
                            lsm_access_group_id_get(group));

    struct allocated_volume *av =
        find_volume(pd, lsm_volume_id_get(volume));

    if (find && av) {
        GHashTable *grants = g_hash_table_lookup(pd->group_grant,
                                                 lsm_access_group_id_get
                                                 (find->ag));
        if (!grants) {
            /* We don't have any mappings for this access group */
            GHashTable *grant =
                g_hash_table_new_full(g_str_hash, g_str_equal,
                                      free, free);
            char *key = strdup(lsm_access_group_id_get(find->ag));
            char *vol_id = strdup(lsm_volume_id_get(volume));
            int *val = (int *) malloc(sizeof(int));

            if (grant && key && val && vol_id) {
                *val = 1;

                /* Create the association for volume id and access value */
                g_hash_table_insert(grant, vol_id, val);

                /* Create the association for access groups */
                g_hash_table_insert(pd->group_grant, key, grant);

            } else {
                rc = LSM_ERR_NO_MEMORY;
                free(key);
                free(val);
                free(vol_id);
                if (grant) {
                    g_hash_table_destroy(grant);
                    grant = NULL;
                }
            }

        } else {
            /* See if we have this volume in the access grants */
            char *vol_id =
                g_hash_table_lookup(grants, lsm_volume_id_get(volume));
            if (!vol_id) {
                vol_id = strdup(lsm_volume_id_get(volume));
                int *val = (int *) malloc(sizeof(int));
                if (vol_id && val) {
                    *val = 1;
                    g_hash_table_insert(grants, vol_id, val);
                } else {
                    rc = LSM_ERR_NO_MEMORY;
                    free(vol_id);
                    free(val);
                }

            } else {
                rc = LSM_ERR_NO_STATE_CHANGE;
            }
        }
    } else {
        if (!av) {
            rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                     "volume not found");
        } else {
            rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_ACCESS_GROUP,
                                     "access group not found");
        }
    }
    return rc;
}

static int volume_unmask(lsm_plugin_ptr c,
                         lsm_access_group * group,
                         lsm_volume * volume, lsm_flag flags)
{
    int rc = LSM_ERR_NO_STATE_CHANGE;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    struct allocated_ag *find = (struct allocated_ag *)
        g_hash_table_lookup(pd->access_groups,
                            lsm_access_group_id_get(group));

    struct allocated_volume *av =
        find_volume(pd, lsm_volume_id_get(volume));

    if (find && av) {
        GHashTable *grants = g_hash_table_lookup(pd->group_grant,
                                                 lsm_access_group_id_get
                                                 (find->ag));

        if (grants) {
            char *vol_id =
                g_hash_table_lookup(grants, lsm_volume_id_get(volume));
            if (vol_id) {
                g_hash_table_remove(grants, lsm_volume_id_get(volume));
                rc = LSM_ERR_OK;
            } else {
                rc = LSM_ERR_NO_STATE_CHANGE;
            }
        }

    } else {
        if (!av) {
            rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                     "volume not found");
        } else {
            rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_ACCESS_GROUP,
                                     "access group not found");
        }
    }
    return rc;
}

static lsm_volume *get_volume_by_id(struct plugin_data *pd, const char *id)
{
    struct allocated_volume *av = find_volume(pd, id);
    if (av) {
        return av->v;
    }
    return NULL;
}

static int vol_accessible_by_ag(lsm_plugin_ptr c,
                                lsm_access_group * group,
                                lsm_volume ** volumes[],
                                uint32_t * count, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    struct allocated_ag *find = (struct allocated_ag *)
        g_hash_table_lookup(pd->access_groups,
                            lsm_access_group_id_get(group));
    if (find) {
        GHashTable *grants = g_hash_table_lookup(pd->group_grant,
                                                 lsm_access_group_id_get
                                                 (find->ag));
        *count = 0;

        if (grants && g_hash_table_size(grants)) {
            *count = g_hash_table_size(grants);
            GList *keys = g_hash_table_get_keys(grants);
            *volumes = lsm_volume_record_array_alloc(*count);

            if (keys && *volumes) {
                GList *curr = NULL;
                int i = 0;

                for (curr = g_list_first(keys);
                     curr != NULL; curr = g_list_next(curr), ++i) {

                    (*volumes)[i] =
                        lsm_volume_record_copy(get_volume_by_id
                                               (pd, (char *) curr->data));
                    if (!(*volumes)[i]) {
                        rc = LSM_ERR_NO_MEMORY;
                        lsm_volume_record_array_free(*volumes, i);
                        *volumes = NULL;
                        *count = 0;
                        break;
                    }
                }

                /* Free the keys */
                g_list_free(keys);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        }

    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_ACCESS_GROUP,
                                 "access group not found");
    }
    return rc;
}

static lsm_access_group *access_group_by_id(struct plugin_data *pd,
                                            const char *key)
{
    struct allocated_ag *find = g_hash_table_lookup(pd->access_groups, key);
    if (find) {
        return find->ag;
    }
    return NULL;
}

static int ag_granted_to_volume(lsm_plugin_ptr c,
                                lsm_volume * volume,
                                lsm_access_group ** groups[],
                                uint32_t * count, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    GHashTableIter iter;
    char *k = NULL;
    GHashTable *v = NULL;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    const char *volume_id = lsm_volume_id_get(volume);
    g_hash_table_iter_init(&iter, pd->group_grant);
    GSList *result = NULL;

    *count = 0;

    while (g_hash_table_iter_next(&iter, (gpointer) & k, (gpointer) & v)) {
        if (g_hash_table_lookup(v, volume_id)) {
            *count += 1;
            result = g_slist_prepend(result, access_group_by_id(pd, k));
        }
    }

    if (*count) {
        int i = 0;
        *groups = lsm_access_group_record_array_alloc(*count);
        GSList *siter = NULL;

        if (*groups) {
            for (siter = result; siter; siter = g_slist_next(siter), i++) {
                (*groups)[i] =
                    lsm_access_group_record_copy((lsm_access_group *)
                                                 siter->data);

                if (!(*groups)[i]) {
                    rc = LSM_ERR_NO_MEMORY;
                    lsm_access_group_record_array_free(*groups, i);
                    *groups = NULL;
                    *count = 0;
                    break;
                }
            }
        } else {
            rc = LSM_ERR_NO_MEMORY;
        }
    }

    if (result) {
        g_slist_free(result);
    }
    return rc;
}

int static volume_dependency(lsm_plugin_ptr c,
                             lsm_volume * volume,
                             uint8_t * yes, lsm_flag flags)
{
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    struct allocated_volume *av =
        find_volume(pd, lsm_volume_id_get(volume));

    if (av) {
        *yes = 0;
        return LSM_ERR_OK;
    } else {
        return LSM_ERR_NOT_FOUND_VOLUME;
    }
}

int static volume_dependency_rm(lsm_plugin_ptr c,
                                lsm_volume * volume,
                                char **job, lsm_flag flags)
{
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    struct allocated_volume *av =
        find_volume(pd, lsm_volume_id_get(volume));

    if (av) {
        return create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);
    } else {
        return LSM_ERR_NOT_FOUND_VOLUME;
    }
}

static int iscsi_chap_auth(lsm_plugin_ptr c, const char *init_id,
                           const char *in_user, const char *in_password,
                           const char *out_user, const char *out_password,
                           lsm_flag flags)
{
    if (init_id) {
        return 0;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

static struct lsm_san_ops_v1 san_ops = {
    list_volumes,
    list_disks,
    volume_create,
    volume_replicate,
    volume_replicate_range_bs,
    volume_replicate_range,
    volume_resize,
    volume_delete,
    volume_enable_disable,
    volume_enable_disable,
    iscsi_chap_auth,
    access_group_list,
    access_group_create,
    access_group_delete,
    access_group_initiator_add,
    access_group_initiator_delete,
    volume_mask,
    volume_unmask,
    vol_accessible_by_ag,
    ag_granted_to_volume,
    volume_dependency,
    volume_dependency_rm,
    list_targets
};

static int fs_list(lsm_plugin_ptr c, const char *search_key,
                   const char *search_value, lsm_fs ** fs[],
                   uint32_t * count, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    *count = g_hash_table_size(pd->fs);

    if (*count) {
        *fs = lsm_fs_record_array_alloc(*count);
        if (*fs) {
            uint32_t i = 0;
            char *k = NULL;
            struct allocated_fs *afs = NULL;
            GHashTableIter iter;
            g_hash_table_iter_init(&iter, pd->fs);
            while (g_hash_table_iter_next
                   (&iter, (gpointer) & k, (gpointer) & afs)) {
                (*fs)[i] = lsm_fs_record_copy(afs->fs);
                if (!(*fs)[i]) {
                    rc = LSM_ERR_NO_MEMORY;
                    lsm_fs_record_array_free(*fs, i);
                    *count = 0;
                    *fs = NULL;
                    break;
                }
                ++i;
            }
        } else {
            rc = lsm_log_error_basic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
        }
    }

    if (LSM_ERR_OK == rc) {
        lsm_plug_fs_search_filter(search_key, search_value, *fs, count);
    }

    return rc;
}

static int fs_create(lsm_plugin_ptr c, lsm_pool * pool, const char *name,
                     uint64_t size_bytes, lsm_fs ** fs, char **job,
                     lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    lsm_pool *p = find_pool(pd, lsm_pool_id_get(pool));


    if (p && !g_hash_table_lookup(pd->fs, md5(name))) {
        uint64_t allocated_size = pool_allocate(p, size_bytes);
        if (allocated_size) {
            char *id = md5(name);
            char *key = strdup(id);
            lsm_fs *new_fs = NULL;

            /* Make a copy to store and a copy to hand back to caller */
            lsm_fs *tfs = lsm_fs_record_alloc(id, name, allocated_size,
                                              allocated_size,
                                              lsm_pool_id_get(pool), sys_id,
                                              NULL);
            new_fs = lsm_fs_record_copy(tfs);

            /* Allocate the memory to keep the associations */
            struct allocated_fs *afs = alloc_fs_record();

            if (key && tfs && afs) {
                afs->fs = tfs;
                afs->p = p;
                g_hash_table_insert(pd->fs, key, afs);

                rc = create_job(pd, job, LSM_DATA_TYPE_FS, new_fs,
                                (void **) fs);
            } else {
                free(key);
                lsm_fs_record_free(new_fs);
                lsm_fs_record_free(tfs);
                free_fs_record(afs);

                *fs = NULL;
                rc = lsm_log_error_basic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
            }
        } else {
            rc = lsm_log_error_basic(c, LSM_ERR_NOT_ENOUGH_SPACE,
                                     "Insufficient space in pool");
        }
    } else {
        if (p == NULL) {
            rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_POOL,
                                     "Pool not found!");
        } else {
            rc = lsm_log_error_basic(c, LSM_ERR_NAME_CONFLICT,
                                     "File system with name exists");
        }
    }
    return rc;
}

static int fs_delete(lsm_plugin_ptr c, lsm_fs * fs, char **job,
                     lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    if (!g_hash_table_remove(pd->fs, lsm_fs_id_get(fs))) {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_FS, "FS not found!");
    } else {
        rc = create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);
    }
    return rc;
}

static int fs_resize(lsm_plugin_ptr c, lsm_fs * fs,
                     uint64_t new_size_bytes, lsm_fs * *rfs,
                     char **job, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    struct allocated_fs *afs =
        g_hash_table_lookup(pd->fs, lsm_fs_id_get(fs));

    *rfs = NULL;
    *job = NULL;

    if (afs) {
        lsm_pool *p = afs->p;
        lsm_fs *tfs = afs->fs;

        pool_deallocate(p, lsm_fs_total_space_get(tfs));
        uint64_t resized_size = pool_allocate(p, new_size_bytes);

        if (resized_size) {

            lsm_fs *resized = lsm_fs_record_alloc(lsm_fs_id_get(tfs),
                                                  lsm_fs_name_get(tfs),
                                                  new_size_bytes,
                                                  new_size_bytes,
                                                  lsm_fs_pool_id_get(tfs),
                                                  lsm_fs_system_id_get(tfs),
                                                  NULL);
            lsm_fs *returned_copy = lsm_fs_record_copy(resized);

            if (resized && returned_copy) {
                lsm_fs_record_free(tfs);
                afs->fs = resized;

                rc = create_job(pd, job, LSM_DATA_TYPE_FS, returned_copy,
                                (void **) rfs);

            } else {
                lsm_fs_record_free(resized);
                lsm_fs_record_free(returned_copy);
                *rfs = NULL;

                pool_deallocate(p, new_size_bytes);
                pool_allocate(p, lsm_fs_total_space_get(tfs));
                rc = lsm_log_error_basic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
            }
        } else {
            /*Could not accommodate re-sized, go back */
            pool_allocate(p, lsm_fs_total_space_get(tfs));
            rc = lsm_log_error_basic(c, LSM_ERR_NOT_ENOUGH_SPACE,
                                     "Insufficient space in pool");
        }

    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_FS,
                                 "file system not found!");
    }
    return rc;
}

static int fs_clone(lsm_plugin_ptr c, lsm_fs * src_fs,
                    const char *dest_fs_name, lsm_fs ** cloned_fs,
                    lsm_fs_ss * optional_snapshot, char **job,
                    lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    struct allocated_fs *find =
        g_hash_table_lookup(pd->fs, lsm_fs_id_get(src_fs));

    if (find) {
        rc = fs_create(c, find->p, dest_fs_name,
                       lsm_fs_total_space_get(find->fs), cloned_fs, job,
                       flags);
    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_FS,
                                 "Source fs not found");
    }

    return rc;
}

static int fs_file_clone(lsm_plugin_ptr c, lsm_fs * fs,
                         const char *src_file_name,
                         const char *dest_file_name,
                         lsm_fs_ss * snapshot, char **job, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    struct allocated_fs *find =
        (struct allocated_fs *) g_hash_table_lookup(pd->fs,
                                                    lsm_fs_id_get(fs));
    if (!find) {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    } else {
        rc = create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);
    }
    return rc;
}

static int fs_child_dependency(lsm_plugin_ptr c, lsm_fs * fs,
                               lsm_string_list * files, uint8_t * yes)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    if (g_hash_table_lookup(pd->fs, lsm_fs_id_get(fs))) {
        *yes = 0;
    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    }
    return rc;
}

static int fs_child_dependency_rm(lsm_plugin_ptr c, lsm_fs * fs,
                                  lsm_string_list * files,
                                  char **job, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    if (!g_hash_table_lookup(pd->fs, lsm_fs_id_get(fs))) {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    } else {
        rc = create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);
    }
    return rc;
}

static int ss_list(lsm_plugin_ptr c, lsm_fs * fs, lsm_fs_ss ** ss[],
                   uint32_t * count, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    struct allocated_fs *find =
        (struct allocated_fs *) g_hash_table_lookup(pd->fs,
                                                    lsm_fs_id_get(fs));

    if (find) {
        char *k = NULL;
        lsm_fs_ss *v = NULL;
        GHashTableIter iter;

        *ss = NULL;
        *count = g_hash_table_size(find->ss);

        if (*count) {
            *ss = lsm_fs_ss_record_array_alloc(*count);
            if (*ss) {
                int i = 0;
                g_hash_table_iter_init(&iter, find->ss);

                while (g_hash_table_iter_next(&iter,
                                              (gpointer) & k,
                                              (gpointer) & v)) {
                    (*ss)[i] = lsm_fs_ss_record_copy(v);
                    if (!(*ss)[i]) {
                        rc = lsm_log_error_basic(c, LSM_ERR_NO_MEMORY,
                                                 "ENOMEM");
                        lsm_fs_ss_record_array_free(*ss, i);
                        *ss = NULL;
                        *count = 0;
                        break;
                    }
                    ++i;
                }

            } else {
                rc = lsm_log_error_basic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
                *count = 0;
            }
        }
    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    }
    return rc;
}

static int ss_create(lsm_plugin_ptr c, lsm_fs * fs,
                     const char *name,
                     lsm_fs_ss ** snapshot, char **job, lsm_flag flags)
{
    int rc = LSM_ERR_NO_MEMORY;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    struct allocated_fs *find =
        (struct allocated_fs *) g_hash_table_lookup(pd->fs,
                                                    lsm_fs_id_get(fs));

    if (find) {
        if (!g_hash_table_lookup(find->ss, md5(name))) {
            char *id = strdup(md5(name));
            if (id) {
                lsm_fs_ss *ss =
                    lsm_fs_ss_record_alloc(id, name, time(NULL), NULL);
                lsm_fs_ss *new_shot = lsm_fs_ss_record_copy(ss);
                if (ss && new_shot) {
                    g_hash_table_insert(find->ss, (gpointer) id,
                                        (gpointer) ss);
                    rc = create_job(pd, job, LSM_DATA_TYPE_SS, new_shot,
                                    (void **) snapshot);
                } else {
                    lsm_fs_ss_record_free(ss);
                    ss = NULL;
                    lsm_fs_ss_record_free(new_shot);
                    *snapshot = NULL;
                    free(id);
                    id = NULL;
                }
            }
        } else {
            rc = lsm_log_error_basic(c, LSM_ERR_NAME_CONFLICT,
                                     "snapshot name exists");
        }
    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    }
    return rc;
}

static int ss_delete(lsm_plugin_ptr c, lsm_fs * fs, lsm_fs_ss * ss,
                     char **job, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    struct allocated_fs *find =
        (struct allocated_fs *) g_hash_table_lookup(pd->fs,
                                                    lsm_fs_id_get(fs));

    if (find) {
        if (!g_hash_table_remove(find->ss, lsm_fs_ss_id_get(ss))) {
            rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_FS_SS,
                                     "snapshot not found");
        } else {
            rc = create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);
        }
    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    }
    return rc;
}

static int ss_restore(lsm_plugin_ptr c, lsm_fs * fs, lsm_fs_ss * ss,
                      lsm_string_list * files,
                      lsm_string_list * restore_files,
                      int all_files, char **job, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    struct allocated_fs *find =
        (struct allocated_fs *) g_hash_table_lookup(pd->fs,
                                                    lsm_fs_id_get(fs));

    if (find) {
        if (!g_hash_table_lookup(find->ss, lsm_fs_ss_id_get(ss))) {
            rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_FS_SS,
                                     "snapshot not found");
        } else {
            rc = create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);
        }
    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    }
    return rc;
}

static struct lsm_fs_ops_v1 fs_ops = {
    fs_list,
    fs_create,
    fs_delete,
    fs_resize,
    fs_clone,
    fs_file_clone,
    fs_child_dependency,
    fs_child_dependency_rm,
    ss_list,
    ss_create,
    ss_delete,
    ss_restore
};

static int nfs_auth_types(lsm_plugin_ptr c, lsm_string_list ** types,
                          lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    *types = lsm_string_list_alloc(1);
    if (*types) {
        rc = lsm_string_list_elem_set(*types, 0, "standard");
    } else {
        rc = LSM_ERR_NO_MEMORY;
    }
    return rc;
}

static int nfs_export_list(lsm_plugin_ptr c, const char *search_key,
                           const char *search_value,
                           lsm_nfs_export ** exports[], uint32_t * count,
                           lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    GHashTableIter fs_iter;
    GHashTableIter exports_iter;
    char *k = NULL;
    struct allocated_fs *v = NULL;
    GSList *result = NULL;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    g_hash_table_iter_init(&fs_iter, pd->fs);

    *count = 0;

    /* Walk through each of the file systems and their associated exports */
    while (g_hash_table_iter_next(&fs_iter, (gpointer) & k, (gpointer) & v)) {
        char *exp_key = NULL;
        lsm_nfs_export **exp_val = NULL;

        g_hash_table_iter_init(&exports_iter, v->exports);
        while (g_hash_table_iter_next(&exports_iter, (gpointer) & exp_key,
                                      (gpointer) & exp_val)) {
            result = g_slist_prepend(result, exp_val);
            *count += 1;
        }
    }

    if (*count) {
        int i = 0;
        GSList *s_iter = NULL;
        *exports = lsm_nfs_export_record_array_alloc(*count);
        if (*exports) {
            for (s_iter = result; s_iter;
                 s_iter = g_slist_next(s_iter), i++) {
                (*exports)[i] =
                    lsm_nfs_export_record_copy((lsm_nfs_export *)
                                               s_iter->data);

                if (!(*exports)[i]) {
                    rc = LSM_ERR_NO_MEMORY;
                    lsm_nfs_export_record_array_free(*exports, i);
                    *exports = NULL;
                    *count = 0;
                    break;
                }
            }
        } else {
            rc = LSM_ERR_NO_MEMORY;
        }
    }

    if (result) {
        g_slist_free(result);
        result = NULL;
    }

    if (LSM_ERR_OK == rc) {
        lsm_plug_nfs_export_search_filter(search_key, search_value,
                                          *exports, count);
    }

    return rc;
}

static int nfs_export_create(lsm_plugin_ptr c,
                             const char *fs_id,
                             const char *export_path,
                             lsm_string_list *root_list,
                             lsm_string_list *rw_list,
                             lsm_string_list *ro_list,
                             uint64_t anon_uid,
                             uint64_t anon_gid,
                             const char *auth_type,
                             const char *options,
                             lsm_nfs_export **exported,
                             lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    char auto_export[2048];
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    struct allocated_fs *fs = g_hash_table_lookup(pd->fs, fs_id);
    if (fs) {
        if (!export_path) {
            snprintf(auto_export, sizeof(auto_export), "/mnt/lsm/nfs/%s",
                     lsm_fs_name_get(fs->fs));
            export_path = auto_export;
        }

        char *key = strdup(md5(export_path));
        *exported = lsm_nfs_export_record_alloc(md5(export_path),
                                                fs_id,
                                                export_path,
                                                auth_type,
                                                root_list,
                                                rw_list,
                                                ro_list,
                                                anon_uid,
                                                anon_gid, options, NULL);

        lsm_nfs_export *value = lsm_nfs_export_record_copy(*exported);

        if (key && *exported && value) {
            g_hash_table_insert(fs->exports, key, value);
        } else {
            rc = LSM_ERR_NO_MEMORY;
            free(key);
            lsm_nfs_export_record_free(*exported);
            lsm_nfs_export_record_free(value);
        }

    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    }
    return rc;
}

static int nfs_export_remove(lsm_plugin_ptr c, lsm_nfs_export * e,
                             lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);

    struct allocated_fs *fs = g_hash_table_lookup(pd->fs,
                                                  lsm_nfs_export_fs_id_get
                                                  (e));
    if (fs) {
        if (!g_hash_table_remove(fs->exports, lsm_nfs_export_id_get(e))) {
            rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_NFS_EXPORT,
                                     "export not found");
        }
    } else {
        rc = lsm_log_error_basic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    }
    return rc;
}

static struct lsm_nas_ops_v1 nfs_ops = {
    nfs_auth_types,
    nfs_export_list,
    nfs_export_create,
    nfs_export_remove
};


void free_group_grant_hash(void *v)
{
    g_hash_table_destroy((GHashTable *) v);
}

void free_allocated_fs(void *v)
{
    free_fs_record((struct allocated_fs *) v);
}

void free_disk(void *d)
{
    lsm_disk_record_free((lsm_disk *) d);
}

void free_allocated_volume(void *v)
{
    if (v) {
        struct allocated_volume *av = (struct allocated_volume *) v;
        lsm_volume_record_free(av->v);
        av->v = NULL;
        av->p = NULL;       /* Pool takes care of itself */
        free(av);
    }
}

static void _unload(struct plugin_data *pd)
{
    int i;

    if (pd) {

        if (pd->disks) {
            g_hash_table_destroy(pd->disks);
            pd->disks = NULL;
        }

        if (pd->jobs) {
            g_hash_table_destroy(pd->jobs);
            pd->jobs = NULL;
        }

        if (pd->fs) {
            g_hash_table_destroy(pd->fs);
            pd->fs = NULL;
        }

        if (pd->group_grant) {
            g_hash_table_destroy(pd->group_grant);
            pd->group_grant = NULL;
        }

        if (pd->access_groups) {
            g_hash_table_destroy(pd->access_groups);
            pd->access_groups = NULL;
        }

        if (pd->volumes) {
            g_hash_table_destroy(pd->volumes);
            pd->volumes = NULL;
        }

        if (pd->pools) {
            g_hash_table_destroy(pd->pools);
            pd->pools = NULL;
        }

        for (i = 0; i < pd->num_systems; ++i) {
            lsm_system_record_free(pd->system[i]);
            pd->system[i] = NULL;
        }
        pd->num_systems = 0;

        free(pd);
        pd = NULL;
    }
}

int load(lsm_plugin_ptr c, const char *uri, const char *password,
         uint32_t timeout, lsm_flag flags)
{
    struct plugin_data *pd = (struct plugin_data *)
        calloc(1, sizeof(struct plugin_data));
    int rc = LSM_ERR_NO_MEMORY;
    int i;
    lsm_pool *p = NULL;
    if (pd) {
        pd->num_systems = 1;
        pd->system[0] = lsm_system_record_alloc(sys_id,
                                                "LSM simulated storage plug-in",
                                                LSM_SYSTEM_STATUS_OK, "",
                                                NULL);

        p = lsm_pool_record_alloc("POOL_3", "lsm_test_aggr",
                                  LSM_POOL_ELEMENT_TYPE_FS |
                                  LSM_POOL_ELEMENT_TYPE_VOLUME, 0,
                                  UINT64_MAX, UINT64_MAX,
                                  LSM_POOL_STATUS_OK, "", sys_id, 0);
        if (p) {
            pd->pools = g_hash_table_new_full(g_str_hash, g_str_equal, free,
                                              free_pool_record);

            g_hash_table_insert(pd->pools, strdup(lsm_pool_id_get(p)), p);

            for (i = 0; i < 3; ++i) {
                char name[32];
                snprintf(name, sizeof(name), "POOL_%d", i);

                p = lsm_pool_record_alloc(name, name,
                                          LSM_POOL_ELEMENT_TYPE_FS |
                                          LSM_POOL_ELEMENT_TYPE_VOLUME, 0,
                                          UINT64_MAX, UINT64_MAX,
                                          LSM_POOL_STATUS_OK, "", sys_id,
                                          NULL);

                if (p) {
                    g_hash_table_insert(pd->pools,
                                        strdup(lsm_pool_id_get(p)), p);
                } else {
                    g_hash_table_destroy(pd->pools);
                    pd->pools = NULL;
                    break;
                }
            }
        }

        pd->volumes = g_hash_table_new_full(g_str_hash, g_str_equal, free,
                                            free_allocated_volume);

        pd->access_groups = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                  free, free_allocated_ag);

        /*  We will delete the key, but the value will get cleaned up in its
           own container */
        pd->group_grant = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                free,
                                                free_group_grant_hash);

        pd->fs = g_hash_table_new_full(g_str_hash, g_str_equal, free,
                                       free_allocated_fs);

        pd->jobs = g_hash_table_new_full(g_str_hash, g_str_equal, free,
                                         free_allocated_job);

        pd->disks = g_hash_table_new_full(g_str_hash, g_str_equal, free,
                                          free_disk);


        for (i = 0; i < 10; ++i) {
            lsm_disk *d = NULL;
            char name[17];
            char *key = NULL;
            snprintf(name, sizeof(name), "Sim C disk %d", i);

            d = lsm_disk_record_alloc(md5(name), name, LSM_DISK_TYPE_SOP,
                                      512, 0x8000000000000,
                                      LSM_DISK_STATUS_OK, sys_id);

            key = strdup(lsm_disk_id_get(d));

            if (!key || !d) {
                g_hash_table_destroy(pd->disks);
                pd->disks = NULL;

                lsm_disk_record_free(d);
                d = NULL;

                free(key);
                key = NULL;

                break;
            }

            g_hash_table_insert(pd->disks, key, d);
            d = NULL;
        }

        if (!pd->system[0] || !pd->volumes || !pd->pools
            || !pd->access_groups || !pd->group_grant || !pd->fs
            || !pd->jobs || !pd->disks) {
            rc = LSM_ERR_NO_MEMORY; /* We need to free everything */
            _unload(pd);
            pd = NULL;
        } else {
            rc = lsm_register_plugin_v1_2(c, pd, &mgm_ops, &san_ops,
                                          &fs_ops, &nfs_ops, &ops_v1_2);
        }
    }
    return rc;
}

int unload(lsm_plugin_ptr c, lsm_flag flags)
{
    struct plugin_data *pd = (struct plugin_data *) lsm_private_data_get(c);
    if (pd) {
        _unload(pd);
        return LSM_ERR_OK;
    } else {
        return LSM_ERR_INVALID_ARGUMENT;
    }
}

int main(int argc, char *argv[])
{
    return lsm_plugin_init_v1(argc, argv, load, unload, name, version);
}


#ifdef  __cplusplus
}
#endif
