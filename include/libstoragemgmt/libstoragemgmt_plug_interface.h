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
 * File:   libstoragemgmt_plug_interface.h
 * Author: tasleson
 */

#ifndef LIBSTORAGEMGMT_PLUG_INTERFACE_H
#define	LIBSTORAGEMGMT_PLUG_INTERFACE_H

#include <stdint.h>
#include <libstoragemgmt/libstoragemgmt_types.h>
#include <libstoragemgmt/libstoragemgmt_error.h>


#ifdef	__cplusplus
extern "C" {
#endif


typedef int (*lsmPlugSetTmo)( lsmConnectPtr c, uint32_t timeout );
typedef int (*lsmPlugGetTmo)( lsmConnectPtr c, uint32_t *timeout );
typedef int (*lsmPlugCapabilities)(lsmConnectPtr conn,
                                        lsmStorageCapabilitiesPtr *cap);


typedef int (*lsmPlugPoolList)(lsmConnectPtr conn, lsmPoolPtr *poolArray,
                        uint32_t *count);

struct lsmMgmtOps {
    lsmPlugSetTmo    tmo_set;
    lsmPlugGetTmo    tmo_get;
    lsmPlugCapabilities     capablities;
};

typedef int (*lsmPlugGetVolumes)( lsmConnectPtr c, lsmPoolPtr **poolArray,
                                        uint32_t *count);

/**
 * Block oriented functions
 */
struct lsmSanOps {
    lsmPlugGetVolumes volumes_get;
};

/**
 * File system oriented functionality
 */
struct lsmFsOps {

};

/**
 * NAS system oriented functionality
 */
struct lsmNasOps {

};

int lsmRegisterPlugin( lsmConnectPtr conn, char *desc, char *version,
                        void * private_data, struct lsmMgmtOps *mgmOps,
                        struct lsmSanOps *sanOp, struct lsmFsOps *fsOp,
                        struct lsmNasOps *nasOp );
void *lsmGetPrivateData( lsmConnectPtr conn );

int lsmLogError( lsmConnectPtr conn, lsmErrorPtr error);

/**
 * Creates an error record.
 * @param code
 * @param domain
 * @param level
 * @param msg
 * @param exception
 * @param debug
 * @param debug_data
 * @param debug_data_size
 * @return Null on error, else valid error error record.
 */
lsmErrorPtr     lsmErrorCreate( lsmErrorNumber code, lsmErrorDomain domain,
                                lsmErrorLevel level, char* msg, char *exception,
                                char *debug, void *debug_data,
                                uint32_t debug_data_size);


lsmPoolPtr *lsmPoolRecordAllocArray( uint32_t size );
lsmPoolPtr lsmPoolRecordAlloc(const char *id, const char *name,
                                uint64_t totalSpace,
                                uint64_t freeSpace);

#ifdef	__cplusplus
}
#endif

#endif	/* LIBSTORAGEMGMT_PLUG_INTERFACE_H */

