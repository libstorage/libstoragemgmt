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

#ifndef LIBSTORAGEMGMT_TARGET_PORT_H
#define LIBSTORAGEMGMT_TARGET_PORT_H

#include "libstoragemgmt_common.h"

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Duplicated a target port record.
 * NOTE: Make sure to free resources with a call to lsm_target_port_record_free
 * @param tp     Record to duplicate
 * @return NULL on memory allocation failure, else duplicated record.
 */
lsm_target_port LSM_DLL_EXPORT *lsm_target_port_copy(lsm_target_port *tp);


/**
 * Frees the resources for a lsm_system
 * @param tp        Record to release
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 */
int LSM_DLL_EXPORT lsm_target_port_record_free(lsm_target_port *tp);

/**
 * Frees the resources for an array for lsm_target_port
 * @param tp        Array to release memory for
 * @param size      Number of elements.
 * @return Error code as enumerated by \ref lsm_error_number.
 * @retval LSM_ERR_OK on success.
 *  */
int LSM_DLL_EXPORT lsm_target_port_record_array_free(lsm_target_port *
                                                     tp[], uint32_t size);

/**
 * Returns the ID of the target port
 * @param tp    lsm_target_port record
 * @return ID, NULL on error
 */
const char LSM_DLL_EXPORT *lsm_target_port_id_get(lsm_target_port *tp);

/**
 * Returns the type of target port
 * @param tp        lsm_target_port record
 * @return enumerated value
 */
lsm_target_port_type LSM_DLL_EXPORT
    lsm_target_port_type_get(lsm_target_port *tp);

/**
 * Returns the service address
 * @param tp    lsm_target_port record
 * @return Service address, NULL on error
 */
const char LSM_DLL_EXPORT *
    lsm_target_port_service_address_get(lsm_target_port *tp);

/**
 * Returns the network address
 * @param tp    lsm_target_port record
 * @return Network address, NULL on error
 */
const char LSM_DLL_EXPORT *
    lsm_target_port_network_address_get(lsm_target_port *tp);

/**
 * Returns the physical address
 * @param tp    lsm_target_port record
 * @return Physical address, NULL on error
 */
const char LSM_DLL_EXPORT *
    lsm_target_port_physical_address_get(lsm_target_port *tp);

/**
 * Returns the physical name
 * @param tp    lsm_target_port record
 * @return Physical name, NULL on error
 */
const char LSM_DLL_EXPORT *
    lsm_target_port_physical_name_get(lsm_target_port *tp);

/**
 * Returns the system_id
 * @param tp    lsm_target_port record
 * @return System id, NULL on error
 */
const char LSM_DLL_EXPORT *
    lsm_target_port_system_id_get(lsm_target_port *tp);

#ifdef  __cplusplus
}
#endif
#endif
