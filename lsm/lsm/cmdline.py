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
import functools
from optparse import OptionParser, OptionGroup
import optparse
import os
import re
import string
import textwrap
import sys
import getpass
import time

import common
import client
import data
from version import VERSION
from data import Capabilities

##@package lsm.cmdline


## Documentation for Wrapper class.
#
# Class to encapsulate the actual class we want to call.  When an attempt is
# made to access an attribute that doesn't exist we will raise an LsmError
# instead of the default keyError.
class Wrapper(object):
    """
    Used to provide an unambiguous error when a feature is not implemented.
    """

    ## The constructor.
    # @param    self    The object self
    # @param    obj     The object instance to wrap
    def __init__(self, obj):
        """
        Constructor which takes an object to wrap.
        """
        self.wrapper = obj

    ## Called each time an attribute is requested of the object
    # @param    self    The object self
    # @param    name    Name of the attribute being accessed
    # @return   The result of the method
    def __getattr__(self, name):
        """
        Called each time an attribute is requested of the object
        """
        if hasattr(self.wrapper, name):
            return functools.partial(self.present, name)
        else:
            raise common.LsmError(common.ErrorNumber.NO_SUPPORT,
                                    "Unsupported operation")

    ## Method which is called to invoke the actual method of interest.
    # @param    self    The object self
    # @param    name    Method to invoke
    # @param    args    Arguments
    # @param    kwargs  Keyword arguments
    # @return   The result of the method invocation
    def present(self, name, *args, **kwargs):
        """
        Method which is called to invoke the actual method of interest.
        """
        return getattr(self.wrapper, name)(*args, **kwargs)

## Wraps the invocation to the command line
# @param    client  Object to invoke calls on (optional)
def cmd_line_wrapper(client = None):
    """
    Common command line code, called.
    """
    try:
        cli = CmdLine()
        cli.process(client)
    except ArgError as ae:
        sys.stderr.write(str(ae))
        sys.exit(2)
    except common.LsmError as le:
        sys.stderr.write(str(le) + "\n")
        sys.exit(4)

## Simple class used to handle \n in optparse output
class MyWrapper:
    """
    Handle \n in text for the command line help etc.
    """
    @staticmethod
    def wrap(text, width=70, **kw):
        rc = []
        for line in text.split("\n"):
            rc.extend(textwrap.wrap(line, width, **kw))
        return rc

    @staticmethod
    def fill(text, width=70, **kw):
        rc = []
        for line in text.split("\n"):
            rc.append(textwrap.fill(line, width, **kw))
        return "\n".join(rc)

## This class represents a command line argument error
class ArgError(Exception):
    def __init__(self, message, *args, **kwargs):
        """
        Class represents an error.
        """
        Exception.__init__(self, *args, **kwargs)
        self.msg = message
    def __str__(self):
        return "%s: error: %s\n" % ( os.path.basename(sys.argv[0]), self.msg)

## Prefixes cmd with "cmd_"
# @param    cmd     The command to prefix with cmd_"
# @return   The cmd string prefixed with "cmd_"
def _c(cmd):
    return "cmd_" + cmd

## Prefixes option with "opt_"
# @param    option  The option to prefix with "opt_"
# @return   The option string prefixed with "opt_"
def _o(option):
    return "opt_" + option

## Class that encapsulates the command line arguments for lsmcli
# Note: This class is used by lsmcli and any python plug-ins.
class CmdLine:
    """
    Command line interface class.
    """

    ##
    # Tries to make the output better when it varies considerably from plug-in to
    # plug-in.
    # @param    rows    Data, first row is header all other data.
    def display_table(self, rows):
        """
        Creates a nicer text dump of tabular data.  First row should be the column
        headers.
        """
        #If any of the table cells is another list, lets flatten using the sep
        for i in range(len(rows)):
            for j in range(len(rows[i])):
                if isinstance(rows[i][j], list):
                    rows[i][j] = self._list(rows[i][j])

        if self.options.sep is not None:
            s = self.options.sep

            #See if we want to display the header or not!
            start = 1
            if self.options.header:
                start = 0

            for i in range(start, len(rows)):
                print s.join([ str(x) for x in rows[i] ])

        else:
            if len(rows) >=2 :
                #Get the max length of each column
                lens = []
                for l in zip(*rows):
                    lens.append(max(len(str(x)) for x in l))
                data_formats = []
                header_formats = []

                #Build the needed format
                for i in range(len(rows[0])):
                    header_formats.append("%%-%ds" % lens[i])

                    #If the row contains numerical data we will right justify.
                    if isinstance(rows[1][i], int):
                        data_formats.append("%%%dd" % lens[i])
                    else:
                        data_formats.append("%%-%ds" % lens[i])

                #Print the header, header separator and then row data.
                header_pattern = " | ".join(header_formats)
                print header_pattern % tuple(rows[0])
                print "-+-".join(['-' * n for n in lens])
                data_pattern = " | ".join(data_formats)

                for i in range(1,len(rows)):
                    print data_pattern % tuple(rows[i])

    def display_data(self, d):

        if d and len(d):

            rows = d[0].column_headers()

            for r in d:
                rows.extend(r.column_data(self.options.human))

            self.display_table(rows)

    ## All the command line arguments and options are created in this method
    # @param    self    The this object pointer
    def cli(self):
        """
        Command line interface parameters
        """
        usage = "usage: %prog [options]... [command]... [command options]..."
        optparse.textwrap = MyWrapper
        parser = OptionParser(usage=usage, version="%prog " + VERSION )
        parser.description = ('libStorageMgmt command line interface. \n')

        parser.epilog = ( 'Copyright 2012 Red Hat, Inc.\n'
                          'Please report bugs to <libstoragemgmt-devel@lists.sourceforge.net>\n')

        parser.add_option( '-u', '--uri', action="store", type="string", dest="uri",
            help='uniform resource identifier (env LSMCLI_URI)')
        parser.add_option( '-P', '--prompt', action="store_true", dest="prompt",
            help='prompt for password (env LSMCLI_PASSWORD)')
        parser.add_option( '-H', '--human', action="store_true", dest="human",
            help='print sizes in human readable format\n'
                 '(e.g., MiB, GiB, TiB)')
        parser.add_option( '-t', '--terse', action="store", dest="sep",
            help='print output in terse form with "SEP" as a record separator')

        parser.add_option( '-w', '--wait', action="store", type="int",
            dest="wait", default= 30000,
            help="command timeout value in ms (default = 30s)")

        parser.add_option( '', '--header', action="store_true", dest="header",
            help='include the header with terse')

        parser.add_option( '-b', '', action="store_true", dest="async", default=False,
            help='run the command async. instead of waiting for completion\n'
                 'command will exit(7) and job id written to stdout.')

        #What action we want to take
        commands = OptionGroup(parser, 'Commands')

        list_choices = ['VOLUMES', 'INITIATORS', 'POOLS', 'FS', 'SNAPSHOTS',
                        'EXPORTS', "NFS_CLIENT_AUTH", 'ACCESS_GROUPS',
                        'SYSTEMS']

        commands.add_option('-l', '--list', action="store", type="choice",
            dest="cmd_list",
            #metavar='<'+ ",".join(list_choices) + '>',
            metavar='<type>',
            choices = list_choices,
            help='List records of type: ' + ",".join(list_choices) + '\n'
                 'Note: SNAPSHOTS requires --fs <fs id>')

        commands.add_option( '', '--capabilities', action="store", type="string",
            dest=_c("capabilities"),
            metavar='<system id>',
            help='Retrieves array capabilities')

        commands.add_option( '', '--delete-fs', action="store", type="string",
            dest=_c("delete-fs"),
            metavar='<fs id>',
            help='Delete a filesystem')

        commands.add_option( '', '--delete-access-group', action="store", type="string",
            dest=_c("delete-access-group"),
            metavar='<group id>',
            help='Deletes an access group')

        commands.add_option( '', '--access-group-add', action="store", type="string",
            dest=_c("access-group-add"),
            metavar='<access group id>',
            help='Adds an initiator to an access group, requires:\n'
                '--id <initiator id\n'
                '--type <initiator type>' )

        commands.add_option( '', '--access-group-remove', action="store", type="string",
            dest=_c("access-group-remove"),
            metavar='<access group id>',
            help='Removes an initiator from an access group, requires:\n'
                 '--id <initiator id>')

        commands.add_option( '', '--create-volume', action="store", type="string",
            dest=_c("create-volume"),
            metavar='<volume name>',
            help="Creates a volume (logical unit) requires:\n"
                 "--size <volume size> (Can use M, G, T)\n"
                 "--pool <pool id>\n"
                 "--provisioning (optional) [DEFAULT|THIN|FULL]\n")

        commands.add_option( '', '--create-fs', action="store", type="string",
            dest=_c("create-fs"),
            metavar='<fs name>',
            help="Creates a file system requires:\n"
                 "--size <fs size> (Can use M, G, T)\n"
                 "--pool <pool id>")

        commands.add_option( '', '--create-ss', action="store", type="string",
            dest=_c("create-ss"),
            metavar='<snapshot name>',
            help="Creates a snapshot, requires:\n"
                 "--file <repeat for each file>(default is all files)\n"
                 "--fs <file system id>")

        commands.add_option('', '--create-access-group', action="store", type="string",
            dest=_c("create-access-group"),
            metavar='<Access group name>',
            help="Creates an access group, requires:\n"
                 "--id <initiator id>\n"
                 '--type [WWPN|WWNN|ISCSI|HOSTNAME]\n'
                 '--system <system id>')

        commands.add_option('', '--access-group-volumes', action="store", type="string",
            dest=_c("access-group-volumes"),
            metavar='<access group id>',
            help='Lists the volumes that the access group has been granted access to')

        commands.add_option('', '--volume-access-group', action="store", type="string",
            dest=_c("volume-access-group"),
            metavar='<volume id>',
            help='Lists the access group(s) that have access to volume')

        commands.add_option('', '--volumes-accessible-initiator', action="store", type="string",
            dest=_c("volumes-accessible-initiator"),
            metavar='<initiator id>',
            help='Lists the volumes that are accessible by the initiator')

        commands.add_option('', '--initiators-granted-volume', action="store", type="string",
            dest=_c("initiators-granted-volume"),
            metavar='<volume id>',
            help='Lists the initiators that have been granted access to specified volume')

        commands.add_option( '', '--restore-ss', action="store", type="string",
            dest=_c("restore-ss"),
            metavar='<snapshot id>',
            help="Restores a FS or specified files to previous snapshot state, requires:\n"
                 "--fs <file system>\n"
                 "--file <repeat for each file (optional)>\n"
                 "--fileas <restore file name (optional)>\n"
                 "--all (optional, exclusive option, restores all files in snapshot other options must be absent)")

        commands.add_option( '', '--clone-fs', action="store", type="string",
            dest=_c("clone-fs"),
            metavar='<source file system id>',
            help="Creates a file system clone requires:\n"
                 "--name <file system clone name>\n"
                 "--backing-snapshot <backing snapshot id> (optional)")

        commands.add_option( '', '--clone-file', action="store", type="string",
            dest=_c("clone-file"),
            metavar='<file system>',
            help="Creates a clone of a file (thin provisioned):\n"
                 "--src  <source file to clone (relative path)>\n"
                 "--dest <destination file (relative path)>\n"
                 "--backing-snapshot <backing snapshot id> (optional)")

        commands.add_option( '', '--delete-volume', action="store", type="string",
            metavar='<volume id>',
            dest=_c("delete-volume"), help='Deletes a volume given its id' )

        commands.add_option( '', '--delete-ss', action="store", type="string",
            metavar='<snapshot id>',
            dest=_c("delete-ss"), help='Deletes a snapshot requires --fs' )

        commands.add_option( '-r', '--replicate-volume', action="store", type="string",
            metavar='<volume id>',
            dest=_c("replicate-volume"), help='replicates a volume, requires:\n'
                                              "--type [SNAPSHOT|CLONE|COPY|MIRROR_ASYNC|MIRROR_SYNC]\n"
                                              "--pool <pool id>\n"
                                              "--name <human name>")

        commands.add_option( '', '--replicate-volume-range', action="store", type="string",
            metavar='<volume id>',
            dest=_c("replicate-volume-range"), help='replicates a portion of a volume, requires:\n'
                                              "--type [SNAPSHOT|CLONE|COPY|MIRROR]\n"
                                              "--dest <destination volume>\n"
                                              "--src_start <source block start number>\n"
                                              "--dest_start <destination block start>\n"
                                              "--count <number of blocks to replicate>")

        commands.add_option( '', '--iscsi-chap', action="store", type="string",
            metavar='<initiator id>',
            dest=_c("iscsi-chap"), help='configures ISCSI inbound CHAP authentication\n'
                                          'requires:\n'
                                          '--username <chap user name>\n'
                                          '--password <chap password>')

        commands.add_option( '', '--access-grant', action="store", type="string",
            metavar='<initiator id>',
            dest=_c("access-grant"), help='grants access to an initiator to a volume\n'
                                          'requires:\n'
                                          '--type <initiator id type>\n'
                                          '--volume <volume id>\n'
                                          '--access [RO|RW], read-only or read-write')

        commands.add_option( '', '--access-grant-group', action="store", type="string",
            metavar='<access group id>',
            dest=_c("access-grant-group"), help='grants access to an access group to a volume\n'
                                          'requires:\n'
                                          '--volume <volume id>\n'
                                          '--access [RO|RW], read-only or read-write')

        commands.add_option( '', '--access-revoke', action="store", type="string",
            metavar='<initiator id>',
            dest=_c("access-revoke"), help= 'removes access for an initiator to a volume\n'
                                            'requires:\n'
                                            '--volume <volume id>')

        commands.add_option( '', '--access-revoke-group', action="store", type="string",
            metavar='<access group id>',
            dest=_c("access-revoke-group"), help= 'removes access for access group to a volume\n'
                                            'requires:\n'
                                            '--volume <volume id>')

        commands.add_option( '', '--resize-volume', action="store", type="string",
            metavar='<volume id>',
            dest=_c("resize-volume"), help= 're-sizes a volume, requires:\n'
                                            '--size <new size>')

        commands.add_option( '', '--resize-fs', action="store", type="string",
            metavar='<fs id>',
            dest=_c("resize-fs"), help= 're-sizes a file system, requires:\n'
                                        '--size <new size>')

        commands.add_option( '', '--nfs-export-remove', action="store", type="string",
            metavar='<nfs export id>',
            dest=_c("nfs-export-remove"), help= 'removes a nfs export')

        commands.add_option( '', '--nfs-export-fs', action="store", type="string",
            metavar='<file system id>',
            dest=_c("nfs-export-fs"), help= 'creates a nfs export\n'
                                            'Required:\n'
                                            '--exportpath e.g. /foo/bar\n'
                                            'Optional:\n'
                                            'Note: root, ro, rw are to be repeated for each host\n'
                                            '--root <no_root_squash host>\n'
                                            '--ro <read only host>\n'
                                            '--rw <read/write host>\n'
                                            '--anonuid <uid to map to anonymous>\n'
                                            '--anongid <gid to map to anonymous>\n'
                                            '--auth-type <NFS client authentication type>\n'
        )

        commands.add_option( '', '--job-status', action="store", type="string",
            metavar='<job status id>',
            dest=_c("job-status"), help= 'retrieve information about job')

        commands.add_option( '', '--volume-dependants', action="store", type="string",
            metavar='<volume id>',
            dest=_c("volume-dependants"), help= 'Returns True if volume has a dependant child')

        commands.add_option( '', '--volume-dependants-rm', action="store", type="string",
            metavar='<volume id>',
            dest=_c("volume-dependants-rm"), help= 'Removes dependencies')

        commands.add_option( '', '--fs-dependants', action="store", type="string",
            metavar='<fs id>',
            dest=_c("fs-dependants"), help= 'Returns true if a child dependency exists.\n'
                                        'Optional:\n'
                                        '--file <file> for File check' )

        commands.add_option( '', '--fs-dependants-rm', action="store", type="string",
            metavar='<fs id>',
            dest=_c("fs-dependants-rm"), help=  'Removes dependencies\n'
                                                'Optional:\n'
                                                '--file <file> for File check' )

        parser.add_option_group(commands)

        #Options to the actions
        #We could hide these with help = optparse.SUPPRESS_HELP
        #Should we?
        command_args = OptionGroup(parser, 'Command options')
        command_args.add_option('', '--size', action="store", type="string",
            metavar='size',
            dest=_o("size"), help='size (Can use M, G, T postfix)')
        command_args.add_option('', '--pool', action="store", type="string",
            metavar='pool id',
            dest=_o("pool"), help='pool ID')
        command_args.add_option('', '--provisioning', action="store", type="choice",
            default = 'DEFAULT',
            choices=['DEFAULT','THIN','FULL'], dest="provisioning", help='[DEFAULT|THIN|FULL]')

        command_args.add_option('', '--type', action="store", type="choice",
            choices=['WWPN', 'WWNN', 'ISCSI', 'HOSTNAME', 'SNAPSHOT', 'CLONE', 'COPY', 'MIRROR_SYNC', 'MIRROR_ASYNC'],
            metavar = "type",
            dest=_o("type"), help='type specifier')

        command_args.add_option('', '--name', action="store", type="string",
            metavar = "name",
            dest=_o("name"),
            help='human readable name')

        command_args.add_option('', '--volume', action="store", type="string",
            metavar = "volume",
            dest=_o("volume"), help='volume ID')

        command_args.add_option('', '--access', action="store", type="choice",
            metavar = "access",
            dest=_o("access"), choices=['RO', 'RW'] ,help='[RO|RW], read-only or read-write access')

        command_args.add_option('', '--id', action="store", type="string",
            metavar = "initiator id",
            dest=_o("id"), help="initiator id")

        command_args.add_option('', '--system', action="store", type="string",
            metavar = "system id",
            dest=_o("system"), help="system id")

        command_args.add_option('', '--backing-snapshot', action="store", type="string",
            metavar = "<backing snapshot>", default=None,
            dest="backing_snapshot", help="backing snap shot name for operation")

        command_args.add_option('', '--src', action="store", type="string",
            metavar = "<source file>", default=None,
            dest=_o("src"), help="source of operation")

        command_args.add_option('', '--dest', action="store", type="string",
            metavar = "<source file>", default=None,
            dest=_o("dest"), help="destination of operation")

        command_args.add_option('', '--file', action="append", type="string",
            metavar = "<file>", default=[],
            dest="file", help="file to include in operation, option can be repeated")

        command_args.add_option('', '--fileas', action="append", type="string",
            metavar = "<fileas>", default=[],
            dest="fileas", help="file to be renamed as, option can be repeated")

        command_args.add_option('', '--fs', action="store", type="string",
            metavar = "<file system>", default=None,
            dest=_o("fs"), help="file system of interest")

        command_args.add_option('', '--exportpath', action="store", type="string",
            metavar = "<path for export>", default=None,
            dest=_o("exportpath"), help="desired export path on array")

        command_args.add_option('', '--root', action="append", type="string",
            metavar = "<no_root_squash_host>", default=[],
            dest="nfs_root", help="list of hosts with no_root_squash")

        command_args.add_option('', '--ro', action="append", type="string",
            metavar = "<read only host>", default=[],
            dest="nfs_ro", help="list of hosts with read/only access")

        command_args.add_option('', '--rw', action="append", type="string",
            metavar = "<read/write host>", default=[],
            dest="nfs_rw", help="list of hosts with read/write access")

        command_args.add_option('', '--anonuid', action="store", type="string",
            metavar = "<anonymous uid>", default=None,
            dest="anonuid", help="uid to map to anonymous")

        command_args.add_option('', '--anongid', action="store", type="string",
            metavar = "<anonymous uid>", default=None,
            dest="anongid", help="gid to map to anonymous")

        command_args.add_option('', '--authtype', action="store", type="string",
            metavar = "<type>", default=None,
            dest="authtype", help="NFS client authentication type")

        command_args.add_option( '', '--all', action="store_true", dest="all",
            default=False, help='specify all in an operation')

        command_args.add_option( '', '--src_start', action="append", type="int",
            metavar="<source block start>", default=None, dest=_o("src_start"),
            help="source block address to replicate")

        command_args.add_option( '', '--dest_start', action="append", type="int",
            metavar="<dest. block start>", default=None, dest=_o("dest_start"),
            help="destination block address to replicate")

        command_args.add_option( '', '--count', action="append", type="int",
            metavar="<block count>", default=None, dest=_o("count"),
            help="number of blocks to replicate")

        command_args.add_option('', '--username', action="store", type="string",
            metavar = "<username>", default=None,
            dest=_o("username"), help="CHAP user name")

        command_args.add_option('', '--password', action="store", type="string",
            metavar = "<password>", default=None,
            dest=_o("password"), help="CHAP password")


        parser.add_option_group(command_args)

        (self.options, self.args) = parser.parse_args()

    ## Checks to make sure only one command was specified on the command line
    # @param    self    The this pointer
    # @return   tuple of command to execute and the value of the command argument
    def _cmd(self):
        cmds = [ e[4:] for e in dir(self.options)
                 if e[0:4]  == "cmd_" and self.options.__dict__[e] is not None ]
        if len(cmds) > 1:
            raise ArgError("More than one command operation specified (" + ",".join(cmds) + ")")

        if len(cmds) == 1:
            return cmds[0], self.options.__dict__['cmd_' + cmds[0]]
        else:
            return None, None


    ## Validates that the required options for a given command are present.
    # @param    self    The this pointer
    # @return   None
    def _validate(self):
        expected_opts = self.verify[self.cmd]['options']
        actual_ops = [ e[4:] for e in dir(self.options)
                       if e[0:4]  == "opt_" and self.options.__dict__[e] is not None ]

        if len(expected_opts):
            if len(expected_opts) == len(actual_ops):
                for e in expected_opts:
                    if e not in actual_ops:
                        print "expected=", ":".join(expected_opts)
                        print "actual=", ":".join(actual_ops)
                        raise ArgError("missing option " + e)

            else:
                raise ArgError("expected options = (" +
                               ",".join(expected_opts) + ") actual = (" +
                               ",".join(actual_ops) + ")")

        #Check size
        if self.options.opt_size:
            self._size(self.options.opt_size)

    def _list(self,l):
        if l and len(l):
            if self.options.sep:
                return self.options.sep.join(l)
            else:
                return ", ".join(l)
        else:
            return "None"

    ## Display the types of nfs client authentication that are supported.
    # @param    self    The this pointer
    # @return None
    def display_nfs_client_authentication(self):
        """
        Dump the supported nfs client authentication types
        """
        if self.options.sep:
            print self.options.sep.join(self.c.export_auth())
        else:
            print ", ".join(self.c.export_auth())

    ## Method that calls the appropriate method based on what the cmd_value is
    # @param    self    The this pointer
    def list(self):
        if self.cmd_value == 'VOLUMES':
            self.display_data(self.c.volumes())
        elif self.cmd_value == 'POOLS':
            self.display_data(self.c.pools())
        elif self.cmd_value == 'FS':
            self.display_data(self.c.fs())
        elif self.cmd_value == 'SNAPSHOTS':
            if self.options.opt_fs is None:
                raise ArgError("--fs <file system id> required")

            fs = self._get_item(self.c.fs(),self.options.opt_fs)
            if fs:
                self.display_data(self.c.fs_snapshots(fs))
            else:
                raise ArgError("filesystem %s not found!" % self.options.opt_volume)
        elif self.cmd_value == 'INITIATORS':
            self.display_data(self.c.initiators())
        elif self.cmd_value == 'EXPORTS':
            self.display_data(self.c.exports())
        elif self.cmd_value == 'NFS_CLIENT_AUTH':
            self.display_nfs_client_authentication()
        elif self.cmd_value == 'ACCESS_GROUPS':
            self.display_data(self.c.access_group_list())
        elif self.cmd_value == 'SYSTEMS':
            self.display_data(self.c.systems())
        else:
            raise ArgError(" unsupported listing type=%s", self.cmd_value)

    ## Converts type initiator type to enumeration type.
    # @param    type    String representation of type
    # @returns  Enumerated value
    @staticmethod
    def _init_type_to_enum(type):
        if type == 'WWPN':
            i = data.Initiator.TYPE_PORT_WWN
        elif type == 'WWNN':
            i = data.Initiator.TYPE_NODE_WWN
        elif type == 'ISCSI':
            i = data.Initiator.TYPE_ISCSI
        elif type == 'HOSTNAME':
            i = data.Initiator.TYPE_HOSTNAME
        else:
            raise ArgError("invalid initiator type " + type)
        return i

    ## Creates an access group.
    # @param    self    The this pointer
    def create_access_group(self):
        name = self.cmd_value
        initiator = self.options.opt_id
        i = CmdLine._init_type_to_enum(self.options.opt_type)
        access_group = self.c.access_group_create(name, initiator, i,
                        self.options.opt_system)
        self.display_data([access_group])

    def _add_rm_access_grp_init(self, op):
        agl = self.c.access_group_list()
        group = self._get_item(agl, self.cmd_value)

        if group:
            if op:
                i = CmdLine._init_type_to_enum(self.options.opt_type)
                self.c.access_group_add_initiator(group, self.options.opt_id, i)
            else:
                i = self._get_item(self.c.initiators(), self.options.opt_id)
                if i:
                    self.c.access_group_del_initiator(group, i.id)
                else:
                    raise ArgError("initiator with id %s not found!" % self.options.opt_id)
        else:
            if not group:
                raise ArgError('access group with id %s not found!' % self.cmd_value)

    ## Adds an initiator from an access group
    def access_group_add(self):
        self._add_rm_access_grp_init(True)

    ## Removes an initiator from an access group
    def access_group_remove(self):
        self._add_rm_access_grp_init(False)

    def access_group_volumes(self):
        agl = self.c.access_group_list()
        group = self._get_item(agl, self.cmd_value)

        if group:
            vols = self.c.volumes_accessible_by_access_group(group)
            self.display_data(vols)
        else:
            raise ArgError('access group with id %s not found!' % self.cmd_value)

    def volume_accessible_init(self):
        i = self._get_item(self.c.initiators(), self.cmd_value)

        if i:
            volumes = self.c.volumes_accessible_by_initiator(i)
            self.display_data(volumes)
        else:
            raise ArgError("initiator with id= %s not found!" % self.cmd_value)

    def init_granted_volume(self):
        vol = self._get_item(self.c.volumes(), self.cmd_value)

        if vol:
            initiators = self.c.initiators_granted_to_volume(vol)
            self.display_data(initiators)
        else:
            raise ArgError("volume with id= %s not found!" % self.cmd_value)

    def iscsi_chap(self):
        init = self._get_item(self.c.initiators(), self.cmd_value)
        if init:
            self.c.iscsi_chap_auth_inbound(init, self.options.opt_username,
                                                    self.options.opt_password)
        else:
            raise ArgError("initiator with id= %s not found" %self.cmd_value)

    def volume_access_group(self):
        vol = self._get_item(self.c.volumes(), self.cmd_value)

        if vol:
            groups = self.c.access_groups_granted_to_volume(vol)
            self.display_data(groups)
        else:
            raise ArgError("volume with id= %s not found!" % self.cmd_value)

    ## Used to delete access group
    # @param    self    The this pointer
    def delete_access_group(self):
        agl = self.c.access_group_list()

        group = self._get_item(agl, self.cmd_value)
        if group:
            return self.c.access_group_del(group)
        else:
            raise ArgError("access group with id = %s not found!" % self.cmd_value)

    ## Used to delete a file system
    # @param    self    The this pointer
    def fs_delete(self):

        fs = self._get_item(self.c.fs(), self.cmd_value)
        if fs:
            self._wait_for_it("delete-fs", self.c.fs_delete(fs), None)
        else:
            raise ArgError("fs with id = %s not found!" % self.cmd_value)

    ## Used to create a file system
    # @param    self    The this pointer
    def fs_create(self):
        #Need a name, size and pool
        size = self._size(self.options.opt_size)
        p = self._get_item(self.c.pools(), self.options.opt_pool)
        name = self.cmd_value
        if p:
            fs = self._wait_for_it("create-fs", *self.c.fs_create(p, name, size))
            self.display_data([fs])
        else:
            raise ArgError("pool with id = %s not found!" % self.options.opt_pool)

    ## Used to resize a file system
    # @param    self    The this pointer
    def fs_resize(self):
        fs = self._get_item(self.c.fs(), self.cmd_value)
        size = self._size(self.options.opt_size)
        fs = self._wait_for_it("resize-fs", *self.c.fs_resize(fs, size))
        self.display_data([fs])

    ## Used to clone a file system
    # @param    self    The this pointer
    def fs_clone(self):
        src_fs = self._get_item(self.c.fs(), self.cmd_value)
        name = self.options.opt_name

        if not src_fs:
            raise ArgError(" source file system with id=%s not found!" % self.cmd_value)

        if self.options.backing_snapshot:
            #go get the snapsnot
            ss = self._get_item(self.c.fs_snapshots(src_fs), self.options.backing_snapshot)
            if not ss:
                raise ArgError(" snapshot with id= %s not found!" % self.options.backing_snapshot)
        else:
            ss = None

        fs = self._wait_for_it("fs_clone", *self.c.fs_clone(src_fs, name, ss))
        self.display_data([fs])

    ## Used to clone a file(s)
    # @param    self    The this pointer
    def file_clone(self):
        fs = self._get_item(self.c.fs(), self.cmd_value)
        src = self.options.opt_src
        dest = self.options.opt_dest

        if self.options.backing_snapshot:
            #go get the snapsnot
            ss = self._get_item(self.c.fs_snapshots(fs), self.options.backing_snapshot)
        else:
            ss = None

        self._wait_for_it("file_clone", self.c.file_clone(fs, src, dest, ss), None)

    def _get_item(self, l, id):
        for i in l:
            if i.id == id:
                return i
        return None

    ##Converts a size parameter into the appropriate number of bytes
    # @param    s   Size to convert to bytes handles M, G, T postfix
    # @return Size in bytes
    @staticmethod
    def _size(s):
        s = string.upper(s)
        m = re.match('([0-9]+)([MGT]?)', s)
        if m:
            unit = m.group(2)
            rc = int(m.group(1))
            if unit == 'M':
                rc *= common.MiB
            elif unit == 'G':
                rc *= common.GiB
            else:
                rc *= common.TiB
        else:
            raise ArgError(" size is not in form <number>|<number[M|G|T]>")
        return rc

    def _cp(self, cap, val):
        if self.options.sep is not None:
            s = self.options.sep
        else:
            s = ':'

        if val == data.Capabilities.SUPPORTED:
            v = "SUPPORTED"
        elif val == data.Capabilities.UNSUPPORTED:
            v = "UNSUPPORTED"
        elif val == data.Capabilities.SUPPORTED_OFFLINE:
            v = "SUPPORTED_OFFLINE"
        elif val == data.Capabilities.NOT_IMPLEMENTED:
            v = "NOT_IMPLEMENTED"
        else:
            v = "UNKNOWN"

        print "%s%s%s" % (cap, s, v)


    def capabilities(self):
        s = self._get_item(self.c.systems(), self.cmd_value)

        if s:
            cap = self.c.capabilities(s)
            self._cp("BLOCK_SUPPORT", cap.get(Capabilities.BLOCK_SUPPORT))
            self._cp("FS_SUPPORT", cap.get(Capabilities.FS_SUPPORT))
            self._cp("INITIATORS", cap.get(Capabilities.INITIATORS))
            self._cp("INITIATORS_GRANTED_TO_VOLUME", cap.get(Capabilities.INITIATORS_GRANTED_TO_VOLUME))
            self._cp("VOLUMES", cap.get(Capabilities.VOLUMES))
            self._cp("VOLUME_CREATE", cap.get(Capabilities.VOLUME_CREATE))
            self._cp("VOLUME_RESIZE", cap.get(Capabilities.VOLUME_RESIZE))
            self._cp("VOLUME_REPLICATE", cap.get(Capabilities.VOLUME_REPLICATE))
            self._cp("VOLUME_REPLICATE_CLONE", cap.get(Capabilities.VOLUME_REPLICATE_CLONE))
            self._cp("VOLUME_REPLICATE_COPY", cap.get(Capabilities.VOLUME_REPLICATE_COPY))
            self._cp("VOLUME_REPLICATE_MIRROR_ASYNC", cap.get(Capabilities.VOLUME_REPLICATE_MIRROR_ASYNC))
            self._cp("VOLUME_REPLICATE_MIRROR_SYNC", cap.get(Capabilities.VOLUME_REPLICATE_MIRROR_SYNC))
            self._cp("VOLUME_COPY_RANGE_BLOCK_SIZE", cap.get(Capabilities.VOLUME_COPY_RANGE_BLOCK_SIZE))
            self._cp("VOLUME_COPY_RANGE", cap.get(Capabilities.VOLUME_COPY_RANGE))
            self._cp("VOLUME_COPY_RANGE_CLONE", cap.get(Capabilities.VOLUME_COPY_RANGE_CLONE))
            self._cp("VOLUME_COPY_RANGE_COPY", cap.get(Capabilities.VOLUME_COPY_RANGE_COPY))
            self._cp("VOLUME_DELETE", cap.get(Capabilities.VOLUME_DELETE))
            self._cp("VOLUME_ONLINE", cap.get(Capabilities.VOLUME_ONLINE))
            self._cp("VOLUME_OFFLINE", cap.get(Capabilities.VOLUME_OFFLINE))
            self._cp("VOLUME_INITIATOR_GRANT", cap.get(Capabilities.VOLUME_INITIATOR_GRANT))
            self._cp("VOLUME_INITIATOR_REVOKE", cap.get(Capabilities.VOLUME_INITIATOR_REVOKE))
            self._cp("VOLUME_ISCSI_CHAP_AUTHENTICATION", cap.get(Capabilities.VOLUME_ISCSI_CHAP_AUTHENTICATION))
            self._cp("ACCESS_GROUP_GRANT", cap.get(Capabilities.ACCESS_GROUP_GRANT))
            self._cp("ACCESS_GROUP_REVOKE", cap.get(Capabilities.ACCESS_GROUP_REVOKE))
            self._cp("ACCESS_GROUP_LIST", cap.get(Capabilities.ACCESS_GROUP_LIST))
            self._cp("ACCESS_GROUP_CREATE", cap.get(Capabilities.ACCESS_GROUP_CREATE))
            self._cp("ACCESS_GROUP_DELETE", cap.get(Capabilities.ACCESS_GROUP_DELETE))
            self._cp("ACCESS_GROUP_ADD_INITIATOR", cap.get(Capabilities.ACCESS_GROUP_ADD_INITIATOR))
            self._cp("ACCESS_GROUP_DEL_INITIATOR", cap.get(Capabilities.ACCESS_GROUP_DEL_INITIATOR))
            self._cp("VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP", cap.get(Capabilities.VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP))
            self._cp("VOLUME_ACCESSIBLE_BY_INITIATOR", cap.get(Capabilities.VOLUME_ACCESSIBLE_BY_INITIATOR))
            self._cp("ACCESS_GROUPS_GRANTED_TO_VOLUME", cap.get(Capabilities.ACCESS_GROUPS_GRANTED_TO_VOLUME))
            self._cp("VOLUME_CHILD_DEPENDENCY", cap.get(Capabilities.VOLUME_CHILD_DEPENDENCY))
            self._cp("VOLUME_CHILD_DEPENDENCY_RM", cap.get(Capabilities.VOLUME_CHILD_DEPENDENCY_RM))
            self._cp("FS", cap.get(Capabilities.FS))
            self._cp("FS_DELETE", cap.get(Capabilities.FS_DELETE))
            self._cp("FS_RESIZE", cap.get(Capabilities.FS_RESIZE))
            self._cp("FS_CREATE", cap.get(Capabilities.FS_CREATE))
            self._cp("FS_CLONE", cap.get(Capabilities.FS_CLONE))
            self._cp("FILE_CLONE", cap.get(Capabilities.FILE_CLONE))
            self._cp("FS_SNAPSHOTS", cap.get(Capabilities.FS_SNAPSHOTS))
            self._cp("FS_SNAPSHOT_CREATE", cap.get(Capabilities.FS_SNAPSHOT_CREATE))
            self._cp("FS_SNAPSHOT_CREATE_SPECIFIC_FILES", cap.get(Capabilities.FS_SNAPSHOT_CREATE_SPECIFIC_FILES))
            self._cp("FS_SNAPSHOT_DELETE", cap.get(Capabilities.FS_SNAPSHOT_DELETE))
            self._cp("FS_SNAPSHOT_REVERT", cap.get(Capabilities.FS_SNAPSHOT_REVERT))
            self._cp("FS_SNAPSHOT_REVERT_SPECIFIC_FILES", cap.get(Capabilities.FS_SNAPSHOT_REVERT_SPECIFIC_FILES))
            self._cp("FS_CHILD_DEPENDENCY", cap.get(Capabilities.FS_CHILD_DEPENDENCY))
            self._cp("FS_CHILD_DEPENDENCY_RM", cap.get(Capabilities.FS_CHILD_DEPENDENCY_RM))
            self._cp("FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES", cap.get(Capabilities.FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES))
            self._cp("EXPORT_AUTH", cap.get(Capabilities.EXPORT_AUTH))
            self._cp("EXPORTS", cap.get(Capabilities.EXPORTS))
            self._cp("EXPORT_FS", cap.get(Capabilities.EXPORT_FS))
            self._cp("EXPORT_REMOVE", cap.get(Capabilities.EXPORT_REMOVE))
        else:
            raise ArgError( "system with id= %s not found!" % self.cmd_value)

    ## Creates a volume
    # @param    self    The this pointer
    def create_volume(self):
        #Get pool
        p = self._get_item(self.c.pools(), self.options.opt_pool)
        if p:
            vol = self._wait_for_it("create-volume",
                *self.c.volume_create(p, self.cmd_value,
                self._size(self.options.opt_size),
                data.Volume.prov_string_to_type(self.options.provisioning)))

            self.display_data([vol])
        else:
            raise ArgError(" pool with id= %s not found!" % self.options.opt_pool)

    ## Creates a snapshot
    # @param    self    The this pointer
    def create_ss(self):
        #Get fs
        fs = self._get_item(self.c.fs(), self.options.opt_fs)
        if fs:
            ss = self._wait_for_it("snapshot-create",
                    *self.c.fs_snapshot_create(fs,self.cmd_value,
                                            self.options.file))

            self.display_data([ss])
        else:
            raise ArgError( "fs with id= %s not found!" % self.options.opt_fs)

    ## Restores a snap shot
    # @param    self    The this pointer
    def restore_ss(self):
        #Get snapshot
        fs = self._get_item(self.c.fs(), self.options.opt_fs)
        ss = self._get_item(self.c.fs_snapshots(fs), self.cmd_value)

        if ss and fs:

            if self.options.file:
                if self.options.fileas:
                    if len(self.options.file) != len(self.options.fileas):
                        raise ArgError("number of --files not equal to --fileas")

            if self.options.all:
                if self.options.file or self.options.fileas:
                    raise ArgError("Unable to specify --all and --files or --fileas")

            if self.options.all is False and self.options.file is None:
                raise ArgError("Need to specify --all or at least one --file")

            self._wait_for_it('restore-ss', self.c.fs_snapshot_revert(fs, ss,
                                    self.options.file, self.options.fileas,
                                    self.options.all), None)
        else:
            if not ss:
                raise ArgError( "ss with id= %s not found!" % self.cmd_value)
            if not fs:
                raise ArgError( "fs with id= %s not found!" % self.options.opt_fs)

    ## Deletes a volume
    # @param    self    The this pointer
    def delete_volume(self):
        v = self._get_item(self.c.volumes(), self.cmd_value)

        if v:
            self._wait_for_it("delete-volume", self.c.volume_delete(v), None)
        else:
            raise ArgError(" volume with id= %s not found!" % self.cmd_value)

    ## Deletes a snap shot
    # @param    self    The this pointer
    def delete_ss(self):
        fs = self._get_item(self.c.fs(), self.options.opt_fs)
        if fs:
            ss = self._get_item(self.c.fs_snapshots(fs), self.cmd_value)
            if ss:
                self._wait_for_it("delete-snapshot", self.c.fs_snapshot_delete(fs,ss), None)
            else:
                raise ArgError(" snapshot with id= %s not found!" % self.cmd_value)
        else:
            raise ArgError(" file system with id= %s not found!" % self.options.opt_fs)

    ## Waits for an operation to complete by polling for the status of the
    # operations.
    # @param    self    The this pointer
    # @param    msg     Message to display if this job fails
    # @param    job     The job id to wait on
    # @param    item    The item that could be available now if there is no job
    def _wait_for_it(self, msg, job, item):
        if not job:
            return item
        else:
            #If a user doesn't want to wait, return the job id to stdout
            #and exit with job in progress
            if self.options.async:
                print job
                self.shutdown(common.ErrorNumber.JOB_STARTED)

            while True:
                (s, percent, i) = self.c.job_status(job)

                if s == common.JobStatus.INPROGRESS:
                    #Add an option to spit out progress?
                    #print "%s - Percent %s complete" % (job, percent)
                    time.sleep(0.25)
                elif s == common.JobStatus.COMPLETE:
                    self.c.job_free(job)
                    return i
                else:
                    #Something better to do here?
                    raise ArgError(msg + " job error code= " + str(s))

    ## Retrieves the status of the specified job
    # @param    self    The this pointer
    def job_status(self):
        (s, percent, i) = self.c.job_status(self.cmd_value)

        if s == common.JobStatus.COMPLETE:
            if i:
                self.display_data([i])

            self.c.job_free(self.cmd_value)
        else:
            print str(percent)

    ## Replicates a volume
    # @param    self    The this pointer
    def replicate_volume(self):
        p = self._get_item(self.c.pools(), self.options.opt_pool)
        v = self._get_item(self.c.volumes(), self.cmd_value)

        if p and v:

            type = data.Volume.rep_String_to_type(self.options.opt_type)
            if type == data.Volume.REPLICATE_UNKNOWN:
                raise ArgError("invalid replication type= %s" % type)

            vol = self._wait_for_it("replicate volume", *self.c.volume_replicate(p, type, v,
                self.options.opt_name))
            self.display_data([vol])
        else:
            if not p:
                raise ArgError("pool with id= %s not found!" % self.options.opt_pool)
            if not v:
                raise ArgError("Volume with id= %s not found!" % self.cmd_value)

    ## Replicates a range of a volume
    # @param    self    The this pointer
    def replicate_vol_range(self):
        src = self._get_item(self.c.volumes(), self.cmd_value)
        dest = self._get_item(self.c.volumes(), self.options.opt_dest)

        if src and dest:
            type = data.Volume.rep_String_to_type(self.options.opt_type)
            if type == data.Volume.REPLICATE_UNKNOWN:
                raise ArgError("invalid replication type= %s" % type)

            src_starts = self.options.opt_src_start
            dest_starts = self.options.opt_dest_start
            counts = self.options.opt_count

            if( 0 < len(src_starts) == len(dest_starts) and
                len(dest_starts) == len(counts) ):
                ranges = []

                for i in range(len(src_starts)):
                    ranges.append(data.BlockRange(src_starts[i], dest_starts[i], counts[i]))
                self.c.volume_replicate_range(type, src, dest, ranges)
        else:
            if not src:
                raise ArgError("src volume with id= %s not found!" % self.cmd_value)
            if not dest:
                raise ArgError("dest volume with id= %s not found!" % self.options.opt_dest)

    ## Used to grant or revoke access to a volume to an initiator.
    # @param    self    The this pointer
    # @param    map     If True we map, else we un-map.
    def _access(self, map=True):
        v = self._get_item(self.c.volumes(), self.options.opt_volume)
        initiator_id = self.cmd_value

        if v:
            if map:
                i_type = CmdLine._init_type_to_enum(self.options.opt_type)
                access = data.Volume.access_string_to_type(self.options.opt_access)
                self.c.initiator_grant(initiator_id, i_type, v, access)
            else:
                initiator = self._get_item(self.c.initiators(), initiator_id)

                if initiator:
                    self.c.initiator_revoke(initiator, v)
                else:
                    raise ArgError("initiator with id= %s not found" % initiator_id)
        else:
            if not v:
                raise ArgError("volume with id= %s not found!" % self.options.opt_volume)

    ## Grant access to volume to an initiator
    # @param    self    The this pointer
    def access_grant(self):
        return self._access()

    ## Revoke access to volume to an initiator
    # @param    self    The this pointer
    def access_revoke(self):
        return self._access(False)

    def _access_group(self, map=True):
        agl = self.c.access_group_list()
        group = self._get_item(agl, self.cmd_value)
        v = self._get_item(self.c.volumes(), self.options.opt_volume)

        if group and v:
            if map:
                access = data.Volume.access_string_to_type(self.options.opt_access)
                self.c.access_group_grant(group, v, access)
            else:
                self.c.access_group_revoke(group, v)
        else:
            if not group:
                raise ArgError("access group with id= %s not found!" % self.cmd_value)
            if not v:
                raise ArgError("volume with id= %s not found!" % self.options.opt_volume)

    def access_grant_group(self):
        return self._access_group(True)

    def access_revoke_group(self):
        return self._access_group(False)

    ## Re-sizes a volume
    # @param    self    The this pointer
    def resize_volume(self):
        v = self._get_item(self.c.volumes(), self.cmd_value)
        if v:
            size = self._size(self.options.opt_size)
            vol = self._wait_for_it("resize", *self.c.volume_resize(v, size))
            self.display_data([vol])
        else:
            raise ArgError("volume with id= %s not found!" % self.cmd_value)

    ## Removes a nfs export
    # @param    self    The this pointer
    def nfs_export_remove(self):
        export = self._get_item(self.c.exports(), self.cmd_value)
        if export:
            self.c.export_remove(export)
        else:
            raise ArgError("nfs export with id= %s not found!" % self.cmd_value)

    ## Exports a file system as a NFS export
    # @param    self    The this pointer
    def nfs_export_fs(self):
        fs = self._get_item(self.c.fs(), self.cmd_value)

        if fs:
            #Check to see if we have some type of access specified
            if len(self.options.nfs_root) == 0 and\
               len(self.options.nfs_rw) == 0 and\
               len(self.options.nfs_ro) == 0:
                raise ArgError(" please specify --root, --ro or --rw access")

            export = self.c.export_fs(fs.id, self.options.opt_exportpath,
                self.options.nfs_root, self.options.nfs_rw, self.options.nfs_ro,
                self.options.anonuid, self.options.anongid,
                self.options.authtype, None)
            self.display_data([export])
        else:
            raise ArgError(" file system with id=%s not found!" % self.cmd_value)

    ## Displays volume dependants.
    # @param    self    The this pointer
    def vol_dependants(self):
        v = self._get_item(self.c.volumes(), self.cmd_value)

        if v:
            rc = self.c.volume_child_dependency(v)
            print str(rc)
        else:
            raise ArgError("volume with id= %s not found!" % self.cmd_value)

    ## Removes volume dependants.
    # @param    self    The this pointer
    def vol_dependants_rm(self):
        v = self._get_item(self.c.volumes(), self.cmd_value)

        if v:
            self._wait_for_it("volume-dependant-rm",
                self.c.volume_child_dependency_rm(v), None)
        else:
            raise ArgError("volume with id= %s not found!" % self.cmd_value)

    ## Displays file system dependants
    # @param    self    The this pointer
    def fs_dependants(self):
        fs = self._get_item(self.c.fs(), self.cmd_value)

        if fs:
            rc = self.c.fs_child_dependency(fs, self.options.file)
            print str(rc)
        else:
            raise ArgError("File system with id= %s not found!" % self.cmd_value)

    ## Removes file system dependants
    # @param    self    The this pointer
    def fs_dependants_rm(self):
        fs = self._get_item(self.c.fs(), self.cmd_value)

        if fs:
            self._wait_for_it("fs-dependants-rm",
                self.c.fs_child_dependency_rm(fs, self.options.file), None)
        else:
            raise ArgError("File system with id= %s not found!" % self.cmd_value)

    ## Class constructor.
    # @param    self    The this pointer
    def __init__(self):
        self.c = None
        self.cli()

        self.cleanup = None

        #Get and set the command and command value we will be executing
        (self.cmd, self.cmd_value) = self._cmd()

        if self.cmd is None:
            raise ArgError("no command specified, try --help")

        #Check for extras
        if len(self.args):
            raise ArgError("Extra command line arguments (" + ",".join(self.args) + ")")

        #Data driven validation
        self.verify = {'list': {'options': [], 'method': self.list},
                       'delete-fs': {'options': [],
                                     'method': self.fs_delete},
                       'delete-access-group': {'options': [],
                                     'method': self.delete_access_group},
                       'capabilities': {'options': [],
                                     'method': self.capabilities },
                       'create-volume': {'options': ['size', 'pool'],
                                         'method': self.create_volume},
                       'create-fs': {'options': ['size', 'pool'],
                                     'method': self.fs_create},
                       'clone-fs': {'options': ['name'],
                                    'method': self.fs_clone},
                       'create-access-group': {'options': ['id', 'type', 'system'],
                                               'method': self.create_access_group},
                       'access-group-add': {'options': ['id', 'type'],
                                               'method': self.access_group_add},
                       'access-group-remove': {'options': ['id'],
                                               'method': self.access_group_remove},
                       'access-group-volumes': {'options': [],
                                               'method': self.access_group_volumes},
                       'volume-access-group': {'options': [],
                                                'method': self.volume_access_group},
                       'volumes-accessible-initiator': {'options': [],
                                               'method': self.volume_accessible_init},

                       'initiators-granted-volume': {'options': [],
                                               'method': self.init_granted_volume},

                       'iscsi-chap': {'options': ['username', 'password'],
                                            'method': self.iscsi_chap},

                       'create-ss': {'options': ['fs'],
                                     'method': self.create_ss},
                       'clone-file': {'options': ['src', 'dest'],
                                      'method': self.file_clone},
                       'delete-volume': {'options': [],
                                         'method': self.delete_volume},
                       'delete-ss': {'options': ['fs'],
                                     'method': self.delete_ss},
                       'replicate-volume': {'options': ['type', 'pool', 'name'],
                                            'method': self.replicate_volume},
                       'access-grant': {'options': ['volume', 'access', 'type'],
                                        'method': self.access_grant},
                       'access-grant-group': {'options': ['volume', 'access'],
                                        'method': self.access_grant_group},
                       'access-revoke': {'options': ['volume'],
                                         'method': self.access_revoke},
                       'access-revoke-group': {'options': ['volume'],
                                         'method': self.access_revoke_group},
                       'resize-volume': {'options': ['size'],
                                         'method': self.resize_volume},
                       'resize-fs': {'options': ['size'],
                                     'method': self.fs_resize},
                       'nfs-export-remove': {'options': [],
                                             'method': self.nfs_export_remove},
                       'nfs-export-fs': {'options': ['exportpath'],
                                         'method': self.nfs_export_fs},
                       'restore-ss': {'options': ['fs'],
                                        'method': self.restore_ss},
                       'job-status': {'options': [],
                                      'method': self.job_status},
                       'replicate-volume-range': {'options': ['type', 'dest',
                                                    'src_start', 'dest_start',
                                                    'count'],
                                      'method': self.replicate_vol_range},
                       'volume-dependants': {'options': [],
                                                'method': self.vol_dependants},
                       'volume-dependants-rm': {'options': [],
                                             'method': self.vol_dependants_rm},
                       'fs-dependants': {'options': [],
                                             'method': self.fs_dependants},
                       'fs-dependants-rm': {'options': [],
                                         'method': self.fs_dependants_rm},

        }
        self._validate()

        self.tmo = int(self.options.wait)
        if not self.tmo or self.tmo < 0:
            raise ArgError("[-w|--wait] reguires a non-zero positive integer")


        self.uri = os.getenv('LSMCLI_URI')
        self.password = os.getenv('LSMCLI_PASSWORD')
        if self.options.uri is not None:
            self.uri = self.options.uri

        if self.uri is None:
            raise ArgError("--uri missing or export LSMCLI_URI")

        #Lastly get the password if requested.
        if self.options.prompt is not None:
            self.password = getpass.getpass()

        if self.password is not None:
            #Check for username
            u = common.uri_parse(self.uri)
            if u['username'] is None:
                raise ArgError("password specified with no user name in uri")

    ## Does appropriate clean-up
    # @param    self    The this pointer
    # @param    ec      The exit code
    def shutdown(self, ec = None):
        if self.cleanup:
            self.cleanup()

        if ec:
            sys.exit(ec)

    ## Process the specified command
    # @param    self    The this pointer
    # @param    cli     The object instance to invoke methods on.
    def process(self, cli = None):
        """
        Process the parsed command.
        """
        if cli:
            #Directly invoking code though a wrapper to catch unsupported
            #operations.
            self.c = Wrapper(cli())
            self.c.startup(self.uri, self.password, self.tmo)
            self.cleanup = self.c.shutdown
        else:
            #Going across the ipc pipe
            self.c = client.Client(self.uri,self.password, self.tmo)

            if os.getenv('LSM_DEBUG_PLUGIN'):
                raw_input("Attach debugger to plug-in, press <return> when ready...")

            self.cleanup = self.c.close

        self.verify[self.cmd]['method']()
        self.shutdown()
