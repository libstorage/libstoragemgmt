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
from string import split
import pywbem
import time
from lsm.plugin.smispy.smis import handle_cim_errors
from lsm import LsmError, ErrorNumber, uri_parse


class ESeries(Smis):
    def plugin_register(self, uri, password, timeout, flags=0):
        """
        The only difference we did here compare to supper method:
            we force to be fallback mode.
        NetApp-E support 'Masking and Mapping' profile but not expose it
        via CIM_RegisteredProfile.
        """
        protocol = 'http'
        port = Smis.IAAN_WBEM_HTTP_PORT
        u = uri_parse(uri, ['scheme', 'netloc', 'host'], None)

        if u['scheme'].lower() == 'smispy+ssl':
            protocol = 'https'
            port = Smis.IAAN_WBEM_HTTPS_PORT

        if 'port' in u:
            port = u['port']

        # smisproxy.py already make sure namespace defined.
        namespace = u['parameters']['namespace']
        self.all_vendor_namespaces = [namespace]

        url = "%s://%s:%s" % (protocol, u['host'], port)
        self.system_list = None
        if 'systems' in u['parameters']:
            self.system_list = split(u['parameters']["systems"], ":")

        self._c = pywbem.WBEMConnection(url, (u['username'], password),
                                        namespace)
        if "no_ssl_verify" in u["parameters"] \
           and u["parameters"]["no_ssl_verify"] == 'yes':
            try:
                self._c = pywbem.WBEMConnection(
                    url,
                    (u['username'], password),
                    namespace,
                    no_verification=True)
            except TypeError:
                # pywbem is not holding fix from
                # https://bugzilla.redhat.com/show_bug.cgi?id=1039801
                pass

        self.tmo = timeout
        self.fallback_mode = True

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
    def access_group_delete(self, group, flags=0):
        ccs = self._get_class_instance('CIM_ControllerConfigurationService')

        pc = self._get_cim_instance_by_id('AccessGroup', group.id)

        in_params = {'ProtocolController': pc.path}

        return self._pi("access_group_delete", Smis.JOB_RETRIEVE_NONE,
                        *(self._c.InvokeMethod('DeleteProtocolController',
                                               ccs.path, **in_params)))[0]

    @handle_cim_errors
    def capabilities(self, system, flags=0):
        cap = self._common_capabilities(system)

        #TODO We need to investigate why our interrogation code doesn't work.
        #The array is telling us one thing, but when we try to use it, it
        #doesn't work
        return cap

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

    @handle_cim_errors
    def access_group_initiator_delete(self, access_group, init_id, flags=0):
        """
        When using HidePaths to remove initiator, the whole SPC will be
        removed. Before we find a workaround for this, I would like to have
        this method disabled as NO_SUPPORT.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT,
                       "SMI-S plugin does not support "
                       "access_group_initiator_delete() against NetApp-E")
