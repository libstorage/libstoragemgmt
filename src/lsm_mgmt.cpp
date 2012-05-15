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

#include <libstoragemgmt/libstoragemgmt.h>
#include <libstoragemgmt/libstoragemgmt_error.h>
#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include <libstoragemgmt/libstoragemgmt_types.h>
#include <stdio.h>
#include <string.h>
#include <libxml/uri.h>

#include "lsm_datatypes.hpp"
#include "lsm_convert.hpp"

/**
 * Common code to validate and initialize the connection.
 */
#define CONN_SETUP(c)   do {            \
    if(!LSM_IS_CONNECT(c)) {            \
        return LSM_ERR_INVALID_CONN;    \
    }                                   \
    lsmErrorFree(c->error);             \
    } while (0)

#define CHECK_NULL_JOB(x) !(x && *x == NULL)

int lsmConnectPassword(const char *uri, const char *password,
                        lsmConnectPtr *conn, uint32_t timeout, lsmErrorPtr *e)
{
    int rc = LSM_ERR_OK;
    lsmConnectPtr c = NULL;

    /* Password is optional */
    if(  uri == NULL || conn == NULL || e == NULL ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    *e = NULL;

    c = getConnection();
    if(c) {
        c->uri = xmlParseURI(uri);
        if( c->uri && c->uri->scheme ) {
            c->raw_uri = strdup(uri);
            if( c->raw_uri ) {
                rc = loadDriver(c, c->uri, password, timeout, e);
                if( rc == LSM_ERR_OK ) {
                    *conn = (lsmConnectPtr)c;
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            rc = LSM_ERR_URI_PARSE;
        }

        /*If we fail for any reason free resources associated with connection*/
        if( rc != LSM_ERR_OK ) {
            freeConnection(c);
        }
    } else {
        rc = LSM_ERR_NO_MEMORY;
    }
    return rc;
}

static int lsmErrorLog(lsmConnectPtr c, lsmErrorPtr error)
{
    if (!LSM_IS_CONNECT(c)) {
        return LSM_ERR_INVALID_CONN;
    }

    if (!LSM_IS_ERROR(error)) {
        return LSM_ERR_INVALID_ERR;
    }

    if (c->error) {
        lsmErrorFree(c->error);
        c->error = NULL;
    }

    c->error = error;
    return LSM_ERR_OK;
}

static lsmErrorNumber logException(lsmConnectPtr c, lsmErrorNumber error,
                                const char *message, const char *exception_msg)
{
    lsmErrorPtr err = lsmErrorCreate(error, LSM_ERR_DOMAIN_FRAME_WORK,
                                        LSM_ERR_LEVEL_ERROR, message,
                                        exception_msg, NULL,
                                        NULL, 0);
    if( err && c ) {
        lsmErrorLog(c, err);
    }
    return error;
}

static int rpc(lsmConnectPtr c, const char *method, const Value &parameters,
                Value &response) throw ()
{
    try {
        response = c->tp->rpc(method,parameters);
    } catch ( const ValueException &ve ) {
        return logException(c, LSM_ERROR_SERIALIZATION, "Serialization error",
                            ve.what());
    } catch ( const LsmException &le ) {
        return logException(c, (lsmErrorNumber)le.error_code, le.what(),
                            NULL);
    } catch (...) {
        return logException(c, LSM_ERR_INTERNAL_ERROR, "Unexpected exception",
                            "Unknown exception");
    }
    return LSM_ERR_OK;
}


int lsmConnectClose(lsmConnectPtr c)
{
    CONN_SETUP(c);
    std::map<std::string, Value> p;
    Value parameters(p);
    Value response;

    //No response data needed on shutdown.
    int rc = rpc(c, "shutdown", parameters, response);

    //Free the connection.
    freeConnection(c);
    return rc;
}

int lsmConnectSetTimeout(lsmConnectPtr c, uint32_t timeout)
{
    CONN_SETUP(c);
    std::map<std::string, Value> p;
    p["ms"] = Value(timeout);
    Value parameters(p);
    Value response;

    //No response data needed on set time out.
    return rpc(c, "set_time_out", parameters, response);
}

int lsmConnectGetTimeout(lsmConnectPtr c, uint32_t *timeout)
{
    CONN_SETUP(c);
    std::map<std::string, Value> p;
    Value parameters(p);
    Value response;

    int rc = rpc(c, "get_time_out", parameters, response);
    if( rc == LSM_ERR_OK ) {
        *timeout = response.asUint32_t();
    }
    return rc;
}

int lsmJobStatusGet( lsmConnectPtr c, const char *job,
                        lsmJobStatus *status, uint8_t *percentComplete,
                        lsmVolumePtr *vol)
{
    CONN_SETUP(c);

    if( NULL == job ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["job_id"] = Value(job);
    Value parameters(p);
    Value response;

    int rc = rpc(c, "job_status", parameters, response);
    if( LSM_ERR_OK == rc ) {
        //We get back an array [status, percent, volume]
        std::vector<Value> j = response.asArray();
        *status = (lsmJobStatus)j[0].asInt32_t();
        *percentComplete = (uint8_t)j[1].asUint32_t();

        if( Value::object_t ==  j[2].valueType() ) {
            *vol = valueToVolume(j[2]);
        } else {
            *vol = NULL;
        }
    }
    return rc;
}

int lsmJobFree(lsmConnectPtr c, char **job)
{
    CONN_SETUP(c);

    if( !(job != NULL && *job != NULL)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["job_id"] = Value(*job);
    Value parameters(p);
    Value response;

    /* Free the memory for the job id */
    free(*job);
    *job = NULL;

    return rpc(c, "job_free", parameters, response);
}

int lsmCapabilities(lsmConnectPtr c, lsmStorageCapabilitiesPtr *cap)
{
    return LSM_ERR_NO_SUPPORT;
}

int lsmPoolList(lsmConnectPtr c, lsmPoolPtr **poolArray,
                        uint32_t *count)
{
    CONN_SETUP(c);
    std::map<std::string, Value> p;
    Value parameters(p);
    Value response;

    int rc = rpc(c, "pools", parameters, response);
    if( LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
        std::vector<Value> pools = response.asArray();

        *count = pools.size();

        if( pools.size() ) {
            *poolArray = lsmPoolRecordAllocArray(pools.size());

            for( size_t i = 0; i < pools.size(); ++i ) {
                (*poolArray)[i] = valueToPool(pools[i]);
            }
        }
    }
    return rc;
}

int lsmInitiatorList(lsmConnectPtr c, lsmInitiatorPtr **initiators,
                                uint32_t *count)
{
    CONN_SETUP(c);
    std::map<std::string, Value> p;
    Value parameters(p);
    Value response;

    int rc = rpc(c, "initiators", parameters, response);
    if( LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
        std::vector<Value> inits = response.asArray();

        *count = inits.size();

        if( inits.size() ) {
            *initiators = lsmInitiatorRecordAllocArray(inits.size());

            for( size_t i = 0; i < inits.size(); ++i ) {
                (*initiators)[i] = valueToInitiator(inits[i]);
            }
        }
    }
    return rc;
}

int lsmVolumeList(lsmConnectPtr c, lsmVolumePtr **volumes, uint32_t *count)
{
    CONN_SETUP(c);
    std::map<std::string, Value> p;
    Value parameters(p);
    Value response;

    int rc = rpc(c, "volumes", parameters, response);
    if( LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
        std::vector<Value> vol = response.asArray();

        *count = vol.size();

        if( vol.size() ) {
            *volumes = lsmVolumeRecordAllocArray(vol.size());

            for( size_t i = 0; i < vol.size(); ++i ) {
                (*volumes)[i] = valueToVolume(vol[i]);
            }
        }
    }
    return rc;
}

typedef void* (*convert)(Value &v);

static void* parse_job_response(Value response, int &rc, char **job, convert conv)
{
    void *val = NULL;
    //We get an array back. first value is job, second is data of interest.
    if( Value::array_t == response.valueType() ) {
        std::vector<Value> r = response.asArray();
        if( Value::string_t == r[0].valueType()) {
            *job = strdup((r[0].asString()).c_str());
            if( *job ) {
                rc = LSM_ERR_JOB_STARTED;
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            rc = LSM_ERR_JOB_STARTED;
        }
        if( Value::object_t == r[1].valueType() ) {
            val = conv(r[1]);
        }
    }
    return val;
}

int lsmVolumeCreate(lsmConnectPtr c, lsmPoolPtr pool, const char *volumeName,
                        uint64_t size, lsmProvisionType provisioning,
                        lsmVolumePtr *newVolume, char **job)
{
    CONN_SETUP(c);

    if( !LSM_IS_POOL(pool)) {
        return LSM_ERR_INVALID_POOL;
    }

    if( NULL == volumeName || CHECK_NULL_JOB(job) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["pool"] = poolToValue(pool);
    p["volume_name"] = Value(volumeName);
    p["size_bytes"] = Value(size);
    p["provisioning"] = Value((int32_t)provisioning);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_create", parameters, response);
    if( LSM_ERR_OK == rc ) {
        *newVolume = (lsmVolumePtr)parse_job_response(response, rc, job,
                                                        (convert)valueToVolume);
    }
    return rc;
}

int lsmVolumeResize(lsmConnectPtr c, lsmVolumePtr volume,
                        uint64_t newSize, lsmVolumePtr *resizedVolume,
                        char **job)
{
    CONN_SETUP(c);

    if( !LSM_IS_VOL(volume)) {
        return LSM_ERR_INVALID_VOL;
    }

    if( !resizedVolume || CHECK_NULL_JOB(job) || newSize == 0 ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    //If you try to resize to same size, we will return error.
    if( ( newSize/volume->blockSize) == volume->numberOfBlocks ) {
        return LSM_ERR_VOLUME_SAME_SIZE;
    }

    std::map<std::string, Value> p;
    p["volume"] = volumeToValue(volume);
    p["new_size_bytes"] = Value(newSize);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_resize", parameters, response);
    if( LSM_ERR_OK == rc ) {
        *resizedVolume = (lsmVolumePtr)parse_job_response(response, rc, job,
                                                        (convert)valueToVolume);
    }
    return rc;
}

int lsmVolumeReplicate(lsmConnectPtr c, lsmPoolPtr pool,
                        lsmReplicationType repType, lsmVolumePtr volumeSrc,
                        const char *name, lsmVolumePtr *newReplicant, char **job)
{
    CONN_SETUP(c);

    if( !LSM_IS_POOL(pool) ) {
        return LSM_ERR_INVALID_POOL;
    }

    if( !LSM_IS_VOL(volumeSrc) ) {
        return LSM_ERR_INVALID_VOL;
    }

    if( !name || !(newReplicant) || CHECK_NULL_JOB(job) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["pool"] = poolToValue(pool);
    p["rep_type"] = Value((int32_t)repType);
    p["volume_src"] = volumeToValue(volumeSrc);
    p["name"] = Value(name);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_replicate", parameters, response);
    if( LSM_ERR_OK == rc ) {
        *newReplicant = (lsmVolumePtr)parse_job_response(response, rc, job,
                                                        (convert)valueToVolume);
    }
    return rc;

}

int lsmVolumeDelete(lsmConnectPtr c, lsmVolumePtr volume, char **job)
{
    CONN_SETUP(c);

    if( !LSM_IS_VOL(volume) ) {
        return LSM_ERR_INVALID_VOL;
    }

    if( CHECK_NULL_JOB(job) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["volume"] = volumeToValue(volume);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_delete", parameters, response);
    if( LSM_ERR_OK == rc ) {
        //We get a value back, either null or job id.
        if( Value::string_t == response.valueType() ) {
            *job = strdup(response.asString().c_str());

            if( *job ) {
                rc = LSM_ERR_JOB_STARTED;
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        }
    }
    return rc;

}

int lsmVolumeStatus(lsmConnectPtr conn, lsmVolumePtr volume,
                        lsmVolumeStatusType *status)
{
    return LSM_ERR_NO_SUPPORT;
}

int lsmInitiatorCreate(lsmConnectPtr c, const char *name, const char *id,
                            lsmInitiatorType type, lsmInitiatorPtr *init)
{
    CONN_SETUP(c);

    if( !name || !id || !init ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["name"] = Value(name);
    p["id"] = Value(id);
    p["id_type"] = Value((int32_t)type);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "initiator_create", parameters, response);
    if( LSM_ERR_OK == rc ) {
        //We get a value back, either null or initiator
        if( Value::object_t == response.valueType() ) {
            *init = valueToInitiator(response);
        }
    }
    return rc;
}

int lsmInitiatorDelete(lsmConnectPtr c, lsmInitiatorPtr i)
{
    CONN_SETUP(c);

    if( !LSM_IS_INIT(i) ){
        return LSM_ERR_INVALID_INIT;
    }

    std::map<std::string, Value> p;
    p["initiator"] = initiatorToValue(i);

    Value parameters(p);
    Value response;

    return rpc(c, "initiator_delete", parameters, response);
}

int lsmAccessGrant( lsmConnectPtr c, lsmInitiatorPtr i, lsmVolumePtr v,
                        lsmAccessType access, char **job)
{
    CONN_SETUP(c);

    if( !LSM_IS_INIT(i) ){
       return LSM_ERR_INVALID_INIT;
    }

    if( !LSM_IS_VOL(v)){
        return LSM_ERR_INVALID_VOL;
    }

    if( CHECK_NULL_JOB(job) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["initiator"] = initiatorToValue(i);
    p["volume"] = volumeToValue(v);
    p["access"] = Value((int32_t)access);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "access_grant", parameters, response);
    if( LSM_ERR_OK == rc ) {
        //We get a value back, either null or job id.
        if( Value::string_t == response.valueType() ) {
            *job = strdup(response.asString().c_str());

            if( *job ) {
                rc = LSM_ERR_JOB_STARTED;
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        }
    }
    return rc;
}

int lsmAccessRevoke( lsmConnectPtr c, lsmInitiatorPtr i, lsmVolumePtr v)
{
    CONN_SETUP(c);

    if(!LSM_IS_INIT(i)) {
        return LSM_ERR_INVALID_INIT;
    }

    if( !LSM_IS_VOL(v)) {
        return LSM_ERR_INVALID_VOL;
    }

    std::map<std::string, Value> p;
    p["initiator"] = initiatorToValue(i);
    p["volume"] = volumeToValue(v);

    Value parameters(p);
    Value response;

    return rpc(c, "access_revoke", parameters, response);
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

int lsmAccessGroupCreate( lsmConnectPtr conn, const char *name)
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

int lsmSystemList(lsmConnectPtr c, lsmSystemPtr **systems,
                                        uint32_t *systemCount)
{
    CONN_SETUP(c);
    std::map<std::string, Value> p;
    Value parameters(p);
    Value response;

    int rc = rpc(c, "systems", parameters, response);
    if( LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
        std::vector<Value> sys = response.asArray();

        *systemCount = sys.size();

        if( sys.size() ) {
            *systems = lsmSystemRecordAllocArray(sys.size());

            for( size_t i = 0; i < sys.size(); ++i ) {
                (*systems)[i] = valueToSystem(sys[i]);
            }
        }
    }
    return rc;
}
