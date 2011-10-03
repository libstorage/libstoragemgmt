/*
 * Copyright (C) 2011 Red Hat, Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Author: tasleson
 */

#ifndef LSM_COMMON_H
#define	LSM_COMMON_H

#ifdef	__cplusplus
extern "C" {
#endif

#if defined _WIN32 || defined __CYGWIN__
    #define LSM_DLL_IMPORT __declspec(dllimport)
    #define LSM_DLL_EXPORT __declspec(dllexport)
    #define LSM_DLL_LOCAL
#else
    #if __GNUC__ >= 4
        #define LSM_DLL_IMPORT __attribute__ ((visibility ("default")))
        #define LSM_DLL_EXPORT __attribute__ ((visibility ("default")))
        #define LSM_DLL_LOCAL  __attribute__ ((visibility ("hidden")))
    #else
        #define LSM_DLL_IMPORT
        #define LSM_DLL_EXPORT
        #define LSM_DLL_LOCAL
    #endif
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* LSM_COMMON_H */

