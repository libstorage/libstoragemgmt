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
#include <glib.h>
#include <limits.h>

#include "hash_table.h"

struct _hash_table {
    GHashTable *hash;
};

struct _hash_table *_hash_table_new(bool need_free_key,
                    void (*value_free_func)(void *value))
{
    struct _hash_table *h = NULL;

    h = (struct _hash_table *) malloc(sizeof(struct _hash_table));
    if (h == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    if (need_free_key == TRUE)
        h->hash = g_hash_table_new_full(g_str_hash, g_str_equal, free,
                                        value_free_func);
    else
        h->hash = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
                                        value_free_func);

    errno = 0;
    return h;
}

int _hash_table_set(struct _hash_table *h, const char *key, void *value)
{
    assert(h != NULL);

    g_hash_table_replace(h->hash, (gpointer) key, (gpointer) value);

    errno = 0;
    return 0;
}

void *_hash_table_get(struct _hash_table *h, const char *key)
{
    gboolean found = FALSE;
    void *data = NULL;

    assert(h != NULL);
    assert(key != NULL);

    found =
        g_hash_table_lookup_extended(h->hash, (gconstpointer) key,
                                     NULL /* ignore original key location */,
                                     &data);
    if (found == FALSE)
        errno = ESRCH;
    else
        errno = 0;

    return data;
}

int _hash_table_del(struct _hash_table *h, const char *key)
{
    assert(h != NULL);
    assert(key != NULL);

    if (g_hash_table_remove(h->hash, (gconstpointer) key) == TRUE)
        errno = 0;
    else
        errno = ESRCH;

    return errno;
}

void _hash_table_free(struct _hash_table *h)
{
    if (h == NULL)
        return;

    g_hash_table_unref(h->hash);
    free(h);
}

int _hash_table_items_get(struct _hash_table *h, const char ***keys,
                          void ***values, uint32_t *count)
{
    uint32_t i = 0;
    guint tmp_count = 0;
    GList *key_list = NULL;
    GList *value_list = NULL;
    GList *org_key_list = NULL;
    GList *org_value_list = NULL;

    assert(h != NULL);
    assert(keys != NULL);
    assert(values != NULL);
    assert(count != NULL);

    *keys = NULL;
    *values = NULL;

    key_list = g_hash_table_get_keys(h->hash);
    value_list = g_hash_table_get_values(h->hash);
    org_key_list = key_list;
    org_value_list = value_list;

    tmp_count = g_list_length(key_list);
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

    for (i = 0; i < tmp_count; ++i) {
        assert(key_list != NULL);
        assert(value_list != NULL);
        (*keys)[i] = key_list->data;
        (*values)[i] = value_list->data;
        key_list = key_list->next;
        value_list = value_list->next;
    }

    g_list_free(org_key_list);
    g_list_free(org_value_list);
    return 0;

nomem:
    g_list_free(org_key_list);
    g_list_free(org_value_list);
    *keys = NULL;
    *values = NULL;
    *count = 0;
    errno = ENOMEM;
    return errno;
}
