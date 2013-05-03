/*
 * Copyright (C) 2011-2013 Red Hat, Inc.
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

#ifndef _LSMCLI_FUNC_H
#define _LSMCLI_FUNC_H

#include "arguments.h"
#include <libstoragemgmt/libstoragemgmt.h>

/**
 * Used to wait for a job to complete.
 * @param cmd_rc    The return code of the library call that may have a job
 * @param c         Connection
 * @param job       Job id
 * @param a         Command line arguments
 * @param vol       New volume to print out.
 * @return LSM_ERR_OK on success else error reason.
 */
int waitForJob(int cmd_rc, lsmConnect *c, char *job,
                    const LSM::Arguments &a, lsmVolume **vol = NULL);

/**
 * Dumps error information to stdout.
 * @param ec    Error code
 * @param e     Error record.
 */
void dumpError(int ec, lsmErrorPtr e);

/**
 * Dumps Volumes, initiators, pools to stdout.
 * @param a     Command line arguments.
 * @param c     Connection
 * @return LSM_ERR_OK on success, else error reason.
 */
int list(const LSM::Arguments &a, lsmConnect *c);

/**
 * Creates an initiator to use for access granting.
 * @param a     Command Line arguments
 * @param c     Connection.
 * @return LSM_ERR_OK on success, else error reason.
 */
int createInit(const LSM::Arguments &a, lsmConnect *c);

/**
 * Deletes an initiator
 * @param a     Command line arguments.
 * @param c     Connection
 * @return LSM_ERR_OK on success, else error reason.
 */
int deleteInit(const LSM::Arguments &a, lsmConnect *c);

/**
 * Creates a volume
 * @param a     Command line arguments.
 * @param c     Connection
 * @return LSM_ERR_OK on success, else error reason.
 */
int createVolume(const LSM::Arguments &a, lsmConnect *c);

/**
 * Deletes a volume
 * @param a     Command line arguments
 * @param c     Connection
 * @return LSM_ERR_OK on success, else error reason.
 */
int deleteVolume(const LSM::Arguments &a, lsmConnect *c);

/**
 * Replicates a volume.
 * @param a     Command line arguments
 * @param c     Connection
 * @return LSM_ERR_OK on success, else error reason.
 */
int replicateVolume(const LSM::Arguments &a, lsmConnect *c);

/**
 * Allows an initiator to use a volume.
 * @param a     Command line arguments
 * @param c     Connection
 * @return LSM_ERR_OK on success, else error reason.
 */
int accessGrant(const LSM::Arguments &a, lsmConnect *c);

/**
 * Revokes access for an initiator to a volume.
 * @param a     Command line arguments.
 * @param c     Connection.
 * @return LSM_ERR_OK on success, else error reason.
 */
int accessRevoke(const LSM::Arguments &a, lsmConnect *c);

/**
 * Resize an existing volume.
 * @param a     Command line arguments
 * @param c     Connection
 * @return LSM_ERR_OK on success, else error reason.
 */
int resizeVolume(const LSM::Arguments &a, lsmConnect *c);

#endif