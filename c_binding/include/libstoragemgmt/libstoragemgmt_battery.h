/*
 * Copyright (C) 2016-2017 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Gris Ge <fge@redhat.com>
 *
 */

#ifndef LIBSTORAGEMGMT_BATTERY_H
#define LIBSTORAGEMGMT_BATTERY_H

#include "libstoragemgmt_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * lsm_battery_record_free - Frees the memory for an individual battery
 * Version:
 *      1.3
 *
 * Description:
 *      Frees the memory for an individual lsm_battery
 *
 * @b:
 *      lsm_battery to release memory for.
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_battery pointer.
 */
int LSM_DLL_EXPORT lsm_battery_record_free(lsm_battery *b);

/**
 * lsm_battery_record_copy - Duplicates a battery record.
 * Version:
 *      1.3
 *
 * Description:
 *      Duplicates a lsm_battery record.
 *
 * @b:
 *      Pointer of lsm_battery to duplicate.
 *
 * Return:
 *      Pointer of lsm_battery. NULL on memory allocation failure or invalid
 *      lsm_battery pointer. Should be freed by lsm_battery_record_free().
 */
lsm_battery LSM_DLL_EXPORT *lsm_battery_record_copy(lsm_battery *b);

/**
 * lsm_battery_record_array_free - Frees the memory of battery array.
 * Version:
 *      1.3
 *
 * Description:
 *      Frees the memory for each of the batteries and then the battery array
 *      itself.
 *
 * @bs:
 *      Array to release memory for.
 * @count:
 *      Number of elements.
 * Return:
 *      Error code as enumerated by 'lsm_error_number':
 *          * LSM_ERR_OK
 *              On success or not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_battery pointer.
 */
int LSM_DLL_EXPORT lsm_battery_record_array_free(lsm_battery *bs[],
                                                 uint32_t count);

/**
 * lsm_battery_id_get - Retrieves the ID for the battery.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Retrieves the ID for the battery.
 *      Note: Address returned is valid until lsm_battery gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @b:
 *      Battery to retrieve ID for.
 *
 * Return:
 *      string. NULL if argument 'b' is NULL or not a valid lsm_battery pointer.
 */
const char LSM_DLL_EXPORT *lsm_battery_id_get(lsm_battery *b);

/**
 * lsm_battery_name_get - Retrieves the name for the battery.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Retrieves the name for the battery.
 *      Note: Address returned is valid until lsm_battery gets freed, copy
 *      return value if you need longer scope. Do not free returned string.
 *
 * @b:
 *      Battery to retrieve name for.
 *
 * Return:
 *      string. NULL if argument 'b' is NULL or not a valid lsm_battery pointer.
 */
const char LSM_DLL_EXPORT *lsm_battery_name_get(lsm_battery *b);

/**
 * lsm_battery_type_get - Retrieves the type for the battery.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Retrieves the type for the battery.
 *
 * @b:
 *      Battery to retrieve type for.
 *
 * Return:
 *      lsm_battery_type. Possible values are:
 *          * LSM_BATTERY_TYPE_UNKNOWN
 *              Unknown or no support or invalid lsm_battery pointer.
 *          * LSM_BATTERY_TYPE_OTHER
 *              Vendor specific type.
 *          * LSM_BATTERY_TYPE_CAPACITOR
 *              Super capacitor.
 *          * LSM_BATTERY_TYPE_CHEMICAL
 *              Chemical battery, e.g. Li-ion battery.
 *
 */
lsm_battery_type LSM_DLL_EXPORT lsm_battery_type_get(lsm_battery *b);

/**
 * lsm_battery_status_get - Retrieves status of the battery.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Retrieves status of the battery.
 *
 * @b:
 *      Battery to retrieve status for.
 *
 * Return:
 *      uint64_t. Status of the specified battery which is a bit sensitive
 *      field. Possible values are:
 *          * LSM_BATTERY_STATUS_UNKNOWN
 *              Unknown or invalid lsm_battery pointer.
 *          * LSM_BATTERY_STATUS_OTHER
 *              Vendor specific status.
 *          * LSM_BATTERY_STATUS_OK
 *              Battery is healthy and charged.
 *          * LSM_BATTERY_STATUS_DISCHARGING
 *              Battery is disconnected from power source and discharging.
 *          * LSM_BATTERY_STATUS_CHARGING
 *              Battery is not fully charged and charging.
 *          * LSM_BATTERY_STATUS_LEARNING
 *              System is trying to discharge and recharge the battery to
 *              learn its capability.
 *          * LSM_BATTERY_STATUS_DEGRADED
 *              Battery is degraded and should be checked or replaced.
 *          * LSM_BATTERY_STATUS_ERROR
 *              Battery is dead and should be replaced.
 *
 */
uint64_t LSM_DLL_EXPORT lsm_battery_status_get(lsm_battery *b);

/**
 * lsm_battery_system_id_get - Retrieves the system ID for the battery.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Retrieves the system id for the specified battery.
 *      Note: Address returned is valid until lsm_battery gets freed, copy
 * return value if you need longer scope. Do not free returned string.
 *
 * @b:
 *      Battery to retrieve system ID for.
 *
 * Return:
 *      string. NULL if argument 'b' is NULL or not a valid lsm_battery pointer.
 *
 */
const char LSM_DLL_EXPORT *lsm_battery_system_id_get(lsm_battery *b);

#ifdef __cplusplus
}
#endif
#endif /* LIBSTORAGEMGMT_BATTERY_H */
