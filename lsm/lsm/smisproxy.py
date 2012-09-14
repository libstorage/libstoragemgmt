# Copyright (C) 2012 Red Hat, Inc.
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

from lsm import Smis
import functools
import common

#The unfortunate truth is that each of the vendors implements functionality
#slightly differently so we will need to have some special code for these
#instances.

class SmisProxy(object):
    """
    Layer to allow us to swap out different implementations of smi-s clients
    based on provider.
    """

    def __init__(self):
        self.impl = None

    def startup(self, uri, password, timeout, flags=0):
        """
        We will provide a concrete implementation of this to get the process
        started.  All other method will be delegated to implementation.
        """

        #TODO Add code to interrogate the provider and then based on the type
        #we will instantiate the most appropriate implementation.  At the
        #moment we will drop back to our current mixed implementation.
        self.impl = Smis()
        self.impl.startup(uri, password, timeout, flags)

    def __getattr__(self, item):
        if hasattr(self.impl, item):
            return functools.partial(self.execute, item)
        else:
            raise common.LsmError(common.ErrorNumber.NO_SUPPORT,
                "Unsupported operation=" + item)

    def execute(self, name, *args, **kwargs):
        return getattr(self.impl, name)(*args, **kwargs)