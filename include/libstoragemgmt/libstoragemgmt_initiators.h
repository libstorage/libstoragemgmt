/*
 * Copyright (C) 2011 Red Hat, Inc.
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
 *
 */

#ifndef LIBSTORAGEMGMT_INITIATORS_H
#define	LIBSTORAGEMGMT_INITIATORS_H

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Frees the memory for each of the initiators records and then the array itself.
 * @param init  Array to free.
 * @param size  Size of array.
 */
void lsmInitiatorRecordFreeArray( lsmInitiatorPtr init[], uint32_t size);

/**
 * Returns the type of identifer returned in @see lsmInitiatorIdGet
 * @param i     lsmInitiator to inquire
 * @return      Initiator Id type, -1 if i is invalid.
 */
lsmInitiatorTypes lsmInitiatorTypeGet(lsmInitiatorPtr i);

/**
 * Returns the initiator id (WWN, IQN etc.)
 * Note: Returned value is only valid as long as i is valid!.
 * @param i     lsmInitiator to inquire
 * @return      Initiator id
 */
char *lsmInitiatorIdGet(lsmInitiatorPtr i);


#ifdef	__cplusplus
}
#endif

#endif	/* LIBSTORAGEMGMT_INITIATORS_H */

