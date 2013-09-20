# Copyright (C) 2011-2013 Red Hat, Inc.
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
#
# Author: tasleson
from string import split

import time
import pywbem
from pywbem import CIMError

from iplugin import IStorageAreaNetwork
from common import uri_parse, LsmError, ErrorNumber, JobStatus, md5
from data import Pool, Initiator, Volume, AccessGroup, System, Capabilities,\
    Disk
from version import VERSION


def handle_cim_errors(method):
    def cim_wrapper(*args, **kwargs):
        try:
            return method(*args, **kwargs)
        except CIMError as ce:
            raise LsmError(ErrorNumber.PLUGIN_ERROR, str(ce))

    return cim_wrapper


class Smis(IStorageAreaNetwork):
    """
    SMI-S plug-ing which exposes a small subset of the overall provided
    functionality of SMI-S
    """

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

    # SMI-S CIM_ComputerSystem OperationalStatus for system
    class SystemOperationalStatus(object):
        UNKNOWN = 0
        OTHER = 1
        OK = 2
        DEGRADED = 3
        STRESSED = 4
        PREDICTIVE_FAILURE = 5
        ERROR = 6
        NON_RECOVERABLE_ERROR = 7
        STARTING = 8
        STOPPING = 9
        STOPPED = 10
        IN_SERVICE = 11
        NO_CONTACT = 12
        LOST_COMMUNICATION = 13
        ABORTED = 14
        DORMANT = 15
        SUPPORTING_ENTITY_IN_ERROR = 16
        COMPLETED = 17
        POWER_MODE = 18

    # SMI-S ExposePaths device access enumerations
    (EXPOSE_PATHS_DA_READ_WRITE, EXPOSE_PATHS_DA_READ_ONLY) = (2, 3)

    def __init__(self):
        self._c = None

    @handle_cim_errors
    def _get_cim_instance_by_id(self, class_type, requested_id,
                                flag_full_info=True):
        """
        Find out the CIM_XXXX Instance which holding the requested_id
        If flag_full_info == True, we return a Instance with full info.
        If you want to save some query time, try set it as False
        """
        class_name = self._cim_class_name_of(class_type)
        property_list = self._property_list_of_id(class_type)
        cim_xxxs = self._c.EnumerateInstances(class_name,
                                              PropertyList=property_list)
        for cim_xxx in cim_xxxs:
            if self._id(class_type, cim_xxx) == requested_id:
                if flag_full_info:
                    cim_xxx = self._c.GetInstance(cim_xxx.path)
                return cim_xxx

        raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                       "Cannot find %s Instance with " % class_name +
                       "%s ID %s" % (class_type, requested_id))

    def _get_class_instance(self, class_name, prop_name=None, prop_value=None,
                            no_throw_on_missing=False):
        """
        Gets an instance of a class that optionally matches a specific
        property name and value
        """
        instances = None

        try:
            if prop_name:
                instances = self._c.EnumerateInstances(
                    class_name, PropertyList=[prop_name])
            else:
                instances = self._c.EnumerateInstances(class_name)
        except CIMError as ce:
            error_code = tuple(ce)[0]

            if error_code == pywbem.CIM_ERR_INVALID_CLASS \
                    and no_throw_on_missing:
                return None
            else:
                raise ce

        if prop_name is None:
            if len(instances) != 1:
                class_names = " ".join([x.classname for x in instances])
                raise LsmError(ErrorNumber.INTERNAL_ERROR,
                               "Expecting one instance of %s and got %s" %
                               (class_name, class_names))

            return instances[0]
        else:
            for i in instances:
                if prop_name in i and i[prop_name] == prop_value:
                    return i

        if no_throw_on_missing:
            return None

        raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                       "Unable to find class instance %s " % class_name +
                       "with property %s " % prop_name +
                       "with value %s" % prop_value)

    def _get_spc(self, initiator_id, volume_id):
        """
        Retrieve the SCSIProtocolController for a given initiator and volume.
        This will return a non-none value when there is a mapping between the
        initiator and the volume.
        """
        init = self._get_class_instance('CIM_StorageHardwareID', 'StorageID',
                                        initiator_id)

        # Look at page 151 (1.5 smi-s spec.) in the block services books for
        # the SNIA_MappingProtocolControllerView

        if init:
            auths = self._c.Associators(init.path,
                                        AssocClass='CIM_AuthorizedSubject')

            if auths:
                for a in auths:
                    spc = self._c.Associators(
                        a.path, AssocClass='CIM_AuthorizedTarget')
                    if spc and len(spc) > 0:
                        logical_device = \
                            self._c.Associators(
                                spc[0].path,
                                AssocClass='CIM_ProtocolControllerForUnit')

                        if logical_device and len(logical_device) > 0:
                            vol = self._c.GetInstance(logical_device[0].path)
                            if 'DeviceID' in vol and \
                                    md5(vol.path) == volume_id:
                                return spc[0]
        return None

    def _pi(self, msg, retrieve_vol, rc, out):
        """
        Handle the the process of invoking an operation.
        """

        # Check to see if operation is done
        if rc == Smis.INVOKE_OK:
            if retrieve_vol:
                return None, self._new_vol_from_name(out)
            else:
                return None, None

        elif rc == Smis.INVOKE_ASYNC:
            # We have an async operation
            job_id = "%s@%s" % (md5(str(out['Job']['InstanceID'])),
                                str(retrieve_vol))
            return job_id, None
        elif rc == Smis.INVOKE_NOT_SUPPORTED:
            raise LsmError(
                ErrorNumber.NO_SUPPORT,
                'SMI-S error code indicates operation not supported')
        else:
            raise LsmError(ErrorNumber.PLUGIN_ERROR,
                           'Error: ' + msg + " rc= " + str(rc))

    def startup(self, uri, password, timeout, flags=0):
        """
        Called when the plug-in runner gets the start request from the client.
        """
        protocol = 'http'
        u = uri_parse(uri, ['scheme', 'netloc', 'host', 'port'],
                      ['namespace'])

        if u['scheme'].lower() == 'smispy+ssl':
            protocol = 'https'

        url = "%s://%s:%s" % (protocol, u['host'], u['port'])

        # System filtering
        self.system_list = None

        if 'systems' in u['parameters']:
            self.system_list = split(u['parameters']["systems"], ":")

        self.tmo = timeout
        self._c = pywbem.WBEMConnection(url, (u['username'], password),
                                        u['parameters']["namespace"])

    def set_time_out(self, ms, flags=0):
        self.tmo = ms

    def get_time_out(self, flags=0):
        return self.tmo

    def shutdown(self, flags=0):
        self._c = None
        self._jobs = None

    def _job_completed_ok(self, status):
        """
        Given a concrete job instance, check the operational status.  This
        is a little convoluted as different SMI-S proxies return the values in
        different positions in list :-)
        """
        rc = False
        op = status['OperationalStatus']

        if len(op) > 1 and \
                ((op[0] == Smis.JOB_OK and op[1] == Smis.JOB_COMPLETE) or
                (op[0] == Smis.JOB_COMPLETE and op[1] == Smis.JOB_OK)):
            rc = True

        return rc

    def _get_job_details(self, job_id):
        (job_id, get_vol) = job_id.split('@', 2)

        if get_vol == 'False':
            get_vol = False
        else:
            get_vol = True

        jobs = self._c.EnumerateInstances('CIM_ConcreteJob')

        for j in jobs:
            tmp_id = md5(j['InstanceID'])
            if tmp_id == job_id:
                return j, get_vol

        raise LsmError(ErrorNumber.NOT_FOUND_JOB, 'Non-existent job')

    def _job_progress(self, job_id):
        """
        Given a concrete job instance name, check the status
        """
        volume = None

        (concrete_job, get_vol) = self._get_job_details(job_id)

        job_state = concrete_job['JobState']

        if job_state in (Smis.JS_NEW, Smis.JS_STARTING, Smis.JS_RUNNING):
            status = JobStatus.INPROGRESS

            pc = concrete_job['PercentComplete']
            if pc > 100:
                percent_complete = 100
            else:
                percent_complete = pc

        elif job_state == Smis.JS_COMPLETED:
            status = JobStatus.COMPLETE
            percent_complete = 100

            if self._job_completed_ok(concrete_job):
                if get_vol:
                    volume = self._new_vol_from_job(concrete_job)
            else:
                status = JobStatus.ERROR

        else:
            raise LsmError(ErrorNumber.PLUGIN_ERROR,
                           str(concrete_job['ErrorDescription']))

        return status, percent_complete, volume

    def _scs_supported_capabilities(self, system, cap):
        """
        Interrogate the supported features of the Storage Configuration
        service
        """
        scs = self._get_class_instance('CIM_StorageConfigurationService',
                                       'SystemName', system.id)

        scs_cap_inst = self._c.Associators(
            scs.path,
            AssocClass='CIM_ElementCapabilities',
            ResultClass='CIM_StorageConfigurationCapabilities')[0]

        # print 'Async', scs_cap_inst['SupportedAsynchronousActions']
        # print 'Sync', scs_cap_inst['SupportedSynchronousActions']

        #TODO Get rid of magic numbers
        if 2 in scs_cap_inst['SupportedStorageElementTypes']:
            cap.set(Capabilities.VOLUMES)

        if 5 in scs_cap_inst['SupportedAsynchronousActions'] \
                or 5 in scs_cap_inst['SupportedSynchronousActions']:
            cap.set(Capabilities.VOLUME_CREATE)

        if 6 in scs_cap_inst['SupportedAsynchronousActions'] \
                or 6 in scs_cap_inst['SupportedSynchronousActions']:
            cap.set(Capabilities.VOLUME_DELETE)

        if 7 in scs_cap_inst['SupportedAsynchronousActions'] \
                or 7 in scs_cap_inst['SupportedSynchronousActions']:
            cap.set(Capabilities.VOLUME_RESIZE)

    def _rs_supported_capabilities(self, system, cap):
        """
        Interrogate the supported features of the replication service
        """
        rs = self._get_class_instance("CIM_ReplicationService", 'SystemName',
                                      system.id, True)

        if rs:
            rs_cap = self._c.Associators(
                rs.path,
                AssocClass='CIM_ElementCapabilities',
                ResultClass='CIM_ReplicationServiceCapabilities')[0]

            if self.RepSvc.Action.CREATE_ELEMENT_REPLICA in \
                    rs_cap['SupportedAsynchronousActions'] \
                    or self.RepSvc.Action.CREATE_ELEMENT_REPLICA in \
                    rs_cap['SupportedSynchronousActions']:
                cap.set(Capabilities.VOLUME_REPLICATE)

            # Mirror support is not working and is not supported at this time.
            # if self.RepSvc.RepTypes.SYNC_MIRROR_LOCAL in \
            #   rs_cap['SupportedReplicationTypes']:
            #    cap.set(Capabilities.DeviceID)

            # if self.RepSvc.RepTypes.ASYNC_MIRROR_LOCAL \
            #    in rs_cap['SupportedReplicationTypes']:
            #    cap.set(Capabilities.VOLUME_REPLICATE_MIRROR_ASYNC)

            if self.RepSvc.RepTypes.SYNC_SNAPSHOT_LOCAL \
                    or self.RepSvc.RepTypes.ASYNC_SNAPSHOT_LOCAL \
                    in rs_cap['SupportedReplicationTypes']:
                cap.set(Capabilities.VOLUME_REPLICATE_CLONE)

            if self.RepSvc.RepTypes.SYNC_CLONE_LOCAL \
                    or self.RepSvc.RepTypes.ASYNC_CLONE_LOCAL \
                    in rs_cap['SupportedReplicationTypes']:
                cap.set(Capabilities.VOLUME_REPLICATE_COPY)
        else:
            # Try older storage configuration service

            rs = self._get_class_instance("CIM_StorageConfigurationService",
                                          'SystemName',
                                          system.id, True)

            if rs:
                rs_cap = self._c.Associators(
                    rs.path,
                    AssocClass='CIM_ElementCapabilities',
                    ResultClass='CIM_StorageConfigurationCapabilities')[0]

                sct = rs_cap['SupportedCopyTypes']

                if len(sct):
                    cap.set(Capabilities.VOLUME_REPLICATE)

                    # Mirror support is not working and is not supported at
                    # this time.

                    # if Smis.CopyTypes.ASYNC in sct:
                    #    cap.set(Capabilities.VOLUME_REPLICATE_MIRROR_ASYNC)

                    # if Smis.CopyTypes.SYNC in sct:
                    #    cap.set(Capabilities.VOLUME_REPLICATE_MIRROR_SYNC)

                    if Smis.CopyTypes.UNSYNCASSOC in sct:
                        cap.set(Capabilities.VOLUME_REPLICATE_CLONE)

                    if Smis.CopyTypes.UNSYNCUNASSOC in sct:
                        cap.set(Capabilities.VOLUME_REPLICATE_COPY)

    def _pcm_supported_capabilities(self, system, cap):
        """
        Interrogate the supported features of
        CIM_ProtocolControllerMaskingCapabilities
        """

        # Get the cim object that represents the system
        cim_sys = self._systems(system.id)[0]

        # Get the protocol controller masking capabilities
        pcm = self._c.Associators(
            cim_sys.path,
            ResultClass='CIM_ProtocolControllerMaskingCapabilities')[0]

        cap.set(Capabilities.ACCESS_GROUP_LIST)

        if pcm['ExposePathsSupported']:
            cap.set(Capabilities.ACCESS_GROUP_GRANT)
            cap.set(Capabilities.ACCESS_GROUP_REVOKE)
            cap.set(Capabilities.ACCESS_GROUP_ADD_INITIATOR)
            cap.set(Capabilities.ACCESS_GROUP_DEL_INITIATOR)

        cap.set(Capabilities.ACCESS_GROUPS_GRANTED_TO_VOLUME)
        cap.set(Capabilities.VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP)

    def _common_capabilities(self, system):
        cap = Capabilities()

        # Assume that the SMI-S we are talking to supports blocks
        cap.set(Capabilities.BLOCK_SUPPORT)
        cap.set(Capabilities.INITIATORS)

        self._scs_supported_capabilities(system, cap)
        self._rs_supported_capabilities(system, cap)
        return cap

    @handle_cim_errors
    def capabilities(self, system, flags=0):
        cap = self._common_capabilities(system)
        self._pcm_supported_capabilities(system, cap)
        return cap

    @handle_cim_errors
    def plugin_info(self, flags=0):
        return "Generic SMI-S support", VERSION

    @handle_cim_errors
    def job_status(self, job_id, flags=0):
        """
        Given a job id returns the current status as a tuple
        (status (enum), percent_complete(integer), volume (None or Volume))
        """
        return self._job_progress(job_id)

    @staticmethod
    def _cim_class_name_of(class_type):
        if class_type == 'Volume':
            return 'CIM_StorageVolume'
        if class_type == 'System':
            return 'CIM_ComputerSystem'
        if class_type == 'Pool':
            return 'CIM_StoragePool'
        if class_type == 'Disk':
            return 'CIM_DiskDrive'
        raise LsmError(ErrorNumber.INTERNAL_ERROR,
                       "self._cim_class_name_of() got unknown " +
                       "class_type %s" % class_type)

    @staticmethod
    def _property_list_of_id(class_type):
        """
        Return a PropertyList which the ID of current class is basing on
        """
        if class_type == 'Volume':
            return ['SystemName', 'DeviceID']
        if class_type == 'System':
            return ['Name']
        if class_type == 'Pool':
            return ['InstanceID']
        if class_type == 'SystemChild':
            return ['SystemName']
        if class_type == 'Disk':
            return ['SystemName', 'DeviceID']
        raise LsmError(ErrorNumber.INTERNAL_ERROR,
                       "self._cim_class_name_of() got unknown " +
                       "class_type %s" % class_type)

    def _sys_id_child(self, cim_xxx):
        """
        Find out the system id of Pool/Volume/Disk and etc
        Currently, we just use SystemName of cim_xxx
        """
        return self._id('SystemChild', cim_xxx)

    def _sys_id(self, cim_sys):
        """
        Return CIM_ComputerSystem['SystemName']
        """
        return self._id('System', cim_sys)

    def _pool_id(self, cim_pool):
        """
        Return CIM_StoragePool['InstanceID']
        """
        return self._id('Pool', cim_pool)

    def _vol_id(self, cim_vol):
        """
        Return the MD5 hash of CIM_StorageVolume['SystemName'] and
        ['DeviceID']
        """
        return self._id('Volume', cim_vol)

    def _disk_id(self, cim_disk):
        """
        Return the MD5 hash of CIM_DiskDrive['SystemName'] and ['DeviceID']
        """
        return self._id('Disk', cim_disk)

    def _id(self, class_type, cim_xxx):
        """
        Return the ID of certain class.
        When ID is based on two or more properties, we use MD5 hash of them.
        If not, return the property value.
        """
        property_list = self._property_list_of_id(class_type)
        for key in property_list:
            if key not in cim_xxx:
                cim_xxx = self._c.GetInstance(cim_xxx.path,
                                              PropertyList=property_list)
                break

        id_str = ''
        for key in property_list:
            if key not in cim_xxx:
                cim_class_name = ''
                if class_type == 'SystemChild':
                    cim_class_name = str(cim_xxx.classname)
                else:
                    cim_class_name = self._cim_class_name_of(class_type)
                raise LsmError(ErrorNumber.NO_SUPPORT,
                               "%s %s " % (cim_class_name, cim_xxx.path) +
                               "does not have property %s" % str(key) +
                               "calculate out %s id" % class_type)
            else:
                id_str += cim_xxx[key]
        if len(property_list) == 1:
            return id_str
        else:
            return md5(id_str)

    @handle_cim_errors
    def _get_pool_from_vol(self, cim_vol):
        """
         Takes a CIMInstance that represents a volume and returns the pool
         id for that volume.
        """
        property_list = self._property_list_of_id('Pool')
        cim_pool = self._c.Associators(
            cim_vol.path,
            AssocClass='CIM_AllocatedFromStoragePool',
            ResultClass='CIM_StoragePool',
            PropertyList=property_list)[0]
        return self._pool_id(cim_pool)

    @staticmethod
    def _get_vol_other_id_info(cv):
        other_id = None

        if 'OtherIdentifyingInfo' in cv \
                and cv["OtherIdentifyingInfo"] is not None \
                and len(cv["OtherIdentifyingInfo"]) > 0:

            other_id = cv["OtherIdentifyingInfo"]

            if isinstance(other_id, list):
                other_id = other_id[0]

            # This is not what we are looking for if the field has this value
            if other_id is not None and other_id == "VPD83Type3":
                other_id = None

        return other_id

    def _new_vol(self, cv, pool_id=None):
        """
        Takes a CIMInstance that represents a volume and returns a lsm Volume
        """

        # Reference page 134 in 1.5 spec.
        status = Volume.STATUS_UNKNOWN

        # OperationalStatus is mandatory
        if 'OperationalStatus' in cv:
            for s in cv["OperationalStatus"]:
                if s == Smis.VOL_OP_STATUS_OK:
                    status |= Volume.STATUS_OK
                elif s == Smis.VOL_OP_STATUS_DEGRADED:
                    status |= Volume.STATUS_DEGRADED
                elif s == Smis.VOL_OP_STATUS_ERR:
                    status |= Volume.STATUS_ERR
                elif s == Smis.VOL_OP_STATUS_STARTING:
                    status |= Volume.STATUS_STARTING
                elif s == Smis.VOL_OP_STATUS_DORMANT:
                    status |= Volume.STATUS_DORMANT

        # This is optional (User friendly name)
        if 'ElementName' in cv:
            user_name = cv["ElementName"]
        else:
            #Better fallback value?
            user_name = cv['DeviceID']

        vpd_83 = self._vpd83_in_cv_name(cv)
        if vpd_83 is None:
            vpd_83 = self._vpd83_in_cv_otherinfo(cv)

        if vpd_83 is None:
            vpd_83 = self._vpd83_in_cv_ibm_xiv(cv)

        if vpd_83 is None:
            vpd_83 = ''

        #This is a fairly expensive operation, so it's in our best interest
        #to not call this very often.
        if pool_id is None:
            #Go an retrieve the pool id
            pool_id = self._get_pool_from_vol(cv)

        return Volume(self._vol_id(cv), user_name, vpd_83, cv["BlockSize"],
                      cv["NumberOfBlocks"], status, cv['SystemName'], pool_id)

    def _vpd83_in_cv_name(self, cv):
        """
        SMI-S 1.6 r4 SPEC part3 5.8.49 Table 81 Page 145, PDF Page 185:
              Table 81 - SMI ReferencedProperties/Methods for
                         CIM_StorageVolume
        NameFomat:
          1   (Other)
          2   (VPD83NAA6)
          3   (VPD83NAA5)
          4   (VPD83Type2)
          5   (VPD83Type1)
          6   (VPD83Type0)
          7   (SNVM)
          8   (NodeWWN)
          9   (NAA)
          10  (EUI64)
          11  (T10VID).
        Explanation of these Format is in:
          SMI-S 1.6 r4 SPEC part1 7.6.2 Table 2 Page 38, PDF Page 60:
              Table 2 - Standard Formats for StorageVolume Names
        Only these combinations is allowed when storing VPD83 in cv["Name"]:
         * NameFormat = NAA(9), NameNamespace = VPD83Type3(1)
            SCSI VPD page 83, type 3h, Association=0, NAA 0010b
            NAA name with first nibble of 2. Formatted as 16 un-separated
            upper case hex digits
         * NameFormat = NAA(9), NameNamespace = VPD83Type3(2)
            SCSI VPD page 83, type 3h, Association=0, NAA 0001b
            NAA name with first nibble of 1. Formatted as 16 un-separated
            upper case hex digits
         * NameFormat = EUI64(10), NameNamespace = VPD83Type2(3)
            SCSI VPD page 83, type 2h, Association=0
            Formatted as 16, 24, or 32 un-separated upper case hex digits
         * NameFormat = T10VID(11), NameNamespace = VPD83Type1(4)
            SCSI VPD page 83, type 1h, Association=0
            Formatted as 1 to 252 bytes of ASCII.
        Will return vpd_83 if found.
        """
        if not ('NameFormat' in cv and
                'NameNamespace' in cv and
                'Name' in cv):
            return None
        nf = cv['NameFormat']
        nn = cv['NameNamespace']
        name = cv['Name']
        if not (nf and nn and name):
            return None
        if (nf == 9 and nn == 1) or \
           (nf == 9 and nn == 2) or \
           (nf == 10 and nn == 3) or \
           (nf == 11 and nn == 4):
            return name

    def _vpd83_in_cv_otherinfo(self, cv):
        """
        In SNIA SMI-S 1.6 r4 part 1 section 7.6.2: "Standard Formats for
        Logical Unit Names" it allow VPD83 stored in 'OtherIdentifyingInfo'
        Quote:
            Storage volumes may have multiple standard names. A page 83
            logical unit identifier shall be placed in the Name property with
            NameFormat and Namespace set as specified in Table 2. Each
            additional name should be placed in an element of
            OtherIdentifyingInfo. The corresponding element in
            IdentifyingDescriptions shall contain a string from the Values
            lists from NameFormat and NameNamespace, separated by a
            semi-colon. For example, an identifier from SCSI VPD page 83 with
            type 3, association 0, and NAA 0101b - the corresponding entry in
            IdentifyingDescriptions[] shall be "NAA;VPD83Type3".
        Will return the vpd_83 value if found
        """
        vpd83_namespaces = ['NAA;VPD83Type1', 'NAA;VPD83Type3',
                            'EUI64;VPD83Type2', 'T10VID;VPD83Type1']
        if not ("IdentifyingDescriptions" in cv and
                "OtherIdentifyingInfo" in cv):
            return None
        id_des = cv["IdentifyingDescriptions"]
        other_info = cv["OtherIdentifyingInfo"]
        if not (isinstance(cv["IdentifyingDescriptions"], list) and
                isinstance(cv["OtherIdentifyingInfo"], list)):
            return None

        index = 0
        len_id_des = len(id_des)
        len_other_info = len(other_info)
        while index < min(len_id_des, len_other_info):
            if [1 for x in vpd83_namespaces if x == id_des[index]]:
                return other_info[index]
            index += 1
        return None

    def _vpd83_in_cv_ibm_xiv(self, cv):
        """
        IBM XIV IBM.2810-MX90014 is not following SNIA standard.
        They are using NameFormat=NodeWWN(8) and
        NameNamespace=NodeWWN(6) and no otherinfo indicated the
        VPD 83 info.
        Its cv["Name"] is equal to VPD 83, will use it.
        """
        if not "CreationClassName" in cv:
            return None
        if cv["CreationClassName"] == "IBMTSDS_SEVolume":
            if "Name" in cv and cv["Name"]:
                return cv["Name"]

    def _new_vol_from_name(self, out):
        """
        Given a volume by CIMInstanceName, return a lsm Volume object
        """
        instance = None

        if 'TheElement' in out:
            instance = self._c.GetInstance(out['TheElement'])
        elif 'TargetElement' in out:
            instance = self._c.GetInstance(out['TargetElement'])

        return self._new_vol(instance)

    def _new_access_group(self, g):
        return AccessGroup(g['DeviceID'], g['ElementName'],
                           self._get_initiators_in_group(g, 'StorageID'),
                           g['SystemName'])

    def _new_vol_from_job(self, job):
        """
        Given a concrete job instance, return referenced volume as lsm volume
        """
        associations = self._c.Associators(job.path,
                                           ResultClass='CIM_StorageVolume')

        for a in associations:
            return self._new_vol(self._c.GetInstance(a.path))
        return None

    @handle_cim_errors
    def volumes(self, flags=0):
        """
        Return all volumes.
        """
        rc = []
        systems = self._systems()
        for s in systems:
            pools = self._pools(s)

            for p in pools:
                vols = self._c.Associators(
                    p.path, ResultClass='CIM_StorageVolume')
                rc.extend([self._new_vol(v, p['InstanceID']) for v in vols])

        return rc

    @handle_cim_errors
    def _systems(self, system_name=None):
        """
        Returns a list of system objects (CIM)
        """
        cim_syss = []
        cim_scss_path = []
        try:
            # Note: Please be informed, if PropertyList is an empty list,
            #       XIV will return NOTHING, so use EnumerateInstanceNames()
            #       when you need nothing but the CIMInstanceName
            cim_scss_path = \
                self._c.EnumerateInstanceNames(
                    'CIM_StorageConfigurationService')
        except CIMError as e:
            if e[0] == pywbem.CIM_ERR_INVALID_CLASS:
                # If array does not support CIM_StorageConfigurationService
                # we use CIM_ComputerSystem which is mandatory.
                # We might get some non-storage array listed as system.
                # but we would like to take that risk instead of
                # skipping basic support of old SMIS provider.
                cim_syss = \
                    self._c.EnumerateInstances(
                        'CIM_ComputerSystem',
                        PropertyList=['Name',
                        'ElementName',
                        'OperationalStatus'])
            else:
                raise e
        if not cim_syss:
            for cim_scs_path in cim_scss_path:
                cim_tmp = \
                    self._c.Associators(cim_scs_path,
                                        AssocClass='CIM_HostedService',
                                        ResultClass='CIM_ComputerSystem',
                                        PropertyList=['Name',
                                                    'ElementName',
                                                    'OperationalStatus'])
                if cim_tmp and cim_tmp[0]:
                    cim_syss.extend([cim_tmp[0]])

        # Filtering
        if system_name is not None:
            for cim_sys in cim_syss:
                if cim_sys['Name'] == system_name:
                    return [cim_sys]

        elif self.system_list:
            cim_filterd_syss = []
            for cim_sys in cim_syss:
                if cim_sys['Name'] in self.system_list:
                    cim_filterd_syss.extend([cim_sys])
            return cim_filterd_syss
        else:
            return cim_syss

    def _pools(self, system):
        pools = self._c.Associators(system.path,
                                    AssocClass='CIM_HostedStoragePool',
                                    ResultClass='CIM_StoragePool')

        return [p for p in pools if not p["Primordial"]]

    @handle_cim_errors
    def pools(self, flags=0):
        """
        Return all pools
        """
        rc = []
        systems = self._systems()
        for s in systems:
            pools = self._pools(s)

            rc.extend(
                [Pool(p['InstanceID'], p["ElementName"],
                      p["TotalManagedSpace"],
                      p["RemainingManagedSpace"],
                      s['Name']) for p in pools])

        return rc

    def _new_system(self, s):
        # In the case of systems we are assuming that the System Name is
        # unique.
        status = System.STATUS_UNKNOWN

        if 'OperationalStatus' in s:
            for os in s['OperationalStatus']:
                if os == Smis.SystemOperationalStatus.OK:
                    status |= System.STATUS_OK
                elif os == Smis.SystemOperationalStatus.DEGRADED:
                    status |= System.STATUS_DEGRADED
                elif os == Smis.SystemOperationalStatus.ERROR or \
                        Smis.SystemOperationalStatus.STRESSED or \
                        Smis.SystemOperationalStatus.NON_RECOVERABLE_ERROR:
                    status |= System.STATUS_ERROR
                elif os == Smis.SystemOperationalStatus.PREDICTIVE_FAILURE:
                    status |= System.STATUS_PREDICTIVE_FAILURE

        return System(s['Name'], s['ElementName'], status)

    @handle_cim_errors
    def systems(self, flags=0):
        """
        Return the storage arrays accessible from this plug-in at this time
        """
        return [self._new_system(s) for s in self._systems()]

    @staticmethod
    def _to_init(i):
        return Initiator(i['StorageID'], i["IDType"], i["ElementName"])

    @handle_cim_errors
    def initiators(self, flags=0):
        """
        Return all initiators.
        """
        initiators = self._c.EnumerateInstances('CIM_StorageHardwareID')
        return [Smis._to_init(i) for i in initiators]

    @handle_cim_errors
    def volume_create(self, pool, volume_name, size_bytes, provisioning,
                      flags=0):
        """
        Create a volume.
        """
        if provisioning != Volume.PROVISION_DEFAULT:
            raise LsmError(ErrorNumber.UNSUPPORTED_PROVISIONING,
                           "Unsupported provisioning")

        # Get the Configuration service for the system we are interested in.
        scs = self._get_class_instance('CIM_StorageConfigurationService',
                                       'SystemName', pool.system_id)
        sp = self._get_cim_instance_by_id('Pool', pool.id, False)

        in_params = {'ElementName': volume_name,
                     'ElementType': pywbem.Uint16(2),
                     'InPool': sp.path,
                     'Size': pywbem.Uint64(size_bytes)}

        return self._pi("volume_create", True,
                        *(self._c.InvokeMethod(
                            'CreateOrModifyElementFromStoragePool',
                            scs.path, **in_params)))

    def _poll(self, msg, job):
        if job:
            while True:
                (s, percent, i) = self.job_status(job)

                if s == JobStatus.INPROGRESS:
                    time.sleep(0.25)
                elif s == JobStatus.COMPLETE:
                    self.job_free(job)
                    return i
                else:
                    raise LsmError(
                        ErrorNumber.PLUGIN_ERROR,
                        msg + ", job error code= " + str(s))

    def _detach(self, vol, sync):
        rs = self._get_class_instance("CIM_ReplicationService", 'SystemName',
                                      vol.system_id, True)

        if rs:
            in_params = {'Operation': pywbem.Uint16(8),
                         'Synchronization': sync.path}

            job_id = self._pi("_detach", False,
                              *(self._c.InvokeMethod(
                                  'ModifyReplicaSynchronization', rs.path,
                                  **in_params)))[0]

            self._poll("ModifyReplicaSynchronization, detach", job_id)

    def _cim_name_match(self, a, b):
        if a['DeviceID'] == b['DeviceID'] \
                and a['SystemName'] == b['SystemName'] \
                and a['SystemCreationClassName'] == \
                b['SystemCreationClassName']:
            return True
        else:
            return False

    def _check_volume_associations(self, vol):
        """
        Check a volume to see if it has any associations with other
        volumes.
        """
        lun = self._get_cim_instance_by_id('Volume', vol.id)

        try:
            ss = self._c.References(lun.path,
                                    ResultClass='CIM_StorageSynchronized')
        except pywbem.CIMError as e:
            if e[0] == pywbem.CIM_ERR_INVALID_CLASS:
                return
            else:
                raise e

        lp = lun.path

        if len(ss):
            for s in ss:
                # TODO: Need to see if detach is a supported operation in
                # replication capabilities.
                #
                # TODO: Theory of delete.  Some arrays will automatically
                # detach a clone, check
                # ReplicationServiceCapabilities.GetSupportedFeatures() and
                # look for "Synchronized clone target detaches automatically".
                # If not automatic then detach manually.  However, we have
                # seen arrays that don't report detach automatically that
                # don't need a detach.
                #
                # This code needs to be re-investigated to work with a wide
                # range of array vendors.

                if s[
                    'SyncState'] == Smis.Synchronized.SyncState.SYNCHRONIZED \
                        and (s['CopyType'] != Smis.CopyTypes.UNSYNCASSOC):
                    if 'SyncedElement' in s:
                        item = s['SyncedElement']

                        if self._cim_name_match(item, lp):
                            self._detach(vol, s)

                    if 'SystemElement' in s:
                        item = s['SystemElement']

                        if self._cim_name_match(item, lp):
                            self._detach(vol, s)

    @handle_cim_errors
    def volume_delete(self, volume, flags=0):
        """
        Delete a volume
        """
        scs = self._get_class_instance('CIM_StorageConfigurationService',
                                       'SystemName', volume.system_id)
        lun = self._get_cim_instance_by_id('Volume', volume.id)

        self._check_volume_associations(volume)

        in_params = {'TheElement': lun.path}

        # Delete returns None or Job number
        return self._pi("volume_delete", False,
                        *(self._c.InvokeMethod('ReturnToStoragePool',
                                               scs.path,
                                               **in_params)))[0]

    @handle_cim_errors
    def volume_resize(self, volume, new_size_bytes, flags=0):
        """
        Re-size a volume
        """
        scs = self._get_class_instance('CIM_StorageConfigurationService',
                                       'SystemName', volume.system_id)
        lun = self._get_cim_instance_by_id('Volume', volume.id)

        in_params = {'ElementType': pywbem.Uint16(2),
                     'TheElement': lun.path,
                     'Size': pywbem.Uint64(new_size_bytes)}

        return self._pi("volume_resize", True,
                        *(self._c.InvokeMethod(
                            'CreateOrModifyElementFromStoragePool',
                            scs.path, **in_params)))

    def _get_supported_sync_and_mode(self, system_id, rep_type):
        """
        Converts from a library capability to a suitable array capability

        returns a tuple (sync, mode)
        """
        rc = [None, None]

        rs = self._get_class_instance("CIM_ReplicationService", 'SystemName',
                                      system_id, True)

        if rs:
            rs_cap = self._c.Associators(
                rs.path,
                AssocClass='CIM_ElementCapabilities',
                ResultClass='CIM_ReplicationServiceCapabilities')[0]

            s_rt = rs_cap['SupportedReplicationTypes']

            if rep_type == Volume.REPLICATE_COPY:
                if self.RepSvc.RepTypes.SYNC_CLONE_LOCAL in s_rt:
                    rc[0] = Smis.SYNC_TYPE_CLONE
                    rc[1] = Smis.CREATE_ELEMENT_REPLICA_MODE_SYNC
                elif self.RepSvc.RepTypes.ASYNC_CLONE_LOCAL in s_rt:
                    rc[0] = Smis.SYNC_TYPE_CLONE
                    rc[1] = Smis.CREATE_ELEMENT_REPLICA_MODE_ASYNC

            elif rep_type == Volume.REPLICATE_MIRROR_ASYNC:
                if self.RepSvc.RepTypes.ASYNC_MIRROR_LOCAL in s_rt:
                    rc[0] = Smis.SYNC_TYPE_MIRROR
                    rc[1] = Smis.CREATE_ELEMENT_REPLICA_MODE_ASYNC

            elif rep_type == Volume.REPLICATE_MIRROR_SYNC:
                if self.RepSvc.RepTypes.SYNC_MIRROR_LOCAL in s_rt:
                    rc[0] = Smis.SYNC_TYPE_MIRROR
                    rc[1] = Smis.CREATE_ELEMENT_REPLICA_MODE_SYNC

            elif rep_type == Volume.REPLICATE_CLONE \
                    or rep_type == Volume.REPLICATE_SNAPSHOT:
                if self.RepSvc.RepTypes.SYNC_CLONE_LOCAL in s_rt:
                    rc[0] = Smis.SYNC_TYPE_SNAPSHOT
                    rc[1] = Smis.CREATE_ELEMENT_REPLICA_MODE_SYNC
                elif self.RepSvc.RepTypes.ASYNC_CLONE_LOCAL in s_rt:
                    rc[0] = Smis.SYNC_TYPE_SNAPSHOT
                    rc[1] = Smis.CREATE_ELEMENT_REPLICA_MODE_ASYNC

        if rc[0] is None:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "Replication type not supported")

        return tuple(rc)

    @handle_cim_errors
    def volume_replicate(self, pool, rep_type, volume_src, name, flags=0):
        """
        Replicate a volume
        """
        if rep_type == Volume.REPLICATE_MIRROR_ASYNC \
                or rep_type == Volume.REPLICATE_MIRROR_SYNC:
            raise LsmError(ErrorNumber.NO_SUPPORT, "Mirroring not supported")

        rs = self._get_class_instance("CIM_ReplicationService", 'SystemName',
                                      volume_src.system_id, True)

        if pool is not None:
            cim_pool = self._get_cim_instance_by_id('Pool', pool.id, False)
        else:
            cim_pool = None

        lun = self._get_cim_instance_by_id('Volume', volume_src.id)

        if rs:
            method = 'CreateElementReplica'

            sync, mode = self._get_supported_sync_and_mode(
                volume_src.system_id, rep_type)

            in_params = {'ElementName': name,
                         'SyncType': pywbem.Uint16(sync),
                         'Mode': pywbem.Uint16(mode),
                         'SourceElement': lun.path,
                         'WaitForCopyState':
                         pywbem.Uint16(Smis.CopyStates.SYNCHRONIZED)}

        else:
            # Check for older support via storage configuration service

            method = 'CreateReplica'

            # Check for storage configuration service
            rs = self._get_class_instance("CIM_StorageConfigurationService",
                                          'SystemName', volume_src.system_id,
                                          True)

            ct = Volume.REPLICATE_CLONE
            if rep_type == Volume.REPLICATE_CLONE:
                ct = Smis.CopyTypes.UNSYNCASSOC
            elif rep_type == Volume.REPLICATE_COPY:
                ct = Smis.CopyTypes.UNSYNCUNASSOC
            elif rep_type == Volume.REPLICATE_MIRROR_ASYNC:
                ct = Smis.CopyTypes.ASYNC
            elif rep_type == Volume.REPLICATE_MIRROR_SYNC:
                ct = Smis.CopyTypes.SYNC

            in_params = {'ElementName': name,
                         'CopyType': pywbem.Uint16(ct),
                         'SourceElement': lun.path}
        if rs:

            if cim_pool is not None:
                in_params['TargetPool'] = cim_pool.path

            return self._pi("volume_replicate", True,
                            *(self._c.InvokeMethod(method,
                                                   rs.path, **in_params)))

        raise LsmError(ErrorNumber.NO_SUPPORT,
                       "volume-replicate not supported")

    def volume_replicate_range_block_size(self, system, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volume_replicate_range(self, rep_type, volume_src, volume_dest,
                               ranges,
                               flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_cim_errors
    def volume_online(self, volume, flags=0):
        return None

    @handle_cim_errors
    def volume_offline(self, volume, flags=0):
        return None

    @handle_cim_errors
    def _initiator_create(self, name, init_id, id_type):
        """
        Create initiator object
        """
        hardware = self._get_class_instance(
            'CIM_StorageHardwareIDManagementService')

        in_params = {'ElementName': name,
                     'StorageID': init_id,
                     'IDType': pywbem.Uint16(id_type)}

        (rc, out) = self._c.InvokeMethod('CreateStorageHardwareID',
                                         hardware.path, **in_params)
        if not rc:
            init = self._get_class_instance('CIM_StorageHardwareID',
                                            'StorageID', init_id)
            return Smis._to_init(init)

        raise LsmError(ErrorNumber.PLUGIN_ERROR, 'Error: ' + str(rc) +
                                                 ' on initiator_create!')

    @handle_cim_errors
    def access_group_grant(self, group, volume, access, flags=0):
        """
        Grant access to a volume to an group
        """
        ccs = self._get_class_instance('CIM_ControllerConfigurationService',
                                       'SystemName', group.system_id)
        lun = self._get_cim_instance_by_id('Volume', volume.id)
        spc = self._get_access_group(group.id)

        if not lun:
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME, "Volume not present")

        if not spc:
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "Access group not present")

        if access == Volume.ACCESS_READ_ONLY:
            da = Smis.EXPOSE_PATHS_DA_READ_ONLY
        else:
            da = Smis.EXPOSE_PATHS_DA_READ_WRITE

        in_params = {'LUNames': [lun['Name']],
                     'ProtocolControllers': [spc.path],
                     'DeviceAccesses': [pywbem.Uint16(da)]}

        # Returns None or job id
        return self._pi("access_grant", False,
                        *(self._c.InvokeMethod('ExposePaths', ccs.path,
                                               **in_params)))[0]

    def _wait(self, job):

        status = self.job_status(job)[0]

        while JobStatus.COMPLETE != status:
            time.sleep(0.5)
            status = self.job_status(job)[0]

        if JobStatus.COMPLETE != status:
            raise LsmError(ErrorNumber.PLUGIN_ERROR,
                           "Expected no errors %s %s" % (job, str(status)))

    @handle_cim_errors
    def access_group_revoke(self, group, volume, flags=0):
        ccs = self._get_class_instance('CIM_ControllerConfigurationService',
                                       'SystemName', volume.system_id)
        lun = self._get_cim_instance_by_id('Volume', volume.id)
        spc = self._get_access_group(group.id)

        if not lun:
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME, "Volume not present")

        if not spc:
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "Access group not present")

        hide_params = {'LUNames': [lun['Name']],
                       'ProtocolControllers': [spc.path]}
        return self._pi("HidePaths", False,
                        *(self._c.InvokeMethod('HidePaths', ccs.path,
                                               **hide_params)))[0]

    def _is_access_group(self, s):
        rc = False

        # This seems horribly wrong for something that is a standard.
        if 'Name' in s and s['Name'] == 'Storage Group':
            # EMC
            rc = True
        elif 'DeviceID' in s and s['DeviceID'][0:3] == 'SPC':
            # NetApp
            rc = True
        return rc

    def _get_access_groups(self):
        rc = []

        # System filtering
        if self.system_list:
            systems = self._systems()
            for s in systems:
                spc = self._c.Associators(
                    s.path,
                    AssocClass='CIM_SystemDevice',
                    ResultClass='CIM_SCSIProtocolController')
                for s in spc:
                    if self._is_access_group(s):
                        rc.append(s)

        else:
            spc = self._c.EnumerateInstances('CIM_SCSIProtocolController')

            for s in spc:
                if self._is_access_group(s):
                    rc.append(s)

        return rc

    def _get_access_group(self, ag_id):
        groups = self._get_access_groups()
        for g in groups:
            if g['DeviceID'] == ag_id:
                return g

        return None

    def _get_initiators_in_group(self, group, field=None):
        rc = []

        ap = self._c.Associators(group.path,
                                 AssocClass='CIM_AuthorizedTarget')

        if len(ap):
            for a in ap:
                inits = self._c.Associators(
                    a.path, AssocClass='CIM_AuthorizedSubject')
                for i in inits:
                    if field is None:
                        rc.append(i)
                    else:
                        rc.append(i[field])

        return rc

    @handle_cim_errors
    def volumes_accessible_by_access_group(self, group, flags=0):
        g = self._get_class_instance('CIM_SCSIProtocolController', 'DeviceID',
                                     group.id)
        if g:
            logical_units = self._c.Associators(
                g.path, AssocClass='CIM_ProtocolControllerForUnit')
            return [self._new_vol(v) for v in logical_units]
        else:
            raise LsmError(
                ErrorNumber.PLUGIN_ERROR,
                'Error: access group %s does not exist!' % group.id)

    @handle_cim_errors
    def access_groups_granted_to_volume(self, volume, flags=0):
        vol = self._get_cim_instance_by_id('Volume', volume.id)

        if vol:
            access_groups = self._c.Associators(
                vol.path,
                AssocClass='CIM_ProtocolControllerForUnit')
            return [self._new_access_group(g) for g in access_groups]
        else:
            raise LsmError(
                ErrorNumber.PLUGIN_ERROR,
                'Error: access group %s does not exist!' % volume.id)

    @handle_cim_errors
    def access_group_list(self, flags=0):
        groups = self._get_access_groups()
        return [self._new_access_group(g) for g in groups]

    @handle_cim_errors
    def access_group_create(self, name, initiator_id, id_type, system_id,
                            flags=0):
        # page 880 1.5 spec. CreateMaskingView
        #
        # No access to a provider that implements this at this time,
        # so unable to develop and test.
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_cim_errors
    def access_group_del(self, group, flags=0):
        # page 880 1.5 spec. DeleteMaskingView
        #
        # No access to a provider that implements this at this time,
        # so unable to develop and test.
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def _initiator_lookup(self, initiator_id):
        """
        Looks up an initiator by initiator id
        returns None or object instance
        """
        init_list = self.initiators()
        initiator = None
        for i in init_list:
            if i.id == initiator_id:
                initiator = i
                break
        return initiator

    @handle_cim_errors
    def access_group_add_initiator(self, group, initiator_id, id_type,
                                   flags=0):
        # Check to see if we have this initiator already, if we don't create
        # it and then add to the view.
        spc = self._get_access_group(group.id)

        initiator = self._initiator_lookup(initiator_id)

        if not initiator:
            initiator = self._initiator_create(initiator_id, initiator_id,
                                               id_type)

        ccs = self._get_class_instance('CIM_ControllerConfigurationService',
                                       'SystemName', group.system_id)

        in_params = {'InitiatorPortIDs': [initiator.id],
                     'ProtocolControllers': [spc.path]}

        # Returns None or job id
        return self._pi("access_group_add_initiator", False,
                        *(self._c.InvokeMethod('ExposePaths', ccs.path,
                                               **in_params)))[0]

    @handle_cim_errors
    def access_group_del_initiator(self, group, initiator, flags=0):
        spc = self._get_access_group(group.id)
        ccs = self._get_class_instance('CIM_ControllerConfigurationService',
                                       'SystemName', group.system_id)

        hide_params = {'InitiatorPortIDs': [initiator],
                       'ProtocolControllers': [spc.path]}
        return self._pi("HidePaths", False,
                        *(self._c.InvokeMethod('HidePaths', ccs.path,
                                               **hide_params)))[0]

    @handle_cim_errors
    def iscsi_chap_auth(self, initiator, in_user, in_password, out_user,
                        out_password, flags):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_cim_errors
    def initiator_grant(self, initiator_id, initiator_type, volume, access,
                        flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_cim_errors
    def initiator_revoke(self, initiator, volume, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_cim_errors
    def volumes_accessible_by_initiator(self, initiator, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_cim_errors
    def initiators_granted_to_volume(self, volume, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_cim_errors
    def job_free(self, job_id, flags=0):
        """
        Frees the resources given a job number.
        """
        concrete_job = self._get_job_details(job_id)[0]

        # See if we should delete the job
        if not concrete_job['DeleteOnCompletion']:
            try:
                self._c.DeleteInstance(concrete_job.path)
            except CIMError:
                pass

    def volume_child_dependency(self, volume, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volume_child_dependency_rm(self, volume, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_cim_errors
    def disks(self, flags=0):
        """
        return all object of data.Disk.
        We are using "Disk Drive Lite Subprofile" of SNIA SMI-S for these
        classes:
            CIM_PhysicalPackage
            CIM_DiskDrive
            CIM_StorageExtent (Primordial)
        Will try these steps to find out disk infomation:
            1. Find out all associated disks: 'CIM_DiskDrive'.
            2. Find out all association storage extension: 'CIM_StorageExtent'.
            3. We will use vendor specific way for all workarounds.
        """
        rc = []
        cim_syss = self._systems()
        for cim_sys in cim_syss:
            cim_disk_pros = self._new_disk_cim_disk_pros()
            cim_disks = self._c.Associators(cim_sys.path,
                                            ResultClass='CIM_DiskDrive',
                                            PropertyList=cim_disk_pros)
            for cim_disk in cim_disks:
                cim_ext_pros = self._new_disk_cim_ext_pros()
                cim_ext = self._pri_cim_ext_of_cim_disk(cim_disk.path,
                                                        cim_ext_pros)
                rc.extend([self._new_disk(cim_disk, cim_ext)])
        return rc

    @staticmethod
    def _new_disk_cim_disk_pros():
        """
        Return all CIM_DiskDrive Properties needed to create a Disk object.
        """
        return ['OperationalStatus', 'EnabledState', 'Name', 'SystemName',
                'DeviceID', 'HealthState', 'ErrorDescription', 'ErrorCleared',
                'PredictiveFailureCount', 'MediaErrorCount', 'Caption',
                'InterconnectType', 'DiskType']

    @staticmethod
    def _new_disk_cim_ext_pros():
        """
        Return all CIM_StorageExtent Properties needed to create a Disk
        object.
        """
        return ['BlockSize', 'NumberOfBlocks']

    @staticmethod
    def _new_disk_cim_phy_pkg_pros():
        """
        Return all CIM_PhysicalPackage Properties needed to create a Disk
        object.
        """
        return ['BlockSize', 'NumberOfBlocks', 'SerialNumber', 'PartNumber',
                'Manufacturer', 'Model']

    @handle_cim_errors
    def _new_disk(self, cim_disk, cim_ext):
        """
        Takes a CIM_DiskDrive and CIM_StorageExtent, returns a lsm Disk
        Assuming cim_disk and cim_ext already contained the correct
        properties.
        """
        cim_phy_pkg_pros = self._new_disk_cim_phy_pkg_pros()
        cim_phy_pkgs = self._c.Associators(cim_disk.path,
                                           AssocClass='CIM_Realizes',
                                           ResultClass='CIM_PhysicalPackage',
                                           PropertyList=cim_phy_pkg_pros)
        if not (cim_phy_pkgs and cim_phy_pkgs[0]):
            raise LsmError(ErrorNumber.INTERNAL_ERROR,
                           "Failed to find out the CIM_PhysicalPackage " +
                           "of CIM_DiskDrive %s" % cim_disk.path)
        cim_phy_pkg = cim_phy_pkgs[0]
        status = Disk.STATUS_UNKNOWN
        enable_status = Disk.ENABLE_STATUS_UNKNOWN
        name = ''
        vendor = ''
        model = ''
        sn = ''
        part_num = ''
        block_size = Disk.BLOCK_SIZE_NOT_FOUND
        num_of_block = Disk.BLOCK_COUNT_NOT_FOUND
        system_id = ''
        error_info = ''
        media_err_count = Disk.MEDIUM_ERROR_COUNT_NOT_SUPPORT
        predictive_fail_count = Disk.PREDICTIVE_FAILURE_COUNT_NOT_SUPPORT
        health = Disk.HEALTH_UNKNOWN
        disk_type = Disk.DISK_TYPE_UNKNOWN

        # These are mandatory
        # we do not check whether they follow the SNIA standard.
        if 'OperationalStatus' in cim_disk:
            status = \
                Disk.status_dmtf_to_lsm_type(cim_disk['OperationalStatus'])
        if 'EnabledState' in cim_disk:
            enable_status = cim_disk['EnabledState']
        if 'Name' in cim_disk:
            name = cim_disk["Name"]
        if 'DeviceID' in cim_disk:
            device_id = cim_disk["DeviceID"]
        if 'SystemName' in cim_disk:
            system_id = cim_disk['SystemName']
        if 'HealthState' in cim_disk:
            health = cim_disk['HealthState']
        if 'BlockSize' in cim_ext:
            block_size = cim_ext['BlockSize']
        if 'NumberOfBlocks' in cim_ext:
            num_of_block = cim_ext['NumberOfBlocks']
        if 'Manufacturer' in cim_phy_pkg:
            vendor = cim_phy_pkg['Manufacturer']
        if 'Model' in cim_phy_pkg:
            model = cim_phy_pkg['Model']
        if 'SerialNumber' in cim_phy_pkg:
            sn = cim_phy_pkg['SerialNumber']
        if 'PartNumber' in cim_phy_pkg:
            part_num = cim_phy_pkg['PartNumber']
        # SNIA SMI-S 1.4 or even 1.6 does not define anyway to find out disk
        # type.
        # Currently, EMC is following DMTF define to do so.
        if 'InterconnectType' in cim_disk:  # DMTF 2.31 CIM_DiskDrive
            disk_type = cim_disk['InterconnectType']
            if 'Caption' in cim_disk:
                # EMC VNX introduced NL_SAS disk.
                if cim_disk['Caption'] == 'NL_SAS':
                    disk_type = Disk.DISK_TYPE_NL_SAS

        if disk_type == Disk.DISK_TYPE_UNKNOWN and 'DiskType' in cim_disk:
            disk_type = \
                Disk.dmtf_disk_type_2_lsm_disk_type(cim_disk['DiskType'])

        # LSI way for checking disk type
        if not disk_type:
            cim_pes = \
                self._c.Associators(
                    cim_disk.path,
                    AssocClass='CIM_SAPAvailableForElement',
                    ResultClass='CIM_ProtocolEndpoint',
                    PropertyList=['CreationClassName'])
            if cim_pes and cim_pes[0]:
                if 'CreationClassName' in cim_pes[0]:
                    ccn = cim_pes[0]['CreationClassName']
                    if ccn == 'LSIESG_TargetSATAProtocolEndpoint':
                        disk_type = Disk.DISK_TYPE_SATA
                    if ccn == 'LSIESG_TargetSASProtocolEndpoint':
                        disk_type = Disk.DISK_TYPE_SAS

        if 'ErrorCleared' in cim_disk:
            if not cim_disk['ErrorCleared']:
                if 'ErrorDescription' in cim_disk:
                    error_info = cim_disk['ErrorDescription']
                else:
                    raise LsmError(ErrorNumber.INTERNAL_ERROR,
                                   "CIM_DiskDrive %s " % cim_disk.id +
                                   "has ErrorCleared == False but " +
                                   "does not have " +
                                   "CIM_DiskDrive['ErrorDescription']")
        if 'MediaErrorCount' in cim_disk:
            media_err_count = cim_disk['MediaErrorCount']
        if 'PredictiveFailureCount' in cim_disk:
            predictive_fail_count = cim_disk['PredictiveFailureCount']

        return Disk(self._disk_id(cim_disk), name, sn, part_num,
                    vendor, model, disk_type,
                    block_size, num_of_block,
                    status, enable_status, health, system_id, error_info,
                    media_err_count, predictive_fail_count)

    @handle_cim_errors
    def _pri_cim_ext_of_cim_disk(self, cim_disk_path, property_list=None):
        """
        Usage:
            Find out the Primordial CIM_StorageExtent of CIM_DiskDrive
            In SNIA SMI-S 1.4 rev.6 Block book, section 11.1.1 'Base Model'
            quote:
            A disk drive is modeled as a single MediaAccessDevice (DiskDrive)
            That shall be linked to a single StorageExtent (representing the
            storage in the drive) by a MediaPresent association. The
            StorageExtent class represents the storage of the drive and
            contains its size.
        Parameter:
            cim_disk_path   # CIM_InstanceName of CIM_DiskDrive
            property_list   # a List of properties needed on returned
                            # CIM_StorageExtent
        Returns:
            cim_pri_ext     # The CIM_Instance of Primordial CIM_StorageExtent
        Exceptions:
            LsmError
                ErrorNumber.INTERNAL_ERROR  # Failed to find out pri cim_ext
        """
        if property_list is None:
            property_list = ['Primordial']
        else:
            if 'Primordial' not in property_list:
                property_list.extend(['Primordial'])

        cim_exts = self._c.Associators(cim_disk_path,
                                      AssocClass='CIM_MediaPresent',
                                      ResultClass='CIM_StorageExtent',
                                      PropertyList=property_list)
        cim_exts = [p for p in cim_exts if p["Primordial"]]
        if cim_exts and cim_exts[0]:
            # As SNIA commanded, only _ONE_ Primordial CIM_StorageExtent for
            # each CIM_DiskDrive
            return cim_exts[0]
        else:
            raise LsmError(ErrorNumber.INTERNAL_ERROR,
                           "Failed to find out Primordial " +
                           "CIM_StorageExtent for CIM_DiskDrive %s " %
                           cim_disk_path)
