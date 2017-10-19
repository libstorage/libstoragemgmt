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
# License along with this library; If not, see <http://www.gnu.org/licenses/>.
#
# Author: tasleson

import hashlib

import os
import unittest
import re

import sys
import syslog
import collections
import inspect

try:
    from urllib.error import (URLError, HTTPError)
    from urllib.parse import urlparse
except ImportError:
    from urllib2 import (URLError,
                         HTTPError)
    from urlparse import urlparse
import functools
import traceback
import six
import ssl
import socket


def default_property(name, allow_set=True, doc=None):
    """
    Creates the get/set properties for the given name.  It assumes that the
    actual attribute is '_' + name

    TODO: Expand this with domain validation to ensure the values are correct.
    """
    attribute_name = '_' + name

    def getter(self):
        return getattr(self, attribute_name)

    def setter(self, value):
        setattr(self, attribute_name, value)

    prop = property(getter, setter if allow_set else None, None, doc)

    def decorator(cls):
        setattr(cls, name, prop)
        return cls

    return decorator


def common_urllib2_error_handler(exp):

    if isinstance(exp, HTTPError):
        raise LsmError(ErrorNumber.PLUGIN_AUTH_FAILED, str(exp))
    if isinstance(exp, URLError):
        desc = str(exp)
        if 'urlopen error' in desc:
            if 'Errno 111' in desc:
                raise LsmError(ErrorNumber.NETWORK_CONNREFUSED,
                               'Connection refused')
            if 'Errno 113' in desc:
                raise LsmError(ErrorNumber.NETWORK_HOSTDOWN,
                               'Host is down')
        error("Unexpected network error:\n" + traceback.format_exc())
        raise LsmError(ErrorNumber.NETWORK_ERROR, desc)

    try:
        if ssl.CertificateError:
            if isinstance(exp, ssl.CertificateError):
                raise LsmError(ErrorNumber.NETWORK_ERROR,
                               "SSL Certificate error (%s)" % str(exp))
    except AttributeError:
        pass

    # Python3 is the gift that keeps on giving!
    if six.PY3 and isinstance(exp, ConnectionError):
        raise LsmError(ErrorNumber.NETWORK_CONNREFUSED, str(exp))

    if isinstance(exp, socket.error):
        raise LsmError(ErrorNumber.NETWORK_CONNREFUSED, str(exp))

    stack_trace = traceback.format_exc()
    error("Unexpected exception:\n" + stack_trace)
    raise LsmError(ErrorNumber.PLUGIN_BUG,
                   "Unexpected exception (TYPE= %s)" % str(type(exp)),
                   stack_trace)


# Documentation for Proxy class.
#
# Class to encapsulate the actual class we want to call.  When an attempt is
# made to access an attribute that doesn't exist we will raise an LsmError
# instead of the default keyError.
class Proxy(object):
    """
    Used to provide an unambiguous error when a feature is not implemented.
    """

    # The constructor.
    # @param    self    The object self
    # @param    obj     The object instance to wrap
    def __init__(self, obj=None):
        """
        Constructor which takes an object to wrap.
        """
        self.proxied_obj = obj

    # Called each time an attribute is requested of the object
    # @param    self    The object self
    # @param    name    Name of the attribute being accessed
    # @return   The result of the method
    def __getattr__(self, name):
        """
        Called each time an attribute is requested of the object
        """
        if hasattr(self.proxied_obj, name):
            return functools.partial(self._present, name)
        else:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "Unsupported operation")

    # Method which is called to invoke the actual method of interest.
    # @param    self                The object self
    # @param    _proxy_method_name  Method to invoke
    # @param    args                Arguments
    # @param    kwargs              Keyword arguments
    # @return   The result of the method invocation
    def _present(self, _proxy_method_name, *args, **kwargs):
        """
        Method which is called to invoke the actual method of interest.
        """
        return getattr(self.proxied_obj, _proxy_method_name)(*args, **kwargs)

# variable in client and specified on the command line for the daemon
UDS_PATH = '/var/run/lsm/ipc'

# Set to True for verbose logging
LOG_VERBOSE = True

# Constant for byte size
SIZE_CONS = {
    'B': 1,
    'KiB': 2 ** 10,
    'KB': 10 ** 3,
    'K': 2 ** 10,
    'k': 2 ** 10,
    'MiB': 2 ** 20,
    'MB': 10 ** 6,
    'M': 2 ** 20,
    'm': 2 ** 20,
    'GiB': 2 ** 30,
    'GB': 10 ** 9,
    'G': 2 ** 30,
    'g': 2 ** 30,
    'TiB': 2 ** 40,
    'TB': 10 ** 12,
    'T': 2 ** 40,
    't': 2 ** 40,
    'PiB': 2 ** 50,
    'PB': 10 ** 15,
    'P': 2 ** 50,
    'p': 2 ** 50,
    'EiB': 2 ** 60,
    'EB': 10 ** 18,
    'E': 2 ** 60,
    'e': 2 ** 60,
}
SIZE_CONS_CHK_LST = ['EiB', 'PiB', 'TiB', 'GiB', 'MiB', 'KiB']


# Converts the size into human format.
# @param    size    Size in bytes
# @param    human   True|False
# @return Human representation of size
def sh(size, human=False):
    """
    Convert size in bytes to human readable size
    The return string will follow IEC binary prefixes, e.g. '1.9 KiB'
    For size less than 1024, we do nothing but return the int we get.
    TODO: Need a expect to handle when size is not a int. int() might do.
    """
    units = None

    if human:
        for key_name in SIZE_CONS_CHK_LST:
            if size >= SIZE_CONS[key_name]:
                size /= float(SIZE_CONS[key_name])
                units = key_name
                break
        if not units:
            units = "B"
        return "%.2f %s" % (size, units)
    else:
        return size


# Converts the size into human format.
# @param  size    Size in bytes
# @return Human representation of size in IEC binary size prefixes.
def size_bytes_2_size_human(size):
    """
    Convert integer size in bytes to human readable size.
    We are following rules of IEC binary prefixes on size:
        http://en.wikipedia.org/wiki/Gibibyte
    The biggest of unit this function supported is PiB.
    The precision is 2 which means you will get '1.99 KiB'
    """
    return sh(size, True)


# Converts the size into human format.
# @param size_human Human readable size string, e.g. '1.9 KiB'
# @return Size in bytes
def size_human_2_size_bytes(size_human):
    """
    Convert human readable size string into integer size in bytes.
    Following rules of IEC binary prefixes on size:
        http://en.wikipedia.org/wiki/Gibibyte
    Supported input size_human in these formats:
        '1.9KiB'        # int(1024*1.9)
        '1 KiB'         # 2**10
        '1B'            # 1
        '2K'            # 2*(2**10), treated as '2KiB'
        '2k'            # 2*(2**10), treated as '2KiB'
        '2KB'           # 2*(10**3)
    """
    regex_size_human = re.compile(r"""
        ^
        ([0-9\.]+)          # 1: number
        [ \t]*              # might have space between number and unit
        ([a-zA-Z]*)         # 2: units
        $
    """, re.X)
    regex_match = regex_size_human.match(size_human)
    size_bytes = 0
    if regex_match:
        number = regex_match.group(1)
        units = regex_match.group(2)
        if not units:
            return int(number)
        units = units.upper()
        units = units.replace('IB', 'iB')
        if units in SIZE_CONS:
            size_bytes = SIZE_CONS[units] * float(number)
    return int(size_bytes)


# Common method used to parse a URI.
# @param    uri         The uri to parse
# @param    requires    Optional list of keys that must be present in output
# @param    required_params Optional list of required parameters that
#           must be present.
# @return   A hash of the parsed values.
def uri_parse(uri, requires=None, required_params=None):
    """
    Common uri parse method that optionally can check for what is needed
    before returning successfully.
    """

    rc = {}
    u = urlparse(uri)

    if u.scheme:
        rc['scheme'] = u.scheme

    if u.netloc:
        rc['netloc'] = u.netloc

    if u.port:
        rc['port'] = u.port

    if u.hostname:
        rc['host'] = u.hostname

    if u.username:
        rc['username'] = u.username
    else:
        rc['username'] = None

    rc['parameters'] = uri_parameters(u)

    if requires:
        for r in requires:
            if r not in rc:
                raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                               'uri missing \"%s\" or is in invalid form' % r)

    if required_params:
        for r in required_params:
            if r not in rc['parameters']:
                raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                               'uri missing query parameter %s' % r)
    return rc


# Parses the parameters (Query string) of the URI
# @param    uri     Full uri
# @returns  hash of the query string parameters.
def uri_parameters(uri):
    # workaround for python bug:
    # http://bugs.python.org/issue9374
    # for URL: smispy+ssl://admin@emc-smi:5989?namespace=root/emc
    # Before the patch commited( RHEL 6 and Fedora 18- ):
    #       '?namespace=root/emc' is saved in uri.path
    # After patched(RHEL 7 and Fedora 19+):
    #       'namespace=root/emc' is saved in uri.query
    if uri.query:
        query = uri.query
    elif uri.path:
        query = urlparse('http:' + uri[2]).query
    else:
        return {}
    if query:
        return dict([part.split('=') for part in query.split('&')])
    else:
        return {}


# Generates the md5 hex digest of passed in parameter.
# @param    t   Item to generate signature on.
# @returns  md5 hex digest.
def md5(t):
    h = hashlib.md5()
    h.update(t.encode("utf-8"))
    return h.hexdigest()


def int_div(a, b):
    # Trying to avoid using past.old_div as we don't have future on all
    # platforms we are trying to support
    if six.PY3:
        return a // b
    else:
        return a / b


# Converts a list of arguments to string.
# @param    args    Args to join
# @return string of arguments joined together.
def params_to_string(*args):
    return ''.join([str(e) for e in args])

# Unfortunately the process name remains as 'python' so we are using argv[0] in
# the output to allow us to determine which python exe is indeed logging to
# syslog.
# TODO:  On newer versions of python this is no longer true, need to fix.


# Posts a message to the syslogger.
# @param    level   Logging level
# @param    prg     Program name
# @param    msg     Message to log.
def post_msg(level, prg, msg):
    """
    If a message includes new lines we will create multiple syslog
    entries so that the message is readable.  Otherwise it isn't very readable.
    Hopefully we won't be logging much :-)
    """
    for l in msg.split('\n'):
        if len(l):
            syslog.syslog(level, prg + ": " + l)


def error(*msg):
    post_msg(syslog.LOG_ERR, os.path.basename(sys.argv[0]),
             params_to_string(*msg))


def info(*msg):
    if LOG_VERBOSE:
        post_msg(syslog.LOG_INFO, os.path.basename(sys.argv[0]),
                 params_to_string(*msg))


class SocketEOF(Exception):
    """
    Exception class to indicate when we read zero bytes from a socket.
    """
    pass


@default_property('code', doc='Error code')
@default_property('msg', doc='Error message')
@default_property('data', doc='Optional error data')
class LsmError(Exception):
    def __init__(self, code, message, data=None, *args, **kwargs):
        """
        Class represents an error.
        """
        Exception.__init__(self, *args, **kwargs)
        self._code = code
        self._msg = message
        self._data = data

    def __str__(self):
        error_no_str = ErrorNumber.error_number_to_str(self.code)
        if self.data is not None and self.data:
            return "%s: %s Data: %s" % \
                   (error_no_str, self.msg, self.data)
        else:
            return "%s: %s " % (error_no_str, self.msg)


def addl_error_data(domain, level, exception, debug=None, debug_data=None):
    """
    Used for gathering additional information about an error.
    """
    return {'domain': domain, 'level': level, 'exception': exception,
            'debug': debug, 'debug_data': debug_data}


def get_class(class_name):
    """
    Given a class name it returns the class, caller will then
    need to run the constructor to create.
    """
    parts = class_name.split('.')
    module = ".".join(parts[:-1])
    if len(module):
        m = __import__(module)
        for comp in parts[1:]:
            m = getattr(m, comp)
    else:
        m = __import__('__main__')
        m = getattr(m, class_name)
    return m


# Note: Some of these don't make sense for python, but they do for other
# Languages so we will be keeping them consistent even though we won't be
# using them.
class ErrorNumber(object):
    OK = 0
    LIB_BUG = 1
    PLUGIN_BUG = 2
    JOB_STARTED = 7
    TIMEOUT = 11
    DAEMON_NOT_RUNNING = 12
    PERMISSION_DENIED = 13

    NAME_CONFLICT = 50
    EXISTS_INITIATOR = 52

    INVALID_ARGUMENT = 101

    NO_STATE_CHANGE = 125

    NETWORK_CONNREFUSED = 140   # Host on network, but connection refused
    NETWORK_HOSTDOWN = 141      # Host unreachable on network
    NETWORK_ERROR = 142         # Generic network error

    NO_MEMORY = 152
    NO_SUPPORT = 153

    # Deletion related errors
    IS_MASKED = 160             # Volume is masked to access group.
    HAS_CHILD_DEPENDENCY = 161  # Volume/File system has child dependency.

    NOT_FOUND_ACCESS_GROUP = 200
    NOT_FOUND_FS = 201
    NOT_FOUND_JOB = 202
    NOT_FOUND_POOL = 203
    NOT_FOUND_FS_SS = 204
    NOT_FOUND_VOLUME = 205
    NOT_FOUND_NFS_EXPORT = 206
    NOT_FOUND_SYSTEM = 208
    NOT_FOUND_DISK = 209

    NOT_LICENSED = 226

    NO_SUPPORT_ONLINE_CHANGE = 250
    NO_SUPPORT_OFFLINE_CHANGE = 251

    PLUGIN_AUTH_FAILED = 300    # Client supplied credential are incorrect

    # Inter-process communication between client & out of process plug-in
    # encountered connection errors
    PLUGIN_IPC_FAIL = 301

    # Incorrect permission on UNIX domain socket used for IPC
    PLUGIN_SOCKET_PERMISSION = 307

    PLUGIN_NOT_EXIST = 311

    NOT_ENOUGH_SPACE = 350

    TRANSPORT_COMMUNICATION = 400
    TRANSPORT_SERIALIZATION = 401
    TRANSPORT_INVALID_ARG = 402

    # refuse to remove the last initiator from access group
    LAST_INIT_IN_ACCESS_GROUP = 502

    UNSUPPORTED_SEARCH_KEY = 510

    # volume_mask() will fail if access group has no member/initiator.
    EMPTY_ACCESS_GROUP = 511

    POOL_NOT_READY = 512    # Pool is not ready for create/resize/etc

    DISK_NOT_FREE = 513     # Disk is not in DISK.STATUS_FREE status.

    _LOCALS = locals()

    @staticmethod
    def error_number_to_str(error_no):
        for error_str in list(ErrorNumber._LOCALS.keys()):
            if ErrorNumber._LOCALS[error_str] == error_no:
                return "%s(%d)" % (error_str, error_no)
        return "UNKNOWN_ERROR_NUMBER(%d)" % error_no


class JobStatus(object):
    INPROGRESS = 1
    COMPLETE = 2
    ERROR = 3


def type_compare(method_name, exp_type, act_val):
    if isinstance(exp_type, collections.Sequence):
        if not isinstance(act_val, collections.Sequence):
            raise TypeError("%s call is returning a %s, but is "
                            "expecting a sequence" %
                            (method_name, str(type(act_val))))
        # If the list has only one expected value we will make sure all
        # elements in the list adhere to it, otherwise we will enforce a one
        # to one check against the expected types.
        if len(exp_type) == 1:
            for av in act_val:
                type_compare(method_name, exp_type[0], av)
        else:
            # Expect a 1-1 type match, extras get ignored at the moment
            for exp, act in zip(exp_type, act_val):
                type_compare(method_name, exp, act)
    else:
        # A number of times a method will return None or some valid type,
        # only check on the type if the value is not None
        if exp_type != type(act_val) and act_val is not None:
            if (isinstance(exp_type, six.string_types) and
                    isinstance(act_val, six.string_types)):
                return
            if not inspect.isclass(exp_type) or \
                    not issubclass(type(act_val), exp_type):
                raise TypeError('%s call expected: %s got: %s ' %
                                (method_name, str(exp_type),
                                 str(type(act_val))))


def return_requires(*types):
    """
    Decorator function that allows us to ensure that we are getting the
    correct types back from a function/method call.

    Note: This is normally frowned upon by the python community, but this API
    needs to be language agnostic, so making sure we have the correct types
    is quite important.
    """
    def outer(func):
        @functools.wraps(func)
        def inner(*args, **kwargs):
            r = func(*args, **kwargs)

            # In this case the user did something like
            # @return_requires(int, string, int)
            # in this case we require that all the args are present.
            if len(types) > 1:
                if len(r) != len(types):
                        raise TypeError("%s call expected %d "
                                        "return values, actual = %d" %
                                        (func.__name__, len(types), len(r)))

                type_compare(func.__name__, types, r)
            elif len(types) == 1:
                # We have one return type (but it could be a sequence)
                type_compare(func.__name__, types[0], r)

            return r
        return inner
    return outer


class TestCommon(unittest.TestCase):
    def setUp(self):
        pass

    def test_simple(self):

        try:
            raise SocketEOF()
        except SocketEOF as e:
            self.assertTrue(isinstance(e, SocketEOF))

        try:
            raise LsmError(10, 'Message', 'Data')
        except LsmError as e:
            self.assertTrue(e.code == 10 and e.msg == 'Message' and
                            e.data == 'Data')

        ed = addl_error_data('domain', 'level', 'exception', 'debug',
                             'debug_data')
        self.assertTrue(ed['domain'] == 'domain' and ed['level'] == 'level' and
                        ed['debug'] == 'debug' and
                        ed['exception'] == 'exception' and
                        ed['debug_data'] == 'debug_data')

    def tearDown(self):
        pass


if __name__ == '__main__':
    unittest.main()
