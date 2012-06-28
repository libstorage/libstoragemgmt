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
#include "libstoragemgmt/libstoragemgmt_snapshot.h"
#include "libstoragemgmt/libstoragemgmt_nfsexport.h"
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
        if( Value::numeric_t == params["ms"].valueType() ) {
            return p->mgmtOps->tmo_set(p, params["ms"].asUint32_t() );
        } else {
            return LSM_ERR_TRANSPORT_INVALID_ARG;
        }
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
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->mgmtOps && p->mgmtOps->job_status ) {

        if( Value::string_t != params["job_id"].valueType() ) {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        } else {

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
        }
    }
    return rc;
}

static int handle_job_free(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->mgmtOps && p->mgmtOps->job_free ) {
        if( Value::string_t == params["job_id"].valueType() ) {
            std::string job_num = params["job_id"].asString();
            char *j = (char*)job_num.c_str();
            rc = p->mgmtOps->job_free(p, j);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
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
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->sanOps && p->sanOps->vol_create ) {

        Value v_p = params["pool"];
        Value v_name = params["volume_name"];
        Value v_size = params["size_bytes"];
        Value v_prov = params["provisioning"];

        if( Value::object_t == v_p.valueType() &&
            Value::string_t == v_name.valueType() &&
            Value::numeric_t == v_size.valueType() &&
            Value::numeric_t == v_prov.valueType()) {

            lsmPoolPtr pool = valueToPool(v_p);
            if( pool ) {
                lsmVolumePtr vol = NULL;
                char *job = NULL;
                const char *name = v_name.asC_str();
                uint64_t size = v_size.asUint64_t();
                lsmProvisionType pro = (lsmProvisionType)v_prov.asInt32_t();

                rc = p->sanOps->vol_create(p, pool, name, size, pro, &vol, &job);
                response = job_handle(rc, vol, job);

                //Free dynamic data.
                lsmPoolRecordFree(pool);
                lsmVolumeRecordFree(vol);
                free(job);

            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_volume_resize(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->sanOps && p->sanOps->vol_resize ) {
        Value v_vol = params["volume"];
        Value v_size = params["new_size_bytes"];

        if( Value::object_t == v_vol.valueType() &&
            Value::numeric_t == v_size.valueType() ) {

            lsmVolumePtr vol = valueToVolume(v_vol);
            if( vol ) {
                lsmVolumePtr resized_vol = NULL;
                uint64_t size = v_size.asUint64_t();
                char *job = NULL;

                rc = p->sanOps->vol_resize(p, vol, size, &resized_vol, &job);
                response = job_handle(rc, resized_vol, job);

                lsmVolumeRecordFree(vol);
                lsmVolumeRecordFree(resized_vol);
                free(job);

            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_volume_replicate(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->vol_replicate ) {

        Value v_pool = params["pool"];
        Value v_vol_src = params["volume_src"];
        Value v_rep = params["rep_type"];
        Value v_name = params["name"];

        if( Value::object_t == v_pool.valueType() &&
            Value::object_t == v_vol_src.valueType() &&
            Value::numeric_t == v_rep.valueType() &&
            Value::string_t == v_name.valueType()) {

            lsmPoolPtr pool = valueToPool(v_pool);
            lsmVolumePtr vol = valueToVolume(v_vol_src);
            lsmVolumePtr newVolume = NULL;
            lsmReplicationType rep = (lsmReplicationType)v_rep.asInt32_t();
            const char *name = v_name.asC_str();
            char *job = NULL;

            if( pool && vol ) {
                rc = p->sanOps->vol_replicate(p, pool, rep, vol, name,
                                                &newVolume, &job);
                response = job_handle(rc, newVolume, job);

                lsmVolumeRecordFree(newVolume);
                free(job);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsmPoolRecordFree(pool);
            lsmVolumeRecordFree(vol);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
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
        Value v_rep = params["rep_type"];
        Value v_vol_src = params["volume_src"];
        Value v_vol_dest = params["volume_dest"];
        Value v_ranges = params["ranges"];

        if( Value::numeric_t == v_rep.valueType() &&
            Value::object_t == v_vol_src.valueType() &&
            Value::object_t == v_vol_dest.valueType() &&
            Value::array_t == v_ranges.valueType()) {

            lsmReplicationType repType = (lsmReplicationType)
                                        v_rep.asInt32_t();
            lsmVolumePtr source = valueToVolume(v_vol_src);
            lsmVolumePtr dest = valueToVolume(v_vol_dest);
            lsmBlockRangePtr *ranges = valueToBlockRangeList(v_ranges,
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

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_volume_delete(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->sanOps && p->sanOps->vol_delete ) {
        Value v_vol = params["volume"];

        if(Value::object_t == v_vol.valueType() ) {
            lsmVolumePtr vol = valueToVolume(params["volume"]);

            if( vol ) {
                char *job = NULL;

                rc = p->sanOps->vol_delete(p, vol, &job);

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                }

                lsmVolumeRecordFree(vol);
                free(job);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_vol_online_offline( lsmPluginPtr p, Value &params,
                                        Value &response, int online)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps &&
        ((online)? p->sanOps->vol_online : p->sanOps->vol_offline)) {

        Value v_vol = params["volume"];

        if( Value::object_t == v_vol.valueType() ) {
            lsmVolumePtr vol = valueToVolume(v_vol);
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
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
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
        Value v_name = params["name"];
        Value v_init_id = params["initiator_id"];
        Value v_id_type = params["id_type"];
        Value v_system_id = params["system_id"];

        if( Value::string_t == v_name.valueType() &&
            Value::string_t == v_init_id.valueType() &&
            Value::numeric_t == v_id_type.valueType() &&
            Value::string_t == v_system_id.valueType()) {

            lsmAccessGroupPtr ag = NULL;
            rc = p->sanOps->ag_create(p, v_name.asC_str(),
                                    v_init_id.asC_str(),
                                    (lsmInitiatorType)v_id_type.asInt32_t(),
                                    v_system_id.asC_str(), &ag);
            if( LSM_ERR_OK == rc ) {
                response = accessGroupToValue(ag);
                lsmAccessGroupRecordFree(ag);
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ag_delete(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->ag_delete ) {
        char *job = NULL;
        Value v_group = params["group"];

        if( Value::object_t == v_group.valueType() ) {
            lsmAccessGroupPtr ag = valueToAccessGroup(v_group);

            if( ag ) {
                rc = p->sanOps->ag_delete(p, ag, &job);

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }

                lsmAccessGroupRecordFree(ag);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ag_initiator_add(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->ag_add_initiator ) {

        Value v_group = params["group"];
        Value v_id = params["initiator_id"];
        Value v_id_type = params["id_type"];


        if( Value::object_t == v_group.valueType() &&
            Value::string_t == v_id.valueType() &&
            Value::numeric_t == v_id_type.valueType() ) {

            lsmAccessGroupPtr ag = valueToAccessGroup(v_group);
            if( ag ) {
                const char *id = v_id.asC_str();
                char *job = NULL;
                lsmInitiatorType id_type = (lsmInitiatorType)
                                            v_id_type.asInt32_t();

                rc = p->sanOps->ag_add_initiator(p, ag, id, id_type, &job);

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }

                lsmAccessGroupRecordFree(ag);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }

    return rc;
}

static int ag_initiator_del(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->ag_del_initiator ) {

        Value v_group = params["group"];
        Value v_init_id = params["initiator_id"];

        if( Value::object_t == v_group.valueType() &&
            Value::string_t == v_init_id.valueType() ) {

            lsmAccessGroupPtr ag = valueToAccessGroup(v_group);

            if( ag ) {
                const char *init = v_init_id.asC_str();
                char *job = NULL;

                rc = p->sanOps->ag_del_initiator(p, ag, init, &job);

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }

                lsmAccessGroupRecordFree(ag);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ag_grant(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->ag_grant ) {

        Value v_group = params["group"];
        Value v_vol = params["volume"];
        Value v_access = params["access"];

        if( Value::object_t == v_group.valueType() &&
            Value::object_t == v_vol.valueType() &&
            Value::numeric_t == v_access.valueType() ) {

            lsmAccessGroupPtr ag = valueToAccessGroup(v_group);
            lsmVolumePtr vol = valueToVolume(v_vol);

            if( ag && vol ) {
                lsmAccessType access = (lsmAccessType)v_access.asInt32_t();

                char *job = NULL;

                rc = p->sanOps->ag_grant(p, ag, vol, access, &job);

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsmAccessGroupRecordFree(ag);
            lsmVolumeRecordFree(vol);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }

    return rc;
}

static int ag_revoke(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->ag_revoke ) {

        Value v_group = params["group"];
        Value v_vol = params["volume"];

        if( Value::object_t == v_group.valueType() &&
            Value::object_t == v_vol.valueType() ) {

            lsmAccessGroupPtr ag = valueToAccessGroup(v_group);
            lsmVolumePtr vol = valueToVolume(v_vol);

            if( ag && vol ) {
                char *job = NULL;

                rc = p->sanOps->ag_revoke(p, ag, vol, &job);

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsmAccessGroupRecordFree(ag);
            lsmVolumeRecordFree(vol);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }

    return rc;
}

static int vol_accessible_by_ag(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->vol_accessible_by_ag ) {
        Value v_group = params["group"];

        if( Value::object_t == v_group.valueType() ) {
            lsmAccessGroupPtr ag = valueToAccessGroup(v_group);

            if( ag ) {
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
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ag_granted_to_volume(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->ag_granted_to_vol ) {

        Value v_vol = params["volume"];

        if( Value::object_t == v_vol.valueType() ) {
            lsmVolumePtr volume = valueToVolume(v_vol);

            if( volume ) {
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
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int volume_dependency(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->vol_child_depends ) {

        Value v_vol = params["volume"];

        if( Value::object_t == v_vol.valueType() ) {
            lsmVolumePtr volume = valueToVolume(v_vol);

            if( volume ) {
                uint8_t yes;

                rc = p->sanOps->vol_child_depends(p, volume, &yes);

                if( LSM_ERR_OK == rc ) {
                    response = Value((bool)(yes));
                }

                lsmVolumeRecordFree(volume);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }

    return rc;
}

static int volume_dependency_rm(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->sanOps->vol_child_depends_rm ) {

        Value v_vol = params["volume"];

        if( Value::object_t == v_vol.valueType() ) {
            lsmVolumePtr volume = valueToVolume(v_vol);

            if( volume ) {

                char *job = NULL;

                rc = p->sanOps->vol_child_depends_rm(p, volume, &job);

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }
                lsmVolumeRecordFree(volume);

            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
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

        Value v_pool = params["pool"];
        Value v_name = params["name"];
        Value v_size = params["size_bytes"];

        if( Value::object_t == v_pool.valueType() &&
            Value::string_t == v_name.valueType() &&
            Value::numeric_t == v_size.valueType() ) {

            lsmPoolPtr pool = valueToPool(v_pool);

            if( pool ) {
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
                lsmPoolRecordFree(pool);

            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int fs_delete(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->fsOps->fs_delete ) {

        Value v_fs = params["fs"];

        if( Value::object_t == v_fs.valueType() ) {

            lsmFsPtr fs = valueToFs(v_fs);

            if( fs ) {
                char *job = NULL;

                rc = p->fsOps->fs_delete(p, fs, &job);

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }
                lsmFsRecordFree(fs);
            } else {
                rc = LSM_ERR_NO_MAPPING;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int fs_resize(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->fsOps->fs_resize ) {

        Value v_fs = params["fs"];
        Value v_size = params["new_size_bytes"];

        if( Value::object_t == v_fs.valueType() &&
            Value::numeric_t == v_size.valueType() ) {

            lsmFsPtr fs = valueToFs(v_fs);

            if( fs ) {
                uint64_t size_bytes = v_size.asUint64_t();
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
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int fs_clone(lsmPluginPtr p, Value &params, Value &response)
{
   int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->sanOps && p->fsOps->fs_clone ) {

        Value v_src_fs = params["src_fs"];
        Value v_name = params["dest_fs_name"];
        Value v_ss = params["snapshot"];  /* This is optional */

        if( Value::object_t == v_src_fs.valueType() &&
            Value::string_t == v_name.valueType() &&
            (Value::null_t == v_ss.valueType() ||
            Value::object_t == v_ss.valueType())) {

            lsmFsPtr clonedFs = NULL;
            char *job = NULL;
            lsmFsPtr fs = valueToFs(v_src_fs);
            const char* name = v_name.asC_str();
            lsmSsPtr ss = valueToSs(v_ss);

            if( fs &&
                (( ss && v_ss.valueType() == Value::object_t) ||
                    (!ss && v_ss.valueType() == Value::null_t) )) {

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

            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsmFsRecordFree(fs);
            lsmSsRecordFree(ss);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
   return rc;
}

static int file_clone(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_OK;

    if( p && p->sanOps && p->fsOps->fs_file_clone ) {

        Value v_fs = params["fs"];
        Value v_src_name = params["src_file_name"];
        Value v_dest_name = params["dest_file_name"];
        Value v_ss = params["snapshot"];    /* This is optional */

        if( Value::object_t == v_fs.valueType() &&
            Value::string_t == v_src_name.valueType() &&
            (Value::string_t == v_dest_name.valueType() ||
            Value::object_t == v_ss.valueType())) {


            lsmFsPtr fs = valueToFs(v_fs);
            lsmSsPtr ss = valueToSs(v_ss);

            if( fs &&
                (( ss && v_ss.valueType() == Value::object_t) ||
                    (!ss && v_ss.valueType() == Value::null_t) )) {

                const char *src = v_src_name.asC_str();
                const char *dest = v_dest_name.asC_str();

                char *job = NULL;

                rc = p->fsOps->fs_file_clone(p, fs, src, dest, ss,
                                                    &job);

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }

            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsmFsRecordFree(fs);
            lsmSsRecordFree(ss);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int fs_child_dependency(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->sanOps && p->fsOps->fs_child_dependency ) {

        Value v_fs = params["fs"];
        Value v_files = params["files"];

        if( Value::object_t == v_fs.valueType() &&
            Value::array_t == v_files.valueType() ) {

            lsmFsPtr fs = valueToFs(v_fs);
            lsmStringListPtr files = valueToStringList(v_files);

            if( fs && files ) {
                uint8_t yes = 0;

                rc = p->fsOps->fs_child_dependency(p, fs, files, &yes);

                if( LSM_ERR_OK == rc ) {
                    response = Value((bool)yes);
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsmFsRecordFree(fs);
            lsmStringListFree(files);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int fs_child_dependency_rm(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->sanOps && p->fsOps->fs_child_dependency_rm ) {

        Value v_fs = params["fs"];
        Value v_files = params["files"];

        if( Value::object_t == v_fs.valueType() &&
            Value::array_t == v_files.valueType() ) {

            lsmFsPtr fs = valueToFs(v_fs);
            lsmStringListPtr files = valueToStringList(v_files);

            if( fs && files ) {
                char *job = NULL;

                rc = p->fsOps->fs_child_dependency_rm(p, fs, files, &job);

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsmFsRecordFree(fs);
            lsmStringListFree(files);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ss_list(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->sanOps && p->fsOps->ss_list ) {

        Value v_fs = params["fs"];

        if( Value::object_t == v_fs.valueType() ) {

            lsmFsPtr fs = valueToFs(v_fs);

            if( fs ) {
                lsmSsPtr *ss = NULL;
                uint32_t count = 0;

                rc = p->fsOps->ss_list(p, fs, &ss, &count);

                if( LSM_ERR_OK == rc ) {
                    std::vector<Value> result;

                    for( uint32_t i = 0; i < count; ++i ) {
                        result.push_back(ssToValue(ss[i]));
                    }
                    response = Value(result);

                    lsmFsRecordFree(fs);
                    fs = NULL;
                    lsmSsRecordFreeArray(ss, count);
                    ss = NULL;
                }
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ss_create(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->sanOps && p->fsOps->ss_create ) {

        Value v_fs = params["fs"];
        Value v_ss_name = params["snapshot_name"];
        Value v_files = params["files"];

        if( Value::object_t == v_fs.valueType() &&
            Value::string_t == v_ss_name.valueType() &&
            Value::array_t == v_files.valueType() ) {

            lsmFsPtr fs = valueToFs(v_fs);
            lsmStringListPtr files = valueToStringList(v_files);

            if( fs && files ) {
                lsmSsPtr ss = NULL;
                char *job = NULL;

                const char *name = v_ss_name.asC_str();

                rc = p->fsOps->ss_create(p, fs, name, files, &ss, &job);

                std::vector<Value> r;
                if( LSM_ERR_OK == rc ) {
                    r.push_back(Value());
                    r.push_back(ssToValue(ss));
                    response = Value(r);
                    lsmSsRecordFree(ss);
                } else if (LSM_ERR_JOB_STARTED == rc ) {
                    r.push_back(Value(job));
                    r.push_back(Value());
                    response = Value(r);
                    free(job);
                }

            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsmFsRecordFree(fs);
            lsmStringListFree(files);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ss_delete(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->sanOps && p->fsOps->ss_delete ) {

        Value v_fs = params["fs"];
        Value v_ss = params["snapshot"];

        if( Value::object_t == v_fs.valueType() &&
            Value::object_t == v_ss.valueType() ) {

            lsmFsPtr fs = valueToFs(v_fs);
            lsmSsPtr ss = valueToSs(v_ss);

            if( fs && ss ) {
                char *job = NULL;
                rc = p->fsOps->ss_delete(p, fs, ss, &job);

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsmFsRecordFree(fs);
            lsmSsRecordFree(ss);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ss_revert(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->sanOps && p->fsOps->ss_revert ) {

        Value v_fs = params["fs"];
        Value v_ss = params["snapshot"];
        Value v_files = params["files"];
        Value v_restore_files = params["restore_files"];
        Value v_all_files = params["all_files"];

        if( Value::object_t == v_fs.valueType() &&
            Value::object_t == v_ss.valueType() &&
            Value::array_t == v_files.valueType() &&
            Value::array_t == v_files.valueType() &&
            Value::boolean_t == v_all_files.valueType() ) {

            char *job = NULL;
            lsmFsPtr fs = valueToFs(v_fs);
            lsmSsPtr ss = valueToSs(v_ss);
            lsmStringListPtr files = valueToStringList(v_files);
            lsmStringListPtr restore_files =
                    valueToStringList(v_restore_files);
            int all_files = (v_all_files.asBool()) ? 1 : 0;

            if( fs && ss && files && restore_files ) {
                rc = p->fsOps->ss_revert(p, fs, ss, files, restore_files,
                                            all_files, &job);

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsmFsRecordFree(fs);
            lsmSsRecordFree(ss);
            lsmStringListFree(files);
            lsmStringListFree(restore_files);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int export_auth(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->nasOps && p->nasOps->nfs_auth_types ) {
        lsmStringListPtr types = NULL;

        rc = p->nasOps->nfs_auth_types(p, &types);
        if( LSM_ERR_OK == rc ) {
            response = stringListToValue(types);
            lsmStringListFree(types);
        }
    }
    return rc;
}

static int exports(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->nasOps && p->nasOps->nfs_list ) {
        lsmNfsExportPtr *exports = NULL;
        uint32_t count = 0;

        rc = p->nasOps->nfs_list(p, &exports, &count);

        if( LSM_ERR_OK == rc ) {
            std::vector<Value> result;

            for( uint32_t i = 0; i < count; ++i ) {
                result.push_back(nfsExportToValue(exports[i]));
            }
            response = Value(result);

            lsmNfsExportRecordFreeArray(exports, count);
            exports = NULL;
            count = 0;
        }
    }

    return rc;
}

static uint64_t get_uid_gid(Value &id)
{
    if( Value::null_t == id.valueType() ) {
        return ANON_UID_GID_NA;
    } else {
        return id.asUint64_t();
    }
}

static int export_fs(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->nasOps && p->nasOps->nfs_export ) {

        Value v_fs_id = params["fs_id"];
        Value v_export_path = params["export_path"];
        Value v_root_list = params["root_list"];
        Value v_rw_list = params["rw_list"];
        Value v_ro_list = params["ro_list"];
        Value v_auth_type = params["auth_type"];
        Value v_options = params["options"];
        Value v_anon_uid = params["anon_uid"];
        Value v_anon_gid = params["anon_gid"];

        if( Value::string_t == v_fs_id.valueType() &&
            Value::string_t == v_export_path.valueType() &&
            Value::array_t == v_root_list.valueType() &&
            Value::array_t == v_rw_list.valueType() &&
            Value::array_t == v_ro_list.valueType() &&
            (Value::string_t == v_auth_type.valueType() ||
            Value::null_t == v_auth_type.valueType()) &&
            (Value::string_t == v_options.valueType() ||
            Value::null_t == v_options.valueType())  &&
            Value::numeric_t == v_anon_uid.valueType() &&
            Value::numeric_t == v_anon_gid.valueType() ) {

            lsmStringListPtr root_list = valueToStringList(v_root_list);
            lsmStringListPtr rw_list = valueToStringList(v_rw_list);
            lsmStringListPtr ro_list = valueToStringList(v_ro_list);

            if( root_list && rw_list && ro_list ) {
                const char *fs_id = v_fs_id.asC_str();
                const char *export_path = v_export_path.asC_str();
                const char *auth_type = v_auth_type.asC_str();
                const char *options = v_options.asC_str();
                lsmNfsExportPtr exported = NULL;

                uint64_t anon_uid = get_uid_gid(v_anon_uid);
                uint64_t anon_gid = get_uid_gid(v_anon_gid);

                rc = p->nasOps->nfs_export(p, fs_id, export_path, root_list,
                                            rw_list, ro_list, anon_uid,
                                            anon_gid, auth_type, options,
                                            &exported);
                if( LSM_ERR_OK == rc ) {
                    response = nfsExportToValue(exported);
                    lsmNfsExportRecordFree(exported);
                }

            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsmStringListFree(root_list);
            lsmStringListFree(rw_list);
            lsmStringListFree(ro_list);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int export_remove(lsmPluginPtr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->nasOps && p->nasOps->nfs_export_remove ) {
        Value v_export = params["export"];

        if( Value::object_t == v_export.valueType() ) {
            lsmNfsExportPtr exp = valueToNfsExport(v_export);

            if( exp ) {
                rc = p->nasOps->nfs_export_remove(p, exp);
                lsmNfsExportRecordFree(exp);
                exp = NULL;
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
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
    ("file_clone", file_clone)
    ("snapshots", ss_list)
    ("snapshot_create", ss_create)
    ("snapshot_delete", ss_delete)
    ("snapshot_revert", ss_revert)
    ("fs_child_dependency", fs_child_dependency)
    ("fs_child_dependency_rm", fs_child_dependency_rm)
    ("export_auth", export_auth)
    ("exports", exports)
    ("export_fs", export_fs)
    ("export_remove", export_remove);

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
