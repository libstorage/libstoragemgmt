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
#include <stdlib.h>

#include <libstoragemgmt/libstoragemgmt.h>
#include <libstoragemgmt/libstoragemgmt_plug_interface.h>

#include "db.h"
#include "mgm_ops.h"
#include "san_ops.h"
#include "utils.h"

#define _VOLUME_ADMIN_STATE_ENABLE_STR  "1"
#define _VOLUME_ADMIN_STATE_DISABLE_STR "0"

static lsm_disk *_sim_disk_to_lsm(char *err_msg, lsm_hash *sim_disk);
lsm_access_group *_sim_ag_to_lsm(char *err_msg, lsm_hash *sim_ag);
static lsm_target_port *_sim_tgt_to_lsm(char *err_msg, lsm_hash *sim_tgt);
static int _volume_admin_state_change(lsm_plugin_ptr c, lsm_volume *v,
                                      const char *admin_state_str);

_xxx_list_func_gen(volume_list, lsm_volume, _sim_vol_to_lsm,
                   lsm_plug_volume_search_filter, _DB_TABLE_VOLS_VIEW,
                   lsm_volume_record_array_free);

_xxx_list_func_gen(disk_list, lsm_disk, _sim_disk_to_lsm,
                   lsm_plug_disk_search_filter, _DB_TABLE_DISKS_VIEW,
                   lsm_disk_record_array_free);

_xxx_list_func_gen(access_group_list, lsm_access_group, _sim_ag_to_lsm,
                   lsm_plug_access_group_search_filter, _DB_TABLE_AGS_VIEW,
                   lsm_access_group_record_array_free);

_xxx_list_func_gen(target_port_list, lsm_target_port, _sim_tgt_to_lsm,
                   lsm_plug_target_port_search_filter, _DB_TABLE_TGTS_VIEW,
                   lsm_target_port_record_array_free);

lsm_volume *_sim_vol_to_lsm(char *err_msg, lsm_hash *sim_vol) {
    uint32_t admin_state = 0;
    const char *plugin_data = NULL;
    uint64_t total_space = 0;
    lsm_volume *lsm_vol = NULL;

    assert(sim_vol != NULL);

    if ((_str_to_uint32(err_msg, lsm_hash_string_get(sim_vol, "admin_state"),
                        &admin_state) != LSM_ERR_OK) ||
        (_str_to_uint64(err_msg, lsm_hash_string_get(sim_vol, "total_space"),
                        &total_space) != LSM_ERR_OK))
        return NULL;

    lsm_vol = lsm_volume_record_alloc(
        lsm_hash_string_get(sim_vol, "lsm_vol_id"),
        lsm_hash_string_get(sim_vol, "name"),
        lsm_hash_string_get(sim_vol, "vpd83"), _BLOCK_SIZE,
        total_space / _BLOCK_SIZE, admin_state, _SYS_ID,
        lsm_hash_string_get(sim_vol, "lsm_pool_id"), plugin_data);

    if (lsm_vol == NULL)
        _lsm_err_msg_set(err_msg, "No memory");

    return lsm_vol;
}

static lsm_disk *_sim_disk_to_lsm(char *err_msg, lsm_hash *sim_disk) {
    uint32_t disk_type_u32 = 0;
    uint64_t total_space = 0;
    uint64_t status = 0;
    lsm_disk *lsm_d = NULL;
    int32_t rpm = LSM_DISK_RPM_UNKNOWN;
    lsm_disk_link_type link_type = LSM_DISK_LINK_TYPE_UNKNOWN;

    if ((_str_to_uint32(err_msg, lsm_hash_string_get(sim_disk, "disk_type"),
                        &disk_type_u32) != LSM_ERR_OK) ||
        (_str_to_uint64(err_msg, lsm_hash_string_get(sim_disk, "status"),
                        &status) != LSM_ERR_OK) ||
        (_str_to_int(err_msg, lsm_hash_string_get(sim_disk, "rpm"),
                     (int *)&rpm) != LSM_ERR_OK) ||
        (_str_to_int(err_msg, lsm_hash_string_get(sim_disk, "link_type"),
                     (int *)&link_type) != LSM_ERR_OK) ||
        (_str_to_uint64(err_msg, lsm_hash_string_get(sim_disk, "total_space"),
                        &total_space) != LSM_ERR_OK))
        return NULL;

    if (strlen(lsm_hash_string_get(sim_disk, "role")) == 0)
        status |= LSM_DISK_STATUS_FREE;

    lsm_d = lsm_disk_record_alloc(lsm_hash_string_get(sim_disk, "lsm_disk_id"),
                                  lsm_hash_string_get(sim_disk, "name"),
                                  (lsm_disk_type)disk_type_u32, _BLOCK_SIZE,
                                  total_space / _BLOCK_SIZE, status, _SYS_ID);

    if (lsm_d == NULL)
        _lsm_err_msg_set(err_msg, "No memory");

    lsm_disk_rpm_set(lsm_d, rpm);
    lsm_disk_link_type_set(lsm_d, link_type);
    lsm_disk_vpd83_set(lsm_d, lsm_hash_string_get(sim_disk, "vpd83"));
    lsm_disk_location_set(lsm_d, lsm_hash_string_get(sim_disk, "location"));

    return lsm_d;
}

static lsm_target_port *_sim_tgt_to_lsm(char *err_msg, lsm_hash *sim_tgt) {
    lsm_target_port_type port_type = LSM_TARGET_PORT_TYPE_OTHER;
    const char *plugin_data = NULL;
    lsm_target_port *lsm_tgt = NULL;

    if (_str_to_int(err_msg, lsm_hash_string_get(sim_tgt, "port_type"),
                    (int *)&port_type) != LSM_ERR_OK)
        return NULL;

    lsm_tgt = lsm_target_port_record_alloc(
        lsm_hash_string_get(sim_tgt, "lsm_tgt_id"), port_type,
        lsm_hash_string_get(sim_tgt, "service_address"),
        lsm_hash_string_get(sim_tgt, "network_address"),
        lsm_hash_string_get(sim_tgt, "physical_address"),
        lsm_hash_string_get(sim_tgt, "physical_name"), _SYS_ID, plugin_data);

    if (lsm_tgt == NULL)
        _lsm_err_msg_set(err_msg, "No memory");

    return lsm_tgt;
}

lsm_access_group *_sim_ag_to_lsm(char *err_msg, lsm_hash *sim_ag) {
    const char *plugin_data = NULL;
    lsm_access_group_init_type init_type = 0;
    const char *init_ids_str = NULL;
    lsm_string_list *init_ids = NULL;
    lsm_access_group *lsm_ag = NULL;

    assert(sim_ag != NULL);

    init_ids_str = lsm_hash_string_get(sim_ag, "init_ids_str");
    if (init_ids_str == NULL) {
        _lsm_err_msg_set(err_msg, "BUG: No 'init_ids_str' in lsm_hash sim_ag");
        return NULL;
    }
    if (_str_to_int(err_msg, lsm_hash_string_get(sim_ag, "init_type"),
                    (int *)&init_type) != LSM_ERR_OK)
        return NULL;
    init_ids = _db_str_to_list(init_ids_str);
    if (init_ids == NULL) {
        _lsm_err_msg_set(err_msg, "BUG: Failed to convert init_ids "
                                  "str to list");
        return NULL;
    }
    lsm_ag = lsm_access_group_record_alloc(
        lsm_hash_string_get(sim_ag, "lsm_ag_id"),
        lsm_hash_string_get(sim_ag, "name"), init_ids, init_type, _SYS_ID,
        plugin_data);
    lsm_string_list_free(init_ids);
    if (lsm_ag == NULL) {
        _lsm_err_msg_set(err_msg, "No memory");
    }

    return lsm_ag;
}

int _volume_create_internal(char *err_msg, sqlite3 *db, const char *name,
                            uint64_t size, uint64_t sim_pool_id) {
    int rc = LSM_ERR_OK;
    char vpd_buff[_VPD_83_LEN];
    char size_str[_BUFF_SIZE];
    char admin_state_str[_BUFF_SIZE];
    lsm_hash *sim_pool = NULL;
    char sim_pool_id_str[_BUFF_SIZE];
    uint64_t element_type = 0;

    assert(db != NULL);
    assert(name != NULL);

    size = _db_blk_size_rounding(size);
    if (_pool_has_enough_free_size(db, sim_pool_id, size) == false) {
        rc = LSM_ERR_NOT_ENOUGH_SPACE;
        _lsm_err_msg_set(err_msg, "Insufficient space in pool");
        goto out;
    }
    _snprintf_buff(err_msg, rc, out, size_str, "%" PRIu64, size);
    _snprintf_buff(err_msg, rc, out, admin_state_str, "%d",
                   LSM_VOLUME_ADMIN_STATE_ENABLED);
    _snprintf_buff(err_msg, rc, out, sim_pool_id_str, "%" PRIu64, sim_pool_id);
    _good(_db_sim_pool_of_sim_id(err_msg, db, sim_pool_id, &sim_pool), rc, out);
    /* Check whether pool support creating volume. */
    _good(_str_to_uint64(err_msg, lsm_hash_string_get(sim_pool, "element_type"),
                         &element_type),
          rc, out);
    if (!(element_type & LSM_POOL_ELEMENT_TYPE_VOLUME)) {
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg, "Specified pool does not support volume "
                                  "creation");
        goto out;
    }

    rc =
        _db_data_add(err_msg, db, _DB_TABLE_VOLS, "vpd83",
                     _random_vpd(vpd_buff), "name", name, "pool_id",
                     sim_pool_id_str, "total_space", size_str, "consumed_size",
                     size_str, "admin_state", admin_state_str, "is_hw_raid_vol",
                     "0", "write_cache_policy", _DB_DEFAULT_WRITE_CACHE_POLICY,
                     "read_cache_policy", _DB_DEFAULT_READ_CACHE_POLICY,
                     "phy_disk_cache", _DB_DEFAULT_PHYSICAL_DISK_CACHE, NULL);
    if (rc != LSM_ERR_OK) {
        if (sqlite3_errcode(db) == SQLITE_CONSTRAINT) {
            rc = LSM_ERR_NAME_CONFLICT;
            _lsm_err_msg_set(err_msg, "Volume name '%s' in use", name);
        }
        goto out;
    }

out:
    if (sim_pool != NULL)
        lsm_hash_free(sim_pool);

    return rc;
}

int volume_create(lsm_plugin_ptr c, lsm_pool *pool, const char *volume_name,
                  uint64_t size, lsm_volume_provision_type provisioning,
                  lsm_volume **new_volume, char **job, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];

    _UNUSED(flags);
    _UNUSED(provisioning);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 4 /* argument count */, pool, volume_name,
                          new_volume, job),
          rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);
    _good(_volume_create_internal(err_msg, db, volume_name, size,
                                  _db_lsm_id_to_sim_id(lsm_pool_id_get(pool))),
          rc, out);
    _good(
        _job_create(err_msg, db, LSM_DATA_TYPE_VOLUME, _db_last_rowid(db), job),
        rc, out);
    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (new_volume != NULL)
        *new_volume = NULL;
    if (rc != LSM_ERR_OK) {
        _db_sql_trans_rollback(db);
        if (job != NULL)
            *job = NULL;
        lsm_log_error_basic(c, rc, err_msg);
    }
    if (rc == LSM_ERR_OK)
        rc = LSM_ERR_JOB_STARTED;
    return rc;
}

int volume_delete(lsm_plugin_ptr c, lsm_volume *volume, char **job,
                  lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_vol = NULL;
    uint64_t sim_vol_id = 0;
    char sql_cmd[_BUFF_SIZE];
    struct _vector *vec = NULL;
    struct _vector *vec_disks = NULL;
    lsm_hash *sim_disk = NULL;
    uint64_t sim_disk_id = 0;
    uint32_t i = 0;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 2 /* argument count */, volume, job), rc,
          out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);

    _good(_db_sql_trans_begin(err_msg, db), rc, out);
    sim_vol_id = _db_lsm_id_to_sim_id(lsm_volume_id_get(volume));
    /* Check volume existence */
    _good(_db_sim_vol_of_sim_id(err_msg, db, sim_vol_id, &sim_vol), rc, out);
    /* Check volume mask status */
    _snprintf_buff(err_msg, rc, out, sql_cmd,
                   "SELECT * FROM " _DB_TABLE_VOL_MASKS " WHERE vol_id=%" PRIu64
                   ";",
                   sim_vol_id);

    _good(_db_sql_exec(err_msg, db, sql_cmd, &vec), rc, out);
    if (_vector_size(vec) != 0) {
        rc = LSM_ERR_IS_MASKED;
        _lsm_err_msg_set(err_msg, "Specified volume is masked to access group");
        goto out;
    }
    _db_sql_exec_vec_free(vec);
    vec = NULL;
    /* Check volume duplication status */
    _snprintf_buff(err_msg, rc, out, sql_cmd,
                   "SELECT * FROM " _DB_TABLE_VOL_REPS
                   " WHERE src_vol_id = %" PRIu64 " AND dst_vol_id != %" PRIu64
                   ";",
                   sim_vol_id, sim_vol_id);

    _good(_db_sql_exec(err_msg, db, sql_cmd, &vec), rc, out);
    if (_vector_size(vec) != 0) {
        rc = LSM_ERR_HAS_CHILD_DEPENDENCY;
        _lsm_err_msg_set(err_msg, "Specified volume has child dependency");
        goto out;
    }

    if (strcmp(lsm_hash_string_get(sim_vol, "is_hw_raid_vol"), "1") == 0) {
        /* Reset disks' role */
        _snprintf_buff(err_msg, rc, out, sql_cmd,
                       "SELECT * FROM " _DB_TABLE_DISKS_VIEW
                       " WHERE owner_pool_id=%" PRIu64 ";",
                       _db_lsm_id_to_sim_id(lsm_volume_pool_id_get(volume)));
        _good(_db_sql_exec(err_msg, db, sql_cmd, &vec_disks), rc, out);
        _vector_for_each(vec_disks, i, sim_disk) {
            sim_disk_id = _db_lsm_id_to_sim_id(
                lsm_hash_string_get(sim_disk, "lsm_disk_id"));
            _good(_db_data_update(err_msg, db, _DB_TABLE_DISKS, sim_disk_id,
                                  "role", NULL),
                  rc, out);
        }

        _good(_db_data_delete(
                  err_msg, db, _DB_TABLE_POOLS,
                  _db_lsm_id_to_sim_id(lsm_volume_pool_id_get(volume))),
              rc, out);
    } else {
        _good(_db_data_delete(err_msg, db, _DB_TABLE_VOLS, sim_vol_id), rc,
              out);
    }

    _good(_job_create(err_msg, db, LSM_DATA_TYPE_NONE, _DB_SIM_ID_NONE, job),
          rc, out);
    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    _db_sql_exec_vec_free(vec);
    _db_sql_exec_vec_free(vec_disks);
    if (sim_vol != NULL)
        lsm_hash_free(sim_vol);

    if (rc != LSM_ERR_OK) {
        _db_sql_trans_rollback(db);
        lsm_log_error_basic(c, rc, err_msg);
        if (job != NULL)
            *job = NULL;
    } else {
        rc = LSM_ERR_JOB_STARTED;
    }
    return rc;
}

int volume_replicate(lsm_plugin_ptr c, lsm_pool *pool,
                     lsm_replication_type rep_type, lsm_volume *volume_src,
                     const char *name, lsm_volume **new_replicant, char **job,
                     lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    uint64_t sim_pool_id = 0;
    char rep_type_str[_BUFF_SIZE];
    uint64_t new_sim_vol_id = 0;
    char new_sim_vol_id_str[_BUFF_SIZE];

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 4 /* argument count */, volume_src, name,
                          new_replicant, job),
          rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);
    if (pool != NULL)
        sim_pool_id = _db_lsm_id_to_sim_id(lsm_pool_id_get(pool));
    else
        sim_pool_id = _db_lsm_id_to_sim_id(lsm_volume_pool_id_get(volume_src));

    _good(
        _volume_create_internal(err_msg, db, name,
                                lsm_volume_block_size_get(volume_src) *
                                    lsm_volume_number_of_blocks_get(volume_src),
                                sim_pool_id),
        rc, out);
    new_sim_vol_id = _db_last_rowid(db);
    _snprintf_buff(err_msg, rc, out, rep_type_str, "%d", rep_type);
    _snprintf_buff(err_msg, rc, out, new_sim_vol_id_str, "%" PRIu64,
                   new_sim_vol_id);
    _good(_db_data_add(err_msg, db, _DB_TABLE_VOL_REPS, "src_vol_id",
                       _db_lsm_id_to_sim_id_str(lsm_volume_id_get(volume_src)),
                       "dst_vol_id", new_sim_vol_id_str, "rep_type",
                       rep_type_str, NULL),
          rc, out);
    _good(_job_create(err_msg, db, LSM_DATA_TYPE_VOLUME, new_sim_vol_id, job),
          rc, out);
    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (rc != LSM_ERR_OK) {
        _db_sql_trans_rollback(db);
        if (job != NULL)
            *job = NULL;
        lsm_log_error_basic(c, rc, err_msg);
    } else {
        rc = LSM_ERR_JOB_STARTED;
    }
    return rc;
}

int volume_replicate_range(lsm_plugin_ptr c, lsm_replication_type rep_type,
                           lsm_volume *src_vol, lsm_volume *dst_vol,
                           lsm_block_range **ranges, uint32_t num_ranges,
                           char **job, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    uint64_t src_sim_vol_id = 0;
    uint64_t dst_sim_vol_id = 0;
    const char *src_sim_vol_id_str = NULL;
    const char *dst_sim_vol_id_str = NULL;
    lsm_hash *src_sim_vol = NULL;
    lsm_hash *dst_sim_vol = NULL;
    struct _vector *vec = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    char rep_type_str[_BUFF_SIZE];
    char sql_cmd[_BUFF_SIZE];

    _UNUSED(flags);
    _UNUSED(num_ranges);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 4 /* argument count */, src_vol, dst_vol,
                          ranges, job),
          rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    src_sim_vol_id = _db_lsm_id_to_sim_id(lsm_volume_id_get(src_vol));
    src_sim_vol_id_str = _db_lsm_id_to_sim_id_str(lsm_volume_id_get(src_vol));
    dst_sim_vol_id = _db_lsm_id_to_sim_id(lsm_volume_id_get(dst_vol));
    dst_sim_vol_id_str = _db_lsm_id_to_sim_id_str(lsm_volume_id_get(dst_vol));

    _good(_db_sim_vol_of_sim_id(err_msg, db, src_sim_vol_id, &src_sim_vol), rc,
          out);
    _good(_db_sim_vol_of_sim_id(err_msg, db, dst_sim_vol_id, &dst_sim_vol), rc,
          out);

    /* Make sure specified destination volume is not a replicate destination of
     * other volume.
     */
    _snprintf_buff(err_msg, rc, out, sql_cmd,
                   "SELECT * FROM " _DB_TABLE_VOL_REPS
                   " WHERE dst_vol_id=%s AND src_vol_id !=%s",
                   dst_sim_vol_id_str, src_sim_vol_id_str);
    _good(_db_sql_exec(err_msg, db, sql_cmd, &vec), rc, out);
    if (_vector_size(vec) > 0) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg,
                         "Destination volume is already a "
                         "replication destination for other source volume");
        goto out;
    }

    _snprintf_buff(err_msg, rc, out, rep_type_str, "%d", rep_type);
    _good(_db_data_add(err_msg, db, _DB_TABLE_VOL_REPS, "src_vol_id",
                       src_sim_vol_id_str, "dst_vol_id", dst_sim_vol_id_str,
                       "rep_type", rep_type_str, NULL),
          rc, out);
    _good(_job_create(err_msg, db, LSM_DATA_TYPE_NONE, _DB_SIM_ID_NONE, job),
          rc, out);
    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (vec != NULL)
        _db_sql_exec_vec_free(vec);
    if (src_sim_vol != NULL)
        lsm_hash_free(src_sim_vol);
    if (dst_sim_vol != NULL)
        lsm_hash_free(dst_sim_vol);
    if (rc != LSM_ERR_OK) {
        _db_sql_trans_rollback(db);
        if (job != NULL)
            *job = NULL;
        lsm_log_error_basic(c, rc, err_msg);
    } else {
        rc = LSM_ERR_JOB_STARTED;
    }
    return rc;
}

int volume_replicate_range_block_size(lsm_plugin_ptr c, lsm_system *system,
                                      uint32_t *bs, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    char err_msg[_LSM_ERR_MSG_LEN];
    const char *sys_id = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 2 /* argument count */, system, bs), rc,
          out);

    sys_id = lsm_system_id_get(system);
    if ((sys_id == NULL) || (strcmp(sys_id, _SYS_ID) != 0)) {
        rc = LSM_ERR_NOT_FOUND_SYSTEM;
        _lsm_err_msg_set(err_msg, "System not found");
        goto out;
    }

    *bs = _BLOCK_SIZE;

out:
    if (rc != LSM_ERR_OK) {
        if (bs != NULL)
            *bs = 0;
        lsm_log_error_basic(c, rc, err_msg);
    }

    return rc;
}

int volume_resize(lsm_plugin_ptr c, lsm_volume *volume, uint64_t new_size,
                  lsm_volume **resized_volume, char **job, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    char err_msg[_LSM_ERR_MSG_LEN];
    uint64_t sim_vol_id = 0;
    lsm_hash *sim_vol = NULL;
    uint64_t increment_size = 0;
    uint64_t cur_size = 0;
    uint64_t sim_pool_id = 0;
    char new_size_str[_BUFF_SIZE];
    sqlite3 *db = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 3 /* argument count */, volume,
                          resized_volume, job),
          rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_vol_id = _db_lsm_id_to_sim_id(lsm_volume_id_get(volume));
    _good(_db_sim_vol_of_sim_id(err_msg, db, sim_vol_id, &sim_vol), rc, out);
    _good(_str_to_uint64(err_msg, lsm_hash_string_get(sim_vol, "total_space"),
                         &cur_size),
          rc, out);
    new_size = _db_blk_size_rounding(new_size);
    if (cur_size == new_size) {
        rc = LSM_ERR_NO_STATE_CHANGE;
        _lsm_err_msg_set(err_msg, "Specified new size is identical to "
                                  "current volume size");
        goto out;
    }
    if (new_size > cur_size) {
        increment_size = new_size - cur_size;
        sim_pool_id = _db_lsm_id_to_sim_id(lsm_volume_pool_id_get(volume));

        if (_pool_has_enough_free_size(db, sim_pool_id, increment_size) ==
            false) {
            rc = LSM_ERR_NOT_ENOUGH_SPACE;
            _lsm_err_msg_set(err_msg, "Insufficient space in pool");
            goto out;
        }
    }
    /*
     * TODO(Gris Ge): If a volume is in a replication relationship, resize
     *                should be handled properly.
     */
    _snprintf_buff(err_msg, rc, out, new_size_str, "%" PRIu64, new_size);
    _good(_db_data_update(err_msg, db, _DB_TABLE_VOLS, sim_vol_id,
                          "total_space", new_size_str),
          rc, out);
    _good(_db_data_update(err_msg, db, _DB_TABLE_VOLS, sim_vol_id,
                          "consumed_size", new_size_str),
          rc, out);

    _good(_job_create(err_msg, db, LSM_DATA_TYPE_VOLUME, sim_vol_id, job), rc,
          out);
    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (resized_volume != NULL)
        *resized_volume = NULL;
    if (sim_vol != NULL)
        lsm_hash_free(sim_vol);

    if (rc != LSM_ERR_OK) {
        _db_sql_trans_rollback(db);
        if (job != NULL)
            *job = NULL;
        lsm_log_error_basic(c, rc, err_msg);
    } else {
        rc = LSM_ERR_JOB_STARTED;
    }
    return rc;
}

static int _volume_admin_state_change(lsm_plugin_ptr c, lsm_volume *v,
                                      const char *admin_state_str) {
    int rc = LSM_ERR_OK;
    char err_msg[_LSM_ERR_MSG_LEN];
    uint64_t sim_vol_id = 0;
    lsm_hash *sim_vol = NULL;
    sqlite3 *db = NULL;

    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 1 /* argument count */, v), rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_vol_id = _db_lsm_id_to_sim_id(lsm_volume_id_get(v));
    _good(_db_sim_vol_of_sim_id(err_msg, db, sim_vol_id, &sim_vol), rc, out);
    _good(_db_data_update(err_msg, db, _DB_TABLE_VOLS, sim_vol_id,
                          "admin_state", admin_state_str),
          rc, out);

    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (sim_vol != NULL)
        lsm_hash_free(sim_vol);

    if (rc != LSM_ERR_OK) {
        _db_sql_trans_rollback(db);
        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

int volume_enable(lsm_plugin_ptr c, lsm_volume *v, lsm_flag flags) {
    _UNUSED(flags);
    return _volume_admin_state_change(c, v, _VOLUME_ADMIN_STATE_ENABLE_STR);
}

int volume_disable(lsm_plugin_ptr c, lsm_volume *v, lsm_flag flags) {
    _UNUSED(flags);
    return _volume_admin_state_change(c, v, _VOLUME_ADMIN_STATE_DISABLE_STR);
}

int iscsi_chap_auth(lsm_plugin_ptr c, const char *init_id, const char *in_user,
                    const char *in_password, const char *out_user,
                    const char *out_password, lsm_flag flags) {
    /* Currently, there is no API method to query status of iscsi CHAP.
     * Hence we do nothing but check for INVALID_ARGUMENT
     */
    char err_msg[_LSM_ERR_MSG_LEN];

    _UNUSED(flags);
    _UNUSED(in_user);
    _UNUSED(in_password);
    _UNUSED(out_user);
    _UNUSED(out_password);
    _UNUSED(c);
    _lsm_err_msg_clear(err_msg);
    return _check_null_ptr(err_msg, 1 /* argument count */, init_id);
}

int access_group_create(lsm_plugin_ptr c, const char *name,
                        const char *initiator_id,
                        lsm_access_group_init_type init_type,
                        lsm_system *system, lsm_access_group **access_group,
                        lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    uint64_t sim_ag_id = 0;
    char sim_ag_id_str[_BUFF_SIZE];
    char init_type_str[_BUFF_SIZE];
    lsm_hash *sim_ag = NULL;
    const char *sys_id = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 4 /* argument count */, name, initiator_id,
                          system, access_group),
          rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sys_id = lsm_system_id_get(system);
    if ((sys_id == NULL) || (strcmp(sys_id, _SYS_ID) != 0)) {
        rc = LSM_ERR_NOT_FOUND_SYSTEM;
        _lsm_err_msg_set(err_msg, "System not found");
        goto out;
    }
    if (strlen(initiator_id) == 0) {
        rc = LSM_ERR_INVALID_ARGUMENT;
        _lsm_err_msg_set(err_msg, "Invalid argument: empty initiator id");
        goto out;
    }
    if (strlen(name) == 0) {
        rc = LSM_ERR_INVALID_ARGUMENT;
        _lsm_err_msg_set(err_msg, "Invalid argument: empty access group name");
        goto out;
    }
    rc = _db_data_add(err_msg, db, "ags", "name", name, NULL);
    if (rc != LSM_ERR_OK) {
        if (sqlite3_errcode(db) == SQLITE_CONSTRAINT) {
            rc = LSM_ERR_NAME_CONFLICT;
            _lsm_err_msg_set(err_msg, "Access group name '%s' in use", name);
        }
        goto out;
    }
    sim_ag_id = _db_last_rowid(db);
    _snprintf_buff(err_msg, rc, out, sim_ag_id_str, "%" PRIu64, sim_ag_id);
    _snprintf_buff(err_msg, rc, out, init_type_str, "%d", (int)init_type);
    rc = _db_data_add(err_msg, db, _DB_TABLE_INITS, "id", initiator_id,
                      "init_type", init_type_str, "owner_ag_id", sim_ag_id_str,
                      NULL);
    if (rc != LSM_ERR_OK) {
        if (sqlite3_errcode(db) == SQLITE_CONSTRAINT) {
            rc = LSM_ERR_EXISTS_INITIATOR;
            _lsm_err_msg_set(err_msg,
                             "Initiator '%s' is used by "
                             "other access group ",
                             initiator_id);
        }
        goto out;
    }
    rc = _db_sim_ag_of_sim_id(err_msg, db, sim_ag_id, &sim_ag);
    if (rc == LSM_ERR_NOT_FOUND_ACCESS_GROUP) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg, "Failed to find newly created access group");
        goto out;
    }
    if (rc != LSM_ERR_OK)
        goto out;
    *access_group = _sim_ag_to_lsm(err_msg, sim_ag);
    if (*access_group == NULL) {
        rc = LSM_ERR_PLUGIN_BUG;
        goto out;
    }

    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (sim_ag != NULL)
        lsm_hash_free(sim_ag);

    if (rc != LSM_ERR_OK) {
        _db_sql_trans_rollback(db);
        if (access_group != NULL)
            *access_group = NULL;
        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

int access_group_delete(lsm_plugin_ptr c, lsm_access_group *group,
                        lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_ag = NULL;
    uint64_t sim_ag_id = 0;
    char sql_cmd[_BUFF_SIZE];
    struct _vector *vec = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 1 /* argument count */, group), rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);
    sim_ag_id = _db_lsm_id_to_sim_id(lsm_access_group_id_get(group));
    /* Check access group existence */
    _good(_db_sim_ag_of_sim_id(err_msg, db, sim_ag_id, &sim_ag), rc, out);
    /* Check volume masking status */
    _snprintf_buff(err_msg, rc, out, sql_cmd,
                   "SELECT * FROM " _DB_TABLE_VOL_MASKS
                   " WHERE ag_id = %" PRIu64 ";",
                   sim_ag_id);

    _good(_db_sql_exec(err_msg, db, sql_cmd, &vec), rc, out);
    if (_vector_size(vec) != 0) {
        rc = LSM_ERR_IS_MASKED;
        _lsm_err_msg_set(err_msg, "Specified access group has masked volume");
        goto out;
    }

    _good(_db_data_delete(err_msg, db, _DB_TABLE_AGS, sim_ag_id), rc, out);
    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    _db_sql_exec_vec_free(vec);
    if (sim_ag != NULL)
        lsm_hash_free(sim_ag);

    if (rc != LSM_ERR_OK) {
        _db_sql_trans_rollback(db);
        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

int access_group_initiator_add(lsm_plugin_ptr c, lsm_access_group *access_group,
                               const char *initiator_id,
                               lsm_access_group_init_type init_type,
                               lsm_access_group **updated_access_group,
                               lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    uint64_t sim_ag_id = 0;
    lsm_hash *sim_ag = NULL;
    struct _vector *vec = NULL;
    char init_type_str[_BUFF_SIZE];
    char sql_cmd_check_exist[_BUFF_SIZE];
    lsm_hash *sim_init = NULL;
    const char *sim_ag_id_str = NULL;
    const char *tmp_sim_ag_id_str = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 3 /* argument count */, access_group,
                          initiator_id, updated_access_group),
          rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    if (strlen(initiator_id) == 0) {
        rc = LSM_ERR_INVALID_ARGUMENT;
        _lsm_err_msg_set(err_msg, "Invalid argument: empty initiator_id");
        goto out;
    }

    sim_ag_id = _db_lsm_id_to_sim_id(lsm_access_group_id_get(access_group));
    sim_ag_id_str =
        _db_lsm_id_to_sim_id_str(lsm_access_group_id_get(access_group));
    _good(_db_sim_ag_of_sim_id(err_msg, db, sim_ag_id, &sim_ag), rc, out);
    _snprintf_buff(err_msg, rc, out, sql_cmd_check_exist,
                   "SELECT * FROM " _DB_TABLE_INITS " WHERE id=\"%s\"",
                   initiator_id);
    _good(_db_sql_exec(err_msg, db, sql_cmd_check_exist, &vec), rc, out);
    if (_vector_size(vec) == 1) {
        /* Since ID is defined as UNIQUE, we only get 1 item at most */
        sim_init = _vector_get(vec, 0);
        tmp_sim_ag_id_str = lsm_hash_string_get(sim_init, "owner_ag_id");
        if (tmp_sim_ag_id_str == NULL) {
            rc = LSM_ERR_PLUGIN_BUG;
            _lsm_err_msg_set(err_msg,
                             "BUG: Got NULL owner_ag_id for init id "
                             "%s",
                             initiator_id);
            goto out;
        }
        if (strcmp(tmp_sim_ag_id_str, sim_ag_id_str) == 0) {
            rc = LSM_ERR_NO_STATE_CHANGE;
            _lsm_err_msg_set(err_msg, "Specified initiator is already in "
                                      "specified access group");
            goto out;
        }
        rc = LSM_ERR_EXISTS_INITIATOR;
        _lsm_err_msg_set(err_msg, "Specified initiator is used by other "
                                  "access group");
        goto out;
    }
    _snprintf_buff(err_msg, rc, out, init_type_str, "%d", init_type);
    /* Already checked LSM_ERR_EXISTS_INITIATOR, we will not get
     * SQLITE_CONSTRAINT error here
     */
    _good(_db_data_add(err_msg, db, _DB_TABLE_INITS, "id", initiator_id,
                       "init_type", init_type_str, "owner_ag_id", sim_ag_id_str,
                       NULL),
          rc, out);
    lsm_hash_free(sim_ag);
    rc = _db_sim_ag_of_sim_id(err_msg, db, sim_ag_id, &sim_ag);
    if (rc == LSM_ERR_NOT_FOUND_ACCESS_GROUP) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg, "BUG: Failed to find updated access group");
        goto out;
    }
    if (rc != LSM_ERR_OK)
        goto out;
    *updated_access_group = _sim_ag_to_lsm(err_msg, sim_ag);
    if (*updated_access_group == NULL) {
        rc = LSM_ERR_PLUGIN_BUG;
        goto out;
    }

    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (sim_ag != NULL)
        lsm_hash_free(sim_ag);

    _db_sql_exec_vec_free(vec);

    if (rc != LSM_ERR_OK) {
        _db_sql_trans_rollback(db);
        if (updated_access_group != NULL)
            *updated_access_group = NULL;
        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

int access_group_initiator_delete(lsm_plugin_ptr c,
                                  lsm_access_group *access_group,
                                  const char *initiator_id,
                                  lsm_access_group_init_type id_type,
                                  lsm_access_group **updated_access_group,
                                  lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    char sql_cmd_check[_BUFF_SIZE];
    char condition[_BUFF_SIZE];
    uint64_t sim_ag_id = 0;
    const char *sim_ag_id_str = NULL;
    lsm_hash *sim_ag = NULL;
    struct _vector *vec = NULL;

    _UNUSED(flags);
    _UNUSED(id_type);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 3 /* argument count */, access_group,
                          initiator_id, updated_access_group),
          rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    if (strlen(initiator_id) == 0) {
        rc = LSM_ERR_INVALID_ARGUMENT;
        _lsm_err_msg_set(err_msg, "Invalid argument: empty initiator_id");
        goto out;
    }

    sim_ag_id = _db_lsm_id_to_sim_id(lsm_access_group_id_get(access_group));
    sim_ag_id_str =
        _db_lsm_id_to_sim_id_str(lsm_access_group_id_get(access_group));
    _good(_db_sim_ag_of_sim_id(err_msg, db, sim_ag_id, &sim_ag), rc, out);

    _snprintf_buff(err_msg, rc, out, sql_cmd_check,
                   "SELECT * FROM " _DB_TABLE_INITS
                   " WHERE id=\"%s\" AND owner_ag_id=\"%s\"",
                   initiator_id, sim_ag_id_str);
    _good(_db_sql_exec(err_msg, db, sql_cmd_check, &vec), rc, out);
    if (_vector_size(vec) == 0) {
        rc = LSM_ERR_NO_STATE_CHANGE;
        _lsm_err_msg_set(err_msg, "Specified initiator is not in "
                                  "specified access group");
        goto out;
    }
    _db_sql_exec_vec_free(vec);
    vec = NULL;
    _snprintf_buff(err_msg, rc, out, sql_cmd_check,
                   "SELECT * FROM " _DB_TABLE_INITS " WHERE owner_ag_id=\"%s\"",
                   sim_ag_id_str);
    _good(_db_sql_exec(err_msg, db, sql_cmd_check, &vec), rc, out);
    if (_vector_size(vec) == 1) {
        rc = LSM_ERR_LAST_INIT_IN_ACCESS_GROUP;
        _lsm_err_msg_set(err_msg, "Refused to remove the last initiator from "
                                  "access group");
        goto out;
    }

    _snprintf_buff(err_msg, rc, out, condition, "id=\"%s\"", initiator_id);
    _good(_db_data_delete_condition(err_msg, db, _DB_TABLE_INITS, condition),
          rc, out);

    lsm_hash_free(sim_ag);
    rc = _db_sim_ag_of_sim_id(err_msg, db, sim_ag_id, &sim_ag);
    if (rc == LSM_ERR_NOT_FOUND_ACCESS_GROUP) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg, "BUG: Failed to find updated access group");
        goto out;
    }
    if (rc != LSM_ERR_OK)
        goto out;
    *updated_access_group = _sim_ag_to_lsm(err_msg, sim_ag);
    if (*updated_access_group == NULL) {
        rc = LSM_ERR_PLUGIN_BUG;
        goto out;
    }
    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (sim_ag != NULL)
        lsm_hash_free(sim_ag);

    _db_sql_exec_vec_free(vec);

    if (rc != LSM_ERR_OK) {
        _db_sql_trans_rollback(db);
        if (updated_access_group != NULL)
            *updated_access_group = NULL;
        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

int volume_mask(lsm_plugin_ptr c, lsm_access_group *group, lsm_volume *volume,
                lsm_flag flags) {
    int rc = LSM_ERR_OK;
    lsm_hash *sim_vol = NULL;
    lsm_hash *sim_ag = NULL;
    uint64_t sim_vol_id = 0;
    uint64_t sim_ag_id = 0;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    char sql_cmd_check_mask[_BUFF_SIZE];
    struct _vector *vec = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);

    _good(_check_null_ptr(err_msg, 2 /* argument count */, group, volume), rc,
          out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_vol_id = _db_lsm_id_to_sim_id(lsm_volume_id_get(volume));
    sim_ag_id = _db_lsm_id_to_sim_id(lsm_access_group_id_get(group));

    _good(_db_sim_vol_of_sim_id(err_msg, db, sim_vol_id, &sim_vol), rc, out);
    _good(_db_sim_ag_of_sim_id(err_msg, db, sim_ag_id, &sim_ag), rc, out);

    _snprintf_buff(err_msg, rc, out, sql_cmd_check_mask,
                   "SELECT * FROM " _DB_TABLE_VOL_MASKS
                   " WHERE ag_id=%s AND vol_id=%s;",
                   _db_lsm_id_to_sim_id_str(lsm_access_group_id_get(group)),
                   _db_lsm_id_to_sim_id_str(lsm_volume_id_get(volume)));

    _good(_db_sql_exec(err_msg, db, sql_cmd_check_mask, &vec), rc, out);

    if (_vector_size(vec) != 0) {
        rc = LSM_ERR_NO_STATE_CHANGE;
        _lsm_err_msg_set(err_msg, "Volume is already masked to specified "
                                  "access group");
        goto out;
    }

    _good(_db_data_add(
              err_msg, db, _DB_TABLE_VOL_MASKS, "vol_id",
              _db_lsm_id_to_sim_id_str(lsm_volume_id_get(volume)), "ag_id",
              _db_lsm_id_to_sim_id_str(lsm_access_group_id_get(group)), NULL),
          rc, out);

    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (sim_ag != NULL)
        lsm_hash_free(sim_ag);
    if (sim_vol != NULL)
        lsm_hash_free(sim_vol);
    if (vec != NULL)
        _db_sql_exec_vec_free(vec);

    if (rc != LSM_ERR_OK) {
        _db_sql_trans_rollback(db);
        lsm_log_error_basic(c, rc, err_msg);
    }

    return rc;
}

int volume_unmask(lsm_plugin_ptr c, lsm_access_group *group, lsm_volume *volume,
                  lsm_flag flags) {
    int rc = LSM_ERR_OK;
    lsm_hash *sim_vol = NULL;
    lsm_hash *sim_ag = NULL;
    uint64_t sim_vol_id = 0;
    uint64_t sim_ag_id = 0;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    char condition[_BUFF_SIZE];
    struct _vector *vec = NULL;
    char sql_cmd_check_mask[_BUFF_SIZE * 4];

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);

    _good(_check_null_ptr(err_msg, 2 /* argument count */, group, volume), rc,
          out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_vol_id = _db_lsm_id_to_sim_id(lsm_volume_id_get(volume));
    sim_ag_id = _db_lsm_id_to_sim_id(lsm_access_group_id_get(group));

    _good(_db_sim_vol_of_sim_id(err_msg, db, sim_vol_id, &sim_vol), rc, out);
    _good(_db_sim_ag_of_sim_id(err_msg, db, sim_ag_id, &sim_ag), rc, out);

    _snprintf_buff(err_msg, rc, out, condition, "ag_id=%s AND vol_id=%s",
                   _db_lsm_id_to_sim_id_str(lsm_access_group_id_get(group)),
                   _db_lsm_id_to_sim_id_str(lsm_volume_id_get(volume)));

    _snprintf_buff(err_msg, rc, out, sql_cmd_check_mask,
                   "SELECT * FROM " _DB_TABLE_VOL_MASKS " WHERE %s;",
                   condition);

    _good(_db_sql_exec(err_msg, db, sql_cmd_check_mask, &vec), rc, out);

    if (_vector_size(vec) == 0) {
        rc = LSM_ERR_NO_STATE_CHANGE;
        _lsm_err_msg_set(err_msg, "Volume is not masked to specified "
                                  "access group");
        goto out;
    }

    _good(
        _db_data_delete_condition(err_msg, db, _DB_TABLE_VOL_MASKS, condition),
        rc, out);
    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (sim_ag != NULL)
        lsm_hash_free(sim_ag);
    if (sim_vol != NULL)
        lsm_hash_free(sim_vol);

    if (vec != NULL)
        _db_sql_exec_vec_free(vec);

    if (rc != LSM_ERR_OK) {
        _db_sql_trans_rollback(db);
        lsm_log_error_basic(c, rc, err_msg);
    }

    return rc;
}

int volumes_accessible_by_access_group(lsm_plugin_ptr c,
                                       lsm_access_group *group,
                                       lsm_volume **volumes[], uint32_t *count,
                                       lsm_flag flags) {
    int rc = LSM_ERR_OK;
    lsm_hash *sim_ag = NULL;
    uint64_t sim_ag_id = 0;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    struct _vector *vec = NULL;
    char sql_cmd[_BUFF_SIZE];

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);

    _good(
        _check_null_ptr(err_msg, 3 /* argument count */, group, volumes, count),
        rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_ag_id = _db_lsm_id_to_sim_id(lsm_access_group_id_get(group));

    _good(_db_sim_ag_of_sim_id(err_msg, db, sim_ag_id, &sim_ag), rc, out);

    _snprintf_buff(err_msg, rc, out, sql_cmd,
                   "SELECT * FROM " _DB_TABLE_VOLS_VIEW_BY_AG
                   " WHERE ag_id=%" PRIu64 ";",
                   sim_ag_id);

    _good(_db_sql_exec(err_msg, db, sql_cmd, &vec), rc, out);

    if (_vector_size(vec) == 0) {
        *count = 0;
        *volumes = NULL;
        goto out;
    }

    _vec_to_lsm_xxx_array(err_msg, vec, lsm_volume, _sim_vol_to_lsm, volumes,
                          count, rc, out);

    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    _db_sql_trans_rollback(db);
    if (sim_ag != NULL)
        lsm_hash_free(sim_ag);

    if (vec != NULL)
        _db_sql_exec_vec_free(vec);

    if (rc != LSM_ERR_OK) {
        if (volumes != NULL)
            *volumes = NULL;
        if (count != NULL)
            *count = 0;
        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

int access_groups_granted_to_volume(lsm_plugin_ptr c, lsm_volume *volume,
                                    lsm_access_group **groups[],
                                    uint32_t *count, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    lsm_hash *sim_vol = NULL;
    uint64_t sim_vol_id = 0;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    struct _vector *vec = NULL;
    char sql_cmd[_BUFF_SIZE];

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);

    _good(
        _check_null_ptr(err_msg, 3 /* argument count */, volume, groups, count),
        rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_vol_id = _db_lsm_id_to_sim_id(lsm_volume_id_get(volume));

    _good(_db_sim_vol_of_sim_id(err_msg, db, sim_vol_id, &sim_vol), rc, out);

    _snprintf_buff(err_msg, rc, out, sql_cmd,
                   "SELECT * FROM " _DB_TABLE_AGS_VIEW_BY_VOL
                   " WHERE vol_id=%" PRIu64 ";",
                   sim_vol_id);

    _good(_db_sql_exec(err_msg, db, sql_cmd, &vec), rc, out);

    if (_vector_size(vec) == 0) {
        *count = 0;
        *groups = NULL;
        goto out;
    }

    _vec_to_lsm_xxx_array(err_msg, vec, lsm_access_group, _sim_ag_to_lsm,
                          groups, count, rc, out);

    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    _db_sql_trans_rollback(db);
    if (sim_vol != NULL)
        lsm_hash_free(sim_vol);

    if (vec != NULL)
        _db_sql_exec_vec_free(vec);

    if (rc != LSM_ERR_OK) {
        if (groups != NULL)
            *groups = NULL;
        if (count != NULL)
            *count = 0;
        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

int vol_child_depends(lsm_plugin_ptr c, lsm_volume *volume, uint8_t *yes,
                      lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_vol = NULL;
    uint64_t sim_vol_id = 0;
    char sql_cmd[_BUFF_SIZE];
    struct _vector *vec = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);

    _good(_check_null_ptr(err_msg, 2 /* argument count */, volume, yes), rc,
          out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_vol_id = _db_lsm_id_to_sim_id(lsm_volume_id_get(volume));

    _good(_db_sim_vol_of_sim_id(err_msg, db, sim_vol_id, &sim_vol), rc, out);

    _snprintf_buff(err_msg, rc, out, sql_cmd,
                   "SELECT * FROM " _DB_TABLE_VOL_REPS
                   " WHERE src_vol_id = %" PRIu64 " AND "
                   "dst_vol_id != %" PRIu64 ";",
                   sim_vol_id, sim_vol_id);

    _good(_db_sql_exec(err_msg, db, sql_cmd, &vec), rc, out);

    if (_vector_size(vec) != 0)
        *yes = 1;
    else
        *yes = 0;

out:
    _db_sql_exec_vec_free(vec);
    if (sim_vol != NULL)
        lsm_hash_free(sim_vol);
    _db_sql_trans_rollback(db);
    if (rc != LSM_ERR_OK) {
        if (yes != NULL)
            *yes = 0;
        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

int vol_child_depends_rm(lsm_plugin_ptr c, lsm_volume *volume, char **job,
                         lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_vol = NULL;
    uint64_t sim_vol_id = 0;
    char sql_cmd[_BUFF_SIZE];
    struct _vector *vec = NULL;
    char condition[_BUFF_SIZE];

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);

    _good(_check_null_ptr(err_msg, 2 /* argument count */, volume, job), rc,
          out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);

    *job = NULL;

    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_vol_id = _db_lsm_id_to_sim_id(lsm_volume_id_get(volume));

    _good(_db_sim_vol_of_sim_id(err_msg, db, sim_vol_id, &sim_vol), rc, out);

    _snprintf_buff(err_msg, rc, out, sql_cmd,
                   "SELECT * FROM " _DB_TABLE_VOL_REPS
                   " WHERE src_vol_id = %" PRIu64 " AND "
                   "dst_vol_id != %" PRIu64 ";",
                   sim_vol_id, sim_vol_id);

    _good(_db_sql_exec(err_msg, db, sql_cmd, &vec), rc, out);

    if (_vector_size(vec) == 0) {
        rc = LSM_ERR_NO_STATE_CHANGE;
        _lsm_err_msg_set(err_msg, "Specified volume is not a replication "
                                  "source");
        goto out;
    }

    _snprintf_buff(err_msg, rc, out, condition, "src_vol_id=%" PRIu64,
                   sim_vol_id);
    _good(_db_data_delete_condition(err_msg, db, _DB_TABLE_VOL_REPS, condition),
          rc, out);

    _good(_job_create(err_msg, db, LSM_DATA_TYPE_NONE, _DB_SIM_ID_NONE, job),
          rc, out);

    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    _db_sql_exec_vec_free(vec);
    if (sim_vol != NULL)
        lsm_hash_free(sim_vol);

    if (rc != LSM_ERR_OK) {
        _db_sql_trans_rollback(db);
        if (job != NULL) {
            free(*job);
            *job = NULL;
        }
        lsm_log_error_basic(c, rc, err_msg);
    } else {
        rc = LSM_ERR_JOB_STARTED;
    }
    return rc;
}
