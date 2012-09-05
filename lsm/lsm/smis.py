# Copyright (C) 2011-2012 Red Hat, Inc.
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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
#
# Author: tasleson
from string import split

import time
import pywbem
from pywbem import CIMError

from iplugin import IStorageAreaNetwork
from common import uri_parse, LsmError, ErrorNumber, JobStatus, md5
from data import Pool, Initiator, Volume, AccessGroup, System, Capabilities

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

    #SMI-S job 'JobState' enumerations
    (JS_NEW, JS_STARTING, JS_RUNNING, JS_SUSPENDED, JS_SHUTTING_DOWN, JS_COMPLETED,
     JS_TERMINATED, JS_KILLED, JS_EXCEPTION) = (2,3,4,5,6,7,8,9,10)

    #SMI-S job 'OperationalStatus' enumerations
    (JOB_OK, JOB_ERROR, JOB_STOPPED, JOB_COMPLETE) = (2,6,10,17)

    #SMI-S invoke return values we are interested in
    ## Reference: Page 54 in 1.5 SMI-S block specification
    (INVOKE_OK,
     INVOKE_NOT_SUPPORTED,
     INVOKE_TIMEOUT,
     INVOKE_FAILED,
     INVOKE_INVALID_PARAMETER,
     INVOKE_IN_USE,
     INVOKE_ASYNC,
     INVOKE_SIZE_NOT_SUPPORTED) = (0, 1, 3, 4, 5, 6, 4096, 4097)

    #SMI-S replication enumerations
    (SYNC_TYPE_MIRROR, SYNC_TYPE_SNAPSHOT, SYNC_TYPE_CLONE) = (6,7,8)

    class RepSvc(object):

        class Action(object):
            CREATE_ELEMENT_REPLICA  = 2

        class RepTypes(object):
            #SMI-S replication service capabilities
            SYNC_MIRROR_LOCAL       = 2
            ASYNC_MIRROR_LOCAL      = 3
            SYNC_MIRROR_REMOTE      = 4
            ASYNC_MIRROR_REMOTE     = 5
            SYNC_SNAPSHOT_LOCAL     = 6
            ASYNC_SNAPSHOT_LOCAL    = 7
            SYNC_SNAPSHOT_REMOTE    = 8
            ASYNC_SNAPSHOT_REMOTE   = 9
            SYNC_CLONE_LOCAL        = 10
            ASYNC_CLONE_LOCAL       = 11
            SYNC_CLONE_REMOTE       = 12
            ASYNC_CLONE_REMOTE      = 13

    class CopyStates(object):
        INITIALIZED     = 2
        UNSYNCHRONIZED  = 3
        SYNCHRONIZED    = 4
        INACTIVE        = 8

    #SMI-S mode for mirror updates
    (CREATE_ELEMENT_REPLICA_MODE_SYNC, CREATE_ELEMENT_REPLICA_MODE_ASYNC ) = (2,3)

    #SMI-S volume 'OperationalStatus' enumerations
    (VOL_OP_STATUS_OK, VOL_OP_STATUS_DEGRADED, VOL_OP_STATUS_ERR, VOL_OP_STATUS_STARTING,
     VOL_OP_STATUS_DORMANT) = (2,3,6,8,15)

    #SMI-S CIM_ComputerSystem OperationalStatus for system
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

    #SMI-S ExposePaths device access enumerations
    (EXPOSE_PATHS_DA_READ_WRITE,EXPOSE_PATHS_DA_READ_ONLY) = (2,3)

    def __init__(self):
        self._c = None

    def _get_class(self, class_name, gen_id, id):
        instances = self._c.EnumerateInstances(class_name)
        for i in instances:
            if gen_id(i) == id:
                return i

        raise LsmError(ErrorNumber.INVALID_ARGUMENT,
            "Unable to find class instance " + class_name +
            " with signature " + id)

    def _get_class_instance(self, class_name, prop_name=None, prop_value=None,
                            no_throw_on_missing = False):
        """
        Gets an instance of a class that optionally matches a specific
        property name and value
        """
        instances = None

        try:
            instances = self._c.EnumerateInstances(class_name)
        except CIMError as ce:
            error_code = tuple(ce)[0]

            if error_code == 5 and no_throw_on_missing:
                return None


        if prop_name is None:
            if len(instances) != 1:
                class_names = " ".join([ x.classname for x in instances])
                raise LsmError(ErrorNumber.INTERNAL_ERROR, "Expecting one instance " \
                                                            "of " + class_name + " and got: " +
                                                            class_names)

            return instances[0]
        else:
            for i in instances:
                if prop_name in i and i[prop_name] == prop_value:
                    return i

        if no_throw_on_missing:
            return None

        raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                        "Unable to find class instance " + class_name +
                        " with property " + prop_name +
                        " with value " + prop_value)

    def _get_pool(self, id):
        """
        Get a specific instance of a pool by pool id.
        """
        return self._get_class_instance("CIM_StoragePool", "InstanceID", id)


    def _get_volume(self, id):
        """
        Get a specific instance of a volume by volume id.
        """
        return self._get_class("CIM_StorageVolume", self._vol_id, id)

    def _get_spc(self, initiator_id, volume_id):
        """
        Retrieve the SCSIProtocolController for a given initiator and volume.
        This will return a non-none value when there is a mapping between the
        initiator and the volume.
        """
        init = self._get_class_instance('CIM_StorageHardwareID', 'StorageID', initiator_id)

        #Look at page 151 (1.5 smi-s spec.) in the block services books for the
        # SNIA_MappingProtocolControllerView

        if init:
            auths = self._c.Associators(init.path, AssocClass='CIM_AuthorizedSubject')

            if auths:
                for a in auths:
                    spc = self._c.Associators(a.path, AssocClass='CIM_AuthorizedTarget')
                    if spc and len(spc) > 0:
                        logical_device = self._c.Associators(spc[0].path,
                                    AssocClass='CIM_ProtocolControllerForUnit')

                        if logical_device and len(logical_device) > 0:
                            vol = self._c.GetInstance(logical_device[0].path)
                            if 'DeviceID' in vol and md5(vol.path) == volume_id:
                                return spc[0]
        return None

    def _pi(self, msg, retrieve_vol, rc, out):
        """
        Handle the the process of invoking an operation.
        """

        #Check to see if operation is done
        if rc == Smis.INVOKE_OK:
            if retrieve_vol:
                return None, self._new_vol_from_name(out)
            else:
                return None, None

        elif rc == Smis.INVOKE_ASYNC:
            #We have an async operation
            job_id = "%s@%s" % (md5(str(out['Job']['InstanceID'])),
                                str(retrieve_vol))
            return job_id, None
        elif rc == Smis.INVOKE_NOT_SUPPORTED:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                'SMI-S error code indicates operation not supported')
        else:
            raise LsmError(ErrorNumber.PLUGIN_ERROR, 'Error: ' + msg + " rc= " + str(rc))

    def startup(self, uri, password, timeout, flags = 0):
        """
        Called when the plug-in runner gets the start request from the client.
        """
        protocol = 'http'
        u = uri_parse(uri, ['scheme','netloc', 'host','port'], ['namespace'])

        if u['scheme'].lower() == 'smispy+ssl':
            protocol = 'https'

        url = "%s://%s:%s" % (protocol, u['host'], u['port'])

        #System filtering
        self.system_list = None

        if 'systems' in u['parameters']:
            self.system_list = split(u['parameters']["systems"], ":")

        self.tmo = timeout
        self._c = pywbem.WBEMConnection(url, (u['username'], password),
                                            u['parameters']["namespace"] )

    def set_time_out(self, ms, flags = 0):
        self.tmo = ms

    def get_time_out(self, flags = 0):
        return self.tmo

    def shutdown(self, flags = 0):
        self._c = None
        self._jobs = None

    def _job_completed_ok(self, status):
        """
        Given a concrete job instance, check the operational status.  This is a
        little convoluted as different SMI-S proxies return the values in different
        positions in list :-)
        """
        rc = False
        op = status['OperationalStatus']

        if len(op) > 1 and \
            ((op[0] == Smis.JOB_OK and op[1] == Smis.JOB_COMPLETE) or
             (op[0] == Smis.JOB_COMPLETE and op[1] == Smis.JOB_OK)):
            rc = True

        return rc

    def _get_job_details(self, job_id):
        (id,get_vol) = job_id.split('@',2)

        if get_vol == 'False':
            get_vol = False
        else:
            get_vol = True

        jobs = self._c.EnumerateInstances('CIM_ConcreteJob')

        for j in jobs:
            tmp_id = md5(j['InstanceID'])
            if tmp_id == id:
                return j, get_vol

        raise LsmError(ErrorNumber.NOT_FOUND_JOB, 'Non-existent job')

    def _job_progress(self, job_id):
        """
        Given a concrete job instance name, check the status
        """
        volume = None

        (concrete_job, get_vol) = self._get_job_details(job_id)

        job_state = concrete_job['JobState']

        if job_state in(Smis.JS_NEW, Smis.JS_STARTING, Smis.JS_RUNNING):
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
        Interrogate the supported features of the Storage Configuration service
        """
        scs = self._get_class_instance( 'CIM_StorageConfigurationService',
            'SystemName', system.id)

        scs_cap_inst = self._c.Associators(scs.path,
                        AssocClass='CIM_ElementCapabilities',
                        ResultClass = 'CIM_StorageConfigurationCapabilities')[0]

        #print 'Async', scs_cap_inst['SupportedAsynchronousActions']
        #print 'Sync', scs_cap_inst['SupportedSynchronousActions']

        #TODO Get rid of magic numbers
        if 2 in scs_cap_inst['SupportedStorageElementTypes']:
            cap.set(Capabilities.VOLUMES)

        if 5 in scs_cap_inst['SupportedAsynchronousActions'] or \
            5 in scs_cap_inst['SupportedSynchronousActions']:
            cap.set(Capabilities.VOLUME_CREATE)

        if 6 in scs_cap_inst['SupportedAsynchronousActions'] or \
           6 in scs_cap_inst['SupportedSynchronousActions']:
            cap.set(Capabilities.VOLUME_DELETE)

        if 7 in scs_cap_inst['SupportedAsynchronousActions'] or \
           7 in scs_cap_inst['SupportedSynchronousActions']:
            cap.set(Capabilities.VOLUME_RESIZE)

    def _rs_supported_capabilities(self, system, cap):
        """
        Interrogate the supported features of the replication service
        """
        rs = self._get_class_instance("CIM_ReplicationService", 'SystemName',
                                        system.id, True)

        if rs:
            rs_cap = self._c.Associators(rs.path, AssocClass='CIM_ElementCapabilities',
                                        ResultClass='CIM_ReplicationServiceCapabilities')[0]

            #print 'Async', rs_cap['SupportedAsynchronousActions']
            #print 'Sync', rs_cap['SupportedSynchronousActions']
            #print 'SupportedReplicationTypes', rs_cap['SupportedReplicationTypes']

            if self.RepSvc.Action.CREATE_ELEMENT_REPLICA in \
               rs_cap['SupportedAsynchronousActions'] or \
               self.RepSvc.Action.CREATE_ELEMENT_REPLICA in \
               rs_cap['SupportedSynchronousActions']:
                cap.set(Capabilities.VOLUME_REPLICATE)

            if self.RepSvc.RepTypes.SYNC_MIRROR_LOCAL in \
               rs_cap['SupportedReplicationTypes']:
                cap.set(Capabilities.VOLUME_REPLICATE_MIRROR_SYNC)

            if self.RepSvc.RepTypes.ASYNC_MIRROR_LOCAL \
                in rs_cap['SupportedReplicationTypes']:
                cap.set(Capabilities.VOLUME_REPLICATE_MIRROR_ASYNC)

            if self.RepSvc.RepTypes.SYNC_SNAPSHOT_LOCAL or \
               self.RepSvc.RepTypes.ASYNC_SNAPSHOT_LOCAL \
                in rs_cap['SupportedReplicationTypes']:
                cap.set(Capabilities.VOLUME_REPLICATE_CLONE)

            if self.RepSvc.RepTypes.SYNC_CLONE_LOCAL or \
               self.RepSvc.RepTypes.ASYNC_CLONE_LOCAL \
                in rs_cap['SupportedReplicationTypes']:
                cap.set(Capabilities.VOLUME_REPLICATE_COPY)

    def _pcm_supported_capabilities(self, system, cap):
        """
        Interrogate the supported features of
        CIM_ProtocolControllerMaskingCapabilities
        """
        pcm = self._get_class_instance(
                                "CIM_ProtocolControllerMaskingCapabilities",
                                'StorageSystem_name', system.id, True)

        if pcm is None:
            pcm = self._get_class_instance(
                "CIM_ProtocolControllerMaskingCapabilities",
                'InstanceID', system.id)



        cap.set(Capabilities.ACCESS_GROUP_LIST)

        if pcm['ExposePathsSupported']:
            cap.set(Capabilities.ACCESS_GROUP_GRANT)
            cap.set(Capabilities.ACCESS_GROUP_REVOKE)
            cap.set(Capabilities.ACCESS_GROUP_ADD_INITIATOR)
            cap.set(Capabilities.ACCESS_GROUP_DEL_INITIATOR)

        cap.set(Capabilities.ACCESS_GROUPS_GRANTED_TO_VOLUME)
        cap.set(Capabilities.VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP)

    @handle_cim_errors
    def capabilities(self, system, flags = 0):
        cap = Capabilities()

        #Assume that the SMI-S we are talking to supports blocks
        cap.set(Capabilities.BLOCK_SUPPORT)
        cap.set(Capabilities.INITIATORS)


        self._scs_supported_capabilities(system,cap)
        self._rs_supported_capabilities(system, cap)
        self._pcm_supported_capabilities(system, cap)

        return cap

    @handle_cim_errors
    def job_status(self, job_id, flags = 0):
        """
        Given a job id returns the current status as a tuple
        (status (enum), percent_complete(integer), volume (None or Volume))
        """
        return self._job_progress(job_id)

    @staticmethod
    def _vol_id(c):
        return md5(c['SystemName'] + c['DeviceID'])

    def _new_vol(self, cv):
        """
        Takes a CIMInstance that represents a volume and returns a lsm Volume
        """
        other_id = ''

        #Reference page 134 in 1.5 spec.
        status = Volume.STATUS_UNKNOWN

        #OperationalStatus is mandatory
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

        #This is optional (User friendly name)
        if 'ElementName' in cv:
            user_name = cv["ElementName"]
        else:
            #Better fallback value?
            user_name = cv['DeviceID']

        #TODO: Make this work for all vendors
        #This is optional(EMC and NetApp place the vpd here)
        if 'OtherIdentifyingInfo' in cv:
            other_id = cv["OtherIdentifyingInfo"]
            if other_id is not None and len(other_id) > 0:
                other_id = other_id[0]
        else:
            #Engenio/LSI use this field.
            nf = cv['NameFormat']
            #Check to see if name format is NAA
            if 9 == nf:
                other_id = cv["Name"]

        return Volume(  self._vol_id(cv), user_name, other_id, cv["BlockSize"],
                        cv["NumberOfBlocks"], status, cv['SystemName'])

    def _new_vol_from_name(self, out):
        """
        Given a volume by CIMInstanceName, return a lsm Volume object
        """
        instance = self._c.GetInstance(out['TheElement'])
        return self._new_vol(instance)

    def _new_access_group(self, g):
        return AccessGroup(g['DeviceID'], g['ElementName'],
            self._get_initiators_in_group(g), g['SystemName'])

    def _new_vol_from_job(self, job):
        """
        Given a concrete job instance, return referenced volume as lsm volume
        """
        associations = self._c.Associators(job.path, ResultClass='CIM_StorageVolume')

        for a in associations:
            return self._new_vol(self._c.GetInstance(a.path))
        return None

    @handle_cim_errors
    def volumes(self, flags = 0):
        """
        Return all volumes.
        """

        #If no filtering, we will get all of the volumes in one shot, else
        #we will retrieve them system by system
        if self.system_list is None:
            volumes = self._c.EnumerateInstances('CIM_StorageVolume')
            return [ self._new_vol(v) for v in volumes ]
        else:
            rc = []
            systems = self._systems()
            for s in systems:
                volumes = self._c.Associators(s.path,
                                                AssocClass='CIM_SystemDevice',
                                                ResultClass='CIM_StorageVolume')
                rc.extend([self._new_vol(v) for v in volumes])
            return rc

    def _systems(self):
        rc = []
        ccs = self._c.EnumerateInstances('CIM_ControllerConfigurationService')

        for c in ccs:
            system = self._c.Associators(c.path,
                AssocClass='CIM_HostedService',
                ResultClass='CIM_ComputerSystem')[0]

            #Filtering
            if self.system_list:
                if system['Name'] in self.system_list:
                    rc.append(system)
            else:
                rc.append(system)

        return rc

    @handle_cim_errors
    def pools(self, flags = 0):
        """
        Return all pools
        """
        rc = []
        systems = self._systems()
        for s in systems:
            pools = self._c.Associators(s.path,
                                AssocClass='CIM_HostedStoragePool',
                                ResultClass='CIM_StoragePool')
            rc.extend(
                [ Pool(p['InstanceID'], p["ElementName"],
                p["TotalManagedSpace"],
                p["RemainingManagedSpace"],
                s['Name']) for p in pools if not p["Primordial"]])

        return rc

    def _new_system(self, s):
        #In the case of systems we are assuming that the System Name is unique.
        status = System.STATUS_UNKNOWN

        if 'OperationalStatus' in s:
            for os in s['OperationalStatus']:
                if os == Smis.SystemOperationalStatus.OK:
                    status |= System.STATUS_OK
                elif os == Smis.SystemOperationalStatus.DEGRADED:
                    status |= System.STATUS_DEGRADED
                elif os ==  Smis.SystemOperationalStatus.ERROR or \
                            Smis.SystemOperationalStatus.STRESSED or \
                            Smis.SystemOperationalStatus.NON_RECOVERABLE_ERROR:
                    status |= System.STATUS_ERROR
                elif os == Smis.SystemOperationalStatus.PREDICTIVE_FAILURE:
                    status |= System.STATUS_PREDICTIVE_FAILURE

        return System(s['Name'], s['ElementName'], status)

    @handle_cim_errors
    def systems(self, flags = 0):
        """
        Return the storage arrays accessible from this plug-in at this time
        """
        return [ self._new_system(s) for s in self._systems() ]

    @staticmethod
    def _to_init(i):
        return Initiator(i['StorageID'], i["IDType"], i["ElementName"])

    @handle_cim_errors
    def initiators(self, flags = 0):
        """
        Return all initiators.
        """
        initiators = self._c.EnumerateInstances('CIM_StorageHardwareID')
        return [ Smis._to_init(i) for i in initiators ]

    @handle_cim_errors
    def volume_create(self, pool, volume_name, size_bytes, provisioning,
                      flags = 0):
        """
        Create a volume.
        """
        if provisioning != Volume.PROVISION_DEFAULT:
            raise LsmError(ErrorNumber.UNSUPPORTED_PROVISIONING,
                            "Unsupported provisioning")

        #Get the Configuration service for the system we are interested in.


        scs = self._get_class_instance( 'CIM_StorageConfigurationService',
                                        'SystemName', pool.system_id)
        sp = self._get_pool(pool.id)

        in_params = {   'ElementName' : volume_name,
                        'ElementType' : pywbem.Uint16(2),
                        'InPool':sp.path,
                        'Size': pywbem.Uint64(size_bytes) }

        return self._pi("volume_create", True,
            *(self._c.InvokeMethod('CreateOrModifyElementFromStoragePool',
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
                    raise LsmError(ErrorNumber.PLUGIN_ERROR, msg +
                                                ", job error code= " + str(s))

    def _detach(self, vol, sync):
        rs = self._get_class_instance("CIM_ReplicationService", 'SystemName',
            vol.system_id, True)

        if rs:
            in_params = { 'Operation' : pywbem.Uint16(8),
                          'Synchronization': sync.path }

            job_id = self._pi("_detach", False,
                *(self._c.InvokeMethod('ModifyReplicaSynchronization', rs.path,
                                        **in_params)))[0]

            self._poll("ModifyReplicaSynchronization, detach", job_id)

    def _cim_name_match(self, a, b):
        if  a['DeviceID'] == b['DeviceID'] and \
            a['SystemName'] == b['SystemName'] and \
            a['SystemCreationClassName'] == b['SystemCreationClassName']:
            return True
        else:
            return False

    def _check_volume_associations(self, vol):
        """
        Check a volume to see if it has any associations with other
        volumes.
        """
        lun = self._get_volume(vol.id)

        ss = self._c.References(lun.path,
                                    ResultClass='CIM_StorageSynchronized')

        lp = lun.path

        if len(ss):
            for s in ss:
                #TODO: Need to see if detach is a supported operation in
                # replication capabilities.
                if s['SyncState'] == 6: #Synchronized
                    if 'SyncedElement' in s:
                        item = s['SyncedElement']

                        if self._cim_name_match(item, lp):
                            self._detach(vol, s)

                    if 'SystemElement' in s:
                        item = s['SystemElement']

                        if self._cim_name_match(item, lp):
                            self._detach(vol, s)

    @handle_cim_errors
    def volume_delete(self, volume, flags = 0):
        """
        Delete a volume
        """
        scs = self._get_class_instance('CIM_StorageConfigurationService',
                                        'SystemName', volume.system_id)
        lun = self._get_volume(volume.id)

        self._check_volume_associations(volume)

        in_params = { 'TheElement' : lun.path }

        #Delete returns None or Job number
        return self._pi("volume_delete", False,
                        *(self._c.InvokeMethod('ReturnToStoragePool', scs.path,
                                                **in_params)))[0]

    @handle_cim_errors
    def volume_resize(self, volume, new_size_bytes, flags = 0):
        """
        Re-size a volume
        """
        scs = self._get_class_instance('CIM_StorageConfigurationService',
                                        'SystemName', volume.system_id)
        lun = self._get_volume(volume.id)

        in_params = {   'ElementType': pywbem.Uint16(2),
                        'TheElement': lun.path, 'Size': pywbem.Uint64(new_size_bytes)}

        return self._pi("volume_resize", True,
                            *(self._c.InvokeMethod('CreateOrModifyElementFromStoragePool',
                            scs.path, **in_params)))

    def _get_supported_sync_and_mode(self, system_id, rep_type):
        """
        Converts from a library capability to a suitable array capability

        returns a tuple (sync, mode)
        """
        rc = [ None, None ]

        rs = self._get_class_instance("CIM_ReplicationService", 'SystemName',
            system_id, True)

        if rs:
            rs_cap = self._c.Associators(rs.path, AssocClass='CIM_ElementCapabilities',
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

            elif rep_type == Volume.REPLICATE_CLONE or \
                    rep_type == Volume.REPLICATE_SNAPSHOT:
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
    def volume_replicate(self, pool, rep_type, volume_src, name, flags = 0):
        """
        Replicate a volume
        """
        rs = self._get_class_instance("CIM_ReplicationService", 'SystemName',
                                        pool.system_id, True)

        if rs:

            cim_pool = self._get_pool(pool.id)
            lun = self._get_volume(volume_src.id)

            sync, mode = self._get_supported_sync_and_mode(pool.system_id, rep_type)

            in_params = {   'ElementName': name,
                            'SyncType' : pywbem.Uint16(sync),
                            'Mode': pywbem.Uint16(mode),
                            'SourceElement': lun.path,
                            'TargetPool': cim_pool.path,
                            'WaitForCopyState' :
                                pywbem.Uint16(Smis.CopyStates.SYNCHRONIZED)}

            return self._pi("volume_replicate", True,
                                            *(self._c.InvokeMethod('CreateElementReplica',
                                            rs.path, **in_params)))
        raise LsmError(ErrorNumber.NO_SUPPORT, "volume-replicate not supported")

    def volume_replicate_range_block_size(self, flags = 0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volume_replicate_range(self, rep_type, volume_src, volume_dest, ranges,
                               flags = 0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_cim_errors
    def volume_online(self, volume, flags = 0):
        return None

    @handle_cim_errors
    def volume_offline(self, volume, flags = 0):
        return None

    @handle_cim_errors
    def _initiator_create(self, name, id, id_type):
        """
        Create initiator object
        """
        hardware = self._get_class_instance('CIM_StorageHardwareIDManagementService')

        in_params = {'ElementName':name,
                     'StorageID': id,
                     'IDType': pywbem.Uint16(id_type)}

        (rc, out) = self._c.InvokeMethod('CreateStorageHardwareID',
                                            hardware.path, **in_params)
        if not rc:
            init = self._get_class_instance('CIM_StorageHardwareID', 'StorageID', id)
            return Smis._to_init(init)

        raise LsmError(ErrorNumber.PLUGIN_ERROR, 'Error: ' + str(rc) +
                                                 ' on initiator_create!')

    @handle_cim_errors
    def access_group_grant(self, group, volume, access, flags = 0):
        """
        Grant access to a volume to an group
        """
        ccs = self._get_class_instance('CIM_ControllerConfigurationService',
                                        'SystemName', group.system_id)
        lun = self._get_volume(volume.id)
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

        in_params = { 'LUNames': [lun['Name']],
                      'ProtocolControllers': [spc.path],
                      'DeviceAccesses': [pywbem.Uint16(da)]}

        #Returns None or job id
        return self._pi("access_grant", False,
            *(self._c.InvokeMethod('ExposePaths', ccs.path, **in_params)))[0]


    def _wait(self, job):

        status = self.job_status(job)[0]

        while JobStatus.COMPLETE != status:
            time.wait(0.5)
            status = self.job_status(job)[0]

        if JobStatus.COMPLETE != status:
            raise LsmError(ErrorNumber.PLUGIN_ERROR, "Expected no errors %s %s"
                            %(job, str(status)))


    @handle_cim_errors
    def access_group_revoke(self, group, volume, flags = 0):
        ccs = self._get_class_instance('CIM_ControllerConfigurationService',
                                            'SystemName', volume.system_id)
        lun = self._get_volume(volume.id)
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

        #This seems horribly wrong for something that is a standard.
        if 'Name' in s and s['Name'] == 'Storage Group':
            #EMC
            rc = True
        elif 'DeviceID' in s and s['DeviceID'][0:3] == 'SPC':
            #NetApp
            rc = True
        return rc

    def _get_access_groups(self):
        rc = []

        #System filtering
        if self.system_list:
            systems = self._systems()
            for s in systems:
                spc = self._c.Associators(s.path,
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

    def _get_access_group(self, id):
        groups = self._get_access_groups()
        for g in groups:
            if g['DeviceID'] == id:
                return g

        return None

    def _get_initiators_in_group(self, group):
        rc = []

        ap = self._c.Associators(group.path, AssocClass='CIM_AuthorizedTarget')

        if len(ap):
            for a in ap:
                inits = self._c.Associators(a.path, AssocClass='CIM_AuthorizedSubject')
                for i in inits:
                    rc.append(i['StorageID'])

        return rc

    @handle_cim_errors
    def volumes_accessible_by_access_group(self, group, flags = 0):
        g = self._get_class_instance('CIM_SCSIProtocolController', 'DeviceID',
            group.id)
        if g:
            logical_units = self._c.Associators(g.path,
                AssocClass='CIM_ProtocolControllerForUnit')
            return [self._new_vol(v) for v in logical_units]
        else:
            raise LsmError(ErrorNumber.PLUGIN_ERROR,
                'Error: access group %s does not exist!' % group.id)

    @handle_cim_errors
    def access_groups_granted_to_volume(self, volume, flags = 0):
        vol = self._get_class_instance('CIM_StorageVolume', 'DeviceID',
            volume.id)

        if vol:
            access_groups = self._c.Associators(vol.path,
                AssocClass='CIM_ProtocolControllerForUnit')
            return [self._new_access_group(g) for g in access_groups]
        else:
            raise LsmError(ErrorNumber.PLUGIN_ERROR,
                'Error: access group %s does not exist!' % volume.id)

    @handle_cim_errors
    def access_group_list(self, flags = 0):
        groups = self._get_access_groups()
        return [ self._new_access_group(g) for g in groups]

    @handle_cim_errors
    def access_group_create(self, name, initiator_id, id_type, system_id,
                            flags = 0):
        #page 880 1.5 spec. CreateMaskingView
        #
        # No access to a provider that implements this at this time,
        # so unable to develop and test.
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_cim_errors
    def access_group_del(self, group, flags = 0):
        #page 880 1.5 spec. DeleteMaskingView
        #
        # No access to a provider that implements this at this time,
        # so unable to develop and test.
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_cim_errors
    def access_group_add_initiator(self, group, initiator_id, id_type,
                                   flags = 0):
        #Check to see if we have this initiator already, if we don't create it
        #and then add to the view.
        spc = self._get_access_group(group.id)

        inits = self.initiators()
        initiator = None
        for i in inits:
            if i.id == initiator_id:
                initiator = i
                break

        if not initiator:
            initiator = self._initiator_create(initiator_id, id_type, initiator_id)

        ccs = self._get_class_instance('CIM_ControllerConfigurationService',
                                        'SystemName', group.system_id)

        in_params = { 'InitiatorPortIDs':[initiator.id],
                      'ProtocolControllers': [spc.path]
                    }

        #Returns None or job id
        return self._pi("access_group_add_initiator", False,
            *(self._c.InvokeMethod('ExposePaths', ccs.path, **in_params)))[0]

    @handle_cim_errors
    def access_group_del_initiator(self, group, initiator, flags = 0):
        spc = self._get_access_group(group.id)
        ccs = self._get_class_instance('CIM_ControllerConfigurationService',
                                            'SystemName', group.system_id)

        hide_params = {'InitiatorPortIDs': [initiator.id],
                       'ProtocolControllers': [spc.path]}
        return self._pi("HidePaths", False,
            *(self._c.InvokeMethod('HidePaths', ccs.path,
                **hide_params)))[0]

    @handle_cim_errors
    def iscsi_chap_auth_inbound( self, initiator, user, password, flags = 0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_cim_errors
    def initiator_grant(self, initiator_id, initiator_type, volume, access,
                        flags = 0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_cim_errors
    def initiator_revoke(self, initiator, volume, flags = 0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_cim_errors
    def volumes_accessible_by_initiator(self, initiator, flags = 0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_cim_errors
    def initiators_granted_to_volume(self, volume, flags = 0 ):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_cim_errors
    def job_free(self, job_id, flags = 0):
        """
        Frees the resources given a job number.
        """
        concrete_job = self._get_job_details(job_id)[0]

        #See if we should delete the job
        if not concrete_job['DeleteOnCompletion']:
            try:
                self._c.DeleteInstance(concrete_job.path)
            except CIMError:
                pass

    def volume_child_dependency(self, volume, flags = 0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volume_child_dependency_rm(self, volume, flags = 0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")
