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
#include "lsmcli_func.h"
#include "arguments.h"
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#define __STDC_FORMAT_MACROS    /* To use PRIu64 */
#include <inttypes.h>
#include <unistd.h>

lsmInitiatorPtr getInitiator(lsmConnectPtr c, std::string initId);

void printVolume(const LSM::Arguments &a, lsmVolumePtr v)
{
    const char *id = lsmVolumeIdGet(v);
    const char *name = lsmVolumeNameGet(v);
    const char *vpd = lsmVolumeVpd83Get(v);
    uint64_t block_size = lsmVolumeBlockSizeGet(v);
    uint64_t block_num = lsmVolumeNumberOfBlocks(v);
    uint32_t status = lsmVolumeOpStatusGet(v);
    uint64_t size = block_size * block_num;
    std::string s = LSM::sizeHuman(a.human.present, size);

    if( a.terse.present ) {
        const char *sep = a.terse.value.c_str();

        printf("%s%s%s%s%s%s%"PRIu64"%s%"PRIu64"%s%u%s%s\n", id, sep, name,
                sep, vpd, sep, block_size, sep, block_num, sep,
                status, sep, s.c_str());
    } else {
        printf("%s %-40s\t%s %-8"PRIu64"\t%-17"PRIu64"\t%u\t%20s\n", id, name,
              vpd, block_size, block_num, status, s.c_str());
    }
}

void printInitiator(const LSM::Arguments &a, lsmInitiatorPtr i)
{
    const char format[] = "%-40s%-16s%-5d\n";
    const char *sep = NULL;

    const char *id = lsmInitiatorIdGet(i);
    const char *name = lsmInitiatorNameGet(i);
    lsmInitiatorType type = lsmInitiatorTypeGet(i);

    if( a.terse.present ) {
        sep = a.terse.value.c_str();
    }

    if( a.terse.present ) {
        printf("%s%s%s%s%d\n", id, sep, name, sep, type);
    } else {
        printf(format, id, name, type);
    }
}

int waitForJob(int cmd_rc, lsmConnectPtr c, char *job,
                const LSM::Arguments &a, lsmVolumePtr *vol)
{
    lsmVolumePtr new_volume = NULL;
    int rc = cmd_rc;
    //Check to see if we are done!
    if( LSM_ERR_OK != rc ) {
        if ( LSM_ERR_JOB_STARTED == rc ) {
            //We have a job to wait for.
            lsmJobStatus status;
            uint8_t percent = 0;

            do {
                usleep(10000);
                rc = lsmJobStatusVolumeGet(c, job, &status, &percent, &new_volume);
                //printf("job = %s, status %d, percent %d\n", job, status, percent);
            } while ( (LSM_JOB_INPROGRESS == status) && (LSM_ERR_OK == rc) );

            if( vol != NULL) {
                *vol = new_volume;
            } else {
                if( new_volume ) {
                    lsmVolumeRecordFree(new_volume);
                    new_volume = NULL;
                }
            }

            if( (LSM_ERR_OK == rc) && (LSM_JOB_COMPLETE == status) && new_volume ) {
                printVolume(a, new_volume);
            } else {
                printf("RC = %d, job = %s, status %d, percent %d\n", rc, job, status, percent);
            }

            //Clean up the job
            int jf = lsmJobFree(c, &job);
            if( LSM_ERR_OK != jf ) {
                printf("lsmJobFree rc= %d\n", jf);
            }
            assert(LSM_ERR_OK == jf);

        } else {
            dumpError(rc, lsmErrorGetLast(c));
        }
    } else {
        if( vol ) {
            printVolume(a, *vol);
        }
    }

    if( vol ) {
        lsmVolumeRecordFree(*vol);
    }

    return rc;
}

void dumpError(int ec, lsmErrorPtr e)
{
    printf("Error occurred: %d\n", ec);

    if( e ) {
        printf("Msg: %s\n", lsmErrorGetMessage(e));
        printf("Exception: %s\n", lsmErrorGetException(e));
        lsmErrorFree(e);
    }
}

//NOTE:  Re-factor these three functions as they are too similar and could be
//consolidated.
static int listVolumes(const LSM::Arguments &a, lsmConnectPtr c)
{
    int rc = 0;
    lsmVolumePtr *vol = NULL;
    uint32_t num_vol = 0;

    rc = lsmVolumeList(c, &vol, & num_vol);

    if( rc == LSM_ERR_OK ) {
        uint32_t i;

        if( !a.terse.present ) {
            printf("ID           Name                                       vpd83                      "
                    "      bs             #blocks                 status            size\n");
        }

        for( i = 0; i <  num_vol; ++i ) {
            printVolume(a, vol[i]);
        }

        lsmVolumeRecordFreeArray(vol,  num_vol);
    } else {
        dumpError(rc, lsmErrorGetLast(c));
    }
    return rc;
}

static int listInitiators(const LSM::Arguments &a, lsmConnectPtr c)
{
    int rc = 0;
    lsmInitiatorPtr *init = NULL;
    uint32_t num_init = 0;
    const char format[] = "%-40s%-16s%-5d\n";

    rc = lsmInitiatorList(c, &init, &num_init);

    if( LSM_ERR_OK == rc ) {
        uint32_t i = 0;

        if( !a.terse.present ) {
            printf(format, "ID", "Name", "Type");
        }

        for( i = 0; i < num_init; ++i ) {
            printInitiator(a,init[i]);
        }
        lsmInitiatorRecordFreeArray(init,num_init);
    } else {
        dumpError(rc, lsmErrorGetLast(c));
    }
    return rc;
}

static int listPools(const LSM::Arguments &a, lsmConnectPtr c)
{
    int rc = 0;
    lsmPoolPtr *pool = NULL;
    uint32_t num_pool = 0;
    const char *sep = NULL;

    rc = lsmPoolList(c, &pool, &num_pool);

    if( LSM_ERR_OK == rc ) {
        uint32_t i = 0;

        if( a.terse.present ) {
            sep = a.terse.value.c_str();
        } else {
            printf("ID                                      Name"
                    "                         Total space                    "
                    "          Free space\n");
        }

        for( i = 0; i < num_pool; ++i ) {
            const char *id = lsmPoolIdGet(pool[i]);
            const char *name = lsmPoolNameGet(pool[i]);
            uint64_t total = lsmPoolTotalSpaceGet(pool[i]);
            uint64_t free = lsmPoolFreeSpaceGet(pool[i]);

            if( a.terse.present ) {
                printf("%s%s%s%s%s%s%s\n", id, sep, name, sep,
                        LSM::sizeHuman(a.human.present, total).c_str(), sep,
                        LSM::sizeHuman(a.human.present, free).c_str());
            } else {
                printf("%s\t%s\t%32s\t%32s\n", id, name,
                        LSM::sizeHuman(a.human.present, total).c_str(),
                        LSM::sizeHuman(a.human.present, free).c_str() );
            }
        }

        lsmPoolRecordFreeArray(pool,num_pool);
    } else {
        dumpError(rc, lsmErrorGetLast(c));
    }
    return rc;
}

int list(const LSM::Arguments &a, lsmConnectPtr c)
{
    int rc = 0;
    if( a.commandValue == LIST_TYPE_VOL ) {
        rc = listVolumes(a,c);
    } else if ( a.commandValue == LIST_TYPE_INIT ) {
        rc = listInitiators(a,c);
    } else if ( a.commandValue == LIST_TYPE_POOL ) {
        rc = listPools(a,c);
    }

    return rc;
}

int createInit(const LSM::Arguments &a, lsmConnectPtr c)
{
    lsmInitiatorPtr init = NULL;

    int rc = lsmInitiatorCreate(c, a.commandValue.c_str(), a.id.value.c_str(),
                                    a.initiatorType(), &init);
    if( LSM_ERR_OK == rc ) {
        printInitiator(a,init);
        lsmInitiatorRecordFree(init);
    } else {
        dumpError(rc, lsmErrorGetLast(c));
    }

    return rc;
}

int deleteInit(const LSM::Arguments &a, lsmConnectPtr c)
{
    int rc = 0;
    lsmInitiatorPtr init = getInitiator(c, a.commandValue);

    if( init ) {
        rc = lsmInitiatorDelete(c, init);
        if( LSM_ERR_OK == rc ) {
            lsmInitiatorRecordFree(init);
        } else {
            dumpError(rc, lsmErrorGetLast(c));
        }
    } else {
        printf("Initiator with id= %s not found!\n", a.commandValue.c_str());
    }
    return rc;
}

//Re-factor these next three functions as they are similar.
lsmPoolPtr getPool(lsmConnectPtr c, std::string poolId)
{
    int rc = 0;
    lsmPoolPtr p = NULL;
    lsmPoolPtr *pool = NULL;
    uint32_t num_pool = 0;
    uint32_t i = 0;

    rc = lsmPoolList(c, &pool, &num_pool);

    if( LSM_ERR_OK == rc ) {
        for( i = 0; i < num_pool; ++i ) {
           if( poolId == lsmPoolIdGet(pool[i])) {
               p = lsmPoolRecordCopy(pool[i]);
               break;
           }
        }
        lsmPoolRecordFreeArray(pool, num_pool);
    } else {
        dumpError(rc, lsmErrorGetLast(c));
    }
    return p;
}

lsmVolumePtr getVolume(lsmConnectPtr c, std::string volumeId)
{
    int rc = 0;
    lsmVolumePtr p = NULL;
    lsmVolumePtr *vol = NULL;
    uint32_t num_vol = 0;
    uint32_t i = 0;

    rc = lsmVolumeList(c, &vol, &num_vol);

    if( LSM_ERR_OK == rc ) {
        for( i = 0; i <  num_vol; ++i ) {
           if( volumeId == lsmVolumeIdGet(vol[i])) {
               p = lsmVolumeRecordCopy(vol[i]);
               break;
           }
        }
        lsmVolumeRecordFreeArray(vol,  num_vol);
    } else {
        dumpError(rc, lsmErrorGetLast(c));
    }
    return p;
}

lsmInitiatorPtr getInitiator(lsmConnectPtr c, std::string initId)
{
    int rc = 0;
    lsmInitiatorPtr p = NULL;
    lsmInitiatorPtr *inits = NULL;
    uint32_t num_inits = 0;
    uint32_t i = 0;

    rc = lsmInitiatorList(c, &inits, &num_inits);

    if( LSM_ERR_OK == rc ) {
        for( i = 0; i <  num_inits; ++i ) {
           if( initId == lsmInitiatorIdGet(inits[i])) {
               p = lsmInitiatorRecordCopy(inits[i]);
               break;
           }
        }
        lsmInitiatorRecordFreeArray(inits, num_inits);
    } else {
        dumpError(rc, lsmErrorGetLast(c));
    }
    return p;
}

int createVolume(const LSM::Arguments &a, lsmConnectPtr c)
{
    int rc = 0;
    lsmVolumePtr vol = NULL;
    char *job = NULL;
    uint64_t size = 0;
    lsmPoolPtr pool = NULL;

    //Get the pool of interest
    pool = getPool(c, a.pool.value);
    if( pool ) {

        LSM::sizeArg(a.size.value.c_str(), &size);

        rc = lsmVolumeCreate(c,pool, a.commandValue.c_str(),
                                size, a.provisionType(), &vol, &job);

        rc = waitForJob(rc, c, job, a, &vol);

        lsmPoolRecordFree(pool);
    } else {
        printf("Pool with id= %s not found!\n", a.pool.value.c_str());
    }
    return rc;
}

int deleteVolume(const LSM::Arguments &a, lsmConnectPtr c)
{
    int rc = 0;
    char *job = NULL;
    //Get the volume pointer to the record in question to delete.
    lsmVolumePtr vol = getVolume(c, a.commandValue);

    if( vol ) {
        rc = lsmVolumeDelete(c, vol, &job);
        rc = waitForJob(rc, c, job, a);
    } else {
        printf("Volume with id= %s not found!\n", a.commandValue.c_str());
    }
    return rc;
}

int replicateVolume(const LSM::Arguments &a, lsmConnectPtr c)
{
    int rc = 0;
    char *job = NULL;
    lsmVolumePtr newVol = NULL;
    lsmVolumePtr vol = getVolume(c, a.commandValue);
    lsmPoolPtr pool = getPool(c, a.pool.value);


    if( vol && pool ) {
        rc = lsmVolumeReplicate(c, pool, a.replicationType(), vol,
                                    a.name.value.c_str(), &newVol, &job);
        rc = waitForJob(rc, c, job, a, &newVol);
    } else {
        if( !vol ) {
            printf("Volume with id= %s not found!\n", a.commandValue.c_str());
        }

        if( !pool ) {
            printf("Pool with id= %s not found!\n", a.pool.value.c_str());
        }
    }

    if( vol ) {
        lsmVolumeRecordFree(vol);
        vol = NULL;
    }

    if( pool ) {
        lsmPoolRecordFree(pool);
        pool = NULL;
    }

    return rc;
}

static int _access(const LSM::Arguments &a, lsmConnectPtr c, bool grant)
{
    int rc = 0;
    char *job = NULL;
    lsmInitiatorPtr init = getInitiator(c, a.commandValue);
    lsmVolumePtr vol = getVolume(c, a.volume.value);

    if( init && vol ) {
        if( grant ) {
            rc = lsmAccessGrant(c, init, vol, a.accessType(), &job);
            rc = waitForJob(rc, c, job, a );
        } else {
            rc = lsmAccessRevoke(c, init, vol);

            if( LSM_ERR_OK != rc ) {
                dumpError(rc, lsmErrorGetLast(c));
            }
        }
    } else {
        if( !init ) {
            printf("Initiator with id= %s not found!\n",
                    a.commandValue.c_str());
        }
        if( !vol ) {
            printf("Volume with id= %s not found!\n", a.volume.value.c_str());
        }
    }

    if( init ) {
        lsmInitiatorRecordFree(init);
        init = NULL;
    }

    if( vol ) {
        lsmVolumeRecordFree(vol);
        vol = NULL;
    }
    return rc;
}

int accessGrant(const LSM::Arguments &a, lsmConnectPtr c)
{
    return _access(a, c, true);
}

int accessRevoke(const LSM::Arguments &a, lsmConnectPtr c)
{
    return _access(a, c, false);
}

int resizeVolume(const LSM::Arguments &a, lsmConnectPtr c)
{
    int rc = 0;
    char *job = NULL;
    uint64_t size = 0;
    lsmVolumePtr newVol = NULL;
    lsmVolumePtr vol = getVolume(c, a.commandValue);

    LSM::sizeArg(a.size.value.c_str(), &size);

    if( vol ) {
        rc = lsmVolumeResize(c, vol, size, &newVol, &job);
        rc = waitForJob(rc, c, job, a, &newVol);
    } else {
        printf("Volume with id= %s not found!\n", a.commandValue.c_str());
    }

    if( vol ) {
        lsmVolumeRecordFree(vol);
        vol = NULL;
    }
    return rc;
}
