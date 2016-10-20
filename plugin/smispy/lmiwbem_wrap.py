# Copyright (C) 2016 Red Hat, Inc.
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

# Try to make lmiwbem look like and toss errors like pywbem to prevent changing
# all kinds of code that depends on pywbem behavior

import lmiwbem as _lmiwbem
import functools
import six


class AuthError(Exception):
    pass


class Error(Exception):
    pass


def _error_handler(method):
    def _wrapper(*args, **kwargs):
        try:
            return method(*args, **kwargs)
        except _lmiwbem.ConnectionError as ce:
            # Try to raise errors which mimic pywbem
            if ce.args[0] == 401:
                raise AuthError()
            if ce.args[0] == 45:
                raise wbem.CIMError(
                    0, 'Socket error: [Errno 113] No route to host')
            raise

        except _lmiwbem.CIMError as ce:
            raise wbem.CIMError(ce)

    return _wrapper


class wbemType(type):

    def __setattr__(cls, name, value):
        object.__setattr__(_lmiwbem, name, value)

    def __getattr__(cls, name):
        if hasattr(_lmiwbem, name):
            return getattr(_lmiwbem, name)
        raise AttributeError(name)


@six.add_metaclass(wbemType)
class wbem(object):

    class Args(Exception):
        def __init__(self, exception_or_ec, msg=None):
            self.details = None
            self.exp = None

            if msg:
                self.details = (exception_or_ec, msg)
            else:
                self.exp = exception_or_ec

        def __getitem__(self, index):
            if self.exp:
                return self.exp.args[index]
            else:
                return self.details[index]

    class CIMError(Exception):
        def __init__(self, exception_or_ec, msg=None):
            self.args = wbem.Args(exception_or_ec, msg)

        def __str__(self):
            return '(%s: %s)' % (str(self.args[0]), str(self.args[1]))

    class WBEMConnection(object):

        @_error_handler
        def __init__(self, url, creds=None, default_namespace='root/cimv2',
                     x509=None, verify_callback=None, ca_certs=None,
                     no_verification=False, timeout=None):

            assert(verify_callback is None)
            assert(ca_certs is None)
            assert(timeout is None)

            connection = _lmiwbem.WBEMConnection(
                url, creds, default_namespace, x509, no_verification)

            # We need this syntax, if you do self._c you will explode!
            object.__setattr__(self, '_c', connection)

        def __setattr__(self, name, value):
            object.__setattr__(self._c, name, value)

        def __getattr__(self, name):
            if hasattr(self._c, name):
                if callable(getattr(self._c, name)):
                    return functools.partial(self._callit, name)
                return getattr(self._c, name)
            raise AttributeError(name)

        @_error_handler
        def _callit(self, _name, *args, **kwargs):
            return getattr(self._c, _name)(*args, **kwargs)
