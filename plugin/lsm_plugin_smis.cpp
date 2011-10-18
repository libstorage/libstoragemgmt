/*
 * Copyright (C) 2011 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Author: tasleson
 */

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include <libstoragemgmt/libstoragemgmt_plugin.h>

#include "smis.h"
#include "util/misc.h"

#ifdef  __cplusplus
extern "C" {
#endif

static char name[] = "Default smi-s plug-in";
static char version [] = "0.01";

static lsmErrorNumber logException(lsmConnectPtr c, lsmErrorNumber error,
                                const char *message, Exception &e)
{
    lsmErrorPtr err = lsmErrorCreate(error, LSM_ERR_DOMAIN_PLUG_IN, LSM_ERR_LEVEL_ERROR,
                                        message, e.getMessage().getCString(),
                                        NULL, NULL, 0);
    if( err && c ) {
        lsmErrorLog(c, err);
    }
    return error;
}

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
        return logException(c, LSM_ERR_PLUGIN_ERROR, "Error while getting time-out", e);
    }
    return rc;
}

static int cap(lsmConnectPtr c, lsmStorageCapabilitiesPtr *cap)
{
    return LSM_ERR_NO_SUPPORT;
}

static int jobStatus(lsmConnectPtr c, uint32_t jobNumber,
                        lsmJobStatus *status, uint8_t *percentComplete,
                        lsmVolumePtr *vol)
{
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        s->jobStatusVol(jobNumber, status, percentComplete, vol);
    } catch (Exception &e) {
        return logException(c, LSM_ERR_PLUGIN_ERROR, "Error while checking job status", e);
    }
    return LSM_ERR_OK;
}

static int jobFree(lsmConnectPtr c, uint32_t jobNumber)
{
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        return s->jobFree(jobNumber);
    } catch (Exception &e) {
        return logException(c, LSM_ERR_PLUGIN_ERROR, "Error while freeing job", e);
    }
    return LSM_ERR_OK;
}

static struct lsmMgmtOps mgmOps = {
    tmoSet,
    tmoGet,
    cap,
        jobStatus,
        jobFree,
};

static int pools(lsmConnectPtr c, lsmPoolPtr **poolArray,
                        uint32_t *count)
{
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        *poolArray = s->getStoragePools(count);
    } catch (Exception &e) {
        return logException(c, LSM_ERR_PLUGIN_ERROR, "Error while getting pools", e);
    }
    return LSM_ERR_OK;
}



static int initiators(lsmConnectPtr c, lsmInitiatorPtr **initArray,
                        uint32_t *count)
{
    Smis *s = (Smis *)lsmGetPrivateData(c);
    try {
        *initArray = s->getInitiators(count);
    } catch (Exception &ex) {
        return LSM_ERR_PLUGIN_ERROR;
    }
    return LSM_ERR_OK;
}

static int volumes(lsmConnectPtr c, lsmVolumePtr **volArray,
                        uint32_t *count)
{
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        *volArray = s->getVolumes(count);
    } catch (Exception &e) {
        return logException(c, LSM_ERR_PLUGIN_ERROR, "Error while getting volumes", e);
    }
    return LSM_ERR_OK;
}

static int createVolume( lsmConnectPtr c, lsmPoolPtr pool, char *volumeName,
                        uint64_t size, lsmProvisionType provisioning,
                        lsmVolumePtr *newVolume, uint32_t *job)
{
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        return s->createLun(pool, volumeName, size, provisioning, newVolume, job);
    } catch (Exception &e) {
        return logException(c, LSM_ERR_PLUGIN_ERROR, "Error while creating volume", e);
    }
    return LSM_ERR_OK;
}

static int createInit( lsmConnectPtr c, char *name, char *id,
                            lsmInitiatorType type, lsmInitiatorPtr *init)
{
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        return s->createInit(name, id, type, init);
    } catch (Exception &e) {
        return logException(c, LSM_ERR_PLUGIN_ERROR, "Error while creating initiator", e);
    }
    return LSM_ERR_OK;
}

static int accessGrant( lsmConnectPtr c, lsmInitiatorPtr i, lsmVolumePtr v,
                        lsmAccessType access, uint32_t *job)
{
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        return s->grantAccess(i,v, access, job);
    } catch (Exception &e) {
        return logException(c, LSM_ERR_PLUGIN_ERROR, "Error while granting access", e);
    }
    return LSM_ERR_OK;
}

static int accessRemove( lsmConnectPtr c, lsmInitiatorPtr i, lsmVolumePtr v)
{
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        return s->removeAccess(i,v );
    } catch (Exception &e) {
        return logException(c, LSM_ERR_PLUGIN_ERROR, "Error while removing access", e);
    }
    return LSM_ERR_OK;
}

static int replicateVolume( lsmConnectPtr c, lsmPoolPtr pool,
                        lsmReplicationType repType, lsmVolumePtr volumeSrc,
                        char *name, lsmVolumePtr *newReplicant, uint32_t *job)
{
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        return s->replicateLun(pool, repType, volumeSrc, name, newReplicant, job);
    } catch (Exception &e) {
        return logException(c, LSM_ERR_PLUGIN_ERROR, "Error while replicating volume", e);
    }
    return LSM_ERR_OK;
}

static int resizeVolume(lsmConnectPtr c, lsmVolumePtr volume,
                                uint64_t newSize, lsmVolumePtr *resizedVolume,
                                uint32_t *job)
{
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        return s->resizeVolume(volume, newSize, resizedVolume, job);
    } catch (Exception &e) {
        return logException(c, LSM_ERR_PLUGIN_ERROR, "Error while re-sizing volume", e);
    }
    return LSM_ERR_OK;
}

static int deleteVolume( lsmConnectPtr c, lsmVolumePtr volume, uint32_t *job)
{
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        return s->deleteVolume(volume, job);
    } catch (Exception &e) {
        return logException(c, LSM_ERR_PLUGIN_ERROR, "Error while deleting volume", e);
    }
    return LSM_ERR_OK;
}

static struct lsmSanOps sanOps = {
    pools,
    initiators,
    volumes,
    createVolume,
    replicateVolume,
    resizeVolume,
    deleteVolume,
    createInit,
    accessGrant,
    accessRemove,
};

int lsmPluginRegister( lsmConnectPtr c, xmlURIPtr uri, char *password,
                        uint32_t timeout, lsmErrorPtr *e )
{
    int rc = LSM_ERR_OK;
    Smis *s = NULL;
    std::string ns;
    String pass;

    /*Open pegasus does not like NULL for the password */
    if( password ) {
        pass = String(password);
    } else {
        pass = String("");
    }

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
        s = new Smis(uri->server, uri->port, ns.c_str(), uri->user, pass,
                        timeout);

        rc = lsmRegisterPlugin( c, name, version, s, &mgmOps, &sanOps, NULL, NULL);
    } catch (Exception &ex) {
        /**
         * We may need to parse the exception text to return a better return
         * code of what actually went wrong here.
         */

        *e = LSM_ERROR_CREATE_PLUGIN_EXCEPTION(LSM_ERR_PLUGIN_REGISTRATION,
                                                "Registration error",
                                                ex.getMessage().getCString());
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

#ifdef  __cplusplus
}
#endif


