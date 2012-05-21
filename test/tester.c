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
#include <libstoragemgmt/libstoragemgmt.h>

#include <inttypes.h>

const char uri[] = "sim://localhost/?statefile=/tmp/lsm_sim_%s";
const char SYSTEM_NAME[] = "LSM simulated storage plug-in";
const char SYSTEM_ID[] = "sim-01";
const char *ISCSI_HOST[2] = { "iqn.1994-05.com.domain:01.89bd01", "iqn.1994-05.com.domain:01.89bd02" };

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
    static char fn[128];
    static char name[32];
    generateRandom(name, sizeof(name));
    snprintf(fn, sizeof(fn),  uri, name);
    return fn;
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
    lsmErrorPtr e;

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

lsmVolumePtr wait_for_job(lsmConnectPtr c, char **job_id)
{
    lsmJobStatus status;
    lsmVolumePtr vol = NULL;
    uint8_t pc = 0;
    int rc = 0;

    do {
        rc = lsmJobStatusGet(c, *job_id, &status, &pc, &vol);
        fail_unless( LSM_ERR_OK == rc, "rc = %d (%s)", rc,  error(lsmErrorGetLast(c)));
        printf("Job %s in progress, %d done, status = %d\n", *job_id, pc, status);
        sleep(1);

    } while( status == LSM_JOB_INPROGRESS );

    rc = lsmJobFree(c, job_id);
    fail_unless( LSM_ERR_OK == rc, "lsmJobFree %d, (%s)", rc, error(lsmErrorGetLast(c)));

    fail_unless( LSM_JOB_COMPLETE == status);
    fail_unless( 100 == pc);

    return vol;
}

void mapping(lsmConnectPtr c)
{

    //Get initiators
    lsmInitiatorPtr *init_list = NULL;
    uint32_t init_count = 0;

    int rc = lsmInitiatorList(c, &init_list, &init_count);

    fail_unless(LSM_ERR_OK == rc, "lsmInitiatorList", rc,
                    error(lsmErrorGetLast(c)));

    lsmVolumePtr *vol_list = NULL;
    uint32_t vol_count = 0;
    rc = lsmVolumeList(c, &vol_list, &vol_count);

    if (LSM_ERR_OK == rc) {
        uint32_t i = 0;
        uint32_t j = 0;

        //Map
        for (i = 0; i < init_count; i++) {
            for (j = 0; j < vol_count; j++) {
                char *job = NULL;

                rc = lsmAccessGrant(c, init_list[i], vol_list[j], LSM_VOLUME_ACCESS_READ_WRITE, &job);

                fail_unless( LSM_ERR_OK == rc, "lsmAccessGrant %d (%s)",
                                rc, error(lsmErrorGetLast(c)));
            }
        }

        //Unmap
        for (i = 0; i < init_count; i++) {
            for (j = 0; j < vol_count; j++) {

                rc = lsmAccessRevoke(c, init_list[i], vol_list[j]);
                fail_unless( LSM_ERR_OK == rc, "lsmAccessRevoke %d (%s)",
                                rc, error(lsmErrorGetLast(c)));
            }
        }

        lsmVolumeRecordFreeArray(vol_list, vol_count);
    }

    lsmInitiatorRecordFreeArray(init_list, init_count);
}

void create_volumes(lsmConnectPtr c, lsmPoolPtr p, int num)
{
    int i;

    for( i = 0; i < num; ++i ) {
        lsmVolumePtr n;
        char *job = NULL;
        char name[32];

        memset(name, 0, sizeof(name));
        snprintf(name, sizeof(name), "test %d", i);

        int vc = lsmVolumeCreate(c, p, name, 20000000,
                                    LSM_PROVISION_DEFAULT, &n, &job);

        fail_unless( vc == LSM_ERR_OK || vc == LSM_ERR_JOB_STARTED,
                "lsmVolumeCreate %d (%s)", vc, error(lsmErrorGetLast(c)));

        if( LSM_ERR_JOB_STARTED == vc ) {
            n = wait_for_job(c, &job);
        }

        lsmVolumeRecordFree(n);
    }
}

START_TEST(test_smoke_test)
{
    uint32_t i = 0;
    lsmErrorPtr e;
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
    fail_unless(count == 3, "We are expecting 2 pools from simulator");

    //Dump pools and select a pool to use for testing.
    for (i = 0; i < count; ++i) {
        printf("Id= %s, name=%s, capacity= %lu, remaining= %lu\n",
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
            n = wait_for_job(c, &job);
        }

        char *jobDel = NULL;
        int delRc = lsmVolumeDelete(c, n, &jobDel);

        fail_unless( delRc == LSM_ERR_OK || delRc == LSM_ERR_JOB_STARTED,
                    "lsmVolumeDelete %d (%s)", rc, error(lsmErrorGetLast(c)));

        if( LSM_ERR_JOB_STARTED == delRc ) {
            wait_for_job(c, &jobDel);
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
        printf("%s - %s - %s - %lu - %lu - %x\n",
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
        resized = wait_for_job(c, &resizeJob);
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
        resized = wait_for_job(c, &job);
    }

    lsmVolumeRecordFree(rep);

    lsmVolumeRecordFreeArray(volumes, count);

    if (pools) {
        lsmPoolRecordFreeArray(pools, poolCount);
    }

    mapping(c);
}

END_TEST


START_TEST(test_access_groups)
{
    lsmErrorPtr e = NULL;
    lsmAccessGroupPtr *groups = NULL;
    lsmAccessGroupPtr group = NULL;
    uint32_t count = 0;
    uint32_t i = 0;

    fail_unless(c!=NULL);

    int rc = lsmAccessGroupList(c, &groups, &count);
    fail_unless(LSM_ERR_OK == rc, "Expected success on listing access groups %d", rc);
    fail_unless(count == 0, "Expect 0 access groups, got %"PRIu32, count);

    rc = lsmAccessGroupCreate(c, "access_group_test", "iqn.1994-05.com.domain:01.89bd01", LSM_INITIATOR_ISCSI, SYSTEM_ID, &group);

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
    }

    lsmAccessGroupRecordFreeArray(groups, count);

    rc = lsmAccessGroupList(c, &groups, &count);
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( 1 == count );

    lsmAccessGroupRecordFreeArray(groups, count);

    char *job = NULL;

    rc = lsmAccessGroupAddInitiator(c, group, "iqn.1994-05.com.domain:01.89bd02", LSM_INITIATOR_ISCSI, &job);

    fail_unless(LSM_ERR_OK == rc, "Expected success on lsmAccessGroupAddInitiator %d", rc);

    rc = lsmAccessGroupList(c, &groups, &count);
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( 1 == count );

    lsmStringListPtr init_list = lsmAccessGroupInitiatorIdGet(groups[0]);
    fail_unless( lsmStringListSize(init_list) == 2 );
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
        rc = lsmAccessGroupDelInitiator(c, groups[0], inits[i], &job);
        fail_unless(LSM_ERR_OK == rc);
    }

    lsmAccessGroupRecordFreeArray(groups, count);

    rc = lsmAccessGroupList(c, &groups, &count);
    fail_unless( LSM_ERR_OK == rc);
    fail_unless( 1 == count );

    init_list = lsmAccessGroupInitiatorIdGet(groups[0]);
    fail_unless( init_list == NULL);

    lsmAccessGroupRecordFreeArray(groups, count);

}

END_TEST

START_TEST(test_access_groups_grant_revoke)
{
    fail_unless(c!=NULL);
    lsmAccessGroupPtr group;
    int rc = 0;
    lsmPoolPtr pool = getTestPool(c);
    char *job = NULL;
    lsmVolumePtr n = NULL;

    fail_unless(pool != NULL);

    rc = lsmAccessGroupCreate(c, "access_group_grant_test",
                                   ISCSI_HOST[0], LSM_INITIATOR_ISCSI, SYSTEM_ID,
                                    &group);

    fail_unless( LSM_ERR_OK == rc );

    int vc = lsmVolumeCreate(c, pool, "volume_grant_test", 20000000,
                                    LSM_PROVISION_DEFAULT, &n, &job);

    fail_unless( vc == LSM_ERR_OK || vc == LSM_ERR_JOB_STARTED,
            "lsmVolumeCreate %d (%s)", vc, error(lsmErrorGetLast(c)));

    if( LSM_ERR_JOB_STARTED == vc ) {
        n = wait_for_job(c, &job);
    }

    fail_unless(n != NULL);

    rc = lsmAccessGroupGrant(c, group, n, LSM_VOLUME_ACCESS_READ_WRITE, &job);
    fail_unless(LSM_ERR_OK == rc);

    rc = lsmAccessGroupRevoke(c, group, n, &job);
    fail_unless(LSM_ERR_OK == rc);

    lsmVolumeRecordFree(n);
    lsmPoolRecordFree(pool);
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
    fail_unless(strcmp(id, SYSTEM_ID) == 0);

    name = lsmSystemNameGet(sys[0]);
    fail_unless(name != NULL);
    fail_unless(strcmp(name, SYSTEM_NAME) == 0);

    lsmSystemRecordFreeArray(sys, count);
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
