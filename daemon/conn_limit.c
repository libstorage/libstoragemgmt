/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2026 Red Hat, Inc.
 */

#include <string.h>

#include "conn_limit.h"

/* A pid of 0 is never a child of ours, so it marks a free slot. */
#define SLOT_FREE 0

struct conn_entry {
    pid_t pid;
    uid_t uid;
};

static struct conn_entry entries[CONN_LIMIT_TABLE_SIZE];
static unsigned int in_use = 0;
static int max_per_uid = CONN_LIMIT_DEFAULT_MAX_PER_UID;

void conn_limit_set_max(int max) { max_per_uid = (max > 0) ? max : 0; }

int conn_limit_get_max(void) { return max_per_uid; }

unsigned int conn_limit_count(uid_t uid) {
    unsigned int i = 0;
    unsigned int count = 0;

    for (i = 0; i < CONN_LIMIT_TABLE_SIZE; ++i) {
        if (entries[i].pid != SLOT_FREE && entries[i].uid == uid) {
            ++count;
        }
    }
    return count;
}

int conn_limit_allow(uid_t uid) {
    if (max_per_uid <= 0) {
        /* Unlimited, the escape hatch for anyone this cap gets in the way
         * of. Nothing below this point runs. */
        return 1;
    }

    if (in_use >= CONN_LIMIT_TABLE_SIZE) {
        /* No room to record the child, so the count would drift anyway.
         * Fail open. */
        return 1;
    }

    return conn_limit_count(uid) < (unsigned int)max_per_uid;
}

int conn_limit_add(pid_t pid, uid_t uid) {
    unsigned int i = 0;

    if (max_per_uid <= 0) {
        return 0;
    }

    for (i = 0; i < CONN_LIMIT_TABLE_SIZE; ++i) {
        if (entries[i].pid == SLOT_FREE) {
            entries[i].pid = pid;
            entries[i].uid = uid;
            ++in_use;
            return 0;
        }
    }
    return -1;
}

void conn_limit_remove(pid_t pid) {
    unsigned int i = 0;

    if (pid == SLOT_FREE) {
        return;
    }

    for (i = 0; i < CONN_LIMIT_TABLE_SIZE; ++i) {
        if (entries[i].pid == pid) {
            entries[i].pid = SLOT_FREE;
            entries[i].uid = 0;
            --in_use;
            return;
        }
    }
}

void conn_limit_reset(void) {
    memset(entries, 0, sizeof(entries));
    in_use = 0;
}
