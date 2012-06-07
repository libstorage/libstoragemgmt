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

#include <libstoragemgmt/libstoragemgmt_accessgroups.h>
#include <libstoragemgmt/libstoragemgmt_common.h>
#include <libstoragemgmt/libstoragemgmt_error.h>
#include <libstoragemgmt/libstoragemgmt_fs.h>
#include <libstoragemgmt/libstoragemgmt_initiators.h>
#include <libstoragemgmt/libstoragemgmt_nfsexport.h>
#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include <libstoragemgmt/libstoragemgmt_pool.h>
#include <libstoragemgmt/libstoragemgmt_snapshot.h>
#include <libstoragemgmt/libstoragemgmt_systems.h>
#include <libstoragemgmt/libstoragemgmt_types.h>
#include <libstoragemgmt/libstoragemgmt_volumes.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define LSM_DEFAULT_PLUGIN_DIR "/var/run/lsm/ipc"

lsmStringListPtr lsmStringListAlloc(uint32_t size)
{
    lsmStringList *rc = NULL;

    if( size ) {
        size_t s = sizeof(lsmStringList) + (sizeof(char*) * size);
        rc = (lsmStringList *)calloc(1, s);
        if( rc ) {
            rc->magic = LSM_STRING_LIST_MAGIC;
            rc->size = size;
        }
    }
    return rc;
}

lsmStringListPtr lsmStringListCopy(lsmStringListPtr src)
{
    lsmStringList *dest = NULL;

    if( LSM_IS_STRING_LIST(src) ) {
        dest = lsmStringListAlloc(src->size);

        if( dest ) {
            uint32_t i;
            for( i = 0; i < src->size ; ++i ) {
                if ( LSM_ERR_OK != lsmStringListSetElem(dest, i,
                        lsmStringListGetElem(src, i))) {
                    /** We had an allocation failure setting an element item */
                    lsmStringListFree(dest);
                    dest = NULL;
                    break;
                }
            }
        }
    }
    return dest;
}

int lsmStringListFree(lsmStringListPtr sl)
{
    if( LSM_IS_STRING_LIST(sl) ) {
        uint32_t i;
        for(i = 0; i < sl->size; ++i ) {
            free(sl->values[i]);
            sl->values[i] = '\0';
        }
        sl->magic = LSM_DEL_MAGIC(LSM_STRING_LIST_MAGIC);
        free(sl);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_SL;
}

int lsmStringListSetElem(lsmStringListPtr sl, uint32_t index,
                                            const char* value)
{
    int rc = LSM_ERR_OK;
    if( LSM_IS_STRING_LIST(sl) ) {
        if( index < sl->size ) {
            if( sl->values[index] ) {
                //There is a value here, free it!
                free(sl->values[index]);
            }
            sl->values[index] = strdup(value);
            if( !sl->values[index] ) {
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            rc = LSM_ERR_INDEX_BOUNDS;
        }
    } else {
        rc = LSM_ERR_INVALID_SL;
    }
    return rc;
}

const char *lsmStringListGetElem(lsmStringListPtr sl, uint32_t index)
{
    if( LSM_IS_STRING_LIST(sl) ) {
        if( index < sl->size ) {
            return sl->values[index];
        }
    }
    return NULL;
}

uint32_t lsmStringListSize(lsmStringListPtr sl)
{
    if( LSM_IS_STRING_LIST(sl) ) {
        return sl->size;
    }
    return 0;
}

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

        c->magic = LSM_DEL_MAGIC(LSM_SYSTEM_MAGIC);
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
        *e = lsmErrorCreate(LSM_ERR_TRANS_PORT_SERIALIZATION,
                                LSM_ERR_DOMAIN_FRAME_WORK,
                                LSM_ERR_LEVEL_ERROR, "Error in serialization",
                                ve.what(), NULL, NULL, 0 );
        rc = LSM_ERR_TRANS_PORT_SERIALIZATION;
    } catch (const LsmException &le) {
        *e = lsmErrorCreate(LSM_ERR_TRANSPORT_COMMUNICATION,
                                LSM_ERR_DOMAIN_FRAME_WORK,
                                LSM_ERR_LEVEL_ERROR, "Error in communication",
                                le.what(), NULL, NULL, 0 );
        rc = LSM_ERR_TRANSPORT_COMMUNICATION;
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

        /* Any of these strdup calls could fail, but we will continue*/
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

    e->magic = LSM_DEL_MAGIC(LSM_ERROR_MAGIC);
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

/**
 * When creating arrays of the different types the code is the same.  This
 * macro is used to create type safe code.
 * @param   name    Name of the function
 * @param   rtype   return type
 * @return An array of pointers of rtype
 */
#define CREATE_ALLOC_ARRAY_FUNC(name, rtype)\
rtype *name(uint32_t size)                  \
{                                           \
    rtype *rc = NULL;                       \
    if (size > 0) {                         \
        size_t s = sizeof(rtype) * size;    \
        rc = (rtype *) malloc(s);           \
    }                                       \
    return rc;                              \
}

/**
 * Common macro for freeing the memory associated with one of these
 * data structures.
 * @param name              Name of function to create
 * @param free_func         Function to call to free one of the elements
 * @param record_type       Type to record
 * @return None
 */
#define CREATE_FREE_ARRAY_FUNC(name, free_func, record_type)\
void name( record_type pa[], uint32_t size)                \
{                                                   \
    if (pa && size) {                               \
        uint32_t i = 0;                             \
        for (i = 0; i < size; ++i) {                \
            free_func(pa[i]);                       \
        }                                           \
        free(pa);                                   \
    }                                               \
}


CREATE_ALLOC_ARRAY_FUNC(lsmPoolRecordAllocArray, lsmPoolPtr)

lsmPoolPtr lsmPoolRecordAlloc(const char *id, const char *name,
            uint64_t totalSpace, uint64_t freeSpace, const char *system_id)
{
    lsmPoolPtr rc = (lsmPoolPtr)malloc(sizeof(lsmPool));
    if (rc) {
        memset(rc, 0, sizeof(lsmPool));
        rc->magic = LSM_POOL_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->totalSpace = totalSpace;
        rc->freeSpace = freeSpace;
        rc->system_id = strdup(system_id);

        if( !rc->id || !rc->name || !rc->system_id ) {
            rc->magic = 0;
            free( rc->id );
            free( rc->name );
            free( rc->system_id);
            rc = NULL;
        }
    }
    return rc;
}

lsmPoolPtr lsmPoolRecordCopy( lsmPoolPtr toBeCopied)
{
    if( LSM_IS_POOL(toBeCopied) ) {
        return lsmPoolRecordAlloc(toBeCopied->id, toBeCopied->name,
                                    toBeCopied->totalSpace,
                                    toBeCopied->freeSpace,
                                    toBeCopied->system_id);
    }
    return NULL;
}

void lsmPoolRecordFree(lsmPoolPtr p)
{
    if (LSM_IS_POOL(p)) {
        p->magic = LSM_DEL_MAGIC(LSM_POOL_MAGIC);
        if (p->name) {
            free(p->name);
            p->name = NULL;
        }

        if (p->id) {
            free(p->id);
            p->id = NULL;
        }

        if( p->system_id) {
            free(p->system_id);
            p->system_id = NULL;
        }
        free(p);
    }
}

CREATE_FREE_ARRAY_FUNC(lsmPoolRecordFreeArray, lsmPoolRecordFree, lsmPoolPtr)

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

char *lsmPoolGetSystemId( lsmPoolPtr p )
{
    if (LSM_IS_POOL(p)) {
        return p->system_id;
    }
    return NULL;
}

CREATE_ALLOC_ARRAY_FUNC(lsmInitiatorRecordAllocArray, lsmInitiatorPtr)

lsmInitiatorPtr lsmInitiatorRecordAlloc(lsmInitiatorType idType, const char* id,
                                        const char* name)
{
    lsmInitiatorPtr rc = (lsmInitiatorPtr)malloc(sizeof(lsmInitiator));
    if (rc) {
        rc->magic = LSM_INIT_MAGIC;
        rc->idType = idType;
        rc->id = strdup(id);
        rc->name = strdup(name);

        if(!rc->id || !rc->name ) {
            free(rc->id);
            free(rc->name);
            free(rc);
            rc = NULL;
        }
    }
    return rc;
}

lsmInitiatorPtr lsmInitiatorRecordCopy(lsmInitiatorPtr i)
{
    lsmInitiatorPtr rc = NULL;
    if( LSM_IS_INIT(i)) {
        rc = lsmInitiatorRecordAlloc(i->idType, i->id, i->name);
    }
    return rc;
}

void lsmInitiatorRecordFree(lsmInitiatorPtr i)
{
    if (i) {
        i->magic = LSM_DEL_MAGIC(LSM_INIT_MAGIC);
        if (i->id) {
            free(i->id);
            i->id = NULL;
            free(i->name);
        }
        free(i);
    }
}

CREATE_FREE_ARRAY_FUNC( lsmInitiatorRecordFreeArray, lsmInitiatorRecordFree,
                        lsmInitiatorPtr)

lsmInitiatorType lsmInitiatorTypeGet(lsmInitiatorPtr i)
{
    return i->idType;
}

char *lsmInitiatorIdGet(lsmInitiatorPtr i)
{
    return i->id;
}

char *lsmInitiatorNameGet(lsmInitiatorPtr i)
{
    return i->name;
}

CREATE_ALLOC_ARRAY_FUNC(lsmVolumeRecordAllocArray, lsmVolumePtr)

lsmVolumePtr lsmVolumeRecordAlloc(const char *id, const char *name,
    const char *vpd83, uint64_t blockSize,
    uint64_t numberOfBlocks,
    uint32_t status, const char *system_id)
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
        rc->system_id = strdup(system_id);

        if( !rc->id || !rc->name || !rc->vpd83 || !rc->system_id) {
            free(rc->id);
            free(rc->name);
            free(rc->vpd83);
            free(rc->system_id);
            free(rc);
            rc = NULL;
        }
    }
    return rc;
}

CREATE_ALLOC_ARRAY_FUNC(lsmSystemRecordAllocArray, lsmSystemPtr)

lsmSystemPtr lsmSystemRecordAlloc( const char *id, const char *name)
{
    lsmSystemPtr rc = (lsmSystemPtr)malloc(sizeof(lsmSystem));
    if (rc) {
        rc->magic = LSM_SYSTEM_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        if( !rc->name || !rc->id ) {
            free(rc->name);
            free(rc->id);
            free(rc);
            rc = NULL;
        }
    }
    return rc;
}

void lsmSystemRecordFree(lsmSystemPtr s)
{
    if( LSM_IS_SYSTEM(s) ) {
        free(s->id);
        free(s->name);
        free(s);
    }
}

CREATE_FREE_ARRAY_FUNC(lsmSystemRecordFreeArray, lsmSystemRecordFree, lsmSystemPtr)

lsmSystemPtr lsmSystemRecordCopy(lsmSystemPtr s)
{
    lsmSystemPtr rc = NULL;
    if( LSM_IS_SYSTEM(s) ) {
        rc = lsmSystemRecordAlloc(s->id, s->name);
    }
    return rc;
}

const char *lsmSystemIdGet(lsmSystemPtr s)
{
    if( LSM_IS_SYSTEM(s) ) {
        return s->id;
    }
    return NULL;
}

const char *lsmSystemNameGet(lsmSystemPtr s)
{
    if( LSM_IS_SYSTEM(s) ) {
        return s->name;
    }
    return NULL;
}

lsmVolumePtr lsmVolumeRecordCopy(lsmVolumePtr vol)
{
    lsmVolumePtr rc = NULL;
    if( LSM_IS_VOL(vol) ) {
        rc = lsmVolumeRecordAlloc( vol->id, vol->name, vol->vpd83,
                                    vol->blockSize, vol->numberOfBlocks,
                                    vol->status, vol->system_id);
    }
    return rc;
}

void lsmVolumeRecordFree(lsmVolumePtr v)
{
    if ( LSM_IS_VOL(v) ) {
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

        if( v->system_id ) {
            free(v->system_id);
            v->system_id = NULL;
        }

        free(v);
    }
}

CREATE_FREE_ARRAY_FUNC( lsmVolumeRecordFreeArray, lsmVolumeRecordFree,
                        lsmVolumePtr)

/* We would certainly expand this to encompass the entire function */
#define MEMBER_GET(x, validation, member, error)  \
    if( validation(x) ) {   \
        return x->member;   \
    } else {                \
        return error;       \
    }

/* We would certainly expand this to encompass the entire function */
#define MEMBER_SET_REF(x, validation, member, value, alloc_func, free_func, error)  \
    if( validation(x) ) {                   \
        if(x->member) {                     \
            free_func(x->member);           \
            x->member = NULL;               \
        }                                   \
        if( value ) {                       \
            x->member = alloc_func(value);  \
            if( !x->member ) {              \
                return LSM_ERR_NO_MEMORY;   \
            }                               \
        }                                   \
        return LSM_ERR_OK;                  \
    } else {                                \
        return error;                       \
    }

/* We would certainly expand this to encompass the entire function */
#define MEMBER_SET_VAL(x, validation, member, value, error)  \
    if( validation(x) ) {                   \
        x->member = value;                  \
        return LSM_ERR_OK;                  \
    } else {                                \
        return error;                       \
    }

const char* lsmVolumeIdGet(lsmVolumePtr v)
{
    MEMBER_GET(v, LSM_IS_VOL, id, NULL);
}

const char* lsmVolumeNameGet(lsmVolumePtr v)
{
    MEMBER_GET(v, LSM_IS_VOL, name, NULL);
}

const char* lsmVolumeVpd83Get(lsmVolumePtr v)
{
    MEMBER_GET(v, LSM_IS_VOL, vpd83, NULL);
}

uint64_t lsmVolumeBlockSizeGet(lsmVolumePtr v)
{
    MEMBER_GET(v, LSM_IS_VOL, blockSize, 0);
}

uint64_t lsmVolumeNumberOfBlocks(lsmVolumePtr v)
{
    MEMBER_GET(v, LSM_IS_VOL, numberOfBlocks, 0);
}

uint32_t lsmVolumeOpStatusGet(lsmVolumePtr v)
{
    MEMBER_GET(v, LSM_IS_VOL, status, 0);
}

char LSM_DLL_EXPORT *lsmVolumeGetSystemIdGet( lsmVolumePtr v)
{
    MEMBER_GET(v, LSM_IS_VOL, system_id, NULL);
}

CREATE_ALLOC_ARRAY_FUNC(lsmAccessGroupRecordAllocArray, lsmAccessGroupPtr)

lsmAccessGroupPtr lsmAccessGroupRecordAlloc(const char *id,
                                        const char *name,
                                        lsmStringListPtr initiators,
                                        const char *system_id)
{
    lsmAccessGroup *rc = NULL;
    if( id && name && system_id ) {
        rc = (lsmAccessGroup *)malloc(sizeof(lsmAccessGroup));
        if( rc ) {
            rc->magic = LSM_ACCESS_GROUP_MAGIC;
            rc->id = strdup(id);
            rc->name = strdup(name);
            rc->system_id = strdup(system_id);
            rc->initiators = lsmStringListCopy(initiators);

            if( !rc->id || !rc->name || !rc->system_id ) {
                rc->magic = 0;
                free(rc->id);
                free(rc->name);
                free(rc->system_id);
                lsmStringListFree(rc->initiators);
                free(rc);
                rc = NULL;
            }
        }
    }
    return rc;
}

lsmAccessGroupPtr lsmAccessGroupRecordCopy( lsmAccessGroupPtr ag )
{
    lsmAccessGroup *rc = NULL;
    if( LSM_IS_ACCESS_GROUP(ag) ) {
        rc = lsmAccessGroupRecordAlloc(ag->id, ag->name, ag->initiators, ag->system_id);
    }
    return rc;
}

void lsmAccessGroupRecordFree(lsmAccessGroupPtr ag)
{
    if( LSM_IS_ACCESS_GROUP(ag) ) {
        ag->magic = LSM_DEL_MAGIC(LSM_ACCESS_GROUP_MAGIC);
        free(ag->id);
        free(ag->name);
        free(ag->system_id);
        lsmStringListFree(ag->initiators);
        free(ag);
    }
}

CREATE_FREE_ARRAY_FUNC(lsmAccessGroupRecordFreeArray, lsmAccessGroupRecordFree,
                        lsmAccessGroupPtr)

const char *lsmAccessGroupIdGet( lsmAccessGroupPtr group )
{
    if( LSM_IS_ACCESS_GROUP(group) ) {
        return group->id;
    }
    return NULL;
}
const char *lsmAccessGroupNameGet( lsmAccessGroupPtr group )
{
    if( LSM_IS_ACCESS_GROUP(group) ) {
        return group->name;
    }
    return NULL;
}
const char *lsmAccessGroupSystemIdGet( lsmAccessGroupPtr group )
{
    if( LSM_IS_ACCESS_GROUP(group) ) {
        return group->system_id;
    }
    return NULL;
}

lsmStringListPtr lsmAccessGroupInitiatorIdGet( lsmAccessGroupPtr group )
{
    if( LSM_IS_ACCESS_GROUP(group) ) {
        return group->initiators;
    }
    return NULL;
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

lsmBlockRangePtr lsmBlockRangeRecordAlloc(uint64_t source_start,
                                            uint64_t dest_start,
                                            uint64_t block_count)
{
    lsmBlockRange *rc = NULL;

    rc = (lsmBlockRange*) malloc(sizeof(lsmBlockRange));
    if( rc ) {
        rc->magic = LSM_BLOCK_RANGE_MAGIC;
        rc->source_start = source_start;
        rc->dest_start = dest_start;
        rc->block_count = block_count;
    }
    return rc;
}

void  lsmBlockRangeRecordFree( lsmBlockRangePtr br )
{
    if( LSM_IS_BLOCK_RANGE(br) ) {
        br->magic = LSM_DEL_MAGIC(LSM_BLOCK_RANGE_MAGIC);
        free(br);
    }
}

lsmBlockRangePtr LSM_DLL_EXPORT lsmBlockRangeRecordCopy( lsmBlockRangePtr source )
{
    return lsmBlockRangeRecordAlloc(source->source_start, source->dest_start,
                                                    source->block_count);
}

CREATE_ALLOC_ARRAY_FUNC(lsmBlockRangeRecordAllocArray, lsmBlockRangePtr)
CREATE_FREE_ARRAY_FUNC(lsmBlockRangeRecordFreeArray, lsmBlockRangeRecordFree,
                        lsmBlockRangePtr)


uint64_t lsmBlockRangeSourceStartGet(lsmBlockRangePtr br)
{
    MEMBER_GET(br, LSM_IS_BLOCK_RANGE, source_start, 0);
}

uint64_t lsmBlockRangeDestStartGet(lsmBlockRangePtr br)
{
    MEMBER_GET(br, LSM_IS_BLOCK_RANGE, dest_start, 0);
}

uint64_t lsmBlockRangeBlockCountGet(lsmBlockRangePtr br)
{
    MEMBER_GET(br, LSM_IS_BLOCK_RANGE, block_count, 0);
}

lsmFsPtr lsmFsRecordAlloc( const char *id, const char *name,
                                            uint64_t total_space,
                                            uint64_t free_space,
                                            const char *pool_id,
                                            const char *system_id)
{
    lsmFs *rc = NULL;
    rc = (lsmFs *)malloc(sizeof(lsmFs));
    if( rc ) {
        rc->magic = LSM_FS_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->pool_id = strdup(pool_id);
        rc->system_id = strdup(system_id);
        rc->total_space = total_space;
        rc->free_space = free_space;

        if( !rc->id || !rc->name || !rc->pool_id || !rc->system_id ) {
            free(rc->id);
            free(rc->name);
            free(rc->pool_id);
            free(rc->system_id);

            rc->magic = LSM_DEL_MAGIC(LSM_FS_MAGIC);
            rc = NULL;
        }
    }
    return rc;
}

void lsmFsRecordFree( lsmFsPtr fs)
{
    if( LSM_IS_FS(fs) ) {
        fs->magic = LSM_DEL_MAGIC(LSM_FS_MAGIC);
        free(fs->id);
        free(fs->name);
        free(fs->pool_id);
        free(fs->system_id);
        free(fs);
    }
}

lsmFsPtr lsmFsRecordCopy(lsmFsPtr source)
{
    lsmFs *dest = NULL;

    if( LSM_IS_FS(source) ) {
        dest = lsmFsRecordAlloc(source->id, source->name,
                                source->total_space, source->free_space,
                                source->pool_id,
                                source->system_id);
    }
    return dest;
}

CREATE_ALLOC_ARRAY_FUNC(lsmFsRecordAllocArray, lsmFsPtr)
CREATE_FREE_ARRAY_FUNC(lsmFsRecordFreeArray, lsmFsRecordFree, lsmFsPtr)

const char *lsmFsIdGet(lsmFsPtr fs)
{
    MEMBER_GET(fs, LSM_IS_FS, id, NULL);
}

const char *lsmFsNameGet(lsmFsPtr fs)
{
    MEMBER_GET(fs, LSM_IS_FS, name, NULL);
}

const char *lsmFsSystemIdGet(lsmFsPtr fs)
{
    MEMBER_GET(fs, LSM_IS_FS, system_id, NULL);
}

const char *lsmFsPoolIdGet(lsmFsPtr fs)
{
    MEMBER_GET(fs, LSM_IS_FS, pool_id, NULL);
}

uint64_t lsmFsTotalSpaceGet(lsmFsPtr fs)
{
    MEMBER_GET(fs, LSM_IS_FS, total_space, 0);
}
uint64_t lsmFsFreeSpaceGet(lsmFsPtr fs)
{
    MEMBER_GET(fs, LSM_IS_FS, free_space, 0);
}

lsmSsPtr lsmSsRecordAlloc( const char *id, const char *name,
                                            uint64_t ts)
{
    lsmSs *rc = (lsmSs *) malloc(sizeof(lsmSs));
    if( rc ) {
        rc->magic = LSM_SS_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->ts = ts;

        if( !rc->id || ! rc->name ) {
            rc->magic = LSM_DEL_MAGIC(LSM_SS_MAGIC);
            free(rc->id);
            free(rc->name);
            free(rc);
            rc = NULL;
        }
    }
    return rc;
}

void lsmSsRecordFree( lsmSsPtr ss)
{
    if( LSM_IS_SS(ss) ) {
        ss->magic = LSM_DEL_MAGIC(LSM_SS_MAGIC);
        free(ss->id);
        free(ss->name);
        free(ss);
    }
}

CREATE_ALLOC_ARRAY_FUNC(lsmSsRecordAllocArray, lsmSsPtr)
CREATE_FREE_ARRAY_FUNC(lsmSsRecordFreeArray, lsmSsRecordFree, lsmSsPtr)


const char *lsmSsIdGet(lsmSsPtr ss)
{
    MEMBER_GET(ss, LSM_IS_SS, id, NULL);
}

const char *lsmSsNameGet(lsmSsPtr ss)
{
    MEMBER_GET(ss, LSM_IS_SS, name, NULL);
}

uint64_t lsmSsTimeStampGet(lsmSsPtr ss)
{
    MEMBER_GET(ss, LSM_IS_SS, ts, 0);
}

lsmNfsExportPtr lsmNfsExportRecordAlloc( const char *id,
                                            const char *fs_id,
                                            const char *export_path,
                                            const char *auth,
                                            lsmStringListPtr root,
                                            lsmStringListPtr rw,
                                            lsmStringListPtr ro,
                                            uint64_t anonuid,
                                            uint64_t anongid,
                                            const char *options)
{
     lsmNfsExportPtr rc = NULL;

    /* These are required */
    if( fs_id && export_path ) {
        rc = (lsmNfsExportPtr)malloc(sizeof(lsmNfsExport));
        if( rc ) {
            rc->magic = LSM_NFS_EXPORT_MAGIC;
            rc->id = (id) ? strdup(id) : NULL;
            rc->fs_id = strdup(fs_id);
            rc->export_path = strdup(export_path);
            rc->auth_type = (auth) ? strdup(auth) : NULL;
            rc->root = lsmStringListCopy(root);
            rc->rw = lsmStringListCopy(rw);
            rc->ro = lsmStringListCopy(ro);
            rc->anonuid = anonuid;
            rc->anongid = anongid;
            rc->options = (options) ? strdup(options) : NULL;

            if( !rc->fs_id || !rc->export_path ) {
                lsmNfsExportRecordFree(rc);
                rc = NULL;
            }
        }
    }

    return rc;
}

void lsmNfsExportRecordFree( lsmNfsExportPtr exp )
{
    if( LSM_IS_NFS_EXPORT(exp) ) {
        exp->magic = LSM_DEL_MAGIC(LSM_NFS_EXPORT_MAGIC);
        free(exp->id);
        free(exp->fs_id);
        free(exp->auth_type);
        lsmStringListFree(exp->root);
        lsmStringListFree(exp->rw);
        lsmStringListFree(exp->ro);
        free(exp->options);
    }
}


lsmNfsExportPtr lsmNfsExportRecordCopy( lsmNfsExportPtr s )
{
    return lsmNfsExportRecordAlloc(s->id, s->fs_id, s->export_path,
                            s->auth_type, s->root, s->rw, s->ro, s->anonuid,
                            s->anongid, s->options);
}

CREATE_ALLOC_ARRAY_FUNC(lsmNfsExportRecordAllocArray, lsmNfsExportPtr)
CREATE_FREE_ARRAY_FUNC(lsmNfsExportRecordFreeArray, lsmNfsExportRecordFree,
                        lsmNfsExportPtr)

const char *lsmNfsExportIdGet( lsmNfsExportPtr exp )
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, id, NULL);
}

int lsmNfsExportIdSet(lsmNfsExportPtr exp, const char *ep )
{
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, id, ep, strdup, free,
                LSM_ERR_INVALID_NFS);
}

const char *lsmNfsExportFsIdGet( lsmNfsExportPtr exp )
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, fs_id, NULL);
}

int lsmNfsExportFsIdSet( lsmNfsExportPtr exp, const char *fs_id)
{
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, fs_id, fs_id, strdup, free,
                LSM_ERR_INVALID_NFS);
}

const char *lsmNfsExportExportPathGet( lsmNfsExportPtr exp )
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, export_path, NULL);
}

int lsmNfsExportExportPathSet( lsmNfsExportPtr exp, const char *ep )
{
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, export_path, ep, strdup, free,
                LSM_ERR_INVALID_NFS);
}

const char *lsmNfsExportAuthTypeGet( lsmNfsExportPtr exp )
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, auth_type, NULL);
}

int lsmNfsExportAuthTypeSet( lsmNfsExportPtr exp, const char *auth )
{
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, auth_type, auth, strdup, free,
                LSM_ERR_INVALID_NFS);
}

lsmStringListPtr lsmNfsExportRootGet( lsmNfsExportPtr exp)
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, root, NULL);
}

int lsmNfsExportRootSet( lsmNfsExportPtr exp, lsmStringListPtr root)
{
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, root, root, lsmStringListCopy,
                lsmStringListFree, LSM_ERR_INVALID_NFS);
}

lsmStringListPtr lsmNfsExportReadWriteGet( lsmNfsExportPtr exp)
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, rw, NULL);
}

int lsmNfsExportReadWriteSet( lsmNfsExportPtr exp, lsmStringListPtr rw)
{
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, rw, rw, lsmStringListCopy,
                lsmStringListFree, LSM_ERR_INVALID_NFS);
}

lsmStringListPtr lsmNfsExportReadOnlyGet( lsmNfsExportPtr exp)
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, ro, NULL);
}

int lsmNfsExportReadOnlySet(lsmNfsExportPtr exp, lsmStringListPtr ro)
{
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, ro, ro, lsmStringListCopy,
                lsmStringListFree, LSM_ERR_INVALID_NFS);
}

uint64_t lsmNfsExportAnonUidGet( lsmNfsExportPtr exp )
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, anonuid, ANON_UID_GID_ERROR);
}

int lsmNfsExportAnonUidSet( lsmNfsExportPtr exp, uint64_t value)
{
    MEMBER_SET_VAL(exp, LSM_IS_NFS_EXPORT, anonuid, value,
                    LSM_ERR_INVALID_NFS);
}

uint64_t lsmNfsExportAnonGidGet( lsmNfsExportPtr exp )
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, anongid, ANON_UID_GID_ERROR);
}

int lsmNfsExportAnonGidSet( lsmNfsExportPtr exp, uint64_t value )
{
    MEMBER_SET_VAL(exp, LSM_IS_NFS_EXPORT, anongid, value,
                    LSM_ERR_INVALID_NFS);
}

const char *lsmNfsExportOptionsGet( lsmNfsExportPtr exp)
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, options, NULL);
}

int lsmNfsExportOptionsSet( lsmNfsExportPtr exp, const char *value )
{
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, options, value, strdup, free,
                LSM_ERR_INVALID_NFS);
}

#ifdef  __cplusplus
}
#endif
