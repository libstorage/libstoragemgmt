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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Author: tasleson
 *
 */

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include <string.h>
#include <inttypes.h>
#define _XOPEN_SOURCE
#include <unistd.h>
#include <crypt.h>
#include <glib.h>
#include <assert.h>
#include <time.h>

#include "libstoragemgmt/libstoragemgmt_optionaldata.h"
#include "libstoragemgmt/libstoragemgmt_disk.h"

#ifdef  __cplusplus
extern "C" {
#endif

static char name[] = "Compiled plug-in example";
static char version [] = "0.2.0";
static char sys_id[] = "sim-01";

#define BS 512
#define MAX_SYSTEMS 1
#define MAX_FS 32
#define MAX_EXPORT 32

/**
 * Creates a pseudo md5 (DO NOT FREE RETURN VALUE!)
 * @param data      Data to generate md5
 * @return Pointer to character array.
 */
char* md5(const char *data)
{
    return crypt(data, "$1$LSM$");
}

/**
 * Removes an item from an array, shifting the elements and clearing the space
 * that was occupied at the end, use with caution :-)
 * @param array         Base address for the array
 * @param remove_index  Element index to remove
 * @param num_elems     Number of elements currently in the array
 * @param elem_size     Size of each array element
 */
void remove_item( void *array, int remove_index, int num_elems,
                    size_t elem_size)
{
    if( array && (num_elems > 0) && (remove_index < num_elems) && elem_size ) {
        /*Are we at the end?, clear that which is at the end */
        if( remove_index + 1 == num_elems ) {
            memset(array + (elem_size * (num_elems - 1)), 0, elem_size);
            return;
        }

        /* Calculate the position of the one after that we want to remove */
        void *src_addr = (void*)(array + ((remove_index + 1) * elem_size));

        /* Calculate the destination */
        void *dest_addr = (void*)(array + (remove_index * elem_size));

        /* Shift the memory */
        memmove(dest_addr, src_addr, ((num_elems - 1) - remove_index) *
                elem_size);
        /* Clear that which was at the end */
        memset(array + (elem_size * (num_elems - 1)), 0, elem_size);
    }
}

struct allocated_volume {
    lsmVolume *v;
    lsmPool *p;
};

struct allocated_fs {
    lsmFs *fs;
    lsmPool *p;
    GHashTable *ss;
    GHashTable *exports;
};

struct allocated_ag {
    lsmAccessGroup *ag;
    lsmInitiatorType ag_type;
};

struct plugin_data {
    uint32_t tmo;
    uint32_t num_systems;
    lsmSystem *system[MAX_SYSTEMS];

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
    lsmDataType type;
    void *return_data;
};

struct allocated_job *alloc_allocated_job( lsmDataType type, void *return_data )
{
    struct allocated_job *rc = malloc( sizeof(struct allocated_job) );
    if( rc ) {
        rc->polls = 0;
        rc->type = type;
        rc->return_data = return_data;
    }
    return rc;
}

void free_allocated_job(void *j)
{
    struct allocated_job *job = j;

    if( job &&  job->return_data ) {
        switch( job->type ) {
        case(LSM_DATA_TYPE_ACCESS_GROUP):
            lsmAccessGroupRecordFree((lsmAccessGroup *)job->return_data);
            break;
        case(LSM_DATA_TYPE_BLOCK_RANGE):
            lsmBlockRangeRecordFree((lsmBlockRange *)job->return_data);
            break;
        case(LSM_DATA_TYPE_FS):
            lsmFsRecordFree((lsmFs *)job->return_data);
            break;
        case(LSM_DATA_TYPE_INITIATOR):
            lsmInitiatorRecordFree((lsmInitiator *)job->return_data);
            break;
        case(LSM_DATA_TYPE_NFS_EXPORT):
            lsmNfsExportRecordFree((lsmNfsExport *)job->return_data);
            break;
        case(LSM_DATA_TYPE_POOL):
            lsmPoolRecordFree((lsmPool *)job->return_data);
            break;
        case(LSM_DATA_TYPE_SS):
            lsmSsRecordFree((lsmSs *)job->return_data);
            break;
        case(LSM_DATA_TYPE_STRING_LIST):
            lsmStringListFree((lsmStringList *)job->return_data);
            break;
        case(LSM_DATA_TYPE_SYSTEM):
            lsmSystemRecordFree((lsmSystem *)job->return_data);
            break;
        case(LSM_DATA_TYPE_VOLUME):
            lsmVolumeRecordFree((lsmVolume *)job->return_data);
            break;
        default:
            break;
        }
        job->return_data = NULL;
    }
    free(job);
}

struct allocated_ag *alloc_allocated_ag( lsmAccessGroup *ag,
                                            lsmInitiatorType i)
{
    struct allocated_ag *aag =
                    (struct allocated_ag *)malloc(sizeof(struct allocated_ag));
    if( aag ) {
        aag->ag = ag;
        aag->ag_type = i;
    }
    return aag;
}

void free_allocated_ag(void *v)
{
    if( v ) {
        struct allocated_ag *aag = (struct allocated_ag *)v;
        lsmAccessGroupRecordFree(aag->ag);
        free(aag);
    }
}

void free_pool_record(void *p)
{
    if( p ) {
        lsmPoolRecordFree((lsmPool*)p);
    }
}

void free_fs_record(struct allocated_fs *fs)
{
    if( fs ) {
        g_hash_table_destroy(fs->ss);
        g_hash_table_destroy(fs->exports);
        lsmFsRecordFree(fs->fs);
        fs->p = NULL;
        free(fs);
    }
}

static void free_ss(void *s)
{
    lsmSsRecordFree((lsmSs *)s);
}

static void free_export(void *exp)
{
    lsmNfsExportRecordFree((lsmNfsExport *)exp);
}

static struct allocated_fs *alloc_fs_record() {
    struct allocated_fs *rc = (struct allocated_fs *)
                                malloc(sizeof(struct allocated_fs));
    if( rc ) {
        rc->fs = NULL;
        rc->p = NULL;
        rc->ss = g_hash_table_new_full(g_str_hash, g_str_equal, free, free_ss);
        rc->exports = g_hash_table_new_full(g_str_hash, g_str_equal, free,
                                            free_export);

        if( !rc->ss || !rc->exports ) {
            if( rc->ss ) {
                g_hash_table_destroy(rc->ss);
            }

            if( rc->exports ) {
                g_hash_table_destroy(rc->exports);
            }

            free(rc);
            rc = NULL;
        }
    }
    return rc;
}

static int create_job(struct plugin_data *pd, char **job, lsmDataType t,
                        void *new_value, void **returned_value)
{
    static int job_num = 0;
    int rc = LSM_ERR_JOB_STARTED;
    char job_id[64];
    char *key = NULL;

    /* Make this random */
    if( 0 ) {
        if( returned_value ) {
            *returned_value = new_value;
        }
        *job = NULL;
        rc = LSM_ERR_OK;
    } else {
        snprintf(job_id, sizeof(job_id), "JOB_%d", job_num);
        job_num += 1;

        if( returned_value ) {
            *returned_value = NULL;
        }

        *job = strdup(job_id);
        key = strdup(job_id);

        struct allocated_job *value = alloc_allocated_job(t, new_value);
        if( *job && key && value ) {
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

static int tmo_set(lsmPluginPtr c, uint32_t timeout, lsmFlag_t flags )
{
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    if(pd) {
        pd->tmo = timeout;
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_PLUGIN;
}

static int tmo_get(lsmPluginPtr c, uint32_t *timeout, lsmFlag_t flags)
{
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    if(pd) {
        *timeout = pd->tmo;
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_PLUGIN;
}

static int cap(lsmPluginPtr c, lsmSystem *system,
                lsmStorageCapabilities **cap, lsmFlag_t flags)
{
    int rc = LSM_ERR_NO_MEMORY;
    *cap = lsmCapabilityRecordAlloc(NULL);

    if( *cap ) {
        rc = lsmCapabilitySetN(*cap, LSM_CAPABILITY_SUPPORTED, 48,
            LSM_CAP_BLOCK_SUPPORT,
            LSM_CAP_FS_SUPPORT,
            LSM_CAP_INITIATORS,
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
            LSM_CAP_VOLUME_ONLINE,
            LSM_CAP_VOLUME_OFFLINE,
            LSM_CAP_ACCESS_GROUP_GRANT,
            LSM_CAP_ACCESS_GROUP_REVOKE,
            LSM_CAP_ACCESS_GROUP_LIST,
            LSM_CAP_ACCESS_GROUP_CREATE,
            LSM_CAP_ACCESS_GROUP_DELETE,
            LSM_CAP_ACCESS_GROUP_ADD_INITIATOR,
            LSM_CAP_ACCESS_GROUP_DEL_INITIATOR,
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
            LSM_CAP_FS_SNAPSHOT_CREATE_SPECIFIC_FILES,
            LSM_CAP_FS_SNAPSHOT_DELETE,
            LSM_CAP_FS_SNAPSHOT_REVERT,
            LSM_CAP_FS_SNAPSHOT_REVERT_SPECIFIC_FILES,
            LSM_CAP_FS_CHILD_DEPENDENCY,
            LSM_CAP_FS_CHILD_DEPENDENCY_RM,
            LSM_CAP_FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES,
            LSM_CAP_EXPORT_AUTH,
            LSM_CAP_EXPORTS,
            LSM_CAP_EXPORT_FS,
            LSM_CAP_EXPORT_REMOVE
            );

        if( LSM_ERR_OK != rc ) {
            lsmCapabilityRecordFree(*cap);
            *cap = NULL;
        }
    }
    return rc;
}

static int jobStatus(lsmPluginPtr c, const char *job_id,
                        lsmJobStatus *status, uint8_t *percentComplete,
                        lsmDataType *t,
                        void **value, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    if(pd) {
        struct allocated_job *val = (struct allocated_job*)
                                        g_hash_table_lookup(pd->jobs,job_id);
        if( val ) {
            *status = LSM_JOB_INPROGRESS;

            val->polls += 34;

            if( (val->polls) >= 100 ) {
                *t = val->type;
                *value = lsmDataTypeCopy(val->type, val->return_data);
                *status = LSM_JOB_COMPLETE;
                *percentComplete = 100;
            } else {
                *percentComplete = val->polls;
            }

        } else {
            rc = LSM_ERR_NOT_FOUND_JOB;
        }
    } else {
        rc = LSM_ERR_INVALID_PLUGIN;
    }

    return rc;
}

static int list_pools(lsmPluginPtr c, lsmPool **poolArray[],
                                        uint32_t *count, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    *count = g_hash_table_size(pd->pools);

    if( *count ) {
        *poolArray = lsmPoolRecordArrayAlloc( *count );
        if( *poolArray ) {
            uint32_t i = 0;
            char *k = NULL;
            lsmPool *p = NULL;
            GHashTableIter iter;
            g_hash_table_iter_init(&iter, pd->pools);
            while(g_hash_table_iter_next(&iter,(gpointer) &k,(gpointer)&p)) {
                (*poolArray)[i] = lsmPoolRecordCopy(p);
                if( !(*poolArray)[i] ) {
                    rc = LSM_ERR_NO_MEMORY;
                    lsmPoolRecordArrayFree(*poolArray, i);
                    *count = 0;
                    *poolArray = NULL;
                    break;
                }
                ++i;
            }
        } else {
            rc = lsmLogErrorBasic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
        }
    }
    return rc;
}

static int list_systems(lsmPluginPtr c, lsmSystem **systems[],
                                        uint32_t *systemCount, lsmFlag_t flags)
{
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    if(pd) {
        *systemCount = pd->num_systems;
        *systems = lsmSystemRecordArrayAlloc(MAX_SYSTEMS);

        if( *systems ) {
            (*systems)[0] = lsmSystemRecordCopy(pd->system[0]);

            if( (*systems)[0] ) {
                return LSM_ERR_OK;
            } else {
                lsmSystemRecordArrayFree(*systems, pd->num_systems);
            }
        }
        return LSM_ERR_NO_MEMORY;
    } else {
        return LSM_ERR_INVALID_PLUGIN;
    }
}

static int jobFree(lsmPluginPtr c, char *job_id, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    if(pd) {
        if( !g_hash_table_remove(pd->jobs, job_id) ) {
            rc = LSM_ERR_NOT_FOUND_JOB;
        }
    } else {
        rc = LSM_ERR_INVALID_PLUGIN;
    }

    return rc;
}

static struct lsmMgmtOpsV1 mgmOps = {
    tmo_set,
    tmo_get,
    cap,
    jobStatus,
    jobFree,
    list_pools,
    list_systems,
};

void freeInitiator(void *i) {
    if( i ) {
        lsmInitiatorRecordFree((lsmInitiator *)i);
    }
}

static int _volume_accessible(struct plugin_data *pd, lsmAccessGroup *ag,
                                lsmVolume *vol)
{
    GHashTable *v = NULL;

    const char *ag_id = lsmAccessGroupIdGet(ag);
    const char *vol_id = lsmVolumeIdGet(vol);

    v = (GHashTable *)g_hash_table_lookup( pd->group_grant, ag_id );

    if( v ) {
        if( g_hash_table_lookup(v, vol_id) ) {
            return 1;
        }
    }
    return 0;
}

static int _list_initiators(lsmPluginPtr c, lsmInitiator **initArray[],
                                        uint32_t *count, lsmFlag_t flags,
                                        lsmVolume *filter)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    if(!pd) {
        return LSM_ERR_INVALID_PLUGIN;
    }

    GHashTable *tmp_inits = g_hash_table_new_full(g_str_hash, g_str_equal, free,
                                                    freeInitiator);

    *count = 0;
    *initArray = NULL;

    if( tmp_inits ) {
        char *k = NULL;
        struct allocated_ag *v = NULL;
        GHashTableIter iter;

        g_hash_table_iter_init (&iter, pd->access_groups);
        while (g_hash_table_iter_next (&iter, (gpointer) &k, (gpointer)&v) ) {
            int include = 1;

            uint32_t i = 0;

            if( filter ) {
                if( !_volume_accessible(pd, v->ag, filter )) {
                    include = 0;
                }
            }

            if( include ) {
                lsmStringList *inits = lsmAccessGroupInitiatorIdGet(v->ag);

                for(i = 0; i < lsmStringListSize(inits); ++i ) {
                    char *init_key = strdup(lsmStringListElemGet(inits,i));
                    lsmInitiator *init_val = lsmInitiatorRecordAlloc(v->ag_type,
                                                                    init_key, "");

                    if( init_key && init_val ) {
                        g_hash_table_insert(tmp_inits, init_key, init_val);
                    } else {
                        free(init_key);
                        lsmInitiatorRecordFree(init_val);
                        rc = LSM_ERR_NO_MEMORY;
                    }
                }
            }
        }

        if( LSM_ERR_OK == rc ) {
            *count = g_hash_table_size(tmp_inits);

            if( *count ) {
                *initArray = lsmInitiatorRecordArrayAlloc(*count);
                if( *initArray ) {
                    int i = 0;
                    char *ikey = NULL;
                    lsmInitiator *ival = NULL;

                    g_hash_table_iter_init (&iter, tmp_inits);
                    while (g_hash_table_iter_next (&iter, (gpointer) &ikey,
                                                            (gpointer)&ival) ) {
                        (*initArray)[i] = lsmInitiatorRecordCopy(ival);
                        if( !(*initArray)[i] ) {
                            rc = LSM_ERR_NO_MEMORY;
                            lsmInitiatorRecordArrayFree(*initArray, i);
                            break;
                        }
                        i += 1;
                    }
                } else {
                    rc = LSM_ERR_NO_MEMORY;
                }
            }
        }

        g_hash_table_destroy(tmp_inits);
    }
    return rc;
}


static int list_initiators(lsmPluginPtr c, lsmInitiator **initArray[],
                                        uint32_t *count, lsmFlag_t flags)
{
    return _list_initiators(c, initArray, count, flags, NULL);
}

static int list_volumes(lsmPluginPtr c, lsmVolume **vols[],
                                        uint32_t *count, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    *count = g_hash_table_size(pd->volumes);


    if( *count ) {
        *vols = lsmVolumeRecordArrayAlloc( *count );
        if( *vols ) {
            uint32_t i = 0;
            char *k = NULL;
            struct allocated_volume *vol;
            GHashTableIter iter;
            g_hash_table_iter_init(&iter, pd->volumes);
            while(g_hash_table_iter_next(&iter,(gpointer) &k,(gpointer)&vol)) {
                (*vols)[i] = lsmVolumeRecordCopy(vol->v);
                if( !(*vols)[i] ) {
                    rc = LSM_ERR_NO_MEMORY;
                    lsmVolumeRecordArrayFree(*vols, i);
                    *count = 0;
                    *vols = NULL;
                    break;
                }
                ++i;
            }
        } else {
            rc = lsmLogErrorBasic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
        }
    }
    return rc;
}

static int list_disks(lsmPluginPtr c, lsmDisk **disks[], uint32_t *count,
                        lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    *count = g_hash_table_size(pd->disks);


    if( *count ) {
        *disks = lsmDiskRecordArrayAlloc( *count );
        if( *disks ) {
            uint32_t i = 0;
            char *k = NULL;
            lsmDisk *disk;
            GHashTableIter iter;
            g_hash_table_iter_init(&iter, pd->disks);
            while(g_hash_table_iter_next(&iter,(gpointer) &k,(gpointer)&disk)) {
                (*disks)[i] = lsmDiskRecordCopy(disk);
                if( !(*disks)[i] ) {
                    rc = LSM_ERR_NO_MEMORY;
                    lsmDiskRecordArrayFree(*disks, i);
                    *count = 0;
                    *disks = NULL;
                    break;
                }
                ++i;
            }
        } else {
            rc = lsmLogErrorBasic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
        }
    }
    return rc;
}

static uint64_t pool_allocate(lsmPool *p, uint64_t size)
{
    uint64_t rounded_size = 0;
    uint64_t free_space = lsmPoolFreeSpaceGet(p);

    rounded_size = (size/BS) * BS;

    if( free_space >= rounded_size ) {
        free_space -= rounded_size;
        lsmPoolFreeSpaceSet(p, free_space);
    } else {
        rounded_size = 0;
    }
    return rounded_size;
}

void pool_deallocate(lsmPool *p, uint64_t size)
{
    uint64_t free_space = lsmPoolFreeSpaceGet(p);

    free_space += size;
    lsmPoolFreeSpaceSet(p, free_space);
}

static lsmPool *find_pool(struct plugin_data *pd, const char* pool_id)
{
    return (lsmPool*) g_hash_table_lookup(pd->pools, pool_id);
}

static lsmPool * find_pool_name(struct plugin_data *pd, const char *name)
{
    char *k = NULL;
    GHashTableIter iter;
    lsmPool *pool = NULL;
    g_hash_table_iter_init(&iter, pd->pools);
    while(g_hash_table_iter_next(&iter,(gpointer) &k,(gpointer)&pool)) {
        if( strcmp(lsmPoolNameGet(pool), name) == 0 ) {
            return pool;
        }
    }
    return NULL;
}

static lsmDisk * find_disk(struct plugin_data *pd, const char* disk_id)
{
    return (lsmDisk*) g_hash_table_lookup(pd->disks, disk_id);
}

static struct allocated_volume *find_volume(struct plugin_data *pd,
        const char* vol_id)
{
    struct allocated_volume *rc = g_hash_table_lookup(pd->volumes, vol_id);
    return rc;
}

static struct allocated_volume * find_volume_name(struct plugin_data *pd,
        const char *name)
{
    struct allocated_volume *found = NULL;
    char *k = NULL;
    struct allocated_volume *vol;
    GHashTableIter iter;
    g_hash_table_iter_init(&iter, pd->volumes);
    while(g_hash_table_iter_next(&iter,(gpointer) &k,(gpointer)&vol)) {
        if( strcmp(lsmVolumeNameGet(vol->v), name) == 0 ) {
            found = vol;
            break;
        }
    }
    return found;
}

static int volume_create(lsmPluginPtr c, lsmPool *pool,
                        const char *volumeName, uint64_t size,
                        lsmProvisionType provisioning, lsmVolume **newVolume,
                        char **job, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;

    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    lsmPool *p = find_pool(pd, lsmPoolIdGet(pool));

    if( p ) {
        if( !find_volume_name(pd, volumeName) ) {
            uint64_t allocated_size = pool_allocate(p, size);
            if( allocated_size ) {
                char *id = md5(volumeName);

                /* We create one to return and a copy to store in memory */

                lsmVolume *v = lsmVolumeRecordAlloc(id, volumeName,
                                   "VPD", BS, allocated_size/BS, 0, sys_id,
                                    lsmPoolIdGet(pool));

                lsmVolume *to_store = lsmVolumeRecordCopy(v);
                struct allocated_volume *av = malloc(sizeof(struct allocated_volume));

                if( v && av && to_store) {
                    av->v = to_store;
                    av->p = p;

                    /*
                     * Make a copy of the key, as we may replace the volume,
                     * but leave the key.
                     */
                    g_hash_table_insert(pd->volumes,
                                        (gpointer)strdup(lsmVolumeIdGet(to_store)),
                                        (gpointer)av);

                    rc = create_job(pd, job, LSM_DATA_TYPE_VOLUME, v,
                                        (void**)newVolume);

                } else {
                    free(av);
                    lsmVolumeRecordFree(v);
                    lsmVolumeRecordFree(to_store);
                    rc = lsmLogErrorBasic(c, LSM_ERR_NO_MEMORY,
                                            "Check for leaks");
                }

            } else {
                rc = lsmLogErrorBasic(c, LSM_ERR_SIZE_INSUFFICIENT_SPACE,
                                            "Insufficient space in pool");
            }

        } else {
            rc = lsmLogErrorBasic(c, LSM_ERR_EXISTS_NAME, "Existing volume "
                                                            "with name");
        }
    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_POOL, "Pool not found!");
    }
    return rc;
}

static int volume_replicate(lsmPluginPtr c, lsmPool *pool,
                        lsmReplicationType repType, lsmVolume *volumeSrc,
                        const char *name, lsmVolume **newReplicant,
                        char **job, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    lsmPool *pool_to_use = NULL;

    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    if( pool ) {
        pool_to_use = find_pool(pd, lsmPoolIdGet(pool));
    } else {
        pool_to_use = find_pool(pd, lsmVolumePoolIdGet(volumeSrc));
    }

    if( !pool_to_use ) {
         rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_POOL,
                                    "Pool not found!");
    } else {
        if( find_volume(pd, lsmVolumeIdGet(volumeSrc) )) {
            rc = volume_create(c, pool_to_use, name,
                                lsmVolumeNumberOfBlocksGet(volumeSrc)*BS,
                                LSM_PROVISION_DEFAULT, newReplicant, job, flags);
        } else {
            rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                    "Volume not found!");
        }
    }
    return rc;
}

static int volume_replicate_range_bs(lsmPluginPtr c, lsmSystem *system,
                                    uint32_t *bs,
                                    lsmFlag_t flags)
{
    *bs = BS;
    return LSM_ERR_OK;
}

static int volume_replicate_range(lsmPluginPtr c,
                                    lsmReplicationType repType,
                                    lsmVolume *source,
                                    lsmVolume *dest,
                                    lsmBlockRange **ranges,
                                    uint32_t num_ranges, char **job,
                                    lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    struct allocated_volume *src_v = find_volume(pd, lsmVolumeIdGet(source));
    struct allocated_volume *dest_v = find_volume(pd, lsmVolumeIdGet(dest));

    if( !src_v || !dest_v ) {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                    "Src or dest volumes not found!");
    } else {
        rc = create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);
    }

    return rc;
}

static int volume_resize(lsmPluginPtr c, lsmVolume *volume,
                                uint64_t newSize, lsmVolume **resizedVolume,
                                char **job, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    struct allocated_volume *av = find_volume(pd, lsmVolumeIdGet(volume));

    if( av ) {
        lsmVolume *v = av->v;
        lsmPool *p = av->p;
        uint64_t curr_size = lsmVolumeNumberOfBlocksGet(v) * BS;

        pool_deallocate(p, curr_size);
        uint64_t resized_size = pool_allocate(p, newSize);
        if( resized_size ) {
            lsmVolume *vp = lsmVolumeRecordAlloc(lsmVolumeIdGet(v),
                                                    lsmVolumeNameGet(v),
                                                    lsmVolumeVpd83Get(v),
                                                    lsmVolumeBlockSizeGet(v),
                                                    resized_size/BS, 0, sys_id,
                                                    lsmVolumePoolIdGet(volume));

            if( vp ) {
                av->v = vp;
                lsmVolumeRecordFree(v);
                rc = create_job(pd, job, LSM_DATA_TYPE_VOLUME,
                                    lsmVolumeRecordCopy(vp),
                                    (void**)resizedVolume);
            } else {
                pool_deallocate(p, resized_size);
                pool_allocate(p, curr_size);
                rc = lsmLogErrorBasic(c, LSM_ERR_NO_MAPPING, "ENOMEM");
            }

        } else {
            /*Could not accommodate re-sized, go back */
            pool_allocate(p, curr_size);
            rc = lsmLogErrorBasic(c, LSM_ERR_SIZE_INSUFFICIENT_SPACE,
                                                "Insufficient space in pool");
        }

    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                    "volume not found!");
    }
    return rc;
}

static int _volume_delete(lsmPluginPtr c, const char *volume_id)
{
    int rc = LSM_ERR_OK;
    GHashTableIter iter;
    char *k = NULL;
    GHashTable *v = NULL;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    struct allocated_volume *av = find_volume(pd, volume_id);

    if( av ) {
        lsmVolume *vp = av->v;
        pool_deallocate(av->p, lsmVolumeNumberOfBlocksGet(vp)*BS);

        g_hash_table_remove(pd->volumes, volume_id);

        g_hash_table_iter_init (&iter, pd->group_grant);
        while( g_hash_table_iter_next( &iter, (gpointer)&k, (gpointer)&v) ) {
            if( g_hash_table_lookup(v, volume_id) ) {
                g_hash_table_remove(v, volume_id );
            }
        }

    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                    "volume not found!");
    }
    return rc;
}

static int _pool_create(lsmPluginPtr c, const char *system_id,
                            const char *pool_name, uint64_t size_bytes,
                            lsmPool **pool, char **job)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    lsmPool *new_pool = NULL;
    lsmPool *pool_to_store = NULL;
    char *key = NULL;

    /* Verify system id */
    if( strcmp(system_id, lsmSystemIdGet(pd->system[0])) == 0 ) {
        /* Verify that we don't already have a pool by that name */
        new_pool = find_pool_name(pd, pool_name);
        if( !new_pool ) {
            /* Create the pool */
            new_pool = lsmPoolRecordAlloc(md5(pool_name), pool_name, size_bytes,
                                        size_bytes, LSM_POOL_STATUS_OK,
                                        system_id);

            pool_to_store = lsmPoolRecordCopy(new_pool);
            key = strdup(lsmPoolIdGet(pool_to_store));
            if( new_pool && pool_to_store && key ) {
                g_hash_table_insert(pd->pools, key, pool_to_store);

                /* Create a job */
                rc = create_job(pd, job, LSM_DATA_TYPE_POOL, new_pool,
                                        (void**)pool);
            } else {
                free(key);
                lsmPoolRecordFree(new_pool);
                lsmPoolRecordFree(pool_to_store);
                rc = lsmLogErrorBasic(c, LSM_ERR_NO_MEMORY, "No memory");
            }
        } else {
            rc = lsmLogErrorBasic(c, LSM_ERR_EXISTS_POOL,
                                    "Pool with name exists!");
        }
    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_SYSTEM,
                                    "system not found!");
    }
    return rc;
}


static int pool_create(lsmPluginPtr c, const char *system_id,
                            const char *pool_name, uint64_t size_bytes,
                            lsmPoolRaidType raid_type,
                            lsmPoolMemberType member_type, lsmPool** pool,
                            char **job, lsmFlag_t flags)
{
    return _pool_create(c, system_id, pool_name, size_bytes, pool, job);
}

static int pool_create_from_disks( lsmPluginPtr c, const char *system_id,
                const char *pool_name, lsmStringList *member_ids,
                lsmPoolRaidType raid_type, lsmPool **pool, char **job,
                lsmFlag_t flags)
{
    /* Check that the disks are valid, then call common routine */
    uint64_t size = 0;
    int rc = LSM_ERR_OK;
    int i = 0;
    int num_disks = lsmStringListSize(member_ids);
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    if( num_disks ) {
        for( i = 0; i < num_disks; ++i ) {
            lsmDisk *d = find_disk(pd, lsmStringListElemGet(member_ids, i));
            if( d ) {
                size += (lsmDiskNumberOfBlocksGet(d) * lsmDiskBlockSizeGet(d));
            } else {
                rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_DISK,
                                        "Disk not found");
                goto bail;
            }
        }

        rc = _pool_create(c, system_id, pool_name, size, pool, job);
    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_INVALID_ARGUMENT, "No disks provided");
    }
bail:
    return rc;
}

static int pool_create_from_volumes( lsmPluginPtr c, const char *system_id,
                const char *pool_name, lsmStringList *member_ids,
                lsmPoolRaidType raid_type, lsmPool **pool, char **job,
                lsmFlag_t flags)
{
    /* Check that the disks are valid, then call common routine */
    uint64_t size = 0;
    int rc = LSM_ERR_OK;
    int i = 0;
    int num_volumes = lsmStringListSize(member_ids);
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    if( num_volumes ) {
        for( i = 0; i < num_volumes; ++i ) {
            struct allocated_volume *v =
                        find_volume(pd, lsmStringListElemGet(member_ids, i));
            if( v ) {
                size += (lsmVolumeNumberOfBlocksGet(v->v) *
                            lsmVolumeNumberOfBlocksGet(v->v));
            } else {
                rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                        "Volume not found");
                goto bail;
            }
        }

        rc = _pool_create(c, system_id, pool_name, size, pool, job);
    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_INVALID_ARGUMENT, "No disks provided");
    }
bail:
    return rc;
}

static int pool_create_from_pool(lsmPluginPtr c, const char *system_id,
                        const char *pool_name, const char *member_id,
                        uint64_t size_bytes, lsmPool **pool, char **job,
                        lsmFlag_t flags )
{
    /* Check that the disks are valid, then call common routine */
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    lsmPool *p = find_pool(pd, member_id);

    if( p ) {
        rc = _pool_create(c, system_id, pool_name, size_bytes, pool, job);
    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_POOL, "Pool not found");
    }
    return rc;
}

static int pool_delete(lsmPluginPtr c, lsmPool *pool, char **job,
                lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    lsmPool *pool_to_delete = find_pool(pd, lsmPoolIdGet(pool));

    if( pool_to_delete ) {

        /* Loop through building a list of volumes in this pool */
        char *k = NULL;
        struct allocated_volume *vol;
        GHashTableIter iter;
        g_hash_table_iter_init(&iter, pd->volumes);
        while(g_hash_table_iter_next(&iter,(gpointer) &k,(gpointer)&vol)) {
            if( strcmp(lsmVolumePoolIdGet(vol->v), lsmPoolIdGet(pool)) == 0 ) {
               rc = lsmLogErrorBasic(c, LSM_ERR_EXISTS_VOLUME,
                                    "volumes exist on pool");
               goto bail;
            }
        }

        /* Remove pool from hash and create job */
        g_hash_table_remove(pd->pools, lsmPoolIdGet(pool));
        rc = create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);

    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_POOL,
                                    "pool not found!");
    }
bail:
    return rc;
}

static int volume_delete(lsmPluginPtr c, lsmVolume *volume,
                                    char **job, lsmFlag_t flags)
{
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    int rc = _volume_delete(c, lsmVolumeIdGet(volume));

    if( LSM_ERR_OK == rc ) {
        rc = create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);
    }
    return rc;
}

static int volume_online_offline(lsmPluginPtr c, lsmVolume *v,
                                    lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    struct allocated_volume *av = find_volume(pd, lsmVolumeIdGet(v));

    if( !av) {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                    "volume not found!");
    }
    return rc;
}

static int access_group_list(lsmPluginPtr c,
                                lsmAccessGroup **groups[],
                                uint32_t *groupCount, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    *groupCount = g_hash_table_size(pd->access_groups);

    if( *groupCount ) {
        *groups = lsmAccessGroupRecordArrayAlloc(*groupCount);
        if( *groups ) {
            int i = 0;
            char *key = NULL;
            struct allocated_ag *val = NULL;
            GHashTableIter iter;

            g_hash_table_iter_init (&iter, pd->access_groups);

            while (g_hash_table_iter_next (&iter, (gpointer) &key,
                                                    (gpointer)&val) ) {
                (*groups)[i] = lsmAccessGroupRecordCopy(val->ag);
                if( !(*groups)[i] ) {
                    rc = LSM_ERR_NO_MEMORY;
                    lsmAccessGroupRecordArrayFree(*groups, i);
                    *groupCount = 0;
                    groups = NULL;
                    break;
                }
                ++i;
            }
        } else {
            rc = LSM_ERR_NO_MEMORY;
        }
    }
    return rc;
}

static int access_group_create(lsmPluginPtr c,
                                const char *name,
                                const char *initiator_id,
                                lsmInitiatorType id_type,
                                const char *system_id,
                                lsmAccessGroup **access_group,
                                lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    lsmAccessGroup *ag = NULL;
    struct allocated_ag *aag = NULL;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    char *id = strdup(md5(name));

    struct allocated_ag *find = (struct allocated_ag *)
                        g_hash_table_lookup(pd->access_groups, id);

    if( !find ) {
        lsmStringList *initiators = lsmStringListAlloc(1);
        if( initiators && id &&
            (LSM_ERR_OK == lsmStringListElemSet(initiators, 0, initiator_id))) {
            ag = lsmAccessGroupRecordAlloc(id, name, initiators,
                                                        system_id);
            aag = alloc_allocated_ag(ag, id_type);
            if( ag && aag ) {
                g_hash_table_insert(pd->access_groups, (gpointer)id,
                                        (gpointer)aag);
                *access_group = lsmAccessGroupRecordCopy(ag);
            } else {
                free_allocated_ag(aag);
                lsmAccessGroupRecordFree(ag);
                rc = lsmLogErrorBasic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
            }
        } else {
            rc = lsmLogErrorBasic(c, LSM_ERR_NO_MEMORY,
                                    "ENOMEM");
        }

        /* Initiators is copied when allocating a group record */
        lsmStringListFree(initiators);

    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_EXISTS_ACCESS_GROUP,
                                    "access group with same id found");
    }

    /*
     *  If we were not successful free memory for id string, id is on the heap
     * because it is passed to the hash table.
     */
    if( LSM_ERR_OK != rc ) {
        free(id);
    }

    return rc;
}

static int access_group_delete( lsmPluginPtr c,
                                lsmAccessGroup *group,
                                lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;

    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    const char *id = lsmAccessGroupIdGet(group);

    gboolean r = g_hash_table_remove(pd->access_groups, (gpointer)id);

    if( !r ) {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_ACCESS_GROUP,
                                    "access group not found");
    } else {
        g_hash_table_remove(pd->group_grant, id);
    }

    if( !g_hash_table_size(pd->access_groups) ) {
        assert( g_hash_table_size(pd->group_grant ) == 0);
    }

    return rc;
}

static int access_group_add_initiator(  lsmPluginPtr c,
                                        lsmAccessGroup *group,
                                        const char *initiator_id,
                                        lsmInitiatorType id_type,
                                        lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    struct allocated_ag *find = (struct allocated_ag *)
                                g_hash_table_lookup(pd->access_groups,
                                lsmAccessGroupIdGet(group));

    if( find ) {
        lsmStringList *inits = lsmAccessGroupInitiatorIdGet(find->ag);
        rc = lsmStringListAppend(inits, initiator_id);
    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_ACCESS_GROUP,
                                    "access group not found");
    }
    return rc;
}

static int access_group_del_initiator(  lsmPluginPtr c,
                                        lsmAccessGroup *group,
                                        const char *init,
                                        lsmFlag_t flags)
{
    int rc = LSM_ERR_INITIATOR_NOT_IN_ACCESS_GROUP;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    struct allocated_ag *find = (struct allocated_ag *)
                                g_hash_table_lookup(pd->access_groups,
                                lsmAccessGroupIdGet(group));

    if( find ) {
        uint32_t i;
        lsmStringList *inits = lsmAccessGroupInitiatorIdGet(find->ag);

        for(i = 0; i < lsmStringListSize(inits); ++i) {
            if( strcmp(init, lsmStringListElemGet(inits, i)) == 0 ) {
                lsmStringListDelete(inits, i);
                rc = LSM_ERR_OK;
                break;
            }
        }
    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_ACCESS_GROUP,
                                    "access group not found");
    }
    return rc;
}

static int access_group_grant(lsmPluginPtr c,
                                lsmAccessGroup *group,
                                lsmVolume *volume,
                                lsmAccessType access,
                                lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    struct allocated_ag *find = (struct allocated_ag *)
                                g_hash_table_lookup(pd->access_groups,
                                lsmAccessGroupIdGet(group));

    struct allocated_volume *av = find_volume(pd, lsmVolumeIdGet(volume));

    if( find && av ) {
        GHashTable *grants = g_hash_table_lookup(pd->group_grant,
                                    lsmAccessGroupIdGet(find->ag));
        if( !grants ) {
            /* We don't have any mappings for this access group*/
            GHashTable *grant = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                        free, free);
            char *key = strdup(lsmAccessGroupIdGet(find->ag));
            char *vol_id = strdup(lsmVolumeIdGet(volume));
            lsmAccessType *val = (lsmAccessType*) malloc(sizeof(lsmAccessType));

            if( grant && key && val && vol_id ) {
                *val = access;

                /* Create the association for volume id and access value */
                g_hash_table_insert(grant, vol_id, val);

                /* Create the association for access groups */
                g_hash_table_insert(pd->group_grant, key, grant);

            } else {
                rc = LSM_ERR_NO_MEMORY;
                free(key);
                free(val);
                free(vol_id);
                if( grant ) {
                    g_hash_table_destroy(grant);
                    grant = NULL;
                }
            }

        } else {
            /* See if we have this volume in the access grants */
            char *vol_id = g_hash_table_lookup(grants, lsmVolumeIdGet(volume));
            if( !vol_id ) {
                vol_id = strdup(lsmVolumeIdGet(volume));
                lsmAccessType *val =
                                (lsmAccessType*) malloc(sizeof(lsmAccessType));
                if( vol_id && val ) {
                    g_hash_table_insert(grants, vol_id, val);
                } else {
                    rc = LSM_ERR_NO_MEMORY;
                    free(vol_id);
                    free(val);
                }

            } else {
                rc = LSM_ERR_IS_MAPPED;
            }
        }
    } else {
        if( !av ) {
            rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                        "volume not found");
        } else {
            rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_ACCESS_GROUP,
                                    "access group not found");
        }
    }
    return rc;
}

static int access_group_revoke(lsmPluginPtr c,
                                lsmAccessGroup *group,
                                lsmVolume *volume,
                                lsmFlag_t flags)
{
    int rc = LSM_ERR_NO_MAPPING;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    struct allocated_ag *find = (struct allocated_ag *)
                                g_hash_table_lookup(pd->access_groups,
                                lsmAccessGroupIdGet(group));

    struct allocated_volume *av = find_volume(pd, lsmVolumeIdGet(volume));

    if( find && av ) {
        GHashTable *grants = g_hash_table_lookup(pd->group_grant,
                                    lsmAccessGroupIdGet(find->ag));

        if( grants ) {
            g_hash_table_remove(grants, lsmVolumeIdGet(volume));
            rc = LSM_ERR_OK;
        }

    } else {
       if( !av ) {
            rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                        "volume not found");
        } else {
            rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_ACCESS_GROUP,
                                    "access group not found");
        }
    }
    return rc;
}

static lsmVolume *get_volume_by_id(struct plugin_data *pd, const char *id)
{
    struct allocated_volume *av = find_volume(pd, id);
    if( av ) {
        return av->v;
    }
    return NULL;
}

static int vol_accessible_by_ag(lsmPluginPtr c,
                                lsmAccessGroup *group,
                                lsmVolume **volumes[],
                                uint32_t *count, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    struct allocated_ag *find = (struct allocated_ag *)
                                g_hash_table_lookup(pd->access_groups,
                                lsmAccessGroupIdGet(group));
    if( find ) {
        GHashTable *grants = g_hash_table_lookup(pd->group_grant,
                                    lsmAccessGroupIdGet(find->ag));
        *count = 0;

        if( grants && g_hash_table_size(grants) ) {
            *count = g_hash_table_size(grants);
            GList *keys = g_hash_table_get_keys(grants);
            *volumes = lsmVolumeRecordArrayAlloc(*count);

            if( keys && *volumes ) {
                GList *curr = NULL;
                int i = 0;

                for(    curr = g_list_first(keys);
                        curr != NULL;
                        curr = g_list_next(curr), ++i ) {

                    (*volumes)[i] = lsmVolumeRecordCopy(get_volume_by_id(pd,
                                                        (char *)curr->data));
                    if( !(*volumes)[i] ) {
                        rc = LSM_ERR_NO_MEMORY;
                        lsmVolumeRecordArrayFree(*volumes, i);
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
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_ACCESS_GROUP,
                                    "access group not found");
    }
    return rc;
}

static lsmAccessGroup *access_group_by_id(struct plugin_data *pd,
                                            const char *key)
{
    struct allocated_ag *find = g_hash_table_lookup(pd->access_groups, key);
    if(find) {
        return find->ag;
    }
    return NULL;
}

static int ag_granted_to_volume( lsmPluginPtr c,
                                    lsmVolume *volume,
                                    lsmAccessGroup **groups[],
                                    uint32_t *count, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    GHashTableIter iter;
    char *k = NULL;
    GHashTable *v = NULL;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    const char* volume_id = lsmVolumeIdGet(volume);
    g_hash_table_iter_init (&iter, pd->group_grant);
    GSList *result = NULL;

    *count = 0;

    while( g_hash_table_iter_next( &iter, (gpointer)&k, (gpointer)&v) ) {
        if( g_hash_table_lookup(v, volume_id) ) {
            *count += 1;
            result = g_slist_prepend(result, access_group_by_id(pd, k));
        }
    }

    if( *count ) {
        int i = 0;
        *groups = lsmAccessGroupRecordArrayAlloc(*count);
        GSList *siter = NULL;

        if( *groups ) {
            for( siter = result; siter ; siter = g_slist_next(siter), i++) {
                (*groups)[i] = lsmAccessGroupRecordCopy(
                                                (lsmAccessGroup *)siter->data);

                if( !(*groups)[i] ) {
                    rc = LSM_ERR_NO_MEMORY;
                    lsmAccessGroupRecordArrayFree(*groups, i);
                    *groups = NULL;
                    *count = 0;
                    break;
                }
            }
        } else {
            rc = LSM_ERR_NO_MEMORY;
        }
    }

    if(result) {
        g_slist_free(result);
    }
    return rc;
}

int static volume_dependency(lsmPluginPtr c,
                                            lsmVolume *volume,
                                            uint8_t *yes, lsmFlag_t flags)
{
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    struct allocated_volume *av = find_volume(pd, lsmVolumeIdGet(volume));

    if( av ) {
        *yes = 0;
        return LSM_ERR_OK;
    } else {
        return LSM_ERR_NOT_FOUND_VOLUME;
    }
}

int static volume_dependency_rm(lsmPluginPtr c,
                                            lsmVolume *volume,
                                            char **job, lsmFlag_t flags)
{
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
   struct allocated_volume *av = find_volume(pd, lsmVolumeIdGet(volume));

    if( av ) {
        return create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);
    } else {
        return LSM_ERR_NOT_FOUND_VOLUME;
    }
}

static void str_concat( char *dest, size_t d_len,
                        const char *str1, const char *str2)
{
    if( dest && d_len && str1 && str2 ) {
        strncpy(dest, str1, d_len);
        strncat(dest, str2,  d_len - strlen(str1) - 1);
    }
}


static int initiator_grant(lsmPluginPtr c, const char *initiator_id,
                                        lsmInitiatorType initiator_type,
                                        lsmVolume *volume,
                                        lsmAccessType access,
                                        lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    lsmAccessGroup *ag = NULL;
    char name[1024];

    str_concat(name, sizeof(name), initiator_id, lsmVolumeIdGet(volume));

    rc = access_group_create(c, name, initiator_id, initiator_type,
                        lsmVolumeSystemIdGet(volume), &ag, flags);
    if( LSM_ERR_OK == rc ) {
        rc = access_group_grant(c, ag, volume, access, flags);

        if( LSM_ERR_OK != rc ) {
            /* If we didn't succeed, remove the access group */
            access_group_delete(c, ag, flags);
        }
        lsmAccessGroupRecordFree(ag);
    }
    return rc;
}


static lsmAccessGroup *get_access_group( lsmPluginPtr c, char *group_name,
                                            int *found)
{
    lsmAccessGroup *ag = NULL;
    lsmAccessGroup **groups = NULL;
    uint32_t count = 0;

    int rc = access_group_list(c, &groups, &count, LSM_FLAG_RSVD);

    if( LSM_ERR_OK == rc ) {
        uint32_t i;
        for( i = 0; i < count; ++i ) {
            if( strcmp(lsmAccessGroupNameGet(groups[i]), group_name) == 0 ) {
                ag = lsmAccessGroupRecordCopy(groups[i]);
                *found = 1;
                break;
            }
        }
        lsmAccessGroupRecordArrayFree(groups, count);
    }
    return ag;
}


static int initiator_revoke(lsmPluginPtr c, lsmInitiator *init,
                                        lsmVolume *volume,
                                        lsmFlag_t flags)
{
    int rc = 0;
    char name[1024];


    str_concat(name, sizeof(name), lsmInitiatorIdGet(init),
                lsmVolumeIdGet(volume));

    int found = 0;
    lsmAccessGroup *ag = get_access_group(c, name, &found);

    if( found && ag ) {
        rc = access_group_delete(c, ag, flags);
    } else {
        if( found && !ag) {
            rc = LSM_ERR_NO_MEMORY;
        } else {
            rc = LSM_ERR_NO_MAPPING;
        }
    }

    lsmAccessGroupRecordFree(ag);

    return rc;
}



static int initiators_granted_to_vol(lsmPluginPtr c,
                                        lsmVolume *volume,
                                        lsmInitiator **initArray[],
                                        uint32_t *count, lsmFlag_t flags)
{
    return _list_initiators(c, initArray, count, flags, volume);
}

static int iscsi_chap_auth(lsmPluginPtr c, lsmInitiator *initiator,
                                const char *in_user, const char *in_password,
                                const char *out_user, const char *out_password,
                                lsmFlag_t flags)
{
    if (initiator) {
        return 0;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

static int _initiator_in_ag(struct plugin_data *pd, lsmAccessGroup *ag,
                            const char *init_id)
{
    lsmStringList *initiators = lsmAccessGroupInitiatorIdGet(ag);
    if( initiators ) {
        uint32_t count = lsmStringListSize(initiators);
        uint32_t i = 0;

        for ( i = 0; i < count; ++i ) {
            if( strcmp(lsmStringListElemGet(initiators, i), init_id) == 0 ) {
                return 1;
            }
        }
    }
    return 0;
}

static int vol_accessible_by_init(lsmPluginPtr c,
                                    lsmInitiator *initiator,
                                    lsmVolume **volumes[],
                                    uint32_t *count, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    GHashTableIter iter;
    char *key = NULL;
    struct allocated_ag *val = NULL;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    const char *search = lsmInitiatorIdGet(initiator);
    GHashTable *tmp_vols = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
                                                    NULL);

    if( tmp_vols ) {

        /*
         * Walk through each access group, if the initiator is present in the
         * group then add all the volumes that are accessible from that group
         * to a list.  Once we are done, then allocate and fill out the volume
         * objects to return
         */
        g_hash_table_iter_init (&iter, pd->access_groups);

        while (g_hash_table_iter_next (&iter, (gpointer) &key,
                                                        (gpointer)&val) ) {
            if( _initiator_in_ag(pd, val->ag, search) ) {
                GHashTable *v = NULL;


                v = (GHashTable *)g_hash_table_lookup( pd->group_grant,
                                                lsmAccessGroupIdGet(val->ag));

                if( v ) {
                    GHashTableIter iter_vol;
                    char *vk = NULL;
                    lsmAccessType *vv = NULL;

                    g_hash_table_iter_init (&iter_vol, v);

                    while( g_hash_table_iter_next(&iter_vol, (gpointer)&vk,
                                                    (gpointer)&vv) ) {
                        g_hash_table_insert(tmp_vols, vk, vk);
                    }
                }
            }
        }

        *count = g_hash_table_size(tmp_vols);
        if( *count ) {
            *volumes = lsmVolumeRecordArrayAlloc( *count );
            if( *volumes ) {
                /* Walk through each volume and see if we should include it*/

                uint32_t alloc_count = 0;
                char *k = NULL;
                struct allocated_volume *vol;
                GHashTableIter iter;
                g_hash_table_iter_init(&iter, pd->volumes);
                while(g_hash_table_iter_next(&iter,(gpointer) &k,(gpointer)&vol)) {
                    lsmVolume *tv = vol->v;

                    if( g_hash_table_lookup(tmp_vols, lsmVolumeIdGet(tv))) {
                        (*volumes)[alloc_count] = lsmVolumeRecordCopy(tv);
                        if( !(*volumes)[alloc_count] ) {
                            rc = LSM_ERR_NO_MEMORY;
                            lsmVolumeRecordArrayFree(*volumes, alloc_count);
                            *count = 0;
                            *volumes = NULL;
                            break;
                        } else {
                            alloc_count += 1;
                        }
                    }
                }
            } else {
                *count = 0;
                rc = LSM_ERR_NO_MEMORY;
            }
        }

        g_hash_table_destroy(tmp_vols);
    } else {
        rc = LSM_ERR_NO_MEMORY;
    }

    return rc;
}

static struct lsmSanOpsV1 sanOps = {
    list_initiators,
    list_volumes,
    list_disks,
    pool_create,
    pool_create_from_disks,
    pool_create_from_volumes,
    pool_create_from_pool,
    pool_delete,
    volume_create,
    volume_replicate,
    volume_replicate_range_bs,
    volume_replicate_range,
    volume_resize,
    volume_delete,
    volume_online_offline,
    volume_online_offline,
    initiator_grant,
    initiator_revoke,
    initiators_granted_to_vol,
    iscsi_chap_auth,
    access_group_list,
    access_group_create,
    access_group_delete,
    access_group_add_initiator,
    access_group_del_initiator,
    access_group_grant,
    access_group_revoke,
    vol_accessible_by_ag,
    vol_accessible_by_init,
    ag_granted_to_volume,
    volume_dependency,
    volume_dependency_rm
};


static int fs_list(lsmPluginPtr c, lsmFs **fs[], uint32_t *count,
                    lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    *count = g_hash_table_size(pd->fs);

    if( *count ) {
        *fs = lsmFsRecordArrayAlloc( *count );
        if( *fs ) {
            uint32_t i = 0;
            char *k = NULL;
            struct allocated_fs *afs = NULL;
            GHashTableIter iter;
            g_hash_table_iter_init(&iter, pd->fs);
            while(g_hash_table_iter_next(&iter,(gpointer) &k,(gpointer)&afs)) {
                (*fs)[i] = lsmFsRecordCopy(afs->fs);
                if( !(*fs)[i] ) {
                    rc = LSM_ERR_NO_MEMORY;
                    lsmFsRecordArrayFree(*fs, i);
                    *count = 0;
                    *fs = NULL;
                    break;
                }
                ++i;
            }
        } else {
            rc = lsmLogErrorBasic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
        }
    }
    return rc;
}

static int fs_create(lsmPluginPtr c, lsmPool *pool, const char *name,
                uint64_t size_bytes, lsmFs **fs, char **job, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    lsmPool *p = find_pool(pd, lsmPoolIdGet(pool));


    if( p && !g_hash_table_lookup(pd->fs, md5(name)) ) {
        uint64_t allocated_size = pool_allocate(p, size_bytes);
        if( allocated_size ) {
            char *id = md5(name);
            char *key = strdup(id);
            lsmFs *new_fs = NULL;

            /* Make a copy to store and a copy to hand back to caller */
            lsmFs *tfs = lsmFsRecordAlloc(id, name, allocated_size,
                                allocated_size, lsmPoolIdGet(pool), sys_id);
            new_fs = lsmFsRecordCopy(tfs);

            /* Allocate the memory to keep the associations */
            struct allocated_fs *afs = alloc_fs_record();

            if( key && tfs && afs ) {
                afs->fs = tfs;
                afs->p = p;
                g_hash_table_insert(pd->fs, key, afs);

                rc = create_job(pd, job, LSM_DATA_TYPE_FS, new_fs, (void**)fs);
            } else {
                free(key);
                lsmFsRecordFree(new_fs);
                lsmFsRecordFree(tfs);
                free_fs_record(afs);

                *fs = NULL;
                rc = lsmLogErrorBasic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
            }
        } else {
            rc = lsmLogErrorBasic(c, LSM_ERR_SIZE_INSUFFICIENT_SPACE,
                                                "Insufficient space in pool");
        }
    } else {
        if( p == NULL ) {
            rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_POOL, "Pool not found!");
        } else {
            rc = lsmLogErrorBasic(c, LSM_ERR_EXISTS_NAME,
                                        "File system with name exists");
        }
    }
    return rc;
}

static int fs_delete(lsmPluginPtr c, lsmFs *fs, char **job, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    if( !g_hash_table_remove(pd->fs, lsmFsIdGet(fs)) ) {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_FS, "FS not found!");
    } else {
        rc = create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);
    }
    return rc;
}

static int fs_resize(lsmPluginPtr c, lsmFs *fs,
                                    uint64_t new_size_bytes, lsmFs * *rfs,
                                    char **job, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    struct allocated_fs *afs = g_hash_table_lookup(pd->fs, lsmFsIdGet(fs));

    *rfs = NULL;
    *job = NULL;

    if( afs ) {
        lsmPool *p = afs->p;
        lsmFs *tfs = afs->fs;

        pool_deallocate(p, lsmFsTotalSpaceGet(tfs));
        uint64_t resized_size = pool_allocate(p, new_size_bytes);

        if( resized_size ) {

            lsmFs *resized = lsmFsRecordAlloc(lsmFsIdGet(tfs),
                                                lsmFsNameGet(tfs),
                                                new_size_bytes,
                                                new_size_bytes,
                                                lsmFsPoolIdGet(tfs),
                                                lsmFsSystemIdGet(tfs));
            lsmFs *returned_copy = lsmFsRecordCopy(resized);

            if( resized && returned_copy ) {
                lsmFsRecordFree(tfs);
                afs->fs = resized;

                rc = create_job(pd, job, LSM_DATA_TYPE_FS, returned_copy,
                                    (void**)rfs);

            } else {
                lsmFsRecordFree(resized);
                lsmFsRecordFree(returned_copy);
                *rfs = NULL;

                pool_deallocate(p, new_size_bytes);
                pool_allocate(p, lsmFsTotalSpaceGet(tfs));
                rc = lsmLogErrorBasic(c, LSM_ERR_NO_MEMORY,
                                    "ENOMEM");
            }
        } else {
            /*Could not accommodate re-sized, go back */
            pool_allocate(p, lsmFsTotalSpaceGet(tfs));
            rc = lsmLogErrorBasic(c, LSM_ERR_SIZE_INSUFFICIENT_SPACE,
                                                "Insufficient space in pool");
        }

    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_FS,
                                    "file system not found!");
    }
    return rc;
}

static int fs_clone(lsmPluginPtr c, lsmFs *src_fs, const char *dest_fs_name,
                    lsmFs **cloned_fs, lsmSs *optional_snapshot,
                    char **job, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    struct allocated_fs *find = g_hash_table_lookup(pd->fs, lsmFsIdGet(src_fs));

    if( find ) {
        rc = fs_create(c, find->p, dest_fs_name, lsmFsTotalSpaceGet(find->fs),
                        cloned_fs, job, flags);
    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_FS, "Source fs not found");
    }

    return rc;
}

static int fs_file_clone(lsmPluginPtr c, lsmFs *fs,
                                    const char *src_file_name,
                                    const char *dest_file_name,
                                    lsmSs *snapshot, char **job,
                                    lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    struct allocated_fs *find = (struct allocated_fs *)g_hash_table_lookup(
                                    pd->fs, lsmFsIdGet(fs));
    if( !find ) {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    } else {
        rc = create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);
    }
    return rc;
}

static int fs_child_dependency(lsmPluginPtr c, lsmFs *fs,
                                                lsmStringList *files,
                                                uint8_t *yes)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    if( g_hash_table_lookup(pd->fs, lsmFsIdGet(fs))) {
        *yes = 0;
    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    }
    return rc;
}

static int fs_child_dependency_rm( lsmPluginPtr c, lsmFs *fs,
                                                lsmStringList *files,
                                                char **job, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);
    if( !g_hash_table_lookup(pd->fs, lsmFsIdGet(fs))) {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    } else {
        rc = create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);
    }
    return rc;
}

static int ss_list(lsmPluginPtr c, lsmFs * fs, lsmSs **ss[],
                                uint32_t *count, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    struct allocated_fs *find = (struct allocated_fs *)g_hash_table_lookup(
                                    pd->fs, lsmFsIdGet(fs));

    if( find ) {
        char *k = NULL;
        lsmSs *v = NULL;
        GHashTableIter iter;

        *ss = NULL;
        *count = g_hash_table_size(find->ss);

        if( *count ) {
            *ss = lsmSsRecordArrayAlloc(*count);
            if( *ss ) {
                int i = 0;
                g_hash_table_iter_init(&iter, find->ss);

                while(g_hash_table_iter_next(&iter,
                                            (gpointer) &k,(gpointer)&v)) {
                    (*ss)[i] = lsmSsRecordCopy(v);
                    if( !(*ss)[i] ) {
                        rc = lsmLogErrorBasic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
                        lsmSsRecordArrayFree(*ss, i);
                        *ss = NULL;
                        *count = 0;
                        break;
                    }
                    ++i;
                }

            } else {
                rc = lsmLogErrorBasic(c, LSM_ERR_NO_MEMORY, "ENOMEM");
                *count = 0;
            }
        }
    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    }
    return rc;
}

static int ss_create(lsmPluginPtr c, lsmFs *fs,
                                    const char *name, lsmStringList *files,
                                    lsmSs **snapshot, char **job,
                                    lsmFlag_t flags)
{
    int rc = LSM_ERR_NO_MEMORY;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    struct allocated_fs *find = (struct allocated_fs *)g_hash_table_lookup(
                                    pd->fs, lsmFsIdGet(fs));

    if( find ) {
        if( !g_hash_table_lookup(find->ss, md5(name)) ) {
            char *id = strdup(md5(name));
            if( id ) {
                lsmSs *ss = lsmSsRecordAlloc(id, name, time(NULL));
                lsmSs *new_shot = lsmSsRecordCopy(ss);
                if( ss && new_shot ) {
                    g_hash_table_insert(find->ss, (gpointer)id, (gpointer)ss);
                    rc = create_job(pd, job, LSM_DATA_TYPE_SS, new_shot,
                                        (void**)snapshot);
                } else {
                    lsmSsRecordFree(ss);
                    ss = NULL;
                    lsmSsRecordFree(new_shot);
                    *snapshot = NULL;
                    free(id);
                    id = NULL;
                }
            }
        } else {
            rc = lsmLogErrorBasic(c, LSM_ERR_EXISTS_NAME,
                                        "snapshot name exists");
        }
    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    }
    return rc;
}

static int ss_delete(lsmPluginPtr c, lsmFs *fs, lsmSs *ss,
                                    char **job, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    struct allocated_fs *find = (struct allocated_fs *)g_hash_table_lookup(
                                    pd->fs, lsmFsIdGet(fs));

    if( find ) {
        if( !g_hash_table_remove(find->ss, lsmSsIdGet(ss)) ) {
            rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_SS,
                                    "snapshot not found");
        } else {
            rc = create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);
        }
    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    }
    return rc;
}

static int ss_revert(lsmPluginPtr c, lsmFs *fs, lsmSs *ss,
                                    lsmStringList *files,
                                    lsmStringList *restore_files,
                                    int all_files, char **job, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    struct allocated_fs *find = (struct allocated_fs *)g_hash_table_lookup(
                                    pd->fs, lsmFsIdGet(fs));

    if( find ) {
        if(!g_hash_table_lookup(find->ss, lsmSsIdGet(ss))) {
            rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_SS,
                                    "snapshot not found");
        } else {
            rc = create_job(pd, job, LSM_DATA_TYPE_NONE, NULL, NULL);
        }
    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    }
    return rc;
}

static struct lsmFsOpsV1 fsOps = {
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
    ss_revert
};

static int nfs_auth_types(lsmPluginPtr c, lsmStringList **types,
                            lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    *types = lsmStringListAlloc(1);
    if( *types ) {
        lsmStringListElemSet(*types, 0, "standard");
    } else {
        rc = LSM_ERR_NO_MEMORY;
    }
    return rc;
}

static int nfs_export_list( lsmPluginPtr c, lsmNfsExport **exports[],
                                uint32_t *count, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    GHashTableIter fs_iter;
    GHashTableIter exports_iter;
    char *k = NULL;
    struct allocated_fs *v = NULL;
    GSList *result = NULL;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    g_hash_table_iter_init (&fs_iter, pd->fs);

    *count = 0;

    /* Walk through each of the file systems and their associated exports */
    while( g_hash_table_iter_next( &fs_iter, (gpointer)&k, (gpointer)&v) ) {
        char *exp_key = NULL;
        lsmNfsExport **exp_val = NULL;

        g_hash_table_iter_init (&exports_iter, v->exports );
        while( g_hash_table_iter_next( &exports_iter,   (gpointer)&exp_key,
                                                        (gpointer)&exp_val) ) {
            result = g_slist_prepend(result, exp_val);
            *count += 1;
        }
    }

    if( *count ) {
        int i = 0;
        GSList *s_iter = NULL;
        *exports = lsmNfsExportRecordArrayAlloc(*count);
        if( *exports ) {
            for( s_iter = result; s_iter ; s_iter = g_slist_next(s_iter), i++) {
                (*exports)[i] = lsmNfsExportRecordCopy(
                                                (lsmNfsExport *)s_iter->data);

                if( !(*exports)[i] ) {
                    rc = LSM_ERR_NO_MEMORY;
                    lsmNfsExportRecordArrayFree(*exports, i);
                    *exports = NULL;
                    *count = 0;
                    break;
                }
            }
        } else {
            rc = LSM_ERR_NO_MEMORY;
        }
    }

    if( result ) {
        g_slist_free(result);
        result = NULL;
    }

    return rc;
}

static int nfs_export_create( lsmPluginPtr c,
                                        const char *fs_id,
                                        const char *export_path,
                                        lsmStringList *root_list,
                                        lsmStringList *rw_list,
                                        lsmStringList *ro_list,
                                        uint64_t anon_uid,
                                        uint64_t anon_gid,
                                        const char *auth_type,
                                        const char *options,
                                        lsmNfsExport **exported,
                                        lsmFlag_t flags
                                        )
{
    int rc = LSM_ERR_OK;
    char auto_export[2048];
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    struct allocated_fs *fs = g_hash_table_lookup(pd->fs, fs_id);
    if( fs ) {
        if (!export_path) {
            snprintf(auto_export, sizeof(auto_export), "/mnt/lsm/nfs/%s",
                lsmFsNameGet(fs->fs));
            export_path = auto_export;
        }

        char *key = strdup(md5(export_path));
        *exported = lsmNfsExportRecordAlloc(md5(export_path),
                                            fs_id,
                                            export_path,
                                            auth_type,
                                            root_list,
                                            rw_list,
                                            ro_list,
                                            anon_uid,
                                            anon_gid,
                                            options);

        lsmNfsExport *value = lsmNfsExportRecordCopy(*exported);

        if( key && *exported && value ) {
            g_hash_table_insert(fs->exports, key, value);
        } else {
            rc = LSM_ERR_NO_MEMORY;
            free(key);
            lsmNfsExportRecordFree(*exported);
            lsmNfsExportRecordFree(value);
        }

    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    }
    return rc;
}

static int nfs_export_remove( lsmPluginPtr c, lsmNfsExport *e,
                                lsmFlag_t flags )
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    struct allocated_fs *fs = g_hash_table_lookup(pd->fs,
                                                    lsmNfsExportFsIdGet(e));
    if( fs ) {
        if( !g_hash_table_remove(fs->exports, lsmNfsExportIdGet(e))) {
            rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_NFS_EXPORT,
                                        "export not found");
        }
    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_FS, "fs not found");
    }
    return rc;
}

static struct lsmNasOpsV1 nfsOps = {
    nfs_auth_types,
    nfs_export_list,
    nfs_export_create,
    nfs_export_remove
};


void free_group_grant_hash(void *v)
{
    g_hash_table_destroy((GHashTable *)v);
}

void free_allocated_fs(void *v)
{
    free_fs_record((struct allocated_fs*)v);
}

void free_disk(void *d)
{
    lsmDiskRecordFree((lsmDisk *)d);
}

void free_allocated_volume(void *v)
{
    if( v ) {
        struct allocated_volume *av = (struct allocated_volume *)v;
        lsmVolumeRecordFree(av->v);
        av->v = NULL;
		av->p = NULL;  /* Pool takes care of itself */
		free(av);
    }
}

/* Foward decl.*/
int unload( lsmPluginPtr c, lsmFlag_t flags);

int load( lsmPluginPtr c, xmlURIPtr uri, const char *password,
                        uint32_t timeout,  lsmFlag_t flags)
{
    struct plugin_data *pd = (struct plugin_data *)
                                malloc(sizeof(struct plugin_data));
    int rc = LSM_ERR_NO_MEMORY;
    int i;
    lsmPool *p = NULL;
    if( pd ) {
        memset(pd, 0, sizeof(struct plugin_data));

        pd->num_systems = 1;
        pd->system[0] = lsmSystemRecordAlloc(sys_id,
                                                "LSM simulated storage plug-in",
                                                LSM_SYSTEM_STATUS_OK);

        p = lsmPoolRecordAlloc("POOL_3", "lsm_test_aggr",
                                            UINT64_MAX, UINT64_MAX,
                                            LSM_POOL_STATUS_OK,
                                            sys_id);
        if( p ) {
            pd->pools = g_hash_table_new_full(g_str_hash, g_str_equal, free,
                        free_pool_record);

            g_hash_table_insert(pd->pools, strdup(lsmPoolIdGet(p)), p);

            for( i = 0; i < 3; ++i ) {
                char name[32];
                snprintf(name, sizeof(name), "POOL_%d", i);

                p = lsmPoolRecordAlloc(name, name, UINT64_MAX,
                                            UINT64_MAX, LSM_POOL_STATUS_OK,
                                            sys_id);

                if( p ) {
                    g_hash_table_insert(pd->pools, strdup(lsmPoolIdGet(p)), p);
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
                            free, free_group_grant_hash);

        pd->fs = g_hash_table_new_full(g_str_hash, g_str_equal, free,
                                            free_allocated_fs);

        pd->jobs = g_hash_table_new_full(g_str_hash, g_str_equal, free,
                                            free_allocated_job);

        pd->disks = g_hash_table_new_full(g_str_hash, g_str_equal, free,
                                            free_disk);

        /* Create disks */
        lsmOptionalData *od = lsmOptionalDataRecordAlloc();
        if( od ) {
            for( i = 0; i < 10; ++i ) {
                lsmDisk *d = NULL;
                char name[17];
                char sn[32];
                char *key = NULL;


                snprintf(name, sizeof(name), "Sim C disk %d", i);
                snprintf(sn, sizeof(sn), "SIMDISKSN00000%04d\n", i);

                lsmOptionalDataStringSet(od, "sn", sn);

                d = lsmDiskRecordAlloc(md5(name), name, LSM_DISK_TYPE_SOP, 512,
                    0x8000000000000, LSM_DISK_STATUS_OK,  od, sys_id);

                key = strdup(lsmDiskIdGet(d));

                if( !key ) {
                    g_hash_table_destroy(pd->disks);
                    pd->disks = NULL;
                    break;
                }

                g_hash_table_insert(pd->disks, key, d);
                d = NULL;
            }
            lsmOptionalDataRecordFree(od);
        }


        if( !pd->system[0] || !pd->volumes || !pd->pools || !pd->access_groups
            || !pd->group_grant || !pd->fs || !pd->jobs || !pd->disks ) {
            rc = LSM_ERR_NO_MEMORY; /* We need to free everything */
            unload(c, 0);
        } else {
            rc = lsmRegisterPluginV1( c, pd, &mgmOps,
                                    &sanOps, &fsOps, &nfsOps);
        }
    }
    return rc;
}

int unload( lsmPluginPtr c, lsmFlag_t flags)
{
    uint32_t i = 0;

    struct plugin_data *pd = (struct plugin_data*)lsmPrivateDataGet(c);

    if( pd ) {

        if( pd->disks ) {
            g_hash_table_destroy(pd->disks);
            pd->disks = NULL;
        }

        if( pd->jobs ) {
            g_hash_table_destroy(pd->jobs);
            pd->jobs = NULL;
        }

        if(pd->fs) {
            g_hash_table_destroy(pd->fs);
            pd->fs = NULL;
        }

        if(pd->group_grant) {
            g_hash_table_destroy(pd->group_grant);
            pd->group_grant = NULL;
        }

        if( pd->access_groups ) {
            g_hash_table_destroy(pd->access_groups);
            pd->access_groups = NULL;
        }

        if( pd->volumes ) {
            g_hash_table_destroy(pd->volumes);
            pd->volumes = NULL;
        }

        if( pd->pools ) {
            g_hash_table_destroy(pd->pools);
            pd->pools = NULL;
        }

        for( i = 0; i < pd->num_systems; ++i ) {
            lsmSystemRecordFree(pd->system[i]);
            pd->system[i]= NULL;
        }
        pd->num_systems = 0;

        free(pd);
        pd = NULL;
    }

    return LSM_ERR_OK;
}

int main(int argc, char *argv[] )
{
    return lsmPluginInitV1(argc, argv, load, unload, name, version);
}


#ifdef  __cplusplus
}
#endif
