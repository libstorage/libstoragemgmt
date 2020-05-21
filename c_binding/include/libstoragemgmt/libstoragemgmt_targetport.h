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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * lsm_target_port_copy - Duplicates a lsm_target_port record.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Duplicates a lsm_target_port record.
 *
 * @tp:
 *      Pointer of lsm_target_port to duplicate.
 *
 * Return:
 *      Pointer of lsm_target_port. NULL on memory allocation failure or invalid
 *      lsm_target_port pointer. Should be freed by
 *      lsm_target_port_record_free().
 */
lsm_target_port LSM_DLL_EXPORT *lsm_target_port_copy(lsm_target_port *tp);

/**
 * lsm_target_port_record_free - Frees the memory for a lsm_target_port
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory for a lsm_target_port.
 *
 * @tp:
 *      lsm_target_port to release memory for.
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When not a valid lsm_target_port pointer.
 */
int LSM_DLL_EXPORT lsm_target_port_record_free(lsm_target_port *tp);

/**
 * lsm_target_port_record_array_free - Frees the memory of lsm_target_port array
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the memory for each of lsm_target_port and then the target_port
 *      array itself.
 *
 * @tp:
 *      Array to release memory for.
 * @size:
 *      Number of elements.
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When not a valid lsm_target_port pointer.
 *  */
int LSM_DLL_EXPORT lsm_target_port_record_array_free(lsm_target_port *tp[],
                                                     uint32_t size);

/**
 * lsm_target_port_id_get - Retrieve the ID for the target_port.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieve the ID for the target_port.
 *      Note: Address returned is valid until lsm_target_port gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @tp:
 *      Pointer of lsm_target_port to retrieve ID for.
 *
 * Return:
 *      string. NULL if argument 'tp' is NULL or not a valid lsm_target_port
 *      pointer.
 */
const char LSM_DLL_EXPORT *lsm_target_port_id_get(lsm_target_port *tp);

/**
 * lsm_target_port_type_get - Retrieve target port type
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieve the type(FC/iSCSI/etc) of target port.
 *
 * @tp:
 *      Target port to retrieve type for.
 *
 * Return:
 *      lsm_target_port_type. Valid values are:
 *          * LSM_TARGET_PORT_TYPE_FC
 *              FC
 *          * LSM_TARGET_PORT_TYPE_FCOE
 *              FCoE
 *          * LSM_TARGET_PORT_TYPE_ISCSI
 *              iSCSI
 *          * LSM_TARGET_PORT_TYPE_OTHER
 *              Vendor specific.
 */
lsm_target_port_type LSM_DLL_EXPORT
lsm_target_port_type_get(lsm_target_port *tp);

/**
 * lsm_target_port_service_address_get - Retrieve the service address for the
 * target_port
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieve the service address for the target_port.
 *      Service address is lower case string which is used by upper layer
 *      like FC and iSCSI:
 *          * FC/FCoE:
 *              WWPN (split with : every two digits)
 *          * iSCSI:
 *              iSCSI IQN
 *      Note: Address returned is valid until lsm_target_port gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @tp:
 *      Pointer of lsm_target_port to retrieve service address for.
 *
 * Return:
 *      string. NULL if argument 'tp' is NULL or not a valid lsm_target_port
 *      pointer.
 */
const char LSM_DLL_EXPORT *
lsm_target_port_service_address_get(lsm_target_port *tp);

/**
 * lsm_target_port_network_address_get - Retrieve the network address for the
 * target_port
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieve the network address for the target_port.
 *      Network address is lower case string which is used by network layer
 *      like FC and TCP/IP:
 *          * FC/FCoE:
 *              WWPN (split with : every two digits)
 *          * iSCSI:
 *              <ipv4_address>:<tcp_port>
 *              [<ipv6_address>]:<tcp_port>
 *      Note: Address returned is valid until lsm_target_port gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @tp:
 *      Pointer of lsm_target_port to retrieve network address for.
 *
 * Return:
 *      string. NULL if argument 'tp' is NULL or not a valid lsm_target_port
 *      pointer.
 */
const char LSM_DLL_EXPORT *
lsm_target_port_network_address_get(lsm_target_port *tp);

/**
 * lsm_target_port_physical_address_get - Retrieve the physical address for the
 * target_port
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieve the physical address for the target_port.
 *      Physical address is lower case string which is used by physical layer
 *      like FC-0 and MAC:
 *          * FC/FCoE:
 *              WWPN (split with : every two digits)
 *          * iSCSI:
 *              MAC address (split with : every two digits)
 *      Note: Address returned is valid until lsm_target_port gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @tp:
 *      Pointer of lsm_target_port to retrieve physical address for.
 *
 * Return:
 *      string. NULL if argument 'tp' is NULL or not a valid lsm_target_port
 *      pointer.
 */
const char LSM_DLL_EXPORT *
lsm_target_port_physical_address_get(lsm_target_port *tp);

/**
 * lsm_target_port_physical_name_get - Retrieve the physical name for the
 * target_port
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieve the physical name for the target_port.
 *      Administrator could use this name to locate the port on the storage
 *      system.
 *      Note: Address returned is valid until lsm_target_port gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @tp:
 *      Pointer of lsm_target_port to retrieve physical name for.
 *
 * Return:
 *      string. NULL if argument 'tp' is NULL or not a valid lsm_target_port
 *      pointer.
 */
const char LSM_DLL_EXPORT *
lsm_target_port_physical_name_get(lsm_target_port *tp);

/**
 * lsm_target_port_system_id_get - Retrieve the system ID for the target_port.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieve the system id for the specified target_port.
 *      Note: Address returned is valid until lsm_target_port gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @tp:
 *      Target port to retrieve system ID for.
 *
 * Return:
 *      string. NULL if argument 'p' is NULL or not a valid lsm_target_port
 *      pointer.
 */
const char LSM_DLL_EXPORT *lsm_target_port_system_id_get(lsm_target_port *tp);

#ifdef __cplusplus
}
#endif
#endif
