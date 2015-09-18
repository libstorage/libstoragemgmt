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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "my_mem.h"

void *_malloc_or_die(size_t size, const char *file_path, int line_num)
{
        void *buffer = malloc(size);
        if (buffer == NULL) {
                fprintf(stderr, "ERROR: malloc failure:'%s' line %d\n",
                        file_path, line_num);
                exit(EXIT_FAILURE);
        }
        return buffer;
}

char *_strdup_or_die(const char *str, const char *file_path, int line_num)
{
        char *new_str = strdup(str);
        if (new_str == NULL) {
                fprintf(stderr, "ERROR: strdup failure: '%s' line %d\n",
                        file_path, line_num);
                exit(EXIT_FAILURE);
        }
        return new_str;
}
