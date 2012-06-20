
/*
 * Copyright (C) 2011-2012 Red Hat, Inc.
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

#include "libstoragemgmt/libstoragemgmt_accessgroups.h"
#include "libstoragemgmt/libstoragemgmt_initiators.h"

#ifdef  __cplusplus
extern "C" {
#endif

static char name[] = "Compiled plug-in example";
static char version [] = "0.01";
static char sys_id[] = "SYS0";
#define BS 512

#define MAX_SYSTEMS 1
#define MAX_POOLS 4
#define MAX_VOLUMES 128
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
 * @param array			Base address for the array
 * @param remove_index	Element index to remove
 * @param num_elems		Number of elements currently in the array
 * @param elem_size		Size of each array element
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
    lsmVolumePtr v;
    lsmPoolPtr p;
};

struct plugin_data {
    uint32_t tmo;
    uint32_t num_systems;
    lsmSystemPtr system[MAX_SYSTEMS];

    uint32_t num_pools;
    lsmPoolPtr pool[MAX_POOLS];

    uint32_t num_volumes;
    struct allocated_volume volume[MAX_VOLUMES];

    uint32_t num_fs;
    lsmFsPtr fs[MAX_FS];

    uint32_t num_exports;
    lsmNfsExportPtr nfs[MAX_EXPORT];

    GHashTable *access_groups;
    GHashTable *group_grant;
};

static int tmo_set(lsmPluginPtr c, uint32_t timeout )
{
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);
    pd->tmo = timeout;
    return LSM_ERR_OK;
}

static int tmo_get(lsmPluginPtr c, uint32_t *timeout)
{
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);
    *timeout = pd->tmo;
    return LSM_ERR_OK;
}

static int cap(lsmPluginPtr c, lsmStorageCapabilitiesPtr *cap)
{
    return LSM_ERR_NO_SUPPORT;
}

static int jobStatus(lsmPluginPtr c, const char *job_id,
                        lsmJobStatus *status, uint8_t *percentComplete,
                        lsmDataType *t,
                        void **value)
{
    return LSM_ERR_NO_SUPPORT;
}

static int list_pools(lsmPluginPtr c, lsmPoolPtr **poolArray,
                                        uint32_t *count)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);
    *count = pd->num_pools;

    *poolArray = lsmPoolRecordAllocArray( pd->num_pools );
    if( *poolArray ) {
        uint32_t i = 0;
        for( i = 0; i < pd->num_pools; ++i ) {
            (*poolArray)[i] = lsmPoolRecordCopy(pd->pool[i]);
            if( !(*poolArray)[i] ) {
                rc = LSM_ERR_NO_MEMORY;
                lsmPoolRecordFreeArray(*poolArray, i);
                *poolArray = NULL;
                *count = 0;
                break;
            }
        }
    } else {
        rc = LSM_ERR_NO_MEMORY;
    }

    return rc;
}

static int list_systems(lsmPluginPtr c, lsmSystemPtr **systems,
                                        uint32_t *systemCount)
{
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);

    *systemCount = pd->num_systems;
    *systems = lsmSystemRecordAllocArray(MAX_SYSTEMS);

    if( *systems ) {
        (*systems)[0] = lsmSystemRecordCopy(pd->system[0]);

        if( (*systems)[0] ) {
            return LSM_ERR_OK;
        } else {
            lsmSystemRecordFreeArray(*systems, pd->num_systems);
        }
    }
    return LSM_ERR_NO_MEMORY;
}

static int jobFree(lsmPluginPtr c, char *job_id)
{
    return LSM_ERR_NOT_IMPLEMENTED;
}

static struct lsmMgmtOps mgmOps = {
    tmo_set,
    tmo_get,
    cap,
    jobStatus,
    jobFree,
    list_pools,
    list_systems,
};

static int list_initiators(lsmPluginPtr c, lsmInitiatorPtr **initArray,
                                        uint32_t *count)
{
    return LSM_ERR_NOT_IMPLEMENTED;
}

static int list_volumes(lsmPluginPtr c, lsmVolumePtr **vols,
                                        uint32_t *count)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);
    *count = pd->num_volumes;

    *vols = lsmVolumeRecordAllocArray( pd->num_volumes );
    if( *vols ) {
        uint32_t i = 0;
        for( i = 0; i < pd->num_volumes; ++i ) {
            (*vols)[i] = lsmVolumeRecordCopy(pd->volume[i].v);
            if( !(*vols)[i] ) {
                rc = LSM_ERR_NO_MEMORY;
                lsmVolumeRecordFreeArray(*vols, i);
                *vols = NULL;
                *count = 0;
                break;
            }
        }
    } else {
        rc = LSM_ERR_NO_MEMORY;
    }

    return rc;
}

static uint64_t pool_allocate(lsmPoolPtr p, uint64_t size)
{
    uint64_t rounded_size = 0;
    int rc = LSM_ERR_OK;
    uint64_t free_space = lsmPoolFreeSpaceGet(p);

    rounded_size = (size/BS) * BS;

    if( free_space >= rounded_size ) {
        free_space -= rounded_size;
        lsmPoolFreeSpaceSet(p, free_space);
    } else {
        rc = 0;
    }
    return rounded_size;
}

void pool_deallocate(lsmPoolPtr p, uint64_t size)
{
    uint64_t free_space = lsmPoolFreeSpaceGet(p);

    free_space += size;
    lsmPoolFreeSpaceSet(p, free_space);
}

static int find_pool(struct plugin_data *pd, const char* pool_id)
{
    if( pd ) {
        int i;
        for( i = 0; i < pd->num_pools; ++i ) {
            if( strcmp(lsmPoolIdGet(pd->pool[i]), pool_id) == 0) {
                return i;
            }
        }
    }
    return -1;
}

static int find_volume_name(struct plugin_data *pd, const char *name)
{
    if( pd ) {
        int i;
        for( i = 0; i < pd->num_volumes; ++i ) {
            if( strcmp(lsmVolumeNameGet(pd->volume[i].v), name) == 0) {
                return i;
            }
        }
    }
    return -1;
}

static int volume_create(lsmPluginPtr c, lsmPoolPtr pool,
                        const char *volumeName, uint64_t size,
                        lsmProvisionType provisioning, lsmVolumePtr *newVolume,
                        char **job)
{
    int rc = LSM_ERR_OK;

    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);
    int pool_index = find_pool(pd, lsmPoolIdGet(pool));

    if( pool_index >= 0 ) {
        if( -1 == find_volume_name(pd, volumeName) ) {
            if ( pd->num_volumes < MAX_VOLUMES ) {
                lsmPoolPtr p = pd->pool[pool_index];
                uint64_t allocated_size = pool_allocate(p, size);
                if( allocated_size ) {
                    char *id = md5(volumeName);

                    lsmVolumePtr v = lsmVolumeRecordAlloc(id, volumeName,
                                       "VPD", BS, allocated_size/BS, 0, sys_id);

                    if( v ) {
                        *newVolume = v;
                        pd->volume[pd->num_volumes].v = lsmVolumeRecordCopy(v);
                        pd->volume[pd->num_volumes].p = p;
                        pd->num_volumes +=1;
                    } else {
                        rc = lsmLogErrorBasic(c, LSM_ERR_NO_MEMORY,
                                                "Check for leaks");
                    }

                } else {
                    rc = lsmLogErrorBasic(c, LSM_ERR_SIZE_INSUFFICIENT_SPACE,
                                                "Insufficient space in pool");
                }
            } else {
                rc = lsmLogErrorBasic(c, LSM_ERR_SIZE_LIMIT_REACHED,
                                            "Number of volumes limit reached");
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

static int volume_replicate(lsmPluginPtr c, lsmPoolPtr pool,
                        lsmReplicationType repType, lsmVolumePtr volumeSrc,
                        const char *name, lsmVolumePtr *newReplicant,
                        char **job)
{
    int rc = LSM_ERR_OK;
    int pi;
    int vi;
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);

    pi = find_pool(pd, lsmPoolIdGet(pool));
    vi = find_volume_name(pd, lsmVolumeNameGet(volumeSrc));

    if( pi > -1 && vi > -1 ) {
        rc = volume_create(c, pool, name,
                                lsmVolumeNumberOfBlocks(volumeSrc)*BS,
                                LSM_PROVISION_DEFAULT, newReplicant, job);


    } else {
        if ( -1 == vi ) {
            rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                    "Volume not found!");
        }

        if( -1 == pi ) {
            rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_POOL,
                                    "Pool not found!");
        }
    }
    return rc;
}

static int volume_replicate_range_bs(lsmPluginPtr c, uint32_t *bs)
{
    *bs = BS;
    return LSM_ERR_OK;
}

static int volume_replicate_range(lsmPluginPtr c,
                                    lsmReplicationType repType,
                                    lsmVolumePtr source,
                                    lsmVolumePtr dest,
                                    lsmBlockRangePtr *ranges,
                                    uint32_t num_ranges, char **job)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);

    int src_v = find_volume_name(pd, lsmVolumeNameGet(source));
    int dest_v = find_volume_name(pd, lsmVolumeNameGet(dest));

    if( -1 == src_v || -1 == dest_v ) {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                    "Src or dest volumes not found!");
    }

    return rc;
}

static int volume_resize(lsmPluginPtr c, lsmVolumePtr volume,
                                uint64_t newSize, lsmVolumePtr *resizedVolume,
                                char **job)
{
    int rc = LSM_ERR_OK;
    int vi;
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);

    vi = find_volume_name(pd, lsmVolumeNameGet(volume));

    if( -1 != vi ) {
        lsmVolumePtr v = pd->volume[vi].v;
        lsmPoolPtr p = pd->volume[vi].p;
        uint64_t curr_size = lsmVolumeNumberOfBlocks(v) * BS;

        pool_deallocate(p, curr_size);
        uint64_t resized_size = pool_allocate(p, newSize);
        if( resized_size ) {
            lsmVolumePtr vp = lsmVolumeRecordAlloc(lsmVolumeIdGet(v),
                                                    lsmVolumeNameGet(v),
                                                    lsmVolumeVpd83Get(v),
                                                    lsmVolumeBlockSizeGet(v),
                                                    resized_size/BS, 0, sys_id);
            if( vp ) {
                pd->volume[vi].v = vp;
                lsmVolumeRecordFree(v);
                *resizedVolume = lsmVolumeRecordCopy(vp);
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

static int volume_delete(lsmPluginPtr c, lsmVolumePtr volume,
                                    char **job)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);
    int vi = find_volume_name(pd, lsmVolumeNameGet(volume));
    if( -1 != vi ) {
        lsmVolumePtr vp = pd->volume[vi].v;
        pool_deallocate(pd->volume[vi].p, lsmVolumeNumberOfBlocks(vp)*BS);

        lsmVolumeRecordFree(vp);
        remove_item(pd->volume, vi, pd->num_volumes,
                        sizeof(struct allocated_volume));
        pd->num_volumes -= 1;

    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                    "volume not found!");
    }
    return rc;
}

static int volume_online_offline(lsmPluginPtr c, lsmVolumePtr v)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);

    int vi = find_volume_name(pd, lsmVolumeNameGet(v));
    if( -1 == vi ) {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                    "volume not found!");
    }
    return rc;
}

static int access_group_list(lsmPluginPtr c,
                                lsmAccessGroupPtr **groups,
                                uint32_t *groupCount)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);

    *groupCount = g_hash_table_size(pd->access_groups);

    if( *groupCount ) {
        *groups = lsmAccessGroupRecordAllocArray(*groupCount);
        if( *groups ) {
            int i = 0;
            char *key = NULL;
            lsmAccessGroupPtr val = NULL;
            GHashTableIter iter;

            g_hash_table_iter_init (&iter, pd->access_groups);

            while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer)&val) ) {
                (*groups)[i] = lsmAccessGroupRecordCopy((lsmAccessGroupPtr)val);
                if( !(*groups)[i] ) {
                    rc = LSM_ERR_NO_MEMORY;
                    lsmAccessGroupRecordFreeArray(*groups, i);
                    *groupCount = 0;
                    groups = NULL;
                    break;
                }
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
                                lsmAccessGroupPtr *access_group)
{
    int rc = LSM_ERR_OK;
    lsmAccessGroupPtr ag = NULL;
    char *id = strdup(md5(name));
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);

    lsmAccessGroupPtr find = (lsmAccessGroupPtr)
                        g_hash_table_lookup(pd->access_groups, id);

    if( !find ) {
        lsmStringListPtr initiators = lsmStringListAlloc(1);
        if( initiators && id &&
            (LSM_ERR_OK == lsmStringListSetElem(initiators, 0, initiator_id))) {
            ag = lsmAccessGroupRecordAlloc(id, name, initiators,
                                                        system_id);
            if( !ag ) {
                lsmStringListFree(initiators);
                rc = lsmLogErrorBasic(c, LSM_ERR_NO_MEMORY,
                                    "ENOMEM");
            } else {
                g_hash_table_insert(pd->access_groups, (gpointer)id,
                                        (gpointer)ag);
                *access_group = lsmAccessGroupRecordCopy(ag);
            }
        } else {
            rc = lsmLogErrorBasic(c, LSM_ERR_NO_MEMORY,
                                    "ENOMEM");
        }

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
                                lsmAccessGroupPtr group,
                                char **job)
{
    int rc = LSM_ERR_OK;

    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);
    const char *id = lsmAccessGroupIdGet(group);

    gboolean r = g_hash_table_remove(pd->access_groups, (gpointer)id);

    if( !r ) {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_ACCESS_GROUP,
                                    "access group not found");
    }
    return rc;
}

static int access_group_add_initiator(  lsmPluginPtr c,
                                        lsmAccessGroupPtr group,
                                        const char *initiator_id,
                                        lsmInitiatorType id_type, char **job)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);

    lsmAccessGroupPtr find = (lsmAccessGroupPtr)
                                g_hash_table_lookup(pd->access_groups,
                                lsmAccessGroupIdGet(group));

    if( find ) {
        lsmStringList *inits = lsmAccessGroupInitiatorIdGet(find);
        rc = lsmStringListAppend(inits, initiator_id);
    } else {
        rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_ACCESS_GROUP,
                                    "access group not found");
    }
    return rc;
}

static int access_group_del_initiator(  lsmPluginPtr c,
                                        lsmAccessGroupPtr group,
                                        const char *init,
                                        char **job)
{
    int rc = LSM_ERR_INITIATOR_NOT_IN_ACCESS_GROUP;
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);

    lsmAccessGroupPtr find = (lsmAccessGroupPtr)
                                g_hash_table_lookup(pd->access_groups,
                                lsmAccessGroupIdGet(group));

    if( find ) {
        uint32_t i;
        lsmStringList *inits = lsmAccessGroupInitiatorIdGet(find);

        for(i = 0; i < lsmStringListSize(inits); ++i) {
            if( strcmp(init, lsmStringListGetElem(inits, i)) == 0 ) {
                lsmStringListRemove(inits, i);
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
                                lsmAccessGroupPtr group,
                                lsmVolumePtr volume,
                                lsmAccessType access, char **job)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);

    lsmAccessGroupPtr find = (lsmAccessGroupPtr)
                                g_hash_table_lookup(pd->access_groups,
                                lsmAccessGroupIdGet(group));

    int vi = find_volume_name(pd, lsmVolumeNameGet(volume));

    if( find && (-1 != vi) ) {
        GHashTable *grants = g_hash_table_lookup(pd->group_grant,
                                    lsmAccessGroupIdGet(find));
        if( !grants ) {
            /* We don't have any mappings for this access group*/
            GHashTable *grant = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                        free, free);
            char *key = strdup(lsmAccessGroupIdGet(find));
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
                lsmAccessType *val = (lsmAccessType*) malloc(sizeof(lsmAccessType));
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
        if( -1 == vi ) {
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
                                lsmAccessGroupPtr group,
                                lsmVolumePtr volume, char **job)
{
    int rc = LSM_ERR_NO_MAPPING;
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);

    lsmAccessGroupPtr find = (lsmAccessGroupPtr)
                                g_hash_table_lookup(pd->access_groups,
                                lsmAccessGroupIdGet(group));

    int vi = find_volume_name(pd, lsmVolumeNameGet(volume));

    if( find && (-1 != vi) ) {
        GHashTable *grants = g_hash_table_lookup(pd->group_grant,
                                    lsmAccessGroupIdGet(find));

        if( (grants && g_hash_table_remove(grants, lsmVolumeIdGet(volume)))) {
            rc = LSM_ERR_OK;

            /* TODO we could clean up the entry in the group_grant table
               if the size == 0 */
        }

    } else {
       if( -1 == vi ) {
            rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_VOLUME,
                                        "volume not found");
        } else {
            rc = lsmLogErrorBasic(c, LSM_ERR_NOT_FOUND_ACCESS_GROUP,
                                    "access group not found");
        }
    }
    return rc;
}

static lsmVolumePtr get_volume_by_id(struct plugin_data *pd, const char *id)
{
    int i = 0;
    for( i = 0; i < pd->num_volumes; ++i ) {
        if( 0 == strcmp(id, lsmVolumeIdGet(pd->volume[i].v))) {
            return pd->volume[i].v;
        }
    }
    return NULL;
}

static int vol_accessible_by_ag(lsmPluginPtr c,
                                lsmAccessGroupPtr group,
                                lsmVolumePtr **volumes,
                                uint32_t *count)
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);

    lsmAccessGroupPtr find = (lsmAccessGroupPtr)
                                g_hash_table_lookup(pd->access_groups,
                                lsmAccessGroupIdGet(group));
    if( find ) {
        GHashTable *grants = g_hash_table_lookup(pd->group_grant,
                                    lsmAccessGroupIdGet(find));
        *count = 0;

        if( grants && g_hash_table_size(grants) ) {
            *count = g_hash_table_size(grants);
            GList *keys = g_hash_table_get_keys(grants);
            *volumes = lsmVolumeRecordAllocArray(*count);

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
                        lsmVolumeRecordFreeArray(*volumes, i);
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

static lsmAccessGroupPtr access_group_by_id(struct plugin_data *pd, const char *key)
{
    return g_hash_table_lookup(pd->access_groups, key);
}

static int ag_granted_to_volume( lsmPluginPtr c,
                                    lsmVolumePtr volume,
                                    lsmAccessGroupPtr **groups,
                                    uint32_t *count)
{
    int rc = LSM_ERR_OK;
    GHashTableIter iter;
    char *k = NULL;
    GHashTable *v = NULL;
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);
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
        *groups = lsmAccessGroupRecordAllocArray(*count);
        GSList *iter = NULL;

        for( iter = result; iter ; iter = g_slist_next(iter), i++) {
            (*groups)[i] = lsmAccessGroupRecordCopy(
                                            (lsmAccessGroupPtr)iter->data);

            if( !(*groups)[i] ) {
                rc = LSM_ERR_NO_MEMORY;
                lsmAccessGroupRecordFreeArray(*groups, i);
                *groups = NULL;
                *count = 0;
                break;
            }
        }
    }

    if(result) {
        g_slist_free(result);
    }
    return rc;
}

int static volume_dependency(lsmPluginPtr c,
                                            lsmVolumePtr volume,
                                            uint8_t *yes)
{
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);
    int vi = find_volume_name(pd, lsmVolumeNameGet(volume));

    if( -1 != vi ) {
        *yes = 0;
        return LSM_ERR_OK;
    } else {
        return LSM_ERR_NOT_FOUND_VOLUME;
    }
}

int static volume_dependency_rm(lsmPluginPtr c,
                                            lsmVolumePtr volume,
                                            char **job)
{
    struct plugin_data *pd = (struct plugin_data*)lsmGetPrivateData(c);
    int vi = find_volume_name(pd, lsmVolumeNameGet(volume));

    if( -1 != vi ) {
        return LSM_ERR_OK;
    } else {
        return LSM_ERR_NOT_FOUND_VOLUME;
    }
}

static struct lsmSanOps sanOps = {
    list_initiators,
    list_volumes,
    volume_create,
    volume_replicate,
    volume_replicate_range_bs,
    volume_replicate_range,
    volume_resize,
    volume_delete,
    volume_online_offline,
    volume_online_offline,
    access_group_list,
    access_group_create,
    access_group_delete,
    access_group_add_initiator,
    access_group_del_initiator,
    access_group_grant,
    access_group_revoke,
    vol_accessible_by_ag,
    ag_granted_to_volume,
    volume_dependency,
    volume_dependency_rm
};

static struct lsmFsOps fsOps = {
};

static struct lsmNasOps nfsOps = {

};

void free_ag_value(void *v)
{
    lsmAccessGroupRecordFree((lsmAccessGroupPtr)v);
}

void free_group_grant_hash(void *v)
{
    g_hash_table_destroy((GHashTable *)v);
}

int load( lsmPluginPtr c, xmlURIPtr uri, const char *password,
                        uint32_t timeout )
{
    struct plugin_data *data = (struct plugin_data *)
                                malloc(sizeof(struct plugin_data));
    int rc = LSM_ERR_NO_MEMORY;
    if( data ) {
        memset(data, 0, sizeof(struct plugin_data));

        data->num_systems = 1;
        data->system[0] = lsmSystemRecordAlloc(sys_id, "C example plug-in");

        data->num_pools = MAX_POOLS;
        data->pool[0] = lsmPoolRecordAlloc("POOL_0", "POOL_ZERO", UINT64_MAX,
                                            UINT64_MAX, sys_id);
        data->pool[1] = lsmPoolRecordAlloc("POOL_1", "POOL_ONE", UINT64_MAX,
                                            UINT64_MAX, sys_id);
        data->pool[2] = lsmPoolRecordAlloc("POOL_2", "POOL_TWO", UINT64_MAX,
                                            UINT64_MAX, sys_id);
        data->pool[3] = lsmPoolRecordAlloc("POOL_3", "lsm_test_aggr", UINT64_MAX,
                                            UINT64_MAX, sys_id);


        data->access_groups = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                    free, free_ag_value);

        /*  We will delete the key, but the value will get cleaned up in its
            own container */
        data->group_grant = g_hash_table_new_full(g_str_hash, g_str_equal,
                            free, free_group_grant_hash);

        /*TODO Check all pointers for memory allocation issues */

        rc = lsmRegisterPlugin( c, name, version, data, &mgmOps,
                                    &sanOps, NULL, NULL);
    }
    return rc;
}

int unload( lsmPluginPtr c )
{
    //TODO Free the data
    return LSM_ERR_OK;
}

int main(int argc, char *argv[] )
{
    return lsmPluginInit(argc, argv, load, unload);
}


#ifdef  __cplusplus
}
#endif