/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * Unit tests for the lsmd per-uid concurrent connection limit.
 */

#include <check.h>

#include "conn_limit.h"

#define UID_A 1000
#define UID_B 1001
/* Free slots are {pid: 0, uid: 0}, so root is the one uid that can be
 * confused with an empty table. lsmcli commonly runs as root. */
#define UID_ROOT 0

/*
 * The cases below are scripts of operations run against a freshly reset
 * table, so a case is a list of steps rather than a block of copied code.
 */
enum op_kind {
    OP_END = 0,      /* end of the script; zero-fills the rest of the array */
    OP_ADD,          /* conn_limit_add(arg1 as pid, arg2 as uid) */
    OP_REMOVE,       /* conn_limit_remove(arg1 as pid) */
    OP_EXPECT_ALLOW, /* conn_limit_allow(arg1 as uid) is true */
    OP_EXPECT_DENY,  /* conn_limit_allow(arg1 as uid) is false */
    OP_EXPECT_COUNT, /* conn_limit_count(arg1 as uid) == arg2 */
};

struct op {
    enum op_kind kind;
    long arg1;
    long arg2;
};

struct scenario {
    const char *name;
    int max_per_uid;
    struct op ops[16];
};

static const struct scenario scenarios[] = {
    {"under the cap connections are allowed",
     3,
     {
         {OP_EXPECT_ALLOW, UID_A, 0},
         {OP_ADD, 100, UID_A},
         {OP_EXPECT_ALLOW, UID_A, 0},
         {OP_ADD, 101, UID_A},
         {OP_EXPECT_COUNT, UID_A, 2},
         {OP_EXPECT_ALLOW, UID_A, 0},
     }},
    {"at the cap connections are refused, and allowed again after a remove",
     2,
     {
         {OP_ADD, 100, UID_A},
         {OP_ADD, 101, UID_A},
         {OP_EXPECT_DENY, UID_A, 0},
         {OP_REMOVE, 100, 0},
         {OP_EXPECT_COUNT, UID_A, 1},
         {OP_EXPECT_ALLOW, UID_A, 0},
         {OP_ADD, 102, UID_A},
         {OP_EXPECT_DENY, UID_A, 0},
     }},
    {"root is counted like any other uid, not confused with a free slot",
     2,
     {
         {OP_EXPECT_COUNT, UID_ROOT, 0},
         {OP_EXPECT_ALLOW, UID_ROOT, 0},
         {OP_ADD, 100, UID_ROOT},
         {OP_EXPECT_COUNT, UID_ROOT, 1},
         {OP_EXPECT_ALLOW, UID_ROOT, 0},
         {OP_ADD, 101, UID_ROOT},
         {OP_EXPECT_COUNT, UID_ROOT, 2},
         {OP_EXPECT_DENY, UID_ROOT, 0},
         {OP_EXPECT_COUNT, UID_A, 0},
         {OP_REMOVE, 100, 0},
         {OP_EXPECT_COUNT, UID_ROOT, 1},
         {OP_EXPECT_ALLOW, UID_ROOT, 0},
     }},
    {"uids are counted independently",
     1,
     {
         {OP_ADD, 100, UID_A},
         {OP_EXPECT_DENY, UID_A, 0},
         {OP_EXPECT_ALLOW, UID_B, 0},
         {OP_ADD, 101, UID_B},
         {OP_EXPECT_DENY, UID_B, 0},
         {OP_EXPECT_COUNT, UID_A, 1},
         {OP_EXPECT_COUNT, UID_B, 1},
         {OP_REMOVE, 101, 0},
         {OP_EXPECT_DENY, UID_A, 0},
         {OP_EXPECT_ALLOW, UID_B, 0},
     }},
    {"a cap of zero is unlimited and tracks nothing",
     0,
     {
         {OP_ADD, 100, UID_A},
         {OP_ADD, 101, UID_A},
         {OP_ADD, 102, UID_A},
         {OP_EXPECT_COUNT, UID_A, 0},
         {OP_EXPECT_ALLOW, UID_A, 0},
     }},
    {"a negative cap is treated as unlimited",
     -1,
     {
         {OP_ADD, 100, UID_A},
         {OP_ADD, 101, UID_A},
         {OP_EXPECT_COUNT, UID_A, 0},
         {OP_EXPECT_ALLOW, UID_A, 0},
     }},
    {"removing an unknown pid is harmless",
     1,
     {
         {OP_REMOVE, 999, 0},
         {OP_ADD, 100, UID_A},
         {OP_REMOVE, 999, 0},
         {OP_REMOVE, 0, 0},
         {OP_EXPECT_COUNT, UID_A, 1},
         {OP_EXPECT_DENY, UID_A, 0},
     }},
    {"a reused pid does not corrupt the count",
     2,
     {
         {OP_ADD, 100, UID_A},
         {OP_REMOVE, 100, 0},
         {OP_EXPECT_COUNT, UID_A, 0},
         {OP_ADD, 100, UID_B},
         {OP_EXPECT_COUNT, UID_A, 0},
         {OP_EXPECT_COUNT, UID_B, 1},
         {OP_REMOVE, 100, 0},
         {OP_EXPECT_COUNT, UID_B, 0},
         {OP_EXPECT_ALLOW, UID_A, 0},
     }},
};

static void run_scenario(const struct scenario *s) {
    size_t i = 0;

    conn_limit_reset();
    conn_limit_set_max(s->max_per_uid);

    for (i = 0; i < sizeof(s->ops) / sizeof(s->ops[0]); ++i) {
        const struct op *o = &s->ops[i];

        if (o->kind == OP_END) {
            break;
        }

        switch (o->kind) {
        case OP_END:
            break;
        case OP_ADD:
            ck_assert_msg(0 == conn_limit_add((pid_t)o->arg1, (uid_t)o->arg2),
                          "%s: step %zu: add(%ld, %ld) failed", s->name, i,
                          o->arg1, o->arg2);
            break;
        case OP_REMOVE:
            conn_limit_remove((pid_t)o->arg1);
            break;
        case OP_EXPECT_ALLOW:
            ck_assert_msg(conn_limit_allow((uid_t)o->arg1),
                          "%s: step %zu: uid %ld should be allowed", s->name, i,
                          o->arg1);
            break;
        case OP_EXPECT_DENY:
            ck_assert_msg(!conn_limit_allow((uid_t)o->arg1),
                          "%s: step %zu: uid %ld should be refused", s->name, i,
                          o->arg1);
            break;
        case OP_EXPECT_COUNT:
            ck_assert_msg(
                conn_limit_count((uid_t)o->arg1) == (unsigned int)o->arg2,
                "%s: step %zu: uid %ld count is %u, expected %ld", s->name, i,
                o->arg1, conn_limit_count((uid_t)o->arg1), o->arg2);
            break;
        }
    }
}

START_TEST(test_scenarios) { run_scenario(&scenarios[_i]); }
END_TEST

START_TEST(test_cap_setter_clamps) {
    conn_limit_set_max(CONN_LIMIT_DEFAULT_MAX_PER_UID);
    ck_assert_int_eq(conn_limit_get_max(), CONN_LIMIT_DEFAULT_MAX_PER_UID);

    conn_limit_set_max(-5);
    ck_assert_int_eq(conn_limit_get_max(), 0);

    conn_limit_set_max(0);
    ck_assert_int_eq(conn_limit_get_max(), 0);
}
END_TEST

/*
 * A full table means we can no longer account for a uid's children, and
 * refusing on that basis would deny service over our own book-keeping. Both
 * the check and the recording have to say so.
 */
START_TEST(test_table_full_fails_open) {
    int i = 0;
    /* Far above the table size, so only exhaustion can end the loop. */
    const int cap = CONN_LIMIT_TABLE_SIZE * 2;

    conn_limit_reset();
    conn_limit_set_max(cap);

    for (i = 0; i < CONN_LIMIT_TABLE_SIZE; ++i) {
        ck_assert_int_eq(conn_limit_add((pid_t)(i + 1), UID_A), 0);
    }
    ck_assert_uint_eq(conn_limit_count(UID_A), CONN_LIMIT_TABLE_SIZE);

    /* Nowhere left to record a child, so the next one is let through
     * untracked and the caller is told. */
    ck_assert_int_eq(conn_limit_add((pid_t)(CONN_LIMIT_TABLE_SIZE + 1), UID_A),
                     -1);
    ck_assert_int_eq(conn_limit_allow(UID_A), 1);
    ck_assert_int_eq(conn_limit_allow(UID_B), 1);
    ck_assert_uint_eq(conn_limit_count(UID_A), CONN_LIMIT_TABLE_SIZE);

    /* With a cap this low the ordinary count check would refuse, so only the
     * exhaustion branch can be what lets these through. Without this the
     * branch's boundary is unpinned and tightening it would go unnoticed -
     * a full table silently enforcing over stale book-keeping is precisely
     * the drift this module must never introduce. */
    conn_limit_set_max(1);
    ck_assert_int_eq(conn_limit_allow(UID_A), 1);
    ck_assert_int_eq(conn_limit_allow(UID_B), 1);
    conn_limit_set_max(cap);

    /* Freeing a slot puts enforcement back in charge. */
    conn_limit_remove(1);
    conn_limit_set_max(1);
    ck_assert_int_eq(conn_limit_allow(UID_A), 0);

    conn_limit_reset();
}
END_TEST

static Suite *conn_limit_suite(void) {
    Suite *s = suite_create("conn_limit");
    TCase *tc = tcase_create("core");

    tcase_add_loop_test(tc, test_scenarios, 0,
                        (int)(sizeof(scenarios) / sizeof(scenarios[0])));
    tcase_add_test(tc, test_cap_setter_clamps);
    tcase_add_test(tc, test_table_full_fails_open);
    suite_add_tcase(s, tc);
    return s;
}

int main(void) {
    int number_failed;
    Suite *s = conn_limit_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? 0 : 1;
}
