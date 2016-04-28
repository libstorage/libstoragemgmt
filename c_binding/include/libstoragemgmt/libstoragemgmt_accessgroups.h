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


#ifndef LSM_ACCESS_GROUP_H
#define LSM_ACCESS_GROUP_H

#include "libstoragemgmt_types.h"


#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Frees the resources for an access group.
 * @param group     Group to free
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_access_group_record_free(lsm_access_group * group);

/**
 * Frees the resources for an array of access groups.
 * @param ag        Array of access groups to free resources for
 * @param size      Number of elements in the array.
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_access_group_record_array_free(lsm_access_group *ag[],
                                                      uint32_t size);

/**
 * Copies an access group.
 * @param ag    Access group to copy
 * @return NULL on error, else copied access group.
 */
lsm_access_group LSM_DLL_EXPORT *
    lsm_access_group_record_copy(lsm_access_group * ag);

/**
 * Returns a pointer to the id.
 * Note: Storage is allocated in the access group and will be deleted when
 * the access group gets freed.  If you need longer lifespan copy the value.
 * @param group     Access group to retrieve id for.
 * @return Null on error (not an access group), else value of group.
 */
const char LSM_DLL_EXPORT *lsm_access_group_id_get(lsm_access_group *group);

/**
 * Returns a pointer to the name.
 * Note: Storage is allocated in the access group and will be deleted when
 * the access group gets freed.  If you need longer lifespan copy the value.
 * @param group     Access group to retrieve id for.
 * @return Null on error (not an access group), else value of name.
 */
const char LSM_DLL_EXPORT *lsm_access_group_name_get(lsm_access_group *group);

/**
 * Returns a pointer to the system id.
 * Note: Storage is allocated in the access group and will be deleted when
 * the access group gets freed.  If you need longer lifespan copy the value.
 * @param group     Access group to retrieve id for.
 * @return Null on error (not an access group), else value of system id.
 */
const char LSM_DLL_EXPORT *
    lsm_access_group_system_id_get(lsm_access_group *group);

/**
 * Returns a pointer to the initiator list.
 * Note: Storage is allocated in the access group and will be deleted when
 * the access group gets freed.  If you need longer lifespan copy the value.
 * @param group     Access group to retrieve id for.
 * @return Null on error (not an access group), else value of initiator list.
 */
lsm_string_list LSM_DLL_EXPORT *
    lsm_access_group_initiator_id_get(lsm_access_group * group);


#ifdef  __cplusplus
}
#endif
#endif
