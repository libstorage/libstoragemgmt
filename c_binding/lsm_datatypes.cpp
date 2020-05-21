/*
 * Copyright (C) 2011-2016 Red Hat, Inc.
 * (C) Copyright 2015-2016 Hewlett Packard Enterprise Development LP
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
 *         Joe Handzik <joseph.t.handzik@hpe.com>
 *         Gris Ge <fge@redhat.com>
 */

#ifndef __cplusplus
#define _GNU_SOURCE
#endif

#include <stdio.h>

#include "lsm_datatypes.hpp"

#include "libstoragemgmt/libstoragemgmt_accessgroups.h"
#include "libstoragemgmt/libstoragemgmt_battery.h"
#include "libstoragemgmt/libstoragemgmt_common.h"
#include "libstoragemgmt/libstoragemgmt_disk.h"
#include "libstoragemgmt/libstoragemgmt_error.h"
#include "libstoragemgmt/libstoragemgmt_fs.h"
#include "libstoragemgmt/libstoragemgmt_nfsexport.h"
#include "libstoragemgmt/libstoragemgmt_plug_interface.h"
#include "libstoragemgmt/libstoragemgmt_pool.h"
#include "libstoragemgmt/libstoragemgmt_snapshot.h"
#include "libstoragemgmt/libstoragemgmt_systems.h"
#include "libstoragemgmt/libstoragemgmt_targetport.h"
#include "libstoragemgmt/libstoragemgmt_types.h"
#include "libstoragemgmt/libstoragemgmt_volumes.h"

#include <dlfcn.h>
#include <glib.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif
#define LSM_DEFAULT_PLUGIN_DIR "/var/run/lsm/ipc"

int lsm_string_list_append(lsm_string_list *sl, const char *value) {
    int rc = LSM_ERR_INVALID_ARGUMENT;

    if (LSM_IS_STRING_LIST(sl)) {
        char *d = strdup(value);
        if (d) {
            g_ptr_array_add(sl->values, d);
            rc = LSM_ERR_OK;
        } else {
            rc = LSM_ERR_NO_MEMORY;
        }
    }
    return rc;
}

int lsm_string_list_delete(lsm_string_list *sl, uint32_t index) {
    int rc = LSM_ERR_INVALID_ARGUMENT;

    if (LSM_IS_STRING_LIST(sl)) {
        if (index < sl->values->len) {
            g_ptr_array_remove_index(sl->values, index);
            rc = LSM_ERR_OK;
        }
    }
    return rc;
}

int lsm_string_list_elem_set(lsm_string_list *sl, uint32_t index,
                             const char *value) {
    int rc = LSM_ERR_OK;
    if (LSM_IS_STRING_LIST(sl)) {
        if (index < sl->values->len) {

            char *i = (char *)g_ptr_array_index(sl->values, index);

            if (i) {
                free(i);
            }

            g_ptr_array_index(sl->values, index) = strdup(value);

            if (!g_ptr_array_index(sl->values, index)) {
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            g_ptr_array_set_size(sl->values, index + 1);
            g_ptr_array_index(sl->values, index) = strdup(value);

            if (!g_ptr_array_index(sl->values, index)) {
                rc = LSM_ERR_NO_MEMORY;
            }
        }
    } else {
        rc = LSM_ERR_INVALID_ARGUMENT;
    }
    return rc;
}

const char *lsm_string_list_elem_get(lsm_string_list *sl, uint32_t index) {
    if (LSM_IS_STRING_LIST(sl)) {
        if (index < sl->values->len) {
            return (const char *)g_ptr_array_index(sl->values, index);
        }
    }
    return NULL;
}

lsm_string_list *lsm_string_list_alloc(uint32_t size) {
    lsm_string_list *rc = NULL;

    rc = (lsm_string_list *)malloc(sizeof(lsm_string_list));
    if (rc) {
        rc->magic = LSM_STRING_LIST_MAGIC;
        rc->values = g_ptr_array_sized_new(size);
        if (!rc->values) {
            rc->magic = LSM_DEL_MAGIC(LSM_STRING_LIST_MAGIC);
            free(rc);
            rc = NULL;
        } else {
            g_ptr_array_set_size(rc->values, size);
            g_ptr_array_set_free_func(rc->values, free);
        }
    }

    return rc;
}

int lsm_string_list_free(lsm_string_list *sl) {
    if (LSM_IS_STRING_LIST(sl)) {
        sl->magic = LSM_DEL_MAGIC(LSM_STRING_LIST_MAGIC);
        g_ptr_array_free(sl->values, TRUE);
        sl->values = NULL;
        free(sl);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

uint32_t lsm_string_list_size(lsm_string_list *sl) {
    if (LSM_IS_STRING_LIST(sl)) {
        return (uint32_t)sl->values->len;
    }
    return 0;
}

lsm_string_list *lsm_string_list_copy(lsm_string_list *src) {
    lsm_string_list *dest = NULL;

    if (LSM_IS_STRING_LIST(src)) {
        uint32_t size = lsm_string_list_size(src);
        dest = lsm_string_list_alloc(size);

        if (dest) {
            uint32_t i;

            for (i = 0; i < size; ++i) {
                if (LSM_ERR_OK !=
                    lsm_string_list_elem_set(
                        dest, i, lsm_string_list_elem_get(src, i))) {
                    /** We had an allocation failure setting an element item */
                    lsm_string_list_free(dest);
                    dest = NULL;
                    break;
                }
            }
        }
    }
    return dest;
}

lsm_connect *connection_get() {
    lsm_connect *c = (lsm_connect *)calloc(1, sizeof(lsm_connect));
    if (c) {
        c->magic = LSM_CONNECT_MAGIC;
    }
    return c;
}

void connection_free(lsm_connect *c) {
    if (LSM_IS_CONNECT(c)) {

        c->magic = LSM_DEL_MAGIC(LSM_CONNECT_MAGIC);
        c->flags = 0;

        if (c->uri) {
            xmlFreeURI(c->uri);
            c->uri = NULL;
        }

        if (c->error) {
            lsm_error_free(c->error);
            c->error = NULL;
        }

        if (c->tp) {
            delete (c->tp);
            c->tp = NULL;
        }

        if (c->raw_uri) {
            free(c->raw_uri);
            c->raw_uri = NULL;
        }

        free(c);
    }
}

static int connection_establish(lsm_connect *c, const char *password,
                                uint32_t timeout, lsm_error_ptr *e,
                                lsm_flag flags) {
    int rc = LSM_ERR_OK;
    std::map<std::string, Value> params;

    try {
        params["uri"] = Value(c->raw_uri);

        if (password) {
            params["password"] = Value(password);
        } else {
            params["password"] = Value();
        }
        params["timeout"] = Value(timeout);
        params["flags"] = Value(flags);
        Value p(params);

        c->tp->rpc("plugin_register", p);
    } catch (const ValueException &ve) {
        *e = lsm_error_create(LSM_ERR_TRANSPORT_SERIALIZATION,
                              "Error in serialization", ve.what(), NULL, NULL,
                              0);
        rc = LSM_ERR_TRANSPORT_SERIALIZATION;
    } catch (const LsmException &le) {
        *e = lsm_error_create(LSM_ERR_TRANSPORT_COMMUNICATION,
                              "Error in communication", le.what(), NULL, NULL,
                              0);
        rc = LSM_ERR_TRANSPORT_COMMUNICATION;
    } catch (...) {
        *e = lsm_error_create(LSM_ERR_LIB_BUG, "Undefined exception", NULL,
                              NULL, NULL, 0);
        rc = LSM_ERR_LIB_BUG;
    }
    return rc;
}

const char *uds_path(void) {
    const char *plugin_dir = getenv("LSM_UDS_PATH");

    if (plugin_dir == NULL) {
        plugin_dir = LSM_DEFAULT_PLUGIN_DIR;
    }
    return plugin_dir;
}

int driver_load(lsm_connect *c, const char *plugin_name, const char *password,
                uint32_t timeout, lsm_error_ptr *e, int startup,
                lsm_flag flags) {
    int rc = LSM_ERR_OK;
    char *plugin_file = NULL;
    const char *plugin_dir = uds_path();

    if (asprintf(&plugin_file, "%s/%s", plugin_dir, plugin_name) == -1) {
        return LSM_ERR_NO_MEMORY;
    }

    if (access(plugin_file, F_OK) != 0) {
        rc = LSM_ERR_PLUGIN_NOT_EXIST;
    } else {
        if (access(plugin_file, R_OK | W_OK) == 0) {
            int ec;
            int sd = Transport::socket_get(std::string(plugin_file), ec);

            if (sd >= 0) {
                c->tp = new Ipc(sd);
                if (startup) {
                    if (connection_establish(c, password, timeout, e, flags)) {
                        rc = LSM_ERR_PLUGIN_IPC_FAIL;
                    }
                }
            } else {
                *e = lsm_error_create(LSM_ERR_PLUGIN_IPC_FAIL,
                                      "Unable to connect to plugin", NULL,
                                      dlerror(), NULL, 0);

                rc = LSM_ERR_PLUGIN_IPC_FAIL;
            }
        } else {
            *e = lsm_error_create(LSM_ERR_PLUGIN_SOCKET_PERMISSION,
                                  "Unable to access plugin", NULL, NULL, NULL,
                                  0);

            rc = LSM_ERR_PLUGIN_SOCKET_PERMISSION;
        }
    }

    free(plugin_file);
    return rc;
}

lsm_error_ptr lsm_error_create(lsm_error_number code, const char *msg,
                               const char *exception, const char *debug,
                               const void *debug_data,
                               uint32_t debug_data_size) {
    lsm_error_ptr err = (lsm_error_ptr)calloc(1, sizeof(lsm_error));

    if (err) {
        err->magic = LSM_ERROR_MAGIC;
        err->code = code;

        /* Any of these strdup calls could fail, but we will continue */
        if (msg) {
            err->message = strdup(msg);
        }

        if (exception) {
            err->exception = strdup(exception);
        }

        if (debug) {
            err->debug = strdup(debug);
        }

        /* We are not going to fail the creation of the error if we cannot
         * allocate the storage for the debug data.
         */
        if (debug_data && (debug_data_size > 0)) {
            err->debug_data = malloc(debug_data_size);

            if (err->debug_data) {
                err->debug_data_size = debug_data_size;
                memcpy(err->debug_data, debug_data, debug_data_size);
            }
        }
    }
    return (lsm_error_ptr)err;
}

int lsm_error_free(lsm_error_ptr e) {
    if (!LSM_IS_ERROR(e)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    if (e->debug_data) {
        free(e->debug_data);
        e->debug_data = NULL;
        e->debug_data_size = 0;
    }

    if (e->debug) {
        free(e->debug);
        e->debug = NULL;
    }

    if (e->exception) {
        free(e->exception);
        e->exception = NULL;
    }

    if (e->message) {
        free(e->message);
        e->message = NULL;
    }

    e->magic = LSM_DEL_MAGIC(LSM_ERROR_MAGIC);
    free(e);

    return LSM_ERR_OK;
}

#define LSM_RETURN_ERR_VAL(type_t, e, x, error)                                \
    if (LSM_IS_ERROR(e)) {                                                     \
        return e->x;                                                           \
    }                                                                          \
    return (type_t)error;

lsm_error_number lsm_error_number_get(lsm_error_ptr e) {
    LSM_RETURN_ERR_VAL(lsm_error_number, e, code, -1);
}

char *lsm_error_message_get(lsm_error_ptr e) {
    LSM_RETURN_ERR_VAL(char *, e, message, NULL);
}

char *lsm_error_exception_get(lsm_error_ptr e) {
    LSM_RETURN_ERR_VAL(char *, e, exception, NULL);
}

char *lsm_error_debug_get(lsm_error_ptr e) {
    LSM_RETURN_ERR_VAL(char *, e, debug, NULL);
}

void *lsm_error_debug_data_get(lsm_error_ptr e, uint32_t *size) {
    if (LSM_IS_ERROR(e) && size != NULL) {
        if (e->debug_data) {
            *size = e->debug_data_size;
            return e->debug_data;
        } else {
            *size = 0;
        }
    }
    return NULL;
}

/**
 * When creating arrays of the different types the code is the same.  This
 * macro is used to create type safe code.
 * @param   name    Name of the function
 * @param   rtype   return type
 * @return An array of pointers of rtype
 */
#define CREATE_ALLOC_ARRAY_FUNC(name, rtype)                                   \
    rtype *name(uint32_t size) {                                               \
        rtype *rc = NULL;                                                      \
        if (size > 0) {                                                        \
            rc = (rtype *)calloc(size, sizeof(rtype));                         \
        }                                                                      \
        return rc;                                                             \
    }

/**
 * Common macro for freeing the memory associated with one of these
 * data structures.
 * @param name              Name of function to create
 * @param free_func         Function to call to free one of the elements
 * @param record_type       Type to record
 * @param error             Value to return on error
 * @return None
 */
#define CREATE_FREE_ARRAY_FUNC(name, free_func, record_type, error)            \
    int name(record_type pa[], uint32_t size) {                                \
        if (pa) {                                                              \
            uint32_t i = 0;                                                    \
            for (i = 0; i < size; ++i) {                                       \
                free_func(pa[i]);                                              \
            }                                                                  \
            free(pa);                                                          \
            return LSM_ERR_OK;                                                 \
        }                                                                      \
        return error;                                                          \
    }

CREATE_ALLOC_ARRAY_FUNC(lsm_pool_record_array_alloc, lsm_pool *)

lsm_pool *lsm_pool_record_alloc(const char *id, const char *name,
                                uint64_t element_type,
                                uint64_t unsupported_actions,
                                uint64_t totalSpace, uint64_t freeSpace,
                                uint64_t status, const char *status_info,
                                const char *system_id,
                                const char *plugin_data) {
    lsm_pool *rc = (lsm_pool *)calloc(1, sizeof(lsm_pool));
    if (rc) {
        rc->magic = LSM_POOL_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->element_type = element_type;
        rc->unsupported_actions = unsupported_actions;
        rc->total_space = totalSpace;
        rc->free_space = freeSpace;
        rc->status = status;
        rc->status_info = strdup(status_info);
        rc->system_id = strdup(system_id);

        if (plugin_data) {
            rc->plugin_data = strdup(plugin_data);
        }

        if (!rc->id || !rc->name || !rc->system_id || !rc->status_info ||
            (plugin_data && !rc->plugin_data)) {
            lsm_pool_record_free(rc);
            rc = NULL;
        }
    }
    return rc;
}

void lsm_pool_free_space_set(lsm_pool *p, uint64_t free_space) {
    if (LSM_IS_POOL(p)) {
        p->free_space = free_space;
    }
}

lsm_pool *lsm_pool_record_copy(lsm_pool *toBeCopied) {
    if (LSM_IS_POOL(toBeCopied)) {
        return lsm_pool_record_alloc(
            toBeCopied->id, toBeCopied->name, toBeCopied->element_type,
            toBeCopied->unsupported_actions, toBeCopied->total_space,
            toBeCopied->free_space, toBeCopied->status, toBeCopied->status_info,
            toBeCopied->system_id, toBeCopied->plugin_data);
    }
    return NULL;
}

int lsm_pool_record_free(lsm_pool *p) {
    if (LSM_IS_POOL(p)) {
        p->magic = LSM_DEL_MAGIC(LSM_POOL_MAGIC);
        if (p->name) {
            free(p->name);
            p->name = NULL;
        }

        if (p->status_info) {
            free(p->status_info);
            p->status_info = NULL;
        }

        if (p->id) {
            free(p->id);
            p->id = NULL;
        }

        if (p->system_id) {
            free(p->system_id);
            p->system_id = NULL;
        }

        free(p->plugin_data);
        p->plugin_data = NULL;

        free(p);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

CREATE_FREE_ARRAY_FUNC(lsm_pool_record_array_free, lsm_pool_record_free,
                       lsm_pool *, LSM_ERR_INVALID_ARGUMENT)

MEMBER_FUNC_GET(char *, lsm_pool, LSM_IS_POOL, id, NULL);
MEMBER_FUNC_GET(char *, lsm_pool, LSM_IS_POOL, name, NULL);
MEMBER_FUNC_GET(const char *, lsm_pool, LSM_IS_POOL, status_info, NULL);
MEMBER_FUNC_GET(char *, lsm_pool, LSM_IS_POOL, system_id, NULL);
MEMBER_FUNC_GET(const char *, lsm_pool, LSM_IS_POOL, plugin_data, NULL);
MEMBER_FUNC_GET(uint64_t, lsm_pool, LSM_IS_POOL, total_space, 0);
MEMBER_FUNC_GET(uint64_t, lsm_pool, LSM_IS_POOL, free_space, 0);
MEMBER_FUNC_GET(uint64_t, lsm_pool, LSM_IS_POOL, status, UINT64_MAX);
MEMBER_FUNC_GET(uint64_t, lsm_pool, LSM_IS_POOL, element_type, 0);
MEMBER_FUNC_GET(uint64_t, lsm_pool, LSM_IS_POOL, unsupported_actions, 0);

CREATE_ALLOC_ARRAY_FUNC(lsm_volume_record_array_alloc, lsm_volume *)

lsm_volume *lsm_volume_record_alloc(const char *id, const char *name,
                                    const char *vpd83, uint64_t blockSize,
                                    uint64_t numberOfBlocks, uint32_t status,
                                    const char *system_id, const char *pool_id,
                                    const char *plugin_data) {
    if (vpd83 && (LSM_ERR_OK != lsm_volume_vpd83_verify(vpd83))) {
        return NULL;
    }

    lsm_volume *rc = (lsm_volume *)calloc(1, sizeof(lsm_volume));
    if (rc) {
        rc->magic = LSM_VOL_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);

        if (vpd83) {
            rc->vpd83 = strdup(vpd83);
        }

        rc->block_size = blockSize;
        rc->number_of_blocks = numberOfBlocks;
        rc->admin_state = status;
        rc->system_id = strdup(system_id);
        rc->pool_id = strdup(pool_id);

        if (plugin_data) {
            rc->plugin_data = strdup(plugin_data);
        }

        if (!rc->id || !rc->name || (vpd83 && !rc->vpd83) || !rc->system_id ||
            !rc->pool_id || (plugin_data && !rc->plugin_data)) {
            lsm_volume_record_free(rc);
            rc = NULL;
        }
    }
    return rc;
}

CREATE_ALLOC_ARRAY_FUNC(lsm_disk_record_array_alloc, lsm_disk *)

lsm_disk *lsm_disk_record_alloc(const char *id, const char *name,
                                lsm_disk_type disk_type, uint64_t block_size,
                                uint64_t block_count, uint64_t disk_status,
                                const char *system_id) {
    lsm_disk *rc = (lsm_disk *)malloc(sizeof(lsm_disk));
    if (rc) {
        rc->magic = LSM_DISK_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->type = disk_type;
        rc->block_size = block_size;
        rc->number_of_blocks = block_count;
        rc->status = disk_status;
        rc->system_id = strdup(system_id);
        rc->vpd83 = NULL;
        rc->location = NULL;
        rc->rpm = LSM_DISK_RPM_NO_SUPPORT;
        rc->link_type = LSM_DISK_LINK_TYPE_NO_SUPPORT;

        if (!rc->id || !rc->name || !rc->system_id) {
            lsm_disk_record_free(rc);
            rc = NULL;
        }
    }
    return rc;
}

CREATE_ALLOC_ARRAY_FUNC(lsm_system_record_array_alloc, lsm_system *)

lsm_system *lsm_system_record_alloc(const char *id, const char *name,
                                    uint32_t status, const char *status_info,
                                    const char *plugin_data) {
    lsm_system *rc = (lsm_system *)calloc(1, sizeof(lsm_system));
    if (rc) {
        rc->magic = LSM_SYSTEM_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->status = status;
        rc->status_info = strdup(status_info);
        rc->fw_version = NULL;
        rc->mode = LSM_SYSTEM_MODE_NO_SUPPORT;
        rc->read_cache_pct = LSM_SYSTEM_READ_CACHE_PCT_NO_SUPPORT;

        if (plugin_data) {
            rc->plugin_data = strdup(plugin_data);
        }

        if (!rc->name || !rc->id || !rc->status_info ||
            (plugin_data && !rc->plugin_data)) {
            lsm_system_record_free(rc);
            rc = NULL;
        }
    }
    return rc;
}

int lsm_system_record_free(lsm_system *s) {
    if (LSM_IS_SYSTEM(s)) {
        s->magic = LSM_DEL_MAGIC(LSM_SYSTEM_MAGIC);

        if (s->id) {
            free(s->id);
            s->id = NULL;
        }

        if (s->name) {
            free(s->name);
            s->name = NULL;
        }

        if (s->status_info) {
            free(s->status_info);
            s->status_info = NULL;
        }

        free(s->plugin_data);

        if (s->fw_version != NULL)
            free((char *)s->fw_version);

        free(s);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

CREATE_FREE_ARRAY_FUNC(lsm_system_record_array_free, lsm_system_record_free,
                       lsm_system *, LSM_ERR_INVALID_ARGUMENT)

lsm_system *lsm_system_record_copy(lsm_system *s) {
    lsm_system *rc = NULL;
    if (LSM_IS_SYSTEM(s)) {
        rc = lsm_system_record_alloc(s->id, s->name, s->status, s->status_info,
                                     s->plugin_data);
        rc->mode = s->mode;
        rc->read_cache_pct = s->read_cache_pct;

        if ((s->fw_version != NULL) &&
            (lsm_system_fw_version_set(rc, s->fw_version) != LSM_ERR_OK)) {
            lsm_system_record_free(rc);
            rc = NULL;
        }
    }
    return rc;
}

MEMBER_FUNC_GET(const char *, lsm_system, LSM_IS_SYSTEM, id, NULL);
MEMBER_FUNC_GET(const char *, lsm_system, LSM_IS_SYSTEM, name, NULL);
MEMBER_FUNC_GET(uint32_t, lsm_system, LSM_IS_SYSTEM, status, UINT32_MAX);
MEMBER_FUNC_GET(const char *, lsm_system, LSM_IS_SYSTEM, plugin_data, NULL);
MEMBER_FUNC_GET(int, lsm_system, LSM_IS_SYSTEM, read_cache_pct,
                LSM_SYSTEM_READ_CACHE_PCT_UNKNOWN);
MEMBER_FUNC_GET(const char *, lsm_system, LSM_IS_SYSTEM, fw_version, NULL);
MEMBER_FUNC_GET(lsm_system_mode_type, lsm_system, LSM_IS_SYSTEM, mode,
                LSM_SYSTEM_MODE_UNKNOWN);

int lsm_system_fw_version_set(lsm_system *sys, const char *fw_ver) {
    if ((sys == NULL) || (fw_ver == NULL) || (fw_ver[0] == '\0') ||
        (!LSM_IS_SYSTEM(sys)))
        return LSM_ERR_INVALID_ARGUMENT;

    if (sys->fw_version != NULL)
        free((char *)sys->fw_version);

    sys->fw_version = strdup(fw_ver);
    if (sys->fw_version == NULL)
        return LSM_ERR_NO_MEMORY;

    return LSM_ERR_OK;
}

int lsm_system_mode_set(lsm_system *sys, lsm_system_mode_type mode) {
    if ((sys == NULL) || (!LSM_IS_SYSTEM(sys)) ||
        (mode == LSM_SYSTEM_MODE_NO_SUPPORT))
        return LSM_ERR_INVALID_ARGUMENT;

    sys->mode = mode;

    return LSM_ERR_OK;
}

int lsm_system_read_cache_pct_set(lsm_system *sys, int read_pct) {
    if ((sys == NULL) || (!LSM_IS_SYSTEM(sys)) ||
        (read_pct == LSM_SYSTEM_READ_CACHE_PCT_NO_SUPPORT))
        return LSM_ERR_INVALID_ARGUMENT;

    sys->read_cache_pct = read_pct;

    return LSM_ERR_OK;
}

lsm_volume *lsm_volume_record_copy(lsm_volume *vol) {
    lsm_volume *rc = NULL;
    if (LSM_IS_VOL(vol)) {
        rc = lsm_volume_record_alloc(vol->id, vol->name, vol->vpd83,
                                     vol->block_size, vol->number_of_blocks,
                                     vol->admin_state, vol->system_id,
                                     vol->pool_id, vol->plugin_data);
    }
    return rc;
}

int lsm_volume_record_free(lsm_volume *v) {
    if (LSM_IS_VOL(v)) {
        v->magic = LSM_DEL_MAGIC(LSM_VOL_MAGIC);

        if (v->id) {
            free(v->id);
            v->id = NULL;
        }

        if (v->name) {
            free(v->name);
            v->name = NULL;
        }

        if (v->vpd83) {
            free(v->vpd83);
            v->vpd83 = NULL;
        }

        if (v->system_id) {
            free(v->system_id);
            v->system_id = NULL;
        }

        if (v->pool_id) {
            free(v->pool_id);
            v->pool_id = NULL;
        }

        free(v->plugin_data);
        v->plugin_data = NULL;

        free(v);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

CREATE_FREE_ARRAY_FUNC(lsm_volume_record_array_free, lsm_volume_record_free,
                       lsm_volume *, LSM_ERR_INVALID_ARGUMENT)

lsm_disk *lsm_disk_record_copy(lsm_disk *disk) {
    lsm_disk *new_lsm_disk = NULL;

    if (LSM_IS_DISK(disk)) {
        new_lsm_disk = lsm_disk_record_alloc(
            disk->id, disk->name, disk->type, disk->block_size,
            disk->number_of_blocks, disk->status, disk->system_id);
        if (disk->vpd83 != NULL)
            if (lsm_disk_vpd83_set(new_lsm_disk, disk->vpd83) != LSM_ERR_OK) {
                lsm_disk_record_free(new_lsm_disk);
                return NULL;
            }
        if ((disk->location != NULL) &&
            (lsm_disk_location_set(new_lsm_disk, disk->location) !=
             LSM_ERR_OK)) {
            lsm_disk_record_free(new_lsm_disk);
            return NULL;
        }
        new_lsm_disk->rpm = disk->rpm;
        new_lsm_disk->link_type = disk->link_type;
        return new_lsm_disk;
    }
    return NULL;
}

int lsm_disk_record_free(lsm_disk *d) {
    if (LSM_IS_DISK(d)) {
        d->magic = LSM_DEL_MAGIC(LSM_DISK_MAGIC);

        free(d->id);
        d->id = NULL;

        free(d->name);
        d->name = NULL;

        free(d->system_id);
        d->system_id = NULL;

        free(d->vpd83);
        d->vpd83 = NULL;

        free((char *)d->location);

        free(d);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

CREATE_FREE_ARRAY_FUNC(lsm_disk_record_array_free, lsm_disk_record_free,
                       lsm_disk *, LSM_ERR_INVALID_ARGUMENT)

/* We would certainly expand this to encompass the entire function */
#define MEMBER_SET_REF(x, validation, member, value, alloc_func, free_func,    \
                       error)                                                  \
    if (validation(x)) {                                                       \
        if (x->member) {                                                       \
            free_func(x->member);                                              \
            x->member = NULL;                                                  \
        }                                                                      \
        if (value) {                                                           \
            x->member = alloc_func(value);                                     \
            if (!x->member) {                                                  \
                return LSM_ERR_NO_MEMORY;                                      \
            }                                                                  \
        }                                                                      \
        return LSM_ERR_OK;                                                     \
    } else {                                                                   \
        return error;                                                          \
    }
/* We would certainly expand this to encompass the entire function */
#define MEMBER_SET_VAL(x, validation, member, value, error)                    \
    if (validation(x)) {                                                       \
        x->member = value;                                                     \
        return LSM_ERR_OK;                                                     \
    } else {                                                                   \
        return error;                                                          \
    }

MEMBER_FUNC_GET(const char *, lsm_volume, LSM_IS_VOL, id, NULL);
MEMBER_FUNC_GET(const char *, lsm_volume, LSM_IS_VOL, name, NULL);
MEMBER_FUNC_GET(char *, lsm_volume, LSM_IS_VOL, system_id, NULL);
MEMBER_FUNC_GET(const char *, lsm_volume, LSM_IS_VOL, vpd83, NULL);
MEMBER_FUNC_GET(const char *, lsm_volume, LSM_IS_VOL, plugin_data, NULL);
MEMBER_FUNC_GET(char *, lsm_volume, LSM_IS_VOL, pool_id, NULL);
MEMBER_FUNC_GET(uint64_t, lsm_volume, LSM_IS_VOL, block_size, 0);
MEMBER_FUNC_GET(uint64_t, lsm_volume, LSM_IS_VOL, number_of_blocks, 0);
MEMBER_FUNC_GET(uint32_t, lsm_volume, LSM_IS_VOL, admin_state,
                LSM_VOLUME_ADMIN_STATE_ENABLED);

int lsm_disk_location_set(lsm_disk *disk, const char *location) {
    if ((disk == NULL) || (location == NULL) || (location[0] == '\0'))
        return LSM_ERR_INVALID_ARGUMENT;

    free((char *)disk->location);
    disk->location = strdup(location);
    if (disk->location == NULL)
        return LSM_ERR_NO_MEMORY;

    return LSM_ERR_OK;
}

MEMBER_FUNC_GET(const char *, lsm_disk, LSM_IS_DISK, id, NULL);
MEMBER_FUNC_GET(const char *, lsm_disk, LSM_IS_DISK, name, NULL);
MEMBER_FUNC_GET(const char *, lsm_disk, LSM_IS_DISK, system_id, NULL);
MEMBER_FUNC_GET(const char *, lsm_disk, LSM_IS_DISK, vpd83, NULL);
MEMBER_FUNC_GET(const char *, lsm_disk, LSM_IS_DISK, location, NULL);
MEMBER_FUNC_GET(lsm_disk_type, lsm_disk, LSM_IS_DISK, type,
                LSM_DISK_TYPE_UNKNOWN);
MEMBER_FUNC_GET(uint64_t, lsm_disk, LSM_IS_DISK, block_size, 0);
MEMBER_FUNC_GET(uint64_t, lsm_disk, LSM_IS_DISK, number_of_blocks, 0);
MEMBER_FUNC_GET(uint64_t, lsm_disk, LSM_IS_DISK, status,
                LSM_DISK_STATUS_UNKNOWN);
MEMBER_FUNC_GET(int32_t, lsm_disk, LSM_IS_DISK, rpm, LSM_DISK_RPM_UNKNOWN);
MEMBER_FUNC_GET(lsm_disk_link_type, lsm_disk, LSM_IS_DISK, link_type,
                LSM_DISK_LINK_TYPE_UNKNOWN);

int lsm_disk_vpd83_set(lsm_disk *disk, const char *vpd83) {
    if ((disk == NULL) || (!LSM_IS_DISK(disk)) || (vpd83 == NULL))
        return LSM_ERR_INVALID_ARGUMENT;

    free(disk->vpd83);

    disk->vpd83 = strdup(vpd83);
    if (disk->vpd83 == NULL)
        return LSM_ERR_NO_MEMORY;

    return LSM_ERR_OK;
}

int lsm_disk_rpm_set(lsm_disk *disk, int32_t rpm) {
    if ((disk == NULL) || (!LSM_IS_DISK(disk)) ||
        (rpm == LSM_DISK_RPM_NO_SUPPORT))
        return LSM_ERR_INVALID_ARGUMENT;

    disk->rpm = rpm;
    return LSM_ERR_OK;
}

int lsm_disk_link_type_set(lsm_disk *disk, lsm_disk_link_type link_type) {
    if ((disk == NULL) || (!LSM_IS_DISK(disk)) ||
        (link_type == LSM_DISK_LINK_TYPE_NO_SUPPORT))
        return LSM_ERR_INVALID_ARGUMENT;

    disk->link_type = link_type;
    return LSM_ERR_OK;
}

CREATE_ALLOC_ARRAY_FUNC(lsm_access_group_record_array_alloc, lsm_access_group *)

static lsm_string_list *standardize_init_list(lsm_string_list *initiators) {
    uint32_t i = 0;
    lsm_string_list *rc = lsm_string_list_copy(initiators);
    char *wwpn = NULL;

    if (rc) {
        for (i = 0; i < lsm_string_list_size(rc); ++i) {
            if (LSM_ERR_OK == wwpn_validate(lsm_string_list_elem_get(rc, i))) {
                /* We have a wwpn, switch to internal representation */
                wwpn = wwpn_convert(lsm_string_list_elem_get(rc, i));
                if (!wwpn ||
                    LSM_ERR_OK != lsm_string_list_elem_set(rc, i, wwpn)) {
                    free(wwpn);
                    lsm_string_list_free(rc);
                    rc = NULL;
                    break;
                }
                free(wwpn);
                wwpn = NULL;
            }
        }
    }

    return rc;
}

lsm_access_group *
lsm_access_group_record_alloc(const char *id, const char *name,
                              lsm_string_list *initiators,
                              lsm_access_group_init_type init_type,
                              const char *system_id, const char *plugin_data) {
    lsm_access_group *rc = NULL;
    if (id && name && system_id) {
        rc = (lsm_access_group *)malloc(sizeof(lsm_access_group));
        if (rc) {
            rc->magic = LSM_ACCESS_GROUP_MAGIC;
            rc->id = strdup(id);
            rc->name = strdup(name);
            rc->system_id = strdup(system_id);
            rc->initiators = standardize_init_list(initiators);
            rc->init_type = init_type;

            if (plugin_data) {
                rc->plugin_data = strdup(plugin_data);
            } else {
                rc->plugin_data = NULL;
            }

            if (!rc->id || !rc->name || !rc->system_id ||
                (plugin_data && !rc->plugin_data) ||
                (initiators && !rc->initiators)) {
                lsm_access_group_record_free(rc);
                rc = NULL;
            }
        }
    }
    return rc;
}

lsm_access_group *lsm_access_group_record_copy(lsm_access_group *ag) {
    lsm_access_group *rc = NULL;
    if (LSM_IS_ACCESS_GROUP(ag)) {
        rc = lsm_access_group_record_alloc(ag->id, ag->name, ag->initiators,
                                           ag->init_type, ag->system_id,
                                           ag->plugin_data);
    }
    return rc;
}

int lsm_access_group_record_free(lsm_access_group *ag) {
    if (LSM_IS_ACCESS_GROUP(ag)) {
        ag->magic = LSM_DEL_MAGIC(LSM_ACCESS_GROUP_MAGIC);
        free(ag->id);
        free(ag->name);
        free(ag->system_id);
        lsm_string_list_free(ag->initiators);
        free(ag->plugin_data);
        free(ag);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

CREATE_FREE_ARRAY_FUNC(lsm_access_group_record_array_free,
                       lsm_access_group_record_free, lsm_access_group *,
                       LSM_ERR_INVALID_ARGUMENT)
MEMBER_FUNC_GET(const char *, lsm_access_group, LSM_IS_ACCESS_GROUP, id, NULL);
MEMBER_FUNC_GET(const char *, lsm_access_group, LSM_IS_ACCESS_GROUP, name,
                NULL);
MEMBER_FUNC_GET(const char *, lsm_access_group, LSM_IS_ACCESS_GROUP, system_id,
                NULL);

MEMBER_FUNC_GET(lsm_access_group_init_type, lsm_access_group,
                LSM_IS_ACCESS_GROUP, init_type,
                LSM_ACCESS_GROUP_INIT_TYPE_UNKNOWN);

lsm_string_list *lsm_access_group_initiator_id_get(lsm_access_group *group) {
    if (LSM_IS_ACCESS_GROUP(group)) {
        return group->initiators;
    }
    return NULL;
}

void lsm_access_group_initiator_id_set(lsm_access_group *group,
                                       lsm_string_list *il) {
    if (LSM_IS_ACCESS_GROUP(group)) {
        if (group->initiators && group->initiators != il) {
            lsm_string_list_free(group->initiators);
        }

        group->initiators = lsm_string_list_copy(il);
    }
}

lsm_error_ptr lsm_error_last_get(lsm_connect *c) {
    if (LSM_IS_CONNECT(c)) {
        lsm_error_ptr e = c->error;
        c->error = NULL;
        return e;
    }
    return NULL;
}

lsm_block_range *lsm_block_range_record_alloc(uint64_t source_start,
                                              uint64_t dest_start,
                                              uint64_t block_count) {
    lsm_block_range *rc = NULL;

    rc = (lsm_block_range *)malloc(sizeof(lsm_block_range));
    if (rc) {
        rc->magic = LSM_BLOCK_RANGE_MAGIC;
        rc->source_start = source_start;
        rc->dest_start = dest_start;
        rc->block_count = block_count;
    }
    return rc;
}

int lsm_block_range_record_free(lsm_block_range *br) {
    if (LSM_IS_BLOCK_RANGE(br)) {
        br->magic = LSM_DEL_MAGIC(LSM_BLOCK_RANGE_MAGIC);
        free(br);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

lsm_block_range *lsm_block_range_record_copy(lsm_block_range *source) {
    lsm_block_range *dest = NULL;

    if (LSM_IS_BLOCK_RANGE(source)) {
        dest = lsm_block_range_record_alloc(
            source->source_start, source->dest_start, source->block_count);
    }
    return dest;
}

CREATE_ALLOC_ARRAY_FUNC(lsm_block_range_record_array_alloc, lsm_block_range *)
CREATE_FREE_ARRAY_FUNC(lsm_block_range_record_array_free,
                       lsm_block_range_record_free, lsm_block_range *,
                       LSM_ERR_INVALID_ARGUMENT)

MEMBER_FUNC_GET(uint64_t, lsm_block_range, LSM_IS_BLOCK_RANGE, source_start, 0);
MEMBER_FUNC_GET(uint64_t, lsm_block_range, LSM_IS_BLOCK_RANGE, dest_start, 0);
MEMBER_FUNC_GET(uint64_t, lsm_block_range, LSM_IS_BLOCK_RANGE, block_count, 0);

lsm_fs *lsm_fs_record_alloc(const char *id, const char *name,
                            uint64_t total_space, uint64_t free_space,
                            const char *pool_id, const char *system_id,
                            const char *plugin_data) {
    lsm_fs *rc = NULL;
    rc = (lsm_fs *)calloc(1, sizeof(lsm_fs));
    if (rc) {
        rc->magic = LSM_FS_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->pool_id = strdup(pool_id);
        rc->system_id = strdup(system_id);
        rc->total_space = total_space;
        rc->free_space = free_space;

        if (plugin_data) {
            rc->plugin_data = strdup(plugin_data);
        }

        if (!rc->id || !rc->name || !rc->pool_id || !rc->system_id ||
            (plugin_data && !rc->plugin_data)) {
            lsm_fs_record_free(rc);
            rc = NULL;
        }
    }
    return rc;
}

int lsm_fs_record_free(lsm_fs *fs) {
    if (LSM_IS_FS(fs)) {
        fs->magic = LSM_DEL_MAGIC(LSM_FS_MAGIC);
        free(fs->id);
        free(fs->name);
        free(fs->pool_id);
        free(fs->system_id);
        free(fs->plugin_data);
        free(fs);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

lsm_fs *lsm_fs_record_copy(lsm_fs *source) {
    lsm_fs *dest = NULL;

    if (LSM_IS_FS(source)) {
        dest = lsm_fs_record_alloc(
            source->id, source->name, source->total_space, source->free_space,
            source->pool_id, source->system_id, source->plugin_data);
    }
    return dest;
}

CREATE_ALLOC_ARRAY_FUNC(lsm_fs_record_array_alloc, lsm_fs *)
CREATE_FREE_ARRAY_FUNC(lsm_fs_record_array_free, lsm_fs_record_free, lsm_fs *,
                       LSM_ERR_INVALID_ARGUMENT)

MEMBER_FUNC_GET(const char *, lsm_fs, LSM_IS_FS, id, NULL);
MEMBER_FUNC_GET(const char *, lsm_fs, LSM_IS_FS, name, NULL);
MEMBER_FUNC_GET(const char *, lsm_fs, LSM_IS_FS, system_id, NULL);
MEMBER_FUNC_GET(const char *, lsm_fs, LSM_IS_FS, plugin_data, NULL);
MEMBER_FUNC_GET(const char *, lsm_fs, LSM_IS_FS, pool_id, NULL);
MEMBER_FUNC_GET(uint64_t, lsm_fs, LSM_IS_FS, total_space, 0);
MEMBER_FUNC_GET(uint64_t, lsm_fs, LSM_IS_FS, free_space, 0);

lsm_fs_ss *lsm_fs_ss_record_alloc(const char *id, const char *name, uint64_t ts,
                                  const char *plugin_data) {
    lsm_fs_ss *rc = (lsm_fs_ss *)calloc(1, sizeof(lsm_fs_ss));
    if (rc) {
        rc->magic = LSM_SS_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->time_stamp = ts;
        if (plugin_data) {
            rc->plugin_data = strdup(plugin_data);
        }

        if (!rc->id || !rc->name || (plugin_data && !rc->plugin_data)) {
            lsm_fs_ss_record_free(rc);
            rc = NULL;
        }
    }
    return rc;
}

lsm_fs_ss *lsm_fs_ss_record_copy(lsm_fs_ss *source) {
    lsm_fs_ss *rc = NULL;
    if (LSM_IS_SS(source)) {
        rc = lsm_fs_ss_record_alloc(source->id, source->name,
                                    source->time_stamp, source->plugin_data);
    }
    return rc;
}

int lsm_fs_ss_record_free(lsm_fs_ss *ss) {
    if (LSM_IS_SS(ss)) {
        ss->magic = LSM_DEL_MAGIC(LSM_SS_MAGIC);
        free(ss->id);
        free(ss->name);
        free(ss->plugin_data);

        free(ss);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

CREATE_ALLOC_ARRAY_FUNC(lsm_fs_ss_record_array_alloc, lsm_fs_ss *)

CREATE_FREE_ARRAY_FUNC(lsm_fs_ss_record_array_free, lsm_fs_ss_record_free,
                       lsm_fs_ss *, LSM_ERR_INVALID_ARGUMENT)

MEMBER_FUNC_GET(const char *, lsm_fs_ss, LSM_IS_SS, id, NULL);
MEMBER_FUNC_GET(const char *, lsm_fs_ss, LSM_IS_SS, name, NULL);
MEMBER_FUNC_GET(const char *, lsm_fs_ss, LSM_IS_SS, plugin_data, NULL);
MEMBER_FUNC_GET(uint64_t, lsm_fs_ss, LSM_IS_SS, time_stamp, 0);

lsm_nfs_export *lsm_nfs_export_record_alloc(
    const char *id, const char *fs_id, const char *export_path,
    const char *auth, lsm_string_list *root, lsm_string_list *rw,
    lsm_string_list *ro, uint64_t anon_uid, uint64_t anon_gid,
    const char *options, const char *plugin_data) {
    lsm_nfs_export *rc = NULL;

    /* This is required */
    if (fs_id) {
        rc = (lsm_nfs_export *)calloc(1, sizeof(lsm_nfs_export));
        if (rc) {
            rc->magic = LSM_NFS_EXPORT_MAGIC;
            rc->id = (id) ? strdup(id) : NULL;
            rc->fs_id = strdup(fs_id);
            rc->export_path = (export_path) ? strdup(export_path) : NULL;
            rc->auth_type = (auth) ? strdup(auth) : NULL;
            rc->root = lsm_string_list_copy(root);
            rc->read_write = lsm_string_list_copy(rw);
            rc->read_only = lsm_string_list_copy(ro);
            rc->anon_uid = anon_uid;
            rc->anon_gid = anon_gid;
            rc->options = (options) ? strdup(options) : NULL;

            if (plugin_data) {
                rc->plugin_data = strdup(plugin_data);
            }

            if (!rc->id || !rc->fs_id || (export_path && !rc->export_path) ||
                (auth && !rc->auth_type) || (root && !rc->root) ||
                (rw && !rc->read_write) || (ro && !rc->read_only) ||
                (options && !rc->options) ||
                (plugin_data && !rc->plugin_data)) {
                lsm_nfs_export_record_free(rc);
                rc = NULL;
            }
        }
    }

    return rc;
}

int lsm_nfs_export_record_free(lsm_nfs_export *exp) {
    if (LSM_IS_NFS_EXPORT(exp)) {
        exp->magic = LSM_DEL_MAGIC(LSM_NFS_EXPORT_MAGIC);
        free(exp->id);
        free(exp->fs_id);
        free(exp->export_path);
        free(exp->auth_type);
        lsm_string_list_free(exp->root);
        lsm_string_list_free(exp->read_write);
        lsm_string_list_free(exp->read_only);
        free(exp->options);
        free(exp->plugin_data);

        free(exp);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

lsm_nfs_export *lsm_nfs_export_record_copy(lsm_nfs_export *s) {
    if (LSM_IS_NFS_EXPORT(s)) {
        return lsm_nfs_export_record_alloc(
            s->id, s->fs_id, s->export_path, s->auth_type, s->root,
            s->read_write, s->read_only, s->anon_uid, s->anon_gid, s->options,
            s->plugin_data);
    }
    return NULL;
}

CREATE_ALLOC_ARRAY_FUNC(lsm_nfs_export_record_array_alloc, lsm_nfs_export *)
CREATE_FREE_ARRAY_FUNC(lsm_nfs_export_record_array_free,
                       lsm_nfs_export_record_free, lsm_nfs_export *,
                       LSM_ERR_INVALID_ARGUMENT)

MEMBER_FUNC_GET(const char *, lsm_nfs_export, LSM_IS_NFS_EXPORT, id, NULL);
MEMBER_FUNC_GET(const char *, lsm_nfs_export, LSM_IS_NFS_EXPORT, fs_id, NULL);
MEMBER_FUNC_GET(const char *, lsm_nfs_export, LSM_IS_NFS_EXPORT, export_path,
                NULL);
MEMBER_FUNC_GET(const char *, lsm_nfs_export, LSM_IS_NFS_EXPORT, auth_type,
                NULL);
MEMBER_FUNC_GET(lsm_string_list *, lsm_nfs_export, LSM_IS_NFS_EXPORT, root,
                NULL);
MEMBER_FUNC_GET(lsm_string_list *, lsm_nfs_export, LSM_IS_NFS_EXPORT,
                read_write, NULL);
MEMBER_FUNC_GET(lsm_string_list *, lsm_nfs_export, LSM_IS_NFS_EXPORT, read_only,
                NULL);
MEMBER_FUNC_GET(uint64_t, lsm_nfs_export, LSM_IS_NFS_EXPORT, anon_uid,
                LSM_NFS_EXPORT_ANON_UID_GID_ERROR);
MEMBER_FUNC_GET(uint64_t, lsm_nfs_export, LSM_IS_NFS_EXPORT, anon_gid,
                LSM_NFS_EXPORT_ANON_UID_GID_ERROR);
MEMBER_FUNC_GET(const char *, lsm_nfs_export, LSM_IS_NFS_EXPORT, options, NULL);
MEMBER_FUNC_GET(const char *, lsm_nfs_export, LSM_IS_NFS_EXPORT, plugin_data,
                NULL);

int lsm_nfs_export_id_set(lsm_nfs_export *exp, const char *ep) {
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, id, ep, strdup, free,
                   LSM_ERR_INVALID_ARGUMENT);
}

int lsm_nfs_export_fs_id_set(lsm_nfs_export *exp, const char *fs_id) {
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, fs_id, fs_id, strdup, free,
                   LSM_ERR_INVALID_ARGUMENT);
}

int lsm_nfs_export_export_path_set(lsm_nfs_export *exp, const char *ep) {
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, export_path, ep, strdup, free,
                   LSM_ERR_INVALID_ARGUMENT);
}

int lsm_nfs_export_auth_type_set(lsm_nfs_export *exp, const char *auth) {
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, auth_type, auth, strdup, free,
                   LSM_ERR_INVALID_ARGUMENT);
}

int lsm_nfs_export_root_set(lsm_nfs_export *exp, lsm_string_list *root) {
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, root, root, lsm_string_list_copy,
                   lsm_string_list_free, LSM_ERR_INVALID_ARGUMENT);
}

int lsm_nfs_export_read_write_set(lsm_nfs_export *exp,
                                  lsm_string_list *read_write) {
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, read_write, read_write,
                   lsm_string_list_copy, lsm_string_list_free,
                   LSM_ERR_INVALID_ARGUMENT);
}

int lsm_nfs_export_read_only_set(lsm_nfs_export *exp,
                                 lsm_string_list *read_only) {
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, read_only, read_only,
                   lsm_string_list_copy, lsm_string_list_free,
                   LSM_ERR_INVALID_ARGUMENT);
}

int lsm_nfs_export_anon_uid_set(lsm_nfs_export *exp, uint64_t value) {
    MEMBER_SET_VAL(exp, LSM_IS_NFS_EXPORT, anon_uid, value,
                   LSM_ERR_INVALID_ARGUMENT);
}

int lsm_nfs_export_anon_gid_set(lsm_nfs_export *exp, uint64_t value) {
    MEMBER_SET_VAL(exp, LSM_IS_NFS_EXPORT, anon_gid, value,
                   LSM_ERR_INVALID_ARGUMENT);
}

int lsm_nfs_export_options_set(lsm_nfs_export *exp, const char *value) {
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, options, value, strdup, free,
                   LSM_ERR_INVALID_ARGUMENT);
}

lsm_capability_value_type lsm_capability_get(lsm_storage_capabilities *cap,
                                             lsm_capability_type t) {
    lsm_capability_value_type rc = LSM_CAP_UNSUPPORTED;

    if (LSM_IS_CAPABILITY(cap) && (uint32_t)t < cap->len) {
        rc = (lsm_capability_value_type)cap->cap[t];
    }
    return rc;
}

int LSM_DLL_EXPORT lsm_capability_supported(lsm_storage_capabilities *cap,
                                            lsm_capability_type t) {
    if (lsm_capability_get(cap, t) == LSM_CAP_SUPPORTED) {
        return 1;
    }
    return 0;
}

int lsm_capability_set(lsm_storage_capabilities *cap, lsm_capability_type t,
                       lsm_capability_value_type v) {
    int rc = LSM_ERR_INVALID_ARGUMENT;

    if (LSM_IS_CAPABILITY(cap)) {
        if ((uint32_t)t < cap->len) {
            cap->cap[t] = v;
            rc = LSM_ERR_OK;
        }
    }

    return rc;
}

int lsm_capability_set_n(lsm_storage_capabilities *cap,
                         lsm_capability_value_type v, ...) {
    int rc = LSM_ERR_OK;
    int index = 0;

    if (!LSM_IS_CAPABILITY(cap)) {
        return LSM_ERR_INVALID_ARGUMENT;
    }

    va_list var_arg;
    va_start(var_arg, v);

    while ((index = va_arg(var_arg, int)) != -1) {
        if (index < (int)cap->len) {
            cap->cap[index] = v;
        } else {
            rc = LSM_ERR_INVALID_ARGUMENT;
            break;
        }
    }

    va_end(var_arg);
    return rc;
}

static char *bytes_to_string(uint8_t *a, uint32_t len) {
    char *buff = NULL;

    if (a && len) {
        uint32_t i = 0;
        char *tmp = NULL;
        size_t str_len = ((sizeof(char) * 2) * len + 1);
        buff = (char *)malloc(str_len);

        if (buff) {
            tmp = buff;
            for (i = 0; i < len; ++i) {
                tmp += sprintf(tmp, "%02x", a[i]);
            }
            buff[str_len - 1] = '\0';
        }
    }
    return buff;
}

static uint8_t *string_to_bytes(const char *hex_string, uint32_t *l) {
    uint8_t *rc = NULL;

    if (hex_string && l) {
        size_t len = strlen(hex_string);
        if (len && (len % 2) == 0) {
            len /= 2;
            rc = (uint8_t *)malloc(sizeof(uint8_t) * len);
            if (rc) {
                size_t i;
                const char *t = hex_string;
                *l = len;

                for (i = 0; i < len; ++i) {
                    if (1 != sscanf(t, "%02hhx", &rc[i])) {
                        free(rc);
                        rc = NULL;
                        *l = 0;
                        break;
                    }
                    t += 2;
                }
            }
        }
    }
    return rc;
}

lsm_storage_capabilities *lsm_capability_record_alloc(const char *value) {
    lsm_storage_capabilities *rc = NULL;
    rc = (lsm_storage_capabilities *)malloc(
        sizeof(struct _lsm_storage_capabilities));
    if (rc) {
        rc->magic = LSM_CAPABILITIES_MAGIC;

        if (value) {
            rc->cap = string_to_bytes(value, &rc->len);
        } else {
            rc->cap = (uint8_t *)calloc(LSM_CAP_MAX, sizeof(uint8_t));
            if (rc->cap) {
                rc->len = LSM_CAP_MAX;
            }
        }

        if (!rc->cap) {
            lsm_capability_record_free(rc);
            rc = NULL;
        }
    }
    return rc;
}

int lsm_capability_record_free(lsm_storage_capabilities *cap) {
    if (LSM_IS_CAPABILITY(cap)) {
        cap->magic = LSM_DEL_MAGIC(LSM_CAPABILITIES_MAGIC);
        free(cap->cap);
        free(cap);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

char *capability_string(lsm_storage_capabilities *c) {
    char *rc = NULL;
    if (LSM_IS_CAPABILITY(c)) {
        rc = bytes_to_string(c->cap, c->len);
    }
    return rc;
}

lsm_hash *lsm_hash_alloc(void) {
    lsm_hash *rc = NULL;

    rc = (lsm_hash *)malloc(sizeof(lsm_hash));
    if (rc) {
        rc->magic = LSM_HASH_MAGIC;
        rc->data = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
        if (!rc->data) {
            lsm_hash_free(rc);
            rc = NULL;
        }
    }
    return rc;
}

lsm_hash *lsm_hash_copy(lsm_hash *src) {
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    lsm_hash *dest = NULL;
    if (LSM_IS_HASH(src)) {
        dest = lsm_hash_alloc();
        if (dest) {
            /* Walk through each from src and duplicate it to dest */
            g_hash_table_iter_init(&iter, src->data);
            while (g_hash_table_iter_next(&iter, &key, &value)) {
                if (LSM_ERR_OK != lsm_hash_string_set(dest, (const char *)key,
                                                      (const char *)value)) {
                    lsm_hash_free(dest);
                    dest = NULL;
                }
            }
        }
    }
    return dest;
}

int lsm_hash_free(lsm_hash *op) {
    if (LSM_IS_HASH(op)) {
        op->magic = LSM_DEL_MAGIC(LSM_HASH_MAGIC);

        if (op->data) {
            g_hash_table_destroy(op->data);
        }

        free(op);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

int lsm_hash_keys(lsm_hash *op, lsm_string_list **l) {
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    if (LSM_IS_HASH(op)) {
        int count = g_hash_table_size(op->data);

        if (count) {
            *l = lsm_string_list_alloc(0);
            g_hash_table_iter_init(&iter, op->data);
            while (g_hash_table_iter_next(&iter, &key, &value)) {
                if (LSM_ERR_OK != lsm_string_list_append(*l, (char *)key)) {
                    lsm_string_list_free(*l);
                    *l = NULL;
                    return LSM_ERR_NO_MEMORY;
                }
            }
        }
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

const char *lsm_hash_string_get(lsm_hash *op, const char *key) {
    if (LSM_IS_HASH(op)) {
        return (const char *)g_hash_table_lookup(op->data, key);
    }
    return NULL;
}

int lsm_hash_string_set(lsm_hash *op, const char *key, const char *value) {
    if (LSM_IS_HASH(op)) {
        char *k_value = strdup(key);
        char *d_value = strdup(value);

        if (k_value && d_value) {
            g_hash_table_remove(op->data, (gpointer)k_value);
            g_hash_table_insert(op->data, (gpointer)k_value, (gpointer)d_value);
            return LSM_ERR_OK;
        } else {
            free(k_value);
            free(d_value);
            return LSM_ERR_NO_MEMORY;
        }
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

lsm_target_port *lsm_target_port_record_alloc(
    const char *id, lsm_target_port_type port_type, const char *service_address,
    const char *network_address, const char *physical_address,
    const char *physical_name, const char *system_id, const char *plugin_data) {
    lsm_target_port *rc = (lsm_target_port *)calloc(1, sizeof(lsm_target_port));
    if (rc) {
        rc->magic = LSM_TARGET_PORT_MAGIC;
        rc->id = strdup(id);
        rc->type = port_type;
        rc->service_address = strdup(service_address);
        rc->network_address = strdup(network_address);
        rc->physical_address = strdup(physical_address);
        rc->physical_name = strdup(physical_name);
        rc->system_id = strdup(system_id);
        rc->plugin_data = (plugin_data) ? strdup(plugin_data) : NULL;

        if (!rc->id || !rc->service_address || !rc->network_address ||
            !rc->physical_address || !rc->physical_name || !rc->system_id ||
            (plugin_data && !rc->plugin_data)) {
            lsm_target_port_record_free(rc);
            rc = NULL;
        }
    }
    return rc;
}

int lsm_target_port_record_free(lsm_target_port *tp) {
    if (LSM_IS_TARGET_PORT(tp)) {
        tp->magic = LSM_DEL_MAGIC(LSM_TARGET_PORT_MAGIC);
        free(tp->id);
        tp->id = NULL;
        free(tp->plugin_data);
        tp->plugin_data = NULL;
        free(tp->system_id);
        tp->system_id = NULL;
        free(tp->physical_name);
        tp->physical_name = NULL;
        free(tp->physical_address);
        tp->physical_address = NULL;
        free(tp->network_address);
        tp->network_address = NULL;
        free(tp->service_address);
        tp->service_address = NULL;
        free(tp);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

lsm_target_port LSM_DLL_EXPORT *lsm_target_port_copy(lsm_target_port *tp) {
    lsm_target_port *rc = NULL;

    if (LSM_IS_TARGET_PORT(tp)) {
        rc = lsm_target_port_record_alloc(
            tp->id, tp->type, tp->service_address, tp->network_address,
            tp->physical_address, tp->physical_name, tp->system_id,
            tp->plugin_data);
    }
    return rc;
}

MEMBER_FUNC_GET(const char *, lsm_target_port, LSM_IS_TARGET_PORT, id, NULL);
MEMBER_FUNC_GET(lsm_target_port_type, lsm_target_port, LSM_IS_TARGET_PORT, type,
                LSM_TARGET_PORT_TYPE_OTHER);
MEMBER_FUNC_GET(const char *, lsm_target_port, LSM_IS_TARGET_PORT,
                service_address, NULL);
MEMBER_FUNC_GET(const char *, lsm_target_port, LSM_IS_TARGET_PORT,
                network_address, NULL);
MEMBER_FUNC_GET(const char *, lsm_target_port, LSM_IS_TARGET_PORT,
                physical_address, NULL);
MEMBER_FUNC_GET(const char *, lsm_target_port, LSM_IS_TARGET_PORT,
                physical_name, NULL);
MEMBER_FUNC_GET(const char *, lsm_target_port, LSM_IS_TARGET_PORT, system_id,
                NULL);

CREATE_ALLOC_ARRAY_FUNC(lsm_target_port_record_array_alloc, lsm_target_port *)

CREATE_FREE_ARRAY_FUNC(lsm_target_port_record_array_free,
                       lsm_target_port_record_free, lsm_target_port *,
                       LSM_ERR_INVALID_ARGUMENT)

static int reg_ex_match(const char *pattern, const char *str) {
    regex_t start_state;
    int status = 0;
    int rc = regcomp(&start_state, pattern, REG_EXTENDED);

    if (rc) {
        // Development only when changing regular expression
        // fprintf(stderr, "%s: bad pattern: '%s' %d\n", str, pattern, rc);
        return -1;
    }

    status = regexec(&start_state, str, 0, NULL, 0);
    regfree(&start_state);

    return status;
}

int iqn_validate(const char *iqn) {
    if ((iqn && strlen(iqn) > 4) &&
        (0 == strncmp(iqn, "iqn", 3) || 0 == strncmp(iqn, "naa", 3) ||
         0 == strncmp(iqn, "eui", 3))) {
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

int wwpn_validate(const char *wwpn) {
    const char *pattern = "^(0x|0X)?([0-9A-Fa-f]{2})"
                          "(([\\.\\:\\-])?[0-9A-Fa-f]{2}){7}$";
    if (0 == reg_ex_match(pattern, wwpn)) {
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

char *wwpn_convert(const char *wwpn) {
    size_t i = 0;
    size_t out = 0;
    char *rc = NULL;

    if (LSM_ERR_OK == wwpn_validate(wwpn)) {
        rc = (char *)calloc(24, 1);
        size_t len = strlen(wwpn);

        if (wwpn[1] == 'x' || wwpn[1] == 'X') {
            i = 2;
        }

        for (; i < len; ++i) {
            if (wwpn[i] != ':' && wwpn[i] != '-' && wwpn[i] != '.') {
                rc[out++] = tolower(wwpn[i]);
            } else {
                rc[out++] = ':';
            }
        }
    }
    return rc;
}

CREATE_ALLOC_ARRAY_FUNC(lsm_battery_record_array_alloc, lsm_battery *);

lsm_battery *lsm_battery_record_alloc(const char *id, const char *name,
                                      lsm_battery_type type, uint64_t status,
                                      const char *system_id,
                                      const char *plugin_data) {
    lsm_battery *rc = NULL;

    if ((id == NULL) || (name == NULL) || (system_id == NULL))
        return NULL;

    rc = (lsm_battery *)malloc(sizeof(lsm_battery));
    if (rc != NULL) {
        rc->magic = LSM_BATTERY_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->type = type;
        rc->status = status;
        rc->system_id = strdup(system_id);
        rc->plugin_data = NULL;

        if (plugin_data != NULL) {
            rc->plugin_data = strdup(plugin_data);
            if (rc->plugin_data == NULL) {
                lsm_battery_record_free(rc);
                return NULL;
            }
        }

        if (rc->id == NULL || rc->name == NULL || rc->system_id == NULL) {
            lsm_battery_record_free(rc);
            return NULL;
        }
    }
    return rc;
}

int lsm_battery_record_free(lsm_battery *b) {
    if (LSM_IS_BATTERY(b)) {
        b->magic = LSM_DEL_MAGIC(LSM_BATTERY_MAGIC);
        free(b->name);
        b->name = NULL;
        free(b->id);
        b->id = NULL;
        free(b->system_id);
        b->system_id = NULL;
        free(b->plugin_data);
        b->plugin_data = NULL;
        free(b);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ARGUMENT;
}

lsm_battery *lsm_battery_record_copy(lsm_battery *b) {
    if (LSM_IS_BATTERY(b))
        return lsm_battery_record_alloc(b->id, b->name, b->type, b->status,
                                        b->system_id, b->plugin_data);
    return NULL;
}

CREATE_FREE_ARRAY_FUNC(lsm_battery_record_array_free, lsm_battery_record_free,
                       lsm_battery *, LSM_ERR_INVALID_ARGUMENT);

MEMBER_FUNC_GET(const char *, lsm_battery, LSM_IS_BATTERY, id, NULL);
MEMBER_FUNC_GET(const char *, lsm_battery, LSM_IS_BATTERY, name, NULL);
MEMBER_FUNC_GET(const char *, lsm_battery, LSM_IS_BATTERY, system_id, NULL);
MEMBER_FUNC_GET(const char *, lsm_battery, LSM_IS_BATTERY, plugin_data, NULL);
MEMBER_FUNC_GET(uint64_t, lsm_battery, LSM_IS_BATTERY, status,
                LSM_BATTERY_STATUS_UNKNOWN);
MEMBER_FUNC_GET(lsm_battery_type, lsm_battery, LSM_IS_BATTERY, type,
                LSM_BATTERY_TYPE_UNKNOWN);

#ifdef __cplusplus
}
#endif
