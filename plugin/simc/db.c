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
#include <limits.h>
#include <sqlite3.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>

#include "db.h"
#include "db_table_init.h"
#include "vector.h"

#define _DB_VERSION_CHECK_PASS          0
#define _DB_VERSION_CHECK_FAIL          1
#define _DB_VERSION_CHECK_EMPTY         2
#define _SIZE_2TIB_STR                  "2199023255552"
#define _SIZE_512GIB_STR                "549755813888"
#define _SIZE_BIG                       "1152921504606846976"
#define _DEFAULT_POOL_STRIP_SIZE        131072 /* 128 KiB*/
#define _POOL_STATUS_OK_STR             "2"
#define _POOL_MEMBER_TYPE_DISK_STR      "2"
#define _POOL_MEMBER_TYPE_POOL_STR      "3"
#define _DISK_ROLE_DATA                 "DATA"
#define _DISK_ROLE_PARITY               "PARITY"
#define _VOLUME_RAID_TYPE_OTHER_STR     "22"
#define _DEFAULT_SYS_READ_CACHE_PCT_STR "10"

static char _SYS_VERSION[_BUFF_SIZE];

static const lsm_volume_raid_type _SUPPORTED_RAID_TYPES[] = {
    LSM_VOLUME_RAID_TYPE_RAID0,  LSM_VOLUME_RAID_TYPE_RAID1,
    LSM_VOLUME_RAID_TYPE_RAID5,  LSM_VOLUME_RAID_TYPE_RAID6,
    LSM_VOLUME_RAID_TYPE_RAID10, LSM_VOLUME_RAID_TYPE_RAID50,
    LSM_VOLUME_RAID_TYPE_RAID60,
};

static const uint32_t _SUPPORTED_STRIP_SIZES[] = {
    8 * 1024,   16 * 1024,  32 * 1024,  64 * 1024,
    128 * 1024, 256 * 1024, 512 * 1024, 1024 * 1024,
};

static int _parse_sql_column(void *v, int columne_count, char **values,
                             char **keys);
static int _db_version_check(sqlite3 *db);

static int _db_data_init(char *err_msg, sqlite3 *db);

static const char *_sys_version(void);

/*
 * Returned memory should be freed by lsm_hash_free().
 */
static int _db_sim_xxx_of_sim_id(char *err_msg, sqlite3 *db,
                                 const char *table_name, uint64_t sim_id,
                                 lsm_hash **sim_xxx, int not_found_err,
                                 const char *not_found_err_str);

static int _db_pool_create_sub_pool(char *err_msg, sqlite3 *db,
                                    const char *name,
                                    uint64_t parent_sim_pool_id,
                                    const char *size_str, uint64_t element_type,
                                    uint64_t unsupported_actions);

static int _parse_sql_column(void *v, int columne_count, char **values,
                             char **keys) {
    int i = 0;
    struct _vector *vec = (struct _vector *)v;
    lsm_hash *sim_xxx = NULL;
    const char *value = NULL;

    assert(vec != NULL);
    assert(values != NULL);
    assert(keys != NULL);

    sim_xxx = lsm_hash_alloc();

    if (sim_xxx == NULL)
        return -1;

    for (; i < columne_count; ++i) {
        value = values[i];
        if (value == NULL)
            value = "";
        if (lsm_hash_string_set(sim_xxx, keys[i], value) != LSM_ERR_OK) {
            lsm_hash_free(sim_xxx);
            return -1;
        }
    }
    if (_vector_insert(vec, sim_xxx) != 0) {
        lsm_hash_free(sim_xxx);
        return -1;
    }

    return 0;
}

static int _db_version_check(sqlite3 *db) {
    int rc = _DB_VERSION_CHECK_FAIL;
    struct _vector *vec = NULL;
    lsm_hash *sim_sys = NULL;
    const char *version = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];

    /* We ignore the failure of below command, assigning to rc just to pass
     * convscan */
    rc = _db_sql_exec(err_msg, db, "SELECT * from systems;", &vec);

    if (_vector_size(vec) == 0) {
        rc = _DB_VERSION_CHECK_EMPTY;
        goto out;
    }

    sim_sys = _vector_get(vec, 0);

    version = lsm_hash_string_get(sim_sys, "version");

    if ((version != NULL) && (strcmp(version, _sys_version()) == 0))
        rc = _DB_VERSION_CHECK_PASS;

out:
    _db_sql_exec_vec_free(vec);

    return rc;
}

static int _db_data_init(char *err_msg, sqlite3 *db) {
    int rc = LSM_ERR_OK;
    char sys_status_str[_BUFF_SIZE];
    char disk_type_str[_BUFF_SIZE];
    char disk_status_str[_BUFF_SIZE];
    char disk_link_type_str[_BUFF_SIZE];
    char disk_rpm_ssd_str[_BUFF_SIZE];
    char tgt_port_type_str[_BUFF_SIZE];
    char bat_type_str[_BUFF_SIZE];
    char bat_status_str[_BUFF_SIZE];
    uint64_t pool_1_disks[2];
    uint64_t test_pool_disks[2];
    uint64_t ssd_pool_disks[2];
    int i = 0;
    uint64_t sim_pool_id = 0;
    char vpd_buff[_VPD_83_LEN];
    char location_str[_BUFF_SIZE];

    assert(db != NULL);
    srand(time(NULL));

    _snprintf_buff(err_msg, rc, out, sys_status_str, "%d",
                   LSM_SYSTEM_STATUS_OK);

    _good(_db_data_add(err_msg, db, _DB_TABLE_SYS, "id", _SYS_ID, "name",
                       "LSM simulated storage plug-in", "status",
                       sys_status_str, "status_info", "", "read_cache_pct",
                       _DEFAULT_SYS_READ_CACHE_PCT_STR, "version",
                       _sys_version(), NULL),
          rc, out);

    _snprintf_buff(err_msg, rc, out, disk_status_str, "%d", LSM_DISK_STATUS_OK);

    /* Add 2 SATA disks(2TiB) */
    _snprintf_buff(err_msg, rc, out, disk_type_str, "%d", LSM_DISK_TYPE_SATA);
    _snprintf_buff(err_msg, rc, out, disk_link_type_str, "%d",
                   LSM_DISK_LINK_TYPE_ATA);
    for (; i < 2; ++i) {
        _snprintf_buff(err_msg, rc, out, location_str, "Port: %d Box: 1 Bay: 1",
                       i);
        _good(_db_data_add(err_msg, db, _DB_TABLE_DISKS, "disk_prefix",
                           "2TiB SATA Disk", "total_space", _SIZE_2TIB_STR,
                           "disk_type", disk_type_str, "status",
                           disk_status_str, "vpd83", _random_vpd(vpd_buff),
                           "rpm", "7200", "link_type", disk_link_type_str,
                           "location", location_str, NULL),
              rc, out);
        pool_1_disks[i] = _db_last_rowid(db);
    }
    /* Add 6 SAS disks(2TiB) */
    _snprintf_buff(err_msg, rc, out, disk_type_str, "%d", LSM_DISK_TYPE_SAS);
    _snprintf_buff(err_msg, rc, out, disk_link_type_str, "%d",
                   LSM_DISK_LINK_TYPE_SAS);
    for (i = 0; i < 6; ++i) {
        _snprintf_buff(err_msg, rc, out, location_str, "Port: %d Box: 1 Bay: 2",
                       i);
        _good(_db_data_add(err_msg, db, _DB_TABLE_DISKS, "disk_prefix",
                           "1 BIG SAS Disk", "total_space", _SIZE_BIG,
                           "disk_type", disk_type_str, "status",
                           disk_status_str, "vpd83", _random_vpd(vpd_buff),
                           "rpm", "15000", "link_type", disk_link_type_str,
                           "location", location_str, NULL),
              rc, out);
        if (i < 2) {
            test_pool_disks[i] = _db_last_rowid(db);
        }
    }

    /* Add 5 SATA SSD disks(512GiB) */
    _snprintf_buff(err_msg, rc, out, disk_type_str, "%d", LSM_DISK_TYPE_SSD);
    _snprintf_buff(err_msg, rc, out, disk_link_type_str, "%d",
                   LSM_DISK_LINK_TYPE_ATA);
    _snprintf_buff(err_msg, rc, out, disk_rpm_ssd_str, "%d",
                   LSM_DISK_RPM_NON_ROTATING_MEDIUM);
    for (i = 0; i < 5; ++i) {
        _snprintf_buff(err_msg, rc, out, location_str, "Port: %d Box: 1 Bay: 3",
                       i);
        _good(_db_data_add(err_msg, db, _DB_TABLE_DISKS, "disk_prefix",
                           "512GiB SSD Disk", "total_space", _SIZE_512GIB_STR,
                           "disk_type", disk_type_str, "status",
                           disk_status_str, "vpd83", _random_vpd(vpd_buff),
                           "rpm", disk_rpm_ssd_str, "link_type",
                           disk_link_type_str, "location", location_str, NULL),
              rc, out);
        if (i < 2) {
            ssd_pool_disks[i] = _db_last_rowid(db);
        }
    }
    /* Add 7 SAS SSD disks(2TiB) */
    _snprintf_buff(err_msg, rc, out, disk_type_str, "%d", LSM_DISK_TYPE_SSD);
    _snprintf_buff(err_msg, rc, out, disk_link_type_str, "%d",
                   LSM_DISK_LINK_TYPE_SAS);
    for (i = 0; i < 5; ++i) {
        _snprintf_buff(err_msg, rc, out, location_str, "Port: %d Box: 1 Bay: 3",
                       i);
        _good(_db_data_add(err_msg, db, _DB_TABLE_DISKS, "disk_prefix",
                           "2TiB SSD Disk", "total_space", _SIZE_2TIB_STR,
                           "disk_type", disk_type_str, "status",
                           disk_status_str, "vpd83", _random_vpd(vpd_buff),
                           "rpm", disk_rpm_ssd_str, "link_type",
                           disk_link_type_str, "location", location_str, NULL),
              rc, out);
    }
    _snprintf_buff(err_msg, rc, out, tgt_port_type_str, "%d",
                   LSM_TARGET_PORT_TYPE_FC);
    _good(_db_data_add(
              err_msg, db, _DB_TABLE_TGTS, "port_type", tgt_port_type_str,
              "service_address", "50:0a:09:86:99:4b:8d:c5", "network_address",
              "50:0a:09:86:99:4b:8d:c5", "physical_address",
              "50:0a:09:86:99:4b:8d:c5", "physical_name", "FC_a_0b", NULL),
          rc, out);

    _snprintf_buff(err_msg, rc, out, tgt_port_type_str, "%d",
                   LSM_TARGET_PORT_TYPE_FCOE);
    _good(_db_data_add(
              err_msg, db, _DB_TABLE_TGTS, "port_type", tgt_port_type_str,
              "service_address", "50:0a:09:86:99:4b:8d:c6", "network_address",
              "50:0a:09:86:99:4b:8d:c6", "physical_address",
              "50:0a:09:86:99:4b:8d:c6", "physical_name", "FCoE_b_0c", NULL),
          rc, out);

    _snprintf_buff(err_msg, rc, out, tgt_port_type_str, "%d",
                   LSM_TARGET_PORT_TYPE_ISCSI);
    _good(_db_data_add(err_msg, db, _DB_TABLE_TGTS, "port_type",
                       tgt_port_type_str, "service_address",
                       "iqn.1986-05.com.example:sim-tgt-03", "network_address",
                       "sim-iscsi-tgt-3.example.com:3260", "physical_address",
                       "a4:4e:31:47:f4:e0", "physical_name", "iSCSI_c_0d",
                       NULL),
          rc, out);

    _good(_db_data_add(err_msg, db, _DB_TABLE_TGTS, "port_type",
                       tgt_port_type_str, "service_address",
                       "iqn.1986-05.com.example:sim-tgt-03", "network_address",
                       "10.0.0.1:3260", "physical_address", "a4:4e:31:47:f4:e1",
                       "physical_name", "iSCSI_c_0e", NULL),
          rc, out);

    _good(_db_data_add(err_msg, db, _DB_TABLE_TGTS, "port_type",
                       tgt_port_type_str, "service_address",
                       "iqn.1986-05.com.example:sim-tgt-03", "network_address",
                       "[2001:470:1f09:efe:a64e:31ff::1]:3260",
                       "physical_address", "a4:4e:31:47:f4:e1", "physical_name",
                       "iSCSI_c_0e", NULL),
          rc, out);

    _snprintf_buff(err_msg, rc, out, bat_status_str, "%d",
                   LSM_BATTERY_STATUS_OK);
    _snprintf_buff(err_msg, rc, out, bat_type_str, "%d",
                   LSM_BATTERY_TYPE_CHEMICAL);
    _good(_db_data_add(err_msg, db, _DB_TABLE_BATS, "name",
                       "Battery SIMB01, 8000 mAh, 05 March 2016", "type",
                       bat_type_str, "status", bat_status_str, NULL),
          rc, out);

    _snprintf_buff(err_msg, rc, out, bat_type_str, "%d",
                   LSM_BATTERY_TYPE_CAPACITOR);
    _good(_db_data_add(err_msg, db, _DB_TABLE_BATS, "name",
                       "Capacitor SIMC01, 500 J, 05 March 2016", "type",
                       bat_type_str, "status", bat_status_str, NULL),
          rc, out);

    /* Create initial pools */
    _good(_db_pool_create_from_disk(
              err_msg, db, "Pool 1", pool_1_disks,
              sizeof(pool_1_disks) / sizeof(pool_1_disks[0]),
              LSM_VOLUME_RAID_TYPE_RAID1,
              LSM_POOL_ELEMENT_TYPE_POOL | LSM_POOL_ELEMENT_TYPE_FS |
                  LSM_POOL_ELEMENT_TYPE_VOLUME | LSM_POOL_ELEMENT_TYPE_DELTA |
                  LSM_POOL_ELEMENT_TYPE_SYS_RESERVED,
              LSM_POOL_UNSUPPORTED_VOLUME_GROW |
                  LSM_POOL_UNSUPPORTED_VOLUME_SHRINK,
              &sim_pool_id, LSM_VOLUME_VCR_STRIP_SIZE_DEFAULT),
          rc, out);

    _good(_db_pool_create_sub_pool(err_msg, db, "Pool 2(sub pool of Pool 1)",
                                   sim_pool_id, _SIZE_512GIB_STR,
                                   LSM_POOL_ELEMENT_TYPE_FS |
                                       LSM_POOL_ELEMENT_TYPE_VOLUME |
                                       LSM_POOL_ELEMENT_TYPE_DELTA,
                                   0 /* No unsupported_actions */),
          rc, out);

    _good(_db_pool_create_from_disk(
              err_msg, db, "Pool 3", ssd_pool_disks,
              sizeof(ssd_pool_disks) / sizeof(ssd_pool_disks[0]),
              LSM_VOLUME_RAID_TYPE_RAID1,
              LSM_POOL_ELEMENT_TYPE_FS | LSM_POOL_ELEMENT_TYPE_VOLUME |
                  LSM_POOL_ELEMENT_TYPE_DELTA,
              0 /* No unsupported_actions */, &sim_pool_id,
              LSM_VOLUME_VCR_STRIP_SIZE_DEFAULT),
          rc, out);

    _good(_db_pool_create_from_disk(
              err_msg, db, "lsm_test_aggr", test_pool_disks,
              sizeof(test_pool_disks) / sizeof(test_pool_disks[0]),
              LSM_VOLUME_RAID_TYPE_RAID0,
              LSM_POOL_ELEMENT_TYPE_FS | LSM_POOL_ELEMENT_TYPE_VOLUME |
                  LSM_POOL_ELEMENT_TYPE_DELTA,
              0 /* No unsupported_actions */, &sim_pool_id,
              _DEFAULT_POOL_STRIP_SIZE),
          rc, out);

out:
    return rc;
}

static const char *_sys_version(void) {
    char version_md5[_MD5_HASH_STR_LEN];

    _md5(_DB_VERSION, version_md5);

    snprintf(_SYS_VERSION, _BUFF_SIZE, "%s_%s_%s", _DB_VERSION_STR_PREFIX,
             _DB_VERSION, version_md5);

    return _SYS_VERSION;
}

int _db_pool_create_from_disk(char *err_msg, sqlite3 *db, const char *name,
                              uint64_t *sim_disk_ids,
                              uint32_t sim_disk_id_count,
                              lsm_volume_raid_type raid_type,
                              uint64_t element_type,
                              uint64_t unsupported_actions,
                              uint64_t *sim_pool_id, uint32_t strip_size) {
    int rc = LSM_ERR_OK;
    uint32_t data_disk_count = 0;
    uint32_t parity_disk_count = 0;
    uint32_t i = 0;
    char element_type_str[_BUFF_SIZE];
    char unsupported_actions_str[_BUFF_SIZE];
    char raid_type_str[_BUFF_SIZE];
    char sim_pool_id_str[_BUFF_SIZE];
    const char *disk_role_data = _DISK_ROLE_DATA;
    const char *disk_role_parity = _DISK_ROLE_PARITY;
    const char *disk_role = NULL;
    char strip_size_str[_BUFF_SIZE];
    size_t j = 0;
    bool found = false;

    assert(db != NULL);
    assert(sim_disk_ids != NULL);
    assert(sim_pool_id != NULL);

    for (j = 0;
         j < sizeof(_SUPPORTED_RAID_TYPES) / sizeof(_SUPPORTED_RAID_TYPES[0]);
         ++j) {
        if (_SUPPORTED_RAID_TYPES[j] == raid_type) {
            found = true;
            break;
        }
    }
    if (found == false) {
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg, "Specified RAID type is not supported");
        goto out;
    }
    if (strip_size != LSM_VOLUME_VCR_STRIP_SIZE_DEFAULT) {
        found = false;
        for (j = 0; j < sizeof(_SUPPORTED_STRIP_SIZES) /
                            sizeof(_SUPPORTED_STRIP_SIZES[0]);
             ++j) {
            if (_SUPPORTED_STRIP_SIZES[j] == strip_size) {
                found = true;
                break;
            }
        }
        if (found == false) {
            rc = LSM_ERR_NO_SUPPORT;
            _lsm_err_msg_set(err_msg, "Specified strip size is not supported");
            goto out;
        }
    }

    if ((raid_type == LSM_VOLUME_RAID_TYPE_RAID1) ||
        (raid_type == LSM_VOLUME_RAID_TYPE_JBOD)) {
        if (strip_size != LSM_VOLUME_VCR_STRIP_SIZE_DEFAULT) {
            rc = LSM_ERR_INVALID_ARGUMENT;
            _lsm_err_msg_set(err_msg,
                             "For RAID 1 and JBOD, strip size should "
                             "be LSM_VOLUME_VCR_STRIP_SIZE_DEFAULT(0)");
            goto out;
        }
        strip_size = _BLOCK_SIZE;
    } else {
        if (strip_size == LSM_VOLUME_VCR_STRIP_SIZE_DEFAULT)
            strip_size = _DEFAULT_POOL_STRIP_SIZE;
    }

    switch (raid_type) {
    case LSM_VOLUME_RAID_TYPE_JBOD:
    case LSM_VOLUME_RAID_TYPE_RAID0:
        parity_disk_count = 0;
        break;
    case LSM_VOLUME_RAID_TYPE_RAID1:
    case LSM_VOLUME_RAID_TYPE_RAID5:
        parity_disk_count = 1;
        break;
    case LSM_VOLUME_RAID_TYPE_RAID6:
    case LSM_VOLUME_RAID_TYPE_RAID50:
        parity_disk_count = 2;
        break;
    case LSM_VOLUME_RAID_TYPE_RAID60:
        parity_disk_count = 4;
        break;
    case LSM_VOLUME_RAID_TYPE_RAID10:
        parity_disk_count = sim_disk_id_count / 2;
        break;
    case LSM_VOLUME_RAID_TYPE_RAID15:
    case LSM_VOLUME_RAID_TYPE_RAID51:
        parity_disk_count = sim_disk_id_count / 2 + 2;
        break;
    case LSM_VOLUME_RAID_TYPE_RAID16:
    case LSM_VOLUME_RAID_TYPE_RAID61:
        parity_disk_count = sim_disk_id_count / 2 + 4;
        break;
    default:
        /* We will never goes here as _SUPPORTED_RAID_TYPES check,
         * exist only for passing convscan or something ugly happened */
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg, "Got unknown RAID type %d", raid_type);
        goto out;
    }

    _snprintf_buff(err_msg, rc, out, strip_size_str, "%" PRIu32, strip_size);

    _snprintf_buff(err_msg, rc, out, element_type_str, "%" PRIu64,
                   element_type);
    _snprintf_buff(err_msg, rc, out, unsupported_actions_str, "%" PRIu64,
                   unsupported_actions);
    _snprintf_buff(err_msg, rc, out, raid_type_str, "%d", raid_type);

    data_disk_count = sim_disk_id_count - parity_disk_count;

    rc = _db_data_add(err_msg, db, _DB_TABLE_POOLS, "name", name, "status",
                      _POOL_STATUS_OK_STR, "status_info", "", "element_type",
                      element_type_str, "unsupported_actions",
                      unsupported_actions_str, "raid_type", raid_type_str,
                      "member_type", _POOL_MEMBER_TYPE_DISK_STR, "strip_size",
                      strip_size_str, NULL);

    if (rc != LSM_ERR_OK) {
        if (sqlite3_errcode(db) == SQLITE_CONSTRAINT) {
            rc = LSM_ERR_NAME_CONFLICT;
            _lsm_err_msg_set(err_msg, "Pool name '%s' in use", name);
        }
        goto out;
    }

    *sim_pool_id = _db_last_rowid(db);

    _snprintf_buff(err_msg, rc, out, sim_pool_id_str, "%" PRIu64, *sim_pool_id);

    for (i = 0; i < sim_disk_id_count; ++i) {
        if (data_disk_count-- > 0)
            disk_role = disk_role_data;
        else if (parity_disk_count-- > 0)
            disk_role = disk_role_parity;
        else
            /* will never be here, just in case */
            break;
        _good(_db_data_update(err_msg, db, _DB_TABLE_DISKS, sim_disk_ids[i],
                              "owner_pool_id", sim_pool_id_str),
              rc, out);

        _good(_db_data_update(err_msg, db, _DB_TABLE_DISKS, sim_disk_ids[i],
                              "role", disk_role),
              rc, out);
    }

out:
    return rc;
}

static int _db_pool_create_sub_pool(char *err_msg, sqlite3 *db,
                                    const char *name,
                                    uint64_t parent_sim_pool_id,
                                    const char *size_str, uint64_t element_type,
                                    uint64_t unsupported_actions) {
    int rc = LSM_ERR_OK;
    char element_type_str[_BUFF_SIZE];
    char unsupported_actions_str[_BUFF_SIZE];
    char parent_sim_pool_id_str[_BUFF_SIZE];

    assert(db != NULL);
    assert(name != NULL);
    assert(size_str != NULL);

    _snprintf_buff(err_msg, rc, out, element_type_str, "%" PRIu64,
                   element_type);
    _snprintf_buff(err_msg, rc, out, unsupported_actions_str, "%" PRIu64,
                   unsupported_actions);
    _snprintf_buff(err_msg, rc, out, parent_sim_pool_id_str, "%" PRIu64,
                   parent_sim_pool_id);

    _good(_db_data_add(err_msg, db, _DB_TABLE_POOLS, "name", name, "status",
                       _POOL_STATUS_OK_STR, "status_info", "", "element_type",
                       element_type_str, "raid_type",
                       _VOLUME_RAID_TYPE_OTHER_STR, "member_type",
                       _POOL_MEMBER_TYPE_POOL_STR, "parent_pool_id",
                       parent_sim_pool_id_str, "total_space", size_str,
                       "unsupported_actions", unsupported_actions_str, NULL),
          rc, out);

out:
    return rc;
}

int _db_init(char *err_msg, sqlite3 **db, const char *db_file,
             uint32_t timeout) {
    int rc = LSM_ERR_OK;
    int db_rc = SQLITE_OK;
    struct _vector *vec = NULL;
    int db_check_rc = _DB_VERSION_CHECK_FAIL;

    assert(db != NULL);

    db_rc = sqlite3_open(db_file, db);
    if (db_rc != SQLITE_OK) {
        rc = LSM_ERR_INVALID_ARGUMENT;
        _lsm_err_msg_set(err_msg,
                         "Failed to open SQLite database file '%s', "
                         "error %d: %s",
                         db_file, db_rc, sqlite3_errmsg(*db));
        goto out;
    }

    db_rc = sqlite3_busy_timeout(*db, timeout & INT_MAX);

    if (db_rc != SQLITE_OK) {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg,
                         "Failed to set timeout %" PRIu32 ", "
                         "sqlite error %d %s",
                         timeout, db_rc, sqlite3_errmsg(*db));
        goto out;
    }

    sqlite3_exec(*db, _TABLE_INIT, NULL /* callback func */,
                 NULL /* callback func first argument */,
                 NULL /* don't generate error message */);

    _good(_db_sql_trans_begin(err_msg, *db), rc, out);

    /* Check db version */
    db_check_rc = _db_version_check(*db);
    if (db_check_rc == _DB_VERSION_CHECK_EMPTY) {
        _good(_db_data_init(err_msg, *db), rc, out);
    } else if (db_check_rc == _DB_VERSION_CHECK_FAIL) {
        rc = LSM_ERR_INVALID_ARGUMENT;
        _lsm_err_msg_set(err_msg,
                         "Stored simulator state incompatible with "
                         "simulator, please move or delete %s",
                         db_file);
        goto out;
    }

    _good(_db_sql_trans_commit(err_msg, *db), rc, out);

out:
    if (rc != LSM_ERR_OK) {
        if (*db != NULL) {
            sqlite3_close(*db);
            _db_sql_trans_rollback(*db);
        }
    }

    _db_sql_exec_vec_free(vec);

    return rc;
}

int _db_sql_exec(char *err_msg, sqlite3 *db, const char *cmd,
                 struct _vector **vec) {
    int rc = LSM_ERR_OK;
    int sql_rc = SQLITE_OK;
    char *sql_err_msg = NULL;

    assert(db != NULL);
    assert(cmd != NULL);
    assert(strlen(cmd) != 0);

    if (vec != NULL) {
        *vec = _vector_new(_VECTOR_NO_PRE_ALLOCATION);
        _alloc_null_check(err_msg, *vec, rc, out);
        sql_rc = sqlite3_exec(db, cmd, _parse_sql_column, *vec, &sql_err_msg);
    } else {
        sql_rc =
            sqlite3_exec(db, cmd, NULL /* callback func */,
                         NULL /* callback func first argument */, &sql_err_msg);
    }
    if (sql_rc == SQLITE_BUSY) {
        rc = LSM_ERR_TIMEOUT;
        _lsm_err_msg_set(err_msg, "Timeout on locking database");
        goto out;
    } else if (sql_rc != SQLITE_OK) {
        rc = LSM_ERR_PLUGIN_BUG;
        if (sql_err_msg != NULL)
            _lsm_err_msg_set(err_msg, "SQLite error %d: %s, %s", sql_rc,
                             sqlite3_errmsg(db), sql_err_msg);
        else
            _lsm_err_msg_set(err_msg, "SQLite error %d: %s", sql_rc,
                             sqlite3_errmsg(db));
        goto out;
    }

out:
    if (rc != LSM_ERR_OK) {
        if (vec != NULL) {
            _db_sql_exec_vec_free(*vec);
            *vec = NULL;
        }
    }
    sqlite3_free(sql_err_msg);
    return rc;
}

void _db_sql_exec_vec_free(struct _vector *vec) {
    uint32_t i = 0;
    lsm_hash *data = NULL;

    _vector_for_each(vec, i, data) lsm_hash_free(data);
    _vector_free(vec);
}

void _db_close(sqlite3 *db) {
    assert(db != NULL);
    sqlite3_close(db);
}

int _db_sql_trans_begin(char *err_msg, sqlite3 *db) {
    assert(db != NULL);
    return _db_sql_exec(err_msg, db, "BEGIN IMMEDIATE TRANSACTION;",
                        NULL /* don't parse output */);
}

int _db_sql_trans_commit(char *err_msg, sqlite3 *db) {
    assert(db != NULL);
    return _db_sql_exec(err_msg, db, "COMMIT;", NULL /* don't parse output */);
}

void _db_sql_trans_rollback(sqlite3 *db) {
    if (db != NULL)
        _db_sql_exec(NULL /* ignore error message */, db, "ROLLBACK;",
                     NULL /* don't parse output */);
}

int _db_data_add(char *err_msg, sqlite3 *db, const char *table_name, ...) {
    int rc = LSM_ERR_OK;
    char sql_cmd[_BUFF_SIZE * 4];
    char keys_str[_BUFF_SIZE];
    char values_str[_BUFF_SIZE];
    const char *key_str = NULL;
    const char *value_str = NULL;
    int keys_printed = 0;
    int values_printed = 0;
    va_list arg;

    assert(db != NULL);
    assert(table_name != NULL);

    va_start(arg, table_name);

    key_str = va_arg(arg, const char *);
    if (key_str != NULL)
        value_str = va_arg(arg, const char *);

    while ((key_str != NULL) && (value_str != NULL)) {
        keys_printed += snprintf(keys_str + keys_printed,
                                 _BUFF_SIZE - keys_printed, "'%s', ", key_str) -
                        1;
        values_printed +=
            snprintf(values_str + values_printed, _BUFF_SIZE - values_printed,
                     "'%s', ", value_str) -
            1;
        if ((_BUFF_SIZE == keys_printed) || (_BUFF_SIZE == values_printed)) {
            va_end(arg);
            rc = LSM_ERR_PLUGIN_BUG;
            _lsm_err_msg_set(err_msg, "Buff too small");
            goto out;
        }
        key_str = va_arg(arg, const char *);
        if (key_str != NULL)
            value_str = va_arg(arg, const char *);
    }
    va_end(arg);

    /* Remove the trailing ", " */
    keys_str[keys_printed - strlen(", ") + 1] = '\0';
    values_str[values_printed - strlen(", ") + 1] = '\0';

    _snprintf_buff(err_msg, rc, out, sql_cmd,
                   "INSERT INTO %s (%s) VALUES (%s);", table_name, keys_str,
                   values_str);

    _good(
        _db_sql_exec(err_msg, db, sql_cmd, NULL /* no need to parse output */),
        rc, out);

out:

    return rc;
}

int _db_data_update(char *err_msg, sqlite3 *db, const char *table_name,
                    uint64_t data_id, const char *key, const char *value) {
    char sql_cmd[_BUFF_SIZE];
    if (value == NULL) {
        if (snprintf(sql_cmd, _BUFF_SIZE,
                     "UPDATE %s SET %s=NULL "
                     "WHERE id='%" PRIu64 "';",
                     table_name, key, data_id) == _BUFF_SIZE)
            goto buff_too_small;
    } else {
        if (snprintf(sql_cmd, _BUFF_SIZE,
                     "UPDATE %s SET %s='%s' "
                     "WHERE id='%" PRIu64 "';",
                     table_name, key, value, data_id) == _BUFF_SIZE)
            goto buff_too_small;
    }

    return _db_sql_exec(err_msg, db, sql_cmd,
                        NULL /* no need to parse output */);
buff_too_small:
    _lsm_err_msg_set(err_msg, "BUG: _db_data_add(): Buff too small");
    return LSM_ERR_PLUGIN_BUG;
}

int _db_data_delete(char *err_msg, sqlite3 *db, const char *table_name,
                    uint64_t data_id) {
    char sql_cmd[_BUFF_SIZE];

    if (snprintf(sql_cmd, _BUFF_SIZE, "DELETE FROM %s WHERE id=%" PRIu64 ";",
                 table_name, data_id) == _BUFF_SIZE) {
        _lsm_err_msg_set(err_msg, "BUG: _db_data_add(): Buff too small");
        return LSM_ERR_PLUGIN_BUG;
    }

    return _db_sql_exec(err_msg, db, sql_cmd,
                        NULL /* no need to parse output */);
}

int _db_data_delete_condition(char *err_msg, sqlite3 *db,
                              const char *table_name, const char *condition) {
    char sql_cmd[_BUFF_SIZE];

    if (snprintf(sql_cmd, _BUFF_SIZE, "DELETE FROM %s WHERE %s;", table_name,
                 condition) == _BUFF_SIZE) {
        _lsm_err_msg_set(err_msg, "BUG: _db_data_add(): Buff too small");
        return LSM_ERR_PLUGIN_BUG;
    }

    return _db_sql_exec(err_msg, db, sql_cmd,
                        NULL /* no need to parse output */);
}

const char *_db_lsm_id_to_sim_id_str(const char *lsm_id) {
    if (lsm_id == NULL)
        return NULL;

    if (strlen(lsm_id) <= _DB_ID_FMT_LEN)
        return NULL;

    return lsm_id + (strlen(lsm_id) - _DB_ID_FMT_LEN);
}

uint64_t _db_lsm_id_to_sim_id(const char *lsm_id) {
    uint64_t sim_id = _DB_SIM_ID_NONE;
    const char *sim_id_str = NULL;

    if (lsm_id == NULL)
        return _DB_SIM_ID_NONE;

    sim_id_str = _db_lsm_id_to_sim_id_str(lsm_id);

    if (sim_id_str != NULL) {
        if (_str_to_uint64(NULL /* ignore error message */, sim_id_str,
                           &sim_id) != LSM_ERR_OK)
            sim_id = _DB_SIM_ID_NONE;
    }

    return sim_id;
}

const char *_db_sim_id_to_lsm_id(char *buff, const char *prefix,
                                 uint64_t sim_id) {
    assert(buff != NULL);
    assert(prefix != NULL);
    assert(sim_id != 0);

    snprintf(buff, _BUFF_SIZE, "%s_%0*" PRIu64, prefix, _DB_ID_FMT_LEN, sim_id);
    return buff;
}

uint64_t _db_last_rowid(sqlite3 *db) {
    sqlite3_int64 rowid = 0;

    assert(db != NULL);

    rowid = sqlite3_last_insert_rowid(db);
    return rowid > 0 ? rowid : 0;
}

uint64_t _db_blk_size_rounding(uint64_t size_bytes) {
    return (size_bytes + _BLOCK_SIZE - 1) / _BLOCK_SIZE * _BLOCK_SIZE;
}

lsm_string_list *_db_str_to_list(const char *list_str) {
    char *saveptr = NULL;
    const char *item_str = NULL;
    lsm_string_list *rc_list = NULL;
    char tmp_str[_BUFF_SIZE];

    assert(list_str != NULL);

    if (strlen(list_str) > _BUFF_SIZE)
        return NULL;

    strncpy(tmp_str, list_str, _BUFF_SIZE - 1);

    rc_list = lsm_string_list_alloc(0 /* no preallocation */);
    if (rc_list == NULL)
        return NULL;

    item_str = strtok_r(tmp_str, _DB_LIST_SPLITTER, &saveptr);

    while (item_str != NULL) {
        if (lsm_string_list_append(rc_list, item_str) != LSM_ERR_OK) {
            lsm_string_list_free(rc_list);
            return NULL;
        }
        item_str = strtok_r(NULL, _DB_LIST_SPLITTER, &saveptr);
    }
    return rc_list;
}

static int _db_sim_xxx_of_sim_id(char *err_msg, sqlite3 *db,
                                 const char *table_name, uint64_t sim_id,
                                 lsm_hash **sim_xxx, int not_found_err,
                                 const char *not_found_err_str) {
    int rc = LSM_ERR_OK;
    struct _vector *vec = NULL;
    char sql_cmd[_BUFF_SIZE];

    assert(db != NULL);
    assert(table_name != NULL);
    assert(sim_xxx != NULL);

    if (sim_id == _DB_SIM_ID_NONE) {
        rc = not_found_err;
        _lsm_err_msg_set(err_msg, "%s", not_found_err_str);
        goto out;
    }

    _snprintf_buff(err_msg, rc, out, sql_cmd,
                   "SELECT * FROM %s WHERE id=%" PRIu64, table_name, sim_id);

    _good(_db_sql_exec(err_msg, db, sql_cmd, &vec), rc, out);

    if (_vector_size(vec) == 1) {
        *sim_xxx = _vector_get(vec, 0);
        *sim_xxx = lsm_hash_copy(*sim_xxx);
    } else if (_vector_size(vec) == 0) {
        rc = not_found_err;
        _lsm_err_msg_set(err_msg, "%s", not_found_err_str);
        goto out;
    } else {
        rc = LSM_ERR_PLUGIN_BUG;
        _lsm_err_msg_set(err_msg,
                         "Got more than 1 data with id %" PRIu64 "in table %s",
                         sim_id, table_name);
        goto out;
    }

out:
    _db_sql_exec_vec_free(vec);
    if (rc != LSM_ERR_OK)
        *sim_xxx = NULL;
    return rc;
}

int _db_sim_pool_of_sim_id(char *err_msg, sqlite3 *db, uint64_t sim_pool_id,
                           lsm_hash **sim_pool) {
    return _db_sim_xxx_of_sim_id(err_msg, db, _DB_TABLE_POOLS_VIEW, sim_pool_id,
                                 sim_pool, LSM_ERR_NOT_FOUND_POOL,
                                 "Pool not found");
}

int _db_sim_vol_of_sim_id(char *err_msg, sqlite3 *db, uint64_t sim_vol_id,
                          lsm_hash **sim_vol) {
    return _db_sim_xxx_of_sim_id(err_msg, db, _DB_TABLE_VOLS_VIEW, sim_vol_id,
                                 sim_vol, LSM_ERR_NOT_FOUND_VOLUME,
                                 "Volume not found");
}

int _db_sim_ag_of_sim_id(char *err_msg, sqlite3 *db, uint64_t sim_ag_id,
                         lsm_hash **sim_ag) {
    return _db_sim_xxx_of_sim_id(err_msg, db, _DB_TABLE_AGS_VIEW, sim_ag_id,
                                 sim_ag, LSM_ERR_NOT_FOUND_ACCESS_GROUP,
                                 "Access group not found");
}

int _db_sim_job_of_sim_id(char *err_msg, sqlite3 *db, uint64_t sim_job_id,
                          lsm_hash **sim_job) {
    return _db_sim_xxx_of_sim_id(err_msg, db, _DB_TABLE_JOBS, sim_job_id,
                                 sim_job, LSM_ERR_NOT_FOUND_JOB,
                                 "Job not found");
}

int _db_sim_fs_of_sim_id(char *err_msg, sqlite3 *db, uint64_t sim_fs_id,
                         lsm_hash **sim_fs) {
    return _db_sim_xxx_of_sim_id(err_msg, db, _DB_TABLE_FSS_VIEW, sim_fs_id,
                                 sim_fs, LSM_ERR_NOT_FOUND_FS, "FS not found");
}

int _db_sim_fs_snap_of_sim_id(char *err_msg, sqlite3 *db,
                              uint64_t sim_fs_snap_id, lsm_hash **sim_fs_snap) {
    return _db_sim_xxx_of_sim_id(
        err_msg, db, _DB_TABLE_FS_SNAPS_VIEW, sim_fs_snap_id, sim_fs_snap,
        LSM_ERR_NOT_FOUND_FS_SS, "FS snapshot not found");
}

int _db_sim_exp_of_sim_id(char *err_msg, sqlite3 *db, uint64_t sim_exp_id,
                          lsm_hash **sim_exp) {
    return _db_sim_xxx_of_sim_id(
        err_msg, db, _DB_TABLE_NFS_EXPS_VIEW, sim_exp_id, sim_exp,
        LSM_ERR_NOT_FOUND_NFS_EXPORT, "NFS export not found");
}

int _db_sim_disk_of_sim_id(char *err_msg, sqlite3 *db, uint64_t sim_disk_id,
                           lsm_hash **sim_disk) {
    return _db_sim_xxx_of_sim_id(err_msg, db, _DB_TABLE_DISKS_VIEW, sim_disk_id,
                                 sim_disk, LSM_ERR_NOT_FOUND_DISK,
                                 "Disk not found");
}

int _db_volume_raid_create_cap_get(char *err_msg,
                                   uint32_t **supported_raid_types,
                                   uint32_t *supported_raid_type_count,
                                   uint32_t **supported_strip_sizes,
                                   uint32_t *supported_strip_size_count) {
    int rc = LSM_ERR_OK;
    ssize_t len = 0;

    assert(supported_raid_types != NULL);
    assert(supported_raid_type_count != NULL);
    assert(supported_strip_sizes != NULL);
    assert(supported_strip_size_count != NULL);

    len = sizeof(uint32_t) *
          (sizeof(_SUPPORTED_RAID_TYPES) / sizeof(_SUPPORTED_RAID_TYPES[0]));
    *supported_raid_types = (uint32_t *)malloc(len);
    _alloc_null_check(err_msg, *supported_raid_types, rc, out);
    memcpy(*supported_raid_types, _SUPPORTED_RAID_TYPES, len);
    *supported_raid_type_count = len / sizeof(uint32_t);

    len = sizeof(uint32_t) *
          (sizeof(_SUPPORTED_STRIP_SIZES) / sizeof(_SUPPORTED_STRIP_SIZES[0]));
    *supported_strip_sizes = (uint32_t *)malloc(len);
    if (*supported_strip_sizes == NULL)
        free(*supported_raid_types);
    _alloc_null_check(err_msg, *supported_strip_sizes, rc, out);
    memcpy(*supported_strip_sizes, _SUPPORTED_STRIP_SIZES, len);
    *supported_strip_size_count = len / sizeof(uint32_t);

out:
    if (rc != LSM_ERR_OK) {
        *supported_raid_types = NULL;
        *supported_raid_type_count = 0;
        *supported_strip_sizes = NULL;
        *supported_strip_size_count = 0;
    }

    return rc;
}
