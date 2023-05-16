/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2016-2023 Red Hat, Inc.
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
