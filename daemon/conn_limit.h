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

/*
 * Tracks which client uid each live plug-in process was forked for, so that
 * one uid cannot occupy every slot in the daemon's process budget.
 *
 * None of this is signal safe and none of it locks: lsmd installs no SIGCHLD
 * handler (see install_sh()) and calls all of it from the single-threaded
 * main event loop. Anything that changes that - a SIGCHLD handler in
 * particular - needs to revisit this.
 *
 * Everything here fails open. The cap exists to bound abuse, not to be a
 * gate, so when the table cannot answer accurately the connection is allowed
 * through rather than denied.
 */

/*
 * Sets the per-uid cap. Zero, or any negative value, means unlimited and
 * disables the tracking entirely.
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
 * already at the cap. Allows when unlimited, and allows when the table has no
 * room left, since a connection we cannot account for is not one we should
 * refuse.
 */
int conn_limit_allow(uid_t uid);

/*
 * Records a forked child. Returns 0 on success, -1 if the table is full, in
 * which case the child simply goes untracked and the caller should warn.
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

#endif
