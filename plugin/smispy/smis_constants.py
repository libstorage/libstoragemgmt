# Copyright (C) 2014 Red Hat, Inc.
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
# USA

 # SMI-S job 'JobState' enumerations
(JS_NEW, JS_STARTING, JS_RUNNING, JS_SUSPENDED, JS_SHUTTING_DOWN,
 JS_COMPLETED,
 JS_TERMINATED, JS_KILLED, JS_EXCEPTION) = (2, 3, 4, 5, 6, 7, 8, 9, 10)

# SMI-S job 'OperationalStatus' enumerations
(JOB_OK, JOB_ERROR, JOB_STOPPED, JOB_COMPLETE) = (2, 6, 10, 17)

# SMI-S invoke return values we are interested in
# Reference: Page 54 in 1.5 SMI-S block specification
(INVOKE_OK,
 INVOKE_NOT_SUPPORTED,
 INVOKE_TIMEOUT,
 INVOKE_FAILED,
 INVOKE_INVALID_PARAMETER,
 INVOKE_IN_USE,
 INVOKE_ASYNC,
 INVOKE_SIZE_NOT_SUPPORTED) = (0, 1, 3, 4, 5, 6, 4096, 4097)

# SMI-S replication enumerations
(SYNC_TYPE_MIRROR, SYNC_TYPE_SNAPSHOT, SYNC_TYPE_CLONE) = (6, 7, 8)

# DMTF 2.29.1 (which SNIA SMI-S 1.6 based on)
# CIM_StorageVolume['NameFormat']
VOL_NAME_FORMAT_OTHER = 1
VOL_NAME_FORMAT_VPD83_NNA6 = 2
VOL_NAME_FORMAT_VPD83_NNA5 = 3
VOL_NAME_FORMAT_VPD83_TYPE2 = 4
VOL_NAME_FORMAT_VPD83_TYPE1 = 5
VOL_NAME_FORMAT_VPD83_TYPE0 = 6
VOL_NAME_FORMAT_SNVM = 7
VOL_NAME_FORMAT_NODE_WWN = 8
VOL_NAME_FORMAT_NNA = 9
VOL_NAME_FORMAT_EUI64 = 10
VOL_NAME_FORMAT_T10VID = 11

# CIM_StorageVolume['NameNamespace']
VOL_NAME_SPACE_OTHER = 1
VOL_NAME_SPACE_VPD83_TYPE3 = 2
VOL_NAME_SPACE_VPD83_TYPE2 = 3
VOL_NAME_SPACE_VPD83_TYPE1 = 4
VOL_NAME_SPACE_VPD80 = 5
VOL_NAME_SPACE_NODE_WWN = 6
VOL_NAME_SPACE_SNVM = 7

JOB_RETRIEVE_NONE = 0
JOB_RETRIEVE_VOLUME = 1
JOB_RETRIEVE_POOL = 2

IAAN_WBEM_HTTP_PORT = 5988
IAAN_WBEM_HTTPS_PORT = 5989


class RepSvc(object):

    class Action(object):
        CREATE_ELEMENT_REPLICA = 2

    class RepTypes(object):
        # SMI-S replication service capabilities
        SYNC_MIRROR_LOCAL = 2
        ASYNC_MIRROR_LOCAL = 3
        SYNC_MIRROR_REMOTE = 4
        ASYNC_MIRROR_REMOTE = 5
        SYNC_SNAPSHOT_LOCAL = 6
        ASYNC_SNAPSHOT_LOCAL = 7
        SYNC_SNAPSHOT_REMOTE = 8
        ASYNC_SNAPSHOT_REMOTE = 9
        SYNC_CLONE_LOCAL = 10
        ASYNC_CLONE_LOCAL = 11
        SYNC_CLONE_REMOTE = 12
        ASYNC_CLONE_REMOTE = 13


class CopyStates(object):
    INITIALIZED = 2
    UNSYNCHRONIZED = 3
    SYNCHRONIZED = 4
    INACTIVE = 8


class CopyTypes(object):
    ASYNC = 2           # Async. mirror
    SYNC = 3            # Sync. mirror
    UNSYNCASSOC = 4     # lsm Clone
    UNSYNCUNASSOC = 5   # lsm Copy


class Synchronized(object):
    class SyncState(object):
        INITIALIZED = 2
        PREPAREINPROGRESS = 3
        PREPARED = 4
        RESYNCINPROGRESS = 5
        SYNCHRONIZED = 6
        FRACTURE_IN_PROGRESS = 7
        QUIESCEINPROGRESS = 8
        QUIESCED = 9
        RESTORE_IN_PROGRESSS = 10
        IDLE = 11
        BROKEN = 12
        FRACTURED = 13
        FROZEN = 14
        COPY_IN_PROGRESS = 15

# SMI-S mode for mirror updates
(CREATE_ELEMENT_REPLICA_MODE_SYNC,
 CREATE_ELEMENT_REPLICA_MODE_ASYNC) = (2, 3)

# SMI-S volume 'OperationalStatus' enumerations
(VOL_OP_STATUS_OK, VOL_OP_STATUS_DEGRADED, VOL_OP_STATUS_ERR,
 VOL_OP_STATUS_STARTING,
 VOL_OP_STATUS_DORMANT) = (2, 3, 6, 8, 15)

# SMI-S ExposePaths device access enumerations
(EXPOSE_PATHS_DA_READ_WRITE, EXPOSE_PATHS_DA_READ_ONLY) = (2, 3)