/*
 * Copyright (C) 2011-2013 Red Hat, Inc.
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

#include <iostream>
#include <string>
#include <stdio.h>
#include "arguments.h"
#include "lsmcli_func.h"
#include <libstoragemgmt/libstoragemgmt.h>

void debug_plugin(void)
{
    char *debug = getenv("LSM_DEBUG_PLUGIN");
    if( debug ) {
        printf("Attach debugger to plug-in, press any key when ready...");
        ::getchar();
    }
}


int main(int argc, char *argv[])
{
    lsmConnect *c = NULL;
    lsmErrorPtr e = NULL;

    LSM::Arguments a;
    LSM::processCommandLine(argc, argv, a);

    int main_rc = 0;
    int lib_rc = lsmConnectPassword(a.uri.value.c_str(),
                                    a.password.value.c_str(), &c, 30000, &e,
                                    LSM_FLAG_RSVD);

    if( LSM_ERR_OK == lib_rc ) {
        debug_plugin();
        switch( a.c ) {
            case (LSM::LIST) : {
                main_rc = list(a,c);
                break;
            }
            case (LSM::CREATE_VOL) : {
                main_rc = createVolume(a,c);
                break;
            }
            case (LSM::DELETE_VOL) : {
                main_rc = deleteVolume(a,c);
                break;
            }
            case (LSM::REPLICATE) : {
                main_rc = replicateVolume(a,c);
                break;
            }
             case (LSM::RESIZE_VOLUME) : {
                main_rc = resizeVolume(a,c);
                break;
            }
            case (LSM::NONE): {
                break;
            }
        }

        lib_rc = lsmConnectClose(c, LSM_FLAG_RSVD);
        if( LSM_ERR_OK != lib_rc ) {
            printf("Error on close %d!\n", lib_rc);
        }
    } else {
        dumpError(lib_rc, e);
    }
    return main_rc;
}