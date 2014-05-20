/*
 * Copyright (C) 2011-2014 Red Hat, Inc.
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
#include "libstoragemgmt/libstoragemgmt_disk.h"
#include "libstoragemgmt/libstoragemgmt_accessgroups.h"
#include "libstoragemgmt/libstoragemgmt_fs.h"
#include "libstoragemgmt/libstoragemgmt_snapshot.h"
#include "libstoragemgmt/libstoragemgmt_nfsexport.h"
#include "libstoragemgmt/libstoragemgmt_plug_interface.h"
#include "libstoragemgmt/libstoragemgmt_volumes.h"
#include "libstoragemgmt/libstoragemgmt_pool.h"
#include "libstoragemgmt/libstoragemgmt_initiators.h"
#include <errno.h>
#include <string.h>
#include <libxml/uri.h>
#include "util/qparams.h"
#include <syslog.h>

//Forward decl.
static int lsm_plugin_run(lsm_plugin_ptr plug);

/**
 * Safe string wrapper
 * @param s Character array to convert to std::string
 * @return String representation.
 */
static std::string ss(char *s)
{
    if( s ) {
        return std::string(s);
    }
    return std::string();
}

void * lsm_data_type_copy(lsm_data_type t, void *item)
{
    void *rc = NULL;

    if( item ) {
        switch( t ) {
            case(LSM_DATA_TYPE_ACCESS_GROUP):
                rc = lsm_access_group_record_copy((lsm_access_group *)item);
                break;
            case(LSM_DATA_TYPE_BLOCK_RANGE):
                rc = lsm_block_range_record_copy((lsm_block_range *)item);
                break;
            case(LSM_DATA_TYPE_FS):
                rc = lsm_fs_record_copy((lsm_fs *)item);
                break;
            case(LSM_DATA_TYPE_INITIATOR):
                rc = lsm_initiator_record_copy((lsm_initiator *)item);
                break;
            case(LSM_DATA_TYPE_NFS_EXPORT):
                rc = lsm_nfs_export_record_copy((lsm_nfs_export *)item);
                break;
            case(LSM_DATA_TYPE_POOL):
                rc = lsm_pool_record_copy((lsm_pool *)item);
                break;
            case(LSM_DATA_TYPE_SS):
                rc = lsm_fs_ss_record_copy((lsm_fs_ss *)item);
                break;
            case(LSM_DATA_TYPE_STRING_LIST):
                rc = lsm_string_list_copy((lsm_string_list *)item);
                break;
            case(LSM_DATA_TYPE_SYSTEM):
                rc = lsm_system_record_copy((lsm_system *)item);
                break;
            case(LSM_DATA_TYPE_VOLUME):
                rc = lsm_volume_record_copy((lsm_volume *)item);
                break;
            case(LSM_DATA_TYPE_DISK):
                 rc = lsm_disk_record_copy((lsm_disk *)item);
                 break;
            default:
                break;
            }
    }
    return rc;
}

static Value job_handle(const Value &val, char *job)
{
    std::vector<Value> r;
    r.push_back(Value(job));
    r.push_back(val);
    return Value(r);
}

int lsm_register_plugin_v1(lsm_plugin_ptr plug,
                        void *private_data, struct lsm_mgmt_ops_v1 *mgm_op,
                        struct lsm_san_ops_v1 *san_op, struct lsm_fs_ops_v1 *fs_op,
                        struct lsm_nas_ops_v1 *nas_op)
{
    int rc = LSM_ERR_INVALID_PLUGIN;

    if(LSM_IS_PLUGIN(plug)) {
        plug->private_data = private_data;
        plug->mgmt_ops = mgm_op;
        plug->san_ops = san_op;
        plug->fs_ops = fs_op;
        plug->nas_ops = nas_op;
        rc = LSM_ERR_OK;
    }
    return rc;
}

void *lsm_private_data_get(lsm_plugin_ptr plug)
{
    if (!LSM_IS_PLUGIN(plug)) {
        return NULL;
    }

    return plug->private_data;
}

static void lsm_plugin_free(lsm_plugin_ptr p, lsm_flag flags)
{
    if( LSM_IS_PLUGIN(p) ) {

        delete(p->tp);
        p->tp = NULL;

        if( p->unreg ) {
            p->unreg(p, flags);
        }

        free(p->desc);
        p->desc = NULL;

        free(p->version);
        p->version = NULL;

        lsm_error_free(p->error);
        p->error = NULL;

        p->magic = LSM_DEL_MAGIC(LSM_PLUGIN_MAGIC);

        free(p);
    }
}

static lsm_plugin_ptr lsm_plugin_alloc(lsm_plugin_register reg,
                                    lsm_plugin_unregister unreg,
                                    const char* desc, const char *version) {

    if( !reg || !unreg ) {
        return NULL;
    }

    lsm_plugin_ptr rc = (lsm_plugin_ptr)calloc(1, sizeof(lsm_plugin));
    if( rc ) {
        rc->magic = LSM_PLUGIN_MAGIC;
        rc->reg = reg;
        rc->unreg = unreg;
        rc->desc = strdup(desc);
        rc->version = strdup(version);

        if (!rc->desc || !rc->version) {
            lsm_plugin_free(rc, LSM_FLAG_RSVD);
            rc = NULL;
        }
    }
    return rc;
}

static void error_send(lsm_plugin_ptr p, int error_code)
{
    if( !LSM_IS_PLUGIN(p) ) {
        return;
    }

    if( p->error ) {
        if( p->tp ) {
            p->tp->errorSend(p->error->code, ss(p->error->message),
                                ss(p->error->debug));
            lsm_error_free(p->error);
            p->error = NULL;
        }
    } else {
        p->tp->errorSend(error_code, "UNA", "UNA");
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

int lsm_plugin_init_v1( int argc, char *argv[], lsm_plugin_register reg,
                    lsm_plugin_unregister unreg,
                    const char *desc, const char *version)
{
    int rc = 1;
    lsm_plugin_ptr plug = NULL;

    if (NULL == desc || NULL == version) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    int sd = 0;
    if( argc == 2 && get_num(argv[1], sd) ) {
        plug = lsm_plugin_alloc(reg, unreg, desc, version);
        if( plug ) {
            plug->tp = new Ipc(sd);
            if (plug->tp) {
                rc = lsm_plugin_run(plug);
            } else {
                lsm_plugin_free(plug, LSM_FLAG_RSVD);
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            rc = LSM_ERR_NO_MEMORY;
        }
    } else {
        //Process command line arguments or display help text.
        rc = 2;
    }

    return rc;
}

typedef int (*handler)(lsm_plugin_ptr p, Value &params, Value &response);

static int handle_unregister(lsm_plugin_ptr p, Value &params, Value &response)
{
    return LSM_ERR_OK;
}

static int handle_register(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    std::string uri_string;
    std::string password;

    if( p && p->reg ) {

        Value uri_v = params["uri"];
        Value passwd_v = params["password"];
        Value tmo_v = params["timeout"];

        if( Value::string_t == uri_v.valueType() &&
            (Value::string_t == passwd_v.valueType() ||
            Value::null_t == passwd_v.valueType()) &&
            Value::numeric_t == tmo_v.valueType()) {
            lsm_flag flags = LSM_FLAG_GET_VALUE(params);

            uri_string = uri_v.asString();

            if( Value::string_t == params["password"].valueType() ) {
                password = params["password"].asString();
            }

            //Let the plug-in initialize itself.
            rc = p->reg(p, uri_string.c_str(), password.c_str(),
                            tmo_v.asUint32_t(),
                            flags);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    } else {
        rc = LSM_ERR_NO_SUPPORT;
    }
    return rc;
}

static int handle_set_time_out( lsm_plugin_ptr p, Value &params, Value &response)
{
    if( p && p->mgmt_ops && p->mgmt_ops->tmo_set ) {
        if( Value::numeric_t == params["ms"].valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {
            return p->mgmt_ops->tmo_set(p, params["ms"].asUint32_t(),
                    LSM_FLAG_GET_VALUE(params));
        } else {
            return LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_get_time_out( lsm_plugin_ptr p, Value &params, Value &response)
{
    uint32_t tmo = 0;
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->mgmt_ops && p->mgmt_ops->tmo_get ) {

        if( LSM_FLAG_EXPECTED_TYPE(params) ) {
            rc = p->mgmt_ops->tmo_get(p, &tmo, LSM_FLAG_GET_VALUE(params));
            if( LSM_ERR_OK == rc) {
                response = Value(tmo);
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_job_status( lsm_plugin_ptr p, Value &params, Value &response)
{
    std::string job_id;
    lsm_job_status status;
    uint8_t percent;
    lsm_data_type t = LSM_DATA_TYPE_UNKNOWN;
    void *value = NULL;
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->mgmt_ops && p->mgmt_ops->job_status ) {

        if( Value::string_t != params["job_id"].valueType() &&
            !LSM_FLAG_EXPECTED_TYPE(params) ) {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        } else {

            job_id = params["job_id"].asString();

            rc = p->mgmt_ops->job_status(p, job_id.c_str(), &status, &percent, &t,
                        &value, LSM_FLAG_GET_VALUE(params));

            if( LSM_ERR_OK == rc) {
                std::vector<Value> result;

                result.push_back(Value((int32_t)status));
                result.push_back(Value(percent));

                if( NULL == value ) {
                    result.push_back(Value());
                } else {
                    if( LSM_DATA_TYPE_VOLUME == t &&
                        LSM_IS_VOL((lsm_volume *)value)) {
                        result.push_back(volume_to_value((lsm_volume *)value));
                        lsm_volume_record_free((lsm_volume *)value);
                    } else if(  LSM_DATA_TYPE_FS == t &&
                        LSM_IS_FS((lsm_fs *)value)) {
                        result.push_back(fs_to_value((lsm_fs *)value));
                        lsm_fs_record_free((lsm_fs *)value);
                    } else if(  LSM_DATA_TYPE_SS == t &&
                        LSM_IS_SS((lsm_fs_ss *)value)) {
                        result.push_back(ss_to_value((lsm_fs_ss *)value));
                        lsm_fs_ss_record_free((lsm_fs_ss *)value);
                    } else if(  LSM_DATA_TYPE_POOL == t &&
                        LSM_IS_POOL((lsm_pool *)value)) {
                        result.push_back(pool_to_value((lsm_pool *)value));
                        lsm_pool_record_free((lsm_pool *)value);
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

static int handle_plugin_info( lsm_plugin_ptr p, Value &params, Value &response )
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p ) {
        std::vector<Value> result;
        result.push_back(Value(p->desc));
        result.push_back(Value(p->version));
        response = Value(result);
        rc = LSM_ERR_OK;
    }
    return rc;
}

static int handle_job_free(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->mgmt_ops && p->mgmt_ops->job_free ) {
        if( Value::string_t == params["job_id"].valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {
            std::string job_num = params["job_id"].asString();
            char *j = (char*)job_num.c_str();
            rc = p->mgmt_ops->job_free(p, j, LSM_FLAG_GET_VALUE(params));
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_system_list(lsm_plugin_ptr p, Value &params,
                                    Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->mgmt_ops && p->mgmt_ops->system_list ) {
        lsm_system **systems;
        uint32_t count = 0;

        if( LSM_FLAG_EXPECTED_TYPE(params) ) {

            rc = p->mgmt_ops->system_list(p, &systems, &count,
                                                LSM_FLAG_GET_VALUE(params));
            if( LSM_ERR_OK == rc ) {
                std::vector<Value> result;

                for( uint32_t i = 0; i < count; ++i ) {
                    result.push_back(system_to_value(systems[i]));
                }

                lsm_system_record_array_free(systems, count);
                systems = NULL;
                response = Value(result);
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}
static int handle_pools(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->mgmt_ops && p->mgmt_ops->pool_list ) {
        lsm_pool **pools = NULL;
        uint32_t count = 0;

        if( LSM_FLAG_EXPECTED_TYPE(params) ) {
            rc = p->mgmt_ops->pool_list(p, &pools, &count,
                                        LSM_FLAG_GET_VALUE(params));
            if( LSM_ERR_OK == rc) {
                std::vector<Value> result;

                for( uint32_t i = 0; i < count; ++i ) {
                    result.push_back(pool_to_value(pools[i]));
                }

                lsm_pool_record_array_free(pools, count);
                pools = NULL;
                response = Value(result);
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_pool_create(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->san_ops && p->san_ops->pool_create ) {

        Value v_sys = params["system"];
        Value v_pool_name = params["pool_name"];
        Value v_size = params["size_bytes"];
        Value v_raid_t = params["raid_type"];
        Value v_member_t = params["member_type"];

        if( Value::object_t == v_sys.valueType() &&
            Value::string_t == v_pool_name.valueType() &&
            Value::numeric_t == v_size.valueType() &&
            Value::numeric_t == v_raid_t.valueType() &&
            Value::numeric_t == v_member_t.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_system *system = value_to_system(v_sys);
            const char *pool_name = v_pool_name.asC_str();
            uint64_t size = v_size.asUint64_t();
            lsm_pool_raid_type raid_type = (lsm_pool_raid_type)v_raid_t.asInt32_t();
            lsm_pool_member_type member_type = (lsm_pool_member_type)v_member_t.asInt32_t();
            lsm_pool *pool = NULL;
            char *job = NULL;

            rc = p->san_ops->pool_create(p, system, pool_name, size, raid_type,
                                            member_type, &pool, &job,
                                            LSM_FLAG_GET_VALUE(params));

            Value p = pool_to_value(pool);
            response = job_handle(p, job);
            lsm_pool_record_free(pool);
            lsm_system_record_free(system);
            free(job);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_pool_create_from_disks(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->san_ops && p->san_ops->pool_create_from_disks ) {

        Value v_sys = params["system"];
        Value v_pool_name = params["pool_name"];
        Value v_disks = params["disks"];
        Value v_raid_t = params["raid_type"];

        if( Value::object_t == v_sys.valueType() &&
            Value::string_t == v_pool_name.valueType() &&
            Value::array_t == v_disks.valueType() &&
            Value::numeric_t == v_raid_t.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            /* Get the array of disks */
            lsm_disk **disks = NULL;
            uint32_t num_disks = 0;
            rc = value_array_to_disks(v_disks, &disks, &num_disks);

            if( LSM_ERR_OK == rc ) {
                lsm_system *sys = value_to_system(v_sys);
                const char *pool_name = v_pool_name.asC_str();
                lsm_pool_raid_type raid_type = (lsm_pool_raid_type)v_raid_t.asInt32_t();

                lsm_pool *pool = NULL;
                char *job = NULL;

                rc = p->san_ops->pool_create_from_disks(p, sys, pool_name,
                                        disks, num_disks, raid_type,
                                        &pool, &job, LSM_FLAG_GET_VALUE(params));

                Value p = pool_to_value(pool);
                response = job_handle(p, job);
                lsm_disk_record_array_free(disks, num_disks);
                lsm_pool_record_free(pool);
                lsm_system_record_free(sys);
                free(job);
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_pool_create_from_volumes(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->san_ops && p->san_ops->pool_create_from_volumes ) {

        Value v_sys = params["system"];
        Value v_pool_name = params["pool_name"];
        Value v_volumes = params["volumes"];
        Value v_raid_t = params["raid_type"];

        if( Value::object_t == v_sys.valueType() &&
            Value::string_t == v_pool_name.valueType() &&
            Value::array_t == v_volumes.valueType() &&
            Value::numeric_t == v_raid_t.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            /* Get the array of disks */
            lsm_volume **volumes = NULL;
            uint32_t num_volumes = 0;
            rc = value_array_to_volumes(v_volumes, &volumes, &num_volumes);

            if( LSM_ERR_OK == rc ) {
                lsm_system *sys = value_to_system(v_sys);
                const char *pool_name = v_pool_name.asC_str();
                lsm_pool_raid_type raid_type = (lsm_pool_raid_type)v_raid_t.asInt32_t();

                lsm_pool *pool = NULL;
                char *job = NULL;

                rc = p->san_ops->pool_create_from_volumes(p, sys, pool_name,
                                        volumes, num_volumes, raid_type,
                                        &pool, &job, LSM_FLAG_GET_VALUE(params));

                Value p = pool_to_value(pool);
                response = job_handle(p, job);
                lsm_volume_record_array_free(volumes, num_volumes);
                lsm_pool_record_free(pool);
                lsm_system_record_free(sys);
                free(job);
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_pool_create_from_pool(lsm_plugin_ptr p, Value &params, Value &response)
{
     int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->san_ops && p->san_ops->pool_create_from_pool ) {

        Value v_sys = params["system"];
        Value v_pool_name = params["pool_name"];
        Value v_pool = params["pool"];
        Value v_size = params["size_bytes"];

        if( Value::object_t == v_sys.valueType() &&
            Value::string_t == v_pool_name.valueType() &&
            Value::object_t == v_pool.valueType() &&
            Value::numeric_t == v_size.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_system *sys = value_to_system(v_sys);
            const char *pool_name = v_pool_name.asC_str();
            lsm_pool *pool = value_to_pool(v_pool);
            uint64_t size = v_size.asUint64_t();

            lsm_pool *created_pool = NULL;
            char *job = NULL;

            rc = p->san_ops->pool_create_from_pool(p, sys, pool_name,
                                            pool, size, &created_pool, &job,
                                            LSM_FLAG_GET_VALUE(params));

            Value p = pool_to_value(created_pool);
            response = job_handle(p, job);
            lsm_pool_record_free(created_pool);
            lsm_pool_record_free(pool);
            lsm_system_record_free(sys);
            free(job);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_pool_delete(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->san_ops && p->san_ops->pool_delete ) {
        Value v_pool = params["pool"];

        if(Value::object_t == v_pool.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {
            lsm_pool *pool = value_to_pool(v_pool);

            if( pool ) {
                char *job = NULL;

                rc = p->san_ops->pool_delete(p, pool, &job,
                                            LSM_FLAG_GET_VALUE(params));

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                }

                lsm_pool_record_free(pool);
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

static int capabilities(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->mgmt_ops && p->mgmt_ops->capablities) {
        lsm_storage_capabilities *c = NULL;

        Value v_s = params["system"];

        if( Value::object_t == v_s.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {
            lsm_system *sys = value_to_system(v_s);

            if( sys ) {
                rc = p->mgmt_ops->capablities(p, sys, &c,
                                                LSM_FLAG_GET_VALUE(params));
                if( LSM_ERR_OK == rc) {
                    response = capabilities_to_value(c);
                    lsm_capability_record_free(c);
                    c = NULL;
                }
                lsm_system_record_free(sys);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static void get_initiators(int rc, lsm_initiator **inits, uint32_t count,
                            Value &resp) {

    if( LSM_ERR_OK == rc ) {
        std::vector<Value> result;

        for( uint32_t i = 0; i < count; ++i ) {
            result.push_back(initiator_to_value(inits[i]));
        }

        lsm_initiator_record_array_free(inits, count);
        inits = NULL;
        resp = Value(result);
    }
}

static int handle_initiators(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->init_get ) {
        lsm_initiator **inits = NULL;
        uint32_t count = 0;

        if( LSM_FLAG_EXPECTED_TYPE(params) ) {
            rc = p->san_ops->init_get(p, &inits, &count,
                                        LSM_FLAG_GET_VALUE(params));
            get_initiators(rc, inits, count, response);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static void get_volumes(int rc, lsm_volume **vols, uint32_t count,
                        Value &response)
{
    if( LSM_ERR_OK == rc ) {
        std::vector<Value> result;

        for( uint32_t i = 0; i < count; ++i ) {
            result.push_back(volume_to_value(vols[i]));
        }

        lsm_volume_record_array_free(vols, count);
        vols = NULL;
        response = Value(result);
    }
}

static int handle_volumes(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;


    if( p && p->san_ops && p->san_ops->vol_get ) {
        lsm_volume **vols = NULL;
        uint32_t count = 0;

        if( LSM_FLAG_EXPECTED_TYPE(params) ) {
            rc = p->san_ops->vol_get(p, &vols, &count,
                                        LSM_FLAG_GET_VALUE(params));

            get_volumes(rc, vols, count, response);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static void get_disks(int rc, lsm_disk **disks, uint32_t count, Value &response)
{
     if( LSM_ERR_OK == rc ) {
        std::vector<Value> result;

        for( uint32_t i = 0; i < count; ++i ) {
            result.push_back(disk_to_value(disks[i]));
        }

        lsm_disk_record_array_free(disks, count);
        disks = NULL;
        response = Value(result);
    }
}

static int handle_disks(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->disk_get ) {
        lsm_disk **disks = NULL;
        uint32_t count = 0;

        if( LSM_FLAG_EXPECTED_TYPE(params) ) {
            rc = p->san_ops->disk_get(p, &disks, &count,
                LSM_FLAG_GET_VALUE(params));
            get_disks(rc, disks, count, response);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_volume_create(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->san_ops && p->san_ops->vol_create ) {

        Value v_p = params["pool"];
        Value v_name = params["volume_name"];
        Value v_size = params["size_bytes"];
        Value v_prov = params["provisioning"];

        if( Value::object_t == v_p.valueType() &&
            Value::string_t == v_name.valueType() &&
            Value::numeric_t == v_size.valueType() &&
            Value::numeric_t == v_prov.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_pool *pool = value_to_pool(v_p);
            if( pool ) {
                lsm_volume *vol = NULL;
                char *job = NULL;
                const char *name = v_name.asC_str();
                uint64_t size = v_size.asUint64_t();
                lsm_provision_type pro = (lsm_provision_type)v_prov.asInt32_t();

                rc = p->san_ops->vol_create(p, pool, name, size, pro, &vol, &job,
                                            LSM_FLAG_GET_VALUE(params));

                Value v = volume_to_value(vol);
                response = job_handle(v, job);

                //Free dynamic data.
                lsm_pool_record_free(pool);
                lsm_volume_record_free(vol);
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

static int handle_volume_resize(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->san_ops && p->san_ops->vol_resize ) {
        Value v_vol = params["volume"];
        Value v_size = params["new_size_bytes"];

        if( Value::object_t == v_vol.valueType() &&
            Value::numeric_t == v_size.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {

            lsm_volume *vol = value_to_volume(v_vol);
            if( vol ) {
                lsm_volume *resized_vol = NULL;
                uint64_t size = v_size.asUint64_t();
                char *job = NULL;

                rc = p->san_ops->vol_resize(p, vol, size, &resized_vol, &job,
                                            LSM_FLAG_GET_VALUE(params));

                Value v = volume_to_value(resized_vol);
                response = job_handle(v, job);

                lsm_volume_record_free(vol);
                lsm_volume_record_free(resized_vol);
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

static int handle_volume_replicate(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->vol_replicate ) {

        Value v_pool = params["pool"];
        Value v_vol_src = params["volume_src"];
        Value v_rep = params["rep_type"];
        Value v_name = params["name"];

        if( (Value::object_t == v_pool.valueType() || Value::null_t == v_pool.valueType()) &&
            Value::object_t == v_vol_src.valueType() &&
            Value::numeric_t == v_rep.valueType() &&
            Value::string_t == v_name.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {

            lsm_pool *pool = value_to_pool(v_pool);
            lsm_volume *vol = value_to_volume(v_vol_src);
            lsm_volume *newVolume = NULL;
            lsm_replication_type rep = (lsm_replication_type)v_rep.asInt32_t();
            const char *name = v_name.asC_str();
            char *job = NULL;

            if( vol ) {
                rc = p->san_ops->vol_replicate(p, pool, rep, vol, name,
                                                &newVolume, &job,
                                                LSM_FLAG_GET_VALUE(params));

                Value v = volume_to_value(newVolume);
                response = job_handle(v, job);

                lsm_volume_record_free(newVolume);
                free(job);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsm_pool_record_free(pool);
            lsm_volume_record_free(vol);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_volume_replicate_range_block_size( lsm_plugin_ptr p,
                                                        Value &params,
                                                        Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    uint32_t block_size = 0;

    if( p && p->san_ops && p->san_ops->vol_rep_range_bs ) {
        Value v_s = params["system"];

        if( Value::object_t == v_s.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {
            lsm_system *sys = value_to_system(v_s);

            if( sys ) {
                rc = p->san_ops->vol_rep_range_bs(p, sys, &block_size,
                                        LSM_FLAG_GET_VALUE(params));

                if( LSM_ERR_OK == rc ) {
                    response = Value(block_size);
                }

                lsm_system_record_free(sys);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_volume_replicate_range(lsm_plugin_ptr p, Value &params,
                                            Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    uint32_t range_count = 0;
    char *job = NULL;
    if( p && p->san_ops && p->san_ops->vol_rep_range ) {
        Value v_rep = params["rep_type"];
        Value v_vol_src = params["volume_src"];
        Value v_vol_dest = params["volume_dest"];
        Value v_ranges = params["ranges"];

        if( Value::numeric_t == v_rep.valueType() &&
            Value::object_t == v_vol_src.valueType() &&
            Value::object_t == v_vol_dest.valueType() &&
            Value::array_t == v_ranges.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {

            lsm_replication_type repType = (lsm_replication_type)
                                        v_rep.asInt32_t();
            lsm_volume *source = value_to_volume(v_vol_src);
            lsm_volume *dest = value_to_volume(v_vol_dest);
            lsm_block_range **ranges = value_to_block_range_list(v_ranges,
                                                                &range_count);

            if( source && dest && ranges ) {

                rc = p->san_ops->vol_rep_range(p, repType, source, dest, ranges,
                                                range_count, &job,
                                                LSM_FLAG_GET_VALUE(params));

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                    job = NULL;
                }

            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsm_volume_record_free(source);
            lsm_volume_record_free(dest);
            lsm_block_range_record_array_free(ranges, range_count);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_volume_delete(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->san_ops && p->san_ops->vol_delete ) {
        Value v_vol = params["volume"];

        if(Value::object_t == v_vol.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {
            lsm_volume *vol = value_to_volume(params["volume"]);

            if( vol ) {
                char *job = NULL;

                rc = p->san_ops->vol_delete(p, vol, &job,
                                            LSM_FLAG_GET_VALUE(params));

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                }

                lsm_volume_record_free(vol);
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

static int handle_vol_online_offline( lsm_plugin_ptr p, Value &params,
                                        Value &response, int online)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops &&
        ((online)? p->san_ops->vol_online : p->san_ops->vol_offline)) {

        Value v_vol = params["volume"];

        if( Value::object_t == v_vol.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {
            lsm_volume *vol = value_to_volume(v_vol);
            if( vol ) {
                if( online ) {
                    rc = p->san_ops->vol_online(p, vol,
                                                LSM_FLAG_GET_VALUE(params));
                } else {
                    rc = p->san_ops->vol_offline(p, vol,
                                                LSM_FLAG_GET_VALUE(params));
                }

                lsm_volume_record_free(vol);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_volume_online(lsm_plugin_ptr p, Value &params, Value &response)
{
    return handle_vol_online_offline(p, params, response, 1);
}

static int handle_volume_offline(lsm_plugin_ptr p, Value &params, Value &response)
{
    return handle_vol_online_offline(p, params, response, 0);
}

static int ag_list(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->ag_list ) {

        if( LSM_FLAG_EXPECTED_TYPE(params) ) {
            lsm_access_group **groups = NULL;
            uint32_t count;

            rc = p->san_ops->ag_list(p, &groups, &count,
                                    LSM_FLAG_GET_VALUE(params));
            if( LSM_ERR_OK == rc ) {
                response = access_group_list_to_value(groups, count);

                /* Free the memory */
                lsm_access_group_record_array_free(groups, count);
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ag_create(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->ag_create ) {
        Value v_name = params["name"];
        Value v_init_id = params["initiator_id"];
        Value v_id_type = params["id_type"];
        Value v_system_id = params["system_id"];

        if( Value::string_t == v_name.valueType() &&
            Value::string_t == v_init_id.valueType() &&
            Value::numeric_t == v_id_type.valueType() &&
            Value::string_t == v_system_id.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_access_group *ag = NULL;
            rc = p->san_ops->ag_create(p, v_name.asC_str(),
                                    v_init_id.asC_str(),
                                    (lsm_initiator_type)v_id_type.asInt32_t(),
                                    v_system_id.asC_str(), &ag,
                                    LSM_FLAG_GET_VALUE(params));
            if( LSM_ERR_OK == rc ) {
                response = access_group_to_value(ag);
                lsm_access_group_record_free(ag);
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ag_delete(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->ag_delete ) {
        Value v_group = params["group"];

        if( Value::object_t == v_group.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_access_group *ag = value_to_access_group(v_group);

            if( ag ) {
                rc = p->san_ops->ag_delete(p, ag, LSM_FLAG_GET_VALUE(params));
                lsm_access_group_record_free(ag);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ag_initiator_add(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->ag_add_initiator ) {

        Value v_group = params["group"];
        Value v_id = params["initiator_id"];
        Value v_id_type = params["id_type"];


        if( Value::object_t == v_group.valueType() &&
            Value::string_t == v_id.valueType() &&
            Value::numeric_t == v_id_type.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {

            lsm_access_group *ag = value_to_access_group(v_group);
            if( ag ) {
                const char *id = v_id.asC_str();
                lsm_initiator_type id_type = (lsm_initiator_type)
                                            v_id_type.asInt32_t();

                rc = p->san_ops->ag_add_initiator(p, ag, id, id_type,
                                                    LSM_FLAG_GET_VALUE(params));

                lsm_access_group_record_free(ag);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }

    return rc;
}

static int ag_initiator_del(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->ag_del_initiator ) {

        Value v_group = params["group"];
        Value v_init_id = params["initiator_id"];

        if( Value::object_t == v_group.valueType() &&
            Value::string_t == v_init_id.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {

            lsm_access_group *ag = value_to_access_group(v_group);

            if( ag ) {
                const char *init = v_init_id.asC_str();
                rc = p->san_ops->ag_del_initiator(p, ag, init,
                                                LSM_FLAG_GET_VALUE(params));
                lsm_access_group_record_free(ag);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ag_grant(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->ag_grant ) {

        Value v_group = params["group"];
        Value v_vol = params["volume"];
        Value v_access = params["access"];

        if( Value::object_t == v_group.valueType() &&
            Value::object_t == v_vol.valueType() &&
            Value::numeric_t == v_access.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {

            lsm_access_group *ag = value_to_access_group(v_group);
            lsm_volume *vol = value_to_volume(v_vol);

            if( ag && vol ) {
                lsm_access_type access = (lsm_access_type)v_access.asInt32_t();



                rc = p->san_ops->ag_grant(p, ag, vol, access,
                                            LSM_FLAG_GET_VALUE(params));
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsm_access_group_record_free(ag);
            lsm_volume_record_free(vol);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }

    return rc;
}

static int ag_revoke(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->ag_revoke ) {

        Value v_group = params["group"];
        Value v_vol = params["volume"];

        if( Value::object_t == v_group.valueType() &&
            Value::object_t == v_vol.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_access_group *ag = value_to_access_group(v_group);
            lsm_volume *vol = value_to_volume(v_vol);

            if( ag && vol ) {
                rc = p->san_ops->ag_revoke(p, ag, vol,
                                            LSM_FLAG_GET_VALUE(params));
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsm_access_group_record_free(ag);
            lsm_volume_record_free(vol);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }

    return rc;
}

static int vol_accessible_by_ag(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->vol_accessible_by_ag ) {
        Value v_group = params["group"];

        if( Value::object_t == v_group.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {
            lsm_access_group *ag = value_to_access_group(v_group);

            if( ag ) {
                lsm_volume **vols = NULL;
                uint32_t count = 0;

                rc = p->san_ops->vol_accessible_by_ag(p, ag, &vols, &count,
                                                LSM_FLAG_GET_VALUE(params));

                if( LSM_ERR_OK == rc ) {
                    std::vector<Value> result;

                    for( uint32_t i = 0; i < count; ++i ) {
                        result.push_back(volume_to_value(vols[i]));
                    }
                    response = Value(result);
                }

                lsm_access_group_record_free(ag);
                lsm_volume_record_array_free(vols, count);
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

static int ag_granted_to_volume(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->ag_granted_to_vol ) {

        Value v_vol = params["volume"];

        if( Value::object_t == v_vol.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {
            lsm_volume *volume = value_to_volume(v_vol);

            if( volume ) {
                lsm_access_group **groups = NULL;
                uint32_t count = 0;

                rc = p->san_ops->ag_granted_to_vol(p, volume, &groups, &count,
                                                LSM_FLAG_GET_VALUE(params));

                if( LSM_ERR_OK == rc ) {
                    std::vector<Value> result;

                    for( uint32_t i = 0; i < count; ++i ) {
                        result.push_back(access_group_to_value(groups[i]));
                    }
                    response = Value(result);
                }

                lsm_volume_record_free(volume);
                lsm_access_group_record_array_free(groups, count);
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

static int volume_dependency(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->vol_child_depends ) {

        Value v_vol = params["volume"];

        if( Value::object_t == v_vol.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {
            lsm_volume *volume = value_to_volume(v_vol);

            if( volume ) {
                uint8_t yes;

                rc = p->san_ops->vol_child_depends(p, volume, &yes,
                                                LSM_FLAG_GET_VALUE(params));

                if( LSM_ERR_OK == rc ) {
                    response = Value((bool)(yes));
                }

                lsm_volume_record_free(volume);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }

    return rc;
}

static int volume_dependency_rm(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->vol_child_depends_rm ) {

        Value v_vol = params["volume"];

        if( Value::object_t == v_vol.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {
            lsm_volume *volume = value_to_volume(v_vol);

            if( volume ) {

                char *job = NULL;

                rc = p->san_ops->vol_child_depends_rm(p, volume, &job,
                                                LSM_FLAG_GET_VALUE(params));

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }
                lsm_volume_record_free(volume);

            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int fs(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->fs_ops->fs_list ) {
        if( LSM_FLAG_EXPECTED_TYPE(params) ) {

            lsm_fs **fs = NULL;
            uint32_t count = 0;

            rc = p->fs_ops->fs_list(p, &fs, &count,
                                        LSM_FLAG_GET_VALUE(params));

            if( LSM_ERR_OK == rc ) {
                std::vector<Value> result;

                for( uint32_t i = 0; i < count; ++i ) {
                    result.push_back(fs_to_value(fs[i]));
                }

                response = Value(result);
                lsm_fs_record_array_free(fs, count);
                fs = NULL;
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int fs_create(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->fs_ops->fs_create ) {

        Value v_pool = params["pool"];
        Value v_name = params["name"];
        Value v_size = params["size_bytes"];

        if( Value::object_t == v_pool.valueType() &&
            Value::string_t == v_name.valueType() &&
            Value::numeric_t == v_size.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {

            lsm_pool *pool = value_to_pool(v_pool);

            if( pool ) {
                const char *name = params["name"].asC_str();
                uint64_t size_bytes = params["size_bytes"].asUint64_t();
                lsm_fs *fs = NULL;
                char *job = NULL;

                rc = p->fs_ops->fs_create(p, pool, name, size_bytes, &fs, &job,
                                            LSM_FLAG_GET_VALUE(params));

                std::vector<Value> r;

                if( LSM_ERR_OK == rc ) {
                    r.push_back(Value());
                    r.push_back(fs_to_value(fs));
                    response = Value(r);
                    lsm_fs_record_free(fs);
                } else if (LSM_ERR_JOB_STARTED == rc ) {
                    r.push_back(Value(job));
                    r.push_back(Value());
                    response = Value(r);
                    free(job);
                }
                lsm_pool_record_free(pool);

            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int fs_delete(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->fs_ops->fs_delete ) {

        Value v_fs = params["fs"];

        if( Value::object_t == v_fs.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_fs *fs = value_to_fs(v_fs);

            if( fs ) {
                char *job = NULL;

                rc = p->fs_ops->fs_delete(p, fs, &job,
                                            LSM_FLAG_GET_VALUE(params));

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }
                lsm_fs_record_free(fs);
            } else {
                rc = LSM_ERR_NO_MAPPING;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int fs_resize(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->fs_ops->fs_resize ) {

        Value v_fs = params["fs"];
        Value v_size = params["new_size_bytes"];

        if( Value::object_t == v_fs.valueType() &&
            Value::numeric_t == v_size.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {

            lsm_fs *fs = value_to_fs(v_fs);

            if( fs ) {
                uint64_t size_bytes = v_size.asUint64_t();
                lsm_fs *rfs = NULL;
                char *job = NULL;

                rc = p->fs_ops->fs_resize(p, fs, size_bytes, &rfs, &job,
                                            LSM_FLAG_GET_VALUE(params));

                std::vector<Value> r;

                if( LSM_ERR_OK == rc ) {
                    r.push_back(Value());
                    r.push_back(fs_to_value(rfs));
                    response = Value(r);
                    lsm_fs_record_free(rfs);
                } else if (LSM_ERR_JOB_STARTED == rc ) {
                    r.push_back(Value(job));
                    r.push_back(Value());
                    response = Value(r);
                    free(job);
                }
                lsm_fs_record_free(fs);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int fs_clone(lsm_plugin_ptr p, Value &params, Value &response)
{
   int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->fs_ops->fs_clone ) {

        Value v_src_fs = params["src_fs"];
        Value v_name = params["dest_fs_name"];
        Value v_ss = params["snapshot"];  /* This is optional */

        if( Value::object_t == v_src_fs.valueType() &&
            Value::string_t == v_name.valueType() &&
            (Value::null_t == v_ss.valueType() ||
            Value::object_t == v_ss.valueType()) &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_fs *clonedFs = NULL;
            char *job = NULL;
            lsm_fs *fs = value_to_fs(v_src_fs);
            const char* name = v_name.asC_str();
            lsm_fs_ss *ss = value_to_ss(v_ss);

            if( fs &&
                (( ss && v_ss.valueType() == Value::object_t) ||
                    (!ss && v_ss.valueType() == Value::null_t) )) {

                rc = p->fs_ops->fs_clone(p, fs, name, &clonedFs, ss, &job,
                                            LSM_FLAG_GET_VALUE(params));

                std::vector<Value> r;
                if( LSM_ERR_OK == rc ) {
                    r.push_back(Value());
                    r.push_back(fs_to_value(clonedFs));
                    response = Value(r);
                    lsm_fs_record_free(clonedFs);
                } else if (LSM_ERR_JOB_STARTED == rc ) {
                    r.push_back(Value(job));
                    r.push_back(Value());
                    response = Value(r);
                    free(job);
                }

            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsm_fs_record_free(fs);
            lsm_fs_ss_record_free(ss);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
   return rc;
}

static int file_clone(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_OK;

    if( p && p->san_ops && p->fs_ops->fs_file_clone ) {

        Value v_fs = params["fs"];
        Value v_src_name = params["src_file_name"];
        Value v_dest_name = params["dest_file_name"];
        Value v_ss = params["snapshot"];    /* This is optional */

        if( Value::object_t == v_fs.valueType() &&
            Value::string_t == v_src_name.valueType() &&
            (Value::string_t == v_dest_name.valueType() ||
            Value::object_t == v_ss.valueType()) &&
            LSM_FLAG_EXPECTED_TYPE(params)) {


            lsm_fs *fs = value_to_fs(v_fs);
            lsm_fs_ss *ss = value_to_ss(v_ss);

            if( fs &&
                (( ss && v_ss.valueType() == Value::object_t) ||
                    (!ss && v_ss.valueType() == Value::null_t) )) {

                const char *src = v_src_name.asC_str();
                const char *dest = v_dest_name.asC_str();

                char *job = NULL;

                rc = p->fs_ops->fs_file_clone(p, fs, src, dest, ss, &job,
                                                LSM_FLAG_GET_VALUE(params));

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }

            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsm_fs_record_free(fs);
            lsm_fs_ss_record_free(ss);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int fs_child_dependency(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->san_ops && p->fs_ops->fs_child_dependency ) {

        Value v_fs = params["fs"];
        Value v_files = params["files"];

        if( Value::object_t == v_fs.valueType() &&
            Value::array_t == v_files.valueType() ) {

            lsm_fs *fs = value_to_fs(v_fs);
            lsm_string_list *files = value_to_string_list(v_files);

            if( fs && files ) {
                uint8_t yes = 0;

                rc = p->fs_ops->fs_child_dependency(p, fs, files, &yes);

                if( LSM_ERR_OK == rc ) {
                    response = Value((bool)yes);
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsm_fs_record_free(fs);
            lsm_string_list_free(files);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int fs_child_dependency_rm(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->san_ops && p->fs_ops->fs_child_dependency_rm ) {

        Value v_fs = params["fs"];
        Value v_files = params["files"];

        if( Value::object_t == v_fs.valueType() &&
            Value::array_t == v_files.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {

            lsm_fs *fs = value_to_fs(v_fs);
            lsm_string_list *files = value_to_string_list(v_files);

            if( fs && files ) {
                char *job = NULL;

                rc = p->fs_ops->fs_child_dependency_rm(p, fs, files, &job,
                                            LSM_FLAG_GET_VALUE(params));

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsm_fs_record_free(fs);
            lsm_string_list_free(files);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ss_list(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->san_ops && p->fs_ops->fs_ss_list ) {

        Value v_fs = params["fs"];

        if( Value::object_t == v_fs.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_fs *fs = value_to_fs(v_fs);

            if( fs ) {
                lsm_fs_ss **ss = NULL;
                uint32_t count = 0;

                rc = p->fs_ops->fs_ss_list(p, fs, &ss, &count,
                                            LSM_FLAG_GET_VALUE(params));

                if( LSM_ERR_OK == rc ) {
                    std::vector<Value> result;

                    for( uint32_t i = 0; i < count; ++i ) {
                        result.push_back(ss_to_value(ss[i]));
                    }
                    response = Value(result);

                    lsm_fs_record_free(fs);
                    fs = NULL;
                    lsm_fs_ss_record_array_free(ss, count);
                    ss = NULL;
                }
            }

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ss_create(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->san_ops && p->fs_ops->fs_ss_create ) {

        Value v_fs = params["fs"];
        Value v_ss_name = params["snapshot_name"];
        Value v_files = params["files"];

        if( Value::object_t == v_fs.valueType() &&
            Value::string_t == v_ss_name.valueType() &&
            Value::array_t == v_files.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {

            lsm_fs *fs = value_to_fs(v_fs);
            lsm_string_list *files = value_to_string_list(v_files);

            if( fs && files ) {
                lsm_fs_ss *ss = NULL;
                char *job = NULL;

                const char *name = v_ss_name.asC_str();

                rc = p->fs_ops->fs_ss_create(p, fs, name, files, &ss, &job,
                                            LSM_FLAG_GET_VALUE(params));

                std::vector<Value> r;
                if( LSM_ERR_OK == rc ) {
                    r.push_back(Value());
                    r.push_back(ss_to_value(ss));
                    response = Value(r);
                    lsm_fs_ss_record_free(ss);
                } else if (LSM_ERR_JOB_STARTED == rc ) {
                    r.push_back(Value(job));
                    r.push_back(Value());
                    response = Value(r);
                    free(job);
                }

            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsm_fs_record_free(fs);
            lsm_string_list_free(files);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ss_delete(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->san_ops && p->fs_ops->fs_ss_delete ) {

        Value v_fs = params["fs"];
        Value v_ss = params["snapshot"];

        if( Value::object_t == v_fs.valueType() &&
            Value::object_t == v_ss.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_fs *fs = value_to_fs(v_fs);
            lsm_fs_ss *ss = value_to_ss(v_ss);

            if( fs && ss ) {
                char *job = NULL;
                rc = p->fs_ops->fs_ss_delete(p, fs, ss, &job,
                                            LSM_FLAG_GET_VALUE(params));

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsm_fs_record_free(fs);
            lsm_fs_ss_record_free(ss);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ss_revert(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->san_ops && p->fs_ops->fs_ss_revert ) {

        Value v_fs = params["fs"];
        Value v_ss = params["snapshot"];
        Value v_files = params["files"];
        Value v_restore_files = params["restore_files"];
        Value v_all_files = params["all_files"];

        if( Value::object_t == v_fs.valueType() &&
            Value::object_t == v_ss.valueType() &&
            Value::array_t == v_files.valueType() &&
            Value::array_t == v_files.valueType() &&
            Value::boolean_t == v_all_files.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {

            char *job = NULL;
            lsm_fs *fs = value_to_fs(v_fs);
            lsm_fs_ss *ss = value_to_ss(v_ss);
            lsm_string_list *files = value_to_string_list(v_files);
            lsm_string_list *restore_files =
                    value_to_string_list(v_restore_files);
            int all_files = (v_all_files.asBool()) ? 1 : 0;

            if( fs && ss && files && restore_files ) {
                rc = p->fs_ops->fs_ss_revert(p, fs, ss, files, restore_files,
                                            all_files, &job,
                                            LSM_FLAG_GET_VALUE(params));

                if( LSM_ERR_JOB_STARTED == rc ) {
                    response = Value(job);
                    free(job);
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsm_fs_record_free(fs);
            lsm_fs_ss_record_free(ss);
            lsm_string_list_free(files);
            lsm_string_list_free(restore_files);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int export_auth(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->nas_ops && p->nas_ops->nfs_auth_types ) {
        lsm_string_list *types = NULL;

        if( LSM_FLAG_EXPECTED_TYPE(params) ) {

            rc = p->nas_ops->nfs_auth_types(p, &types,
                                                LSM_FLAG_GET_VALUE(params));
            if( LSM_ERR_OK == rc ) {
                response = string_list_to_value(types);
                lsm_string_list_free(types);
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }

    }
    return rc;
}

static int exports(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->nas_ops && p->nas_ops->nfs_list ) {
        lsm_nfs_export **exports = NULL;
        uint32_t count = 0;

        if( LSM_FLAG_EXPECTED_TYPE(params) ) {
            rc = p->nas_ops->nfs_list(p, &exports, &count,
                                        LSM_FLAG_GET_VALUE(params));

            if( LSM_ERR_OK == rc ) {
                std::vector<Value> result;

                for( uint32_t i = 0; i < count; ++i ) {
                    result.push_back(nfs_export_to_value(exports[i]));
                }
                response = Value(result);

                lsm_nfs_export_record_array_free(exports, count);
                exports = NULL;
                count = 0;
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }

    return rc;
}

static int64_t get_uid_gid(Value &id)
{
    if( Value::null_t == id.valueType() ) {
        return ANON_UID_GID_NA;
    } else {
        return id.asInt64_t();
    }
}

static int export_fs(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->nas_ops && p->nas_ops->nfs_export ) {

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
            (Value::string_t == v_export_path.valueType() ||
            Value::null_t == v_export_path.valueType()) &&
            Value::array_t == v_root_list.valueType() &&
            Value::array_t == v_rw_list.valueType() &&
            Value::array_t == v_ro_list.valueType() &&
            (Value::string_t == v_auth_type.valueType() ||
            Value::null_t == v_auth_type.valueType()) &&
            (Value::string_t == v_options.valueType() ||
            Value::null_t == v_options.valueType())  &&
            Value::numeric_t == v_anon_uid.valueType() &&
            Value::numeric_t == v_anon_gid.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {

            lsm_string_list *root_list = value_to_string_list(v_root_list);
            lsm_string_list *rw_list = value_to_string_list(v_rw_list);
            lsm_string_list *ro_list = value_to_string_list(v_ro_list);

            if( root_list && rw_list && ro_list ) {
                const char *fs_id = v_fs_id.asC_str();
                const char *export_path = v_export_path.asC_str();
                const char *auth_type = v_auth_type.asC_str();
                const char *options = v_options.asC_str();
                lsm_nfs_export *exported = NULL;

                int64_t anon_uid = get_uid_gid(v_anon_uid);
                int64_t anon_gid = get_uid_gid(v_anon_gid);

                rc = p->nas_ops->nfs_export(p, fs_id, export_path, root_list,
                                            rw_list, ro_list, anon_uid,
                                            anon_gid, auth_type, options,
                                            &exported,
                                            LSM_FLAG_GET_VALUE(params));
                if( LSM_ERR_OK == rc ) {
                    response = nfs_export_to_value(exported);
                    lsm_nfs_export_record_free(exported);
                }

            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsm_string_list_free(root_list);
            lsm_string_list_free(rw_list);
            lsm_string_list_free(ro_list);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int export_remove(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->nas_ops && p->nas_ops->nfs_export_remove ) {
        Value v_export = params["export"];

        if( Value::object_t == v_export.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {
            lsm_nfs_export *exp = value_to_nfs_export(v_export);

            if( exp ) {
                rc = p->nas_ops->nfs_export_remove(p, exp,
                                                LSM_FLAG_GET_VALUE(params));
                lsm_nfs_export_record_free(exp);
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

static int initiator_grant(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->initiator_grant ) {
        Value v_init_id = params["initiator_id"];
        Value v_init_type = params["initiator_type"];
        Value v_vol = params["volume"];
        Value v_access = params["access"];

        if( Value::string_t == v_init_id.valueType() &&
            Value::numeric_t == v_init_type.valueType() &&
            Value::object_t == v_vol.valueType() &&
            Value::numeric_t == v_access.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {

            const char *init_id = v_init_id.asC_str();
            lsm_initiator_type i_type = (lsm_initiator_type)v_init_type.asInt32_t();
            lsm_volume *vol = value_to_volume(v_vol);
            lsm_access_type access = (lsm_access_type)v_access.asInt32_t();
            lsm_flag flags = LSM_FLAG_GET_VALUE(params);

            if( vol ) {
                rc = p->san_ops->initiator_grant(p, init_id, i_type, vol, access,
                                                flags);
                lsm_volume_record_free(vol);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int init_granted_to_volume(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->initiators_granted_to_vol ) {
        Value v_vol = params["volume"];

        if( Value::object_t == v_vol.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {
            lsm_initiator **inits = NULL;
            uint32_t count = 0;

            lsm_volume *vol = value_to_volume(v_vol);
            lsm_flag flags = LSM_FLAG_GET_VALUE(params);

            if( vol ) {
                rc = p->san_ops->initiators_granted_to_vol(p, vol, &inits,
                                                            &count, flags);
                get_initiators(rc, inits, count, response);
                lsm_volume_record_free(vol);
                vol = NULL;
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int initiator_revoke(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;
    if( p && p->san_ops && p->san_ops->initiator_revoke ) {
        Value v_init = params["initiator"];
        Value v_vol = params["volume"];

        if( Value::object_t == v_init.valueType() &&
            Value::object_t == v_vol.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {

            lsm_initiator *init = value_to_initiator(v_init);
            lsm_volume *vol = value_to_volume(v_vol);
            lsm_flag flags = LSM_FLAG_GET_VALUE(params);

            if( init && vol ) {
                rc = p->san_ops->initiator_revoke(p, init, vol, flags);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsm_initiator_record_free(init);
            lsm_volume_record_free(vol);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int iscsi_chap(lsm_plugin_ptr p, Value &params, Value &response)
{
    int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->iscsi_chap_auth ) {
        Value v_init = params["initiator"];
        Value v_in_user = params["in_user"];
        Value v_in_password = params["in_password"];
        Value v_out_user = params["out_user"];
        Value v_out_password = params["out_password"];

        if( Value::object_t == v_init.valueType() &&
            (Value::string_t == v_in_user.valueType() ||
            Value::null_t == v_in_user.valueType()) &&
            (Value::string_t == v_in_password.valueType() ||
            Value::null_t == v_in_password.valueType()) &&
            (Value::string_t == v_out_user.valueType() ||
            Value::null_t == v_out_user.valueType()) &&
            (Value::string_t == v_out_password.valueType() ||
            Value::null_t == v_out_password.valueType()) &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {

            lsm_initiator *init = value_to_initiator(v_init);
            if( init ) {
                rc = p->san_ops->iscsi_chap_auth(p, init,
                                                    v_in_user.asC_str(),
                                                    v_in_password.asC_str(),
                                                    v_out_user.asC_str(),
                                                    v_out_password.asC_str(),
                                                    LSM_FLAG_GET_VALUE(params));
                lsm_initiator_record_free(init);
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int vol_accessible_by_init(lsm_plugin_ptr p, Value &params, Value &response)
{
   int rc = LSM_ERR_NO_SUPPORT;

    if( p && p->san_ops && p->san_ops->vol_accessible_by_init ) {
        Value v_init = params["initiator"];

        if( Value::object_t == v_init.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params) ) {
            lsm_volume **vols = NULL;
            uint32_t count = 0;

            lsm_initiator *init = value_to_initiator(v_init);
            lsm_flag flags = LSM_FLAG_GET_VALUE(params);

            if( init ) {
                rc = p->san_ops->vol_accessible_by_init(p, init, &vols, &count,
                                                        flags);
                get_volumes(rc, vols, count, response);
                lsm_initiator_record_free(init);
                init = NULL;
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
    ("access_group_add_initiator", ag_initiator_add)
    ("access_group_create", ag_create)
    ("access_group_delete", ag_delete)
    ("access_group_del_initiator", ag_initiator_del)
    ("access_group_grant", ag_grant)
    ("access_group_list", ag_list)
    ("access_group_revoke", ag_revoke)
    ("access_groups_granted_to_volume", ag_granted_to_volume)
    ("capabilities", capabilities)
    ("disks", handle_disks)
    ("export_auth", export_auth)
    ("export_fs", export_fs)
    ("export_remove", export_remove)
    ("exports", exports)
    ("file_clone", file_clone)
    ("fs_child_dependency", fs_child_dependency)
    ("fs_child_dependency_rm", fs_child_dependency_rm)
    ("fs_clone", fs_clone)
    ("fs_create", fs_create)
    ("fs_delete", fs_delete)
    ("fs", fs)
    ("fs_resize", fs_resize)
    ("fs_snapshot_create", ss_create)
    ("fs_snapshot_delete", ss_delete)
    ("fs_snapshot_revert", ss_revert)
    ("fs_snapshots", ss_list)
    ("time_out_get", handle_get_time_out)
    ("initiators", handle_initiators)
    ("initiator_grant", initiator_grant)
    ("initiators_granted_to_volume", init_granted_to_volume)
    ("initiator_revoke", initiator_revoke)
    ("iscsi_chap_auth", iscsi_chap)
    ("job_free", handle_job_free)
    ("job_status", handle_job_status)
    ("plugin_info", handle_plugin_info)
    ("pools", handle_pools)
    ("pool_create", handle_pool_create)
    ("pool_create_from_disks", handle_pool_create_from_disks)
    ("pool_create_from_volumes", handle_pool_create_from_volumes)
    ("pool_create_from_pool", handle_pool_create_from_pool)
    ("pool_delete", handle_pool_delete)
    ("time_out_set", handle_set_time_out)
    ("plugin_unregister", handle_unregister)
    ("plugin_register", handle_register)
    ("systems", handle_system_list)
    ("volume_child_dependency_rm", volume_dependency_rm)
    ("volume_child_dependency", volume_dependency)
    ("volume_create", handle_volume_create)
    ("volume_delete", handle_volume_delete)
    ("volume_offline", handle_volume_offline)
    ("volume_online", handle_volume_online)
    ("volume_replicate", handle_volume_replicate)
    ("volume_replicate_range_block_size", handle_volume_replicate_range_block_size)
    ("volume_replicate_range", handle_volume_replicate_range)
    ("volume_resize", handle_volume_resize)
    ("volumes_accessible_by_access_group", vol_accessible_by_ag)
    ("volumes_accessible_by_initiator", vol_accessible_by_init)
    ("volumes", handle_volumes);

static int process_request(lsm_plugin_ptr p, const std::string &method, Value &request,
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

static int lsm_plugin_run(lsm_plugin_ptr p)
{
    int rc = 0;
    lsm_flag flags = 0;

    if( LSM_IS_PLUGIN(p) ) {
        while(true) {
            try {

                if( !LSM_IS_PLUGIN(p) ) {
                    syslog(LOG_USER|LOG_NOTICE, "Someone stepped on "
                                                "plugin pointer, exiting!");
                    break;
                }

                Value req = p->tp->readRequest();
                Value resp;

                if( req.isValidRequest() ) {
                    std::string method = req["method"].asString();
                    rc = process_request(p, method, req, resp);

                    if( LSM_ERR_OK == rc || LSM_ERR_JOB_STARTED == rc ) {
                        p->tp->responseSend(resp);
                    } else {
                        error_send(p, rc);
                    }

                    if( method == "shutdown" ) {
                        flags = LSM_FLAG_GET_VALUE(req["params"]);
                        break;
                    }
                } else {
                    syslog(LOG_USER|LOG_NOTICE, "Invalid request");
                    break;
                }
            } catch (EOFException &eof) {
                break;
            } catch (ValueException &ve) {
                syslog(LOG_USER|LOG_NOTICE, "Plug-in exception: %s", ve.what());
                rc = 1;
                break;
            } catch (LsmException &le) {
                syslog(LOG_USER|LOG_NOTICE, "Plug-in exception: %s", le.what());
                rc = 2;
                break;
            } catch ( ... ) {
                syslog(LOG_USER|LOG_NOTICE, "Plug-in un-handled exception");
                rc = 3;
                break;
            }
        }
        lsm_plugin_free(p, flags);
        p = NULL;
    } else {
        rc = LSM_ERR_INVALID_PLUGIN;
    }

    return rc;
}

int lsm_log_error_basic( lsm_plugin_ptr plug, lsm_error_number code, const char* msg )
{
    if( !LSM_IS_PLUGIN(plug) ) {
        return LSM_ERR_INVALID_PLUGIN;
    }

    lsm_error_ptr e = LSM_ERROR_CREATE_PLUGIN_MSG(code, msg);
    if( e ) {
        int rc = lsm_plugin_error_log(plug, e);

        if( LSM_ERR_OK != rc ) {
            syslog(LOG_USER|LOG_NOTICE,
                    "Plug-in error %d while reporting an error, code= %d, msg= %s",
                    rc, code, msg);
        }
    }
    return (int)code;
}

int lsm_plugin_error_log( lsm_plugin_ptr plug, lsm_error_ptr error)
{
    if( !LSM_IS_PLUGIN(plug) ) {
        return LSM_ERR_INVALID_PLUGIN;
    }

    if(!LSM_IS_ERROR(error) ) {
        return LSM_ERR_INVALID_ERR;
    }

    if( plug->error ) {
        lsm_error_free(plug->error);
    }

    plug->error = error;

    return LSM_ERR_OK;
}


#define STR_D(c, s) \
do { \
    if(s) { \
        (c) = strdup(s); \
        if( !c ) {\
            rc = LSM_ERR_NO_MEMORY; \
            goto bail; \
        } \
    } \
} while(0)\

int LSM_DLL_EXPORT lsm_uri_parse(const char *uri, char **scheme, char **user,
                                char **server, int *port, char **path,
                                lsm_optional_data **query_params)
{
    int rc = LSM_ERR_INVALID_URI;
    xmlURIPtr u = NULL;

    if( uri && strlen(uri) > 0 ) {
        *scheme = NULL;
        *user = NULL;
        *server = NULL;
        *port = -1;
        *path = NULL;
        *query_params = NULL;

        u = xmlParseURI(uri);
        if( u ) {
            STR_D(*scheme, u->scheme);
            STR_D(*user, u->user);
            STR_D(*server, u->server);
            STR_D(*path, u->path);
            *port = u->port;

            *query_params = lsm_optional_data_record_alloc();
            if( *query_params ) {
                int i;
                struct qparam_set *qp = NULL;
                qp = qparam_query_parse(u->query_raw);

                for( i = 0; i < qp->n; ++i ) {
                    rc = lsm_optional_data_string_set(*query_params,
                                                        qp->p[i].name,
                                                        qp->p[i].value);
                    if( LSM_ERR_OK != rc ) {
                        goto bail;
                    }
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
                goto bail;
            }

            rc = LSM_ERR_OK;
        }

    bail:
        if( rc != LSM_ERR_OK ){
            free(*scheme);
            *scheme = NULL;
            free(*user);
            *user = NULL;
            free(*server);
            *server = NULL;
            *port = -1;
            free(*path);
            *path = NULL;
            lsm_optional_data_record_free(*query_params);
            *query_params = NULL;
        }

        if( u ) {
            xmlFreeURI(u);
            u = NULL;
        }
    }
    return rc;
}