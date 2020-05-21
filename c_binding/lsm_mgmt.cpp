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

#include "libstoragemgmt/libstoragemgmt.h"
#include "libstoragemgmt/libstoragemgmt_error.h"
#include "libstoragemgmt/libstoragemgmt_plug_interface.h"
#include "libstoragemgmt/libstoragemgmt_types.h"
#include <dirent.h>
#include <libxml/uri.h>
#include <stdio.h>
#include <string.h>

#include "lsm_convert.hpp"
#include "lsm_datatypes.hpp"

#define COUNT_OF(x)                                                            \
    ((sizeof(x) / sizeof(0 [x])) / ((size_t)(!(sizeof(x) % sizeof(0 [x])))))

static const char *const POOL_SEARCH_KEYS[] = {"id", "system_id"};

#define POOL_SEARCH_KEYS_COUNT COUNT_OF(POOL_SEARCH_KEYS)

static const char *const VOLUME_SEARCH_KEYS[] = {"id", "system_id", "pool_id"};
#define VOLUME_SEARCH_KEYS_COUNT COUNT_OF(VOLUME_SEARCH_KEYS)

static const char *const DISK_SEARCH_KEYS[] = {"id", "system_id"};

#define DISK_SEARCH_KEYS_COUNT COUNT_OF(DISK_SEARCH_KEYS)

static const char *const FS_SEARCH_KEYS[] = {"id", "system_id", "pool_id"};

#define FS_SEARCH_KEYS_COUNT COUNT_OF(FS_SEARCH_KEYS)

static const char *const NFS_EXPORT_SEARCH_KEYS[] = {"id", "fs_id"};

#define NFS_EXPORT_SEARCH_KEYS_COUNT COUNT_OF(NFS_EXPORT_SEARCH_KEYS)

static const char *const ACCESS_GROUP_SEARCH_KEYS[] = {"id", "system_id"};

#define ACCESS_GROUP_SEARCH_KEYS_COUNT COUNT_OF(ACCESS_GROUP_SEARCH_KEYS)

static const char *const TARGET_PORT_SEARCH_KEYS[] = {"id", "system_id"};

#define TARGET_PORT_SEARCH_KEYS_COUNT COUNT_OF(TARGET_PORT_SEARCH_KEYS)

static int get_battery_array(lsm_connect *c, int rc, Value &response,
                             lsm_battery **bs[], uint32_t *count);

/**
 * Common code to validate and initialize the connection.
 */
#define CONN_SETUP(c)                                                          \
    do {                                                                       \
        if (!LSM_IS_CONNECT(c)) {                                              \
            return LSM_ERR_INVALID_ARGUMENT;                                   \
        }                                                                      \
        lsm_error_free(c->error);                                              \
        c->error = NULL;                                                       \
    } while (0)

static int check_search_key(const char *search_key,
                            const char *const supported_keys[],
                            size_t supported_keys_count) {
    size_t i = 0;
    for (i = 0; i < supported_keys_count; ++i) {
        if (0 == strcmp(search_key, supported_keys[i])) {
            return 1;
        }
    }
    return 0;
}

int lsm_initiator_id_verify(const char *init_id,
                            lsm_access_group_init_type *init_type) {
    int rc = LSM_ERR_INVALID_ARGUMENT;

    if (init_id != NULL && strlen(init_id) > 3) {

        switch (*init_type) {
        case (LSM_ACCESS_GROUP_INIT_TYPE_UNKNOWN):
            if (0 == iqn_validate(init_id)) {
                *init_type = LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN;
                rc = LSM_ERR_OK;
            }
            if (0 == wwpn_validate(init_id)) {
                *init_type = LSM_ACCESS_GROUP_INIT_TYPE_WWPN;
                rc = LSM_ERR_OK;
            }
            break;
        case (LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN):
            if (0 == iqn_validate(init_id)) {
                *init_type = LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN;
                rc = LSM_ERR_OK;
            }
            break;
        case (LSM_ACCESS_GROUP_INIT_TYPE_WWPN):
            if (0 == wwpn_validate(init_id)) {
                *init_type = LSM_ACCESS_GROUP_INIT_TYPE_WWPN;
                rc = LSM_ERR_OK;
            }
            break;
        default:
            break;
        }
    }
    return rc;
}

int lsm_volume_vpd83_verify(const char *vpd83) {
    int rc = LSM_ERR_INVALID_ARGUMENT;
    size_t i;
    size_t vpd83_len;

    if (vpd83) {
        vpd83_len = strlen(vpd83);
        if ((vpd83_len == 32 && vpd83[0] == '6') ||
            (vpd83_len == 16 && vpd83[0] == '2') ||
            (vpd83_len == 16 && vpd83[0] == '3') ||
            (vpd83_len == 16 && vpd83[0] == '5')) {
            for (i = 0; i < vpd83_len; ++i) {
                char v = vpd83[i];
                //  0-9 || a-f is OK
                if (!((v >= 48 && v <= 57) || (v >= 97 && v <= 102))) {
                    return rc;
                }
            }
            rc = LSM_ERR_OK;
        }
    }
    return rc;
}

static int verify_initiator_id(const char *id, lsm_access_group_init_type t,
                               Value &initiator) {
    initiator = Value(id);

    if (t == LSM_ACCESS_GROUP_INIT_TYPE_WWPN) {
        char *wwpn = wwpn_convert(id);
        if (wwpn) {
            initiator = Value(wwpn);
            free(wwpn);
            wwpn = NULL;
        } else {
            return LSM_ERR_INVALID_ARGUMENT;
        }
    } else if (t == LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN) {
        if (iqn_validate(id)) {
            return LSM_ERR_INVALID_ARGUMENT;
        }
    }
    return LSM_ERR_OK;
}

/**
 * Strings are non null with a len >= 1
 */
#define CHECK_STR(x) (!(x) || !strlen(x))

/**
 * When we pass in a pointer for an out value we want to make sure that
 * the pointer isn't null, and that the dereferenced value is != NULL to prevent
 * memory leaks.
 */
#define CHECK_RP(x) (!(x) || *(x) != NULL)

int lsm_connect_password(const char *uri, const char *password,
                         lsm_connect **conn, uint32_t timeout, lsm_error_ptr *e,
                         lsm_flag flags) {
    int rc = LSM_ERR_OK;
    lsm_connect *c = NULL;

    /* Password is optional */
    if (CHECK_STR(uri) || CHECK_RP(conn) || !timeout || CHECK_RP(e) ||
        LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    c = connection_get();
    if (c) {
        c->uri = xmlParseURI(uri);
        if (c->uri && c->uri->scheme) {
            c->raw_uri = strdup(uri);
            if (c->raw_uri) {
                rc = driver_load(c, c->uri->scheme, password, timeout, e, 1,
                                 flags);
                if (rc == LSM_ERR_OK) {
                    *conn = (lsm_connect *)c;
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            rc = LSM_ERR_INVALID_ARGUMENT;
        }

        /*If we fail for any reason free resources associated with connection */
        if (rc != LSM_ERR_OK) {
            connection_free(c);
        }
    } else {
        rc = LSM_ERR_NO_MEMORY;
    }
    return rc;
}

static int lsm_error_log(lsm_connect *c, lsm_error_ptr error) {
    if (!LSM_IS_CONNECT(c) || !LSM_IS_ERROR(error)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (c->error) {
        lsm_error_free(c->error);
        c->error = NULL;
    }

    c->error = error;
    return LSM_ERR_OK;
}

static lsm_error_number log_exception(lsm_connect *c, lsm_error_number error,
                                      const char *message,
                                      const char *exception_msg) {
    lsm_error_ptr err =
        lsm_error_create(error, message, exception_msg, NULL, NULL, 0);
    if (err) {
        lsm_error_log(c, err);
    }
    return error;
}

static int rpc(lsm_connect *c, const char *method, const Value &parameters,
               Value &response) throw() {
    try {
        response = c->tp->rpc(method, parameters);
    } catch (const ValueException &ve) {
        return log_exception(c, LSM_ERR_TRANSPORT_SERIALIZATION,
                             "Serialization error", ve.what());
    } catch (const LsmException &le) {
        return log_exception(c, (lsm_error_number)le.error_code, le.what(),
                             NULL);
    } catch (const EOFException &eof) {
        return log_exception(c, LSM_ERR_TRANSPORT_COMMUNICATION, "Plug-in died",
                             "Check syslog");
    } catch (...) {
        return log_exception(c, LSM_ERR_LIB_BUG, "Unexpected exception",
                             "Unknown exception");
    }
    return LSM_ERR_OK;
}

static int job_check(lsm_connect *c, int rc, Value &response, char **job) {
    try {
        if (LSM_ERR_OK == rc) {
            // We get a value back, either null or job id.
            if (Value::string_t == response.valueType()) {
                *job = strdup(response.asString().c_str());

                if (*job) {
                    rc = LSM_ERR_JOB_STARTED;
                } else {
                    rc = LSM_ERR_NO_MEMORY;
                }
            } else {
                *job = NULL;
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Wrong type", ve.what());
    }
    return rc;
}

static int get_access_groups(lsm_connect *c, int rc, Value &response,
                             lsm_access_group **groups[], uint32_t *count) {
    try {
        if (LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
            rc = value_array_to_access_groups(response, groups, count);
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }
    return rc;
}

static int add_search_params(std::map<std::string, Value> &p, const char *k,
                             const char *v, const char *const supported_keys[],
                             size_t supported_keys_count) {
    if (k) {
        if (v) {
            if (!check_search_key(k, supported_keys, supported_keys_count)) {
                return LSM_ERR_UNSUPPORTED_SEARCH_KEY;
            }
        } else {
            return LSM_ERR_INVALID_ARGUMENT;
        }
    }
    p["search_key"] = Value(k);
    p["search_value"] = Value(v);
    return LSM_ERR_OK;
}

int lsm_connect_close(lsm_connect *c, lsm_flag flags) {
    CONN_SETUP(c);

    if (LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    // No response data needed on plugin_unregister
    int rc = rpc(c, "plugin_unregister", parameters, response);

    // Free the connection.
    connection_free(c);
    return rc;
}

static Value _create_flag_param(lsm_flag flags) {
    std::map<std::string, Value> p;
    p["flags"] = Value(flags);
    return Value(p);
}

int lsm_plugin_info_get(lsm_connect *c, char **desc, char **version,
                        lsm_flag flags) {
    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (CHECK_RP(desc) || CHECK_RP(version)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    try {

        Value parameters = _create_flag_param(flags);
        Value response;

        rc = rpc(c, "plugin_info", parameters, response);

        if (rc == LSM_ERR_OK) {
            std::vector<Value> j = response.asArray();
            *desc = strdup(j[0].asC_str());
            *version = strdup(j[1].asC_str());

            if (!*desc || !*version) {
                rc = LSM_ERR_NO_MEMORY;
                free(*desc);
                free(*version);
            }
        }
    } catch (const ValueException &ve) {
        free(*desc);
        *desc = NULL;
        free(*version);
        *version = NULL;
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }

    return rc;
}

int lsm_available_plugins_list(const char *sep, lsm_string_list **plugins,
                               lsm_flag flags) {
    int rc = LSM_ERR_OK;
    DIR *dirp = NULL;
    struct dirent *dp = NULL;
    lsm_connect *c = NULL;
    lsm_error_ptr e = NULL;
    char *desc = NULL;
    char *version = NULL;
    char *s = NULL;
    const char *uds_dir = uds_path();
    lsm_string_list *plugin_list = NULL;

    if (CHECK_STR(sep) || CHECK_RP(plugins) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    plugin_list = lsm_string_list_alloc(0);
    if (!plugin_list) {
        return LSM_ERR_NO_MEMORY;
    }

    dirp = opendir(uds_dir);
    if (dirp) {
        for (;;) {
            dp = readdir(dirp);
            if (NULL == dp) {
                break;
            }
            // Check to see if we have a socket
            if (DT_SOCK == dp->d_type) {
                c = connection_get();
                if (c) {
                    rc = driver_load(c, dp->d_name, NULL, 30000, &e, 0, 0);
                    if (LSM_ERR_OK == rc) {
                        // Get the plugin information
                        rc = lsm_plugin_info_get(c, &desc, &version, 0);
                        if (LSM_ERR_OK == rc) {
                            int format =
                                asprintf(&s, "%s%s%s", desc, sep, version);
                            free(desc);
                            desc = NULL;
                            free(version);
                            version = NULL;

                            if (-1 == format) {
                                rc = LSM_ERR_NO_MEMORY;
                                break;
                            }

                            rc = lsm_string_list_append(plugin_list, s);
                            free(s);
                            s = NULL;
                            if (LSM_ERR_OK != rc) {
                                break;
                            }
                        }
                    } else {
                        break;
                    }

                    connection_free(c);
                    c = NULL;
                }
            }
        } /* for(;;) */

        if (e) {
            lsm_error_free(e);
            e = NULL;
        }

        if (c) {
            connection_free(c);
            c = NULL;
        }

        if (-1 == closedir(dirp)) {
            // log the error
            rc = LSM_ERR_LIB_BUG;
        }

    } else { /* If dirp == NULL */
        // Log the error
        rc = LSM_ERR_LIB_BUG;
    }

    if (LSM_ERR_OK == rc) {
        *plugins = plugin_list;
    } else {
        lsm_string_list_free(plugin_list);
        plugin_list = NULL;
    }

    return rc;
}

int lsm_connect_timeout_set(lsm_connect *c, uint32_t timeout, lsm_flag flags) {
    CONN_SETUP(c);

    if (LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["ms"] = Value(timeout);
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    // No response data needed on set time out.
    return rpc(c, "time_out_set", parameters, response);
}

int lsm_connect_timeout_get(lsm_connect *c, uint32_t *timeout, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    try {

        Value parameters = _create_flag_param(flags);
        Value response;

        rc = rpc(c, "time_out_get", parameters, response);
        if (rc == LSM_ERR_OK) {
            *timeout = response.asUint32_t();
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }
    return rc;
}

static int job_status(lsm_connect *c, const char *job, lsm_job_status *status,
                      uint8_t *percentComplete, Value &returned_value,
                      lsm_flag flags) {
    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (!job || !status || !percentComplete) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    try {
        std::map<std::string, Value> p;
        p["job_id"] = Value(job);
        p["flags"] = Value(flags);
        Value parameters(p);
        Value response;

        rc = rpc(c, "job_status", parameters, response);
        if (LSM_ERR_OK == rc) {
            // We get back an array [status, percent, volume]
            std::vector<Value> j = response.asArray();
            *status = (lsm_job_status)j[0].asInt32_t();
            *percentComplete = (uint8_t)j[1].asUint32_t();

            returned_value = j[2];
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }
    return rc;
}

int lsm_job_status_get(lsm_connect *c, const char *job_id,
                       lsm_job_status *status, uint8_t *percentComplete,
                       lsm_flag flags) {
    CONN_SETUP(c);

    if (LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    Value rv;
    return job_status(c, job_id, status, percentComplete, rv, flags);
}

int lsm_job_status_pool_get(lsm_connect *c, const char *job,
                            lsm_job_status *status, uint8_t *percentComplete,
                            lsm_pool **pool, lsm_flag flags) {
    Value rv;
    int rc = LSM_ERR_OK;

    CONN_SETUP(c);

    if (CHECK_RP(pool) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    try {

        rc = job_status(c, job, status, percentComplete, rv, flags);

        if (LSM_ERR_OK == rc) {
            if (Value::object_t == rv.valueType()) {
                *pool = value_to_pool(rv);
                if (!(*pool)) {
                    rc = LSM_ERR_NO_MEMORY;
                }
            } else {
                *pool = NULL;
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }
    return rc;
}

/* TODO(Gris Ge): Should raise INVALID_ARGUMENT error when specified job does
 *                not a volume return job. The same issue also applies to
 *                lsm_job_status_pool_get() and etc.
 */
int lsm_job_status_volume_get(lsm_connect *c, const char *job,
                              lsm_job_status *status, uint8_t *percentComplete,
                              lsm_volume **vol, lsm_flag flags) {
    Value rv;
    int rc = LSM_ERR_OK;

    CONN_SETUP(c);

    if (CHECK_RP(vol) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    try {

        rc = job_status(c, job, status, percentComplete, rv, flags);

        if (LSM_ERR_OK == rc) {
            if (Value::object_t == rv.valueType()) {
                *vol = value_to_volume(rv);
                if (!(*vol)) {
                    rc = LSM_ERR_NO_MEMORY;
                }
            } else {
                *vol = NULL;
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }
    return rc;
}

int lsm_job_status_fs_get(lsm_connect *c, const char *job,
                          lsm_job_status *status, uint8_t *percentComplete,
                          lsm_fs **fs, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    Value rv;

    if (CHECK_RP(fs) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    try {

        rc = job_status(c, job, status, percentComplete, rv, flags);

        if (LSM_ERR_OK == rc) {
            if (Value::object_t == rv.valueType()) {
                *fs = value_to_fs(rv);
                if (!(*fs)) {
                    rc = LSM_ERR_NO_MEMORY;
                }
            } else {
                *fs = NULL;
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }
    return rc;
}

int lsm_job_status_ss_get(lsm_connect *c, const char *job,
                          lsm_job_status *status, uint8_t *percentComplete,
                          lsm_fs_ss **ss, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    Value rv;

    if (CHECK_RP(ss) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    try {

        rc = job_status(c, job, status, percentComplete, rv, flags);

        if (LSM_ERR_OK == rc) {
            if (Value::object_t == rv.valueType()) {
                *ss = value_to_ss(rv);
                if (!(*ss)) {
                    rc = LSM_ERR_NO_MEMORY;
                }
            } else {
                *ss = NULL;
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }
    return rc;
}

int lsm_job_free(lsm_connect *c, char **job, lsm_flag flags) {
    CONN_SETUP(c);

    if (job == NULL || strlen(*job) < 1 || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["job_id"] = Value(*job);
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    int rc = rpc(c, "job_free", parameters, response);

    if (LSM_ERR_OK == rc) {
        /* Free the memory for the job id */
        free(*job);
        *job = NULL;
    }
    return rc;
}

int lsm_capabilities(lsm_connect *c, lsm_system *system,
                     lsm_storage_capabilities **cap, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (!LSM_IS_SYSTEM(system) || CHECK_RP(cap) ||
        LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;

    p["system"] = system_to_value(system);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    try {
        rc = rpc(c, "capabilities", parameters, response);

        if (LSM_ERR_OK == rc && Value::object_t == response.valueType()) {
            *cap = value_to_capabilities(response);
            if (!(*cap)) {
                rc = LSM_ERR_NO_MEMORY;
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }

    return rc;
}

int lsm_pool_list(lsm_connect *c, char *search_key, char *search_value,
                  lsm_pool **poolArray[], uint32_t *count, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (!poolArray || !count || CHECK_RP(poolArray)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    *count = 0;
    *poolArray = NULL;

    try {
        std::map<std::string, Value> p;

        rc = add_search_params(p, search_key, search_value, POOL_SEARCH_KEYS,
                               POOL_SEARCH_KEYS_COUNT);
        if (LSM_ERR_OK != rc) {
            return rc;
        }

        p["flags"] = Value(flags);
        Value parameters(p);
        Value response;

        rc = rpc(c, "pools", parameters, response);
        if (LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
            std::vector<Value> pools = response.asArray();

            *count = pools.size();

            if (pools.size()) {
                *poolArray = lsm_pool_record_array_alloc(pools.size());

                for (size_t i = 0; i < pools.size(); ++i) {
                    (*poolArray)[i] = value_to_pool(pools[i]);
                    if (!(*poolArray)[i]) {
                        rc = LSM_ERR_NO_MEMORY;
                        goto error;
                    }
                }
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
        goto error;
    }

out:
    return rc;

error:
    if (*poolArray && *count) {
        lsm_pool_record_array_free(*poolArray, *count);
        *poolArray = NULL;
        *count = 0;
    }
    goto out;
}

int lsm_pool_member_info(lsm_connect *c, lsm_pool *pool,
                         lsm_volume_raid_type *raid_type,
                         lsm_pool_member_type *member_type,
                         lsm_string_list **member_ids, lsm_flag flags) {
    if (LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (!LSM_IS_POOL(pool)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (!raid_type || !member_type || !member_ids) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["pool"] = pool_to_value(pool);
    p["flags"] = Value(flags);
    Value parameters(p);
    try {

        Value response;

        rc = rpc(c, "pool_member_info", parameters, response);
        if (LSM_ERR_OK == rc) {
            std::vector<Value> j = response.asArray();
            *raid_type = (lsm_volume_raid_type)j[0].asInt32_t();
            *member_type = (lsm_pool_member_type)j[1].asInt32_t();
            *member_ids = NULL;
            if (Value::array_t == j[2].valueType()) {
                if (j[2].asArray().size()) {
                    *member_ids = value_to_string_list(j[2]);
                    if (*member_ids == NULL) {
                        return LSM_ERR_NO_MEMORY;
                    } else if (lsm_string_list_size(*member_ids) !=
                               j[2].asArray().size()) {

                        lsm_string_list_free(*member_ids);
                        return LSM_ERR_NO_MEMORY;
                    }
                }
            } else {
                rc = log_exception(c, LSM_ERR_PLUGIN_BUG,
                                   "member_ids data is not an array",
                                   "member_ids data is not an array");
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }
    return rc;
}

int lsm_target_port_list(lsm_connect *c, const char *search_key,
                         const char *search_value,
                         lsm_target_port **target_ports[], uint32_t *count,
                         lsm_flag flags) {
    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (!target_ports || !count || CHECK_RP(target_ports)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    try {
        std::map<std::string, Value> p;

        rc = add_search_params(p, search_key, search_value,
                               TARGET_PORT_SEARCH_KEYS,
                               TARGET_PORT_SEARCH_KEYS_COUNT);
        if (LSM_ERR_OK != rc) {
            return rc;
        }

        p["flags"] = Value(flags);
        Value parameters(p);
        Value response;

        rc = rpc(c, "target_ports", parameters, response);
        if (LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
            std::vector<Value> tp = response.asArray();

            *count = tp.size();

            if (tp.size()) {
                *target_ports = lsm_target_port_record_array_alloc(tp.size());

                for (size_t i = 0; i < tp.size(); ++i) {
                    (*target_ports)[i] = value_to_target_port(tp[i]);
                    if (!((*target_ports)[i])) {
                        rc = LSM_ERR_NO_MEMORY;
                        goto error;
                    }
                }
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
        goto error;
    }
out:
    return rc;

error:
    if (*target_ports && *count) {
        lsm_target_port_record_array_free(*target_ports, *count);
        *target_ports = NULL;
        *count = 0;
    }
    goto out;
}

static int get_volume_array(lsm_connect *c, int rc, Value &response,
                            lsm_volume **volumes[], uint32_t *count) {
    if (LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
        try {
            rc = value_array_to_volumes(response, volumes, count);
        } catch (const ValueException &ve) {
            rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Wrong type", ve.what());
        }
    }
    return rc;
}

int lsm_volume_list(lsm_connect *c, const char *search_key,
                    const char *search_value, lsm_volume **volumes[],
                    uint32_t *count, lsm_flag flags) {
    CONN_SETUP(c);

    if (!volumes || !count || CHECK_RP(volumes)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);

    int rc = add_search_params(p, search_key, search_value, VOLUME_SEARCH_KEYS,
                               VOLUME_SEARCH_KEYS_COUNT);
    if (LSM_ERR_OK != rc) {
        return rc;
    }

    Value parameters(p);
    Value response;

    rc = rpc(c, "volumes", parameters, response);
    return get_volume_array(c, rc, response, volumes, count);
}

static int get_disk_array(lsm_connect *c, int rc, Value &response,
                          lsm_disk **disks[], uint32_t *count) {
    if (LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
        rc = value_array_to_disks(response, disks, count);

        if (LSM_ERR_OK != rc) {
            rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", NULL);
        }
    }

    return rc;
}

int lsm_disk_list(lsm_connect *c, const char *search_key,
                  const char *search_value, lsm_disk **disks[], uint32_t *count,
                  lsm_flag flags) {
    CONN_SETUP(c);

    if (CHECK_RP(disks) || !count) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);

    int rc = add_search_params(p, search_key, search_value, DISK_SEARCH_KEYS,
                               DISK_SEARCH_KEYS_COUNT);
    if (LSM_ERR_OK != rc) {
        return rc;
    }

    Value parameters(p);
    Value response;

    rc = rpc(c, "disks", parameters, response);
    return get_disk_array(c, rc, response, disks, count);
}

typedef void *(*convert)(Value &v);

static void *parse_job_response(lsm_connect *c, Value response, int &rc,
                                char **job, convert conv) {
    void *val = NULL;
    *job = NULL;

    try {
        // We get an array back. first value is job, second is data of interest.
        if (Value::array_t == response.valueType()) {
            std::vector<Value> r = response.asArray();
            if (Value::string_t == r[0].valueType()) {
                *job = strdup((r[0].asString()).c_str());
                if (!(*job)) {
                    rc = LSM_ERR_NO_MEMORY;
                    goto error;
                }

                rc = LSM_ERR_JOB_STARTED;
            }
            if (Value::object_t == r[1].valueType()) {
                val = conv(r[1]);
                if (!val) {
                    rc = LSM_ERR_NO_MEMORY;
                    goto error;
                }
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
        goto error;
    }

out:
    return val;

error:
    free(*job);
    *job = NULL;
    goto out;
}

int lsm_volume_create(lsm_connect *c, lsm_pool *pool, const char *volumeName,
                      uint64_t size, lsm_volume_provision_type provisioning,
                      lsm_volume **newVolume, char **job, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_POOL(pool)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (CHECK_STR(volumeName) || !size || CHECK_RP(newVolume) ||
        CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["pool"] = pool_to_value(pool);
    p["volume_name"] = Value(volumeName);
    p["size_bytes"] = Value(size);
    p["provisioning"] = Value((int32_t)provisioning);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_create", parameters, response);
    if (LSM_ERR_OK == rc) {
        *newVolume = (lsm_volume *)parse_job_response(c, response, rc, job,
                                                      (convert)value_to_volume);
    }
    return rc;
}

int lsm_volume_resize(lsm_connect *c, lsm_volume *volume, uint64_t newSize,
                      lsm_volume **resizedVolume, char **job, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_VOL(volume) || !newSize || CHECK_RP(resizedVolume) ||
        CHECK_RP(job) || newSize == 0 || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }
    // If you try to resize to same size, we will return error.
    if ((newSize / volume->block_size) == volume->number_of_blocks) {
        return LSM_ERR_NO_STATE_CHANGE;
    }

    std::map<std::string, Value> p;
    p["volume"] = volume_to_value(volume);
    p["new_size_bytes"] = Value(newSize);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_resize", parameters, response);
    if (LSM_ERR_OK == rc) {
        *resizedVolume = (lsm_volume *)parse_job_response(
            c, response, rc, job, (convert)value_to_volume);
    }
    return rc;
}

int lsm_volume_replicate(lsm_connect *c, lsm_pool *pool,
                         lsm_replication_type repType, lsm_volume *volumeSrc,
                         const char *name, lsm_volume **newReplicant,
                         char **job, lsm_flag flags) {
    CONN_SETUP(c);

    if ((pool && !LSM_IS_POOL(pool)) || !LSM_IS_VOL(volumeSrc)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (CHECK_STR(name) || CHECK_RP(newReplicant) || CHECK_RP(job) ||
        LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["pool"] = pool_to_value(pool);
    p["rep_type"] = Value((int32_t)repType);
    p["volume_src"] = volume_to_value(volumeSrc);
    p["name"] = Value(name);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_replicate", parameters, response);
    if (LSM_ERR_OK == rc) {
        *newReplicant = (lsm_volume *)parse_job_response(
            c, response, rc, job, (convert)value_to_volume);
    }
    return rc;
}

int lsm_volume_replicate_range_block_size(lsm_connect *c, lsm_system *system,
                                          uint32_t *bs, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (!bs || LSM_FLAG_UNUSED_CHECK(flags) || !LSM_IS_SYSTEM(system)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    try {
        std::map<std::string, Value> p;
        p["system"] = system_to_value(system);
        p["flags"] = Value(flags);
        Value parameters(p);
        Value response;

        rc = rpc(c, "volume_replicate_range_block_size", parameters, response);
        if (LSM_ERR_OK == rc) {
            if (Value::numeric_t == response.valueType()) {
                *bs = response.asUint32_t();
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }
    return rc;
}

int lsm_volume_replicate_range(lsm_connect *c, lsm_replication_type repType,
                               lsm_volume *source, lsm_volume *dest,
                               lsm_block_range **ranges, uint32_t num_ranges,
                               char **job, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_VOL(source) || !LSM_IS_VOL(dest)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (!ranges || !num_ranges || CHECK_RP(job) ||
        LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["rep_type"] = Value((int32_t)repType);
    p["volume_src"] = volume_to_value(source);
    p["volume_dest"] = volume_to_value(dest);
    p["ranges"] = block_range_list_to_value(ranges, num_ranges);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_replicate_range", parameters, response);
    return job_check(c, rc, response, job);
}

static Value _create_volume_flag_param(lsm_volume *volume, lsm_flag flags) {
    std::map<std::string, Value> p;
    p["volume"] = volume_to_value(volume);
    p["flags"] = Value(flags);

    return Value(p);
}

int lsm_volume_delete(lsm_connect *c, lsm_volume *volume, char **job,
                      lsm_flag flags) {
    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (!LSM_IS_VOL(volume)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    try {

        Value parameters = _create_volume_flag_param(volume, flags);
        Value response;

        rc = rpc(c, "volume_delete", parameters, response);
        rc = job_check(c, rc, response, job);
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }
    return rc;
}

int lsm_volume_raid_info(lsm_connect *c, lsm_volume *volume,
                         lsm_volume_raid_type *raid_type, uint32_t *strip_size,
                         uint32_t *disk_count, uint32_t *min_io_size,
                         uint32_t *opt_io_size, lsm_flag flags) {
    if (LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (!LSM_IS_VOL(volume)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (!raid_type || !strip_size || !disk_count || !min_io_size ||
        !opt_io_size) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    try {

        Value parameters = _create_volume_flag_param(volume, flags);
        Value response;

        rc = rpc(c, "volume_raid_info", parameters, response);
        if (LSM_ERR_OK == rc) {
            // We get a value back, either null or job id.
            std::vector<Value> j = response.asArray();
            *raid_type = (lsm_volume_raid_type)j[0].asInt32_t();
            *strip_size = j[1].asUint32_t();
            *disk_count = j[2].asUint32_t();
            *min_io_size = j[3].asUint32_t();
            *opt_io_size = j[4].asUint32_t();
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }
    return rc;
}

int lsm_iscsi_chap_auth(lsm_connect *c, const char *init_id,
                        const char *username, const char *password,
                        const char *out_user, const char *out_password,
                        lsm_flag flags) {
    CONN_SETUP(c);

    if (iqn_validate(init_id) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["init_id"] = Value(init_id);
    p["in_user"] = Value(username);
    p["in_password"] = Value(password);
    p["out_user"] = Value(out_user);
    p["out_password"] = Value(out_password);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    return rpc(c, "iscsi_chap_auth", parameters, response);
}

static int online_offline(lsm_connect *c, lsm_volume *v, const char *operation,
                          lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_VOL(v)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["volume"] = volume_to_value(v);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;
    return rpc(c, operation, parameters, response);
}

int lsm_volume_enable(lsm_connect *c, lsm_volume *volume, lsm_flag flags) {
    return online_offline(c, volume, "volume_enable", flags);
}

int lsm_volume_disable(lsm_connect *c, lsm_volume *volume, lsm_flag flags) {
    return online_offline(c, volume, "volume_disable", flags);
}

int lsm_access_group_list(lsm_connect *c, const char *search_key,
                          const char *search_value, lsm_access_group **groups[],
                          uint32_t *groupCount, lsm_flag flags) {
    CONN_SETUP(c);

    if (!groups || !groupCount) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;

    int rc =
        add_search_params(p, search_key, search_value, ACCESS_GROUP_SEARCH_KEYS,
                          ACCESS_GROUP_SEARCH_KEYS_COUNT);
    if (LSM_ERR_OK != rc) {
        return rc;
    }

    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    rc = rpc(c, "access_groups", parameters, response);
    return get_access_groups(c, rc, response, groups, groupCount);
}

int lsm_access_group_create(lsm_connect *c, const char *name,
                            const char *init_id,
                            lsm_access_group_init_type init_type,
                            lsm_system *system, lsm_access_group **access_group,
                            lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_SYSTEM(system) || CHECK_STR(name) || CHECK_STR(init_id) ||
        CHECK_RP(access_group) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    Value id;

    if (LSM_ERR_OK != verify_initiator_id(init_id, init_type, id)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["name"] = Value(name);
    p["init_id"] = id;
    p["init_type"] = Value((int32_t)init_type);
    p["system"] = system_to_value(system);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    *access_group = NULL;

    int rc = rpc(c, "access_group_create", parameters, response);
    try {
        if (LSM_ERR_OK == rc) {
            // We should be getting a value back.
            if (Value::object_t == response.valueType()) {
                *access_group = value_to_access_group(response);
                if (!(*access_group)) {
                    rc = LSM_ERR_NO_MEMORY;
                }
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }
    return rc;
}

int lsm_access_group_delete(lsm_connect *c, lsm_access_group *access_group,
                            lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_ACCESS_GROUP(access_group) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["access_group"] = access_group_to_value(access_group);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    return rpc(c, "access_group_delete", parameters, response);
}

static int _lsm_ag_add_delete(lsm_connect *c, lsm_access_group *access_group,
                              const char *init_id,
                              lsm_access_group_init_type init_type,
                              lsm_access_group **updated_access_group,
                              lsm_flag flags, const char *message) {
    CONN_SETUP(c);

    if (!LSM_IS_ACCESS_GROUP(access_group) || CHECK_STR(init_id) ||
        LSM_FLAG_UNUSED_CHECK(flags) || CHECK_RP(updated_access_group)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    Value id;

    if (LSM_ERR_OK != verify_initiator_id(init_id, init_type, id)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["access_group"] = access_group_to_value(access_group);
    p["init_id"] = id;
    p["init_type"] = Value((int32_t)init_type);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, message, parameters, response);
    try {
        if (LSM_ERR_OK == rc) {
            // We should be getting a value back.
            if (Value::object_t == response.valueType()) {
                *updated_access_group = value_to_access_group(response);
                if (!(*updated_access_group)) {
                    rc = LSM_ERR_NO_MEMORY;
                }
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }

    return rc;
}

int lsm_access_group_initiator_add(lsm_connect *c,
                                   lsm_access_group *access_group,
                                   const char *init_id,
                                   lsm_access_group_init_type init_type,
                                   lsm_access_group **updated_access_group,
                                   lsm_flag flags) {
    return _lsm_ag_add_delete(c, access_group, init_id, init_type,
                              updated_access_group, flags,
                              "access_group_initiator_add");
}

int lsm_access_group_initiator_delete(lsm_connect *c,
                                      lsm_access_group *access_group,
                                      const char *init_id,
                                      lsm_access_group_init_type init_type,
                                      lsm_access_group **updated_access_group,
                                      lsm_flag flags) {
    return _lsm_ag_add_delete(c, access_group, init_id, init_type,
                              updated_access_group, flags,
                              "access_group_initiator_delete");
}

int lsm_volume_mask(lsm_connect *c, lsm_access_group *access_group,
                    lsm_volume *volume, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_ACCESS_GROUP(access_group) || !LSM_IS_VOL(volume) ||
        LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["access_group"] = access_group_to_value(access_group);
    p["volume"] = volume_to_value(volume);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    return rpc(c, "volume_mask", parameters, response);
}

int lsm_volume_unmask(lsm_connect *c, lsm_access_group *group,
                      lsm_volume *volume, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_ACCESS_GROUP(group) || !LSM_IS_VOL(volume) ||
        LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["access_group"] = access_group_to_value(group);
    p["volume"] = volume_to_value(volume);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    return rpc(c, "volume_unmask", parameters, response);
}

int lsm_volumes_accessible_by_access_group(lsm_connect *c,
                                           lsm_access_group *group,
                                           lsm_volume **volumes[],
                                           uint32_t *count, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (!LSM_IS_ACCESS_GROUP(group) || !volumes || !count ||
        LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    try {
        std::map<std::string, Value> p;
        p["access_group"] = access_group_to_value(group);
        p["flags"] = Value(flags);

        Value parameters(p);
        Value response;

        rc = rpc(c, "volumes_accessible_by_access_group", parameters, response);
        if (LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
            std::vector<Value> vol = response.asArray();

            *count = vol.size();

            if (vol.size()) {
                *volumes = lsm_volume_record_array_alloc(vol.size());

                if (*volumes) {
                    for (size_t i = 0; i < vol.size(); ++i) {
                        (*volumes)[i] = value_to_volume(vol[i]);
                        if (!((*volumes)[i])) {
                            rc = LSM_ERR_NO_MEMORY;
                            goto error;
                        }
                    }
                } else {
                    rc = LSM_ERR_NO_MEMORY;
                }
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
        goto error;
    }

out:
    return rc;

error:
    if (*volumes && *count) {
        lsm_volume_record_array_free(*volumes, *count);
        *volumes = NULL;
        *count = 0;
    }
    goto out;
}

int lsm_access_groups_granted_to_volume(lsm_connect *c, lsm_volume *volume,
                                        lsm_access_group **groups[],
                                        uint32_t *groupCount, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_VOL(volume)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (!groups || !groupCount || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["volume"] = volume_to_value(volume);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "access_groups_granted_to_volume", parameters, response);
    return get_access_groups(c, rc, response, groups, groupCount);
}

static int _retrieve_bool(int rc, Value &response, uint8_t *yes) {
    int rc_out = rc;

    *yes = 0;

    if (LSM_ERR_OK == rc) {
        // We should be getting a boolean value back.
        if (Value::boolean_t == response.valueType()) {
            if (response.asBool()) {
                *yes = 1;
            }
        } else {
            rc_out = LSM_ERR_PLUGIN_BUG;
        }
    }
    return rc_out;
}

int lsm_volume_child_dependency(lsm_connect *c, lsm_volume *volume,
                                uint8_t *yes, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (!LSM_IS_VOL(volume)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (!yes || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    try {

        Value parameters = _create_volume_flag_param(volume, flags);
        Value response;

        rc = rpc(c, "volume_child_dependency", parameters, response);
        rc = _retrieve_bool(rc, response, yes);
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }
    return rc;
}

int lsm_volume_child_dependency_delete(lsm_connect *c, lsm_volume *volume,
                                       char **job, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_VOL(volume) || CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    Value parameters = _create_volume_flag_param(volume, flags);
    Value response;

    int rc = rpc(c, "volume_child_dependency_rm", parameters, response);
    return job_check(c, rc, response, job);
}

int lsm_system_list(lsm_connect *c, lsm_system **systems[],
                    uint32_t *systemCount, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (!systems || !systemCount) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    try {
        std::map<std::string, Value> p;
        p["flags"] = Value(flags);
        Value parameters(p);
        Value response;

        rc = rpc(c, "systems", parameters, response);
        if (LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
            std::vector<Value> sys = response.asArray();

            *systemCount = sys.size();

            if (sys.size()) {
                *systems = lsm_system_record_array_alloc(sys.size());

                if (*systems) {
                    for (size_t i = 0; i < sys.size(); ++i) {
                        (*systems)[i] = value_to_system(sys[i]);
                        if (!(*systems)[i]) {
                            rc = LSM_ERR_NO_MEMORY;
                            goto error;
                        }
                    }
                } else {
                    rc = LSM_ERR_NO_MEMORY;
                }
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
        goto error;
    }
out:
    return rc;

error:
    if (*systems) {
        lsm_system_record_array_free(*systems, *systemCount);
        *systems = NULL;
        *systemCount = 0;
    }
    goto out;
}

int lsm_fs_list(lsm_connect *c, const char *search_key,
                const char *search_value, lsm_fs **fs[], uint32_t *fsCount,
                lsm_flag flags) {
    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (!fs || !fsCount) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    try {
        std::map<std::string, Value> p;

        rc = add_search_params(p, search_key, search_value, FS_SEARCH_KEYS,
                               FS_SEARCH_KEYS_COUNT);
        if (LSM_ERR_OK != rc) {
            return rc;
        }

        p["flags"] = Value(flags);
        Value parameters(p);
        Value response;

        rc = rpc(c, "fs", parameters, response);
        if (LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
            std::vector<Value> sys = response.asArray();

            *fsCount = sys.size();

            if (sys.size()) {
                *fs = lsm_fs_record_array_alloc(sys.size());

                if (*fs) {
                    for (size_t i = 0; i < sys.size(); ++i) {
                        (*fs)[i] = value_to_fs(sys[i]);
                        if (!((*fs)[i])) {
                            rc = LSM_ERR_NO_MEMORY;
                            goto error;
                        }
                    }
                } else {
                    rc = LSM_ERR_NO_MEMORY;
                }
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
        goto error;
    }
out:
    return rc;

error:
    if (*fs && *fsCount) {
        lsm_fs_record_array_free(*fs, *fsCount);
        *fs = NULL;
        *fsCount = 0;
    }
    goto out;
}

int lsm_fs_create(lsm_connect *c, lsm_pool *pool, const char *name,
                  uint64_t size_bytes, lsm_fs **fs, char **job,
                  lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_POOL(pool)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (CHECK_STR(name) || !size_bytes || CHECK_RP(fs) || CHECK_RP(job) ||
        LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["pool"] = pool_to_value(pool);
    p["name"] = Value(name);
    p["size_bytes"] = Value(size_bytes);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs_create", parameters, response);
    if (LSM_ERR_OK == rc) {
        *fs = (lsm_fs *)parse_job_response(c, response, rc, job,
                                           (convert)value_to_fs);
    }
    return rc;
}

static Value _create_fs_flag_param(lsm_fs *fs, lsm_flag flags) {
    std::map<std::string, Value> p;
    p["fs"] = fs_to_value(fs);
    p["flags"] = Value(flags);
    return Value(p);
}

int lsm_fs_delete(lsm_connect *c, lsm_fs *fs, char **job, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_FS(fs) || CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    Value parameters = _create_fs_flag_param(fs, flags);
    Value response;

    int rc = rpc(c, "fs_delete", parameters, response);
    return job_check(c, rc, response, job);
}

int lsm_fs_resize(lsm_connect *c, lsm_fs *fs, uint64_t new_size_bytes,
                  lsm_fs **rfs, char **job, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_FS(fs) || !new_size_bytes || CHECK_RP(rfs) || CHECK_RP(job) ||
        LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["fs"] = fs_to_value(fs);
    p["new_size_bytes"] = Value(new_size_bytes);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs_resize", parameters, response);
    if (LSM_ERR_OK == rc) {
        *rfs = (lsm_fs *)parse_job_response(c, response, rc, job,
                                            (convert)value_to_fs);
    }
    return rc;
}

int lsm_fs_clone(lsm_connect *c, lsm_fs *src_fs, const char *name,
                 lsm_fs_ss *optional_ss, lsm_fs **cloned_fs, char **job,
                 lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_FS(src_fs) || CHECK_STR(name) || CHECK_RP(cloned_fs) ||
        CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["src_fs"] = fs_to_value(src_fs);
    p["dest_fs_name"] = Value(name);
    p["snapshot"] = ss_to_value(optional_ss);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs_clone", parameters, response);
    if (LSM_ERR_OK == rc) {
        *cloned_fs = (lsm_fs *)parse_job_response(c, response, rc, job,
                                                  (convert)value_to_fs);
    }
    return rc;
}

int lsm_fs_file_clone(lsm_connect *c, lsm_fs *fs, const char *src_file_name,
                      const char *dest_file_name, lsm_fs_ss *snapshot,
                      char **job, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_FS(fs) || CHECK_STR(src_file_name) ||
        CHECK_STR(dest_file_name) || CHECK_RP(job) ||
        LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["fs"] = fs_to_value(fs);
    p["src_file_name"] = Value(src_file_name);
    p["dest_file_name"] = Value(dest_file_name);
    p["snapshot"] = ss_to_value(snapshot);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs_file_clone", parameters, response);
    return job_check(c, rc, response, job);
}

static Value _create_fs_file_flag_params(lsm_fs *fs, lsm_string_list *files,
                                         lsm_flag flags) {
    std::map<std::string, Value> p;
    p["fs"] = fs_to_value(fs);
    p["files"] = string_list_to_value(files);
    p["flags"] = Value(flags);
    return Value(p);
}

int lsm_fs_child_dependency(lsm_connect *c, lsm_fs *fs, lsm_string_list *files,
                            uint8_t *yes, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (!LSM_IS_FS(fs)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (files) {
        if (!LSM_IS_STRING_LIST(files)) {
            return LSM_ERR_INVALID_ARGUMENT;
        }
    }

    if (!yes || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    try {

        Value parameters = _create_fs_file_flag_params(fs, files, flags);
        Value response;

        rc = rpc(c, "fs_child_dependency", parameters, response);
        rc = _retrieve_bool(rc, response, yes);
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }
    return rc;
}

int lsm_fs_child_dependency_delete(lsm_connect *c, lsm_fs *fs,
                                   lsm_string_list *files, char **job,
                                   lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_FS(fs)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (files) {
        if (!LSM_IS_STRING_LIST(files)) {
            return LSM_ERR_INVALID_ARGUMENT;
        }
    }

    if (CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    Value parameters = _create_fs_file_flag_params(fs, files, flags);
    Value response;

    int rc = rpc(c, "fs_child_dependency_rm", parameters, response);
    return job_check(c, rc, response, job);
}

int lsm_fs_ss_list(lsm_connect *c, lsm_fs *fs, lsm_fs_ss **ss[],
                   uint32_t *ssCount, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (!LSM_IS_FS(fs)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (CHECK_RP(ss) || !ssCount) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    Value parameters = _create_fs_flag_param(fs, flags);
    Value response;

    try {
        rc = rpc(c, "fs_snapshots", parameters, response);
        if (LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
            std::vector<Value> sys = response.asArray();

            *ssCount = sys.size();

            if (sys.size()) {
                *ss = lsm_fs_ss_record_array_alloc(sys.size());

                if (*ss) {
                    for (size_t i = 0; i < sys.size(); ++i) {
                        (*ss)[i] = value_to_ss(sys[i]);
                        if (!((*ss)[i])) {
                            rc = LSM_ERR_NO_MEMORY;
                            goto error;
                        }
                    }
                } else {
                    rc = LSM_ERR_NO_MEMORY;
                }
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
        goto error;
    }
out:
    return rc;

error:
    if (*ss && *ssCount) {
        lsm_fs_ss_record_array_free(*ss, *ssCount);
        *ss = NULL;
        *ssCount = 0;
    }

    goto out;
}

int lsm_fs_ss_create(lsm_connect *c, lsm_fs *fs, const char *name,
                     lsm_fs_ss **snapshot, char **job, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_FS(fs)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (CHECK_STR(name) || CHECK_RP(snapshot) || CHECK_RP(job) ||
        LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["fs"] = fs_to_value(fs);
    p["snapshot_name"] = Value(name);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs_snapshot_create", parameters, response);
    if (LSM_ERR_OK == rc) {
        *snapshot = (lsm_fs_ss *)parse_job_response(c, response, rc, job,
                                                    (convert)value_to_ss);
    }
    return rc;
}

int lsm_fs_ss_delete(lsm_connect *c, lsm_fs *fs, lsm_fs_ss *ss, char **job,
                     lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_FS(fs)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (!LSM_IS_SS(ss)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["fs"] = fs_to_value(fs);
    p["snapshot"] = ss_to_value(ss);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs_snapshot_delete", parameters, response);
    return job_check(c, rc, response, job);
}

int lsm_fs_ss_restore(lsm_connect *c, lsm_fs *fs, lsm_fs_ss *ss,
                      lsm_string_list *files, lsm_string_list *restore_files,
                      int all_files, char **job, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_FS(fs)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (!LSM_IS_SS(ss)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (files) {
        if (!LSM_IS_STRING_LIST(files)) {
            return LSM_ERR_INVALID_ARGUMENT;
        }
    }

    if (restore_files) {
        if (!LSM_IS_STRING_LIST(restore_files)) {
            return LSM_ERR_INVALID_ARGUMENT;
        }
    }

    if (CHECK_RP(job) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["fs"] = fs_to_value(fs);
    p["snapshot"] = ss_to_value(ss);
    p["files"] = string_list_to_value(files);
    p["restore_files"] = string_list_to_value(restore_files);
    p["all_files"] = Value((all_files) ? true : false);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "fs_snapshot_restore", parameters, response);
    return job_check(c, rc, response, job);
}

int lsm_nfs_list(lsm_connect *c, const char *search_key,
                 const char *search_value, lsm_nfs_export **exports[],
                 uint32_t *count, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if (CHECK_RP(exports) || !count) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    *count = 0;
    *exports = NULL;

    try {
        std::map<std::string, Value> p;

        rc = add_search_params(p, search_key, search_value,
                               NFS_EXPORT_SEARCH_KEYS,
                               NFS_EXPORT_SEARCH_KEYS_COUNT);
        if (LSM_ERR_OK != rc) {
            return rc;
        }

        p["flags"] = Value(flags);
        Value parameters(p);
        Value response;

        rc = rpc(c, "exports", parameters, response);
        if (LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
            std::vector<Value> exps = response.asArray();

            *count = exps.size();

            if (*count) {
                *exports = lsm_nfs_export_record_array_alloc(*count);

                if (*exports) {
                    for (size_t i = 0; i < *count; ++i) {
                        (*exports)[i] = value_to_nfs_export(exps[i]);
                        if (!((*exports)[i])) {
                            rc = LSM_ERR_NO_MEMORY;
                            goto error;
                        }
                    }
                } else {
                    rc = LSM_ERR_NO_MEMORY;
                }
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
        goto error;
    }
out:
    return rc;

error:
    if (*exports && *count) {
        lsm_nfs_export_record_array_free(*exports, *count);
        *exports = NULL;
        *count = 0;
    }
    goto out;
}

int lsm_nfs_export_fs(lsm_connect *c, const char *fs_id,
                      const char *export_path, lsm_string_list *root_list,
                      lsm_string_list *rw_list, lsm_string_list *ro_list,
                      uint64_t anon_uid, uint64_t anon_gid,
                      const char *auth_type, const char *options,
                      lsm_nfs_export **exported, lsm_flag flags) {
    CONN_SETUP(c);

    if (root_list) {
        if (!LSM_IS_STRING_LIST(root_list)) {
            return LSM_ERR_INVALID_ARGUMENT;
        }
    }

    if (rw_list) {
        if (!LSM_IS_STRING_LIST(rw_list)) {
            return LSM_ERR_INVALID_ARGUMENT;
        }
    }

    if (ro_list) {
        if (!LSM_IS_STRING_LIST(ro_list)) {
            return LSM_ERR_INVALID_ARGUMENT;
        }
    }

    if (CHECK_STR(fs_id) || CHECK_RP(exported) ||
        !(root_list || rw_list || ro_list) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;

    p["fs_id"] = Value(fs_id);
    p["export_path"] = Value(export_path);
    p["root_list"] = string_list_to_value(root_list);
    p["rw_list"] = string_list_to_value(rw_list);
    p["ro_list"] = string_list_to_value(ro_list);
    if ((int64_t)anon_uid == LSM_NFS_EXPORT_ANON_UID_GID_NA)
        p["anon_uid"] = Value(LSM_NFS_EXPORT_ANON_UID_GID_NA);
    else if ((int64_t)anon_uid == LSM_NFS_EXPORT_ANON_UID_GID_ERROR)
        p["anon_uid"] = Value(LSM_NFS_EXPORT_ANON_UID_GID_ERROR);
    else
        p["anon_uid"] = Value(anon_uid);
    if ((int64_t)anon_gid == LSM_NFS_EXPORT_ANON_UID_GID_NA)
        p["anon_gid"] = Value(LSM_NFS_EXPORT_ANON_UID_GID_NA);
    else if ((int64_t)anon_gid == LSM_NFS_EXPORT_ANON_UID_GID_ERROR)
        p["anon_gid"] = Value(LSM_NFS_EXPORT_ANON_UID_GID_ERROR);
    else
        p["anon_gid"] = Value(anon_gid);
    p["auth_type"] = Value(auth_type);
    p["options"] = Value(options);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "export_fs", parameters, response);
    try {
        if (LSM_ERR_OK == rc && Value::object_t == response.valueType()) {
            *exported = value_to_nfs_export(response);
            if (!(*exported)) {
                rc = LSM_ERR_NO_MEMORY;
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }
    return rc;
}

int lsm_nfs_export_delete(lsm_connect *c, lsm_nfs_export *e, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_NFS_EXPORT(e) || LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["export"] = nfs_export_to_value(e);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "export_remove", parameters, response);
    return rc;
}

int lsm_volume_raid_create_cap_get(lsm_connect *c, lsm_system *system,
                                   uint32_t **supported_raid_types,
                                   uint32_t *supported_raid_type_count,
                                   uint32_t **supported_strip_sizes,
                                   uint32_t *supported_strip_size_count,
                                   lsm_flag flags) {
    CONN_SETUP(c);

    if (!supported_raid_types || !supported_raid_type_count ||
        !supported_strip_sizes || !supported_strip_size_count) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    *supported_raid_types = NULL;

    std::map<std::string, Value> p;
    p["system"] = system_to_value(system);
    p["flags"] = Value(flags);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_raid_create_cap_get", parameters, response);
    try {
        std::vector<Value> j = response.asArray();

        rc = values_to_uint32_array(j[0], supported_raid_types,
                                    supported_raid_type_count);

        if (rc != LSM_ERR_OK) {
            goto error;
        }

        rc = values_to_uint32_array(j[1], supported_strip_sizes,
                                    supported_strip_size_count);
        if (rc != LSM_ERR_OK) {
            goto error;
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }

out:
    return rc;

error:
    free(*supported_raid_types);
    *supported_raid_types = NULL;
    *supported_raid_type_count = 0;
    *supported_strip_sizes = NULL;
    *supported_strip_size_count = 0;
    goto out;
}

int lsm_volume_raid_create(lsm_connect *c, const char *name,
                           lsm_volume_raid_type raid_type, lsm_disk *disks[],
                           uint32_t disk_count, uint32_t strip_size,
                           lsm_volume **new_volume, lsm_flag flags) {
    CONN_SETUP(c);

    if (disk_count == 0) {
        return log_exception(c, LSM_ERR_INVALID_ARGUMENT,
                             "Require at least one disks", NULL);
    }

    if (raid_type == LSM_VOLUME_RAID_TYPE_RAID1 && disk_count != 2) {
        return log_exception(c, LSM_ERR_INVALID_ARGUMENT,
                             "RAID 1 only allows two disks", NULL);
    }

    if (raid_type == LSM_VOLUME_RAID_TYPE_RAID5 && disk_count < 3) {
        return log_exception(c, LSM_ERR_INVALID_ARGUMENT,
                             "RAID 5 require 3 or more disks", NULL);
    }

    if (raid_type == LSM_VOLUME_RAID_TYPE_RAID6 && disk_count < 4) {
        return log_exception(c, LSM_ERR_INVALID_ARGUMENT,
                             "RAID 5 require 4 or more disks", NULL);
    }

    if (disk_count % 2) {
        if (raid_type == LSM_VOLUME_RAID_TYPE_RAID10 && disk_count < 4) {
            return log_exception(c, LSM_ERR_INVALID_ARGUMENT,
                                 "RAID 10 require even disks count and 4 "
                                 "or more disks",
                                 NULL);
        }
        if (raid_type == LSM_VOLUME_RAID_TYPE_RAID50 && disk_count < 6) {
            return log_exception(c, LSM_ERR_INVALID_ARGUMENT,
                                 "RAID 50 require even disks count and 6 or "
                                 "more disks",
                                 NULL);
        }
        if (raid_type == LSM_VOLUME_RAID_TYPE_RAID60 && disk_count < 8) {
            return log_exception(c, LSM_ERR_INVALID_ARGUMENT,
                                 "RAID 60 require even disks count and 8 or "
                                 "more disks",
                                 NULL);
        }
    }

    if (CHECK_RP(new_volume)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["name"] = Value(name);
    p["raid_type"] = Value((int32_t)raid_type);
    p["strip_size"] = Value((int32_t)strip_size);
    p["flags"] = Value(flags);
    std::vector<Value> disks_value;
    disks_value.reserve(disk_count);
    for (uint32_t i = 0; i < disk_count; i++) {
        disks_value.push_back(disk_to_value(disks[i]));
    }
    p["disks"] = disks_value;

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_raid_create", parameters, response);
    try {
        if (LSM_ERR_OK == rc) {
            *new_volume = value_to_volume(response);
            if (!(*new_volume)) {
                rc = LSM_ERR_NO_MEMORY;
            }
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }
    return rc;
}

int lsm_volume_ident_led_on(lsm_connect *c, lsm_volume *volume,
                            lsm_flag flags) {
    CONN_SETUP(c);

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);
    p["volume"] = volume_to_value(volume);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_ident_led_on", parameters, response);

    return rc;
}

int lsm_volume_ident_led_off(lsm_connect *c, lsm_volume *volume,
                             lsm_flag flags) {
    CONN_SETUP(c);

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);
    p["volume"] = volume_to_value(volume);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "volume_ident_led_off", parameters, response);

    return rc;
}

int lsm_system_read_cache_pct_update(lsm_connect *c, lsm_system *system,
                                     uint32_t read_pct, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_SYSTEM(system))
        return log_exception(c, LSM_ERR_INVALID_ARGUMENT,
                             "Invalid argument: system", NULL);

    if (LSM_FLAG_UNUSED_CHECK(flags))
        return log_exception(c, LSM_ERR_INVALID_ARGUMENT,
                             "Invalid argument: flags", NULL);

    if (read_pct > 100)
        return log_exception(c, LSM_ERR_INVALID_ARGUMENT,
                             "Invalid argument: read_pct, "
                             "should >=0 and <= 100",
                             NULL);

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);
    p["read_pct"] = Value((int32_t)read_pct);
    p["system"] = system_to_value(system);

    Value parameters(p);
    Value response;

    int rc = rpc(c, "system_read_cache_pct_update", parameters, response);

    return rc;
}

static int get_battery_array(lsm_connect *c, int rc, Value &response,
                             lsm_battery **bs[], uint32_t *count) {
    if (LSM_ERR_OK == rc && Value::array_t == response.valueType()) {
        try {
            rc = value_array_to_batteries(response, bs, count);
        } catch (const ValueException &ve) {
            rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", NULL);
        }
    }

    return rc;
}

int lsm_battery_list(lsm_connect *c, const char *search_key,
                     const char *search_value, lsm_battery **bs[],
                     uint32_t *count, lsm_flag flags) {
    CONN_SETUP(c);

    if (CHECK_RP(bs) || !count) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["flags"] = Value(flags);

    int rc = add_search_params(p, search_key, search_value, DISK_SEARCH_KEYS,
                               DISK_SEARCH_KEYS_COUNT);
    if (LSM_ERR_OK != rc) {
        return rc;
    }

    Value parameters(p);
    Value response;

    rc = rpc(c, "batteries", parameters, response);
    return get_battery_array(c, rc, response, bs, count);
}

int lsm_volume_cache_info(lsm_connect *c, lsm_volume *volume,
                          uint32_t *write_cache_policy,
                          uint32_t *write_cache_status,
                          uint32_t *read_cache_policy,
                          uint32_t *read_cache_status,
                          uint32_t *physical_disk_cache, lsm_flag flags) {
    if (LSM_FLAG_UNUSED_CHECK(flags)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    int rc = LSM_ERR_OK;
    CONN_SETUP(c);

    if ((!LSM_IS_VOL(volume)) || (write_cache_policy == NULL) ||
        (write_cache_status == NULL) || (read_cache_policy == NULL) ||
        (read_cache_status == NULL) || (physical_disk_cache == NULL))
        return LSM_ERR_INVALID_ARGUMENT;

    try {

        Value parameters = _create_volume_flag_param(volume, flags);
        Value response;

        rc = rpc(c, "volume_cache_info", parameters, response);
        if (LSM_ERR_OK == rc) {
            std::vector<Value> j = response.asArray();
            *write_cache_policy = j[0].asUint32_t();
            *write_cache_status = j[1].asUint32_t();
            *read_cache_policy = j[2].asUint32_t();
            *read_cache_status = j[3].asUint32_t();
            *physical_disk_cache = j[4].asUint32_t();
        }
    } catch (const ValueException &ve) {
        rc = log_exception(c, LSM_ERR_PLUGIN_BUG, "Unexpected type", ve.what());
    }

    if (rc != LSM_ERR_OK) {
        *write_cache_policy = LSM_VOLUME_WRITE_CACHE_POLICY_UNKNOWN;
        *write_cache_status = LSM_VOLUME_WRITE_CACHE_STATUS_UNKNOWN;
        *read_cache_policy = LSM_VOLUME_READ_CACHE_POLICY_UNKNOWN;
        *read_cache_status = LSM_VOLUME_READ_CACHE_STATUS_UNKNOWN;
        *physical_disk_cache = LSM_VOLUME_PHYSICAL_DISK_CACHE_UNKNOWN;
    }

    return rc;
}

int lsm_volume_physical_disk_cache_update(lsm_connect *c, lsm_volume *volume,
                                          uint32_t pdc, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_VOL(volume) || LSM_FLAG_UNUSED_CHECK(flags) ||
        ((pdc != LSM_VOLUME_PHYSICAL_DISK_CACHE_DISABLED) &&
         (pdc != LSM_VOLUME_PHYSICAL_DISK_CACHE_ENABLED))) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["volume"] = volume_to_value(volume);
    p["pdc"] = Value(pdc);
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    // No response data.
    return rpc(c, "volume_physical_disk_cache_update", parameters, response);
}

int lsm_volume_write_cache_policy_update(lsm_connect *c, lsm_volume *volume,
                                         uint32_t wcp, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_VOL(volume) || LSM_FLAG_UNUSED_CHECK(flags) ||
        ((wcp != LSM_VOLUME_WRITE_CACHE_POLICY_AUTO) &&
         (wcp != LSM_VOLUME_WRITE_CACHE_POLICY_WRITE_BACK) &&
         (wcp != LSM_VOLUME_WRITE_CACHE_POLICY_WRITE_THROUGH))) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["volume"] = volume_to_value(volume);
    p["wcp"] = Value(wcp);
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    // No response data.
    return rpc(c, "volume_write_cache_policy_update", parameters, response);
}

int lsm_volume_read_cache_policy_update(lsm_connect *c, lsm_volume *volume,
                                        uint32_t rcp, lsm_flag flags) {
    CONN_SETUP(c);

    if (!LSM_IS_VOL(volume) || LSM_FLAG_UNUSED_CHECK(flags) ||
        ((rcp != LSM_VOLUME_READ_CACHE_POLICY_DISABLED) &&
         (rcp != LSM_VOLUME_READ_CACHE_POLICY_ENABLED))) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    std::map<std::string, Value> p;
    p["volume"] = volume_to_value(volume);
    p["rcp"] = Value(rcp);
    p["flags"] = Value(flags);
    Value parameters(p);
    Value response;

    // No response data.
    return rpc(c, "volume_read_cache_policy_update", parameters, response);
}
