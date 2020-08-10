/*
 * Copyright (C) 2011-2016 Red Hat, Inc.
 * (C) Copyright 2015-2017 Hewlett Packard Enterprise Development LP
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
 * License along with this library; If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: tasleson
 *         Joe Handzik <joseph.t.handzik@hpe.com>
 *         Gris Ge <fge@redhat.com>
 */

#include <check.h>
#include <fcntl.h>
#include <libstoragemgmt/libstoragemgmt.h>
#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int compare_battery(lsm_battery *l, lsm_battery *r);

const char SYSTEM_NAME[] = "LSM simulated storage plug-in";
const char SYSTEM_ID[] = "sim-01";
const char *ISCSI_HOST[2] = {"iqn.1994-05.com.domain:01.89bd01",
                             "iqn.1994-05.com.domain:01.89bd02"};
static int is_simc_plugin = 0;

#define POLL_SLEEP                50000
#define VPD83_TO_SEARCH           "600508b1001c79ade5178f0626caaa9c"
#define INVALID_VPD83             "600508b1001c79ade5178f0626caaa9c1"
#define VALID_BUT_NOT_EXIST_VPD83 "5000000000000000"
#define NOT_EXIST_SD_PATH         "/dev/sdazzzzzzzzzzz"
#define _URI_BUFF_SIZE            128

lsm_connect *c = NULL;

char *error(lsm_error_ptr e) {
    static char eb[1024];
    memset(eb, 0, sizeof(eb));

    if (e != NULL) {
        snprintf(eb, sizeof(eb), "Error msg= %s - exception %s - debug %s",
                 lsm_error_message_get(e), lsm_error_exception_get(e),
                 lsm_error_debug_get(e));
        lsm_error_free(e);
        e = NULL;
    } else {
        snprintf(eb, sizeof(eb), "No addl. error info.");
    }
    return eb;
}

/**
 * Macro for calls which we expect success.
 * @param variable  Where the result of the call is placed
 * @param func      Name of function
 * @param ...       Function parameters
 */
#define G(variable, func, ...)                                                 \
    variable = func(__VA_ARGS__);                                              \
    ck_assert_msg(LSM_ERR_OK == variable, "call:%s rc = %d %s (which %d)",     \
                  #func, variable, error(lsm_error_last_get(c)),               \
                  is_simc_plugin);

/**
 * Macro for calls which we expect failure.
 * @param variable  Where the result of the call is placed
 * @param func      Name of function
 * @param ...       Function parameters
 */
#define F(variable, func, ...)                                                 \
    variable = func(__VA_ARGS__);                                              \
    ck_assert_msg(LSM_ERR_OK != variable, "call:%s rc = %d %s (which %d)",     \
                  #func, variable, error(lsm_error_last_get(c)),               \
                  is_simc_plugin);

#define ASSERT_STR_MATCH(l, r)                                                 \
    ck_assert_msg(strcmp(l, r) == 0, "%s != %s", l, r);

/**
 * Generates a random string in the buffer with specified length.
 * Note: This function should not produce repeating sequences or duplicates
 * regardless if it's used repeatedly in the same function in the same process
 * or different forked processes.
 * @param buff  Buffer to write the random string to
 * @param len   Length of the random string
 */
void generate_random(char *buff, uint32_t len) {
    uint32_t i = 0;
    ssize_t cur_got = 0;
    size_t got = 0;
    int fd = -1;
    uint8_t *raw_data;

    if (buff == NULL)
        return;

    raw_data = (uint8_t *)malloc(len * sizeof(uint8_t));
    if (raw_data == NULL)
        return;

    memset(raw_data, 0, len);

    /* Coverity said we should not use rand() */
    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        goto out;

    while (got < len) {
        cur_got = read(fd, raw_data + got, len - got);
        if (cur_got < 0) {
            goto out;
        }
        got += cur_got;
    }

    for (i = 0; i < (len - 1); ++i) {
        buff[i] = 'a' + raw_data[i] % 26;
    }
    buff[len - 1] = '\0';

out:
    if (fd >= 0)
        close(fd);
    free(raw_data);
    return;
}

const char *plugin_to_use(char *uri_buff) {
    const char *rundir = NULL;
    char name[32];

    rundir = getenv("LSM_TEST_RUNDIR");

    if (rundir) {
        generate_random(name, sizeof(name) / sizeof(name[0]));
        snprintf(uri_buff, _URI_BUFF_SIZE,
                 "%s://localhost/?statefile=%s/lsm_sim_%s",
                 is_simc_plugin == 1 ? "simc" : "sim", rundir, name);
    } else {
        printf("Missing LSM_TEST_RUNDIR, expect test failures!\n");
        exit(1);
    }

    printf("URI = %s\n", uri_buff);
    return uri_buff;
}

lsm_pool *get_test_pool(lsm_connect *c) {
    lsm_pool **pools = NULL;
    uint32_t count = 0;
    lsm_pool *test_pool = NULL;
    int rc = 0;

    G(rc, lsm_pool_list, c, NULL, NULL, &pools, &count, LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_OK == rc) {
        uint32_t i = 0;
        for (i = 0; i < count; ++i) {
            if (strcmp(lsm_pool_name_get(pools[i]), "lsm_test_aggr") == 0) {
                test_pool = lsm_pool_record_copy(pools[i]);
                G(rc, lsm_pool_record_array_free, pools, count);
                break;
            }
        }
    }
    return test_pool;
}

void dump_error(lsm_error_ptr e) {
    int rc = 0;
    if (e != NULL) {
        printf("Error msg= %s - exception %s - debug %s\n",
               lsm_error_message_get(e), lsm_error_exception_get(e),
               lsm_error_debug_get(e));

        G(rc, lsm_error_free, e);
        e = NULL;
    } else {
        printf("No additional error information!\n");
    }
}

void setup(void) {
    /*
     * Note: Do not use any error reporting functions in this function
     */
    char uri[_URI_BUFF_SIZE];

    lsm_error_ptr e = NULL;

    int rc = lsm_connect_password(plugin_to_use(uri), NULL, &c, 30000, &e,
                                  LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_OK == rc) {
        if (getenv("LSM_DEBUG_PLUGIN")) {
            printf("Attach debugger to plug-in, press <return> when ready...");
            if (getchar() == -1)
                exit(EXIT_FAILURE);
        }
    }
    if (rc != LSM_ERR_OK) {
        printf("Failed to create connection: code %d, %s\n", rc, error(e));
        exit(EXIT_FAILURE);
    }
}

void teardown(void) {
    /*
     * Note: Do not use any error reporting functions in this function
     */

    if (c) {
        lsm_connect_close(c, LSM_CLIENT_FLAG_RSVD);
        c = NULL;
    }
}

void wait_for_job(lsm_connect *c, char **job_id) {
    lsm_job_status status;
    uint8_t pc = 0;
    int rc = 0;

    do {
        G(rc, lsm_job_status_get, c, *job_id, &status, &pc,
          LSM_CLIENT_FLAG_RSVD);
        printf("GENERIC: Job %s in progress, %d done, status = %d\n", *job_id,
               pc, status);
        usleep(POLL_SLEEP);

    } while (status == LSM_JOB_INPROGRESS);

    G(rc, lsm_job_free, c, job_id, LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(LSM_JOB_COMPLETE == status, "status = %d", status);
    ck_assert_msg(100 == pc, "Percent complete %d", pc);
    ck_assert_msg(job_id != NULL, "NULL");
}

lsm_volume *wait_for_job_vol(lsm_connect *c, char **job_id) {
    lsm_job_status status;
    lsm_volume *vol = NULL;
    uint8_t pc = 0;
    int rc = 0;

    do {
        G(rc, lsm_job_status_volume_get, c, *job_id, &status, &pc, &vol,
          LSM_CLIENT_FLAG_RSVD);
        printf("VOLUME: Job %s in progress, %d done, status = %d\n", *job_id,
               pc, status);
        usleep(POLL_SLEEP);

    } while (rc == LSM_ERR_OK && status == LSM_JOB_INPROGRESS);

    printf("Volume complete: Job %s percent %d done, status = %d, rc=%d\n",
           *job_id, pc, status, rc);

    G(rc, lsm_job_free, c, job_id, LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(LSM_JOB_COMPLETE == status, "status = %d", status);
    ck_assert_msg(100 == pc, "Percent complete %d", pc);

    return vol;
}

lsm_pool *wait_for_job_pool(lsm_connect *c, char **job_id) {
    lsm_job_status status;
    lsm_pool *pool = NULL;
    uint8_t pc = 0;
    int rc = 0;

    do {
        G(rc, lsm_job_status_pool_get, c, *job_id, &status, &pc, &pool,
          LSM_CLIENT_FLAG_RSVD);
        printf("POOL: Job %s in progress, %d done, status = %d\n", *job_id, pc,
               status);
        usleep(POLL_SLEEP);

    } while (status == LSM_JOB_INPROGRESS);

    G(rc, lsm_job_free, c, job_id, LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(LSM_JOB_COMPLETE == status, "status = %d", status);
    ck_assert_msg(100 == pc, "Percent complete %d", pc);

    return pool;
}

lsm_fs *wait_for_job_fs(lsm_connect *c, char **job_id) {
    lsm_job_status status;
    lsm_fs *fs = NULL;
    uint8_t pc = 0;
    int rc = 0;

    do {
        G(rc, lsm_job_status_fs_get, c, *job_id, &status, &pc, &fs,
          LSM_CLIENT_FLAG_RSVD);
        printf("FS: Job %s in progress, %d done, status = %d\n", *job_id, pc,
               status);
        usleep(POLL_SLEEP);

    } while (status == LSM_JOB_INPROGRESS);

    G(rc, lsm_job_free, c, job_id, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_JOB_COMPLETE == status, "status = %d", status);
    ck_assert_msg(100 == pc, "Percent complete %d", pc);

    return fs;
}

lsm_fs_ss *wait_for_job_ss(lsm_connect *c, char **job_id) {
    lsm_job_status status;
    lsm_fs_ss *ss = NULL;
    uint8_t pc = 0;
    int rc = 0;

    do {
        G(rc, lsm_job_status_ss_get, c, *job_id, &status, &pc, &ss,
          LSM_CLIENT_FLAG_RSVD);
        printf("SS: Job %s in progress, %d done, status = %d\n", *job_id, pc,
               status);
        usleep(POLL_SLEEP);

    } while (status == LSM_JOB_INPROGRESS);

    G(rc, lsm_job_free, c, job_id, LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(LSM_JOB_COMPLETE == status, "status = %d", status);
    ck_assert_msg(100 == pc, "Percent complete %d", pc);

    return ss;
}

int compare_string_lists(lsm_string_list *l, lsm_string_list *r) {
    uint32_t i = 0;

    if (l && r) {

        if (l == r) {
            return 0;
        }

        if (lsm_string_list_size(l) != lsm_string_list_size(r)) {
            return 1;
        }

        for (i = 0; i < lsm_string_list_size(l); ++i) {
            if (strcmp(lsm_string_list_elem_get(l, i),
                       lsm_string_list_elem_get(r, i)) != 0) {
                return 1;
            }
        }
        return 0;
    }
    return 1;
}

void create_volumes(lsm_connect *c, lsm_pool *p, int num) {
    int i;

    for (i = 0; i < num; ++i) {
        lsm_volume *n = NULL;
        char *job = NULL;
        char name[32];

        memset(name, 0, sizeof(name));
        snprintf(name, sizeof(name), "test %d", i);

        int vc = lsm_volume_create(c, p, name, 20000000,
                                   LSM_VOLUME_PROVISION_DEFAULT, &n, &job,
                                   LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(vc == LSM_ERR_OK || vc == LSM_ERR_JOB_STARTED,
                      "lsmVolumeCreate %d (%s)", vc,
                      error(lsm_error_last_get(c)));

        if (LSM_ERR_JOB_STARTED == vc) {
            n = wait_for_job_vol(c, &job);
        } else {
            ck_assert_msg(LSM_ERR_OK == vc, "vc = %d", vc);
        }

        G(vc, lsm_volume_record_free, n);
        n = NULL;
    }
}

lsm_system *get_system(lsm_connect *c) {
    lsm_system *rc_sys = NULL;
    lsm_system **sys = NULL;
    uint32_t count = 0;
    int rc = 0;

    G(rc, lsm_system_list, c, &sys, &count, LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_OK == rc && count) {
        rc_sys = lsm_system_record_copy(sys[0]);
        G(rc, lsm_system_record_array_free, sys, count);
    }
    return rc_sys;
}

START_TEST(test_smoke_test) {
    uint32_t i = 0;
    int rc = 0;

    lsm_pool *selectedPool = NULL;
    uint32_t poolCount = 0;

    uint32_t set_tmo = 31123;
    uint32_t tmo = 0;

    // Set timeout.
    G(rc, lsm_connect_timeout_set, c, set_tmo, LSM_CLIENT_FLAG_RSVD);

    // Get time-out.
    G(rc, lsm_connect_timeout_get, c, &tmo, LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(set_tmo == tmo, " %u != %u", set_tmo, tmo);

    lsm_pool **pools = NULL;
    uint32_t count = 0;
    int poolToUse = -1;

    // Get pool list
    G(rc, lsm_pool_list, c, NULL, NULL, &pools, &poolCount,
      LSM_CLIENT_FLAG_RSVD);

    // Check pool count
    count = poolCount;
    ck_assert_msg(count == 4, "We are expecting 4 pools from simulator");

    // Dump pools and select a pool to use for testing.
    for (i = 0; i < count; ++i) {
        printf("Id= %s, name=%s, capacity= %" PRIu64 ", remaining= %" PRIu64 " "
               "system %s\n",
               lsm_pool_id_get(pools[i]), lsm_pool_name_get(pools[i]),
               lsm_pool_total_space_get(pools[i]),
               lsm_pool_free_space_get(pools[i]),
               lsm_pool_system_id_get(pools[i]));

        ck_assert_msg(strcmp(lsm_pool_system_id_get(pools[i]), SYSTEM_ID) == 0,
                      "Expecting system id of %s, got %s", SYSTEM_ID,
                      lsm_pool_system_id_get(pools[i]));

        ck_assert_msg(lsm_pool_status_get(pools[i]) == LSM_POOL_STATUS_OK,
                      "%" PRIu64, lsm_pool_status_get(pools[i]));

        if (lsm_pool_free_space_get(pools[i]) > 20000000) {
            poolToUse = i;
        }
    }

    if (poolToUse != -1) {
        lsm_volume *n = NULL;
        char *job = NULL;

        selectedPool = pools[poolToUse];

        int vc = lsm_volume_create(c, pools[poolToUse], "test", 20000000,
                                   LSM_VOLUME_PROVISION_DEFAULT, &n, &job,
                                   LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(vc == LSM_ERR_OK || vc == LSM_ERR_JOB_STARTED,
                      "lsmVolumeCreate %d (%s)", vc,
                      error(lsm_error_last_get(c)));

        if (LSM_ERR_JOB_STARTED == vc) {
            n = wait_for_job_vol(c, &job);

            ck_assert_msg(n != NULL, "Expected NULL");
        }

        uint8_t dependants = 10;
        int child_depends = 0;
        G(child_depends, lsm_volume_child_dependency, c, n, &dependants,
          LSM_CLIENT_FLAG_RSVD);
        ck_assert_msg(dependants == 0, "dependants = %d", dependants);

        child_depends = lsm_volume_child_dependency_delete(
            c, n, &job, LSM_CLIENT_FLAG_RSVD);
        if (LSM_ERR_JOB_STARTED == child_depends) {
            wait_for_job(c, &job);
        } else if (LSM_ERR_NO_STATE_CHANGE != child_depends) {
            ck_assert_msg(LSM_ERR_OK == child_depends, "rc = %d",
                          child_depends);
            ck_assert_msg(NULL == job, "Expected job == NULL");
        }

        lsm_block_range **range = lsm_block_range_record_array_alloc(3);
        ck_assert_msg(NULL != range, "Expected range != NULL");

        uint32_t bs = 0;
        lsm_system *system = get_system(c);

        int rep_bs = 0;
        G(rep_bs, lsm_volume_replicate_range_block_size, c, system, &bs,
          LSM_CLIENT_FLAG_RSVD);
        ck_assert_msg(512 == bs, "block size = %d", bs);

        lsm_system_record_free(system);

        int rep_i = 0;

        for (rep_i = 0; rep_i < 3; ++rep_i) {
            range[rep_i] = lsm_block_range_record_alloc(
                (rep_i * 1000), ((rep_i + 100) * 10000), 10);

            lsm_block_range *copy = lsm_block_range_record_copy(range[rep_i]);

            ck_assert_msg(lsm_block_range_source_start_get(range[rep_i]) ==
                              lsm_block_range_source_start_get(copy),
                          "miss-match copy");

            ck_assert_msg(lsm_block_range_dest_start_get(range[rep_i]) ==
                              lsm_block_range_dest_start_get(copy),
                          "miss-match copy");

            ck_assert_msg(lsm_block_range_block_count_get(range[rep_i]) ==
                              lsm_block_range_block_count_get(copy),
                          "miss-match copy");

            G(rc, lsm_block_range_record_free, copy);
            copy = NULL;
        }

        int rep_range =
            lsm_volume_replicate_range(c, LSM_VOLUME_REPLICATE_CLONE, n, n,
                                       range, 3, &job, LSM_CLIENT_FLAG_RSVD);

        if (LSM_ERR_JOB_STARTED == rep_range) {
            wait_for_job(c, &job);
        } else {

            if (LSM_ERR_OK != rep_range) {
                dump_error(lsm_error_last_get(c));
            }

            ck_assert_msg(LSM_ERR_OK == rep_range, "rep_range = %d", rep_range);
        }

        G(rc, lsm_block_range_record_array_free, range, 3);

        int online = 0;
        G(online, lsm_volume_disable, c, n, LSM_CLIENT_FLAG_RSVD);

        G(online, lsm_volume_enable, c, n, LSM_CLIENT_FLAG_RSVD);

        char *jobDel = NULL;
        int delRc = lsm_volume_delete(c, n, &jobDel, LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(delRc == LSM_ERR_OK || delRc == LSM_ERR_JOB_STARTED,
                      "lsm_volume_delete %d (%s)", rc,
                      error(lsm_error_last_get(c)));

        if (LSM_ERR_JOB_STARTED == delRc) {
            wait_for_job_vol(c, &jobDel);
        }

        G(rc, lsm_volume_record_free, n);
    }

    // Create some volumes for testing.
    create_volumes(c, selectedPool, 3);

    lsm_volume **volumes = NULL;
    count = 0;
    /* Get a list of volumes */
    G(rc, lsm_volume_list, c, NULL, NULL, &volumes, &count,
      LSM_CLIENT_FLAG_RSVD);

    for (i = 0; i < count; ++i) {
        printf("%s - %s - %s - %" PRIu64 " - %" PRIu64 " - %x\n",
               lsm_volume_id_get(volumes[i]), lsm_volume_name_get(volumes[i]),
               lsm_volume_vpd83_get(volumes[i]),
               lsm_volume_block_size_get(volumes[i]),
               lsm_volume_number_of_blocks_get(volumes[i]),
               lsm_volume_admin_state_get(volumes[i]));
    }

    if (count) {

        lsm_volume *rep = NULL;
        char *job = NULL;

        // Try a re-size then a snapshot
        lsm_volume *resized = NULL;
        char *resizeJob = NULL;

        int resizeRc =
            lsm_volume_resize(c, volumes[0],
                              ((lsm_volume_number_of_blocks_get(volumes[0]) *
                                lsm_volume_block_size_get(volumes[0])) *
                               2),
                              &resized, &resizeJob, LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(resizeRc == LSM_ERR_OK || resizeRc == LSM_ERR_JOB_STARTED,
                      "lsmVolumeResize %d (%s)", resizeRc,
                      error(lsm_error_last_get(c)));

        if (LSM_ERR_JOB_STARTED == resizeRc) {
            resized = wait_for_job_vol(c, &resizeJob);
        }

        G(rc, lsm_volume_record_free, resized);

        // Lets create a clone of one.
        int repRc =
            lsm_volume_replicate(c, NULL, // Pool is optional
                                 LSM_VOLUME_REPLICATE_CLONE, volumes[0],
                                 "CLONE1", &rep, &job, LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(repRc == LSM_ERR_OK || repRc == LSM_ERR_JOB_STARTED,
                      "lsmVolumeReplicate %d (%s)", repRc,
                      error(lsm_error_last_get(c)));

        if (LSM_ERR_JOB_STARTED == repRc) {
            rep = wait_for_job_vol(c, &job);
        }

        G(rc, lsm_volume_record_free, rep);

        G(rc, lsm_volume_record_array_free, volumes, count);

        if (pools) {
            G(rc, lsm_pool_record_array_free, pools, poolCount);
        }
    }
}

END_TEST

START_TEST(test_access_groups) {
    lsm_access_group **groups = NULL;
    lsm_access_group *group = NULL;
    uint32_t count = 0;
    uint32_t i = 0;
    lsm_string_list *init_list = NULL;
    lsm_system *system = NULL;
    int rc = 0;

    ck_assert_msg(c != NULL, "Expecting c != NULL");

    system = get_system(c);

    G(rc, lsm_access_group_list, c, NULL, NULL, &groups, &count,
      LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(count == 0, "Expect 0 access groups, got %" PRIu32, count);
    ck_assert_msg(groups == NULL, "Expecting groups == NULL");

    G(rc, lsm_access_group_create, c, "test_access_groups",
      "iqn.1994-05.com.domain:01.89bd01", LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN,
      system, &group, LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_OK == rc) {
        lsm_string_list *init_list = lsm_access_group_initiator_id_get(group);
        lsm_string_list *init_copy = NULL;

        ck_assert_msg(lsm_string_list_size(init_list) == 1, "list size = %d",
                      lsm_string_list_size(init_list));

        init_copy = lsm_string_list_copy(init_list);
        lsm_access_group_initiator_id_set(group, init_copy);

        printf("%s - %s - %s\n", lsm_access_group_id_get(group),
               lsm_access_group_name_get(group),
               lsm_access_group_system_id_get(group));

        ck_assert_msg(NULL != lsm_access_group_id_get(group), "NULL");
        ck_assert_msg(NULL != lsm_access_group_name_get(group), "NULL");
        ck_assert_msg(NULL != lsm_access_group_system_id_get(group), "NULL");
        ck_assert_msg(LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN ==
                          lsm_access_group_init_type_get(group),
                      "type = %d", (int)lsm_access_group_init_type_get(group));

        lsm_access_group *copy = lsm_access_group_record_copy(group);
        if (copy) {
            ck_assert_msg(strcmp(lsm_access_group_id_get(group),
                                 lsm_access_group_id_get(copy)) == 0,
                          "%s != %s", lsm_access_group_id_get(group),
                          lsm_access_group_id_get(copy));
            ck_assert_msg(strcmp(lsm_access_group_name_get(group),
                                 lsm_access_group_name_get(copy)) == 0,
                          "%s != %s", lsm_access_group_name_get(group),
                          lsm_access_group_name_get(copy));
            ck_assert_msg(strcmp(lsm_access_group_system_id_get(group),
                                 lsm_access_group_system_id_get(copy)) == 0,
                          "%s != %s", lsm_access_group_system_id_get(group),
                          lsm_access_group_system_id_get(copy));
            ck_assert_msg(lsm_access_group_init_type_get(group) ==
                              lsm_access_group_init_type_get(copy),
                          "%d != %d", lsm_access_group_init_type_get(group),
                          lsm_access_group_init_type_get(copy));

            G(rc, lsm_access_group_record_free, copy);
            copy = NULL;
        }

        G(rc, lsm_string_list_free, init_copy);
        init_copy = NULL;
    }

    G(rc, lsm_access_group_list, c, NULL, NULL, &groups, &count,
      LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(1 == count, "count = %d", count);
    G(rc, lsm_access_group_record_array_free, groups, count);
    groups = NULL;
    count = 0;
    // char *job = NULL;
    lsm_access_group *updated = NULL;

    rc = lsm_access_group_initiator_add(
        c, group, "iqn.1994-05.com.domain:01.89bd02",
        LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN, &updated, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_OK == rc,
                  "Expected success on lsmAccessGroupInitiatorAdd %d %d", rc,
                  is_simc_plugin);

    G(rc, lsm_access_group_list, c, NULL, NULL, &groups, &count,
      LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(1 == count, "count = %d", count);

    ck_assert_msg(updated != NULL, "Updated = %p", updated);
    lsm_access_group_record_free(updated);
    updated = NULL;

    if (count) {
        init_list = lsm_access_group_initiator_id_get(groups[0]);
        ck_assert_msg(lsm_string_list_size(init_list) == 2,
                      "Expecting 2 initiators, current num = %d\n",
                      lsm_string_list_size(init_list));
        for (i = 0; i < lsm_string_list_size(init_list) - 1; ++i) {
            printf("%d = %s\n", i, lsm_string_list_elem_get(init_list, i));

            printf("Deleting initiator %s from group!\n",
                   lsm_string_list_elem_get(init_list, i));

            G(rc, lsm_access_group_initiator_delete, c, groups[0],
              lsm_string_list_elem_get(init_list, i),
              LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN, &updated,
              LSM_CLIENT_FLAG_RSVD)

            ck_assert_msg(updated != NULL, "updated = %p", updated);
            lsm_access_group_record_free(updated);
            updated = NULL;
        }
        init_list = NULL;
    }

    if (group) {
        G(rc, lsm_access_group_record_free, group);
        group = NULL;
    }

    G(rc, lsm_access_group_record_array_free, groups, count);
    groups = NULL;
    count = 0;

    G(rc, lsm_access_group_list, c, NULL, NULL, &groups, &count,
      LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_OK == rc, "rd = %d", rc);
    ck_assert_msg(1 == count, "count = %d", count);

    if (count) {
        init_list = lsm_access_group_initiator_id_get(groups[0]);
        ck_assert_msg(init_list != NULL, "init_list = %p", init_list);
        ck_assert_msg(lsm_string_list_size(init_list) == 1, "%d",
                      lsm_string_list_size(init_list));
        init_list = NULL;
        G(rc, lsm_access_group_record_array_free, groups, count);
        groups = NULL;
        count = 0;
    }

    G(rc, lsm_system_record_free, system);
    system = NULL;
}
END_TEST

START_TEST(test_access_groups_grant_revoke) {
    ck_assert_msg(c != NULL, "c = %p", c);
    lsm_access_group *group = NULL;
    int rc = 0;
    lsm_pool *pool = get_test_pool(c);
    char *job = NULL;
    lsm_volume *n = NULL;
    lsm_system *system = NULL;

    ck_assert_msg(pool != NULL, "pool = %p", pool);
    system = get_system(c);

    G(rc, lsm_access_group_create, c, "test_access_groups_grant_revoke",
      ISCSI_HOST[0], LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN, system, &group,
      LSM_CLIENT_FLAG_RSVD);

    int vc = lsm_volume_create(c, pool, "volume_grant_test", 20000000,
                               LSM_VOLUME_PROVISION_DEFAULT, &n, &job,
                               LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(vc == LSM_ERR_OK || vc == LSM_ERR_JOB_STARTED,
                  "lsmVolumeCreate %d (%s)", vc, error(lsm_error_last_get(c)));

    if (LSM_ERR_JOB_STARTED == vc) {
        n = wait_for_job_vol(c, &job);
    }

    ck_assert_msg(n != NULL, "n = %p", n);

    rc = lsm_volume_mask(c, group, n, LSM_CLIENT_FLAG_RSVD);
    if (LSM_ERR_JOB_STARTED == rc) {
        wait_for_job(c, &job);
    } else {
        ck_assert_msg(LSM_ERR_OK == rc, "rc = %d, plug-in = %d", rc,
                      is_simc_plugin);
    }

    lsm_volume **volumes = NULL;
    uint32_t v_count = 0;
    G(rc, lsm_volumes_accessible_by_access_group, c, group, &volumes, &v_count,
      LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(v_count == 1, "v_count = %d", v_count);

    if (v_count >= 1) {
        ck_assert_msg(
            strcmp(lsm_volume_id_get(volumes[0]), lsm_volume_id_get(n)) == 0,
            "%s != %s", lsm_volume_id_get(volumes[0]), lsm_volume_id_get(n));
        G(rc, lsm_volume_record_array_free, volumes, v_count);
    }

    lsm_access_group **groups;
    uint32_t g_count = 0;
    G(rc, lsm_access_groups_granted_to_volume, c, n, &groups, &g_count,
      LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(g_count == 1, "g_count = %d", g_count);

    if (g_count >= 1) {
        ck_assert_msg(strcmp(lsm_access_group_id_get(groups[0]),
                             lsm_access_group_id_get(group)) == 0,
                      "%s != %s", lsm_access_group_id_get(groups[0]),
                      lsm_access_group_id_get(group));
        G(rc, lsm_access_group_record_array_free, groups, g_count);
    }

    rc = lsm_volume_unmask(c, group, n, LSM_CLIENT_FLAG_RSVD);
    if (LSM_ERR_JOB_STARTED == rc) {
        wait_for_job(c, &job);
    } else {
        ck_assert_msg(LSM_ERR_OK == rc, "rc = %d, is_simc_plugin=%d", rc,
                      is_simc_plugin);
    }

    G(rc, lsm_access_group_delete, c, group, LSM_CLIENT_FLAG_RSVD);
    G(rc, lsm_access_group_record_free, group);

    G(rc, lsm_volume_record_free, n);
    G(rc, lsm_pool_record_free, pool);

    G(rc, lsm_system_record_free, system);
}
END_TEST

START_TEST(test_fs) {
    ck_assert_msg(c != NULL, "c = %p", c);

    lsm_fs **fs_list = NULL;
    int rc = 0;
    uint32_t fs_count = 0;
    lsm_fs *nfs = NULL;
    lsm_fs *resized_fs = NULL;
    char *job = NULL;
    uint64_t fs_free_space = 0;

    lsm_pool *test_pool = get_test_pool(c);

    G(rc, lsm_fs_list, c, NULL, NULL, &fs_list, &fs_count,
      LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(0 == fs_count, "fs_count = %d", fs_count);

    rc = lsm_fs_create(c, test_pool, "C_unit_test", 50000000, &nfs, &job,
                       LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_JOB_STARTED == rc) {
        ck_assert_msg(NULL == nfs, "nfs = %p", nfs);

        nfs = wait_for_job_fs(c, &job);
    } else {
        ck_assert_msg(LSM_ERR_OK == rc, "%d", rc);
    }

    ck_assert_msg(NULL != nfs, "nfs = %p", nfs);

    fs_free_space = lsm_fs_free_space_get(nfs);
    ck_assert_msg(fs_free_space != 0, "fs_free_space = %lu", fs_free_space);

    lsm_fs *cloned_fs = NULL;
    rc = lsm_fs_clone(c, nfs, "cloned_fs", NULL, &cloned_fs, &job,
                      LSM_CLIENT_FLAG_RSVD);
    if (LSM_ERR_JOB_STARTED == rc) {
        ck_assert_msg(NULL == cloned_fs, "cloned_fs = %p", cloned_fs);
        cloned_fs = wait_for_job_fs(c, &job);

        rc = lsm_fs_record_free(cloned_fs);
        cloned_fs = NULL;
        ck_assert_msg(LSM_ERR_OK == rc, "rc= %d", rc);
    } else {
        ck_assert_msg(LSM_ERR_OK == rc, "rc= %d", rc);
    }

    rc = lsm_fs_file_clone(c, nfs, "src/file.txt", "dest/file.txt", NULL, &job,
                           LSM_CLIENT_FLAG_RSVD);
    if (LSM_ERR_JOB_STARTED == rc) {
        wait_for_job(c, &job);
    } else {
        ck_assert_msg(LSM_ERR_OK == rc, "rc= %d", rc);
    }

    G(rc, lsm_fs_list, c, NULL, NULL, &fs_list, &fs_count,
      LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(2 == fs_count, "fs_count = %d", fs_count);
    G(rc, lsm_fs_record_array_free, fs_list, fs_count);
    fs_list = NULL;
    fs_count = 0;

    rc = lsm_fs_resize(c, nfs, 100000000, &resized_fs, &job,
                       LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_JOB_STARTED == rc) {
        ck_assert_msg(NULL == resized_fs, "resized_fs = %p", resized_fs);
        resized_fs = wait_for_job_fs(c, &job);
    }

    uint8_t yes_no = 10;
    G(rc, lsm_fs_child_dependency, c, nfs, NULL, &yes_no, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(yes_no != 0, "%d", yes_no);

    rc = lsm_fs_child_dependency_delete(c, nfs, NULL, &job,
                                        LSM_CLIENT_FLAG_RSVD);
    if (LSM_ERR_JOB_STARTED == rc) {
        ck_assert_msg(NULL != job, "NULL");
        wait_for_job(c, &job);
    } else {
        ck_assert_msg(LSM_ERR_OK == rc, "rc= %d", rc);
    }

    rc = lsm_fs_delete(c, resized_fs, &job, LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_JOB_STARTED == rc) {
        wait_for_job(c, &job);
    } else {
        ck_assert_msg(LSM_ERR_OK == rc, "lsm_fs_delete() failed: %d (%s)", rc,
                      error(lsm_error_last_get(c)));
    }

    G(rc, lsm_fs_record_free, resized_fs);
    G(rc, lsm_fs_record_free, nfs);
    G(rc, lsm_pool_record_free, test_pool);
}
END_TEST

START_TEST(test_ss) {
    ck_assert_msg(c != NULL, "c = %p", c);
    lsm_fs_ss **ss_list = NULL;
    uint32_t ss_count = 0;
    char *job = NULL;
    lsm_fs *fs = NULL;
    lsm_fs_ss *ss = NULL;

    lsm_pool *test_pool = get_test_pool(c);

    int rc = lsm_fs_create(c, test_pool, "test_fs", 100000000, &fs, &job,
                           LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_JOB_STARTED == rc) {
        fs = wait_for_job_fs(c, &job);
    }

    ck_assert_msg(fs != NULL, "fs = %p", fs);

    G(rc, lsm_pool_record_free, test_pool);
    test_pool = NULL;

    G(rc, lsm_fs_ss_list, c, fs, &ss_list, &ss_count, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(NULL == ss_list, "ss_list = %p", ss_list);
    ck_assert_msg(0 == ss_count, "ss_count = %d", ss_count);

    rc = lsm_fs_ss_create(c, fs, "test_snap", &ss, &job, LSM_CLIENT_FLAG_RSVD);
    if (LSM_ERR_JOB_STARTED == rc) {
        printf("Waiting for snap to create!\n");
        ss = wait_for_job_ss(c, &job);
    } else {
        ck_assert_msg(LSM_ERR_OK == rc, "rc = %d", rc);
    }

    ck_assert_msg(NULL != ss, "ss = %p", ss);

    G(rc, lsm_fs_ss_list, c, fs, &ss_list, &ss_count, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(NULL != ss_list, "ss_list = %p", ss_list);
    ck_assert_msg(1 == ss_count, "ss_count = %d", ss_count);

    lsm_string_list *files = lsm_string_list_alloc(1);
    if (files) {
        G(rc, lsm_string_list_elem_set, files, 0, "some/file/name.txt");
    }

    rc = lsm_fs_ss_restore(c, fs, ss, files, files, 0, &job,
                           LSM_CLIENT_FLAG_RSVD);
    if (LSM_ERR_JOB_STARTED == rc) {
        printf("Waiting for  lsm_fs_ss_restore!\n");
        wait_for_job(c, &job);
    } else {
        ck_assert_msg(LSM_ERR_OK == rc, "rc = %d", rc);
    }

    G(rc, lsm_string_list_free, files);

    rc = lsm_fs_ss_delete(c, fs, ss, &job, LSM_CLIENT_FLAG_RSVD);
    if (LSM_ERR_JOB_STARTED == rc) {
        wait_for_job(c, &job);
    }

    G(rc, lsm_fs_ss_record_array_free, ss_list, ss_count);
    G(rc, lsm_fs_record_free, fs);
    G(rc, lsm_fs_ss_record_free, ss);
}
END_TEST

START_TEST(test_systems) {
    uint32_t count = 0;
    lsm_system **sys = NULL;
    const char *id = NULL;
    const char *name = NULL;
    uint32_t status = 0;
    int rc = 0;

    ck_assert_msg(c != NULL, "c = %p", c);

    G(rc, lsm_system_list, c, &sys, &count, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(count == 1, "count = %d", count);

    if (count) {
        id = lsm_system_id_get(sys[0]);
        ck_assert_msg(id != NULL, "NULL");
        ASSERT_STR_MATCH(id, SYSTEM_ID);

        name = lsm_system_name_get(sys[0]);
        ck_assert_msg(name != NULL, "NULL");
        ASSERT_STR_MATCH(name, SYSTEM_NAME);

        status = lsm_system_status_get(sys[0]);
        ck_assert_msg(status == LSM_SYSTEM_STATUS_OK, "status = %x", status);
    }

    G(rc, lsm_system_record_array_free, sys, count);
}
END_TEST

#define COMPARE_STR_FUNC(func, l, r)                                           \
    rc = strcmp(func(l), func(r));                                             \
    if (rc)                                                                    \
        return rc;

#define COMPARE_NUMBER_FUNC(func, l, r)                                        \
    if (func(l) != func(r))                                                    \
        return 1;

static int compare_disks(lsm_disk *l, lsm_disk *r) {
    int rc;
    if (l && r) {
        COMPARE_STR_FUNC(lsm_disk_id_get, l, r);
        COMPARE_STR_FUNC(lsm_disk_name_get, l, r);
        COMPARE_STR_FUNC(lsm_disk_system_id_get, l, r);
        COMPARE_NUMBER_FUNC(lsm_disk_type_get, l, r);
        COMPARE_NUMBER_FUNC(lsm_disk_number_of_blocks_get, l, r);
        COMPARE_NUMBER_FUNC(lsm_disk_block_size_get, l, r);
        COMPARE_NUMBER_FUNC(lsm_disk_status_get, l, r);
        return 0;
    }
    return 1;
}

static int compare_battery(lsm_battery *l, lsm_battery *r) {
    int rc;
    if (l && r) {
        COMPARE_STR_FUNC(lsm_battery_id_get, l, r);
        COMPARE_STR_FUNC(lsm_battery_name_get, l, r);
        COMPARE_STR_FUNC(lsm_battery_system_id_get, l, r);
        COMPARE_STR_FUNC(lsm_battery_plugin_data_get, l, r);
        COMPARE_NUMBER_FUNC(lsm_battery_type_get, l, r);
        COMPARE_NUMBER_FUNC(lsm_battery_status_get, l, r);
        return 0;
    }
    return 1;
}

START_TEST(test_disks) {
    uint32_t count = 0;
    lsm_disk **d = NULL;
    const char *id;
    const char *name;
    const char *system_id;
    uint32_t i = 0;

    ck_assert_msg(c != NULL, "c = %p", c);

    int rc = lsm_disk_list(c, NULL, NULL, &d, &count, 0);

    if (LSM_ERR_OK == rc) {
        ck_assert_msg(LSM_ERR_OK == rc, "%d", rc);
        ck_assert_msg(count >= 1, "count = %d", count);

        for (i = 0; i < count; ++i) {
            lsm_disk *d_copy = lsm_disk_record_copy(d[i]);
            ck_assert_msg(d_copy != NULL, "NULL");
            if (d_copy) {
                ck_assert_msg(compare_disks(d[i], d_copy) == 0,
                              "compare_disks failed");
                lsm_disk_record_free(d_copy);
                d_copy = NULL;
            }

            id = lsm_disk_id_get(d[i]);
            ck_assert_msg(id != NULL && strlen(id) > 0, "NULL");

            name = lsm_disk_name_get(d[i]);
            ck_assert_msg(id != NULL && strlen(name) > 0, "NULL");

            system_id = lsm_disk_system_id_get(d[i]);
            ck_assert_msg(id != NULL && strlen(system_id) > 0, "NULL");
            ck_assert_msg(strcmp(system_id, SYSTEM_ID) == 0, "%s", id);

            ck_assert_msg(lsm_disk_type_get(d[i]) >= 1, "%d",
                          (int)lsm_disk_type_get(d[i]));
            ck_assert_msg(lsm_disk_number_of_blocks_get(d[i]) >= 1, "%lu",
                          lsm_disk_number_of_blocks_get(d[i]));
            ck_assert_msg(lsm_disk_block_size_get(d[i]) >= 1, "%lu",
                          lsm_disk_block_size_get(d[i]));
            ck_assert_msg(lsm_disk_status_get(d[i]) >= 1, "%lu",
                          lsm_disk_status_get(d[i]));
            ck_assert_msg(lsm_disk_vpd83_get(d[i]) != NULL, "NULL");
        }
        lsm_disk_record_array_free(d, count);
    } else {
        ck_assert_msg(d == NULL, "d = %p", d);
        ck_assert_msg(count == 0, "count = %d", count);
    }
}
END_TEST

START_TEST(test_disk_rpm_and_link_type) {
    uint32_t count = 0;
    lsm_disk **disks = NULL;
    uint32_t i = 0;
    int rc = LSM_ERR_OK;
    int32_t rpm = LSM_DISK_RPM_UNKNOWN;
    lsm_disk_link_type link_type = LSM_DISK_LINK_TYPE_UNKNOWN;

    ck_assert_msg(c != NULL, "c = %p", c);

    rc = lsm_disk_list(c, NULL, NULL, &disks, &count, 0);
    ck_assert_msg(LSM_ERR_OK == rc, "rc: %d", rc);

    if (LSM_ERR_OK == rc) {
        ck_assert_msg(count >= 1, "count = %d", count);

        for (; i < count; ++i) {
            rpm = lsm_disk_rpm_get(disks[i]);
            ck_assert_msg(rpm != LSM_DISK_RPM_UNKNOWN,
                          "Should not be LSM_DISK_RPM_UNKNOWN when input disk "
                          "is valid %d",
                          rc);

            link_type = lsm_disk_link_type_get(disks[i]);
            ck_assert_msg(link_type != LSM_DISK_LINK_TYPE_UNKNOWN,
                          "Should not be LSM_DISK_LINK_TYPE_UNKNOWN when input "
                          "disk is valid %d",
                          rc);
        }

        rpm = lsm_disk_rpm_get(NULL);
        ck_assert_msg(
            rpm == LSM_DISK_RPM_UNKNOWN,
            "Should be LSM_DISK_RPM_UNKNOWN when input disk is NULL %d", rc);
        link_type = lsm_disk_link_type_get(NULL);
        ck_assert_msg(rpm == LSM_DISK_LINK_TYPE_UNKNOWN,
                      "Should be LSM_DISK_LINK_TYPE_UNKNOWN when input disk is "
                      "NULL %d",
                      rc);

        lsm_disk_record_array_free(disks, count);
    } else {
        ck_assert_msg(disks == NULL, "disks = %p", disks);
        ck_assert_msg(count == 0, "count = %d", count);
    }
}
END_TEST

START_TEST(test_disk_location) {
    uint32_t count = 0;
    lsm_disk **d = NULL;
    const char *location = NULL;
    uint32_t i = 0;
    int rc = LSM_ERR_OK;

    ck_assert_msg(c != NULL, "c = %p", c);

    G(rc, lsm_disk_list, c, NULL, NULL, &d, &count, 0);
    ck_assert_msg(count >= 1, "count = %d", count);

    for (; i < count; ++i) {
        location = lsm_disk_location_get(d[i]);
        ck_assert_msg(location != NULL,
                      "Got NULL return from lsm_disk_location_get()");

        printf("Disk location: (%s)\n", location);
    }
    location = lsm_disk_location_get(NULL);
    ck_assert_msg(location == NULL,
                  "Got non-NULL return from lsm_disk_location_get(NULL)");

    lsm_disk_record_array_free(d, count);
}
END_TEST

START_TEST(test_nfs_exports) {
    ck_assert_msg(c != NULL, "c = %p", c);
    int rc = 0;

    lsm_pool *test_pool = get_test_pool(c);
    lsm_fs *nfs = NULL;
    char *job = NULL;

    ck_assert_msg(NULL != test_pool, "test_pool = %p", test_pool);

    if (test_pool) {
        rc = lsm_fs_create(c, test_pool, "C_unit_test_nfs_export", 50000000,
                           &nfs, &job, LSM_CLIENT_FLAG_RSVD);

        if (LSM_ERR_JOB_STARTED == rc) {
            nfs = wait_for_job_fs(c, &job);
        } else {
            ck_assert_msg(LSM_ERR_OK == rc, "RC = %d", rc);
        }

        ck_assert_msg(nfs != NULL, "nfs = %p", nfs);
        lsm_nfs_export **exports = NULL;
        uint32_t count = 0;

        G(rc, lsm_pool_record_free, test_pool);
        test_pool = NULL;

        if (nfs) {
            G(rc, lsm_nfs_list, c, NULL, NULL, &exports, &count,
              LSM_CLIENT_FLAG_RSVD);
            ck_assert_msg(count == 0, "count = %d", count);
            ck_assert_msg(NULL == exports, "exports = %p", exports);

            lsm_string_list *access = lsm_string_list_alloc(1);
            ck_assert_msg(NULL != access, "access = %p", access);

            G(rc, lsm_string_list_elem_set, access, 0, "192.168.2.29");

            lsm_nfs_export *e = NULL;

            G(rc, lsm_nfs_export_fs, c, lsm_fs_id_get(nfs), NULL, access,
              access, NULL, ANON_UID_GID_NA, ANON_UID_GID_NA, NULL, NULL, &e,
              LSM_CLIENT_FLAG_RSVD);

            G(rc, lsm_nfs_export_record_free, e);
            e = NULL;

            G(rc, lsm_string_list_free, access);
            access = NULL;

            G(rc, lsm_nfs_list, c, NULL, NULL, &exports, &count,
              LSM_CLIENT_FLAG_RSVD);
            ck_assert_msg(exports != NULL, "exports = %p", exports);
            ck_assert_msg(count == 1, "count = %d", count);

            if (count) {
                G(rc, lsm_nfs_export_delete, c, exports[0],
                  LSM_CLIENT_FLAG_RSVD);
                G(rc, lsm_nfs_export_record_array_free, exports, count);
                exports = NULL;

                G(rc, lsm_nfs_list, c, NULL, NULL, &exports, &count,
                  LSM_CLIENT_FLAG_RSVD);

                ck_assert_msg(count == 0, "count = %d", count);
                ck_assert_msg(NULL == exports, "exports = %p", exports);
            }

            G(rc, lsm_fs_record_free, nfs);
            nfs = NULL;
        }
    }
}
END_TEST

struct bad_record {
    uint32_t m;
};

START_TEST(test_volume_methods) {
    lsm_volume *v = NULL;
    lsm_pool *test_pool = NULL;
    char *job = NULL;

    int rc = 0;

    ck_assert_msg(c != NULL, "c = %p", c);

    test_pool = get_test_pool(c);

    if (test_pool) {
        rc = lsm_volume_create(c, test_pool, "lsm_volume_method_test", 10000000,
                               LSM_VOLUME_PROVISION_DEFAULT, &v, &job,
                               LSM_CLIENT_FLAG_RSVD);

        if (LSM_ERR_JOB_STARTED == rc) {
            v = wait_for_job_vol(c, &job);
        } else {
            ck_assert_msg(LSM_ERR_OK == rc, "rc %d", rc);
        }

        if (v) {
            ASSERT_STR_MATCH(lsm_volume_pool_id_get(v),
                             lsm_pool_id_get(test_pool));

            rc = lsm_volume_delete(c, v, &job, LSM_CLIENT_FLAG_RSVD);
            if (LSM_ERR_JOB_STARTED == rc) {
                wait_for_job(c, &job);
            } else {
                ck_assert_msg(LSM_ERR_OK == rc, "rc %d", rc);
            }

            G(rc, lsm_volume_record_free, v);
            v = NULL;
        }

        G(rc, lsm_pool_record_free, test_pool);
        test_pool = NULL;
    }
}
END_TEST

START_TEST(test_invalid_input) {
    ck_assert_msg(c != NULL, "c = %p", c);
    int rc = 0;

    struct bad_record bad;
    bad.m = 0xA0A0A0A0;

    printf("Testing arguments\n");

    lsm_pool *test_pool = get_test_pool(c);

    lsm_connect *test_connect = NULL;
    lsm_error_ptr test_error = NULL;

    rc = lsm_connect_password(NULL, NULL, NULL, 20000, NULL,
                              LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc %d", rc);

    rc = lsm_connect_password("INVALID_URI:\\yep", NULL, &test_connect, 20000,
                              &test_error, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc %d", rc);

    rc = lsm_connect_close((lsm_connect *)&bad, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_connect_close((lsm_connect *)NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_job_status_get(c, NULL, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    char *job = NULL;
    rc = lsm_job_status_get(c, job, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    lsm_job_status status;

    rc = lsm_job_status_get(c, job, &status, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    uint8_t percent_complete;
    rc = lsm_job_status_get(c, "NO_SUCH_JOB", &status, &percent_complete,
                            LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_NOT_FOUND_JOB == rc, "rc %d", rc);

    /* lsmJobStatusVolumeGet */
    lsm_volume *vol = NULL;
    rc = lsm_job_status_volume_get(c, NULL, NULL, NULL, NULL,
                                   LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_job_status_volume_get(c, NULL, NULL, NULL, &vol,
                                   LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_job_status_volume_get(c, job, NULL, NULL, &vol,
                                   LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_job_status_volume_get(c, job, &status, NULL, &vol,
                                   LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_job_status_volume_get(c, "NO_SUCH_JOB", &status, &percent_complete,
                                   &vol, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_NOT_FOUND_JOB == rc, "rc %d", rc);

    /* lsmJobStatusFsGet */
    lsm_fs *fs = NULL;

    rc = lsm_job_status_fs_get(c, NULL, NULL, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_job_status_fs_get(c, NULL, NULL, NULL, &fs, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_job_status_fs_get(c, job, NULL, NULL, &fs, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc =
        lsm_job_status_fs_get(c, job, &status, NULL, &fs, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_job_status_fs_get(c, "NO_SUCH_JOB", &status, &percent_complete,
                               &fs, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_NOT_FOUND_JOB == rc, "rc %d", rc);

    /* lsmJobStatusFsGet */
    lsm_fs_ss *ss = (lsm_fs_ss *)&bad;

    rc = lsm_job_status_ss_get(c, NULL, NULL, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_job_status_ss_get(c, NULL, NULL, NULL, &ss, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    ss = NULL;

    rc = lsm_job_status_ss_get(c, job, NULL, NULL, &ss, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc =
        lsm_job_status_ss_get(c, job, &status, NULL, &ss, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_job_status_ss_get(c, "NO_SUCH_JOB", &status, &percent_complete,
                               &ss, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_NOT_FOUND_JOB == rc, "rc %d", rc);

    /* lsmJobFree */
    char *bogus_job = strdup("NO_SUCH_JOB");
    rc = lsm_job_free(c, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_job_free(c, &bogus_job, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_NOT_FOUND_JOB == rc, "rc %d", rc);

    ck_assert_msg(bogus_job != NULL, "Expected bogus job to != NULL!");
    free(bogus_job);

    /* lsm_disk_list */
    uint32_t count = 0;
    lsm_disk **disks = NULL;

    rc = lsm_disk_list(c, NULL, NULL, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_disk_list(c, "bogus_key", NULL, &disks, &count,
                       LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_disk_list(c, "bogus_key", "nope", &disks, &count,
                       LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_UNSUPPORTED_SEARCH_KEY == rc, "rc %d", rc);

    /* lsmPoolList */
    rc = lsm_pool_list(c, NULL, NULL, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    lsm_pool **pools = NULL;
    rc = lsm_pool_list(c, NULL, NULL, &pools, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_pool_list(c, NULL, NULL, NULL, &count, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    pools = (lsm_pool **)&bad;
    rc = lsm_pool_list(c, NULL, NULL, &pools, &count, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    pools = NULL;
    rc = lsm_pool_list(c, "bogus_key", "nope", &pools, &count,
                       LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_UNSUPPORTED_SEARCH_KEY == rc, "rc %d", rc);

    rc = lsm_pool_list(c, "bogus_key", NULL, &pools, &count,
                       LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    /* lsmVolumeList */
    rc = lsm_volume_list(c, NULL, NULL, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    lsm_volume **vols = NULL;
    rc = lsm_volume_list(c, NULL, NULL, &vols, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_volume_list(c, NULL, NULL, NULL, &count, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    vols = (lsm_volume **)&bad;
    rc = lsm_volume_list(c, NULL, NULL, &vols, &count, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    vols = NULL;
    rc = lsm_volume_list(c, "bogus_key", "nope", &vols, &count,
                         LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_UNSUPPORTED_SEARCH_KEY == rc, "rc %d", rc);

    rc = lsm_volume_list(c, "bogus_key", NULL, &vols, &count,
                         LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    /* lsmVolumeCreate */
    lsm_volume *new_vol = NULL;
    job = NULL;

    rc = lsm_volume_create(c, NULL, NULL, 0, 0, NULL, NULL,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_volume_create(c, (lsm_pool *)&bad, "BAD_POOL", 10000000,
                           LSM_VOLUME_PROVISION_DEFAULT, &new_vol, &job,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_volume_create(c, test_pool, "", 10000000,
                           LSM_VOLUME_PROVISION_DEFAULT, &new_vol, &job,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_volume_create(c, test_pool, "ARG_TESTING", 10000000,
                           LSM_VOLUME_PROVISION_DEFAULT, NULL, &job,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_volume_create(c, test_pool, "ARG_TESTING", 10000000,
                           LSM_VOLUME_PROVISION_DEFAULT, &new_vol, NULL,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    job = "NOT_NULL";
    rc = lsm_volume_create(c, test_pool, "ARG_TESTING", 10000000,
                           LSM_VOLUME_PROVISION_DEFAULT, &new_vol, &job,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    job = NULL;
    rc = lsm_volume_create(c, test_pool, "ARG_TESTING", 10000000,
                           LSM_VOLUME_PROVISION_DEFAULT, &new_vol, &job,
                           LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_JOB_STARTED == rc) {
        new_vol = wait_for_job_vol(c, &job);
    } else {
        ck_assert_msg(LSM_ERR_OK == rc, "rc %d", rc);
    }

    /* lsmVolumeResize */
    rc = lsm_volume_resize(c, NULL, 0, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    lsm_volume *resized = (lsm_volume *)&bad;
    rc = lsm_volume_resize(c, new_vol, 20000000, &resized, NULL,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    resized = NULL;
    rc = lsm_volume_resize(c, new_vol, 20000000, &resized, NULL,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_volume_resize(c, new_vol,
                           lsm_volume_number_of_blocks_get(new_vol) *
                               lsm_volume_block_size_get(new_vol),
                           &resized, &job, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_NO_STATE_CHANGE == rc, "rc = %d", rc);

    rc = lsm_volume_resize(c, new_vol, 20000000, &resized, &job,
                           LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_JOB_STARTED == rc) {
        resized = wait_for_job_vol(c, &job);
    } else {
        ck_assert_msg(LSM_ERR_OK == rc, "rc %d", rc);
    }

    /* lsmVolumeDelete */
    rc = lsm_volume_delete(c, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_volume_delete(c, resized, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc %d", rc);

    rc = lsm_volume_delete(c, resized, &job, LSM_CLIENT_FLAG_RSVD);
    if (LSM_ERR_JOB_STARTED == rc) {
        wait_for_job(c, &job);
    } else {
        ck_assert_msg(LSM_ERR_OK == rc, "rc %d", rc);
    }

    /* lsmStorageCapabilities * */
    lsm_system **sys = NULL;
    uint32_t num_systems = 0;
    rc = lsm_system_list(c, &sys, &num_systems, LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(LSM_ERR_OK == rc, "rc %d", rc);
    ck_assert_msg(sys != NULL, "sys = %p", sys);
    ck_assert_msg(num_systems >= 1, "num_systems %d", num_systems);

    rc = lsm_capabilities(c, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT, "rc %d", rc);

    if (num_systems) {
        rc = lsm_capabilities(c, sys[0], NULL, LSM_CLIENT_FLAG_RSVD);
        ck_assert_msg(LSM_ERR_INVALID_ARGUMENT, "rc %d", rc);
    }

    /* lsmVolumeReplicate */
    lsm_volume *cloned = NULL;
    rc = lsm_volume_replicate(c, (lsm_pool *)&bad, 0, NULL, NULL, NULL, NULL,
                              LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_volume_replicate(c, test_pool, LSM_VOLUME_REPLICATE_CLONE, NULL,
                              "cloned", &cloned, &job, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_volume_replicate(c, test_pool, LSM_VOLUME_REPLICATE_CLONE, new_vol,
                              "", &cloned, &job, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_volume_replicate(c, test_pool, LSM_VOLUME_REPLICATE_CLONE, new_vol,
                              "cloned", NULL, &job, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_volume_replicate(c, test_pool, LSM_VOLUME_REPLICATE_CLONE, new_vol,
                              "cloned", &cloned, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmVolumeReplicateRangeBlockSize */
    rc = lsm_volume_replicate_range_block_size(c, NULL, NULL,
                                               LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmVolumeReplicateRange */
    rc = lsm_volume_replicate_range(c, LSM_VOLUME_REPLICATE_CLONE, NULL, NULL,
                                    NULL, 0, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_volume_replicate_range(c, LSM_VOLUME_REPLICATE_CLONE, new_vol,
                                    NULL, NULL, 0, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_volume_replicate_range(c, LSM_VOLUME_REPLICATE_CLONE, new_vol,
                                    new_vol, NULL, 1, &job,
                                    LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_volume_enable(c, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_volume_disable(c, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmAccessGroupCreate */
    lsm_access_group *ag = NULL;
    lsm_system *system = NULL;
    system = get_system(c);

    rc = lsm_access_group_create(c, NULL, NULL, 0, system, NULL,
                                 LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_access_group_create(c, "my_group", ISCSI_HOST[0],
                                 LSM_ACCESS_GROUP_INIT_TYPE_OTHER, NULL, NULL,
                                 LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_access_group_create(c, "my_group", ISCSI_HOST[0],
                                 LSM_ACCESS_GROUP_INIT_TYPE_OTHER, system, &ag,
                                 LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_OK, "rc = %d", rc);
    ck_assert_msg(ag != NULL, "ag = %p", ag);

    /* lsmAccessGroupDel */
    rc = lsm_access_group_delete(c, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmAccessGroupInitiatorAdd */
    rc = lsm_access_group_initiator_add(c, NULL, NULL, 0, NULL,
                                        LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_access_group_initiator_delete(c, NULL, NULL, 0, NULL,
                                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_access_group_initiator_delete(c, ag, NULL,
                                           LSM_ACCESS_GROUP_INIT_TYPE_OTHER,
                                           NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_volume_mask(c, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_volume_mask(c, ag, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_volume_unmask(c, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_volume_unmask(c, ag, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmVolumesAccessibleByAccessGroup */
    rc = lsm_volumes_accessible_by_access_group(c, NULL, NULL, NULL,
                                                LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_volumes_accessible_by_access_group(c, ag, NULL, NULL,
                                                LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmAccessGroupsGrantedToVolume */
    rc = lsm_access_groups_granted_to_volume(c, NULL, NULL, NULL,
                                             LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_access_groups_granted_to_volume(c, new_vol, NULL, NULL,
                                             LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmVolumeChildDependency */
    rc = lsm_volume_child_dependency(c, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_volume_child_dependency(c, new_vol, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /*lsmVolumeChildDependencyDelete*/
    rc =
        lsm_volume_child_dependency_delete(c, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_volume_child_dependency_delete(c, new_vol, NULL,
                                            LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmSystemList */
    lsm_system **systems = NULL;
    rc = lsm_system_list(c, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_system_list(c, &systems, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmFsList */
    rc = lsm_fs_list(c, NULL, NULL, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    lsm_fs **fsl = NULL;
    rc = lsm_fs_list(c, NULL, NULL, &fsl, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc =
        lsm_fs_list(c, "bogus_key", "nope", &fsl, &count, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_UNSUPPORTED_SEARCH_KEY, "rc = %d", rc);

    /*lsmFsCreate*/
    rc = lsm_fs_create(c, NULL, NULL, 0, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_create(c, test_pool, NULL, 0, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    lsm_fs *arg_fs = NULL;
    rc = lsm_fs_create(c, test_pool, "argument_fs", 10000000, &arg_fs, &job,
                       LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_JOB_STARTED == rc) {
        arg_fs = wait_for_job_fs(c, &job);
    } else {
        ck_assert_msg(LSM_ERR_OK == rc, "rc = %d", rc);
    }

    /* lsmFsDelete */
    rc = lsm_fs_delete(c, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_delete(c, arg_fs, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmFsResize */
    rc = lsm_fs_resize(c, NULL, 0, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_resize(c, arg_fs, 0, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /* lsmFsClone */
    rc = lsm_fs_clone(c, NULL, NULL, NULL, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_clone(c, arg_fs, NULL, NULL, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /*lsmFsFileClone*/
    rc = lsm_fs_file_clone(c, NULL, NULL, NULL, NULL, NULL,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_file_clone(c, arg_fs, NULL, NULL, NULL, NULL,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_child_dependency(c, NULL, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    lsm_string_list *badf = (lsm_string_list *)&bad;
    rc = lsm_fs_child_dependency(c, arg_fs, badf, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    lsm_string_list *f = lsm_string_list_alloc(1);
    rc = lsm_fs_child_dependency(c, arg_fs, f, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    /*lsmFsChildDependencyDelete*/
    rc = lsm_fs_child_dependency_delete(c, NULL, NULL, NULL,
                                        LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_child_dependency_delete(c, arg_fs, badf, NULL,
                                        LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_child_dependency_delete(c, arg_fs, f, NULL,
                                        LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_ss_list(c, NULL, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_ss_list(c, arg_fs, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_ss_create(c, NULL, NULL, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_ss_create(c, arg_fs, NULL, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    lsm_fs_ss *arg_ss = NULL;

    rc = lsm_fs_ss_create(c, arg_fs, "arg_snapshot", &arg_ss, &job,
                          LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_JOB_STARTED == rc) {
        arg_ss = wait_for_job_ss(c, &job);
    } else {
        ck_assert_msg(rc == LSM_ERR_OK, "rc = %d", rc);
    }

    rc = lsm_fs_ss_delete(c, NULL, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_ss_delete(c, arg_fs, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_ss_delete(c, arg_fs, arg_ss, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_ss_restore(c, NULL, NULL, NULL, NULL, 0, NULL,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_ss_restore(c, arg_fs, NULL, NULL, NULL, 0, NULL,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_ss_restore(c, arg_fs, arg_ss, badf, NULL, 0, NULL,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_ss_restore(c, arg_fs, arg_ss, badf, badf, 0, NULL,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_fs_ss_restore(c, arg_fs, arg_ss, f, f, 0, NULL,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_nfs_list(c, NULL, NULL, NULL, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_access_group_record_free(ag);
    ag = NULL;
    ck_assert_msg(LSM_ERR_OK == rc, "%d", rc);

    rc = lsm_fs_ss_record_free(arg_ss);
    ck_assert_msg(LSM_ERR_OK == rc, "%d", rc);
    arg_ss = NULL;

    rc = lsm_fs_record_free(arg_fs);
    ck_assert_msg(LSM_ERR_OK == rc, "%d", rc);
    arg_fs = NULL;

    rc = lsm_nfs_export_fs(c, NULL, NULL, NULL, NULL, NULL, 0, 0, NULL, NULL,
                           NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_nfs_export_fs(c, NULL, NULL, badf, NULL, NULL, 0, 0, NULL, NULL,
                           NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_nfs_export_fs(c, NULL, NULL, f, badf, NULL, 0, 0, NULL, NULL, NULL,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_nfs_export_fs(c, NULL, NULL, f, f, badf, 0, 0, NULL, NULL, NULL,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_nfs_export_fs(c, NULL, NULL, f, f, f, 0, 0, NULL, NULL, NULL,
                           LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_nfs_export_delete(c, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT, "rc = %d", rc);

    rc = lsm_volume_record_free(new_vol);
    new_vol = NULL;
    ck_assert_msg(rc == LSM_ERR_OK, "rc = %d", rc);

    rc = lsm_volume_record_free(resized);
    resized = NULL;
    ck_assert_msg(rc == LSM_ERR_OK, "rc = %d", rc);

    rc = lsm_system_record_array_free(sys, num_systems);
    ck_assert_msg(LSM_ERR_OK == rc, "%d", rc);

    rc = lsm_pool_record_free(test_pool);
    ck_assert_msg(LSM_ERR_OK == rc, "%d", rc);

    G(rc, lsm_system_record_free, system);
    system = NULL;
    G(rc, lsm_string_list_free, f);
    f = NULL;
}
END_TEST

static void cap_test(lsm_storage_capabilities *cap, lsm_capability_type t) {
    lsm_capability_value_type supported;
    supported = lsm_capability_get(cap, t);

    ck_assert_msg(lsm_capability_supported(cap, t) != 0,
                  "lsm_capability_supported returned unsupported");
    ck_assert_msg(supported == LSM_CAP_SUPPORTED, "supported = %d for %d",
                  supported, t);
}

START_TEST(test_capabilities) {
    int rc = 0;

    lsm_system **sys = NULL;
    uint32_t sys_count = 0;
    lsm_storage_capabilities *cap = NULL;

    G(rc, lsm_system_list, c, &sys, &sys_count, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(sys_count >= 1, "count = %d", sys_count);

    if (sys_count > 0) {
        G(rc, lsm_capabilities, c, sys[0], &cap, LSM_CLIENT_FLAG_RSVD);

        if (LSM_ERR_OK == rc) {
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
            cap_test(cap, LSM_CAP_VOLUME_ENABLE);
            cap_test(cap, LSM_CAP_VOLUME_DISABLE);
            cap_test(cap, LSM_CAP_VOLUME_MASK);
            cap_test(cap, LSM_CAP_VOLUME_UNMASK);
            cap_test(cap, LSM_CAP_ACCESS_GROUPS);
            cap_test(cap, LSM_CAP_ACCESS_GROUP_CREATE_WWPN);
            cap_test(cap, LSM_CAP_ACCESS_GROUP_INITIATOR_ADD_WWPN);
            cap_test(cap, LSM_CAP_ACCESS_GROUP_INITIATOR_DELETE);
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
            cap_test(cap, LSM_CAP_FS_SNAPSHOT_DELETE);
            cap_test(cap, LSM_CAP_FS_SNAPSHOT_RESTORE);
            cap_test(cap, LSM_CAP_FS_SNAPSHOT_RESTORE_SPECIFIC_FILES);
            cap_test(cap, LSM_CAP_FS_CHILD_DEPENDENCY);
            cap_test(cap, LSM_CAP_FS_CHILD_DEPENDENCY_RM);
            cap_test(cap, LSM_CAP_FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES);
            cap_test(cap, LSM_CAP_EXPORT_AUTH);
            cap_test(cap, LSM_CAP_EXPORTS);
            cap_test(cap, LSM_CAP_EXPORT_FS);
            cap_test(cap, LSM_CAP_EXPORT_REMOVE);

            G(rc, lsm_capability_record_free, cap);
            cap = NULL;
        }

        G(rc, lsm_system_record_array_free, sys, sys_count);
    }
}
END_TEST

START_TEST(test_iscsi_auth_in) {
    lsm_access_group *group = NULL;
    lsm_system *system = NULL;
    int rc = 0;

    system = get_system(c);
    printf("get_system() OK\n");

    G(rc, lsm_access_group_create, c, "ISCSI_AUTH", ISCSI_HOST[0],
      LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN, system, &group,
      LSM_CLIENT_FLAG_RSVD);
    printf("lsm_access_group_create() OK\n");

    ck_assert_msg(LSM_ERR_OK == rc, "rc = %d", rc);
    G(rc, lsm_system_record_free, system);
    printf("lsm_system_record_free() OK\n");

    system = NULL;

    if (LSM_ERR_OK == rc) {

        rc = lsm_iscsi_chap_auth(c, ISCSI_HOST[0], "username", "secret", NULL,
                                 NULL, LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(LSM_ERR_OK == rc, "rc = %d", rc);

        rc = lsm_access_group_delete(c, group, LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(LSM_ERR_OK == rc, "rc = %d", rc);

        lsm_access_group_record_free(group);
        group = NULL;
    }
}
END_TEST

START_TEST(test_plugin_info) {
    char *desc = NULL;
    char *version = NULL;
    int rc = 0;

    G(rc, lsm_plugin_info_get, c, &desc, &version, LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_OK == rc) {
        printf("Desc: (%s), Version: (%s)\n", desc, version);
        free(desc);
        free(version);
    }

    rc = lsm_plugin_info_get(NULL, &desc, &version, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc = %d", rc);

    rc = lsm_plugin_info_get(c, NULL, &version, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc = %d", rc);

    rc = lsm_plugin_info_get(c, &desc, NULL, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(LSM_ERR_INVALID_ARGUMENT == rc, "rc = %d", rc);
}
END_TEST

START_TEST(test_system_fw_version) {
    const char *fw_ver = NULL;
    int rc = 0;
    lsm_system **sys = NULL;
    uint32_t sys_count = 0;

    G(rc, lsm_system_list, c, &sys, &sys_count, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(sys_count >= 1, "count = %d", sys_count);

    fw_ver = lsm_system_fw_version_get(sys[0]);
    ck_assert_msg(fw_ver != NULL, "Got unexpected NULL return from "
                                  "lsm_system_fw_version_get()");

    fw_ver = lsm_system_fw_version_get(NULL);
    ck_assert_msg(fw_ver == NULL, "Got unexpected non-NULL return from "
                                  "lsm_system_fw_version_get(NULL)");

    lsm_system_record_array_free(sys, sys_count);
}
END_TEST

START_TEST(test_system_mode) {
    lsm_system_mode_type mode = LSM_SYSTEM_MODE_NO_SUPPORT;
    int rc = 0;
    lsm_system **sys = NULL;
    uint32_t sys_count = 0;

    G(rc, lsm_system_list, c, &sys, &sys_count, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(sys_count >= 1, "count = %d", sys_count);

    mode = lsm_system_mode_get(sys[0]);

    ck_assert_msg(mode != LSM_SYSTEM_MODE_UNKNOWN,
                  "Got unexpected LSM_SYSTEM_MODE_UNKNOWN from "
                  "lsm_system_mode_get()");

    mode = lsm_system_mode_get(NULL);
    ck_assert_msg(mode == LSM_SYSTEM_MODE_UNKNOWN,
                  "Got unexpected return %d from "
                  "lsm_system_mode_get(NULL)",
                  mode);

    lsm_system_record_array_free(sys, sys_count);
}
END_TEST

START_TEST(test_read_cache_pct) {
    int read_cache_pct = LSM_SYSTEM_READ_CACHE_PCT_NO_SUPPORT;
    int rc = 0;
    lsm_system **sys = NULL;
    uint32_t sys_count = 0;

    G(rc, lsm_system_list, c, &sys, &sys_count, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(sys_count >= 1, "count = %d", sys_count);

    read_cache_pct = lsm_system_read_cache_pct_get(sys[0]);

    ck_assert_msg(read_cache_pct >= 0,
                  "Read cache should bigger that 0, but got %d",
                  read_cache_pct);

    read_cache_pct = lsm_system_read_cache_pct_get(NULL);
    ck_assert_msg(LSM_SYSTEM_READ_CACHE_PCT_UNKNOWN == read_cache_pct,
                  "When input system pointer is NULL, "
                  "lsm_system_read_cache_pct_get() should return "
                  "LSM_SYSTEM_READ_CACHE_PCT_UNKNOWN, but got %d",
                  read_cache_pct);

    lsm_system_record_array_free(sys, sys_count);
}
END_TEST

START_TEST(test_read_cache_pct_update) {
    int rc = 0;
    lsm_system **sys = NULL;
    uint32_t sys_count = 0;
    int pct = 0;

    G(rc, lsm_system_list, c, &sys, &sys_count, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(sys_count >= 1, "count = %d", sys_count);

    G(rc, lsm_system_read_cache_pct_update, c, sys[0], 100,
      LSM_CLIENT_FLAG_RSVD);
    lsm_system_record_array_free(sys, sys_count);
    G(rc, lsm_system_list, c, &sys, &sys_count, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(sys_count >= 1, "count = %d", sys_count);

    pct = lsm_system_read_cache_pct_get(sys[0]);
    printf("Got pct %d\n", pct);
    ck_assert_msg(pct == 100,
                  "Failed to set system read cache to 100, got %d, "
                  "but got error message.",
                  pct);

    printf("Read cache percentage changed\n");

    /* Test INVALID argument */
    rc = lsm_system_read_cache_pct_update(c, sys[0], -1, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "Expecting LSM_ERR_INVALID_ARGUMENT, but got %d", rc);
    rc = lsm_system_read_cache_pct_update(c, sys[0], 101, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "Expecting LSM_ERR_INVALID_ARGUMENT, but got %d", rc);
    rc = lsm_system_read_cache_pct_update(c, NULL, 10, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "Expecting LSM_ERR_INVALID_ARGUMENT, but got %d", rc);

    lsm_system_record_array_free(sys, sys_count);
}
END_TEST

START_TEST(test_get_available_plugins) {
    int i = 0;
    int num = 0;
    lsm_string_list *plugins = NULL;
    int rc = 0;

    G(rc, lsm_available_plugins_list, ":", &plugins, 0);

    num = lsm_string_list_size(plugins);
    for (i = 0; i < num; i++) {
        const char *info = lsm_string_list_elem_get(plugins, i);
        ck_assert_msg(strlen(info) > 0, "%lu", strlen(info));
        printf("%s\n", info);
    }

    G(rc, lsm_string_list_free, plugins);
    plugins = NULL;
}
END_TEST

START_TEST(test_error_reporting) {
    uint8_t d[4] = {0x00, 0x01, 0x02, 0x03};
    char msg[] = "Testing Errors";
    char exception[] = "Exception text";
    char debug_msg[] = "Debug message";
    void *debug_data = NULL;
    uint32_t debug_size = 0;

    lsm_error_ptr e = lsm_error_create(LSM_ERR_LIB_BUG, msg, exception,
                                       debug_msg, d, sizeof(d));

    ck_assert_msg(e != NULL, "%p", e);

    if (e) {
        ck_assert_msg(LSM_ERR_LIB_BUG == lsm_error_number_get(e), "%d",
                      lsm_error_number_get(e));
        ASSERT_STR_MATCH(msg, lsm_error_message_get(e));
        ASSERT_STR_MATCH(exception, lsm_error_exception_get(e));
        ASSERT_STR_MATCH(debug_msg, lsm_error_debug_get(e));
        debug_data = lsm_error_debug_data_get(e, &debug_size);
        ck_assert_msg(debug_data != NULL, "%p", debug_data);
        ck_assert_msg(debug_size == sizeof(d), "%u", debug_size);
        if (debug_data && (debug_size == sizeof(d)))
            ck_assert_msg(memcmp(d, debug_data, debug_size) == 0,
                          "memcmp fail");
        int free_rc = lsm_error_free(e);
        ck_assert_msg(LSM_ERR_OK == free_rc, "%d", free_rc);
    }
}
END_TEST

START_TEST(test_capability) {
    int rc;
    size_t i;
    lsm_capability_type expected_present[] = {
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
        LSM_CAP_VOLUME_ENABLE,
        LSM_CAP_VOLUME_DISABLE,
        LSM_CAP_VOLUME_MASK,
        LSM_CAP_VOLUME_UNMASK,
        LSM_CAP_ACCESS_GROUPS,
        LSM_CAP_ACCESS_GROUP_CREATE_WWPN,
        LSM_CAP_ACCESS_GROUP_INITIATOR_ADD_WWPN,
        LSM_CAP_ACCESS_GROUP_INITIATOR_DELETE,
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
        LSM_CAP_FS_SNAPSHOT_DELETE,
        LSM_CAP_FS_SNAPSHOT_RESTORE,
        LSM_CAP_FS_SNAPSHOT_RESTORE_SPECIFIC_FILES,
        LSM_CAP_FS_CHILD_DEPENDENCY,
        LSM_CAP_FS_CHILD_DEPENDENCY_RM,
        LSM_CAP_FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES,
        LSM_CAP_EXPORT_AUTH,
        LSM_CAP_EXPORTS,
        LSM_CAP_EXPORT_FS,
        LSM_CAP_EXPORT_REMOVE};

    lsm_storage_capabilities *cap = lsm_capability_record_alloc(NULL);

    ck_assert_msg(cap != NULL, "cap = %p", cap);

    if (cap) {
        G(rc, lsm_capability_set_n, cap, LSM_CAP_SUPPORTED, LSM_CAP_VOLUMES,
          LSM_CAP_VOLUME_CREATE, LSM_CAP_VOLUME_RESIZE,
          LSM_CAP_VOLUME_REPLICATE, LSM_CAP_VOLUME_REPLICATE_CLONE,
          LSM_CAP_VOLUME_REPLICATE_COPY, LSM_CAP_VOLUME_REPLICATE_MIRROR_ASYNC,
          LSM_CAP_VOLUME_REPLICATE_MIRROR_SYNC,
          LSM_CAP_VOLUME_COPY_RANGE_BLOCK_SIZE, LSM_CAP_VOLUME_COPY_RANGE,
          LSM_CAP_VOLUME_COPY_RANGE_CLONE, LSM_CAP_VOLUME_COPY_RANGE_COPY,
          LSM_CAP_VOLUME_DELETE, LSM_CAP_VOLUME_ENABLE, LSM_CAP_VOLUME_DISABLE,
          LSM_CAP_VOLUME_MASK, LSM_CAP_VOLUME_UNMASK, LSM_CAP_ACCESS_GROUPS,
          LSM_CAP_ACCESS_GROUP_CREATE_WWPN,
          LSM_CAP_ACCESS_GROUP_INITIATOR_ADD_WWPN,
          LSM_CAP_ACCESS_GROUP_INITIATOR_DELETE,
          LSM_CAP_VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP,
          LSM_CAP_ACCESS_GROUPS_GRANTED_TO_VOLUME,
          LSM_CAP_VOLUME_CHILD_DEPENDENCY, LSM_CAP_VOLUME_CHILD_DEPENDENCY_RM,
          LSM_CAP_FS, LSM_CAP_FS_DELETE, LSM_CAP_FS_RESIZE, LSM_CAP_FS_CREATE,
          LSM_CAP_FS_CLONE, LSM_CAP_FILE_CLONE, LSM_CAP_FS_SNAPSHOTS,
          LSM_CAP_FS_SNAPSHOT_CREATE, LSM_CAP_FS_SNAPSHOT_DELETE,
          LSM_CAP_FS_SNAPSHOT_RESTORE,
          LSM_CAP_FS_SNAPSHOT_RESTORE_SPECIFIC_FILES,
          LSM_CAP_FS_CHILD_DEPENDENCY, LSM_CAP_FS_CHILD_DEPENDENCY_RM,
          LSM_CAP_FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES, LSM_CAP_EXPORT_AUTH,
          LSM_CAP_EXPORTS, LSM_CAP_EXPORT_FS, LSM_CAP_EXPORT_REMOVE, -1);

        G(rc, lsm_capability_set, cap, LSM_CAP_EXPORTS, LSM_CAP_SUPPORTED);

        for (i = 0; i < sizeof(expected_present) / sizeof(expected_present[0]);
             ++i) {

            ck_assert_msg(lsm_capability_get(cap, expected_present[i]) ==
                              LSM_CAP_SUPPORTED,
                          "%d",
                          (int)lsm_capability_get(cap, expected_present[i]));
        }

        G(rc, lsm_capability_record_free, cap);
    }
}
END_TEST

START_TEST(test_nfs_export_funcs) {
    const char id[] = "export_unique_id";
    const char fs_id[] = "fs_unique_id";
    const char export_path[] = "/mnt/foo";
    const char auth[] = "simple";
    uint64_t anonuid = 1021;
    uint64_t anongid = 1000;
    const char options[] = "vendor_specific_option";
    const char p_data[] = "plug-in private data";
    char rstring[33];
    int rc = 0;

    lsm_string_list *root = lsm_string_list_alloc(0);
    G(rc, lsm_string_list_append, root, "192.168.100.2");
    G(rc, lsm_string_list_append, root, "192.168.100.3");

    lsm_string_list *rw = lsm_string_list_alloc(0);
    G(rc, lsm_string_list_append, rw, "192.168.100.2");
    G(rc, lsm_string_list_append, rw, "192.168.100.3");

    lsm_string_list *rand = lsm_string_list_alloc(0);

    lsm_string_list *ro = lsm_string_list_alloc(0);
    G(rc, lsm_string_list_append, ro, "*");

    lsm_nfs_export *export =
        lsm_nfs_export_record_alloc(id, fs_id, export_path, auth, root, rw, ro,
                                    anonuid, anongid, options, p_data);

    lsm_nfs_export *copy = lsm_nfs_export_record_copy(export);

    ASSERT_STR_MATCH(lsm_nfs_export_id_get(copy), id);
    ASSERT_STR_MATCH(lsm_nfs_export_fs_id_get(copy), fs_id);
    ASSERT_STR_MATCH(lsm_nfs_export_export_path_get(copy), export_path);
    ASSERT_STR_MATCH(lsm_nfs_export_auth_type_get(copy), auth);
    ASSERT_STR_MATCH(lsm_nfs_export_options_get(copy), options);

    ck_assert_msg(lsm_nfs_export_anon_uid_get(copy) == anonuid, "%lu",
                  lsm_nfs_export_anon_uid_get(copy));
    ck_assert_msg(lsm_nfs_export_anon_gid_get(copy) == anongid, "%lu",
                  lsm_nfs_export_anon_gid_get(copy));

    ck_assert_msg(compare_string_lists(lsm_nfs_export_root_get(export),
                                       lsm_nfs_export_root_get(copy)) == 0,
                  "lists failed to compare");
    ck_assert_msg(compare_string_lists(lsm_nfs_export_read_write_get(export),
                                       lsm_nfs_export_read_write_get(copy)) ==
                      0,
                  "lists failed to compare");
    ck_assert_msg(compare_string_lists(lsm_nfs_export_read_only_get(export),
                                       lsm_nfs_export_read_only_get(copy)) == 0,
                  "lists failed to compare");

    G(rc, lsm_nfs_export_record_free, copy);

    generate_random(rstring, sizeof(rstring));
    G(rc, lsm_nfs_export_id_set, export, rstring);
    ASSERT_STR_MATCH(lsm_nfs_export_id_get(export), rstring);

    generate_random(rstring, sizeof(rstring));
    G(rc, lsm_nfs_export_fs_id_set, export, rstring);
    ASSERT_STR_MATCH(lsm_nfs_export_fs_id_get(export), rstring);

    generate_random(rstring, sizeof(rstring));
    G(rc, lsm_nfs_export_export_path_set, export, rstring);
    ASSERT_STR_MATCH(lsm_nfs_export_export_path_get(export), rstring);

    generate_random(rstring, sizeof(rstring));
    G(rc, lsm_nfs_export_auth_type_set, export, rstring);
    ASSERT_STR_MATCH(lsm_nfs_export_auth_type_get(export), rstring);

    generate_random(rstring, sizeof(rstring));
    G(rc, lsm_nfs_export_options_set, export, rstring);
    ASSERT_STR_MATCH(lsm_nfs_export_options_get(export), rstring);

    anonuid = anonuid + 700;
    G(rc, lsm_nfs_export_anon_uid_set, export, anonuid);

    anongid = anongid + 400;
    G(rc, lsm_nfs_export_anon_gid_set, export, anongid);

    ck_assert_msg(lsm_nfs_export_anon_uid_get(export) == anonuid, "%lu",
                  lsm_nfs_export_anon_uid_get(export));
    ck_assert_msg(lsm_nfs_export_anon_gid_get(export) == anongid, "%lu",
                  lsm_nfs_export_anon_gid_get(export));

    generate_random(rstring, sizeof(rstring));
    G(rc, lsm_string_list_append, rand, rstring);
    G(rc, lsm_nfs_export_root_set, export, rand);
    ck_assert_msg(compare_string_lists(lsm_nfs_export_root_get(export), rand) ==
                      0,
                  "lists failed to compare");

    generate_random(rstring, sizeof(rstring));
    G(rc, lsm_string_list_append, rand, rstring);
    G(rc, lsm_nfs_export_read_write_set, export, rand);
    ck_assert_msg(
        compare_string_lists(lsm_nfs_export_read_write_get(export), rand) == 0,
        "lists failed to compare");

    generate_random(rstring, sizeof(rstring));
    G(rc, lsm_string_list_append, rand, rstring);
    G(rc, lsm_nfs_export_read_only_set, export, rand);
    ck_assert_msg(
        compare_string_lists(lsm_nfs_export_read_only_get(export), rand) == 0,
        "lists failed to compare");

    G(rc, lsm_nfs_export_record_free, export);
    export = NULL;
    G(rc, lsm_string_list_free, root);
    root = NULL;
    G(rc, lsm_string_list_free, rw);
    rw = NULL;
    G(rc, lsm_string_list_free, ro);
    ro = NULL;
    G(rc, lsm_string_list_free, rand);
    rand = NULL;
}
END_TEST

START_TEST(test_uri_parse) {
    const char uri_g[] = "sim://user@host:123/path/?namespace=root/uber";
    const char uri_no_path[] = "smis://user@host?namespace=root/emc";
    char *scheme = NULL;
    char *user = NULL;
    char *server = NULL;
    char *path = NULL;
    int port = 0;
    lsm_hash *qp = NULL;
    int rc = 0;

    G(rc, lsm_uri_parse, uri_g, &scheme, &user, &server, &port, &path, &qp);

    if (LSM_ERR_OK == rc) {
        ck_assert_msg(strcmp(scheme, "sim") == 0, "%s", scheme);
        ck_assert_msg(strcmp(user, "user") == 0, "%s", user);
        ck_assert_msg(strcmp(server, "host") == 0, "%s", server);
        ck_assert_msg(strcmp(path, "/path/") == 0, "%s", path);
        ck_assert_msg(port == 123, "%d", port);

        ck_assert_msg(qp != NULL, "qp = %p", qp);
        if (qp) {
            ck_assert_msg(
                strcmp("root/uber", lsm_hash_string_get(qp, "namespace")) == 0,
                "%s", lsm_hash_string_get(qp, "namespace"));
        }

        free(scheme);
        scheme = NULL;
        free(user);
        user = NULL;
        free(server);
        server = NULL;
        free(path);
        path = NULL;
        G(rc, lsm_hash_free, qp);
        qp = NULL;
    }

    port = 0;

    G(rc, lsm_uri_parse, uri_no_path, &scheme, &user, &server, &port, &path,
      &qp);

    if (LSM_ERR_OK == rc) {
        ck_assert_msg(strcmp(scheme, "smis") == 0, "%s", scheme);
        ck_assert_msg(strcmp(user, "user") == 0, "%s", user);
        ck_assert_msg(strcmp(server, "host") == 0, "%s", server);
        ck_assert_msg(path == NULL, "%s", path);
        ck_assert_msg(port == 0, "%d", port);

        ck_assert_msg(qp != NULL, "qp = %p", qp);
        if (qp) {
            ck_assert_msg(
                strcmp("root/emc", lsm_hash_string_get(qp, "namespace")) == 0,
                "%s", lsm_hash_string_get(qp, "namespace"));
        }

        free(scheme);
        scheme = NULL;
        free(user);
        user = NULL;
        free(server);
        server = NULL;
        G(rc, lsm_hash_free, qp);
        qp = NULL;
    }
}
END_TEST

START_TEST(test_search_pools) {
    int rc;
    lsm_pool **pools = NULL;
    uint32_t poolCount = 0;

    G(rc, lsm_pool_list, c, NULL, NULL, &pools, &poolCount,
      LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_OK == rc && poolCount) {
        lsm_pool **search_pools = NULL;
        uint32_t search_count = 0;

        G(rc, lsm_pool_list, c, "id", lsm_pool_id_get(pools[0]), &search_pools,
          &search_count, LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(search_count == 1, "Expecting 1 pool, got %d",
                      search_count);

        G(rc, lsm_pool_record_array_free, search_pools, search_count);

        /* Search for non-existent pool*/
        search_pools = NULL;
        search_count = 0;

        G(rc, lsm_pool_list, c, "id", "non-existent-id", &search_pools,
          &search_count, LSM_CLIENT_FLAG_RSVD);
        ck_assert_msg(search_count == 0, "Expecting no pools! %d",
                      search_count);

        /* Search which results in all pools */
        G(rc, lsm_pool_list, c, "system_id", lsm_pool_system_id_get(pools[0]),
          &search_pools, &search_count, LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(search_count == poolCount, "Expecting %d pools, got %d",
                      poolCount, search_count);

        G(rc, lsm_pool_record_array_free, search_pools, search_count);
        search_pools = NULL;
        search_count = 0;

        G(rc, lsm_pool_record_array_free, pools, poolCount);
        pools = NULL;
        poolCount = 0;
    }
}
END_TEST

START_TEST(test_search_volumes) {
    int rc;
    lsm_volume **volumes = NULL;
    uint32_t volume_count = 0;

    lsm_pool *pool = get_test_pool(c);

    // Make some volumes to we can actually filter
    create_volumes(c, pool, 10);

    G(rc, lsm_volume_list, c, NULL, NULL, &volumes, &volume_count,
      LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(volume_count > 0, "We are expecting some volumes!");

    if (LSM_ERR_OK == rc && volume_count) {
        lsm_volume **search_volume = NULL;
        uint32_t search_count = 0;

        G(rc, lsm_volume_list, c, "id", lsm_volume_id_get(volumes[0]),
          &search_volume, &search_count, LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(search_count == 1, "Expecting 1 pool, got %d",
                      search_count);

        G(rc, lsm_volume_record_array_free, search_volume, search_count);
        search_volume = NULL;
        search_count = 0;

        /* Search for non-existent */
        G(rc, lsm_volume_list, c, "id", "non-existent-id", &search_volume,
          &search_count, LSM_CLIENT_FLAG_RSVD);
        ck_assert_msg(search_count == 0, "Expecting no volumes! %d",
                      search_count);

        /* Search which results in all volumes */
        G(rc, lsm_volume_list, c, "system_id",
          lsm_volume_system_id_get(volumes[0]), &search_volume, &search_count,
          LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(search_count == volume_count,
                      "Expecting %d volumes, got %d", volume_count,
                      search_count);

        G(rc, lsm_volume_record_array_free, search_volume, search_count);
        search_volume = NULL;
        search_count = 0;

        G(rc, lsm_volume_record_array_free, volumes, volume_count);
        volumes = NULL;
        volume_count = 0;
    }

    G(rc, lsm_pool_record_free, pool);
    pool = NULL;
}
END_TEST

START_TEST(test_search_disks) {
    int rc;
    lsm_disk **disks = NULL;
    uint32_t disk_count = 0;

    lsm_pool *pool = get_test_pool(c);

    G(rc, lsm_disk_list, c, NULL, NULL, &disks, &disk_count,
      LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(disk_count > 0, "We are expecting some disks!");

    if (LSM_ERR_OK == rc && disk_count) {

        lsm_disk **search_disks = NULL;
        uint32_t search_count = 0;

        G(rc, lsm_disk_list, c, "id", lsm_disk_id_get(disks[0]), &search_disks,
          &search_count, LSM_CLIENT_FLAG_RSVD);
        ck_assert_msg(search_count == 1, "Expecting 1 disk, got %d",
                      search_count);

        G(rc, lsm_disk_record_array_free, search_disks, search_count);
        search_disks = NULL;
        search_count = 0;

        /* Search for non-existent */
        G(rc, lsm_disk_list, c, "id", "non-existent-id", &search_disks,
          &search_count, LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(search_count == 0, "Expecting no disks! %d",
                      search_count);

        /* Search which results in all disks */
        G(rc, lsm_disk_list, c, "system_id", lsm_disk_system_id_get(disks[0]),
          &search_disks, &search_count, LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(search_count == disk_count, "Expecting %d disks, got %d",
                      disk_count, search_count);

        G(rc, lsm_disk_record_array_free, search_disks, search_count);
        G(rc, lsm_disk_record_array_free, disks, disk_count);
        disks = NULL;
        disk_count = 0;
    }

    lsm_pool_record_free(pool);
}
END_TEST

START_TEST(test_search_access_groups) {
    int rc;
    lsm_access_group **ag = NULL;
    uint32_t count = 0;
    int i = 0;
    lsm_access_group *group = NULL;

    lsm_pool *pool = get_test_pool(c);
    lsm_system *system = get_system(c);

    ck_assert_msg(system != NULL, "Missing system!");

    for (i = 0; i < 2; ++i) {
        char ag_name[64];

        snprintf(ag_name, sizeof(ag_name), "test_access_group_%d", i);

        G(rc, lsm_access_group_create, c, ag_name, ISCSI_HOST[i],
          LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN, system, &group,
          LSM_CLIENT_FLAG_RSVD);

        if (LSM_ERR_OK == rc) {
            G(rc, lsm_access_group_record_free, group);
            group = NULL;
        }
    }

    G(rc, lsm_system_record_free, system);
    system = NULL;

    G(rc, lsm_access_group_list, c, NULL, NULL, &ag, &count,
      LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(count > 0, "We are expecting some access_groups!");

    if (LSM_ERR_OK == rc && count) {

        lsm_access_group **search_ag = NULL;
        uint32_t search_count = 0;

        G(rc, lsm_access_group_list, c, "id", lsm_access_group_id_get(ag[0]),
          &search_ag, &search_count, LSM_CLIENT_FLAG_RSVD);
        ck_assert_msg(search_count == 1, "Expecting 1 access group, got %d",
                      search_count);

        G(rc, lsm_access_group_record_array_free, search_ag, search_count);

        /* Search for non-existent */
        search_ag = NULL;
        search_count = 0;

        G(rc, lsm_access_group_list, c, "id", "non-existent-id", &search_ag,
          &search_count, LSM_CLIENT_FLAG_RSVD);
        ck_assert_msg(search_count == 0, "Expecting no access groups! %d",
                      search_count);

        /* Search which results in all disks */
        G(rc, lsm_access_group_list, c, "system_id",
          lsm_access_group_system_id_get(ag[0]), &search_ag, &search_count,
          LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(search_count == count,
                      "Expecting %d access groups, got %d", count,
                      search_count);

        G(rc, lsm_access_group_record_array_free, search_ag, search_count);
        search_ag = NULL;
        search_count = 0;

        G(rc, lsm_access_group_record_array_free, ag, count);
        ag = NULL;
        count = 0;
    }

    G(rc, lsm_pool_record_free, pool);
    pool = NULL;
}
END_TEST

START_TEST(test_search_fs) {
    int rc;
    lsm_fs **fsl = NULL;
    lsm_fs *fs = NULL;
    uint32_t count = 0;
    int i = 0;
    char *job = NULL;

    lsm_pool *pool = get_test_pool(c);

    for (i = 0; i < 2; ++i) {
        char fs_name[64];

        snprintf(fs_name, sizeof(fs_name), "test_fs_%d", i);

        rc = lsm_fs_create(c, pool, fs_name, 50000000, &fs, &job,
                           LSM_CLIENT_FLAG_RSVD);

        if (LSM_ERR_JOB_STARTED == rc) {
            ck_assert_msg(NULL == fs, "fs = %p", fs);
            fs = wait_for_job_fs(c, &job);
        } else {
            ck_assert_msg(LSM_ERR_OK == rc, "rc = %d", rc);
        }

        G(rc, lsm_fs_record_free, fs);
        fs = NULL;
    }

    G(rc, lsm_fs_list, c, NULL, NULL, &fsl, &count, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(count > 0, "We are expecting some file systems!");

    if (LSM_ERR_OK == rc && count) {

        lsm_fs **search_fs = NULL;
        uint32_t search_count = 0;

        G(rc, lsm_fs_list, c, "id", lsm_fs_id_get(fsl[0]), &search_fs,
          &search_count, LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(search_count == 1, "Expecting 1 fs, got %d",
                      search_count);

        G(rc, lsm_fs_record_array_free, search_fs, search_count);
        search_fs = NULL;
        search_count = 0;

        /* Search for non-existent */
        G(rc, lsm_fs_list, c, "id", "non-existent-id", &search_fs,
          &search_count, LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(search_count == 0, "Expecting no fs! %d", search_count);

        /* Search which results in all disks */
        G(rc, lsm_fs_list, c, "system_id", lsm_fs_system_id_get(fsl[0]),
          &search_fs, &search_count, LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(search_count == count, "Expecting %d fs, got %d", count,
                      search_count);

        G(rc, lsm_fs_record_array_free, search_fs, search_count);

        G(rc, lsm_fs_record_array_free, fsl, count);
        fsl = NULL;
        count = 0;
    }

    lsm_pool_record_free(pool);
}
END_TEST

static void verify_string(const char *method, const char *value) {
    ck_assert_msg(method != NULL, "%s rc is NULL", method);
    if (value) {
        ck_assert_msg(strlen(value) > 0, "%s string len = 0", method);
    }
}

START_TEST(test_target_ports) {
    lsm_target_port **tp = NULL;
    uint32_t count = 0;
    uint32_t i = 0;
    int rc = 0;

    G(rc, lsm_target_port_list, c, NULL, NULL, &tp, &count,
      LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_OK == rc) {
        for (i = 0; i < count; ++i) {
            verify_string("lsm_target_port_id_get",
                          lsm_target_port_id_get(tp[i]));

            int pt = (int)lsm_target_port_type_get(tp[i]);
            ck_assert_msg(pt >= 0 && pt <= 4, "%d", pt);

            verify_string("lsm_target_port_service_address_get",
                          lsm_target_port_service_address_get(tp[i]));

            verify_string("lsm_target_port_network_address_get",
                          lsm_target_port_network_address_get(tp[i]));

            verify_string("lsm_target_port_physical_address_get",
                          lsm_target_port_physical_address_get(tp[i]));

            verify_string("lsm_target_port_physical_name_get",
                          lsm_target_port_physical_name_get(tp[i]));

            verify_string("lsm_target_port_system_id_get",
                          lsm_target_port_system_id_get(tp[i]));
        }

        {
            lsm_target_port **search = NULL;
            uint32_t search_count = 0;

            G(rc, lsm_target_port_list, c, "id", "does_not_exist", &search,
              &search_count, LSM_CLIENT_FLAG_RSVD);
            ck_assert_msg(search_count == 0, "%d", search_count);

            G(rc, lsm_target_port_list, c, "system_id", "sim-01", &search,
              &search_count, LSM_CLIENT_FLAG_RSVD);

            ck_assert_msg(search_count == 5, "%d", search_count);
            if (search_count) {
                G(rc, lsm_target_port_record_array_free, search, search_count);
            }
        }

        G(rc, lsm_target_port_record_array_free, tp, count);
    }
}
END_TEST

START_TEST(test_initiator_id_verification) {
    int rc = 0;
    lsm_access_group *group = NULL;
    lsm_access_group *updated_group = NULL;
    lsm_access_group **groups = NULL;
    uint32_t count = 0;
    lsm_system *system = get_system(c);

    G(rc, lsm_access_group_list, c, NULL, NULL, &groups, &count,
      LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(count == 0, "Expect 0 access groups, got %" PRIu32, count);
    ck_assert_msg(groups == NULL, "groups = %p", groups);

    /* Test valid iqns first, then invalid */

    G(rc, lsm_access_group_create, c, "test_ag_iscsi",
      "iqn.1994-05.com.domain.sub:whatever-the.users_wants",
      LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN, system, &group,
      LSM_CLIENT_FLAG_RSVD);

    G(rc, lsm_access_group_initiator_add, c, group,
      "iqn.2001-04.com.example:storage:diskarrays-sn-a8675309",
      LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN, &updated_group,
      LSM_CLIENT_FLAG_RSVD);

    G(rc, lsm_access_group_record_free, group);
    group = updated_group;
    updated_group = NULL;

    G(rc, lsm_access_group_initiator_add, c, group, "iqn.2001-04.com.example",
      LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN, &updated_group,
      LSM_CLIENT_FLAG_RSVD);

    G(rc, lsm_access_group_record_free, group);
    group = updated_group;
    updated_group = NULL;

    G(rc, lsm_access_group_initiator_add, c, group,
      "iqn.2001-04.com.example:storage.tape1.sys1.xyz",
      LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN, &updated_group,
      LSM_CLIENT_FLAG_RSVD);

    G(rc, lsm_access_group_record_free, group);
    group = updated_group;
    updated_group = NULL;

    G(rc, lsm_access_group_initiator_add, c, group,
      "iqn.2001-04.com.example:storage.disk2.sys1.xyz",
      LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN, &updated_group,
      LSM_CLIENT_FLAG_RSVD);

    G(rc, lsm_access_group_record_free, group);
    group = updated_group;
    updated_group = NULL;

    G(rc, lsm_access_group_initiator_add, c, group, "0x0011223344556677",
      LSM_ACCESS_GROUP_INIT_TYPE_WWPN, &updated_group, LSM_CLIENT_FLAG_RSVD);

    G(rc, lsm_access_group_record_free, group);
    group = updated_group;
    updated_group = NULL;

    G(rc, lsm_access_group_initiator_add, c, group, "00:11:22:33:44:55:66:78",
      LSM_ACCESS_GROUP_INIT_TYPE_WWPN, &updated_group, LSM_CLIENT_FLAG_RSVD);

    G(rc, lsm_access_group_record_free, group);
    group = updated_group;
    updated_group = NULL;

    G(rc, lsm_access_group_initiator_add, c, group, "00-11-22-33-44-55-66-79",
      LSM_ACCESS_GROUP_INIT_TYPE_WWPN, &updated_group, LSM_CLIENT_FLAG_RSVD);

    G(rc, lsm_access_group_record_free, group);
    group = updated_group;
    updated_group = NULL;

    G(rc, lsm_access_group_initiator_add, c, group, "0x00-11-22-33-44-55-66-80",
      LSM_ACCESS_GROUP_INIT_TYPE_WWPN, &updated_group, LSM_CLIENT_FLAG_RSVD);

    G(rc, lsm_access_group_record_free, group);
    group = updated_group;
    updated_group = NULL;

    /* Test invalid */
    rc = lsm_access_group_initiator_add(c, group, "0x:0011223344556677",
                                        LSM_ACCESS_GROUP_INIT_TYPE_WWPN,
                                        &updated_group, LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "Expected initiator id with invalid form to fail! %d", rc);

    /* Test invalid iqn */
    rc = lsm_access_group_initiator_add(
        c, group, "0011223344556677:", LSM_ACCESS_GROUP_INIT_TYPE_WWPN,
        &updated_group, LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "Expected initiator id with invalid form to fail! %d", rc);

    /* Test invalid iqn */
    rc = lsm_access_group_initiator_add(c, group, "001122334455667788",
                                        LSM_ACCESS_GROUP_INIT_TYPE_WWPN,
                                        &updated_group, LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "Expected initiator id with invalid form to fail! %d", rc);

    /* Test invalid iqn */
    rc = lsm_access_group_initiator_add(c, group, "0x001122334455",
                                        LSM_ACCESS_GROUP_INIT_TYPE_WWPN,
                                        &updated_group, LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "Expected initiator id with invalid form to fail! %d", rc);

    /* Test invalid iqn */
    rc = lsm_access_group_initiator_add(c, group, "0x00+11:22:33:44:55:66:77",
                                        LSM_ACCESS_GROUP_INIT_TYPE_WWPN,
                                        &updated_group, LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "Expected initiator id with invalid form to fail! %d", rc);

    /* Delete group */
    G(rc, lsm_access_group_delete, c, group, LSM_CLIENT_FLAG_RSVD);
    G(rc, lsm_access_group_record_free, group);
    group = NULL;

    G(rc, lsm_system_record_free, system);
    system = NULL;
}
END_TEST

START_TEST(test_volume_vpd_check) {
    int rc;

    F(rc, lsm_volume_vpd83_verify, NULL);
    F(rc, lsm_volume_vpd83_verify, "012345678901234567890123456789AB");
    F(rc, lsm_volume_vpd83_verify, "012345678901234567890123456789ax");
    F(rc, lsm_volume_vpd83_verify, "012345678901234567890123456789ag");
    F(rc, lsm_volume_vpd83_verify, "1234567890123456789012345abcdef");
    F(rc, lsm_volume_vpd83_verify, "01234567890123456789012345abcdefa");
    F(rc, lsm_volume_vpd83_verify, "01234567890123456789012345abcdef");
    F(rc, lsm_volume_vpd83_verify, "55cd2e404beec32e0");
    F(rc, lsm_volume_vpd83_verify, "55cd2e404beec32ex");
    F(rc, lsm_volume_vpd83_verify, "55cd2e404beec32A");
    F(rc, lsm_volume_vpd83_verify, "35cd2e404beec32A");

    G(rc, lsm_volume_vpd83_verify, "61234567890123456789012345abcdef");
    G(rc, lsm_volume_vpd83_verify, "55cd2e404beec32e");
    G(rc, lsm_volume_vpd83_verify, "35cd2e404beec32e");
    G(rc, lsm_volume_vpd83_verify, "25cd2e404beec32e");
}
END_TEST

START_TEST(test_volume_raid_info) {
    lsm_volume *volume = NULL;
    char *job = NULL;
    lsm_pool *pool = get_test_pool(c);

    int rc = lsm_volume_create(c, pool, "volume_raid_info_test", 20000000,
                               LSM_VOLUME_PROVISION_DEFAULT, &volume, &job,
                               LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(rc == LSM_ERR_OK || rc == LSM_ERR_JOB_STARTED,
                  "lsmVolumeCreate %d (%s)", rc, error(lsm_error_last_get(c)));

    if (LSM_ERR_JOB_STARTED == rc) {
        volume = wait_for_job_vol(c, &job);
    }

    lsm_volume_raid_type raid_type;
    uint32_t strip_size, disk_count, min_io_size, opt_io_size;

    G(rc, lsm_volume_raid_info, c, volume, &raid_type, &strip_size, &disk_count,
      &min_io_size, &opt_io_size, LSM_CLIENT_FLAG_RSVD);

    G(rc, lsm_volume_record_free, volume);
    G(rc, lsm_pool_record_free, pool);
    volume = NULL;
}
END_TEST

START_TEST(test_pool_member_info) {
    int rc;
    lsm_pool **pools = NULL;
    uint32_t poolCount = 0;
    G(rc, lsm_pool_list, c, NULL, NULL, &pools, &poolCount,
      LSM_CLIENT_FLAG_RSVD);

    lsm_volume_raid_type raid_type;
    lsm_pool_member_type member_type;
    lsm_string_list *member_ids = NULL;

    uint32_t i;
    uint32_t y;
    for (i = 0; i < poolCount; i++) {
        G(rc, lsm_pool_member_info, c, pools[i], &raid_type, &member_type,
          &member_ids, LSM_CLIENT_FLAG_RSVD);
        for (y = 0; y < lsm_string_list_size(member_ids); y++) {
            // Simulator user reading the member id.
            const char *cur_member_id = lsm_string_list_elem_get(member_ids, y);
            ck_assert_msg(strlen(cur_member_id), "%ld", strlen(cur_member_id));
        }
        lsm_string_list_free(member_ids);
    }
    G(rc, lsm_pool_record_array_free, pools, poolCount);
}
END_TEST

START_TEST(test_volume_raid_create_cap_get) {
    int rc;
    lsm_system **sys = NULL;
    uint32_t sys_count = 0;

    G(rc, lsm_system_list, c, &sys, &sys_count, LSM_CLIENT_FLAG_RSVD);
    ck_assert_msg(sys_count >= 1, "count = %d", sys_count);

    if (sys_count > 0) {
        uint32_t *supported_raid_types = NULL;
        uint32_t supported_raid_type_count = 0;
        uint32_t *supported_strip_sizes = NULL;
        uint32_t supported_strip_size_count = 0;

        G(rc, lsm_volume_raid_create_cap_get, c, sys[0], &supported_raid_types,
          &supported_raid_type_count, &supported_strip_sizes,
          &supported_strip_size_count, 0);

        free(supported_raid_types);
        free(supported_strip_sizes);
    }
    G(rc, lsm_system_record_array_free, sys, sys_count);
}
END_TEST

START_TEST(test_volume_raid_create) {
    int rc;

    lsm_disk **disks = NULL;
    uint32_t disk_count = 0;

    G(rc, lsm_disk_list, c, NULL, NULL, &disks, &disk_count, 0);

    // Try to create two disks RAID 1.
    uint32_t free_disk_count = 0;
    lsm_disk *free_disks[2];
    uint32_t i = 0;
    for (i = 0; i < disk_count; i++) {
        if (lsm_disk_status_get(disks[i]) & LSM_DISK_STATUS_FREE) {
            free_disks[free_disk_count++] = disks[i];
            if (free_disk_count == 2) {
                break;
            }
        }
    }
    ck_assert_msg(free_disk_count == 2, "Failed to find two free disks");

    lsm_volume *new_volume = NULL;

    G(rc, lsm_volume_raid_create, c, "test_volume_raid_create",
      LSM_VOLUME_RAID_TYPE_RAID1, free_disks, free_disk_count,
      LSM_VOLUME_VCR_STRIP_SIZE_DEFAULT, &new_volume, LSM_CLIENT_FLAG_RSVD);

    char *job_del = NULL;
    int del_rc =
        lsm_volume_delete(c, new_volume, &job_del, LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(del_rc == LSM_ERR_OK || del_rc == LSM_ERR_JOB_STARTED,
                  "lsm_volume_delete %d (%s)", rc,
                  error(lsm_error_last_get(c)));

    if (LSM_ERR_JOB_STARTED == del_rc) {
        wait_for_job_vol(c, &job_del);
    }

    G(rc, lsm_disk_record_array_free, disks, disk_count);
    // The new pool should be automatically be deleted when volume got
    // deleted.
    lsm_pool **pools = NULL;
    uint32_t count = 0;
    G(rc, lsm_pool_list, c, "id", lsm_volume_pool_id_get(new_volume), &pools,
      &count, LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(
        count == 0,
        "New HW RAID pool still exists, it should be deleted along with "
        "lsm_volume_delete()");

    lsm_pool_record_array_free(pools, count);

    G(rc, lsm_volume_record_free, new_volume);
}
END_TEST

START_TEST(test_volume_ident_led_on) {
    lsm_volume *volume = NULL;
    char *job = NULL;
    lsm_pool *pool = get_test_pool(c);

    int rc = lsm_volume_create(c, pool, "volume_raid_info_test", 20000000,
                               LSM_VOLUME_PROVISION_DEFAULT, &volume, &job,
                               LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(rc == LSM_ERR_OK || rc == LSM_ERR_JOB_STARTED,
                  "lsmVolumeCreate %d (%s)", rc, error(lsm_error_last_get(c)));

    if (LSM_ERR_JOB_STARTED == rc) {
        volume = wait_for_job_vol(c, &job);
    }

    G(rc, lsm_volume_ident_led_on, c, volume, LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_OK == rc)
        printf("Volume IDENT LED set\n");

    G(rc, lsm_volume_record_free, volume);
    G(rc, lsm_pool_record_free, pool);
}
END_TEST

START_TEST(test_volume_ident_led_off) {
    lsm_volume *volume = NULL;
    char *job = NULL;
    lsm_pool *pool = get_test_pool(c);

    int rc = lsm_volume_create(c, pool, "volume_raid_info_test", 20000000,
                               LSM_VOLUME_PROVISION_DEFAULT, &volume, &job,
                               LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(rc == LSM_ERR_OK || rc == LSM_ERR_JOB_STARTED,
                  "lsmVolumeCreate %d (%s)", rc, error(lsm_error_last_get(c)));

    if (LSM_ERR_JOB_STARTED == rc) {
        volume = wait_for_job_vol(c, &job);
    }

    G(rc, lsm_volume_ident_led_off, c, volume, LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_OK == rc)
        printf("Volume IDENT LED clear\n");

    G(rc, lsm_volume_record_free, volume);
    G(rc, lsm_pool_record_free, pool);
}
END_TEST

/*
 * lsm_local_disk_list() should never fail.
 */
START_TEST(test_local_disk_list) {
    int rc = LSM_ERR_OK;
    lsm_string_list *disk_paths = NULL;
    /* Not initialized in order to test dangling pointer disk_path_list */
    lsm_error *lsm_err = NULL;

    if (is_simc_plugin == 1) {
        /* silently skip on simc, no need for duplicate test. */
        return;
    }

    rc = lsm_local_disk_list(&disk_paths, &lsm_err);
    if (lsm_err)
        lsm_error_free(lsm_err);
    ck_assert_msg(rc == LSM_ERR_OK, "lsm_local_disk_list() failed as %d", rc);
    ck_assert_msg(disk_paths != NULL, "lsm_local_disk_list() return NULL for "
                                      "disk_paths");
    lsm_string_list_free(disk_paths);
}
END_TEST

/*
 * Just check whether LSM_ERR_INVALID_ARGUMENT handle correctly.
 */
START_TEST(test_local_disk_vpd83_search) {
    int rc = LSM_ERR_OK;
    lsm_string_list *disk_path_list;
    /* Not initialized in order to test dangling pointer disk_path_list */
    lsm_error *lsm_err = NULL;

    if (is_simc_plugin == 1) {
        /* silently skip on simc, no need for duplicate test. */
        return;
    }

    rc = lsm_local_disk_vpd83_search(NULL, &disk_path_list, &lsm_err);
    ck_assert_msg(
        rc == LSM_ERR_INVALID_ARGUMENT,
        "lsm_local_disk_vpd83_search(): Expecting "
        "LSM_ERR_INVALID_ARGUMENT when vpd83 argument pointer is NULL");

    ck_assert_msg(disk_path_list == NULL,
                  "lsm_local_disk_vpd83_search(): Expecting "
                  "disk_path_list been set as NULL.");

    ck_assert_msg(lsm_err != NULL, "lsm_local_disk_vpd83_search(): Expecting "
                                   "lsm_err been set as non-NULL.");
    ck_assert_msg(lsm_error_number_get(lsm_err) == LSM_ERR_INVALID_ARGUMENT,
                  "lsm_local_disk_vpd83_search(): Expecting "
                  "lsm_err been set with LSM_ERR_INVALID_ARGUMENT");
    ck_assert_msg(lsm_error_message_get(lsm_err) != NULL,
                  "lsm_local_disk_vpd83_search(): Expecting "
                  "lsm_err been set with non-NULL error message");
    lsm_error_free(lsm_err);

    rc = lsm_local_disk_vpd83_search(VPD83_TO_SEARCH, NULL, &lsm_err);
    ck_assert_msg(
        rc == LSM_ERR_INVALID_ARGUMENT,
        "lsm_local_disk_vpd83_search(): Expecting "
        "LSM_ERR_INVALID_ARGUMENT when disk_path_list argument pointer "
        "is NULL");
    lsm_error_free(lsm_err);

    rc = lsm_local_disk_vpd83_search(VPD83_TO_SEARCH, &disk_path_list, NULL);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "lsm_local_disk_vpd83_search(): Expecting "
                  "LSM_ERR_INVALID_ARGUMENT when lsm_err argument pointer "
                  "is NULL");

    rc = lsm_local_disk_vpd83_search(INVALID_VPD83, &disk_path_list, &lsm_err);
    ck_assert_msg(lsm_err != NULL,
                  "lsm_local_disk_vpd83_search(): Expecting lsm_err "
                  "been set at not NULL when incorrect VPD83 provided");
    ck_assert_msg(lsm_error_number_get(lsm_err) == LSM_ERR_INVALID_ARGUMENT,
                  "lsm_local_disk_vpd83_search(): Expecting "
                  "lsm_err been set with LSM_ERR_INVALID_ARGUMENT");
    ck_assert_msg(lsm_error_message_get(lsm_err) != NULL,
                  "lsm_local_disk_vpd83_search(): Expecting "
                  "lsm_err been set with non-NULL error message");
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "lsm_local_disk_vpd83_search(): Expecting LSM_ERR_OK "
                  "when no argument is NULL");
    lsm_error_free(lsm_err);

    rc =
        lsm_local_disk_vpd83_search(VPD83_TO_SEARCH, &disk_path_list, &lsm_err);
    ck_assert_msg(rc == LSM_ERR_OK,
                  "lsm_local_disk_vpd83_search(): Expecting LSM_ERR_OK"
                  "when no argument is NULL");

    ck_assert_msg(lsm_err == NULL,
                  "lsm_local_disk_vpd83_search(): Expecting lsm_err as NULL"
                  "when valid argument provided");

    if (disk_path_list != NULL)
        lsm_string_list_free(disk_path_list);

    if (lsm_err != NULL)
        lsm_error_free(lsm_err);
    /* ^ No need to free lsm_err as programme already quit due to above check,
     * keeping this line is just for code demonstration.
     */

    rc = lsm_local_disk_vpd83_search(VALID_BUT_NOT_EXIST_VPD83, &disk_path_list,
                                     &lsm_err);
    ck_assert_msg(rc == LSM_ERR_OK,
                  "lsm_local_disk_vpd83_search(): Expecting LSM_ERR_OK"
                  "when no argument is NULL");
    ck_assert_msg(lsm_err == NULL,
                  "lsm_local_disk_vpd83_search(): Expecting lsm_err as NULL"
                  "when valid argument provided");
    ck_assert_msg(disk_path_list == NULL,
                  "lsm_local_disk_vpd83_search(): Expecting disk_path_list as "
                  "NULL when searching for VALID_BUT_NOT_EXIST_VPD83");

    if (lsm_err != NULL)
        lsm_error_free(lsm_err);
    /* ^ Just to trick coverity scan. The 'ck_assert_msg' already quit */
    if (disk_path_list != NULL)
        lsm_string_list_free(disk_path_list);
    /* ^ Just to trick coverity scan. The 'ck_assert_msg' already quit */
}
END_TEST

START_TEST(test_local_disk_serial_num_get) {
    int rc = LSM_ERR_OK;
    char *serial_num;
    /* Not initialized in order to test dangling pointer serial_num */
    lsm_error *lsm_err = NULL;

    rc = lsm_local_disk_serial_num_get(NULL, &serial_num, &lsm_err);

    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "lsm_local_disk_serial_num_get(): Expecting "
                  "LSM_ERR_INVALID_ARGUMENT when input is NULL");
    ck_assert_msg(serial_num == NULL,
                  "lsm_local_disk_serial_num_get(): Expecting "
                  "serial_num been set as NULL.");
    ck_assert_msg(lsm_err != NULL, "lsm_local_disk_serial_num_get(): Expecting "
                                   "lsm_err been set as non-NULL.");
    ck_assert_msg(lsm_error_number_get(lsm_err) == LSM_ERR_INVALID_ARGUMENT,
                  "lsm_local_disk_serial_num_get(): Expecting "
                  "lsm_err been set with LSM_ERR_INVALID_ARGUMENT");
    ck_assert_msg(lsm_error_message_get(lsm_err) != NULL,
                  "lsm_local_disk_serial_num_get(): Expecting "
                  "lsm_err been set with non-NULL error message");
    lsm_error_free(lsm_err);

    rc = lsm_local_disk_serial_num_get("/dev/sda", NULL, &lsm_err);

    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "lsm_local_disk_serial_num_get(): Expecting "
                  "LSM_ERR_INVALID_ARGUMENT when input is NULL");
    lsm_error_free(lsm_err);

    rc = lsm_local_disk_serial_num_get("/dev/sda", &serial_num, NULL);

    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "lsm_local_disk_serial_num_get(): Expecting "
                  "LSM_ERR_INVALID_ARGUMENT when lsm_err is NULL");

    /* We cannot make sure /dev/sda exists, but worth trying */
    lsm_local_disk_serial_num_get("/dev/sda", &serial_num, &lsm_err);
    if (lsm_err != NULL)
        lsm_error_free(lsm_err);
    free(serial_num);

    /* Test nonexistent disk */
    rc =
        lsm_local_disk_serial_num_get(NOT_EXIST_SD_PATH, &serial_num, &lsm_err);
    ck_assert_msg(rc == LSM_ERR_NOT_FOUND_DISK,
                  "lsm_local_disk_serial_num_get(): Expecting "
                  "LSM_ERR_NOT_FOUND_DISK when disk does not exist");
    ck_assert_msg(serial_num == NULL,
                  "lsm_local_disk_serial_num_get(): Expecting "
                  "serial_num as NULL when disk does not exist");
    ck_assert_msg(lsm_err != NULL, "lsm_local_disk_serial_num_get(): Expecting "
                                   "lsm_err not NULL when disk does not exist");
    ck_assert_msg(lsm_error_number_get(lsm_err) == LSM_ERR_NOT_FOUND_DISK,
                  "lsm_local_disk_serial_num_get(): Expecting "
                  "lsm_err been set with LSM_ERR_NOT_FOUND_DISK");
    ck_assert_msg(lsm_error_message_get(lsm_err) != NULL,
                  "lsm_local_disk_serial_num_get(): Expecting lsm_err "
                  "been set with non-NULL error message when disk not exist");
    lsm_error_free(lsm_err);
    if (serial_num != NULL)
        free(serial_num);
    /* ^ Just to trick coverity scan. The 'serial_num' is NULL here */
}
END_TEST

START_TEST(test_local_disk_vpd83_get) {
    int rc = LSM_ERR_OK;
    char *vpd83;
    /* Not initialized in order to test dangling pointer vpd83 */
    lsm_error *lsm_err = NULL;

    rc = lsm_local_disk_vpd83_get(NULL, &vpd83, &lsm_err);

    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "lsm_local_disk_vpd83_get(): Expecting "
                  "LSM_ERR_INVALID_ARGUMENT when input is NULL");
    ck_assert_msg(vpd83 == NULL, "lsm_local_disk_vpd83_get(): Expecting "
                                 "vpd83 been set as NULL.");
    ck_assert_msg(lsm_err != NULL, "lsm_local_disk_vpd83_get(): Expecting "
                                   "lsm_err been set as non-NULL.");
    ck_assert_msg(lsm_error_number_get(lsm_err) == LSM_ERR_INVALID_ARGUMENT,
                  "lsm_local_disk_vpd83_get(): Expecting "
                  "lsm_err been set with LSM_ERR_INVALID_ARGUMENT");
    ck_assert_msg(lsm_error_message_get(lsm_err) != NULL,
                  "lsm_local_disk_vpd83_get(): Expecting "
                  "lsm_err been set with non-NULL error message");
    lsm_error_free(lsm_err);

    rc = lsm_local_disk_vpd83_get("/dev/sda", NULL, &lsm_err);

    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "lsm_local_disk_vpd83_get(): Expecting "
                  "LSM_ERR_INVALID_ARGUMENT when input is NULL");
    lsm_error_free(lsm_err);

    rc = lsm_local_disk_vpd83_get("/dev/sda", &vpd83, NULL);

    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "lsm_local_disk_vpd83_get(): Expecting "
                  "LSM_ERR_INVALID_ARGUMENT when lsm_err is NULL");

    /* We cannot make sure /dev/sda exists, but worth trying */
    lsm_local_disk_vpd83_get("/dev/sda", &vpd83, &lsm_err);
    if (lsm_err != NULL)
        lsm_error_free(lsm_err);
    free(vpd83);

    /* Test non-exist disk */
    rc = lsm_local_disk_vpd83_get(NOT_EXIST_SD_PATH, &vpd83, &lsm_err);
    ck_assert_msg(rc == LSM_ERR_NOT_FOUND_DISK,
                  "lsm_local_disk_vpd83_get(): Expecting "
                  "LSM_ERR_NOT_FOUND_DISK when disk not exist");
    ck_assert_msg(vpd83 == NULL, "lsm_local_disk_vpd83_get(): Expecting "
                                 "vpd83 as NULL when disk not exist");
    ck_assert_msg(lsm_err != NULL, "lsm_local_disk_vpd83_get(): Expecting "
                                   "lsm_err not NULL when disk not exist");
    ck_assert_msg(lsm_error_number_get(lsm_err) == LSM_ERR_NOT_FOUND_DISK,
                  "lsm_local_disk_vpd83_get(): Expecting "
                  "lsm_err been set with LSM_ERR_NOT_FOUND_DISK");
    ck_assert_msg(lsm_error_message_get(lsm_err) != NULL,
                  "lsm_local_disk_vpd83_get(): Expecting lsm_err "
                  "been set with non-NULL error message when disk not exist");
    lsm_error_free(lsm_err);
    free(vpd83);
}
END_TEST

START_TEST(test_local_disk_rpm_get) {
    int rc = 0;
    int32_t rpm = LSM_DISK_RPM_UNKNOWN;
    lsm_error *lsm_err = NULL;

    rc = lsm_local_disk_rpm_get("/dev/sda", &rpm, &lsm_err);
    if (rc == LSM_ERR_OK) {
        ck_assert_msg(rpm != LSM_DISK_RPM_UNKNOWN,
                      "lsm_local_disk_rpm_get(): "
                      "Expecting rpm not been LSM_DISK_RPM_UNKNOWN "
                      "when rc == LSM_ERR_OK");
    } else {
        lsm_error_free(lsm_err);
    }

    /* Test non-exist disk */
    rc = lsm_local_disk_rpm_get(NOT_EXIST_SD_PATH, &rpm, &lsm_err);
    ck_assert_msg(rc == LSM_ERR_NOT_FOUND_DISK,
                  "lsm_local_disk_rpm_get(): "
                  "Expecting LSM_ERR_NOT_FOUND_DISK error with "
                  "non-exist sd_path");
    ck_assert_msg(lsm_err != NULL,
                  "lsm_local_disk_rpm_get(): "
                  "Expecting lsm_err not NULL with non-exist sd_path");
    ck_assert_msg(lsm_error_number_get(lsm_err) == LSM_ERR_NOT_FOUND_DISK,
                  "lsm_local_disk_rpm_get(): "
                  "Expecting error number of lsm_err been set as "
                  "LSM_ERR_NOT_FOUND_DISK with non-exist sd_path");
    ck_assert_msg(lsm_error_message_get(lsm_err) != NULL,
                  "lsm_local_disk_rpm_get(): "
                  "Expecting error message of lsm_err not NULL "
                  "with non-exist sd_path");
    lsm_error_free(lsm_err);
}
END_TEST

START_TEST(test_local_disk_link_type) {
    int rc = 0;
    lsm_disk_link_type link_type = LSM_DISK_LINK_TYPE_UNKNOWN;
    lsm_error *lsm_err = NULL;

    rc = lsm_local_disk_link_type_get("/dev/sda", &link_type, &lsm_err);
    if (lsm_err != NULL)
        ck_assert_msg(rc != LSM_ERR_LIB_BUG,
                      "lsm_local_disk_link_type_get() got LSM_ERR_LIB_BUG: %s",
                      lsm_error_message_get(lsm_err));
    else
        ck_assert_msg(rc != LSM_ERR_LIB_BUG,
                      "lsm_local_disk_link_type_get() got LSM_ERR_LIB_BUG with "
                      "NULL lsm_err");

    if (rc != LSM_ERR_OK)
        lsm_error_free(lsm_err);

    /* Test non-exist disk */
    rc = lsm_local_disk_link_type_get(NOT_EXIST_SD_PATH, &link_type, &lsm_err);
    ck_assert_msg(rc == LSM_ERR_NOT_FOUND_DISK,
                  "lsm_local_disk_link_type_get(): "
                  "Expecting LSM_ERR_NOT_FOUND_DISK error with "
                  "non-exist disk_path");
    ck_assert_msg(lsm_err != NULL,
                  "lsm_local_disk_link_type_get(): "
                  "Expecting lsm_err not NULL with non-exist disk_path");
    ck_assert_msg(lsm_error_number_get(lsm_err) == LSM_ERR_NOT_FOUND_DISK,
                  "lsm_local_disk_link_type_get(): "
                  "Expecting error number of lsm_err been set as "
                  "LSM_ERR_NOT_FOUND_DISK with non-exist disk_path");
    ck_assert_msg(lsm_error_message_get(lsm_err) != NULL,
                  "lsm_local_disk_link_type_get(): "
                  "Expecting error message of lsm_err not NULL "
                  "with non-exist disk_path");
    lsm_error_free(lsm_err);
}
END_TEST

START_TEST(test_local_disk_health_status_get) {
    int rc = 0;
    int32_t health_status = LSM_DISK_HEALTH_STATUS_UNKNOWN;
    lsm_error *lsm_err = NULL;

    rc = lsm_local_disk_health_status_get("/dev/sda", &health_status, &lsm_err);
    if (rc == LSM_ERR_OK) {
        ck_assert_msg(health_status == LSM_DISK_HEALTH_STATUS_UNKNOWN,
                      "lsm_local_disk_health_status_get(): "
                      "Expecting health_status to be "
                      "LSM_DISK_HEALTH_STATUS_UNKNOWN "
                      "when rc == LSM_ERR_OK and the disk "
                      "type is unknown");
    } else {
        lsm_error_free(lsm_err);
    }

    /* Test disk that does not exist */
    rc = lsm_local_disk_health_status_get(NOT_EXIST_SD_PATH, &health_status,
                                          &lsm_err);
    ck_assert_msg(rc == LSM_ERR_NOT_FOUND_DISK,
                  "lsm_local_disk_health_status_get(): "
                  "Expecting LSM_ERR_NOT_FOUND_DISK error with "
                  "non-existent sd_path");
    ck_assert_msg(lsm_err != NULL,
                  "lsm_local_disk_health_status_get(): "
                  "Expecting lsm_err not NULL with non-existent sd_path");
    ck_assert_msg(lsm_error_number_get(lsm_err) == LSM_ERR_NOT_FOUND_DISK,
                  "lsm_local_disk_health_status_get(): "
                  "Expecting error number of lsm_err to be set as "
                  "LSM_ERR_NOT_FOUND_DISK with non-existent sd_path");
    ck_assert_msg(lsm_error_message_get(lsm_err) != NULL,
                  "lsm_local_disk_health_status_get(): "
                  "Expecting error message of lsm_err to not be NULL "
                  "with non-existent sd_path");
    lsm_error_free(lsm_err);
}
END_TEST

START_TEST(test_batteries) {
    uint32_t count = 0;
    lsm_battery **bs = NULL;
    const char *id = NULL;
    const char *name = NULL;
    const char *system_id = NULL;
    uint32_t i = 0;
    lsm_battery *b_copy = NULL;

    ck_assert_msg(c != NULL, "c = %p", c);

    int rc = lsm_battery_list(c, NULL, NULL, &bs, &count, 0);

    ck_assert_msg(LSM_ERR_OK == rc, "lsm_battery_list(): rc %d", rc);
    ck_assert_msg(count >= 1, "Got no battery");

    for (; i < count; ++i) {
        b_copy = lsm_battery_record_copy(bs[i]);
        ck_assert_msg(b_copy != NULL, "b_copy %p", b_copy);
        ck_assert_msg(compare_battery(bs[i], b_copy) == 0,
                      "src copy miss-match");
        lsm_battery_record_free(b_copy);
        b_copy = NULL;

        id = lsm_battery_id_get(bs[i]);
        ck_assert_msg(id != NULL && strlen(id) > 0, "NULL");

        name = lsm_battery_name_get(bs[i]);
        ck_assert_msg(name != NULL && strlen(name) > 0, "NULL");

        system_id = lsm_battery_system_id_get(bs[i]);
        ck_assert_msg(system_id != NULL && strlen(system_id) > 0, "NULL");
        ck_assert_msg(strcmp(system_id, SYSTEM_ID) == 0,
                      "Incorrect battery system id: %s", id);
        ck_assert_msg(lsm_battery_type_get(bs[i]) >= 1, "%u",
                      lsm_battery_type_get(bs[i]));
        ck_assert_msg(lsm_battery_status_get(bs[i]) >= 1, "%lu",
                      lsm_battery_status_get(bs[i]));
    }
    lsm_battery_record_array_free(bs, count);
}
END_TEST

START_TEST(test_volume_cache_info) {
    lsm_volume *volume = NULL;
    char *job = NULL;
    lsm_pool *pool = NULL;

    uint32_t write_cache_policy = LSM_VOLUME_WRITE_CACHE_POLICY_UNKNOWN;
    uint32_t write_cache_status = LSM_VOLUME_WRITE_CACHE_STATUS_UNKNOWN;
    uint32_t read_cache_policy = LSM_VOLUME_READ_CACHE_POLICY_UNKNOWN;
    uint32_t read_cache_status = LSM_VOLUME_READ_CACHE_STATUS_UNKNOWN;
    uint32_t physical_disk_cache = LSM_VOLUME_PHYSICAL_DISK_CACHE_UNKNOWN;

    pool = get_test_pool(c);

    ck_assert_msg(pool != NULL, "Failed to find the test pool");

    int rc = lsm_volume_create(c, pool, "volume_cache_info_test", 20000000,
                               LSM_VOLUME_PROVISION_DEFAULT, &volume, &job,
                               LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(rc == LSM_ERR_OK || rc == LSM_ERR_JOB_STARTED,
                  "lsm_volume_create() %d (%s)", rc,
                  error(lsm_error_last_get(c)));

    if (LSM_ERR_JOB_STARTED == rc) {
        volume = wait_for_job_vol(c, &job);
    }

    G(rc, lsm_volume_cache_info, c, volume, &write_cache_policy,
      &write_cache_status, &read_cache_policy, &read_cache_status,
      &physical_disk_cache, LSM_CLIENT_FLAG_RSVD);

    G(rc, lsm_volume_record_free, volume);
    G(rc, lsm_pool_record_free, pool);
    volume = NULL;
}
END_TEST

START_TEST(test_volume_pdc_update) {
    lsm_volume *volume = NULL;
    char *job = NULL;
    lsm_pool *pool = NULL;
    uint32_t write_cache_policy = LSM_VOLUME_WRITE_CACHE_POLICY_UNKNOWN;
    uint32_t write_cache_status = LSM_VOLUME_WRITE_CACHE_STATUS_UNKNOWN;
    uint32_t read_cache_policy = LSM_VOLUME_READ_CACHE_POLICY_UNKNOWN;
    uint32_t read_cache_status = LSM_VOLUME_READ_CACHE_STATUS_UNKNOWN;
    uint32_t physical_disk_cache = LSM_VOLUME_PHYSICAL_DISK_CACHE_UNKNOWN;
    uint32_t all_pdcs[] = {LSM_VOLUME_PHYSICAL_DISK_CACHE_DISABLED,
                           LSM_VOLUME_PHYSICAL_DISK_CACHE_ENABLED};
    size_t i = 0;

    pool = get_test_pool(c);

    ck_assert_msg(pool != NULL, "Failed to find the test pool");

    int rc = lsm_volume_create(c, pool, "volume_cache_info_test", 20000000,
                               LSM_VOLUME_PROVISION_DEFAULT, &volume, &job,
                               LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(rc == LSM_ERR_OK || rc == LSM_ERR_JOB_STARTED,
                  "lsm_volume_create() %d (%s)", rc,
                  error(lsm_error_last_get(c)));

    if (LSM_ERR_JOB_STARTED == rc) {
        volume = wait_for_job_vol(c, &job);
    }
    for (; i < (sizeof(all_pdcs) / sizeof(all_pdcs[0])); ++i) {
        G(rc, lsm_volume_physical_disk_cache_update, c, volume, all_pdcs[i],
          LSM_CLIENT_FLAG_RSVD);

        G(rc, lsm_volume_cache_info, c, volume, &write_cache_policy,
          &write_cache_status, &read_cache_policy, &read_cache_status,
          &physical_disk_cache, LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(physical_disk_cache == all_pdcs[i],
                      "Failed to change physical disk cache to %" PRIu32 "",
                      all_pdcs[i]);
    }

    G(rc, lsm_volume_record_free, volume);
    G(rc, lsm_pool_record_free, pool);
    volume = NULL;
}
END_TEST

START_TEST(test_volume_wcp_update) {
    lsm_volume *volume = NULL;
    char *job = NULL;
    lsm_pool *pool = NULL;
    uint32_t write_cache_policy = LSM_VOLUME_WRITE_CACHE_POLICY_UNKNOWN;
    uint32_t write_cache_status = LSM_VOLUME_WRITE_CACHE_STATUS_UNKNOWN;
    uint32_t read_cache_policy = LSM_VOLUME_READ_CACHE_POLICY_UNKNOWN;
    uint32_t read_cache_status = LSM_VOLUME_READ_CACHE_STATUS_UNKNOWN;
    uint32_t physical_disk_cache = LSM_VOLUME_PHYSICAL_DISK_CACHE_UNKNOWN;
    uint32_t all_wcps[] = {LSM_VOLUME_WRITE_CACHE_POLICY_AUTO,
                           LSM_VOLUME_WRITE_CACHE_POLICY_WRITE_BACK,
                           LSM_VOLUME_WRITE_CACHE_POLICY_WRITE_THROUGH};
    size_t i = 0;

    pool = get_test_pool(c);

    ck_assert_msg(pool != NULL, "Failed to find the test pool");

    int rc = lsm_volume_create(c, pool, "volume_cache_info_test", 20000000,
                               LSM_VOLUME_PROVISION_DEFAULT, &volume, &job,
                               LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(rc == LSM_ERR_OK || rc == LSM_ERR_JOB_STARTED,
                  "lsm_volume_create() %d (%s)", rc,
                  error(lsm_error_last_get(c)));

    if (LSM_ERR_JOB_STARTED == rc) {
        volume = wait_for_job_vol(c, &job);
    }
    for (; i < (sizeof(all_wcps) / sizeof(all_wcps[0])); ++i) {
        G(rc, lsm_volume_write_cache_policy_update, c, volume, all_wcps[i],
          LSM_CLIENT_FLAG_RSVD);

        G(rc, lsm_volume_cache_info, c, volume, &write_cache_policy,
          &write_cache_status, &read_cache_policy, &read_cache_status,
          &physical_disk_cache, LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(write_cache_policy == all_wcps[i],
                      "Failed to change write cache policy to %" PRIu32 "",
                      all_wcps[i]);
    }

    G(rc, lsm_volume_record_free, volume);
    G(rc, lsm_pool_record_free, pool);
    volume = NULL;
}
END_TEST

START_TEST(test_volume_rcp_update) {
    lsm_volume *volume = NULL;
    char *job = NULL;
    lsm_pool *pool = NULL;
    uint32_t write_cache_policy = LSM_VOLUME_WRITE_CACHE_POLICY_UNKNOWN;
    uint32_t write_cache_status = LSM_VOLUME_WRITE_CACHE_STATUS_UNKNOWN;
    uint32_t read_cache_policy = LSM_VOLUME_READ_CACHE_POLICY_UNKNOWN;
    uint32_t read_cache_status = LSM_VOLUME_READ_CACHE_STATUS_UNKNOWN;
    uint32_t physical_disk_cache = LSM_VOLUME_PHYSICAL_DISK_CACHE_UNKNOWN;
    uint32_t all_rcps[] = {LSM_VOLUME_READ_CACHE_POLICY_DISABLED,
                           LSM_VOLUME_READ_CACHE_POLICY_ENABLED};
    size_t i = 0;

    pool = get_test_pool(c);

    ck_assert_msg(pool != NULL, "Failed to find the test pool");

    int rc = lsm_volume_create(c, pool, "volume_cache_info_test", 20000000,
                               LSM_VOLUME_PROVISION_DEFAULT, &volume, &job,
                               LSM_CLIENT_FLAG_RSVD);

    ck_assert_msg(rc == LSM_ERR_OK || rc == LSM_ERR_JOB_STARTED,
                  "lsm_volume_create() %d (%s)", rc,
                  error(lsm_error_last_get(c)));

    if (LSM_ERR_JOB_STARTED == rc) {
        volume = wait_for_job_vol(c, &job);
    }
    for (; i < (sizeof(all_rcps) / sizeof(all_rcps[0])); ++i) {
        G(rc, lsm_volume_read_cache_policy_update, c, volume, all_rcps[i],
          LSM_CLIENT_FLAG_RSVD);

        G(rc, lsm_volume_cache_info, c, volume, &write_cache_policy,
          &write_cache_status, &read_cache_policy, &read_cache_status,
          &physical_disk_cache, LSM_CLIENT_FLAG_RSVD);

        ck_assert_msg(read_cache_policy == all_rcps[i],
                      "Failed to change write cache policy to %" PRIu32 "",
                      all_rcps[i]);
    }

    G(rc, lsm_volume_record_free, volume);
    G(rc, lsm_pool_record_free, pool);
    volume = NULL;
}
END_TEST

#define _TEST_LOCAL_DISK_LED(on_func, off_func)                                \
    do {                                                                       \
        int rc = LSM_ERR_OK;                                                   \
        lsm_string_list *disk_paths = NULL;                                    \
        lsm_error *lsm_err = NULL;                                             \
        uint32_t i = 0;                                                        \
        const char *disk_path = NULL;                                          \
        rc = lsm_local_disk_list(&disk_paths, &lsm_err);                       \
        if (lsm_err != NULL)                                                   \
            lsm_error_free(lsm_err);                                           \
        ck_assert_msg(rc == LSM_ERR_OK, "lsm_local_disk_list() failed as %d",  \
                      rc);                                                     \
        /* Only try maximum 4 disks */                                         \
        for (; i < lsm_string_list_size(disk_paths) && i < 4; ++i) {           \
            disk_path = lsm_string_list_elem_get(disk_paths, i);               \
            ck_assert_msg(disk_path != NULL, "Got NULL disk path");            \
            rc = on_func(disk_path, &lsm_err);                                 \
            ck_assert_msg(rc == LSM_ERR_OK || rc == LSM_ERR_NO_SUPPORT ||      \
                              rc == LSM_ERR_PERMISSION_DENIED,                 \
                          #on_func "(): Got unexpected return: %d", rc);       \
            if (rc != LSM_ERR_OK) {                                            \
                ck_assert_msg(lsm_err != NULL,                                 \
                              #on_func "(): Got NULL lsm_err while "           \
                                       "rc(%d) != LSM_ERR_OK",                 \
                              rc);                                             \
                lsm_error_free(lsm_err);                                       \
            }                                                                  \
            if (rc == LSM_ERR_OK) {                                            \
                printf(#on_func "(): success on disk %s\n", disk_path);        \
            }                                                                  \
            if (rc == LSM_ERR_NO_SUPPORT) {                                    \
                printf(#on_func "(): not supported disk %s\n", disk_path);     \
            }                                                                  \
            rc = off_func(disk_path, &lsm_err);                                \
            ck_assert_msg(rc == LSM_ERR_OK || rc == LSM_ERR_NO_SUPPORT ||      \
                              rc == LSM_ERR_PERMISSION_DENIED,                 \
                          #off_func "(): Got unexpected return: %d", rc);      \
            if (rc != LSM_ERR_OK) {                                            \
                ck_assert_msg(lsm_err != NULL,                                 \
                              #off_func "(): Got NULL lsm_err "                \
                                        "while rc(%d) != LSM_ERR_OK",          \
                              rc);                                             \
                lsm_error_free(lsm_err);                                       \
            }                                                                  \
            if (rc == LSM_ERR_OK) {                                            \
                printf(#off_func "(): success on disk %s\n", disk_path);       \
            }                                                                  \
            if (rc == LSM_ERR_NO_SUPPORT) {                                    \
                printf(#off_func "(): not supported disk %s\n", disk_path);    \
            }                                                                  \
        }                                                                      \
        lsm_string_list_free(disk_paths);                                      \
    } while (0)

START_TEST(test_local_disk_ident_led) {
    _TEST_LOCAL_DISK_LED(lsm_local_disk_ident_led_on,
                         lsm_local_disk_ident_led_off);
}
END_TEST

START_TEST(test_local_disk_fault_led) {
    _TEST_LOCAL_DISK_LED(lsm_local_disk_fault_led_on,
                         lsm_local_disk_fault_led_off);
}
END_TEST

START_TEST(test_local_disk_led_status_get) {
    int rc = LSM_ERR_OK;
    lsm_string_list *disk_paths = NULL;
    lsm_error *lsm_err = NULL;
    uint32_t i = 0;
    const char *disk_path = NULL;
    uint32_t led_status = LSM_DISK_LED_STATUS_UNKNOWN;

    rc = lsm_local_disk_list(&disk_paths, &lsm_err);
    if (lsm_err)
        lsm_error_free(lsm_err);
    ck_assert_msg(rc == LSM_ERR_OK, "lsm_local_disk_list() failed as %d", rc);
    /* Only try maximum 4 disks */
    for (; i < lsm_string_list_size(disk_paths) && i < 4; ++i) {
        disk_path = lsm_string_list_elem_get(disk_paths, i);
        ck_assert_msg(disk_path != NULL, "Got NULL disk path");
        rc = lsm_local_disk_led_status_get(disk_path, &led_status, &lsm_err);
        ck_assert_msg(rc == LSM_ERR_OK || rc == LSM_ERR_NO_SUPPORT ||
                          rc == LSM_ERR_PERMISSION_DENIED,
                      "lsm_local_disk_led_status_get(): "
                      "Got unexpected return: %d",
                      rc);
        if (lsm_err)
            lsm_error_free(lsm_err);
        lsm_err = NULL;
    }

    /* Test invalid argument */
    rc = lsm_local_disk_led_status_get(NULL, &led_status, &lsm_err);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "Expecting LSM_ERR_INVALID_ARGUMENT, but got %d", rc);
    ck_assert_msg(lsm_err != NULL,
                  "Expecting non-NULL lsm_error, but got NULL");
    lsm_error_free(lsm_err);

    rc = lsm_local_disk_led_status_get("/dev/sda", NULL, &lsm_err);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "Expecting LSM_ERR_INVALID_ARGUMENT, but got %d", rc);
    ck_assert_msg(lsm_err != NULL,
                  "Expecting non-NULL lsm_error, but got NULL");
    lsm_error_free(lsm_err);

    rc = lsm_local_disk_led_status_get("/dev/sda", &led_status, NULL);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "Expecting LSM_ERR_INVALID_ARGUMENT, but got %d", rc);

    /* Test not exists disk */
    rc =
        lsm_local_disk_led_status_get(NOT_EXIST_SD_PATH, &led_status, &lsm_err);
    ck_assert_msg(rc == LSM_ERR_NOT_FOUND_DISK,
                  "Expecting LSM_ERR_NOT_FOUND_DISK, but got %d", rc);
    ck_assert_msg(lsm_err != NULL,
                  "Expecting non-NULL lsm_error, but got NULL");
    lsm_error_free(lsm_err);
    lsm_string_list_free(disk_paths);
}
END_TEST

/*TODO(Gris Ge): Merge duplicate code of local disk test cases */
START_TEST(test_local_disk_link_speed_get) {
    int rc = LSM_ERR_OK;
    lsm_string_list *disk_paths = NULL;
    lsm_error *lsm_err = NULL;
    uint32_t i = 0;
    const char *disk_path = NULL;
    uint32_t link_speed = LSM_DISK_LINK_SPEED_UNKNOWN;

    rc = lsm_local_disk_list(&disk_paths, &lsm_err);
    if (lsm_err)
        lsm_error_free(lsm_err);
    ck_assert_msg(rc == LSM_ERR_OK, "lsm_local_disk_list() failed as %d", rc);
    /* Only try maximum 4 disks */
    for (; i < lsm_string_list_size(disk_paths) && i < 4; ++i) {
        disk_path = lsm_string_list_elem_get(disk_paths, i);
        ck_assert_msg(disk_path != NULL, "Got NULL disk path");
        rc = lsm_local_disk_link_speed_get(disk_path, &link_speed, &lsm_err);
        ck_assert_msg(rc == LSM_ERR_OK || rc == LSM_ERR_NO_SUPPORT ||
                          rc == LSM_ERR_PERMISSION_DENIED,
                      "lsm_local_disk_led_status_get(): "
                      "Got unexpected return: %d",
                      rc);
        if (lsm_err)
            lsm_error_free(lsm_err);
        lsm_err = NULL;
    }

    /* Test invalid argument */
    rc = lsm_local_disk_link_speed_get(NULL, &link_speed, &lsm_err);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "Expecting LSM_ERR_INVALID_ARGUMENT, but got %d", rc);
    ck_assert_msg(lsm_err != NULL,
                  "Expecting non-NULL lsm_error, but got NULL");
    ck_assert_msg(link_speed == LSM_DISK_LINK_SPEED_UNKNOWN,
                  "Expecting link_speed to have been set as "
                  "LSM_DISK_LINK_SPEED_UNKNOWN, but got %" PRIu32 "",
                  link_speed);
    lsm_error_free(lsm_err);

    rc = lsm_local_disk_link_speed_get("/dev/sda", NULL, &lsm_err);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "Expecting LSM_ERR_INVALID_ARGUMENT, but got %d", rc);
    ck_assert_msg(lsm_err != NULL,
                  "Expecting non-NULL lsm_error, but got NULL");
    lsm_error_free(lsm_err);

    rc = lsm_local_disk_link_speed_get("/dev/sda", &link_speed, NULL);
    ck_assert_msg(rc == LSM_ERR_INVALID_ARGUMENT,
                  "Expecting LSM_ERR_INVALID_ARGUMENT, but got %d", rc);
    ck_assert_msg(link_speed == LSM_DISK_LINK_SPEED_UNKNOWN,
                  "Expecting link_speed to have been set as "
                  "LSM_DISK_LINK_SPEED_UNKNOWN, but got %" PRIu32 "",
                  link_speed);

    /* Test not exists disk */
    rc =
        lsm_local_disk_link_speed_get(NOT_EXIST_SD_PATH, &link_speed, &lsm_err);
    ck_assert_msg(rc == LSM_ERR_NOT_FOUND_DISK,
                  "Expecting LSM_ERR_NOT_FOUND_DISK, but got %d", rc);
    ck_assert_msg(lsm_err != NULL,
                  "Expecting non-NULL lsm_error, but got NULL");
    ck_assert_msg(link_speed == LSM_DISK_LINK_SPEED_UNKNOWN,
                  "Expecting link_speed to have been set as "
                  "LSM_DISK_LINK_SPEED_UNKNOWN, but got %" PRIu32 "",
                  link_speed);
    lsm_error_free(lsm_err);
    lsm_string_list_free(disk_paths);
}
END_TEST

Suite *lsm_suite(void) {
    Suite *s = suite_create("libStorageMgmt");

    TCase *basic = tcase_create("Basic");
    tcase_add_checked_fixture(basic, setup, teardown);

    tcase_add_test(basic, test_volume_vpd_check);
    tcase_add_test(basic, test_initiator_id_verification);
    tcase_add_test(basic, test_target_ports);
    tcase_add_test(basic, test_search_fs);
    tcase_add_test(basic, test_search_access_groups);
    tcase_add_test(basic, test_search_disks);
    tcase_add_test(basic, test_search_volumes);
    tcase_add_test(basic, test_search_pools);

    tcase_add_test(basic, test_uri_parse);

    tcase_add_test(basic, test_error_reporting);
    tcase_add_test(basic, test_capability);
    tcase_add_test(basic, test_nfs_export_funcs);
    tcase_add_test(basic, test_disks);
    tcase_add_test(basic, test_disk_location);
    tcase_add_test(basic, test_disk_rpm_and_link_type);
    tcase_add_test(basic, test_plugin_info);
    tcase_add_test(basic, test_system_fw_version);
    tcase_add_test(basic, test_system_mode);
    tcase_add_test(basic, test_get_available_plugins);
    tcase_add_test(basic, test_volume_methods);
    tcase_add_test(basic, test_iscsi_auth_in);
    tcase_add_test(basic, test_capabilities);
    tcase_add_test(basic, test_smoke_test);
    tcase_add_test(basic, test_access_groups);
    tcase_add_test(basic, test_systems);
    tcase_add_test(basic, test_read_cache_pct);
    tcase_add_test(basic, test_access_groups_grant_revoke);
    tcase_add_test(basic, test_fs);
    tcase_add_test(basic, test_ss);
    tcase_add_test(basic, test_nfs_exports);
    tcase_add_test(basic, test_invalid_input);
    tcase_add_test(basic, test_volume_raid_info);
    tcase_add_test(basic, test_pool_member_info);
    tcase_add_test(basic, test_volume_raid_create_cap_get);
    tcase_add_test(basic, test_volume_raid_create);
    tcase_add_test(basic, test_volume_ident_led_on);
    tcase_add_test(basic, test_volume_ident_led_off);
    tcase_add_test(basic, test_local_disk_vpd83_search);
    tcase_add_test(basic, test_local_disk_serial_num_get);
    tcase_add_test(basic, test_local_disk_vpd83_get);
    tcase_add_test(basic, test_read_cache_pct_update);
    tcase_add_test(basic, test_local_disk_list);
    tcase_add_test(basic, test_local_disk_rpm_get);
    tcase_add_test(basic, test_local_disk_link_type);
    tcase_add_test(basic, test_local_disk_health_status_get);
    tcase_add_test(basic, test_batteries);
    tcase_add_test(basic, test_volume_cache_info);
    tcase_add_test(basic, test_volume_pdc_update);
    tcase_add_test(basic, test_volume_wcp_update);
    tcase_add_test(basic, test_volume_rcp_update);
    tcase_add_test(basic, test_local_disk_ident_led);
    tcase_add_test(basic, test_local_disk_fault_led);
    tcase_add_test(basic, test_local_disk_led_status_get);
    tcase_add_test(basic, test_local_disk_link_speed_get);

    suite_add_tcase(s, basic);
    return s;
}

int main(int argc, char **argv) {
    int number_failed;
    Suite *s = lsm_suite();
    SRunner *sr = srunner_create(s);

    /* Test against simc:// if got any cli argument
     * else use sim:// instead
     */
    if ((argc >= 2) && (argv[1] != NULL))
        is_simc_plugin = 1;

    srunner_run_all(sr, CK_NORMAL);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
