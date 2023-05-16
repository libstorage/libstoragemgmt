/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2016-2023 Red Hat, Inc.
 *
 * Author: Gris Ge <fge@redhat.com>
 */

#ifndef _SIMC_NFS_OPS_H_
#define _SIMC_NFS_OPS_H_

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>

int nfs_auth_types(lsm_plugin_ptr c, lsm_string_list **types, lsm_flag flags);

int nfs_list(lsm_plugin_ptr c, const char *search_key, const char *search_value,
             lsm_nfs_export **exports[], uint32_t *count, lsm_flag flags);

int nfs_export_fs(lsm_plugin_ptr c, const char *fs_id, const char *export_path,
                  lsm_string_list *root_list, lsm_string_list *rw_list,
                  lsm_string_list *ro_list, uint64_t anon_uid,
                  uint64_t anon_gid, const char *auth_type, const char *options,
                  lsm_nfs_export **exported, lsm_flag flags);

int nfs_export_remove(lsm_plugin_ptr c, lsm_nfs_export *e, lsm_flag flags);

#endif /* End of _SIMC_NFS_OPS_H_ */
