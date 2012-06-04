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

#include "lsm_plugin_ipc.hpp"
#include "lsm_datatypes.hpp"
#include "lsm_ipc.hpp"
#include "lsm_convert.hpp"
#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include <libstoragemgmt/libstoragemgmt_volumes.h>
#include <libstoragemgmt/libstoragemgmt_pool.h>
#include <libstoragemgmt/libstoragemgmt_initiators.h>
#include <errno.h>
#include <string.h>
#include <libxml/uri.h>
#include <syslog.h>

//Forward decl.
static int lsmPluginRun(lsmPluginPtr plug);

/**
 * Safe string wrapper
 * @param s Character array to convert to std::string
 * @return String representation.
 */
std::string ss(char *s)
{
    if( s ) {
        return std::string(s);
    }
    return std::string();
}

int lsmRegisterPlugin(lsmPluginPtr plug, const char *desc, const char *version,
                        void *private_data, struct lsmMgmtOps *mgmOps,
                        struct lsmSanOps *sanOp, struct lsmFsOps *fsOp,
                        struct lsmNasOps *nasOp)
{
    int rc = LSM_ERR_OK;

    if (NULL == desc || NULL == version) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    plug->desc = strdup(desc);
    plug->version = strdup(version);

    if( !plug->desc || !plug->version ) {
        free(plug->desc);
        free(plug->version);
        rc = LSM_ERR_NO_MEMORY;
    } else {
        plug->privateData = private_data;
        plug->mgmtOps = mgmOps;
        plug->sanOps = sanOp;
        plug->fsOps = fsOp;
        plug->nasOps = nasOp;
    }

    return rc;
}

void *lsmGetPrivateData(lsmPluginPtr plug)
{
    if (!LSM_IS_PLUGIN(plug)) {
        return NULL;
    }

    return plug->privateData;
}

static lsmPluginPtr lsmPluginAlloc(lsmPluginRegister reg, lsmPluginUnregister unreg) {

    if( !reg || !unreg ) {
        return NULL;
    }

    lsmPluginPtr rc = (lsmPluginPtr)malloc(sizeof(lsmPlugin));
    if( rc ) {
        memset(rc, 0, sizeof( lsmPlugin));
        rc->magic = LSM_PLUGIN_MAGIC;
        rc->reg = reg;
        rc->unreg = unreg;
    }
    return rc;
}

static void lsmPluginFree(lsmPluginPtr p)
{
    if( LSM_IS_PLUGIN(p) ) {

        delete(p->tp);
        p->tp = NULL;

        if( p->unreg ) {
            p->unreg(p);
        }

        free(p->desc);
        p->desc = NULL;

        free(p->version);
        p->version = NULL;

        lsmErrorFree(p->error);
        p->error = NULL;

        free(p);
    }
}

static void sendError(lsmPluginPtr p, int error_code)
{
    if( !LSM_IS_PLUGIN(p) ) {
        return;
    }

    if( p->error ) {
        if( p->tp ) {
            p->tp->sendError(p->error->code, ss(p->error->message),
                                ss(p->error->debug));
            lsmErrorFree(p->error);
            p->error = NULL;
        }
    } else {
        p->tp->sendError(error_code, "UNA", "UNA");
    }
}


/**
 * Checks to see if a character string is an integer and returns result
 * @param[in] sn    Character array holding the integer
 * @param[out] num  The numeric value contained in string
 * @return true if sn is an integer, else false
 */
static bool get_num( char *sn, int &num)
{
    errno = 0;

    num = strtol(sn, NULL, 10);
    if( !errno ) {
        return true;
    }
    return false;
}

/**
 * Handle the startup sequence with the client.
 * @param p Plug-in
 * @return true on success, else false
 */
static bool startup(lsmPluginPtr p)
{
    bool rc = false;
    xmlURIPtr uri = NULL;
    lsmErrorPtr e = NULL;

    //We are just getting established with the client, if the socket
    //closes on us or we encounter an error we will need to log to syslog so
    //we can debug what went wrong.
    try {

        Value req = p->tp->readRequest();
        if( req.isValidRequest() ) {
            std::map<std::string, Value> r = req.asObject();
            if( r["method"].asString() == "startup" ) {
                std::map<std::string, Value> params = r["params"].asObject();

                uri = xmlParseURI(params["uri"].asString().c_str());
                if( uri ) {
                    int reg_rc;

                    const char *pass = NULL;

                    if( Value::string_t == params["password"].valueType() ) {
                        pass = params["password"].asString().c_str();
                    }

                    //Let the plug-in initialize itself.
                    reg_rc = p->reg(p, uri, pass, params["timeout"].asUint32_t());

                    if( LSM_ERR_OK != reg_rc ) {
                        sendError(p, reg_rc);
                    } else {
                        p->tp->sendResponse(Value());
                        rc = true;
                    }

                    xmlFreeURI(uri);
                    uri = NULL;
                }
            }
        }
    } catch( ... ) {
        /* TODO Add more specific exception handling and log to syslog */
        if( uri ) {
            xmlFreeURI(uri);
            uri = NULL;
        }

        if( e ) {
            lsmErrorFree(e);
            e = NULL;
        }
    }
    return rc;
}


int lsmPluginInit( int argc, char *argv[], lsmPluginRegister reg,
                                lsmPluginUnregister unreg)
{
    int rc = 1;
    lsmPluginPtr plug = NULL;

    int sd = 0;
    if( argc == 2 && get_num(argv[1], sd) ) {
        plug = lsmPluginAlloc(reg, unreg);
        if( plug ) {
            plug->tp = new Ipc(sd);
            if( !startup(plug) ) {
                lsmPluginFree(plug);
                plug = NULL;
            } else {
                rc = lsmPluginRun(plug);
            }
        }
    } else {
        //Process command line arguments or display help text.
        rc = 2;
    }

    return rc;
}

typedef int (*handler)(lsmPluginPtr p, Value &params, Value &response);

static int handle_shutdown(lsmPluginPtr p, Value &params, Value &response)
{
    return LSM_ERR_OK;
}

static int handle_set_time_out( lsmPluginPtr p, Value &params, Value &response)
{
    if( p->mgmtOps->tmo_set ) {
        return p->mgmtOps->tmo_set(p, params["ms"].asUint32_t() );
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_get_time_out( lsmPluginPtr p, Value &params, Value &response)
{
    uint32_t tmo = 0;

    if( p->mgmtOps->tmo_get ) {
        int rc = p->mgmtOps->tmo_get(p, &tmo );
        if( LSM_ERR_OK == rc) {
            response = Value(tmo);
        }
        return rc;
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_job_status( lsmPluginPtr p, Value &params, Value &response)
{
    const char *job_id = NULL;
    lsmJobStatus status;
    uint8_t percent;
    lsmVolumePtr vol = NULL;

    if( p->mgmtOps->job_status ) {

        job_id = params["job_id"].asString().c_str();

        int rc = p->mgmtOps->job_status(p, job_id, &status, &percent, &vol);
        if( LSM_ERR_OK == rc) {
            std::vector<Value> result;

            result.push_back(Value((int32_t)status));
            result.push_back(Value(percent));

            if( NULL == vol ) {
                result.push_back(Value());
            } else {
                result.push_back(volumeToValue(vol));
                lsmVolumeRecordFree(vol);
            }
            response = Value(result);
        }
        return rc;
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_job_free(lsmPluginPtr p, Value &params, Value &response)
{
    if( p->mgmtOps->job_free ) {
        std::string job_num = params["job_id"].asString();
        char *j = (char*)job_num.c_str();
        return p->mgmtOps->job_free(p, j);
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_pools(lsmPluginPtr p, Value &params, Value &response)
{
    if( p->sanOps->pool_get ) {
        lsmPoolPtr *pools = NULL;
        uint32_t count = 0;
        int rc = p->sanOps->pool_get(p, &pools, &count);
        if( LSM_ERR_OK == rc) {
            std::vector<Value> result;

            for( uint32_t i = 0; i < count; ++i ) {
                result.push_back(poolToValue(pools[i]));
            }

            lsmPoolRecordFreeArray(pools, count);
            pools = NULL;
            response = Value(result);
        }
        return rc;
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_initiators(lsmPluginPtr p, Value &params, Value &response)
{
    if( p->sanOps->init_get ) {
        lsmInitiatorPtr *inits = NULL;
        uint32_t count = 0;
        int rc = p->sanOps->init_get(p, &inits, &count);
        if( LSM_ERR_OK == rc ) {
            std::vector<Value> result;

            for( uint32_t i = 0; i < count; ++i ) {
                result.push_back(initiatorToValue(inits[i]));
            }

            lsmInitiatorRecordFreeArray(inits, count);
            inits = NULL;
            response = Value(result);
        }
        return rc;
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_volumes(lsmPluginPtr p, Value &params, Value &response)
{
    if( p->sanOps->vol_get ) {
        lsmVolumePtr *vols = NULL;
        uint32_t count = 0;
        int rc = p->sanOps->vol_get(p, &vols, &count);

        if( LSM_ERR_OK == rc ) {
            std::vector<Value> result;

            for( uint32_t i = 0; i < count; ++i ) {
                result.push_back(volumeToValue(vols[i]));
            }

            lsmVolumeRecordFreeArray(vols, count);
            vols = NULL;
            response = Value(result);
        }
        return rc;
    }
    return LSM_ERR_NO_SUPPORT;
}

static Value job_handle(int rc, lsmVolumePtr vol, char *job)
{
    Value result;
    std::vector<Value> r;

    if( LSM_ERR_OK == rc ) {
        r.push_back(Value());
        r.push_back(volumeToValue(vol));
        result = Value(r);
    } else if( LSM_ERR_JOB_STARTED == rc ) {
        r.push_back(Value(job));
        r.push_back(Value());
        result = Value(r);
    }
    return result;
}

static int handle_volume_create(lsmPluginPtr p, Value &params, Value &response)
{
    if( p->sanOps->vol_create ) {
        lsmPoolPtr pool = valueToPool(params["pool"]);
        const char *name = params["volume_name"].asString().c_str();
        uint64_t size = params["size_bytes"].asUint64_t();
        lsmProvisionType pro = (lsmProvisionType)params["provisioning"].asInt32_t();
        lsmVolumePtr vol = NULL;
        char *job = NULL;

        int rc = p->sanOps->vol_create(p, pool, name, size, pro, &vol, &job);
        response = job_handle(rc, vol, job);

        //Free dynamic data.
        lsmPoolRecordFree(pool);
        lsmVolumeRecordFree(vol);
        free(job);
        return rc;
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_volume_resize(lsmPluginPtr p, Value &params, Value &response)
{
    if( p->sanOps->vol_resize ) {
        lsmVolumePtr vol = valueToVolume(params["volume"]);
        lsmVolumePtr resized_vol = NULL;
        uint64_t size = params["new_size_bytes"].asUint64_t();
        char *job = NULL;

        int rc = p->sanOps->vol_resize(p, vol, size, &resized_vol, &job );
        response = job_handle(rc, resized_vol, job);

        lsmVolumeRecordFree(vol);
        lsmVolumeRecordFree(resized_vol);
        free(job);
        return rc;
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_volume_replicate(lsmPluginPtr p, Value &params, Value &response)
{
    if( p->sanOps->vol_replicate ) {
        lsmPoolPtr pool = valueToPool(params["pool"]);
        lsmVolumePtr vol = valueToVolume(params["volume_src"]);
        lsmVolumePtr newVolume = NULL;
        lsmReplicationType rep = (lsmReplicationType)params["rep_type"].asInt32_t();
        const char *name = params["name"].asString().c_str();
        char *job = NULL;

        int rc = p->sanOps->vol_replicate(p, pool, rep, vol, name, &newVolume, &job);
        response = job_handle(rc, newVolume, job);

        lsmPoolRecordFree(pool);
        lsmVolumeRecordFree(vol);
        lsmVolumeRecordFree(newVolume);
        free(job);
        return rc;
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_volume_delete(lsmPluginPtr p, Value &params, Value &response)
{
    if( p->sanOps->vol_delete ) {
        lsmVolumePtr vol = valueToVolume(params["volume"]);
        char *job = NULL;

        int rc = p->sanOps->vol_delete(p, vol, &job);

        if( LSM_ERR_JOB_STARTED == rc ) {
            response = Value(job);
        }

        lsmVolumeRecordFree(vol);
        free(job);
        return rc;
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_initiator_create(lsmPluginPtr p, Value &params, Value &response)
{
    if( p->sanOps->init_create ) {
        const char *name = params["name"].asString().c_str();
        const char *id = params["id"].asString().c_str();
        lsmInitiatorType t = (lsmInitiatorType)params["id_type"].asInt32_t();
        lsmInitiatorPtr init = NULL;

        int rc = p->sanOps->init_create(p, name, id, t, &init);

        if( LSM_ERR_OK == rc ) {
            response = Value(initiatorToValue(init));
        }
        lsmInitiatorRecordFree(init);
        return rc;
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_initiator_delete(lsmPluginPtr p, Value &params, Value &response)
{
    if( p->sanOps->init_delete ) {
        lsmInitiatorPtr i = valueToInitiator(params["initiator"]);

        int rc = p->sanOps->init_delete(p, i);
        lsmInitiatorRecordFree(i);
        return rc;
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_access_grant(lsmPluginPtr p, Value &params, Value &response)
{
    if( p->sanOps->access_grant ) {
        lsmInitiatorPtr i = valueToInitiator(params["initiator"]);
        lsmVolumePtr v = valueToVolume(params["volume"]);
        lsmAccessType access = (lsmAccessType)params["access"].asInt32_t();
        char *job = NULL;

        int rc = p->sanOps->access_grant(p, i, v, access, &job);

         if( LSM_ERR_JOB_STARTED == rc ) {
            response = Value(job);
        }

        lsmInitiatorRecordFree(i);
        lsmVolumeRecordFree(v);
        free(job);
        return rc;
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_access_revoke(lsmPluginPtr p, Value &params, Value &response)
{
    if( p->sanOps->access_remove ) {
        lsmInitiatorPtr i = valueToInitiator(params["initiator"]);
        lsmVolumePtr v = valueToVolume(params["volume"]);

        int rc = p->sanOps->access_remove(p, i, v);

        lsmInitiatorRecordFree(i);
        lsmVolumeRecordFree(v);
        return rc;
    }
    return LSM_ERR_NO_SUPPORT;
}

/**
 * map of function pointers
 */
static std::map<std::string,handler> dispatch = static_map<std::string,handler>
    ("shutdown", handle_shutdown)
    ("set_time_out", handle_set_time_out)
    ("get_time_out", handle_get_time_out)
    ("job_status", handle_job_status)
    ("job_free", handle_job_free)
    ("pools", handle_pools)
    ("initiators", handle_initiators)
    ("volumes", handle_volumes)
    ("volume_create", handle_volume_create)
    ("volume_resize", handle_volume_resize)
    ("volume_replicate", handle_volume_replicate)
    ("volume_delete", handle_volume_delete)
    ("initiator_create", handle_initiator_create)
    ("initiator_delete", handle_initiator_delete)
    ("access_grant", handle_access_grant)
    ("access_revoke", handle_access_revoke);

static int process_request(lsmPluginPtr p, const std::string &method, Value &request,
                    Value &response)
{
    int rc = LSM_ERR_INTERNAL_ERROR;

    response = Value(); //Default response will be null

    if( dispatch.find(method) != dispatch.end() ) {
        rc = (dispatch[method])(p, request["params"], response);
    } else {
        rc = LSM_ERR_NO_SUPPORT;
    }

    return rc;
}

static int lsmPluginRun(lsmPluginPtr p)
{
    int rc = 0;
    while(true) {
        try {
            Value req = p->tp->readRequest();
            Value resp;

            if( req.isValidRequest() ) {
                std::string method = req["method"].asString();
                int rc = process_request(p, method, req, resp);

                if( LSM_ERR_OK == rc || LSM_ERR_JOB_STARTED == rc ) {
                    p->tp->sendResponse(resp);
                } else {
                    sendError(p, rc);
                }

                if( method == "shutdown" ) {
                    break;
                }
            } else {
                syslog(LOG_USER|LOG_NOTICE, "Invalid request");
                break;
            }
        } catch (EOFException &eof) {
            break;
        } catch (ValueException &ve) {
            syslog(LOG_USER|LOG_NOTICE, "Plug-in exception: %s", ve.what() );
            rc = 1;
            break;
        } catch (LsmException &le) {
            syslog(LOG_USER|LOG_NOTICE, "Plug-in exception: %s", le.what() );
            rc = 2;
            break;
        } catch ( ... ) {
            syslog(LOG_USER|LOG_NOTICE, "Plug-in un-handled exception");
            rc = 3;
            break;
        }
    }

    lsmPluginFree(p);
    p = NULL;

    return rc;
}

int lsmPluginErrorLog( lsmPluginPtr plug, lsmErrorPtr error)
{
    if( !LSM_IS_PLUGIN(plug) ) {
        return LSM_INVALID_PLUGIN;
    }

    if(!LSM_IS_ERROR(error) ) {
        return LSM_ERR_INVALID_ERR;
    }

    if( plug->error ) {
        lsmErrorFree(plug->error);
    }

    plug->error = error;

    return LSM_ERR_OK;
}
