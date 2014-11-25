# Copyright (C) 2011-2014 Red Hat, Inc.
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
#         Gris Ge <fge@redhat.com>

from string import split
import time
import copy
import os
import re

import pywbem
from pywbem import CIMError
import smis_cap
import smis_sys
import smis_pool
import smis_disk
from lsm.plugin.smispy import smis_vol
from lsm.plugin.smispy import smis_ag
import dmtf

from lsm import (IStorageAreaNetwork, uri_parse, LsmError, ErrorNumber,
                 JobStatus, md5, Volume, AccessGroup, Pool,
                 VERSION, TargetPort,
                 search_property)

from utils import (merge_list, handle_cim_errors, hex_string_format)

from smis_common import SmisCommon

## Variable Naming scheme:
#   cim_xxx         CIMInstance
#   cim_xxx_path    CIMInstanceName
#   cim_sys         CIM_ComputerSystem  (root or leaf)
#   cim_pool        CIM_StoragePool
#   cim_scs         CIM_StorageConfigurationService
#   cim_vol         CIM_StorageVolume
#   cim_rp          CIM_RegisteredProfile
#   cim_init        CIM_StorageHardwareID
#   cim_spc         CIM_SCSIProtocolController
#   cim_init_mg     CIM_InitiatorMaskingGroup
#   cim_fc_tgt      CIM_FCPort
#   cim_iscsi_pg    CIM_iSCSIProtocolEndpoint   # iSCSI portal group
#   cim_iscsi_node  CIM_SCSIProtocolController
#   cim_tcp         CIM_TCPProtocolEndpoint,
#   cim_ip          CIM_IPProtocolEndpoint
#   cim_eth         CIM_EthernetPort
#   cim_pe          CIM_SCSIProtocolEndpoint
#   cim_gmms        CIM_GroupMaskingMappingService
#   cim_ccs         CIM_ControllerConfigurationService
#   cim_rs          CIM_ReplicationService
#   cim_hwms        CIM_StorageHardwareIDManagementService
#
#   sys             Object of LSM System
#   pool            Object of LSM Pool
#   vol             Object of LSM Volume

## Method Naming scheme:
#   _cim_xxx()
#       Return CIMInstance without any Associations() call.
#   _cim_xxx_of(cim_yyy)
#       Return CIMInstance associated to cim_yyy
#   _adj_cim_xxx()
#       Return CIMInstance with 'adj' only
#   _cim_xxx_of_id(some_id)
#       Return CIMInstance for given ID

# Terminology
#   SPC             CIM_SCSIProtocolController
#   BSP             SNIA SMI-S 'Block Services Package' profile
#   Group M&M       SNIA SMI-S 'Group Masking and Mapping' profile


def _lsm_tgt_port_type_of_cim_fc_tgt(cim_fc_tgt):
    """
    We are assuming we got CIM_FCPort. Caller should make sure of that.
    Return TargetPool.PORT_TYPE_FC as fallback
    """
    # In SNIA SMI-S 1.6.1 public draft 2, 'PortDiscriminator' is mandatory
    # for FCoE target port.
    if 'PortDiscriminator' in cim_fc_tgt and \
       cim_fc_tgt['PortDiscriminator'] and \
       dmtf.FC_PORT_PORT_DISCRIMINATOR_FCOE in cim_fc_tgt['PortDiscriminator']:
        return TargetPort.TYPE_FCOE
    if 'LinkTechnology' in cim_fc_tgt and \
       cim_fc_tgt['LinkTechnology'] == dmtf.NET_PORT_LINK_TECH_ETHERNET:
        return TargetPort.TYPE_FCOE
    return TargetPort.TYPE_FC


class Smis(IStorageAreaNetwork):
    """
    SMI-S plug-ing which exposes a small subset of the overall provided
    functionality of SMI-S
    """
    _JOB_ERROR_HANDLER = {
        SmisCommon.JOB_RETRIEVE_VOLUME_CREATE:
        smis_vol.volume_create_error_handler,
    }

    def __init__(self):
        self._c = None
        self.tmo = 0

    @handle_cim_errors
    def plugin_register(self, uri, password, timeout, flags=0):
        """
        Called when the plug-in runner gets the start request from the client.
        Checkout interop support status via:
            1. Enumerate CIM_RegisteredProfile in 'interop' namespace.
            2. if nothing found, then
               Enumerate CIM_RegisteredProfile in 'root/interop' namespace.
            3. if nothing found, then
               Enumerate CIM_RegisteredProfile in user defined namespace.
        """
        protocol = 'http'
        port = SmisCommon.IAAN_WBEM_HTTP_PORT
        u = uri_parse(uri, ['scheme', 'netloc', 'host'], None)

        if u['scheme'].lower() == 'smispy+ssl':
            protocol = 'https'
            port = SmisCommon.IAAN_WBEM_HTTPS_PORT

        if 'port' in u:
            port = u['port']

        url = "%s://%s:%s" % (protocol, u['host'], port)

        # System filtering
        system_list = None

        if 'systems' in u['parameters']:
            system_list = split(u['parameters']["systems"], ":")

        namespace = None
        if 'namespace' in u['parameters']:
            namespace = u['parameters']['namespace']

        no_ssl_verify = False
        if "no_ssl_verify" in u["parameters"] \
           and u["parameters"]["no_ssl_verify"] == 'yes':
            no_ssl_verify = True

        debug_path = None
        if 'debug_path' in u['parameters']:
            debug_path = u['parameters']['debug_path']

        self._c = SmisCommon(
            url, u['username'], password, namespace, no_ssl_verify,
            debug_path, system_list)

        self.tmo = timeout

    @handle_cim_errors
    def time_out_set(self, ms, flags=0):
        self.tmo = ms

    @handle_cim_errors
    def time_out_get(self, flags=0):
        return self.tmo

    @handle_cim_errors
    def plugin_unregister(self, flags=0):
        self._c = None

    @handle_cim_errors
    def capabilities(self, system, flags=0):
        cim_sys = smis_sys.cim_sys_of_sys_id(self._c, system.id)
        return smis_cap.get(self._c, cim_sys, system)

    @handle_cim_errors
    def plugin_info(self, flags=0):
        return "Generic SMI-S support", VERSION

    @handle_cim_errors
    def job_status(self, job_id, flags=0):
        """
        Given a job id returns the current status as a tuple
        (status (enum), percent_complete(integer), volume (None or Volume))
        """
        completed_item = None

        error_handler = None

        (ignore, retrieve_data, method_data) = SmisCommon.parse_job_id(job_id)

        if retrieve_data in Smis._JOB_ERROR_HANDLER.keys():
            error_handler = Smis._JOB_ERROR_HANDLER[retrieve_data]

        cim_job_pros = SmisCommon.cim_job_pros()
        cim_job_pros.extend(
            ['JobState', 'PercentComplete', 'ErrorDescription',
             'OperationalStatus'])
        cim_job = self._c.cim_job_of_job_id(job_id, cim_job_pros)

        job_state = cim_job['JobState']

        try:
            if job_state in (dmtf.JOB_STATE_NEW, dmtf.JOB_STATE_STARTING,
                             dmtf.JOB_STATE_RUNNING):
                status = JobStatus.INPROGRESS

                pc = cim_job['PercentComplete']
                if pc > 100:
                    percent_complete = 100
                else:
                    percent_complete = pc

            elif job_state == dmtf.JOB_STATE_COMPLETED:
                status = JobStatus.COMPLETE
                percent_complete = 100

                if SmisCommon.cim_job_completed_ok(cim_job):
                    if retrieve_data == SmisCommon.JOB_RETRIEVE_VOLUME or \
                       retrieve_data == SmisCommon.JOB_RETRIEVE_VOLUME_CREATE:
                        completed_item = self._new_vol_from_job(cim_job)
                else:
                    raise LsmError(
                        ErrorNumber.PLUGIN_BUG,
                        str(cim_job['ErrorDescription']))
            else:
                raise LsmError(
                    ErrorNumber.PLUGIN_BUG, str(cim_job['ErrorDescription']))

        except Exception:
            if error_handler is not None:
                error_handler(self._c, method_data)
            else:
                raise
        return status, percent_complete, completed_item

    def _new_vol_from_name(self, out):
        """
        Given a volume by CIMInstanceName, return a lsm Volume object
        """
        cim_vol = None
        cim_vol_pros = smis_vol.cim_vol_pros()

        if 'TheElement' in out:
            cim_vol = self._c.GetInstance(
                out['TheElement'],
                PropertyList=cim_vol_pros)
        elif 'TargetElement' in out:
            cim_vol = self._c.GetInstance(
                out['TargetElement'],
                PropertyList=cim_vol_pros)

        pool_id = smis_pool.pool_id_of_cim_vol(self._c, cim_vol.path)
        sys_id = smis_sys.sys_id_of_cim_vol(cim_vol)

        return smis_vol.cim_vol_to_lsm_vol(cim_vol, pool_id, sys_id)

    def _new_vol_from_job(self, job):
        """
        Given a concrete job instance, return referenced volume as lsm volume
        """
        cim_vol_pros = smis_vol.cim_vol_pros()
        cim_vols = []
        # Workaround for HP 3PAR:
        #   When doing volume-replicate for 'COPY" type, Associators() will
        #   return [None] if PropertyList defined. It works well
        #   for CLONE type.
        if job.path.classname == 'TPD_ConcreteJob':
            cim_vols = self._c.Associators(
                job.path,
                AssocClass='CIM_AffectedJobElement',
                ResultClass='CIM_StorageVolume')
        else:
            cim_vols = self._c.Associators(
                job.path,
                AssocClass='CIM_AffectedJobElement',
                ResultClass='CIM_StorageVolume',
                PropertyList=cim_vol_pros)
        for cim_vol in cim_vols:
            pool_id = smis_pool.pool_id_of_cim_vol(self._c, cim_vol.path)
            sys_id = smis_sys.sys_id_of_cim_vol(cim_vol)
            return smis_vol.cim_vol_to_lsm_vol(cim_vol, pool_id, sys_id)
        return None

    @handle_cim_errors
    def volumes(self, search_key=None, search_value=None, flags=0):
        """
        Return all volumes.
        We are basing on "Block Services Package" profile version 1.4 or
        later:
            CIM_ComputerSystem
                 |
                 |  (CIM_HostedStoragePool)
                 |
                 v
            CIM_StoragePool
                 |
                 | (CIM_AllocatedFromStoragePool)
                 |
                 v
            CIM_StorageVolume
        As 'Block Services Package' is mandatory for 'Array' profile, we
        don't check support status here as startup() already checked 'Array'
        profile.
        """
        rc = []
        cim_sys_pros = smis_sys.cim_sys_id_pros()
        cim_syss = smis_sys.root_cim_sys(self._c, cim_sys_pros)
        cim_vol_pros = smis_vol.cim_vol_pros()
        for cim_sys in cim_syss:
            sys_id = smis_sys.sys_id_of_cim_sys(cim_sys)
            pool_pros = smis_pool.cim_pool_id_pros()
            cim_pools = smis_pool.cim_pools_of_cim_sys_path(
                self._c, cim_sys.path, pool_pros)
            for cim_pool in cim_pools:
                pool_id = smis_pool.pool_id_of_cim_pool(cim_pool)
                cim_vols = smis_vol.cim_vol_of_cim_pool_path(
                    self._c, cim_pool.path, cim_vol_pros)
                for cim_vol in cim_vols:
                    rc.append(
                        smis_vol.cim_vol_to_lsm_vol(cim_vol, pool_id, sys_id))
        return search_property(rc, search_key, search_value)

    @handle_cim_errors
    def pools(self, search_key=None, search_value=None, flags=0):
        """
        Convert CIM_StoragePool to lsm.Pool.
        To list all CIM_StoragePool:
            1. List all root CIM_ComputerSystem.
            2. List all CIM_StoragePool associated to CIM_ComputerSystem.
        """
        rc = []
        cim_pool_pros = smis_pool.cim_pool_pros()

        cim_sys_pros = smis_sys.cim_sys_id_pros()
        cim_syss = smis_sys.root_cim_sys(self._c, cim_sys_pros)

        for cim_sys in cim_syss:
            system_id = smis_sys.sys_id_of_cim_sys(cim_sys)
            cim_pools = smis_pool.cim_pools_of_cim_sys_path(
                self._c, cim_sys.path, cim_pool_pros)
            for cim_pool in cim_pools:
                rc.append(
                    smis_pool.cim_pool_to_lsm_pool(
                        self._c, cim_pool, system_id))

        return search_property(rc, search_key, search_value)

    @handle_cim_errors
    def systems(self, flags=0):
        """
        Return the storage arrays accessible from this plug-in at this time

        As 'Block Services Package' is mandatory for 'Array' profile, we
        don't check support status here as startup() already checked 'Array'
        profile.
        """
        cim_sys_pros = smis_sys.cim_sys_pros()
        cim_syss = smis_sys.root_cim_sys(self._c, cim_sys_pros)

        return [smis_sys.cim_sys_to_lsm_sys(s) for s in cim_syss]

    @handle_cim_errors
    def volume_create(self, pool, volume_name, size_bytes, provisioning,
                      flags=0):
        """
        Create a volume.
        """
        # Use user provide lsm.Pool.element_type to speed thing up.
        if not Pool.ELEMENT_TYPE_VOLUME & pool.element_type:
            raise LsmError(
                ErrorNumber.NO_SUPPORT,
                "Pool not suitable for creating volumes")

        # Use THICK volume by default unless unsupported or user requested.
        dmtf_element_type = dmtf.ELEMENT_THICK_VOLUME

        if provisioning == Volume.PROVISION_DEFAULT:
            # Prefer thick/full volume unless only thin volume supported.
            # HDS AMS only support thin volume in their thin pool.
            if not Pool.ELEMENT_TYPE_VOLUME_FULL & pool.element_type and \
               Pool.ELEMENT_TYPE_VOLUME_THIN & pool.element_type:
                dmtf_element_type = dmtf.ELEMENT_THIN_VOLUME
        else:
            # User is requesting certain type of volume
            if provisioning == Volume.PROVISION_FULL and \
               Pool.ELEMENT_TYPE_VOLUME_FULL & pool.element_type:
                dmtf_element_type = dmtf.ELEMENT_THICK_VOLUME
            elif (provisioning == Volume.PROVISION_THIN and
                  Pool.ELEMENT_TYPE_VOLUME_THIN & pool.element_type):
                dmtf_element_type = dmtf.ELEMENT_THIN_VOLUME
            else:
                raise LsmError(
                    ErrorNumber.NO_SUPPORT,
                    "Pool not suitable for creating volume with "
                    "requested provisioning type")

        # Get the Configuration service for the system we are interested in.
        cim_scs = self._c.cim_scs_of_sys_id(pool.system_id)

        cim_pool_path = smis_pool.lsm_pool_to_cim_pool_path(
            self._c, pool)

        in_params = {'ElementName': volume_name,
                     'ElementType': dmtf_element_type,
                     'InPool': cim_pool_path,
                     'Size': pywbem.Uint64(size_bytes)}

        error_handler = Smis._JOB_ERROR_HANDLER[
            SmisCommon.JOB_RETRIEVE_VOLUME_CREATE]

        return self._c.invoke_method(
            'CreateOrModifyElementFromStoragePool', cim_scs.path,
            in_params,
            out_handler=self._new_vol_from_name,
            error_handler=error_handler,
            retrieve_data=SmisCommon.JOB_RETRIEVE_VOLUME_CREATE,
            method_data=volume_name)

    def _detach_netapp_e(self, vol, sync):
        #Get the Configuration service for the system we are interested in.
        cim_scs = self._c.cim_scs_of_sys_id(vol.system_id)

        in_params = {'Operation': pywbem.Uint16(2),
                     'Synchronization': sync.path}

        self._c.invoke_method_wait(
            'ModifySynchronization', cim_scs.path, in_params)

    def _detach(self, vol, sync):
        if self._c.is_netappe():
            return self._detach_netapp_e(vol, sync)

        cim_rs = self._c.cim_rs_of_sys_id(vol.system_id, raise_error=False)

        if cim_rs:
            in_params = {'Operation': pywbem.Uint16(8),
                         'Synchronization': sync.path}

            self._c.invoke_method_wait(
                'ModifyReplicaSynchronization', cim_rs.path, in_params)

    @staticmethod
    def _cim_name_match(a, b):
        if a['DeviceID'] == b['DeviceID'] \
                and a['SystemName'] == b['SystemName'] \
                and a['SystemCreationClassName'] == \
                b['SystemCreationClassName']:
            return True
        else:
            return False

    def _deal_volume_associations_netappe(self, vol, cim_vol_path):
        """
        Check a volume to see if it has any associations with other
        volumes.
        """
        rc = False

        ss = self._c.References(cim_vol_path,
                                ResultClass='CIM_StorageSynchronized')

        if len(ss):
            for s in ss:
                if 'SyncedElement' in s:
                    item = s['SyncedElement']

                    if Smis._cim_name_match(item, cim_vol_path):
                        self._detach(vol, s)
                        rc = True

                if 'SystemElement' in s:
                    item = s['SystemElement']

                    if Smis._cim_name_match(item, cim_vol_path):
                        self._detach(vol, s)
                        rc = True

        return rc

    def _deal_volume_associations(self, vol, cim_vol_path):
        """
        Check a volume to see if it has any associations with other
        volumes and deal with them.
        """
        if self._c.is_netappe():
            return self._deal_volume_associations_netappe(vol, cim_vol_path)

        try:
            ss = self._c.References(cim_vol_path,
                                    ResultClass='CIM_StorageSynchronized')
        except pywbem.CIMError as e:
            if e[0] == pywbem.CIM_ERR_INVALID_CLASS:
                return
            else:
                raise

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

                if 'SyncState' in s and 'CopyType' in s:
                    if s['SyncState'] == dmtf.ST_SYNC_STATE_SYNCHRONIZED and \
                       s['CopyType'] != \
                       dmtf.ST_CONF_CAP_COPY_TYPE_UNSYNC_ASSOC:
                        if 'SyncedElement' in s:
                            item = s['SyncedElement']

                            if Smis._cim_name_match(item, cim_vol_path):
                                self._detach(vol, s)

                        if 'SystemElement' in s:
                            item = s['SystemElement']

                            if Smis._cim_name_match(item, cim_vol_path):
                                self._detach(vol, s)

    def _volume_delete_netapp_e(self, volume, flags=0):
        cim_scs = self._c.cim_scs_of_sys_id(volume.system_id)
        cim_vol_path = smis_vol.lsm_vol_to_cim_vol_path(self._c, volume)

        #If we actually have an association to delete, the volume will be
        #deleted with the association, no need to call ReturnToStoragePool
        if not self._deal_volume_associations(volume, cim_vol_path):
            in_params = {'TheElement': cim_vol_path}

            #Delete returns None or Job number
            return self._c.invoke_method(
                'ReturnToStoragePool', cim_scs.path, in_params)[0]

        #Loop to check to see if volume is actually gone yet!
        try:
            cim_vol = self._c.GetInstance(cim_vol_path, PropertyList=[])
            while cim_vol is not None:
                cim_vol = self._c.GetInstance(cim_vol_path, PropertyList=[])
                time.sleep(0.125)
        except (LsmError, CIMError) as e:
            pass

    @handle_cim_errors
    def volume_delete(self, volume, flags=0):
        """
        Delete a volume
        """
        cim_scs = self._c.cim_scs_of_sys_id(volume.system_id)

        cim_vol_path = smis_vol.lsm_vol_to_cim_vol_path(self._c, volume)

        self._deal_volume_associations(volume, cim_vol_path)

        in_params = {'TheElement': cim_vol_path}

        # Delete returns None or Job number
        return self._c.invoke_method(
            'ReturnToStoragePool', cim_scs.path, in_params)[0]

    @handle_cim_errors
    def volume_resize(self, volume, new_size_bytes, flags=0):
        """
        Re-size a volume
        """
        cim_scs = self._c.cim_scs_of_sys_id(volume.system_id)

        cim_vol_path = smis_vol.lsm_vol_to_cim_vol_path(self._c, volume)

        in_params = {'ElementType': pywbem.Uint16(2),
                     'TheElement': cim_vol_path,
                     'Size': pywbem.Uint64(new_size_bytes)}

        return self._c.invoke_method(
            'CreateOrModifyElementFromStoragePool', cim_scs.path, in_params,
            out_handler=self._new_vol_from_name,
            retrieve_data=SmisCommon.JOB_RETRIEVE_VOLUME)

    def _get_supported_sync_and_mode(self, system_id, rep_type):
        """
        Converts from a library capability to a suitable array capability

        returns a tuple (sync, mode)
        """
        rc = [None, None]

        cim_rs = self._c.cim_rs_of_sys_id(system_id, raise_error=False)

        if cim_rs:
            rs_cap = self._c.Associators(
                cim_rs.path,
                AssocClass='CIM_ElementCapabilities',
                ResultClass='CIM_ReplicationServiceCapabilities')[0]

            s_rt = rs_cap['SupportedReplicationTypes']

            if rep_type == Volume.REPLICATE_COPY:
                if dmtf.REPLICA_CAP_TYPE_SYNC_CLONE_LOCAL in s_rt:
                    rc[0] = dmtf.SYNC_TYPE_CLONE
                    rc[1] = dmtf.REPLICA_MODE_SYNC
                elif dmtf.REPLICA_CAP_TYPE_ASYNC_CLONE_LOCAL in s_rt:
                    rc[0] = dmtf.SYNC_TYPE_CLONE
                    rc[1] = dmtf.REPLICA_MODE_ASYNC

            elif rep_type == Volume.REPLICATE_MIRROR_ASYNC:
                if dmtf.REPLICA_CAP_TYPE_ASYNC_MIRROR_LOCAL in s_rt:
                    rc[0] = dmtf.SYNC_TYPE_MIRROR
                    rc[1] = dmtf.REPLICA_MODE_ASYNC

            elif rep_type == Volume.REPLICATE_MIRROR_SYNC:
                if dmtf.REPLICA_CAP_TYPE_SYNC_MIRROR_LOCAL in s_rt:
                    rc[0] = dmtf.SYNC_TYPE_MIRROR
                    rc[1] = dmtf.REPLICA_MODE_SYNC

            elif rep_type == Volume.REPLICATE_CLONE:
                if dmtf.REPLICA_CAP_TYPE_SYNC_CLONE_LOCAL in s_rt:
                    rc[0] = dmtf.SYNC_TYPE_SNAPSHOT
                    rc[1] = dmtf.REPLICA_MODE_SYNC
                elif dmtf.REPLICA_CAP_TYPE_ASYNC_CLONE_LOCAL in s_rt:
                    rc[0] = dmtf.SYNC_TYPE_SNAPSHOT
                    rc[1] = dmtf.REPLICA_MODE_ASYNC

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

        cim_rs = self._c.cim_rs_of_sys_id(
            volume_src.system_id, raise_error=False)

        # Some (EMC VMAX, Dot hill) SMI-S Provider allow duplicated
        # ElementName, we have to do pre-check here.
        if smis_vol.volume_name_exists(self._c, name):
            raise LsmError(ErrorNumber.NAME_CONFLICT,
                           "Volume with name '%s' already exists!" % name)

        cim_pool_path = None
        if pool is not None:
            cim_pool_path = smis_pool.lsm_pool_to_cim_pool_path(self._c, pool)

        src_cim_vol_path = smis_vol.lsm_vol_to_cim_vol_path(
            self._c, volume_src)

        if cim_rs:
            method = 'CreateElementReplica'

            sync, mode = self._get_supported_sync_and_mode(
                volume_src.system_id, rep_type)

            in_params = {'ElementName': name,
                         'SyncType': sync,
                         #'Mode': mode,
                         'SourceElement': src_cim_vol_path,
                         'WaitForCopyState': dmtf.COPY_STATE_SYNC}

        else:
            # Check for older support via storage configuration service

            method = 'CreateReplica'

            # Check for storage configuration service
            cim_rs = self._c.cim_scs_of_sys_id(
                volume_src.system_id, raise_error=False)

            ct = Volume.REPLICATE_CLONE
            if rep_type == Volume.REPLICATE_CLONE:
                ct = dmtf.ST_CONF_CAP_COPY_TYPE_UNSYNC_ASSOC
            elif rep_type == Volume.REPLICATE_COPY:
                ct = dmtf.ST_CONF_CAP_COPY_TYPE_UNSYNC_UNASSOC
            elif rep_type == Volume.REPLICATE_MIRROR_ASYNC:
                ct = dmtf.ST_CONF_CAP_COPY_TYPE_ASYNC
            elif rep_type == Volume.REPLICATE_MIRROR_SYNC:
                ct = dmtf.ST_CONF_CAP_COPY_TYPE_SYNC

            in_params = {'ElementName': name,
                         'CopyType': ct,
                         'SourceElement': src_cim_vol_path}
        if cim_rs:

            if cim_pool_path is not None:
                in_params['TargetPool'] = cim_pool_path

            return self._c.invoke_method(
                method, cim_rs.path, in_params,
                out_handler=self._new_vol_from_name,
                retrieve_data=SmisCommon.JOB_RETRIEVE_VOLUME)

        raise LsmError(ErrorNumber.NO_SUPPORT,
                       "volume-replicate not supported")

    def _cim_dev_mg_path_create(self, cim_gmms_path, name, cim_vol_path,
                                vol_id):
        rc = SmisCommon.SNIA_INVOKE_FAILED
        out = None

        in_params = {
            'GroupName': name,
            'Members': [cim_vol_path],
            'Type': dmtf.MASK_GROUP_TYPE_DEV}

        cim_dev_mg_path = None
        try:
            cim_dev_mg_path = self._c.invoke_method_wait(
                'CreateGroup', cim_gmms_path, in_params,
                out_key='MaskingGroup',
                expect_class='CIM_TargetMaskingGroup')
        except (LsmError, CIMError):
            cim_dev_mg_path = self._check_exist_cim_dev_mg(
                name, cim_gmms_path, cim_vol_path, vol_id)
            if cim_dev_mg_path is None:
                raise

        return cim_dev_mg_path

    def _cim_tgt_mg_path_create(self, cim_sys_path, cim_gmms_path, name,
                                init_type):
        """
        Create CIM_TargetMaskingGroup
        Currently, LSM does not support target ports masking
        we will mask to all target ports.
        Return CIMInstanceName of CIM_TargetMaskingGroup
        """
        rc = SmisCommon.SNIA_INVOKE_FAILED
        out = None

        in_params = {
            'GroupName': name,
            'Type': dmtf.MASK_GROUP_TYPE_TGT}

        if init_type == AccessGroup.INIT_TYPE_WWPN:
            cim_fc_tgts = self._cim_fc_tgt_of(cim_sys_path)
            all_cim_fc_peps_path = []
            all_cim_fc_peps_path.extend(
                [self._cim_pep_path_of_fc_tgt(x.path) for x in cim_fc_tgts])
            in_params['Members'] = all_cim_fc_peps_path

        elif init_type == AccessGroup.INIT_TYPE_ISCSI_IQN:
            cim_iscsi_pgs = self._cim_iscsi_pg_of(cim_sys_path)
            in_params['Members'] = [x.path for x in cim_iscsi_pgs]
        else:
            # Already checked at the beginning of this method
            pass

        cim_tgt_mg_path = None
        try:
            cim_tgt_mg_path = self._c.invoke_method_wait(
                'CreateGroup', cim_gmms_path, in_params,
                out_key='MaskingGroup', expect_class='CIM_TargetMaskingGroup')
        except (LsmError, CIMError):
            cim_tgt_mg_path = self._check_exist_cim_tgt_mg(name)
            if cim_tgt_mg_path is None:
                raise

        return cim_tgt_mg_path

    def _cim_spc_path_create(self, cim_gmms_path, cim_init_mg_path,
                             cim_tgt_mg_path, cim_dev_mg_path, name):
        in_params = {
            'ElementName': name,
            'InitiatorMaskingGroup': cim_init_mg_path,
            'TargetMaskingGroup': cim_tgt_mg_path,
            'DeviceMaskingGroup': cim_dev_mg_path,
        }

        return self._c.invoke_method_wait(
            'CreateMaskingView', cim_gmms_path, in_params,
            out_key='ProtocolController',
            expect_class='CIM_SCSIProtocolController')

    def _volume_mask_group(self, access_group, volume, flags=0):
        """
        Grant access to a volume to an group
        Use GroupMaskingMappingService.AddMembers() for Group Masking
        Use ControllerConfigurationService.ExposePaths() for Masking.
        Currently, LSM does not have a way to control which target port to
        mask.
        If CIM_TargetMaskingGroup already defined for current
        CIM_InitiatorMaskingGroup, we use that.
        If No CIM_TargetMaskingGroup exist, we create one with all possible
        target ports(all FC and FCoE port for access_group.init_type == WWPN,
        and the same to iSCSI)
        """
        cim_init_mg_path = smis_ag.lsm_ag_to_cim_init_mg_path(
            self._c, access_group)

        cim_inits = smis_ag.cim_init_of_cim_init_mg_path(
            self._c, cim_init_mg_path)
        if len(cim_inits) == 0:
            raise LsmError(ErrorNumber.EMPTY_ACCESS_GROUP,
                           "Access group %s is empty(no member), " %
                           access_group.id +
                           "will not do volume_mask()")

        if access_group.init_type != AccessGroup.INIT_TYPE_WWPN and \
           access_group.init_type != AccessGroup.INIT_TYPE_ISCSI_IQN:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "SMI-S plugin only support iSCSI and FC/FCoE "
                           "access group volume masking, but got "
                           "access group init_type: %d" %
                           access_group.init_type)

        cim_vol_path = smis_vol.lsm_vol_to_cim_vol_path(self._c, volume)

        cim_gmms = self._c.cim_gmms_of_sys_id(access_group.system_id)

        cim_spcs_path = self._c.AssociatorNames(
            cim_init_mg_path,
            AssocClass='CIM_AssociatedInitiatorMaskingGroup',
            ResultClass='CIM_SCSIProtocolController')

        if len(cim_spcs_path) == 0:
            # We have to create the SPC and dev_mg now.
            cim_sys = smis_sys.cim_sys_of_sys_id(
                self._c, access_group.system_id)

            cim_tgt_mg_path = self._cim_tgt_mg_path_create(
                cim_sys.path, cim_gmms.path, access_group.name,
                access_group.init_type)
            cim_dev_mg_path = self._cim_dev_mg_path_create(
                cim_gmms.path, access_group.name, cim_vol_path, volume.id)
            # Done when SPC created.
            self._cim_spc_path_create(
                cim_gmms.path, cim_init_mg_path, cim_tgt_mg_path,
                cim_dev_mg_path, access_group.name)
        else:
            # CIM_InitiatorMaskingGroup might have multiple SPC when having
            # many tgt_mg. It's seldom use, but possible.
            for cim_spc_path in cim_spcs_path:
                # Check whether already masked
                cim_vols = smis_ag.cim_vols_masked_to_cim_spc_path(
                    self._c, cim_spc_path, smis_vol.cim_vol_id_pros())
                for cur_cim_vol in cim_vols:
                    if smis_vol.vol_id_of_cim_vol(cur_cim_vol) == volume.id:
                        raise LsmError(
                            ErrorNumber.NO_STATE_CHANGE,
                            "Volume already masked to requested access group")

                # SNIA require each cim_spc only have one cim_dev_mg
                # associated
                cim_dev_mg_path = self._c.AssociatorNames(
                    cim_spc_path,
                    AssocClass='CIM_AssociatedDeviceMaskingGroup',
                    ResultClass='CIM_DeviceMaskingGroup')[0]
                in_params = {
                    'MaskingGroup': cim_dev_mg_path,
                    'Members': [cim_vol_path],
                }
                self._c.invoke_method_wait(
                    'AddMembers', cim_gmms.path, in_params)
        return None

    @handle_cim_errors
    def volume_mask(self, access_group, volume, flags=0):
        """
        Grant access to a volume to an group
        """
        mask_type = smis_cap.mask_type(self._c, raise_error=True)
        # Workaround for EMC VNX/CX
        if mask_type == smis_cap.MASK_TYPE_GROUP:
            cim_sys = smis_sys.cim_sys_of_sys_id(self._c, volume.system_id)
            if cim_sys.path.classname == 'Clar_StorageSystem':
                mask_type = smis_cap.MASK_TYPE_MASK

        if mask_type == smis_cap.MASK_TYPE_GROUP:
            return self._volume_mask_group(access_group, volume, flags)
        return self._volume_mask_old(access_group, volume, flags)

    def _cim_vol_masked_to_spc(self, cim_spc_path, vol_id, property_list=None):
        """
        Check whether provided volume id is masked to cim_spc_path.
        If so, return cim_vol, or return None
        """
        if property_list is None:
            property_list = smis_vol.cim_vol_id_pros()
        else:
            property_list = merge_list(
                property_list, smis_vol.cim_vol_id_pros())

        masked_cim_vols = smis_ag.cim_vols_masked_to_cim_spc_path(
            self._c, cim_spc_path, property_list)
        for masked_cim_vol in masked_cim_vols:
            if smis_vol.vol_id_of_cim_vol(masked_cim_vol) == vol_id:
                return masked_cim_vol

        return None

    def _volume_mask_old(self, access_group, volume, flags):
        cim_spc_path = smis_ag.lsm_ag_to_cim_spc_path(self._c, access_group)

        cim_inits = smis_ag.cim_init_of_cim_spc_path(self._c, cim_spc_path)
        if len(cim_inits) == 0:
            raise LsmError(ErrorNumber.EMPTY_ACCESS_GROUP,
                           "Access group %s is empty(no member), " %
                           access_group.id +
                           "will not do volume_mask()")

        # Pre-Check: Already masked
        if self._cim_vol_masked_to_spc(cim_spc_path, volume.id):
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE,
                "Volume already masked to requested access group")

        cim_ccs = self._c.cim_ccs_of_sys_id(volume.system_id)

        cim_vol_path = smis_vol.lsm_vol_to_cim_vol_path(self._c, volume)
        cim_vol = self._c.GetInstance(cim_vol_path, PropertyList=['Name'])

        in_params = {'LUNames': [cim_vol['Name']],
                     'ProtocolControllers': [cim_spc_path],
                     'DeviceAccesses': [dmtf.CTRL_CONF_SRV_DA_RW]}

        self._c.invoke_method_wait('ExposePaths', cim_ccs.path, in_params)
        return None

    def _volume_unmask_group(self, access_group, volume):
        """
        Use CIM_GroupMaskingMappingService.RemoveMembers() against
        CIM_DeviceMaskingGroup
        If SupportedDeviceGroupFeatures does not allow empty
        DeviceMaskingGroup in SPC, we remove SPC and DeviceMaskingGroup.
        """
        cim_sys = smis_sys.cim_sys_of_sys_id(self._c, volume.system_id)

        cim_gmms_cap = self._c.Associators(
            cim_sys.path,
            AssocClass='CIM_ElementCapabilities',
            ResultClass='CIM_GroupMaskingMappingCapabilities',
            PropertyList=['SupportedDeviceGroupFeatures',
                          'SupportedSynchronousActions',
                          'SupportedAsynchronousActions'])[0]

        flag_empty_dev_in_spc = False

        if dmtf.GMM_CAP_DEV_MG_ALLOW_EMPTY_W_SPC in \
           cim_gmms_cap['SupportedDeviceGroupFeatures']:
            flag_empty_dev_in_spc = True

        if flag_empty_dev_in_spc is False:
            if ((dmtf.GMM_CAP_DELETE_SPC not in
                 cim_gmms_cap['SupportedSynchronousActions']) and
                (dmtf.GMM_CAP_DELETE_SPC not in
                 cim_gmms_cap['SupportedAsynchronousActions'])):
                raise LsmError(
                    ErrorNumber.NO_SUPPORT,
                    "volume_unmask() not supported. It requires one of these "
                    "1. support of DeleteMaskingView(). 2. allowing empty "
                    "DeviceMaskingGroup in SPC. But target SMI-S provider "
                    "does not support any of these")

        cim_vol_path = smis_vol.lsm_vol_to_cim_vol_path(self._c, volume)
        vol_cim_spcs_path = self._c.AssociatorNames(
            cim_vol_path,
            AssocClass='CIM_ProtocolControllerForUnit',
            ResultClass='CIM_SCSIProtocolController')

        if len(vol_cim_spcs_path) == 0:
            # Already unmasked
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE,
                "Volume is not masked to requested access group")

        cim_init_mg_path = smis_ag.lsm_ag_to_cim_init_mg_path(
            self._c, access_group)
        ag_cim_spcs_path = self._c.AssociatorNames(
            cim_init_mg_path,
            AssocClass='CIM_AssociatedInitiatorMaskingGroup',
            ResultClass='CIM_SCSIProtocolController')

        found_cim_spc_path = None
        for ag_cim_spc_path in ag_cim_spcs_path:
            for vol_cim_spc_path in vol_cim_spcs_path:
                if vol_cim_spc_path == ag_cim_spc_path:
                    found_cim_spc_path = vol_cim_spc_path
                    break

        if found_cim_spc_path is None:
            # Already unmasked
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE,
                "Volume is not masked to requested access group")

        # SNIA require each cim_spc only have one cim_dev_mg associated.
        cim_dev_mg_path = self._c.AssociatorNames(
            found_cim_spc_path,
            AssocClass='CIM_AssociatedDeviceMaskingGroup',
            ResultClass='CIM_DeviceMaskingGroup')[0]

        cim_gmms = self._c.cim_gmms_of_sys_id(volume.system_id)

        if flag_empty_dev_in_spc is False:
            # We have to check whether this volume is the last
            # one in the DeviceMaskingGroup, if so, we have to
            # delete the SPC
            cur_cim_vols_path = self._c.AssociatorNames(
                cim_dev_mg_path,
                AssocClass='CIM_OrderedMemberOfCollection',
                ResultClass='CIM_StorageVolume')
            if len(cur_cim_vols_path) == 1:
                # last volume, should delete SPC
                in_params = {
                    'ProtocolController': found_cim_spc_path,
                }
                self._c.invoke_method_wait(
                    'DeleteMaskingView', cim_gmms.path, in_params)

        in_params = {
            'MaskingGroup': cim_dev_mg_path,
            'Members': [cim_vol_path],
        }
        self._c.invoke_method_wait(
            'RemoveMembers', cim_gmms.path, in_params)

        return None

    @handle_cim_errors
    def volume_unmask(self, access_group, volume, flags=0):
        mask_type = smis_cap.mask_type(self._c, raise_error=True)
        # Workaround for EMC VNX/CX
        if mask_type == smis_cap.MASK_TYPE_GROUP:
            cim_sys = smis_sys.cim_sys_of_sys_id(self._c, volume.system_id)
            if cim_sys.path.classname == 'Clar_StorageSystem':
                mask_type = smis_cap.MASK_TYPE_MASK

        if mask_type == smis_cap.MASK_TYPE_GROUP:
            return self._volume_unmask_group(access_group, volume)
        return self._volume_unmask_old(access_group, volume)

    def _volume_unmask_old(self, access_group, volume):
        cim_ccs = self._c.cim_ccs_of_sys_id(volume.system_id)
        cim_spc_path = smis_ag.lsm_ag_to_cim_spc_path(self._c, access_group)

        # Pre-check: not masked
        cim_vol = self._cim_vol_masked_to_spc(
            cim_spc_path, volume.id, ['Name'])

        if cim_vol is None:
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE,
                "Volume is not masked to requested access group")

        hide_params = {'LUNames': [cim_vol['Name']],
                       'ProtocolControllers': [cim_spc_path]}

        self._c.invoke_method_wait('HidePaths', cim_ccs.path, hide_params)
        return None

    def _is_access_group(self, cim_spc):
        if self._c.is_netappe():
            return True

        rc = True
        _SMIS_EMC_ADAPTER_ROLE_MASKING = 'MASK_VIEW'

        if 'EMCAdapterRole' in cim_spc:
            # Currently SNIA does not define LUN mapping.
            # EMC is using their specific way for LUN mapping which
            # expose their frontend ports as a SPC(SCSIProtocolController).
            # which we shall filter out.
            emc_adp_roles = cim_spc['EMCAdapterRole'].split(' ')
            if _SMIS_EMC_ADAPTER_ROLE_MASKING not in emc_adp_roles:
                rc = False
        return rc

    def _cim_spc_of(self, system_id, property_list=None):
        """
        Return a list of CIM_SCSIProtocolController.
        Following SNIA SMIS 'Masking and Mapping Profile':
            CIM_ControllerConfigurationService
                |
                | CIM_ConcreteDependency
                v
            CIM_SCSIProtocolController
        """
        cim_ccs = None
        rc_cim_spcs = []

        if property_list is None:
            property_list = []

        try:
            cim_ccs = self._c.cim_ccs_of_sys_id(system_id, raise_error=False)
        except CIMError as ce:
            error_code = tuple(ce)[0]
            if error_code == pywbem.CIM_ERR_INVALID_CLASS or \
               error_code == pywbem.CIM_ERR_INVALID_PARAMETER:
                raise LsmError(ErrorNumber.NO_SUPPORT,
                               'AccessGroup is not supported ' +
                               'by this array')
        if cim_ccs is None:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           'AccessGroup is not supported by this array')

        cim_spcs = self._c.Associators(
            cim_ccs.path,
            AssocClass='CIM_ConcreteDependency',
            ResultClass='CIM_SCSIProtocolController',
            PropertyList=property_list)
        for cim_spc in cim_spcs:
            if self._is_access_group(cim_spc):
                rc_cim_spcs.append(cim_spc)
        return rc_cim_spcs

    @handle_cim_errors
    def volumes_accessible_by_access_group(self, access_group, flags=0):
        mask_type = smis_cap.mask_type(self._c, raise_error=True)
        cim_vols = []
        cim_vol_pros = smis_vol.cim_vol_pros()

        # Workaround for EMC VNX/CX
        if mask_type == smis_cap.MASK_TYPE_GROUP:
            cim_sys = smis_sys.cim_sys_of_sys_id(
                self._c, access_group.system_id)
            if cim_sys.path.classname == 'Clar_StorageSystem':
                mask_type = smis_cap.MASK_TYPE_MASK

        if mask_type == smis_cap.MASK_TYPE_GROUP:
            cim_init_mg_path = smis_ag.lsm_ag_to_cim_init_mg_path(
                self._c, access_group)

            cim_spcs_path = self._c.AssociatorNames(
                cim_init_mg_path,
                AssocClass='CIM_AssociatedInitiatorMaskingGroup',
                ResultClass='CIM_SCSIProtocolController')

            for cim_spc_path in cim_spcs_path:
                cim_vols.extend(
                    smis_ag.cim_vols_masked_to_cim_spc_path(
                        self._c, cim_spc_path, cim_vol_pros))
        else:
            cim_spc_path = smis_ag.lsm_ag_to_cim_spc_path(
                self._c, access_group)
            cim_vols = smis_ag.cim_vols_masked_to_cim_spc_path(
                self._c, cim_spc_path, cim_vol_pros)
        rc = []
        for cim_vol in cim_vols:
            pool_id = smis_pool.pool_id_of_cim_vol(self._c, cim_vol.path)
            sys_id = smis_sys.sys_id_of_cim_vol(cim_vol)
            rc.append(
                smis_vol.cim_vol_to_lsm_vol(cim_vol, pool_id, sys_id))
        return rc

    @handle_cim_errors
    def access_groups_granted_to_volume(self, volume, flags=0):
        rc = []
        mask_type = smis_cap.mask_type(self._c, raise_error=True)
        cim_vol_path = smis_vol.lsm_vol_to_cim_vol_path(self._c, volume)

        # Workaround for EMC VNX/CX
        if mask_type == smis_cap.MASK_TYPE_GROUP:
            cim_sys = smis_sys.cim_sys_of_sys_id(self._c, volume.system_id)
            if cim_sys.path.classname == 'Clar_StorageSystem':
                mask_type = smis_cap.MASK_TYPE_MASK

        cim_spc_pros = None
        if mask_type == smis_cap.MASK_TYPE_GROUP:
            cim_spc_pros = []
        else:
            cim_spc_pros = smis_ag.cim_spc_pros()

        cim_spcs = self._c.Associators(
            cim_vol_path,
            AssocClass='CIM_ProtocolControllerForUnit',
            ResultClass='CIM_SCSIProtocolController',
            PropertyList=cim_spc_pros)

        if mask_type == smis_cap.MASK_TYPE_GROUP:
            cim_init_mg_pros = smis_ag.cim_init_mg_pros()
            for cim_spc in cim_spcs:
                cim_init_mgs = self._c.Associators(
                    cim_spc.path,
                    AssocClass='CIM_AssociatedInitiatorMaskingGroup',
                    ResultClass='CIM_InitiatorMaskingGroup',
                    PropertyList=cim_init_mg_pros)
                rc.extend(
                    list(
                        smis_ag.cim_init_mg_to_lsm_ag(
                            self._c, x, volume.system_id)
                        for x in cim_init_mgs))
        else:
            for cim_spc in cim_spcs:
                if self._is_access_group(cim_spc):
                    rc.append(
                        smis_ag.cim_spc_to_lsm_ag(
                            self._c, cim_spc, volume.system_id))

        return rc

    def _cim_init_mg_of(self, system_id, property_list=None):
        """
        We use this association to get all CIM_InitiatorMaskingGroup:
            CIM_GroupMaskingMappingService
                    |
                    | CIM_ServiceAffectsElement
                    v
            CIM_InitiatorMaskingGroup
        """
        if property_list is None:
            property_list = []

        cim_gmms = self._c.cim_gmms_of_sys_id(system_id)

        return self._c.Associators(
            cim_gmms.path,
            AssocClass='CIM_ServiceAffectsElement',
            ResultClass='CIM_InitiatorMaskingGroup',
            PropertyList=property_list)

    @handle_cim_errors
    def access_groups(self, search_key=None, search_value=None, flags=0):
        rc = []
        mask_type = smis_cap.mask_type(self._c, raise_error=True)

        cim_sys_pros = smis_sys.cim_sys_id_pros()
        cim_syss = smis_sys.root_cim_sys(self._c, cim_sys_pros)

        cim_spc_pros = smis_ag.cim_spc_pros()
        for cim_sys in cim_syss:
            if cim_sys.path.classname == 'Clar_StorageSystem':
                # Workaround for EMC VNX/CX.
                # Even they claim support of Group M&M via
                # CIM_RegisteredProfile, but actually they don't support it.
                mask_type = smis_cap.MASK_TYPE_MASK

            system_id = smis_sys.sys_id_of_cim_sys(cim_sys)
            if mask_type == smis_cap.MASK_TYPE_GROUP:
                cim_init_mg_pros = smis_ag.cim_init_mg_pros()
                cim_init_mgs = self._cim_init_mg_of(
                    system_id, cim_init_mg_pros)
                rc.extend(
                    list(
                        smis_ag.cim_init_mg_to_lsm_ag(self._c, x, system_id)
                        for x in cim_init_mgs))
            elif mask_type == smis_cap.MASK_TYPE_MASK:
                cim_spcs = self._cim_spc_of(system_id, cim_spc_pros)
                rc.extend(
                    list(
                        smis_ag.cim_spc_to_lsm_ag(self._c, cim_spc, system_id)
                        for cim_spc in cim_spcs))
            else:
                raise LsmError(ErrorNumber.PLUGIN_BUG,
                               "_get_cim_spc_by_id(): Got invalid mask_type: "
                               "%s" % mask_type)

        return search_property(rc, search_key, search_value)

    def _ag_init_add_group(self, access_group, init_id, init_type):
        cim_sys = smis_sys.cim_sys_of_sys_id(self._c, access_group.system_id)

        if cim_sys.path.classname == 'Clar_StorageSystem':
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "EMC VNX/CX require WWNN defined when adding a "
                           "new initiator which is not supported by LSM yet. "
                           "Please do it via EMC vendor specific tools.")

        cim_init_mg_path = smis_ag.lsm_ag_to_cim_init_mg_path(
            self._c, access_group)

        exist_cim_inits = smis_ag.cim_init_of_cim_init_mg_path(
            self._c, cim_init_mg_path)

        # Check whether already added.
        for exist_cim_init in exist_cim_inits:
            if smis_ag.init_id_of_cim_init(exist_cim_init) == init_id:
                return copy.deepcopy(access_group)

        cim_init_path = smis_ag.cim_init_path_check_or_create(
            self._c, access_group.system_id, init_id, init_type)

        cim_gmms = self._c.cim_gmms_of_sys_id(access_group.system_id)

        in_params = {
            'MaskingGroup': cim_init_mg_path,
            'Members': [cim_init_path],
        }

        new_cim_init_mg_path = self._c.invoke_method_wait(
            'AddMembers', cim_gmms.path, in_params,
            out_key='MaskingGroup', expect_class='CIM_InitiatorMaskingGroup')
        cim_init_mg_pros = smis_ag.cim_init_mg_pros()
        new_cim_init_mg = self._c.GetInstance(
            new_cim_init_mg_path, PropertyList=cim_init_mg_pros,
            LocalOnly=False)
        return smis_ag.cim_init_mg_to_lsm_ag(
            self._c, new_cim_init_mg, access_group.system_id)

    @handle_cim_errors
    def access_group_initiator_add(self, access_group, init_id, init_type,
                                   flags=0):
        init_id = smis_ag.lsm_init_id_to_snia(init_id)
        mask_type = smis_cap.mask_type(self._c, raise_error=True)

        if mask_type == smis_cap.MASK_TYPE_GROUP:
            return self._ag_init_add_group(access_group, init_id, init_type)
        else:
            return self._ag_init_add_old(access_group, init_id, init_type)

    def _ag_init_add_old(self, access_group, init_id, init_type):
        # CIM_StorageHardwareIDManagementService.CreateStorageHardwareID()
        # is mandatory since 1.4rev6
        cim_sys = smis_sys.cim_sys_of_sys_id(self._c, access_group.system_id)

        if cim_sys.path.classname == 'Clar_StorageSystem':
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "EMC VNX/CX require WWNN defined when adding "
                           "new initiator which is not supported by LSM yet. "
                           "Please do it via EMC vendor specific tools. "
                           "EMC VNX does not support adding iSCSI IQN neither")

        cim_spc_path = smis_ag.lsm_ag_to_cim_spc_path(
            self._c, access_group)

        exist_cim_inits = smis_ag.cim_init_of_cim_spc_path(
            self._c, cim_spc_path)

        for exist_cim_init in exist_cim_inits:
            if smis_ag.init_id_of_cim_init(exist_cim_init) == init_id:
                return copy.deepcopy(access_group)

        # Check to see if we have this initiator already, if not we
        # create it and then add to the view.

        smis_ag.cim_init_path_check_or_create(
            self._c, access_group.system_id, init_id, init_type)

        cim_ccs = self._c.cim_ccs_of_sys_id(access_group.system_id)

        in_params = {'InitiatorPortIDs': [init_id],
                     'ProtocolControllers': [cim_spc_path]}

        cim_spc_path = self._c.invoke_method_wait(
            'ExposePaths', cim_ccs.path, in_params,
            out_key='ProtocolControllers', flag_out_array=True,
            expect_class='CIM_SCSIProtocolController')

        cim_spc_pros = smis_ag.cim_spc_pros()
        cim_spc = self._c.GetInstance(
            cim_spc_path, PropertyList=cim_spc_pros, LocalOnly=False)
        return smis_ag.cim_spc_to_lsm_ag(
            self._c, cim_spc, access_group.system_id)

    def _ag_init_del_group(self, access_group, init_id):
        """
        Call CIM_GroupMaskingMappingService.RemoveMembers() against
        CIM_InitiatorMaskingGroup.
        """
        cim_init_mg_path = smis_ag.lsm_ag_to_cim_init_mg_path(
            self._c, access_group)
        cur_cim_inits = smis_ag.cim_init_of_cim_init_mg_path(
            self._c, cim_init_mg_path)

        cim_init = None
        for cur_cim_init in cur_cim_inits:
            if smis_ag.init_id_of_cim_init(cur_cim_init) == init_id:
                cim_init = cur_cim_init
                break

        if cim_init is None:
            raise LsmError(ErrorNumber.NO_STATE_CHANGE,
                           "Initiator %s does not exist in defined "
                           "access group %s" %
                           (init_id, access_group.id))

        if len(cur_cim_inits) == 1:
            raise LsmError(ErrorNumber.LAST_INIT_IN_ACCESS_GROUP,
                           "Refuse to remove last initiator from access group")

        cim_gmms = self._c.cim_gmms_of_sys_id(access_group.system_id)

        # RemoveMembers from InitiatorMaskingGroup
        in_params = {
            'MaskingGroup': cim_init_mg_path,
            'Members': [cim_init.path],
        }

        self._c.invoke_method_wait('RemoveMembers', cim_gmms.path, in_params)

        cim_init_mg_pros = smis_ag.cim_init_mg_pros()
        cim_init_mg = self._c.GetInstance(
            cim_init_mg_path, PropertyList=cim_init_mg_pros)

        return smis_ag.cim_init_mg_to_lsm_ag(
            self._c, cim_init_mg, access_group.system_id)

    @handle_cim_errors
    def access_group_initiator_delete(self, access_group, init_id, init_type,
                                      flags=0):
        if self._c.is_netappe():
            # When using HidePaths to remove initiator, the whole SPC will be
            # removed. Before we find a workaround for this, I would like to
            # have this method disabled as NO_SUPPORT.
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "SMI-S plugin does not support "
                           "access_group_initiator_delete() against NetApp-E")
        init_id = smis_ag.lsm_init_id_to_snia(init_id)
        mask_type = smis_cap.mask_type(self._c, raise_error=True)

        if mask_type == smis_cap.MASK_TYPE_GROUP:
            return self._ag_init_del_group(access_group, init_id)
        else:
            return self._ag_init_del_old(access_group, init_id)

    def _ag_init_del_old(self, access_group, init_id):
        cim_spc_path = smis_ag.lsm_ag_to_cim_spc_path(self._c, access_group)

        cim_ccs = self._c.cim_ccs_of_sys_id(access_group.system_id)

        hide_params = {'InitiatorPortIDs': [init_id],
                       'ProtocolControllers': [cim_spc_path]}
        self._c.invoke_method_wait('HidePaths', cim_ccs.path, hide_params)

        return None

    @handle_cim_errors
    def job_free(self, job_id, flags=0):
        """
        Frees the resources given a job number.
        """
        cim_job = self._c.cim_job_of_job_id(job_id, ['DeleteOnCompletion'])

        # See if we should delete the job
        if not cim_job['DeleteOnCompletion']:
            try:
                self._c.DeleteInstance(cim_job.path)
            except CIMError:
                pass

    @handle_cim_errors
    def disks(self, search_key=None, search_value=None, flags=0):
        """
        return all object of data.Disk.
        We are using "Disk Drive Lite Subprofile" v1.4 of SNIA SMI-S for these
        classes to create LSM Disk:
            CIM_DiskDrive
            CIM_StorageExtent (Primordial)
        Due to 'Multiple Computer System' profile, disks might associated to
        sub ComputerSystem. To improve performance of listing disks, we will
        use EnumerateInstances(). Which means we have to filter the results
        by ourselves in case URI contain 'system=xxx'.
        """
        rc = []
        self._c.profile_check(SmisCommon.SNIA_DISK_LITE_PROFILE,
                              SmisCommon.SMIS_SPEC_VER_1_4,
                              raise_error=True)
        cim_disk_pros = smis_disk.cim_disk_pros()
        cim_disks = self._c.EnumerateInstances(
            'CIM_DiskDrive', PropertyList=cim_disk_pros)
        for cim_disk in cim_disks:
            if self._c.system_list and \
               smis_disk.sys_id_of_cim_disk(cim_disk) not in \
               self._c.system_list:
                continue

            rc.extend([smis_disk.cim_disk_to_lsm_disk(self._c, cim_disk)])
        return search_property(rc, search_key, search_value)

    @staticmethod
    def _is_frontend_fc_tgt(cim_fc_tgt):
        """
        Check CIM_FCPort['UsageRestriction'] for frontend port.
        """
        dmtf_usage = cim_fc_tgt['UsageRestriction']
        if dmtf_usage == dmtf.TGT_PORT_USAGE_FRONTEND_ONLY or \
           dmtf_usage == dmtf.TGT_PORT_USAGE_UNRESTRICTED:
            return True
        return False

    def _cim_fc_tgt_of(self, cim_sys_path, property_list=None):
        """
        Get all CIM_FCPort (frontend only) from CIM_ComputerSystem and its
        leaf CIM_ComputerSystem
        """
        rc = []
        if property_list is None:
            property_list = ['UsageRestriction']
        else:
            property_list = merge_list(property_list, ['UsageRestriction'])
        all_cim_syss_path = [cim_sys_path]
        if smis_cap.multi_sys_is_supported(self._c):
            all_cim_syss_path.extend(
                self._leaf_cim_syss_path_of(cim_sys_path))
        for cur_cim_sys_path in all_cim_syss_path:
            cur_cim_fc_tgts = self._c.Associators(
                cur_cim_sys_path,
                AssocClass='CIM_SystemDevice',
                ResultClass='CIM_FCPort',
                PropertyList=property_list)
            for cim_fc_tgt in cur_cim_fc_tgts:
                if Smis._is_frontend_fc_tgt(cim_fc_tgt):
                    rc.extend([cim_fc_tgt])
        return rc

    @staticmethod
    def _cim_fc_tgt_to_lsm(cim_fc_tgt, system_id):
        """
        Convert CIM_FCPort to Lsm.TargetPort
        """
        port_id = md5(cim_fc_tgt['DeviceID'])
        port_type = _lsm_tgt_port_type_of_cim_fc_tgt(cim_fc_tgt)
        # SNIA define WWPN string as upper, no splitter, 16 digits.
        # No need to check.
        wwpn = hex_string_format(cim_fc_tgt['PermanentAddress'], 16, 2)
        port_name = cim_fc_tgt['ElementName']
        plugin_data = None
        return TargetPort(port_id, port_type, wwpn, wwpn, wwpn, port_name,
                          system_id, plugin_data)

    def _iscsi_node_name_of(self, cim_iscsi_pg_path):
        """
            CIM_iSCSIProtocolEndpoint
                    |
                    |
                    v
            CIM_SAPAvailableForElement
                    |
                    |
                    v
            CIM_SCSIProtocolController  # iSCSI Node

        """
        cim_spcs = self._c.Associators(
            cim_iscsi_pg_path,
            ResultClass='CIM_SCSIProtocolController',
            AssocClass='CIM_SAPAvailableForElement',
            PropertyList=['Name', 'NameFormat'])
        cim_iscsi_nodes = []
        for cim_spc in cim_spcs:
            if cim_spc.classname == 'Clar_MappingSCSIProtocolController':
                # EMC has vendor specific class which contain identical
                # properties of SPC for iSCSI node.
                continue
            if cim_spc['NameFormat'] == dmtf.SPC_NAME_FORMAT_ISCSI:
                cim_iscsi_nodes.extend([cim_spc])

        if len(cim_iscsi_nodes) == 0:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "_iscsi_node_of(): No iSCSI node "
                           "CIM_SCSIProtocolController associated to %s"
                           % cim_iscsi_pg_path)
        if len(cim_iscsi_nodes) > 1:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "_iscsi_node_of(): Got two or more iSCSI node "
                           "CIM_SCSIProtocolController associated to %s: %s"
                           % (cim_iscsi_pg_path, cim_iscsi_nodes))
        return cim_iscsi_nodes[0]['Name']

    def _cim_iscsi_pg_of(self, cim_sys_path, property_list=None):
        """
        Get all CIM_iSCSIProtocolEndpoint(Target only) from CIM_ComputerSystem
        and its leaf CIM_ComputerSystem
        """
        rc = []
        if property_list is None:
            property_list = ['Role']
        else:
            property_list = merge_list(property_list, ['Role'])
        all_cim_syss_path = [cim_sys_path]
        if smis_cap.multi_sys_is_supported(self._c):
            all_cim_syss_path.extend(
                self._leaf_cim_syss_path_of(cim_sys_path))
        for cur_cim_sys_path in all_cim_syss_path:
            cur_cim_iscsi_pgs = self._c.Associators(
                cur_cim_sys_path,
                AssocClass='CIM_HostedAccessPoint',
                ResultClass='CIM_iSCSIProtocolEndpoint',
                PropertyList=property_list)
            for cim_iscsi_pg in cur_cim_iscsi_pgs:
                if cim_iscsi_pg['Role'] == dmtf.ISCSI_TGT_ROLE_TARGET:
                    rc.extend([cim_iscsi_pg])
        return rc

    def _cim_iscsi_pg_to_lsm(self, cim_iscsi_pg, system_id):
        """
        Return a list of TargetPort CIM_iSCSIProtocolEndpoint
        Associations:
            CIM_SCSIProtocolController  # iSCSI Node
                    ^
                    |   CIM_SAPAvailableForElement
                    |
            CIM_iSCSIProtocolEndpoint   # iSCSI Portal Group
                    |
                    |   CIM_BindsTo
                    v
            CIM_TCPProtocolEndpoint     # Need TCP port, default is 3260
                    |
                    |   CIM_BindsTo
                    v
            CIM_IPProtocolEndpoint      # Need IPv4 and IPv6 address
                    |
                    |   CIM_DeviceSAPImplementation
                    v
            CIM_EthernetPort            # Need MAC address (Optional)
        Assuming there is storage array support iSER
        (iSCSI over RDMA of Infinity Band),
        this method is only for iSCSI over TCP.
        """
        rc = []
        port_type = TargetPort.TYPE_ISCSI
        plugin_data = None
        cim_tcps = self._c.Associators(
            cim_iscsi_pg.path,
            ResultClass='CIM_TCPProtocolEndpoint',
            AssocClass='CIM_BindsTo',
            PropertyList=['PortNumber'])
        if len(cim_tcps) == 0:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "_cim_iscsi_pg_to_lsm():  "
                           "No CIM_TCPProtocolEndpoint associated to %s"
                           % cim_iscsi_pg.path)
        iscsi_node_name = self._iscsi_node_name_of(cim_iscsi_pg.path)

        for cim_tcp in cim_tcps:
            tcp_port = cim_tcp['PortNumber']
            cim_ips = self._c.Associators(
                cim_tcp.path,
                ResultClass='CIM_IPProtocolEndpoint',
                AssocClass='CIM_BindsTo',
                PropertyList=['IPv4Address', 'IPv6Address', 'SystemName',
                              'EMCPortNumber', 'IPv6AddressType'])
            for cim_ip in cim_ips:
                ipv4_addr = ''
                ipv6_addr = ''
                # 'IPv4Address', 'IPv6Address' are optional in SMI-S 1.4.
                if 'IPv4Address' in cim_ip and cim_ip['IPv4Address']:
                    ipv4_addr = cim_ip['IPv4Address']
                if 'IPv6Address' in cim_ip and cim_ip['IPv6Address']:
                    ipv6_addr = cim_ip['IPv6Address']
                # 'IPv6AddressType' is not listed in SMI-S but in DMTF CIM
                # Schema
                # Only allow IPv6 Global Unicast Address, 6to4, and Unique
                # Local Address.
                if 'IPv6AddressType' in cim_ip and cim_ip['IPv6AddressType']:
                    ipv6_addr_type = cim_ip['IPv6AddressType']
                    if ipv6_addr_type != dmtf.IPV6_ADDR_TYPE_GUA and \
                       ipv6_addr_type != dmtf.IPV6_ADDR_TYPE_6TO4 and \
                       ipv6_addr_type != dmtf.IPV6_ADDR_TYPE_ULA:
                        ipv6_addr = ''

                # NetApp is using this kind of IPv6 address
                # 0000:0000:0000:0000:0000:0000:0a10:29d5
                # even when IPv6 is not enabled on their array.
                # It's not a legal IPv6 address anyway. No need to do
                # vendor check.
                if ipv6_addr[0:29] == '0000:0000:0000:0000:0000:0000':
                    ipv6_addr = ''

                if ipv4_addr is None and ipv6_addr is None:
                    continue
                cim_eths = self._c.Associators(
                    cim_ip.path,
                    ResultClass='CIM_EthernetPort',
                    AssocClass='CIM_DeviceSAPImplementation',
                    PropertyList=['PermanentAddress', 'ElementName'])
                nics = []
                # NetApp ONTAP cluster-mode show one IP bonded to multiple
                # ethernet,
                # Not sure it's their BUG or real ethernet channel bonding.
                # Waiting reply.
                if len(cim_eths) == 0:
                    nics = [('', '')]
                else:
                    for cim_eth in cim_eths:
                        mac_addr = ''
                        port_name = ''
                        if 'PermanentAddress' in cim_eth and \
                           cim_eth["PermanentAddress"]:
                            mac_addr = cim_eth["PermanentAddress"]
                        # 'ElementName' is optional in CIM_EthernetPort
                        if 'ElementName' in cim_eth and cim_eth["ElementName"]:
                            port_name = cim_eth['ElementName']
                        nics.extend([(mac_addr, port_name)])
                for nic in nics:
                    mac_address = nic[0]
                    port_name = nic[1]
                    if mac_address:
                        # Convert to lsm require form
                        mac_address = hex_string_format(mac_address, 12, 2)

                    if ipv4_addr:
                        network_address = "%s:%s" % (ipv4_addr, tcp_port)
                        port_id = md5("%s:%s:%s" % (mac_address,
                                                    network_address,
                                                    iscsi_node_name))
                        rc.extend(
                            [TargetPort(port_id, port_type, iscsi_node_name,
                                        network_address, mac_address,
                                        port_name, system_id, plugin_data)])
                    if ipv6_addr:
                        # DMTF or SNIA did defined the IPv6 string format.
                        # we just guess here.
                        if len(ipv6_addr) == 39:
                            ipv6_addr = ipv6_addr.replace(':', '')
                            if len(ipv6_addr) == 32:
                                ipv6_addr = hex_string_format(
                                    ipv6_addr, 32, 4)

                        network_address = "[%s]:%s" % (ipv6_addr, tcp_port)
                        port_id = md5("%s:%s:%s" % (mac_address,
                                                    network_address,
                                                    iscsi_node_name))
                        rc.extend(
                            [TargetPort(port_id, port_type, iscsi_node_name,
                                        network_address, mac_address,
                                        port_name, system_id, plugin_data)])
        return rc

    def _leaf_cim_syss_path_of(self, cim_sys_path):
        """
        Return a list of CIMInstanceName of leaf CIM_ComputerSystem
        """
        max_loop_count = 10   # There is no storage array need 10 layer of
                              # Computer
        loop_counter = max_loop_count
        rc = []
        leaf_cim_syss_path = []
        try:
            leaf_cim_syss_path = self._c.AssociatorNames(
                cim_sys_path,
                ResultClass='CIM_ComputerSystem',
                AssocClass='CIM_ComponentCS',
                Role='GroupComponent',
                ResultRole='PartComponent')
        except CIMError as ce:
            error_code = tuple(ce)[0]
            if error_code == pywbem.CIM_ERR_INVALID_CLASS or \
               error_code == pywbem.CIM_ERR_NOT_SUPPORTED:
                return []

        if len(leaf_cim_syss_path) > 0:
            rc = leaf_cim_syss_path
            for cim_sys_path in leaf_cim_syss_path:
                rc.extend(self._leaf_cim_syss_path_of(cim_sys_path))

        return rc

    @handle_cim_errors
    def target_ports(self, search_key=None, search_value=None, flags=0):
        rc = []

        cim_fc_tgt_pros = ['UsageRestriction', 'ElementName', 'SystemName',
                           'PermanentAddress', 'PortDiscriminator',
                           'LinkTechnology', 'DeviceID']

        cim_syss = smis_sys.root_cim_sys(
            self._c, property_list=smis_sys.cim_sys_id_pros())
        for cim_sys in cim_syss:
            system_id = smis_sys.sys_id_of_cim_sys(cim_sys)
            flag_fc_support = smis_cap.fc_tgt_is_supported(self._c)
            flag_iscsi_support = smis_cap.iscsi_tgt_is_supported(self._c)

            # Assuming: if one system does not support target_ports(),
            # all systems from the same provider will not support
            # target_ports().
            if flag_fc_support is False and flag_iscsi_support is False:
                raise LsmError(ErrorNumber.NO_SUPPORT,
                               "Target SMI-S provider does not support any of"
                               "these profiles: '%s %s', '%s %s'"
                               % (SmisCommon.SMIS_SPEC_VER_1_4,
                                  SmisCommon.SNIA_FC_TGT_PORT_PROFILE,
                                  SmisCommon.SMIS_SPEC_VER_1_4,
                                  SmisCommon.SNIA_ISCSI_TGT_PORT_PROFILE))

            if flag_fc_support:
                # CIM_FCPort might be not belong to root cim_sys
                # In that case, CIM_FCPort['SystemName'] will not be
                # the name of root CIM_ComputerSystem.
                cim_fc_tgt_pros = ['UsageRestriction', 'ElementName',
                                   'SystemName', 'PermanentAddress',
                                   'PortDiscriminator', 'LinkTechnology',
                                   'DeviceID']
                cim_fc_tgts = self._cim_fc_tgt_of(cim_sys.path,
                                                  cim_fc_tgt_pros)
                rc.extend(
                    list(
                        Smis._cim_fc_tgt_to_lsm(x, system_id)
                        for x in cim_fc_tgts))

            if flag_iscsi_support:
                cim_iscsi_pgs = self._cim_iscsi_pg_of(cim_sys.path)
                for cim_iscsi_pg in cim_iscsi_pgs:
                    rc.extend(
                        self._cim_iscsi_pg_to_lsm(cim_iscsi_pg, system_id))

        # NetApp is sharing CIM_TCPProtocolEndpoint which
        # cause duplicate TargetPort. It's a long story, they heard my
        # bug report.
        if len(cim_syss) >= 1 and \
           cim_syss[0].classname == 'ONTAP_StorageSystem':
            id_list = []
            new_rc = []
            # We keep the original list order by not using dict.values()
            for lsm_tp in rc:
                if lsm_tp.id not in id_list:
                    id_list.extend([lsm_tp.id])
                    new_rc.extend([lsm_tp])
            rc = new_rc

        return search_property(rc, search_key, search_value)

    def _cim_pep_path_of_fc_tgt(self, cim_fc_tgt_path):
        """
        Return CIMInstanceName of CIM_SCSIProtocolEndpoint of CIM_FCPort
        In 1.4r6, it's one-to-one map.
        """
        return self._c.AssociatorNames(
            cim_fc_tgt_path,
            AssocClass='CIM_DeviceSAPImplementation',
            ResultClass='CIM_SCSIProtocolEndpoint')[0]

    def _check_exist_cim_tgt_mg(self, name):
        """
        We should do more checks[1] in stead of use it directly.
        But considering EMC VMAX is the only support vendor, make it quick
        and works could be priority 1.
        We can improve this for any bug report.

        [1] At least check whether CIM_TargetMaskingGroup is already used
            by other SPC.
        """
        cim_tgt_mgs = self._c.EnumerateInstances(
            'CIM_TargetMaskingGroup',
            PropertyList=['ElementName'])
        for cim_tgt_mg in cim_tgt_mgs:
            if cim_tgt_mg['ElementName'] == name:
                return cim_tgt_mg.path

        return None

    def _check_exist_cim_dev_mg(self, name, cim_gmms_path, cim_vol_path,
                                vol_id):
        """
        This is buggy check, but it works on EMC VMAX which is only supported
        platform of Group Masking and Mapping.
        When found CIM_DeviceMaskingGroup, make sure cim_vol is included.
        """
        cim_dev_mgs = self._c.EnumerateInstances(
            'CIM_DeviceMaskingGroup',
            PropertyList=['ElementName'])
        cim_dev_mg = None
        for tmp_cim_dev_mg in cim_dev_mgs:
            if tmp_cim_dev_mg['ElementName'] == name:
                cim_dev_mg = tmp_cim_dev_mg
                break
        if cim_dev_mg:
            # Check whether cim_vol included.
            cim_vol_pros = smis_vol.cim_vol_id_pros()
            cim_vols = self._c.Associators(
                cim_dev_mg.path,
                AssocClass='CIM_OrderedMemberOfCollection',
                ResultClass='CIM_StorageVolume',
                PropertyList=cim_vol_pros)
            for cim_vol in cim_vols:
                if smis_vol.vol_id_of_cim_vol(cim_vol) == vol_id:
                    return cim_dev_mg.path

            # We should add this volume to found DeviceMaskingGroup
            in_params = {
                'MaskingGroup': cim_dev_mg.path,
                'Members': [cim_vol_path],
            }
            self._c.invoke_method_wait('AddMembers', cim_gmms_path, in_params)
            return cim_dev_mg.path

        return None

    @handle_cim_errors
    def access_group_create(self, name, init_id, init_type, system,
                            flags=0):
        """
        Using 1.5.0 'Group Masking and Mapping' profile.
        Actually, only EMC VMAX/DMX support this now(July 2014).
        Steps:
            0. Check exist SPC of init_id for duplication call and
               conflict.
            1. Create CIM_InitiatorMaskingGroup
        """
        org_init_id = init_id
        init_id = smis_ag.lsm_init_id_to_snia(init_id)

        self._c.profile_check(SmisCommon.SNIA_GROUP_MASK_PROFILE,
                              SmisCommon.SMIS_SPEC_VER_1_5,
                              raise_error=True)

        if init_type != AccessGroup.INIT_TYPE_WWPN and \
           init_type != AccessGroup.INIT_TYPE_ISCSI_IQN:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "SMI-S plugin only support creating FC/FCoE WWPN "
                           "and iSCSI AccessGroup")

        cim_sys = smis_sys.cim_sys_of_sys_id(self._c, system.id)
        if cim_sys.path.classname == 'Clar_StorageSystem':
            # EMC VNX/CX does not support Group M&M, which incorrectly exposed
            # in CIM_RegisteredProfile
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "access_group_create() is not supported by "
                           "EMC VNX/CX which lacks the support of SNIA 1.5+ "
                           "Group Masking and Mapping profile")

        flag_fc_support = smis_cap.fc_tgt_is_supported(self._c)
        flag_iscsi_support = smis_cap.iscsi_tgt_is_supported(self._c)

        if init_type == AccessGroup.INIT_TYPE_WWPN and not flag_fc_support:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "Target SMI-S provider does not support "
                           "FC target port, which not allow creating "
                           "WWPN access group")

        if init_type == AccessGroup.INIT_TYPE_ISCSI_IQN and \
           not flag_iscsi_support:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "Target SMI-S provider does not support "
                           "iSCSI target port, which not allow creating "
                           "iSCSI IQN access group")

        cim_init_path = smis_ag.cim_init_path_check_or_create(
            self._c, system.id, init_id, init_type)

        # Create CIM_InitiatorMaskingGroup
        cim_gmms = self._c.cim_gmms_of_sys_id(system.id)

        in_params = {'GroupName': name,
                     'Members': [cim_init_path],
                     'Type': dmtf.MASK_GROUP_TYPE_INIT}

        cim_init_mg_pros = smis_ag.cim_init_mg_pros()

        try:
            cim_init_mg_path = self._c.invoke_method_wait(
                'CreateGroup', cim_gmms.path, in_params,
                out_key='MaskingGroup',
                expect_class='CIM_InitiatorMaskingGroup')
        except (LsmError, CIMError):
            # Check possible failure
            # 1. Initiator already exist in other group.
            exist_cim_init_mg_paths = self._c.AssociatorNames(
                cim_init_path,
                AssocClass='CIM_MemberOfCollection',
                ResultClass='CIM_InitiatorMaskingGroup')

            if len(exist_cim_init_mg_paths) != 0:
                raise LsmError(ErrorNumber.EXISTS_INITIATOR,
                               "Initiator %s " % org_init_id +
                               "already exist in other access group")

            # 2. Requested name used by other group.
            exist_cim_init_mgs = self._cim_init_mg_of(
                system.id, property_list=['ElementName'])
            for exist_cim_init_mg in exist_cim_init_mgs:
                if exist_cim_init_mg['ElementName'] == name:
                    raise LsmError(ErrorNumber.NAME_CONFLICT,
                                   "Requested name %s is used by " % name +
                                   "another access group")
            raise

        cim_init_mg = self._c.GetInstance(
            cim_init_mg_path, PropertyList=cim_init_mg_pros)
        return smis_ag.cim_init_mg_to_lsm_ag(self._c, cim_init_mg, system.id)

    @handle_cim_errors
    def access_group_delete(self, access_group, flags=0):
        self._c.profile_check(
            SmisCommon.SNIA_GROUP_MASK_PROFILE, SmisCommon.SMIS_SPEC_VER_1_5,
            raise_error=True)

        cim_init_mg_path = smis_ag.lsm_ag_to_cim_init_mg_path(
            self._c, access_group)

        # Check whether still have volume masked.
        cim_spcs_path = self._c.AssociatorNames(
            cim_init_mg_path,
            AssocClass='CIM_AssociatedInitiatorMaskingGroup',
            ResultClass='CIM_SCSIProtocolController')

        for cim_spc_path in cim_spcs_path:
            if len(self._c.AssociatorNames(
                    cim_spc_path,
                    AssocClass='CIM_ProtocolControllerForUnit',
                    ResultClass='CIM_StorageVolume')) >= 1:
                raise LsmError(ErrorNumber.IS_MASKED,
                               "Access Group %s has volume masked" %
                               access_group.id)

        cim_gmms = self._c.cim_gmms_of_sys_id(access_group.system_id)

        in_params = {
            'MaskingGroup': cim_init_mg_path,
            'Force': True,
        }

        self._c.invoke_method_wait('DeleteGroup', cim_gmms.path, in_params)
        return None
