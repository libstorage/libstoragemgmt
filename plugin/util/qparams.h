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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
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
# define _QPARAMS_H_

#ifdef __cplusplus
extern "C" {
#endif

#if defined _WIN32 || defined __CYGWIN__
    #define QPARAM_DLL_IMPORT __declspec(dllimport)
    #define QPARAM_DLL_EXPORT __declspec(dllexport)
    #define QPARAM_DLL_LOCAL
#else
    #if __GNUC__ >= 4
        #define QPARAM_DLL_IMPORT __attribute__ ((visibility ("default")))
        #define QPARAM_DLL_EXPORT __attribute__ ((visibility ("default")))
        #define QPARAM_DLL_LOCAL  __attribute__ ((visibility ("hidden")))
    #else
        #define QPARAM_DLL_IMPORT
        #define QPARAM_DLL_EXPORT
        #define QPARAM_DLL_LOCAL
    #endif
#endif

/**
 *  * ATTRIBUTE_SENTINEL:
 *   *
 *    * Macro to check for NULL-terminated varargs lists
 *     */
#  ifndef ATTRIBUTE_SENTINEL
#   if __GNUC_PREREQ (4, 0)
#    define ATTRIBUTE_SENTINEL __attribute__((__sentinel__))
#   else
#    define ATTRIBUTE_SENTINEL
#   endif
#  endif


/**
 *  Single web service query parameter 'name=value'.
 */
struct qparam {
  char *name;           /**< Name (unescaped). */
  char *value;          /**< Value (unescaped). */
  int ignore;           /**< Ignore this field in qparam_get_query */
};

/**
 *  Set of parameters.
 */
struct qparam_set {
  int n;            /**< number of parameters used */
  int alloc;            /**< allocated space */
  struct qparam *p;     /**< array of parameters */
};

/* New parameter set. */
QPARAM_DLL_LOCAL struct qparam_set *new_qparam_set (int init_alloc, ...)
    ATTRIBUTE_SENTINEL;

/* Appending parameters. */
QPARAM_DLL_LOCAL int append_qparams (struct qparam_set *ps, ...)
    ATTRIBUTE_SENTINEL;
QPARAM_DLL_LOCAL int append_qparam (struct qparam_set *ps,
                          const char *name, const char *value);


/* Parse a query string into a parameter set. */
QPARAM_DLL_LOCAL struct qparam_set *qparam_query_parse (const char *query);

QPARAM_DLL_LOCAL void free_qparam_set (struct qparam_set *ps);

#ifdef __cplusplus
}
#endif

#endif /* _QPARAMS_H_ */


