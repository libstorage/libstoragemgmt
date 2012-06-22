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
#include "libstoragemgmt/libstoragemgmt_systems.h"
#include "libstoragemgmt/libstoragemgmt_blockrange.h"
#include "libstoragemgmt/libstoragemgmt_accessgroups.h"
#include "libstoragemgmt/libstoragemgmt_fs.h"
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

static lsmPluginPtr lsmPluginAlloc(lsmPluginRegister reg,
                                    lsmPluginUnregister unreg) {

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
    if( p && p->mgmtOps && p->mgmtOps->tmo_set ) {
        return p->mgmtOps->tmo_set(p, params["ms"].asUint32_t() );
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_get_time_out( lsmPluginPtr p, Value &params, Value &response)
{
    uint32_t tmo = 0;

    if( p && p->mgmtOps && p->mgmtOps->tmo_get ) {
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
    lsmDataType t = LSM_DATA_TYPE_UNKNOWN;
    void *value = NULL;

    if( p && p->mgmtOps && p->mgmtOps->job_status ) {

        job_id = params["job_id"].asString().c_str();

        int rc = p->mgmtOps->job_status(p, job_id, &status, &percent, &t,
                    &value);
        if( LSM_ERR_OK == rc) {
            std::vector<Value> result;

            result.push_back(Value((int32_t)status));
            result.push_back(Value(percent));

            if( NULL == value ) {
                result.push_back(Value());
            } else {
                if( LSM_DATA_TYPE_VOLUME == t &&
                    LSM_IS_VOL((lsmVolumePtr)value)) {
                    result.push_back(volumeToValue((lsmVolumePtr)value));
                    lsmVolumeRecordFree((lsmVolumePtr)value);
                } else {
                    rc = LSM_ERR_PLUGIN_ERROR;
                }
            }
            response = Value(result);
        }
        return rc;
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_job_free(lsmPluginPtr p, Value &params, Value &response)
{
    if( p && p->mgmtOps && p->mgmtOps->job_free ) {
        std::string job_num = params["job_id"].asString();
        char *j = (char*)job_num.c_str();
        return p->mgmtOps->job_free(p, j);
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_system_list(lsmPluginPtr p, Value &params,
                                    Value &response)
{
    if( p && p->mgmtOps && p->mgmtOps->system_list ) {
        lsmSystemPtr *systems;
        uint32_t count = 0;

        int rc = p->mgmtOps->system_list(p, &systems, &count);
        if( LSM_ERR_OK == rc ) {
            std::vector<Value> result;

            for( uint32_t i = 0; i < count; ++i ) {
                result.push_back(systemToValue(systems[i]));
            }

            lsmSystemRecordFreeArray(systems, count);
            systems = NULL;
            response = Value(result);
        }
        return rc;
    }
    return LSM_ERR_NO_SUPPORT;
}
static int handle_pools(lsmPluginPtr p, Value &params, Value &response)
{
    if( p && p->mgmtOps && p->mgmtOps->pool_list ) {
        lsmPoolPtr *pools = NULL;
        uint32_t count = 0;
        int rc = p->mgmtOps->pool_list(p, &pools, &count);
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
    if( p && p->sanOps && p->sanOps->init_get ) {
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
    if( p && p->sanOps && p->sanOps->vol_get ) {
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
    if( p && p->sanOps && p->sanOps->vol_create ) {
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
    if( p && p->sanOps && p->sanOps->vol_resize ) {
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
    if( p && p->sanOps && p->sanOps->vol_replicate ) {
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

static int handle_volume_replicate_range_block_size( lsmPluginPtr p,
                                                        Value &params,
                                                        Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    uint32_t block_size = 0;

    if( p && p->sanOps && p->sanOps->vol_rep_range_bs ) {
        rc = p->sanOps->vol_rep_range_bs(p, &block_size);
        if( LSM_ERR_OK == rc ) {
            response = Value(block_size);
        }
    }
    return rc;
}

static int handle_volume_replicate_range(lsmPluginPtr p, Value &params,
                                            Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    uint32_t range_count = 0;
    char *job = NULL;
    if( p && p->sanOps && p->sanOps->vol_rep_range ) {
        lsmReplicationType repType = (lsmReplicationType)
                                        params["rep_type"].asInt32_t();
        lsmVolumePtr source = valueToVolume(params["volume_src"]);
        lsmVolumePtr dest = valueToVolume(params["volume_dest"]);
        lsmBlockRangePtr *ranges = valueToBlockRangeList(params["ranges"],
                                                            &range_count);

        if( source && dest && ranges ) {

            rc = p->sanOps->vol_rep_range(p, repType, source, dest, ranges,
                                                range_count, &job);

            if( LSM_ERR_JOB_STARTED == rc ) {
                response = Value(job);
            }

        } else {
            rc = LSM_ERR_NO_MEMORY;
        }

        lsmVolumeRecordFree(source);
        lsmVolumeRecordFree(dest);
        lsmBlockRangeRecordFreeArray(ranges, range_count);
    }
    return rc;
}

static int handle_volume_delete(lsmPluginPtr p, Value &params, Value &response)
{
    if( p && p->sanOps && p->sanOps->vol_delete ) {
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

static int handle_vol_online_offline( lsmPluginPtr p, Value &params,
                                        Value &response, int online)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps &&
        ((online)? p->sanOps->vol_online : p->sanOps->vol_offline)) {
        lsmVolumePtr vol = valueToVolume(params["volume"]);
        if( vol ) {
            if( online ) {
                rc = p->sanOps->vol_online(p, vol);
            } else {
                rc = p->sanOps->vol_offline(p, vol);
            }

            lsmVolumeRecordFree(vol);
        } else {
            rc = LSM_ERR_NO_MEMORY;
        }
    }
    return rc;
}

static int handle_volume_online(lsmPluginPtr p, Value &params, Value &response)
{
    return handle_vol_online_offline(p, params, response, 1);
}

static int handle_volume_offline(lsmPluginPtr p, Value &params, Value &response)
{
    return handle_vol_online_offline(p, params, response, 0);
}

static int ag_list(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->ag_list ) {
        lsmAccessGroupPtr *groups = NULL;
        uint32_t count;

        rc = p->sanOps->ag_list(p, &groups, &count);
        if( LSM_ERR_OK == rc ) {
            response = accessGroupListToValue(groups, count);

            /* Free the memory */
            lsmAccessGroupRecordFreeArray(groups, count);
        }
    }
    return rc;
}

static int ag_create(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->ag_create ) {
        lsmAccessGroupPtr ag = NULL;
        rc = p->sanOps->ag_create(p, params["name"].asC_str(),
                                params["initiator_id"].asC_str(),
                                (lsmInitiatorType)params["id_type"].asInt32_t(),
                                params["system_id"].asC_str(), &ag);
        if( LSM_ERR_OK == rc ) {
            response = accessGroupToValue(ag);
            lsmAccessGroupRecordFree(ag);
        }
    }
    return rc;
}

static int ag_delete(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->ag_delete ) {
        char *job = NULL;

        lsmAccessGroupPtr ag = valueToAccessGroup(params["group"]);
        rc = p->sanOps->ag_delete(p, ag, &job);

        if( LSM_ERR_JOB_STARTED == rc ) {
            response = Value(job);
            free(job);
        }

        lsmAccessGroupRecordFree(ag);
    }

    return rc;
}

static int ag_initiator_add(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->ag_add_initiator ) {
        lsmAccessGroupPtr ag = valueToAccessGroup(params["group"]);
        const char *id = params["initiator_id"].asC_str();
        char *job = NULL;
        lsmInitiatorType id_type = (lsmInitiatorType)
                                    params["id_type"].asInt32_t();

        rc = p->sanOps->ag_add_initiator(p, ag, id, id_type, &job);

        if( LSM_ERR_JOB_STARTED == rc ) {
            response = Value(job);
            free(job);
        }

        lsmAccessGroupRecordFree(ag);
    }

    return rc;
}

static int ag_initiator_del(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->ag_del_initiator ) {
        lsmAccessGroupPtr ag = valueToAccessGroup(params["group"]);
        const char *init = params["initiator_id"].asC_str();
        char *job = NULL;

        rc = p->sanOps->ag_del_initiator(p, ag, init, &job);

        if( LSM_ERR_JOB_STARTED == rc ) {
            response = Value(job);
            free(job);
        }

        lsmAccessGroupRecordFree(ag);
    }

    return rc;
}

static int ag_grant(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->ag_grant ) {
        lsmAccessGroupPtr ag = valueToAccessGroup(params["group"]);
        lsmVolumePtr vol = valueToVolume(params["volume"]);
        lsmAccessType access = (lsmAccessType)params["access"].asInt32_t();

        char *job = NULL;

        rc = p->sanOps->ag_grant(p, ag, vol, access, &job);

        if( LSM_ERR_JOB_STARTED == rc ) {
            response = Value(job);
            free(job);
        }

        lsmAccessGroupRecordFree(ag);
        lsmVolumeRecordFree(vol);
    }

    return rc;
}

static int ag_revoke(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->ag_revoke ) {
        lsmAccessGroupPtr ag = valueToAccessGroup(params["group"]);
        lsmVolumePtr vol = valueToVolume(params["volume"]);

        char *job = NULL;

        rc = p->sanOps->ag_revoke(p, ag, vol, &job);

        if( LSM_ERR_JOB_STARTED == rc ) {
            response = Value(job);
            free(job);
        }

        lsmAccessGroupRecordFree(ag);
        lsmVolumeRecordFree(vol);
    }

    return rc;
}

static int vol_accessible_by_ag(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->vol_accessible_by_ag ) {
        lsmAccessGroupPtr ag = valueToAccessGroup(params["group"]);
        lsmVolumePtr *vols = NULL;
        uint32_t count = 0;

        rc = p->sanOps->vol_accessible_by_ag(p, ag, &vols, &count);

        if( LSM_ERR_OK == rc ) {
            std::vector<Value> result;

            for( uint32_t i = 0; i < count; ++i ) {
                result.push_back(volumeToValue(vols[i]));
            }
            response = Value(result);
        }

        lsmAccessGroupRecordFree(ag);
        lsmVolumeRecordFreeArray(vols, count);
        vols = NULL;
    }

    return rc;
}

static int ag_granted_to_volume(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->ag_granted_to_vol ) {
        lsmVolumePtr volume = valueToVolume(params["volume"]);
        lsmAccessGroupPtr *groups = NULL;
        uint32_t count = 0;

        rc = p->sanOps->ag_granted_to_vol(p, volume, &groups, &count);

        if( LSM_ERR_OK == rc ) {
            std::vector<Value> result;

            for( uint32_t i = 0; i < count; ++i ) {
                result.push_back(accessGroupToValue(groups[i]));
            }
            response = Value(result);
        }

        lsmVolumeRecordFree(volume);
        lsmAccessGroupRecordFreeArray(groups, count);
        groups = NULL;
    }

    return rc;
}

static int volume_dependency(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->vol_child_depends ) {
        lsmVolumePtr volume = valueToVolume(params["volume"]);
        uint8_t yes;

        rc = p->sanOps->vol_child_depends(p, volume, &yes);

        if( LSM_ERR_OK == rc ) {
            response = Value((bool)(yes));
        }

        lsmVolumeRecordFree(volume);
    }

    return rc;
}

static int volume_dependency_rm(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->vol_child_depends_rm ) {
        lsmVolumePtr volume = valueToVolume(params["volume"]);
        char *job = NULL;

        rc = p->sanOps->vol_child_depends_rm(p, volume, &job);

        if( LSM_ERR_JOB_STARTED == rc ) {
            response = Value(job);
            free(job);
        }
        lsmVolumeRecordFree(volume);
    }
    return rc;
}

static int fs(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->fsOps->fs_list ) {
        lsmFsPtr *fs = NULL;
        uint32_t count = 0;

        rc = p->fsOps->fs_list(p, &fs, &count);

        if( LSM_ERR_OK == rc ) {
            std::vector<Value> result;

            for( uint32_t i = 0; i < count; ++i ) {
                result.push_back(fsToValue(fs[i]));
            }

            response = Value(result);
            lsmFsRecordFreeArray(fs, count);
            fs = NULL;
        }
    }
    return rc;
}

static int fs_create(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->fsOps->fs_create ) {
        lsmPoolPtr pool = valueToPool(params["pool"]);
        const char *name = params["name"].asC_str();
        uint64_t size_bytes = params["size_bytes"].asUint64_t();
        lsmFsPtr fs = NULL;
        char *job = NULL;

        rc = p->fsOps->fs_create(p, pool, name, size_bytes, &fs, &job);

        std::vector<Value> r;

        if( LSM_ERR_OK == rc ) {
            r.push_back(Value());
            r.push_back(fsToValue(fs));
            response = Value(r);
            lsmFsRecordFree(fs);
        } else if (LSM_ERR_JOB_STARTED == rc ) {
            r.push_back(Value(job));
            r.push_back(Value());
            response = Value(r);
            free(job);
        }
    }
    return rc;
}

static int fs_delete(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->fsOps->fs_delete ) {
        lsmFsPtr fs = valueToFs(params["fs"]);
        char *job = NULL;

        rc = p->fsOps->fs_delete(p, fs, &job);

        if( LSM_ERR_JOB_STARTED == rc ) {
            response = Value(job);
            free(job);
        }
        lsmFsRecordFree(fs);
    }
    return rc;
}

static int fs_resize(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->fsOps->fs_resize ) {
        lsmFsPtr fs = valueToFs(params["fs"]);
        uint64_t size_bytes = params["new_size_bytes"].asUint64_t();
        lsmFsPtr rfs = NULL;
        char *job = NULL;

        rc = p->fsOps->fs_resize(p, fs, size_bytes, &rfs, &job);

        std::vector<Value> r;

        if( LSM_ERR_OK == rc ) {
            r.push_back(Value());
            r.push_back(fsToValue(rfs));
            response = Value(r);
            lsmFsRecordFree(rfs);
        } else if (LSM_ERR_JOB_STARTED == rc ) {
            r.push_back(Value(job));
            r.push_back(Value());
            response = Value(r);
            free(job);
        }
        lsmFsRecordFree(fs);
    }
    return rc;
}

static int fs_clone(lsmPluginPtr p, Value &params, Value &response)
{
   int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->fsOps->fs_clone ) {
        lsmFsPtr clonedFs = NULL;
        char *job = NULL;
        lsmFsPtr fs = valueToFs(params["src_fs"]);
        const char* name = params["dest_fs_name"].asC_str();
        lsmSsPtr ss = valueToSs(params["snapshot"]);

        rc = p->fsOps->fs_clone(p, fs, name, &clonedFs, ss, &job);

        std::vector<Value> r;
        if( LSM_ERR_OK == rc ) {
            r.push_back(Value());
            r.push_back(fsToValue(clonedFs));
            response = Value(r);
            lsmFsRecordFree(clonedFs);
        } else if (LSM_ERR_JOB_STARTED == rc ) {
            r.push_back(Value(job));
            r.push_back(Value());
            response = Value(r);
            free(job);
        }
        lsmFsRecordFree(fs);
    }
   return rc;
}

static int fs_child_dependency(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->sanOps && p->fsOps->fs_child_dependency ) {
        int ok = 0;
        lsmFsPtr fs = valueToFs(params["fs"]);
        lsmStringListPtr files = valueToStringList(params["files"], &ok);

        if( !ok ) {
            rc = LSM_ERR_NO_MEMORY;
        } else {
            uint8_t yes = 0;

            rc = p->fsOps->fs_child_dependency(p, fs, files, &yes);

            if( LSM_ERR_OK == rc ) {
                response = Value((bool)yes);
            }
        }

        lsmFsRecordFree(fs);
        lsmStringListFree(files);
    }
    return rc;
}

static int fs_child_dependency_rm(lsmPluginPtr p, Value &params, Value &response)
{
     int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->sanOps && p->fsOps->fs_child_dependency_rm ) {
        int ok = 0;
        lsmFsPtr fs = valueToFs(params["fs"]);
        lsmStringListPtr files = valueToStringList(params["files"], &ok);
        char *job = NULL;

        if( !ok ) {

            rc = LSM_ERR_NO_MEMORY;
        } else {
            rc = p->fsOps->fs_child_dependency_rm(p, fs, files, &job);

            if( LSM_ERR_JOB_STARTED == rc ) {
                response = Value(job);
                free(job);
            }
        }

        lsmFsRecordFree(fs);
        lsmStringListFree(files);
    }
    return rc;
}

static int not_supported(lsmPluginPtr p, Value &params, Value &response)
{
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
    ("systems", handle_system_list)
    ("initiators", handle_initiators)
    ("volumes", handle_volumes)
    ("volume_create", handle_volume_create)
    ("volume_resize", handle_volume_resize)
    ("volume_replicate", handle_volume_replicate)
    ("volume_replicate_range_block_size", handle_volume_replicate_range_block_size)
    ("volume_replicate_range", handle_volume_replicate_range)
    ("volume_delete", handle_volume_delete)
    ("volume_online", handle_volume_online)
    ("volume_offline", handle_volume_offline)
    ("access_group_grant", ag_grant)
    ("access_group_revoke", ag_revoke)
    ("access_group_list", ag_list)
    ("access_group_create", ag_create)
    ("access_group_del", ag_delete)
    ("access_group_add_initiator", ag_initiator_add)
    ("access_group_del_initiator", ag_initiator_del)
    ("volumes_accessible_by_access_group", vol_accessible_by_ag)
    ("access_groups_granted_to_volume", ag_granted_to_volume)
    ("volume_child_dependency", volume_dependency)
    ("volume_child_dependency_rm", volume_dependency_rm)
    ("fs", fs)
    ("fs_delete", fs_delete)
    ("fs_resize", fs_resize)
    ("fs_create", fs_create)
    ("fs_clone", fs_clone)
    ("file_clone", not_supported)
    ("snapshots", not_supported)
    ("snapshot_create", not_supported)
    ("snapshot_delete", not_supported)
    ("snapshot_revert", not_supported)
    ("fs_child_dependency", fs_child_dependency)
    ("fs_child_dependency_rm", fs_child_dependency_rm)
    ("export_auth", not_supported)
    ("exports", not_supported)
    ("export_fs", not_supported)
    ("export_remove", not_supported);

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

int lsmLogErrorBasic( lsmPluginPtr plug, lsmErrorNumber code, const char* msg )
{
    lsmErrorPtr e = LSM_ERROR_CREATE_PLUGIN_MSG(code, msg);
    if( e ) {
        int rc = lsmPluginErrorLog(plug, e);

        if( LSM_ERR_OK != rc ) {
            syslog(LOG_USER|LOG_NOTICE,
                    "Plug-in error %d while reporting an error, code= %d, msg= %s",
                    rc, code, msg);
        }
    }
    return (int)code;
}

int lsmPluginErrorLog( lsmPluginPtr plug, lsmErrorPtr error)
{
    if( !LSM_IS_PLUGIN(plug) ) {
        return LSM_ERR_INVALID_PLUGIN;
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
