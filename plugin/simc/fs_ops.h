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

#ifndef _SIMC_FS_OPS_H_
#define _SIMC_FS_OPS_H_

#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int fs_list(lsm_plugin_ptr c, const char *search_key, const char *search_value,
            lsm_fs **fs[], uint32_t *fs_count, lsm_flag flags);

int fs_create(lsm_plugin_ptr c, lsm_pool *pool, const char *name,
              uint64_t size_bytes, lsm_fs **fs, char **job, lsm_flag flags);

int fs_delete(lsm_plugin_ptr c, lsm_fs *fs, char **job, lsm_flag flags);

int fs_clone(lsm_plugin_ptr c, lsm_fs *src_fs, const char *dest_fs_name,
             lsm_fs **cloned_fs, lsm_fs_ss *optional_snapshot, char **job,
             lsm_flag flags);

int fs_child_dependency(lsm_plugin_ptr c, lsm_fs *fs, lsm_string_list *files,
                        uint8_t *yes);

int fs_child_dependency_rm(lsm_plugin_ptr c, lsm_fs *fs, lsm_string_list *files,
                           char **job, lsm_flag flags);

int fs_resize(lsm_plugin_ptr c, lsm_fs *fs, uint64_t new_size, lsm_fs **rfs,
              char **job, lsm_flag flags);

int fs_file_clone(lsm_plugin_ptr c, lsm_fs *fs, const char *src_file_name,
                  const char *dest_file_name, lsm_fs_ss *snapshot, char **job,
                  lsm_flag flags);

int fs_snapshot_list(lsm_plugin_ptr c, lsm_fs *fs, lsm_fs_ss **ss[],
                     uint32_t *ss_count, lsm_flag flags);

int fs_snapshot_create(lsm_plugin_ptr c, lsm_fs *fs, const char *name,
                       lsm_fs_ss **snapshot, char **job, lsm_flag flags);

int fs_snapshot_delete(lsm_plugin_ptr c, lsm_fs *fs, lsm_fs_ss *ss, char **job,
                       lsm_flag flags);

int fs_snapshot_restore(lsm_plugin_ptr c, lsm_fs *fs, lsm_fs_ss *ss,
                        lsm_string_list *files, lsm_string_list *restore_files,
                        int all_files, char **job, lsm_flag flags);

lsm_fs *_sim_fs_to_lsm(char *err_msg, lsm_hash *sim_fs);
lsm_fs_ss *_sim_fs_snap_to_lsm(char *err_msg, lsm_hash *sim_fs_snap);

#endif /* End of _SIMC_FS_OPS_H_ */
