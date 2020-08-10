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
#include <time.h>

#include <libstoragemgmt/libstoragemgmt.h>
#include <libstoragemgmt/libstoragemgmt_plug_interface.h>

#include "db.h"
#include "fs_ops.h"
#include "mgm_ops.h"
#include "utils.h"

static int _fs_create_internal(char *err_msg, sqlite3 *db, const char *name,
                               uint64_t size, uint64_t sim_pool_id);

_xxx_list_func_gen(fs_list, lsm_fs, _sim_fs_to_lsm, lsm_plug_fs_search_filter,
                   _DB_TABLE_FSS_VIEW, lsm_fs_record_array_free);

lsm_fs *_sim_fs_to_lsm(char *err_msg, lsm_hash *sim_fs) {
    const char *plugin_data = NULL;
    uint64_t total_space = 0;
    uint64_t free_space = 0;
    lsm_fs *lsm_fs_obj = NULL;

    assert(sim_fs != NULL);

    if ((_str_to_uint64(err_msg, lsm_hash_string_get(sim_fs, "free_space"),
                        &free_space) != LSM_ERR_OK) ||
        (_str_to_uint64(err_msg, lsm_hash_string_get(sim_fs, "total_space"),
                        &total_space) != LSM_ERR_OK))
        return NULL;

    lsm_fs_obj = lsm_fs_record_alloc(
        lsm_hash_string_get(sim_fs, "lsm_fs_id"),
        lsm_hash_string_get(sim_fs, "name"), total_space, free_space,
        lsm_hash_string_get(sim_fs, "lsm_pool_id"), _SYS_ID, plugin_data);

    if (lsm_fs_obj == NULL)
        _lsm_err_msg_set(err_msg, "No memory");

    return lsm_fs_obj;
}

lsm_fs_ss *_sim_fs_snap_to_lsm(char *err_msg, lsm_hash *sim_fs_snap) {
    const char *plugin_data = NULL;
    uint64_t timestamp = 0;
    lsm_fs_ss *lsm_fs_snap = NULL;

    assert(sim_fs_snap != NULL);

    if (_str_to_uint64(err_msg, lsm_hash_string_get(sim_fs_snap, "timestamp"),
                       &timestamp) != LSM_ERR_OK)
        return NULL;

    lsm_fs_snap = lsm_fs_ss_record_alloc(
        lsm_hash_string_get(sim_fs_snap, "lsm_fs_snap_id"),
        lsm_hash_string_get(sim_fs_snap, "name"), timestamp, plugin_data);

    if (lsm_fs_snap == NULL)
        _lsm_err_msg_set(err_msg, "No memory");

    return lsm_fs_snap;
}

static int _fs_create_internal(char *err_msg, sqlite3 *db, const char *name,
                               uint64_t size, uint64_t sim_pool_id) {
    int rc = LSM_ERR_OK;
    char size_str[_BUFF_SIZE];
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
    _snprintf_buff(err_msg, rc, out, sim_pool_id_str, "%" PRIu64, sim_pool_id);
    _good(_db_sim_pool_of_sim_id(err_msg, db, sim_pool_id, &sim_pool), rc, out);
    /* Check whether pool support creating fs. */
    _good(_str_to_uint64(err_msg, lsm_hash_string_get(sim_pool, "element_type"),
                         &element_type),
          rc, out);
    if (!(element_type & LSM_POOL_ELEMENT_TYPE_FS)) {
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg, "Specified pool does not support fs "
                                  "creation");
        goto out;
    }
    rc = _db_data_add(err_msg, db, _DB_TABLE_FSS, "name", name, "total_space",
                      size_str, "consumed_size", size_str, "free_space",
                      size_str, "pool_id", sim_pool_id_str, NULL);

    if (rc != LSM_ERR_OK) {
        if (sqlite3_errcode(db) == SQLITE_CONSTRAINT) {
            rc = LSM_ERR_NAME_CONFLICT;
            _lsm_err_msg_set(err_msg, "FS name '%s' in use", name);
        }
        goto out;
    }

out:
    if (sim_pool != NULL)
        lsm_hash_free(sim_pool);

    return rc;
}

int fs_create(lsm_plugin_ptr c, lsm_pool *pool, const char *name,
              uint64_t size_bytes, lsm_fs **fs, char **job, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 4 /* argument count */, pool, name, fs, job),
          rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);
    _good(_fs_create_internal(err_msg, db, name, size_bytes,
                              _db_lsm_id_to_sim_id(lsm_pool_id_get(pool))),
          rc, out);
    _good(_job_create(err_msg, db, LSM_DATA_TYPE_FS, _db_last_rowid(db), job),
          rc, out);
    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (fs != NULL)
        *fs = NULL;
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

int fs_delete(lsm_plugin_ptr c, lsm_fs *fs, char **job, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_fs = NULL;
    uint64_t sim_fs_id = 0;
    char sql_cmd[_BUFF_SIZE];
    struct _vector *vec = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 2 /* argument count */, fs, job), rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);

    _good(_db_sql_trans_begin(err_msg, db), rc, out);
    sim_fs_id = _db_lsm_id_to_sim_id(lsm_fs_id_get(fs));
    /* Check fs existence */
    _good(_db_sim_fs_of_sim_id(err_msg, db, sim_fs_id, &sim_fs), rc, out);
    /* Check fs clone(clone here means read and writeable snapshot) */
    _snprintf_buff(err_msg, rc, out, sql_cmd,
                   "SELECT * FROM " _DB_TABLE_FS_CLONES
                   " WHERE src_fs_id = %" PRIu64 ";",
                   sim_fs_id);

    _good(_db_sql_exec(err_msg, db, sql_cmd, &vec), rc, out);
    if (_vector_size(vec) != 0) {
        rc = LSM_ERR_HAS_CHILD_DEPENDENCY;
        _lsm_err_msg_set(err_msg, "Specified fs has child dependency");
        goto out;
    }

    _good(_db_data_delete(err_msg, db, _DB_TABLE_FSS, sim_fs_id), rc, out);
    _good(_job_create(err_msg, db, LSM_DATA_TYPE_NONE, _DB_SIM_ID_NONE, job),
          rc, out);
    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    _db_sql_exec_vec_free(vec);
    if (sim_fs != NULL)
        lsm_hash_free(sim_fs);

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

int fs_clone(lsm_plugin_ptr c, lsm_fs *src_fs, const char *dest_fs_name,
             lsm_fs **cloned_fs, lsm_fs_ss *optional_snapshot, char **job,
             lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_fs = NULL;
    lsm_hash *sim_fs_snap = NULL;
    uint64_t sim_fs_id = 0;
    uint64_t dst_sim_fs_id = 0;
    char dst_sim_fs_id_str[_BUFF_SIZE];
    uint64_t sim_fs_snap_id = 0;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 4 /* argument count */, src_fs, dest_fs_name,
                          cloned_fs, job),
          rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);

    _good(_db_sql_trans_begin(err_msg, db), rc, out);
    sim_fs_id = _db_lsm_id_to_sim_id(lsm_fs_id_get(src_fs));
    /* Check fs existence */
    _good(_db_sim_fs_of_sim_id(err_msg, db, sim_fs_id, &sim_fs), rc, out);

    if (optional_snapshot != NULL) {
        sim_fs_snap_id =
            _db_lsm_id_to_sim_id(lsm_fs_ss_id_get(optional_snapshot));
        /* No need to trace state of snap id here due to lack of query method.
         * We just check snapshot existence
         */
        _good(_db_sim_fs_snap_of_sim_id(err_msg, db, sim_fs_snap_id,
                                        &sim_fs_snap),
              rc, out);
        lsm_hash_free(sim_fs_snap);
    }

    _good(_fs_create_internal(err_msg, db, dest_fs_name,
                              lsm_fs_total_space_get(src_fs),
                              _db_lsm_id_to_sim_id(lsm_fs_pool_id_get(src_fs))),
          rc, out);

    dst_sim_fs_id = _db_last_rowid(db);

    _snprintf_buff(err_msg, rc, out, dst_sim_fs_id_str, "%" PRIu64,
                   dst_sim_fs_id);

    _good(_db_data_add(err_msg, db, _DB_TABLE_FS_CLONES, "src_fs_id",
                       _db_lsm_id_to_sim_id_str(lsm_fs_id_get(src_fs)),
                       "dst_fs_id", dst_sim_fs_id_str, NULL),
          rc, out);

    _good(_job_create(err_msg, db, LSM_DATA_TYPE_FS, dst_sim_fs_id, job), rc,
          out);
    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (sim_fs != NULL)
        lsm_hash_free(sim_fs);
    if (cloned_fs != NULL)
        *cloned_fs = NULL;
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

int fs_child_dependency(lsm_plugin_ptr c, lsm_fs *fs, lsm_string_list *files,
                        uint8_t *yes) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_fs = NULL;
    uint64_t sim_fs_id = 0;
    char sql_cmd[_BUFF_SIZE];
    struct _vector *vec = NULL;

    _UNUSED(files);
    _lsm_err_msg_clear(err_msg);

    _good(_check_null_ptr(err_msg, 2 /* argument count */, fs, yes), rc, out);

    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);

    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_fs_id = _db_lsm_id_to_sim_id(lsm_fs_id_get(fs));

    _good(_db_sim_fs_of_sim_id(err_msg, db, sim_fs_id, &sim_fs), rc, out);

    /* Check fs snapshot status */
    _snprintf_buff(err_msg, rc, out, sql_cmd,
                   "SELECT * FROM " _DB_TABLE_FS_SNAPS_VIEW
                   " WHERE fs_id=%" PRIu64 ";",
                   sim_fs_id);

    _good(_db_sql_exec(err_msg, db, sql_cmd, &vec), rc, out);
    if (_vector_size(vec) != 0) {
        *yes = 1;
        goto out;
    }
    _db_sql_exec_vec_free(vec);
    vec = NULL;
    /* Check fs clone(clone here means read and writeable snapshot) */
    _snprintf_buff(err_msg, rc, out, sql_cmd,
                   "SELECT * FROM " _DB_TABLE_FS_CLONES
                   " WHERE src_fs_id = %" PRIu64 ";",
                   sim_fs_id);

    _good(_db_sql_exec(err_msg, db, sql_cmd, &vec), rc, out);
    if (_vector_size(vec) != 0)
        *yes = 1;

out:
    _db_sql_exec_vec_free(vec);
    if (sim_fs != NULL)
        lsm_hash_free(sim_fs);
    _db_sql_trans_rollback(db);
    if (rc != LSM_ERR_OK) {
        if (yes != NULL)
            *yes = 0;
        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

int fs_child_dependency_rm(lsm_plugin_ptr c, lsm_fs *fs, lsm_string_list *files,
                           char **job, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    uint8_t yes = 0;
    char err_msg[_LSM_ERR_MSG_LEN];
    uint64_t sim_fs_id = 0;
    char condition[_BUFF_SIZE];
    sqlite3 *db = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);

    _good(_check_null_ptr(err_msg, 2 /* argument count */, fs, job), rc, out);

    _good(fs_child_dependency(c, fs, files, &yes), rc, out);
    if (yes == 0) {
        rc = LSM_ERR_NO_STATE_CHANGE;
        _lsm_err_msg_set(err_msg, "Specified file system does not have child "
                                  "dependency");
        goto out;
    }

    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    /*
     * Assuming API definition is break all clone relationship and remove
     * all snapshot of this source file system.
     */

    /* Previous fs_child_dependency() call already checked the fs existence */
    sim_fs_id = _db_lsm_id_to_sim_id(lsm_fs_id_get(fs));

    _snprintf_buff(err_msg, rc, out, condition, "src_fs_id = %" PRIu64,
                   sim_fs_id);
    _good(
        _db_data_delete_condition(err_msg, db, _DB_TABLE_FS_CLONES, condition),
        rc, out);

    _snprintf_buff(err_msg, rc, out, condition, "fs_id = %" PRIu64, sim_fs_id);
    _good(_db_data_delete_condition(err_msg, db, _DB_TABLE_FS_SNAPS, condition),
          rc, out);

    _good(_job_create(err_msg, db, LSM_DATA_TYPE_NONE, _DB_SIM_ID_NONE, job),
          rc, out);
    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    _db_sql_trans_rollback(db);
    if (rc != LSM_ERR_OK) {
        lsm_log_error_basic(c, rc, err_msg);
    } else {
        rc = LSM_ERR_JOB_STARTED;
    }
    return rc;
}

int fs_resize(lsm_plugin_ptr c, lsm_fs *fs, uint64_t new_size, lsm_fs **rfs,
              char **job, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    char err_msg[_LSM_ERR_MSG_LEN];
    uint64_t sim_fs_id = 0;
    lsm_hash *sim_fs = NULL;
    uint64_t increment_size = 0;
    uint64_t cur_size = 0;
    uint64_t sim_pool_id = 0;
    char new_size_str[_BUFF_SIZE];
    sqlite3 *db = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 3 /* argument count */, fs, rfs, job), rc,
          out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_fs_id = _db_lsm_id_to_sim_id(lsm_fs_id_get(fs));
    _good(_db_sim_fs_of_sim_id(err_msg, db, sim_fs_id, &sim_fs), rc, out);
    _good(_str_to_uint64(err_msg, lsm_hash_string_get(sim_fs, "total_space"),
                         &cur_size),
          rc, out);
    new_size = _db_blk_size_rounding(new_size);
    if (cur_size == new_size) {
        rc = LSM_ERR_NO_STATE_CHANGE;
        _lsm_err_msg_set(err_msg, "Specified new size is identical to "
                                  "current fs size");
        goto out;
    }
    if (new_size > cur_size) {
        increment_size = new_size - cur_size;
        sim_pool_id = _db_lsm_id_to_sim_id(lsm_fs_pool_id_get(fs));

        if (_pool_has_enough_free_size(db, sim_pool_id, increment_size) ==
            false) {
            rc = LSM_ERR_NOT_ENOUGH_SPACE;
            _lsm_err_msg_set(err_msg, "Insufficient space in pool");
            goto out;
        }
    }
    _snprintf_buff(err_msg, rc, out, new_size_str, "%" PRIu64, new_size);
    _good(_db_data_update(err_msg, db, _DB_TABLE_FSS, sim_fs_id, "total_space",
                          new_size_str),
          rc, out);
    _good(_db_data_update(err_msg, db, _DB_TABLE_FSS, sim_fs_id,
                          "consumed_size", new_size_str),
          rc, out);
    _good(_db_data_update(err_msg, db, _DB_TABLE_FSS, sim_fs_id, "free_space",
                          new_size_str),
          rc, out);

    _good(_job_create(err_msg, db, LSM_DATA_TYPE_FS, sim_fs_id, job), rc, out);
    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (rfs != NULL)
        *rfs = NULL;
    if (sim_fs != NULL)
        lsm_hash_free(sim_fs);

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

int fs_file_clone(lsm_plugin_ptr c, lsm_fs *fs, const char *src_file_name,
                  const char *dest_file_name, lsm_fs_ss *snapshot, char **job,
                  lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_fs = NULL;
    lsm_hash *sim_fs_snap = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 4 /* argument count */, fs, src_file_name,
                          dest_file_name, job),
          rc, out);

    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);
    /* Check fs existence */
    _good(_db_sim_fs_of_sim_id(
              err_msg, db, _db_lsm_id_to_sim_id(lsm_fs_id_get(fs)), &sim_fs),
          rc, out);
    if (snapshot != NULL)
        _good(_db_sim_fs_snap_of_sim_id(
                  err_msg, db, _db_lsm_id_to_sim_id(lsm_fs_ss_id_get(snapshot)),
                  &sim_fs_snap),
              rc, out);
    /* We don't have API to query file level clone. So do nothing here */

    _good(_job_create(err_msg, db, LSM_DATA_TYPE_NONE, _DB_SIM_ID_NONE, job),
          rc, out);
    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (sim_fs != NULL)
        lsm_hash_free(sim_fs);
    if (sim_fs_snap != NULL)
        lsm_hash_free(sim_fs_snap);
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

int fs_snapshot_list(lsm_plugin_ptr c, lsm_fs *fs, lsm_fs_ss **ss[],
                     uint32_t *ss_count, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    struct _vector *vec = NULL;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    char sql_cmd[_BUFF_SIZE];
    lsm_hash *sim_fs = NULL;
    uint64_t sim_fs_id = 0;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 2 /* argument count */, ss, ss_count), rc,
          out);

    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);
    /* Check fs existence */
    sim_fs_id = _db_lsm_id_to_sim_id(lsm_fs_id_get(fs));
    _good(_db_sim_fs_of_sim_id(err_msg, db, sim_fs_id, &sim_fs), rc, out);

    _snprintf_buff(err_msg, rc, out, sql_cmd,
                   "SELECT * from " _DB_TABLE_FS_SNAPS_VIEW
                   " WHERE fs_id=%" PRIu64 ";",
                   sim_fs_id);

    _good(_db_sql_exec(err_msg, db, sql_cmd, &vec), rc, out);
    if (_vector_size(vec) == 0) {
        *ss = NULL;
        *ss_count = 0;
        goto out;
    }
    _vec_to_lsm_xxx_array(err_msg, vec, lsm_fs_ss, _sim_fs_snap_to_lsm, ss,
                          ss_count, rc, out);
out:
    _db_sql_trans_rollback(db);
    _db_sql_exec_vec_free(vec);

    if (sim_fs != NULL)
        lsm_hash_free(sim_fs);

    if (rc != LSM_ERR_OK) {
        if ((ss != NULL) && (ss_count != NULL)) {
            if (*ss != NULL)
                lsm_fs_ss_record_array_free(*ss, *ss_count);
        }
        if (ss != NULL)
            *ss = NULL;
        if (ss_count != NULL)
            *ss_count = 0;
        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

int fs_snapshot_create(lsm_plugin_ptr c, lsm_fs *fs, const char *name,
                       lsm_fs_ss **snapshot, char **job, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    struct _vector *vec = NULL;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_fs = NULL;
    uint64_t sim_fs_id = 0;
    char ts_str[_BUFF_SIZE];
    struct timespec ts;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 4 /* argument count */, fs, name, snapshot,
                          job),
          rc, out);

    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);
    /* Check fs existence */
    sim_fs_id = _db_lsm_id_to_sim_id(lsm_fs_id_get(fs));
    _good(_db_sim_fs_of_sim_id(err_msg, db, sim_fs_id, &sim_fs), rc, out);

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg, "BUG: clock_gettime(CLOCK_REALTIME, &ts) "
                                  "failed");
        goto out;
    }
    _snprintf_buff(err_msg, rc, out, ts_str, "%" PRIu64,
                   (uint64_t)difftime(ts.tv_sec, 0));

    rc = _db_data_add(err_msg, db, _DB_TABLE_FS_SNAPS, "name", name, "fs_id",
                      _db_lsm_id_to_sim_id_str(lsm_fs_id_get(fs)), "timestamp",
                      ts_str, NULL);

    if (rc != LSM_ERR_OK) {
        if (sqlite3_errcode(db) == SQLITE_CONSTRAINT) {
            rc = LSM_ERR_NAME_CONFLICT;
            _lsm_err_msg_set(err_msg, "FS snapshot name '%s' in use", name);
        }
        goto out;
    }
    _good(_job_create(err_msg, db, LSM_DATA_TYPE_SS, _db_last_rowid(db), job),
          rc, out);
    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    _db_sql_exec_vec_free(vec);
    if (snapshot != NULL)
        *snapshot = NULL;

    if (sim_fs != NULL)
        lsm_hash_free(sim_fs);

    if (rc != LSM_ERR_OK) {
        lsm_log_error_basic(c, rc, err_msg);
    } else {
        rc = LSM_ERR_JOB_STARTED;
    }
    return rc;
}

int fs_snapshot_delete(lsm_plugin_ptr c, lsm_fs *fs, lsm_fs_ss *ss, char **job,
                       lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_fs_snap = NULL;
    uint64_t sim_fs_snap_id = 0;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 3 /* argument count */, fs, ss, job), rc,
          out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);

    _good(_db_sql_trans_begin(err_msg, db), rc, out);
    sim_fs_snap_id = _db_lsm_id_to_sim_id(lsm_fs_ss_id_get(ss));
    /* The existence of fs snapshot indicate the fs is exist due to the sqlite
     * REFERENCES and PRAGMA foreign_keys = ON
     */
    _good(_db_sim_fs_snap_of_sim_id(err_msg, db, sim_fs_snap_id, &sim_fs_snap),
          rc, out);
    _good(_db_data_delete(err_msg, db, _DB_TABLE_FS_SNAPS, sim_fs_snap_id), rc,
          out);
    _good(_job_create(err_msg, db, LSM_DATA_TYPE_NONE, _DB_SIM_ID_NONE, job),
          rc, out);
    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (sim_fs_snap != NULL)
        lsm_hash_free(sim_fs_snap);

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

int fs_snapshot_restore(lsm_plugin_ptr c, lsm_fs *fs, lsm_fs_ss *ss,
                        lsm_string_list *files, lsm_string_list *restore_files,
                        int all_files, char **job, lsm_flag flags) {
    /* Currently LSM cannot query status of this action.
     * we simply check existence of fs and snapshot(if snapshot is not NULL)
     * To simplify code, we use fs_file_clone() here which do exactly the same
     * check.
     */
    _UNUSED(files);
    _UNUSED(restore_files);
    _UNUSED(all_files);
    return fs_file_clone(c, fs, "dummy", "dummy", ss, job, flags);
}
