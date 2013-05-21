/*
 * Copyright (C) 2011-2013 Red Hat, Inc.
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

/**
 * Strings are non null with a len >= 1
 */
#define CHECK_STR(x) ( !(x) || !strlen(x) )

/**
 * When we pass in a pointer for an out value we want to make sure that
 * the pointer isn't null, and that the dereferenced value is != NULL to prevent
 * memory leaks.
 */
#define CHECK_RP(x)  (!(x) || *(x) != NULL)

int lsmConnectPassword(const char *uri, const char *password,
                        lsmConnect **conn, uint32_t timeout, lsmErrorPtr *e,
                        lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    lsmConnect *c = NULL;

    /* Password is optional */
    if(  CHECK_STR(uri) || CHECK_RP(conn) || !timeout || CHECK_RP(e) ||
         LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    c = getConnection();
    if(c) {
        c->uri = xmlParseURI(uri);
        if( c->uri && c->uri->scheme ) {
            c->raw_uri = strdup(uri);
            if( c->raw_uri ) {
                rc = loadDriver(c, c->uri, password, timeout, e, flags);
                if( rc == LSM_ERR_OK ) {
                    *conn = (lsmConnect *)c;
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            rc = LSM_ERR_INVALID_URI;
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

static int lsmErrorLog(lsmConnect *c, lsmErrorPtr error)
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

static lsmErrorNumber logException(lsmConnect *c, lsmErrorNumber error,
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

static int rpc(lsmConnect *c, const char *method, const Value &parameters,
                Value &response) throw ()
{
    try {
        response = c->tp->rpc(method,parameters);
    } catch ( const ValueException &ve ) {
        return logException(c, LSM_ERR_TRANSPORT_SERIALIZATION, "Serialization error",
                            ve.what());
    } catch ( const LsmException &le ) {
        return logException(c, (lsmErrorNumber)le.error_code, le.what(),
                            NULL);
    } catch ( EOFException &eof ) {
        return logException(c, LSM_ERR_TRANSPORT_COMMUNICATION, "Plug-in died",
                                "Check syslog");
    } catch (...) {
        return logException(c, LSM_ERR_INTERNAL_ERROR, "Unexpected exception",
                            "Unknown exception");
    }
    return LSM_ERR_OK;
}

static int jobCheck( int rc, Value &response, char **job )
{
    if( LSM_ERR_OK == rc ) {
        //We get a value back, either null or job id.
        if( Value::string_t == response.valueType() ) {
            *job = strdup(response.asString().c_str());

            if( *job ) {
                rc = LSM_ERR_JOB_STARTED;
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            *job = NULL;
        }
    }
    return rc;
}

static int getAccessGroups( int rc, Value &response, lsmAccessGroup **groups[],
                            uint32_t *count)
{
    if( LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
        *groups = valueToAccessGroupList(response, count);
    }
    return rc;
}

int lsmConnectClose(lsmConnect *c, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    //No response data needed on shutdown.
    int rc = rpc(c, "shutdown", parameters, response);

    //Free the connection.
    freeConnection(c);
    return rc;
}

int LSM_DLL_EXPORT lsmPluginGetInfo(lsmConnect *c, char **desc,
                                        char **version, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if( CHECK_RP(desc) || CHECK_RP(version) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    int rc = rpc(c, "plugin_info", parameters, response);

    if( rc == LSM_ERR_OK ) {
        std::vector<Value> j = response.asArray();
        *desc = strdup(j[0].asC_str());
        *version = strdup(j[1].asC_str());

        if( !*desc || !*version ) {
            rc = LSM_ERR_NO_MEMORY;
            free(*desc);
            free(*version);
        }
    }

    return rc;
}

int lsmConnectSetTimeout(lsmConnect *c, uint32_t timeout, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["ms"] = Value(timeout);
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    //No response data needed on set time out.
    return rpc(c, "set_time_out", parameters, response);
}

int lsmConnectGetTimeout(lsmConnect *c, uint32_t *timeout, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    int rc = rpc(c, "get_time_out", parameters, response);
    if( rc == LSM_ERR_OK ) {
        *timeout = response.asUint32_t();
    }
    return rc;
}

static int jobStatus( lsmConnect *c, const char *job,
                        lsmJobStatus *status, uint8_t *percentComplete,
                        Value &returned_value, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !job || !status || !percentComplete ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["job_id"] = Value(job);
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    int rc = rpc(c, "job_status", parameters, response);
    if( LSM_ERR_OK == rc ) {
        //We get back an array [status, percent, volume]
        std::vector<Value> j = response.asArray();
        *status = (lsmJobStatus)j[0].asInt32_t();
        *percentComplete = (uint8_t)j[1].asUint32_t();

        returned_value = j[2];
    }
    return rc;
}

int lsmJobStatusGet(lsmConnect *c, const char *job_id,
                    lsmJobStatus *status, uint8_t *percentComplete,
                    lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    Value rv;
    return jobStatus(c, job_id, status, percentComplete, rv, flags);
}

int lsmJobStatusVolumeGet( lsmConnect *c, const char *job,
                        lsmJobStatus *status, uint8_t *percentComplete,
                        lsmVolume **vol, lsmFlag_t flags)
{
    Value rv;

    CONN_SETUP(c);

    if( CHECK_RP(vol) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    int rc = jobStatus(c, job, status, percentComplete, rv, flags);

    if( LSM_ERR_OK == rc ) {
        if( Value::object_t ==  rv.valueType() ) {
            *vol = valueToVolume(rv);
        } else {
            *vol = NULL;
        }
    }
    return rc;
}

int lsmJobStatusFsGet(lsmConnect *c, const char *job,
                                lsmJobStatus *status, uint8_t *percentComplete,
                                lsmFs **fs, lsmFlag_t flags)
{
    Value rv;

    if( CHECK_RP(fs) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    int rc = jobStatus(c, job, status, percentComplete, rv, flags);

    if( LSM_ERR_OK == rc ) {
        if( Value::object_t ==  rv.valueType() ) {
            *fs = valueToFs(rv);
        } else {
            *fs = NULL;
        }
    }
    return rc;
}

int lsmJobStatusSsGet(lsmConnect *c, const char *job,
                                lsmJobStatus *status, uint8_t *percentComplete,
                                lsmSs **ss, lsmFlag_t flags)
{
    Value rv;

    if( CHECK_RP(ss) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    int rc = jobStatus(c, job, status, percentComplete, rv, flags);

    if( LSM_ERR_OK == rc ) {
        if( Value::object_t ==  rv.valueType() ) {
            *ss = valueToSs(rv);
        } else {
            *ss = NULL;
        }
    }
    return rc;
}

int lsmJobFree(lsmConnect *c, char **job, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( job == NULL || strlen(*job) < 1 || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["job_id"] = Value(*job);
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    int rc = rpc(c, "job_free", parameters, response);

    if( LSM_ERR_OK == rc ) {
        /* Free the memory for the job id */
        free(*job);
        *job = NULL;
    }
    return rc;
}

int lsmCapabilities(lsmConnect *c, lsmSystem *system,
                    lsmStorageCapabilities **cap, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_SYSTEM(system) ) {
        return LSM_ERR_INVALID_SYSTEM;
    }

    if( CHECK_RP(cap) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;

    p["system"] = systemToValue(system);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "capabilities", parameters, response);

    if( LSM_ERR_OK == rc && Value::object_t == response.valueType() ) {
        *cap = valueToCapabilities(response);
    }

    return rc;
}

int lsmPoolList(lsmConnect *c, lsmPool **poolArray[],
                        uint32_t *count, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !poolArray || !count || CHECK_RP(poolArray) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);
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

static int get_initiator_array(int rc, Value &response,
                                lsmInitiator **initiators[], uint32_t *count)
{
    if( LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
        std::vector<Value> inits = response.asArray();

        *count = inits.size();

        if( inits.size() ) {

            *initiators = lsmInitiatorRecordAllocArray(inits.size());

            if( *initiators ) {


                for( size_t i = 0; i < inits.size(); ++i ) {
                    (*initiators)[i] = valueToInitiator(inits[i]);
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        }
    }
    return rc;
}

int lsmInitiatorList(lsmConnect *c, lsmInitiator **initiators[],
                                uint32_t *count, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !initiators || !count || CHECK_RP(initiators) ||
        LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    int rc = rpc(c, "initiators", parameters, response);
    return get_initiator_array(rc, response, initiators, count);
}

static int get_volume_array(int rc, Value response,
                            lsmVolume **volumes[], uint32_t *count)
{
    if( LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
        std::vector<Value> vol = response.asArray();

        *count = vol.size();

        if( vol.size() ) {
            *volumes = lsmVolumeRecordAllocArray(vol.size());

            if( *volumes ){
                for( size_t i = 0; i < vol.size(); ++i ) {
                    (*volumes)[i] = valueToVolume(vol[i]);
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        }
    }
    return rc;
}


int lsmVolumeList(lsmConnect *c, lsmVolume **volumes[], uint32_t *count,
                    lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !volumes || !count || CHECK_RP(volumes) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volumes", parameters, response);
    return get_volume_array(rc, response, volumes, count);
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

int lsmVolumeCreate(lsmConnect *c, lsmPool *pool, const char *volumeName,
                        uint64_t size, lsmProvisionType provisioning,
                        lsmVolume **newVolume, char **job, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_POOL(pool)) {
        return LSM_ERR_INVALID_POOL;
    }

    if( CHECK_STR(volumeName) || !size || CHECK_RP(newVolume) ||
        CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags) ){
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["pool"] = poolToValue(pool);
    p["volume_name"] = Value(volumeName);
    p["size_bytes"] = Value(size);
    p["provisioning"] = Value((int32_t)provisioning);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_create", parameters, response);
    if( LSM_ERR_OK == rc ) {
        *newVolume = (lsmVolume *)parse_job_response(response, rc, job,
                                                        (convert)valueToVolume);
    }
    return rc;
}

int lsmVolumeResize(lsmConnect *c, lsmVolume *volume,
                        uint64_t newSize, lsmVolume **resizedVolume,
                        char **job, lsmFlag_t flags )
{
    CONN_SETUP(c);

    if( !LSM_IS_VOL(volume)) {
        return LSM_ERR_INVALID_VOL;
    }

    if( !newSize || CHECK_RP(resizedVolume) || CHECK_RP(job)
        || newSize == 0 || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    //If you try to resize to same size, we will return error.
    if( ( newSize/volume->blockSize) == volume->numberOfBlocks ) {
        return LSM_ERR_VOLUME_SAME_SIZE;
    }

    std::map<std::string, Value> p;
    p["volume"] = volumeToValue(volume);
    p["new_size_bytes"] = Value(newSize);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_resize", parameters, response);
    if( LSM_ERR_OK == rc ) {
        *resizedVolume = (lsmVolume *)parse_job_response(response, rc, job,
                                                        (convert)valueToVolume);
    }
    return rc;
}

int lsmVolumeReplicate(lsmConnect *c, lsmPool *pool,
                        lsmReplicationType repType, lsmVolume *volumeSrc,
                        const char *name, lsmVolume **newReplicant,
                        char **job, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( pool && !LSM_IS_POOL(pool) ) {
        return LSM_ERR_INVALID_POOL;
    }

    if( !LSM_IS_VOL(volumeSrc) ) {
        return LSM_ERR_INVALID_VOL;
    }

    if(CHECK_STR(name) || CHECK_RP(newReplicant) || CHECK_RP(job) ||
        LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["pool"] = poolToValue(pool);
    p["rep_type"] = Value((int32_t)repType);
    p["volume_src"] = volumeToValue(volumeSrc);
    p["name"] = Value(name);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_replicate", parameters, response);
    if( LSM_ERR_OK == rc ) {
        *newReplicant = (lsmVolume *)parse_job_response(response, rc, job,
                                                        (convert)valueToVolume);
    }
    return rc;

}

int lsmVolumeReplicateRangeBlockSize(lsmConnect *c, lsmSystem *system,
                                        uint32_t *bs, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !bs || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if( !LSM_IS_SYSTEM(system) ) {
        return LSM_ERR_INVALID_SYSTEM;
    }

    std::map<std::string, Value> p;
    p["system"] = systemToValue(system);
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_replicate_range_block_size", parameters, response);
    if( LSM_ERR_OK == rc ) {
        if( Value::numeric_t == response.valueType() ) {
            *bs = response.asUint32_t();
        }
    }
    return rc;
}


int lsmVolumeReplicateRange(lsmConnect *c,
                                                lsmReplicationType repType,
                                                lsmVolume *source,
                                                lsmVolume *dest,
                                                lsmBlockRange **ranges,
                                                uint32_t num_ranges,
                                                char **job, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_VOL(source) || !LSM_IS_VOL(dest) ) {
        return LSM_ERR_INVALID_VOL;
    }

    if( !ranges || !num_ranges || CHECK_RP(job) ||
        LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["rep_type"] = Value((int32_t)repType);
    p["volume_src"] = volumeToValue(source);
    p["volume_dest"] = volumeToValue(dest);
    p["ranges"] = blockRangeListToValue(ranges, num_ranges);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_replicate_range", parameters, response);
    return jobCheck(rc, response, job);
}

int lsmVolumeDelete(lsmConnect *c, lsmVolume *volume, char **job,
                    lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_VOL(volume) ) {
        return LSM_ERR_INVALID_VOL;
    }

    if( CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["volume"] = volumeToValue(volume);
    p["flags"] = Value(flags);

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

int lsmISCSIChapAuth(lsmConnect *c, lsmInitiator *initiator,
                                const char *username, const char *password,
                                const char *out_user, const char *out_password,
                                lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_INIT(initiator) ) {
        return LSM_ERR_INVALID_INIT;
    }

    if(LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["initiator"] = initiatorToValue(initiator);
    p["in_user"] = Value(username);
    p["in_password"] = Value(password);
    p["out_user"] = Value(out_user);
    p["out_password"] = Value(out_password);
    p["flags"] = Value(flags);


    Value parameters(p);
    Value response;

    return rpc(c, "iscsi_chap_auth", parameters, response);
}

int lsmInitiatorGrant(lsmConnect *c,
                    const char *initiator_id,
                    lsmInitiatorType initiator_type,
                    lsmVolume *volume,
                    lsmAccessType access,
                    lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_VOL(volume)){
        return LSM_ERR_INVALID_VOL;
    }

    if( CHECK_STR(initiator_id) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["initiator_id"] = Value(initiator_id);
    p["initiator_type"] = Value((int32_t)initiator_type);
    p["volume"] = volumeToValue(volume);
    p["access"] = Value((int32_t)access);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    return rpc(c, "initiator_grant", parameters, response);
}

int lsmInitiatorRevoke( lsmConnect *c, lsmInitiator *i, lsmVolume *v,
                        lsmFlag_t flags)
{
    CONN_SETUP(c);

    if(!LSM_IS_INIT(i)) {
        return LSM_ERR_INVALID_INIT;
    }

    if( !LSM_IS_VOL(v) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_VOL;
    }

    std::map<std::string, Value> p;
    p["initiator"] = initiatorToValue(i);
    p["volume"] = volumeToValue(v);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    return rpc(c, "initiator_revoke", parameters, response);
}

int lsmVolumesAccessibleByInitiator(lsmConnect *c,
                                        lsmInitiator *initiator,
                                        lsmVolume **volumes[],
                                        uint32_t *count, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_INIT(initiator) ){
        return LSM_ERR_INVALID_INIT;
    }

    if( CHECK_RP(volumes) || !count || LSM_FLAG_UNUSED_CHECK(flags) ){
        return LSM_ERR_INVALID_ARGUMENT;
    }


    std::map<std::string, Value> p;
    p["initiator"] = initiatorToValue(initiator);
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    int rc = rpc(c, "volumes_accessible_by_initiator", parameters, response);
    return get_volume_array(rc, response, volumes, count);
}

int lsmInitiatorsGrantedToVolume(lsmConnect *c, lsmVolume *volume,
                                lsmInitiator **initiators[], uint32_t *count,
                                lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_VOL(volume) ) {
        return LSM_ERR_INVALID_VOL;
    }

    if( CHECK_RP(initiators) || !count || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["volume"] = volumeToValue(volume);
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    int rc = rpc(c, "initiators_granted_to_volume", parameters, response);
    return get_initiator_array(rc, response, initiators, count);
}


static int online_offline(lsmConnect *c, lsmVolume *v,
                            const char* operation, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_VOL(v)) {
        return LSM_ERR_INVALID_VOL;
    }

    if ( LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["volume"] = volumeToValue(v);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;
    return rpc(c, operation, parameters, response);
}

int lsmVolumeOnline(lsmConnect *c, lsmVolume *volume, lsmFlag_t flags)
{
    return online_offline(c, volume, "volume_online", flags);
}

int lsmVolumeOffline(lsmConnect *c, lsmVolume *volume, lsmFlag_t flags)
{
    return online_offline(c, volume, "volume_offline", flags);
}

int lsmAccessGroupList( lsmConnect *c, lsmAccessGroup **groups[],
                        uint32_t *groupCount, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !groups || !groupCount || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    int rc = rpc(c, "access_group_list", parameters, response);
    return getAccessGroups(rc, response, groups, groupCount);
}

int lsmAccessGroupCreate(lsmConnect *c, const char *name,
                            const char *initiator_id, lsmInitiatorType id_type,
                            const char *system_id,
                            lsmAccessGroup **access_group, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( CHECK_STR(name) || CHECK_STR(initiator_id) || CHECK_STR(system_id)
        || CHECK_RP(access_group) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["name"] = Value(name);
    p["initiator_id"] = Value(initiator_id);
    p["id_type"] = Value((int32_t)id_type);
    p["system_id"] = Value(system_id);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    *access_group = NULL;

    int rc = rpc(c, "access_group_create", parameters, response);
    if( LSM_ERR_OK == rc ) {
        //We should be getting a value back.
        if( Value::object_t == response.valueType() ) {
            *access_group = valueToAccessGroup(response);
        }
    }
    return rc;
}

int lsmAccessGroupDel(lsmConnect *c, lsmAccessGroup *group, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_ACCESS_GROUP(group) ){
        return LSM_ERR_INVALID_ACCESS_GROUP;
    }

    if( LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["group"] = accessGroupToValue(group);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    return rpc(c, "access_group_del", parameters, response);
}

int lsmAccessGroupAddInitiator(lsmConnect *c,
                                lsmAccessGroup *group,
                                const char *initiator_id,
                                lsmInitiatorType id_type,
                                lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_ACCESS_GROUP(group) ) {
        return LSM_ERR_INVALID_ACCESS_GROUP;
    }

    if( CHECK_STR(initiator_id) ||
        LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["group"] = accessGroupToValue(group);
    p["initiator_id"] = initiator_id;
    p["id_type"] = Value((int32_t)id_type);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    return rpc(c, "access_group_add_initiator", parameters, response);
}

int lsmAccessGroupDelInitiator(lsmConnect *c, lsmAccessGroup *group,
                                const char* initiator_id, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_ACCESS_GROUP(group)) {
        return LSM_ERR_INVALID_ACCESS_GROUP;
    }

    if( CHECK_STR(initiator_id) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["group"] = accessGroupToValue(group);
    p["initiator_id"] = Value(initiator_id);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    return rpc(c, "access_group_del_initiator", parameters, response);
}

int lsmAccessGroupGrant(lsmConnect *c, lsmAccessGroup *group,
                                            lsmVolume *volume,
                                            lsmAccessType access,
                                            lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_ACCESS_GROUP(group)) {
        return LSM_ERR_INVALID_ACCESS_GROUP;
    }

    if( !LSM_IS_VOL(volume) ) {
        return LSM_ERR_INVALID_VOL;
    }

    if( LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["group"] = accessGroupToValue(group);
    p["volume"] = volumeToValue(volume);
    p["access"] = Value((int32_t)access);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    return rpc(c, "access_group_grant", parameters, response);
}


int lsmAccessGroupRevoke(lsmConnect *c, lsmAccessGroup *group,
                                            lsmVolume *volume,
                                            lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_ACCESS_GROUP(group)) {
        return LSM_ERR_INVALID_ACCESS_GROUP;
    }

    if( !LSM_IS_VOL(volume)) {
        return LSM_ERR_INVALID_VOL;
    }

    if( LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["group"] = accessGroupToValue(group);
    p["volume"] = volumeToValue(volume);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    return rpc(c, "access_group_revoke", parameters, response);
}

int lsmVolumesAccessibleByAccessGroup(lsmConnect *c,
                                        lsmAccessGroup *group,
                                        lsmVolume **volumes[],
                                        uint32_t *count, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_ACCESS_GROUP(group)) {
        return LSM_ERR_INVALID_ACCESS_GROUP;
    }

    if( !volumes || !count || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["group"] = accessGroupToValue(group);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volumes_accessible_by_access_group", parameters, response);
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

int lsmAccessGroupsGrantedToVolume(lsmConnect *c,
                                    lsmVolume *volume,
                                    lsmAccessGroup **groups[],
                                    uint32_t *groupCount, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_VOL(volume)) {
        return LSM_ERR_INVALID_VOL;
    }

    if( !groups || !groupCount || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["volume"] = volumeToValue(volume);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "access_groups_granted_to_volume", parameters, response);
    return getAccessGroups(rc, response, groups, groupCount);
}

int lsmVolumeChildDependency(lsmConnect *c, lsmVolume *volume,
                                uint8_t *yes, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_VOL(volume)) {
        return LSM_ERR_INVALID_VOL;
    }

    if( !yes || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["volume"] = volumeToValue(volume);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    *yes = 0;

    int rc = rpc(c, "volume_child_dependency", parameters, response);
    if( LSM_ERR_OK == rc ) {
        //We should be getting a boolean value back.
        if( Value::boolean_t == response.valueType() ) {
            if( response.asBool() ) {
                *yes = 1;
            }
        } else {
            rc = LSM_ERR_INTERNAL_ERROR;
        }
    }
    return rc;
}

int lsmVolumeChildDependencyRm(lsmConnect *c, lsmVolume *volume,
                                char **job, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_VOL(volume)) {
        return LSM_ERR_INVALID_VOL;
    }

    if( CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["volume"] = volumeToValue(volume);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_child_dependency_rm", parameters, response);
    return jobCheck(rc, response, job);
}

int lsmSystemList(lsmConnect *c, lsmSystem **systems[],
                    uint32_t *systemCount, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !systems || ! systemCount || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    int rc = rpc(c, "systems", parameters, response);
    if( LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
        std::vector<Value> sys = response.asArray();

        *systemCount = sys.size();

        if( sys.size() ) {
            *systems = lsmSystemRecordAllocArray(sys.size());

            if( *systems ) {
                for( size_t i = 0; i < sys.size(); ++i ) {
                    (*systems)[i] = valueToSystem(sys[i]);
                    if( !(*systems)[i] ) {
                        lsmSystemRecordFreeArray(*systems, i);
                        rc = LSM_ERR_NO_MEMORY;
                        break;
                    }
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        }
    }
    return rc;
}

int lsmFsList(lsmConnect *c, lsmFs **fs[], uint32_t *fsCount,
                lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !fs || !fsCount || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs", parameters, response);
    if( LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
        std::vector<Value> sys = response.asArray();

        *fsCount = sys.size();

        if( sys.size() ) {
            *fs = lsmFsRecordAllocArray(sys.size());

            for( size_t i = 0; i < sys.size(); ++i ) {
                (*fs)[i] = valueToFs(sys[i]);
            }
        }
    }
    return rc;

}

int lsmFsCreate(lsmConnect *c, lsmPool *pool, const char *name,
                    uint64_t size_bytes, lsmFs **fs, char **job,
                    lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_POOL(pool)) {
        return LSM_ERR_INVALID_POOL;
    }

    if( CHECK_STR(name) || !size_bytes || CHECK_RP(fs) || CHECK_RP(job)
        || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["pool"] = poolToValue(pool);
    p["name"] = Value(name);
    p["size_bytes"] = Value(size_bytes);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs_create", parameters, response);
    if( LSM_ERR_OK == rc ) {
        *fs = (lsmFs *)parse_job_response(response, rc, job,
                                                        (convert)valueToFs);
    }
    return rc;
}

int lsmFsDelete(lsmConnect *c, lsmFs *fs, char **job, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_FS(fs) ) {
        return LSM_ERR_INVALID_FS;
    }

    if( CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["fs"] = fsToValue(fs);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs_delete", parameters, response);
    return jobCheck(rc, response, job);
}


int lsmFsResize(lsmConnect *c, lsmFs *fs,
                                    uint64_t new_size_bytes, lsmFs **rfs,
                                    char **job, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_FS(fs)) {
        return LSM_ERR_INVALID_FS;
    }

    if( !new_size_bytes || CHECK_RP(rfs) || CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["fs"] = fsToValue(fs);
    p["new_size_bytes"] = Value(new_size_bytes);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs_resize", parameters, response);
    if( LSM_ERR_OK == rc ) {
        *rfs = (lsmFs *)parse_job_response(response, rc, job,
                                                        (convert)valueToFs);
    }
    return rc;
}

int lsmFsClone(lsmConnect *c, lsmFs *src_fs,
                                    const char *name, lsmSs *optional_ss,
                                    lsmFs **cloned_fs,
                                    char **job, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_FS(src_fs)) {
        return LSM_ERR_INVALID_FS;
    }

    if( CHECK_STR(name) || CHECK_RP(cloned_fs) || CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["src_fs"] = fsToValue(src_fs);
    p["dest_fs_name"] = Value(name);
    p["snapshot"] = ssToValue(optional_ss);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs_clone", parameters, response);
    if( LSM_ERR_OK == rc ) {
        *cloned_fs = (lsmFs *)parse_job_response(response, rc, job,
                                                        (convert)valueToFs);
    }
    return rc;
}

int lsmFsFileClone(lsmConnect *c, lsmFs *fs, const char *src_file_name,
                    const char *dest_file_name, lsmSs *snapshot, char **job,
                    lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_FS(fs)) {
        return LSM_ERR_INVALID_FS;
    }

    if( CHECK_STR(src_file_name) || CHECK_STR(dest_file_name)
        || CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["fs"] = fsToValue(fs);
    p["src_file_name"] = Value(src_file_name);
    p["dest_file_name"] = Value(dest_file_name);
    p["snapshot"] = ssToValue(snapshot);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "file_clone", parameters, response);
    return jobCheck(rc, response, job);
}

int lsmFsChildDependency( lsmConnect *c, lsmFs *fs, lsmStringList *files,
                            uint8_t *yes, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_FS(fs) ) {
        return LSM_ERR_INVALID_FS;
    }

    if( files ) {
        if( !LSM_IS_STRING_LIST(files) ) {
            return LSM_ERR_INVALID_SL;
        }
    }

    if( !yes || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["fs"] = fsToValue(fs);
    p["files"] = stringListToValue(files);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    *yes = 0;

    int rc = rpc(c, "fs_child_dependency", parameters, response);
    if( LSM_ERR_OK == rc ) {
        //We should be getting a boolean value back.
        if( Value::boolean_t == response.valueType() ) {
            if( response.asBool() ) {
                *yes = 1;
            }
        } else {
            rc = LSM_ERR_INTERNAL_ERROR;
        }
    }
    return rc;
}

int lsmFsChildDependencyRm( lsmConnect *c, lsmFs *fs, lsmStringList *files,
                            char **job, lsmFlag_t flags )
{
    CONN_SETUP(c);

    if( !LSM_IS_FS(fs)) {
        return LSM_ERR_INVALID_FS;
    }

    if( files ) {
        if( !LSM_IS_STRING_LIST(files) ) {
            return LSM_ERR_INVALID_SL;
        }
    }

    if( CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["fs"] = fsToValue(fs);
    p["files"] = stringListToValue(files);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs_child_dependency_rm", parameters, response);
    return jobCheck(rc, response, job);
}

int lsmFsSsList(lsmConnect *c, lsmFs *fs, lsmSs **ss[],
                                uint32_t *ssCount, lsmFlag_t flags )
{
    CONN_SETUP(c);

    if( !LSM_IS_FS(fs) ) {
        return LSM_ERR_INVALID_FS;
    }

    if( CHECK_RP(ss) || !ssCount || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["fs"] = fsToValue(fs);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs_snapshots", parameters, response);
    if( LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
        std::vector<Value> sys = response.asArray();

        *ssCount = sys.size();

        if( sys.size() ) {
            *ss = lsmSsRecordAllocArray(sys.size());

            for( size_t i = 0; i < sys.size(); ++i ) {
                (*ss)[i] = valueToSs(sys[i]);
            }
        }
    }
    return rc;

}

int lsmFsSsCreate(lsmConnect *c, lsmFs *fs, const char *name,
                    lsmStringList *files, lsmSs **snapshot, char **job,
                    lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_FS(fs)) {
        return LSM_ERR_INVALID_FS;
    }

    if( files ) {
        if( !LSM_IS_STRING_LIST(files) ) {
            return LSM_ERR_INVALID_SL;
        }
    }

    if( CHECK_STR(name) || CHECK_RP(snapshot) || CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["fs"] = fsToValue(fs);
    p["snapshot_name"] = Value(name);
    p["files"] = stringListToValue(files);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs_snapshot_create", parameters, response);
    if( LSM_ERR_OK == rc ) {
        *snapshot = (lsmSs *)parse_job_response(response, rc, job,
                                                        (convert)valueToSs);
    }
    return rc;
}

int lsmFsSsDelete(lsmConnect *c, lsmFs *fs, lsmSs *ss, char **job,
                    lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_FS(fs) ) {
        return LSM_ERR_INVALID_FS;
    }

    if( !LSM_IS_SS(ss) ) {
        return LSM_ERR_INVALID_SS;
    }

    if( CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["fs"] = fsToValue(fs);
    p["snapshot"] = ssToValue(ss);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs_snapshot_delete", parameters, response);
    return jobCheck(rc, response, job);
}

int lsmFsSsRevert(lsmConnect *c, lsmFs *fs, lsmSs *ss,
                                    lsmStringList *files,
                                    lsmStringList *restore_files,
                                    int all_files, char **job, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_FS(fs) ) {
        return LSM_ERR_INVALID_FS;
    }

    if( !LSM_IS_SS(ss) ) {
        return LSM_ERR_INVALID_SS;
    }

    if( files ) {
        if( !LSM_IS_STRING_LIST(files) ) {
            return LSM_ERR_INVALID_SL;
        }
    }

    if( restore_files ) {
        if( !LSM_IS_STRING_LIST(restore_files) ) {
            return LSM_ERR_INVALID_SL;
        }
    }

    if( CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["fs"] = fsToValue(fs);
    p["snapshot"] = ssToValue(ss);
    p["files"] = stringListToValue(files);
    p["restore_files"] = stringListToValue(restore_files);
    p["all_files"] = Value((all_files)?true:false);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs_snapshot_revert", parameters, response);
    return jobCheck(rc, response, job);

}

int lsmNfsList( lsmConnect *c, lsmNfsExport **exports[], uint32_t *count,
                lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( CHECK_RP(exports) || !count || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    int rc = rpc(c, "exports", parameters, response);
    if( LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
        std::vector<Value> exps = response.asArray();

        *count = exps.size();

        if( *count ) {
            *exports = lsmNfsExportRecordAllocArray(*count);

            for( size_t i = 0; i < *count; ++i ) {
                (*exports)[i] = valueToNfsExport(exps[i]);
            }
        }
    }
    return rc;
}

int lsmNfsExportFs( lsmConnect *c,
                                        const char *fs_id,
                                        const char *export_path,
                                        lsmStringList *root_list,
                                        lsmStringList *rw_list,
                                        lsmStringList *ro_list,
                                        uint64_t anon_uid,
                                        uint64_t anon_gid,
                                        const char *auth_type,
                                        const char *options,
                                        lsmNfsExport **exported,
                                        lsmFlag_t flags
                                        )
{
    CONN_SETUP(c);

    if( root_list ) {
        if( !LSM_IS_STRING_LIST(root_list) ) {
            return LSM_ERR_INVALID_SL;
        }
    }

    if( rw_list ) {
        if( !LSM_IS_STRING_LIST(rw_list) ) {
            return LSM_ERR_INVALID_SL;
        }
    }

     if( ro_list ) {
        if( !LSM_IS_STRING_LIST(ro_list) ) {
            return LSM_ERR_INVALID_SL;
        }
    }

    if( CHECK_STR(fs_id) || CHECK_RP(exported)
        || !(root_list || rw_list || ro_list) || LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;

    p["fs_id"] = Value(fs_id);
    p["export_path"] = Value(export_path);
    p["root_list"] = stringListToValue(root_list);
    p["rw_list"] = stringListToValue(rw_list);
    p["ro_list"] = stringListToValue(ro_list);
    p["anon_uid"] = Value(anon_uid);
    p["anon_gid"] = Value(anon_gid);
    p["auth_type"] = Value(auth_type);
    p["options"] = Value(options);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "export_fs", parameters, response);
    if( LSM_ERR_OK == rc && Value::object_t == response.valueType()) {
        *exported = valueToNfsExport(response);
    }
    return rc;
}

int lsmNfsExportRemove( lsmConnect *c, lsmNfsExport *e, lsmFlag_t flags)
{
    CONN_SETUP(c);

    if( !LSM_IS_NFS_EXPORT(e) ) {
        return LSM_ERR_INVALID_NFS;
    }

    if( LSM_FLAG_UNUSED_CHECK(flags) ) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["export"] = nfsExportToValue(e);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "export_remove", parameters, response);
    return rc;
}
