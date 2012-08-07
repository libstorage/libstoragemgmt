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
#ifndef LIBSTORAGEMGMT_SYSTEMS_H
#define LIBSTORAGEMGMT_SYSTEMS_H

#include "libstoragemgmt_common.h"
#include "libstoragemgmt_types.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Duplicated a system record.
 * NOTE: Make sure to free resources with a call to lsmSystemRecordFree
 * @param s     Record to duplicate
 * @return NULL on memory allocation failure, else duplicated record.
 */
lsmSystemPtr lsmSystemRecordCopy(lsmSystemPtr s);


/**
 * Frees the resources for a lsmSystemPtr
 * @param s Record to release
 */
void LSM_DLL_EXPORT lsmSystemRecordFree(lsmSystemPtr s);

/**
 * Frees the resources for an array for lsmSystemPtrs
 * @param s     Array to release memory for
 * @param size  Number of elements.
 */
void LSM_DLL_EXPORT lsmSystemRecordFreeArray( lsmSystemPtr s[], uint32_t size );

/**
 * Retrieve the Id for the system.
 * Note: Address returned is valid until lsmSystemPtr gets freed, copy return
 * value if you need longer scope.  Do not free returned string.
 * @param s System to retrieve id for.
 * @return NULL on error, else value.
 */
const char LSM_DLL_EXPORT *lsmSystemIdGet(lsmSystemPtr s);

/**
 * Retrieve the Id for the system.
 * Note: Address returned is valid until lsmSystemPtr gets freed, copy return
 * value if you need longer scope.  Do not free returned string.
 * @param s System to retrieve id for.
 * @return NULL on error, else value.
 */
const char LSM_DLL_EXPORT *lsmSystemNameGet(lsmSystemPtr s);

/**
 * Retrieve the status for the system.
 * @param s     System to retrieve status for
 * @return System status which is a bit sensitive field, returns UINT32_MAX on
 * bad system pointer.
 */
uint32_t lsmSystemStatusGet(lsmSystemPtr s);

#ifdef  __cplusplus
}
#endif

#endif