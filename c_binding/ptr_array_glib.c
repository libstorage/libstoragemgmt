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

#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <glib.h>
#include <assert.h>

#include "ptr_array.h"

struct _ptr_array {
    GPtrArray *array;
    void (*free_func)(void *); // only used for _ptr_array_set_index()
};

struct _ptr_array *_ptr_array_sized_new(uint32_t size)
{
    struct _ptr_array *pa = NULL;

    errno = 0;
    pa = (struct _ptr_array *) malloc (sizeof(struct _ptr_array));
    if (pa == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    pa->array = g_ptr_array_sized_new((guint) (size & G_MAXUINT));
    return pa;
}

void _ptr_array_free(struct _ptr_array *pa)
{
    if (pa == NULL)
        return;
    g_ptr_array_free(pa->array, TRUE);
    free(pa);
}

void _ptr_array_set_free_func(struct _ptr_array *pa,
                              void (*free_func)(void *))
{
    if (pa != NULL) {
        g_ptr_array_set_free_func(pa->array, free_func);
        pa->free_func = free_func;
    }
}

void *_ptr_array_index(struct _ptr_array *pa, uint32_t index)
{
    assert(pa != NULL);
    assert(index < pa->array->len);
    errno = 0;
    return (void *) g_ptr_array_index(pa->array, index);
}

void _ptr_array_set_index(struct _ptr_array *pa, uint32_t index, void *data)
{
    void *org_data = NULL;

    assert(pa != NULL);
    assert(index < pa->array->len);

    errno = 0;
    org_data = g_ptr_array_index(pa->array, index);
    if (pa->free_func != NULL)
        pa->free_func(org_data);
    g_ptr_array_index(pa->array, index) = data;
}

int _ptr_array_set_size(struct _ptr_array *pa, uint32_t len)
{
    assert(pa != NULL);

    errno = 0;
    g_ptr_array_set_size(pa->array, (gint) (len & G_MAXINT));
    return errno;
}

void _ptr_array_remove_index(struct _ptr_array *pa, uint32_t index)
{
    assert(pa != NULL);
    assert(index < pa->array->len);

    errno = 0;
    g_ptr_array_remove_index(pa->array, index);
}

int _ptr_array_append(struct _ptr_array *pa, void *data)
{
    assert(pa != NULL);

    errno = 0;
    g_ptr_array_add(pa->array, (gpointer) data);
    return errno;
}

uint32_t _ptr_array_len(struct _ptr_array *pa)
{
    assert(pa != NULL);
    return (uint32_t) pa->array->len;
}
