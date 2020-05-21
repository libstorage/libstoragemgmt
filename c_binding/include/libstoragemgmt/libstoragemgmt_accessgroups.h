/*
 * Copyright (C) 2011-2017 Red Hat, Inc.
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

#ifndef LSM_ACCESS_GROUP_H
#define LSM_ACCESS_GROUP_H

#include "libstoragemgmt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * lsm_access_group_record_free - Frees the memory for access group.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory for an individual lsm_access_group.
 *
 * @group:
 *      lsm_access_group to release memory for.
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_access_group
 *              pointer.
 */
int LSM_DLL_EXPORT lsm_access_group_record_free(lsm_access_group *group);

/**
 * lsm_access_group_record_array_free - Frees the memory of access group array.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory for each of the access groups and then the access group
 *      array itself.
 *
 * @ag:
 *      Array to release memory for.
 * @size:
 *      Number of elements.
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_access_group
 *              pointer.
 */
int LSM_DLL_EXPORT lsm_access_group_record_array_free(lsm_access_group *ag[],
                                                      uint32_t size);

/**
 * lsm_access group_record_copy - Duplicates a access group record.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Duplicates a lsm_access_group record.
 *
 * @ag:
 *      Pointer of lsm_access_group to duplicate.
 *
 * Return:
 *      Pointer of lsm_access_group. NULL on memory allocation failure or
 *      invalid lsm_access_group pointer. Should be freed by
 *      lsm_access_group_record_free().
 */
lsm_access_group LSM_DLL_EXPORT *
lsm_access_group_record_copy(lsm_access_group *ag);

/**
 * lsm_access_group_id_get - Retrieves the ID for the access group.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the ID for the access group.
 *      Note: Address returned is valid until lsm_access_group gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @group:
 *      Access group to retrieve ID for.
 *
 * Return:
 *      string. NULL if argument 'group' is NULL or not a valid lsm_access_group
 *      pointer.
 */
const char LSM_DLL_EXPORT *lsm_access_group_id_get(lsm_access_group *group);

/**
 * lsm_access_group_name_get - Retrieves the name for the access group.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the name for the access group.
 *      Note: Address returned is valid until lsm_access_group gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @group:
 *      Access group to retrieve name for.
 *
 * Return:
 *      string. NULL if argument 'group' is NULL or not a valid lsm_access_group
 *      pointer.
 */
const char LSM_DLL_EXPORT *lsm_access_group_name_get(lsm_access_group *group);

/**
 * lsm_access_group_system_id_get - Retrieves the system ID of the access group.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the system id for the specified access group.
 *      Note: Address returned is valid until lsm_access_group gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @group:
 *      Access group to retrieve system ID for.
 *
 * Return:
 *      string. NULL if argument 'group' is NULL or not a valid lsm_access_group
 *      pointer.
 */
const char LSM_DLL_EXPORT *
lsm_access_group_system_id_get(lsm_access_group *group);

/**
 * lsm_access_group_initiator_id_get - Retrieves the initiators of access group.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves the initiators for the specified access group.
 *      Note: Address returned is valid until lsm_access_group gets freed, copy
 *      return value if you need longer scope. Do not free returned
 *      lsm_string_list.
 *
 * @group:
 *      Access group to retrieve initiators for.
 *
 * Return:
 *      lsm_string_list pointer. NULL if argument 'group' is NULL or not a valid
 *      lsm_access_group pointer.
 */
lsm_string_list LSM_DLL_EXPORT *
lsm_access_group_initiator_id_get(lsm_access_group *group);

/**
 * lsm_access_group_init_type_get - Retrieves the initiator type for specified
 * access group.
 *
 * Version:
 *      1.7
 *
 * Description:
 *      Retrieves the initiator type for the specified access group.
 *
 * @group:
 *      Access group to retrieve type of initiators present.
 *
 * Return: lsm_access_group_init_type
 *
 */
lsm_access_group_init_type LSM_DLL_EXPORT
lsm_access_group_init_type_get(lsm_access_group *group);

#ifdef __cplusplus
}
#endif
#endif
