/*
 * Copyright 2011, Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include <libstoragemgmt/libstoragemgmt_plugin.h>

#include "smis.h"
#include "util/misc.h"

#ifdef	__cplusplus
extern "C" {
#endif

static char name[] = "Default smi-s plug-in";
static char version [] = "0.01";

static int tmoSet(lsmConnectPtr c, uint32_t timeout )
{
    int rc = LSM_ERR_OK;
    try {
        ((Smis *)lsmGetPrivateData(c))->setTmo(timeout);
    } catch ( Exception &e) {
        rc = LSM_ERR_PLUGIN_ERROR;
    }
    return rc;
}

static int tmoGet(lsmConnectPtr c, uint32_t *timeout)
{
    int rc = LSM_ERR_OK;
    try {
        *timeout = ((Smis *)lsmGetPrivateData(c))->getTmo();
    } catch ( Exception &e) {
        rc = LSM_ERR_PLUGIN_ERROR;
    }
    return rc;
}

static int cap(lsmConnectPtr c, lsmStorageCapabilitiesPtr *cap)
{
    return LSM_ERR_NO_SUPPORT;
}

static struct lsmMgmtOps mgmOps = {
	tmoSet,
	tmoGet,
	cap,
};

static int volumes(lsmConnectPtr c, lsmPoolPtr **poolArray,
                        uint32_t *count)
{
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        *poolArray = s->getStoragePools(count);
    } catch (Exception &e) {
        //TODO Create error with additional information.
        return LSM_ERR_PLUGIN_ERROR;
    }
    return LSM_ERR_OK;
}

static struct lsmSanOps sanOps = {
    volumes,
};

int lsmPluginRegister( lsmConnectPtr c, xmlURIPtr uri, char *password,
                        uint32_t timeout, lsmErrorPtr *e )
{
    int rc = LSM_ERR_OK;
    Smis *s = NULL;
    std::string ns;

    /** pull the name space! */
    if( !uri->query_raw ) {
        return LSM_ERR_URI_PARSE;
    } else {
        ns = LSM::getValue(uri->query_raw, "namespace");

        if( ns.length() == 0 ) {
            return LSM_ERR_URI_PARSE;
        }
    }

    try {
        s = new Smis(uri->server, uri->port, ns.c_str(), uri->user, password,
                        timeout);

        rc = lsmRegisterPlugin( c, name, version, s, &mgmOps, &sanOps, NULL, NULL);
    } catch (Exception &ex) {
        *e = LSM_ERROR_CREATE_PLUGIN_EXCEPTION(LSM_ERR_PLUGIN_REGISTRATION, "Registration error", "FUBR");
        rc = LSM_ERR_PLUGIN_REGISTRATION;
    }

    return rc;
}

int lsmPluginUnregister( lsmConnectPtr c )
{
    Smis *s = (Smis *)lsmGetPrivateData(c);
    delete(s);
    return LSM_ERR_OK;
}

#ifdef	__cplusplus
}
#endif


