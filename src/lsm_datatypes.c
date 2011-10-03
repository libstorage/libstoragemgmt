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

#define _GNU_SOURCE
#include <stdio.h>

#include "lsm_datatypes.h"
#include <libstoragemgmt/libstoragemgmt_types.h>
#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include <libstoragemgmt/libstoragemgmt_error.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>

/*NOTE: Need to change this! */
#define LSM_DEFAULT_PLUGIN_DIR "./plugin"

int lsmRegisterPlugin( lsmConnectPtr conn, char *desc, char *version,
                        void *private_data,  struct lsmMgmtOps *mgmOps,
						struct lsmSanOps *sanOp, struct lsmFsOps *fsOp,
						struct lsmNasOps *nasOp )
{
	if( NULL == desc || NULL == version ) {
		return LSM_ERR_INVALID_ARGUMENT;
	}

	struct lsmConnect *c = (struct lsmConnect *)conn;
	c->plugin.desc = strdup(desc);
	c->plugin.version = strdup(version);
	c->plugin.privateData = private_data;

	c->plugin.mgmtOps = mgmOps;
	c->plugin.sanOps = sanOp;
	c->plugin.fsOps = fsOp;
	c->plugin.nasOps = nasOp;

	return LSM_ERR_OK;
}

void *lsmGetPrivateData( lsmConnectPtr conn )
{
	if( !LSM_IS_CONNECT(conn)) {
		return NULL;
	}
	return ((struct lsmConnect *)conn)->plugin.privateData;
}

struct lsmConnect *getConnection()
{
	struct lsmConnect *c = malloc(sizeof(struct lsmConnect));
	if( c ) {
		memset(c, 0, sizeof(struct lsmConnect));
		c->magic = LSM_CONNECT_MAGIC;
	}
	return c;
}

void freeConnection(struct lsmConnect *c)
{
	if(c) {

		if( c->plugin.version ) {
			free(c->plugin.version);
			c->plugin.version = NULL;
		}

		if( c->plugin.desc ) {
			free(c->plugin.desc);
			c->plugin.desc = NULL;
		}

		c->magic = 0;
		c->flags = 0;

		if( c->uri ) {
			xmlFreeURI(c->uri);
			c->uri = NULL;
		}

		if( c->handle ) {
			/* What do we want to do with an error here? */
			dlclose(c->handle);
			c->handle = NULL;
		}

		if( c->error ) {
			lsmErrorFree(c->error);
			c->error = NULL;
		}

		free(c);
	}
}

int loadDriver(struct lsmConnect *c, xmlURIPtr uri, char *password,
				uint32_t timeout, lsmErrorPtr *e)
{
	int rc = LSM_ERR_OK;
	lsmRegister reg_plug;

	const char *plugin_dir = getenv("LSM_PLUGIN_DIR");
	char *plugin_file = NULL;

	if( plugin_dir == NULL ) {
		plugin_dir = LSM_DEFAULT_PLUGIN_DIR;
	}

	if( asprintf( &plugin_file, "%s/lsm_plugin_%s.so",
					plugin_dir,uri->scheme) == -1 ) {
		return LSM_ERR_NO_MEMORY;
	}

	if (access(plugin_file, R_OK) == 0) {

		c->handle = dlopen(plugin_file, RTLD_NOW | RTLD_LOCAL);
		if( c->handle ) {

			reg_plug = dlsym(c->handle, "lsmPluginRegister");
			c->unregister = dlsym(c->handle, "lsmPluginUnregister");

			if( reg_plug != NULL && c->unregister != NULL ) {
				/*Note: we should probably pass in the error pointer so that
				 * The plug-in itself and return additional error information.
				 */
				rc = (*reg_plug)((lsmConnectPtr)c, uri, password, timeout);
			} else {
				rc = LSM_ERR_PLUGIN_DLSYM;
			}

		} else {
			*e = lsmErrorCreate(LSM_ERR_PLUGIN_DLOPEN,
								LSM_ERR_DOMAIN_FRAME_WORK,
								LSM_ERR_LEVEL_ERROR,
								"Error on dlopen",
								NULL,
								dlerror(),
								NULL,
								0
								);

			rc = LSM_ERR_PLUGIN_DLOPEN;
		}
	} else {
		rc = LSM_ERR_PLUGIN_PERMISSIONS;
	}

	free(plugin_file);
	return rc;
}


lsmErrorPtr lsmErrorCreate( lsmErrorNumber code, lsmErrorDomain domain,
							lsmErrorLevel level, char* msg, char *exception,
                            char *debug, void *debug_data,
                            uint32_t debug_data_size)
{
	struct lsmError *err = malloc(sizeof(struct lsmError));

	if(err) {
		memset(err, 0, sizeof(struct lsmError));
		err->magic = LSM_ERROR_MAGIC;
		err->code = code;
		err->domain = domain;
		err->level = level;

		if(msg) {
			err->message = strdup(msg);
		}

		if( exception ) {
			err->exception = strdup(exception);
		}

		if( debug ) {
			err->debug = strdup(debug);
		}

		/* We are not going to fail the creation of the error if we cannot
		 * allocate the storage for the debug data.
		 */
		if( debug_data && (debug_data_size > 0) ) {
			err->debug = malloc(debug_data_size);

			if( debug ) {
				err->debug_data_size = debug_data_size;
				memcpy(err->debug, debug, debug_data_size);
			}
		}
	}
	return (lsmErrorPtr)err;
}

int lsmErrorFree(lsmErrorPtr e)
{
	if( !LSM_IS_ERROR(e) ) {
		return LSM_ERR_INVALID_ERR;
	}

	struct lsmError *t = (struct lsmError *)e;

	if( t->debug_data ) {
		free(t->debug_data);
		t->debug_data = NULL;
		t->debug_data_size = 0;
	}

	if( t->debug ) {
		free(t->debug);
		t->debug = NULL;
	}

	if( t->exception ) {
		free(t->exception);
		t->exception = NULL;
	}

	if( t->message ) {
		free(t->message);
		t->message = NULL;
	}

	t->magic = 0;
	free(t);

	return LSM_ERR_OK;
}


int lsmLogError( lsmConnectPtr conn, lsmErrorPtr error) {
	if( !LSM_IS_CONNECT(conn) ) {
		return LSM_ERR_INVALID_CONN;
	}

	if( !LSM_IS_ERROR(error)) {
		return LSM_ERR_INVALID_ERR;
	}

	struct lsmConnect *c = (struct lsmConnect *)conn;

	if( c->error ) {
		lsmErrorFree(c->error);
		c->error = NULL;
	}

	c->error = error;
	return LSM_ERR_OK;
}


#define LSM_RETURN_ERR_VAL(e, x, error)	\
		if( LSM_IS_ERROR(e) ) {		\
			return (((struct lsmError*)e))->x;			\
		}							\
		return error;				\

lsmErrorNumber lsmErrorGetNumber(lsmErrorPtr e)
{
	LSM_RETURN_ERR_VAL(e, code, -1);
}
lsmErrorDomain  lsmErrorGetDomain(lsmErrorPtr e)
{
	LSM_RETURN_ERR_VAL(e, domain, -1);
}
lsmErrorLevel  lsmErrorGetLevel(lsmErrorPtr e)
{
	LSM_RETURN_ERR_VAL(e, level, -1);
}

char* lsmErrorGetMessage(lsmErrorPtr e)
{
	LSM_RETURN_ERR_VAL(e, message, NULL);
}
char* lsmErrorGetException(lsmErrorPtr e)
{
	LSM_RETURN_ERR_VAL(e, exception, NULL);
}
char* lsmErrorGetDebug(lsmErrorPtr e)
{
	LSM_RETURN_ERR_VAL(e, debug, NULL);
}
void* lsmErrorGetDebugData(lsmErrorPtr e, uint32_t *size)
{
	if( LSM_IS_ERROR(e) && size != NULL ) {
		struct lsmError *ep = (struct lsmError*) e;

		if( ep->debug_data ) {
			*size = ep->debug_data_size;
			return ep->debug_data;
		} else {
			*size = 0;
		}
	}
	return NULL;
}

lsmPoolPtr *lsmPoolRecordAllocArray( uint32_t size )
{
    struct lsmPool **rc = NULL;

    if( size > 0) {
        size_t s = sizeof(struct lsmPool *) * size;
        rc = (struct lsmPool **)malloc(s);
    }
    return (lsmPoolPtr*)rc;
}

lsmPoolPtr lsmPoolRecordAlloc(const char *id, const char *name, uint64_t totalSpace,
                                uint64_t freeSpace)
{
    struct lsmPool *rc = malloc( sizeof(struct lsmPool));
    if( rc ) {
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->totalSpace = totalSpace;
        rc->freeSpace = freeSpace;
    }
    return rc;
}

void lsmPoolRecordFree(lsmPoolPtr p)
{
    if( p ) {
        struct lsmPool *t = (struct lsmPool*)p;
        if( t->name ) {
            free(t->name);
            t->name = NULL;
        }

        if( t->id ) {
            free(t->id);
            t->id = NULL;
        }
        free(p);
    }
}

void lsmPoolRecordFreeArray( lsmPoolPtr pa[], uint32_t size )
{
    if( pa && size ) {
		int i = 0;
		for(i = 0; i < size; ++i ) {
			lsmPoolRecordFree(pa[i]);
        }
		free(pa);
    }
}

char *lsmPoolNameGet( lsmPoolPtr p)
{
	return ((struct lsmPool*)p)->name;
}

char *lsmPoolIdGet( lsmPoolPtr p)
{
	return ((struct lsmPool*)p)->id;
}

uint64_t lsmPoolTotalSpaceGet( lsmPoolPtr p)
{
	return ((struct lsmPool*)p)->totalSpace;
}
uint64_t lsmPoolFreeSpaceGet( lsmPoolPtr p)
{
	return ((struct lsmPool*)p)->freeSpace;
}
