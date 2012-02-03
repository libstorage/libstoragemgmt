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

#ifndef  __cplusplus
#define _GNU_SOURCE
#endif

#include <stdio.h>

#include "lsm_datatypes.hpp"
#include <libstoragemgmt/libstoragemgmt_volumes.h>
#include <libstoragemgmt/libstoragemgmt_pool.h>
#include <libstoragemgmt/libstoragemgmt_initiators.h>
#include <libstoragemgmt/libstoragemgmt_types.h>
#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include <libstoragemgmt/libstoragemgmt_error.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>

#ifdef  __cplusplus
extern "C" {
#endif

/*NOTE: Need to change this! */
#define LSM_DEFAULT_PLUGIN_DIR "/tmp/lsm/ipc"


lsmConnectPtr getConnection()
{
    lsmConnectPtr c = (lsmConnectPtr)malloc(sizeof(lsmConnect));
    if (c) {
        memset(c, 0, sizeof(lsmConnect));
        c->magic = LSM_CONNECT_MAGIC;
    }
    return c;
}

void freeConnection(lsmConnectPtr c)
{
    if (c) {

        c->magic = 0;
        c->flags = 0;

        if (c->uri) {
            xmlFreeURI(c->uri);
            c->uri = NULL;
        }

        if (c->error) {
            lsmErrorFree(c->error);
            c->error = NULL;
        }

        if( c->tp ) {
            delete(c->tp);
            c->tp = NULL;
        }

        if( c->raw_uri ) {
            free(c->raw_uri);
            c->raw_uri = NULL;
        }

        free(c);
    }
}

static int establishConnection( lsmConnectPtr c, const char * password,
                                uint32_t timeout, lsmErrorPtr *e)
{
    int rc = LSM_ERR_OK;
    std::map<std::string, Value> params;

    try {
        params["uri"] = Value(c->raw_uri);

        if( password ) {
            params["password"] = Value(password);
        } else {
            params["password"] = Value();
        }
        params["timeout"] = Value(timeout);
        Value p(params);

        c->tp->rpc("startup", p);
    } catch (const ValueException &ve) {
        *e = lsmErrorCreate(LSM_ERROR_SERIALIZATION,
                                LSM_ERR_DOMAIN_FRAME_WORK,
                                LSM_ERR_LEVEL_ERROR, "Error in serialization",
                                ve.what(), NULL, NULL, 0 );
        rc = LSM_ERROR_SERIALIZATION;
    } catch (const LsmException &le) {
        *e = lsmErrorCreate(LSM_ERROR_COMMUNICATION,
                                LSM_ERR_DOMAIN_FRAME_WORK,
                                LSM_ERR_LEVEL_ERROR, "Error in communication",
                                le.what(), NULL, NULL, 0 );
        rc = LSM_ERROR_COMMUNICATION;
    } catch (...) {
        *e = lsmErrorCreate(LSM_ERR_INTERNAL_ERROR,
                                LSM_ERR_DOMAIN_FRAME_WORK,
                                LSM_ERR_LEVEL_ERROR, "Undefined exception",
                                NULL, NULL, NULL, 0 );
        rc = LSM_ERR_INTERNAL_ERROR;
    }
    return rc;
}


int loadDriver(lsmConnectPtr c, xmlURIPtr uri, const char *password,
    uint32_t timeout, lsmErrorPtr *e)
{
    int rc = LSM_ERR_OK;

    const char *plugin_dir = getenv("LSM_UDS_PATH"); //Make this match for all supported languages
    char *plugin_file = NULL;

    if (plugin_dir == NULL) {
        plugin_dir = LSM_DEFAULT_PLUGIN_DIR;
    }

    if (asprintf(&plugin_file, "%s/%s", plugin_dir, uri->scheme) == -1) {
        return LSM_ERR_NO_MEMORY;
    }

    if (access(plugin_file, R_OK) == 0) {
        int ec;
        int sd = Transport::getSocket(std::string(plugin_file), ec);

        if( sd >= 0 ) {
            c->tp = new Ipc(sd);
            if( establishConnection(c, password, timeout, e)) {
                rc = LSM_ERR_PLUGIN_DLOPEN;
            }
        } else {
             *e = lsmErrorCreate(LSM_ERR_PLUGIN_DLOPEN,
                                LSM_ERR_DOMAIN_FRAME_WORK,
                                LSM_ERR_LEVEL_ERROR, "Unable to connect to plugin",
                                NULL, dlerror(), NULL, 0 );

            rc = LSM_ERR_PLUGIN_DLOPEN;
        }
    } else {
        rc = LSM_ERR_PLUGIN_PERMISSIONS;
    }

    free(plugin_file);
    return rc;
}

lsmErrorPtr lsmErrorCreate(lsmErrorNumber code, lsmErrorDomain domain,
    lsmErrorLevel level, const char* msg,
    const char *exception, const char *debug,
    const void *debug_data, uint32_t debug_data_size)
{
    lsmErrorPtr err = (lsmErrorPtr)malloc(sizeof(lsmError));

    if (err) {
        memset(err, 0, sizeof(lsmError));
        err->magic = LSM_ERROR_MAGIC;
        err->code = code;
        err->domain = domain;
        err->level = level;

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
            err->debug = (char *)malloc(debug_data_size);

            if (debug) {
                err->debug_data_size = debug_data_size;
                memcpy(err->debug, debug, debug_data_size);
            }
        }
    }
    return(lsmErrorPtr) err;
}

int lsmErrorFree(lsmErrorPtr e)
{
    if (!LSM_IS_ERROR(e)) {
        return LSM_ERR_INVALID_ERR;
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

    e->magic = 0;
    free(e);

    return LSM_ERR_OK;
}

#define LSM_RETURN_ERR_VAL(type_t, e, x, error) \
        if( LSM_IS_ERROR(e) ) {     \
            return e->x;            \
        }                           \
        return (type_t)error;               \


lsmErrorNumber lsmErrorGetNumber(lsmErrorPtr e)
{
    LSM_RETURN_ERR_VAL(lsmErrorNumber, e, code, -1);
}

lsmErrorDomain lsmErrorGetDomain(lsmErrorPtr e)
{
    LSM_RETURN_ERR_VAL(lsmErrorDomain, e, domain, -1);
}

lsmErrorLevel lsmErrorGetLevel(lsmErrorPtr e)
{
    LSM_RETURN_ERR_VAL(lsmErrorLevel, e, level, -1);
}

char* lsmErrorGetMessage( lsmErrorPtr e)
{
    LSM_RETURN_ERR_VAL(char*, e, message, NULL);
}

char* lsmErrorGetException(lsmErrorPtr e)
{
    LSM_RETURN_ERR_VAL(char*, e, exception, NULL);
}

char* lsmErrorGetDebug(lsmErrorPtr e)
{
    LSM_RETURN_ERR_VAL(char*, e, debug, NULL);
}

void* lsmErrorGetDebugData(lsmErrorPtr e, uint32_t *size)
{
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

lsmPoolPtr *lsmPoolRecordAllocArray(uint32_t size)
{
    lsmPoolPtr *rc = NULL;

    if (size > 0) {
        size_t s = sizeof(lsmPoolPtr) * size;
        rc = (lsmPoolPtr *) malloc(s);
    }
    return rc;
}

lsmPoolPtr lsmPoolRecordAlloc(const char *id, const char *name, uint64_t totalSpace,
    uint64_t freeSpace)
{
    lsmPoolPtr rc = (lsmPoolPtr)malloc(sizeof(lsmPool));
    if (rc) {
        memset(rc, 0, sizeof(lsmPool));
        rc->magic = LSM_POOL_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->totalSpace = totalSpace;
        rc->freeSpace = freeSpace;
    }
    return rc;
}

lsmPoolPtr lsmPoolRecordCopy( lsmPoolPtr toBeCopied)
{
    if( LSM_IS_POOL(toBeCopied) ) {
        return lsmPoolRecordAlloc(toBeCopied->id, toBeCopied->name,
                                    toBeCopied->totalSpace,
                                    toBeCopied->freeSpace);
    }
    return NULL;
}

void lsmPoolRecordFree(lsmPoolPtr p)
{
    if (LSM_IS_POOL(p)) {
        if (p->name) {
            free(p->name);
            p->name = NULL;
        }

        if (p->id) {
            free(p->id);
            p->id = NULL;
        }
        free(p);
    }
}

void lsmPoolRecordFreeArray(lsmPoolPtr pa[], uint32_t size)
{
    if (pa && size) {
        uint32_t i = 0;
        for (i = 0; i < size; ++i) {
            lsmPoolRecordFree(pa[i]);
        }
        free(pa);
    }
}

char *lsmPoolNameGet(lsmPoolPtr p)
{
    if (LSM_IS_POOL(p)) {
        return p->name;
    }
    return NULL;
}

char *lsmPoolIdGet(lsmPoolPtr p)
{
    if (LSM_IS_POOL(p)) {
        return p->id;
    }
    return NULL;
}

uint64_t lsmPoolTotalSpaceGet(lsmPoolPtr p)
{
    if (LSM_IS_POOL(p)) {
        return p->totalSpace;
    }
    return 0;
}

uint64_t lsmPoolFreeSpaceGet(lsmPoolPtr p)
{
    if (LSM_IS_POOL(p)) {
        return p->freeSpace;
    }
    return 0;
}

lsmInitiatorPtr *lsmInitiatorRecordAllocArray(uint32_t size)
{
    lsmInitiator **rc = NULL;

    if (size > 0) {
        size_t s = sizeof(lsmInitiator *) * size;
        rc = (lsmInitiator **) malloc(s);
        memset(rc, 0, s);
    }
    return(lsmInitiatorPtr*) rc;
}

lsmInitiatorPtr lsmInitiatorRecordAlloc(lsmInitiatorType idType, const char* id)
{
    lsmInitiatorPtr rc = (lsmInitiatorPtr)malloc(sizeof(lsmInitiator));
    if (rc) {
        rc->magic = LSM_INIT_MAGIC;
        rc->idType = idType;
        rc->id = strdup(id);
    }
    return rc;
}

lsmInitiatorPtr lsmInitiatorRecordCopy(lsmInitiatorPtr i)
{
    lsmInitiatorPtr rc = NULL;
    if( LSM_IS_INIT(i)) {
        rc = lsmInitiatorRecordAlloc(i->idType, i->id);
    }
    return rc;
}

void lsmInitiatorRecordFree(lsmInitiatorPtr i)
{
    if (i) {
        if (i->id) {
            free(i->id);
            i->id = NULL;
        }
        free(i);
    }
}

void lsmInitiatorRecordFreeArray(lsmInitiatorPtr init[], uint32_t size)
{
    if (init && size) {
        uint32_t i = 0;
        for (i = 0; i < size; ++i) {
            lsmInitiatorRecordFree(init[i]);
        }
        free(init);
    }
}

lsmInitiatorType lsmInitiatorTypeGet(lsmInitiatorPtr i)
{
    return i->idType;
}

char *lsmInitiatorIdGet(lsmInitiatorPtr i)
{
    return i->id;
}

lsmVolumePtr *lsmVolumeRecordAllocArray(uint32_t size)
{
    lsmVolumePtr *rc = NULL;

    if (size > 0) {
        size_t s = sizeof(lsmVolume) * size;
        rc = (lsmVolumePtr *) malloc(s);
        memset(rc, 0, s);
    }
    return rc;
}

lsmVolumePtr lsmVolumeRecordAlloc(const char *id, const char *name,
    const char *vpd83, uint64_t blockSize,
    uint64_t numberOfBlocks,
    uint32_t status)
{
    lsmVolumePtr rc = (lsmVolumePtr)malloc(sizeof(lsmVolume));
    if (rc) {
        rc->magic = LSM_VOL_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->vpd83 = strdup(vpd83);
        rc->blockSize = blockSize;
        rc->numberOfBlocks = numberOfBlocks;
        rc->status = status;
    }
    return rc;
}

lsmVolumePtr lsmVolumeRecordCopy(lsmVolumePtr vol)
{
    lsmVolumePtr rc = NULL;
    if( LSM_IS_VOL(vol) ) {
        rc = lsmVolumeRecordAlloc( vol->id, vol->name, vol->vpd83,
                                    vol->blockSize, vol->numberOfBlocks,
                                    vol->status);
    }
    return rc;
}

void lsmVolumeRecordFree(lsmVolumePtr v)
{
    if (v) {

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
        free(v);
    }
}

void lsmVolumeRecordFreeArray(lsmVolumePtr vol[], uint32_t size)
{
    if (vol && size) {
        uint32_t i = 0;
        for (i = 0; i < size; ++i) {
            lsmVolumeRecordFree(vol[i]);
        }
        free(vol);
    }
}

#define VOL_GET(x, member) \
    x->member;

const char* lsmVolumeIdGet(lsmVolumePtr v)
{
    return VOL_GET(v, id);
}

const char* lsmVolumeNameGet(lsmVolumePtr v)
{
    return VOL_GET(v, name);
}

const char* lsmVolumeVpd83Get(lsmVolumePtr v)
{
    return VOL_GET(v, vpd83);
}

uint64_t lsmVolumeBlockSizeGet(lsmVolumePtr v)
{
    return VOL_GET(v, blockSize);
}

uint64_t lsmVolumeNumberOfBlocks(lsmVolumePtr v)
{
    return VOL_GET(v, numberOfBlocks);
}

uint32_t lsmVolumeOpStatusGet(lsmVolumePtr v)
{
    return VOL_GET(v, status);
}

lsmErrorPtr lsmErrorGetLast(lsmConnectPtr c)
{
    if (LSM_IS_CONNECT(c)) {
        lsmErrorPtr e = c->error;
        c->error = NULL;
        return e;
    }
    return NULL;
}

#ifdef  __cplusplus
}
#endif
