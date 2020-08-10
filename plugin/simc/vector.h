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
 */

#ifndef _SIMC_VECTOR_H_
#define _SIMC_VECTOR_H_

#include <stdint.h>

#define _VECTOR_NO_PRE_ALLOCATION 0

struct _vector;

struct _vector *_vector_new(uint32_t size);

/*
 * The index is started at 0.
 * Abort by assert() if vec pointer is NULL.
 * Abort by assert() if out of index.
 */
void _vector_update(struct _vector *vec, void *data, uint32_t index);

/*
 * Abort by assert() if vec pointer is NULL.
 * Return -1 if no memory, original vec is untouched.
 * Return 0 if no error.
 */
int _vector_insert(struct _vector *vec, void *data);

void _vector_free(struct _vector *vec);

/*
 * Abort by assert() if vec pointer is NULL.
 * Abort by assert() if out of index.
 */
void *_vector_get(struct _vector *vec, uint32_t index);

/*
 * Abort by assert() if vec pointer is NULL.
 */
uint32_t _vector_size(struct _vector *vec);

#define _vector_for_each(vec, i, data)                                         \
    for (i = 0; ((vec != NULL) && (i < _vector_size(vec)) &&                   \
                 (data = _vector_get(vec, i)));                                \
         ++i)

#endif /* End of _SIMC_VECTOR_H_ */
