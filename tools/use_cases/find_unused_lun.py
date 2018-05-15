#!/usr/bin/python
#
# Copyright (C) 2017 Red Hat, Inc.
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
#

import os
import errno
import json
import sys
from lsm import Client, LsmError, ErrorNumber, LocalDisk


def print_stderr(msg):
    sys.stderr.write(msg)
    sys.stderr.write("\n")


def is_free_disk(blk_path):
    """
    The manpage of open(2):
        There is one exception: on Linux 2.6 and later, O_EXCL can be used
        without O_CREAT if pathname refers to a block device.  If the block
        device is in use by the system (e.g., mounted), open() fails with the
        error EBUSY.
    Even when disk has partitions, when all partitions are not used, this
    function still treats it as free disk.
    When specific disk or its partition is used by LVM, if there is no data
    stored in it, this function still treats it as free disk.
    """
    try:
        fd = os.open(blk_path, os.O_EXCL)
        os.close(fd)
        return True
    except OSError as err:
        if err.errno == errno.EBUSY:
            return False
        elif err.errno == errno.EACCES:
            raise LsmError(ErrorNumber.PERMISSION_DENIED,
                           "Permission deny, try sudo or run as root")
        else:
            raise


def get_mpath(blk_path):
    """
    Use `/sys/block/sdX/holders/dm-0/dm/name` to get mpath name,
    use `/sys/block/sdf/holders/dm-0/dm/uuid` to check whether holder is
    multipath.
    """
    blk_name = blk_path[len("/dev/"):]
    sysfs_holder_folder = "/sys/block/%s/holders" % blk_name
    try:
        holders = os.listdir(sysfs_holder_folder)
    except FileNotFoundError:
        return None
    if len(holders) != 1:
        return None

    holder_name = holders[0]
    sysfs_dm_name_path = "/sys/block/%s/dm/name" % holder_name
    sysfs_dm_uuid_path = "/sys/block/%s/dm/uuid" % holder_name
    with open(sysfs_dm_uuid_path) as f:
        if not f.read().startswith("mpath-"):
            return None
    with open(sysfs_dm_name_path) as f:
        return f.read().strip()


def is_free_multipath(blk_path):
    mpath_name = get_mpath(blk_path)
    if mpath_name:
        return is_free_disk("/dev/mapper/%s" % mpath_name)
    return False


def find_unused_lun(c):
    """
    Return a list of lsm.Volume objects.
    Unused here means the LUN/volume meets one of following conditions:
        * the block device representing LUN/Volume could be opened with O_EXCL,
          which means its block is completely free.
        * The block device is used as PV of LVM, but not been used.
    """
    vols = c.volumes()
    vpd83_to_vol_hash = {}
    free_vol_dict = {}
    for vol in vols:
        if vol.vpd83:
            vpd83_to_vol_hash[vol.vpd83] = vol

    for disk_path in LocalDisk.list():
        if is_free_disk(disk_path) or is_free_multipath(disk_path):

            try:
                vpd83 = LocalDisk.vpd83_get(disk_path)
            except LsmError as lsm_err:
                if lsm_err.code == ErrorNumber.NO_SUPPORT:
                    continue
                raise
            if vpd83 not in vpd83_to_vol_hash:
                print_stderr("Found free disk %s, but not managed by "
                             "libstoragemgmt plugin" % disk_path)
                continue
            free_vol_dict[vpd83] = vpd83_to_vol_hash[vpd83]

    return free_vol_dict.values()


def format_vol(vol, sys_dict):
    d = {
        "id": vol.id,
        "name": vol.name,
        "wwid": vol.vpd83,
        "system_id": vol.system_id,
        "system_name": sys_dict[vol.system_id].name
    }
    blk_paths = LocalDisk.vpd83_search(vol.vpd83)
    if blk_paths:
        d['blk_paths'] = blk_paths
        mpath_name = get_mpath(blk_paths[0])
        if mpath_name:
            d['mpath_blk'] = "/dev/mapper/%s" % mpath_name
    return d


def main():
    uri = os.getenv('LSMCLI_URI')
    password = os.getenv('LSMCLI_PASSWORD')
    if uri is None:
        print_stderr("LSMCLI_URI environment not defined, using 'local://'")
        uri = "local://"
    c = Client(uri, password)
    free_vols = find_unused_lun(c)
    if not free_vols:
        print_stderr("Didn't find any free LUN")
        exit()
    syss = c.systems()
    sys_dict = {}
    for lsm_sys in syss:
        sys_dict[lsm_sys.id] = lsm_sys
    print_stderr("\nFound %d free LUN(s):\n" % len(free_vols))
    for vol in free_vols:
        print(json.dumps(format_vol(vol, sys_dict), indent=4))


if __name__ == '__main__':
    main()
