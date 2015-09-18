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
 */

#ifndef _MY_MEM_H_
#define _MY_MEM_H_

#include <stdlib.h>

void *_malloc_or_die(size_t size, const char *file_path, int line_num);

#define malloc_or_die(n) _malloc_or_die((n), __FILE__, __LINE__)

char *_strdup_or_die(const char *str, const char *file_path, int line_num);

#define strdup_or_die(c) _strdup_or_die((c), __FILE__, __LINE__)

#endif  /* End of _MY_MEM_H_  */
