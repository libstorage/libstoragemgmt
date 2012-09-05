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
import hashlib

import os
import unittest
import urlparse

import sys
import syslog

# variable in client and specified on the command line for the daemon
UDS_PATH = '/var/run/lsm/ipc'

#Set to True for verbose logging
LOG_VERBOSE = True

## Constant for MiB
MiB = 1048576
## Constant for GiB
GiB = 1073741824
## Constant for TiB
TiB = 1099511627776

##Converts the size into human format.
# @param    size    Size in bytes
# @param    human   True|False
# @return Human representation of size
def sh(size, human=False):
    """
    Size for humans
    """
    units = None

    if human:
        if size >= TiB:
            size /= float(TiB)
            units = "TiB"
        elif size >= GiB:
            size /= float(GiB)
            units = "GiB"
        elif size >= MiB:
            size /= float(MiB)
            units = "MiB"

    if units:
        return "%.2f " % size + units
    else:
        return size

## Common method used to parse a URI.
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
    u = urlparse.urlparse(uri)

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
                raise LsmError(ErrorNumber.PLUGIN_ERROR, 'uri missing %s' % r)

    if required_params:
        for r in required_params:
            if r not in rc['parameters']:
                raise LsmError(ErrorNumber.PLUGIN_ERROR,
                                'uri missing query parameter %s' % r)
    return rc

## Parses the parameters (Query string) of the URI
# @param    uri     Full uri
# @returns  hash of the query string parameters.
def uri_parameters( uri ):
    url = urlparse.urlparse('http:' + uri[2])
    if len(url) >= 5 and len(url[4]):
        return dict([part.split('=') for part in url[4].split('&')])
    else:
        return {}

## Generates the md5 hex digest of passed in parameter.
# @param    t   Item to generate signature on.
# @returns  md5 hex digest.
def md5(t):
    h = hashlib.md5()
    h.update(t)
    return h.hexdigest()

## Converts a list of arguments to string.
# @param    args    Args to join
# @return string of arguments joined together.
def params_to_string(*args):
    return ''.join( [ str(e) for e in args] )

# Unfortunately the process name remains as 'python' so we are using argv[0] in
# the output to allow us to determine which python exe is indeed logging to
# syslog.
# TODO:  On newer versions of python this is no longer true, need to fix.

## Posts a message to the syslogger.
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

def Error(*msg):
    post_msg(syslog.LOG_ERR, os.path.basename(sys.argv[0]),
                params_to_string(*msg))

def Info(*msg):
    if LOG_VERBOSE:
        post_msg(syslog.LOG_INFO, os.path.basename(sys.argv[0]),
                    params_to_string(*msg))

class SocketEOF(Exception):
    """
    Exception class to indicate when we read zero bytes from a socket.
    """
    pass

class LsmError(Exception):

    def __init__(self, code, message, data=None, *args, **kwargs):
        """
        Class represents an error.
        """
        Exception.__init__(self, *args, **kwargs)
        self.code = code
        self.msg = message
        self.data = data

    def __str__(self):
        if self.data is not None:
            return "error: %s msg: %s data: %s" % (self.code, self.msg, self.data)
        else:
            return "error: %s msg: %s " % (self.code, self.msg)

def addl_error_data(domain, level, exception, debug = None, debug_data = None):
    """
    Used for gathering additional information about an error.
    """
    return {'domain': domain, 'level': level, 'exception': exception,
            'debug': debug, 'debug_data': debug_data}


def get_class( class_name ):
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


class ErrorLevel(object):
    NONE = 0
    WARNING = 1
    ERROR = 2

#Note: Some of these don't make sense for python, but they do for other
#Languages so we will be keeping them consistent even though we won't be
#using them.
class ErrorNumber(object):
    OK = 0
    INTERNAL_ERROR = 1
    JOB_STARTED = 7
    INDEX_BOUNDS = 10
    TIMEOUT = 11

    EXISTS_ACCESS_GROUP = 50
    EXISTS_FS = 51
    EXISTS_INITIATOR = 52
    EXISTS_NAME = 53
    FS_NOT_EXPORTED = 54
    INITIATOR_NOT_IN_ACCESS_GROUP = 55

    INVALID_ACCESS_GROUP = 100
    INVALID_ARGUMENT = 101
    INVALID_CONN = 102
    INVALID_ERR = 103
    INVALID_FS = 104
    INVALID_INIT = 105
    INVALID_JOB = 106
    INVALID_NAME = 107
    INVALID_NFS = 108
    INVALID_PLUGIN = 109
    INVALID_POOL = 110
    INVALID_SL = 111
    INVALID_SS = 112
    INVALID_URI = 113
    INVALID_VALUE = 114
    INVALID_VOLUME = 115
    INVALID_CAPABILITY = 116
    INVALID_SYSTEM = 117
    INVALID_IQN = 118

    IS_MAPPED = 125

    NO_CONNECT = 150
    NO_MAPPING = 151
    NO_MEMORY = 152
    NO_SUPPORT = 153

    NOT_FOUND_ACCESS_GROUP = 200
    NOT_FOUND_FS = 201
    NOT_FOUND_JOB = 202
    NOT_FOUND_POOL = 203
    NOT_FOUND_SS = 204
    NOT_FOUND_VOLUME = 205
    NOT_FOUND_NFS_EXPORT = 206
    NOT_FOUND_INITIATOR = 207

    NOT_IMPLEMENTED = 225
    NOT_LICENSED = 226

    OFF_LINE = 250
    ON_LINE = 251

    PLUGIN_AUTH_FAILED = 300
    PLUGIN_DLOPEN = 301
    PLUGIN_DLSYM = 302
    PLUGIN_ERROR = 303
    PLUGIN_MISSING_HOST = 304
    PLUGIN_MISSING_NS = 305
    PLUGIN_MISSING_PORT = 306
    PLUGIN_PERMISSION = 307
    PLUGIN_REGISTRATION = 308
    PLUGIN_UNKNOWN_HOST = 309
    PLUGIN_TIMEOUT = 310

    SIZE_INSUFFICIENT_SPACE = 350
    SIZE_SAME = 351
    SIZE_TOO_LARGE = 352
    SIZE_TOO_SMALL = 353
    SIZE_LIMIT_REACHED = 354

    TRANSPORT_COMMUNICATION = 400
    TRANSPORT_SERIALIZATION = 401
    TRANSPORT_INVALID_ARG = 402

    UNSUPPORTED_INITIATOR_TYPE = 450
    UNSUPPORTED_PROVISIONING = 451
    UNSUPPORTED_REPLICATION_TYPE = 452

class JobStatus(object):
    INPROGRESS = 1
    COMPLETE = 2
    STOPPED = 3
    ERROR = 4

class TestCommon(unittest.TestCase):
    def setUp(self):
        pass

    def test_simple(self):

        try:
            raise SocketEOF()
        except SocketEOF as e:
            self.assertTrue( isinstance(e,SocketEOF))

        try:
            raise LsmError(10, 'Message', 'Data')
        except LsmError as e:
            self.assertTrue(e.code == 10 and e.msg == 'Message'
                            and e.data == 'Data')

        ed = addl_error_data('domain', 'level', 'exception', 'debug',
                                'debug_data')
        self.assertTrue(ed['domain'] == 'domain' and ed['level'] == 'level'
                        and ed['debug'] == 'debug'
                        and ed['exception'] == 'exception'
                        and ed['debug_data'] == 'debug_data')

    def tearDown(self):
        pass

if __name__ == '__main__':
    unittest.main()
