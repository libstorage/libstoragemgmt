/*
 * Copyright (C) 2016 Red Hat, Inc.
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
 * New in version 1.3.
 * Free the memory for a battery record
 * @param b     Battery memory to free
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_battery_record_free(lsm_battery *b);

/**
 * New in version 1.3.
 * Copy a battery record
 * @param b     Battery record to copy
 * @return Copy of battery record
 */
lsm_battery LSM_DLL_EXPORT *lsm_battery_record_copy(lsm_battery *b);

/**
 * New in version 1.3.
 * Free an array of battery records
 * @param bs            Array of battery records.
 * @param count         Count of battery array.
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_battery_record_array_free(lsm_battery *bs[],
                                                 uint32_t count);

/**
 * New in version 1.3.
 * Returns the battery id
 * Note: Return value is valid as long as battery pointer is valid.  It gets
 * freed when record is freed.
 * @param b     Battery record of interest
 * @return String id
 */
const char LSM_DLL_EXPORT *lsm_battery_id_get(lsm_battery *b);

/**
 * New in version 1.3.
 * Returns the battery name
 * Note: Return value is valid as long as battery pointer is valid.  It gets
 * freed when record is freed.
 * @param b     Battery record of interest
 * @return Disk name
 */
const char LSM_DLL_EXPORT *lsm_battery_name_get(lsm_battery *b);

/**
 * New in version 1.3.
 * Returns the battery type (enumeration)
 * Note: Return value is valid as long as battery pointer is valid.  It gets
 * freed when record is freed.
 * @param b     Battery record of interest
 * @return Disk type
 */
lsm_battery_type LSM_DLL_EXPORT lsm_battery_type_get(lsm_battery *b);

/**
 * New in version 1.3.
 * Returns the battery status
 * Note: Return value is valid as long as battery pointer is valid.  It gets
 * freed when record is freed.
 * @param b     Battery record of interest
 * @return Status of the battery
 */
uint64_t LSM_DLL_EXPORT lsm_battery_status_get(lsm_battery *b);

/**
 * New in version 1.3.
 * Returns the system id
 * Note: Return value is valid as long as battery pointer is valid.  It gets
 * freed when record is freed.
 * @param b     Battery record of interest
 * @return Which system the battery belongs too.
 */
const char LSM_DLL_EXPORT *lsm_battery_system_id_get(lsm_battery *b);

#ifdef __cplusplus
}
#endif
#endif                          /* LIBSTORAGEMGMT_BATTERY_H */
