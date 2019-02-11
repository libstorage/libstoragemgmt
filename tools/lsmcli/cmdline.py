# Copyright (C) 2012-2016 Red Hat, Inc.
# (C) Copyright 2017 Hewlett Packard Enterprise Development LP
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
#         Gris Ge <fge@redhat.com>

import os
import sys
import getpass
import re
import time
import tty
import termios
from argparse import ArgumentParser, ArgumentTypeError
from argparse import RawTextHelpFormatter
import six
from lsm import (Client, Pool, VERSION, LsmError, Disk,
                 Volume, JobStatus, ErrorNumber, BlockRange,
                 uri_parse, Proxy, size_human_2_size_bytes,
                 AccessGroup, FileSystem, NfsExport, TargetPort, LocalDisk,
                 Battery)

from lsm.lsmcli.data_display import (
    DisplayData, PlugData, out,
    vol_provision_str_to_type, vol_rep_type_str_to_type, VolumeRAIDInfo,
    PoolRAIDInfo, VcrCap, LocalDiskInfo, VolumeRAMCacheInfo)

_CONNECTION_FREE_COMMANDS = ['local-disk-list',
                             'local-disk-ident-led-on',
                             'local-disk-ident-led-off',
                             'local-disk-fault-led-on',
                             'local-disk-fault-led-off']

if six.PY3:
    long = int

try:
    from collections import OrderedDict
except ImportError:
    # python 2.6 or earlier, use backport
    # noinspection PyUnresolvedReferences
    from ordereddict import OrderedDict


# Wraps the invocation to the command line
# @param    c   Object to invoke calls on (optional)
def cmd_line_wrapper(c=None):
    """
    Common command line code, called.
    """
    err_exit = 0
    cli = None

    try:
        cli = CmdLine()
        cli.process(c)
    except ArgError as ae:
        sys.stderr.write(str(ae))
        sys.stderr.flush()
        err_exit = 2
    except LsmError as le:
        sys.stderr.write(str(le) + "\n")
        sys.stderr.flush()
        if le.code == ErrorNumber.PERMISSION_DENIED:
            err_exit = 13   # common error code for EACCES
        else:
            err_exit = 4
    except KeyboardInterrupt:
        err_exit = 1
    except SystemExit as se:
        # argparse raises a SystemExit
        err_exit = se.code
    except:
        import traceback
        traceback.print_exc(file=sys.stdout)
        # We get *any* other exception don't return a successful error code
        err_exit = 2
    finally:
        # Regardless of what happens, we will try to close the connection if
        # possible to allow the plugin to clean up gracefully.
        if cli:
            try:
                # This will exit if are successful
                cli.shutdown(err_exit)
            except Exception:
                pass
        sys.exit(err_exit)


# Get a character from stdin without needing a return key pressed.
# Returns the character pressed
def getch():
    fd = sys.stdin.fileno()
    prev = termios.tcgetattr(fd)
    try:
        tty.setraw(sys.stdin.fileno())
        ch = sys.stdin.read(1)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, prev)
    return ch


def parse_convert_init(init_id):
    """
    If init_id is a WWPN, convert it into LSM standard version:
        (?:[0-9a-f]{2}:){7}[0-9a-f]{2}

    Return (converted_init_id, lsm_init_type)
    """
    valid, converted_init_type, converted_init_id = \
        AccessGroup.initiator_id_verify(init_id)

    if valid:
        return converted_init_id, converted_init_type

    raise ArgError("--init \"%s\" is not a valid WWPN or iSCSI IQN" % init_id)


def _check_init(init_id):
    """
    Call back from validating an initiator
    :param init_id: Initiator to validate
    :return: Value of initiator or raises an exception
    """
    valid, _, converted_init_id = \
        AccessGroup.initiator_id_verify(init_id)

    if valid:
        return converted_init_id
    raise ArgumentTypeError("\"%s\" is invalid WWPN or iSCSI IQN" % init_id)


def _check_positive_integer(num):
    """
    Call back for validating a positive integer
    :param num: Number string to check
    :return: Numeric value, else exception
    """
    try:
        rc = long(num, 10)
        if rc < 0:
            raise ArgumentTypeError(
                "invalid: require positive integer value '%d'" % rc)

        return rc
    except ValueError:
        raise ArgumentTypeError(
            "invalid: not a positive integer value '%s'" % num)


_CHILD_OPTION_DST_PREFIX = 'child_'


def _upper(s):
    return s.upper()


def _valid_ip4_address(address):
    """
    Check if a string represents a valid ip4 address
    :param address: String representing address
    :return: True if valid address, else false
    """
    if not address:
        return False

    parts = address.split('.')
    if len(parts) != 4:
        return False

    if '/' in address:
        return False

    for i in parts:
        if not 0 < len(i) <= 3:
            return False

        if len(i) > 1 and i[0] == '0':
            return False

        try:
            if int(i, 10) > 255:
                return False
        except ValueError:
            return False

    return True


def _valid_ip6_address(address):
    """
    Check if a string represents a valid ipv6 address
    :param address: String representing address
    :return: True if valid address, else false
    """
    allowed = 'ABCDEFabcdef0123456789:'
    has_zeros = False

    if not address:
        return False

    if '/' in address:
        return False

    if len(address.split("::")) > 2:
        return False

    parts = address.split(':')
    if len(parts) < 3 or len(parts) > 9:
        return False

    # Check for ipv4 suffix, validate and remove while adding padding for
    # addl. checks.
    if '.' in parts[-1]:
        if not _valid_ip4_address(parts.pop()):
            print("Not valid ipv suffix")
            return False
        parts.extend(['0', '0'])

    if '::' in address:
        parts = [p for p in parts if p != '']
        # Add one segment of zero to catch full address with extra ':'
        parts.append('0')
        has_zeros = True

    if (has_zeros and len(parts) <= 8) or len(parts) == 8:
        return all(len(x) <= 4 for x in parts) and \
               all(x in allowed for x in "".join(parts))
    return False


def _is_valid_network_name(ip_hn):
    """
    Checks to see if the supplied string is a valid ip4/6 or hostname
    :param ip_hn: String representing address user inputted
    :return: True if valid IP address or hostname
    """
    allowed = re.compile("(?!-)[A-Z0-9-]{1,63}(?<!-)$", re.IGNORECASE)
    digits_only = re.compile("^[0-9.]+$")

    # Check ipv4, ipv6, then for valid hostname
    if _valid_ip4_address(ip_hn):
        return True

    if _valid_ip6_address(ip_hn):
        return True

    if len(ip_hn) > 255:
        return False

    # A hostname cannot exist with only digits per spec. as that is confusing
    # for distinguishing IP from hostname
    if digits_only.match(ip_hn):
        return False

    if ip_hn[-1] == ".":
        ip_hn = ip_hn[:-1]  # Yes, absolute hostnames have a trailing dot!
    return all(allowed.match(x) for x in ip_hn.split("."))


def _add_common_options(arg_parser, is_child=False):
    """
    As https://bugs.python.org/issue23058 indicate, argument parser should
    not have subparser sharing the same argument and destination.
    For subparser, we add common options as 'child_xxx' destination.
    For default value, False is the only allowed default value in root.
    """
    prefix = ''
    if is_child:
        prefix = _CHILD_OPTION_DST_PREFIX

    arg_parser.add_argument(
        '-v', '--version', action='version',
        version="%s %s" % (sys.argv[0], VERSION))

    arg_parser.add_argument(
        '-u', '--uri', action="store", type=str, metavar='<URI>',
        dest="%suri" % prefix,
        help='Uniform resource identifier (env LSMCLI_URI)')

    arg_parser.add_argument(
        '-P', '--prompt', action="store_true", dest="%sprompt" % prefix,
        help='Prompt for password (env LSMCLI_PASSWORD)')

    arg_parser.add_argument(
        '-H', '--human', action="store_true", dest="%shuman" % prefix,
        help='Print sizes in human readable format\n'
             '(e.g., MiB, GiB, TiB)')

    arg_parser.add_argument(
        '-t', '--terse', action="store", dest="%ssep" % prefix,
        metavar='<SEP>',
        help='Print output in terse form with "SEP" '
             'as a record separator')

    arg_parser.add_argument(
        '-e', '--enum', action="store_true", dest="%senum" % prefix,
        default=False,
        help='Display enumerated types as numbers instead of text')

    arg_parser.add_argument(
        '-f', '--force', action="store_true", dest="%sforce" % prefix,
        default=False,
        help='Bypass confirmation prompt for data loss operations')

    arg_parser.add_argument(
        '-w', '--wait', action="store", dest="%swait" % prefix,
        help="Command timeout value in ms (default = 30s)",
        type=_check_positive_integer)

    arg_parser.add_argument(
        '--header', action="store_true", dest="%sheader" % prefix,
        help='Include the header with terse')

    arg_parser.add_argument(
        '-b', action="store_true", dest="%s_async" % prefix, default=False,
        help='Run the command async. Instead of waiting for completion.\n '
             'Command will exit(7) and job id written to stdout.')

    arg_parser.add_argument(
        '-s', '--script', action="store_true", dest="%sscript" % prefix,
        default=False,
        help='Displaying data in script friendly way with '
             'additional information(if exists)')

    if is_child:
        default_dict = dict()
        default_dict['%swait' % prefix] = 30000
        arg_parser.set_defaults(**default_dict)


def _add_sd_paths(lsm_obj):
    lsm_obj.sd_paths = []
    try:
        if len(lsm_obj.vpd83) > 0:
            lsm_obj.sd_paths = LocalDisk.vpd83_search(lsm_obj.vpd83)
    except LsmError as lsm_err:
        if lsm_err.code != ErrorNumber.NO_SUPPORT:
            raise
    return lsm_obj


# This class represents a command line argument error
class ArgError(Exception):
    def __init__(self, message, *args, **kwargs):
        """
        Class represents an error.
        """
        Exception.__init__(self, *args, **kwargs)
        self.msg = message

    def __str__(self):
        return "%s: error: %s\n" % (os.path.basename(sys.argv[0]), self.msg)


# Finds an item based on the id.  Each list item requires a member "id"
# @param    l       list to search
# @param    the_id  the id to match
# @param    friendly_name - name to put in the exception saying what we
#           couldn't find
def _get_item(l, the_id, friendly_name='item', raise_error=True):
    for item in l:
        if item.id == the_id:
            return item
    if raise_error:
        raise ArgError('%s with ID %s not found!' % (friendly_name, the_id))
    else:
        return None


def _check_network_host(addr):
    """
    Custom value checker for hostname/IP address
    :param addr:
    :return:
    """
    valid = _is_valid_network_name(addr)
    if valid:
        return addr
    raise ArgumentTypeError("%s is invalid IP or hostname" % addr)


list_choices = ['VOLUMES', 'POOLS', 'FS', 'SNAPSHOTS',
                'EXPORTS', "NFS_CLIENT_AUTH", 'ACCESS_GROUPS',
                'SYSTEMS', 'DISKS', 'PLUGINS', 'TARGET_PORTS', 'BATTERIES']

provision_types = ('DEFAULT', 'THIN', 'FULL')
provision_help = "provisioning type: " + ", ".join(provision_types)

replicate_types = ('CLONE', 'COPY', 'MIRROR_ASYNC', 'MIRROR_SYNC')
replicate_help = "replication type: " + ", ".join(replicate_types)

policy_types = ['ENABLE', 'DISABLE']
policy_help = 'Policy: ' + ', '.join(policy_types)
policy_opt = dict(name="--policy", metavar='<POLICY>',
                  help=policy_help, choices=policy_types,
                  type=_upper)

write_cache_policy_types = ['WB', 'AUTO', 'WT']
write_cache_policy_help = 'Write cache polices: ' + \
                          ', '.join(write_cache_policy_types) + \
                          ' which stand for "write back", "auto", ' + \
                          '"write through"'
write_cache_policy_opt = dict(name="--policy", metavar='<POLICY>',
                              help=write_cache_policy_help,
                              choices=write_cache_policy_types,
                              type=_upper)

size_help = 'Can use B, KiB, MiB, GiB, TiB, PiB postfix (IEC sizing)'

sys_id_opt = dict(name='--sys', metavar='<SYS_ID>', help='System ID')
sys_id_filter_opt = sys_id_opt.copy()
sys_id_filter_opt['help'] = \
    'Search by System ID. Only supported for: \n' \
    '(VOLUMES, POOLS, FS, DISKS, ACCESS_GROUPS,\n' \
    'TARGET_PORTS, BATTERIES)'

pool_id_opt = dict(name='--pool', metavar='<POOL_ID>', help='Pool ID')
pool_id_filter_opt = pool_id_opt.copy()
pool_id_filter_opt['help'] = \
    'Search by Pool ID. Only supported for:\n' \
    '(VOLUMES, POOLS, FS)'

vol_id_opt = dict(name='--vol', metavar='<VOL_ID>', help='Volume ID')
vol_id_filter_opt = vol_id_opt.copy()
vol_id_filter_opt['help'] = \
    'Search by Volume ID. Only supported for:\n' \
    '(VOLUMES, ACCESS_GROUPS)'

fs_id_opt = dict(name='--fs', metavar='<FS_ID>', help='File System ID')
fs_id_filter_opt = fs_id_opt.copy()
fs_id_filter_opt['help'] = \
    'Search by FS ID. Only supported for:\n' \
    '(FS, SNAPSHOTS, EXPORTS)'

ag_id_opt = dict(name='--ag', metavar='<AG_ID>', help='Access Group ID')
ag_id_filter_opt = ag_id_opt.copy()
ag_id_filter_opt['help'] = \
    'Search by Access Group ID. Only supported for:\n' \
    '(ACCESS_GROUPS, VOLUMES)'

init_id_opt = dict(name='--init', metavar='<INIT_ID>', help='Initiator ID',
                   type=_check_init)
snap_id_opt = dict(name='--snap', metavar='<SNAP_ID>', help='Snapshot ID')
export_id_opt = dict(name='--export', metavar='<EXPORT_ID>', help='Export ID')

nfs_export_id_filter_opt = dict(
    name='--nfs-export', metavar='<NFS_EXPORT_ID>',
    help=
    'Search by NFS Export ID. Only supported for:\n'
    '(EXPORTS)')

disk_id_filter_opt = dict(name='--disk', metavar='<DISK_ID>',
                          help='Search by Disk ID. Only supported for:\n'
                               '(DISKS)')

size_opt = dict(name='--size', metavar='<SIZE>', help=size_help)

tgt_id_filter_opt = dict(name="--tgt", metavar='<TGT_ID>',
                         help="Search by target port ID.  Only supported for:\n"
                              "(TARGET_PORTS)")

local_disk_path_opt = dict(name='--path', help="Local disk path",
                           metavar='<DISK_PATH>')

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
                 type=_upper),
        ],
        optional=[
            dict(sys_id_filter_opt),
            dict(pool_id_filter_opt),
            dict(vol_id_filter_opt),
            dict(disk_id_filter_opt),
            dict(ag_id_filter_opt),
            dict(fs_id_filter_opt),
            dict(nfs_export_id_filter_opt),
            dict(tgt_id_filter_opt),
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
                 type=_upper),
        ],
    ),

    dict(
        name='volume-raid-create',
        help='Creates a RAIDed volume on hardware RAID',
        args=[
            dict(name="--name", help='volume name', metavar='<NAME>'),
            dict(name="--disk", metavar='<DISK>',
                 help='Free disks for new RAIDed volume.\n'
                      'This is repeatable argument.',
                 action='append'),
            dict(name="--raid-type",
                 help="RAID type for the new RAID group. "
                      "Should be one of these:\n    %s" %
                      "\n    ".
                      join(VolumeRAIDInfo.VOL_CREATE_RAID_TYPES_STR),
                 choices=VolumeRAIDInfo.VOL_CREATE_RAID_TYPES_STR,
                 type=_upper),
        ],
        optional=[
            dict(name="--strip-size",
                 help="Strip size. " + size_help),
        ],
    ),

    dict(
        name='volume-raid-create-cap',
        help='Query capability of creating a RAIDed volume on hardware RAID',
        args=[
            dict(sys_id_opt),
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
        help='Re-sizes a volume',
        args=[
            dict(vol_id_opt),
            dict(name='--size', metavar='<NEW_SIZE>',
                 help="New size. %s" % size_help),
        ],
    ),

    dict(
        name='volume-replicate',
        help='Creates a new volume and replicates provided volume to it.',
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
        help='Replicates a portion of a volume to existing volume',
        args=[
            dict(name="--src-vol", metavar='<SRC_VOL_ID>',
                 help='Source volume id'),
            dict(name="--dst-vol", metavar='<DST_VOL_ID>',
                 help='Destination volume id'),
            dict(name="--rep-type", metavar='<REP_TYPE>',
                 help="Replication type: CLONE, COPY",
                 choices=["CLONE", "COPY"]),
            dict(name="--src-start", metavar='<SRC_START_BLK>',
                 help='Source volume start block number.\n'
                      'This is repeatable argument.',
                 action='append', type=_check_positive_integer),
            dict(name="--dst-start", metavar='<DST_START_BLK>',
                 help='Destination volume start block number.\n'
                      'This is repeatable argument.',
                 action='append', type=_check_positive_integer),
            dict(name="--count", metavar='<BLK_COUNT>',
                 help='Number of blocks to replicate.\n'
                      'This is repeatable argument.',
                 action='append', type=_check_positive_integer),
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
        help='Removes volume dependencies',
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
        name='volume-mask',
        help='Grants access to an access group to a volume, '
             'like LUN Masking',
        args=[
            dict(vol_id_opt),
            dict(ag_id_opt),
        ],
    ),

    dict(
        name='volume-unmask',
        help='Revoke the access of specified access group to a volume',
        args=[
            dict(ag_id_opt),
            dict(vol_id_opt),
        ],
    ),

    dict(
        name='volume-enable',
        help='Enable block access of a volume',
        args=[
            dict(vol_id_opt),
        ],
    ),

    dict(
        name='volume-disable',
        help='Disable block access of a volume',
        args=[
            dict(vol_id_opt),
        ],
    ),

    dict(
        name='volume-raid-info',
        help='Query volume RAID information',
        args=[
            dict(vol_id_opt),
        ],
    ),

    dict(
        name='volume-ident-led-on',
        help='Enable the IDENT LED for a volume',
        args=[
            dict(name="--vol", metavar='<VOL_ID>',
                 help='Targeted volume.\n'),
        ],
    ),

    dict(
        name='volume-ident-led-off',
        help='Disable the IDENT LED for a volume',
        args=[
            dict(name="--vol", metavar='<VOL_ID>',
                 help='Targeted volume.\n'),
        ],
    ),

    dict(
        name='system-read-cache-pct-update',
        help='Change the read cache percentage of a system',
        args=[
            dict(name="--sys", metavar='<SYS_ID>',
                 help='Targeted system.\n'),
            dict(name="--read-pct",
                 help="Read cache percentage.\n",
                 type=_check_positive_integer),
        ],
    ),

    dict(
        name='pool-member-info',
        help='Query Pool membership information',
        args=[
            dict(pool_id_opt),
        ],
    ),

    dict(
        name='access-group-create',
        help='Create an access group',
        args=[
            dict(name='--name', metavar='<AG_NAME>',
                 help="Human readable name for access group"),
            # TODO: _client.py access_group_create should support multiple
            #       initiators when creating.
            dict(init_id_opt),
            dict(sys_id_opt),
        ],
    ),

    dict(
        name='access-group-add',
        help='Add an initiator into existing access group',
        args=[
            dict(ag_id_opt),
            dict(init_id_opt),
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
        name='access-group-volumes',
        help='Lists the volumes that the access group has'
             ' been granted access to',
        args=[
            dict(ag_id_opt),
        ],
    ),

    dict(
        name='iscsi-chap',
        help='Configures iSCSI inbound/outbound CHAP authentication',
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
        help='Re-sizes a filesystem',
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
            dict(name="--anonuid", metavar='<ANON_UID>',
                 help='UID(User ID) to map to anonymous user',
                 default=NfsExport.ANON_UID_GID_NA,
                 type=_check_positive_integer),
            dict(name="--anongid", metavar='<ANON_GID>',
                 help='GID(Group ID) to map to anonymous user',
                 default=NfsExport.ANON_UID_GID_NA,
                 type=_check_positive_integer),
            dict(name="--auth-type", metavar='<AUTH_TYPE>',
                 help='NFS client authentication type'),
            dict(name="--root-host", metavar='<ROOT_HOST>',
                 help="The host/IP has root access.\n"
                      "This is repeatable argument.",
                 action='append',
                 default=[], type=_check_network_host),
            dict(name="--ro-host", metavar='<RO_HOST>',
                 help="The host/IP has readonly access.\n"
                      "This is repeatable argument.\n"
                      "At least one '--ro-host' or '--rw-host' is required.",
                 action='append', default=[], type=_check_network_host),
            dict(name="--rw-host", metavar='<RW_HOST>',
                 help="The host/IP has readwrite access.\n"
                      "This is repeatable argument.\n"
                      "At least one '--ro-host' or '--rw-host' is required.",
                 action='append', default=[], type=_check_network_host),
        ],
    ),

    dict(
        name='fs-unexport',
        help='Remove an NFS export',
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
        help='Removes file system dependencies',
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
                 help='Destination file (relative path)'
                      ", this is a repeatable argument."),
        ],
        optional=[
            dict(name="--backing-snapshot", help='backing snapshot id'),
        ],
    ),

    dict(
        name='local-disk-list',
        help='Query local disk information',
        args=[
        ],
        optional=[
        ],
    ),
    dict(
        name='volume-cache-info',
        help='Query volume RAM cache information',
        args=[
            dict(vol_id_opt),
        ],
    ),
    dict(
        name='volume-phy-disk-cache-update',
        help='Update volume physical disk cache setting',
        args=[
            dict(vol_id_opt),
            dict(policy_opt),
        ],
    ),

    dict(
        name='volume-read-cache-policy-update',
        help='Update volume read cache policy',
        args=[
            dict(vol_id_opt),
            dict(policy_opt),
        ],
    ),
    dict(
        name='volume-write-cache-policy-update',
        help='Update volume write cache policy',
        args=[
            dict(vol_id_opt),
            dict(write_cache_policy_opt),
        ],
    ),
    dict(
        name='local-disk-ident-led-on',
        help='Turn on the identification LED for a local disk',
        args=[
            dict(local_disk_path_opt),
        ],
    ),
    dict(
        name='local-disk-ident-led-off',
        help='Turn off the identification LED for a local disk',
        args=[
            dict(local_disk_path_opt),
        ],
    ),
    dict(
        name='local-disk-fault-led-on',
        help='Turn on the fault LED for a local disk',
        args=[
            dict(local_disk_path_opt),
        ],
    ),
    dict(
        name='local-disk-fault-led-off',
        help='Turn off the fault LED for a local disk',
        args=[
            dict(local_disk_path_opt),
        ],
    ),
)

aliases = dict(
    ls='list --type systems',
    lp='list --type pools',
    lv='list --type volumes',
    ld='list --type disks',
    la='list --type access_groups',
    lf='list --type fs',
    lt='list --type target_ports',
    c='capabilities',
    p='plugin-info',
    vc='volume-create',
    vrc='volume-raid-create',
    vrcc='volume-raid-create-cap',
    vd='volume-delete',
    vr='volume-resize',
    vm='volume-mask',
    vu='volume-unmask',
    ve='volume-enable',
    vi='volume-disable',
    ac='access-group-create',
    aa='access-group-add',
    ar='access-group-remove',
    ad='access-group-delete',
    vri='volume-raid-info',
    vilon='volume-ident-led-on',
    viloff='volume-ident-led-off',
    srcpu='system-read-cache-pct-update',
    pmi='pool-member-info',
    ldl='local-disk-list',
    lb='list --type batteries',
    vci='volume-cache-info',
    vpdcu='volume-phy-disk-cache-update',
    vrcpu='volume-read-cache-policy-update',
    vwcpu='volume-write-cache-policy-update',
    ldilon='local-disk-ident-led-on',
    ldiloff='local-disk-ident-led-off',
    ldflon='local-disk-fault-led-on',
    ldfloff='local-disk-fault-led-off',
)


# Class that encapsulates the command line arguments for lsmcli
# Note: This class is used by lsmcli and any python plug-ins.
class CmdLine(object):
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

    ##
    # Tries to make the output better when it varies considerably from
    # plug-in to plug-in.
    # @param    objects    Data, first row is header all other data.
    def display_data(self, objects):
        display_all = False

        if len(objects) == 0:
            return

        display_way = DisplayData.DISPLAY_WAY_DEFAULT

        flag_with_header = True
        if self.args.sep:
            flag_with_header = False
        if self.args.header:
            flag_with_header = True

        if self.args.script:
            display_way = DisplayData.DISPLAY_WAY_SCRIPT

        DisplayData.display_data(
            objects, display_way=display_way, flag_human=self.args.human,
            flag_enum=self.args.enum,
            splitter=self.args.sep, flag_with_header=flag_with_header,
            flag_dsp_all_data=display_all)

    def display_available_plugins(self):
        d = []
        sep = '<}{>'
        plugins = Client.available_plugins(sep)

        for p in plugins:
            desc, version = p.split(sep)
            d.append(PlugData(desc, version))

        self.display_data(d)

    @staticmethod
    def handle_alias():
        """
        Walk the command line argument list and build up a new command line
        with the appropriate substitutions which is then passed to argparse, so
        that we can avoid adding more sub parsers and do all argument parsing
        before the need to talk to the library
        :return copy of command line args with alias expansion:
        """
        rc = []
        for i in sys.argv[1:]:
            if i in aliases:
                rc.extend(aliases[i].split(" "))
            else:
                rc.append(i)
        return rc

    @staticmethod
    def alias_help_text():
        rc = "command aliases:\n"
        for k, v in sorted(aliases.items()):
            rc += "   {0:<18}   Alias of '{1}'\n".format(k, v)
        return rc

    # All the command line arguments and options are created in this method
    def cli(self):
        """
        Command line interface parameters
        """
        parent_parser = ArgumentParser(add_help=False)
        _add_common_options(parent_parser, is_child=True)

        parser = ArgumentParser(
            description='The libStorageMgmt command line interface.'
                        ' Run %(prog)s <command> -h for more on each command.',
            epilog=CmdLine.alias_help_text() +
                        '\n\nCopyright 2012-2018 Red Hat, Inc.\n'
                        'Please report bugs to '
                        '<libstoragemgmt-devel@lists.fedorahosted.org>\n',
            formatter_class=RawTextHelpFormatter)
        _add_common_options(parser, is_child=False)

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

        if len(sys.argv) == 1:
            parser.print_usage()
            exit(1)

        self.parser = parser

        known_args = parser.parse_args(args=CmdLine.handle_alias())
        # Copy child value to root.

        for k, v in vars(known_args).items():
            if k.startswith(_CHILD_OPTION_DST_PREFIX):
                root_k = k[len(_CHILD_OPTION_DST_PREFIX):]
                if getattr(known_args, root_k) is None or \
                   getattr(known_args, root_k) is False:
                    setattr(known_args, root_k, v)

        return known_args

    # Display the types of nfs client authentication that are supported.
    # @return None
    def display_nfs_client_authentication(self):
        """
        Dump the supported nfs client authentication types
        """
        if self.args.sep:
            out(self.args.sep.join(self.c.export_auth()))
        else:
            out(", ".join(self.c.export_auth()))

    # Determine what the search key and search value are for listing
    # @param    args    Argparse argument object
    # @return (key, value) tuple
    @staticmethod
    def _get_search_key_value(args):

        search_key = None
        search_value = None

        search_args = ((args.sys, 'system_id'),
                       (args.pool, 'pool_id'),
                       (args.vol, 'volume_id'),
                       (args.disk, 'disk_id'),
                       (args.ag, 'access_group_id'),
                       (args.fs, 'fs_id'),
                       (args.nfs_export, 'nfs_export_id'),
                       (args.tgt, 'tgt_port_id'))

        for sa in search_args:
            if sa[0]:
                if search_key:
                    raise ArgError(
                        "Search key specified more than once (%s, %s)" %
                        (search_key, sa[1]))
                else:
                    (search_value, search_key) = sa

        return search_key, search_value

    # Method that calls the appropriate method based on what the list type is
    # @param    args    Argparse argument object
    def list(self, args):
        (search_key, search_value) = CmdLine._get_search_key_value(args)

        if args.type == 'VOLUMES':
            lsm_vols = []
            if search_key == 'volume_id':
                search_key = 'id'
            if search_key == 'access_group_id':
                lsm_ag = _get_item(self.c.access_groups(), args.ag,
                                   "Access Group", raise_error=False)
                if lsm_ag:
                    lsm_vols = self.c.volumes_accessible_by_access_group(
                        lsm_ag)
            elif search_key and search_key not in Volume.SUPPORTED_SEARCH_KEYS:
                raise ArgError("Search key '%s' is not supported by "
                               "volume listing." % search_key)
            else:
                lsm_vols = self.c.volumes(search_key, search_value)

            self.display_data(list(_add_sd_paths(v) for v in lsm_vols))

        elif args.type == 'POOLS':
            if search_key == 'pool_id':
                search_key = 'id'
            if search_key and search_key not in Pool.SUPPORTED_SEARCH_KEYS:
                raise ArgError("Search key '%s' is not supported by "
                               "pool listing." % search_key)
            self.display_data(
                self.c.pools(search_key, search_value))
        elif args.type == 'FS':
            if search_key == 'fs_id':
                search_key = 'id'
            if search_key and \
               search_key not in FileSystem.SUPPORTED_SEARCH_KEYS:
                raise ArgError("Search key '%s' is not supported by "
                               "volume listing." % search_key)
            self.display_data(self.c.fs(search_key, search_value))
        elif args.type == 'SNAPSHOTS':
            if args.fs is None:
                raise ArgError("--fs <file system id> required")
            if search_key and search_key != "fs_id":
                raise ArgError("Search key '%s' is not supported by "
                               "snapshot listing." % search_key)
            fs = _get_item(self.c.fs(), args.fs, 'File System')
            self.display_data(self.c.fs_snapshots(fs))
        elif args.type == 'EXPORTS':
            if search_key == 'nfs_export_id':
                search_key = 'id'
            if search_key and \
               search_key not in NfsExport.SUPPORTED_SEARCH_KEYS:
                raise ArgError("Search key '%s' is not supported by "
                               "NFS Export listing" % search_key)
            self.display_data(self.c.exports(search_key, search_value))
        elif args.type == 'NFS_CLIENT_AUTH':
            if search_key:
                raise ArgError("NFS client authentication type listing with "
                               "search is not supported")
            self.display_nfs_client_authentication()
        elif args.type == 'ACCESS_GROUPS':
            if search_key == 'access_group_id':
                search_key = 'id'
            if search_key == 'volume_id':
                lsm_vol = _get_item(self.c.volumes(), args.vol,
                                    "Volume", raise_error=False)
                if lsm_vol:
                    return self.display_data(
                        self.c.access_groups_granted_to_volume(lsm_vol))
                else:
                    return self.display_data([])
            elif (search_key and
                  search_key not in AccessGroup.SUPPORTED_SEARCH_KEYS):
                raise ArgError("Search key '%s' is not supported by "
                               "Access Group listing" % search_key)
            self.display_data(
                self.c.access_groups(search_key, search_value))
        elif args.type == 'SYSTEMS':
            if search_key:
                raise ArgError("System listing with search is not supported")
            self.display_data(self.c.systems())
        elif args.type == 'DISKS':
            if search_key == 'disk_id':
                search_key = 'id'
            if search_key and search_key not in Disk.SUPPORTED_SEARCH_KEYS:
                raise ArgError("Search key '%s' is not supported by "
                               "disk listing" % search_key)
            self.display_data(
                list(_add_sd_paths(d)
                     for d in self.c.disks(search_key, search_value)))
        elif args.type == 'TARGET_PORTS':
            if search_key == 'tgt_port_id':
                search_key = 'id'
            if search_key and \
               search_key not in TargetPort.SUPPORTED_SEARCH_KEYS:
                raise ArgError("Search key '%s' is not supported by "
                               "target port listing" % search_key)
            self.display_data(
                self.c.target_ports(search_key, search_value))
        elif args.type == 'PLUGINS':
            if search_key:
                raise ArgError("Plugins listing with search is not supported")
            self.display_available_plugins()
        elif args.type == 'BATTERIES':
            if search_key and \
               search_key not in Battery.SUPPORTED_SEARCH_KEYS:
                raise ArgError("Search key '%s' is not supported by "
                               "battery listing" % search_key)
            self.display_data(
                self.c.batteries(search_key, search_value))
        else:
            raise ArgError("unsupported listing type=%s" % args.type)

    # Creates an access group.
    def access_group_create(self, args):
        system = _get_item(self.c.systems(), args.sys, "System")
        (init_id, init_type) = parse_convert_init(args.init)
        access_group = self.c.access_group_create(args.name, init_id,
                                                  init_type, system)
        self.display_data([access_group])

    def _add_rm_access_grp_init(self, args, op):
        lsm_ag = _get_item(self.c.access_groups(), args.ag, "Access Group")
        (init_id, init_type) = parse_convert_init(args.init)

        if op:
            return self.c.access_group_initiator_add(lsm_ag, init_id,
                                                     init_type)
        else:
            return self.c.access_group_initiator_delete(lsm_ag, init_id,
                                                        init_type)

    # Adds an initiator from an access group
    def access_group_add(self, args):
        self.display_data([self._add_rm_access_grp_init(args, True)])

    # Removes an initiator from an access group
    def access_group_remove(self, args):
        self.display_data([self._add_rm_access_grp_init(args, False)])

    def access_group_volumes(self, args):
        agl = self.c.access_groups()
        group = _get_item(agl, args.ag, "Access Group")
        vols = self.c.volumes_accessible_by_access_group(group)
        self.display_data(list(_add_sd_paths(v) for v in vols))

    def iscsi_chap(self, args):
        (init_id, init_type) = parse_convert_init(args.init)
        if init_type != AccessGroup.INIT_TYPE_ISCSI_IQN:
            raise ArgError("--init \"%s\" is not a valid iSCSI IQN" % args.init)

        if self.args.in_user and not self.args.in_pass:
            raise ArgError("--in-user requires --in-pass")

        if self.args.in_pass and not self.args.in_user:
            raise ArgError("--in-pass requires --in-user")

        if self.args.out_user and not self.args.out_pass:
            raise ArgError("--out-user requires --out-pass")

        if self.args.out_pass and not self.args.out_user:
            raise ArgError("--out-pass requires --out-user")

        # Enforce consistency across all
        if self.args.out_user and self.args.out_pass and not \
                (self.args.in_user and self.args.in_pass):
            raise ArgError("out-user and out-password only supported if "
                           "inbound is supplied")

        self.c.iscsi_chap_auth(init_id, args.in_user,
                               self.args.in_pass,
                               self.args.out_user,
                               self.args.out_pass)

    def volume_access_group(self, args):
        vol = _get_item(self.c.volumes(), args.vol, "Volume")
        groups = self.c.access_groups_granted_to_volume(vol)
        self.display_data(groups)

    # Used to delete access group
    def access_group_delete(self, args):
        agl = self.c.access_groups()
        group = _get_item(agl, args.ag, "Access Group")
        return self.c.access_group_delete(group)

    # Used to delete a file system
    def fs_delete(self, args):
        fs = _get_item(self.c.fs(), args.fs, "File System")
        if self.confirm_prompt(True):
            self._wait_for_it("fs-delete", self.c.fs_delete(fs), None)

    # Used to create a file system
    def fs_create(self, args):
        p = _get_item(self.c.pools(), args.pool, "Pool")
        fs = self._wait_for_it("fs-create",
                               *self.c.fs_create(p, args.name,
                                                 self._size(args.size)))
        self.display_data([fs])

    # Used to resize a file system
    def fs_resize(self, args):
        fs = _get_item(self.c.fs(), args.fs, "File System")
        size = self._size(args.size)

        if size == fs.total_space:
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE, "Specified size same as current")

        if self.confirm_prompt(False):
            fs = self._wait_for_it("fs-resize",
                                   *self.c.fs_resize(fs, size))
            self.display_data([fs])

    # Used to clone a file system
    def fs_clone(self, args):
        src_fs = _get_item(
            self.c.fs(), args.src_fs, "Source File System")

        ss = None
        if args.backing_snapshot:
            # go get the snapshot
            ss = _get_item(self.c.fs_snapshots(src_fs),
                           args.backing_snapshot, "Snapshot")

        fs = self._wait_for_it(
            "fs_clone", *self.c.fs_clone(src_fs, args.dst_name, ss))
        self.display_data([fs])

    # Used to clone a file(s)
    def file_clone(self, args):
        fs = _get_item(self.c.fs(), args.fs, "File System")
        if self.args.backing_snapshot:
            # go get the snapshot
            ss = _get_item(self.c.fs_snapshots(fs),
                           args.backing_snapshot, "Snapshot")
        else:
            ss = None

        self._wait_for_it(
            "fs_file_clone", self.c.fs_file_clone(fs, args.src, args.dst, ss),
            None)

    # Converts a size parameter into the appropriate number of bytes
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

        if val:
            v = "SUPPORTED"
        else:
            v = "UNSUPPORTED"

        out("%s%s%s" % (cap, s, v))

    def capabilities(self, args):
        s = _get_item(self.c.systems(), args.sys, "System")

        cap = self.c.capabilities(s)
        sup_caps = sorted(cap.get_supported().values())
        all_caps = sorted(cap.get_supported(True).values())

        sep = DisplayData.DEFAULT_SPLITTER
        if self.args.sep is not None:
            sep = self.args.sep

        cap_data = OrderedDict()
        # Show support capabilities first
        for v in sup_caps:
            cap_data[v] = 'SUPPORTED'

        for v in all_caps:
            if v not in sup_caps:
                cap_data[v] = 'UNSUPPORTED'

        DisplayData.display_data_script_way([cap_data], sep)

    def plugin_info(self, args):
        desc, version = self.c.plugin_info()

        if args.sep:
            out("%s%s%s" % (desc, args.sep, version))
        else:
            out("Description: %s Version: %s" % (desc, version))

    # Creates a volume
    def volume_create(self, args):
        # Get pool
        p = _get_item(self.c.pools(), args.pool, "Pool")
        vol = self._wait_for_it(
            "volume-create",
            *self.c.volume_create(
                p,
                args.name,
                self._size(args.size),
                vol_provision_str_to_type(args.provisioning)))
        self.display_data([_add_sd_paths(vol)])

    # Creates a snapshot
    def fs_snap_create(self, args):
        # Get fs
        fs = _get_item(self.c.fs(), args.fs, "File System")
        ss = self._wait_for_it("snapshot-create",
                               *self.c.fs_snapshot_create(
                                   fs,
                                   args.name))

        self.display_data([ss])

    # Restores a snap shot
    def fs_snap_restore(self, args):
        # Get snapshot
        fs = _get_item(self.c.fs(), args.fs, "File System")
        ss = _get_item(self.c.fs_snapshots(fs), args.snap, "Snapshot")
        files = self.args.file
        if len(files) == 0:
            files = None

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
                self.c.fs_snapshot_restore(
                    fs, ss, files, self.args.fileas, flag_all_files),
                None)

    # Deletes a volume
    def volume_delete(self, args):
        v = _get_item(self.c.volumes(), args.vol, "Volume")
        if self.confirm_prompt(True):
            self._wait_for_it("volume-delete", self.c.volume_delete(v),
                              None)

    # Deletes a snap shot
    def fs_snap_delete(self, args):
        fs = _get_item(self.c.fs(), args.fs, "File System")
        ss = _get_item(self.c.fs_snapshots(fs), args.snap, "Snapshot")

        if self.confirm_prompt(True):
            self._wait_for_it("fs_snap_delete",
                              self.c.fs_snapshot_delete(fs, ss), None)

    # Waits for an operation to complete by polling for the status of the
    # operations.
    # @param    msg     Message to display if this job fails
    # @param    job     The job id to wait on
    # @param    item    The item that could be available now if there is no job
    def _wait_for_it(self, msg, job, item):
        if not job:
            return item
        else:
            # If a user doesn't want to wait, return the job id to stdout
            # and exit with job in progress
            if self.args._async:
                out(job)
                self.shutdown(ErrorNumber.JOB_STARTED)

            while True:
                (s, percent, item) = self.c.job_status(job)

                if s == JobStatus.INPROGRESS:
                    # Add an option to spit out progress?
                    # print "%s - Percent %s complete" % (job, percent)
                    time.sleep(0.25)
                elif s == JobStatus.COMPLETE:
                    self.c.job_free(job)
                    return item
                else:
                    # Something better to do here?
                    raise ArgError(msg + " job error code= " + str(s))

    # Retrieves the status of the specified job
    def job_status(self, args):
        (s, percent, item) = self.c.job_status(args.job)

        if s == JobStatus.COMPLETE:
            if item:
                self.display_data([_add_sd_paths(item)])

            self.c.job_free(args.job)
        else:
            out(str(percent))
            self.shutdown(ErrorNumber.JOB_STARTED)

    # Replicates a volume
    def volume_replicate(self, args):
        p = None
        if args.pool:
            p = _get_item(self.c.pools(), args.pool, "Pool")

        v = _get_item(self.c.volumes(), args.vol, "Volume")

        rep_type = vol_rep_type_str_to_type(args.rep_type)
        if rep_type == Volume.REPLICATE_UNKNOWN:
            raise ArgError("invalid replication type= %s" % rep_type)

        vol = self._wait_for_it(
            "replicate volume",
            *self.c.volume_replicate(p, rep_type, v, args.name))
        self.display_data([_add_sd_paths(vol)])

    # Check to see if block ranges are overlapping
    @staticmethod
    def _check_overlap(ranges):

        def _overlap(r, member):
            for i in range(1, len(r)):
                ps = getattr(r[i - 1], member)  # Previous start
                pc = r[i - 1].block_count       # Previous count
                cs = getattr(r[i], member)      # Current start
                cc = r[i].block_count           # Current count
                if ps + pc > cs:
                    raise ArgError("Overlapping %s replication "
                                   "range %d..%d overlaps with %d..%d" %
                                   (member, ps, ps + pc - 1, cs, cs + cc - 1))

        # Sort the src ranges
        ranges.sort(key=lambda x: x.src_block)
        _overlap(ranges, "src_block")
        ranges.sort(key=lambda x: x.dest_block)
        _overlap(ranges, "dest_block")

    # Replicates a range of a volume
    def volume_replicate_range(self, args):
        src = _get_item(self.c.volumes(), args.src_vol, "Source Volume")
        dst = _get_item(self.c.volumes(), args.dst_vol,
                        "Destination Volume")

        rep_type = vol_rep_type_str_to_type(args.rep_type)
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
        for b in range(len(src_starts)):

            # Validate some assumptions for source & count
            count = long(counts[b])
            src_start = long(src_starts[b])
            dst_start = long(dst_starts[b])

            if count < 0:
                raise ArgError("--count: value < 0")

            if src_start < 0:
                raise ArgError("--src-start: value < 0")

            if dst_start < 0:
                raise ArgError("--dst_start: value < 0")

            if src_start + count > src.num_of_blocks:
                raise ArgError("--src-start + --count > source size")
            if dst_start + count > dst.num_of_blocks:
                raise ArgError("--dst-start + --count > destination size")

            ranges.append(BlockRange(src_start, dst_start, count))

        CmdLine._check_overlap(ranges)

        if self.confirm_prompt(False):
            self.c.volume_replicate_range(rep_type, src, dst, ranges)

    #
    # Returns the block size in bytes for each block represented in
    # volume_replicate_range
    def volume_replicate_range_block_size(self, args):
        s = _get_item(self.c.systems(), args.sys, "System")
        out(self.c.volume_replicate_range_block_size(s))

    def volume_mask(self, args):
        vol = _get_item(self.c.volumes(), args.vol, 'Volume')
        ag = _get_item(self.c.access_groups(), args.ag, 'Access Group')
        self.c.volume_mask(ag, vol)

    def volume_unmask(self, args):
        ag = _get_item(self.c.access_groups(), args.ag, "Access Group")
        vol = _get_item(self.c.volumes(), args.vol, "Volume")
        return self.c.volume_unmask(ag, vol)

    # Re-sizes a volume
    def volume_resize(self, args):
        v = _get_item(self.c.volumes(), args.vol, "Volume")
        size = self._size(args.size)

        if size == v.block_size * v.num_of_blocks:
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE, "Specified size same as current")

        if self.confirm_prompt(False):
            vol = self._wait_for_it("resize",
                                    *self.c.volume_resize(v, size))
            self.display_data([_add_sd_paths(vol)])

    # Enable a volume
    def volume_enable(self, args):
        v = _get_item(self.c.volumes(), args.vol, "Volume")
        self.c.volume_enable(v)

    # Disable a volume
    def volume_disable(self, args):
        v = _get_item(self.c.volumes(), args.vol, "Volume")
        self.c.volume_disable(v)

    # Removes a nfs export
    def fs_unexport(self, args):
        export = _get_item(self.c.exports(), args.export, "NFS Export")
        self.c.export_remove(export)

    # Exports a file system as a NFS export
    def fs_export(self, args):
        fs = _get_item(self.c.fs(), args.fs, "File System")

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

    # Displays volume dependants.
    def volume_dependants(self, args):
        v = _get_item(self.c.volumes(), args.vol, "Volume")
        rc = self.c.volume_child_dependency(v)
        out(rc)

    # Removes volume dependants.
    def volume_dependants_rm(self, args):
        v = _get_item(self.c.volumes(), args.vol, "Volume")
        self._wait_for_it("volume-dependant-rm",
                          self.c.volume_child_dependency_rm(v), None)

    def volume_raid_info(self, args):
        lsm_vol = _get_item(self.c.volumes(), args.vol, "Volume")
        self.display_data(
            [
                VolumeRAIDInfo(
                    lsm_vol.id, *self.c.volume_raid_info(lsm_vol))])

    def pool_member_info(self, args):
        lsm_pool = _get_item(self.c.pools(), args.pool, "Pool")
        self.display_data(
            [
                PoolRAIDInfo(
                    lsm_pool.id, *self.c.pool_member_info(lsm_pool))])

    def volume_raid_create(self, args):
        raid_type = VolumeRAIDInfo.raid_type_str_to_lsm(args.raid_type)

        all_lsm_disks = self.c.disks()
        lsm_disks = [d for d in all_lsm_disks if d.id in args.disk]
        if len(lsm_disks) != len(args.disk):
            raise LsmError(
                ErrorNumber.NOT_FOUND_DISK,
                "Disk ID %s not found" %
                ', '.join(set(args.disk) - set(d.id for d in all_lsm_disks)))

        busy_disks = [d.id for d in lsm_disks
                      if not d.status & Disk.STATUS_FREE]

        if len(busy_disks) >= 1:
            raise LsmError(
                ErrorNumber.DISK_NOT_FREE,
                "Disk %s is not free" % ", ".join(busy_disks))

        if args.strip_size:
            strip_size = size_human_2_size_bytes(args.strip_size)
        else:
            strip_size = Volume.VCR_STRIP_SIZE_DEFAULT

        self.display_data([
            _add_sd_paths(
                self.c.volume_raid_create(
                    args.name, raid_type, lsm_disks, strip_size))])

    def volume_raid_create_cap(self, args):
        lsm_sys = _get_item(self.c.systems(), args.sys, "System")
        self.display_data([
            VcrCap(lsm_sys.id, *self.c.volume_raid_create_cap_get(lsm_sys))])

    def volume_ident_led_on(self, args):
        lsm_volume = _get_item(self.c.volumes(), args.vol, "Volume")

        self.c.volume_ident_led_on(lsm_volume)

    def volume_ident_led_off(self, args):
        lsm_volume = _get_item(self.c.volumes(), args.vol, "Volume")

        self.c.volume_ident_led_off(lsm_volume)

    def system_read_cache_pct_update(self, args):
        lsm_system = _get_item(self.c.systems(), args.sys, "System")
        try:
            read_pct = int(args.read_pct)
        except ValueError as ve:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT, str(ve))

        self.c.system_read_cache_pct_update(lsm_system, read_pct)
        lsm_system = _get_item(self.c.systems(), args.sys, "System")
        self.display_data([lsm_system])

    # Displays file system dependants
    def fs_dependants(self, args):
        fs = _get_item(self.c.fs(), args.fs, "File System")
        rc = self.c.fs_child_dependency(fs, args.file)
        out(rc)

    # Removes file system dependants
    def fs_dependants_rm(self, args):
        fs = _get_item(self.c.fs(), args.fs, "File System")
        self._wait_for_it("fs-dependants-rm",
                          self.c.fs_child_dependency_rm(fs,
                                                        args.file),
                          None)

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

    def is_connection_free_cmd(self):
        """
        Return True if current command is one of _CONNECTION_FREE_COMMANDS.
        """
        if self.args.func.__name__.replace("_", "-") in \
           _CONNECTION_FREE_COMMANDS:
            return True
        return False

    # Class constructor.
    def __init__(self):
        self.uri = None
        self.c = None
        self.parser = None
        self.unknown_args = None
        self.args = self.cli()

        self.cleanup = None

        self.tmo = int(self.args.wait)
        if not self.tmo or self.tmo < 0:
            raise ArgError("[-w|--wait] requires a non-zero positive integer")

        if self.is_connection_free_cmd():
            return

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
            if 'type' in self.args and self.args.type == "PLUGINS":
                self.uri = "sim://"
                self.password = None
            else:
                raise ArgError("--uri missing or export LSMCLI_URI")

        # Lastly get the password if requested.
        if self.args.prompt:
            self.password = getpass.getpass()

        if self.password is not None:
            # Check for username
            u = uri_parse(self.uri)
            if u['username'] is None:
                raise ArgError("password specified with no user name in uri")

    # Does appropriate clean-up
    # @param    ec      The exit code
    def shutdown(self, ec=None):
        if self.cleanup:
            self.cleanup()

        if ec:
            sys.exit(ec)

    # Process the specified command
    # @param    cli     The object instance to invoke methods on.
    def process(self, cli=None):
        """
        Process the parsed command.
        """
        if self.is_connection_free_cmd():
            self.args.func(self.args)
        else:
            if cli:
                # Directly invoking code though a wrapper to catch unsupported
                # operations.
                self.c = Proxy(cli())
                self.c.plugin_register(self.uri, self.password, self.tmo)
                self.cleanup = self.c.plugin_unregister
            else:
                # Going across the ipc pipe
                self.c = Proxy(Client(self.uri, self.password, self.tmo))

                if os.getenv('LSM_DEBUG_PLUGIN'):
                    input("Attach debugger to plug-in, "
                          "press <return> when ready...")

                self.cleanup = self.c.close

            self.args.func(self.args)
            self.shutdown()

    def local_disk_list(self, args):
        local_disks = []
        func_dict = {
            "vpd83": LocalDisk.vpd83_get,
            "rpm": LocalDisk.rpm_get,
            "link_type": LocalDisk.link_type_get,
            "serial_num": LocalDisk.serial_num_get,
            "led_status": LocalDisk.led_status_get,
            "link_speed": LocalDisk.link_speed_get,
            "health_status": LocalDisk.health_status_get,
        }
        for disk_path in LocalDisk.list():
            info_dict = {
                "vpd83": "",
                "rpm": Disk.RPM_NO_SUPPORT,
                "link_type": Disk.LINK_TYPE_NO_SUPPORT,
                "serial_num": "",
                "led_status": Disk.LED_STATUS_UNKNOWN,
                "link_speed": Disk.LINK_SPEED_UNKNOWN,
                "health_status": Disk.HEALTH_STATUS_UNKNOWN,
            }
            for key in info_dict.keys():
                try:
                    info_dict[key] = func_dict[key](disk_path)
                except LsmError as lsm_err:
                    if lsm_err.code != ErrorNumber.NO_SUPPORT:
                        sys.stderr.write("WARN: %s('%s'): %d %s\n" %
                                         (func_dict[key].__name__, disk_path,
                                          lsm_err.code, lsm_err.msg))

            local_disks.append(
                LocalDiskInfo(disk_path,
                              info_dict["vpd83"],
                              info_dict["rpm"],
                              info_dict["link_type"],
                              info_dict["serial_num"],
                              info_dict["led_status"],
                              info_dict["link_speed"],
                              info_dict["health_status"]))

        self.display_data(local_disks)

    def volume_cache_info(self, args):
        lsm_vol = _get_item(self.c.volumes(), args.vol, "Volume")
        self.display_data(
            [
                VolumeRAMCacheInfo(
                    lsm_vol.id, *self.c.volume_cache_info(lsm_vol))])

    def volume_phy_disk_cache_update(self, args):
        lsm_vol = _get_item(self.c.volumes(), args.vol, "Volume")
        if args.policy == "ENABLE":
            policy = Volume.READ_CACHE_POLICY_ENABLED
        else:
            policy = Volume.READ_CACHE_POLICY_DISABLED
        self.c.volume_physical_disk_cache_update(lsm_vol, policy)
        self.display_data(
            [
                VolumeRAMCacheInfo(
                    lsm_vol.id, *self.c.volume_cache_info(lsm_vol))])

    def volume_read_cache_policy_update(self, args):
        lsm_vol = _get_item(self.c.volumes(), args.vol, "Volume")
        if args.policy == "ENABLE":
            policy = Volume.PHYSICAL_DISK_CACHE_ENABLED
        else:
            policy = Volume.PHYSICAL_DISK_CACHE_DISABLED
        self.c.volume_read_cache_policy_update(lsm_vol, policy)
        self.display_data(
            [
                VolumeRAMCacheInfo(
                    lsm_vol.id, *self.c.volume_cache_info(lsm_vol))])

    def volume_write_cache_policy_update(self, args):
        lsm_vol = _get_item(self.c.volumes(), args.vol, "Volume")
        if args.policy == 'WB':
            policy = Volume.WRITE_CACHE_POLICY_WRITE_BACK
        elif args.policy == 'AUTO':
            policy = Volume.WRITE_CACHE_POLICY_AUTO
        else:
            policy = Volume.WRITE_CACHE_POLICY_WRITE_THROUGH
        self.c.volume_write_cache_policy_update(lsm_vol, policy)
        self.display_data(
            [
                VolumeRAMCacheInfo(
                    lsm_vol.id, *self.c.volume_cache_info(lsm_vol))])

    def local_disk_ident_led_on(self, args):
        LocalDisk.ident_led_on(args.path)

    def local_disk_ident_led_off(self, args):
        LocalDisk.ident_led_off(args.path)

    def local_disk_fault_led_on(self, args):
        LocalDisk.fault_led_on(args.path)

    def local_disk_fault_led_off(self, args):
        LocalDisk.fault_led_off(args.path)
