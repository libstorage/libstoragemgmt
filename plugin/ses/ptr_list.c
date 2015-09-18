/*
 * Copyright (C) 2015 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
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
 *
 */

#include "ptr_list.h"

#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

struct _list_node {
    void *data;
    void *next;
};

struct pointer_list {
    struct _list_node *first_node;
    uint32_t len;
    struct _list_node *last_node;
};

struct pointer_list *ptr_list_new(void)
{
    struct pointer_list *ptr_list = NULL;
    ptr_list = (struct pointer_list *) malloc(sizeof(struct pointer_list));
    if (ptr_list == NULL)
        exit(EXIT_FAILURE);

    ptr_list->len = 0;
    ptr_list->first_node = NULL;
    ptr_list->last_node = NULL;
    return ptr_list;
}

void ptr_list_add(struct pointer_list *ptr_list, void *data)
{
    struct _list_node *node = NULL;

    if (ptr_list == NULL)
        return;

    node = (struct _list_node *) malloc(sizeof(struct _list_node));
    if (node == NULL)
        exit(EXIT_FAILURE);

    node->data = data;
    node->next = NULL;

    if (ptr_list->first_node == NULL) {
        ptr_list->first_node = node;
    } else {
        ptr_list->last_node->next = node;
    }
    ptr_list->last_node = node;
    ++(ptr_list->len);
}

uint32_t ptr_list_len(struct pointer_list *ptr_list)
{
    if (ptr_list == NULL)
        return 0;
    return ptr_list->len;
}

void *ptr_list_index(struct pointer_list *ptr_list, uint32_t index)
{
    uint32_t i = 0;
    struct _list_node *node;

    if ((ptr_list == NULL) || (ptr_list->len == 0) || (ptr_list->len <= index))
        return NULL;

    if (index == ptr_list->len - 1)
        return ptr_list->last_node->data;

    node = ptr_list->first_node;
    while((i < index) && (node != NULL)) {
        node = (struct _list_node *) node->next;
        ++i;
    }
    if (i == index)
        return node->data;
    return NULL;
}

void ptr_list_free(struct pointer_list *ptr_list)
{
    struct _list_node *node = NULL;
    struct _list_node *tmp_node = NULL;
    uint32_t i;

    if (ptr_list == NULL)
        return;

    node = ptr_list->first_node;

    while(node != NULL) {
        tmp_node = node;
        node = (struct _list_node *) node->next;
        free(tmp_node);
    }

    free(ptr_list);
}

void ptr_list_2_array(struct pointer_list *ptr_list, void ***array,
                      uint32_t *count)
{
    uint32_t i = 0;
    void *data = NULL;

    if ((ptr_list == NULL) || (array == NULL) || (count == NULL))
        return;

    *array = NULL;
    *count = ptr_list_len(ptr_list);
    if (*count == 0)
        return;

    *array = (void **) malloc(sizeof(void *) * (*count));
    if (*array == NULL)
        exit(EXIT_FAILURE);

    ptr_list_for_each(ptr_list, i, data) {
        (*array)[i] = data;
    }
    return;
}
