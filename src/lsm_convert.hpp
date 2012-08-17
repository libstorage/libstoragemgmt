/*
 * Copyright (C) 2011-2012 Red Hat, Inc.
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Author: tasleson
 */

#ifndef LSM_CONVERT_HPP
#define LSM_CONVERT_HPP

#include "lsm_datatypes.hpp"
#include "lsm_ipc.hpp"

/**
 * Converts an array of Values to a lsmStringList
 * @param list      List represented as an vector of strings.
 * @return lsmStringList pointer, NULL on error.
 */
lsmStringList *valueToStringList( Value &list);

/**
 * Converts a lsmStringList to a Value
 * @param sl        String list to convert
 * @return Value
 */
Value stringListToValue( lsmStringList *sl);

/**
 * Converts a volume to a volume.
 * @param vol Value to convert.
 * @return lsmVolume *, else NULL on error
 */
lsmVolume * valueToVolume(Value &vol);

/**
 * Converts a lsmVolume * to a Value
 * @param vol lsmVolume to convert
 * @return Value
 */
Value volumeToValue(lsmVolume *vol);

/**
 * Converts a value to lsmInitiator *
 * @param init lsmVolume to convert
 * @return lsmInitiator *, else NULL on error.
 */
lsmInitiator *valueToInitiator(Value &init);

/**
 * Converts an lsmInitiator * to Value
 * @param init lsmInitiator to convert
 * @return Value
 */
Value initiatorToValue(lsmInitiator *init);

/**
 * Converts a value to a pool
 * @param pool To convert to lsmPool *
 * @return lsmPool *, else NULL on error.
 */
lsmPool *valueToPool(Value &pool);

/**
 * Converts a lsmPool * to Value
 * @param pool Pool pointer to convert
 * @return Value
 */
Value poolToValue(lsmPool *pool);

/**
 * Converts a value to a system
 * @param system to convert to lsmSystem *
 * @return lsmSystem pointer, else NULL on error
 */
lsmSystem *valueToSystem(Value &system);

/**
 * Converts a lsmSystem * to a Value
 * @param system pointer to convert to Value
 * @return Value
 */
Value systemToValue(lsmSystem *system);

/**
 * Converts a Value to a lsmAccessGroup
 * @param group to convert to lsmAccessGroup*
 * @return lsmAccessGroup *, NULL on error
 */
lsmAccessGroup *valueToAccessGroup(Value &group);

/**
 * Converts a lsmAccessGroupPtr to a Value
 * @param group     Group to convert
 * @return Value, null value type on error.
 */
Value accessGroupToValue(lsmAccessGroupPtr group);

/**
 * Converts an access group list to an array of access group pointers
 * @param[in] group         Value representing a std::vector of access groups
 * @param[out] count         Number of items in the returned array.
 * @return NULL on memory allocation failure, else pointer to access group
 *          array.
 */
lsmAccessGroup **valueToAccessGroupList( Value &group, uint32_t *count );

/**
 * Converts an array of lsmAccessGroupPtr to Value(s)
 * @param group             Pointer to an array of lsmAccessGroupPtr
 * @param count             Number of items in array.
 * @return std::vector of Values representing access groups
 */
Value accessGroupListToValue( lsmAccessGroupPtr *group, uint32_t count);

/**
 * Converts a Value to a lsmBlockRange
 * @param br        Value representing a block range
 * @return lsmBlockRangePtr
 */
lsmBlockRange *valueToBlockRange(Value &br);

/**
 * Converts a lsmBlockRange to a Value
 * @param br        lsmBlockRange to convert
 * @return Value, null value type on error
 */
Value blockRangeToValue(lsmBlockRange *br);

/**
 * Converts a Value to an array of lsmBlockRangePtr
 * @param[in] brl           Value representing block range(s)
 * @param[out] count        Number of items in the resulting array
 * @return NULL on memory allocation failure, else array of lsmBlockRangePtr
 */
lsmBlockRangePtr *valueToBlockRangeList(Value &brl,  uint32_t *count);

/**
 * Converts an array of lsmBlockRangePtr to Value
 * @param brl           An array of lsmBlockRangePtr
 * @param count         Number of items in input
 * @return Value
 */
Value blockRangeListToValue( lsmBlockRangePtr *brl, uint32_t count);

/**
 * Converts a value to a lsmFs *
 * @param fs        Value representing a FS to be converted
 * @return lsmFs pointer or NULL on error.
 */
lsmFs *valueToFs(Value &fs);

/**
 * Converts a lsmFs pointer to a Value
 * @param fs        File system pointer to convert
 * @return Value
 */
Value fsToValue(lsmFs *fs);

/**
 * Converts a value to a lsmSs *
 * @param ss        Value representing a snapshot to be converted
 * @return lsmSs pointer or NULL on error.
 */
lsmSs *valueToSs(Value &ss);

/**
 * Converts a lsmSs pointer to a Value
 * @param ss        Snapshot pointer to convert
 * @return Value
 */
Value ssToValue(lsmSs *ss);

/**
 * Converts a value to a lsmNfsExport *
 * @param exp        Value representing a nfs export to be converted
 * @return lsmNfsExport pointer or NULL on error.
 */
lsmNfsExport *valueToNfsExport(Value &exp);

/**
 * Converts a lsmNfsExport pointer to a Value
 * @param exp        NFS export pointer to convert
 * @return Value
 */
Value nfsExportToValue(lsmNfsExport *exp);

/**
 * Converts a Value to a lsmCapabilites
 * @param exp       Value representing a lsmCapabilities
 * @return lsmCapabilities pointer or NULL on error
 */
lsmStorageCapabilities *valueToCapabilities(Value &exp);

/**
 * Converts a lsmCapabilites to a value
 * @param cap       lsmCapabilites to convert to value
 * @return Value
 */
Value capabilitiesToValue(lsmStorageCapabilities *cap);

#endif
