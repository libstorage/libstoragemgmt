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
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>

#include "db.h"
#include "fs_ops.h"
#include "san_ops.h"
#include "utils.h"

static lsm_system *sim_sys_to_lsm(char *err_msg, lsm_hash *sim_sys);
static lsm_pool *sim_p_to_lsm(char *err_msg, lsm_hash *sim_p);
static const char *time_stamp_str_get(char *buff);

_xxx_list_func_gen(pool_list, lsm_pool, sim_p_to_lsm,
                   lsm_plug_pool_search_filter, _DB_TABLE_POOLS_VIEW,
                   lsm_pool_record_array_free);

static lsm_system *sim_sys_to_lsm(char *err_msg, lsm_hash *sim_sys) {
    lsm_system *sys = NULL;
    uint32_t status = LSM_SYSTEM_STATUS_OK;
    const char *plugin_data = NULL;
    int read_cache_pct = LSM_SYSTEM_READ_CACHE_PCT_UNKNOWN;

    assert(sim_sys != NULL);

    if (_str_to_uint32(err_msg, lsm_hash_string_get(sim_sys, "status"),
                       &status) != LSM_ERR_OK)
        return NULL;
    if (_str_to_int(err_msg, lsm_hash_string_get(sim_sys, "read_cache_pct"),
                    &read_cache_pct) != LSM_ERR_OK)
        return NULL;

    sys = lsm_system_record_alloc(lsm_hash_string_get(sim_sys, "id"),
                                  lsm_hash_string_get(sim_sys, "name"), status,
                                  lsm_hash_string_get(sim_sys, "status_info"),
                                  plugin_data);

    if (sys != NULL) {
        lsm_system_fw_version_set(sys, lsm_hash_string_get(sim_sys, "version"));
        lsm_system_mode_set(sys, LSM_SYSTEM_MODE_HARDWARE_RAID);
        lsm_system_read_cache_pct_set(sys, read_cache_pct);
    }

    return sys;
}

static lsm_pool *sim_p_to_lsm(char *err_msg, lsm_hash *sim_p) {
    lsm_pool *p = NULL;
    uint64_t element_type = 0;
    uint64_t unsupported_actions = 0;
    uint64_t total_space = 0;
    uint64_t free_space = 0;
    uint64_t status = 0;
    const char *plugin_data = NULL;

    if ((_str_to_uint64(err_msg, lsm_hash_string_get(sim_p, "status"),
                        &status) != LSM_ERR_OK) ||
        (_str_to_uint64(err_msg, lsm_hash_string_get(sim_p, "element_type"),
                        &element_type) != LSM_ERR_OK) ||
        (_str_to_uint64(err_msg,
                        lsm_hash_string_get(sim_p, "unsupported_actions"),
                        &unsupported_actions) != LSM_ERR_OK) ||
        (_str_to_uint64(err_msg, lsm_hash_string_get(sim_p, "total_space"),
                        &total_space) != LSM_ERR_OK) ||
        (_str_to_uint64(err_msg, lsm_hash_string_get(sim_p, "free_space"),
                        &free_space) != LSM_ERR_OK))
        return NULL;

    p = lsm_pool_record_alloc(lsm_hash_string_get(sim_p, "lsm_pool_id"),
                              lsm_hash_string_get(sim_p, "name"), element_type,
                              unsupported_actions, total_space, free_space,
                              status, lsm_hash_string_get(sim_p, "status_info"),
                              _SYS_ID, plugin_data);
    return p;
}

static const char *time_stamp_str_get(char *buff) {
    struct timespec ts;

    assert(buff != NULL);

    memset(buff, 0, _BUFF_SIZE);

    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
        snprintf(buff, _BUFF_SIZE, "%ld.%ld", (long)difftime(ts.tv_sec, 0),
                 ts.tv_nsec);

    return buff;
}

int tmo_set(lsm_plugin_ptr c, uint32_t timeout, lsm_flag flags) {
    int rc = LSM_ERR_NO_SUPPORT;
    char err_msg[_LSM_ERR_MSG_LEN];
    sqlite3 *db = NULL;
    int db_rc = SQLITE_OK;
    struct _simc_private_data *pri_data = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);

    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);

    if (timeout >= INT_MAX) {
        rc = LSM_ERR_INVALID_ARGUMENT;
        _lsm_err_msg_set(err_msg, "Timeout value should smaller than %d",
                         INT_MAX);
        goto out;
    }
    /* sqlite3 version prior 3.7.15 does not support timeout query,
     * we save the timeout value in lsm_plugin_ptr c user data
     */
    pri_data = lsm_private_data_get(c);
    if (pri_data == NULL) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg, "Got NULL plugin private data");
        goto out;
    }

    db_rc = sqlite3_busy_timeout(db, timeout & INT_MAX);
    if (db_rc != SQLITE_OK) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg,
                         "BUG: Failed to set timeout via "
                         "sqlite3_busy_timeout(), %d(%s)",
                         db_rc, sqlite3_errmsg(db));
        goto out;
    }

    pri_data->timeout = timeout;

out:

    if (rc != LSM_ERR_OK)
        lsm_log_error_basic(c, rc, err_msg);

    return rc;
}

int tmo_get(lsm_plugin_ptr c, uint32_t *timeout, lsm_flag flags) {
    int rc = LSM_ERR_NO_SUPPORT;
    char err_msg[_LSM_ERR_MSG_LEN];
    struct _simc_private_data *pri_data = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);

    _good(_check_null_ptr(err_msg, 1 /* argument count */, timeout), rc, out);

    pri_data = lsm_private_data_get(c);
    if (pri_data == NULL) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg, "Got NULL plugin private data");
        goto out;
    }
    *timeout = pri_data->timeout;

out:
    if (rc != LSM_ERR_OK) {
        *timeout = 0;
        lsm_log_error_basic(c, rc, err_msg);
    }

    return rc;
}

int capabilities(lsm_plugin_ptr c, lsm_system *sys,
                 lsm_storage_capabilities **cap, lsm_flag flags) {
    int rc = LSM_ERR_NO_MEMORY;
    char err_msg[_LSM_ERR_MSG_LEN];
    const char *sys_id = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);

    _good(_check_null_ptr(err_msg, 2 /* argument count */, sys, cap), rc, out);
    sys_id = lsm_system_id_get(sys);
    if ((sys_id == NULL) || (strcmp(sys_id, _SYS_ID) != 0)) {
        rc = LSM_ERR_NOT_FOUND_SYSTEM;
        _lsm_err_msg_set(err_msg, "System not found");
        goto out;
    }

    *cap = lsm_capability_record_alloc(NULL);
    _alloc_null_check(err_msg, *cap, rc, out);

    rc = lsm_capability_set_n(
        *cap, LSM_CAP_SUPPORTED, LSM_CAP_VOLUMES, LSM_CAP_VOLUME_CREATE,
        LSM_CAP_VOLUME_RESIZE, LSM_CAP_VOLUME_REPLICATE,
        LSM_CAP_VOLUME_REPLICATE_CLONE, LSM_CAP_VOLUME_REPLICATE_COPY,
        LSM_CAP_VOLUME_REPLICATE_MIRROR_ASYNC,
        LSM_CAP_VOLUME_REPLICATE_MIRROR_SYNC,
        LSM_CAP_VOLUME_COPY_RANGE_BLOCK_SIZE, LSM_CAP_VOLUME_COPY_RANGE,
        LSM_CAP_VOLUME_COPY_RANGE_CLONE, LSM_CAP_VOLUME_COPY_RANGE_COPY,
        LSM_CAP_VOLUME_DELETE, LSM_CAP_VOLUME_ENABLE, LSM_CAP_VOLUME_DISABLE,
        LSM_CAP_VOLUME_MASK, LSM_CAP_VOLUME_UNMASK, LSM_CAP_ACCESS_GROUPS,
        LSM_CAP_ACCESS_GROUP_CREATE_WWPN, LSM_CAP_ACCESS_GROUP_DELETE,
        LSM_CAP_ACCESS_GROUP_INITIATOR_ADD_WWPN,
        LSM_CAP_ACCESS_GROUP_INITIATOR_DELETE,
        LSM_CAP_VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP,
        LSM_CAP_ACCESS_GROUPS_GRANTED_TO_VOLUME,
        LSM_CAP_VOLUME_CHILD_DEPENDENCY, LSM_CAP_VOLUME_CHILD_DEPENDENCY_RM,
        LSM_CAP_ACCESS_GROUP_CREATE_ISCSI_IQN,
        LSM_CAP_ACCESS_GROUP_INITIATOR_ADD_ISCSI_IQN,
        LSM_CAP_VOLUME_ISCSI_CHAP_AUTHENTICATION, LSM_CAP_VOLUME_RAID_INFO,
        LSM_CAP_VOLUME_THIN, LSM_CAP_BATTERIES, LSM_CAP_VOLUME_CACHE_INFO,
        LSM_CAP_VOLUME_PHYSICAL_DISK_CACHE_UPDATE,
        LSM_CAP_VOLUME_WRITE_CACHE_POLICY_UPDATE_WRITE_BACK,
        LSM_CAP_VOLUME_WRITE_CACHE_POLICY_UPDATE_AUTO,
        LSM_CAP_VOLUME_WRITE_CACHE_POLICY_UPDATE_WRITE_THROUGH,
        LSM_CAP_VOLUME_READ_CACHE_POLICY_UPDATE, LSM_CAP_FS, LSM_CAP_FS_DELETE,
        LSM_CAP_FS_RESIZE, LSM_CAP_FS_CREATE, LSM_CAP_FS_CLONE,
        LSM_CAP_FILE_CLONE, LSM_CAP_FS_SNAPSHOTS, LSM_CAP_FS_SNAPSHOT_CREATE,
        LSM_CAP_FS_SNAPSHOT_DELETE, LSM_CAP_FS_SNAPSHOT_RESTORE,
        LSM_CAP_FS_SNAPSHOT_RESTORE_SPECIFIC_FILES, LSM_CAP_FS_CHILD_DEPENDENCY,
        LSM_CAP_FS_CHILD_DEPENDENCY_RM,
        LSM_CAP_FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES, LSM_CAP_EXPORT_AUTH,
        LSM_CAP_EXPORTS, LSM_CAP_EXPORT_FS, LSM_CAP_EXPORT_REMOVE,
        LSM_CAP_EXPORT_CUSTOM_PATH, LSM_CAP_SYS_READ_CACHE_PCT_UPDATE,
        LSM_CAP_SYS_READ_CACHE_PCT_GET, LSM_CAP_SYS_FW_VERSION_GET,
        LSM_CAP_SYS_MODE_GET, LSM_CAP_DISK_LOCATION, LSM_CAP_DISK_RPM,
        LSM_CAP_DISK_LINK_TYPE, LSM_CAP_VOLUME_LED, LSM_CAP_TARGET_PORTS,
        LSM_CAP_DISKS, LSM_CAP_POOL_MEMBER_INFO, LSM_CAP_VOLUME_RAID_CREATE,
        LSM_CAP_DISK_VPD83_GET, -1);

    if (LSM_ERR_OK != rc) {
        lsm_capability_record_free(*cap);
        _lsm_err_msg_set(err_msg, "lsm_capability_set_n() failed %d", rc);
        *cap = NULL;
    }

out:
    if (rc != LSM_ERR_OK) {
        if (cap != NULL)
            *cap = NULL;
        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

int job_status(lsm_plugin_ptr c, const char *job, lsm_job_status *status,
               uint8_t *percent_complete, lsm_data_type *type, void **value,
               lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_job = NULL;
    uint64_t sim_job_id = 0;
    uint64_t sim_data_id = 0;
    lsm_hash *sim_data = NULL;
    const char *time_stamp_str = NULL;
    char cur_time_stamp_str[_BUFF_SIZE];
    double job_start_time = 0;
    double cur_time = 0;
    uint64_t duration = 0;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);

    _good(_check_null_ptr(err_msg, 5 /* argument count */, job, status,
                          percent_complete, type, value),
          rc, out);

    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);

    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_job_id = _db_lsm_id_to_sim_id(job);
    if (sim_job_id == 0) {
        rc = LSM_ERR_NOT_FOUND_JOB;
        _lsm_err_msg_set(err_msg, "Job not found");
        goto out;
    }
    _good(_db_sim_job_of_sim_id(err_msg, db, sim_job_id, &sim_job), rc, out);

    time_stamp_str = lsm_hash_string_get(sim_job, "timestamp");
    if ((time_stamp_str == NULL) || (strlen(time_stamp_str) == 0)) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg,
                         "BUG: Got NULL or empty time stamp for job "
                         "%s",
                         job);
        goto out;
    }
    job_start_time = strtod(time_stamp_str, NULL);
    if (job_start_time == 0) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg,
                         "BUG: Failed to convert job creation "
                         "time stamp '%s'",
                         time_stamp_str);
        goto out;
    }

    time_stamp_str_get(cur_time_stamp_str);
    cur_time = strtod(cur_time_stamp_str, NULL);
    if (cur_time == 0) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg,
                         "BUG: Failed to convert current time stamp "
                         "'%s'",
                         cur_time_stamp_str);
        goto out;
    }

    _good(_str_to_uint64(err_msg, lsm_hash_string_get(sim_job, "duration"),
                         &duration),
          rc, out);

    if (duration == 0) {
        *percent_complete = 100;
        *status = LSM_JOB_COMPLETE;
    } else if (cur_time <= job_start_time) {
        *percent_complete = 0;
        *status = LSM_JOB_INPROGRESS;
    } else if ((cur_time - job_start_time) >= duration) {
        *percent_complete = 100;
        *status = LSM_JOB_COMPLETE;
    } else {
        *percent_complete = ((cur_time - job_start_time) / duration * 100);
        *status = LSM_JOB_INPROGRESS;
    }

    _good(_str_to_int(err_msg, lsm_hash_string_get(sim_job, "data_type"), type),
          rc, out);

    if (*status != LSM_JOB_COMPLETE) {
        *value = NULL;
        goto out;
    }

    _good(_str_to_uint64(err_msg, lsm_hash_string_get(sim_job, "data_id"),
                         &sim_data_id),
          rc, out);

    if (*type == LSM_DATA_TYPE_NONE) {
        *value = NULL;
    } else if (*type == LSM_DATA_TYPE_VOLUME) {
        _good(_db_sim_vol_of_sim_id(err_msg, db, sim_data_id, &sim_data), rc,
              out);
        *value = _sim_vol_to_lsm(err_msg, sim_data);
    } else if (*type == LSM_DATA_TYPE_FS) {
        _good(_db_sim_fs_of_sim_id(err_msg, db, sim_data_id, &sim_data), rc,
              out);
        *value = _sim_fs_to_lsm(err_msg, sim_data);
    } else if (*type == LSM_DATA_TYPE_SS) {
        _good(_db_sim_fs_snap_of_sim_id(err_msg, db, sim_data_id, &sim_data),
              rc, out);
        *value = _sim_fs_snap_to_lsm(err_msg, sim_data);
    } else {
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg, "job data type %d not supported yet", *type);
        goto out;
    }

out:
    _db_sql_trans_rollback(db);

    if (sim_job != NULL)
        lsm_hash_free(sim_job);

    if (sim_data != NULL)
        lsm_hash_free(sim_data);

    if (rc != LSM_ERR_OK) {
        *status = LSM_JOB_ERROR;
        *value = NULL;
        *percent_complete = 0;
        *type = LSM_DATA_TYPE_UNKNOWN;
        lsm_log_error_basic(c, rc, err_msg);
    }

    return rc;
}

int job_free(lsm_plugin_ptr c, char *job_id, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    uint64_t sim_job_id = 0;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_job = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);

    _good(_check_null_ptr(err_msg, 1 /* argument count */, job_id), rc, out);

    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);

    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_job_id = _db_lsm_id_to_sim_id(job_id);

    if (sim_job_id == 0) {
        rc = LSM_ERR_NOT_FOUND_JOB;
        _lsm_err_msg_set(err_msg, "Job not found");
        goto out;
    }

    _good(_db_sim_job_of_sim_id(err_msg, db, sim_job_id, &sim_job), rc, out);

    _good(_db_data_delete(err_msg, db, _DB_TABLE_JOBS, sim_job_id), rc, out);

    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (sim_job != NULL)
        lsm_hash_free(sim_job);

    if (rc != LSM_ERR_OK) {
        lsm_log_error_basic(c, rc, err_msg);
        _db_sql_trans_rollback(db);
    };

    return rc;
}

int system_list(lsm_plugin_ptr c, lsm_system **systems[],
                uint32_t *system_count, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    struct _vector *vec = NULL;
    uint32_t i = 0;
    lsm_hash *sim_sys = NULL;
    lsm_system *lsm_sys = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);

    _good(
        _check_null_ptr(err_msg, 2 /* argument count */, systems, system_count),
        rc, out);

    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);

    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    _good(_db_sql_exec(err_msg, db, "SELECT * from systems;", &vec), rc, out);

    if (_vector_size(vec) == 0) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg, "BUG: No system found");
        goto out;
    }

    *systems = lsm_system_record_array_alloc(_vector_size(vec));
    _alloc_null_check(err_msg, *systems, rc, out);
    *system_count = _vector_size(vec);
    for (; i < *system_count; ++i)
        (*systems)[i] = NULL;

    _vector_for_each(vec, i, sim_sys) {
        lsm_sys = sim_sys_to_lsm(err_msg, sim_sys);
        if (lsm_sys == NULL) {
            rc = LSM_ERR_PLUGIN_BUG;
            goto out;
        }
        (*systems)[i] = lsm_sys;
    }

out:
    _db_sql_trans_rollback(db);
    _db_sql_exec_vec_free(vec);

    if (rc != LSM_ERR_OK) {
        if ((systems != NULL) && (*systems != NULL)) {
            lsm_system_record_array_free(*systems, *system_count);
            *systems = NULL;
            *system_count = 0;
        }
        lsm_log_error_basic(c, rc, err_msg);
    }

    return rc;
}

int _job_create(char *err_msg, sqlite3 *db, lsm_data_type data_type,
                uint64_t sim_id, char **lsm_job_id) {
    int rc = LSM_ERR_OK;
    char *duration = NULL;
    char time_stamp_str[_BUFF_SIZE];
    char data_type_str[_BUFF_SIZE];
    char sim_id_str[_BUFF_SIZE];
    char job_id_str[_BUFF_SIZE];

    assert(db != NULL);
    assert(lsm_job_id != NULL);

    *lsm_job_id = NULL;

    duration = getenv("LSM_SIM_TIME");
    if (duration == NULL)
        duration = _DB_DEFAULT_JOB_DURATION;

    _snprintf_buff(err_msg, rc, out, data_type_str, "%d", data_type);
    _snprintf_buff(err_msg, rc, out, sim_id_str, "%" PRIu64, sim_id);

    _good(_db_data_add(err_msg, db, _DB_TABLE_JOBS, "duration", duration,
                       "timestamp", time_stamp_str_get(time_stamp_str),
                       "data_type", data_type_str, "data_id", sim_id_str, NULL),
          rc, out);

    _db_sim_id_to_lsm_id(job_id_str, "JOB_ID", _db_last_rowid(db));

    *lsm_job_id = strdup(job_id_str);
    _alloc_null_check(err_msg, *lsm_job_id, rc, out);

out:
    return rc;
}

bool _pool_has_enough_free_size(sqlite3 *db, uint64_t sim_pool_id,
                                uint64_t size) {
    bool rc = false;
    lsm_hash *sim_pool = NULL;
    uint64_t free_size = 0;

    assert(db != NULL);
    assert(sim_pool_id != 0);

    if (_db_sim_pool_of_sim_id(NULL /* ignore error message */, db, sim_pool_id,
                               &sim_pool) == LSM_ERR_OK) {
        if ((_str_to_uint64(NULL /* ignore error message */,
                            lsm_hash_string_get(sim_pool, "free_space"),
                            &free_size) == LSM_ERR_OK) &&
            (free_size >= size))
            rc = true;
        lsm_hash_free(sim_pool);
    }

    return rc;
}
