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
#include <stdint.h>

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>

#include "db.h"
#include "mgm_ops.h"
#include "nfs_ops.h"
#include "utils.h"

static lsm_nfs_export *_sim_exp_to_lsm(char *err_msg, lsm_hash *sim_exp);

/*
 * This function is simply split some lines of out nfs_export_fs() to make
 * function small.
 */
static int _nfs_export(char *err_msg, sqlite3 *db, const char *sim_fs_id_str,
                       const char *export_path, lsm_string_list *root_list,
                       lsm_string_list *rw_list, lsm_string_list *ro_list,
                       uint64_t anon_uid, uint64_t anon_gid,
                       const char *auth_type, const char *options,
                       uint64_t *sim_exp_id);

_xxx_list_func_gen(nfs_list, lsm_nfs_export, _sim_exp_to_lsm,
                   lsm_plug_nfs_export_search_filter, _DB_TABLE_NFS_EXPS_VIEW,
                   lsm_nfs_export_record_array_free);

static lsm_nfs_export *_sim_exp_to_lsm(char *err_msg, lsm_hash *sim_exp) {
    const char *plugin_data = NULL;
    uint64_t anon_uid = 0;
    uint64_t anon_gid = 0;
    lsm_nfs_export *lsm_nfs_obj = NULL;
    lsm_string_list *root_hosts = NULL;
    lsm_string_list *rw_hosts = NULL;
    lsm_string_list *ro_hosts = NULL;
    const char *root_hosts_str = NULL;
    const char *rw_hosts_str = NULL;
    const char *ro_hosts_str = NULL;

    assert(sim_exp != NULL);

    if (strcmp(lsm_hash_string_get(sim_exp, "anon_uid"), "-1") == 0)
        anon_uid = -1;
    else if (_str_to_uint64(err_msg, lsm_hash_string_get(sim_exp, "anon_uid"),
                            &anon_uid) != LSM_ERR_OK)
        return NULL;

    if (strcmp(lsm_hash_string_get(sim_exp, "anon_gid"), "-1") == 0)
        anon_gid = -1;
    else if (_str_to_uint64(err_msg, lsm_hash_string_get(sim_exp, "anon_gid"),
                            &anon_gid) != LSM_ERR_OK)
        return NULL;

    root_hosts_str = lsm_hash_string_get(sim_exp, "exp_root_hosts_str");
    if (root_hosts_str == NULL) {
        _lsm_err_msg_set(err_msg, "BUG: No 'exp_root_hosts_str' in lsm_hash "
                                  "sim_exp");
        return NULL;
    }
    rw_hosts_str = lsm_hash_string_get(sim_exp, "exp_rw_hosts_str");
    if (rw_hosts_str == NULL) {
        _lsm_err_msg_set(err_msg, "BUG: No 'exp_rw_hosts_str' in lsm_hash "
                                  "sim_exp");
        return NULL;
    }
    ro_hosts_str = lsm_hash_string_get(sim_exp, "exp_ro_hosts_str");
    if (ro_hosts_str == NULL) {
        _lsm_err_msg_set(err_msg, "BUG: No 'exp_ro_hosts_str' in lsm_hash "
                                  "sim_exp");
        return NULL;
    }

    root_hosts = _db_str_to_list(root_hosts_str);
    if (root_hosts == NULL) {
        _lsm_err_msg_set(err_msg, "BUG: Failed to convert exp_root_hosts_str "
                                  "to list");
        return NULL;
    }
    rw_hosts = _db_str_to_list(rw_hosts_str);
    if (rw_hosts == NULL) {
        _lsm_err_msg_set(err_msg, "BUG: Failed to convert exp_rw_hosts_str "
                                  "to list");
        lsm_string_list_free(root_hosts);
        return NULL;
    }
    ro_hosts = _db_str_to_list(ro_hosts_str);
    if (ro_hosts == NULL) {
        _lsm_err_msg_set(err_msg, "BUG: Failed to convert exp_ro_hosts_str "
                                  "to list");
        lsm_string_list_free(root_hosts);
        lsm_string_list_free(rw_hosts);
        return NULL;
    }

    lsm_nfs_obj = lsm_nfs_export_record_alloc(
        lsm_hash_string_get(sim_exp, "lsm_exp_id"),
        lsm_hash_string_get(sim_exp, "lsm_fs_id"),
        lsm_hash_string_get(sim_exp, "exp_path"),
        lsm_hash_string_get(sim_exp, "auth_type"), root_hosts, rw_hosts,
        ro_hosts, anon_uid, anon_gid, lsm_hash_string_get(sim_exp, "options"),
        plugin_data);
    lsm_string_list_free(root_hosts);
    lsm_string_list_free(rw_hosts);
    lsm_string_list_free(ro_hosts);

    if (lsm_nfs_obj == NULL)
        _lsm_err_msg_set(err_msg, "No memory");

    return lsm_nfs_obj;
}

int nfs_auth_types(lsm_plugin_ptr c, lsm_string_list **types, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    _UNUSED(c);
    _UNUSED(flags);
    *types = lsm_string_list_alloc(1);
    if (*types) {
        rc = lsm_string_list_elem_set(*types, 0, "standard");
    } else {
        rc = LSM_ERR_NO_MEMORY;
    }
    return rc;
}

int nfs_export_fs(lsm_plugin_ptr c, const char *fs_id, const char *export_path,
                  lsm_string_list *root_list, lsm_string_list *rw_list,
                  lsm_string_list *ro_list, uint64_t anon_uid,
                  uint64_t anon_gid, const char *auth_type, const char *options,
                  lsm_nfs_export **exported, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_fs = NULL;
    lsm_hash *sim_exp = NULL;
    uint64_t sim_fs_id = 0;
    uint64_t sim_exp_id = 0;
    char tmp_export_path[_BUFF_SIZE];
    char vpd83[_VPD_83_LEN];

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 2 /* argument count */, fs_id, exported), rc,
          out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);

    _good(_db_sql_trans_begin(err_msg, db), rc, out);
    sim_fs_id = _db_lsm_id_to_sim_id(fs_id);
    /* Check fs existence */
    _good(_db_sim_fs_of_sim_id(err_msg, db, sim_fs_id, &sim_fs), rc, out);

    if (export_path == NULL) {
        _random_vpd(vpd83);
        /* We only want 8 digits of VPD83 */
        vpd83[8] = '\0';
        _snprintf_buff(err_msg, rc, out, tmp_export_path, "/nfs_exp_%s", vpd83);
        export_path = tmp_export_path;
    }

    _good(_nfs_export(err_msg, db, _db_lsm_id_to_sim_id_str(fs_id), export_path,
                      root_list, rw_list, ro_list, anon_uid, anon_gid,
                      auth_type, options, &sim_exp_id),
          rc, out);

    if (_db_sim_exp_of_sim_id(err_msg, db, sim_exp_id, &sim_exp) !=
        LSM_ERR_OK) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg,
                         "BUG: Failed to find newly created NFS export");
        goto out;
    }

    *exported = _sim_exp_to_lsm(err_msg, sim_exp);
    if (*exported == NULL) {
        rc = LSM_ERR_PLUGIN_BUG;
        goto out;
    }

    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (sim_fs != NULL)
        lsm_hash_free(sim_fs);
    if (sim_exp != NULL)
        lsm_hash_free(sim_exp);
    if (rc != LSM_ERR_OK) {
        _db_sql_trans_rollback(db);
        if (exported != NULL)
            *exported = NULL;
        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

int nfs_export_remove(lsm_plugin_ptr c, lsm_nfs_export *e, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_exp = NULL;
    uint64_t sim_exp_id = 0;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 1 /* argument count */, e), rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);

    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_exp_id = _db_lsm_id_to_sim_id(lsm_nfs_export_id_get(e));

    _good(_db_sim_exp_of_sim_id(err_msg, db, sim_exp_id, &sim_exp), rc, out);

    _good(_db_data_delete(err_msg, db, _DB_TABLE_NFS_EXPS, sim_exp_id), rc,
          out);

    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (sim_exp != NULL)
        lsm_hash_free(sim_exp);
    if (rc != LSM_ERR_OK) {
        _db_sql_trans_rollback(db);
        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

static int _nfs_export(char *err_msg, sqlite3 *db, const char *sim_fs_id_str,
                       const char *export_path, lsm_string_list *root_list,
                       lsm_string_list *rw_list, lsm_string_list *ro_list,
                       uint64_t anon_uid, uint64_t anon_gid,
                       const char *auth_type, const char *options,
                       uint64_t *sim_exp_id) {

    int rc = LSM_ERR_OK;
    size_t i = 0;
    uint32_t j = 0;
    lsm_string_list *host_list = NULL;
    const char *host_list_table = NULL;
    const char *host = NULL;
    char anon_uid_str[_BUFF_SIZE];
    char anon_gid_str[_BUFF_SIZE];
    char sim_exp_id_str[_BUFF_SIZE];
    struct _tmp_type {
        lsm_string_list *host_list;
        const char *table_name;
    };

    struct _tmp_type host_lists[] = {
        {
            root_list,
            _DB_TABLE_NFS_EXP_ROOT_HOSTS,
        },
        {
            rw_list,
            _DB_TABLE_NFS_EXP_RW_HOSTS,
        },
        {
            ro_list,
            _DB_TABLE_NFS_EXP_RO_HOSTS,
        },
    };

    _snprintf_buff(err_msg, rc, out, anon_uid_str, "%" PRIi64,
                   (int64_t)anon_uid);

    _snprintf_buff(err_msg, rc, out, anon_gid_str, "%" PRIi64,
                   (int64_t)anon_gid);

    rc = _db_data_add(err_msg, db, _DB_TABLE_NFS_EXPS, "fs_id", sim_fs_id_str,
                      "exp_path", export_path, "anon_uid", anon_uid_str,
                      "anon_gid", anon_gid_str, "auth_type",
                      auth_type != NULL ? auth_type : "", "options",
                      options != NULL ? options : "", NULL);

    if (rc != LSM_ERR_OK) {
        if (sqlite3_errcode(db) == SQLITE_CONSTRAINT) {
            rc = LSM_ERR_NAME_CONFLICT;
            _lsm_err_msg_set(err_msg,
                             "Export path '%s' is already used by "
                             "other NFS export",
                             export_path);
        }
        goto out;
    }
    *sim_exp_id = _db_last_rowid(db);
    _snprintf_buff(err_msg, rc, out, sim_exp_id_str, "%" PRIu64, *sim_exp_id);

    for (; i < sizeof(host_lists) / sizeof(host_lists[0]); ++i) {
        host_list = host_lists[i].host_list;
        host_list_table = host_lists[i].table_name;
        if (host_list == NULL)
            continue;
        for (j = 0; j < lsm_string_list_size(host_list); ++j) {
            host = lsm_string_list_elem_get(host_list, j);
            if (host == NULL)
                continue;
            _good(_db_data_add(err_msg, db, host_list_table, "host", host,
                               "exp_id", sim_exp_id_str, NULL),
                  rc, out);
        }
    }

out:
    if (rc != LSM_ERR_OK)
        *sim_exp_id = _DB_SIM_ID_NONE;
    return rc;
}
