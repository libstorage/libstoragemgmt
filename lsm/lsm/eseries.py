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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
#
# Author: tasleson

from smis import Smis
import pywbem
#from pywbem import CIMError
import common
import time


class ESeries(Smis):

    def _check_volume_associations(self, vol):
        """
        Check a volume to see if it has any associations with other
        volumes.
        """
        rc = False
        lun = self._get_volume(vol.id)

        ss = self._c.References(lun.path,
                                ResultClass='CIM_StorageSynchronized')

        lp = lun.path

        if len(ss):
            for s in ss:
                if 'SyncedElement' in s:
                    item = s['SyncedElement']

                    if self._cim_name_match(item, lp):
                        self._detach(vol, s)
                        rc = True

                if 'SystemElement' in s:
                    item = s['SystemElement']

                    if self._cim_name_match(item, lp):
                        self._detach(vol, s)
                        rc = True

        return rc

    def _detach(self, vol, sync):

        #Get the Configuration service for the system we are interested in.
        scs = self._get_class_instance('CIM_StorageConfigurationService',
                                       'SystemName', vol.system_id)

        in_params = {'Operation': pywbem.Uint16(2),
                     'Synchronization': sync.path}

        job_id = self._pi("_detach", False,
                          *(self._c.InvokeMethod(
                              'ModifySynchronization', scs.path,
                              **in_params)))[0]

        self._poll("ModifySynchronization, detach", job_id)

    def volume_delete(self, volume, flags=0):
        scs = self._get_class_instance('CIM_StorageConfigurationService',
                                       'SystemName', volume.system_id)
        lun = self._get_volume(volume.id)

        #If we actually have an association to delete, the volume will be deleted
        #with the association, no need to call ReturnToStoragePool
        if not self._check_volume_associations(volume):
            in_params = {'TheElement': lun.path}

            #Delete returns None or Job number
            return self._pi("volume_delete", False,
                            *(self._c.InvokeMethod('ReturnToStoragePool',
                                                   scs.path, **in_params)))[0]

        #Loop to check to see if volume is actually gone yet!
        try:
            lun = self._get_volume(volume.id)
            while lun is not None:
                lun = self._get_volume(volume.id)
                time.sleep(0.125)
        except common.LsmError as e:
            pass