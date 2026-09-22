/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2011-2023 Red Hat, Inc.
 *
 * Author: Tony Asleson <tasleson@redhat.com>
 */

#ifndef LSM_IPC_TIMEOUT_H
#define LSM_IPC_TIMEOUT_H

/* How long (seconds) a client has to complete plugin_register before the
 * plug-in gives up on it. The plug-in runners turn this into a single
 * absolute deadline covering every read and write until registration, so a
 * peer cannot renew it by sending requests, nor stall us by refusing to read
 * our replies. Kept consistent with REGISTRATION_TIMEOUT in
 * python_binding/lsm/_pluginrunner.py. Cleared once the client registers, so
 * slow post-registration operations are never interrupted. */
#define LSM_PLUGIN_INITIAL_RECV_TIMEOUT_SECONDS 30

#endif
