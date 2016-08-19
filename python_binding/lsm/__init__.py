# Copyright (C) 2011-2016 Red Hat, Inc.
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
# License along with this library; If not, see <http://www.gnu.org/licenses/>.

import six

if six.PY3:
    from .version import VERSION

    from ._common import error, info, LsmError, ErrorNumber, \
        JobStatus, uri_parse, md5, Proxy, size_bytes_2_size_human, \
        common_urllib2_error_handler, size_human_2_size_bytes, int_div

    from ._local_disk import LocalDisk

    from ._data import (Disk, Volume, Pool, System, FileSystem, FsSnapshot,
                        NfsExport, BlockRange, AccessGroup, TargetPort,
                        Capabilities, Battery)
    from ._iplugin import IPlugin, IStorageAreaNetwork, \
        INetworkAttachedStorage, INfs

    from ._client import Client
    from ._pluginrunner import PluginRunner, search_property

else:
    from version import VERSION

    from _common import error, info, LsmError, ErrorNumber, \
        JobStatus, uri_parse, md5, Proxy, size_bytes_2_size_human, \
        common_urllib2_error_handler, size_human_2_size_bytes, int_div

    from _local_disk import LocalDisk

    from _data import (Disk, Volume, Pool, System, FileSystem, FsSnapshot,
                       NfsExport, BlockRange, AccessGroup, TargetPort,
                       Capabilities, Battery)
    from _iplugin import IPlugin, IStorageAreaNetwork, \
        INetworkAttachedStorage, INfs

    from _client import Client
    from _pluginrunner import PluginRunner, search_property

__all__ = []
