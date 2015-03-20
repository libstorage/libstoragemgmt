# Copyright (C) 2015 Red Hat, Inc.
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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#
# Author: Gris Ge <fge@redhat.com>

from lsm import (
    IPlugin, Client, Capabilities, VERSION, LsmError, ErrorNumber)


class SmartArray(IPlugin):
    def plugin_register(self, uri, password, timeout, flags=Client.FLAG_RSVD):
        pass

    def plugin_unregister(self, flags=Client.FLAG_RSVD):
        pass

    def job_status(self, job_id, flags=Client.FLAG_RSVD):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")

    def job_free(self, job_id, flags=Client.FLAG_RSVD):
        pass

    def plugin_info(self, flags=Client.FLAG_RSVD):
        return "HP SmartArray Plugin", VERSION

    def time_out_set(self, ms, flags=Client.FLAG_RSVD):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")

    def time_out_get(self, flags=Client.FLAG_RSVD):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")

    def capabilities(self, system, flags=Client.FLAG_RSVD):
        return Capabilities()

    def systems(self, flags=Client.FLAG_RSVD):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")

    def pools(self, search_key=None, search_value=None,
              flags=Client.FLAG_RSVD):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")
