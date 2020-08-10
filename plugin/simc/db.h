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

#ifndef _SIMC_DB_H_
#define _SIMC_DB_H

#include <sqlite3.h>
#include <stdint.h>

#include "utils.h"
#include "vector.h"

#define _DB_VERSION "4.1"

#define _SYS_ID "sim-01"

#define _BLOCK_SIZE 512

#define _DB_DEFAULT_WRITE_CACHE_POLICY "3"
/* ^ LSM_VOLUME_WRITE_CACHE_POLICY_AUTO */

#define _DB_DEFAULT_READ_CACHE_POLICY "2"
/* ^ LSM_VOLUME_READ_CACHE_POLICY_ENABLED */

#define _DB_DEFAULT_PHYSICAL_DISK_CACHE "3"
/* ^ LSM_VOLUME_PHYSICAL_DISK_CACHE_DISABLED */

#define _DB_DEFAULT_JOB_DURATION "1"
/* ^ 1 seconds for ASYNC job */

#define _DB_TABLE_SYS                "systems"
#define _DB_TABLE_POOLS_VIEW         "pools_view"
#define _DB_TABLE_POOLS              "pools"
#define _DB_TABLE_VOLS_VIEW          "volumes_view"
#define _DB_TABLE_VOLS               "volumes"
#define _DB_TABLE_DISKS_VIEW         "disks_view"
#define _DB_TABLE_DISKS              "disks"
#define _DB_TABLE_AGS_VIEW           "ags_view"
#define _DB_TABLE_AGS                "ags"
#define _DB_TABLE_JOBS               "jobs"
#define _DB_TABLE_VOL_MASKS          "vol_masks"
#define _DB_TABLE_VOLS_VIEW_BY_AG    "volumes_by_ag_view"
#define _DB_TABLE_AGS_VIEW_BY_VOL    "ags_by_vol_view"
#define _DB_TABLE_VOL_REPS           "vol_reps"
#define _DB_TABLE_INITS              "inits"
#define _DB_TABLE_TGTS               "tgts"
#define _DB_TABLE_TGTS_VIEW          "tgts_view"
#define _DB_TABLE_FSS                "fss"
#define _DB_TABLE_FSS_VIEW           "fss_view"
#define _DB_TABLE_FS_CLONES          "fs_clones"
#define _DB_TABLE_FS_SNAPS           "fs_snaps"
#define _DB_TABLE_FS_SNAPS_VIEW      "fs_snaps_view"
#define _DB_TABLE_NFS_EXPS           "exps"
#define _DB_TABLE_NFS_EXPS_VIEW      "exps_view"
#define _DB_TABLE_NFS_EXP_ROOT_HOSTS "exp_root_hosts"
#define _DB_TABLE_NFS_EXP_RW_HOSTS   "exp_rw_hosts"
#define _DB_TABLE_NFS_EXP_RO_HOSTS   "exp_ro_hosts"
#define _DB_TABLE_BATS               "batteries"
#define _DB_TABLE_BATS_VIEW          "bats_view"

#define _DB_SIM_ID_NONE 0

#define _DB_LIST_SPLITTER      "#"
#define _DB_VERSION_STR_PREFIX "LSM_SIMULATOR_DATA"
#define _DB_ID_FMT_LEN         5
#define _DB_ID_FMT_LEN_STR     "5"
#define _DB_ID_PADDING         "00000"

/*
 * Create db_file is not exist as 0666 mode, initialize database tables and
 * fill in with initial data.
 */
int _db_init(char *err_msg, sqlite3 **db, const char *db_file,
             uint32_t timeout);

int _db_sql_exec(char *err_msg, sqlite3 *db, const char *cmd,
                 struct _vector **vec);

void _db_sql_exec_vec_free(struct _vector *vec);

void _db_close(sqlite3 *db);

int _db_sql_trans_begin(char *err_msg, sqlite3 *db);
int _db_sql_trans_commit(char *err_msg, sqlite3 *db);
void _db_sql_trans_rollback(sqlite3 *db);

/*
 * The ... va_arg should be NULL terminated strings.
 */
int _db_data_add(char *err_msg, sqlite3 *db, const char *table_name, ...);

int _db_data_update(char *err_msg, sqlite3 *db, const char *table_name,
                    uint64_t data_id, const char *key, const char *value);

int _db_data_delete(char *err_msg, sqlite3 *db, const char *table_name,
                    uint64_t data_id);

int _db_data_delete_condition(char *err_msg, sqlite3 *db,
                              const char *table_name, const char *condition);

const char *_db_lsm_id_to_sim_id_str(const char *lsm_id);

uint64_t _db_lsm_id_to_sim_id(const char *lsm_id);

/*
 * buff: char[_BUFF_SIZE]
 */
const char *_db_sim_id_to_lsm_id(char *buff, const char *prefix,
                                 uint64_t sim_id);

/*
 * return 0 for error.
 */
uint64_t _db_last_rowid(sqlite3 *db);

uint64_t _db_blk_size_rounding(uint64_t size_bytes);

int _db_sim_pool_of_sim_id(char *err_msg, sqlite3 *db, uint64_t sim_pool_id,
                           lsm_hash **sim_pool);

int _db_sim_vol_of_sim_id(char *err_msg, sqlite3 *db, uint64_t sim_vol_id,
                          lsm_hash **sim_vol);

int _db_sim_ag_of_sim_id(char *err_msg, sqlite3 *db, uint64_t sim_ag_id,
                         lsm_hash **sim_ag);

int _db_sim_job_of_sim_id(char *err_msg, sqlite3 *db, uint64_t sim_job_id,
                          lsm_hash **sim_job);

int _db_sim_fs_of_sim_id(char *err_msg, sqlite3 *db, uint64_t sim_fs_id,
                         lsm_hash **sim_fs);

int _db_sim_fs_snap_of_sim_id(char *err_msg, sqlite3 *db,
                              uint64_t sim_fs_snap_id, lsm_hash **sim_fs_snap);

int _db_sim_exp_of_sim_id(char *err_msg, sqlite3 *db, uint64_t sim_exp_id,
                          lsm_hash **sim_exp);

int _db_sim_disk_of_sim_id(char *err_msg, sqlite3 *db, uint64_t sim_disk_id,
                           lsm_hash **sim_disk);

/*
 * This function does not check whether disk is free!
 */
int _db_pool_create_from_disk(char *err_msg, sqlite3 *db, const char *name,
                              uint64_t *sim_disk_ids,
                              uint32_t sim_disk_id_count,
                              lsm_volume_raid_type raid_type,
                              uint64_t element_type,
                              uint64_t unsupported_actions,
                              uint64_t *sim_pool_id, uint32_t strip_size);

int _db_volume_raid_create_cap_get(char *err_msg,
                                   uint32_t **supported_raid_types,
                                   uint32_t *supported_raid_type_count,
                                   uint32_t **supported_strip_sizes,
                                   uint32_t *supported_strip_size_count);

lsm_string_list *_db_str_to_list(const char *list_str);

#endif /* End of _SIMC_DB_H_ */
