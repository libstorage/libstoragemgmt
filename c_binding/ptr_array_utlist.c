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
#include <assert.h>

#include "ptr_array.h"
#include "utlist.h"

#define _DELETE_ENTRY(pa, e) \
    do { \
        if ((e != NULL) && (pa->free_func != NULL)) \
            pa->free_func(e->data); \
        LL_DELETE(pa->head, e); \
        free(e); \
    } while(0)

#define _APPEND_ENTRY(pa, e, d, goto_nomem) \
    do { \
        e = (struct _entry *) malloc(sizeof(struct _entry)); \
        if (e == NULL) \
            goto goto_nomem; \
        e->data = d; \
        e->next = NULL; \
        LL_APPEND(pa->head, e); \
        ++(pa->len); \
    } while(0)

struct _entry;
struct _entry {
    void *data;
    struct _entry *next;
};

struct _ptr_array {
    struct _entry *head;
    uint32_t len;
    // free function.
    void (*free_func)(void *);
};

struct _ptr_array *_ptr_array_sized_new(uint32_t size)
{
    struct _ptr_array *pa = NULL;
    struct _entry *e = NULL;
    uint32_t i = 0;

    errno = 0;
    pa = (struct _ptr_array *) malloc (sizeof(struct _ptr_array));
    if (pa == NULL)
        goto nomem;
    pa->head = NULL;
    pa->free_func = NULL;
    pa->len = 0;
    if (size == 0)
        return pa;
    for (i = 0; i < size; ++i)
        _APPEND_ENTRY(pa, e, NULL, nomem);
    return pa;

 nomem:
    errno = ENOMEM;
    if (pa != NULL) {
        LL_FOREACH(pa->head, e)
            free(e);
        free(pa);
    }
    return NULL;
}

void _ptr_array_free(struct _ptr_array *pa)
{
    struct _entry *e = NULL;
    struct _entry *tmp = NULL;

    if (pa == NULL)
        return;

    LL_FOREACH_SAFE(pa->head, e, tmp) {
        if ((e != NULL) && (pa->free_func != NULL))
            pa->free_func(e->data);
        free(e);
    }
    free(pa);
}

void _ptr_array_set_free_func(struct _ptr_array *pa,
                              void (*free_func)(void *))
{
    if (pa != NULL)
        pa->free_func = free_func;
}

void *_ptr_array_index(struct _ptr_array *pa, uint32_t index)
{
    struct _entry *e = NULL;
    uint32_t i = 0;

    assert(pa != NULL);
    assert(index < pa->len);

    errno = 0;

    LL_FOREACH(pa->head, e) {
        if (i == index) {
            if (e == NULL)
                break;
            return e->data;
        }
        ++i;
    }
    return NULL;
}

void _ptr_array_set_index(struct _ptr_array *pa, uint32_t index, void *data)
{
    struct _entry *e = NULL;
    uint32_t i = 0;

    assert(pa != NULL);
    assert(index < pa->len);

    errno = 0;

    LL_FOREACH(pa->head, e) {
        if (i == index) {
            if (e == NULL)
                break;
            if (pa->free_func != NULL)
                pa->free_func(e->data);
            e->data = data;
            return;
        }
        ++i;
    }
}

int _ptr_array_set_size(struct _ptr_array *pa, uint32_t len)
{
    struct _entry *e = NULL;
    struct _entry *tmp = NULL;
    uint32_t i = 0;

    assert(pa != NULL);

    errno = 0;

    LL_FOREACH_SAFE(pa->head, e, tmp) {
        if (i >= len) {
            // Should delete entry
            if ((e != NULL) && (pa->free_func != NULL))
                pa->free_func(e->data);
            free(e);
            e->next = NULL;
        }
        ++i;
    }
    if (i < len)
        for (; i < len; ++i)
            _APPEND_ENTRY(pa, e, NULL, nomem);
    pa->len = len;
    return errno;

 nomem:
    errno = ENOMEM;
    i = 0;
    /* Restore old state of pointer array */
    LL_FOREACH(pa->head, e)
        if ( i >= pa->len)
            _DELETE_ENTRY(pa, e);
    return errno;
}

void _ptr_array_remove_index(struct _ptr_array *pa, uint32_t index)
{
    struct _entry *e = NULL;
    uint32_t i = 0;

    assert(pa != NULL);
    assert(index < pa->len);

    errno = 0;
    LL_FOREACH(pa->head, e) {
        if (i == index) {
            _DELETE_ENTRY(pa, e);
            return;
        }
        ++i;
    }
}

int _ptr_array_append(struct _ptr_array *pa, void *data)
{
    struct _entry *e = NULL;

    assert(pa != NULL);

    errno = 0;
    _APPEND_ENTRY(pa, e, data, nomem);
    return errno;

 nomem:
    errno = ENOMEM;
    return errno;
}

uint32_t _ptr_array_len(struct _ptr_array *pa)
{
    assert(pa != NULL);
    return pa->len;
}
