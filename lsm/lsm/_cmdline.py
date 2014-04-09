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
import os
import sys
import getpass
import time

from argparse import ArgumentParser
from argparse import RawTextHelpFormatter

from lsm import (Client, Pool, VERSION, LsmError, Capabilities, Disk,
                 Initiator, Volume, JobStatus, ErrorNumber, BlockRange,
                 uri_parse)

from _data import PlugData
from _common import getch, size_human_2_size_bytes, Proxy
from lsmcli_data_display import DisplayData

##@package lsm.cmdline


## Users are reporting errors with broken pipe when piping output
# to another program.  This appears to be related to this issue:
# http://bugs.python.org/issue11380
# Unable to reproduce, but hopefully this will address it.
# @param msg    The message to be written to stdout
def out(msg):
    try:
        sys.stdout.write(str(msg))
        sys.stdout.write("\n")
        sys.stdout.flush()
    except IOError:
        sys.exit(1)


## Wraps the invocation to the command line
# @param    c   Object to invoke calls on (optional)
def cmd_line_wrapper(c=None):
    """
    Common command line code, called.
    """
    try:
        cli = CmdLine()
        cli.process(c)
    except ArgError as ae:
        sys.stderr.write(str(ae))
        sys.stderr.flush()
        sys.exit(2)
    except LsmError as le:
        sys.stderr.write(str(le) + "\n")
        sys.stderr.flush()
        sys.exit(4)
    except KeyboardInterrupt:
        sys.exit(1)


## This class represents a command line argument error
class ArgError(Exception):
    def __init__(self, message, *args, **kwargs):
        """
        Class represents an error.
        """
        Exception.__init__(self, *args, **kwargs)
        self.msg = message

    def __str__(self):
        return "%s: error: %s\n" % (os.path.basename(sys.argv[0]), self.msg)


## Finds an item based on the id.  Each list item requires a member "id"
# @param    l       list to search
# @param    the_id  the id to match
# @param    friendly_name - name to put in the exception saying what we
#           couldn't find
def _get_item(l, the_id, friendly_name='item'):
    for i in l:
        if i.id == the_id:
            return i
    raise ArgError('%s with id %s not found!' % (friendly_name, the_id))

list_choices = ['VOLUMES', 'INITIATORS', 'POOLS', 'FS', 'SNAPSHOTS',
                'EXPORTS', "NFS_CLIENT_AUTH", 'ACCESS_GROUPS',
                'SYSTEMS', 'DISKS', 'PLUGINS']

initiator_id_types = ('WWPN', 'WWNN', 'ISCSI', 'HOSTNAME', 'SAS')
initiator_id_help = "Initiator ID type: " + ", ".join(initiator_id_types)

provision_types = ('DEFAULT', 'THIN', 'FULL')
provision_help = "provisioning type: " + ", ".join(provision_types)

access_types = ('RO', 'RW')
access_help = "Access type: %s. Default value is RW." % ", ".join(access_types)

replicate_types = ('SNAPSHOT', 'CLONE', 'COPY', 'MIRROR_ASYNC', 'MIRROR_SYNC')
replicate_help = "replication type: " + ", ".join(replicate_types)

size_help = 'Can use B, KiB, MiB, GiB, TiB, PiB postfix (IEC sizing)'

member_types = ('DISK', 'VOLUME', 'POOL', 'DISK_ATA', 'DISK_SATA',
                'DISK_SAS', 'DISK_FC', 'DISK_SOP', 'DISK_SCSI', 'DISK_NL_SAS',
                'DISK_HDD', 'DISK_SSD', 'DISK_HYBRID')

member_types_formated = ''
for i in range(0, len(member_types), 4):
    member_types_formated += "\n    "
    for member_type_str in member_types[i:i+4]:
        member_types_formated += "%-15s" % member_type_str

member_help = "Valid member type: " + member_types_formated

raid_types = ('JBOD', 'RAID0', 'RAID1', 'RAID3', 'RAID4', 'RAID5', 'RAID6',
              'RAID10', 'RAID50', 'RAID60', 'RAID51', 'RAID61',
              'NOT_APPLICABLE')

raid_types_formated = ''
for i in range(0, len(raid_types), 4):
    raid_types_formated += "\n    "
    for raid_type_str in raid_types[i:i+4]:
        raid_types_formated += "%-15s" % raid_type_str

raid_help = "Valid RAID type:" + raid_types_formated

sys_id_opt = dict(name='--sys', metavar='<SYS_ID>', help='System ID')
pool_id_opt = dict(name='--pool', metavar='<POOL_ID>', help='Pool ID')
vol_id_opt = dict(name='--vol', metavar='<VOL_ID>', help='Volume ID')
fs_id_opt = dict(name='--fs', metavar='<FS_ID>', help='File system ID')
ag_id_opt = dict(name='--ag', metavar='<AG_ID>', help='Access group ID')
init_id_opt = dict(name='--init', metavar='<INIT_ID>', help='Initiator ID')
snap_id_opt = dict(name='--snap', metavar='<SNAP_ID>', help='Snapshot ID')
export_id_opt = dict(name='--export', metavar='<EXPORT_ID>', help='Export ID')

size_opt = dict(name='--size', metavar='<SIZE>', help=size_help)
access_opt = dict(name='--access', metavar='<ACCESS>', help=access_help,
                  choices=access_types, type=str.upper)

init_type_opt = dict(name="--init-type", help=initiator_id_help,
                     metavar='<INIT_TYPE>', choices=initiator_id_types,
                     type=str.upper)

cmds = (
    dict(
        name='list',
        help="List records of different types",
        args=[
            dict(name='--type',
                 help="List records of type:\n    " +
                      "\n    ".join(list_choices) +
                      "\n\nWhen listing SNAPSHOTS, it requires --fs <FS_ID>.",
                 metavar='<TYPE>',
                 choices=list_choices,
                 type=str.upper),
        ],
        optional=[
            dict(name=('-a', '--all'),
                 help='Retrieve and display in scrit friendly way with ' +
                      'all information including optional data if available',
                 default=False,
                 action='store_true'),
            dict(fs_id_opt),
        ],
    ),

    dict(
        name='job-status',
        help='Retrieve information about a job',
        args=[
            dict(name="--job", metavar="<JOB_ID>", help='job status id'),
        ],
    ),

    dict(
        name='capabilities',
        help='Retrieves array capabilities',
        args=[
            dict(sys_id_opt),
        ],
    ),

    dict(
        name='plugin-info',
        help='Retrieves plugin description and version',
    ),

    dict(
        name='volume-create',
        help='Creates a volume (logical unit)',
        args=[
            dict(name="--name", help='volume name', metavar='<NAME>'),
            dict(size_opt),
            dict(pool_id_opt),
        ],
        optional=[
            dict(name="--provisioning", help=provision_help,
                 default='DEFAULT',
                 choices=provision_types,
                 type=str.upper),
        ],
    ),

    dict(
        name='volume-delete',
        help='Deletes a volume given its id',
        args=[
            dict(vol_id_opt),
        ],
    ),

    dict(
        name='volume-resize',
        help='Resizes a volume',
        args=[
            dict(vol_id_opt),
            dict(name='--size', metavar='<NEW_SIZE>',
                 help="New size. %s" % size_help),
        ],
    ),

    dict(
        name='volume-replicate',
        help='Creates a new volume and peplicates provided volume to it.',
        args=[
            dict(vol_id_opt),
            dict(name="--name", metavar='<NEW_VOL_NAME>',
                 help='The name for New replicated volume'),
            dict(name="--rep-type", metavar='<REPL_TYPE>',
                 help=replicate_help, choices=replicate_types),
        ],
        optional=[
            dict(name="--pool",
                 help='Pool ID to contain the new volume.\nBy default, '
                      'new volume will be created in the same pool.'),
        ],
    ),

    dict(
        name='volume-replicate-range',
        help='Replicates a portion of a volume',
        args=[
            dict(name="--src-vol", metavar='<SRC_VOL_ID>',
                 help='Source volume id'),
            dict(name="--dst-vol", metavar='<DST_VOL_ID>',
                 help='Destination volume id'),
            dict(name="--rep-type", metavar='<REP_TYPE>', help=replicate_help,
                 choices=replicate_types),
            dict(name="--src-start", metavar='<SRC_START_BLK>',
                 help='Source volume start block number.\n'
                      'This is repeatable argument.',
                 action='append'),
            dict(name="--dst-start", metavar='<DST_START_BLK>',
                 help='destination volume start block number.\n'
                      'This is repeatable argument.',
                 action='append'),
            dict(name="--count", metavar='<BLK_COUNT>',
                 help='Number of blocks to replicate.\n'
                      'This is repeatable argument.',
                 action='append'),
        ],
    ),

    dict(
        name='volume-replicate-range-block-size',
        help='Size of each replicated block on a system in bytes',
        args=[
            dict(sys_id_opt),
        ],
    ),

    dict(
        name='volume-dependants',
        help='Returns True if volume has a dependant child, like replication',
        args=[
            dict(vol_id_opt),
        ],
    ),

    dict(
        name='volume-dependants-rm',
        help='Removes dependencies',
        args=[
            dict(vol_id_opt),
        ],
    ),

    dict(
        name='volume-access-group',
        help='Lists the access group(s) that have access to volume',
        args=[
            dict(vol_id_opt),
        ],
    ),

    dict(
        name='volumes-accessible-initiator',
        help='Lists the volumes that are accessible by the initiator',
        args=[
            dict(init_id_opt),
        ],
    ),


    dict(
        name='access-group-grant',
        help='Grants access to an access group to a volume, '
             'like LUN Masking',
        args=[
            dict(ag_id_opt),
            dict(vol_id_opt),
        ],
        optional=[
            dict(access_opt),
        ],
    ),

    dict(
        name='access-group-revoke',
        help='Revoke the access of certain access group to a volume',
        args=[
            dict(ag_id_opt),
            dict(vol_id_opt),
        ],
    ),

    dict(
        name='access-group-create',
        help='Create an access group',
        args=[
            dict(name='--name', metavar='<AG_NAME>',
                 help="Human readble name for access group"),
            # TODO: _client.py access_group_create should support multipal
            #       initiators when creating.
            dict(init_id_opt),
            dict(init_type_opt),
            dict(sys_id_opt),
        ],
    ),

    # TODO: Merge access_group_add() and access_group_remove() into
    #       access_group_mofidy(ag_id, new_inits, init_type, flags)
    dict(
        name='access-group-add',
        help='Add an initiator into existing access group',
        args=[
            dict(ag_id_opt),
            dict(init_id_opt),
            dict(init_type_opt),
        ],
    ),
    dict(
        name='access-group-remove',
        help='Remove an initiator from existing access group',
        args=[
            dict(ag_id_opt),
            dict(init_id_opt),
        ],
    ),

    dict(
        name='access-group-delete',
        help='Deletes an access group',
        args=[
            dict(ag_id_opt),
        ],
    ),

    dict(
        name='access-grant',
        help='Grants access to an initiator to a volume',
        args=[
            dict(init_id_opt),
            dict(init_type_opt),
            dict(vol_id_opt),
        ],
        optional=[
            dict(access_opt),
        ],
    ),

    dict(
        name='access-revoke',
        help='Removes access for an initiator to a volume',
        args=[
            dict(init_id_opt),
            dict(vol_id_opt),
        ],
    ),

    dict(
        name='access-group-volumes',
        help='Lists the volumes that the access group has'
             ' been granted access to',
        args=[
            dict(ag_id_opt),
        ],
    ),

    dict(
        name='initiators-granted-volume',
        help='Lists the initiators that have been '
             'granted access to specified volume',
        args=[
            dict(vol_id_opt),
        ],
    ),

    dict(
        name='iscsi-chap',
        help='Configures ISCSI inbound/outbound CHAP authentication',
        args=[
            dict(init_id_opt),
        ],
        optional=[
            dict(name="--in-user", metavar='<IN_USER>',
                 help='Inbound chap user name'),
            dict(name="--in-pass", metavar='<IN_PASS>',
                 help='Inbound chap password'),
            dict(name="--out-user", metavar='<OUT_USER>',
                 help='Outbound chap user name'),
            dict(name="--out-pass", metavar='<OUT_PASS>',
                 help='Outbound chap password'),
        ],
    ),

    dict(
        name='fs-create',
        help='Creates a file system',
        args=[
            dict(name="--name", metavar='<FS_NAME>',
                 help='name of the file system'),
            dict(size_opt),
            dict(pool_id_opt),
        ],
    ),

    dict(
        name='fs-delete',
        help='Delete a filesystem',
        args=[
            dict(fs_id_opt)
        ],
    ),

    dict(
        name='fs-resize',
        help='Resizes a filesystem',
        args=[
            dict(fs_id_opt),
            dict(name="--size", metavar="<NEW_SIZE>",
                 help="New size. %s" % size_help),
        ],
    ),

    dict(
        name='fs-export',
        help='Export a filesystem via NFS.',
        args=[
            dict(fs_id_opt),
        ],
        optional=[
            dict(name="--exportpath", metavar='<EXPORT_PATH>',
                 help="NFS server export path. e.g. '/foo/bar'."),
            dict(name="--anonuid", metavar='<ANONY_UID>',
                 help='UID(User ID) to map to anonymous user'),
            dict(name="--anongid", metavar='<ANONY_GID>',
                 help='GID(Group ID) to map to anonymous user'),
            dict(name="--auth-type", metavar='<AUTH_TYPE>',
                 help='NFS client authentication type'),
            dict(name="--root-host", metavar='<ROOT_HOST>',
                 help="The host/IP has root access.\n"
                      "This is repeatable argument.",
                 action='append',
                 default=[]),
            dict(name="--ro-host", metavar='<RO_HOST>',
                 help="The host/IP has readonly access.\n"
                      "This is repeatable argument.",
                 action='append', default=[]),
            dict(name="--rw-host", metavar='<RW_HOST>',
                 help="The host/IP has readwrite access.\n"
                      "This is repeatable argument.",
                 action='append', default=[]),
        ],
    ),

    dict(
        name='fs-unexport',
        help='Delete an NFS export',
        args=[
            dict(export_id_opt),
        ],
    ),

    dict(
        name='fs-clone',
        help='Creates a file system clone',
        args=[
            dict(name="--src-fs", metavar='<SRC_FS_ID>',
                 help='The ID of existing source file system.'),
            dict(name="--dst-name", metavar='<DST_FS_NAME>',
                 help='The name for newly created destination file system.'),
        ],
        optional=[
            dict(name="--backing-snapshot", metavar='<BE_SS_ID>',
                 help='backing snapshot id'),
        ],
    ),

    dict(
        name='fs-snap-create',
        help='Creates a snapshot',
        args=[
            dict(name="--name", metavar="<SNAP_NAME>",
                 help='The human friendly name of new snapshot'),
            dict(fs_id_opt),
        ],
        optional=[
            dict(name="--file", metavar="<FILE_PATH>",
                 help="Only create snapshot for provided file\n"
                      "Without this argument, all files will be snapshoted\n"
                      "This is a repeatable argument.",
                 action='append', default=[]),
        ],
    ),

    dict(
        name='fs-snap-delete',
        help='Deletes a snapshot',
        args=[
            dict(snap_id_opt),
            dict(fs_id_opt),        # TODO: why we need filesystem ID?
        ],
    ),

    dict(
        name='fs-snap-restore',
        help='Restores a FS or specified files to '
             'previous snapshot state',
        args=[
            dict(snap_id_opt),
            dict(fs_id_opt),
        ],
        optional=[
            dict(name="--file", metavar="<FILE_PATH>",
                 help="Only restore provided file\n"
                      "Without this argument, all files will be restored\n"
                      "This is a repeatable argument.",
                 action='append', default=[]),
            dict(name="--fileas", metavar="<NEW_FILE_PATH>",
                 help="store restore file name to another name.\n"
                      "This is a repeatable argument.",
                 action='append',
                 default=[]),
        ],
    ),

    dict(
        name='fs-dependants',
        help='Returns True if filesystem has a child '
             'dependency(clone/snapshot) exists',
        args=[
            dict(fs_id_opt),
        ],
        optional=[
            dict(name="--file", metavar="<FILE_PATH>",
                 action="append", default=[],
                 help="For file check\nThis is a repeatable argument."),
        ],
    ),

    dict(
        name='fs-dependants-rm',
        help='Removes filessystem dependencies',
        args=[
            dict(fs_id_opt),
        ],
        optional=[
            dict(name="--file", action='append', default=[],
                 help='File or files to remove dependencies for.\n'
                      "This is a repeatable argument.",),
        ],
    ),


    dict(
        name='file-clone',
        help='Creates a clone of a file (thin provisioned)',
        args=[
            dict(fs_id_opt),
            dict(name="--src", metavar="<SRC_FILE_PATH>",
                 help='source file to clone (relative path)\n'
                      "This is a repeatable argument.",),
            dict(name="--dst", metavar="<DST_FILE_PATH>",
                 help='destination file (relative path)'
                      "This is a repeatable argument."),
        ],
        optional=[
            dict(name="--backing-snapshot", help='backing snapshot id'),
        ],
    ),

    dict(
        name='pool-create',
        help='Creates a storage pool',
        args=[
            dict(sys_id_opt),
            dict(name="--name", metavar="<POOL_NAME>",
                 help="Human friendly name for new pool"),
            dict(size_opt),
        ],
        optional=[
            dict(name="--raid-type",  metavar='<RAID_TYPE>',
                 help=raid_help,
                 choices=raid_types,
                 type=str.upper),
            dict(name="--member-type", metavar='<MEMBER_TYPE>',
                 help=member_help,
                 choices=member_types),
            dict(name=('-o', '--optional'),
                 help='Retrieve additional optional info if available',
                 default=False,
                 action='store_true'),
        ],
    ),

    dict(
        name='pool-create-from-disks',
        help='Creates a storage pool from disks',
        args=[
            dict(sys_id_opt),
            dict(name="--name", metavar="<POOL_NAME>",
                 help="Human friendly name for new pool"),
            dict(name="--member-id", metavar='<MEMBER_ID>',
                 help='The ID of disks to create new pool\n'
                      'This is a repeatable argument',
                 action='append'),
            dict(name="--raid-type",  metavar='<RAID_TYPE>',
                 help=raid_help,
                 choices=raid_types,
                 type=str.upper),
        ],
        optional=[
            dict(name=('-o', '--optional'),
                 help='Retrieve additional optional info if available',
                 default=False,
                 action='store_true'),
        ],
    ),

    dict(
        name='pool-create-from-volumes',
        help='Creates a storage pool from volumes',
        args=[
            dict(sys_id_opt),
            dict(name="--name", metavar="<POOL_NAME>",
                 help="Human friendly name for new pool"),
            dict(name="--member-id", metavar='<MEMBER_ID>',
                 help='The ID of volumes to create new pool\n'
                      'This is a repeatable argument',
                 action='append'),
            dict(name="--raid-type",  metavar='<RAID_TYPE>',
                 help=raid_help,
                 choices=raid_types,
                 type=str.upper),
        ],
        optional=[
            dict(name=('-o', '--optional'),
                 help='Retrieve additional optional info if available',
                 default=False,
                 action='store_true'),
        ],
    ),

    dict(
        name='pool-create-from-pool',
        help='Creates a sub-pool from another storage pool',
        args=[
            dict(sys_id_opt),
            dict(name="--name", metavar="<POOL_NAME>",
                 help="Human friendly name for new pool"),
            dict(name="--member-id", metavar='<POOL_ID>',
                 help='The ID of pool to create new pool from\n',
                 action='append'),
            dict(name="--size", metavar='<SIZE>',
                 help='The size of new pool'),
        ],
        optional=[
            dict(name=('-o', '--optional'),
                 help='Retrieve additional optional info if available',
                 default=False,
                 action='store_true'),
        ],
    ),

    dict(
        name='pool-delete',
        help='Deletes a storage pool',
        args=[
            dict(pool_id_opt),
        ],
    ),
)


## Class that encapsulates the command line arguments for lsmcli
# Note: This class is used by lsmcli and any python plug-ins.
class CmdLine:
    """
    Command line interface class.
    """

    ##
    # Warn of imminent data loss
    # @param    deleting    Indicate data will be lost vs. may be lost
    #                       (re-size)
    # @return True if operation confirmed, else False
    def confirm_prompt(self, deleting):
        """
        Give the user a chance to bail.
        """
        if not self.args.force:
            msg = "will" if deleting else "may"
            out("Warning: You are about to do an operation that %s cause data "
                "to be lost!\nPress [Y|y] to continue, any other key to abort"
                % msg)

            pressed = getch()
            if pressed.upper() == 'Y':
                return True
            else:
                out('Operation aborted!')
                return False
        else:
            return True

    def _display_data_script_way(self, all_key_2_values, key_2_str, key_seq):
        """
        Display like iscsiadm do. Better for scripting.
        """
        key_column_width = 1
        value_column_width = 1
        key_sequence = []
        for key_name in key_seq:
            # Use suggested key sequence to sort
            if key_name not in all_key_2_values[0].keys():
                continue
            else:
                key_sequence.extend([key_name])

        for key_2_value in all_key_2_values:
            for key_name in key_sequence:
                # find the max column width of key
                cur_key_width = len(key_2_str[key_name])
                if cur_key_width > key_column_width:
                    key_column_width = cur_key_width
                # find the max column width of value
                cur_value = key_2_value[key_name]
                cur_value_width = 0
                if isinstance(cur_value, list):
                    if len(cur_value) == 0:
                        continue
                    cur_value_width = len(str(cur_value[0]))
                else:
                    cur_value_width = len(str(cur_value))
                if cur_value_width > value_column_width:
                    value_column_width = cur_value_width

        spliter = ' | '
        if self.args.sep is not None:
            spliter = self.args.sep
        row_format = '%%-%ds%s%%-%ds' % (key_column_width,
                                         spliter,
                                         value_column_width)
        sub_row_format = '%s%s%%-%ds' % (' ' * key_column_width,
                                         spliter,
                                         value_column_width)
        obj_spliter = '%s%s%s' % ('-' * key_column_width,
                                  '-' * len(spliter),
                                  '-' * value_column_width)

        for key_2_value in all_key_2_values:
            out(obj_spliter)
            for key_name in key_sequence:
                key_str = key_2_str[key_name]
                value = key_2_value[key_name]
                if isinstance(value, list):
                    flag_first_data = True
                    for sub_value in value:
                        if flag_first_data:
                            out(row_format % (key_str, str(sub_value)))
                            flag_first_data = False
                        else:
                            out(sub_row_format % str(sub_value))
                else:
                    out(row_format % (key_str, str(value)))
        out(obj_spliter)

    ##
    # Tries to make the output better when it varies considerably from
    # plug-in to plug-in.
    # @param    objects    Data, first row is header all other data.
    def display_data(self, objects, extra_properties=None):
        if len(objects) == 0:
            return

        if hasattr(self.args, 'all') and self.args.all:
            self.args.script = True

        flag_with_header = True
        if self.args.sep:
            flag_with_header = False
        if self.args.header:
            flag_with_header = True

        display_way = DisplayData.DISPLAY_WAY_DEFAULT
        if self.args.script:
            display_way = DisplayData.DISPLAY_WAY_SCRIPT

        flag_new_way_works = DisplayData.display_data(
            objects, display_way=display_way, flag_human=self.args.human,
            flag_enum=self.args.enum, extra_properties=extra_properties,
            spliter=self.args.sep, flag_with_header=flag_with_header,
            flag_dsp_all_data=self.args.all)

        if flag_new_way_works:
            return

        # Assuming all objects are from the same class.
        key_2_str = objects[0]._str_of_key()
        key_seq = objects[0]._key_display_sequence()
        all_key_2_values = []
        for obj in objects:
            obj_key_2_value = obj._value_of_key(
                key_name=None,
                human=self.args.human,
                enum_as_number=self.args.enum,
                list_convert=False)
            for key in obj_key_2_value.keys():
                if obj_key_2_value[key] is None:
                    del obj_key_2_value[key]
            all_key_2_values.extend([obj_key_2_value])

        if self.args.script:
            self._display_data_script_way(all_key_2_values,
                                          key_2_str,
                                          key_seq)
        else:
            two_d_list = CmdLine._convert_to_two_d_list(all_key_2_values,
                                                        key_2_str,
                                                        key_seq)
            self._display_two_d_list(two_d_list)

    @staticmethod
    def _convert_to_two_d_list(all_key_2_values, key_2_str, key_seq):
        two_d_list = []
        key_sequence = []
        for key_name in key_seq:
            # Use suggested key sequence to sort
            if key_name in all_key_2_values[0].keys():
                key_sequence.extend([key_name])
        column_width = len(key_sequence)

        # find out column width
        row_width = 0
        for key_2_value in all_key_2_values:
            cur_max_wd = 0
            for key_name in key_2_value.keys():
                if isinstance(key_2_value[key_name], list):
                    cur_row_width = len(key_2_value[key_name])
                    if cur_row_width > cur_max_wd:
                        cur_max_wd = cur_row_width
                else:
                    pass
            if cur_max_wd == 0:
                cur_max_wd = 1
            row_width += cur_max_wd
        # one line for header
        row_width += 1
        # init 2D list
        for raw in range(0, row_width):
            new = []
            for column in range(0, column_width):
                new.append('')
            two_d_list.append(new)
        # header
        for index in range(0, len(key_sequence)):
            key_name = key_sequence[index]
            two_d_list[0][index] = key_2_str[key_name]

        current_row_num = 0
        for key_2_value in all_key_2_values:
            current_row_num += 1
            save_row_num = current_row_num
            for index in range(0, len(key_sequence)):
                key_name = key_sequence[index]
                value = key_2_value[key_name]
                if isinstance(value, list):
                    for sub_index in range(0, len(value)):
                        tmp_row_num = save_row_num + sub_index
                        two_d_list[tmp_row_num][index] = str(value[sub_index])

                    if save_row_num + len(value) > current_row_num:
                        current_row_num = save_row_num + len(value) - 1
                else:
                    two_d_list[save_row_num][index] = str(value)
        return two_d_list

    def _display_two_d_list(self, two_d_list):
        row_formats = []
        spliter_row_formats = []
        row_start = 0
        if self.args.sep is not None:
            row_start = 1
            if self.args.header:
                row_start = 0
        for column_index in range(0, len(two_d_list[0])):
            max_width = CmdLine._find_max_width(
                two_d_list,
                column_index,
                row_start)
            row_formats.extend(['%%-%ds' % max_width])
            spliter_row_formats.extend(['-' * max_width])
        row_format = ''
        if self.args.sep is not None:
            row_format = self.args.sep.join(row_formats)
        else:
            row_format = " | ".join(row_formats)
        for row_index in range(row_start, len(two_d_list)):
            out(row_format % tuple(two_d_list[row_index]))
            if row_index == 0:
                spliter = '-+-'.join(spliter_row_formats)
                out(spliter)

    @staticmethod
    def _find_max_width(two_d_list, column_index, row_start=0):
        max_width = 1
        for row_index in range(row_start, len(two_d_list)):
            row_data = two_d_list[row_index]
            if len(row_data[column_index]) > max_width:
                max_width = len(row_data[column_index])
        return max_width

    def display_available_plugins(self):
        d = []
        sep = '<}{>'
        plugins = Client.get_available_plugins(sep)

        for p in plugins:
            desc, version = p.split(sep)
            d.append(PlugData(desc, version))

        self.display_data(d)

    ## All the command line arguments and options are created in this method
    def cli(self):
        """
        Command line interface parameters
        """
        parent_parser = ArgumentParser(add_help=False)

        parent_parser.add_argument(
            '-v', '--version', action='version',
            version="%s %s" % (sys.argv[0], VERSION))

        parent_parser.add_argument(
            '-u', '--uri', action="store", type=str, metavar='<URI>',
            dest="uri", help='Uniform resource identifier (env LSMCLI_URI)')

        parent_parser.add_argument(
            '-P', '--prompt', action="store_true", dest="prompt",
            help='Prompt for password (env LSMCLI_PASSWORD)')

        parent_parser.add_argument(
            '-H', '--human', action="store_true", dest="human",
            help='Print sizes in human readable format\n'
                 '(e.g., MiB, GiB, TiB)')

        parent_parser.add_argument(
            '-t', '--terse', action="store", dest="sep", metavar='<SEP>',
            help='Print output in terse form with "SEP" '
                 'as a record separator')

        parent_parser.add_argument(
            '-e', '--enum', action="store_true", dest="enum", default=False,
            help='Display enumerated types as numbers instead of text')

        parent_parser.add_argument(
            '-f', '--force', action="store_true", dest="force", default=False,
            help='Bypass confirmation prompt for data loss operations')

        parent_parser.add_argument(
            '-w', '--wait', action="store", type=int, dest="wait",
            default=30000, help="Command timeout value in ms (default = 30s)")

        parent_parser.add_argument(
            '--header', action="store_true", dest="header",
            help='Include the header with terse')

        parent_parser.add_argument(
            '-b', action="store_true", dest="async", default=False,
            help='Run the command async. Instead of waiting for completion.\n '
                 'Command will exit(7) and job id written to stdout.')

        parent_parser.add_argument(
            '-s', '--script', action="store_true", dest="script",
            default=False, help='Displaying data in script friendly way.')

        parser = ArgumentParser(
            description='The libStorageMgmt command line interface.'
                        ' Run %(prog)s <command> -h for more on each command.',
            epilog='Copyright 2012-2013 Red Hat, Inc.\n'
                   'Please report bugs to '
                   '<libstoragemgmt-devel@lists.sourceforge.net>\n',
            formatter_class=RawTextHelpFormatter,
            parents=[parent_parser])

        subparsers = parser.add_subparsers(metavar="command")

        # Walk the command list and add all of them to the parser
        for cmd in cmds:
            sub_parser = subparsers.add_parser(
                cmd['name'], help=cmd['help'], parents=[parent_parser],
                formatter_class=RawTextHelpFormatter)

            group = sub_parser.add_argument_group("cmd required arguments")
            for arg in cmd.get('args', []):
                name = arg['name']
                del arg['name']
                group.add_argument(name, required=True, **arg)

            group = sub_parser.add_argument_group("cmd optional arguments")
            for arg in cmd.get('optional', []):
                flags = arg['name']
                del arg['name']
                if not isinstance(flags, tuple):
                    flags = (flags,)
                group.add_argument(*flags, **arg)

            sub_parser.set_defaults(
                func=getattr(self, cmd['name'].replace("-", "_")))

        return parser.parse_args()

    def _list(self, l):
        if l and len(l):
            if self.args.sep:
                return self.args.sep.join(l)
            else:
                return ", ".join(l)
        else:
            return "None"

    ## Display the types of nfs client authentication that are supported.
    # @return None
    def display_nfs_client_authentication(self):
        """
        Dump the supported nfs client authentication types
        """
        if self.args.sep:
            out(self.args.sep.join(self.c.export_auth()))
        else:
            out(", ".join(self.c.export_auth()))

    ## Method that calls the appropriate method based on what the list type is
    # @param    args    Argparse argument object
    def list(self, args):
        if args.type == 'VOLUMES':
            self.display_data(self.c.volumes())
        elif args.type == 'POOLS':
            if args.all:
                self.display_data(
                    self.c.pools(Pool.RETRIEVE_FULL_INFO))
            else:
                self.display_data(self.c.pools())
        elif args.type == 'FS':
            self.display_data(self.c.fs())
        elif args.type == 'SNAPSHOTS':
            if args.fs is None:
                raise ArgError("--fs <file system id> required")

            fs = _get_item(self.c.fs(), args.fs, 'filesystem')
            self.display_data(self.c.fs_snapshots(fs))
        elif args.type == 'INITIATORS':
            self.display_data(self.c.initiators())
        elif args.type == 'EXPORTS':
            self.display_data(self.c.exports())
        elif args.type == 'NFS_CLIENT_AUTH':
            self.display_nfs_client_authentication()
        elif args.type == 'ACCESS_GROUPS':
            self.display_data(self.c.access_group_list())
        elif args.type == 'SYSTEMS':
            self.display_data(self.c.systems())
        elif args.type == 'DISKS':
            if args.all:
                self.display_data(
                    self.c.disks(Disk.RETRIEVE_FULL_INFO))
            else:
                self.display_data(self.c.disks())
        elif args.type == 'PLUGINS':
            self.display_available_plugins()
        else:
            raise ArgError("unsupported listing type=%s" % args.type)

    ## Converts type initiator type to enumeration type.
    # @param    type    String representation of type
    # @returns  Enumerated value
    @staticmethod
    def _init_type_to_enum(init_type):
        if init_type == 'WWPN':
            i = Initiator.TYPE_PORT_WWN
        elif init_type == 'WWNN':
            i = Initiator.TYPE_NODE_WWN
        elif init_type == 'ISCSI':
            i = Initiator.TYPE_ISCSI
        elif init_type == 'HOSTNAME':
            i = Initiator.TYPE_HOSTNAME
        elif init_type == 'SAS':
            i = Initiator.TYPE_SAS
        else:
            raise ArgError("invalid initiator type " + init_type)
        return i

    ## Creates an access group.
    def access_group_create(self, args):
        i = CmdLine._init_type_to_enum(args.init_type)
        access_group = self.c.access_group_create(args.name, args.init, i,
                                                  args.sys)
        self.display_data([access_group])

    def _add_rm_access_grp_init(self, args, op):
        agl = self.c.access_group_list()
        group = _get_item(agl, args.ag, "access group id")

        if op:
            i = CmdLine._init_type_to_enum(args.init_type)
            self.c.access_group_add_initiator(
                group, args.init, i)
        else:
            i = _get_item(self.c.initiators(), args.init, "initiator id")
            self.c.access_group_del_initiator(group, i.id)

    ## Adds an initiator from an access group
    def access_group_add(self, args):
        self._add_rm_access_grp_init(args, True)

    ## Removes an initiator from an access group
    def access_group_remove(self, args):
        self._add_rm_access_grp_init(args, False)

    def access_group_volumes(self, args):
        agl = self.c.access_group_list()
        group = _get_item(agl, args.ag, "access group id")
        vols = self.c.volumes_accessible_by_access_group(group)
        self.display_data(vols)

    def volumes_accessible_initiator(self, args):
        i = _get_item(self.c.initiators(), args.init, "initiator id")
        volumes = self.c.volumes_accessible_by_initiator(i)
        self.display_data(volumes)

    def initiators_granted_volume(self, args):
        vol = _get_item(self.c.volumes(), args.vol, "volume id")
        initiators = self.c.initiators_granted_to_volume(vol)
        self.display_data(initiators)

    def iscsi_chap(self, args):
        init = _get_item(self.c.initiators(), args.init, "initiator id")
        self.c.iscsi_chap_auth(init, args.in_user,
                               self.args.in_pass,
                               self.args.out_user,
                               self.args.out_pass)

    def volume_access_group(self, args):
        vol = _get_item(self.c.volumes(), args.vol, "volume id")
        groups = self.c.access_groups_granted_to_volume(vol)
        self.display_data(groups)

    ## Used to delete access group
    def access_group_delete(self, args):
        agl = self.c.access_group_list()
        group = _get_item(agl, args.ag, "access group id")
        return self.c.access_group_del(group)

    ## Used to delete a file system
    def fs_delete(self, args):
        fs = _get_item(self.c.fs(), args.fs, "filesystem id")
        if self.confirm_prompt(True):
            self._wait_for_it("fs-delete", self.c.fs_delete(fs), None)

    ## Used to create a file system
    def fs_create(self, args):
        p = _get_item(self.c.pools(), args.pool, "pool id")
        fs = self._wait_for_it("fs-create",
                               *self.c.fs_create(p, args.name,
                                                 self._size(args.size)))
        self.display_data([fs])

    ## Used to resize a file system
    def fs_resize(self, args):
        fs = _get_item(self.c.fs(), args.fs, "filesystem id")
        size = self._size(args.size)

        if self.confirm_prompt(False):
            fs = self._wait_for_it("fs-resize",
                                   *self.c.fs_resize(fs, size))
            self.display_data([fs])

    ## Used to clone a file system
    def fs_clone(self, args):
        src_fs = _get_item(
            self.c.fs(), args.src_fs, "source file system id")

        ss = None
        if args.backing_snapshot:
            #go get the snapsnot
            ss = _get_item(self.c.fs_snapshots(src_fs),
                           args.backing_snapshot, "snapshot id")

        fs = self._wait_for_it(
            "fs_clone", *self.c.fs_clone(src_fs, args.dst_name, ss))
        self.display_data([fs])

    ## Used to clone a file(s)
    def file_clone(self, args):
        fs = _get_item(self.c.fs(), args.fs, "filesystem id")

        if self.args.backing_snapshot:
            #go get the snapsnot
            ss = _get_item(self.c.fs_snapshots(fs),
                           args.backing_snapshot, "snapshot id")
        else:
            ss = None

        self._wait_for_it(
            "file_clone", self.c.file_clone(fs, args.src, args.dst, ss), None)

    ##Converts a size parameter into the appropriate number of bytes
    # @param    s   Size to convert to bytes handles B, K, M, G, T, P postfix
    # @return Size in bytes
    @staticmethod
    def _size(s):
        size_bytes = size_human_2_size_bytes(s)
        if size_bytes <= 0:
            raise ArgError("Incorrect size argument format: '%s'" % s)
        return size_bytes

    def _cp(self, cap, val):
        if self.args.sep is not None:
            s = self.args.sep
        else:
            s = ':'

        if val == Capabilities.SUPPORTED:
            v = "SUPPORTED"
        elif val == Capabilities.UNSUPPORTED:
            v = "UNSUPPORTED"
        elif val == Capabilities.SUPPORTED_OFFLINE:
            v = "SUPPORTED_OFFLINE"
        elif val == Capabilities.NOT_IMPLEMENTED:
            v = "NOT_IMPLEMENTED"
        else:
            v = "UNKNOWN"

        out("%s%s%s" % (cap, s, v))

    def capabilities(self, args):
        s = _get_item(self.c.systems(), args.sys, "system id")

        cap = self.c.capabilities(s)
        self._cp("BLOCK_SUPPORT", cap.get(Capabilities.BLOCK_SUPPORT))
        self._cp("FS_SUPPORT", cap.get(Capabilities.FS_SUPPORT))
        self._cp("INITIATORS", cap.get(Capabilities.INITIATORS))
        self._cp("INITIATORS_GRANTED_TO_VOLUME",
                 cap.get(Capabilities.INITIATORS_GRANTED_TO_VOLUME))
        self._cp("VOLUMES", cap.get(Capabilities.VOLUMES))
        self._cp("VOLUME_CREATE", cap.get(Capabilities.VOLUME_CREATE))
        self._cp("VOLUME_RESIZE", cap.get(Capabilities.VOLUME_RESIZE))
        self._cp("VOLUME_REPLICATE",
                 cap.get(Capabilities.VOLUME_REPLICATE))
        self._cp("VOLUME_REPLICATE_CLONE",
                 cap.get(Capabilities.VOLUME_REPLICATE_CLONE))
        self._cp("VOLUME_REPLICATE_COPY",
                 cap.get(Capabilities.VOLUME_REPLICATE_COPY))
        self._cp("VOLUME_REPLICATE_MIRROR_ASYNC",
                 cap.get(Capabilities.VOLUME_REPLICATE_MIRROR_ASYNC))
        self._cp("VOLUME_REPLICATE_MIRROR_SYNC",
                 cap.get(Capabilities.VOLUME_REPLICATE_MIRROR_SYNC))
        self._cp("VOLUME_COPY_RANGE_BLOCK_SIZE",
                 cap.get(Capabilities.VOLUME_COPY_RANGE_BLOCK_SIZE))
        self._cp("VOLUME_COPY_RANGE",
                 cap.get(Capabilities.VOLUME_COPY_RANGE))
        self._cp("VOLUME_COPY_RANGE_CLONE",
                 cap.get(Capabilities.VOLUME_COPY_RANGE_CLONE))
        self._cp("VOLUME_COPY_RANGE_COPY",
                 cap.get(Capabilities.VOLUME_COPY_RANGE_COPY))
        self._cp("VOLUME_DELETE", cap.get(Capabilities.VOLUME_DELETE))
        self._cp("VOLUME_ONLINE", cap.get(Capabilities.VOLUME_ONLINE))
        self._cp("VOLUME_OFFLINE", cap.get(Capabilities.VOLUME_OFFLINE))
        self._cp("VOLUME_INITIATOR_GRANT",
                 cap.get(Capabilities.VOLUME_INITIATOR_GRANT))
        self._cp("VOLUME_INITIATOR_REVOKE",
                 cap.get(Capabilities.VOLUME_INITIATOR_REVOKE))
        self._cp("VOLUME_THIN",
                 cap.get(Capabilities.VOLUME_THIN))
        self._cp("VOLUME_ISCSI_CHAP_AUTHENTICATION",
                 cap.get(Capabilities.VOLUME_ISCSI_CHAP_AUTHENTICATION))
        self._cp("ACCESS_GROUP_GRANT",
                 cap.get(Capabilities.ACCESS_GROUP_GRANT))
        self._cp("ACCESS_GROUP_REVOKE",
                 cap.get(Capabilities.ACCESS_GROUP_REVOKE))
        self._cp("ACCESS_GROUP_LIST",
                 cap.get(Capabilities.ACCESS_GROUP_LIST))
        self._cp("ACCESS_GROUP_CREATE",
                 cap.get(Capabilities.ACCESS_GROUP_CREATE))
        self._cp("ACCESS_GROUP_DELETE",
                 cap.get(Capabilities.ACCESS_GROUP_DELETE))
        self._cp("ACCESS_GROUP_ADD_INITIATOR",
                 cap.get(Capabilities.ACCESS_GROUP_ADD_INITIATOR))
        self._cp("ACCESS_GROUP_DEL_INITIATOR",
                 cap.get(Capabilities.ACCESS_GROUP_DEL_INITIATOR))
        self._cp("VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP",
                 cap.get(Capabilities.VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP))
        self._cp("VOLUME_ACCESSIBLE_BY_INITIATOR",
                 cap.get(Capabilities.VOLUME_ACCESSIBLE_BY_INITIATOR))
        self._cp("ACCESS_GROUPS_GRANTED_TO_VOLUME",
                 cap.get(Capabilities.ACCESS_GROUPS_GRANTED_TO_VOLUME))
        self._cp("VOLUME_CHILD_DEPENDENCY",
                 cap.get(Capabilities.VOLUME_CHILD_DEPENDENCY))
        self._cp("VOLUME_CHILD_DEPENDENCY_RM",
                 cap.get(Capabilities.VOLUME_CHILD_DEPENDENCY_RM))
        self._cp("FS", cap.get(Capabilities.FS))
        self._cp("FS_DELETE", cap.get(Capabilities.FS_DELETE))
        self._cp("FS_RESIZE", cap.get(Capabilities.FS_RESIZE))
        self._cp("FS_CREATE", cap.get(Capabilities.FS_CREATE))
        self._cp("FS_CLONE", cap.get(Capabilities.FS_CLONE))
        self._cp("FILE_CLONE", cap.get(Capabilities.FILE_CLONE))
        self._cp("FS_SNAPSHOTS", cap.get(Capabilities.FS_SNAPSHOTS))
        self._cp("FS_SNAPSHOT_CREATE",
                 cap.get(Capabilities.FS_SNAPSHOT_CREATE))
        self._cp("FS_SNAPSHOT_CREATE_SPECIFIC_FILES",
                 cap.get(Capabilities.FS_SNAPSHOT_CREATE_SPECIFIC_FILES))
        self._cp("FS_SNAPSHOT_DELETE",
                 cap.get(Capabilities.FS_SNAPSHOT_DELETE))
        self._cp("FS_SNAPSHOT_REVERT",
                 cap.get(Capabilities.FS_SNAPSHOT_REVERT))
        self._cp("FS_SNAPSHOT_REVERT_SPECIFIC_FILES",
                 cap.get(Capabilities.FS_SNAPSHOT_REVERT_SPECIFIC_FILES))
        self._cp("FS_CHILD_DEPENDENCY",
                 cap.get(Capabilities.FS_CHILD_DEPENDENCY))
        self._cp("FS_CHILD_DEPENDENCY_RM",
                 cap.get(Capabilities.FS_CHILD_DEPENDENCY_RM))
        self._cp("FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES", cap.get(
                 Capabilities.FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES))
        self._cp("EXPORT_AUTH", cap.get(Capabilities.EXPORT_AUTH))
        self._cp("EXPORTS", cap.get(Capabilities.EXPORTS))
        self._cp("EXPORT_FS", cap.get(Capabilities.EXPORT_FS))
        self._cp("EXPORT_REMOVE", cap.get(Capabilities.EXPORT_REMOVE))
        self._cp("EXPORT_CUSTOM_PATH",
                 cap.get(Capabilities.EXPORT_CUSTOM_PATH))

    def plugin_info(self, args):
        desc, version = self.c.plugin_info()

        if args.sep:
            out("%s%s%s" % (desc, args.sep, version))
        else:
            out("Description: %s Version: %s" % (desc, version))

    ## Creates a volume
    def volume_create(self, args):
        #Get pool
        p = _get_item(self.c.pools(), args.pool, "pool id")
        vol = self._wait_for_it(
            "volume-create",
            *self.c.volume_create(
                p,
                args.name,
                self._size(args.size),
                Volume._prov_string_to_type(args.provisioning)))
        self.display_data([vol])

    ## Creates a snapshot
    def fs_snap_create(self, args):
        #Get fs
        fs = _get_item(self.c.fs(), args.fs, "fs id")
        ss = self._wait_for_it("snapshot-create",
                               *self.c.fs_snapshot_create(
                                   fs,
                                   args.name,
                                   self.args.file))

        self.display_data([ss])

    ## Restores a snap shot
    def fs_snap_restore(self, args):
        #Get snapshot
        fs = _get_item(self.c.fs(), args.fs, "fs id")
        ss = _get_item(self.c.fs_snapshots(fs), args.snap, "snapshot id")

        flag_all_files = True

        if self.args.file:
            flag_all_files = False
            if self.args.fileas:
                if len(self.args.file) != len(self.args.fileas):
                    raise ArgError(
                        "number of --file not equal to --fileas")

        if self.confirm_prompt(True):
            self._wait_for_it(
                'fs-snap-restore',
                self.c.fs_snapshot_revert(
                    fs, ss, self.args.file, self.args.fileas, flag_all_files),
                None)

    ## Deletes a volume
    def volume_delete(self, args):
        v = _get_item(self.c.volumes(), args.vol, "volume id")
        if self.confirm_prompt(True):
            self._wait_for_it("volume-delete", self.c.volume_delete(v),
                              None)

    ## Deletes a snap shot
    def fs_snap_delete(self, args):
        fs = _get_item(self.c.fs(), args.fs, "filesystem id")
        ss = _get_item(self.c.fs_snapshots(fs), args.snap, "snapshot id")

        if self.confirm_prompt(True):
            self._wait_for_it("fs_snap_delete",
                              self.c.fs_snapshot_delete(fs, ss), None)

    ## Waits for an operation to complete by polling for the status of the
    # operations.
    # @param    msg     Message to display if this job fails
    # @param    job     The job id to wait on
    # @param    item    The item that could be available now if there is no job
    def _wait_for_it(self, msg, job, item):
        if not job:
            return item
        else:
            #If a user doesn't want to wait, return the job id to stdout
            #and exit with job in progress
            if self.args.async:
                out(job)
                self.shutdown(ErrorNumber.JOB_STARTED)

            while True:
                (s, percent, i) = self.c.job_status(job)

                if s == JobStatus.INPROGRESS:
                    #Add an option to spit out progress?
                    #print "%s - Percent %s complete" % (job, percent)
                    time.sleep(0.25)
                elif s == JobStatus.COMPLETE:
                    self.c.job_free(job)
                    return i
                else:
                    #Something better to do here?
                    raise ArgError(msg + " job error code= " + str(s))

    ## Retrieves the status of the specified job
    def job_status(self, args):
        (s, percent, i) = self.c.job_status(args.job)

        if s == JobStatus.COMPLETE:
            if i:
                self.display_data([i])

            self.c.job_free(args.job)
        else:
            out(str(percent))
            self.shutdown(ErrorNumber.JOB_STARTED)

    ## Replicates a volume
    def volume_replicate(self, args):
        p = None
        if args.pool:
            p = _get_item(self.c.pools(), args.pool, "pool id")

        v = _get_item(self.c.volumes(), args.vol, "volume id")

        rep_type = Volume._rep_string_to_type(args.rep_type)
        if rep_type == Volume.REPLICATE_UNKNOWN:
            raise ArgError("invalid replication type= %s" % rep_type)

        vol = self._wait_for_it(
            "replicate volume",
            *self.c.volume_replicate(p, rep_type, v, args.name))
        self.display_data([vol])

    ## Replicates a range of a volume
    def volume_replicate_range(self, args):
        src = _get_item(self.c.volumes(), args.src_vol, "source volume id")
        dst = _get_item(self.c.volumes(), args.dst_vol,
                        "destination volume id")

        rep_type = Volume._rep_string_to_type(args.rep_type)
        if rep_type == Volume.REPLICATE_UNKNOWN:
            raise ArgError("invalid replication type= %s" % rep_type)

        src_starts = args.src_start
        dst_starts = args.dst_start
        counts = args.count

        if not len(src_starts) \
                or not (len(src_starts) == len(dst_starts) == len(counts)):
            raise ArgError("Differing numbers of src_start, dest_start, "
                           "and count parameters")

        ranges = []
        for i in range(len(src_starts)):
            ranges.append(BlockRange(src_starts[i], dst_starts[i], counts[i]))

        if self.confirm_prompt(False):
            self.c.volume_replicate_range(rep_type, src, dst, ranges)

    ##
    # Returns the block size in bytes for each block represented in
    # volume_replicate_range
    def volume_replicate_range_block_size(self, args):
        s = _get_item(self.c.systems(), args.sys, "system id")
        out(self.c.volume_replicate_range_block_size(s))

    ## Used to grant or revoke access to a volume to an initiator.
    # @param    grant   bool, if True we grant, else we un-grant.
    def _access(self, grant, args):
        v = _get_item(self.c.volumes(), args.vol, "volume id")
        initiator_id = args.init

        if grant:
            i_type = CmdLine._init_type_to_enum(args.init_type)
            access = 'DEFAULT'
            if args.access is not None:
                access = Volume._access_string_to_type(args.access)

            self.c.initiator_grant(initiator_id, i_type, v, access)
        else:
            initiator = _get_item(self.c.initiators(), initiator_id,
                                  "initiator id")

            self.c.initiator_revoke(initiator, v)

    ## Grant access to volume to an initiator
    def access_grant(self, args):
        return self._access(True, args)

    ## Revoke access to volume to an initiator
    def access_revoke(self, args):
        return self._access(False, args)

    def _access_group(self, args, grant=True):
        agl = self.c.access_group_list()
        group = _get_item(agl, args.ag, "access group id")
        v = _get_item(self.c.volumes(), args.vol, "volume id")

        if grant:
            access = 'RW'
            if args.access is not None:
                access = args.access
            access = Volume._access_string_to_type(args.access)
            self.c.access_group_grant(group, v, access)
        else:
            self.c.access_group_revoke(group, v)

    def access_group_grant(self, args):
        return self._access_group(args, grant=True)

    def access_group_revoke(self, args):
        return self._access_group(args, grant=False)

    ## Re-sizes a volume
    def volume_resize(self, args):
        v = _get_item(self.c.volumes(), args.vol, "volume id")
        size = self._size(args.size)

        if self.confirm_prompt(False):
            vol = self._wait_for_it("resize",
                                    *self.c.volume_resize(v, size))
            self.display_data([vol])

    ## Removes a nfs export
    def fs_unexport(self, args):
        export = _get_item(self.c.exports(), args.export, "nfs export id")
        self.c.export_remove(export)

    ## Exports a file system as a NFS export
    def fs_export(self, args):
        fs = _get_item(self.c.fs(), args.fs, "file system id")

        # Check to see if we have some type of access specified
        if len(args.rw_host) == 0 \
                and len(args.ro_host) == 0:
            raise ArgError(" please specify --ro-host or --rw-host")

        export = self.c.export_fs(
            fs.id,
            args.exportpath,
            args.root_host,
            args.rw_host,
            args.ro_host,
            args.anonuid,
            args.anongid,
            args.auth_type,
            None)
        self.display_data([export])

    ## Displays volume dependants.
    def volume_dependants(self, args):
        v = _get_item(self.c.volumes(), args.vol, "volume id")
        rc = self.c.volume_child_dependency(v)
        out(rc)

    ## Removes volume dependants.
    def volume_dependants_rm(self, args):
        v = _get_item(self.c.volumes(), args.vol, "volume id")
        self._wait_for_it("volume-dependant-rm",
                          self.c.volume_child_dependency_rm(v), None)

    ## Displays file system dependants
    def fs_dependants(self, args):
        fs = _get_item(self.c.fs(), args.fs, "file system id")
        rc = self.c.fs_child_dependency(fs, args.file)
        out(rc)

    ## Removes file system dependants
    def fs_dependants_rm(self, args):
        fs = _get_item(self.c.fs(), args.fs, "file system id")
        self._wait_for_it("fs-dependants-rm",
                          self.c.fs_child_dependency_rm(fs,
                                                        args.file),
                          None)

    ## Deletes a pool
    def pool_delete(self, args):
        pool = _get_item(self.c.pools(), args.pool, "pool id")
        if self.confirm_prompt(True):
            self._wait_for_it("pool-delete",
                              self.c.pool_delete(pool),
                              None)

    ## Creates a pool
    def pool_create(self, args):
        pool_name = args.name
        raid_type = Pool.RAID_TYPE_UNKNOWN
        member_type = Pool.MEMBER_TYPE_UNKNOWN
        size_bytes = self._size(self.args.size)

        if args.raid_type:
            raid_type = Pool._raid_type_str_to_type(
                self.args.raid_type)
            if raid_type == Pool.RAID_TYPE_UNKNOWN:
                raise ArgError("Unknown RAID type specified: %s" %
                               args.raid_type)

        if args.member_type:
            member_type = Pool._member_type_str_to_type(
                args.member_type)
            if member_type == Pool.MEMBER_TYPE_UNKNOWN:
                raise ArgError("Unkonwn member type specified: %s" %
                               args.member_type)

        pool = self._wait_for_it("pool-create",
                                 *self.c.pool_create(self.args.sys,
                                                     pool_name,
                                                     size_bytes,
                                                     raid_type,
                                                     member_type,
                                                     0))
        self.display_data([pool])

    def pool_create_from_disks(self, args):
        if len(args.member_id) <= 0:
            raise ArgError("No disk ID was provided for new pool")

        member_ids = args.member_id
        disks = self.c.disks()
        disk_ids = [d.id for d in disks]
        for member_id in member_ids:
            if member_id not in disk_ids:
                raise ArgError("Invalid Disk ID specified in " +
                               "--member-id %s " % member_id)

        raid_type = Pool._raid_type_str_to_type(
            self.args.raid_type)
        if raid_type == Pool.RAID_TYPE_UNKNOWN:
            raise ArgError("Unknown RAID type specified: %s" %
                           self.args.raid_type)

        pool_name = args.name
        pool = self._wait_for_it(
            "pool-create-from-disks",
            *self.c.pool_create_from_disks(
                self.args.sys, pool_name, member_ids, raid_type, 0))
        self.display_data([pool])

    def pool_create_from_volumes(self, args):
        if len(args.member_id) <= 0:
            raise ArgError("No volume ID was provided for new pool")

        member_ids = args.member_id
        volumes = self.c.volumes()
        vol_ids = [v.id for v in volumes]
        for member_id in member_ids:
            if member_id not in vol_ids:
                raise ArgError("Invalid volumes ID specified in " +
                               "--member-id %s " % member_id)

        raid_type = Pool._raid_type_str_to_type(
            self.args.raid_type)
        if raid_type == Pool.RAID_TYPE_UNKNOWN:
            raise ArgError("Unknown RAID type specified: %s" %
                           self.args.raid_type)

        pool_name = args.name
        pool = self._wait_for_it(
            "pool-create-from-volumes",
            *self.c.pool_create_from_volumes(
                self.args.sys, pool_name, member_ids, raid_type, 0))
        self.display_data([pool])

    def pool_create_from_pool(self, args):
        if len(args.member_id) <= 0:
            raise ArgError("No volume ID was provided for new pool")

        member_ids = args.member_id
        if len(member_ids) > 1:
            raise ArgError("Two or more member defined, but creating pool " +
                           "from pool only allow one member pool")

        member_id = member_ids[0]

        pools = self.c.pools()
        pool_ids = [p.id for p in pools]
        if member_id not in pool_ids:
            raise ArgError("Invalid pools ID specified in " +
                           "--member-id %s " % member_id)

        size_bytes = self._size(self.args.size)

        pool_name = args.name
        pool = self._wait_for_it(
            "pool-create-from-pool",
            *self.c.pool_create_from_pool(
                self.args.sys, pool_name, member_id, size_bytes, 0))
        self.display_data([pool])

    def _read_configfile(self):
        """
        Set uri from config file. Will be overridden by cmdline option or
        env var if present.
        """

        allowed_config_options = ("uri",)

        config_path = os.path.expanduser("~") + "/.lsmcli"
        if not os.path.exists(config_path):
            return

        with open(config_path) as f:
            for line in f:

                if line.lstrip().startswith("#"):
                    continue

                try:
                    name, val = [x.strip() for x in line.split("=", 1)]
                    if name in allowed_config_options:
                        setattr(self, name, val)
                except ValueError:
                    pass

    ## Class constructor.
    def __init__(self):
        self.uri = None
        self.c = None
        self.args = self.cli()

        self.cleanup = None

        self.tmo = int(self.args.wait)
        if not self.tmo or self.tmo < 0:
            raise ArgError("[-w|--wait] reguires a non-zero positive integer")

        self._read_configfile()
        if os.getenv('LSMCLI_URI') is not None:
            self.uri = os.getenv('LSMCLI_URI')
        self.password = os.getenv('LSMCLI_PASSWORD')
        if self.args.uri is not None:
            self.uri = self.args.uri

        if self.uri is None:
            # We need a valid plug-in to instantiate even if all we are trying
            # to do is list the plug-ins at the moment to keep that code
            # the same in all cases, even though it isn't technically
            # required for the client library (static method)
            # TODO: Make this not necessary.
            if (self.args.type == "PLUGINS"):
                self.uri = "sim://"
                self.password = None
            else:
                raise ArgError("--uri missing or export LSMCLI_URI")

        # Lastly get the password if requested.
        if self.args.prompt:
            self.password = getpass.getpass()

        if self.password is not None:
            #Check for username
            u = uri_parse(self.uri)
            if u['username'] is None:
                raise ArgError("password specified with no user name in uri")

    ## Does appropriate clean-up
    # @param    ec      The exit code
    def shutdown(self, ec=None):
        if self.cleanup:
            self.cleanup()

        if ec:
            sys.exit(ec)

    ## Process the specified command
    # @param    cli     The object instance to invoke methods on.
    def process(self, cli=None):
        """
        Process the parsed command.
        """
        if cli:
            #Directly invoking code though a wrapper to catch unsupported
            #operations.
            self.c = Proxy(cli())
            self.c.startup(self.uri, self.password, self.tmo)
            self.cleanup = self.c.shutdown
        else:
            #Going across the ipc pipe
            self.c = Proxy(Client(self.uri, self.password, self.tmo))

            if os.getenv('LSM_DEBUG_PLUGIN'):
                raw_input(
                    "Attach debugger to plug-in, press <return> when ready...")

            self.cleanup = self.c.close

        self.args.func(self.args)
        self.shutdown()
