/* Copyright (C) 2007, 2009-2011 Red Hat, Inc.
 *
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
 * Authors:
 *    Richard W.M. Jones <rjones@redhat.com>
 *
 * Utility functions to help parse and assemble query strings.
 *
 *
 *
 * !!!NOTE!!!: Taken from libvirt and modified to remove libvirt coupling.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "qparams.h"
#include <libxml/uri.h>
#include <string.h>

struct qparam_set *new_qparam_set(int init_alloc, ...) {
    va_list args;
    struct qparam_set *ps;
    const char *pname, *pvalue;

    if (init_alloc <= 0)
        init_alloc = 1;

    ps = (struct qparam_set *)calloc(1, sizeof(*(ps)));
    if (!ps) {
        return NULL;
    }

    ps->n = 0;
    ps->alloc = init_alloc;

    ps->p = (struct qparam *)calloc(ps->alloc, sizeof(*(ps->p)));

    if (!ps->p) {
        free(ps);
        return NULL;
    }

    va_start(args, init_alloc);
    while ((pname = va_arg(args, char *)) != NULL) {
        pvalue = va_arg(args, char *);

        if (append_qparam(ps, pname, pvalue) == -1) {
            free_qparam_set(ps);
            ps = NULL;
            break;
        }
    }
    va_end(args);

    return ps;
}

int append_qparams(struct qparam_set *ps, ...) {
    va_list args;
    const char *pname, *pvalue;
    int ret = 0;

    va_start(args, ps);
    while ((pname = va_arg(args, char *)) != NULL) {
        pvalue = va_arg(args, char *);

        if (append_qparam(ps, pname, pvalue) == -1) {
            ret = -1;
            break;
        }
    }
    va_end(args);

    return ret;
}

/* Ensure there is space to store at least one more parameter
 * at the end of the set.
 */
static int grow_qparam_set(struct qparam_set *ps) {
    if (ps->n >= ps->alloc) {

        void *tmp = realloc(ps->p, ps->alloc * 2);
        if (!tmp) {
            return -1;
        }
        ps->p = (struct qparam *)tmp;
        ps->alloc *= 2;
    }

    return 0;
}

int append_qparam(struct qparam_set *ps, const char *name, const char *value) {
    char *pname, *pvalue;

    pname = strdup(name);
    if (!pname) {
        return -1;
    }

    pvalue = strdup(value);
    if (!pvalue) {
        free(pname);
        return -1;
    }

    if (grow_qparam_set(ps) == -1) {
        free(pname);
        free(pvalue);
        return -1;
    }

    ps->p[ps->n].name = pname;
    ps->p[ps->n].value = pvalue;
    ps->p[ps->n].ignore = 0;
    ps->n++;

    return 0;
}

void free_qparam_set(struct qparam_set *ps) {
    int i;

    for (i = 0; i < ps->n; ++i) {
        free(ps->p[i].name);
        free(ps->p[i].value);
    }
    free(ps->p);
    ps->p = NULL;
    free(ps);
}

struct qparam_set *qparam_query_parse(const char *query) {
    struct qparam_set *ps;
    const char *end, *eq;

    ps = new_qparam_set(0, NULL);
    if (!ps) {
        return NULL;
    }

    if (!query || query[0] == '\0')
        return ps;

    while (*query) {
        char *name = NULL, *value = NULL;

        /* Find the next separator, or end of the string. */
        end = strchr(query, '&');
        if (!end)
            end = strchr(query, ';');
        if (!end)
            end = query + strlen(query);

        /* Find the first '=' character between here and end. */
        eq = strchr(query, '=');
        if (eq && eq >= end)
            eq = NULL;

        /* Empty section (eg. "&&"). */
        if (end == query)
            goto next;

        /* If there is no '=' character, then we have just "name"
         * and consistent with CGI.pm we assume value is "".
         */
        else if (!eq) {
            name = xmlURIUnescapeString(query, end - query, NULL);
            if (!name)
                goto out_of_memory;
        }
        /* Or if we have "name=" here (works around annoying
         * problem when calling xmlURIUnescapeString with len = 0).
         */
        else if (eq + 1 == end) {
            name = xmlURIUnescapeString(query, eq - query, NULL);
            if (!name)
                goto out_of_memory;
        }
        /* If the '=' character is at the beginning then we have
         * "=value" and consistent with CGI.pm we _ignore_ this.
         */
        else if (query == eq)
            goto next;

        /* Otherwise it's "name=value". */
        else {
            name = xmlURIUnescapeString(query, eq - query, NULL);
            if (!name)
                goto out_of_memory;
            value = xmlURIUnescapeString(eq + 1, end - (eq + 1), NULL);
            if (!value) {
                free(name);
                goto out_of_memory;
            }
        }

        /* Append to the parameter set. */
        if (append_qparam(ps, name, value ? value : "") == -1) {
            free(name);
            free(value);
            goto out_of_memory;
        }
        free(name);
        free(value);

    next:
        query = end;
        if (*query)
            query++; /* skip '&' separator */
    }

    return ps;

out_of_memory:
    free_qparam_set(ps);
    return NULL;
}
