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

#endif
