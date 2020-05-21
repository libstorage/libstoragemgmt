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

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Because the nfs export functions use an unsigned data type these values
 * will be represented as (2**64-1 and 2**64-2 respectively)
 */
#define ANON_UID_GID_NA -1
/* ^ Deprecated, please use LSM_NFS_EXPORT_ANON_UID_GID_NA instead */
#define ANON_UID_GID_ERROR (ANON_UID_GID_NA - 1)
/* ^ Deprecated, please use LSM_NFS_EXPORT_ANON_UID_GID_ERROR instead */

#define LSM_NFS_EXPORT_ANON_UID_GID_NA    -1
#define LSM_NFS_EXPORT_ANON_UID_GID_ERROR -2

/*
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
lsm_nfs_export LSM_DLL_EXPORT *lsm_nfs_export_record_alloc(
    const char *id, const char *fs_id, const char *export_path,
    const char *auth, lsm_string_list *root, lsm_string_list *rw,
    lsm_string_list *ro, uint64_t anonuid, uint64_t anongid,
    const char *options, const char *plugin_data);

/*
 * Allocated the memory for an array of NFS export records.
 * @param size          Number of elements
 * @return Allocated memory, NULL on error
 */
lsm_nfs_export LSM_DLL_EXPORT **
lsm_nfs_export_record_array_alloc(uint32_t size);

/**
 * lsm_nfs_export_record_free - Free the lsm_nfs_export memory.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory resources for the specified lsm_nfs_export.
 *
 * @exp:
 *      The pointer of lsm_nfs_export to free.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When not a valid lsm_disk pointer.
 */
int LSM_DLL_EXPORT lsm_nfs_export_record_free(lsm_nfs_export *exp);

/**
 * lsm_nfs_export_record_array_free - Free the memory of lsm_nfs_export array.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory resources for an array of lsm_nfs_export.
 *
 * @exps:
 *      Array to release memory for.
 * @size:
 *      Number of elements.
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When not a valid lsm_nfs_export pointer.
 *
 */
int LSM_DLL_EXPORT lsm_nfs_export_record_array_free(lsm_nfs_export *exps[],
                                                    uint32_t size);

/**
 * lsm_nfs_export_record_copy - Duplicates a lsm_nfs_export record.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Duplicates a lsm_nfs_export record.
 *
 * @source:
 *      Pointer of lsm_nfs_export to duplicate.
 *
 * Return:
 *      Pointer of lsm_nfs_export. NULL on memory allocation failure. Should be
 *      freed by lsm_nfs_export_record_free().
 */
lsm_nfs_export LSM_DLL_EXPORT *
lsm_nfs_export_record_copy(lsm_nfs_export *source);

/**
 * lsm_nfs_export_id_get - Retrieves the ID of the nfs_export.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the ID for the nfs_export.
 *      Note: Address returned is valid until lsm_nfs_export gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @exp:
 *      NFS export to retrieve id for.
 *
 * Return:
 *      string. NULL if argument 'exp' is NULL or not a valid lsm_nfs_export
 *      pointer.
 */
const char LSM_DLL_EXPORT *lsm_nfs_export_id_get(lsm_nfs_export *exp);
int LSM_DLL_EXPORT lsm_nfs_export_id_set(lsm_nfs_export *exp, const char *ep);

/**
 * lsm_nfs_export_fs_id_get - Retrieves the file system ID of the nfs_export
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the file system ID for the lsm_nfs_export.
 *      Note: Address returned is valid until lsm_nfs_export gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @exp:
 *      NFS export to retrieve fs_id for.
 *
 * Return:
 *      string. NULL if argument 'exp' is NULL or not a valid lsm_nfs_export
 *      pointer.
 */
const char LSM_DLL_EXPORT *lsm_nfs_export_fs_id_get(lsm_nfs_export *exp);
int LSM_DLL_EXPORT lsm_nfs_export_fs_id_set(lsm_nfs_export *exp,
                                            const char *fs_id);

/**
 * lsm_nfs_export_export_path_get - Retrieves the export path of the nfs_export
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the export path for the lsm_nfs_export.
 *      Note: Address returned is valid until lsm_nfs_export gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @exp:
 *      NFS export to retrieve export path for.
 *
 * Return:
 *      string. NULL if argument 'exp' is NULL or not a valid lsm_nfs_export
 *      pointer.
 */
const char LSM_DLL_EXPORT *lsm_nfs_export_export_path_get(lsm_nfs_export *exp);

int LSM_DLL_EXPORT lsm_nfs_export_export_path_set(lsm_nfs_export *exp,
                                                  const char *export_path);

/**
 * lsm_nfs_export_auth_type_get - Retrieves the authentication type of the
 * nfs_export
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the authentication type for the lsm_nfs_export.
 *      Note: Address returned is valid until lsm_nfs_export gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @exp:
 *      NFS export to retrieve authentication type for.
 *
 * Return:
 *      string. NULL if argument 'exp' is NULL or not a valid lsm_nfs_export
 *      pointer.
 */
const char LSM_DLL_EXPORT *lsm_nfs_export_auth_type_get(lsm_nfs_export *exp);
int LSM_DLL_EXPORT lsm_nfs_export_auth_type_set(lsm_nfs_export *exp,
                                                const char *value);

/**
 * lsm_nfs_export_root_get - Retrieves the host list with root access of the
 * nfs_export
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the host list with root access for the lsm_nfs_export.
 *      Note: Address returned is valid until lsm_nfs_export gets freed, copy
 *      return value if you need longer scope. Do not free returned
 *      lsm_string_list.
 *
 * @exp:
 *      NFS export to retrieve host list for.
 *
 * Return:
 *      lsm_string_list. NULL if argument 'exp' is NULL or not a valid
 *      lsm_nfs_export pointer.
 */
lsm_string_list LSM_DLL_EXPORT *lsm_nfs_export_root_get(lsm_nfs_export *exp);

int LSM_DLL_EXPORT lsm_nfs_export_root_set(lsm_nfs_export *exp,
                                           lsm_string_list *value);

/**
 * lsm_nfs_export_read_write_get - Retrieves the host list with read and write
 * of the nfs_export
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the host list with read and write for the lsm_nfs_export.
 *      Note: Address returned is valid until lsm_nfs_export gets freed, copy
 *      return value if you need longer scope. Do not free returned
 *      lsm_string_list.
 *
 * @exp:
 *      NFS export to retrieve host list for.
 *
 * Return:
 *      lsm_string_list. NULL if argument 'exp' is NULL or not a valid
 *      lsm_nfs_export pointer.
 */
lsm_string_list LSM_DLL_EXPORT *
lsm_nfs_export_read_write_get(lsm_nfs_export *exp);

int LSM_DLL_EXPORT lsm_nfs_export_read_write_set(lsm_nfs_export *exp,
                                                 lsm_string_list *value);

/**
 * lsm_nfs_export_read_only_get - Retrieves the host list with read only
 * of the nfs_export
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the host list with read only for the lsm_nfs_export.
 *      Note: Address returned is valid until lsm_nfs_export gets freed, copy
 *      return value if you need longer scope. Do not free returned
 *      lsm_string_list.
 *
 * @exp:
 *      NFS export to retrieve host list for.
 *
 * Return:
 *      lsm_string_list. NULL if argument 'exp' is NULL or not a valid
 *      lsm_nfs_export pointer.
 */
lsm_string_list LSM_DLL_EXPORT *
lsm_nfs_export_read_only_get(lsm_nfs_export *exp);

int LSM_DLL_EXPORT lsm_nfs_export_read_only_set(lsm_nfs_export *exp,
                                                lsm_string_list *value);

/**
 * lsm_nfs_export_anon_uid_get -  Retrieves the user id mapped for anonymous
 * id for the NFS export.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the user ID for anonymous ID of the lsm_nfs_export.
 *
 * @exp:
 *      NFS export to retrieve anonymous uid for.
 *
 * Return:
 *      uint64_t. LSM_NFS_EXPORT_ANON_UID_GID_NA if this NFS export does not
 *      have setting for anonymous id.
 *      LSM_NFS_EXPORT_ANON_UID_GID_ERROR if exp is NULL or invalid
 *      lsm_nfs_export pointer.
 */
uint64_t LSM_DLL_EXPORT lsm_nfs_export_anon_uid_get(lsm_nfs_export *exp);

int LSM_DLL_EXPORT lsm_nfs_export_anon_uid_set(lsm_nfs_export *exp,
                                               uint64_t value);

/**
 * lsm_nfs_export_anon_gid_get -  Retrieves the group id mapped for anonymous
 * id for the NFS export.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the group ID for anonymous ID of the lsm_nfs_export.
 *
 * @exp:
 *      NFS export to retrieve anonymous gid for.
 *
 * Return:
 *      uint64_t. LSM_NFS_EXPORT_ANON_UID_GID_NA if this NFS export does not
 *      have setting for anonymous id.
 *      LSM_NFS_EXPORT_ANON_UID_GID_ERROR if exp is NULL or invalid
 *      lsm_nfs_export pointer.
 */
uint64_t LSM_DLL_EXPORT lsm_nfs_export_anon_gid_get(lsm_nfs_export *exp);

int LSM_DLL_EXPORT lsm_nfs_export_anon_gid_set(lsm_nfs_export *exp,
                                               uint64_t value);

/**
 * lsm_nfs_export_options_get - Retrieves the options of the nfs_export
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the options for the lsm_nfs_export.
 *      Note: Address returned is valid until lsm_nfs_export gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @exp:
 *      NFS export to retrieve options for.
 *
 * Return:
 *      string. NULL if argument 'exp' is NULL or not a valid lsm_nfs_export
 *      pointer.
 */
const char LSM_DLL_EXPORT *lsm_nfs_export_options_get(lsm_nfs_export *exp);

int LSM_DLL_EXPORT lsm_nfs_export_options_set(lsm_nfs_export *exp,
                                              const char *value);

#ifdef __cplusplus
}
#endif
#endif
