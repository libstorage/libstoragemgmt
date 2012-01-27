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

void printVolume(const LSM::Arguments &a, lsmVolumePtr v)
{
    const char *sep = NULL;
    const char *id = lsmVolumeIdGet(v);
    const char *name = lsmVolumeNameGet(v);
    const char *vpd = lsmVolumeVpd83Get(v);
    uint64_t block_size = lsmVolumeBlockSizeGet(v);
    uint64_t block_num = lsmVolumeNumberOfBlocks(v);
    uint32_t status = lsmVolumeOpStatusGet(v);
    uint64_t size = block_size * block_num;

    if( a.terse.present ) {
        sep = a.terse.value.c_str();
        printf("%s%s%s%s%s%s%lu%s%lu%s%u%s%s\n", id, sep, name,
                sep, vpd, sep, block_size, sep, block_num, sep,
                status, sep, LSM::sizeHuman(a.human.present, size).c_str());
    } else {
        printf("%s %-40s\t%s %-8lu\t%-17lu\t%u\t%20s\n", id, name,
                vpd, block_size, block_num, status,
                LSM::sizeHuman(a.human.present, size).c_str());
    }
}

int waitForJob(int cmd_rc, lsmConnectPtr c, uint32_t job,
                const LSM::Arguments &a, lsmVolumePtr *vol)
{
    int rc = cmd_rc;
    //Check to see if we are done!
    if( LSM_ERR_OK != rc ) {
        if ( LSM_ERR_JOB_STARTED == rc ) {
            //We have a job to wait for.
            printf("Wait for it...\n");
            lsmJobStatus status;
            uint8_t percent = 0;

            do {
                sleep(1);
                rc = lsmJobStatusGet(c, job, &status, &percent, vol);
            } while ( (LSM_JOB_INPROGRESS == status) && (LSM_ERR_OK == rc) );

            if( (LSM_ERR_OK == rc) && (LSM_JOB_COMPLETE == status) && *vol ) {
                printVolume(a, *vol);
            } else {
                printf("RC = %d, job = %d, status %d, percent %d\n", rc, job, status, percent);
            }

            //Clean up the job
            int jf = lsmJobFree(c, job);
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
    const char *sep = NULL;

    rc = lsmVolumeList(c, &vol, & num_vol);

    if( rc == LSM_ERR_OK ) {
        uint32_t i;

        if( a.terse.present ) {
            sep = a.terse.value.c_str();
        } else {
            printf("ID           Name\t\t\t\t\tvpd83                      "
                    "      bs\t\t#blocks\t\t\tstatus\t\t\tsize\n");
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
    const char *sep = NULL;

    rc = lsmInitiatorList(c, &init, &num_init);

    if( LSM_ERR_OK == rc ) {
        uint32_t i = 0;

        if( a.terse.present ) {
            sep = a.terse.value.c_str();
        } else {
            printf("ID\t\t\t\t\tType\n");
        }

        for( i = 0; i < num_init; ++i ) {
            const char *id = lsmInitiatorIdGet(init[i]);
            lsmInitiatorType type = lsmInitiatorTypeGet(init[i]);

            if( a.terse.present ) {
                printf("%s%s%d\n", id, sep, type);
            } else {
                printf("%s\t%d\n", id, type);
            }
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
            printf("ID                                       Name"
                    "                        Total space                    "
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
        lsmInitiatorRecordFree(init);
    } else {
        dumpError(rc, lsmErrorGetLast(c));
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
    uint32_t job = 0;
    uint64_t size = 0;
    lsmPoolPtr pool = NULL;

    //Get the pool of interest
    pool = getPool(c, a.pool.value);
    if( pool ) {

        LSM::sizeArg(a.size.value, size);

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
    uint32_t job = 0;
    //Get the volume pointer to the record in question to delete.
    lsmVolumePtr vol = getVolume(c, a.commandValue);

    if( vol ) {
        rc = lsmVolumeDelete(c, vol, &job);
        rc = waitForJob(rc, c, job, a );
    } else {
        printf("Volume with id= %s not found!\n", a.commandValue.c_str());
    }
    return rc;
}

int replicateVolume(const LSM::Arguments &a, lsmConnectPtr c)
{
    int rc = 0;
    uint32_t job;
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
    uint32_t job = 0;
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
    uint32_t job = 0;
    uint64_t size = 0;
    lsmVolumePtr newVol = NULL;
    lsmVolumePtr vol = getVolume(c, a.commandValue);

    LSM::sizeArg(a.size.value, size);

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