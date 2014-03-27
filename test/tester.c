/*
 * Copyright (C) 2011-2014 Red Hat, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <check.h>
#include <unistd.h>
#include <time.h>
#include <libstoragemgmt/libstoragemgmt.h>
#include <libstoragemgmt/libstoragemgmt_plug_interface.h>

const char uri[] = "sim://localhost/?statefile=/tmp/%d/lsm_sim_%s";
const char SYSTEM_NAME[] = "LSM simulated storage plug-in";
const char SYSTEM_ID[] = "sim-01";
const char *ISCSI_HOST[2] = {   "iqn.1994-05.com.domain:01.89bd01",
                                "iqn.1994-05.com.domain:01.89bd02" };

static int which_plugin = 0;

#define POLL_SLEEP 50000

lsmConnect *c = NULL;

/**
* Generates a random string in the buffer with specified length.
* Note: This function should not produce repeating sequences or duplicates
* regardless if it's used repeatedly in the same function in the same process
* or different forked processes.
* @param buff  Buffer to write the random string to
* @param len   Length of the random string
*/
void generateRandom(char *buff, uint32_t len)
{
    uint32_t i = 0;
    static int seed = 0;
    static int pid = 0;

    /* Re-seed the random number generator at least once per unique process */
    if( (!seed || !pid) || (pid != getpid()) ) {
        seed = time(NULL);
        pid = getpid();
        srandom(seed + pid);
     }

    if( buff && (len > 1) ) {
        for(i = 0; i < (len - 1); ++i) {
            buff[i] = 97 + rand()%26;
        }
        buff[len-1] = '\0';
    }
}

char *plugin_to_use()
{
    if( which_plugin == 1) {
        return "simc://";
    } else {
        char *rundir = getenv("LSM_TEST_RUNDIR");

        /* The python plug-in keeps state, but the unit tests don't expect this
         * create a new random file for the state to keep things new.
         */

        if( rundir ) {
            int rdir = atoi(rundir);
            static char fn[128];
            static char name[32];
            generateRandom(name, sizeof(name));
            snprintf(fn, sizeof(fn),  uri, rdir, name);
            printf("URI = %s\n", fn);
            return fn;
        } else {
            printf("Missing LSM_TEST_RUNDIR, expect test failures!\n");
            return "sim://";
        }
    }
}

lsmPool *getTestPool(lsmConnect *c)
{
    lsmPool **pools = NULL;
    uint32_t count = 0;
    lsmPool *test_pool = NULL;

    int rc = lsmPoolList(c, &pools, &count, LSM_FLAG_RSVD);
    if( LSM_ERR_OK == rc ) {
        uint32_t i = 0;
        for(i = 0; i < count; ++i ) {
            if(strcmp(lsmPoolNameGet(pools[i]), "lsm_test_aggr") == 0 ) {
                test_pool = lsmPoolRecordCopy(pools[i]);
                lsmPoolRecordArrayFree(pools, count);
                break;
            }
        }
    }
    return test_pool;
}

void dump_error(lsmErrorPtr e)
{
    if (e != NULL) {
        printf("Error msg= %s - exception %s - debug %s\n",
            lsmErrorMessageGet(e),
            lsmErrorExceptionGet(e), lsmErrorDebugGet(e));

        lsmErrorFree(e);
        e = NULL;
    } else {
        printf("No additional error information!\n");
    }
}

void setup(void)
{
    lsmErrorPtr e = NULL;

    int rc = lsmConnectPassword(plugin_to_use(), NULL, &c, 30000, &e,
                LSM_FLAG_RSVD);
    if( rc ) {
        printf("rc= %d\n", rc);
        dump_error(e);
    } else {

        if( getenv("LSM_DEBUG_PLUGIN") ) {
            printf("Attach debugger to plug-in, press <return> when ready...");
            getchar();
        }
    }
    fail_unless(LSM_ERR_OK == rc);
}

void teardown(void)
{
    int rc = lsmConnectClose(c, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_OK == rc, "lsmConnectClose rc = %d", rc);
    c = NULL;
}

char *error(lsmErrorPtr e)
{
    static char eb[1024];
    memset(eb, 0, sizeof(eb));

    if( e != NULL ) {
        snprintf(eb, sizeof(eb), "Error msg= %s - exception %s - debug %s",
            lsmErrorMessageGet(e),
            lsmErrorExceptionGet(e), lsmErrorDebugGet(e));
        lsmErrorFree(e);
        e = NULL;
    } else {
        snprintf(eb, sizeof(eb), "No addl. error info.");
    }
    return eb;
}

void wait_for_job(lsmConnect *c, char **job_id)
{
    lsmJobStatus status;
    uint8_t pc = 0;
    int rc = 0;

    do {
        rc = lsmJobStatusGet(c, *job_id, &status, &pc, LSM_FLAG_RSVD);
        fail_unless( LSM_ERR_OK == rc, "lsmJobStatusVolumeGet = %d (%s)", rc,  error(lsmErrorLastGet(c)));
        printf("GENERIC: Job %s in progress, %d done, status = %d\n", *job_id, pc, status);
        usleep(POLL_SLEEP);

    } while( status == LSM_JOB_INPROGRESS );

    rc = lsmJobFree(c, job_id, LSM_FLAG_RSVD);
    fail_unless( LSM_ERR_OK == rc, "lsmJobFree %d, (%s)", rc, error(lsmErrorLastGet(c)));

    fail_unless( LSM_JOB_COMPLETE == status);
    fail_unless( 100 == pc);
    fail_unless( job_id != NULL );
}

lsmVolume *wait_for_job_vol(lsmConnect *c, char **job_id)
{
    lsmJobStatus status;
    lsmVolume *vol = NULL;
    uint8_t pc = 0;
    int rc = 0;

    do {
        rc = lsmJobStatusVolumeGet(c, *job_id, &status, &pc, &vol, LSM_FLAG_RSVD);
        fail_unless( LSM_ERR_OK == rc, "rc = %d (%s)", rc,  error(lsmErrorLastGet(c)));
        printf("VOLUME: Job %s in progress, %d done, status = %d\n", *job_id, pc, status);
        usleep(POLL_SLEEP);

    } while( rc == LSM_ERR_OK && status == LSM_JOB_INPROGRESS );

    printf("Volume complete: Job %s percent %d done, status = %d, rc=%d\n", *job_id, pc, status, rc);

    rc = lsmJobFree(c, job_id, LSM_FLAG_RSVD);
    fail_unless( LSM_ERR_OK == rc, "lsmJobFree %d, (%s)", rc, error(lsmErrorLastGet(c)));

    fail_unless( LSM_JOB_COMPLETE == status);
    fail_unless( 100 == pc);

    return vol;
}

lsmPool *wait_for_job_pool(lsmConnect *c, char **job_id)
{
    lsmJobStatus status;
    lsmPool *pool = NULL;
    uint8_t pc = 0;
    int rc = 0;

    do {
        rc = lsmJobStatusPoolGet(c, *job_id, &status, &pc, &pool, LSM_FLAG_RSVD);
        fail_unless( LSM_ERR_OK == rc, "rc = %d (%s)", rc,  error(lsmErrorLastGet(c)));
        printf("POOL: Job %s in progress, %d done, status = %d\n", *job_id, pc, status);
        usleep(POLL_SLEEP);

    } while( status == LSM_JOB_INPROGRESS );

    rc = lsmJobFree(c, job_id, LSM_FLAG_RSVD);
    fail_unless( LSM_ERR_OK == rc, "lsmJobFree %d, (%s)", rc, error(lsmErrorLastGet(c)));

    fail_unless( LSM_JOB_COMPLETE == status);
    fail_unless( 100 == pc);

    return pool;
}

lsmFs *wait_for_job_fs(lsmConnect *c, char **job_id)
{
    lsmJobStatus status;
    lsmFs *fs = NULL;
    uint8_t pc = 0;
    int rc = 0;

    do {
        rc = lsmJobStatusFsGet(c, *job_id, &status, &pc, &fs, LSM_FLAG_RSVD);
        fail_unless( LSM_ERR_OK == rc, "rc = %d (%s)", rc,  error(lsmErrorLastGet(c)));
        printf("FS: Job %s in progress, %d done, status = %d\n", *job_id, pc, status);
        usleep(POLL_SLEEP);

    } while( status == LSM_JOB_INPROGRESS );

    rc = lsmJobFree(c, job_id, LSM_FLAG_RSVD);
    fail_unless( LSM_ERR_OK == rc, "lsmJobFree %d, (%s)", rc, error(lsmErrorLastGet(c)));

    fail_unless( LSM_JOB_COMPLETE == status);
    fail_unless( 100 == pc);

    return fs;
}

lsmSs *wait_for_job_ss(lsmConnect *c, char **job_id)
{
    lsmJobStatus status;
    lsmSs *ss = NULL;
    uint8_t pc = 0;
    int rc = 0;

    do {
        rc = lsmJobStatusSsGet(c, *job_id, &status, &pc, &ss, LSM_FLAG_RSVD);
        fail_unless( LSM_ERR_OK == rc, "rc = %d (%s)", rc,  error(lsmErrorLastGet(c)));
        printf("SS: Job %s in progress, %d done, status = %d\n", *job_id, pc, status);
        usleep(POLL_SLEEP);

    } while( status == LSM_JOB_INPROGRESS );

    rc = lsmJobFree(c, job_id, LSM_FLAG_RSVD);
    fail_unless( LSM_ERR_OK == rc, "lsmJobFree %d, (%s)", rc, error(lsmErrorLastGet(c)));

    fail_unless( LSM_JOB_COMPLETE == status);
    fail_unless( 100 == pc);

    return ss;
}

int compare_string_lists(lsmStringList *l, lsmStringList *r)
{
    if( l && r) {
        int i = 0;

        if( l == r ) {
            return 0;
        }

        if( lsmStringListSize(l) != lsmStringListSize(r) ) {
            return 1;
        }

        for( i = 0; i < lsmStringListSize(l); ++i ) {
            if( strcmp(lsmStringListElemGet(l, i),
                lsmStringListElemGet(r, i)) != 0) {
                return 1;
            }
        }
        return 0;
    }
    return 1;
}

void create_volumes(lsmConnect *c, lsmPool *p, int num)
{
    int i;

    for( i = 0; i < num; ++i ) {
        lsmVolume *n = NULL;
        char *job = NULL;
        char name[32];

        memset(name, 0, sizeof(name));
        snprintf(name, sizeof(name), "test %d", i);

        int vc = lsmVolumeCreate(c, p, name, 20000000,
                                    LSM_PROVISION_DEFAULT, &n, &job, LSM_FLAG_RSVD);

        fail_unless( vc == LSM_ERR_OK || vc == LSM_ERR_JOB_STARTED,
                "lsmVolumeCreate %d (%s)", vc, error(lsmErrorLastGet(c)));

        if( LSM_ERR_JOB_STARTED == vc ) {
            n = wait_for_job_vol(c, &job);
        } else {
            fail_unless(LSM_ERR_OK == vc);
        }

        lsmVolumeRecordFree(n);
        n = NULL;
    }
}

lsmSystem *get_system()
{
    lsmSystem *rc_sys = NULL;
    lsmSystem **sys=NULL;
    uint32_t count = 0;

    int rc = lsmSystemList(c, &sys, &count, LSM_FLAG_RSVD);

    fail_unless(LSM_ERR_OK == rc);

    if( LSM_ERR_OK == rc && count) {
        rc_sys = lsmSystemRecordCopy(sys[0]);
        lsmSystemRecordArrayFree(sys, count);
    }
    return rc_sys;
}

START_TEST(test_smoke_test)
{
    uint32_t i = 0;
    int rc = 0;

    lsmPool *selectedPool = NULL;
    uint32_t poolCount = 0;

    uint32_t set_tmo = 31123;
    uint32_t tmo = 0;

    //Set timeout.
    rc = lsmConnectTimeoutSet(c, set_tmo, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_OK == rc, "lsmConnectSetTimeout %d (%s)", rc,
                    error(lsmErrorLastGet(c)));


    //Get time-out.
    rc = lsmConnectTimeoutGet(c, &tmo, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_OK == rc, "Error getting tmo %d (%s)", rc,
                error(lsmErrorLastGet(c)));

    fail_unless( set_tmo == tmo, " %u != %u", set_tmo, tmo );

    lsmPool **pools = NULL;
    uint32_t count = 0;
    int poolToUse = -1;

    //Get pool list
    rc = lsmPoolList(c, &pools, &poolCount, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_OK == rc, "lsmPoolList rc =%d (%s)", rc,
                    error(lsmErrorLastGet(c)));

    //Check pool count
    count = poolCount;
    fail_unless(count == 4, "We are expecting 4 pools from simulator");

    //Dump pools and select a pool to use for testing.
    for (i = 0; i < count; ++i) {
        printf("Id= %s, name=%s, capacity= %"PRIu64 ", remaining= %"PRIu64" "
            "system %s\n",
            lsmPoolIdGet(pools[i]),
            lsmPoolNameGet(pools[i]),
            lsmPoolTotalSpaceGet(pools[i]),
            lsmPoolFreeSpaceGet(pools[i]),
            lsmPoolSystemIdGet(pools[i]));

        fail_unless( strcmp(lsmPoolSystemIdGet(pools[i]), SYSTEM_ID) == 0,
            "Expecting system id of %s, got %s",
            SYSTEM_ID, lsmPoolSystemIdGet(pools[i]));

        fail_unless(lsmPoolStatusGet(pools[i]) == LSM_POOL_STATUS_OK,
            "%"PRIu64, lsmPoolStatusGet(pools[i]));

        if (lsmPoolFreeSpaceGet(pools[i]) > 20000000) {
            poolToUse = i;
        }
    }

    if (poolToUse != -1) {
        lsmVolume *n = NULL;
        char *job = NULL;

        selectedPool = pools[poolToUse];

        int vc = lsmVolumeCreate(c, pools[poolToUse], "test", 20000000,
                                    LSM_PROVISION_DEFAULT, &n, &job, LSM_FLAG_RSVD);

        fail_unless( vc == LSM_ERR_OK || vc == LSM_ERR_JOB_STARTED,
                    "lsmVolumeCreate %d (%s)", vc, error(lsmErrorLastGet(c)));

        if( LSM_ERR_JOB_STARTED == vc ) {
            n = wait_for_job_vol(c, &job);

            fail_unless( n != NULL);
        }

        uint8_t dependants = 10;
        int child_depends = lsmVolumeChildDependency(c, n, &dependants, LSM_FLAG_RSVD);
        fail_unless(LSM_ERR_OK == child_depends, "returned = %d", child_depends);
        fail_unless(dependants == 0);

        child_depends = lsmVolumeChildDependencyRm(c, n, &job, LSM_FLAG_RSVD);
        if( LSM_ERR_JOB_STARTED == child_depends ) {
            wait_for_job(c, &job);
        } else {
            fail_unless(LSM_ERR_OK == child_depends, "rc = %d", child_depends);
            fail_unless(NULL == job);
        }


        lsmBlockRange **range = lsmBlockRangeRecordArrayAlloc(3);
        fail_unless(NULL != range);


        uint32_t bs = 0;
        lsmSystem * system = get_system();

        int rep_bs = lsmVolumeReplicateRangeBlockSize(c, system, &bs, LSM_FLAG_RSVD);
        fail_unless(LSM_ERR_OK == rep_bs, "%d", rep_bs);
        fail_unless(512 == bs);

        lsmSystemRecordFree(system);

        int rep_i = 0;

        for(rep_i = 0; rep_i < 3; ++rep_i) {
            range[rep_i] = lsmBlockRangeRecordAlloc((rep_i * 1000),
                                                        ((rep_i + 100) * 10000), 10);

            lsmBlockRange *copy = lsmBlockRangeRecordCopy(range[rep_i]);

            fail_unless( lsmBlockRangeSourceStartGet(range[rep_i]) ==
                lsmBlockRangeSourceStartGet(copy));

            fail_unless( lsmBlockRangeDestStartGet(range[rep_i]) ==
                            lsmBlockRangeDestStartGet(copy));

            fail_unless ( lsmBlockRangeBlockCountGet(range[rep_i]) ==
                            lsmBlockRangeBlockCountGet( copy ));

            lsmBlockRangeRecordFree(copy);
            copy = NULL;

        }

        int rep_range =  lsmVolumeReplicateRange(c, LSM_VOLUME_REPLICATE_CLONE,
                                                    n, n, range, 3, &job, LSM_FLAG_RSVD);

        if( LSM_ERR_JOB_STARTED == rep_range ) {
            wait_for_job(c, &job);
        } else {

            if( LSM_ERR_OK != rep_range ) {
                dump_error(lsmErrorLastGet(c));
            }

            fail_unless(LSM_ERR_OK == rep_range);
        }

        lsmBlockRangeRecordArrayFree(range, 3);

        int online = lsmVolumeOffline(c, n, LSM_FLAG_RSVD);
        fail_unless( LSM_ERR_OK == online);

        online = lsmVolumeOnline(c, n, LSM_FLAG_RSVD);
        fail_unless( LSM_ERR_OK == online);

        char *jobDel = NULL;
        int delRc = lsmVolumeDelete(c, n, &jobDel, LSM_FLAG_RSVD);

        fail_unless( delRc == LSM_ERR_OK || delRc == LSM_ERR_JOB_STARTED,
                    "lsmVolumeDelete %d (%s)", rc, error(lsmErrorLastGet(c)));

        if( LSM_ERR_JOB_STARTED == delRc ) {
            wait_for_job_vol(c, &jobDel);
        }

        lsmVolumeRecordFree(n);
    }

    lsmInitiator **inits = NULL;
    /* Get a list of initiators */
    rc = lsmInitiatorList(c, &inits, &count, LSM_FLAG_RSVD);

    fail_unless( LSM_ERR_OK == rc, "lsmInitiatorList %d (%s)", rc,
                                    error(lsmErrorLastGet(c)));

    fail_unless( count == 0, "Count 0 != %d\n", count);


    //Create some volumes for testing.
    create_volumes(c, selectedPool, 3);

    lsmVolume **volumes = NULL;
    /* Get a list of volumes */
    rc = lsmVolumeList(c, &volumes, &count, LSM_FLAG_RSVD);


    fail_unless( LSM_ERR_OK == rc , "lsmVolumeList %d (%s)",rc,
                                    error(lsmErrorLastGet(c)));

    for (i = 0; i < count; ++i) {
        printf("%s - %s - %s - %"PRIu64" - %"PRIu64" - %x\n",
            lsmVolumeIdGet(volumes[i]),
            lsmVolumeNameGet(volumes[i]),
            lsmVolumeVpd83Get(volumes[i]),
            lsmVolumeBlockSizeGet(volumes[i]),
            lsmVolumeNumberOfBlocksGet(volumes[i]),
            lsmVolumeOpStatusGet(volumes[i]));
    }


    lsmVolume *rep = NULL;
    char *job = NULL;

    //Try a re-size then a snapshot
    lsmVolume *resized = NULL;
    char  *resizeJob = NULL;

    int resizeRc = lsmVolumeResize(c, volumes[0],
        ((lsmVolumeNumberOfBlocksGet(volumes[0]) *
        lsmVolumeBlockSizeGet(volumes[0])) * 2), &resized, &resizeJob, LSM_FLAG_RSVD);

    fail_unless(resizeRc == LSM_ERR_OK || resizeRc == LSM_ERR_JOB_STARTED,
                    "lsmVolumeResize %d (%s)", resizeRc,
                    error(lsmErrorLastGet(c)));

    if( LSM_ERR_JOB_STARTED == resizeRc ) {
        resized = wait_for_job_vol(c, &resizeJob);
    }

    lsmVolumeRecordFree(resized);

    //Lets create a snapshot of one.
    int repRc = lsmVolumeReplicate(c, NULL,             //Pool is optional
        LSM_VOLUME_REPLICATE_SNAPSHOT,
        volumes[0], "SNAPSHOT1",
        &rep, &job, LSM_FLAG_RSVD);

    fail_unless(repRc == LSM_ERR_OK || repRc == LSM_ERR_JOB_STARTED,
                    "lsmVolumeReplicate %d (%s)", repRc,
                    error(lsmErrorLastGet(c)));

    if( LSM_ERR_JOB_STARTED == repRc ) {
        rep = wait_for_job_vol(c, &job);
    }

    lsmVolumeRecordFree(rep);

    lsmVolumeRecordArrayFree(volumes, count);

    if (pools) {
        lsmPoolRecordArrayFree(pools, poolCount);
    }
}

END_TEST


START_TEST(test_access_groups)
{
    lsmAccessGroup **groups = NULL;
    lsmAccessGroup *group = NULL;
    uint32_t count = 0;
    uint32_t i = 0;

    fail_unless(c!=NULL);

    int rc = lsmAccessGroupList(c, &groups, &count, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_OK == rc, "Expected success on listing access groups %d", rc);
    fail_unless(count == 0, "Expect 0 access groups, got %"PRIu32, count);

    lsmAccessGroupRecordArrayFree(groups, count);
    groups = NULL;
    count = 0;


    rc = lsmAccessGroupCreate(c, "test_access_groups", "iqn.1994-05.com.domain:01.89bd01",
                    LSM_INITIATOR_ISCSI, SYSTEM_ID, &group, LSM_FLAG_RSVD);

    if( LSM_ERR_OK == rc ) {
        lsmStringList *init_list = lsmAccessGroupInitiatorIdGet(group);
        lsmStringList *init_copy = NULL;

        fail_unless(lsmStringListSize(init_list) == 1);

        init_copy = lsmStringListCopy(init_list);
        lsmAccessGroupInitiatorIdSet(group, init_copy);

        printf("%s - %s - %s\n",    lsmAccessGroupIdGet(group),
                                    lsmAccessGroupNameGet(group),
                                    lsmAccessGroupSystemIdGet(group));

        fail_unless(NULL != lsmAccessGroupIdGet(group));
        fail_unless(NULL != lsmAccessGroupNameGet(group));
        fail_unless(NULL != lsmAccessGroupSystemIdGet(group));

        lsmAccessGroup *copy = lsmAccessGroupRecordCopy(group);
        if( copy ) {
            fail_unless( strcmp(lsmAccessGroupIdGet(group), lsmAccessGroupIdGet(copy)) == 0);
            fail_unless( strcmp(lsmAccessGroupNameGet(group), lsmAccessGroupNameGet(copy)) == 0) ;
            fail_unless( strcmp(lsmAccessGroupSystemIdGet(group), lsmAccessGroupSystemIdGet(copy)) == 0);

            lsmAccessGroupRecordFree(copy);
        }

        lsmStringListFree(init_copy);
        init_copy = NULL;
    }

    rc = lsmAccessGroupList(c, &groups, &count, LSM_FLAG_RSVD);
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( 1 == count );
    lsmAccessGroupRecordArrayFree(groups, count);
    groups = NULL;
    count = 0;

    char *job = NULL;

    rc = lsmAccessGroupInitiatorAdd(c, group, "iqn.1994-05.com.domain:01.89bd02", LSM_INITIATOR_ISCSI, LSM_FLAG_RSVD);

    if( LSM_ERR_JOB_STARTED == rc ) {
        wait_for_job(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc, "Expected success on lsmAccessGroupInitiatorAdd %d", rc);
    }

    rc = lsmAccessGroupList(c, &groups, &count, LSM_FLAG_RSVD);
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( 1 == count );

    lsmStringList *init_list = lsmAccessGroupInitiatorIdGet(groups[0]);
    fail_unless( lsmStringListSize(init_list) == 2, "Expecting 2 initiators, current num = %d\n", lsmStringListSize(init_list) );
    for( i = 0; i < lsmStringListSize(init_list); ++i) {
        printf("%d = %s\n", i, lsmStringListElemGet(init_list, i));
    }
    lsmStringListFree(init_list);


    uint32_t init_list_count = 0;
    lsmInitiator **inits = NULL;
    rc = lsmInitiatorList(c, &inits, &init_list_count, LSM_FLAG_RSVD);

    fail_unless(LSM_ERR_OK == rc );
    printf("We have %d initiators\n", init_list_count);

    fail_unless(2 == init_list_count);

    for( i = 0; i < init_list_count; ++i ) {
        printf("Deleting initiator %s from group!\n", lsmInitiatorIdGet(inits[i]));
        rc = lsmAccessGroupInitiatorDelete(c, groups[0],
                                            lsmInitiatorIdGet(inits[i]), LSM_FLAG_RSVD);

        if( LSM_ERR_JOB_STARTED == rc ) {
            wait_for_job_fs(c, &job);
        } else {
            fail_unless(LSM_ERR_OK == rc);
        }
    }

    lsmAccessGroupRecordArrayFree(groups, count);
    groups = NULL;
    count = 0;

    rc = lsmAccessGroupList(c, &groups, &count, LSM_FLAG_RSVD);
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( 1 == count );

    init_list = lsmAccessGroupInitiatorIdGet(groups[0]);
    fail_unless( init_list != NULL);
    fail_unless( lsmStringListSize(init_list) == 0);

    lsmAccessGroupRecordArrayFree(groups, count);
    groups = NULL;
    count = 0;

}

END_TEST

START_TEST(test_access_groups_grant_revoke)
{
    fail_unless(c!=NULL);
    lsmAccessGroup *group = NULL;
    int rc = 0;
    lsmPool *pool = getTestPool(c);
    char *job = NULL;
    lsmVolume *n = NULL;

    fail_unless(pool != NULL);

    rc = lsmAccessGroupCreate(c, "test_access_groups_grant_revoke",
                                   ISCSI_HOST[0], LSM_INITIATOR_ISCSI, SYSTEM_ID,
                                    &group, LSM_FLAG_RSVD);

    fail_unless( LSM_ERR_OK == rc );

    int vc = lsmVolumeCreate(c, pool, "volume_grant_test", 20000000,
                                    LSM_PROVISION_DEFAULT, &n, &job, LSM_FLAG_RSVD);

    fail_unless( vc == LSM_ERR_OK || vc == LSM_ERR_JOB_STARTED,
            "lsmVolumeCreate %d (%s)", vc, error(lsmErrorLastGet(c)));

    if( LSM_ERR_JOB_STARTED == vc ) {
        n = wait_for_job_vol(c, &job);
    }

    fail_unless(n != NULL);

    rc = lsmAccessGroupGrant(c, group, n, LSM_VOLUME_ACCESS_READ_WRITE, LSM_FLAG_RSVD);
    if( LSM_ERR_JOB_STARTED == rc ) {
        wait_for_job(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc, "rc = %d", rc);
    }

    lsmVolume **volumes = NULL;
    uint32_t v_count = 0;
    rc = lsmVolumesAccessibleByAccessGroup(c, group, &volumes, &v_count, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_OK == rc);
    fail_unless(v_count == 1);

    if( v_count >= 1 ) {
        fail_unless(strcmp(lsmVolumeIdGet(volumes[0]), lsmVolumeIdGet(n)) == 0);
        lsmVolumeRecordArrayFree(volumes, v_count);
    }

    lsmAccessGroup **groups;
    uint32_t g_count = 0;
    rc = lsmAccessGroupsGrantedToVolume(c, n, &groups, &g_count, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_OK == rc);
    fail_unless(g_count == 1);


    if( g_count >= 1 ) {
        fail_unless(strcmp(lsmAccessGroupIdGet(groups[0]), lsmAccessGroupIdGet(group)) == 0);
        lsmAccessGroupRecordArrayFree(groups, g_count);
    }

    rc = lsmAccessGroupRevoke(c, group, n, LSM_FLAG_RSVD);
    if( LSM_ERR_JOB_STARTED == rc ) {
        wait_for_job(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc);
    }

    rc = lsmAccessGroupDelete(c, group, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_OK == rc);
    lsmAccessGroupRecordFree(group);

    lsmVolumeRecordFree(n);
    lsmPoolRecordFree(pool);
}
END_TEST

START_TEST(test_fs)
{
    fail_unless(c!=NULL);

    lsmFs **fs_list = NULL;
    int rc = 0;
    uint32_t fs_count = 0;
    lsmFs *nfs = NULL;
    lsmFs *resized_fs = NULL;
    char *job = NULL;
    uint64_t fs_free_space = 0;

    lsmPool *test_pool = getTestPool(c);

    rc = lsmFsList(c, &fs_list, &fs_count, LSM_FLAG_RSVD);

    fail_unless(LSM_ERR_OK == rc);
    fail_unless(0 == fs_count);

    rc = lsmFsCreate(c, test_pool, "C_unit_test", 50000000, &nfs, &job, LSM_FLAG_RSVD);

    if( LSM_ERR_JOB_STARTED == rc ) {
        fail_unless(NULL == nfs);

        nfs = wait_for_job_fs(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc);
    }

    fail_unless(NULL != nfs);

    fs_free_space = lsmFsFreeSpaceGet(nfs);
    fail_unless(fs_free_space != 0);

    lsmFs *cloned_fs = NULL;
    rc = lsmFsClone(c, nfs, "cloned_fs", NULL, &cloned_fs, &job, LSM_FLAG_RSVD);
    if( LSM_ERR_JOB_STARTED == rc ) {
        fail_unless(NULL == cloned_fs);
        cloned_fs = wait_for_job_fs(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc, "rc= %d", rc);
    }

    rc = lsmFsFileClone(c, nfs, "src/file.txt", "dest/file.txt", NULL, &job, LSM_FLAG_RSVD);
    if( LSM_ERR_JOB_STARTED == rc ) {
        wait_for_job(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc);
    }


    rc = lsmFsList(c, &fs_list, &fs_count, LSM_FLAG_RSVD);

    fail_unless(LSM_ERR_OK == rc);
    fail_unless(2 == fs_count, "fs_count = %d", fs_count);
    lsmFsRecordArrayFree(fs_list, fs_count);

    rc = lsmFsResize(c,nfs, 100000000, &resized_fs, &job, LSM_FLAG_RSVD);

    if( LSM_ERR_JOB_STARTED == rc ) {
        fail_unless(NULL == resized_fs);
        resized_fs = wait_for_job_fs(c, &job);
    }

    uint8_t yes_no = 10;
    rc = lsmFsChildDependency(c, nfs, NULL, &yes_no, LSM_FLAG_RSVD);
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( yes_no == 0);

    rc = lsmFsChildDependencyRm(c, nfs, NULL, &job, LSM_FLAG_RSVD);
    if( LSM_ERR_JOB_STARTED == rc ) {
        fail_unless(NULL != job);
        wait_for_job(c, &job);
    } else {
        fail_unless( LSM_ERR_OK == rc);
    }

    rc = lsmFsDelete(c, resized_fs, &job, LSM_FLAG_RSVD);

    if( LSM_ERR_JOB_STARTED == rc ) {
        wait_for_job(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc);
    }

    lsmFsRecordFree(resized_fs);
    lsmFsRecordFree(nfs);
    lsmPoolRecordFree(test_pool);
}
END_TEST

START_TEST(test_ss)
{
    fail_unless(c != NULL);
    lsmSs **ss_list = NULL;
    uint32_t ss_count = 0;
    char *job = NULL;
    lsmFs *fs = NULL;
    lsmSs *ss = NULL;

    printf("Testing snapshots\n");

    lsmPool *test_pool = getTestPool(c);

    int rc = lsmFsCreate(c, test_pool, "test_fs", 100000000, &fs, &job, LSM_FLAG_RSVD);

    if( LSM_ERR_JOB_STARTED == rc ) {
        fs = wait_for_job_fs(c, &job);
    }

    fail_unless(fs != NULL);


    rc = lsmFsSsList(c, fs, &ss_list, &ss_count, LSM_FLAG_RSVD);
    printf("List return code= %d\n", rc);

    if(rc) {
        printf("%s\n", error(lsmErrorLastGet(c)));
    }
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( NULL == ss_list);
    fail_unless( 0 == ss_count );


    rc = lsmFsSsCreate(c, fs, "test_snap", NULL, &ss, &job, LSM_FLAG_RSVD);
    if( LSM_ERR_JOB_STARTED == rc ) {
        printf("Waiting for snap to create!\n");
        ss = wait_for_job_ss(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc);
    }

    fail_unless( NULL != ss);

    rc = lsmFsSsList(c, fs, &ss_list, &ss_count, LSM_FLAG_RSVD);
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( NULL != ss_list);
    fail_unless( 1 == ss_count );

    lsmStringList *files = lsmStringListAlloc(1);
    if(files) {
        rc = lsmStringListElemSet(files, 0, "some/file/name.txt");
        fail_unless( LSM_ERR_OK == rc, "lsmStringListElemSet rc = %d", rc);
    }

    rc = lsmFsSsRevert(c, fs, ss, files, files, 0, &job, LSM_FLAG_RSVD);
    if( LSM_ERR_JOB_STARTED == rc ) {
        printf("Waiting for  lsmSsRevert!\n");
        wait_for_job(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc);
    }

    lsmStringListFree(files);

    rc = lsmFsSsDelete(c, fs, ss, &job, LSM_FLAG_RSVD);

    if( LSM_ERR_JOB_STARTED == rc ) {
        wait_for_job(c, &job);
    }

    lsmSsRecordArrayFree(ss_list, ss_count);
    lsmFsRecordFree(fs);

    printf("Testing snapshots done!\n");
}
END_TEST

START_TEST(test_systems)
{
    uint32_t count = 0;
    lsmSystem **sys=NULL;
    const char *id = NULL;
    const char *name = NULL;
    uint32_t status = 0;

    fail_unless(c!=NULL);

    int rc = lsmSystemList(c, &sys, &count, LSM_FLAG_RSVD);

    fail_unless(LSM_ERR_OK == rc);
    fail_unless(count == 1);

    id = lsmSystemIdGet(sys[0]);
    fail_unless(id != NULL);
    fail_unless(strcmp(id, SYSTEM_ID) == 0, "%s", id);

    name = lsmSystemNameGet(sys[0]);
    fail_unless(name != NULL);
    fail_unless(strcmp(name, SYSTEM_NAME) == 0);

    status = lsmSystemStatusGet(sys[0]);
    fail_unless(status == LSM_SYSTEM_STATUS_OK, "status = %x", status);

    lsmSystemRecordArrayFree(sys, count);
}
END_TEST

#define COMPARE_STR_FUNC(func, l, r) \
    rc = strcmp(func(l), func(r)); \
    if( rc ) \
        return rc;\

#define COMPARE_NUMBER_FUNC(func, l, r)\
     if( func(l) != func(r) ) \
        return 1;\


static int compare_disks(lsmDisk *l, lsmDisk *r)
{
    int rc;
    if( l && r ) {
        COMPARE_STR_FUNC(lsmDiskIdGet, l, r);
        COMPARE_STR_FUNC(lsmDiskNameGet, l, r);
        COMPARE_STR_FUNC(lsmDiskSystemIdGet, l, r);
        COMPARE_NUMBER_FUNC(lsmDiskTypeGet, l, r);
        COMPARE_NUMBER_FUNC(lsmDiskNumberOfBlocksGet, l, r);
        COMPARE_NUMBER_FUNC(lsmDiskBlockSizeGet, l, r);
        COMPARE_NUMBER_FUNC(lsmDiskStatusGet, l, r);
        return 0;
    }
    return 1;
}

START_TEST(test_disks)
{
    uint32_t count = 0;
    lsmDisk **d = NULL;
    const char *id;
    const char *name;
    const char *system_id;
    int i = 0;
    uint32_t key_count = 0;
    lsmStringList *keys = NULL;
    const char *key = NULL;
    const char *data = NULL;
    lsmOptionalData *od = NULL;
    uint32_t j;

    fail_unless(c!=NULL);

    int rc = lsmDiskList(c, &d, &count, 0);

    if( LSM_ERR_OK == rc ) {
        fail_unless(LSM_ERR_OK == rc, "%d", rc);
        fail_unless(count >= 1);

        for( i = 0; i < count; ++i ) {
            lsmDisk *d_copy = lsmDiskRecordCopy( d[i] );
            fail_unless( d_copy != NULL );
            if( d_copy ) {
                fail_unless(compare_disks(d[i], d_copy) == 0);
                lsmDiskRecordFree(d_copy);
                d_copy = NULL;
            }

            id = lsmDiskIdGet(d[i]);
            fail_unless(id != NULL && strlen(id) > 0);

            name = lsmDiskNameGet(d[i]);
            fail_unless(id != NULL && strlen(name) > 0);

            system_id = lsmDiskSystemIdGet(d[i]);
            fail_unless(id != NULL && strlen(system_id) > 0);
            fail_unless(strcmp(system_id, SYSTEM_ID) == 0, "%s", id);

            fail_unless( lsmDiskTypeGet(d[i]) >= 1 );
            fail_unless( lsmDiskNumberOfBlocksGet(d[i]) >= 1);
            fail_unless( lsmDiskBlockSizeGet(d[i]) >= 1);
            fail_unless( lsmDiskStatusGet(d[i]) >= 1);

            od = lsmDiskOptionalDataGet(d[i]);
            if( od ) {
                /* Iterate through the keys, grabbing the data */
                rc = lsmOptionalDataListGet(od, &keys, &key_count);
                if( LSM_ERR_OK == rc && keys != NULL && key_count > 0 ) {
                    //uint32_t num_keys = lsmStringListSize(keys);
                    //fail_unless( num_keys == key_count, "%d != %d", num_keys, key_count);
                    for(j = 0; j < key_count; ++j ) {
                        key = lsmStringListElemGet(keys, j);
                        data = lsmOptionalDataStringGet(od, key);
                        fail_unless(key != NULL && strlen(key) > 0);
                        fail_unless(data != NULL && strlen(data) > 0);
                    }

                    if( keys ) {
                        rc = lsmStringListFree(keys);
                        fail_unless(LSM_ERR_OK == rc, "rc = %d", rc);
                    }
                }
                lsmOptionalDataRecordFree(od);
            }
        }
        lsmDiskRecordArrayFree(d, count);
    } else {
        fail_unless(d == NULL);
        fail_unless(count == 0);
    }
}
END_TEST

START_TEST(test_nfs_exports)
{
    fail_unless(c != NULL);
    int rc = 0;

    lsmPool *test_pool = getTestPool(c);
    lsmFs *nfs = NULL;
    char *job = NULL;

    fail_unless(NULL != test_pool);

    rc = lsmFsCreate(c, test_pool, "C_unit_test_nfs_export", 50000000, &nfs, &job, LSM_FLAG_RSVD);

    if( LSM_ERR_JOB_STARTED == rc ) {
        nfs = wait_for_job_fs(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc, "RC = %d", rc);
    }

    fail_unless(nfs != NULL);
    lsmNfsExport **exports = NULL;
    uint32_t count = 0;

    rc = lsmNfsList(c, &exports, &count, LSM_FLAG_RSVD);

    fail_unless(LSM_ERR_OK == rc, "lsmNfsList rc= %d\n", rc);
    fail_unless(count == 0);
    fail_unless(NULL == exports);


    lsmStringList *access = lsmStringListAlloc(1);
    fail_unless(NULL != access);

    lsmStringListElemSet(access, 0, "192.168.2.29");

    lsmNfsExport *e = NULL;

    rc = lsmNfsExportFs(c, lsmFsIdGet(nfs), NULL, access, access, NULL,
                            ANON_UID_GID_NA, ANON_UID_GID_NA, NULL, NULL, &e, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_OK == rc, "lsmNfsExportFs %d\n", rc);

    lsmNfsExportRecordFree(e);
    e=NULL;

    rc = lsmNfsList(c, &exports, &count, LSM_FLAG_RSVD);
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( exports != NULL);
    fail_unless( count == 1 );

    rc  = lsmNfsExportDelete(c, exports[0], LSM_FLAG_RSVD);
    fail_unless( LSM_ERR_OK == rc, "lsmNfsExportRemove %d\n", rc );
    lsmNfsExportRecordArrayFree(exports, count);

    exports = NULL;

    rc = lsmNfsList(c, &exports, &count, LSM_FLAG_RSVD);

    fail_unless(LSM_ERR_OK == rc, "lsmNfsList rc= %d\n", rc);
    fail_unless(count == 0);
    fail_unless(NULL == exports);
}
END_TEST

struct bad_record
{
    uint32_t m;
};


START_TEST(test_volume_methods)
{
    lsmVolume *v = NULL;
    lsmPool *test_pool = NULL;
    char *job = NULL;

    int rc = 0;

    fail_unless(c != NULL);

    test_pool = getTestPool(c);

    if( test_pool ) {
        rc = lsmVolumeCreate(c, test_pool, "lsm_volume_method_test",
                                    10000000, LSM_PROVISION_DEFAULT,
                                    &v, &job, LSM_FLAG_RSVD);

        if( LSM_ERR_JOB_STARTED == rc ) {
            v = wait_for_job_vol(c, &job);
        } else {
            fail_unless(LSM_ERR_OK == rc, "rc %d", rc);
        }

        if ( v ) {
            fail_unless( strcmp(lsmVolumePoolIdGet(v), lsmPoolIdGet(test_pool)) == 0 );

            rc = lsmVolumeDelete(c, v, &job, LSM_FLAG_RSVD);
            if( LSM_ERR_JOB_STARTED == rc ) {
                v = wait_for_job_vol(c, &job);
            } else {
                fail_unless(LSM_ERR_OK == rc, "rc %d", rc);
            }

            lsmVolumeRecordFree(v);
        }

        lsmPoolRecordFree(test_pool);
    }
}
END_TEST

START_TEST(test_invalid_input)
{
    fail_unless(c != NULL);
    int rc = 0;

    struct bad_record bad;
    bad.m = 0xA0A0A0A0;

    printf("Testing arguments\n");

    lsmPool *test_pool = getTestPool(c);

    lsmConnect *test_connect = NULL;
    lsmErrorPtr test_error = NULL;

    rc = lsmConnectPassword(NULL, NULL, NULL, 20000, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc %d", rc);

    rc = lsmConnectPassword("INVALID_URI:\\yep", NULL, &test_connect, 20000,
                                &test_error, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_URI, "rc %d", rc);


    rc = lsmConnectClose((lsmConnect *)&bad, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_CONN == rc, "rc %d", rc);

    rc = lsmConnectClose((lsmConnect *)NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_CONN == rc, "rc %d", rc);



    rc = lsmJobStatusGet(c, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    char *job = NULL;
    rc = lsmJobStatusGet(c, job, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    lsmJobStatus status;


    rc = lsmJobStatusGet(c, job, &status, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    uint8_t percent_complete;
    rc = lsmJobStatusGet(c, "NO_SUCH_JOB", &status, &percent_complete, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_NOT_FOUND_JOB == rc, "rc %d", rc);

    /* lsmJobStatusVolumeGet */
    lsmVolume *vol = NULL;
    rc = lsmJobStatusVolumeGet(c, NULL, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusVolumeGet(c, NULL, NULL, NULL, &vol, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusVolumeGet(c, job, NULL, NULL, &vol, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusVolumeGet(c, job, &status, NULL, &vol, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusVolumeGet(c, "NO_SUCH_JOB", &status, &percent_complete, &vol, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_NOT_FOUND_JOB == rc, "rc %d", rc);

    /* lsmJobStatusFsGet */
    lsmFs *fs = NULL;

    rc = lsmJobStatusFsGet(c, NULL, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusFsGet(c, NULL, NULL, NULL, &fs, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusFsGet(c, job, NULL, NULL, &fs, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusFsGet(c, job, &status, NULL, &fs, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusFsGet(c, "NO_SUCH_JOB", &status, &percent_complete, &fs, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_NOT_FOUND_JOB == rc, "rc %d", rc);

    /* lsmJobStatusFsGet */
    lsmSs *ss = (lsmSs *)&bad;

    rc = lsmJobStatusSsGet(c, NULL, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusSsGet(c, NULL, NULL, NULL, &ss, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    ss = NULL;

    rc = lsmJobStatusSsGet(c, job, NULL, NULL, &ss, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusSsGet(c, job, &status, NULL, &ss, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusSsGet(c, "NO_SUCH_JOB", &status, &percent_complete, &ss, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_NOT_FOUND_JOB == rc, "rc %d", rc);


    /* lsmJobFree */
    char *bogus_job = strdup("NO_SUCH_JOB");
    rc = lsmJobFree(c, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobFree(c, &bogus_job, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_NOT_FOUND_JOB == rc, "rc %d", rc);

    fail_unless(bogus_job != NULL, "Expected bogus job to != NULL!");
    free(bogus_job);


    /* lsmPoolList */
    rc = lsmPoolList(c, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    lsmPool **pools = NULL;
    rc = lsmPoolList(c, &pools, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    uint32_t count = 0;
    rc = lsmPoolList(c, NULL, &count, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    pools = (lsmPool **)&bad;
    rc = lsmPoolList(c, &pools, &count, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    /* lsmInitiatorList */
     rc = lsmInitiatorList(c, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    lsmInitiator **inits = NULL;
    rc = lsmInitiatorList(c, &inits, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmInitiatorList(c, NULL, &count, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    inits = (lsmInitiator **)&bad;
    rc = lsmInitiatorList(c, &inits, &count, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    /* lsmVolumeList */
     rc = lsmVolumeList(c, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    lsmVolume **vols = NULL;
    rc = lsmVolumeList(c, &vols, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmVolumeList(c, NULL, &count, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    vols = (lsmVolume **)&bad;
    rc = lsmVolumeList(c, &vols, &count, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    /* lsmVolumeCreate */
    lsmVolume *new_vol = NULL;
    job = NULL;

    rc = lsmVolumeCreate(c, NULL, NULL, 0, 0, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_POOL == rc, "rc %d", rc);

    rc = lsmVolumeCreate(c, (lsmPool *)&bad, "BAD_POOL", 10000000,
                            LSM_PROVISION_DEFAULT, &new_vol, &job, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_POOL == rc, "rc %d", rc);

    rc = lsmVolumeCreate(c, test_pool, "", 10000000, LSM_PROVISION_DEFAULT,
                            &new_vol, &job, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmVolumeCreate(c, test_pool, "ARG_TESTING", 10000000, LSM_PROVISION_DEFAULT,
                            NULL, &job, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmVolumeCreate(c, test_pool, "ARG_TESTING", 10000000, LSM_PROVISION_DEFAULT,
                            &new_vol, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    job = "NOT_NULL";
    rc = lsmVolumeCreate(c, test_pool, "ARG_TESTING", 10000000, LSM_PROVISION_DEFAULT,
                            &new_vol, &job, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    job = NULL;
    rc = lsmVolumeCreate(c, test_pool, "ARG_TESTING", 10000000, LSM_PROVISION_DEFAULT,
                            &new_vol, &job, LSM_FLAG_RSVD);

    if( LSM_ERR_JOB_STARTED == rc ) {
        new_vol = wait_for_job_vol(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc, "rc %d", rc);
    }

    /* lsmVolumeResize */
    rc = lsmVolumeResize(c, NULL, 0, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_VOL == rc, "rc %d", rc);


    lsmVolume *resized = (lsmVolume *)&bad;
    rc = lsmVolumeResize(c, new_vol, 20000000, &resized, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    resized = NULL;
    rc = lsmVolumeResize(c, new_vol, 20000000, &resized, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmVolumeResize(c, new_vol,    lsmVolumeNumberOfBlocksGet(new_vol) *
                                        lsmVolumeBlockSizeGet(new_vol),
                                        &resized, &job, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_VOLUME_SAME_SIZE == rc, "rc = %d", rc);

    rc = lsmVolumeResize(c, new_vol, 20000000, &resized, &job, LSM_FLAG_RSVD);

    if( LSM_ERR_JOB_STARTED == rc ) {
        resized = wait_for_job_vol(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc, "rc %d", rc);
    }

    /* lsmVolumeDelete */
    rc = lsmVolumeDelete(c, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_VOL == rc, "rc %d", rc);

    rc = lsmVolumeDelete(c, resized, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmVolumeDelete(c, resized, &job, LSM_FLAG_RSVD);
    if( LSM_ERR_JOB_STARTED == rc ) {
        wait_for_job(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc, "rc %d", rc);
    }

    /* lsmStorageCapabilities * */
    lsmSystem **sys = NULL;
    uint32_t num_systems = 0;
    rc = lsmSystemList(c, &sys, &num_systems, LSM_FLAG_RSVD );

    fail_unless(LSM_ERR_OK == rc, "rc %d", rc);
    fail_unless( sys != NULL);
    fail_unless( num_systems >= 1, "num_systems %d", num_systems);


    rc = lsmCapabilities(c, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_SYSTEM, "rc %d", rc);

    rc = lsmCapabilities(c, sys[0], NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT, "rc %d", rc);

    /* lsmVolumeReplicate */
    lsmVolume *cloned = NULL;
    rc = lsmVolumeReplicate(c, (lsmPool *)&bad, 0, NULL, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_POOL, "rc = %d", rc);

    rc = lsmVolumeReplicate(c, test_pool, LSM_VOLUME_REPLICATE_CLONE, NULL,
                            "cloned", &cloned, &job, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);

    rc = lsmVolumeReplicate(c, test_pool, LSM_VOLUME_REPLICATE_CLONE, new_vol,
                            "", &cloned, &job, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmVolumeReplicate(c, test_pool, LSM_VOLUME_REPLICATE_CLONE, new_vol,
                            "cloned", NULL, &job, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmVolumeReplicate(c, test_pool, LSM_VOLUME_REPLICATE_CLONE, new_vol,
                            "cloned", &cloned, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    /* lsmVolumeReplicateRangeBlockSize */
    rc = lsmVolumeReplicateRangeBlockSize(c, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmVolumeReplicateRange */
    rc = lsmVolumeReplicateRange(c, LSM_VOLUME_REPLICATE_CLONE, NULL, NULL,
                                    NULL, 0, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);

    rc = lsmVolumeReplicateRange(c, LSM_VOLUME_REPLICATE_CLONE, new_vol,
                                    NULL, NULL, 0, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);

    rc = lsmVolumeReplicateRange(c, LSM_VOLUME_REPLICATE_CLONE, new_vol, new_vol,
                                    NULL, 1, &job, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    rc = lsmVolumeOnline(c, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);

    rc = lsmVolumeOffline(c, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);


    /* lsmAccessGroupCreate */
    lsmAccessGroup *ag = NULL;

    rc = lsmAccessGroupCreate(c, NULL, NULL, 0, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmAccessGroupCreate(c, "my_group", ISCSI_HOST[0], LSM_INITIATOR_OTHER,
                                "system-id", NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmAccessGroupCreate(c, "my_group", ISCSI_HOST[0], LSM_INITIATOR_OTHER,
                                SYSTEM_ID, &ag, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_OK, "rc = %d", rc);
    fail_unless(ag != NULL);


    /* lsmAccessGroupDel */
    rc = lsmAccessGroupDelete(c, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ACCESS_GROUP, "rc = %d", rc);

    /* lsmAccessGroupInitiatorAdd */
    rc = lsmAccessGroupInitiatorAdd(c, NULL, NULL, 0, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ACCESS_GROUP, "rc = %d", rc);


    rc = lsmAccessGroupInitiatorDelete(c, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ACCESS_GROUP, "rc = %d", rc);

    rc = lsmAccessGroupInitiatorDelete(c, ag, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);



    rc = lsmAccessGroupGrant(c, NULL, NULL, 0, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ACCESS_GROUP, "rc = %d", rc);

    rc = lsmAccessGroupGrant(c, ag, NULL, 0, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);

    rc = lsmAccessGroupRevoke(c, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ACCESS_GROUP, "rc = %d", rc);

    rc = lsmAccessGroupRevoke(c, ag, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);


    /* lsmVolumesAccessibleByAccessGroup */
    rc = lsmVolumesAccessibleByAccessGroup(c, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ACCESS_GROUP, "rc = %d", rc);

    rc = lsmVolumesAccessibleByAccessGroup(c, ag, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmAccessGroupsGrantedToVolume */
    rc = lsmAccessGroupsGrantedToVolume(c, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);

    rc = lsmAccessGroupsGrantedToVolume(c, new_vol, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmVolumeChildDependency */
    rc = lsmVolumeChildDependency(c, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);

    rc = lsmVolumeChildDependency(c, new_vol, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /*lsmVolumeChildDependencyRm*/
    rc = lsmVolumeChildDependencyRm(c, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);

    rc = lsmVolumeChildDependencyRm(c, new_vol, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmSystemList */
    lsmSystem **systems = NULL;
    rc = lsmSystemList(c, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    rc = lsmSystemList(c, &systems, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmFsList */
    rc = lsmFsList(c, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    lsmFs **fsl = NULL;
    rc = lsmFsList(c, &fsl, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    /*lsmFsCreate*/
    rc = lsmFsCreate(c, NULL, NULL, 0, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_POOL, "rc = %d", rc);

    rc = lsmFsCreate(c, test_pool, NULL, 0, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    lsmFs *arg_fs = NULL;
    rc = lsmFsCreate(c, test_pool, "argument_fs", 10000000, &arg_fs, &job,
                        LSM_FLAG_RSVD);

    if( LSM_ERR_JOB_STARTED == rc ) {
        arg_fs = wait_for_job_fs(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc, "rc = %d", rc);
    }

    /* lsmFsDelete */
    rc = lsmFsDelete(c, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    rc = lsmFsDelete(c, arg_fs, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmFsResize */
    rc = lsmFsResize(c, NULL, 0, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    rc = lsmFsResize(c, arg_fs, 0, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmFsClone */
    rc = lsmFsClone(c, NULL, NULL, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    rc = lsmFsClone(c, arg_fs, NULL, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    /*lsmFsFileClone*/
    rc = lsmFsFileClone(c, NULL, NULL, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    rc = lsmFsFileClone(c, arg_fs, NULL, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    rc = lsmFsChildDependency(c, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    lsmStringList *badf = (lsmStringList *)&bad;
    rc = lsmFsChildDependency(c, arg_fs, badf, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_SL, "rc = %d", rc);

    lsmStringList *f = lsmStringListAlloc(1);
    rc = lsmFsChildDependency(c, arg_fs, f, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /*lsmFsChildDependencyRm*/
    rc = lsmFsChildDependencyRm(c, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    rc = lsmFsChildDependencyRm(c, arg_fs, badf, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_SL, "rc = %d", rc);

    rc = lsmFsChildDependencyRm(c, arg_fs, f, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);



    rc = lsmFsSsList(c, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);


    rc = lsmFsSsList(c, arg_fs, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    rc = lsmFsSsCreate(c, NULL, NULL, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    rc = lsmFsSsCreate(c, arg_fs, NULL, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    lsmSs *arg_ss = NULL;
    rc = lsmFsSsCreate(c, arg_fs, "arg_snapshot", badf, &arg_ss, &job,
                        LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_SL, "rc = %d", rc);

    rc = lsmFsSsCreate(c, arg_fs, "arg_snapshot", NULL, &arg_ss, &job,
                        LSM_FLAG_RSVD);

    if( LSM_ERR_JOB_STARTED == rc ) {
        arg_ss = wait_for_job_ss(c, &job);
    } else {
        fail_unless(rc == LSM_ERR_OK, "rc = %d", rc);
    }

    rc = lsmFsSsDelete(c, NULL, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    rc = lsmFsSsDelete(c, arg_fs, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_SS, "rc = %d", rc);

    rc = lsmFsSsDelete(c, arg_fs, arg_ss, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    rc = lsmFsSsRevert(c, NULL, NULL, NULL, NULL, 0, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    rc = lsmFsSsRevert(c, arg_fs, NULL, NULL, NULL, 0, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_SS, "rc = %d", rc);

    rc = lsmFsSsRevert(c, arg_fs, arg_ss, badf, NULL, 0, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_SL, "rc = %d", rc);

    rc = lsmFsSsRevert(c, arg_fs, arg_ss, badf, badf, 0, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_SL, "rc = %d", rc);

    rc = lsmFsSsRevert(c, arg_fs, arg_ss, f, f, 0, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmNfsList(c, NULL, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    rc = lsmNfsExportFs(c, NULL, NULL, NULL, NULL, NULL, 0,0,NULL, NULL, NULL,
                        LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmNfsExportFs(c, NULL, NULL, badf, NULL, NULL, 0,0,NULL, NULL, NULL,
                        LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_SL, "rc = %d", rc);

    rc = lsmNfsExportFs(c, NULL, NULL, f, badf, NULL, 0,0,NULL, NULL, NULL,
                        LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_SL, "rc = %d", rc);

    rc = lsmNfsExportFs(c, NULL, NULL, f, f, badf, 0,0,NULL, NULL, NULL,
                        LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_SL, "rc = %d", rc);


    rc = lsmNfsExportFs(c, NULL, NULL, f, f, f, 0,0, NULL, NULL, NULL,
                        LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmNfsExportDelete(c, NULL, LSM_FLAG_RSVD);
    fail_unless(rc == LSM_ERR_INVALID_NFS, "rc = %d", rc);


    /* Pool create */
    int raid_type = 65535;
    int member_type = 65535;
    uint64_t size = 0;
    int flags = 10;


    rc = lsmPoolCreate(NULL, NULL, NULL, size, raid_type, member_type, NULL, NULL, flags);
    fail_unless(rc == LSM_ERR_INVALID_CONN, "rc = %d", rc);

    rc = lsmPoolCreate(c, NULL, NULL, size, raid_type, member_type, NULL, NULL, flags);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmPoolCreate(c, SYSTEM_ID, NULL, size, raid_type, member_type, NULL, NULL, flags);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmPoolCreate(c, SYSTEM_ID, "pool name", size, raid_type, member_type, NULL, NULL, flags);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    size = 1024*1024*1024;

    rc = lsmPoolCreate(c, SYSTEM_ID, "pool name", size, raid_type, member_type, NULL, NULL, flags);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    raid_type = LSM_POOL_RAID_TYPE_0;
    rc = lsmPoolCreate(c, SYSTEM_ID, "pool name", size, raid_type, member_type, NULL, NULL, flags);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    member_type = LSM_POOL_MEMBER_TYPE_DISK;
    rc = lsmPoolCreate(c, SYSTEM_ID, "pool name", size, raid_type, member_type, NULL, NULL, flags);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    lsmPool *pcp = NULL;
    rc = lsmPoolCreate(c, SYSTEM_ID, "pool name", size, raid_type, member_type, &pcp, NULL, flags);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    char *pcj = NULL;
    rc = lsmPoolCreate(c, SYSTEM_ID, "pool name", size, raid_type, member_type, &pcp, &pcj, flags);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


}
END_TEST

static void cap_test( lsmStorageCapabilities *cap,  lsmCapabilityType t)
{
    lsmCapabilityValueType supported;
    supported = lsmCapabilityGet(cap, t);

    fail_unless( supported == LSM_CAPABILITY_SUPPORTED,
                    "supported = %d for %d", supported, t);
}

START_TEST(test_capabilities)
{
    int rc = 0;

    lsmSystem **sys = NULL;
    uint32_t sys_count = 0;
    lsmStorageCapabilities *cap = NULL;

    rc = lsmSystemList(c, &sys, &sys_count, LSM_FLAG_RSVD);
    fail_unless( LSM_ERR_OK == rc, "rc = %d", rc);
    fail_unless( sys_count >= 1, "count = %d", sys_count);

    printf("lsmCapabilities\n");
    rc = lsmCapabilities(c, sys[0], &cap, LSM_FLAG_RSVD);
    fail_unless( LSM_ERR_OK == rc, "rc = %d", rc);

    if( LSM_ERR_OK == rc ) {
        cap_test(cap, LSM_CAP_BLOCK_SUPPORT);
        cap_test(cap, LSM_CAP_FS_SUPPORT);
        cap_test(cap, LSM_CAP_VOLUMES);
        cap_test(cap, LSM_CAP_VOLUME_CREATE);
        cap_test(cap, LSM_CAP_VOLUME_RESIZE);
        cap_test(cap, LSM_CAP_VOLUME_REPLICATE);
        cap_test(cap, LSM_CAP_VOLUME_REPLICATE_CLONE);
        cap_test(cap, LSM_CAP_VOLUME_REPLICATE_COPY);
        cap_test(cap, LSM_CAP_VOLUME_REPLICATE_MIRROR_ASYNC);
        cap_test(cap, LSM_CAP_VOLUME_REPLICATE_MIRROR_SYNC);
        cap_test(cap, LSM_CAP_VOLUME_COPY_RANGE_BLOCK_SIZE);
        cap_test(cap, LSM_CAP_VOLUME_COPY_RANGE);
        cap_test(cap, LSM_CAP_VOLUME_COPY_RANGE_CLONE);
        cap_test(cap, LSM_CAP_VOLUME_COPY_RANGE_COPY);
        cap_test(cap, LSM_CAP_VOLUME_DELETE);
        cap_test(cap, LSM_CAP_VOLUME_ONLINE);
        cap_test(cap, LSM_CAP_VOLUME_OFFLINE);
        cap_test(cap, LSM_CAP_ACCESS_GROUP_GRANT);
        cap_test(cap, LSM_CAP_ACCESS_GROUP_REVOKE);
        cap_test(cap, LSM_CAP_ACCESS_GROUP_LIST);
        cap_test(cap, LSM_CAP_ACCESS_GROUP_CREATE);
        cap_test(cap, LSM_CAP_ACCESS_GROUP_DELETE);
        cap_test(cap, LSM_CAP_ACCESS_GROUP_ADD_INITIATOR);
        cap_test(cap, LSM_CAP_ACCESS_GROUP_DEL_INITIATOR);
        cap_test(cap, LSM_CAP_VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP);
        cap_test(cap, LSM_CAP_ACCESS_GROUPS_GRANTED_TO_VOLUME);
        cap_test(cap, LSM_CAP_VOLUME_CHILD_DEPENDENCY);
        cap_test(cap, LSM_CAP_VOLUME_CHILD_DEPENDENCY_RM);
        cap_test(cap, LSM_CAP_FS);
        cap_test(cap, LSM_CAP_FS_DELETE);
        cap_test(cap, LSM_CAP_FS_RESIZE);
        cap_test(cap, LSM_CAP_FS_CREATE);
        cap_test(cap, LSM_CAP_FS_CLONE);
        cap_test(cap, LSM_CAP_FILE_CLONE);
        cap_test(cap, LSM_CAP_FS_SNAPSHOTS);
        cap_test(cap, LSM_CAP_FS_SNAPSHOT_CREATE);
        cap_test(cap, LSM_CAP_FS_SNAPSHOT_CREATE_SPECIFIC_FILES);
        cap_test(cap, LSM_CAP_FS_SNAPSHOT_DELETE);
        cap_test(cap, LSM_CAP_FS_SNAPSHOT_REVERT);
        cap_test(cap, LSM_CAP_FS_SNAPSHOT_REVERT_SPECIFIC_FILES);
        cap_test(cap, LSM_CAP_FS_CHILD_DEPENDENCY);
        cap_test(cap, LSM_CAP_FS_CHILD_DEPENDENCY_RM);
        cap_test(cap, LSM_CAP_FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES );
        cap_test(cap, LSM_CAP_EXPORT_AUTH);
        cap_test(cap, LSM_CAP_EXPORTS);
        cap_test(cap, LSM_CAP_EXPORT_FS);
        cap_test(cap, LSM_CAP_EXPORT_REMOVE);
    }
}
END_TEST

static int get_init(lsmConnect *c, const char *init_id, lsmInitiator * *found) {
    lsmInitiator **inits = NULL;
    uint32_t count = 0;
    int rc = lsmInitiatorList(c, &inits, &count, LSM_FLAG_RSVD);

    fail_unless(LSM_ERR_OK == rc );

    if( LSM_ERR_OK == rc ) {
        uint32_t i = 0;
        for(i = 0; i < count; ++i ) {
            if( strcmp(lsmInitiatorIdGet(inits[i]), init_id) == 0 ) {
                *found = lsmInitiatorRecordCopy(inits[i]);
                if( !*found ) {
                    rc = LSM_ERR_NO_MEMORY;
                }
                break;
            }
        }
    }

    lsmInitiatorRecordArrayFree(inits, count);

    return rc;
}



START_TEST(test_initiator_methods)
{
    fail_unless(c != NULL);

    lsmPool *test_pool = getTestPool(c);
    lsmVolume *nv = NULL;
    char *job = NULL;
    int rc = 0;


    rc = lsmVolumeCreate(c, test_pool, "initiator_mapping_test", 40 *1024*1024,
                        LSM_PROVISION_DEFAULT, &nv, &job, LSM_FLAG_RSVD);

    if( LSM_ERR_JOB_STARTED == rc ) {
        nv = wait_for_job_vol(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc );
    }

    rc = lsmInitiatorGrant(c, ISCSI_HOST[0], LSM_INITIATOR_ISCSI, nv,
                            LSM_VOLUME_ACCESS_READ_WRITE, LSM_FLAG_RSVD);

    if( LSM_ERR_JOB_STARTED == rc ) {
        wait_for_job(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc, "rc = %d", rc);
    }

    lsmInitiator *initiator = NULL;

    rc = get_init(c, ISCSI_HOST[0], &initiator);

    fail_unless(LSM_ERR_OK == rc, "rc = %d", rc);


    lsmVolume **volumes = NULL;
    uint32_t count = 0;
    rc = lsmVolumesAccessibleByInitiator(c, initiator, &volumes, &count, LSM_FLAG_RSVD);

    fail_unless( LSM_ERR_OK == rc );

    if( LSM_ERR_OK == rc ) {
        fail_unless(count == 1, "count = %d", count);
        if( count == 1 ) {
            fail_unless( strcmp(lsmVolumeIdGet(volumes[0]), lsmVolumeIdGet(nv)) == 0);
        }

        lsmVolumeRecordArrayFree(volumes, count);
    }

    lsmInitiator **initiators = NULL;
    count = 0;
    rc = lsmInitiatorsGrantedToVolume(c, nv, &initiators, &count, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_OK == rc, "rc = %d", rc);

    if( LSM_ERR_OK == rc ) {
        fail_unless( count == 1, "count = %d", count);
        if( count == 1 ) {
            fail_unless( strcmp(lsmInitiatorIdGet(initiators[0]),
                            lsmInitiatorIdGet(initiator)) == 0);

            fail_unless(lsmInitiatorTypeGet(initiators[0]) == LSM_INITIATOR_ISCSI);
            fail_unless(lsmInitiatorNameGet(initiators[0]) != NULL);
        }

        lsmInitiatorRecordArrayFree(initiators, count);
    }

    if( LSM_ERR_OK == rc ) {
        rc = lsmInitiatorRevoke(c, initiator, nv, LSM_FLAG_RSVD);

        if( LSM_ERR_JOB_STARTED == rc ) {
            wait_for_job(c, &job);
        } else {
            fail_unless(LSM_ERR_OK == rc, "rc = %d", rc);
        }

        lsmInitiatorRecordFree(initiator);
    }

    lsmVolumeRecordFree(nv);
    lsmPoolRecordFree(test_pool);
}
END_TEST

START_TEST(test_iscsi_auth_in)
{
    lsmAccessGroup *group = NULL;
    char *job = NULL;

    int rc = lsmAccessGroupCreate(c, "ISCSI_AUTH", ISCSI_HOST[0],
                    LSM_INITIATOR_ISCSI, SYSTEM_ID, &group, LSM_FLAG_RSVD);

    fail_unless(LSM_ERR_OK == rc, "rc = %d");

    if( LSM_ERR_OK == rc ) {
        lsmInitiator **inits = NULL;
        uint32_t init_count = 0;

         rc = lsmInitiatorList(c, &inits, &init_count, LSM_FLAG_RSVD );
         fail_unless(LSM_ERR_OK == rc );

         if( LSM_ERR_OK == rc && init_count ) {
             rc = lsmISCSIChapAuth(c, inits[0], "username", "secret",
                                            NULL, NULL, LSM_FLAG_RSVD);

             fail_unless(LSM_ERR_OK == rc, "rc = %d", rc) ;
         }

         rc = lsmAccessGroupDelete(c, group, LSM_FLAG_RSVD);

         if(LSM_ERR_JOB_STARTED == rc ) {
             wait_for_job(c, &job);
         } else {
             fail_unless(LSM_ERR_OK == rc );
         }

         lsmAccessGroupRecordFree(group);
         group = NULL;
    }
}
END_TEST

START_TEST(test_plugin_info)
{
    char *desc = NULL;
    char *version = NULL;

    int rc = lsmPluginInfoGet(c, &desc, &version, LSM_FLAG_RSVD);

    fail_unless(LSM_ERR_OK == rc, "rc = %d", rc);

    if( LSM_ERR_OK == rc ) {
        printf("Desc: (%s), Version: (%s)\n", desc, version);
        free(desc);
        free(version);
    }

    rc = lsmPluginInfoGet(NULL, &desc, &version, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_CONN == rc, "rc = %d", rc);

    rc = lsmPluginInfoGet(c, NULL, &version, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc = %d", rc);

    rc = lsmPluginInfoGet(c, &desc, NULL, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc = %d", rc);
}
END_TEST

START_TEST(test_get_available_plugins)
{
    int i = 0;
    int num = 0;
    lsmStringList *plugins = NULL;

    int rc = lsmAvailablePluginsList(":", &plugins, 0);
    fail_unless(LSM_ERR_OK == rc, "rc = %d", rc);

    num = lsmStringListSize(plugins);
    for( i = 0; i < num; i++) {
        const char *info = lsmStringListElemGet(plugins, i);
        fail_unless(strlen(info) > 0);
        printf("%s\n", info);
    }

    fail_unless(lsmStringListFree(plugins) == LSM_ERR_OK);
}
END_TEST

START_TEST(test_error_reporting)
{
    uint8_t d[4] = {0x00, 0x01, 0x02, 0x03};
    char msg[] = "Testing Errors";
    char exception[] = "Exception text";
    char debug_msg[] = "Debug message";
    void *debug_data = NULL;
    uint32_t debug_size = 0;

    lsmErrorPtr e = lsmErrorCreate(   LSM_ERR_INTERNAL_ERROR,
                                        LSM_ERR_DOMAIN_PLUG_IN,
                                        LSM_ERR_LEVEL_ERROR, msg,
                                        exception, debug_msg,
                                        d, sizeof(d));

    fail_unless(e != NULL);

    if( e ) {
        fail_unless(LSM_ERR_INTERNAL_ERROR == lsmErrorNumberGet(e));
        fail_unless(LSM_ERR_DOMAIN_PLUG_IN == lsmErrorDomainGet(e));
        fail_unless(LSM_ERR_LEVEL_ERROR == lsmErrorLevelGet(e));
        fail_unless(strcmp(msg, lsmErrorMessageGet(e)) == 0);
        fail_unless(strcmp(exception, lsmErrorExceptionGet(e)) == 0);
        fail_unless(strcmp(debug_msg, lsmErrorDebugGet(e)) == 0);
        debug_data = lsmErrorDebugDataGet(e, &debug_size);
        fail_unless(debug_data != NULL);
        fail_unless(debug_size == sizeof(d));
        fail_unless(memcmp(d, debug_data, debug_size) == 0);
        fail_unless( LSM_ERR_OK == lsmErrorFree(e) );
    }
}
END_TEST

START_TEST(test_capability)
{
    int rc;
    int i;
    lsmCapabilityType expected_present[] = {
        LSM_CAP_BLOCK_SUPPORT,
        LSM_CAP_FS_SUPPORT,
        LSM_CAP_INITIATORS,
        LSM_CAP_VOLUMES,
        LSM_CAP_VOLUME_CREATE,
        LSM_CAP_VOLUME_RESIZE,
        LSM_CAP_VOLUME_REPLICATE,
        LSM_CAP_VOLUME_REPLICATE_CLONE,
        LSM_CAP_VOLUME_REPLICATE_COPY,
        LSM_CAP_VOLUME_REPLICATE_MIRROR_ASYNC,
        LSM_CAP_VOLUME_REPLICATE_MIRROR_SYNC,
        LSM_CAP_VOLUME_COPY_RANGE_BLOCK_SIZE,
        LSM_CAP_VOLUME_COPY_RANGE,
        LSM_CAP_VOLUME_COPY_RANGE_CLONE,
        LSM_CAP_VOLUME_COPY_RANGE_COPY,
        LSM_CAP_VOLUME_DELETE,
        LSM_CAP_VOLUME_ONLINE,
        LSM_CAP_VOLUME_OFFLINE,
        LSM_CAP_ACCESS_GROUP_GRANT,
        LSM_CAP_ACCESS_GROUP_REVOKE,
        LSM_CAP_ACCESS_GROUP_LIST,
        LSM_CAP_ACCESS_GROUP_CREATE,
        LSM_CAP_ACCESS_GROUP_DELETE,
        LSM_CAP_ACCESS_GROUP_ADD_INITIATOR,
        LSM_CAP_ACCESS_GROUP_DEL_INITIATOR,
        LSM_CAP_VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP,
        LSM_CAP_ACCESS_GROUPS_GRANTED_TO_VOLUME,
        LSM_CAP_VOLUME_CHILD_DEPENDENCY,
        LSM_CAP_VOLUME_CHILD_DEPENDENCY_RM,
        LSM_CAP_FS,
        LSM_CAP_FS_DELETE,
        LSM_CAP_FS_RESIZE,
        LSM_CAP_FS_CREATE,
        LSM_CAP_FS_CLONE,
        LSM_CAP_FILE_CLONE,
        LSM_CAP_FS_SNAPSHOTS,
        LSM_CAP_FS_SNAPSHOT_CREATE,
        LSM_CAP_FS_SNAPSHOT_CREATE_SPECIFIC_FILES,
        LSM_CAP_FS_SNAPSHOT_DELETE,
        LSM_CAP_FS_SNAPSHOT_REVERT,
        LSM_CAP_FS_SNAPSHOT_REVERT_SPECIFIC_FILES,
        LSM_CAP_FS_CHILD_DEPENDENCY,
        LSM_CAP_FS_CHILD_DEPENDENCY_RM,
        LSM_CAP_FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES,
        LSM_CAP_EXPORT_AUTH,
        LSM_CAP_EXPORTS,
        LSM_CAP_EXPORT_FS,
        LSM_CAP_EXPORT_REMOVE};

    lsmCapabilityType expected_absent[] = {
        LSM_CAP_EXPORT_CUSTOM_PATH,
        LSM_CAP_POOL_CREATE,
        LSM_CAP_POOL_CREATE_FROM_DISKS,
        LSM_CAP_POOL_CREATE_FROM_VOLUMES,
        LSM_CAP_POOL_CREATE_FROM_POOL,

        LSM_CAP_POOL_CREATE_DISK_RAID_0,
        LSM_CAP_POOL_CREATE_DISK_RAID_1,
        LSM_CAP_POOL_CREATE_DISK_RAID_JBOD,
        LSM_CAP_POOL_CREATE_DISK_RAID_3,
        LSM_CAP_POOL_CREATE_DISK_RAID_4,
        LSM_CAP_POOL_CREATE_DISK_RAID_5,
        LSM_CAP_POOL_CREATE_DISK_RAID_6,
        LSM_CAP_POOL_CREATE_DISK_RAID_10,
        LSM_CAP_POOL_CREATE_DISK_RAID_50,
        LSM_CAP_POOL_CREATE_DISK_RAID_51,
        LSM_CAP_POOL_CREATE_DISK_RAID_60,
        LSM_CAP_POOL_CREATE_DISK_RAID_61,
        LSM_CAP_POOL_CREATE_DISK_RAID_15,
        LSM_CAP_POOL_CREATE_DISK_RAID_16,
        LSM_CAP_POOL_CREATE_DISK_RAID_NOT_APPLICABLE,

        LSM_CAP_POOL_CREATE_VOLUME_RAID_0,
        LSM_CAP_POOL_CREATE_VOLUME_RAID_1,
        LSM_CAP_POOL_CREATE_VOLUME_RAID_JBOD,
        LSM_CAP_POOL_CREATE_VOLUME_RAID_3,
        LSM_CAP_POOL_CREATE_VOLUME_RAID_4,
        LSM_CAP_POOL_CREATE_VOLUME_RAID_5,
        LSM_CAP_POOL_CREATE_VOLUME_RAID_6,
        LSM_CAP_POOL_CREATE_VOLUME_RAID_10,
        LSM_CAP_POOL_CREATE_VOLUME_RAID_50,
        LSM_CAP_POOL_CREATE_VOLUME_RAID_51,
        LSM_CAP_POOL_CREATE_VOLUME_RAID_60,
        LSM_CAP_POOL_CREATE_VOLUME_RAID_61,
        LSM_CAP_POOL_CREATE_VOLUME_RAID_15,
        LSM_CAP_POOL_CREATE_VOLUME_RAID_16,
        LSM_CAP_POOL_CREATE_VOLUME_RAID_NOT_APPLICABLE
    };


    lsmStorageCapabilities *cap = lsmCapabilityRecordAlloc(NULL);

    fail_unless(cap != NULL);

    if( cap ) {
        rc = lsmCapabilitySetN(cap, LSM_CAPABILITY_SUPPORTED, 48,
            LSM_CAP_BLOCK_SUPPORT,
            LSM_CAP_FS_SUPPORT,
            LSM_CAP_INITIATORS,
            LSM_CAP_VOLUMES,
            LSM_CAP_VOLUME_CREATE,
            LSM_CAP_VOLUME_RESIZE,
            LSM_CAP_VOLUME_REPLICATE,
            LSM_CAP_VOLUME_REPLICATE_CLONE,
            LSM_CAP_VOLUME_REPLICATE_COPY,
            LSM_CAP_VOLUME_REPLICATE_MIRROR_ASYNC,
            LSM_CAP_VOLUME_REPLICATE_MIRROR_SYNC,
            LSM_CAP_VOLUME_COPY_RANGE_BLOCK_SIZE,
            LSM_CAP_VOLUME_COPY_RANGE,
            LSM_CAP_VOLUME_COPY_RANGE_CLONE,
            LSM_CAP_VOLUME_COPY_RANGE_COPY,
            LSM_CAP_VOLUME_DELETE,
            LSM_CAP_VOLUME_ONLINE,
            LSM_CAP_VOLUME_OFFLINE,
            LSM_CAP_ACCESS_GROUP_GRANT,
            LSM_CAP_ACCESS_GROUP_REVOKE,
            LSM_CAP_ACCESS_GROUP_LIST,
            LSM_CAP_ACCESS_GROUP_CREATE,
            LSM_CAP_ACCESS_GROUP_DELETE,
            LSM_CAP_ACCESS_GROUP_ADD_INITIATOR,
            LSM_CAP_ACCESS_GROUP_DEL_INITIATOR,
            LSM_CAP_VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP,
            LSM_CAP_ACCESS_GROUPS_GRANTED_TO_VOLUME,
            LSM_CAP_VOLUME_CHILD_DEPENDENCY,
            LSM_CAP_VOLUME_CHILD_DEPENDENCY_RM,
            LSM_CAP_FS,
            LSM_CAP_FS_DELETE,
            LSM_CAP_FS_RESIZE,
            LSM_CAP_FS_CREATE,
            LSM_CAP_FS_CLONE,
            LSM_CAP_FILE_CLONE,
            LSM_CAP_FS_SNAPSHOTS,
            LSM_CAP_FS_SNAPSHOT_CREATE,
            LSM_CAP_FS_SNAPSHOT_CREATE_SPECIFIC_FILES,
            LSM_CAP_FS_SNAPSHOT_DELETE,
            LSM_CAP_FS_SNAPSHOT_REVERT,
            LSM_CAP_FS_SNAPSHOT_REVERT_SPECIFIC_FILES,
            LSM_CAP_FS_CHILD_DEPENDENCY,
            LSM_CAP_FS_CHILD_DEPENDENCY_RM,
            LSM_CAP_FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES,
            LSM_CAP_EXPORT_AUTH,
            LSM_CAP_EXPORTS,
            LSM_CAP_EXPORT_FS,
            LSM_CAP_EXPORT_REMOVE
            );

        fail_unless(rc == LSM_ERR_OK);
        rc = lsmCapabilitySet(cap, LSM_CAP_EXPORTS, LSM_CAPABILITY_SUPPORTED);
        fail_unless(rc == LSM_ERR_OK);

        for( i = 0;
            i < sizeof(expected_present)/sizeof(expected_present[0]);
            ++i) {

            fail_unless( lsmCapabilityGet(cap, expected_present[i]) ==
                            LSM_CAPABILITY_SUPPORTED);
        }

        for( i = 0;
            i < sizeof(expected_absent)/sizeof(expected_absent[0]);
            ++i) {

            fail_unless( lsmCapabilityGet(cap, expected_absent[i]) ==
                            LSM_CAPABILITY_UNSUPPORTED);
        }


        lsmCapabilityRecordFree(cap);
    }

}
END_TEST


START_TEST(test_nfs_export_funcs)
{
    const char id[] = "export_unique_id";
    const char fs_id[] = "fs_unique_id";
    const char export_path[] = "/mnt/foo";
    const char auth[] = "simple";
    uint64_t anonuid = 1021;
    uint64_t anongid = 1000;
    const char options[] = "vendor_specific_option";
    char rstring[33];


    lsmStringList *root = lsmStringListAlloc(0);
    lsmStringListAppend(root, "192.168.100.2");
    lsmStringListAppend(root, "192.168.100.3");

    lsmStringList *rw = lsmStringListAlloc(0);
    lsmStringListAppend(rw, "192.168.100.2");
    lsmStringListAppend(rw, "192.168.100.3");

    lsmStringList *rand =  lsmStringListAlloc(0);

    lsmStringList *ro = lsmStringListAlloc(0);
    lsmStringListAppend(ro, "*");


    lsmNfsExport *export = lsmNfsExportRecordAlloc(id, fs_id, export_path, auth,
        root, rw, ro, anonuid, anongid, options);

    lsmNfsExport *copy = lsmNfsExportRecordCopy(export);


    fail_unless( strcmp(lsmNfsExportIdGet(copy), id) == 0 );
    fail_unless( strcmp(lsmNfsExportFsIdGet(copy), fs_id) == 0);
    fail_unless( strcmp(lsmNfsExportExportPathGet(copy), export_path) == 0);
    fail_unless( strcmp(lsmNfsExportAuthTypeGet(copy), auth) == 0);
    fail_unless( strcmp(lsmNfsExportOptionsGet(copy), options) == 0);
    fail_unless( lsmNfsExportAnonUidGet(copy) == anonuid);
    fail_unless( lsmNfsExportAnonGidGet(copy) == anongid);

    fail_unless(compare_string_lists(lsmNfsExportRootGet(export), lsmNfsExportRootGet(copy)) == 0);
    fail_unless(compare_string_lists(lsmNfsExportReadWriteGet(export), lsmNfsExportReadWriteGet(copy)) == 0);
    fail_unless(compare_string_lists(lsmNfsExportReadOnlyGet(export), lsmNfsExportReadOnlyGet(copy)) == 0);

    lsmNfsExportRecordFree(copy);


    generateRandom(rstring, sizeof(rstring));
    lsmNfsExportIdSet(export, rstring);
    fail_unless( strcmp(lsmNfsExportIdGet(export), rstring) == 0 );

    generateRandom(rstring, sizeof(rstring));
    lsmNfsExportFsIdSet(export, rstring);
    fail_unless( strcmp(lsmNfsExportFsIdGet(export), rstring) == 0 );

    generateRandom(rstring, sizeof(rstring));
    lsmNfsExportExportPathSet(export, rstring);
    fail_unless( strcmp(lsmNfsExportExportPathGet(export), rstring) == 0 );

    generateRandom(rstring, sizeof(rstring));
    lsmNfsExportAuthTypeSet(export, rstring);
    fail_unless( strcmp(lsmNfsExportAuthTypeGet(export), rstring) == 0 );

    generateRandom(rstring, sizeof(rstring));
    lsmNfsExportOptionsSet(export, rstring);
    fail_unless( strcmp(lsmNfsExportOptionsGet(export), rstring) == 0 );

    anonuid = anonuid + 700;
    lsmNfsExportAnonUidSet(export, anonuid);

    anongid = anongid + 400;
    lsmNfsExportAnonGidSet(export, anongid);

    fail_unless(lsmNfsExportAnonUidGet(export) == anonuid);
    fail_unless(lsmNfsExportAnonGidGet(export) == anongid);


    generateRandom(rstring, sizeof(rstring));
    lsmStringListAppend(rand, rstring);
    lsmNfsExportRootSet(export, rand);
    fail_unless(compare_string_lists(lsmNfsExportRootGet(export), rand) == 0);

    generateRandom(rstring, sizeof(rstring));
    lsmStringListAppend(rand, rstring);
    lsmNfsExportReadWriteSet(export, rand);
    fail_unless(compare_string_lists(lsmNfsExportReadWriteGet(export), rand) == 0);

    generateRandom(rstring, sizeof(rstring));
    lsmStringListAppend(rand, rstring);
    lsmNfsExportReadOnlySet(export, rand);
    fail_unless(compare_string_lists(lsmNfsExportReadOnlyGet(export), rand) == 0);


    lsmNfsExportRecordFree(export);
    lsmStringListFree(root);
    lsmStringListFree(rw);
    lsmStringListFree(ro);
    lsmStringListFree(rand);
}
END_TEST

START_TEST(test_pool_delete)
{
    int rc = 0;
    char *job = NULL;
    lsmVolume *v = NULL;

    printf("Testing pool delete!\n");

    lsmPool *test_pool = getTestPool(c);

    fail_unless( test_pool != NULL );

    if( test_pool ) {

        rc = lsmVolumeCreate(c, test_pool, "lsm_volume_pool_remove_test",
                                    10000000, LSM_PROVISION_DEFAULT,
                                    &v, &job, LSM_FLAG_RSVD);

        if( LSM_ERR_JOB_STARTED == rc ) {
            v = wait_for_job_vol(c, &job);
        } else {
            fail_unless(LSM_ERR_OK == rc, "rc %d", rc);
        }

        if( v ) {

            rc = lsmPoolDelete(c, test_pool, &job, LSM_FLAG_RSVD);

            fail_unless(LSM_ERR_EXISTS_VOLUME == rc, "rc %d", rc);

            if( LSM_ERR_EXISTS_VOLUME == rc ) {

                /* Delete the volume and try again */
                rc = lsmVolumeDelete(c, v, &job, LSM_FLAG_RSVD);

                if( LSM_ERR_JOB_STARTED == rc ) {
                    v = wait_for_job_vol(c, &job);
                } else {
                    fail_unless(LSM_ERR_OK == rc, "rc %d", rc);
                }

                rc = lsmPoolDelete(c, test_pool, &job, LSM_FLAG_RSVD);

                if( LSM_ERR_JOB_STARTED == rc ) {
                    v = wait_for_job_vol(c, &job);
                } else {
                    fail_unless(LSM_ERR_OK == rc, "rc %d", rc);
                }
            }
        }

        lsmPoolRecordFree(test_pool);
        lsmVolumeRecordFree(v);
    }
}
END_TEST

START_TEST(test_pool_create)
{
    int rc = 0;
    lsmPool *pool = NULL;
    char *job = NULL;
    lsmDisk **disks = NULL;
    uint32_t num_disks = 0;
    lsmStringList *member_ids = lsmStringListAlloc(0);
    char *pool_one = NULL;

    /*
     * Test basic pool create option.
     */
    rc = lsmPoolCreate(c, SYSTEM_ID, "pool_create_unit_test", 1024*1024*1024,
                        LSM_POOL_RAID_TYPE_0, LSM_POOL_MEMBER_TYPE_DISK, &pool,
                        &job, LSM_FLAG_RSVD);

    if( LSM_ERR_JOB_STARTED == rc ) {
        pool = wait_for_job_pool(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc, "rc %d", rc);
    }

    lsmPoolRecordFree(pool);
    pool = NULL;

    /*
     * Test pool creations from disks
     */
    rc = lsmDiskList(c, &disks, &num_disks, LSM_FLAG_RSVD);
    fail_unless(LSM_ERR_OK == rc, "rc = %d", rc);
    if( LSM_ERR_OK == rc && num_disks ) {
        int i = 0;

        /* Python simulator one accepts same type and size */
        lsmDiskType disk_type = lsmDiskTypeGet(disks[num_disks-1]);
        uint64_t size = lsmDiskNumberOfBlocksGet(disks[num_disks-1]);

        for( i = 0; i < num_disks; ++i ) {
            /* Only include disks of one type */
            if( lsmDiskTypeGet(disks[i]) == disk_type &&
                    size == lsmDiskNumberOfBlocksGet(disks[i])) {
                lsmStringListAppend(member_ids, lsmDiskIdGet(disks[i]));
            }
        }

        lsmDiskRecordArrayFree(disks, num_disks);
    }

    rc = lsmPoolCreateFromDisks(c, SYSTEM_ID, "pool_create_from_disks",
                                member_ids, LSM_POOL_RAID_TYPE_0, &pool, &job,
                                LSM_FLAG_RSVD);

    if( LSM_ERR_JOB_STARTED == rc ) {
        pool = wait_for_job_pool(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc, "lsmPoolCreateFromDisks %d (%s)", rc,
                                        error(lsmErrorLastGet(c)));
    }

    lsmStringListFree(member_ids);
    member_ids = lsmStringListAlloc(0);
    lsmPoolRecordFree(pool);
    pool = NULL;


    /* Test pool creation from volume */
    {
        lsmVolume **volumes = NULL;
        uint32_t num_volumes = 0;
        int i = 0;
        lsmPool **pools = NULL;
        uint32_t num_pools = 0;
        lsmVolume *vol = NULL;
        char r_name[32];

        /* Create some volumes */
        rc = lsmPoolList(c, &pools, &num_pools, LSM_FLAG_RSVD);

        fail_unless(LSM_ERR_OK == rc, "rc %d", rc);

        if( LSM_ERR_OK == rc ) {

            pool_one = strdup(lsmPoolIdGet(pools[0]));

            for( i = 0; i < num_pools; ++i ) {
                job = NULL;
                vol = NULL;

                generateRandom(r_name, sizeof(r_name));

                rc = lsmVolumeCreate(c, pools[i], r_name,
                        1024*1024*1024, LSM_PROVISION_DEFAULT,
                        &vol, &job, LSM_FLAG_RSVD);

                if( LSM_ERR_JOB_STARTED == rc ) {
                    vol = wait_for_job_vol(c, &job);
                } else {
                    fail_unless(LSM_ERR_OK == rc, "rc %d", rc);
                }

                lsmVolumeRecordFree(vol);
            }
        }

        rc = lsmVolumeList(c, &volumes, &num_volumes, LSM_FLAG_RSVD);
        fail_unless( LSM_ERR_OK == rc );

        if( num_volumes ) {
            uint64_t size = lsmVolumeNumberOfBlocksGet(volumes[num_volumes-1]);
            for( i = num_volumes - 1; i > 0; --i ) {
                if( lsmVolumeNumberOfBlocksGet(volumes[i]) == size ) {
                    lsmStringListAppend(member_ids, lsmVolumeIdGet(volumes[i]));
                }
            }

            pool = NULL;
            job = NULL;

            rc = lsmPoolCreateFromVolumes(c, SYSTEM_ID,
                                    "pool_create_from_volumes", member_ids,
                                    LSM_POOL_RAID_TYPE_0, &pool, &job,
                                    LSM_FLAG_RSVD);

            if( LSM_ERR_JOB_STARTED == rc ) {
                pool = wait_for_job_pool(c, &job);
            } else {
                fail_unless(LSM_ERR_OK == rc,
                                    "lsmPoolCreateFromVolumes %d (%s)",
                                    rc, error(lsmErrorLastGet(c)));
            }

            lsmPoolRecordFree(pool);
            pool = NULL;

            lsmVolumeRecordArrayFree(volumes, num_volumes);
            volumes = NULL;
            num_volumes = 0;
            lsmStringListFree(member_ids);
        }
    }

    /* Test pool creation from pool */
    {
        if( pool_one ) {
            pool = NULL;
            job = NULL;

            rc = lsmPoolCreateFromPool(c, SYSTEM_ID, "New pool from pool",
                                        pool_one, 1024*1024*1024, &pool,
                                        &job, LSM_FLAG_RSVD);

            if( LSM_ERR_JOB_STARTED == rc ) {
                pool = wait_for_job_pool(c, &job);
            } else {
                fail_unless(LSM_ERR_OK == rc,
                                    "lsmPoolCreateFromVolumes %d (%s)",
                                    rc, error(lsmErrorLastGet(c)));
            }

            lsmPoolRecordFree(pool);
            pool = NULL;

            free(pool_one);
            pool_one = NULL;
        }
    }
}
END_TEST

Suite * lsm_suite(void)
{
    Suite *s = suite_create("libStorageMgmt");

    TCase *basic = tcase_create("Basic");
    tcase_add_checked_fixture (basic, setup, teardown);


    tcase_add_test(basic, test_pool_delete);
    tcase_add_test(basic, test_pool_create);

    tcase_add_test(basic, test_error_reporting);
    tcase_add_test(basic, test_capability);
    tcase_add_test(basic, test_nfs_export_funcs);
    tcase_add_test(basic, test_disks);
    tcase_add_test(basic, test_plugin_info);
    tcase_add_test(basic, test_get_available_plugins);
    tcase_add_test(basic, test_volume_methods);
    tcase_add_test(basic, test_iscsi_auth_in);
    tcase_add_test(basic, test_initiator_methods);
    tcase_add_test(basic, test_capabilities);
    tcase_add_test(basic, test_smoke_test);
    tcase_add_test(basic, test_access_groups);
    tcase_add_test(basic, test_systems);
    tcase_add_test(basic, test_access_groups_grant_revoke);
    tcase_add_test(basic, test_fs);
    tcase_add_test(basic, test_ss);
    tcase_add_test(basic, test_nfs_exports);
    tcase_add_test(basic, test_invalid_input);

    suite_add_tcase(s, basic);
    return s;
}

int main(int argc, char** argv)
{
    int number_failed;
    Suite *s = lsm_suite();
    SRunner *sr = srunner_create(s);

    /*
     * Don't run python plug-in tests if we are looking for
     * memory leaks.
     */
    if( !getenv("LSM_VALGRIND") ) {
        srunner_run_all(sr, CK_NORMAL);
    }

    /* Switch plug-in backend to test C language compat. */
    which_plugin = 1;

    srunner_run_all(sr, CK_NORMAL);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return(number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
