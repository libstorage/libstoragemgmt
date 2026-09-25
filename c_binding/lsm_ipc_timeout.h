/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2011-2023 Red Hat, Inc.
 *
 * Author: Tony Asleson <tasleson@redhat.com>
 */

#ifndef LSM_IPC_TIMEOUT_H
#define LSM_IPC_TIMEOUT_H

/* Default for how long (seconds) a client has to complete plugin_register
 * before the plug-in gives up on it. The plug-in runners turn this into a
 * single absolute deadline covering every read and write until registration,
 * so a peer cannot renew it by sending requests, nor stall us by refusing to
 * read our replies. Cleared once the client registers, so slow
 * post-registration operations (e.g. an array that is slow to authenticate)
 * are never interrupted.
 *
 * lsmd.conf(5)'s "plugin-registration-timeout" overrides this default; lsmd
 * resolves it once at startup and passes it down to each plug-in it execs
 * via the LSM_PLUGIN_REGISTRATION_TIMEOUT_ENV environment variable (plugins
 * run as separate processes, potentially not even linked against this
 * header, so the value has to cross that boundary as a plain string rather
 * than a shared constant). A plug-in falls back to this default if the
 * variable is absent or not a positive integer - which also covers running a
 * plug-in by hand, outside of lsmd. Kept consistent with REGISTRATION_TIMEOUT
 * in python_binding/lsm/_pluginrunner.py. */
#define LSM_PLUGIN_INITIAL_RECV_TIMEOUT_SECONDS 30

/* Name of the environment variable lsmd sets (from
 * "plugin-registration-timeout" in lsmd.conf) before exec'ing a plug-in; see
 * LSM_PLUGIN_INITIAL_RECV_TIMEOUT_SECONDS above. Kept consistent with the
 * same literal in python_binding/lsm/_pluginrunner.py. */
#define LSM_PLUGIN_REGISTRATION_TIMEOUT_ENV "LSM_PLUGIN_REGISTRATION_TIMEOUT"

#endif
