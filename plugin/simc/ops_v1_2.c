/*
 * Copyright (C) 2016 Red Hat, Inc.
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
#include <inttypes.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>

#include "db.h"
#include "ops_v1_2.h"
#include "san_ops.h"
#include "utils.h"

int volume_raid_info(lsm_plugin_ptr c, lsm_volume *volume,
                     lsm_volume_raid_type *raid_type, uint32_t *strip_size,
                     uint32_t *disk_count, uint32_t *min_io_size,
                     uint32_t *opt_io_size, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_vol = NULL;
    lsm_hash *sim_p = NULL;
    uint64_t sim_vol_id = 0;
    uint64_t sim_p_id = 0;
    lsm_pool_member_type member_type = LSM_POOL_MEMBER_TYPE_UNKNOWN;
    uint32_t data_disk_count = 0;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 6 /* argument count */, volume, raid_type,
                          strip_size, disk_count, min_io_size, opt_io_size),
          rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_vol_id = _db_lsm_id_to_sim_id(lsm_volume_id_get(volume));
    sim_p_id = _db_lsm_id_to_sim_id(lsm_volume_pool_id_get(volume));
    _good(_db_sim_vol_of_sim_id(err_msg, db, sim_vol_id, &sim_vol), rc, out);
    _good(_db_sim_pool_of_sim_id(err_msg, db, sim_p_id, &sim_p), rc, out);

    _good(_str_to_int(err_msg, lsm_hash_string_get(sim_p, "member_type"),
                      (int *)&member_type),
          rc, out);

    if (member_type == LSM_POOL_MEMBER_TYPE_POOL) {
        _good(_str_to_uint64(err_msg,
                             lsm_hash_string_get(sim_p, "parent_pool_id"),
                             &sim_p_id),
              rc, out);
        _good(_db_sim_pool_of_sim_id(err_msg, db, sim_p_id, &sim_p), rc, out);
    } else if (member_type != LSM_POOL_MEMBER_TYPE_DISK) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg, "BUG: Got unknown pool member type %d",
                         member_type);
        goto out;
    }

    _good(_str_to_int(err_msg, lsm_hash_string_get(sim_p, "raid_type"),
                      (int *)raid_type),
          rc, out);
    _good(_str_to_uint32(err_msg, lsm_hash_string_get(sim_p, "strip_size"),
                         strip_size),
          rc, out);
    *min_io_size = *strip_size;
    _good(_str_to_uint32(err_msg, lsm_hash_string_get(sim_p, "disk_count"),
                         disk_count),
          rc, out);
    _good(_str_to_uint32(err_msg, lsm_hash_string_get(sim_p, "data_disk_count"),
                         &data_disk_count),
          rc, out);
    if ((*raid_type == LSM_VOLUME_RAID_TYPE_RAID1) ||
        (*raid_type == LSM_VOLUME_RAID_TYPE_JBOD))
        *opt_io_size = _BLOCK_SIZE;
    else
        *opt_io_size = *strip_size * data_disk_count;

out:
    _db_sql_trans_rollback(db);
    if (sim_vol != NULL)
        lsm_hash_free(sim_vol);
    if (sim_p != NULL)
        lsm_hash_free(sim_p);
    if (rc != LSM_ERR_OK) {
        if (raid_type != NULL)
            *raid_type = LSM_VOLUME_RAID_TYPE_UNKNOWN;
        if (strip_size != NULL)
            *strip_size = LSM_VOLUME_STRIP_SIZE_UNKNOWN;
        if (disk_count != NULL)
            *disk_count = LSM_VOLUME_DISK_COUNT_UNKNOWN;
        if (min_io_size != NULL)
            *min_io_size = LSM_VOLUME_MIN_IO_SIZE_UNKNOWN;
        if (opt_io_size != NULL)
            *opt_io_size = LSM_VOLUME_OPT_IO_SIZE_UNKNOWN;

        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

int pool_member_info(lsm_plugin_ptr c, lsm_pool *pool,
                     lsm_volume_raid_type *raid_type,
                     lsm_pool_member_type *member_type,
                     lsm_string_list **member_ids, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_p = NULL;
    uint64_t sim_p_id = 0;
    char sql_cmd[_BUFF_SIZE];
    struct _vector *vec = NULL;
    lsm_hash *sim_disk = NULL;
    uint32_t i = 0;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 4 /* argument count */, pool, raid_type,
                          member_type, member_ids),
          rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_p_id = _db_lsm_id_to_sim_id(lsm_pool_id_get(pool));
    _good(_db_sim_pool_of_sim_id(err_msg, db, sim_p_id, &sim_p), rc, out);

    _good(_str_to_int(err_msg, lsm_hash_string_get(sim_p, "member_type"),
                      (int *)member_type),
          rc, out);
    _good(_str_to_int(err_msg, lsm_hash_string_get(sim_p, "raid_type"),
                      (int *)raid_type),
          rc, out);

    switch (*member_type) {
    case LSM_POOL_MEMBER_TYPE_POOL:
        *member_ids = lsm_string_list_alloc(1);
        _alloc_null_check(err_msg, *member_ids, rc, out);
        rc = lsm_string_list_elem_set(
            *member_ids, 0, lsm_hash_string_get(sim_p, "parent_lsm_pool_id"));
        if (rc != LSM_ERR_OK) {
            _lsm_err_msg_set(err_msg,
                             "lsm_string_list_elem_set() failed with "
                             "%d",
                             rc);
            goto out;
        }

        break;
    case LSM_POOL_MEMBER_TYPE_DISK:
        _snprintf_buff(err_msg, rc, out, sql_cmd,
                       "SELECT lsm_disk_id FROM " _DB_TABLE_DISKS_VIEW
                       " WHERE owner_pool_id = %" PRIu64 ";",
                       sim_p_id);
        _good(_db_sql_exec(err_msg, db, sql_cmd, &vec), rc, out);
        *member_ids = lsm_string_list_alloc(_vector_size(vec));
        _alloc_null_check(err_msg, *member_ids, rc, out);
        _vector_for_each(vec, i, sim_disk) {
            rc = lsm_string_list_elem_set(
                *member_ids, i, lsm_hash_string_get(sim_disk, "lsm_disk_id"));
            if (rc != LSM_ERR_OK) {
                _lsm_err_msg_set(err_msg,
                                 "lsm_string_list_elem_set() "
                                 "failed with %d",
                                 rc);
                goto out;
            }
        }
        break;
    default:
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg, "BUG: Got unknown pool member type %d",
                         *member_type);
        goto out;
    }
out:
    _db_sql_trans_rollback(db);
    _db_sql_exec_vec_free(vec);
    if (sim_p != NULL)
        lsm_hash_free(sim_p);
    if (rc != LSM_ERR_OK) {
        if (raid_type != NULL)
            *raid_type = LSM_VOLUME_RAID_TYPE_UNKNOWN;
        if (member_type != NULL)
            *member_type = LSM_POOL_MEMBER_TYPE_UNKNOWN;
        if (member_ids != NULL)
            *member_ids = NULL;
        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

int volume_raid_create_cap_get(lsm_plugin_ptr c, lsm_system *system,
                               uint32_t **supported_raid_types,
                               uint32_t *supported_raid_type_count,
                               uint32_t **supported_strip_sizes,
                               uint32_t *supported_strip_size_count,
                               lsm_flag flags) {
    int rc = LSM_ERR_OK;
    char err_msg[_LSM_ERR_MSG_LEN];
    const char *sys_id = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 5 /* argument count */, system,
                          supported_raid_types, supported_raid_type_count,
                          supported_strip_sizes, supported_strip_size_count),
          rc, out);

    sys_id = lsm_system_id_get(system);
    if ((sys_id == NULL) || (strcmp(sys_id, _SYS_ID) != 0)) {
        rc = LSM_ERR_NOT_FOUND_SYSTEM;
        _lsm_err_msg_set(err_msg, "System not found");
    }
    _good(_db_volume_raid_create_cap_get(
              err_msg, supported_raid_types, supported_raid_type_count,
              supported_strip_sizes, supported_strip_size_count),
          rc, out);

out:
    if (rc != LSM_ERR_OK)
        lsm_log_error_basic(c, rc, err_msg);
    return rc;
}

int volume_raid_create(lsm_plugin_ptr c, const char *name,
                       lsm_volume_raid_type raid_type, lsm_disk *disks[],
                       uint32_t disk_count, uint32_t strip_size,
                       lsm_volume **new_volume, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    uint32_t i = 0;
    uint64_t sim_disk_id = 0;
    uint64_t sim_pool_id = 0;
    uint64_t sim_vol_id = 0;
    lsm_hash *sim_disk = NULL;
    lsm_hash *sim_vol = NULL;
    lsm_hash *sim_pool = NULL;
    uint64_t *sim_disk_ids = NULL;
    uint64_t all_size = 0;
    char pool_name[_BUFF_SIZE];

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 3 /* argument count */, name, disks,
                          new_volume),
          rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    if (disk_count == 0) {
        rc = LSM_ERR_INVALID_ARGUMENT;
        _lsm_err_msg_set(err_msg, "Got no disks to create pool");
        goto out;
    }

    sim_disk_ids = (uint64_t *)malloc(sizeof(uint64_t) * disk_count);
    _alloc_null_check(err_msg, sim_disk_ids, rc, out);

    for (i = 0; i < disk_count; ++i) {
        sim_disk_id = _db_lsm_id_to_sim_id(lsm_disk_id_get(disks[i]));
        _good(_db_sim_disk_of_sim_id(err_msg, db, sim_disk_id, &sim_disk), rc,
              out);

        if (strlen(lsm_hash_string_get(sim_disk, "role")) != 0) {
            lsm_hash_free(sim_disk);
            rc = LSM_ERR_DISK_NOT_FREE;
            _lsm_err_msg_set(err_msg, "Disk %s is used by other pool",
                             lsm_disk_id_get(disks[i]));
            goto out;
        }

        lsm_hash_free(sim_disk);
        sim_disk = NULL;
        sim_disk_ids[i] = sim_disk_id;
    }
    _snprintf_buff(err_msg, rc, out, pool_name, "RAID Pool for volume %s",
                   name);

    _good(_db_pool_create_from_disk(
              err_msg, db, pool_name, sim_disk_ids, disk_count, raid_type,
              LSM_POOL_ELEMENT_TYPE_VOLUME, 0 /* No unsupported_actions */,
              &sim_pool_id, strip_size),
          rc, out);
    if (_db_sim_pool_of_sim_id(err_msg, db, sim_pool_id, &sim_pool) !=
        LSM_ERR_OK) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg, "BUG: Failed to find newly created pool");
        goto out;
    }

    _good(_str_to_uint64(err_msg, lsm_hash_string_get(sim_pool, "free_space"),
                         &all_size),
          rc, out);

    _good(_volume_create_internal(err_msg, db, name, all_size, sim_pool_id), rc,
          out);
    sim_vol_id = _db_last_rowid(db);
    _good(_db_data_update(err_msg, db, _DB_TABLE_VOLS, sim_vol_id,
                          "is_hw_raid_vol", "1"),
          rc, out);

    _good(_db_sim_vol_of_sim_id(err_msg, db, sim_vol_id, &sim_vol), rc, out);

    *new_volume = _sim_vol_to_lsm(err_msg, sim_vol);
    if (*new_volume == NULL) {
        rc = LSM_ERR_NO_MEMORY;
        goto out;
    }

    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (sim_disk != NULL)
        lsm_hash_free(sim_disk);
    if (sim_pool != NULL)
        lsm_hash_free(sim_pool);
    if (sim_vol != NULL)
        lsm_hash_free(sim_vol);
    free(sim_disk_ids);
    if (rc != LSM_ERR_OK) {
        if (new_volume != NULL)
            *new_volume = NULL;
        _db_sql_trans_rollback(db);
        if (new_volume != NULL)
            *new_volume = NULL;

        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}
