#!/usr/bin/env python

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

import pprint
import sys
import lsm.na
import os
from optparse import OptionParser

def process_params(p):
    rc = {}

    if p is None:
        return p
    else:
        for e in p:
            if '==' in e:
                (name, value) = e.split('==')
                rc[name] = value
            else:
                print 'Param:', e , 'not in the form name==value'
                sys.exit(1)
    return rc

if __name__ == '__main__':
    pp = pprint.PrettyPrinter(indent=2)

    user = os.getenv('NA_USER')
    password = os.getenv('NA_PASSWORD')

    parser = OptionParser()
    parser.add_option(  "-t", "--host", action="store", type="string",
        dest="host", help="controller name or IP")
    parser.add_option(  "-c", "--command", action="store", type="string",
        dest="command", help="command to execute")
    parser.add_option("-p", "--param", action="append", type="string",
        dest="params", help="command parameters in the form name==value")
    parser.add_option("-s", "--ssl", action="store_true", dest="ssl",
        help="enable ssl" )
    parser.add_option('-d', "--dumpxml", action="store", dest="xmlfile",
        help="file to dump response to for debug")

    if user and password :
        (options, args) = parser.parse_args()
        if options.command and options.host:
            if options.xmlfile:
                lsm.na.xml_debug = options.xmlfile

            result = lsm.na.netapp_filer(options.host, user, password, options.command,
                process_params(options.params), options.ssl)
            pp.pprint(result)
        else:
            parser.error("host and command are required")
    else:
        print 'Please create environmental variables for NA_USER and NA_PASSWORD'
        sys.exit(1)