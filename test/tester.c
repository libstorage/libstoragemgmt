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

#include <stdio.h>
#include <stdlib.h>
#include <check.h>
#include <unistd.h>
#include <time.h>
#include <libstoragemgmt/libstoragemgmt.h>

const char uri[] = "simc://localhost/?statefile=/tmp/lsm_sim_%s";
const char SYSTEM_NAME[] = "LSM simulated storage plug-in";
const char SYSTEM_ID[] = "sim-01";
const char *ISCSI_HOST[2] = {   "iqn.1994-05.com.domain:01.89bd01",
                                "iqn.1994-05.com.domain:01.89bd02" };

#define POLL_SLEEP 300000

lsmConnectPtr c = NULL;

void generateRandom(char *buff, uint32_t len)
{
    uint32_t i = 0;
    static int seed = 0;

    if( !seed ) {
        seed = time(NULL);
        srandom(seed);
    }

    if( buff && (len > 1) ) {
        for(i = 0; i < (len - 1); ++i) {
            buff[i] = 97 + rand()%26;
        }
		buff[len-1] = '\0';
    }
}

char *stateName()
{
#if REUSE_STATE
    return "sim://";
#else
    static char fn[128];
    static char name[32];
    generateRandom(name, sizeof(name));
    snprintf(fn, sizeof(fn),  uri, name);
    return fn;
#endif
}

lsmPoolPtr getTestPool(lsmConnectPtr c)
{
    lsmPoolPtr *pools = NULL;
    uint32_t count = 0;
    lsmPoolPtr test_pool = NULL;

    int rc = lsmPoolList(c, &pools, &count);
    if( LSM_ERR_OK == rc ) {
        uint32_t i = 0;
        for(i = 0; i < count; ++i ) {
            if(strcmp(lsmPoolNameGet(pools[i]), "lsm_test_aggr") == 0 ) {
                test_pool = lsmPoolRecordCopy(pools[i]);
                lsmPoolRecordFreeArray(pools, count);
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
            lsmErrorGetMessage(e),
            lsmErrorGetException(e), lsmErrorGetDebug(e));

        lsmErrorFree(e);
        e = NULL;
    } else {
        printf("No additional error information!\n");
    }
}

void setup(void)
{
    lsmErrorPtr e = NULL;

    int rc = lsmConnectPassword(stateName(), NULL, &c, 30000, &e);
    if( rc ) {
        printf("rc= %d\n", rc);
        dump_error(e);
    }
    fail_unless(LSM_ERR_OK == rc);
}

void teardown(void)
{
    int rc = lsmConnectClose(c);
    fail_unless(LSM_ERR_OK == rc, "lsmConnectClose rc = %d", rc);
    c = NULL;
}

char *error(lsmErrorPtr e)
{
    static char eb[1024];
    memset(eb, 0, sizeof(eb));

    if( e != NULL ) {
        snprintf(eb, sizeof(eb), "Error msg= %s - exception %s - debug %s",
            lsmErrorGetMessage(e),
            lsmErrorGetException(e), lsmErrorGetDebug(e));
        lsmErrorFree(e);
        e = NULL;
    } else {
        snprintf(eb, sizeof(eb), "No addl. error info.");
    }
    return eb;
}

void wait_for_job(lsmConnectPtr c, char **job_id)
{
    lsmJobStatus status;
    uint8_t pc = 0;
    int rc = 0;

    do {
        rc = lsmJobStatusGet(c, *job_id, &status, &pc);
        fail_unless( LSM_ERR_OK == rc, "lsmJobStatusVolumeGet = %d (%s)", rc,  error(lsmErrorGetLast(c)));
        printf("GENERIC: Job %s in progress, %d done, status = %d\n", *job_id, pc, status);
        usleep(POLL_SLEEP);

    } while( status == LSM_JOB_INPROGRESS );

    rc = lsmJobFree(c, job_id);
    fail_unless( LSM_ERR_OK == rc, "lsmJobFree %d, (%s)", rc, error(lsmErrorGetLast(c)));

    fail_unless( LSM_JOB_COMPLETE == status);
    fail_unless( 100 == pc);
    fail_unless( job_id != NULL );
}

lsmVolumePtr wait_for_job_vol(lsmConnectPtr c, char **job_id)
{
    lsmJobStatus status;
    lsmVolumePtr vol = NULL;
    uint8_t pc = 0;
    int rc = 0;

    do {
        rc = lsmJobStatusVolumeGet(c, *job_id, &status, &pc, &vol);
        fail_unless( LSM_ERR_OK == rc, "rc = %d (%s)", rc,  error(lsmErrorGetLast(c)));
        printf("VOLUME: Job %s in progress, %d done, status = %d\n", *job_id, pc, status);
        usleep(POLL_SLEEP);

    } while( rc == LSM_ERR_OK && status == LSM_JOB_INPROGRESS );

    printf("Volume complete: Job %s percent %d done, status = %d, rc=%d\n", *job_id, pc, status, rc);

    rc = lsmJobFree(c, job_id);
    fail_unless( LSM_ERR_OK == rc, "lsmJobFree %d, (%s)", rc, error(lsmErrorGetLast(c)));

    fail_unless( LSM_JOB_COMPLETE == status);
    fail_unless( 100 == pc);

    return vol;
}

lsmFsPtr wait_for_job_fs(lsmConnectPtr c, char **job_id)
{
    lsmJobStatus status;
    lsmFsPtr fs = NULL;
    uint8_t pc = 0;
    int rc = 0;

    do {
        rc = lsmJobStatusFsGet(c, *job_id, &status, &pc, &fs);
        fail_unless( LSM_ERR_OK == rc, "rc = %d (%s)", rc,  error(lsmErrorGetLast(c)));
        printf("FS: Job %s in progress, %d done, status = %d\n", *job_id, pc, status);
        usleep(POLL_SLEEP);

    } while( status == LSM_JOB_INPROGRESS );

    rc = lsmJobFree(c, job_id);
    fail_unless( LSM_ERR_OK == rc, "lsmJobFree %d, (%s)", rc, error(lsmErrorGetLast(c)));

    fail_unless( LSM_JOB_COMPLETE == status);
    fail_unless( 100 == pc);

    return fs;
}

lsmSsPtr wait_for_job_ss(lsmConnectPtr c, char **job_id)
{
    lsmJobStatus status;
    lsmSsPtr ss = NULL;
    uint8_t pc = 0;
    int rc = 0;

    do {
        rc = lsmJobStatusSsGet(c, *job_id, &status, &pc, &ss);
        fail_unless( LSM_ERR_OK == rc, "rc = %d (%s)", rc,  error(lsmErrorGetLast(c)));
        printf("SS: Job %s in progress, %d done, status = %d\n", *job_id, pc, status);
        usleep(POLL_SLEEP);

    } while( status == LSM_JOB_INPROGRESS );

    rc = lsmJobFree(c, job_id);
    fail_unless( LSM_ERR_OK == rc, "lsmJobFree %d, (%s)", rc, error(lsmErrorGetLast(c)));

    fail_unless( LSM_JOB_COMPLETE == status);
    fail_unless( 100 == pc);

    return ss;
}

void create_volumes(lsmConnectPtr c, lsmPoolPtr p, int num)
{
    int i;

    for( i = 0; i < num; ++i ) {
        lsmVolumePtr n = NULL;
        char *job = NULL;
        char name[32];

        memset(name, 0, sizeof(name));
        snprintf(name, sizeof(name), "test %d", i);

        int vc = lsmVolumeCreate(c, p, name, 20000000,
                                    LSM_PROVISION_DEFAULT, &n, &job);

        fail_unless( vc == LSM_ERR_OK || vc == LSM_ERR_JOB_STARTED,
                "lsmVolumeCreate %d (%s)", vc, error(lsmErrorGetLast(c)));

        if( LSM_ERR_JOB_STARTED == vc ) {
            n = wait_for_job_vol(c, &job);
        } else {
            fail_unless(LSM_ERR_OK == vc);
        }

        lsmVolumeRecordFree(n);
        n = NULL;
    }
}

START_TEST(test_smoke_test)
{
    uint32_t i = 0;
    int rc = 0;

    lsmPoolPtr selectedPool = NULL;
    uint32_t poolCount = 0;

    uint32_t set_tmo = 31123;
    uint32_t tmo = 0;

    //Set timeout.
    rc = lsmConnectSetTimeout(c, set_tmo);
    fail_unless(LSM_ERR_OK == rc, "lsmConnectSetTimeout %d (%s)", rc,
                    error(lsmErrorGetLast(c)));


    //Get time-out.
    rc = lsmConnectGetTimeout(c, &tmo);
    fail_unless(LSM_ERR_OK == rc, "Error getting tmo %d (%s)", rc,
                error(lsmErrorGetLast(c)));

    fail_unless( set_tmo == tmo, " %u != %u", set_tmo, tmo );

    lsmPoolPtr *pools = NULL;
    uint32_t count = 0;
    int poolToUse = -1;

    //Get pool list
    rc = lsmPoolList(c, &pools, &poolCount);
    fail_unless(LSM_ERR_OK == rc, "lsmPoolList rc =%d (%s)", rc,
                    error(lsmErrorGetLast(c)));

    //Check pool count
    count = poolCount;
    fail_unless(count == 4, "We are expecting 4 pools from simulator");

    //Dump pools and select a pool to use for testing.
    for (i = 0; i < count; ++i) {
        printf("Id= %s, name=%s, capacity= %"PRIu64 ", remaining= %"PRIu64"\n",
            lsmPoolIdGet(pools[i]),
            lsmPoolNameGet(pools[i]),
            lsmPoolTotalSpaceGet(pools[i]),
            lsmPoolFreeSpaceGet(pools[i]));

        if (lsmPoolFreeSpaceGet(pools[i]) > 20000000) {
            poolToUse = i;
        }
    }

    if (poolToUse != -1) {
        lsmVolumePtr n = NULL;
        char *job = NULL;

        selectedPool = pools[poolToUse];

        int vc = lsmVolumeCreate(c, pools[poolToUse], "test", 20000000,
                                    LSM_PROVISION_DEFAULT, &n, &job);

        fail_unless( vc == LSM_ERR_OK || vc == LSM_ERR_JOB_STARTED,
                    "lsmVolumeCreate %d (%s)", vc, error(lsmErrorGetLast(c)));

        if( LSM_ERR_JOB_STARTED == vc ) {
            n = wait_for_job_vol(c, &job);

            fail_unless( n != NULL);
        }

        uint8_t dependants = 10;
        int child_depends = lsmVolumeChildDependency(c, n, &dependants);
        fail_unless(LSM_ERR_OK == child_depends, "returned = %d", child_depends);
        fail_unless(dependants == 0);

        child_depends = lsmVolumeChildDependencyRm(c, n, &job);
        if( LSM_ERR_JOB_STARTED == child_depends ) {
            wait_for_job(c, &job);
        } else {
            fail_unless(LSM_ERR_OK == child_depends, "rc = %d", child_depends);
            fail_unless(NULL == job);
        }


        lsmBlockRangePtr *range = lsmBlockRangeRecordAllocArray(3);
        fail_unless(NULL != range);


        uint32_t bs = 0;
        int rep_bs = lsmVolumeReplicateRangeBlockSize(c, &bs);
        fail_unless(LSM_ERR_OK == rep_bs);
        fail_unless(512 == bs);


        int rep_i = 0;

        for(rep_i = 0; rep_i < 3; ++rep_i) {
            range[rep_i] = lsmBlockRangeRecordAlloc((rep_i * 1000),
                                                        ((rep_i + 100) * 10000), 10);
        }

        int rep_range =  lsmVolumeReplicateRange(c, LSM_VOLUME_REPLICATE_CLONE,
                                                    n, n, range, 3, &job);

        if( LSM_ERR_JOB_STARTED == rep_range ) {
            wait_for_job(c, &job);
        } else {

            if( LSM_ERR_OK != rep_range ) {
                dump_error(lsmErrorGetLast(c));
            }

            fail_unless(LSM_ERR_OK == rep_range);
        }

        lsmBlockRangeRecordFreeArray(range, 3);

        int online = lsmVolumeOffline(c, n);
        fail_unless( LSM_ERR_OK == online);

        online = lsmVolumeOnline(c, n);
        fail_unless( LSM_ERR_OK == online);

        char *jobDel = NULL;
        int delRc = lsmVolumeDelete(c, n, &jobDel);

        fail_unless( delRc == LSM_ERR_OK || delRc == LSM_ERR_JOB_STARTED,
                    "lsmVolumeDelete %d (%s)", rc, error(lsmErrorGetLast(c)));

        if( LSM_ERR_JOB_STARTED == delRc ) {
            wait_for_job_vol(c, &jobDel);
        }

        lsmVolumeRecordFree(n);
    }

    lsmInitiatorPtr *inits = NULL;
    /* Get a list of initiators */
    rc = lsmInitiatorList(c, &inits, &count);

    fail_unless( LSM_ERR_OK == rc, "lsmInitiatorList %d (%s)", rc,
                                    error(lsmErrorGetLast(c)));

    fail_unless( count == 0, "Count 0 != %d\n", count);


    //Create some volumes for testing.
    create_volumes(c, selectedPool, 3);

    lsmVolumePtr *volumes = NULL;
    /* Get a list of volumes */
    rc = lsmVolumeList(c, &volumes, &count);


    fail_unless( LSM_ERR_OK == rc , "lsmVolumeList %d (%s)",rc,
                                    error(lsmErrorGetLast(c)));

    for (i = 0; i < count; ++i) {
        printf("%s - %s - %s - %"PRIu64" - %"PRIu64" - %x\n",
            lsmVolumeIdGet(volumes[i]),
            lsmVolumeNameGet(volumes[i]),
            lsmVolumeVpd83Get(volumes[i]),
            lsmVolumeBlockSizeGet(volumes[i]),
            lsmVolumeNumberOfBlocks(volumes[i]),
            lsmVolumeOpStatusGet(volumes[i]));
    }


    lsmVolumePtr rep = NULL;
    char *job = NULL;

    //Try a re-size then a snapshot
    lsmVolumePtr resized = NULL;
    char  *resizeJob = NULL;

    int resizeRc = lsmVolumeResize(c, volumes[0],
        ((lsmVolumeNumberOfBlocks(volumes[0]) *
        lsmVolumeBlockSizeGet(volumes[0])) * 2), &resized, &resizeJob);

    fail_unless(resizeRc == LSM_ERR_OK || resizeRc == LSM_ERR_JOB_STARTED,
                    "lsmVolumeResize %d (%s)", resizeRc,
                    error(lsmErrorGetLast(c)));

    if( LSM_ERR_JOB_STARTED == resizeRc ) {
        resized = wait_for_job_vol(c, &resizeJob);
    }

    lsmVolumeRecordFree(resized);

    //Lets create a snapshot of one.
    int repRc = lsmVolumeReplicate(c, selectedPool,
        LSM_VOLUME_REPLICATE_SNAPSHOT,
        volumes[0], "SNAPSHOT1",
        &rep, &job);

    fail_unless(repRc == LSM_ERR_OK || repRc == LSM_ERR_JOB_STARTED,
                    "lsmVolumeReplicate %d (%s)", repRc,
                    error(lsmErrorGetLast(c)));

    if( LSM_ERR_JOB_STARTED == repRc ) {
        rep = wait_for_job_vol(c, &job);
    }

    lsmVolumeRecordFree(rep);

    lsmVolumeRecordFreeArray(volumes, count);

    if (pools) {
        lsmPoolRecordFreeArray(pools, poolCount);
    }
}

END_TEST


START_TEST(test_access_groups)
{
    lsmAccessGroupPtr *groups = NULL;
    lsmAccessGroupPtr group = NULL;
    uint32_t count = 0;
    uint32_t i = 0;

    fail_unless(c!=NULL);

    int rc = lsmAccessGroupList(c, &groups, &count);
    fail_unless(LSM_ERR_OK == rc, "Expected success on listing access groups %d", rc);
    fail_unless(count == 0, "Expect 0 access groups, got %"PRIu32, count);

    lsmAccessGroupRecordFreeArray(groups, count);
    groups = NULL;
    count = 0;


    rc = lsmAccessGroupCreate(c, "test_access_groups", "iqn.1994-05.com.domain:01.89bd01", LSM_INITIATOR_ISCSI, SYSTEM_ID, &group);

    if( LSM_ERR_OK == rc ) {
        lsmStringListPtr init_list = lsmAccessGroupInitiatorIdGet(group);



        printf("%s - %s - %s\n",    lsmAccessGroupIdGet(group),
                                    lsmAccessGroupNameGet(group),
                                    lsmAccessGroupSystemIdGet(group));

        fail_unless(NULL != lsmAccessGroupIdGet(group));
        fail_unless(NULL != lsmAccessGroupNameGet(group));
        fail_unless(NULL != lsmAccessGroupSystemIdGet(group));

        lsmAccessGroupPtr copy = lsmAccessGroupRecordCopy(group);
        if( copy ) {
            fail_unless( strcmp(lsmAccessGroupIdGet(group), lsmAccessGroupIdGet(copy)) == 0);
            fail_unless( strcmp(lsmAccessGroupNameGet(group), lsmAccessGroupNameGet(copy)) == 0) ;
            fail_unless( strcmp(lsmAccessGroupSystemIdGet(group), lsmAccessGroupSystemIdGet(copy)) == 0);

            lsmAccessGroupRecordFree(copy);
        }

        lsmStringListFree(init_list);
        init_list = NULL;
    }

    rc = lsmAccessGroupList(c, &groups, &count);
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( 1 == count );
    lsmAccessGroupRecordFreeArray(groups, count);
    groups = NULL;
    count = 0;

    char *job = NULL;

    rc = lsmAccessGroupAddInitiator(c, group, "iqn.1994-05.com.domain:01.89bd02", LSM_INITIATOR_ISCSI, &job);

    if( LSM_ERR_JOB_STARTED == rc ) {
        wait_for_job(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc, "Expected success on lsmAccessGroupAddInitiator %d", rc);
    }

    rc = lsmAccessGroupList(c, &groups, &count);
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( 1 == count );

    lsmStringListPtr init_list = lsmAccessGroupInitiatorIdGet(groups[0]);
    fail_unless( lsmStringListSize(init_list) == 2, "Expecting 2 initiators, current num = %d\n", lsmStringListSize(init_list) );
    for( i = 0; i < lsmStringListSize(init_list); ++i) {
        printf("%d = %s\n", i, lsmStringListGetElem(init_list, i));
    }
    lsmStringListFree(init_list);


    uint32_t init_list_count = 0;
    lsmInitiatorPtr *inits = NULL;
    rc = lsmInitiatorList(c, &inits, &init_list_count);

    fail_unless(LSM_ERR_OK == rc );
    printf("We have %d initiators\n", init_list_count);

    fail_unless(2 == init_list_count);

    for( i = 0; i < init_list_count; ++i ) {
        printf("Deleting initiator %s from group!\n", lsmInitiatorIdGet(inits[i]));
        rc = lsmAccessGroupDelInitiator(c, groups[0],
                                            lsmInitiatorIdGet(inits[i]), &job);

        if( LSM_ERR_JOB_STARTED == rc ) {
            wait_for_job_fs(c, &job);
        } else {
            fail_unless(LSM_ERR_OK == rc);
        }
    }

    lsmAccessGroupRecordFreeArray(groups, count);
    groups = NULL;
    count = 0;

    rc = lsmAccessGroupList(c, &groups, &count);
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( 1 == count );

    init_list = lsmAccessGroupInitiatorIdGet(groups[0]);
    fail_unless( init_list != NULL);
    fail_unless( lsmStringListSize(init_list) == 0);

    lsmAccessGroupRecordFreeArray(groups, count);
    groups = NULL;
    count = 0;

}

END_TEST

START_TEST(test_access_groups_grant_revoke)
{
    fail_unless(c!=NULL);
    lsmAccessGroupPtr group = NULL;
    int rc = 0;
    lsmPoolPtr pool = getTestPool(c);
    char *job = NULL;
    lsmVolumePtr n = NULL;

    fail_unless(pool != NULL);

    rc = lsmAccessGroupCreate(c, "test_access_groups_grant_revoke",
                                   ISCSI_HOST[0], LSM_INITIATOR_ISCSI, SYSTEM_ID,
                                    &group);

    fail_unless( LSM_ERR_OK == rc );

    int vc = lsmVolumeCreate(c, pool, "volume_grant_test", 20000000,
                                    LSM_PROVISION_DEFAULT, &n, &job);

    fail_unless( vc == LSM_ERR_OK || vc == LSM_ERR_JOB_STARTED,
            "lsmVolumeCreate %d (%s)", vc, error(lsmErrorGetLast(c)));

    if( LSM_ERR_JOB_STARTED == vc ) {
        n = wait_for_job_vol(c, &job);
    }

    fail_unless(n != NULL);

    rc = lsmAccessGroupGrant(c, group, n, LSM_VOLUME_ACCESS_READ_WRITE, &job);
    if( LSM_ERR_JOB_STARTED == rc ) {
        wait_for_job(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc);
    }

    lsmVolumePtr *volumes = NULL;
    uint32_t v_count = 0;
    rc = lsmVolumesAccessibleByAccessGroup(c, group, &volumes, &v_count);
    fail_unless(LSM_ERR_OK == rc);
    fail_unless(v_count == 1);

    fail_unless(strcmp(lsmVolumeIdGet(volumes[0]), lsmVolumeIdGet(n)) == 0);
    lsmVolumeRecordFreeArray(volumes, v_count);

    lsmAccessGroupPtr *groups;
    uint32_t g_count = 0;
    rc = lsmAccessGroupsGrantedToVolume(c, n, &groups, &g_count);
    fail_unless(LSM_ERR_OK == rc);
    fail_unless(g_count == 1);

    fail_unless(strcmp(lsmAccessGroupIdGet(groups[0]), lsmAccessGroupIdGet(group)) == 0);
    lsmAccessGroupRecordFreeArray(groups, g_count);

    rc = lsmAccessGroupRevoke(c, group, n, &job);
    if( LSM_ERR_JOB_STARTED == rc ) {
        wait_for_job(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc);
    }

    rc = lsmAccessGroupDel(c, group, &job);
    fail_unless(LSM_ERR_OK == rc);
    lsmAccessGroupRecordFree(group);

    lsmVolumeRecordFree(n);
    lsmPoolRecordFree(pool);
}
END_TEST

START_TEST(test_fs)
{
    fail_unless(c!=NULL);

    lsmFsPtr *fs_list = NULL;
    int rc = 0;
    uint32_t fs_count = 0;
    lsmFsPtr nfs = NULL;
    lsmFsPtr resized_fs = NULL;
    char *job = NULL;

    lsmPoolPtr test_pool = getTestPool(c);

    rc = lsmFsList(c, &fs_list, &fs_count);

    fail_unless(LSM_ERR_OK == rc);
    fail_unless(0 == fs_count);

    rc = lsmFsCreate(c, test_pool, "C_unit_test", 50000000, &nfs, &job);

    if( LSM_ERR_JOB_STARTED == rc ) {
        fail_unless(NULL == nfs);

        nfs = wait_for_job_fs(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc);
    }

    fail_unless(NULL != nfs);

    lsmFsPtr cloned_fs = NULL;
    rc = lsmFsClone(c, nfs, "cloned_fs", NULL, &cloned_fs, &job);
    if( LSM_ERR_JOB_STARTED == rc ) {
        fail_unless(NULL == cloned_fs);
        cloned_fs = wait_for_job_fs(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc, "rc= %d", rc);
    }

    rc = lsmFsFileClone(c, nfs, "src/file.txt", "dest/file.txt", NULL, &job);
    if( LSM_ERR_JOB_STARTED == rc ) {
        wait_for_job(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc);
    }


    rc = lsmFsList(c, &fs_list, &fs_count);

    fail_unless(LSM_ERR_OK == rc);
    fail_unless(2 == fs_count, "fs_count = %d", fs_count);
    lsmFsRecordFreeArray(fs_list, fs_count);

    rc = lsmFsResize(c,nfs, 100000000, &resized_fs, &job);

    if( LSM_ERR_JOB_STARTED == rc ) {
        fail_unless(NULL == resized_fs);
        resized_fs = wait_for_job_fs(c, &job);
    }

    uint8_t yes_no = 10;
    rc = lsmFsChildDependency(c, nfs, NULL, &yes_no);
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( yes_no == 0);

    rc = lsmFsChildDependencyRm(c, nfs, NULL, &job);
    if( LSM_ERR_JOB_STARTED == rc ) {
        fail_unless(NULL != job);
        wait_for_job(c, &job);
    } else {
        fail_unless( LSM_ERR_OK == rc);
    }

    rc = lsmFsDelete(c, resized_fs, &job);

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
    lsmSsPtr *ss_list = NULL;
    uint32_t ss_count = 0;
    char *job = NULL;
    lsmFsPtr fs = NULL;
    lsmSsPtr ss = NULL;

    printf("Testing snapshots\n");

    lsmPoolPtr test_pool = getTestPool(c);

    int rc = lsmFsCreate(c, test_pool, "test_fs", 100000000, &fs, &job);

    if( LSM_ERR_JOB_STARTED == rc ) {
        fs = wait_for_job_fs(c, &job);
    }

    fail_unless(fs != NULL);


    rc = lsmFsSsList(c, fs, &ss_list, &ss_count);
    printf("List return code= %d\n", rc);

    if(rc) {
        printf("%s\n", error(lsmErrorGetLast(c)));
    }
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( NULL == ss_list);
    fail_unless( 0 == ss_count );


    rc = lsmFsSsCreate(c, fs, "test_snap", NULL, &ss, &job);
    if( LSM_ERR_JOB_STARTED == rc ) {
        printf("Waiting for snap to create!\n");
        ss = wait_for_job_ss(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc);
    }

    fail_unless( NULL != ss);

    rc = lsmFsSsList(c, fs, &ss_list, &ss_count);
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( NULL != ss_list);
    fail_unless( 1 == ss_count );

    lsmStringListPtr files = lsmStringListAlloc(1);
    if(files) {
        rc = lsmStringListSetElem(files, 0, "some/file/name.txt");
        fail_unless( LSM_ERR_OK == rc, "lsmStringListSetElem rc = %d", rc);
    }

    rc = lsmFsSsRevert(c, fs, ss, files, files, 0, &job);
    if( LSM_ERR_JOB_STARTED == rc ) {
        printf("Waiting for  lsmSsRevert!\n");
        wait_for_job(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc);
    }

    lsmStringListFree(files);

    rc = lsmFsSsDelete(c, fs, ss, &job);

    if( LSM_ERR_JOB_STARTED == rc ) {
        wait_for_job(c, &job);
    }

    lsmSsRecordFreeArray(ss_list, ss_count);
    lsmFsRecordFree(fs);

    printf("Testing snapshots done!\n");
}
END_TEST

START_TEST(test_systems)
{
    uint32_t count = 0;
    lsmSystemPtr *sys=NULL;
    const char *id = NULL;
    const char *name = NULL;

    fail_unless(c!=NULL);

    int rc = lsmSystemList(c, &sys, &count);

    fail_unless(LSM_ERR_OK == rc);
    fail_unless(count == 1);

    id = lsmSystemIdGet(sys[0]);
    fail_unless(id != NULL);
    fail_unless(strcmp(id, SYSTEM_ID) == 0, "%s", id);

    name = lsmSystemNameGet(sys[0]);
    fail_unless(name != NULL);
    fail_unless(strcmp(name, SYSTEM_NAME) == 0);

    lsmSystemRecordFreeArray(sys, count);
}
END_TEST

START_TEST(test_nfs_exports)
{
    fail_unless(c != NULL);
    int rc = 0;

    lsmPoolPtr test_pool = getTestPool(c);
    lsmFsPtr nfs = NULL;
    char *job = NULL;

    fail_unless(NULL != test_pool);

    rc = lsmFsCreate(c, test_pool, "C_unit_test_nfs_export", 50000000, &nfs, &job);

    if( LSM_ERR_JOB_STARTED == rc ) {
        nfs = wait_for_job_fs(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc, "RC = %d", rc);
    }

    fail_unless(nfs != NULL);
    lsmNfsExportPtr *exports = NULL;
    uint32_t count = 0;

    rc = lsmNfsList(c, &exports, &count);

    fail_unless(LSM_ERR_OK == rc, "lsmNfsList rc= %d\n", rc);
    fail_unless(count == 0);
    fail_unless(NULL == exports);


    lsmStringListPtr access = lsmStringListAlloc(1);
    fail_unless(NULL != access);

    lsmStringListSetElem(access, 0, "192.168.2.29");

    lsmNfsExportPtr e = NULL;

    rc = lsmNfsExportFs(c, lsmFsIdGet(nfs), "/tony", access, access, NULL,
                            ANON_UID_GID_NA, ANON_UID_GID_NA, NULL, NULL, &e);
    fail_unless(LSM_ERR_OK == rc, "lsmNfsExportFs %d\n", rc);

    lsmNfsExportRecordFree(e);
    e=NULL;

    rc = lsmNfsList(c, &exports, &count);
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( exports != NULL);
    fail_unless( count == 1 );

    rc  = lsmNfsExportRemove(c, exports[0]);
    fail_unless( LSM_ERR_OK == rc );
    lsmNfsExportRecordFreeArray(exports, count);

    exports = NULL;

    rc = lsmNfsList(c, &exports, &count);

    fail_unless(LSM_ERR_OK == rc, "lsmNfsList rc= %d\n", rc);
    fail_unless(count == 0);
    fail_unless(NULL == exports);
}
END_TEST

struct bad_record
{
    uint32_t m;
};


START_TEST(test_invalid_input)
{
    fail_unless(c != NULL);
    int rc = 0;

    struct bad_record bad;
    bad.m = 0xA0A0A0A0;

    printf("Testing arguments\n");

    lsmPoolPtr test_pool = getTestPool(c);

    lsmConnectPtr test_connect = NULL;
    lsmErrorPtr test_error = NULL;

    rc = lsmConnectPassword(NULL, NULL, NULL, 20000, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc %d", rc);

    rc = lsmConnectPassword("INVALID_URI:\\yep", NULL, &test_connect, 20000,
                                &test_error);
    fail_unless(rc == LSM_ERR_INVALID_URI, "rc %d", rc);


    rc = lsmConnectClose((lsmConnectPtr)&bad);
    fail_unless(LSM_ERR_INVALID_CONN == rc, "rc %d", rc);

    rc = lsmConnectClose((lsmConnectPtr)NULL);
    fail_unless(LSM_ERR_INVALID_CONN == rc, "rc %d", rc);



    rc = lsmJobStatusGet(c, NULL, NULL, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    char *job = NULL;
    rc = lsmJobStatusGet(c, job, NULL, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    lsmJobStatus status;


    rc = lsmJobStatusGet(c, job, &status, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    uint8_t percent_complete;
    rc = lsmJobStatusGet(c, "NO_SUCH_JOB", &status, &percent_complete);
    fail_unless(LSM_ERR_NOT_FOUND_JOB == rc, "rc %d", rc);

    /* lsmJobStatusVolumeGet */
    lsmVolumePtr vol = NULL;
    rc = lsmJobStatusVolumeGet(c, NULL, NULL, NULL, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusVolumeGet(c, NULL, NULL, NULL, &vol);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusVolumeGet(c, job, NULL, NULL, &vol);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusVolumeGet(c, job, &status, NULL, &vol);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusVolumeGet(c, "NO_SUCH_JOB", &status, &percent_complete, &vol);
    fail_unless(LSM_ERR_NOT_FOUND_JOB == rc, "rc %d", rc);

    /* lsmJobStatusFsGet */
    lsmFsPtr fs = NULL;

    rc = lsmJobStatusFsGet(c, NULL, NULL, NULL, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusFsGet(c, NULL, NULL, NULL, &fs);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusFsGet(c, job, NULL, NULL, &fs);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusFsGet(c, job, &status, NULL, &fs);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusFsGet(c, "NO_SUCH_JOB", &status, &percent_complete, &fs);
    fail_unless(LSM_ERR_NOT_FOUND_JOB == rc, "rc %d", rc);

    /* lsmJobStatusFsGet */
    lsmSsPtr ss = (lsmSsPtr)&bad;

    rc = lsmJobStatusSsGet(c, NULL, NULL, NULL, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusSsGet(c, NULL, NULL, NULL, &ss);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    ss = NULL;

    rc = lsmJobStatusSsGet(c, job, NULL, NULL, &ss);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusSsGet(c, job, &status, NULL, &ss);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobStatusSsGet(c, "NO_SUCH_JOB", &status, &percent_complete, &ss);
    fail_unless(LSM_ERR_NOT_FOUND_JOB == rc, "rc %d", rc);


    /* lsmJobFree */
    char *bogus_job = strdup("NO_SUCH_JOB");
    rc = lsmJobFree(c, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmJobFree(c, &bogus_job);
    fail_unless(LSM_ERR_NOT_FOUND_JOB == rc, "rc %d", rc);

    fail_unless(bogus_job != NULL, "Expected bogus job to != NULL!");
    free(bogus_job);


    /* lsmPoolList */
    rc = lsmPoolList(c, NULL, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    lsmPoolPtr *pools = NULL;
    rc = lsmPoolList(c, &pools, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    uint32_t count = 0;
    rc = lsmPoolList(c, NULL, &count);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    pools = (lsmPoolPtr*)&bad;
    rc = lsmPoolList(c, &pools, &count);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    /* lsmInitiatorList */
     rc = lsmInitiatorList(c, NULL, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    lsmInitiatorPtr *inits = NULL;
    rc = lsmInitiatorList(c, &inits, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmInitiatorList(c, NULL, &count);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    inits = (lsmInitiatorPtr*)&bad;
    rc = lsmInitiatorList(c, &inits, &count);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    /* lsmVolumeList */
     rc = lsmVolumeList(c, NULL, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    lsmVolumePtr *vols = NULL;
    rc = lsmVolumeList(c, &vols, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmVolumeList(c, NULL, &count);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    vols = (lsmVolumePtr*)&bad;
    rc = lsmVolumeList(c, &vols, &count);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    /* lsmVolumeCreate */
    lsmVolumePtr new_vol = NULL;
    job = NULL;

    rc = lsmVolumeCreate(c, NULL, NULL, 0, 0, NULL, NULL);
    fail_unless(LSM_ERR_INVALID_POOL == rc, "rc %d", rc);

    rc = lsmVolumeCreate(c, (lsmPoolPtr)&bad, "BAD_POOL", 10000000,
                            LSM_PROVISION_DEFAULT, &new_vol, &job);
    fail_unless(LSM_ERR_INVALID_POOL == rc, "rc %d", rc);

    rc = lsmVolumeCreate(c, test_pool, "", 10000000, LSM_PROVISION_DEFAULT,
                            &new_vol, &job);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmVolumeCreate(c, test_pool, "ARG_TESTING", 10000000, LSM_PROVISION_DEFAULT,
                            NULL, &job);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmVolumeCreate(c, test_pool, "ARG_TESTING", 10000000, LSM_PROVISION_DEFAULT,
                            &new_vol, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    job = "NOT_NULL";
    rc = lsmVolumeCreate(c, test_pool, "ARG_TESTING", 10000000, LSM_PROVISION_DEFAULT,
                            &new_vol, &job);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    job = NULL;
    rc = lsmVolumeCreate(c, test_pool, "ARG_TESTING", 10000000, LSM_PROVISION_DEFAULT,
                            &new_vol, &job);

    if( LSM_ERR_JOB_STARTED == rc ) {
        new_vol = wait_for_job_vol(c, &job);
    } else {
        fail_unless(LSM_ERR_OK != rc, "rc %d", rc);
    }

    /* lsmVolumeResize */
    rc = lsmVolumeResize(c, NULL, 0, NULL, NULL);
    fail_unless(LSM_ERR_INVALID_VOL == rc, "rc %d", rc);


    lsmVolumePtr resized = (lsmVolumePtr)&bad;
    rc = lsmVolumeResize(c, new_vol, 20000000, &resized, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    resized = NULL;
    rc = lsmVolumeResize(c, new_vol, 20000000, &resized, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmVolumeResize(c, new_vol,    lsmVolumeNumberOfBlocks(new_vol) *
                                        lsmVolumeBlockSizeGet(new_vol),
                                        &resized, &job);
    fail_unless(LSM_ERR_VOLUME_SAME_SIZE == rc, "rc = %d", rc);

    rc = lsmVolumeResize(c, new_vol, 20000000, &resized, &job);

    if( LSM_ERR_JOB_STARTED == rc ) {
        resized = wait_for_job_vol(c, &job);
    } else {
        fail_unless(LSM_ERR_OK != rc, "rc %d", rc);
    }

    /* lsmVolumeDelete */
    rc = lsmVolumeDelete(c, NULL, NULL);
    fail_unless(LSM_ERR_INVALID_VOL == rc, "rc %d", rc);

    rc = lsmVolumeDelete(c, resized, NULL);
    fail_unless(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsmVolumeDelete(c, resized, &job);
    if( LSM_ERR_JOB_STARTED == rc ) {
        wait_for_job(c, &job);
    } else {
        fail_unless(LSM_ERR_OK != rc, "rc %d", rc);
    }

    /* lsmStorageCapabilitiesPtr */
    lsmStorageCapabilitiesPtr cap = NULL;
    rc = lsmCapabilities(c, &cap);
    fail_unless(LSM_ERR_NO_SUPPORT == rc);


    /* lsmVolumeReplicate */
    lsmVolumePtr cloned = NULL;
    rc = lsmVolumeReplicate(c, NULL, 0, NULL, NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_POOL, "rc = %d", rc);

    rc = lsmVolumeReplicate(c, test_pool, LSM_VOLUME_REPLICATE_CLONE, NULL,
                            "cloned", &cloned, &job);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);

    rc = lsmVolumeReplicate(c, test_pool, LSM_VOLUME_REPLICATE_CLONE, new_vol,
                            "", &cloned, &job);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmVolumeReplicate(c, test_pool, LSM_VOLUME_REPLICATE_CLONE, new_vol,
                            "cloned", NULL, &job);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmVolumeReplicate(c, test_pool, LSM_VOLUME_REPLICATE_CLONE, new_vol,
                            "cloned", &cloned, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    /* lsmVolumeReplicateRangeBlockSize */
    rc = lsmVolumeReplicateRangeBlockSize(c, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmVolumeReplicateRange */
    rc = lsmVolumeReplicateRange(c, LSM_VOLUME_REPLICATE_CLONE, NULL, NULL,
                                    NULL, 0, NULL);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);

    rc = lsmVolumeReplicateRange(c, LSM_VOLUME_REPLICATE_CLONE, new_vol,
                                    NULL, NULL, 0, NULL);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);

    rc = lsmVolumeReplicateRange(c, LSM_VOLUME_REPLICATE_CLONE, new_vol, new_vol,
                                    NULL, 1, &job);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    rc = lsmVolumeStatus(c, NULL, NULL);
    fail_unless(rc == LSM_ERR_NO_SUPPORT, "rc = %d", rc);


    rc = lsmVolumeOnline(c, NULL);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);

    rc = lsmVolumeOffline(c, NULL);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);


    /* lsmAccessGroupCreate */
    lsmAccessGroupPtr ag = NULL;

    rc = lsmAccessGroupCreate(c, NULL, NULL, 0, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmAccessGroupCreate(c, "my_group", ISCSI_HOST[0], LSM_INITIATOR_OTHER,
                                "system-id", NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmAccessGroupCreate(c, "my_group", ISCSI_HOST[0], LSM_INITIATOR_OTHER,
                                SYSTEM_ID, &ag);
    fail_unless(rc == LSM_ERR_OK, "rc = %d", rc);
    fail_unless(ag != NULL);


    /* lsmAccessGroupDel */
    rc = lsmAccessGroupDel(c, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ACCESS_GROUP, "rc = %d", rc);

    rc = lsmAccessGroupDel(c, ag, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    /* lsmAccessGroupAddInitiator */
    rc = lsmAccessGroupAddInitiator(c, NULL, NULL, 0, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ACCESS_GROUP, "rc = %d", rc);


    rc = lsmAccessGroupAddInitiator(c, ag, ISCSI_HOST[0], 0, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmAccessGroupDelInitiator(c, NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ACCESS_GROUP, "rc = %d", rc);

    rc = lsmAccessGroupDelInitiator(c, ag, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);



    rc = lsmAccessGroupGrant(c, NULL, NULL, 0, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ACCESS_GROUP, "rc = %d", rc);

    rc = lsmAccessGroupGrant(c, ag, NULL, 0, NULL);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);


    rc = lsmAccessGroupGrant(c, ag, new_vol, 0, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    rc = lsmAccessGroupRevoke(c, NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ACCESS_GROUP, "rc = %d", rc);

    rc = lsmAccessGroupRevoke(c, ag, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);

    rc = lsmAccessGroupRevoke(c, ag, new_vol, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    /* lsmVolumesAccessibleByAccessGroup */
    rc = lsmVolumesAccessibleByAccessGroup(c, NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ACCESS_GROUP, "rc = %d", rc);

    rc = lsmVolumesAccessibleByAccessGroup(c, ag, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmAccessGroupsGrantedToVolume */
    rc = lsmAccessGroupsGrantedToVolume(c, NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);

    rc = lsmAccessGroupsGrantedToVolume(c, new_vol, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmVolumeChildDependency */
    rc = lsmVolumeChildDependency(c, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);

    rc = lsmVolumeChildDependency(c, new_vol, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /*lsmVolumeChildDependencyRm*/
    rc = lsmVolumeChildDependencyRm(c, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_VOL, "rc = %d", rc);

    rc = lsmVolumeChildDependencyRm(c, new_vol, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmSystemList */
    lsmSystemPtr *systems = NULL;
    rc = lsmSystemList(c, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    rc = lsmSystemList(c, &systems, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmFsList */
    rc = lsmFsList(c, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    lsmFsPtr *fsl = NULL;
    rc = lsmFsList(c, &fsl, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    /*lsmFsCreate*/
    rc = lsmFsCreate(c, NULL, NULL, 0, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_POOL, "rc = %d", rc);

    rc = lsmFsCreate(c, test_pool, NULL, 0, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    lsmFsPtr arg_fs = NULL;
    rc = lsmFsCreate(c, test_pool, "argument_fs", 10000000, &arg_fs, &job);

    if( LSM_ERR_JOB_STARTED == rc ) {
        arg_fs = wait_for_job_fs(c, &job);
    } else {
        fail_unless(LSM_ERR_OK == rc, "rc = %d", rc);
    }

    /* lsmFsDelete */
    rc = lsmFsDelete(c, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    rc = lsmFsDelete(c, arg_fs, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmFsResize */
    rc = lsmFsResize(c, NULL, 0, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    rc = lsmFsResize(c, arg_fs, 0, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmFsClone */
    rc = lsmFsClone(c, NULL, NULL, NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    rc = lsmFsClone(c, arg_fs, NULL, NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    /*lsmFsFileClone*/
    rc = lsmFsFileClone(c, NULL, NULL, NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    rc = lsmFsFileClone(c, arg_fs, NULL, NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    rc = lsmFsChildDependency(c, NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    lsmStringListPtr badf = (lsmStringListPtr)&bad;
    rc = lsmFsChildDependency(c, arg_fs, badf, NULL);
    fail_unless(rc == LSM_ERR_INVALID_SL, "rc = %d", rc);

    lsmStringListPtr f = lsmStringListAlloc(1);
    rc = lsmFsChildDependency(c, arg_fs, f, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /*lsmFsChildDependencyRm*/
    rc = lsmFsChildDependencyRm(c, NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    rc = lsmFsChildDependencyRm(c, arg_fs, badf, NULL);
    fail_unless(rc == LSM_ERR_INVALID_SL, "rc = %d", rc);

    rc = lsmFsChildDependencyRm(c, arg_fs, f, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);



    rc = lsmFsSsList(c, NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);


    rc = lsmFsSsList(c, arg_fs, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    rc = lsmFsSsCreate(c, NULL, NULL, NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    rc = lsmFsSsCreate(c, arg_fs, NULL, NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    lsmSsPtr arg_ss = NULL;
    rc = lsmFsSsCreate(c, arg_fs, "arg_snapshot", badf, &arg_ss, &job);
    fail_unless(rc == LSM_ERR_INVALID_SL, "rc = %d", rc);

    rc = lsmFsSsCreate(c, arg_fs, "arg_snapshot", NULL, &arg_ss, &job);

    if( LSM_ERR_JOB_STARTED == rc ) {
        arg_ss = wait_for_job_ss(c, &job);
    } else {
        fail_unless(rc == LSM_ERR_OK, "rc = %d", rc);
    }

    rc = lsmFsSsDelete(c, NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    rc = lsmFsSsDelete(c, arg_fs, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_SS, "rc = %d", rc);

    rc = lsmFsSsDelete(c, arg_fs, arg_ss, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    rc = lsmFsSsRevert(c, NULL, NULL, NULL, NULL, 0, NULL);
    fail_unless(rc == LSM_ERR_INVALID_FS, "rc = %d", rc);

    rc = lsmFsSsRevert(c, arg_fs, NULL, NULL, NULL, 0, NULL);
    fail_unless(rc == LSM_ERR_INVALID_SS, "rc = %d", rc);

    rc = lsmFsSsRevert(c, arg_fs, arg_ss, badf, NULL, 0, NULL);
    fail_unless(rc == LSM_ERR_INVALID_SL, "rc = %d", rc);

    rc = lsmFsSsRevert(c, arg_fs, arg_ss, badf, badf, 0, NULL);
    fail_unless(rc == LSM_ERR_INVALID_SL, "rc = %d", rc);

    rc = lsmFsSsRevert(c, arg_fs, arg_ss, f, f, 0, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmNfsList(c, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);


    rc = lsmNfsExportFs(c, NULL, NULL, NULL, NULL, NULL, 0,0,NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmNfsExportFs(c, NULL, NULL, badf, NULL, NULL, 0,0,NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_SL, "rc = %d", rc);

    rc = lsmNfsExportFs(c, NULL, NULL, f, badf, NULL, 0,0,NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_SL, "rc = %d", rc);

    rc = lsmNfsExportFs(c, NULL, NULL, f, f, badf, 0,0,NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_SL, "rc = %d", rc);


    rc = lsmNfsExportFs(c, NULL, NULL, f, f, f, 0,0, NULL, NULL, NULL);
    fail_unless(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsmNfsExportRemove(c, NULL);
    fail_unless(rc == LSM_ERR_INVALID_NFS, "rc = %d", rc);
}
END_TEST

Suite * lsm_suite(void)
{
    Suite *s = suite_create("libStorageMgmt");

    TCase *basic = tcase_create("Basic");
    tcase_add_checked_fixture (basic, setup, teardown);

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
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return(number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
