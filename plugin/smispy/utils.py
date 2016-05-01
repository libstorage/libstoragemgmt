# Copyright (C) 2014-2016 Red Hat, Inc.
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; If not, see <http://www.gnu.org/licenses/>.
#
# Author: Gris Ge <fge@redhat.com>

import traceback
from lsm import (LsmError, ErrorNumber, error)
from pywbem import (CIMError, CIMInstanceName)
import pywbem
import json


def merge_list(list_a, list_b):
    return list(set(list_a + list_b))


def handle_cim_errors(method):
    def cim_wrapper(*args, **kwargs):
        try:
            return method(*args, **kwargs)
        except LsmError as lsm:
            raise
        except CIMError as ce:
            error_code, desc = ce

            if error_code == 0:
                if 'Socket error' in desc:
                    if 'Errno 111' in desc:
                        raise LsmError(ErrorNumber.NETWORK_CONNREFUSED,
                                       'Connection refused')
                    if 'Errno 113' in desc:
                        raise LsmError(ErrorNumber.NETWORK_HOSTDOWN,
                                       'Host is down')
                    if 'Errno 104' in desc:
                        raise LsmError(ErrorNumber.NETWORK_CONNREFUSED,
                                       'Connection reset by peer')
                    # We know we have a socket error of some sort, lets
                    # report a generic network error with the string from the
                    # library.
                    raise LsmError(ErrorNumber.NETWORK_ERROR, str(ce))
                elif 'SSL error' in desc:
                    raise LsmError(ErrorNumber.TRANSPORT_COMMUNICATION,
                                   desc)
                elif 'The web server returned a bad status line':
                    raise LsmError(ErrorNumber.TRANSPORT_COMMUNICATION,
                                   desc)
                elif 'HTTP error' in desc:
                    raise LsmError(ErrorNumber.TRANSPORT_COMMUNICATION,
                                   desc)
            raise LsmError(ErrorNumber.PLUGIN_BUG, desc)
        except pywbem.cim_http.AuthError as ae:
            raise LsmError(ErrorNumber.PLUGIN_AUTH_FAILED, "Unauthorized user")
        except pywbem.cim_http.Error as te:
            raise LsmError(ErrorNumber.NETWORK_ERROR, str(te))
        except Exception as e:
            error("Unexpected exception:\n" + traceback.format_exc())
            raise LsmError(ErrorNumber.PLUGIN_BUG, str(e),
                           traceback.format_exc())
    return cim_wrapper


def hex_string_format(hex_str, length, every):
    hex_str = hex_str.lower()
    return ':'.join(hex_str[i:i + every] for i in range(0, length, every))


def cim_path_to_path_str(cim_path):
    """
    Convert CIMInstanceName to a string which could save in plugin_data
    """
    return json.dumps({
        'classname': cim_path.classname,
        'keybindings': dict(cim_path.keybindings),
        'host': cim_path.host,
        'namespace': cim_path.namespace,
    })


def path_str_to_cim_path(path_str):
    """
    Convert a string into CIMInstanceName.
    """
    path_dict = json.loads(path_str)
    return CIMInstanceName(**path_dict)
