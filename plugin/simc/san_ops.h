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

#ifndef _SIMC_SAN_OPS_H_
#define _SIMC_SAN_OPS_H_

#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int volume_list(lsm_plugin_ptr c, const char *search_key,
                const char *search_val, lsm_volume **vol_array[],
                uint32_t *count, lsm_flag flags);

int disk_list(lsm_plugin_ptr c, const char *search_key,
              const char *search_value, lsm_disk **disk_array[],
              uint32_t *count, lsm_flag flags);

int volume_create(lsm_plugin_ptr c, lsm_pool *pool, const char *volume_name,
                  uint64_t size, lsm_volume_provision_type provisioning,
                  lsm_volume **new_volume, char **job, lsm_flag flags);

int volume_replicate(lsm_plugin_ptr c, lsm_pool *pool,
                     lsm_replication_type rep_type, lsm_volume *volume_src,
                     const char *name, lsm_volume **new_replicant, char **job,
                     lsm_flag flags);

int volume_replicate_range(lsm_plugin_ptr c, lsm_replication_type rep_type,
                           lsm_volume *src_vol, lsm_volume *dst_vol,
                           lsm_block_range **ranges, uint32_t num_ranges,
                           char **job, lsm_flag flags);

int volume_replicate_range_block_size(lsm_plugin_ptr c, lsm_system *system,
                                      uint32_t *bs, lsm_flag flags);

int volume_resize(lsm_plugin_ptr c, lsm_volume *volume, uint64_t new_size,
                  lsm_volume **resized_volume, char **job, lsm_flag flags);

int volume_enable(lsm_plugin_ptr c, lsm_volume *v, lsm_flag flags);

int volume_disable(lsm_plugin_ptr c, lsm_volume *v, lsm_flag flags);

int volume_delete(lsm_plugin_ptr c, lsm_volume *volume, char **job,
                  lsm_flag flags);

int access_group_delete(lsm_plugin_ptr c, lsm_access_group *group,
                        lsm_flag flags);

int iscsi_chap_auth(lsm_plugin_ptr c, const char *init_id, const char *in_user,
                    const char *in_password, const char *out_user,
                    const char *out_password, lsm_flag flags);

int access_group_list(lsm_plugin_ptr c, const char *search_key,
                      const char *search_value, lsm_access_group **groups[],
                      uint32_t *count, lsm_flag flags);

int access_group_create(lsm_plugin_ptr c, const char *name,
                        const char *initiator_id,
                        lsm_access_group_init_type init_type,
                        lsm_system *system, lsm_access_group **access_group,
                        lsm_flag flags);

int access_group_initiator_add(lsm_plugin_ptr c, lsm_access_group *access_group,
                               const char *initiator_id,
                               lsm_access_group_init_type init_type,
                               lsm_access_group **updated_access_group,
                               lsm_flag flags);

int access_group_initiator_delete(lsm_plugin_ptr c,
                                  lsm_access_group *access_group,
                                  const char *initiator_id,
                                  lsm_access_group_init_type id_type,
                                  lsm_access_group **updated_access_group,
                                  lsm_flag flags);

int volume_mask(lsm_plugin_ptr c, lsm_access_group *group, lsm_volume *volume,
                lsm_flag flags);

int volume_unmask(lsm_plugin_ptr c, lsm_access_group *group, lsm_volume *volume,
                  lsm_flag flags);

int volumes_accessible_by_access_group(lsm_plugin_ptr c,
                                       lsm_access_group *group,
                                       lsm_volume **volumes[], uint32_t *count,
                                       lsm_flag flags);

int access_groups_granted_to_volume(lsm_plugin_ptr c, lsm_volume *volume,
                                    lsm_access_group **groups[],
                                    uint32_t *group_count, lsm_flag flags);

int vol_child_depends(lsm_plugin_ptr c, lsm_volume *volume, uint8_t *yes,
                      lsm_flag flags);

int vol_child_depends_rm(lsm_plugin_ptr c, lsm_volume *volume, char **job,
                         lsm_flag flags);

int target_port_list(lsm_plugin_ptr c, const char *search_key,
                     const char *search_value,
                     lsm_target_port **target_port_array[], uint32_t *count,
                     lsm_flag flags);

lsm_volume *_sim_vol_to_lsm(char *err_msg, lsm_hash *sim_vol);

lsm_access_group *_sim_ag_to_lsm(char *err_msg, lsm_hash *sim_ag);

int _volume_create_internal(char *err_msg, sqlite3 *db, const char *name,
                            uint64_t size, uint64_t sim_pool_id);

#endif /* End of _SIMC_SAN_OPS_H_ */
