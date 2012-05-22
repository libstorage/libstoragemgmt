/*
 * Copyright (C) 2011-2012 Red Hat, Inc.
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
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
 */
void LSM_DLL_EXPORT lsmAccessGroupRecordFree( lsmAccessGroupPtr group );

/**
 * Frees the resources for an array of access groups.
 * @param ag        Array of access groups to free resources for
 * @param size      Number of elements in the array.
 */
void LSM_DLL_EXPORT lsmAccessGroupRecordFreeArray( lsmAccessGroupPtr ag[], uint32_t size );

/**
 * Copies an access group.
 * @param ag    Access group to copy
 * @return NULL on error, else copied access group.
 */
lsmAccessGroupPtr LSM_DLL_EXPORT lsmAccessGroupRecordCopy( lsmAccessGroupPtr ag );

/**
 * Returns a pointer to the id.
 * Note: Storage is allocated in the access group and will be deleted when
 * the access group gets freed.  If you need longer lifespan copy the value.
 * @param group     Access group to retrieve id for.
 * @return Null on error (not an access group), else value of group.
 */
const char LSM_DLL_EXPORT *lsmAccessGroupIdGet( lsmAccessGroupPtr group );

/**
 * Returns a pointer to the name.
 * Note: Storage is allocated in the access group and will be deleted when
 * the access group gets freed.  If you need longer lifespan copy the value.
 * @param group     Access group to retrieve id for.
 * @return Null on error (not an access group), else value of name.
 */
const char LSM_DLL_EXPORT *lsmAccessGroupNameGet( lsmAccessGroupPtr group );

/**
 * Returns a pointer to the system id.
 * Note: Storage is allocated in the access group and will be deleted when
 * the access group gets freed.  If you need longer lifespan copy the value.
 * @param group     Access group to retrieve id for.
 * @return Null on error (not an access group), else value of system id.
 */
const char LSM_DLL_EXPORT *lsmAccessGroupSystemIdGet( lsmAccessGroupPtr group );

/**
 * Returns a pointer to the initiator list.
 * Note: Storage is allocated in the access group and will be deleted when
 * the access group gets freed.  If you need longer lifespan copy the value.
 * @param group     Access group to retrieve id for.
 * @return Null on error (not an access group), else value of initiator list.
 */
lsmStringListPtr LSM_DLL_EXPORT lsmAccessGroupInitiatorIdGet( lsmAccessGroupPtr group );


#ifdef  __cplusplus
}
#endif

#endif
