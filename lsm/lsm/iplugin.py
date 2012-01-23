#!/usr/bin/env python

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

from abc import ABCMeta, abstractmethod

# ** Important** See Client class documentation.
class IPlugin(object):
    """
    Plug-in interface that all plug-ins must implement for basic
    operation.
    """
    __metaclass__ = ABCMeta

    @abstractmethod
    def startup(self, uri, password, timeout):
        """
        Method first called to setup the plug-in

        Return None on success, else LsmError exception
        """
        pass

    @abstractmethod
    def set_time_out(self, ms):
        """
        Sets any time-outs for the plug-in (ms)

        Return None on success, else LsmError exception
        """
        pass

    @abstractmethod
    def get_time_out(self):
        """
        Retrieves the current time-out

        Return time-out in ms, else raise LsmError
        """
        pass

    @abstractmethod
    def shutdown(self):
        """
        Called when the client wants to finish up or the socket goes eof.
        Plug-in should clean up all resources.  Note: In the case where
        the socket goes EOF and the shutdown runs into errors the exception(s)
        will not be delivered to the client!

        Return None on success, else LsmError exception
        """
        pass

    @abstractmethod
    def job_status(self, job_number):
        """
        Returns the stats of the given job number.

        Returns a tuple ( status (enumeration), percent_complete, volume).
        else LsmError exception.
        """
        pass

    @abstractmethod
    def job_free(self, job_number):
        """
        Frees resources for a given job number.

        Returns None on success, else raises an LsmError
        """
        pass

    @abstractmethod
    def volumes(self):
        """
        Returns an array of volume objects

        """
        pass

    @abstractmethod
    def pools(self):
        """
        Returns an array of pool objects
        """
        pass

    @abstractmethod
    def initiators(self):
        """
        Return an array of initiator objects
        """
        pass

    @abstractmethod
    def volume_create(self, pool, volume_name, size_bytes, provisioning):
        """
        Creates a volume, given a pool, volume name, size and provisioning

        returns a tuple (job_id, new volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        pass

    @abstractmethod
    def volume_delete(self, volume):
        """
        Deletes a volume.

        Returns None on success, else raises an LsmError
        """
        pass

    @abstractmethod
    def volume_resize(self, volume, new_size_bytes):
        """
        Re-sizes a volume.

        Returns a tuple (job_id, re-sized_volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """

        pass

    @abstractmethod
    def volume_replicate(self, pool, rep_type, vol_src, name):
        """
        Replicates a volume from the specified pool.

        Returns a tuple (job_id, replicated volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        pass

    @abstractmethod
    def volume_online(self, volume):
        """
        Makes a volume available to the host

        returns None on success, else raises LsmError on errors.
        """
        pass

    @abstractmethod
    def volume_offline(self, volume):
        """
        Makes a volume unavailable to the host

        returns None on success, else raises LsmError on errors.
        """
        pass

    @abstractmethod
    def initiator_create(self, name, id, id_type):
        """
        Creates an initiator to be used for granting access to volumes.

        Returns an initiator object, else raises an LsmError
        """
        pass

    @abstractmethod
    def access_grant(self, initiator, volume, access):
        """
        Access control for allowing an initiator to use a volume.

        Returns None on success else job id if in progress.
        Raises LsmError on errors.
        """

        pass

    @abstractmethod
    def access_revoke(self, initiator, volume):
        """
        Revokes privileges an initiator has to a volume

        Returns none on success, else raises a LsmError on error.
        """
        pass
