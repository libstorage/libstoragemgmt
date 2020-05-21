/*
 * Copyright (C) 2010-2011 Red Hat, Inc.
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
 * !!!NOTE!!!: Taken from libvirt and modified to remove libvirt coupling.
 */

#ifndef _QPARAMS_H_
#define _QPARAMS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "libstoragemgmt/libstoragemgmt_common.h"

#ifndef __GNUC_PREREQ
#if defined __GNUC__ && defined __GNUC_MINOR__
#define __GNUC_PREREQ(maj, min)                                                \
    ((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
#define __GNUC_PREREQ(maj, min) 0
#endif
#endif
/**
 *  * ATTRIBUTE_SENTINEL:
 *   *
 *    * Macro to check for NULL-terminated varargs lists
 *     */
#ifndef ATTRIBUTE_SENTINEL
#if __GNUC_PREREQ(4, 0)
#define ATTRIBUTE_SENTINEL __attribute__((__sentinel__))
#else
#define ATTRIBUTE_SENTINEL
#endif
#endif

/**
 *  Single web service query parameter 'name=value'.
 */
struct qparam {
    char *name;  /**< Name (unescaped). */
    char *value; /**< Value (unescaped). */
    int ignore;  /**< Ignore this field in qparam_get_query */
};

/**
 *  Set of parameters.
 */
struct qparam_set {
    int n;            /**< number of parameters used */
    int alloc;        /**< allocated space */
    struct qparam *p; /**< array of parameters */
};

/* New parameter set. */
LSM_DLL_LOCAL struct qparam_set *new_qparam_set(int init_alloc,
                                                ...) ATTRIBUTE_SENTINEL;

/* Appending parameters. */
LSM_DLL_LOCAL int append_qparams(struct qparam_set *ps, ...) ATTRIBUTE_SENTINEL;
LSM_DLL_LOCAL int append_qparam(struct qparam_set *ps, const char *name,
                                const char *value);

/* Parse a query string into a parameter set. */
LSM_DLL_LOCAL struct qparam_set *qparam_query_parse(const char *query);

LSM_DLL_LOCAL void free_qparam_set(struct qparam_set *ps);

#ifdef __cplusplus
}
#endif

#endif /* _QPARAMS_H_ */
