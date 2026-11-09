/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2026 Red Hat, Inc.
 */

#include <string.h>
#include <time.h>

#include "conn_limit.h"

/* A pid of 0 is never a child of ours, so it marks a free slot. */
#define SLOT_FREE 0

/* Rates are kept in thousandths of a connection, so a bucket can hand out
 * fractions of one per millisecond without floating point. */
#define RATE_TOKEN_SCALE 1000

/* Longest elapsed time we credit in one refill, in milliseconds. Refilling
 * more than a full bucket goes to waste anyway, and this keeps the multiply
 * in conn_limit_rate_allow() comfortably inside a long long. */
#define RATE_MAX_ELAPSED_MS (60LL * 60LL * 1000LL) /* one hour */

struct conn_entry {
    pid_t pid;
    uid_t uid;
};

struct rate_entry {
    uid_t uid;
    long long tokens; /* scaled by RATE_TOKEN_SCALE */
    long long last_ms;
    int used;
};

static struct conn_entry entries[CONN_LIMIT_TABLE_SIZE];
static unsigned int in_use = 0;
static int max_per_uid = CONN_LIMIT_DEFAULT_MAX_PER_UID;

static struct rate_entry rate_entries[CONN_RATE_TABLE_SIZE];
static int rate_per_uid = CONN_RATE_DEFAULT_PER_UID;

void conn_limit_set_max(int max) { max_per_uid = (max > 0) ? max : 0; }

int conn_limit_get_max(void) { return max_per_uid; }

void conn_limit_set_rate(int per_second) {
    rate_per_uid = (per_second > 0) ? per_second : 0;
}

int conn_limit_get_rate(void) { return rate_per_uid; }

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

int conn_limit_table_full(void) { return in_use >= CONN_LIMIT_TABLE_SIZE; }

int conn_limit_allow(uid_t uid) {
    if (max_per_uid <= 0) {
        /* Unlimited, the escape hatch for anyone this cap gets in the way
         * of. Nothing below this point runs. */
        return 1;
    }

    if (conn_limit_table_full()) {
        /* No room to record the child, so admitting it would let the count
         * drift under and silently erode the cap for every uid, not just this
         * one. A daemon that cannot account for a connection is at capacity,
         * so refuse it rather than losing the bound. */
        return 0;
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

static long long monotonic_ms(void) {
    struct timespec now;

    if (-1 == clock_gettime(CLOCK_MONOTONIC, &now)) {
        return 0;
    }
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/* Size of one uid's bucket, in scaled tokens. A uid allowed to hold
 * max_per_uid concurrent children has to be able to open that many at once,
 * so the bucket is never smaller than the concurrent cap. */
static long long rate_burst(void) {
    long long burst = (long long)rate_per_uid * CONN_RATE_BURST_MULTIPLIER;

    if (max_per_uid > 0 && (long long)max_per_uid > burst) {
        burst = max_per_uid;
    }
    return burst;
}

int conn_limit_rate_allow(uid_t uid) {
    struct rate_entry *entry = NULL;
    struct rate_entry *free_slot = NULL;
    struct rate_entry *oldest = NULL;
    unsigned int i = 0;
    long long now = 0;
    long long elapsed = 0;
    long long full = 0;

    if (rate_per_uid <= 0) {
        return 1;
    }

    now = monotonic_ms();
    full = rate_burst() * RATE_TOKEN_SCALE;

    for (i = 0; i < CONN_RATE_TABLE_SIZE; ++i) {
        struct rate_entry *cur = &rate_entries[i];

        if (!cur->used) {
            if (free_slot == NULL) {
                free_slot = cur;
            }
            continue;
        }

        if (cur->uid == uid) {
            entry = cur;
            break;
        }

        if (oldest == NULL || cur->last_ms < oldest->last_ms) {
            oldest = cur;
        }
    }

    if (entry == NULL) {
        entry = (free_slot != NULL) ? free_slot : oldest;
        if (entry == NULL) {
            /* Cannot happen with a non-zero table size, but a connection we
             * cannot account for is not one we can meaningfully refuse. */
            return 1;
        }
        entry->uid = uid;
        entry->tokens = full; /* Start full, so the first burst gets through. */
        entry->last_ms = now;
        entry->used = 1;
    }

    elapsed = now - entry->last_ms;
    if (elapsed < 0) {
        elapsed = 0;
    } else if (elapsed > RATE_MAX_ELAPSED_MS) {
        elapsed = RATE_MAX_ELAPSED_MS;
    }

    /* elapsed ms * (rate/sec) == scaled tokens: the two 1000s cancel. */
    entry->tokens += elapsed * rate_per_uid;
    if (entry->tokens > full) {
        entry->tokens = full;
    }
    entry->last_ms = now;

    if (entry->tokens < RATE_TOKEN_SCALE) {
        return 0;
    }
    entry->tokens -= RATE_TOKEN_SCALE;
    return 1;
}

void conn_limit_rate_reset(void) {
    memset(rate_entries, 0, sizeof(rate_entries));
}
