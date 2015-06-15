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

#ifndef LSM_COMMON_H
#define LSM_COMMON_H

#include "libstoragemgmt_types.h"


#ifdef  __cplusplus
extern "C" {
#endif

#if defined _WIN32 || defined __CYGWIN__
#define LSM_DLL_IMPORT __declspec(dllimport)
#define LSM_DLL_EXPORT __declspec(dllexport)
#define LSM_DLL_LOCAL
#else
#if __GNUC__ >= 4
#define LSM_DLL_IMPORT __attribute__ ((visibility ("default")))
#define LSM_DLL_EXPORT __attribute__ ((visibility ("default")))
#define LSM_DLL_LOCAL  __attribute__ ((visibility ("hidden")))
#else
#define LSM_DLL_IMPORT
#define LSM_DLL_EXPORT
#define LSM_DLL_LOCAL
#endif
#endif

/**
 * Allocates storage for string line of specified size.
 * @param size  Initial number of strings to allocate
 * @return NULL on error, else valid lsm_string_list record pointer
 */
lsm_string_list LSM_DLL_EXPORT *lsm_string_list_alloc(uint32_t size);

/**
 * Frees the memory allocated with the lsmStringListFree
 * @param sl    Record to free
 * @return LSM_ERR_OK on success, else error reason.
 */
int LSM_DLL_EXPORT lsm_string_list_free(lsm_string_list *sl);

/**
 * Copies a lsm_string_list record.
 * @param src       Source to copy
 * @return NULL on error, else copy of source.
 */
lsm_string_list LSM_DLL_EXPORT *lsm_string_list_copy(lsm_string_list *src);

/**
 * Set the specified element with the passed value.
 * @param sl        Valid string list pointer
 * @param index      Element position to set value to
 * @param value     Value to use for assignment
 * @return LSM_ERR_OK on success, else error reason
 */
int LSM_DLL_EXPORT lsm_string_list_elem_set(lsm_string_list *sl,
                                            uint32_t index,
                                            const char *value);

/**
 * Returns the value at the specified elem index
 * @param sl        Valid string list pointer
 * @param index     Index to retrieve
 * @return Value at that index position.
 */
const char LSM_DLL_EXPORT *lsm_string_list_elem_get(lsm_string_list *sl,
                                                    uint32_t index);

/**
 * Returns the size of the list
 * @param sl        Valid string list pointer
 * @return  size of list, note you cannot create a zero sized list, so
 *          0 indicates error with structure
 *
 */
uint32_t LSM_DLL_EXPORT lsm_string_list_size(lsm_string_list *sl);

/**
 * Appends a char * to the string list, will grow container as needed.
 * @param sl    String list to append to
 * @param add   Character string to add
 * @return LSM_ERR_OK on success, else error reason
 */
int LSM_DLL_EXPORT lsm_string_list_append(lsm_string_list *sl,
                                          const char *add);

/**
 * Deletes the string at the specified index.
 * NOTE: The elements after this one are moved down, thus if you wanted to
 * iterate over the list deleting each element one by one you need to do so in
 * reverse order.
 * @param sl            String list to remove item from
 * @param index         Specified index
 * @return LSM_ERR_OK on success, else error reason
 */
int LSM_DLL_EXPORT lsm_string_list_delete(lsm_string_list *sl,
                                          uint32_t index);

/**
 * Checks to see if initiator id is valid
 * @param init_id       Initiator value
 * @param init_type     Type of initiator id, will get modified
 *                      to determined if type passed in is UNKNOWN
 * @return LSM_ERR_OK if initiator id is OK, else LSM_INVALID_ARGUMENT
 */
int LSM_DLL_EXPORT
    lsm_initiator_id_verify(const char *init_id,
                            lsm_access_group_init_type *init_type);


/**
 * Checks to see if volume vpd83 is valid
 * @param vpd83         VPD string to check
 * @return LSM_ERR_OK if vpd is OK, else LSM_INVALID_ARGUMENT
 */
int LSM_DLL_EXPORT lsm_volume_vpd83_verify(const char *vpd83);

#ifdef  __cplusplus
}
#endif
#endif                          /* LSM_COMMON_H */
