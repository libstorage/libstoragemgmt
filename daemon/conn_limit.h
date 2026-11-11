/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2026 Red Hat, Inc.
 */

#ifndef LSM_CONN_LIMIT_H
#define LSM_CONN_LIMIT_H

#include <sys/types.h>

/* Default number of concurrent plug-in processes a single client uid may
 * hold. Well above any plausible legitimate workload - no real client opens
 * dozens of simultaneous connections to one plug-in - while leaving a local
 * attacker with a small slice of the daemon's process budget. Override with
 * "max-connections-per-uid" in lsmd.conf. */
#define CONN_LIMIT_DEFAULT_MAX_PER_UID 64

/* Number of live children we can track. Comfortably above the TasksMax the
 * packaged systemd unit imposes, so running out is not expected to happen. */
#define CONN_LIMIT_TABLE_SIZE 1024

/* Default number of new plug-in processes lsmd will fork for one client uid
 * per second, and how many seconds worth it may burst through at once.
 *
 * The concurrent cap above only bounds children that are still alive. A
 * client that connects and hangs up in a loop never has more than a couple
 * of children outstanding, so without a rate limit it can make lsmd fork and
 * exec a plug-in without bound. Override with
 * "max-connections-per-second-per-uid" in lsmd.conf; 0 disables the rate
 * limit. */
#define CONN_RATE_DEFAULT_PER_UID  8
#define CONN_RATE_BURST_MULTIPLIER 4

/* Number of client uids whose bucket we remember. A connecting uid that finds
 * no free slot evicts the least recently served one, so this only bounds book
 * keeping, not which uids can connect. */
#define CONN_RATE_TABLE_SIZE 128

/* How often, at most, lsmd logs that it refused a connection from one uid.
 * A refused client costs nothing to retry, so one line per refusal would let
 * it write to the log as fast as it can connect. */
#define CONN_REFUSAL_LOG_INTERVAL_SECONDS 5

/* Number of client uids whose last refusal we remember. As with the rate
 * table, a uid that finds no free slot evicts the least recently logged one;
 * the worst that costs is a refusal logged sooner than the interval. */
#define CONN_REFUSAL_LOG_TABLE_SIZE 32

/*
 * Tracks which client uid each live plug-in process was forked for, so that
 * one uid cannot occupy every slot in the daemon's process budget, and bounds
 * how fast one uid can make lsmd fork and exec plug-ins.
 *
 * None of this is signal safe and none of it locks: lsmd installs no SIGCHLD
 * handler (see install_sh()) and calls all of it from the single-threaded
 * main event loop. Anything that changes that - a SIGCHLD handler in
 * particular - needs to revisit this.
 *
 * The concurrent table fails closed. Once it has no room left to record a new
 * child, admitting one would let the per-uid cap silently erode for every
 * uid, so the connection is refused instead. The daemon logs that separately
 * from an ordinary at-the-cap refusal. The rate limiter fails open only in
 * the trivial sense that a disabled limit (0) admits everything.
 */

/*
 * Sets the per-uid cap. Zero means unlimited and disables the tracking
 * entirely. A negative value is treated as zero; the daemon validates
 * "max-connections-per-uid" before calling this and never passes one.
 */
void conn_limit_set_max(int max_per_uid);

/*
 * Returns the cap in effect, 0 when unlimited.
 */
int conn_limit_get_max(void);

/*
 * Returns the number of live children currently tracked for a uid.
 */
unsigned int conn_limit_count(uid_t uid);

/*
 * Returns 1 if a new connection from this uid may be served, 0 if the uid is
 * already at the cap or the table has no room left to track it. Allows when
 * the cap is disabled.
 */
int conn_limit_allow(uid_t uid);

/*
 * Returns 1 if the tracking table cannot accept another entry.
 */
int conn_limit_table_full(void);

/*
 * Sets the per-uid fork rate, in new plug-in processes per second. Zero
 * disables the rate limit. A negative value is treated as zero; the daemon
 * validates "max-connections-per-second-per-uid" before calling this.
 */
void conn_limit_set_rate(int per_second);

/*
 * Returns the rate in effect, 0 when unlimited.
 */
int conn_limit_get_rate(void);

/*
 * Returns 1 and consumes a token if this uid may start another plug-in now,
 * 0 if it has exhausted its burst and has not yet refilled. The bucket starts
 * full, and is never smaller than conn_limit_get_max() so a client that is
 * permitted to hold that many concurrent children can still open them.
 * Allows everything when the rate limit is disabled.
 */
int conn_limit_rate_allow(uid_t uid);

/*
 * Milliseconds on the monotonic clock, the time base of
 * conn_limit_refusal_should_log().
 */
long long conn_limit_now_ms(void);

/*
 * Called for every connection refused from a uid, at time now_ms. Returns 1
 * if the refusal should be logged, 0 if one was logged for this uid within
 * the last CONN_REFUSAL_LOG_INTERVAL_SECONDS. When it returns 1, *suppressed
 * is the number of refusals for this uid that went unlogged since the
 * previous line, for the caller to report. That count only surfaces with the
 * next logged refusal, so the tail of a burst that then stops is never
 * reported. The time is a parameter so the unit tests need not sleep through
 * the interval.
 */
int conn_limit_refusal_should_log(uid_t uid, long long now_ms,
                                  unsigned int *suppressed);

/*
 * Records a forked child. Returns 0 on success, -1 if the table is full, in
 * which case the child simply goes untracked and the caller should warn.
 * conn_limit_allow() refuses before this point once the table is full, so the
 * -1 case should only follow a failure in that check.
 */
int conn_limit_add(pid_t pid, uid_t uid);

/*
 * Forgets a child that has exited. An unknown pid is not an error.
 */
void conn_limit_remove(pid_t pid);

/*
 * Drops every tracked entry. Used by the unit tests to isolate cases; the
 * daemon has no reason to call it, as its children outlive a plug-in rescan.
 */
void conn_limit_reset(void);

/*
 * Drops every rate bucket. Used by the unit tests to isolate cases.
 */
void conn_limit_rate_reset(void);

/*
 * Forgets every logged refusal. Used by the unit tests to isolate cases.
 */
void conn_limit_refusal_log_reset(void);

#endif
