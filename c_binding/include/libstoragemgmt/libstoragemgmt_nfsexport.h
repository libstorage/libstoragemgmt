/*
 * Copyright (C) 2011-2014 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or any later version.
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
 */

#ifndef LSM_NFS_EXPORT_H
#define LSM_NFS_EXPORT_H

#include "libstoragemgmt_types.h"


#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Because the nfs export functions use an unsigned data type these values
 * will be represented as (2**64-1 and 2**64-2 respectively)
 */
#define ANON_UID_GID_NA     -1
#define ANON_UID_GID_ERROR (ANON_UID_GID_NA - 1)

/**
 * Allocated memory for a NFS export record
 * @param id            Export ID  (Set to NULL when creating new export)
 * @param fs_id         File system ID that is exported
 * @param export_path   The desired path for the export (May be NULL)
 * @param auth          NFS client authentication type  (May be NULL)
 * @param root          List of hosts that have root access (May be NULL)
 * @param rw            List of hosts that have read/write access (May be NULL)
 * @param ro            List of hosts that have read only access (May be NULL)
 * @param anonuid       User id that should be mapped to anonymous
 *                      (Valid or set to ANON_UID_GID_NA).
 * @param anongid       Group id that should be mapped to anonymous
 *                      (Valid or set to ANON_UID_GID_NA)
 * @param options       String of options passed to array
 * @param plugin_data   Reserved for plug-in use
 * @return Valid export pointer, else NULL on error.
 */
lsm_nfs_export LSM_DLL_EXPORT *
    lsm_nfs_export_record_alloc(const char *id, const char *fs_id,
                                const char *export_path, const char *auth,
                                lsm_string_list *root, lsm_string_list *rw,
                                lsm_string_list *ro, uint64_t anonuid,
                                uint64_t anongid, const char *options,
                                const char *plugin_data);

/**
 * Allocated the memory for an array of NFS export records.
 * @param size          Number of elements
 * @return Allocated memory, NULL on error
 */
lsm_nfs_export LSM_DLL_EXPORT **
    lsm_nfs_export_record_array_alloc(uint32_t size);


/**
 * Frees the memory for a NFS export record.
 * @param exp
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_nfs_export_record_free(lsm_nfs_export *exp);

/**
 * Frees the memory for the NFS export array and the memory for each entry
 * @param exps          Memory to free
 * @param size          Number of entries
 * @return LSM_ERR_OK on success, else error reason.
 *  */
int LSM_DLL_EXPORT lsm_nfs_export_record_array_free(lsm_nfs_export * exps[],
                                                    uint32_t size);

/**
 * Duplicates the source and returns the copy.
 * @param source            Source record to copy
 * @return Copy of source, else NULL one error.
 */
lsm_nfs_export LSM_DLL_EXPORT *
    lsm_nfs_export_record_copy(lsm_nfs_export *source);

/**
 * Returns the ID
 * @param exp       Valid nfs export record
 * @return Pointer to ID
 */
const char LSM_DLL_EXPORT *lsm_nfs_export_id_get(lsm_nfs_export *exp);
int LSM_DLL_EXPORT lsm_nfs_export_id_set(lsm_nfs_export *exp,
                                         const char *ep);

/**
 * Returns the file system id
 * @param exp       Valid nfs export record
 * @return Pointer to file system id
 */
const char LSM_DLL_EXPORT *lsm_nfs_export_fs_id_get(lsm_nfs_export *exp);
int LSM_DLL_EXPORT lsm_nfs_export_fs_id_set(lsm_nfs_export *exp,
                                            const char *fs_id);

/**
 * Returns the export path
 * @param exp       Valid nfs export record
 * @return Pointer to export path
 */
const char LSM_DLL_EXPORT *
    lsm_nfs_export_export_path_get(lsm_nfs_export *exp);

int LSM_DLL_EXPORT lsm_nfs_export_export_path_set(lsm_nfs_export *exp,
                                                  const char *export_path);

/**
 * Returns the client authentication type
 * @param exp       Valid nfs export record
 * @return Pointer to authentication type
 */
const char LSM_DLL_EXPORT *lsm_nfs_export_auth_type_get(lsm_nfs_export
                                                        * exp);
int LSM_DLL_EXPORT lsm_nfs_export_auth_type_set(lsm_nfs_export *exp,
                                                const char *value);

/**
 * Returns the list of hosts that have root access
 * @param exp       Valid nfs export record
 * @return list of hosts.
 */
lsm_string_list LSM_DLL_EXPORT *
    lsm_nfs_export_root_get(lsm_nfs_export * exp);

int LSM_DLL_EXPORT lsm_nfs_export_root_set(lsm_nfs_export *exp,
                                           lsm_string_list *value);

/**
 * Returns the list of hosts that have read/write access to export.
 * @param exp       Valid nfs export record
 * @return list of hosts.
 */
lsm_string_list LSM_DLL_EXPORT *
    lsm_nfs_export_read_write_get(lsm_nfs_export *exp);

int LSM_DLL_EXPORT lsm_nfs_export_read_write_set(lsm_nfs_export *exp,
                                                 lsm_string_list *value);

/**
 * Returns the list of hosts that have read only access to export.
 * @param exp       Valid nfs export record
 * @return list of hosts
 */
lsm_string_list LSM_DLL_EXPORT *
    lsm_nfs_export_read_only_get(lsm_nfs_export *exp);

int LSM_DLL_EXPORT lsm_nfs_export_read_only_set(lsm_nfs_export *exp,
                                                lsm_string_list *value);

/**
 * Returns the id which is to be mapped to anonymous id
 * @param exp       Valid nfs export record
 * @return ANON_UID_GID_NA value is returned when this isn't set, else value
 * mapped to anonymous group id.  For errors ANON_UID_GID_ERROR is returned.
 */
uint64_t LSM_DLL_EXPORT lsm_nfs_export_anon_uid_get(lsm_nfs_export *exp);

int LSM_DLL_EXPORT lsm_nfs_export_anon_uid_set(lsm_nfs_export *exp,
                                               uint64_t value);

/**
 * Returns the group id which is to be mapped to anonymous group
 * @param exp       Valid nfs export record
 * @return ANON_UID_GID_NA value is returned when this isn't set, else value
 * mapped to anonymous group id.  For errors ANON_UID_GID_ERROR is returned.
 */
uint64_t LSM_DLL_EXPORT lsm_nfs_export_anon_gid_get(lsm_nfs_export *exp);

int LSM_DLL_EXPORT lsm_nfs_export_anon_gid_set(lsm_nfs_export *exp,
                                               uint64_t value);

/**
 * Returns the options for this export.
 * @param exp       Valid nfs export record
 * @return Options value, NULL if not applicable.
 */
const char LSM_DLL_EXPORT *lsm_nfs_export_options_get(lsm_nfs_export *exp);

int LSM_DLL_EXPORT lsm_nfs_export_options_set(lsm_nfs_export *exp,
                                              const char *value);

#ifdef  __cplusplus
}
#endif
#endif
