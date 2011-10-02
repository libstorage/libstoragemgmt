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
 * File:   libstoragemgmt_plugin.h
 * Author: tasleson
 */

#ifndef LIBSTORAGEMGMT_PLUGIN_H
#define	LIBSTORAGEMGMT_PLUGIN_H

#include "libstoragemgmt.h"
#include <libxml/uri.h>

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Used by framework to call into plug-in for registration.
 * @param[in] c             Connection
 * @param[in] uri           uri
 * @param[in] password      Password
 * @param[in] timeout       Timeout in ms
 * @return LSM_ERR_OK, else error reason.
 */
int lsmPluginRegister(  lsmConnectPtr c, xmlURIPtr uri, char *password,
                        uint32_t timeout);

/**
 * Used by framework to unregister a plug-in.
 * @param[in] c     Connection
 * @return LSM_ERR_OK on success, else error reason.
 */
int lsmPluginUnregister( lsmConnectPtr c );

#ifdef	__cplusplus
}
#endif

#endif	/* LIBSTORAGEMGMT_PLUGIN_H */

