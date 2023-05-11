/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2016-2023 Red Hat, Inc.
 *
 * Author: Gris Ge <fge@redhat.com>
 */

#include <assert.h>
#include <stdlib.h>

#include "vector.h"

struct _vector {
    void **data_array;
    uint32_t size;
};

struct _vector *_vector_new(uint32_t size) {
    struct _vector *vec = NULL;
    uint32_t i = 0;

    vec = (struct _vector *)malloc(sizeof(struct _vector));
    if (vec == NULL)
        return NULL;

    vec->size = size;
    if (size == 0) {
        vec->data_array = NULL;
    } else {
        vec->data_array = (void **)malloc(sizeof(void *) * size);
        if (vec->data_array == NULL) {
            free(vec);
            return NULL;
        }
        for (; i < size; ++i)
            vec->data_array[i] = NULL;
    }
    return vec;
}

void _vector_update(struct _vector *vec, void *data, uint32_t index) {
    assert(vec != NULL);
    assert(vec->size > index);

    vec->data_array[index] = data;
}

int _vector_insert(struct _vector *vec, void *data) {
    assert(vec != NULL);

    void **new_data_array = NULL;

    new_data_array = realloc(vec->data_array, sizeof(void *) * (vec->size + 1));
    if (new_data_array == NULL)
        return -1;
    vec->data_array = new_data_array;
    vec->data_array[vec->size] = data;
    vec->size++;

    return 0;
}

void _vector_free(struct _vector *vec) {
    if (vec != NULL)
        free(vec->data_array);
    free(vec);
}

void *_vector_get(struct _vector *vec, uint32_t index) {
    assert(vec != NULL);
    assert(vec->size > index);

    return vec->data_array[index];
}

uint32_t _vector_size(struct _vector *vec) {
    assert(vec != NULL);
    return vec->size;
}
