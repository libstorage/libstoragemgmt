/*
 * Copyright (C) 2011 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Author: tasleson
 */

#ifndef LSM_DATATYPES_H
#define	LSM_DATATYPES_H

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include <libstoragemgmt/libstoragemgmt_common.h>
#include <stdint.h>
#include <libxml/uri.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct lsmVolume {

};

struct lsmPool {
    char *id;
    char *name;
    uint64_t    totalSpace;
    uint64_t    freeSpace;
};

struct lsmInitiator {
};

struct lsmStorageCapabilities {
};

struct lsmAccessGroup {
};


#define LSM_CONNECT_MAGIC       0xFEEDB0B0
#define LSM_IS_CONNECT(obj)     ((obj) && \
                                (((struct lsmConnect*)obj))->magic==LSM_CONNECT_MAGIC)

/**
 * Function pointer decl. for the functions that the plug-in must export.
 */
typedef int (*lsmRegister)(lsmConnectPtr c, xmlURIPtr uri, char *password,
                uint32_t timeout);
typedef int (*lsmUnregister)( lsmConnectPtr c );

struct lsmPlugin {
    char    *desc;
    char    *version;
    void    *privateData;
    struct lsmMgmtOps    *mgmtOps;
    struct lsmSanOps    *sanOps;
    struct lsmNasOps    *nasOps;
    struct lsmFsOps     *fsOps;
};


struct lsmConnect {
    uint32_t    magic;
    uint32_t    flags;
    xmlURIPtr   uri;
    void        *handle;
    lsmError    *error;
    lsmUnregister       unregister;
    struct lsmPlugin   plugin;
};


#define LSM_ERROR_MAGIC       0xDEADB0B0
#define LSM_IS_ERROR(obj)     ((obj) && \
                                (((struct lsmError*)obj))->magic==LSM_ERROR_MAGIC)

struct lsmError {
    uint32_t    magic;
    lsmErrorNumber code;
    lsmErrorDomain domain;
    lsmErrorLevel level;
    uint32_t    reserved;
    char *message;              /**< Human readable error message */
    char *exception;            /**< Exception message if present */
    char *debug;                /**< Debug message */
    void *debug_data;           /**< Debug data */
    uint32_t debug_data_size;     /**< Size of the data */
};

/**
 * Returns a pointer to a newly created connection structure.
 * @return NULL on memory exhaustion, else new connection.
 */
LSM_DLL_LOCAL struct lsmConnect *getConnection();

/**
 * De-allocates the connection.
 * @param c     Connection to free.
 */
LSM_DLL_LOCAL void freeConnection(struct lsmConnect *c);

/**
 * Loads the requester driver specified in the uri.
 * @param c
 * @param uri
 * @param password
 * @param timeout
 * @return LSM_ERR_OK on success, else error code.
 */
LSM_DLL_LOCAL int loadDriver(struct lsmConnect *c, xmlURIPtr uri, char *password,
                                uint32_t timeout, lsmErrorPtr *e);

/**
 * Frees the memory for an individual pool
 * @param p Valid pool
 */
LSM_DLL_LOCAL void lsmPoolRecordFree(lsmPoolPtr p);

#ifdef	__cplusplus
}
#endif

#endif	/* LSM_DATATYPES_H */

