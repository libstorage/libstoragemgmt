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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
#
# Author: tasleson

from smis import Smis
import pywbem
import time
from smis import handle_cim_errors
from lsm import LsmError, ErrorNumber, Capabilities


class ESeries(Smis):

    def _deal_volume_associations(self, vol, lun):
        """
        Check a volume to see if it has any associations with other
        volumes.
        """
        rc = False
        lun_path = lun.path

        ss = self._c.References(lun_path,
                                ResultClass='CIM_StorageSynchronized')

        if len(ss):
            for s in ss:
                if 'SyncedElement' in s:
                    item = s['SyncedElement']

                    if Smis._cim_name_match(item, lun_path):
                        self._detach(vol, s)
                        rc = True

                if 'SystemElement' in s:
                    item = s['SystemElement']

                    if Smis._cim_name_match(item, lun_path):
                        self._detach(vol, s)
                        rc = True

        return rc

    def _is_access_group(self, s):
        return True

    @handle_cim_errors
    def access_group_del(self, group, flags=0):
        ccs = self._get_class_instance('CIM_ControllerConfigurationService')

        pc = self._get_cim_instance_by_id('AccessGroup', group.id)

        in_params = {'ProtocolController': pc.path}

        return self._pi("access_group_del", Smis.JOB_RETRIEVE_NONE,
                        *(self._c.InvokeMethod('DeleteProtocolController',
                                               ccs.path, **in_params)))[0]

    @handle_cim_errors
    def capabilities(self, system, flags=0):
        cap = self._common_capabilities(system)

        #We will explicitly set initiator grant/revoke
        cap.set(Capabilities.VOLUME_INITIATOR_GRANT)
        cap.set(Capabilities.VOLUME_INITIATOR_REVOKE)

        #TODO We need to investigate why our interrogation code doesn't work.
        #The array is telling us one thing, but when we try to use it, it
        #doesn't work
        return cap

    def _get_initiators_in_group(self, cim_grp):
        ag_init_ids = []
        cim_st_hwid_pros = self._property_list_of_id('Initiator')
        cim_st_hwids = self._get_cim_st_hwid_in_spc(cim_grp, cim_st_hwid_pros)
        ag_init_ids = [self._init_id(i) for i in cim_st_hwids]
        return ag_init_ids

    def _get_group_initiator_is_in(self, initiator_id):
        groups = self._get_access_groups()

        for g in groups:
            initiators = self._get_initiators_in_group(g)
            for i in initiators:
                if i == initiator_id:
                    return g

        return None

    @handle_cim_errors
    def initiator_grant(self, initiator_id, initiator_type, volume, access,
                        flags=0):
        ccs = self._get_class_instance('CIM_ControllerConfigurationService')
        lun = self._get_cim_instance_by_id('Volume', volume.id)

        #Need to check for existence of initiator, else create one
        initiator = self._initiator_lookup(initiator_id)

        in_params = {'LUNames': [lun['DeviceID']],
                     'DeviceAccesses': [pywbem.Uint16(2)]}

        if initiator is not None:
            #In this case we need to find the access group that contains the
            #initiator and then pass the
            group = self._get_group_initiator_is_in(initiator_id)

            if group is None:
                raise LsmError(ErrorNumber.UNSUPPORTED_PROVISIONING,
                               "Unsupported provisioning")

            in_params['ProtocolControllers'] = [group.path]
        else:
            in_params['InitiatorPortIDs'] = [initiator_id]

        #Returns None or job id
        return self._pi("initiator_grant", Smis.JOB_RETRIEVE_NONE,
                        *(self._c.InvokeMethod('ExposePaths', ccs.path,
                                               **in_params)))[0]

    @handle_cim_errors
    def initiator_revoke(self, initiator, volume, flags=0):
        (found, spc) = self._get_spc(initiator.id, volume.id)

        if found:
            ccs = self._get_class_instance(
                'CIM_ControllerConfigurationService')

            in_params = dict(ProtocolController=spc.path,
                             DeleteChildrenProtocolControllers=True,
                             DeleteUnits=True)

            #Returns None or job id
            return self._pi("access_revoke", Smis.JOB_RETRIEVE_NONE,
                            *(self._c.InvokeMethod('DeleteProtocolController',
                                                   ccs.path, **in_params)))[0]

    def _detach(self, vol, sync):

        #Get the Configuration service for the system we are interested in.
        scs = self._get_class_instance('CIM_StorageConfigurationService',
                                       'SystemName', vol.system_id)

        in_params = {'Operation': pywbem.Uint16(2),
                     'Synchronization': sync.path}

        job_id = self._pi("_detach", Smis.JOB_RETRIEVE_NONE,
                          *(self._c.InvokeMethod(
                              'ModifySynchronization', scs.path,
                              **in_params)))[0]

        self._poll("ModifySynchronization, detach", job_id)

    @handle_cim_errors
    def volume_delete(self, volume, flags=0):
        scs = self._get_class_instance('CIM_StorageConfigurationService',
                                       'SystemName', volume.system_id)
        lun = self._get_cim_instance_by_id('Volume', volume.id)

        #If we actually have an association to delete, the volume will be
        #deleted with the association, no need to call ReturnToStoragePool
        if not self._deal_volume_associations(volume, lun):
            in_params = {'TheElement': lun.path}

            #Delete returns None or Job number
            return self._pi("volume_delete", Smis.JOB_RETRIEVE_NONE,
                            *(self._c.InvokeMethod('ReturnToStoragePool',
                                                   scs.path, **in_params)))[0]

        #Loop to check to see if volume is actually gone yet!
        try:
            lun = self._get_cim_instance_by_id('Volume', volume.id)
            while lun is not None:
                lun = self._get_cim_instance_by_id('Volume', volume.id)
                time.sleep(0.125)
        except LsmError as e:
            pass
