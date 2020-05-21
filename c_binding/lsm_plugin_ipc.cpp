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
 * License along with this library; If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: tasleson
 */

#include "lsm_plugin_ipc.hpp"
#include "libstoragemgmt/libstoragemgmt_accessgroups.h"
#include "libstoragemgmt/libstoragemgmt_battery.h"
#include "libstoragemgmt/libstoragemgmt_blockrange.h"
#include "libstoragemgmt/libstoragemgmt_disk.h"
#include "libstoragemgmt/libstoragemgmt_fs.h"
#include "libstoragemgmt/libstoragemgmt_nfsexport.h"
#include "libstoragemgmt/libstoragemgmt_plug_interface.h"
#include "libstoragemgmt/libstoragemgmt_pool.h"
#include "libstoragemgmt/libstoragemgmt_snapshot.h"
#include "libstoragemgmt/libstoragemgmt_systems.h"
#include "libstoragemgmt/libstoragemgmt_targetport.h"
#include "libstoragemgmt/libstoragemgmt_volumes.h"
#include "lsm_convert.hpp"
#include "lsm_datatypes.hpp"
#include "lsm_ipc.hpp"
#include "util/qparams.h"
#include <errno.h>
#include <libxml/uri.h>
#include <string.h>
#include <syslog.h>

#define UNUSED(x) (void)(x)

// Forward decl.
static int lsm_plugin_run(lsm_plugin_ptr plug);
static void get_batteries(int rc, lsm_battery *bs[], uint32_t count,
                          Value &response);
static int handle_batteries(lsm_plugin_ptr p, Value &params, Value &response);
static int handle_volume_cache_info(lsm_plugin_ptr p, Value &params,
                                    Value &response);
static int handle_volume_pdc_update(lsm_plugin_ptr p, Value &params,
                                    Value &response);
static int handle_volume_wcp_update(lsm_plugin_ptr p, Value &params,
                                    Value &response);
static int handle_volume_rcp_update(lsm_plugin_ptr p, Value &params,
                                    Value &response);

/**
 * Safe string wrapper
 * @param s Character array to convert to std::string
 * @return String representation.
 */
static std::string ss(char *s) {
    if (s) {
        return std::string(s);
    }
    return std::string();
}

void *lsm_data_type_copy(lsm_data_type t, void *item) {
    void *rc = NULL;

    if (item) {
        switch (t) {
        case (LSM_DATA_TYPE_ACCESS_GROUP):
            rc = lsm_access_group_record_copy((lsm_access_group *)item);
            break;
        case (LSM_DATA_TYPE_BLOCK_RANGE):
            rc = lsm_block_range_record_copy((lsm_block_range *)item);
            break;
        case (LSM_DATA_TYPE_FS):
            rc = lsm_fs_record_copy((lsm_fs *)item);
            break;
        case (LSM_DATA_TYPE_NFS_EXPORT):
            rc = lsm_nfs_export_record_copy((lsm_nfs_export *)item);
            break;
        case (LSM_DATA_TYPE_POOL):
            rc = lsm_pool_record_copy((lsm_pool *)item);
            break;
        case (LSM_DATA_TYPE_SS):
            rc = lsm_fs_ss_record_copy((lsm_fs_ss *)item);
            break;
        case (LSM_DATA_TYPE_STRING_LIST):
            rc = lsm_string_list_copy((lsm_string_list *)item);
            break;
        case (LSM_DATA_TYPE_SYSTEM):
            rc = lsm_system_record_copy((lsm_system *)item);
            break;
        case (LSM_DATA_TYPE_VOLUME):
            rc = lsm_volume_record_copy((lsm_volume *)item);
            break;
        case (LSM_DATA_TYPE_DISK):
            rc = lsm_disk_record_copy((lsm_disk *)item);
            break;
        default:
            break;
        }
    }
    return rc;
}

static Value job_handle(const Value &val, char *job) {
    std::vector<Value> r;
    r.push_back(Value(job));
    r.push_back(val);
    return Value(r);
}

int lsm_register_plugin_v1(lsm_plugin_ptr plug, void *private_data,
                           struct lsm_mgmt_ops_v1 *mgm_op,
                           struct lsm_san_ops_v1 *san_op,
                           struct lsm_fs_ops_v1 *fs_op,
                           struct lsm_nas_ops_v1 *nas_op) {
    int rc = LSM_ERR_INVALID_ARGUMENT;

    if (LSM_IS_PLUGIN(plug)) {
        plug->private_data = private_data;
        plug->mgmt_ops = mgm_op;
        plug->san_ops = san_op;
        plug->fs_ops = fs_op;
        plug->nas_ops = nas_op;
        rc = LSM_ERR_OK;
    }
    return rc;
}

int lsm_register_plugin_v1_2(lsm_plugin_ptr plug, void *private_data,
                             struct lsm_mgmt_ops_v1 *mgm_op,
                             struct lsm_san_ops_v1 *san_op,
                             struct lsm_fs_ops_v1 *fs_op,
                             struct lsm_nas_ops_v1 *nas_op,
                             struct lsm_ops_v1_2 *ops_v1_2) {
    int rc = lsm_register_plugin_v1(plug, private_data, mgm_op, san_op, fs_op,
                                    nas_op);

    if (rc != LSM_ERR_OK) {
        return rc;
    }
    plug->ops_v1_2 = ops_v1_2;
    return rc;
}

int lsm_register_plugin_v1_3(lsm_plugin_ptr plug, void *private_data,
                             struct lsm_mgmt_ops_v1 *mgm_op,
                             struct lsm_san_ops_v1 *san_op,
                             struct lsm_fs_ops_v1 *fs_op,
                             struct lsm_nas_ops_v1 *nas_op,
                             struct lsm_ops_v1_2 *ops_v1_2,
                             struct lsm_ops_v1_3 *ops_v1_3) {
    int rc = lsm_register_plugin_v1_2(plug, private_data, mgm_op, san_op, fs_op,
                                      nas_op, ops_v1_2);

    if (rc != LSM_ERR_OK) {
        return rc;
    }
    plug->ops_v1_3 = ops_v1_3;
    return rc;
}

void *lsm_private_data_get(lsm_plugin_ptr plug) {
    if (!LSM_IS_PLUGIN(plug)) {
        return NULL;
    }

    return plug->private_data;
}

static void lsm_plugin_free(lsm_plugin_ptr p, lsm_flag flags) {
    if (LSM_IS_PLUGIN(p)) {

        delete (p->tp);
        p->tp = NULL;

        if (p->unreg) {
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
                                       const char *desc, const char *version) {

    if (!reg || !unreg) {
        return NULL;
    }

    lsm_plugin_ptr rc = (lsm_plugin_ptr)calloc(1, sizeof(lsm_plugin));
    if (rc) {
        rc->magic = LSM_PLUGIN_MAGIC;
        rc->reg = reg;
        rc->unreg = unreg;
        rc->desc = strdup(desc);
        rc->version = strdup(version);

        if (!rc->desc || !rc->version) {
            lsm_plugin_free(rc, LSM_CLIENT_FLAG_RSVD);
            rc = NULL;
        }
    }
    return rc;
}

static void error_send(lsm_plugin_ptr p, int error_code) {
    if (!LSM_IS_PLUGIN(p)) {
        return;
    }

    if (p->error) {
        if (p->tp) {
            p->tp->errorSend(p->error->code, ss(p->error->message),
                             ss(p->error->debug));
            lsm_error_free(p->error);
            p->error = NULL;
        }
    } else {
        p->tp->errorSend(error_code, "Plugin didn't provide error message", "");
    }
}

static int get_search_params(Value &params, char **k, char **v) {
    int rc = LSM_ERR_OK;
    Value key = params["search_key"];
    Value val = params["search_value"];

    if (Value::string_t == key.valueType()) {
        if (Value::string_t == val.valueType()) {
            *k = strdup(key.asC_str());
            *v = strdup(val.asC_str());

            if (*k == NULL || *v == NULL) {
                free(*k);
                *k = NULL;
                free(*v);
                *v = NULL;
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    } else if (Value::null_t != key.valueType()) {
        rc = LSM_ERR_TRANSPORT_INVALID_ARG;
    }

    return rc;
}

/**
 * Checks to see if a character string is an integer and returns result
 * @param[in] sn    Character array holding the integer
 * @param[out] num  The numeric value contained in string
 * @return true if sn is an integer, else false
 */
static bool get_num(char *sn, int &num) {
    errno = 0;

    num = strtol(sn, NULL, 10);
    if (!errno) {
        return true;
    }
    return false;
}

int lsm_plugin_init_v1(int argc, char *argv[], lsm_plugin_register reg,
                       lsm_plugin_unregister unreg, const char *desc,
                       const char *version) {
    int rc = 1;
    lsm_plugin_ptr plug = NULL;

    if (NULL == desc || NULL == version) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    int sd = 0;
    if (argc == 2 && get_num(argv[1], sd)) {
        plug = lsm_plugin_alloc(reg, unreg, desc, version);
        if (plug) {
            plug->tp = new Ipc(sd);
            if (plug->tp) {
                rc = lsm_plugin_run(plug);
            } else {
                lsm_plugin_free(plug, LSM_CLIENT_FLAG_RSVD);
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            rc = LSM_ERR_NO_MEMORY;
        }
    } else {
        // Process command line arguments or display help text.
        rc = 2;
    }

    return rc;
}

typedef int (*handler)(lsm_plugin_ptr p, Value &params, Value &response);

static int handle_unregister(lsm_plugin_ptr p, Value &params, Value &response) {
    UNUSED(p);
    UNUSED(params);
    UNUSED(response);
    /* This is handled in the event loop */
    return LSM_ERR_OK;
}

static int handle_register(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    std::string uri_string;
    std::string password;

    UNUSED(response);

    if (p && p->reg) {

        Value uri_v = params["uri"];
        Value passwd_v = params["password"];
        Value tmo_v = params["timeout"];

        if (Value::string_t == uri_v.valueType() &&
            (Value::string_t == passwd_v.valueType() ||
             Value::null_t == passwd_v.valueType()) &&
            Value::numeric_t == tmo_v.valueType()) {
            lsm_flag flags = LSM_FLAG_GET_VALUE(params);

            uri_string = uri_v.asString();

            if (Value::string_t == params["password"].valueType()) {
                password = params["password"].asString();
            }
            // Let the plug-in initialize itself.
            rc = p->reg(p, uri_string.c_str(), password.c_str(),
                        tmo_v.asUint32_t(), flags);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    } else {
        rc = LSM_ERR_NO_SUPPORT;
    }
    return rc;
}

static int handle_set_time_out(lsm_plugin_ptr p, Value &params,
                               Value &response) {
    UNUSED(response);
    if (p && p->mgmt_ops && p->mgmt_ops->tmo_set) {
        if (Value::numeric_t == params["ms"].valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {
            return p->mgmt_ops->tmo_set(p, params["ms"].asUint32_t(),
                                        LSM_FLAG_GET_VALUE(params));
        } else {
            return LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return LSM_ERR_NO_SUPPORT;
}

static int handle_get_time_out(lsm_plugin_ptr p, Value &params,
                               Value &response) {
    uint32_t tmo = 0;
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->mgmt_ops && p->mgmt_ops->tmo_get) {

        if (LSM_FLAG_EXPECTED_TYPE(params)) {
            rc = p->mgmt_ops->tmo_get(p, &tmo, LSM_FLAG_GET_VALUE(params));
            if (LSM_ERR_OK == rc) {
                response = Value(tmo);
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_job_status(lsm_plugin_ptr p, Value &params, Value &response) {
    std::string job_id;
    lsm_job_status status;
    uint8_t percent;
    lsm_data_type t = LSM_DATA_TYPE_UNKNOWN;
    void *value = NULL;
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->mgmt_ops && p->mgmt_ops->job_status) {

        if (Value::string_t != params["job_id"].valueType() &&
            !LSM_FLAG_EXPECTED_TYPE(params)) {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        } else {

            job_id = params["job_id"].asString();

            rc =
                p->mgmt_ops->job_status(p, job_id.c_str(), &status, &percent,
                                        &t, &value, LSM_FLAG_GET_VALUE(params));

            if (LSM_ERR_OK == rc) {
                std::vector<Value> result;

                result.push_back(Value((int32_t)status));
                result.push_back(Value(percent));

                if (NULL == value) {
                    result.push_back(Value());
                } else {
                    if (LSM_DATA_TYPE_VOLUME == t &&
                        LSM_IS_VOL((lsm_volume *)value)) {
                        result.push_back(volume_to_value((lsm_volume *)value));
                        lsm_volume_record_free((lsm_volume *)value);
                    } else if (LSM_DATA_TYPE_FS == t &&
                               LSM_IS_FS((lsm_fs *)value)) {
                        result.push_back(fs_to_value((lsm_fs *)value));
                        lsm_fs_record_free((lsm_fs *)value);
                    } else if (LSM_DATA_TYPE_SS == t &&
                               LSM_IS_SS((lsm_fs_ss *)value)) {
                        result.push_back(ss_to_value((lsm_fs_ss *)value));
                        lsm_fs_ss_record_free((lsm_fs_ss *)value);
                    } else if (LSM_DATA_TYPE_POOL == t &&
                               LSM_IS_POOL((lsm_pool *)value)) {
                        result.push_back(pool_to_value((lsm_pool *)value));
                        lsm_pool_record_free((lsm_pool *)value);
                    } else {
                        rc = LSM_ERR_PLUGIN_BUG;
                    }
                }
                response = Value(result);
            }
        }
    }
    return rc;
}

static int handle_plugin_info(lsm_plugin_ptr p, Value &params,
                              Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    UNUSED(params);

    if (p) {
        std::vector<Value> result;
        result.push_back(Value(p->desc));
        result.push_back(Value(p->version));
        response = Value(result);
        rc = LSM_ERR_OK;
    }
    return rc;
}

static int handle_job_free(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    UNUSED(response);
    if (p && p->mgmt_ops && p->mgmt_ops->job_free) {
        if (Value::string_t == params["job_id"].valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {
            std::string job_num = params["job_id"].asString();
            char *j = (char *)job_num.c_str();
            rc = p->mgmt_ops->job_free(p, j, LSM_FLAG_GET_VALUE(params));
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_system_list(lsm_plugin_ptr p, Value &params,
                              Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->mgmt_ops && p->mgmt_ops->system_list) {
        lsm_system **systems = NULL;
        uint32_t count = 0;

        if (LSM_FLAG_EXPECTED_TYPE(params)) {

            rc = p->mgmt_ops->system_list(p, &systems, &count,
                                          LSM_FLAG_GET_VALUE(params));
            if (LSM_ERR_OK == rc) {
                std::vector<Value> result;
                result.reserve(count);

                for (uint32_t i = 0; i < count; ++i) {
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

static int handle_pools(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    char *key = NULL;
    char *val = NULL;

    if (p && p->mgmt_ops && p->mgmt_ops->pool_list) {
        lsm_pool **pools = NULL;
        uint32_t count = 0;

        if (LSM_FLAG_EXPECTED_TYPE(params) &&
            ((rc = get_search_params(params, &key, &val)) == LSM_ERR_OK)) {
            rc = p->mgmt_ops->pool_list(p, key, val, &pools, &count,
                                        LSM_FLAG_GET_VALUE(params));
            if (LSM_ERR_OK == rc) {
                std::vector<Value> result;
                result.reserve(count);

                for (uint32_t i = 0; i < count; ++i) {
                    result.push_back(pool_to_value(pools[i]));
                }

                lsm_pool_record_array_free(pools, count);
                pools = NULL;
                response = Value(result);
            }
            free(key);
            free(val);
        } else {
            if (rc == LSM_ERR_NO_SUPPORT) {
                rc = LSM_ERR_TRANSPORT_INVALID_ARG;
            }
        }
    }
    return rc;
}

static int handle_target_ports(lsm_plugin_ptr p, Value &params,
                               Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    char *key = NULL;
    char *val = NULL;

    if (p && p->san_ops && p->san_ops->target_port_list) {
        lsm_target_port **target_ports = NULL;
        uint32_t count = 0;

        if (LSM_FLAG_EXPECTED_TYPE(params) &&
            ((rc = get_search_params(params, &key, &val)) == LSM_ERR_OK)) {
            rc = p->san_ops->target_port_list(
                p, key, val, &target_ports, &count, LSM_FLAG_GET_VALUE(params));
            if (LSM_ERR_OK == rc) {
                std::vector<Value> result;
                result.reserve(count);

                for (uint32_t i = 0; i < count; ++i) {
                    result.push_back(target_port_to_value(target_ports[i]));
                }

                lsm_target_port_record_array_free(target_ports, count);
                target_ports = NULL;
                response = Value(result);
            }
            free(key);
            free(val);
        } else {
            if (rc == LSM_ERR_NO_SUPPORT) {
                rc = LSM_ERR_TRANSPORT_INVALID_ARG;
            }
        }
    }
    return rc;
}

static int capabilities(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->mgmt_ops && p->mgmt_ops->capablities) {
        lsm_storage_capabilities *c = NULL;

        Value v_s = params["system"];

        if (IS_CLASS_SYSTEM(v_s) && LSM_FLAG_EXPECTED_TYPE(params)) {
            lsm_system *sys = value_to_system(v_s);

            if (sys) {
                rc = p->mgmt_ops->capablities(p, sys, &c,
                                              LSM_FLAG_GET_VALUE(params));
                if (LSM_ERR_OK == rc) {
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

static void get_volumes(int rc, lsm_volume **vols, uint32_t count,
                        Value &response) {
    if (LSM_ERR_OK == rc) {
        std::vector<Value> result;
        result.reserve(count);

        for (uint32_t i = 0; i < count; ++i) {
            result.push_back(volume_to_value(vols[i]));
        }

        lsm_volume_record_array_free(vols, count);
        vols = NULL;
        response = Value(result);
    }
}

static int handle_volumes(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    char *key = NULL;
    char *val = NULL;

    if (p && p->san_ops && p->san_ops->vol_get) {
        lsm_volume **vols = NULL;
        uint32_t count = 0;

        if (LSM_FLAG_EXPECTED_TYPE(params) &&
            (rc = get_search_params(params, &key, &val)) == LSM_ERR_OK) {
            rc = p->san_ops->vol_get(p, key, val, &vols, &count,
                                     LSM_FLAG_GET_VALUE(params));

            get_volumes(rc, vols, count, response);
            free(key);
            free(val);
        } else {
            if (rc == LSM_ERR_NO_SUPPORT) {
                rc = LSM_ERR_TRANSPORT_INVALID_ARG;
            }
        }
    }
    return rc;
}

static void get_disks(int rc, lsm_disk **disks, uint32_t count,
                      Value &response) {
    if (LSM_ERR_OK == rc) {
        std::vector<Value> result;
        result.reserve(count);

        for (uint32_t i = 0; i < count; ++i) {
            result.push_back(disk_to_value(disks[i]));
        }

        lsm_disk_record_array_free(disks, count);
        disks = NULL;
        response = Value(result);
    }
}

static int handle_disks(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    char *key = NULL;
    char *val = NULL;

    if (p && p->san_ops && p->san_ops->disk_get) {
        lsm_disk **disks = NULL;
        uint32_t count = 0;

        if (LSM_FLAG_EXPECTED_TYPE(params) &&
            (rc = get_search_params(params, &key, &val)) == LSM_ERR_OK) {
            rc = p->san_ops->disk_get(p, key, val, &disks, &count,
                                      LSM_FLAG_GET_VALUE(params));
            get_disks(rc, disks, count, response);
            free(key);
            free(val);
        } else {
            if (rc == LSM_ERR_NO_SUPPORT) {
                rc = LSM_ERR_TRANSPORT_INVALID_ARG;
            }
        }
    }
    return rc;
}

static int handle_volume_create(lsm_plugin_ptr p, Value &params,
                                Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    if (p && p->san_ops && p->san_ops->vol_create) {

        Value v_p = params["pool"];
        Value v_name = params["volume_name"];
        Value v_size = params["size_bytes"];
        Value v_prov = params["provisioning"];

        if (IS_CLASS_POOL(v_p) && Value::string_t == v_name.valueType() &&
            Value::numeric_t == v_size.valueType() &&
            Value::numeric_t == v_prov.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_pool *pool = value_to_pool(v_p);
            if (pool) {
                lsm_volume *vol = NULL;
                char *job = NULL;
                const char *name = v_name.asC_str();
                uint64_t size = v_size.asUint64_t();
                lsm_volume_provision_type pro =
                    (lsm_volume_provision_type)v_prov.asInt32_t();

                rc = p->san_ops->vol_create(p, pool, name, size, pro, &vol,
                                            &job, LSM_FLAG_GET_VALUE(params));

                Value v = volume_to_value(vol);
                response = job_handle(v, job);

                // Free dynamic data.
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

static int handle_volume_resize(lsm_plugin_ptr p, Value &params,
                                Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    if (p && p->san_ops && p->san_ops->vol_resize) {
        Value v_vol = params["volume"];
        Value v_size = params["new_size_bytes"];

        if (IS_CLASS_VOLUME(v_vol) && Value::numeric_t == v_size.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_volume *vol = value_to_volume(v_vol);
            if (vol) {
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

static int handle_volume_replicate(lsm_plugin_ptr p, Value &params,
                                   Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->san_ops && p->san_ops->vol_replicate) {

        Value v_pool = params["pool"];
        Value v_vol_src = params["volume_src"];
        Value v_rep = params["rep_type"];
        Value v_name = params["name"];

        if (((Value::object_t == v_pool.valueType() && IS_CLASS_POOL(v_pool)) ||
             Value::null_t == v_pool.valueType()) &&
            IS_CLASS_VOLUME(v_vol_src) &&
            Value::numeric_t == v_rep.valueType() &&
            Value::string_t == v_name.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_pool *pool = (Value::null_t == v_pool.valueType())
                                 ? NULL
                                 : value_to_pool(v_pool);
            lsm_volume *vol = value_to_volume(v_vol_src);
            lsm_volume *newVolume = NULL;
            lsm_replication_type rep = (lsm_replication_type)v_rep.asInt32_t();
            const char *name = v_name.asC_str();
            char *job = NULL;

            if (vol &&
                (pool || (!pool && Value::null_t == v_pool.valueType()))) {
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

static int handle_volume_replicate_range_block_size(lsm_plugin_ptr p,
                                                    Value &params,
                                                    Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    uint32_t block_size = 0;

    if (p && p->san_ops && p->san_ops->vol_rep_range_bs) {
        Value v_s = params["system"];

        if (IS_CLASS_SYSTEM(v_s) && LSM_FLAG_EXPECTED_TYPE(params)) {
            lsm_system *sys = value_to_system(v_s);

            if (sys) {
                rc = p->san_ops->vol_rep_range_bs(p, sys, &block_size,
                                                  LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_OK == rc) {
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
                                         Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    uint32_t range_count = 0;
    char *job = NULL;
    if (p && p->san_ops && p->san_ops->vol_rep_range) {
        Value v_rep = params["rep_type"];
        Value v_vol_src = params["volume_src"];
        Value v_vol_dest = params["volume_dest"];
        Value v_ranges = params["ranges"];

        if (Value::numeric_t == v_rep.valueType() &&
            IS_CLASS_VOLUME(v_vol_src) && IS_CLASS_VOLUME(v_vol_dest) &&
            Value::array_t == v_ranges.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_replication_type repType =
                (lsm_replication_type)v_rep.asInt32_t();
            lsm_volume *source = value_to_volume(v_vol_src);
            lsm_volume *dest = value_to_volume(v_vol_dest);
            lsm_block_range **ranges =
                value_to_block_range_list(v_ranges, &range_count);

            if (source && dest && ranges) {

                rc = p->san_ops->vol_rep_range(p, repType, source, dest, ranges,
                                               range_count, &job,
                                               LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_JOB_STARTED == rc) {
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

static int handle_volume_delete(lsm_plugin_ptr p, Value &params,
                                Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    if (p && p->san_ops && p->san_ops->vol_delete) {
        Value v_vol = params["volume"];

        if (IS_CLASS_VOLUME(v_vol) && LSM_FLAG_EXPECTED_TYPE(params)) {
            lsm_volume *vol = value_to_volume(v_vol);

            if (vol) {
                char *job = NULL;

                rc = p->san_ops->vol_delete(p, vol, &job,
                                            LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_JOB_STARTED == rc) {
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

static int handle_vol_enable_disable(lsm_plugin_ptr p, Value &params,
                                     Value &response, int online) {
    int rc = LSM_ERR_NO_SUPPORT;
    UNUSED(response);

    if (p && p->san_ops &&
        ((online) ? p->san_ops->vol_enable : p->san_ops->vol_disable)) {

        Value v_vol = params["volume"];

        if (IS_CLASS_VOLUME(v_vol) && LSM_FLAG_EXPECTED_TYPE(params)) {
            lsm_volume *vol = value_to_volume(v_vol);
            if (vol) {
                if (online) {
                    rc = p->san_ops->vol_enable(p, vol,
                                                LSM_FLAG_GET_VALUE(params));
                } else {
                    rc = p->san_ops->vol_disable(p, vol,
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

static int handle_volume_enable(lsm_plugin_ptr p, Value &params,
                                Value &response) {
    return handle_vol_enable_disable(p, params, response, 1);
}

static int handle_volume_disable(lsm_plugin_ptr p, Value &params,
                                 Value &response) {
    return handle_vol_enable_disable(p, params, response, 0);
}

static int handle_volume_raid_info(lsm_plugin_ptr p, Value &params,
                                   Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    if (p && p->ops_v1_2 && p->ops_v1_2->vol_raid_info) {
        Value v_vol = params["volume"];

        if (IS_CLASS_VOLUME(v_vol) && LSM_FLAG_EXPECTED_TYPE(params)) {
            lsm_volume *vol = value_to_volume(v_vol);
            std::vector<Value> result;

            if (vol) {
                lsm_volume_raid_type raid_type;
                uint32_t strip_size;
                uint32_t disk_count;
                uint32_t min_io_size;
                uint32_t opt_io_size;

                rc = p->ops_v1_2->vol_raid_info(
                    p, vol, &raid_type, &strip_size, &disk_count, &min_io_size,
                    &opt_io_size, LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_OK == rc) {
                    result.push_back(Value((int32_t)raid_type));
                    result.push_back(Value(strip_size));
                    result.push_back(Value(disk_count));
                    result.push_back(Value(min_io_size));
                    result.push_back(Value(opt_io_size));
                    response = Value(result);
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

static int handle_pool_member_info(lsm_plugin_ptr p, Value &params,
                                   Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    if (p && p->ops_v1_2 && p->ops_v1_2->pool_member_info) {
        Value v_pool = params["pool"];

        if (IS_CLASS_POOL(v_pool) && LSM_FLAG_EXPECTED_TYPE(params)) {
            lsm_pool *pool = value_to_pool(v_pool);
            std::vector<Value> result;

            if (pool) {
                lsm_volume_raid_type raid_type = LSM_VOLUME_RAID_TYPE_UNKNOWN;
                lsm_pool_member_type member_type = LSM_POOL_MEMBER_TYPE_UNKNOWN;
                lsm_string_list *member_ids = NULL;

                rc = p->ops_v1_2->pool_member_info(p, pool, &raid_type,
                                                   &member_type, &member_ids,
                                                   LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_OK == rc) {
                    result.push_back(Value((int32_t)raid_type));
                    result.push_back(Value((int32_t)member_type));
                    result.push_back(string_list_to_value(member_ids));
                    if (member_ids != NULL) {
                        lsm_string_list_free(member_ids);
                    }
                    response = Value(result);
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

static int ag_list(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    char *key = NULL;
    char *val = NULL;

    if (p && p->san_ops && p->san_ops->ag_list) {

        if (LSM_FLAG_EXPECTED_TYPE(params) &&
            (rc = get_search_params(params, &key, &val)) == LSM_ERR_OK) {
            lsm_access_group **groups = NULL;
            uint32_t count;

            rc = p->san_ops->ag_list(p, key, val, &groups, &count,
                                     LSM_FLAG_GET_VALUE(params));
            if (LSM_ERR_OK == rc) {
                response = access_group_list_to_value(groups, count);

                /* Free the memory */
                lsm_access_group_record_array_free(groups, count);
            }
            free(key);
            free(val);
        } else {
            if (rc == LSM_ERR_NO_SUPPORT) {
                rc = LSM_ERR_TRANSPORT_INVALID_ARG;
            }
        }
    }
    return rc;
}

static int ag_create(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->san_ops && p->san_ops->ag_create) {
        Value v_name = params["name"];
        Value v_init_id = params["init_id"];
        Value v_init_type = params["init_type"];
        Value v_system = params["system"];

        if (Value::string_t == v_name.valueType() &&
            Value::string_t == v_init_id.valueType() &&
            Value::numeric_t == v_init_type.valueType() &&
            IS_CLASS_SYSTEM(v_system) && LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_access_group *ag = NULL;
            lsm_system *system = value_to_system(v_system);

            if (system) {
                rc = p->san_ops->ag_create(
                    p, v_name.asC_str(), v_init_id.asC_str(),
                    (lsm_access_group_init_type)v_init_type.asInt32_t(), system,
                    &ag, LSM_FLAG_GET_VALUE(params));
                if (LSM_ERR_OK == rc) {
                    response = access_group_to_value(ag);
                    lsm_access_group_record_free(ag);
                }

                lsm_system_record_free(system);
                system = NULL;
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ag_delete(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    UNUSED(response);

    if (p && p->san_ops && p->san_ops->ag_delete) {
        Value v_access_group = params["access_group"];

        if (IS_CLASS_ACCESS_GROUP(v_access_group) &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_access_group *ag = value_to_access_group(v_access_group);

            if (ag) {
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

static int ag_initiator_add(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->san_ops && p->san_ops->ag_add_initiator) {

        Value v_group = params["access_group"];
        Value v_init_id = params["init_id"];
        Value v_init_type = params["init_type"];

        if (IS_CLASS_ACCESS_GROUP(v_group) &&
            Value::string_t == v_init_id.valueType() &&
            Value::numeric_t == v_init_type.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_access_group *ag = value_to_access_group(v_group);
            if (ag) {
                lsm_access_group *updated_access_group = NULL;
                const char *id = v_init_id.asC_str();
                lsm_access_group_init_type id_type =
                    (lsm_access_group_init_type)v_init_type.asInt32_t();

                rc = p->san_ops->ag_add_initiator(p, ag, id, id_type,
                                                  &updated_access_group,
                                                  LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_OK == rc) {
                    response = access_group_to_value(updated_access_group);
                    lsm_access_group_record_free(updated_access_group);
                }

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

static int ag_initiator_del(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->san_ops && p->san_ops->ag_del_initiator) {

        Value v_group = params["access_group"];
        Value v_init_id = params["init_id"];
        Value v_init_type = params["init_type"];

        if (IS_CLASS_ACCESS_GROUP(v_group) &&
            Value::string_t == v_init_id.valueType() &&
            Value::numeric_t == v_init_type.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_access_group *ag = value_to_access_group(v_group);

            if (ag) {
                lsm_access_group *updated_access_group = NULL;
                const char *id = v_init_id.asC_str();
                lsm_access_group_init_type id_type =
                    (lsm_access_group_init_type)v_init_type.asInt32_t();
                rc = p->san_ops->ag_del_initiator(p, ag, id, id_type,
                                                  &updated_access_group,
                                                  LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_OK == rc) {
                    response = access_group_to_value(updated_access_group);
                    lsm_access_group_record_free(updated_access_group);
                }

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

static int volume_mask(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    UNUSED(response);
    if (p && p->san_ops && p->san_ops->ag_grant) {

        Value v_group = params["access_group"];
        Value v_vol = params["volume"];

        if (IS_CLASS_ACCESS_GROUP(v_group) && IS_CLASS_VOLUME(v_vol) &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_access_group *ag = value_to_access_group(v_group);
            lsm_volume *vol = value_to_volume(v_vol);

            if (ag && vol) {
                rc = p->san_ops->ag_grant(p, ag, vol,
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

static int volume_unmask(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    UNUSED(response);
    if (p && p->san_ops && p->san_ops->ag_revoke) {

        Value v_group = params["access_group"];
        Value v_vol = params["volume"];

        if (IS_CLASS_ACCESS_GROUP(v_group) && IS_CLASS_VOLUME(v_vol) &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_access_group *ag = value_to_access_group(v_group);
            lsm_volume *vol = value_to_volume(v_vol);

            if (ag && vol) {
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

static int vol_accessible_by_ag(lsm_plugin_ptr p, Value &params,
                                Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->san_ops && p->san_ops->vol_accessible_by_ag) {
        Value v_access_group = params["access_group"];

        if (IS_CLASS_ACCESS_GROUP(v_access_group) &&
            LSM_FLAG_EXPECTED_TYPE(params)) {
            lsm_access_group *ag = value_to_access_group(v_access_group);

            if (ag) {
                lsm_volume **vols = NULL;
                uint32_t count = 0;

                rc = p->san_ops->vol_accessible_by_ag(
                    p, ag, &vols, &count, LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_OK == rc) {
                    std::vector<Value> result;
                    result.reserve(count);

                    for (uint32_t i = 0; i < count; ++i) {
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

static int ag_granted_to_volume(lsm_plugin_ptr p, Value &params,
                                Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->san_ops && p->san_ops->ag_granted_to_vol) {

        Value v_vol = params["volume"];

        if (IS_CLASS_VOLUME(v_vol) && LSM_FLAG_EXPECTED_TYPE(params)) {
            lsm_volume *volume = value_to_volume(v_vol);

            if (volume) {
                lsm_access_group **groups = NULL;
                uint32_t count = 0;

                rc = p->san_ops->ag_granted_to_vol(p, volume, &groups, &count,
                                                   LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_OK == rc) {
                    std::vector<Value> result;
                    result.reserve(count);

                    for (uint32_t i = 0; i < count; ++i) {
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

static int volume_dependency(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->san_ops && p->san_ops->vol_child_depends) {

        Value v_vol = params["volume"];

        if (IS_CLASS_VOLUME(v_vol) && LSM_FLAG_EXPECTED_TYPE(params)) {
            lsm_volume *volume = value_to_volume(v_vol);

            if (volume) {
                uint8_t yes;

                rc = p->san_ops->vol_child_depends(p, volume, &yes,
                                                   LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_OK == rc) {
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

static int volume_dependency_rm(lsm_plugin_ptr p, Value &params,
                                Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->san_ops && p->san_ops->vol_child_depends_rm) {

        Value v_vol = params["volume"];

        if (IS_CLASS_VOLUME(v_vol) && LSM_FLAG_EXPECTED_TYPE(params)) {
            lsm_volume *volume = value_to_volume(v_vol);

            if (volume) {

                char *job = NULL;

                rc = p->san_ops->vol_child_depends_rm(
                    p, volume, &job, LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_JOB_STARTED == rc) {
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

static int fs(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    char *key = NULL;
    char *val = NULL;

    if (p && p->fs_ops && p->fs_ops->fs_list) {
        if (LSM_FLAG_EXPECTED_TYPE(params) &&
            ((rc = get_search_params(params, &key, &val)) == LSM_ERR_OK)) {

            lsm_fs **fs = NULL;
            uint32_t count = 0;

            rc = p->fs_ops->fs_list(p, key, val, &fs, &count,
                                    LSM_FLAG_GET_VALUE(params));

            if (LSM_ERR_OK == rc) {
                std::vector<Value> result;
                result.reserve(count);

                for (uint32_t i = 0; i < count; ++i) {
                    result.push_back(fs_to_value(fs[i]));
                }

                response = Value(result);
                lsm_fs_record_array_free(fs, count);
                fs = NULL;
            }
            free(key);
            free(val);
        } else {
            if (rc == LSM_ERR_NO_SUPPORT) {
                rc = LSM_ERR_TRANSPORT_INVALID_ARG;
            }
        }
    }
    return rc;
}

static int fs_create(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->fs_ops && p->fs_ops->fs_create) {

        Value v_pool = params["pool"];
        Value v_name = params["name"];
        Value v_size = params["size_bytes"];

        if (IS_CLASS_POOL(v_pool) && Value::string_t == v_name.valueType() &&
            Value::numeric_t == v_size.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_pool *pool = value_to_pool(v_pool);

            if (pool) {
                const char *name = params["name"].asC_str();
                uint64_t size_bytes = params["size_bytes"].asUint64_t();
                lsm_fs *fs = NULL;
                char *job = NULL;

                rc = p->fs_ops->fs_create(p, pool, name, size_bytes, &fs, &job,
                                          LSM_FLAG_GET_VALUE(params));

                std::vector<Value> r;

                if (LSM_ERR_OK == rc) {
                    r.push_back(Value());
                    r.push_back(fs_to_value(fs));
                    response = Value(r);
                    lsm_fs_record_free(fs);
                } else if (LSM_ERR_JOB_STARTED == rc) {
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

static int fs_delete(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->fs_ops && p->fs_ops->fs_delete) {

        Value v_fs = params["fs"];

        if (IS_CLASS_FILE_SYSTEM(v_fs) && LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_fs *fs = value_to_fs(v_fs);

            if (fs) {
                char *job = NULL;

                rc = p->fs_ops->fs_delete(p, fs, &job,
                                          LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_JOB_STARTED == rc) {
                    response = Value(job);
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

static int fs_resize(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->fs_ops && p->fs_ops->fs_resize) {

        Value v_fs = params["fs"];
        Value v_size = params["new_size_bytes"];

        if (IS_CLASS_FILE_SYSTEM(v_fs) &&
            Value::numeric_t == v_size.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_fs *fs = value_to_fs(v_fs);

            if (fs) {
                uint64_t size_bytes = v_size.asUint64_t();
                lsm_fs *rfs = NULL;
                char *job = NULL;

                rc = p->fs_ops->fs_resize(p, fs, size_bytes, &rfs, &job,
                                          LSM_FLAG_GET_VALUE(params));

                std::vector<Value> r;

                if (LSM_ERR_OK == rc) {
                    r.push_back(Value());
                    r.push_back(fs_to_value(rfs));
                    response = Value(r);
                    lsm_fs_record_free(rfs);
                } else if (LSM_ERR_JOB_STARTED == rc) {
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

static int fs_clone(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->fs_ops && p->fs_ops->fs_clone) {

        Value v_src_fs = params["src_fs"];
        Value v_name = params["dest_fs_name"];
        Value v_ss = params["snapshot"]; /* This is optional */

        if (IS_CLASS_FILE_SYSTEM(v_src_fs) &&
            Value::string_t == v_name.valueType() &&
            (Value::null_t == v_ss.valueType() ||
             Value::object_t == v_ss.valueType()) &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_fs *clonedFs = NULL;
            char *job = NULL;
            lsm_fs *fs = value_to_fs(v_src_fs);
            const char *name = v_name.asC_str();
            lsm_fs_ss *ss =
                (Value::null_t == v_ss.valueType()) ? NULL : value_to_ss(v_ss);

            if (fs && ((ss && v_ss.valueType() == Value::object_t) ||
                       (!ss && v_ss.valueType() == Value::null_t))) {

                rc = p->fs_ops->fs_clone(p, fs, name, &clonedFs, ss, &job,
                                         LSM_FLAG_GET_VALUE(params));

                std::vector<Value> r;
                if (LSM_ERR_OK == rc) {
                    r.push_back(Value());
                    r.push_back(fs_to_value(clonedFs));
                    response = Value(r);
                    lsm_fs_record_free(clonedFs);
                } else if (LSM_ERR_JOB_STARTED == rc) {
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

static int fs_file_clone(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_OK;

    if (p && p->fs_ops && p->fs_ops->fs_file_clone) {

        Value v_fs = params["fs"];
        Value v_src_name = params["src_file_name"];
        Value v_dest_name = params["dest_file_name"];
        Value v_ss = params["snapshot"]; /* This is optional */

        if (IS_CLASS_FILE_SYSTEM(v_fs) &&
            Value::string_t == v_src_name.valueType() &&
            Value::string_t == v_dest_name.valueType() &&
            (Value::null_t == v_ss.valueType() ||
             Value::object_t == v_ss.valueType()) &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_fs *fs = value_to_fs(v_fs);
            lsm_fs_ss *ss =
                (Value::null_t == v_ss.valueType()) ? NULL : value_to_ss(v_ss);

            if (fs && ((ss && v_ss.valueType() == Value::object_t) ||
                       (!ss && v_ss.valueType() == Value::null_t))) {

                const char *src = v_src_name.asC_str();
                const char *dest = v_dest_name.asC_str();

                char *job = NULL;

                rc = p->fs_ops->fs_file_clone(p, fs, src, dest, ss, &job,
                                              LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_JOB_STARTED == rc) {
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

static int fs_child_dependency(lsm_plugin_ptr p, Value &params,
                               Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    if (p && p->fs_ops && p->fs_ops->fs_child_dependency) {

        Value v_fs = params["fs"];
        Value v_files = params["files"];

        if (IS_CLASS_FILE_SYSTEM(v_fs) &&
            (Value::array_t == v_files.valueType() ||
             Value::null_t == v_files.valueType()) &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_fs *fs = value_to_fs(v_fs);
            lsm_string_list *files = (Value::null_t == v_files.valueType())
                                         ? NULL
                                         : value_to_string_list(v_files);

            if (fs &&
                (files || (!files && Value::null_t == v_files.valueType()))) {
                uint8_t yes = 0;

                rc = p->fs_ops->fs_child_dependency(p, fs, files, &yes);

                if (LSM_ERR_OK == rc) {
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

static int fs_child_dependency_rm(lsm_plugin_ptr p, Value &params,
                                  Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    if (p && p->fs_ops && p->fs_ops->fs_child_dependency_rm) {

        Value v_fs = params["fs"];
        Value v_files = params["files"];

        if (IS_CLASS_FILE_SYSTEM(v_fs) &&
            (Value::array_t == v_files.valueType() ||
             Value::null_t == v_files.valueType()) &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_fs *fs = value_to_fs(v_fs);
            lsm_string_list *files = (Value::null_t == v_files.valueType())
                                         ? NULL
                                         : value_to_string_list(v_files);

            if (fs &&
                (files || (!files && Value::null_t == v_files.valueType()))) {
                char *job = NULL;

                rc = p->fs_ops->fs_child_dependency_rm(
                    p, fs, files, &job, LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_JOB_STARTED == rc) {
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

static int ss_list(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    if (p && p->fs_ops && p->fs_ops->fs_ss_list) {

        Value v_fs = params["fs"];

        if (IS_CLASS_FILE_SYSTEM(v_fs) && LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_fs *fs = value_to_fs(v_fs);

            if (fs) {
                lsm_fs_ss **ss = NULL;
                uint32_t count = 0;

                rc = p->fs_ops->fs_ss_list(p, fs, &ss, &count,
                                           LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_OK == rc) {
                    std::vector<Value> result;
                    result.reserve(count);

                    for (uint32_t i = 0; i < count; ++i) {
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

static int ss_create(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    if (p && p->fs_ops && p->fs_ops->fs_ss_create) {

        Value v_fs = params["fs"];
        Value v_ss_name = params["snapshot_name"];

        if (IS_CLASS_FILE_SYSTEM(v_fs) &&
            Value::string_t == v_ss_name.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {
            lsm_fs *fs = value_to_fs(v_fs);

            if (fs) {
                lsm_fs_ss *ss = NULL;
                char *job = NULL;

                const char *name = v_ss_name.asC_str();

                rc = p->fs_ops->fs_ss_create(p, fs, name, &ss, &job,
                                             LSM_FLAG_GET_VALUE(params));

                std::vector<Value> r;
                if (LSM_ERR_OK == rc) {
                    r.push_back(Value());
                    r.push_back(ss_to_value(ss));
                    response = Value(r);
                    lsm_fs_ss_record_free(ss);
                } else if (LSM_ERR_JOB_STARTED == rc) {
                    r.push_back(Value(job));
                    r.push_back(Value());
                    response = Value(r);
                    free(job);
                }

            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsm_fs_record_free(fs);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int ss_delete(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    if (p && p->fs_ops && p->fs_ops->fs_ss_delete) {

        Value v_fs = params["fs"];
        Value v_ss = params["snapshot"];

        if (IS_CLASS_FILE_SYSTEM(v_fs) && IS_CLASS_FS_SNAPSHOT(v_ss) &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_fs *fs = value_to_fs(v_fs);
            lsm_fs_ss *ss = value_to_ss(v_ss);

            if (fs && ss) {
                char *job = NULL;
                rc = p->fs_ops->fs_ss_delete(p, fs, ss, &job,
                                             LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_JOB_STARTED == rc) {
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

static int ss_restore(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    if (p && p->fs_ops && p->fs_ops->fs_ss_restore) {

        Value v_fs = params["fs"];
        Value v_ss = params["snapshot"];
        Value v_files = params["files"];
        Value v_restore_files = params["restore_files"];
        Value v_all_files = params["all_files"];

        if (IS_CLASS_FILE_SYSTEM(v_fs) && IS_CLASS_FS_SNAPSHOT(v_ss) &&
            (Value::array_t == v_files.valueType() ||
             Value::null_t == v_files.valueType()) &&
            (Value::array_t == v_restore_files.valueType() ||
             Value::null_t == v_restore_files.valueType()) &&
            Value::boolean_t == v_all_files.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            char *job = NULL;
            lsm_fs *fs = value_to_fs(v_fs);
            lsm_fs_ss *ss = value_to_ss(v_ss);
            lsm_string_list *files = (Value::null_t == v_files.valueType())
                                         ? NULL
                                         : value_to_string_list(v_files);
            lsm_string_list *restore_files =
                (Value::null_t == v_restore_files.valueType())
                    ? NULL
                    : value_to_string_list(v_restore_files);
            int all_files = (v_all_files.asBool()) ? 1 : 0;

            if (fs && ss &&
                (files || (!files && Value::null_t == v_files.valueType())) &&
                (restore_files ||
                 (!restore_files &&
                  Value::null_t == v_restore_files.valueType()))) {
                rc = p->fs_ops->fs_ss_restore(p, fs, ss, files, restore_files,
                                              all_files, &job,
                                              LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_JOB_STARTED == rc) {
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

static int export_auth(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    if (p && p->nas_ops && p->nas_ops->nfs_auth_types) {
        lsm_string_list *types = NULL;

        if (LSM_FLAG_EXPECTED_TYPE(params)) {

            rc = p->nas_ops->nfs_auth_types(p, &types,
                                            LSM_FLAG_GET_VALUE(params));
            if (LSM_ERR_OK == rc) {
                response = string_list_to_value(types);
                lsm_string_list_free(types);
            }
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int exports(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    char *key = NULL;
    char *val = NULL;

    if (p && p->nas_ops && p->nas_ops->nfs_list) {
        lsm_nfs_export **exports = NULL;
        uint32_t count = 0;

        if (LSM_FLAG_EXPECTED_TYPE(params) &&
            (rc = get_search_params(params, &key, &val)) == LSM_ERR_OK) {
            rc = p->nas_ops->nfs_list(p, key, val, &exports, &count,
                                      LSM_FLAG_GET_VALUE(params));

            if (LSM_ERR_OK == rc) {
                std::vector<Value> result;
                result.reserve(count);

                for (uint32_t i = 0; i < count; ++i) {
                    result.push_back(nfs_export_to_value(exports[i]));
                }
                response = Value(result);

                lsm_nfs_export_record_array_free(exports, count);
                exports = NULL;
                count = 0;
            }
            free(key);
            free(val);
        } else {
            if (rc == LSM_ERR_NO_SUPPORT) {
                rc = LSM_ERR_TRANSPORT_INVALID_ARG;
            }
        }
    }

    return rc;
}

static int64_t get_uid_gid(Value &id) {
    if (Value::null_t == id.valueType()) {
        return ANON_UID_GID_NA;
    } else {
        return id.asInt64_t();
    }
}

static int export_fs(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    if (p && p->nas_ops && p->nas_ops->nfs_export) {

        Value v_fs_id = params["fs_id"];
        Value v_export_path = params["export_path"];
        Value v_root_list = params["root_list"];
        Value v_rw_list = params["rw_list"];
        Value v_ro_list = params["ro_list"];
        Value v_auth_type = params["auth_type"];
        Value v_options = params["options"];
        Value v_anon_uid = params["anon_uid"];
        Value v_anon_gid = params["anon_gid"];

        if (Value::string_t == v_fs_id.valueType() &&
            (Value::string_t == v_export_path.valueType() ||
             Value::null_t == v_export_path.valueType()) &&
            Value::array_t == v_root_list.valueType() &&
            Value::array_t == v_rw_list.valueType() &&
            Value::array_t == v_ro_list.valueType() &&
            (Value::string_t == v_auth_type.valueType() ||
             Value::null_t == v_auth_type.valueType()) &&
            (Value::string_t == v_options.valueType() ||
             Value::null_t == v_options.valueType()) &&
            Value::numeric_t == v_anon_uid.valueType() &&
            Value::numeric_t == v_anon_gid.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_string_list *root_list = value_to_string_list(v_root_list);
            lsm_string_list *rw_list = value_to_string_list(v_rw_list);
            lsm_string_list *ro_list = value_to_string_list(v_ro_list);

            if (root_list && rw_list && ro_list) {
                const char *fs_id = v_fs_id.asC_str();
                const char *export_path = v_export_path.asC_str();
                const char *auth_type = v_auth_type.asC_str();
                const char *options = v_options.asC_str();
                lsm_nfs_export *exported = NULL;

                int64_t anon_uid = get_uid_gid(v_anon_uid);
                int64_t anon_gid = get_uid_gid(v_anon_gid);

                rc = p->nas_ops->nfs_export(
                    p, fs_id, export_path, root_list, rw_list, ro_list,
                    anon_uid, anon_gid, auth_type, options, &exported,
                    LSM_FLAG_GET_VALUE(params));
                if (LSM_ERR_OK == rc) {
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

static int export_remove(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    UNUSED(response);
    if (p && p->nas_ops && p->nas_ops->nfs_export_remove) {
        Value v_export = params["export"];

        if (IS_CLASS_FS_EXPORT(v_export) && LSM_FLAG_EXPECTED_TYPE(params)) {
            lsm_nfs_export *exp = value_to_nfs_export(v_export);

            if (exp) {
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

static int iscsi_chap(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;

    UNUSED(response);
    if (p && p->san_ops && p->san_ops->iscsi_chap_auth) {
        Value v_init = params["init_id"];
        Value v_in_user = params["in_user"];
        Value v_in_password = params["in_password"];
        Value v_out_user = params["out_user"];
        Value v_out_password = params["out_password"];

        if (Value::string_t == v_init.valueType() &&
            (Value::string_t == v_in_user.valueType() ||
             Value::null_t == v_in_user.valueType()) &&
            (Value::string_t == v_in_password.valueType() ||
             Value::null_t == v_in_password.valueType()) &&
            (Value::string_t == v_out_user.valueType() ||
             Value::null_t == v_out_user.valueType()) &&
            (Value::string_t == v_out_password.valueType() ||
             Value::null_t == v_out_password.valueType()) &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            rc = p->san_ops->iscsi_chap_auth(
                p, v_init.asC_str(), v_in_user.asC_str(),
                v_in_password.asC_str(), v_out_user.asC_str(),
                v_out_password.asC_str(), LSM_FLAG_GET_VALUE(params));

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_volume_raid_create_cap_get(lsm_plugin_ptr p, Value &params,
                                             Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    if (p && p->ops_v1_2 && p->ops_v1_2->vol_create_raid_cap_get) {
        Value v_system = params["system"];

        if (IS_CLASS_SYSTEM(v_system) && LSM_FLAG_EXPECTED_TYPE(params)) {

            uint32_t *supported_raid_types = NULL;
            uint32_t supported_raid_type_count = 0;

            uint32_t *supported_strip_sizes = NULL;
            uint32_t supported_strip_size_count = 0;

            lsm_system *sys = value_to_system(v_system);

            if (sys) {

                rc = p->ops_v1_2->vol_create_raid_cap_get(
                    p, sys, &supported_raid_types, &supported_raid_type_count,
                    &supported_strip_sizes, &supported_strip_size_count,
                    LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_OK == rc) {
                    std::vector<Value> result;
                    result.push_back(uint32_array_to_value(
                        supported_raid_types, supported_raid_type_count));
                    result.push_back(uint32_array_to_value(
                        supported_strip_sizes, supported_strip_size_count));
                    response = Value(result);
                    free(supported_raid_types);
                    free(supported_strip_sizes);
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }

            lsm_system_record_free(sys);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_volume_raid_create(lsm_plugin_ptr p, Value &params,
                                     Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    if (p && p->ops_v1_2 && p->ops_v1_2->vol_create_raid) {
        Value v_name = params["name"];
        Value v_raid_type = params["raid_type"];
        Value v_strip_size = params["strip_size"];
        Value v_disks = params["disks"];

        if (Value::string_t == v_name.valueType() &&
            Value::numeric_t == v_raid_type.valueType() &&
            Value::numeric_t == v_strip_size.valueType() &&
            Value::array_t == v_disks.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_disk **disks = NULL;
            uint32_t disk_count = 0;
            rc = value_array_to_disks(v_disks, &disks, &disk_count);
            if (LSM_ERR_OK != rc) {
                lsm_disk_record_array_free(disks, disk_count);
                return rc;
            }

            const char *name = v_name.asC_str();
            lsm_volume_raid_type raid_type =
                (lsm_volume_raid_type)v_raid_type.asInt32_t();
            uint32_t strip_size = v_strip_size.asUint32_t();

            lsm_volume *new_vol = NULL;

            rc = p->ops_v1_2->vol_create_raid(p, name, raid_type, disks,
                                              disk_count, strip_size, &new_vol,
                                              LSM_FLAG_GET_VALUE(params));

            if (LSM_ERR_OK == rc) {
                response = volume_to_value(new_vol);
                lsm_volume_record_free(new_vol);
            }

            lsm_disk_record_array_free(disks, disk_count);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_volume_ident_led_on(lsm_plugin_ptr p, Value &params,
                                      Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    UNUSED(response);
    if (p && p->ops_v1_3 && p->ops_v1_3->vol_ident_on) {
        Value v_vol = params["volume"];

        if (Value::object_t == v_vol.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_volume *volume = value_to_volume(v_vol);

            rc = p->ops_v1_3->vol_ident_on(p, volume,
                                           LSM_FLAG_GET_VALUE(params));

            lsm_volume_record_free(volume);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_volume_ident_led_off(lsm_plugin_ptr p, Value &params,
                                       Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    UNUSED(response);
    if (p && p->ops_v1_3 && p->ops_v1_3->vol_ident_off) {
        Value v_vol = params["volume"];

        if (Value::object_t == v_vol.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_volume *volume = value_to_volume(v_vol);

            rc = p->ops_v1_3->vol_ident_off(p, volume,
                                            LSM_FLAG_GET_VALUE(params));

            lsm_volume_record_free(volume);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_system_read_cache_pct_update(lsm_plugin_ptr p, Value &params,
                                               Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    UNUSED(response);
    if (p && p->ops_v1_3 && p->ops_v1_3->sys_read_cache_pct_update) {
        Value v_sys = params["system"];
        Value v_read_pct = params["read_pct"];

        if (Value::object_t == v_sys.valueType() &&
            Value::numeric_t == v_read_pct.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_system *system = value_to_system(v_sys);
            uint32_t read_pct = v_read_pct.asUint32_t();

            rc = p->ops_v1_3->sys_read_cache_pct_update(
                p, system, read_pct, LSM_FLAG_GET_VALUE(params));

            lsm_system_record_free(system);

        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

/**
 * map of function pointers
 */
static std::map<std::string, handler> dispatch =
    static_map<std::string, handler>("access_group_initiator_add",
                                     ag_initiator_add)(
        "access_group_create", ag_create)("access_group_delete", ag_delete)(
        "access_group_initiator_delete",
        ag_initiator_del)("volume_mask", volume_mask)("access_groups", ag_list)(
        "volume_unmask", volume_unmask)("access_groups_granted_to_volume",
                                        ag_granted_to_volume)(
        "capabilities", capabilities)("disks", handle_disks)(
        "export_auth", export_auth)("export_fs", export_fs)(
        "export_remove", export_remove)("exports", exports)("fs_file_clone",
                                                            fs_file_clone)(
        "fs_child_dependency", fs_child_dependency)("fs_child_dependency_rm",
                                                    fs_child_dependency_rm)(
        "fs_clone", fs_clone)("fs_create", fs_create)("fs_delete", fs_delete)(
        "fs", fs)("fs_resize", fs_resize)("fs_snapshot_create", ss_create)(
        "fs_snapshot_delete", ss_delete)("fs_snapshot_restore", ss_restore)(
        "fs_snapshots", ss_list)("time_out_get", handle_get_time_out)(
        "iscsi_chap_auth", iscsi_chap)("job_free", handle_job_free)(
        "job_status", handle_job_status)("plugin_info", handle_plugin_info)(
        "pools", handle_pools)("target_ports", handle_target_ports)(
        "time_out_set", handle_set_time_out)("plugin_unregister",
                                             handle_unregister)(
        "plugin_register", handle_register)("systems", handle_system_list)(
        "volume_child_dependency_rm",
        volume_dependency_rm)("volume_child_dependency", volume_dependency)(
        "volume_create", handle_volume_create)("volume_delete",
                                               handle_volume_delete)(
        "volume_disable", handle_volume_disable)("volume_enable",
                                                 handle_volume_enable)(
        "volume_replicate",
        handle_volume_replicate)("volume_replicate_range_block_size",
                                 handle_volume_replicate_range_block_size)(
        "volume_replicate_range",
        handle_volume_replicate_range)("volume_resize", handle_volume_resize)(
        "volumes_accessible_by_access_group", vol_accessible_by_ag)(
        "volumes", handle_volumes)("volume_raid_info", handle_volume_raid_info)(
        "pool_member_info", handle_pool_member_info)("volume_raid_create",
                                                     handle_volume_raid_create)(
        "volume_raid_create_cap_get", handle_volume_raid_create_cap_get)(
        "volume_ident_led_on", handle_volume_ident_led_on)(
        "volume_ident_led_off", handle_volume_ident_led_off)(
        "system_read_cache_pct_update", handle_system_read_cache_pct_update)(
        "batteries", handle_batteries)("volume_cache_info",
                                       handle_volume_cache_info)(
        "volume_physical_disk_cache_update", handle_volume_pdc_update)(
        "volume_write_cache_policy_update", handle_volume_wcp_update)(
        "volume_read_cache_policy_update", handle_volume_rcp_update);

static int process_request(lsm_plugin_ptr p, const std::string &method,
                           Value &request, Value &response) {
    int rc = LSM_ERR_LIB_BUG;

    response = Value(); // Default response will be null

    if (dispatch.find(method) != dispatch.end()) {
        rc = (dispatch[method])(p, request["params"], response);
    } else {
        rc = LSM_ERR_NO_SUPPORT;
    }

    return rc;
}

static int lsm_plugin_run(lsm_plugin_ptr p) {
    int rc = 0;
    lsm_flag flags = 0;

    if (LSM_IS_PLUGIN(p)) {
        while (true) {
            try {

                if (!LSM_IS_PLUGIN(p)) {
                    syslog(LOG_USER | LOG_NOTICE, "Someone stepped on "
                                                  "plugin pointer, exiting!");
                    break;
                }

                Value req = p->tp->readRequest();
                Value resp;

                if (req.isValidRequest()) {
                    std::string method = req["method"].asString();
                    rc = process_request(p, method, req, resp);

                    if (LSM_ERR_OK == rc || LSM_ERR_JOB_STARTED == rc) {
                        p->tp->responseSend(resp);
                    } else {
                        error_send(p, rc);
                    }

                    if (method == "plugin_unregister") {
                        flags = LSM_FLAG_GET_VALUE(req["params"]);
                        break;
                    }
                } else {
                    syslog(LOG_USER | LOG_NOTICE, "Invalid request");
                    break;
                }
            } catch (EOFException &eof) {
                break;
            } catch (ValueException &ve) {
                syslog(LOG_USER | LOG_NOTICE, "Plug-in exception: %s",
                       ve.what());
                rc = 1;
                break;
            } catch (LsmException &le) {
                syslog(LOG_USER | LOG_NOTICE, "Plug-in exception: %s",
                       le.what());
                rc = 2;
                break;
            } catch (...) {
                syslog(LOG_USER | LOG_NOTICE, "Plug-in un-handled exception");
                rc = 3;
                break;
            }
        }
        lsm_plugin_free(p, flags);
        p = NULL;
    } else {
        rc = LSM_ERR_INVALID_ARGUMENT;
    }

    return rc;
}

int lsm_log_error_basic(lsm_plugin_ptr plug, lsm_error_number code,
                        const char *msg) {
    if (!LSM_IS_PLUGIN(plug)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    lsm_error_ptr e = LSM_ERROR_CREATE_PLUGIN_MSG(code, msg);
    if (e) {
        int rc = lsm_plugin_error_log(plug, e);

        if (LSM_ERR_OK != rc) {
            syslog(LOG_USER | LOG_NOTICE,
                   "Plug-in error %d while reporting an error, code= %d, "
                   "msg= %s",
                   rc, code, msg);
        }
    }
    return (int)code;
}

int lsm_plugin_error_log(lsm_plugin_ptr plug, lsm_error_ptr error) {
    if (!LSM_IS_PLUGIN(plug) || !LSM_IS_ERROR(error)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (plug->error) {
        lsm_error_free(plug->error);
    }

    plug->error = error;

    return LSM_ERR_OK;
}

#define STR_D(c, s)                                                            \
    do {                                                                       \
        if (s) {                                                               \
            (c) = strdup(s);                                                   \
            if (!c) {                                                          \
                rc = LSM_ERR_NO_MEMORY;                                        \
                goto bail;                                                     \
            }                                                                  \
        }                                                                      \
    } while (0)

int LSM_DLL_EXPORT lsm_uri_parse(const char *uri, char **scheme, char **user,
                                 char **server, int *port, char **path,
                                 lsm_hash **query_params) {
    int rc = LSM_ERR_INVALID_ARGUMENT;
    xmlURIPtr u = NULL;

    if (uri && strlen(uri) > 0) {
        *scheme = NULL;
        *user = NULL;
        *server = NULL;
        *port = -1;
        *path = NULL;
        *query_params = NULL;

        u = xmlParseURI(uri);
        if (u) {
            STR_D(*scheme, u->scheme);
            STR_D(*user, u->user);
            STR_D(*server, u->server);
            STR_D(*path, u->path);
            *port = u->port;

            *query_params = lsm_hash_alloc();
            if (*query_params) {
                int i;
                struct qparam_set *qp = NULL;
                qp = qparam_query_parse(u->query_raw);

                if (qp) {
                    for (i = 0; i < qp->n; ++i) {
                        rc = lsm_hash_string_set(*query_params, qp->p[i].name,
                                                 qp->p[i].value);
                        if (LSM_ERR_OK != rc) {
                            free_qparam_set(qp);
                            goto bail;
                        }
                    }
                    free_qparam_set(qp);
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
                goto bail;
            }

            rc = LSM_ERR_OK;
        }

    bail:
        if (rc != LSM_ERR_OK) {
            free(*scheme);
            *scheme = NULL;
            free(*user);
            *user = NULL;
            free(*server);
            *server = NULL;
            *port = -1;
            free(*path);
            *path = NULL;
            lsm_hash_free(*query_params);
            *query_params = NULL;
        }

        if (u) {
            xmlFreeURI(u);
            u = NULL;
        }
    }
    return rc;
}

typedef int (*array_cmp)(void *item, void *cmp_data);
typedef void (*free_item)(void *item);

#define CMP_FUNCTION(name, method, method_type)                                \
    static int name(void *i, void *d) {                                        \
        method_type *v = (method_type *)i;                                     \
        char *val = (char *)d;                                                 \
                                                                               \
        if ((method(v) != NULL) && (val != NULL) &&                            \
            (strcmp(method(v), val) == 0)) {                                   \
            return 1;                                                          \
        }                                                                      \
        return 0;                                                              \
    }

#define CMP_FREE_FUNCTION(name, method, method_type)                           \
    static void name(void *i) { method((method_type *)i); }

static int filter(void *a[], size_t size, array_cmp cmp, void *cmp_data,
                  free_item fo) {
    int remaining = 0;
    size_t i = 0;

    for (i = 0; i < size; ++i) {
        if (cmp(a[i], cmp_data)) {
            memmove(&a[remaining], &a[i], sizeof(void *));
            remaining += 1;
        } else {
            fo(a[i]);
            a[i] = NULL;
        }
    }
    return remaining;
}

CMP_FUNCTION(volume_compare_id, lsm_volume_id_get, lsm_volume)
CMP_FUNCTION(volume_compare_system, lsm_volume_system_id_get, lsm_volume)
CMP_FUNCTION(volume_compare_pool, lsm_volume_pool_id_get, lsm_volume)
CMP_FREE_FUNCTION(volume_free, lsm_volume_record_free, lsm_volume)

void lsm_plug_volume_search_filter(const char *search_key,
                                   const char *search_value, lsm_volume *vols[],
                                   uint32_t *count) {
    array_cmp cmp = NULL;

    if (search_key) {

        if (0 == strcmp("id", search_key)) {
            cmp = volume_compare_id;
        } else if (0 == strcmp("system_id", search_key)) {
            cmp = volume_compare_system;
        } else if (0 == strcmp("pool_id", search_key)) {
            cmp = volume_compare_pool;
        }

        if (cmp) {
            *count = filter((void **)vols, *count, cmp, (void *)search_value,
                            volume_free);
        }
    }
}

CMP_FUNCTION(pool_compare_id, lsm_pool_id_get, lsm_pool)
CMP_FUNCTION(pool_compare_system, lsm_pool_system_id_get, lsm_pool)
CMP_FREE_FUNCTION(pool_free, lsm_pool_record_free, lsm_pool);

void lsm_plug_pool_search_filter(const char *search_key,
                                 const char *search_value, lsm_pool *pools[],
                                 uint32_t *count) {
    array_cmp cmp = NULL;

    if (search_key) {

        if (0 == strcmp("id", search_key)) {
            cmp = pool_compare_id;
        } else if (0 == strcmp("system_id", search_key)) {
            cmp = pool_compare_system;
        }

        if (cmp) {
            *count = filter((void **)pools, *count, cmp, (void *)search_value,
                            pool_free);
        }
    }
}

CMP_FUNCTION(disk_compare_id, lsm_disk_id_get, lsm_disk)
CMP_FUNCTION(disk_compare_system, lsm_disk_system_id_get, lsm_disk)
CMP_FREE_FUNCTION(disk_free, lsm_disk_record_free, lsm_disk)

void lsm_plug_disk_search_filter(const char *search_key,
                                 const char *search_value, lsm_disk *disks[],
                                 uint32_t *count) {
    array_cmp cmp = NULL;

    if (search_key) {

        if (0 == strcmp("id", search_key)) {
            cmp = disk_compare_id;
        } else if (0 == strcmp("system_id", search_key)) {
            cmp = disk_compare_system;
        }

        if (cmp) {
            *count = filter((void **)disks, *count, cmp, (void *)search_value,
                            disk_free);
        }
    }
}

CMP_FUNCTION(access_group_compare_id, lsm_access_group_id_get, lsm_access_group)
CMP_FUNCTION(access_group_compare_system, lsm_access_group_system_id_get,
             lsm_access_group)
CMP_FREE_FUNCTION(access_group_free, lsm_access_group_record_free,
                  lsm_access_group);

void lsm_plug_access_group_search_filter(const char *search_key,
                                         const char *search_value,
                                         lsm_access_group *ag[],
                                         uint32_t *count) {
    array_cmp cmp = NULL;

    if (search_key) {

        if (0 == strcmp("id", search_key)) {
            cmp = access_group_compare_id;
        } else if (0 == strcmp("system_id", search_key)) {
            cmp = access_group_compare_system;
        }

        if (cmp) {
            *count = filter((void **)ag, *count, cmp, (void *)search_value,
                            access_group_free);
        }
    }
}

CMP_FUNCTION(fs_compare_id, lsm_fs_id_get, lsm_fs)
CMP_FUNCTION(fs_compare_system, lsm_fs_system_id_get, lsm_fs)
CMP_FREE_FUNCTION(fs_free, lsm_fs_record_free, lsm_fs);

void lsm_plug_fs_search_filter(const char *search_key, const char *search_value,
                               lsm_fs *fs[], uint32_t *count) {
    array_cmp cmp = NULL;

    if (search_key) {

        if (0 == strcmp("id", search_key)) {
            cmp = fs_compare_id;
        } else if (0 == strcmp("system_id", search_key)) {
            cmp = fs_compare_system;
        }

        if (cmp) {
            *count =
                filter((void **)fs, *count, cmp, (void *)search_value, fs_free);
        }
    }
}

CMP_FUNCTION(nfs_compare_id, lsm_nfs_export_id_get, lsm_nfs_export)
CMP_FUNCTION(nfs_compare_fs_id, lsm_nfs_export_fs_id_get, lsm_nfs_export)
CMP_FREE_FUNCTION(nfs_free, lsm_nfs_export_record_free, lsm_nfs_export)

void lsm_plug_nfs_export_search_filter(const char *search_key,
                                       const char *search_value,
                                       lsm_nfs_export *exports[],
                                       uint32_t *count) {
    array_cmp cmp = NULL;

    if (search_key) {

        if (0 == strcmp("id", search_key)) {
            cmp = nfs_compare_id;
        } else if (0 == strcmp("fs_id", search_key)) {
            cmp = nfs_compare_fs_id;
        }

        if (cmp) {
            *count = filter((void **)exports, *count, cmp, (void *)search_value,
                            nfs_free);
        }
    }
}

CMP_FUNCTION(tp_compare_id, lsm_target_port_id_get, lsm_target_port)
CMP_FUNCTION(tp_compare_system_id, lsm_target_port_system_id_get,
             lsm_target_port)
CMP_FREE_FUNCTION(tp_free, lsm_target_port_record_free, lsm_target_port)

void lsm_plug_target_port_search_filter(const char *search_key,
                                        const char *search_value,
                                        lsm_target_port *tp[],
                                        uint32_t *count) {
    array_cmp cmp = NULL;

    if (search_key) {

        if (0 == strcmp("id", search_key)) {
            cmp = tp_compare_id;
        } else if (0 == strcmp("system_id", search_key)) {
            cmp = tp_compare_system_id;
        }

        if (cmp) {
            *count =
                filter((void **)tp, *count, cmp, (void *)search_value, tp_free);
        }
    }
}

static void get_batteries(int rc, lsm_battery *bs[], uint32_t count,
                          Value &response) {
    uint32_t i = 0;

    if (LSM_ERR_OK == rc) {
        std::vector<Value> result;
        result.reserve(count);

        for (; i < count; ++i)
            result.push_back(battery_to_value(bs[i]));

        lsm_battery_record_array_free(bs, count);
        bs = NULL;
        response = Value(result);
    }
}

static int handle_batteries(lsm_plugin_ptr p, Value &params, Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    char *key = NULL;
    char *val = NULL;
    lsm_battery **bs = NULL;
    uint32_t count = 0;

    if (p && p->ops_v1_3 && p->ops_v1_3->battery_list) {

        if (LSM_FLAG_EXPECTED_TYPE(params) &&
            (rc = get_search_params(params, &key, &val)) == LSM_ERR_OK) {
            rc = p->ops_v1_3->battery_list(p, key, val, &bs, &count,
                                           LSM_FLAG_GET_VALUE(params));

            get_batteries(rc, bs, count, response);
            free(key);
            free(val);
        } else {
            if (rc == LSM_ERR_NO_SUPPORT) {
                rc = LSM_ERR_TRANSPORT_INVALID_ARG;
            }
        }
    }
    return rc;
}

CMP_FUNCTION(battery_compare_id, lsm_battery_id_get, lsm_battery)
CMP_FUNCTION(battery_compare_system, lsm_battery_system_id_get, lsm_battery)
CMP_FREE_FUNCTION(battery_free, lsm_battery_record_free, lsm_battery);

void lsm_plug_battery_search_filter(const char *search_key,
                                    const char *search_value, lsm_battery *bs[],
                                    uint32_t *count) {
    array_cmp cmp = NULL;

    if (search_key) {

        if (0 == strcmp("id", search_key)) {
            cmp = battery_compare_id;
        } else if (0 == strcmp("system_id", search_key)) {
            cmp = battery_compare_system;
        }

        if (cmp)
            *count = filter((void **)bs, *count, cmp, (void *)search_value,
                            battery_free);
    }
}

static int handle_volume_cache_info(lsm_plugin_ptr p, Value &params,
                                    Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    if (p && p->ops_v1_3 && p->ops_v1_3->vol_cache_info) {
        Value v_vol = params["volume"];

        if (IS_CLASS_VOLUME(v_vol) && LSM_FLAG_EXPECTED_TYPE(params)) {
            lsm_volume *vol = value_to_volume(v_vol);
            std::vector<Value> result;

            if (vol) {
                uint32_t write_cache_policy;
                uint32_t write_cache_status;
                uint32_t read_cache_policy;
                uint32_t read_cache_status;
                uint32_t physical_disk_cache;

                rc = p->ops_v1_3->vol_cache_info(
                    p, vol, &write_cache_policy, &write_cache_status,
                    &read_cache_policy, &read_cache_status,
                    &physical_disk_cache, LSM_FLAG_GET_VALUE(params));

                if (LSM_ERR_OK == rc) {
                    result.push_back(Value(write_cache_policy));
                    result.push_back(Value(write_cache_status));
                    result.push_back(Value(read_cache_policy));
                    result.push_back(Value(read_cache_status));
                    result.push_back(Value(physical_disk_cache));
                    response = Value(result);
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

static int handle_volume_pdc_update(lsm_plugin_ptr p, Value &params,
                                    Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    lsm_volume *lsm_vol = NULL;
    uint32_t pdc = LSM_VOLUME_PHYSICAL_DISK_CACHE_UNKNOWN;

    UNUSED(response);
    if (p && p->ops_v1_3 && p->ops_v1_3->vol_pdc_update) {
        Value v_vol = params["volume"];
        Value v_pdc = params["pdc"];

        if (Value::object_t == v_vol.valueType() &&
            Value::numeric_t == v_pdc.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_vol = value_to_volume(v_vol);
            pdc = v_pdc.asUint32_t();
            if ((pdc != LSM_VOLUME_PHYSICAL_DISK_CACHE_ENABLED) &&
                (pdc != LSM_VOLUME_PHYSICAL_DISK_CACHE_DISABLED)) {
                lsm_volume_record_free(lsm_vol);
                return LSM_ERR_INVALID_ARGUMENT;
            }

            rc = p->ops_v1_3->vol_pdc_update(p, lsm_vol, pdc,
                                             LSM_FLAG_GET_VALUE(params));

            lsm_volume_record_free(lsm_vol);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_volume_wcp_update(lsm_plugin_ptr p, Value &params,
                                    Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    lsm_volume *lsm_vol = NULL;
    uint32_t wcp = LSM_VOLUME_WRITE_CACHE_POLICY_UNKNOWN;

    UNUSED(response);
    if (p && p->ops_v1_3 && p->ops_v1_3->vol_wcp_update) {
        Value v_vol = params["volume"];
        Value v_wcp = params["wcp"];

        if (Value::object_t == v_vol.valueType() &&
            Value::numeric_t == v_wcp.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_vol = value_to_volume(v_vol);
            wcp = v_wcp.asUint32_t();
            if ((wcp != LSM_VOLUME_WRITE_CACHE_POLICY_WRITE_BACK) &&
                (wcp != LSM_VOLUME_WRITE_CACHE_POLICY_WRITE_THROUGH) &&
                (wcp != LSM_VOLUME_WRITE_CACHE_POLICY_AUTO)) {
                lsm_volume_record_free(lsm_vol);
                return LSM_ERR_INVALID_ARGUMENT;
            }

            rc = p->ops_v1_3->vol_wcp_update(p, lsm_vol, wcp,
                                             LSM_FLAG_GET_VALUE(params));

            lsm_volume_record_free(lsm_vol);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}

static int handle_volume_rcp_update(lsm_plugin_ptr p, Value &params,
                                    Value &response) {
    int rc = LSM_ERR_NO_SUPPORT;
    lsm_volume *lsm_vol = NULL;
    uint32_t rcp = LSM_VOLUME_READ_CACHE_POLICY_UNKNOWN;

    UNUSED(response);
    if (p && p->ops_v1_3 && p->ops_v1_3->vol_rcp_update) {
        Value v_vol = params["volume"];
        Value v_rcp = params["rcp"];

        if (Value::object_t == v_vol.valueType() &&
            Value::numeric_t == v_rcp.valueType() &&
            LSM_FLAG_EXPECTED_TYPE(params)) {

            lsm_vol = value_to_volume(v_vol);
            rcp = v_rcp.asUint32_t();
            if ((rcp != LSM_VOLUME_READ_CACHE_POLICY_ENABLED) &&
                (rcp != LSM_VOLUME_READ_CACHE_POLICY_DISABLED)) {
                lsm_volume_record_free(lsm_vol);
                return LSM_ERR_INVALID_ARGUMENT;
            }

            rc = p->ops_v1_3->vol_rcp_update(p, lsm_vol, rcp,
                                             LSM_FLAG_GET_VALUE(params));

            lsm_volume_record_free(lsm_vol);
        } else {
            rc = LSM_ERR_TRANSPORT_INVALID_ARG;
        }
    }
    return rc;
}
