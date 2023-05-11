/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * 
 * Copyright (C) 2016-2023 Red Hat, Inc.
 *
 * Author: Gris Ge <fge@redhat.com>
 */

#ifndef _SIMC_MGM_OPS_H_
#define _SIMC_MGM_OPS_H_

#include <assert.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>

int tmo_set(lsm_plugin_ptr c, uint32_t timeout, lsm_flag flags);

int tmo_get(lsm_plugin_ptr c, uint32_t *timeout, lsm_flag flags);

int capabilities(lsm_plugin_ptr c, lsm_system *sys,
                 lsm_storage_capabilities **cap, lsm_flag flags);

int job_status(lsm_plugin_ptr c, const char *job, lsm_job_status *status,
               uint8_t *percent_complete, lsm_data_type *type, void **value,
               lsm_flag flags);

int job_free(lsm_plugin_ptr c, char *job_id, lsm_flag flags);

int pool_list(lsm_plugin_ptr c, const char *search_key,
              const char *search_value, lsm_pool **pool_array[],
              uint32_t *count, lsm_flag flags);

int system_list(lsm_plugin_ptr c, lsm_system **systems[],
                uint32_t *system_count, lsm_flag flags);

int _job_create(char *err_msg, sqlite3 *db, lsm_data_type data_type,
                uint64_t sim_id, char **lsm_job_id);

bool _pool_has_enough_free_size(sqlite3 *db, uint64_t sim_pool_id,
                                uint64_t size);

#endif /* End of _SIMC_MGM_OPS_H_ */
