/*
 * Copyright (C) 2011-2012 Red Hat, Inc.
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

#include "smis.h"
#include "util/misc.h"
#include <syslog.h>

#ifdef  __cplusplus
extern "C" {
#endif

static char name[] = "Default smi-s plug-in";
static char version [] = "0.01";

static lsmErrorNumber logException(lsmPluginPtr p, lsmErrorNumber error,
                                const char *message, Exception &e)
{
    const char* exception_msg = e.getMessage().getCString();

    lsmErrorPtr err = lsmErrorCreate(error, LSM_ERR_DOMAIN_PLUG_IN,
                                        LSM_ERR_LEVEL_ERROR, message,
                                        exception_msg, NULL,
                                        NULL, 0);
    if( err && p ) {
        lsmPluginErrorLog(p, err);
    }
    return error;
}

static int tmoSet(lsmPluginPtr c, uint32_t timeout )
{
    int rc = LSM_ERR_OK;
    try {
        ((Smis *)lsmGetPrivateData(c))->setTmo(timeout);
    } catch ( Exception &e) {
        rc = LSM_ERR_PLUGIN_ERROR;
    }
    return rc;
}

static int tmoGet(lsmPluginPtr c, uint32_t *timeout)
{
    int rc = LSM_ERR_OK;
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        *timeout = s->getTmo();
    } catch ( Exception &e) {
        rc = logException(c, LSM_ERR_PLUGIN_ERROR, "Error while getting time-out", e);
    }
    return rc;
}

static int cap(lsmPluginPtr c, lsmStorageCapabilitiesPtr *cap)
{
    return LSM_ERR_NO_SUPPORT;
}

static int jobStatus(lsmPluginPtr c, const char *job_id,
                        lsmJobStatus *status, uint8_t *percentComplete,
                        lsmVolumePtr *vol)
{
    int rc = LSM_ERR_OK;
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        rc = s->jobStatusVol(job_id, status, percentComplete, vol);
    } catch (Exception &e) {
        rc = logException(c, LSM_ERR_PLUGIN_ERROR, "Error while checking job status", e);
    }
    return rc;
}

static int jobFree(lsmPluginPtr c, char *jobNumber)
{
    int rc = LSM_ERR_OK;
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        rc = s->jobFree(jobNumber);
    } catch (Exception &e) {
        rc = logException(c, LSM_ERR_PLUGIN_ERROR, "Error while freeing job", e);
    }
    return rc;
}

static struct lsmMgmtOps mgmOps = {
    tmoSet,
    tmoGet,
    cap,
    jobStatus,
    jobFree,
};

static int pools(lsmPluginPtr c, lsmPoolPtr **poolArray,
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



static int initiators(lsmPluginPtr c, lsmInitiatorPtr **initArray,
                        uint32_t *count)
{
    Smis *s = (Smis *)lsmGetPrivateData(c);
    try {
        *initArray = s->getInitiators(count);
    } catch (Exception &ex) {
        return logException(c, LSM_ERR_PLUGIN_ERROR, "Error while getting initiators", ex);
    }
    return LSM_ERR_OK;
}

static int volumes(lsmPluginPtr c, lsmVolumePtr **volArray,
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

static int createVolume( lsmPluginPtr c, lsmPoolPtr pool, const char *volumeName,
                        uint64_t size, lsmProvisionType provisioning,
                        lsmVolumePtr *newVolume, char **job)
{
    int rc = LSM_ERR_OK;
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        rc = s->createLun(pool, volumeName, size, provisioning, newVolume, job);
    } catch (Exception &e) {
        rc = logException(c, LSM_ERR_PLUGIN_ERROR, "Error while creating volume", e);
    }
    return rc;
}

static int createInit( lsmPluginPtr c, const char *name, const char *id,
                            lsmInitiatorType type, lsmInitiatorPtr *init)
{
    int rc = LSM_ERR_OK;
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        rc = s->createInit(name, id, type, init);
    } catch (Exception &e) {
        rc = logException(c, LSM_ERR_PLUGIN_ERROR, "Error while creating initiator", e);
    }
    return rc;
}

static int deleteInit( lsmPluginPtr c, lsmInitiatorPtr init)
{
    int rc = LSM_ERR_OK;
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        rc = s->deleteInit(init);
    } catch (Exception &e) {
        rc = logException(c, LSM_ERR_PLUGIN_ERROR, "Error while deleting initiator", e);
    }
    return rc;
}

static int accessGrant( lsmPluginPtr c, lsmInitiatorPtr i, lsmVolumePtr v,
                        lsmAccessType access, char **job)
{
    int rc = LSM_ERR_OK;
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        rc = s->grantAccess(i,v, access, job);
    } catch (Exception &e) {
        rc = logException(c, LSM_ERR_PLUGIN_ERROR, "Error while granting access", e);
    }
    return rc;
}

static int accessRemove( lsmPluginPtr c, lsmInitiatorPtr i, lsmVolumePtr v)
{
    int rc = LSM_ERR_OK;
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        rc = s->removeAccess(i,v );
    } catch (Exception &e) {
        rc = logException(c, LSM_ERR_PLUGIN_ERROR, "Error while removing access", e);
    }
    return rc;
}

static int replicateVolume( lsmPluginPtr c, lsmPoolPtr pool,
                        lsmReplicationType repType, lsmVolumePtr volumeSrc,
                        const char *name, lsmVolumePtr *newReplicant, char **job)
{
    int rc = LSM_ERR_OK;
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        rc = s->replicateLun(pool, repType, volumeSrc, name, newReplicant, job);
    } catch (Exception &e) {
        rc = logException(c, LSM_ERR_PLUGIN_ERROR, "Error while replicating volume", e);
    }
    return rc;
}

static int resizeVolume(lsmPluginPtr c, lsmVolumePtr volume,
                                uint64_t newSize, lsmVolumePtr *resizedVolume,
                                char **job)
{
    int rc = LSM_ERR_OK;
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        rc = s->resizeVolume(volume, newSize, resizedVolume, job);
    } catch (Exception &e) {
        rc = logException(c, LSM_ERR_PLUGIN_ERROR, "Error while re-sizing volume", e);
    }
    return rc;
}

static int deleteVolume( lsmPluginPtr c, lsmVolumePtr volume, char **job)
{
    int rc = LSM_ERR_OK;
    Smis *s = (Smis *)lsmGetPrivateData(c);

    try {
        rc = s->deleteVolume(volume, job);
    } catch (Exception &e) {
        rc = logException(c, LSM_ERR_PLUGIN_ERROR, "Error while deleting volume", e);
    }
    return rc;
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
    deleteInit,
    accessGrant,
    accessRemove,
};

int load( lsmPluginPtr c, xmlURIPtr uri, const char *password,
                        uint32_t timeout )
{
    int rc = LSM_ERR_OK;
    Smis *s = NULL;
    std::string ns;
    std::string pass;
    std::string user;

    if(uri->server == NULL) {
        return LSM_ERR_MISSING_HOST;
    }

    if(uri->user == NULL) {
        user = std::string();
    } else {
        user = std::string(uri->user);
    }

    if( uri->port == 0 ) {
        return LSM_ERR_MISSING_PORT;
    }

    /*Open pegasus does not like NULL for the password */
    if( password ) {
        pass = std::string(password);
    } else {
        pass =  std::string("");
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
        s = new Smis(uri->server, uri->port, ns.c_str(), user.c_str(),
                        pass.c_str(), timeout);

        rc = lsmRegisterPlugin( c, name, version, s, &mgmOps, &sanOps, NULL, NULL);
    } catch (Exception &ex) {
        /**
         * We may need to parse the exception text to return a better return
         * code of what actually went wrong here.
         */
        logException(c, LSM_ERR_PLUGIN_REGISTRATION, "Registration error", ex);
        rc = LSM_ERR_PLUGIN_REGISTRATION;
    }

    return rc;
}

int unload( lsmPluginPtr c )
{
    Smis *s = (Smis *)lsmGetPrivateData(c);
    delete(s);
    return LSM_ERR_OK;
}

int main(int argc, char *argv[] )
{
    syslog(LOG_USER|LOG_NOTICE, "Warning: Plug-in deprecated, use smispy instead!");
    return lsmPluginInit(argc, argv, load, unload);
}

#ifdef  __cplusplus
}
#endif


