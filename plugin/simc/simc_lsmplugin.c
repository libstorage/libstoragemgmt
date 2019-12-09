/*
 * Copyright (C) 2011-2016 Red Hat, Inc.
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


#include <stdio.h>
#include <openssl/md5.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <unistd.h>

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>

#include "utils.h"
#include "db.h"
#include "mgm_ops.h"
#include "san_ops.h"
#include "fs_ops.h"
#include "nfs_ops.h"
#include "ops_v1_2.h"
#include "ops_v1_3.h"

#define PLUGIN_NAME                 "Compiled plug-in example"
#define DEFAULT_STATE_FILE_PATH     "/tmp/lsm_sim_data"

int plugin_register(lsm_plugin_ptr c, const char *uri, const char *password,
                    uint32_t timeout, lsm_flag flags);
int plugin_unregister(lsm_plugin_ptr c, lsm_flag flags);

static struct lsm_mgmt_ops_v1 mgm_ops = {
    tmo_set,
    tmo_get,
    capabilities,
    job_status,
    job_free,
    pool_list,
    system_list,
};

static struct lsm_san_ops_v1 san_ops = {
    volume_list,
    disk_list,
    volume_create,
    volume_replicate,
    volume_replicate_range_block_size,
    volume_replicate_range,
    volume_resize,
    volume_delete,
    volume_enable,
    volume_disable,
    iscsi_chap_auth,
    access_group_list,
    access_group_create,
    access_group_delete,
    access_group_initiator_add,
    access_group_initiator_delete,
    volume_mask,
    volume_unmask,
    volumes_accessible_by_access_group,
    access_groups_granted_to_volume,
    vol_child_depends,
    vol_child_depends_rm,
    target_port_list,
};

static struct lsm_fs_ops_v1 fs_ops = {
    fs_list,
    fs_create,
    fs_delete,
    fs_resize,
    fs_clone,
    fs_file_clone,
    fs_child_dependency,
    fs_child_dependency_rm,
    fs_snapshot_list,
    fs_snapshot_create,
    fs_snapshot_delete,
    fs_snapshot_restore,
};

static struct lsm_nas_ops_v1 nfs_ops = {
    nfs_auth_types,
    nfs_list,
    nfs_export_fs,
    nfs_export_remove,
};

static struct lsm_ops_v1_2 ops_v1_2 = {
    volume_raid_info,
    pool_member_info,
    volume_raid_create_cap_get,
    volume_raid_create,
};

static struct lsm_ops_v1_3 ops_v1_3 = {
    volume_ident_led_on,
    volume_ident_led_off,
    system_read_cache_pct_update,
    battery_list,
    volume_cache_info,
    volume_physical_disk_cache_update,
    volume_write_cache_policy_update,
    volume_read_cache_policy_update,
};

int plugin_register(lsm_plugin_ptr c, const char *uri, const char *password,
                    uint32_t timeout, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    char *scheme = NULL;
    char *user = NULL;
    char *server = NULL;
    int port = 0;
    char *path = NULL;
    lsm_hash *uri_params = NULL;
    const char *statefile = NULL;
    int fd = -1;
    /* Create database file with 0666 permission if not exists */
    mode_t fd_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    char err_msg[_LSM_ERR_MSG_LEN];
    struct sqlite3 *db = NULL;
    struct _simc_private_data *pri_data = NULL;

    _UNUSED(password);
    _UNUSED(flags);
    _lsm_err_msg_clear(err_msg);

    /* Use URI 'statefile' parameter as state file path if defined,
     * else use system environment LSM_SIM_DATA,
     * else use DEFAULT_STATE_FILE_PATH
     */
    _good(lsm_uri_parse(uri, &scheme, &user, &server, &port, &path,
                        &uri_params), rc, out);

    if (uri_params != NULL)
        statefile = lsm_hash_string_get(uri_params, "statefile");

    if (statefile == NULL)
        statefile = getenv("LSM_SIM_DATA");

    if (statefile == NULL)
        statefile = DEFAULT_STATE_FILE_PATH;

    if (! _file_exists(statefile)) {
        fd = open(statefile, O_WRONLY | O_CREAT, fd_mode);
        if (fd < 0) {
            rc = LSM_ERR_INVALID_ARGUMENT;
            _lsm_err_msg_set(err_msg, "Failed to create statefile '%s', "
                             "error %d: %s", statefile, errno, strerror(errno));
            goto out;
        }
        close(fd);
    }

    _good(_db_init(err_msg, &db, statefile, timeout), rc, out);

    pri_data = (struct _simc_private_data *)
        malloc(sizeof(struct _simc_private_data));
    _alloc_null_check(err_msg, pri_data, rc, out);

    pri_data->db = db;
    pri_data->timeout = timeout;

    rc = lsm_register_plugin_v1_3(c, pri_data, &mgm_ops, &san_ops,
                                  &fs_ops, &nfs_ops, &ops_v1_2,
                                  &ops_v1_3);

 out:
    free(scheme);
    free(user);
    free(server);
    free(path);
    if (uri_params != NULL)
        lsm_hash_free(uri_params);

    if (rc != LSM_ERR_OK) {
        _db_close(db);
        lsm_log_error_basic(c, rc, err_msg);
    }

    return rc;
}

int plugin_unregister(lsm_plugin_ptr c, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    struct _simc_private_data *pri_data = NULL;

    _UNUSED(flags);
    if (c != NULL) {
        pri_data = lsm_private_data_get(c);
        if ((pri_data != NULL) && (pri_data->db != NULL))
                _db_close(pri_data->db);
        free(pri_data);
    }

    return rc;
}

int main(int argc, char *argv[])
{
    return lsm_plugin_init_v1(argc, argv, plugin_register,
                              plugin_unregister, PLUGIN_NAME, _DB_VERSION);
}
