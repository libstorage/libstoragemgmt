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

#This functionality was gleaned from packet traces with a NetApp filer and
#should be considered experimental as I'm sure it is missing something :-)

import os
import pprint
import urllib2
import xml.etree.ElementTree as xml
import sys
from optparse import OptionParser

#Code for XmlListConfig and XmlDictConfig taken from recipe.
#http://code.activestate.com/recipes/410469-xml-as-dictionary/
#Modified slightly to remove namespace
#Licensed: PSF

#Set to an appropriate directory and file to dump the raw response.
xml_debug = None

def _ns(tag):
    return tag[tag.find('}')+1:]

class XmlListConfig(list):
    def __init__(self, aList):
        super(XmlListConfig, self).__init__()
        for element in aList:
            if element:
                if len(element) == 1 or element[0].tag != element[1].tag:
                    self.append(XmlDictConfig(element))
                elif element[0].tag == element[1].tag:
                    self.append(XmlListConfig(element))
            elif element.text:
                text = element.text.strip()
                if text:
                    self.append(text)

class XmlDictConfig(dict):
    def __init__(self, parent_element, **kwargs):
        super(XmlDictConfig, self).__init__(**kwargs)
        if parent_element.items():
            self.update(dict(parent_element.items()))
        for element in parent_element:
            if element:
                if len(element) == 1 or element[0].tag != element[1].tag:
                    aDict = XmlDictConfig(element)
                else:
                    aDict = {_ns(element[0].tag): XmlListConfig(element)}
                if element.items():
                    aDict.update(dict(element.items()))
                self.update({_ns(element.tag): aDict})
            elif element.items():
                self.update({_ns(element.tag): dict(element.items())})
            else:
                self.update({_ns(element.tag): element.text})

def netapp_filer_parse_response(resp):
    if xml_debug:
            out = open(xml_debug, "wb")
            out.write(resp)
            out.close()
    return XmlDictConfig(xml.fromstring(resp))

def netapp_filer(host, username, password, command, parameters = None, ssl=False):
    """
    Issue a command to the NetApp filer.
    Note: Change to default ssl on before we ship a release version.
    """
    proto = 'http'
    if ssl:
        proto = 'https'

    url = "%s://%s/servlets/netapp.servlets.admin.XMLrequest_filer" % (proto, host)
    req = urllib2.Request(url)
    req.add_header('Content-Type', 'text/xml')

    password_manager = urllib2.HTTPPasswordMgrWithDefaultRealm()
    password_manager.add_password(None, url, username, password)
    auth_manager = urllib2.HTTPBasicAuthHandler(password_manager)

    opener = urllib2.build_opener(auth_manager)
    urllib2.install_opener(opener)

    #build the command and the arguments for it
    p = ""

    if parameters:
        for k,v in parameters.items():
            p += "<%s>%s</%s>" % (k,v,k)

    payload = "<%s>\n%s\n</%s>" % (command,p,command)

    data = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE netapp SYSTEM "file:/etc/netapp_filer.dtd">
<netapp xmlns="http://www.netapp.com/filer/admin" version="1.1">
%s
</netapp>
""" % payload

    handler = urllib2.urlopen(req, data)

    if handler.getcode() == 200:
        rc = netapp_filer_parse_response(handler.read())
    else:
        raise RuntimeError("http post response= " + str(handler.getcode()))

    handler.close()

    return rc

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
                xml_debug = options.xmlfile

            result = netapp_filer(options.host, user, password, options.command,
                                    process_params(options.params), options.ssl)
            pp.pprint(result)
        else:
            parser.error("host and command are required")
    else:
        print 'Please create environmental variables for NA_USER and NA_PASSWORD'
        sys.exit(1)