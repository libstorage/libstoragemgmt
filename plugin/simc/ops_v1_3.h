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

#ifndef _SIMC_OPS_V1_3_H_
#define _SIMC_OPS_V1_3_H_

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>

int volume_ident_led_on(lsm_plugin_ptr c, lsm_volume *volume, lsm_flag flags);

int volume_ident_led_off(lsm_plugin_ptr c, lsm_volume *volume, lsm_flag flags);

int system_read_cache_pct_update(lsm_plugin_ptr c, lsm_system *system,
                                 uint32_t read_pct, lsm_flag flags);

int battery_list(lsm_plugin_ptr c, const char *search_key,
                 const char *search_val, lsm_battery **bs[], uint32_t *count,
                 lsm_flag flags);

int volume_cache_info(lsm_plugin_ptr c, lsm_volume *volume,
                      uint32_t *write_cache_policy,
                      uint32_t *write_cache_status, uint32_t *read_cache_policy,
                      uint32_t *read_cache_status,
                      uint32_t *physical_disk_cache, lsm_flag flags);

int volume_physical_disk_cache_update(lsm_plugin_ptr c, lsm_volume *volume,
                                      uint32_t pdc, lsm_flag flags);

int volume_write_cache_policy_update(lsm_plugin_ptr c, lsm_volume *volume,
                                     uint32_t wcp, lsm_flag flags);

int volume_read_cache_policy_update(lsm_plugin_ptr c, lsm_volume *volume,
                                    uint32_t rcp, lsm_flag flags);

#endif /* End of _SIMC_OPS_V1_3_H_ */
