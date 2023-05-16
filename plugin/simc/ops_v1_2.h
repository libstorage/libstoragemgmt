/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2016-2023 Red Hat, Inc.
 *
 * Author: Gris Ge <fge@redhat.com>
 */

#ifndef _SIMC_OPS_V1_2_H_
#define _SIMC_OPS_V1_2_H_

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>

int volume_raid_info(lsm_plugin_ptr c, lsm_volume *volume,
                     lsm_volume_raid_type *raid_type, uint32_t *strip_size,
                     uint32_t *disk_count, uint32_t *min_io_size,
                     uint32_t *opt_io_size, lsm_flag flags);

int pool_member_info(lsm_plugin_ptr c, lsm_pool *pool,
                     lsm_volume_raid_type *raid_type,
                     lsm_pool_member_type *member_type,
                     lsm_string_list **member_ids, lsm_flag flags);

int volume_raid_create_cap_get(lsm_plugin_ptr c, lsm_system *system,
                               uint32_t **supported_raid_types,
                               uint32_t *supported_raid_type_count,
                               uint32_t **supported_strip_sizes,
                               uint32_t *supported_strip_size_count,
                               lsm_flag flags);

int volume_raid_create(lsm_plugin_ptr c, const char *name,
                       lsm_volume_raid_type raid_type, lsm_disk *disks[],
                       uint32_t disk_count, uint32_t strip_size,
                       lsm_volume **new_volume, lsm_flag flags);

#endif /* End of _SIMC_OPS_V1_2_H_ */
