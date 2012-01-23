#ifndef LSM_CONVERT_HPP
#define LSM_CONVERT_HPP

#include "lsm_datatypes.h"
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

#endif