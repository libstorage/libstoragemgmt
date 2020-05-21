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

#ifndef LSM_DATATYPES_H
#define LSM_DATATYPES_H

#include "libstoragemgmt/libstoragemgmt_common.h"
#include "libstoragemgmt/libstoragemgmt_plug_interface.h"
#include "libxml/uri.h"
#include "lsm_ipc.hpp"
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Helper macros to ease getter functions */
#define MEMBER_FUNC_GET(return_type, arg_type, validation, member, error)      \
    return_type arg_type##_##member##_get(arg_type *x) {                       \
        if (validation(x)) {                                                   \
            return x->member;                                                  \
        } else {                                                               \
            return error;                                                      \
        }                                                                      \
    }

#define MAGIC_CHECK(obj, m)       ((obj) && ((obj)->magic == (m)))
#define LSM_DEL_MAGIC(obj)        ((obj & 0x0FFFFFFF) | 0xD0000000)
#define LSM_VOL_MAGIC             0xAA7A0000
#define LSM_IS_VOL(obj)           MAGIC_CHECK(obj, LSM_VOL_MAGIC)
#define LSM_FLAG_UNUSED_CHECK(x)  (x != 0)
#define LSM_FLAG_GET_VALUE(x)     x["flags"].asUint64_t()
#define LSM_FLAG_EXPECTED_TYPE(x) (Value::numeric_t == x["flags"].valueType())
/**
 * Information about storage volumes.
 */
struct LSM_DLL_LOCAL _lsm_volume {
    uint32_t magic;
    char *id;                  /**< System wide unique identifier */
    char *name;                /**< Human recognizeable name */
    char *vpd83;               /**< SCSI page 83 unique ID */
    uint64_t block_size;       /**< Block size */
    uint64_t number_of_blocks; /**< Number of blocks */
    uint32_t admin_state;      /**< Status */
    char *system_id;           /**< System this volume belongs */
    char *pool_id;             /**< Pool this volume is derived from */
    char *plugin_data;         /**< Private data for plugin */
};

#define LSM_POOL_MAGIC   0xAA7A0001
#define LSM_IS_POOL(obj) MAGIC_CHECK(obj, LSM_POOL_MAGIC)

/**
 * Information about storage pools.
 */
struct LSM_DLL_LOCAL _lsm_pool {
    uint32_t magic;               /**< Used for verfication */
    char *id;                     /**< System wide unique identifier */
    char *name;                   /**< Human recognizeable name */
    uint64_t element_type;        /**< What the pool can be used for */
    uint64_t unsupported_actions; /**< What pool cannot be used for */
    uint64_t total_space;         /**< Total size */
    uint64_t free_space;          /**< Free space available */
    uint64_t status;              /**< Status of pool */
    char *status_info;            /**< Status info for pool */
    char *system_id;              /**< system id */
    char *plugin_data;            /**< Private data for plugin */
};

#define LSM_ACCESS_GROUP_MAGIC   0xAA7A0003
#define LSM_IS_ACCESS_GROUP(obj) MAGIC_CHECK(obj, LSM_ACCESS_GROUP_MAGIC)

/**
 * Information pertaining to a storage group.
 */
struct _lsm_access_group {
    uint32_t magic;  /**< Used for verification */
    char *id;        /**< Id */
    char *name;      /**< Name */
    char *system_id; /**< System id */
    lsm_access_group_init_type init_type;
    /**< Init type */
    lsm_string_list *initiators;
    /**< List of initiators */
    char *plugin_data; /**< Reserved for the plugin to use */
};

#define LSM_NFS_EXPORT_MAGIC   0xAA7A0006
#define LSM_IS_NFS_EXPORT(obj) MAGIC_CHECK(obj, LSM_NFS_EXPORT_MAGIC)

/**
 * Structure for NFS export information
 */
struct _lsm_nfs_export {
    uint32_t magic;              /**< Used for verfication */
    char *id;                    /**< Id */
    char *fs_id;                 /**< File system id */
    char *export_path;           /**< Export path */
    char *auth_type;             /**< Supported authentication types */
    lsm_string_list *root;       /**< List of hosts with root access */
    lsm_string_list *read_write; /**< List of hosts with read & write access */
    lsm_string_list *read_only;  /**< List of hosts with read only access */
    uint64_t anon_uid;           /**< Uid that should map to anonymous */
    uint64_t anon_gid;           /**< Gid that should map to anonymous */
    char *options;               /**< Options */
    char *plugin_data;           /**< Reserved for the plugin to use */
};

#define LSM_BLOCK_RANGE_MAGIC   0xAA7A0007
#define LSM_IS_BLOCK_RANGE(obj) MAGIC_CHECK(obj, LSM_BLOCK_RANGE_MAGIC)

/**
 * Structure for block range ( a region to be replicated )
 */
struct _lsm_block_range {
    uint32_t magic;        /**< Used for verification */
    uint64_t source_start; /**< Source address */
    uint64_t dest_start;   /**< Dest address */
    uint64_t block_count;  /**< Number of blocks */
};

#define LSM_CAPABILITIES_MAGIC 0xAA7A0008
#define LSM_IS_CAPABILITY(obj) MAGIC_CHECK(obj, LSM_CAPABILITIES_MAGIC)

#define LSM_CAP_MAX 512

/**
 * Capabilities of the plug-in and storage array.
 */
struct _lsm_storage_capabilities {
    uint32_t magic; /**< Used for verification */
    uint32_t len;   /**< Len of cap field */
    uint8_t *cap;   /**< Capacity data */
};

#define LSM_SYSTEM_MAGIC   0xAA7A0009
#define LSM_IS_SYSTEM(obj) MAGIC_CHECK(obj, LSM_SYSTEM_MAGIC)

/**
 * Structure for a system
 */
struct _lsm_system {
    uint32_t magic;            /**< Used for verification */
    char *id;                  /**< Id */
    char *name;                /**< Name */
    uint32_t status;           /**< Enumerated status value */
    char *status_info;         /**< System status text */
    char *plugin_data;         /**< Reserved for the plugin to use */
    const char *fw_version;    /**< Firmware version */
    lsm_system_mode_type mode; /**< System mode */
    int read_cache_pct;        /**< Read cache percentage */
};

#define LSM_CONNECT_MAGIC   0xAA7A000A
#define LSM_IS_CONNECT(obj) MAGIC_CHECK(obj, LSM_CONNECT_MAGIC)

#define LSM_PLUGIN_MAGIC   0xAA7A000B
#define LSM_IS_PLUGIN(obj) MAGIC_CHECK(obj, LSM_PLUGIN_MAGIC)

/**
 * Information pertaining to the plug-in specifics.
 */
struct LSM_DLL_LOCAL _lsm_plugin {
    uint32_t magic;              /**< Magic, used for structure validation */
    Ipc *tp;                     /**< IPC transport */
    char *desc;                  /**< Description */
    char *version;               /**< Version */
    void *private_data;          /**< Private data for plug-in */
    lsm_error *error;            /**< Error information */
    lsm_plugin_register reg;     /**< Plug-in registration */
    lsm_plugin_unregister unreg; /**< Plug-in unregistration */
    struct lsm_mgmt_ops_v1 *mgmt_ops; /**< Callback for management ops */
    struct lsm_san_ops_v1 *san_ops;   /**< Callbacks for SAN ops */
    struct lsm_nas_ops_v1 *nas_ops;   /**< Callbacks for NAS ops */
    struct lsm_fs_ops_v1 *fs_ops;     /**< Callbacks for fs ops */
    struct lsm_ops_v1_2 *ops_v1_2;    /**< Callbacks for v1.2 ops */
    struct lsm_ops_v1_3 *ops_v1_3;    /**< Callbacks for v1.3 ops */
};

/**
 * Information pertaining to the connection.  This is the main structure and
 * opaque data type for the library.
 */
struct LSM_DLL_LOCAL _lsm_connect {
    uint32_t magic;   /**< Magic, used for structure validation */
    uint32_t flags;   /**< Flags for the connection */
    xmlURIPtr uri;    /**< URI */
    char *raw_uri;    /**< Raw URI string */
    lsm_error *error; /**< Error information */
    Ipc *tp;          /**< IPC transport */
};

#define LSM_ERROR_MAGIC   0xAA7A000C
#define LSM_IS_ERROR(obj) MAGIC_CHECK(obj, LSM_ERROR_MAGIC)

/**
 * Used to house error information.
 */
struct LSM_DLL_LOCAL _lsm_error {
    uint32_t magic;        /**< Magic, used for struct validation */
    lsm_error_number code; /**< Error code */
    uint32_t reserved;     /**< Reserved */
    char *message;         /**< Human readable error message */
    char *exception;       /**< Exception message if present */
    char *debug;           /**< Debug message */
    void *debug_data;      /**< Debug data */
    uint32_t debug_data_size;
    /**< Size of the data */
};

/**
 * Used to house string collection.
 */
#define LSM_STRING_LIST_MAGIC   0xAA7A000D
#define LSM_IS_STRING_LIST(obj) MAGIC_CHECK(obj, LSM_STRING_LIST_MAGIC)
struct LSM_DLL_LOCAL _lsm_string_list {
    uint32_t magic; /**< Magic value */
    GPtrArray *values;
};

/**
 * Structure for File system information.
 */
#define LSM_FS_MAGIC   0xAA7A000E
#define LSM_IS_FS(obj) MAGIC_CHECK(obj, LSM_FS_MAGIC)
struct LSM_DLL_LOCAL _lsm_fs {
    uint32_t magic;       /**< Magic, used for struct validation */
    char *id;             /**< Id */
    char *name;           /**< Name */
    char *pool_id;        /**< Pool ID */
    uint64_t total_space; /**< Total space */
    uint64_t free_space;  /**< Free space */
    char *system_id;      /**< System ID */
    char *plugin_data;    /**< Plugin private data */
};

#define LSM_SS_MAGIC   0xAA7A000F
#define LSM_IS_SS(obj) MAGIC_CHECK(obj, LSM_SS_MAGIC)
struct LSM_DLL_LOCAL _lsm_fs_ss {
    uint32_t magic;
    char *id;
    char *name;
    uint64_t time_stamp;
    char *plugin_data; /**< Reserved for the plugin to use */
};

#define LSM_DISK_MAGIC   0xAA7A0010
#define LSM_IS_DISK(obj) MAGIC_CHECK(obj, LSM_DISK_MAGIC)
struct LSM_DLL_LOCAL _lsm_disk {
    uint32_t magic;
    char *id;
    char *name;
    lsm_disk_type type;
    uint64_t block_size;
    uint64_t number_of_blocks;
    uint64_t status; /* Bit field */
    char *system_id;
    char *vpd83;
    const char *location;
    int32_t rpm;
    lsm_disk_link_type link_type;
};

#define LSM_HASH_MAGIC   0xAA7A0011
#define LSM_IS_HASH(obj) MAGIC_CHECK(obj, LSM_HASH_MAGIC)
struct LSM_DLL_LOCAL _lsm_hash {
    uint32_t magic;
    GHashTable *data;
};

#define LSM_TARGET_PORT_MAGIC   0xAA7A0012
#define LSM_IS_TARGET_PORT(obj) MAGIC_CHECK(obj, LSM_TARGET_PORT_MAGIC)
struct LSM_DLL_LOCAL _lsm_target_port {
    uint32_t magic;
    char *id;
    lsm_target_port_type type;
    char *service_address;
    char *network_address;
    char *physical_address;
    char *physical_name;
    char *system_id;
    char *plugin_data;
};

#define LSM_BATTERY_MAGIC   0xAA7A0013
#define LSM_IS_BATTERY(obj) MAGIC_CHECK(obj, LSM_BATTERY_MAGIC)
struct LSM_DLL_LOCAL _lsm_battery {
    uint32_t magic;
    char *id;
    char *name;
    lsm_battery_type type;
    uint64_t status;
    char *system_id;
    char *plugin_data;
};

/**
 * Returns a pointer to a newly created connection structure.
 * @return NULL on memory exhaustion, else new connection.
 */
lsm_connect LSM_DLL_LOCAL *connection_get();

/**
 * De-allocates the connection.
 * @param c     Connection to free.
 */
void LSM_DLL_LOCAL connection_free(lsm_connect *c);

/**
 * Loads the requester driver specified in the uri.
 * @param c             Connection
 * @param plugin        Short name of plugin
 * @param password      Password
 * @param timeout       Initial timeout
 * @param e             Error data
 * @param startup       If non zero call rpc start_up, else skip
 * @param flags         Reserved flag for future use
 * @return LSM_ERR_OK on success, else error code.
 */
int LSM_DLL_LOCAL driver_load(lsm_connect *c, const char *plugin,
                              const char *password, uint32_t timeout,
                              lsm_error_ptr *e, int startup, lsm_flag flags);

char LSM_DLL_LOCAL *capability_string(lsm_storage_capabilities *c);

const char LSM_DLL_LOCAL *uds_path(void);

/**
 * Take a character string and tries to convert to a number.
 * Note: Number is defined as what is acceptable for JSON number.  The number
 * is represented by int64_t if possible, else uint64_t and then long double.
 * @param str_num       Character string containing number
 * @param si            Signed 64 bit number
 * @param ui            Unsigned 64 bit number
 * @param d             Long double
 * @return              -1 = Invalid string pointer
 *                       0 = Not a number
 *                       1 = Number converted to signed integer, value in si
 *                       2 = Number converted to unsigned integer, value in ui
 *                       3 = Number converted to long double, value in d
 */
int LSM_DLL_LOCAL number_convert(const char *str_num, int64_t *si, uint64_t *ui,
                                 long double *d);

/**
 * Validates an iSCSI IQN
 * @param iqn   iSCSI iqn to check
 * @return LSM_ERR_OK on success, else LSM_ERR_INVALID_ARGUMENT
 */
int LSM_DLL_LOCAL iqn_validate(const char *iqn);

/**
 * Validates an WWPN
 * @param wwpn   wwpn to check
 * @return LSM_ERR_OK on success, else LSM_ERR_INVALID_ARGUMENT
 */
int LSM_DLL_LOCAL wwpn_validate(const char *wwpn);

/**
 * Given a WWPN validate it and then convert to internal representation.
 * @param wwpn      World wide port name to validate
 * @return NULL if not patch, else string with common lsm format
 */
char LSM_DLL_LOCAL *wwpn_convert(const char *wwpn);

#ifdef __cplusplus
}
#endif
#endif /* LSM_DATATYPES_H */
