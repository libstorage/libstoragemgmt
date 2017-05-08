/*
 * Copyright (C) 2017 Red Hat, Inc.
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

#ifndef _PTR_ARRAY_H_
#define _PTR_ARRAY_H_

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct _ptr_array;

/*
 * Return NULL if out of memory, errno will be ENOMEM.
 */
struct _ptr_array *_ptr_array_sized_new(uint32_t size);

void _ptr_array_free(struct _ptr_array *pa);

void _ptr_array_set_free_func(struct _ptr_array *pa, void (*free_func)(void *));

/*
 * If 'pa' if NULL or out of index, abort by assert().
 */
void *_ptr_array_index(struct _ptr_array *pa, uint32_t index);

/*
 * If 'pa' if NULL or out of index, abort by assert().
 */
void _ptr_array_set_index(struct _ptr_array *pa, uint32_t index, void *data);

/*
 * Return 0 if OK or index is smaller or equal to current size.
 * Return ENOMEM if out of memory, errno will be ENOMEM.
 * If 'pa' if NULL, abort by assert().
 */
int _ptr_array_set_size(struct _ptr_array *pa, uint32_t len);

/*
 * Set errno to EINVAL if 'pa' is NULL or out of index.
 * If 'pa' if NULL or out of index, abort by assert().
 */
void _ptr_array_remove_index(struct _ptr_array *pa, uint32_t index);

/*
 * Return ENOMEM if out of memory, errno will be ENOMEM.
 * If 'pa' if NULL, abort by assert().
 */
int _ptr_array_append(struct _ptr_array *pa, void *data);

/*
 * If 'pa' if NULL, abort by assert().
 */
uint32_t _ptr_array_len(struct _ptr_array *pa);

#ifdef  __cplusplus
}
#endif
#endif  /* End of _PTR_ARRAY_H_ */
