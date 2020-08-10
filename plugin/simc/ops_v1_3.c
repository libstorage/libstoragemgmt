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
#include "ops_v1_3.h"
#include "utils.h"

static lsm_battery *_sim_bat_to_lsm(char *err_msg, lsm_hash *sim_bat);
static int _vol_cache_update(lsm_plugin_ptr c, lsm_volume *volume,
                             const char *key_name, uint32_t value);

_xxx_list_func_gen(battery_list, lsm_battery, _sim_bat_to_lsm,
                   lsm_plug_battery_search_filter, _DB_TABLE_BATS_VIEW,
                   lsm_battery_record_array_free);

static lsm_battery *_sim_bat_to_lsm(char *err_msg, lsm_hash *sim_bat) {
    const char *plugin_data = NULL;
    lsm_battery *lsm_bat = NULL;
    uint64_t status = LSM_BATTERY_STATUS_UNKNOWN;
    lsm_battery_type type = LSM_BATTERY_TYPE_UNKNOWN;

    if ((_str_to_int(err_msg, lsm_hash_string_get(sim_bat, "type"),
                     (int *)&type) != LSM_ERR_OK) ||
        (_str_to_uint64(err_msg, lsm_hash_string_get(sim_bat, "status"),
                        &status) != LSM_ERR_OK))
        return NULL;

    lsm_bat =
        lsm_battery_record_alloc(lsm_hash_string_get(sim_bat, "lsm_bat_id"),
                                 lsm_hash_string_get(sim_bat, "name"), type,
                                 status, _SYS_ID, plugin_data);

    if (lsm_bat == NULL)
        _lsm_err_msg_set(err_msg, "No memory");

    return lsm_bat;
}

static int _vol_cache_update(lsm_plugin_ptr c, lsm_volume *volume,
                             const char *key_name, uint32_t value) {
    int rc = LSM_ERR_OK;
    char err_msg[_LSM_ERR_MSG_LEN];
    uint64_t sim_vol_id = 0;
    lsm_hash *sim_vol = NULL;
    sqlite3 *db = NULL;
    char value_str[_BUFF_SIZE];

    assert(key_name != NULL);

    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 1 /* argument count */, volume), rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_vol_id = _db_lsm_id_to_sim_id(lsm_volume_id_get(volume));
    _good(_db_sim_vol_of_sim_id(err_msg, db, sim_vol_id, &sim_vol), rc, out);

    _snprintf_buff(err_msg, rc, out, value_str, "%" PRIu32, value);

    _good(_db_data_update(err_msg, db, _DB_TABLE_VOLS, sim_vol_id, key_name,
                          value_str),
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

int volume_ident_led_on(lsm_plugin_ptr c, lsm_volume *volume, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    lsm_hash *sim_vol = NULL;
    uint64_t sim_vol_id = 0;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 1 /* argument count */, volume), rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    /* Do nothing but check the existence of volume */
    sim_vol_id = _db_lsm_id_to_sim_id(lsm_volume_id_get(volume));
    _good(_db_sim_vol_of_sim_id(err_msg, db, sim_vol_id, &sim_vol), rc, out);

out:
    _db_sql_trans_rollback(db);
    if (sim_vol != NULL)
        lsm_hash_free(sim_vol);
    if (rc != LSM_ERR_OK)
        lsm_log_error_basic(c, rc, err_msg);
    return rc;
}

int volume_ident_led_off(lsm_plugin_ptr c, lsm_volume *volume, lsm_flag flags) {
    return volume_ident_led_on(c, volume, flags);
}

int system_read_cache_pct_update(lsm_plugin_ptr c, lsm_system *system,
                                 uint32_t read_pct, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    sqlite3 *db = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    char sql_cmd[_BUFF_SIZE];
    const char *in_sys_id = NULL;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 1 /* argument count */, system), rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    in_sys_id = lsm_system_id_get(system);

    if ((in_sys_id == NULL) || (strcmp(in_sys_id, _SYS_ID) != 0)) {
        rc = LSM_ERR_NOT_FOUND_SYSTEM;
        _lsm_err_msg_set(err_msg, "System not found");
        goto out;
    }

    _snprintf_buff(err_msg, rc, out, sql_cmd,
                   "UPDATE " _DB_TABLE_SYS " SET read_cache_pct=%" PRIu32
                   " WHERE id='" _SYS_ID "';",
                   read_pct);

    _good(
        _db_sql_exec(err_msg, db, sql_cmd, NULL /* no need to parse output */),
        rc, out);

    _good(_db_sql_trans_commit(err_msg, db), rc, out);

out:
    if (rc != LSM_ERR_OK) {
        _db_sql_trans_rollback(db);
        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

int volume_cache_info(lsm_plugin_ptr c, lsm_volume *volume,
                      uint32_t *write_cache_policy,
                      uint32_t *write_cache_status, uint32_t *read_cache_policy,
                      uint32_t *read_cache_status,
                      uint32_t *physical_disk_cache, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    char err_msg[_LSM_ERR_MSG_LEN];
    uint64_t sim_vol_id = 0;
    lsm_hash *sim_vol = NULL;
    sqlite3 *db = NULL;
    char sql_cmd[_BUFF_SIZE];
    struct _vector *vec = NULL;
    bool battery_ok = false;

    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);
    _good(_check_null_ptr(err_msg, 5 /* argument count */, volume,
                          write_cache_policy, write_cache_status,
                          read_cache_policy, read_cache_status,
                          physical_disk_cache),
          rc, out);
    _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);
    _good(_db_sql_trans_begin(err_msg, db), rc, out);

    sim_vol_id = _db_lsm_id_to_sim_id(lsm_volume_id_get(volume));
    _good(_db_sim_vol_of_sim_id(err_msg, db, sim_vol_id, &sim_vol), rc, out);

    _good(_str_to_uint32(err_msg,
                         lsm_hash_string_get(sim_vol, "write_cache_policy"),
                         write_cache_policy),
          rc, out);
    _good(_str_to_uint32(err_msg,
                         lsm_hash_string_get(sim_vol, "read_cache_policy"),
                         read_cache_policy),
          rc, out);
    _good(_str_to_uint32(err_msg,
                         lsm_hash_string_get(sim_vol, "phy_disk_cache"),
                         physical_disk_cache),
          rc, out);

    /* Check whether has a battery in OK status */
    _snprintf_buff(err_msg, rc, out, sql_cmd,
                   "SELECT id from " _DB_TABLE_BATS " WHERE status = '%d';",
                   LSM_BATTERY_STATUS_OK);
    _good(_db_sql_exec(err_msg, db, sql_cmd, &vec), rc, out);
    if (_vector_size(vec) > 0)
        battery_ok = true;

    switch (*write_cache_policy) {
    case LSM_VOLUME_WRITE_CACHE_POLICY_AUTO:
        if (battery_ok == true)
            *write_cache_status = LSM_VOLUME_WRITE_CACHE_STATUS_WRITE_BACK;
        else
            *write_cache_status = LSM_VOLUME_WRITE_CACHE_STATUS_WRITE_THROUGH;
        break;
    case LSM_VOLUME_WRITE_CACHE_POLICY_WRITE_BACK:
        *write_cache_status = LSM_VOLUME_WRITE_CACHE_STATUS_WRITE_BACK;
        break;
    case LSM_VOLUME_WRITE_CACHE_POLICY_WRITE_THROUGH:
        *write_cache_status = LSM_VOLUME_WRITE_CACHE_STATUS_WRITE_THROUGH;
        break;
    case LSM_VOLUME_WRITE_CACHE_POLICY_UNKNOWN:
        *write_cache_status = LSM_VOLUME_WRITE_CACHE_STATUS_UNKNOWN;
        break;
    default:
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg,
                         "BUG: Got unknown write_cache_policy %" PRIu32,
                         *write_cache_policy);
        goto out;
    }

    switch (*read_cache_policy) {
    case LSM_VOLUME_READ_CACHE_POLICY_DISABLED:
        *read_cache_status = LSM_VOLUME_READ_CACHE_STATUS_DISABLED;
        break;
    case LSM_VOLUME_READ_CACHE_POLICY_ENABLED:
        *read_cache_status = LSM_VOLUME_READ_CACHE_STATUS_ENABLED;
        break;
    case LSM_VOLUME_READ_CACHE_POLICY_UNKNOWN:
        *read_cache_status = LSM_VOLUME_READ_CACHE_POLICY_UNKNOWN;
        break;
    default:
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg, "BUG: Got unknown read_cache_policy %" PRIu32,
                         *read_cache_policy);
        goto out;
    }

out:
    _db_sql_exec_vec_free(vec);
    _db_sql_trans_rollback(db);
    if (sim_vol != NULL)
        lsm_hash_free(sim_vol);

    if (rc != LSM_ERR_OK) {
        if (write_cache_policy != NULL)
            *write_cache_policy = LSM_VOLUME_WRITE_CACHE_POLICY_UNKNOWN;
        if (write_cache_status != NULL)
            *write_cache_status = LSM_VOLUME_WRITE_CACHE_STATUS_UNKNOWN;
        if (read_cache_policy != NULL)
            *read_cache_policy = LSM_VOLUME_READ_CACHE_POLICY_UNKNOWN;
        if (read_cache_status != NULL)
            *read_cache_status = LSM_VOLUME_READ_CACHE_STATUS_UNKNOWN;
        if (physical_disk_cache != NULL)
            *physical_disk_cache = LSM_VOLUME_PHYSICAL_DISK_CACHE_UNKNOWN;
        lsm_log_error_basic(c, rc, err_msg);
    }
    return rc;
}

int volume_physical_disk_cache_update(lsm_plugin_ptr c, lsm_volume *volume,
                                      uint32_t pdc, lsm_flag flags) {
    _UNUSED(flags);
    return _vol_cache_update(c, volume, "phy_disk_cache", pdc);
}

int volume_write_cache_policy_update(lsm_plugin_ptr c, lsm_volume *volume,
                                     uint32_t wcp, lsm_flag flags) {
    _UNUSED(flags);
    return _vol_cache_update(c, volume, "write_cache_policy", wcp);
}

int volume_read_cache_policy_update(lsm_plugin_ptr c, lsm_volume *volume,
                                    uint32_t rcp, lsm_flag flags) {
    _UNUSED(flags);
    return _vol_cache_update(c, volume, "read_cache_policy", rcp);
}
