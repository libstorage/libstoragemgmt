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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Author: tasleson
 */

#ifndef LIBSTORAGEMGMT_INITIATORS_H
#define LIBSTORAGEMGMT_INITIATORS_H

#include "libstoragemgmt_common.h"
#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Frees the memory for one initiator record.
 * @param i     Initiator record.
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_initiator_record_free(lsm_initiator *i);

/**
 * Returns a copy of an initiator record.
 * @param i     Initiator record to be copied.
 * @return Copy of initiator or NULL on error.
 */
lsm_initiator LSM_DLL_EXPORT *lsm_initiator_record_copy(lsm_initiator *i);

/**
 * Frees the memory for each of the initiators records and then the array itself.
 * @param init  Array to free.
 * @param size  Size of array.
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_initiator_record_array_free( lsm_initiator *init[], uint32_t size);

/**
 * Returns the type of identifer returned in @see lsmInitiatorIdGet
 * @param i     lsm_initiator to inquire
 * @return      Initiator Id type, -1 if i is invalid.
 */
lsm_initiator_type LSM_DLL_EXPORT lsm_initiator_type_get(lsm_initiator *i);

/**
 * Returns the initiator id (WWN, IQN etc.)
 * Note: Returned value is only valid as long as i is valid!.
 * @param i     lsm_initiator to inquire
 * @return      Initiator id
 */
char LSM_DLL_EXPORT *lsm_initiator_id_get(lsm_initiator *i);

/**
 * Returns the user specified name associated with an initiator.
 * @param i     lsm_initiator to inquire
 * @return      Initiator name
 */
char LSM_DLL_EXPORT *lsm_initiator_name_get(lsm_initiator *i);

#ifdef  __cplusplus
}
#endif

#endif  /* LIBSTORAGEMGMT_INITIATORS_H */
