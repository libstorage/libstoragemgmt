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
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <search.h>
#include <string.h>
#include <stdbool.h>

#include "hash_table.h"
#include "uthash.h"

#define _INITAL_MAX_ENTRIE_COUNT    128

#define _FREE_KEY(k) \
    if (h->need_free_key == true) \
        free(k);

#define _FREE_VALUE(v) \
    if (h->value_free_func != NULL) \
        h->value_free_func(v);

struct __entry {
    char *key;
    void *value;
    UT_hash_handle hh;
};

struct _hash_table {
    struct __entry *head;
    bool need_free_key;
    void (*value_free_func)(void *value);
};

struct _hash_table *_hash_table_new(bool need_free_key,
                                    void (*value_free_func)(void *value))
{
    struct _hash_table *h = NULL;

    h = (struct _hash_table *) malloc(sizeof(struct _hash_table));
    if (h == NULL)
        goto nomem;

    h->head = NULL;
    h->need_free_key = need_free_key;
    h->value_free_func = value_free_func;

    errno = 0;
    return h;

nomem:
    errno = ENOMEM;
    return NULL;
}

int _hash_table_set(struct _hash_table *h, const char *key, void *value)
{
    struct __entry *e = NULL;

    assert(h != NULL);
    assert(key != NULL);

    e = (struct __entry *) malloc(sizeof(struct __entry));
    if (e == NULL)
        goto nomem;

    e->key = (char *) key;
    e->value = value;

    _hash_table_del(h, key); /* ignore the error */

    HASH_ADD_KEYPTR(hh, h->head, key, strlen(key), e);

    errno = 0;
    return 0;

nomem:
    errno = ENOMEM;
    return errno;
}

void *_hash_table_get(struct _hash_table *h, const char *key)
{
    struct __entry *e = NULL;

    assert(h != NULL);
    assert(key != NULL);

    HASH_FIND_STR(h->head, key, e);
    if (e == NULL)
        goto notfound;

    return e->value;

notfound:
    errno = ESRCH;
    return NULL;
}

int _hash_table_del(struct _hash_table *h, const char *key)
{
    struct __entry *e = NULL;

    assert(h != NULL);
    assert(key != NULL);

    HASH_FIND_STR(h->head, key, e);
    if (e == NULL)
        goto notfound;

    HASH_DEL(h->head, e);
    _FREE_KEY(e->key);
    _FREE_VALUE(e->value);
    free(e);

    errno = 0;
    return errno;

notfound:
    errno = ESRCH;
    return errno;
}

void _hash_table_free(struct _hash_table *h)
{
    struct __entry *e = NULL;
    struct __entry *tmp = NULL;

    if (h == NULL)
        return;

    HASH_ITER(hh, h->head, e, tmp) {
        HASH_DEL(h->head, e);
        _FREE_KEY(e->key);
        _FREE_VALUE(e->value);
        free(e);
    }

    free(h);
}

int _hash_table_items_get(struct _hash_table *h, const char ***keys,
              void ***values, uint32_t *count)
{
    uint32_t i = 0;
    unsigned int tmp_count = 0;
    struct __entry *e = NULL;

    assert(h != NULL);
    assert(keys != NULL);
    assert(values != NULL);
    assert(count != NULL);

    *keys = NULL;
    *values = NULL;

    tmp_count = HASH_COUNT(h->head);
    assert(tmp_count <= UINT32_MAX);

    *count = tmp_count & UINT32_MAX;

    *keys = (const char **) malloc(sizeof(char *) * (*count));
    if (*keys == NULL)
        goto nomem;

    *values = (void **) malloc(sizeof(void *) * (*count));
    if (*values == NULL) {
        free(*keys);
        goto nomem;
    }

    /* initialize the array to all NULL */
    for (i = 0; i < *count; ++i) {
        (*keys)[i] = NULL;
        (*values)[i] = NULL;
    }

    i = 0;
    for (e = h->head; (e != NULL) && (i < *count); e = e->hh.next) {
        (*keys)[i] = e->key;
        (*values)[i] = e->value;
        i++;
    }

    return 0;

nomem:
    *keys = NULL;
    *values = NULL;
    *count = 0;
    errno = ENOMEM;
    return errno;
}
