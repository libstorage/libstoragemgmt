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

#include <libstoragemgmt/libstoragemgmt.h>
#include <libstoragemgmt/libstoragemgmt_error.h>
#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include <libstoragemgmt/libstoragemgmt_types.h>
#include "lsm_datatypes.h"

#include <stdio.h>
#include <libxml/uri.h>

/**
 * Common code to validate and initialize the connection.
 */
#define CONN_SETUP(c)	do {			\
	if(!LSM_IS_CONNECT(c)) {			\
		return LSM_ERR_INVALID_CONN;	\
	}									\
	lsmErrorFree(c->error);				\
	} while (0)

int lsmConnectPassword(char *uri, char *password,
                        lsmConnectPtr *conn, uint32_t timeout, lsmErrorPtr *e)
{
	int rc = LSM_ERR_OK;
	lsmConnectPtr c = NULL;

	*e = NULL;

	/* Password is optional */
	if(  uri == NULL || conn == NULL ) {
		return LSM_ERR_INVALID_ARGUMENT;
	}

	c = getConnection();
	if(c) {
		c->uri = xmlParseURI(uri);
		if( c->uri && c->uri->scheme ) {
			rc = loadDriver(c, c->uri, password, timeout, e);
			if( rc == LSM_ERR_OK ) {
				*conn = (lsmConnectPtr)c;
			}
		} else {
			rc = LSM_ERR_URI_PARSE;
		}

		if( rc != LSM_ERR_OK ) {
			freeConnection(c);
		}
	} else {
		rc = LSM_ERR_NO_MEMORY;
	}
	return rc;
}

int lsmConnectClose(lsmConnectPtr c)
{
	CONN_SETUP(c);

	if( c->unregister ) {
		return (*c->unregister)(c);
	}
	return LSM_ERR_NO_SUPPORT;
}

int lsmConnectSetTimeout(lsmConnectPtr c, uint32_t timeout)
{
	CONN_SETUP(c);

	if( c->plugin.mgmtOps && c->plugin.mgmtOps->tmo_set) {
		return (*(c->plugin.mgmtOps->tmo_set))(c, timeout);
	}
	return LSM_ERR_NO_SUPPORT;
}

int lsmConnectGetTimeout(lsmConnectPtr c, uint32_t *timeout)
{
	CONN_SETUP(c);

	if( c->plugin.mgmtOps && c->plugin.mgmtOps->tmo_get) {
		return (*(c->plugin.mgmtOps->tmo_get))(c, timeout);
	}
	return LSM_ERR_NO_SUPPORT;
}

int lsmCapabilities(lsmConnectPtr c, lsmStorageCapabilitiesPtr *cap)
{
	return LSM_ERR_NO_SUPPORT;
}

int lsmPoolList(lsmConnectPtr c, lsmPoolPtr **poolArray,
                        uint32_t *count)
{
	CONN_SETUP(c);

	if( c->plugin.sanOps && c->plugin.sanOps->pool_get) {
		return (*(c->plugin.sanOps->pool_get))(c, poolArray, count);
	}
	return LSM_ERR_NO_SUPPORT;
}

int lsmInitiatorList(lsmConnectPtr c, lsmInitiatorPtr **initiators,
                                uint32_t *count)
{
	CONN_SETUP(c);

	if( c->plugin.sanOps && c->plugin.sanOps->init_get) {
		return (*(c->plugin.sanOps->init_get))(c, initiators, count);
	}

	return LSM_ERR_NO_SUPPORT;
}

int lsmVolumeList(lsmConnectPtr c, lsmVolumePtr **volumes, uint32_t *count)
{
	CONN_SETUP(c);

	if( c->plugin.sanOps && c->plugin.sanOps->volumes_get) {
		return (*(c->plugin.sanOps->volumes_get))(c, volumes, count);
	}

	return LSM_ERR_NO_SUPPORT;
}

int lsmVolumeCreate(lsmConnectPtr conn, lsmPoolPtr pool, char *volumeName,
                        uint64_t size, lsmProvisionType provisioning,
                        lsmVolumePtr *newVolume, int32_t *job)
{
	return LSM_ERR_NO_SUPPORT;
}

int lsmVolumeResize(lsmConnectPtr conn, lsmVolumePtr *volume,
                        uint64_t newSize, uint32_t *job)
{
	return LSM_ERR_NO_SUPPORT;
}

int lsmVolumeReplicate(lsmConnectPtr conn, lsmPoolPtr pool,
                        lsmReplicationType repType, lsmVolumePtr volumeSrc,
                        char *name, lsmVolumePtr **newReplicant, int32_t *job)
{
	return LSM_ERR_NO_SUPPORT;
}

int lsmVolumeDelete(lsmConnectPtr conn, lsmVolumePtr volume)
{
	return LSM_ERR_NO_SUPPORT;
}

int lsmVolumeStatus(lsmConnectPtr conn, lsmVolumePtr volume,
						lsmVolumeStatusType *status)
{
	return LSM_ERR_NO_SUPPORT;
}

int lsmVolumeOnline(lsmConnectPtr conn, lsmVolumePtr volume)
{
	return LSM_ERR_NO_SUPPORT;
}

int lsmVolumeOffline(lsmConnectPtr conn, lsmVolumePtr volume)
{
	return LSM_ERR_NO_SUPPORT;
}

int lsmAccessGroupList( lsmConnectPtr conn, lsmAccessGroupPtr **groups,
                        uint32_t *groupCount)
{
	return LSM_ERR_NO_SUPPORT;
}

int lsmAccessGroupCreate( lsmConnectPtr conn, char *name)
{
	return LSM_ERR_NO_SUPPORT;
}

int lsmAccessGroupDel( lsmConnectPtr conn, lsmAccessGroupPtr group)
{
	return LSM_ERR_NO_SUPPORT;
}

int lsmAccessGroupAddInitiator( lsmConnectPtr conn, lsmAccessGroupPtr group,
                                lsmInitiatorPtr initiator, lsmAccessType access)
{
	return LSM_ERR_NO_SUPPORT;
}

int lsmAccessGroupDelInitiator( lsmConnectPtr conn, lsmAccessGroupPtr group,
                                lsmInitiatorPtr initiator)
{
	return LSM_ERR_NO_SUPPORT;
}

