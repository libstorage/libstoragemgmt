# Copyright (C) 2011-2015 Red Hat, Inc.
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

import random
import tempfile
import os
import time
import sqlite3


from lsm import (size_human_2_size_bytes)
from lsm import (System, Volume, Disk, Pool, FileSystem, AccessGroup,
                 FsSnapshot, NfsExport, md5, LsmError, TargetPort,
                 ErrorNumber, JobStatus)


def _handle_errors(method):
    def wrapper(*args, **kargs):
        try:
            return method(*args, **kargs)
        except sqlite3.OperationalError as sql_error:
            if type(args[0]) is SimArray and hasattr(args[0], 'bs_obj'):
                args[0].bs_obj.trans_rollback()
            if str(sql_error) == 'database is locked':
                raise LsmError(
                    ErrorNumber.TIMEOUT,
                    "Timeout to require lock on state file")
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "Got unexpected error from sqlite3: %s" % str(sql_error))
        except LsmError:
            if type(args[0]) is SimArray and hasattr(args[0], 'bs_obj'):
                args[0].bs_obj.trans_rollback()
            raise
        except Exception as base_error:
            if type(args[0]) is SimArray and hasattr(args[0], 'bs_obj'):
                args[0].bs_obj.trans_rollback()
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "Got unexpected error: %s" % str(base_error))
    return wrapper


def _random_vpd():
    """
    Generate a random VPD83 NAA_Type3 ID
    """
    vpd = ['60']
    for _ in range(0, 15):
        vpd.append(str('%02x' % (random.randint(0, 255))))
    return "".join(vpd)


class PoolRAID(object):
    _RAID_DISK_CHK = {
        Volume.RAID_TYPE_JBOD: lambda x: x > 0,
        Volume.RAID_TYPE_RAID0: lambda x: x > 0,
        Volume.RAID_TYPE_RAID1: lambda x: x == 2,
        Volume.RAID_TYPE_RAID3: lambda x: x >= 3,
        Volume.RAID_TYPE_RAID4: lambda x: x >= 3,
        Volume.RAID_TYPE_RAID5: lambda x: x >= 3,
        Volume.RAID_TYPE_RAID6: lambda x: x >= 4,
        Volume.RAID_TYPE_RAID10: lambda x: x >= 4 and x % 2 == 0,
        Volume.RAID_TYPE_RAID15: lambda x: x >= 6 and x % 2 == 0,
        Volume.RAID_TYPE_RAID16: lambda x: x >= 8 and x % 2 == 0,
        Volume.RAID_TYPE_RAID50: lambda x: x >= 6 and x % 2 == 0,
        Volume.RAID_TYPE_RAID60: lambda x: x >= 8 and x % 2 == 0,
        Volume.RAID_TYPE_RAID51: lambda x: x >= 6 and x % 2 == 0,
        Volume.RAID_TYPE_RAID61: lambda x: x >= 8 and x % 2 == 0,
    }

    _RAID_PARITY_DISK_COUNT_FUNC = {
        Volume.RAID_TYPE_JBOD: lambda x: x,
        Volume.RAID_TYPE_RAID0: lambda x: x,
        Volume.RAID_TYPE_RAID1: lambda x: 1,
        Volume.RAID_TYPE_RAID3: lambda x: x - 1,
        Volume.RAID_TYPE_RAID4: lambda x: x - 1,
        Volume.RAID_TYPE_RAID5: lambda x: x - 1,
        Volume.RAID_TYPE_RAID6: lambda x: x - 2,
        Volume.RAID_TYPE_RAID10: lambda x: x / 2,
        Volume.RAID_TYPE_RAID15: lambda x: x / 2 - 1,
        Volume.RAID_TYPE_RAID16: lambda x: x / 2 - 2,
        Volume.RAID_TYPE_RAID50: lambda x: x - 2,
        Volume.RAID_TYPE_RAID60: lambda x: x - 4,
        Volume.RAID_TYPE_RAID51: lambda x: x / 2 - 1,
        Volume.RAID_TYPE_RAID61: lambda x: x / 2 - 2,
    }

    @staticmethod
    def data_disk_count(raid_type, disk_count):
        """
        Return a integer indicating how many disks should be used as
        real data(not mirrored or parity) disks.
        Treating RAID 5 and 6 using fixed parity disk.
        """
        if raid_type not in PoolRAID._RAID_DISK_CHK.keys():
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "data_disk_count(): Got unsupported raid type(%d)" %
                raid_type)

        if PoolRAID._RAID_DISK_CHK[raid_type](disk_count) is False:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "data_disk_count(): Illegal disk count"
                "(%d) for raid type(%d)" % (disk_count, raid_type))
        return PoolRAID._RAID_PARITY_DISK_COUNT_FUNC[raid_type](disk_count)


class BackStore(object):
    VERSION = "3.4"
    VERSION_SIGNATURE = 'LSM_SIMULATOR_DATA_%s_%s' % (VERSION, md5(VERSION))
    JOB_DEFAULT_DURATION = 1
    JOB_DATA_TYPE_VOL = 1
    JOB_DATA_TYPE_FS = 2
    JOB_DATA_TYPE_FS_SNAP = 3

    SYS_ID = "sim-01"
    SYS_NAME = "LSM simulated storage plug-in"
    BLK_SIZE = 512
    DEFAULT_STRIP_SIZE = 128 * 1024  # 128 KiB

    _LIST_SPLITTER = '#'

    SYS_KEY_LIST = ['id', 'name', 'status', 'status_info', 'version']

    POOL_KEY_LIST = [
        'id', 'name', 'status', 'status_info',
        'element_type', 'unsupported_actions', 'raid_type',
        'member_type', 'parent_pool_id', 'total_space', 'free_space',
        'strip_size']

    DISK_KEY_LIST = [
        'id', 'name', 'total_space', 'disk_type', 'status',
        'owner_pool_id', 'role']

    VOL_KEY_LIST = [
        'id', 'vpd83', 'name', 'total_space', 'consumed_size',
        'pool_id', 'admin_state', 'thinp', 'is_hw_raid_vol']

    TGT_KEY_LIST = [
        'id', 'port_type', 'service_address', 'network_address',
        'physical_address', 'physical_name']

    AG_KEY_LIST = ['id', 'name', 'init_type', 'init_ids_str']

    JOB_KEY_LIST = ['id', 'duration', 'timestamp', 'data_type', 'data_id']

    FS_KEY_LIST = [
        'id', 'name', 'total_space', 'free_space', 'consumed_size',
        'pool_id']

    FS_SNAP_KEY_LIST = [
        'id', 'fs_id', 'name', 'timestamp']

    EXP_KEY_LIST = [
        'id', 'fs_id', 'exp_path', 'auth_type', 'anon_uid', 'anon_gid',
        'options', 'exp_root_hosts_str', 'exp_rw_hosts_str',
        'exp_ro_hosts_str']

    SUPPORTED_VCR_RAID_TYPES = [
        Volume.RAID_TYPE_RAID0, Volume.RAID_TYPE_RAID1,
        Volume.RAID_TYPE_RAID5, Volume.RAID_TYPE_RAID6,
        Volume.RAID_TYPE_RAID10, Volume.RAID_TYPE_RAID50,
        Volume.RAID_TYPE_RAID60]

    SUPPORTED_VCR_STRIP_SIZES = [
        8 * 1024, 16 * 1024, 32 * 1024, 64 * 1024, 128 * 1024, 256 * 1024,
        512 * 1024, 1024 * 1024]

    def __init__(self, statefile, timeout):
        if not os.path.exists(statefile):
            os.close(os.open(statefile, os.O_WRONLY | os.O_CREAT))
            # Due to umask, os.open() created file migt not be 666 permission.
            os.chmod(statefile, 0o666)

        self.statefile = statefile
        self.lastrowid = None
        self.sql_conn = sqlite3.connect(
            statefile, timeout=int(timeout/1000), isolation_level="IMMEDIATE")
        # Create tables no matter exist or not. No lock required.

        sql_cmd = "PRAGMA foreign_keys = ON;\n"

        sql_cmd += (
            """
            CREATE TABLE systems (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            status INTEGER NOT NULL,
            status_info TEXT,
            version TEXT NOT NULL);
            """)
            # ^ version hold the signature of data

        sql_cmd += (
            "CREATE TABLE tgts ("
            "id INTEGER PRIMARY KEY, "
            "port_type INTEGER NOT NULL, "
            "service_address TEXT NOT NULL, "
            "network_address TEXT NOT NULL, "
            "physical_address TEXT NOT NULL, "
            "physical_name TEXT NOT NULL);\n")

        sql_cmd += (
            "CREATE TABLE pools ("
            "id INTEGER PRIMARY KEY, "
            "name TEXT UNIQUE NOT NULL, "
            "status INTEGER NOT NULL, "
            "status_info TEXT, "
            "element_type INTEGER NOT NULL, "
            "unsupported_actions INTEGER, "
            "raid_type INTEGER NOT NULL, "
            "parent_pool_id INTEGER, "
            # ^ Indicate this pool is allocated from # other pool
            "member_type INTEGER, "
            "strip_size INTEGER, "
            "total_space LONG);\n")
            # ^ total_space here is only for sub-pool (pool from pool)

        sql_cmd += (
            "CREATE TABLE disks ("
            "id INTEGER PRIMARY KEY, "
            "total_space LONG NOT NULL, "
            "disk_type INTEGER NOT NULL, "
            "status INTEGER NOT NULL, "
            "disk_prefix TEXT NOT NULL, "
            "owner_pool_id INTEGER, "
            # ^ Indicate this disk is used to assemble a pool
            "role TEXT,"
            "FOREIGN KEY(owner_pool_id) "
            "REFERENCES pools(id) ON DELETE CASCADE );\n")

        sql_cmd += (
            "CREATE TABLE volumes ("
            "id INTEGER PRIMARY KEY, "
            "vpd83 TEXT NOT NULL, "
            "name TEXT UNIQUE NOT NULL, "
            "total_space LONG NOT NULL, "
            "consumed_size LONG NOT NULL, "
            # ^ Reserved for future thinp support.
            "admin_state INTEGER, "
            "thinp INTEGER NOT NULL, "
            "is_hw_raid_vol INTEGER, "
            # ^ Once its volume deleted, pool will be delete also.
            # For HW RAID simulation only.

            "pool_id INTEGER NOT NULL, "
            "FOREIGN KEY(pool_id) "
            "REFERENCES pools(id) ON DELETE CASCADE);\n")

        sql_cmd += (
            "CREATE TABLE ags ("
            "id INTEGER PRIMARY KEY, "
            "name TEXT UNIQUE NOT NULL);\n")

        sql_cmd += (
            "CREATE TABLE inits ("
            "id TEXT UNIQUE NOT NULL, "
            "init_type INTEGER NOT NULL, "
            "owner_ag_id INTEGER NOT NULL, "
            "FOREIGN KEY(owner_ag_id) "
            "REFERENCES ags(id) ON DELETE CASCADE);\n")

        sql_cmd += (
            "CREATE TABLE vol_masks ("
            "vol_id INTEGER NOT NULL, "
            "ag_id INTEGER NOT NULL, "
            "FOREIGN KEY(vol_id) REFERENCES volumes(id) ON DELETE CASCADE, "
            "FOREIGN KEY(ag_id) REFERENCES ags(id) ON DELETE CASCADE);\n")

        sql_cmd += (
            "CREATE TABLE vol_reps ("
            "rep_type INTEGER, "
            "src_vol_id INTEGER NOT NULL, "
            "dst_vol_id INTEGER NOT NULL, "
            "FOREIGN KEY(src_vol_id) "
            "REFERENCES volumes(id) ON DELETE CASCADE, "
            "FOREIGN KEY(dst_vol_id) "
            "REFERENCES volumes(id) ON DELETE CASCADE);\n")

        sql_cmd += (
            "CREATE TABLE fss ("
            "id INTEGER PRIMARY KEY, "
            "name TEXT UNIQUE NOT NULL, "
            "total_space LONG NOT NULL, "
            "consumed_size LONG NOT NULL, "
            "free_space LONG, "
            "pool_id INTEGER NOT NULL, "
            "FOREIGN KEY(pool_id) "
            "REFERENCES pools(id) ON DELETE CASCADE);\n")

        sql_cmd += (
            "CREATE TABLE fs_snaps ("
            "id INTEGER PRIMARY KEY, "
            "name TEXT UNIQUE NOT NULL, "
            "fs_id INTEGER NOT NULL, "
            "timestamp LONG NOT NULL, "
            "FOREIGN KEY(fs_id) "
            "REFERENCES fss(id) ON DELETE CASCADE);\n")

        sql_cmd += (
            "CREATE TABLE fs_clones ("
            "src_fs_id INTEGER NOT NULL, "
            "dst_fs_id INTEGER NOT NULL, "
            "FOREIGN KEY(src_fs_id) "
            "REFERENCES fss(id) ON DELETE CASCADE, "
            "FOREIGN KEY(dst_fs_id) "
            "REFERENCES fss(id) ON DELETE CASCADE);\n")

        sql_cmd += (
            "CREATE TABLE exps ("
            "id INTEGER PRIMARY KEY, "
            "fs_id INTEGER NOT NULL, "
            "exp_path TEXT UNIQUE NOT NULL, "
            "auth_type TEXT, "
            "anon_uid INTEGER, "
            "anon_gid INTEGER, "
            "options TEXT, "
            "FOREIGN KEY(fs_id) "
            "REFERENCES fss(id) ON DELETE CASCADE);\n")

        sql_cmd += (
            "CREATE TABLE exp_root_hosts("
            "host TEXT NOT NULL, "
            "exp_id INTEGER NOT NULL, "
            "FOREIGN KEY(exp_id) "
            "REFERENCES exps(id) ON DELETE CASCADE);\n")

        sql_cmd += (
            "CREATE TABLE exp_rw_hosts("
            "host TEXT NOT NULL, "
            "exp_id INTEGER NOT NULL, "
            "FOREIGN KEY(exp_id) "
            "REFERENCES exps(id) ON DELETE CASCADE);\n")

        sql_cmd += (
            "CREATE TABLE exp_ro_hosts("
            "host TEXT NOT NULL, "
            "exp_id INTEGER NOT NULL, "
            "FOREIGN KEY(exp_id) "
            "REFERENCES exps(id) ON DELETE CASCADE);\n")

        sql_cmd += (
            "CREATE TABLE jobs ("
            "id INTEGER PRIMARY KEY, "
            "duration INTEGER NOT NULL, "
            "timestamp TEXT NOT NULL, "
            "data_type INTEGER, "
            "data_id TEXT);\n")

        # Create views
        sql_cmd += (
            """
            CREATE VIEW pools_view AS
                SELECT
                    pool0.id,
                    pool0.name,
                    pool0.status,
                    pool0.status_info,
                    pool0.element_type,
                    pool0.unsupported_actions,
                    pool0.raid_type,
                    pool0.member_type,
                    pool0.parent_pool_id,
                    pool0.strip_size,
                    pool1.total_space total_space,
                    pool1.total_space -
                    pool2.vol_consumed_size  -
                    pool3.fs_consumed_size -
                    pool4.sub_pool_consumed_size free_space
                FROM
                    pools pool0
                        LEFT JOIN (
                            SELECT
                                pool.id,
                                    ifnull(pool.total_space,
                                        ifnull(SUM(disk.total_space), 0))
                                total_space
                            FROM pools pool
                                LEFT JOIN disks disk
                                    ON pool.id = disk.owner_pool_id AND
                                       disk.role = 'DATA'
                            GROUP BY
                                pool.id
                        ) pool1 ON pool0.id = pool1.id

                        LEFT JOIN (
                            SELECT
                                pool.id,
                                ifnull(SUM(volume.consumed_size), 0)
                                vol_consumed_size
                            FROM pools pool
                                LEFT JOIN volumes volume
                                    ON volume.pool_id = pool.id
                            GROUP BY
                                pool.id
                        ) pool2 ON pool0.id = pool2.id

                        LEFT JOIN (
                            SELECT
                                pool.id,
                                ifnull(SUM(fs.consumed_size), 0)
                                fs_consumed_size
                            FROM pools pool
                                LEFT JOIN fss fs
                                    ON fs.pool_id = pool.id
                            GROUP BY
                                pool.id
                        ) pool3 ON pool0.id = pool3.id

                        LEFT JOIN (
                            SELECT
                                pool.id,
                                ifnull(SUM(sub_pool.total_space), 0)
                                sub_pool_consumed_size
                            FROM pools pool
                                LEFT JOIN pools sub_pool
                                    ON sub_pool.parent_pool_id = pool.id
                            GROUP BY
                                pool.id
                        ) pool4 ON pool0.id = pool4.id

                GROUP BY
                    pool0.id;
            """)
        sql_cmd += (
            """
            CREATE VIEW disks_view AS
                SELECT
                    id,
                        disk_prefix || '_' || id
                    name,
                    total_space,
                    disk_type,
                    role,
                    status,
                    owner_pool_id
                FROM
                    disks
            ;
            """)
        sql_cmd += (
            """
            CREATE VIEW volumes_by_ag_view AS
                SELECT
                    vol.id,
                    vol.vpd83,
                    vol.name,
                    vol.total_space,
                    vol.consumed_size,
                    vol.pool_id,
                    vol.admin_state,
                    vol.thinp,
                    vol.is_hw_raid_vol,
                    vol_mask.ag_id ag_id
                FROM
                    volumes vol
                        LEFT JOIN vol_masks vol_mask
                            ON vol_mask.vol_id = vol.id
            ;
            """)

        sql_cmd += (
            """
            CREATE VIEW ags_view AS
                SELECT
                    ag.id,
                    ag.name,
                        CASE
                            WHEN count(DISTINCT init.init_type) = 1
                                THEN init.init_type
                            WHEN count(DISTINCT init.init_type) = 2
                                THEN %s
                            ELSE %s
                        END
                    init_type,
                    group_concat(init.id, '%s') init_ids_str
                FROM
                    ags ag
                        LEFT JOIN inits init
                            ON ag.id = init.owner_ag_id
                GROUP BY
                    ag.id
                ORDER BY
                    init.init_type
            ;
            """ % (
            AccessGroup.INIT_TYPE_ISCSI_WWPN_MIXED,
            AccessGroup.INIT_TYPE_UNKNOWN, BackStore._LIST_SPLITTER))

        sql_cmd += (
            """
            CREATE VIEW ags_by_vol_view AS
                SELECT
                    ag_new.id,
                    ag_new.name,
                    ag_new.init_type,
                    ag_new.init_ids_str,
                    vol_mask.vol_id vol_id
                FROM
                    (
                        SELECT
                            ag.id,
                            ag.name,
                                CASE
                                    WHEN count(DISTINCT init.init_type) = 1
                                        THEN init.init_type
                                    WHEN count(DISTINCT init.init_type) = 2
                                        THEN %s
                                    ELSE %s
                                END
                            init_type,
                            group_concat(init.id, '%s') init_ids_str
                        FROM
                            ags ag
                                LEFT JOIN inits init
                                    ON ag.id = init.owner_ag_id
                        GROUP BY
                            ag.id
                        ORDER BY
                            init.init_type
                    ) ag_new
                        LEFT JOIN vol_masks vol_mask
                            ON vol_mask.ag_id = ag_new.id
            ;
            """ % (
            AccessGroup.INIT_TYPE_ISCSI_WWPN_MIXED,
            AccessGroup.INIT_TYPE_UNKNOWN, BackStore._LIST_SPLITTER))

        sql_cmd += (
            """
            CREATE VIEW exps_view AS
                SELECT
                    exp.id,
                    exp.fs_id,
                    exp.exp_path,
                    exp.auth_type,
                    exp.anon_uid,
                    exp.anon_gid,
                    exp.options,
                    exp2.exp_root_hosts_str,
                    exp3.exp_rw_hosts_str,
                    exp4.exp_ro_hosts_str
                FROM
                    exps exp
                        LEFT JOIN (
                            SELECT
                                exp_t2.id,
                                    group_concat(
                                        exp_root_host.host, '%s')
                                exp_root_hosts_str
                            FROM
                                exps exp_t2
                                LEFT JOIN exp_root_hosts exp_root_host
                                    ON exp_t2.id = exp_root_host.exp_id
                            GROUP BY
                                exp_t2.id
                        ) exp2
                            ON exp.id = exp2.id
                        LEFT JOIN (
                            SELECT
                                exp_t3.id,
                                    group_concat(
                                        exp_rw_host.host, '%s')
                                exp_rw_hosts_str
                            FROM
                                exps exp_t3
                                LEFT JOIN exp_rw_hosts exp_rw_host
                                    ON exp_t3.id = exp_rw_host.exp_id
                            GROUP BY
                                exp_t3.id
                        ) exp3
                            ON exp.id = exp3.id
                        LEFT JOIN (
                            SELECT
                                exp_t4.id,
                                    group_concat(
                                        exp_ro_host.host, '%s')
                                exp_ro_hosts_str
                            FROM
                                exps exp_t4
                                LEFT JOIN exp_ro_hosts exp_ro_host
                                    ON exp_t4.id = exp_ro_host.exp_id
                            GROUP BY
                                exp_t4.id
                        ) exp4
                            ON exp.id = exp4.id
                GROUP BY
                    exp.id;
            ;
            """ % (
            BackStore._LIST_SPLITTER, BackStore._LIST_SPLITTER,
            BackStore._LIST_SPLITTER))

        sql_cur = self.sql_conn.cursor()
        try:
            sql_cur.executescript(sql_cmd)
        except sqlite3.OperationalError as sql_error:
            if 'already exists' in str(sql_error):
                pass
            else:
                raise sql_error
        except sqlite3.DatabaseError as sql_error:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Stored simulator state incompatible with "
                "simulator, please move or delete %s" % self.statefile)

    def _check_version(self):
        sim_syss = self.sim_syss()
        if len(sim_syss) == 0 or not sim_syss[0]:
            return False
        else:
            if 'version' in sim_syss[0] and \
               sim_syss[0]['version'] == BackStore.VERSION_SIGNATURE:
                return True

        raise LsmError(
            ErrorNumber.INVALID_ARGUMENT,
            "Stored simulator state incompatible with "
            "simulator, please move or delete %s" % self.statefile)

    def check_version_and_init(self):
        """
        Raise error if version not match.
        If empty database found, initiate.
        """
        # The complex lock workflow is all caused by python sqlite3 do
        # autocommit for "CREATE TABLE" command.
        self.trans_begin()
        if self._check_version():
            self.trans_commit()
            return
        else:
            self._data_add(
                'systems',
                {
                    'id': BackStore.SYS_ID,
                    'name': BackStore.SYS_NAME,
                    'status': System.STATUS_OK,
                    'status_info': "",
                    'version': BackStore.VERSION_SIGNATURE,
                })

            size_bytes_2t = size_human_2_size_bytes('2TiB')
            size_bytes_512g = size_human_2_size_bytes('512GiB')
            # Add 2 SATA disks(2TiB)
            pool_1_disks = []
            for _ in range(0, 2):
                self._data_add(
                    'disks',
                    {
                        'disk_prefix': "2TiB SATA Disk",
                        'total_space': size_bytes_2t,
                        'disk_type': Disk.TYPE_SATA,
                        'status': Disk.STATUS_OK,
                    })
                pool_1_disks.append(self.lastrowid)

            test_pool_disks = []
            # Add 6 SAS disks(2TiB)
            for _ in range(0, 6):
                self._data_add(
                    'disks',
                    {
                        'disk_prefix': "2TiB SAS Disk",
                        'total_space': size_bytes_2t,
                        'disk_type': Disk.TYPE_SAS,
                        'status': Disk.STATUS_OK,
                    })
                if len(test_pool_disks) < 2:
                    test_pool_disks.append(self.lastrowid)

            ssd_pool_disks = []
            # Add 5 SSD disks(512GiB)
            for _ in range(0, 5):
                self._data_add(
                    'disks',
                    {
                        'disk_prefix': "512GiB SSD Disk",
                        'total_space': size_bytes_512g,
                        'disk_type': Disk.TYPE_SSD,
                        'status': Disk.STATUS_OK,
                    })
                if len(ssd_pool_disks) < 2:
                    ssd_pool_disks.append(self.lastrowid)

            # Add 7 SSD disks(2TiB)
            for _ in range(0, 7):
                self._data_add(
                    'disks',
                    {
                        'disk_prefix': "2TiB SSD Disk",
                        'total_space': size_bytes_2t,
                        'disk_type': Disk.TYPE_SSD,
                        'status': Disk.STATUS_OK,
                    })

            pool_1_id = self.sim_pool_create_from_disk(
                name='Pool 1',
                raid_type=Volume.RAID_TYPE_RAID1,
                sim_disk_ids=pool_1_disks,
                element_type=Pool.ELEMENT_TYPE_POOL |
                Pool.ELEMENT_TYPE_FS |
                Pool.ELEMENT_TYPE_VOLUME |
                Pool.ELEMENT_TYPE_DELTA |
                Pool.ELEMENT_TYPE_SYS_RESERVED,
                unsupported_actions=Pool.UNSUPPORTED_VOLUME_GROW |
                Pool.UNSUPPORTED_VOLUME_SHRINK)

            self.sim_pool_create_sub_pool(
                name='Pool 2(sub pool of Pool 1)',
                parent_pool_id=pool_1_id,
                element_type=Pool.ELEMENT_TYPE_FS |
                Pool.ELEMENT_TYPE_VOLUME |
                Pool.ELEMENT_TYPE_DELTA,
                size=size_bytes_512g)

            self.sim_pool_create_from_disk(
                name='Pool 3',
                raid_type=Volume.RAID_TYPE_RAID1,
                sim_disk_ids=ssd_pool_disks,
                element_type=Pool.ELEMENT_TYPE_FS |
                Pool.ELEMENT_TYPE_VOLUME |
                Pool.ELEMENT_TYPE_DELTA)

            self.sim_pool_create_from_disk(
                name='lsm_test_aggr',
                element_type=Pool.ELEMENT_TYPE_FS |
                Pool.ELEMENT_TYPE_VOLUME |
                Pool.ELEMENT_TYPE_DELTA,
                raid_type=Volume.RAID_TYPE_RAID0,
                sim_disk_ids=test_pool_disks)

            self._data_add(
                'tgts',
                {
                    'port_type': TargetPort.TYPE_FC,
                    'service_address': '50:0a:09:86:99:4b:8d:c5',
                    'network_address': '50:0a:09:86:99:4b:8d:c5',
                    'physical_address': '50:0a:09:86:99:4b:8d:c5',
                    'physical_name': 'FC_a_0b',
                })

            self._data_add(
                'tgts',
                {
                    'port_type': TargetPort.TYPE_FCOE,
                    'service_address': '50:0a:09:86:99:4b:8d:c6',
                    'network_address': '50:0a:09:86:99:4b:8d:c6',
                    'physical_address': '50:0a:09:86:99:4b:8d:c6',
                    'physical_name': 'FCoE_b_0c',
                })
            self._data_add(
                'tgts',
                {
                    'port_type': TargetPort.TYPE_ISCSI,
                    'service_address': 'iqn.1986-05.com.example:sim-tgt-03',
                    'network_address': 'sim-iscsi-tgt-3.example.com:3260',
                    'physical_address': 'a4:4e:31:47:f4:e0',
                    'physical_name': 'iSCSI_c_0d',
                })
            self._data_add(
                'tgts',
                {
                    'port_type': TargetPort.TYPE_ISCSI,
                    'service_address': 'iqn.1986-05.com.example:sim-tgt-03',
                    'network_address': '10.0.0.1:3260',
                    'physical_address': 'a4:4e:31:47:f4:e1',
                    'physical_name': 'iSCSI_c_0e',
                })
            self._data_add(
                'tgts',
                {
                    'port_type': TargetPort.TYPE_ISCSI,
                    'service_address': 'iqn.1986-05.com.example:sim-tgt-03',
                    'network_address': '[2001:470:1f09:efe:a64e:31ff::1]:3260',
                    'physical_address': 'a4:4e:31:47:f4:e1',
                    'physical_name': 'iSCSI_c_0e',
                })

            self.trans_commit()
            return

    def _sql_exec(self, sql_cmd, key_list=None):
        """
        Execute sql command and get all output.
        If key_list is not None, will convert returned sql data to a list of
        dictionaries.
        """
        sql_cur = self.sql_conn.cursor()
        sql_cur.execute(sql_cmd)
        self.lastrowid = sql_cur.lastrowid
        sql_output = sql_cur.fetchall()
        if key_list and sql_output:
            return list(
                dict(zip(key_list, value_list))
                for value_list in sql_output
                if value_list)
        else:
            return sql_output

    def _get_table(self, table_name, key_list):
        sql_cmd = "SELECT %s FROM %s" % (",".join(key_list), table_name)
        return self._sql_exec(sql_cmd, key_list)

    def trans_begin(self):
        self.sql_conn.execute("BEGIN IMMEDIATE TRANSACTION;")

    def trans_commit(self):
        self.sql_conn.commit()

    def trans_rollback(self):
        self.sql_conn.rollback()

    def _data_add(self, table_name, data_dict):
        keys = data_dict.keys()
        values = ['' if v is None else str(v) for v in data_dict.values()]

        sql_cmd = "INSERT INTO %s (%s) VALUES (%s);" % \
                  (table_name,
                   "'%s'" % ("', '".join(keys)),
                   "'%s'" % ("', '".join(values)))
        self._sql_exec(sql_cmd)

    def _data_find(self, table, condition, key_list, flag_unique=False):
        sql_cmd = "SELECT %s FROM %s WHERE %s" % (
            ",".join(key_list), table, condition)
        sim_datas = self._sql_exec(sql_cmd, key_list)
        if flag_unique:
            if len(sim_datas) == 0:
                return None
            elif len(sim_datas) == 1:
                return sim_datas[0]
            else:
                raise LsmError(
                    ErrorNumber.PLUGIN_BUG,
                    "_data_find(): Got non-unique data: %s" % locals())
        else:
            return sim_datas

    def _data_update(self, table, data_id, column_name, value):
        if value is None:
            value = ''

        sql_cmd = "UPDATE %s SET %s='%s' WHERE id='%s'" % \
            (table, column_name, value, data_id)

        self._sql_exec(sql_cmd)

    def _data_delete(self, table, condition):
        sql_cmd = "DELETE FROM %s WHERE %s;" % (table, condition)
        self._sql_exec(sql_cmd)

    def sim_job_create(self, job_data_type=None, data_id=None):
        """
        Return a job id(Integer)
        """
        self._data_add(
            "jobs",
            {
                "duration": os.getenv(
                    "LSM_SIM_TIME", BackStore.JOB_DEFAULT_DURATION),
                "timestamp": time.time(),
                "data_type": job_data_type,
                "data_id": data_id,
            })
        return self.lastrowid

    def sim_job_delete(self, sim_job_id):
        self._data_delete('jobs', 'id="%s"' % sim_job_id)

    def sim_job_status(self, sim_job_id):
        """
        Return (progress, data_type, data) tuple.
        progress is the integer of percent.
        """
        sim_job = self._data_find(
            'jobs', 'id=%s' % sim_job_id, BackStore.JOB_KEY_LIST,
            flag_unique=True)
        if sim_job is None:
            raise LsmError(
                ErrorNumber.NOT_FOUND_JOB, "Job not found")

        progress = int(
            (time.time() - float(sim_job['timestamp'])) /
            sim_job['duration'] * 100)

        data = None
        data_type = None

        if progress >= 100:
            progress = 100
            if sim_job['data_type'] == BackStore.JOB_DATA_TYPE_VOL:
                data = self.sim_vol_of_id(sim_job['data_id'])
                data_type = sim_job['data_type']
            elif sim_job['data_type'] == BackStore.JOB_DATA_TYPE_FS:
                data = self.sim_fs_of_id(sim_job['data_id'])
                data_type = sim_job['data_type']
            elif sim_job['data_type'] == BackStore.JOB_DATA_TYPE_FS_SNAP:
                data = self.sim_fs_snap_of_id(sim_job['data_id'])
                data_type = sim_job['data_type']

        return (progress, data_type, data)

    def sim_syss(self):
        """
        Return a list of sim_sys dict.
        """
        return self._get_table('systems', BackStore.SYS_KEY_LIST)

    def sim_disk_ids_of_pool(self, sim_pool_id):
        return list(
            d['id']
            for d in self._data_find(
                'disks', 'owner_pool_id="%s"' % sim_pool_id, ['id']))

    def sim_disks(self):
        """
        Return a list of sim_disk dict.
        """
        return self._get_table('disks_view', BackStore.DISK_KEY_LIST)

    def sim_pools(self):
        """
        Return a list of sim_pool dict.
        """
        return self._get_table('pools_view', BackStore.POOL_KEY_LIST)

    def sim_pool_of_id(self, sim_pool_id):
        return self._sim_data_of_id(
            "pools_view", sim_pool_id, BackStore.POOL_KEY_LIST,
            ErrorNumber.NOT_FOUND_POOL, "Pool")

    def sim_pool_create_from_disk(self, name, sim_disk_ids, raid_type,
                                  element_type, unsupported_actions=0,
                                  strip_size=0):
        if strip_size == 0:
            strip_size = BackStore.DEFAULT_STRIP_SIZE

        self._data_add(
            'pools',
            {
                'name': name,
                'status': Pool.STATUS_OK,
                'status_info': '',
                'element_type': element_type,
                'unsupported_actions': unsupported_actions,
                'raid_type': raid_type,
                'member_type': Pool.MEMBER_TYPE_DISK,
                'strip_size': strip_size,
            })

        data_disk_count = PoolRAID.data_disk_count(
            raid_type, len(sim_disk_ids))

        # update disk owner
        sim_pool_id = self.lastrowid
        for sim_disk_id in sim_disk_ids[:data_disk_count]:
            self._data_update(
                'disks', sim_disk_id, 'owner_pool_id', sim_pool_id)
            self._data_update(
                'disks', sim_disk_id, 'role', 'DATA')

        for sim_disk_id in sim_disk_ids[data_disk_count:]:
            self._data_update(
                'disks', sim_disk_id, 'owner_pool_id', sim_pool_id)
            self._data_update(
                'disks', sim_disk_id, 'role', 'PARITY')

        return sim_pool_id

    def sim_pool_create_sub_pool(self, name, parent_pool_id, size,
                                 element_type, unsupported_actions=0):
        self._data_add(
            'pools',
            {
                'name': name,
                'status': Pool.STATUS_OK,
                'status_info': '',
                'element_type': element_type,
                'unsupported_actions': unsupported_actions,
                'raid_type': Volume.RAID_TYPE_OTHER,
                'member_type': Pool.MEMBER_TYPE_POOL,
                'parent_pool_id': parent_pool_id,
                'total_space': size,
            })
        return self.lastrowid

    def sim_pool_disks_count(self, sim_pool_id):
        return self._sql_exec(
            "SELECT COUNT(id) FROM disks WHERE owner_pool_id=%s;" %
            sim_pool_id)[0][0]

    def sim_pool_data_disks_count(self, sim_pool_id=None):
        return self._sql_exec(
            "SELECT COUNT(id) FROM disks WHERE "
            "owner_pool_id=%s and role='DATA';" % sim_pool_id)[0][0]

    def sim_vols(self, sim_ag_id=None):
        """
        Return a list of sim_vol dict.
        """
        if sim_ag_id:
            return self._data_find(
                'volumes_by_ag_view', 'ag_id=%s' % sim_ag_id,
                BackStore.VOL_KEY_LIST)
        else:
            return self._get_table('volumes', BackStore.VOL_KEY_LIST)

    def _sim_data_of_id(self, table_name, data_id, key_list, lsm_error_no,
                        data_name):
        sim_data = self._data_find(
            table_name, 'id=%s' % data_id, key_list, flag_unique=True)
        if sim_data is None:
            if lsm_error_no:
                raise LsmError(
                    lsm_error_no, "%s not found" % data_name)
            else:
                return None
        return sim_data

    def sim_vol_of_id(self, sim_vol_id):
        """
        Return sim_vol if found. Raise error if not found.
        """
        return self._sim_data_of_id(
            "volumes", sim_vol_id, BackStore.VOL_KEY_LIST,
            ErrorNumber.NOT_FOUND_VOLUME,
            "Volume")

    def _check_pool_free_space(self, sim_pool_id, size_bytes):
        sim_pool = self.sim_pool_of_id(sim_pool_id)

        if (sim_pool['free_space'] < size_bytes):
            raise LsmError(ErrorNumber.NOT_ENOUGH_SPACE,
                           "Insufficient space in pool")

    @staticmethod
    def _block_rounding(size_bytes):
        return (size_bytes + BackStore.BLK_SIZE - 1) / \
            BackStore.BLK_SIZE * BackStore.BLK_SIZE

    def sim_vol_create(self, name, size_bytes, sim_pool_id, thinp,
                       is_hw_raid_vol=0):

        size_bytes = BackStore._block_rounding(size_bytes)
        self._check_pool_free_space(sim_pool_id, size_bytes)
        sim_vol = dict()
        sim_vol['vpd83'] = _random_vpd()
        sim_vol['name'] = name
        sim_vol['thinp'] = thinp
        sim_vol['pool_id'] = sim_pool_id
        sim_vol['total_space'] = size_bytes
        sim_vol['consumed_size'] = size_bytes
        sim_vol['admin_state'] = Volume.ADMIN_STATE_ENABLED
        sim_vol['is_hw_raid_vol'] = is_hw_raid_vol

        try:
            self._data_add("volumes", sim_vol)
        except sqlite3.IntegrityError as sql_error:
            raise LsmError(
                ErrorNumber.NAME_CONFLICT,
                "Name '%s' is already in use by other volume" % name)

        return self.lastrowid

    def sim_vol_delete(self, sim_vol_id):
        """
        This does not check whether volume exist or not.
        """
        # Check existence.
        sim_vol = self.sim_vol_of_id(sim_vol_id)

        if self._sim_ag_ids_of_masked_vol(sim_vol_id):
            raise LsmError(
                ErrorNumber.IS_MASKED,
                "Volume is masked to access group")

        dst_sim_vol_ids = self.dst_sim_vol_ids_of_src(sim_vol_id)
        if len(dst_sim_vol_ids) >= 1:
            for dst_sim_vol_id in dst_sim_vol_ids:
                if dst_sim_vol_id != sim_vol_id:
                    # Don't raise error on volume internal replication.
                    raise LsmError(
                        ErrorNumber.PLUGIN_BUG,
                        "Requested volume is a replication source")
        if sim_vol['is_hw_raid_vol']:
            # Delete the parent pool instead if found a HW RAID volume.
            self._data_delete("pools", 'id="%s"' % sim_vol['pool_id'])
        else:
            self._data_delete("volumes", 'id="%s"' % sim_vol_id)

    def sim_vol_mask(self, sim_vol_id, sim_ag_id):
        self.sim_vol_of_id(sim_vol_id)
        self.sim_ag_of_id(sim_ag_id)
        exist_mask = self._data_find(
            'vol_masks', 'ag_id="%s" AND vol_id="%s"' %
            (sim_ag_id, sim_vol_id), ['vol_id'])
        if exist_mask:
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE,
                "Volume is already masked to requested access group")

        self._data_add(
            "vol_masks", {'ag_id': sim_ag_id, 'vol_id': sim_vol_id})

        return None

    def sim_vol_unmask(self, sim_vol_id, sim_ag_id):
        self.sim_vol_of_id(sim_vol_id)
        self.sim_ag_of_id(sim_ag_id)
        condition = 'ag_id="%s" AND vol_id="%s"' % (sim_ag_id, sim_vol_id)
        exist_mask = self._data_find('vol_masks', condition, ['vol_id'])
        if exist_mask:
            self._data_delete('vol_masks', condition)
        else:
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE,
                "Volume is not masked to requested access group")
        return None

    def _sim_vol_ids_of_masked_ag(self, sim_ag_id):
        return list(
            m['vol_id'] for m in self._data_find(
                'vol_masks', 'ag_id="%s"' % sim_ag_id, ['vol_id']))

    def _sim_ag_ids_of_masked_vol(self, sim_vol_id):
        return list(
            m['ag_id'] for m in self._data_find(
                'vol_masks', 'vol_id="%s"' % sim_vol_id, ['ag_id']))

    def sim_vol_resize(self, sim_vol_id, new_size_bytes):
        org_new_size_bytes = new_size_bytes
        new_size_bytes = BackStore._block_rounding(new_size_bytes)
        sim_vol = self.sim_vol_of_id(sim_vol_id)
        if sim_vol['total_space'] == new_size_bytes:
            if org_new_size_bytes != new_size_bytes:
                # Even volume size is identical to rounded size,
                # but it's not what user requested, hence we silently pass.
                return
            else:
                raise LsmError(
                    ErrorNumber.NO_STATE_CHANGE,
                    "Volume size is identical to requested")

        sim_pool = self.sim_pool_of_id(sim_vol['pool_id'])

        increment = new_size_bytes - sim_vol['total_space']

        if increment > 0:

            if sim_pool['unsupported_actions'] & Pool.UNSUPPORTED_VOLUME_GROW:
                raise LsmError(
                    ErrorNumber.NO_SUPPORT,
                    "Requested pool does not allow volume size grow")

            if sim_pool['free_space'] < increment:
                raise LsmError(
                    ErrorNumber.NOT_ENOUGH_SPACE, "Insufficient space in pool")

        elif sim_pool['unsupported_actions'] & Pool.UNSUPPORTED_VOLUME_SHRINK:
            raise LsmError(
                ErrorNumber.NO_SUPPORT,
                "Requested pool does not allow volume size grow")

        # TODO(Gris Ge): If a volume is in a replication relationship, resize
        #                should be handled properly.
        self._data_update(
            'volumes', sim_vol_id, "total_space", new_size_bytes)
        self._data_update(
            'volumes', sim_vol_id, "consumed_size", new_size_bytes)

    def dst_sim_vol_ids_of_src(self, src_sim_vol_id):
        """
        Return a list of dst_vol_id for provided source volume ID.
        """
        self.sim_vol_of_id(src_sim_vol_id)
        return list(
            d['dst_vol_id'] for d in self._data_find(
                'vol_reps', 'src_vol_id="%s"' % src_sim_vol_id,
                ['dst_vol_id']))

    def sim_vol_replica(self, src_sim_vol_id, dst_sim_vol_id, rep_type,
                        blk_ranges=None):
        self.sim_vol_of_id(src_sim_vol_id)
        self.sim_vol_of_id(dst_sim_vol_id)

        # TODO(Gris Ge): Use consumed_size < total_space to reflect the CLONE
        #                type.
        cur_src_sim_vol_ids = list(
            r['src_vol_id'] for r in self._data_find(
                'vol_reps', 'dst_vol_id="%s"' % dst_sim_vol_id,
                ['src_vol_id']))
        if len(cur_src_sim_vol_ids) == 1 and \
           cur_src_sim_vol_ids[0] == src_sim_vol_id:
            # src and dst match. Maybe user are overriding old setting.
            pass
        elif len(cur_src_sim_vol_ids) == 0:
            pass
        else:
            # TODO(Gris Ge): Need to introduce new API error
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "Target volume is already a replication target for other "
                "source volume")

        self._data_add(
            'vol_reps',
            {
                'src_vol_id': src_sim_vol_id,
                'dst_vol_id': dst_sim_vol_id,
                'rep_type': rep_type,
            })

        # No need to trace block range due to lack of query method.

    def sim_vol_src_replica_break(self, src_sim_vol_id):

        if not self.dst_sim_vol_ids_of_src(src_sim_vol_id):
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE,
                "Provided volume is not a replication source")

        self._data_delete(
            'vol_reps', 'src_vol_id="%s"' % src_sim_vol_id)

    def sim_vol_state_change(self, sim_vol_id, new_admin_state):
        sim_vol = self.sim_vol_of_id(sim_vol_id)
        if sim_vol['admin_state'] == new_admin_state:
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE,
                "Volume admin state is identical to requested")

        self._data_update(
            'volumes', sim_vol_id, "admin_state", new_admin_state)

    @staticmethod
    def _sim_ag_format(sim_ag):
        """
        Update 'init_type' and 'init_ids' of sim_ag
        """
        sim_ag['init_ids'] = sim_ag['init_ids_str'].split(
            BackStore._LIST_SPLITTER)
        del sim_ag['init_ids_str']
        return sim_ag

    def sim_ags(self, sim_vol_id=None):
        if sim_vol_id:
            sim_ags = self._data_find(
                'ags_by_vol_view', 'vol_id=%s' % sim_vol_id,
                BackStore.AG_KEY_LIST)
        else:
            sim_ags = self._get_table('ags_view', BackStore.AG_KEY_LIST)

        return [BackStore._sim_ag_format(a) for a in sim_ags]

    def _sim_init_create(self, init_type, init_id, sim_ag_id):
        try:
            self._data_add(
                "inits",
                {
                    'id': init_id,
                    'init_type': init_type,
                    'owner_ag_id': sim_ag_id
                })
        except sqlite3.IntegrityError as sql_error:
            raise LsmError(
                ErrorNumber.EXISTS_INITIATOR,
                "Initiator '%s' is already in use by other access group" %
                init_id)

    def iscsi_chap_auth_set(self, init_id, in_user, in_pass, out_user,
                            out_pass):
        # Currently, there is no API method to query status of iscsi CHAP.
        return None

    def sim_ag_create(self, name, init_type, init_id):
        try:
            self._data_add("ags", {'name': name})
            sim_ag_id = self.lastrowid
        except sqlite3.IntegrityError as sql_error:
            raise LsmError(
                ErrorNumber.NAME_CONFLICT,
                "Name '%s' is already in use by other access group" %
                name)

        self._sim_init_create(init_type, init_id, sim_ag_id)

        return sim_ag_id

    def sim_ag_delete(self, sim_ag_id):
        self.sim_ag_of_id(sim_ag_id)
        if self._sim_vol_ids_of_masked_ag(sim_ag_id):
            raise LsmError(
                ErrorNumber.IS_MASKED,
                "Access group has volume masked to")

        self._data_delete('ags', 'id="%s"' % sim_ag_id)

    def sim_ag_init_add(self, sim_ag_id, init_id, init_type):
        sim_ag = self.sim_ag_of_id(sim_ag_id)
        if init_id in sim_ag['init_ids']:
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE,
                "Initiator already in access group")

        if init_type != AccessGroup.INIT_TYPE_ISCSI_IQN and \
           init_type != AccessGroup.INIT_TYPE_WWPN:
            raise LsmError(
                ErrorNumber.NO_SUPPORT,
                "Only support iSCSI IQN and WWPN initiator type")

        self._sim_init_create(init_type, init_id, sim_ag_id)
        return None

    def sim_ag_init_delete(self, sim_ag_id, init_id):
        sim_ag = self.sim_ag_of_id(sim_ag_id)
        if init_id not in sim_ag['init_ids']:
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE,
                "Initiator is not in defined access group")
        if len(sim_ag['init_ids']) == 1:
            raise LsmError(
                ErrorNumber.LAST_INIT_IN_ACCESS_GROUP,
                "Refused to remove the last initiator from access group")

        self._data_delete('inits', 'id="%s"' % init_id)

    def sim_ag_of_id(self, sim_ag_id):
        sim_ag = self._sim_data_of_id(
            "ags_view", sim_ag_id, BackStore.AG_KEY_LIST,
            ErrorNumber.NOT_FOUND_ACCESS_GROUP,
            "Access Group")
        BackStore._sim_ag_format(sim_ag)
        return sim_ag

    def sim_fss(self):
        """
        Return a list of sim_fs dict.
        """
        return self._get_table('fss', BackStore.FS_KEY_LIST)

    def sim_fs_of_id(self, sim_fs_id, raise_error=True):
        lsm_error_no = ErrorNumber.NOT_FOUND_FS
        if not raise_error:
            lsm_error_no = None

        return self._sim_data_of_id(
            "fss", sim_fs_id, BackStore.FS_KEY_LIST, lsm_error_no,
            "File System")

    def sim_fs_create(self, name, size_bytes, sim_pool_id):
        size_bytes = BackStore._block_rounding(size_bytes)
        self._check_pool_free_space(sim_pool_id, size_bytes)
        try:
            self._data_add(
                "fss",
                {
                    'name': name,
                    'total_space': size_bytes,
                    'consumed_size': size_bytes,
                    'free_space': size_bytes,
                    'pool_id': sim_pool_id,
                })
        except sqlite3.IntegrityError as sql_error:
            raise LsmError(
                ErrorNumber.NAME_CONFLICT,
                "Name '%s' is already in use by other fs" % name)
        return self.lastrowid

    def sim_fs_delete(self, sim_fs_id):
        self.sim_fs_of_id(sim_fs_id)
        if self.clone_dst_sim_fs_ids_of_src(sim_fs_id):
            # TODO(Gris Ge): API does not have dedicate error for this
            #                scenario.
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "Requested file system is a clone source")

        if self.sim_fs_snaps(sim_fs_id):
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "Requested file system has snapshot attached")

        if self._data_find('exps', 'fs_id="%s"' % sim_fs_id, ['id']):
            # TODO(Gris Ge): API does not have dedicate error for this
            #                scenario
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "Requested file system is exported via NFS")

        self._data_delete("fss", 'id="%s"' % sim_fs_id)

    def sim_fs_resize(self, sim_fs_id, new_size_bytes):
        org_new_size_bytes = new_size_bytes
        new_size_bytes = BackStore._block_rounding(new_size_bytes)
        sim_fs = self.sim_fs_of_id(sim_fs_id)

        if sim_fs['total_space'] == new_size_bytes:
            if new_size_bytes != org_new_size_bytes:
                return
            else:
                raise LsmError(
                    ErrorNumber.NO_STATE_CHANGE,
                    "File System size is identical to requested")

        # TODO(Gris Ge): If a fs is in a clone/snapshot relationship, resize
        #                should be handled properly.

        sim_pool = self.sim_pool_of_id(sim_fs['pool_id'])

        if new_size_bytes > sim_fs['total_space'] and \
           sim_pool['free_space'] < new_size_bytes - sim_fs['total_space']:
            raise LsmError(
                ErrorNumber.NOT_ENOUGH_SPACE, "Insufficient space in pool")

        self._data_update(
            'fss', sim_fs_id, "total_space", new_size_bytes)
        self._data_update(
            'fss', sim_fs_id, "consumed_size", new_size_bytes)
        self._data_update(
            'fss', sim_fs_id, "free_space", new_size_bytes)

    def sim_fs_snaps(self, sim_fs_id):
        self.sim_fs_of_id(sim_fs_id)
        return self._data_find(
            'fs_snaps', 'fs_id="%s"' % sim_fs_id, BackStore.FS_SNAP_KEY_LIST)

    def sim_fs_snap_of_id(self, sim_fs_snap_id, sim_fs_id=None):
        sim_fs_snap = self._sim_data_of_id(
            'fs_snaps', sim_fs_snap_id, BackStore.FS_SNAP_KEY_LIST,
            ErrorNumber.NOT_FOUND_FS_SS, 'File system snapshot')
        if sim_fs_id and sim_fs_snap['fs_id'] != sim_fs_id:
            raise LsmError(
                ErrorNumber.NOT_FOUND_FS_SS,
                "Defined file system snapshot ID is not belong to requested "
                "file system")
        return sim_fs_snap

    def sim_fs_snap_create(self, sim_fs_id, name):
        self.sim_fs_of_id(sim_fs_id)
        try:
            self._data_add(
                'fs_snaps',
                {
                    'name': name,
                    'fs_id': sim_fs_id,
                    'timestamp': int(time.time()),
                })
        except sqlite3.IntegrityError as sql_error:
            raise LsmError(
                ErrorNumber.NAME_CONFLICT,
                "The name is already used by other file system snapshot")
        return self.lastrowid

    def sim_fs_snap_restore(self, sim_fs_id, sim_fs_snap_id, files,
                            restore_files, flag_all_files):
        # Currently LSM cannot query stauts of this action.
        # we simply check existence
        self.sim_fs_of_id(sim_fs_id)
        if sim_fs_snap_id:
            self.sim_fs_snap_of_id(sim_fs_snap_id, sim_fs_id)
        return

    def sim_fs_snap_delete(self, sim_fs_snap_id, sim_fs_id):
        self.sim_fs_of_id(sim_fs_id)
        self.sim_fs_snap_of_id(sim_fs_snap_id, sim_fs_id)
        self._data_delete('fs_snaps', 'id="%s"' % sim_fs_snap_id)

    def sim_fs_snap_del_by_fs(self, sim_fs_id):
        sql_cmd = "DELETE FROM fs_snaps WHERE fs_id='%s';" % sim_fs_id
        self._sql_exec(sql_cmd)

    def sim_fs_clone(self, src_sim_fs_id, dst_sim_fs_id, sim_fs_snap_id):
        self.sim_fs_of_id(src_sim_fs_id)
        self.sim_fs_of_id(dst_sim_fs_id)

        if sim_fs_snap_id:
            # No need to trace state of snap id here due to lack of
            # query method.
            # We just check snapshot existence
            self.sim_fs_snap_of_id(sim_fs_snap_id, src_sim_fs_id)

        self._data_add(
            'fs_clones',
            {
                'src_fs_id': src_sim_fs_id,
                'dst_fs_id': dst_sim_fs_id,
            })

    def sim_fs_file_clone(self, sim_fs_id, src_fs_name, dst_fs_name,
                          sim_fs_snap_id):
        # We don't have API to query file level clone.
        # Simply check existence
        self.sim_fs_of_id(sim_fs_id)
        if sim_fs_snap_id:
            self.sim_fs_snap_of_id(sim_fs_snap_id, sim_fs_id)
        return

    def clone_dst_sim_fs_ids_of_src(self, src_sim_fs_id):
        """
        Return a list of dst_fs_id for provided clone source fs ID.
        """
        self.sim_fs_of_id(src_sim_fs_id)
        return list(
            d['dst_fs_id'] for d in self._data_find(
                'fs_clones', 'src_fs_id="%s"' % src_sim_fs_id,
                ['dst_fs_id']))

    def sim_fs_src_clone_break(self, src_sim_fs_id):
        self._data_delete('fs_clones', 'src_fs_id="%s"' % src_sim_fs_id)

    def _sim_exp_format(self, sim_exp):
        for key_name in ['root_hosts', 'rw_hosts', 'ro_hosts']:
            table_name = "exp_%s_str" % key_name
            if sim_exp[table_name]:
                sim_exp[key_name] = sim_exp[table_name].split(
                    BackStore._LIST_SPLITTER)
            else:
                sim_exp[key_name] = []
            del sim_exp[table_name]
        return sim_exp

    def sim_exps(self):
        return list(
            self._sim_exp_format(e)
            for e in self._get_table('exps_view', BackStore.EXP_KEY_LIST))

    def sim_exp_of_id(self, sim_exp_id):
        return self._sim_exp_format(
            self._sim_data_of_id(
                'exps_view', sim_exp_id, BackStore.EXP_KEY_LIST,
                ErrorNumber.NOT_FOUND_NFS_EXPORT, 'NFS Export'))

    def sim_exp_create(self, sim_fs_id, exp_path, root_hosts, rw_hosts,
                       ro_hosts, anon_uid, anon_gid, auth_type, options):
        if exp_path is None:
            exp_path = "/nfs_exp_%s" % _random_vpd()[:8]
        self.sim_fs_of_id(sim_fs_id)

        try:
            self._data_add(
                'exps',
                {
                    'fs_id': sim_fs_id,
                    'exp_path': exp_path,
                    'anon_uid': anon_uid,
                    'anon_gid': anon_gid,
                    'auth_type': auth_type,
                    'options': options,
                })
        except sqlite3.IntegrityError as sql_error:
            # TODO(Gris Ge): Should we create new error instead of
            #                NAME_CONFLICT?
            raise LsmError(
                ErrorNumber.NAME_CONFLICT,
                "Export path is already used by other NFS export")

        sim_exp_id = self.lastrowid

        for root_host in root_hosts:
            self._data_add(
                'exp_root_hosts',
                {
                    'host': root_host,
                    'exp_id': sim_exp_id,
                })
        for rw_host in rw_hosts:
            self._data_add(
                'exp_rw_hosts',
                {
                    'host': rw_host,
                    'exp_id': sim_exp_id,
                })
        for ro_host in ro_hosts:
            self._data_add(
                'exp_ro_hosts',
                {
                    'host': ro_host,
                    'exp_id': sim_exp_id,
                })

        return sim_exp_id

    def sim_exp_delete(self, sim_exp_id):
        self.sim_exp_of_id(sim_exp_id)
        self._data_delete('exps', 'id="%s"' % sim_exp_id)

    def sim_tgts(self):
        """
        Return a list of sim_tgt dict.
        """
        return self._get_table('tgts', BackStore.TGT_KEY_LIST)


class SimArray(object):
    SIM_DATA_FILE = os.getenv("LSM_SIM_DATA",
                              tempfile.gettempdir() + '/lsm_sim_data')

    ID_FMT = 5

    @staticmethod
    def _sim_id_to_lsm_id(sim_id, prefix):
        return "%s_ID_%0*d" % (prefix, SimArray.ID_FMT, sim_id)

    @staticmethod
    def _lsm_id_to_sim_id(lsm_id, lsm_error):
        try:
            return int(lsm_id[-SimArray.ID_FMT:])
        except ValueError:
            raise lsm_error

    @staticmethod
    def _sim_job_id_of(job_id):
        return SimArray._lsm_id_to_sim_id(
            job_id, LsmError(ErrorNumber.NOT_FOUND_JOB, "Job not found"))

    @staticmethod
    def _sim_pool_id_of(pool_id):
        return SimArray._lsm_id_to_sim_id(
            pool_id, LsmError(ErrorNumber.NOT_FOUND_POOL, "Pool not found"))

    @staticmethod
    def _sim_vol_id_of(vol_id):
        return SimArray._lsm_id_to_sim_id(
            vol_id, LsmError(
                ErrorNumber.NOT_FOUND_VOLUME, "Volume not found"))

    @staticmethod
    def _sim_fs_id_of(fs_id):
        return SimArray._lsm_id_to_sim_id(
            fs_id, LsmError(
                ErrorNumber.NOT_FOUND_FS, "File system not found"))

    @staticmethod
    def _sim_fs_snap_id_of(snap_id):
        return SimArray._lsm_id_to_sim_id(
            snap_id, LsmError(
                ErrorNumber.NOT_FOUND_FS_SS,
                "File system snapshot not found"))

    @staticmethod
    def _sim_exp_id_of(exp_id):
        return SimArray._lsm_id_to_sim_id(
            exp_id, LsmError(
                ErrorNumber.NOT_FOUND_NFS_EXPORT,
                "File system export not found"))

    @staticmethod
    def _sim_ag_id_of(ag_id):
        return SimArray._lsm_id_to_sim_id(
            ag_id, LsmError(
                ErrorNumber.NOT_FOUND_NFS_EXPORT,
                "File system export not found"))

    @_handle_errors
    def __init__(self, statefile, timeout):
        if statefile is None:
            statefile = SimArray.SIM_DATA_FILE

        self.bs_obj = BackStore(statefile, timeout)
        self.bs_obj.check_version_and_init()
        self.statefile = statefile
        self.timeout = timeout

    def _job_create(self, data_type=None, sim_data_id=None):
        sim_job_id = self.bs_obj.sim_job_create(
            data_type, sim_data_id)
        return SimArray._sim_id_to_lsm_id(sim_job_id, 'JOB')

    @_handle_errors
    def job_status(self, job_id, flags=0):
        sim_job_id = SimArray._sim_job_id_of(job_id)

        (progress, data_type, sim_data) = self.bs_obj.sim_job_status(
            sim_job_id)
        status = JobStatus.INPROGRESS
        if progress == 100:
            status = JobStatus.COMPLETE

        data = None
        if data_type == BackStore.JOB_DATA_TYPE_VOL:
            data = SimArray._sim_vol_2_lsm(sim_data)
        elif data_type == BackStore.JOB_DATA_TYPE_FS:
            data = SimArray._sim_fs_2_lsm(sim_data)
        elif data_type == BackStore.JOB_DATA_TYPE_FS_SNAP:
            data = SimArray._sim_fs_snap_2_lsm(sim_data)

        return (status, progress, data)

    @_handle_errors
    def job_free(self, job_id, flags=0):
        self.bs_obj.trans_begin()
        self.bs_obj.sim_job_delete(SimArray._sim_job_id_of(job_id))
        self.bs_obj.trans_commit()
        return None

    @_handle_errors
    def time_out_set(self, ms, flags=0):
        self.bs_obj = BackStore(self.statefile, int(ms/1000))
        self.timeout = ms
        return None

    @_handle_errors
    def time_out_get(self, flags=0):
        return self.timeout

    @staticmethod
    def _sim_sys_2_lsm(sim_sys):
        return System(
            sim_sys['id'], sim_sys['name'], sim_sys['status'],
            sim_sys['status_info'])

    @_handle_errors
    def systems(self):
        return list(
            SimArray._sim_sys_2_lsm(sim_sys)
            for sim_sys in self.bs_obj.sim_syss())

    @staticmethod
    def _sim_vol_2_lsm(sim_vol):
        vol_id = SimArray._sim_id_to_lsm_id(sim_vol['id'], 'VOL')
        pool_id = SimArray._sim_id_to_lsm_id(sim_vol['pool_id'], 'POOL')
        return Volume(vol_id, sim_vol['name'], sim_vol['vpd83'],
                      BackStore.BLK_SIZE,
                      int(sim_vol['total_space'] / BackStore.BLK_SIZE),
                      sim_vol['admin_state'], BackStore.SYS_ID,
                      pool_id)

    @_handle_errors
    def volumes(self):
        return list(
            SimArray._sim_vol_2_lsm(v) for v in self.bs_obj.sim_vols())

    @staticmethod
    def _sim_pool_2_lsm(sim_pool):
        pool_id = SimArray._sim_id_to_lsm_id(sim_pool['id'], 'POOL')
        name = sim_pool['name']
        total_space = sim_pool['total_space']
        free_space = sim_pool['free_space']
        status = sim_pool['status']
        status_info = sim_pool['status_info']
        sys_id = BackStore.SYS_ID
        element_type = sim_pool['element_type']
        unsupported_actions = sim_pool['unsupported_actions']
        return Pool(
            pool_id, name, element_type, unsupported_actions, total_space,
            free_space, status, status_info, sys_id)

    @_handle_errors
    def pools(self, flags=0):
        self.bs_obj.trans_begin()
        sim_pools = self.bs_obj.sim_pools()
        self.bs_obj.trans_rollback()
        return list(
            SimArray._sim_pool_2_lsm(sim_pool) for sim_pool in sim_pools)

    @staticmethod
    def _sim_disk_2_lsm(sim_disk):
        disk_status = Disk.STATUS_OK
        if sim_disk['role'] is None:
            disk_status |= Disk.STATUS_FREE

        return Disk(
            SimArray._sim_id_to_lsm_id(sim_disk['id'], 'DISK'),
            sim_disk['name'],
            sim_disk['disk_type'], BackStore.BLK_SIZE,
            int(sim_disk['total_space'] / BackStore.BLK_SIZE),
            disk_status, BackStore.SYS_ID)

    @_handle_errors
    def disks(self):
        return list(
            SimArray._sim_disk_2_lsm(sim_disk)
            for sim_disk in self.bs_obj.sim_disks())

    @_handle_errors
    def volume_create(self, pool_id, vol_name, size_bytes, thinp, flags=0,
                      _internal_use=False, _is_hw_raid_vol=0):
        """
        The '_internal_use' parameter is only for SimArray internal use.
        This method will return the new sim_vol id instead of job_id when
        '_internal_use' marked as True.
        """
        if _internal_use is False:
            self.bs_obj.trans_begin()

        new_sim_vol_id = self.bs_obj.sim_vol_create(
            vol_name, size_bytes, SimArray._sim_pool_id_of(pool_id),
            thinp, is_hw_raid_vol=_is_hw_raid_vol)

        if _internal_use:
            return new_sim_vol_id

        job_id = self._job_create(
            BackStore.JOB_DATA_TYPE_VOL, new_sim_vol_id)
        self.bs_obj.trans_commit()

        return job_id, None

    @_handle_errors
    def volume_delete(self, vol_id, flags=0):
        self.bs_obj.trans_begin()
        self.bs_obj.sim_vol_delete(SimArray._sim_vol_id_of(vol_id))
        job_id = self._job_create()
        self.bs_obj.trans_commit()
        return job_id

    @_handle_errors
    def volume_resize(self, vol_id, new_size_bytes, flags=0):
        self.bs_obj.trans_begin()

        sim_vol_id = SimArray._sim_vol_id_of(vol_id)
        self.bs_obj.sim_vol_resize(sim_vol_id, new_size_bytes)
        job_id = self._job_create(
            BackStore.JOB_DATA_TYPE_VOL, sim_vol_id)
        self.bs_obj.trans_commit()

        return job_id, None

    @_handle_errors
    def volume_replicate(self, dst_pool_id, rep_type, src_vol_id, new_vol_name,
                         flags=0):
        self.bs_obj.trans_begin()

        src_sim_vol_id = SimArray._sim_pool_id_of(src_vol_id)
        # Verify the existence of source volume
        src_sim_vol = self.bs_obj.sim_vol_of_id(src_sim_vol_id)

        dst_sim_vol_id = self.volume_create(
            dst_pool_id, new_vol_name, src_sim_vol['total_space'],
            src_sim_vol['thinp'], _internal_use=True)

        self.bs_obj.sim_vol_replica(src_sim_vol_id, dst_sim_vol_id, rep_type)

        job_id = self._job_create(
            BackStore.JOB_DATA_TYPE_VOL, dst_sim_vol_id)
        self.bs_obj.trans_commit()

        return job_id, None

    @_handle_errors
    def volume_replicate_range_block_size(self, sys_id, flags=0):
        if sys_id != BackStore.SYS_ID:
            raise LsmError(
                ErrorNumber.NOT_FOUND_SYSTEM,
                "System not found")
        return BackStore.BLK_SIZE

    @_handle_errors
    def volume_replicate_range(self, rep_type, src_vol_id, dst_vol_id, ranges,
                               flags=0):
        self.bs_obj.trans_begin()

        # TODO(Gris Ge): check whether star_blk + count is out of volume
        #                boundary
        # TODO(Gris Ge): Should check block overlap.

        self.bs_obj.sim_vol_replica(
            SimArray._sim_pool_id_of(src_vol_id),
            SimArray._sim_pool_id_of(dst_vol_id), rep_type, ranges)

        job_id = self._job_create()

        self.bs_obj.trans_commit()
        return job_id

    @_handle_errors
    def volume_enable(self, vol_id, flags=0):
        self.bs_obj.trans_begin()
        self.bs_obj.sim_vol_state_change(
            SimArray._sim_vol_id_of(vol_id), Volume.ADMIN_STATE_ENABLED)
        self.bs_obj.trans_commit()
        return None

    @_handle_errors
    def volume_disable(self, vol_id, flags=0):
        self.bs_obj.trans_begin()
        self.bs_obj.sim_vol_state_change(
            SimArray._sim_vol_id_of(vol_id), Volume.ADMIN_STATE_DISABLED)
        self.bs_obj.trans_commit()
        return None

    @_handle_errors
    def volume_child_dependency(self, vol_id, flags=0):
        # TODO(Gris Ge): API defination is blur:
        #       0. Should we break replication if provided volume is a
        #          replication target?
        #          Assuming answer is no.
        #       1. _client.py comments incorrect:
        #           "Implies that this volume cannot be deleted or possibly
        #            modified because it would affect its children"
        #          The 'modify' here is incorrect. If data on source volume
        #          changes, SYNC_MIRROR replication will change all target
        #          volumes.
        #       2. Should 'mask' relationship included?
        #           # Assuming only replication counts here.
        #       3. For volume internal block replication, should we return
        #          True or False.
        #          # Assuming False
        #       4. volume_child_dependency_rm() against volume internal
        #          block replication, remove replication or raise error?
        #          # Assuming remove replication
        src_sim_vol_id = SimArray._sim_vol_id_of(vol_id)
        dst_sim_vol_ids = self.bs_obj.dst_sim_vol_ids_of_src(src_sim_vol_id)
        for dst_sim_fs_id in dst_sim_vol_ids:
            if dst_sim_fs_id != src_sim_vol_id:
                return True
        return False

    @_handle_errors
    def volume_child_dependency_rm(self, vol_id, flags=0):
        self.bs_obj.trans_begin()

        self.bs_obj.sim_vol_src_replica_break(
            SimArray._sim_vol_id_of(vol_id))

        job_id = self._job_create()
        self.bs_obj.trans_commit()
        return job_id

    @staticmethod
    def _sim_fs_2_lsm(sim_fs):
        fs_id = SimArray._sim_id_to_lsm_id(sim_fs['id'], 'FS')
        pool_id = SimArray._sim_id_to_lsm_id(sim_fs['id'], 'POOL')
        return FileSystem(fs_id, sim_fs['name'],
                          sim_fs['total_space'], sim_fs['free_space'],
                          pool_id, BackStore.SYS_ID)

    @_handle_errors
    def fs(self):
        return list(SimArray._sim_fs_2_lsm(f) for f in self.bs_obj.sim_fss())

    @_handle_errors
    def fs_create(self, pool_id, fs_name, size_bytes, flags=0,
                  _internal_use=False):

        if not _internal_use:
            self.bs_obj.trans_begin()

        new_sim_fs_id = self.bs_obj.sim_fs_create(
            fs_name, size_bytes, SimArray._sim_pool_id_of(pool_id))

        if _internal_use:
            return new_sim_fs_id

        job_id = self._job_create(
            BackStore.JOB_DATA_TYPE_FS, new_sim_fs_id)
        self.bs_obj.trans_commit()

        return job_id, None

    @_handle_errors
    def fs_delete(self, fs_id, flags=0):
        self.bs_obj.trans_begin()
        self.bs_obj.sim_fs_delete(SimArray._sim_fs_id_of(fs_id))
        job_id = self._job_create()
        self.bs_obj.trans_commit()
        return job_id

    @_handle_errors
    def fs_resize(self, fs_id, new_size_bytes, flags=0):
        sim_fs_id = SimArray._sim_fs_id_of(fs_id)
        self.bs_obj.trans_begin()
        self.bs_obj.sim_fs_resize(sim_fs_id, new_size_bytes)
        job_id = self._job_create(BackStore.JOB_DATA_TYPE_FS, sim_fs_id)
        self.bs_obj.trans_commit()
        return job_id, None

    @_handle_errors
    def fs_clone(self, src_fs_id, dst_fs_name, snap_id, flags=0):
        self.bs_obj.trans_begin()

        sim_fs_snap_id = None
        if snap_id:
            sim_fs_snap_id = SimArray._sim_fs_snap_id_of(snap_id)

        src_sim_fs_id = SimArray._sim_fs_id_of(src_fs_id)
        src_sim_fs = self.bs_obj.sim_fs_of_id(src_sim_fs_id)
        pool_id = SimArray._sim_id_to_lsm_id(src_sim_fs['pool_id'], 'POOL')

        dst_sim_fs_id = self.fs_create(
            pool_id, dst_fs_name, src_sim_fs['total_space'],
            _internal_use=True)

        self.bs_obj.sim_fs_clone(src_sim_fs_id, dst_sim_fs_id, sim_fs_snap_id)

        job_id = self._job_create(
            BackStore.JOB_DATA_TYPE_FS, dst_sim_fs_id)
        self.bs_obj.trans_commit()

        return job_id, None

    @_handle_errors
    def fs_file_clone(self, fs_id, src_fs_name, dst_fs_name, snap_id, flags=0):
        self.bs_obj.trans_begin()
        sim_fs_snap_id = None
        if snap_id:
            sim_fs_snap_id = SimArray._sim_fs_snap_id_of(snap_id)

        self.bs_obj.sim_fs_file_clone(
            SimArray._sim_fs_id_of(fs_id), src_fs_name, dst_fs_name,
            sim_fs_snap_id)

        job_id = self._job_create()
        self.bs_obj.trans_commit()
        return job_id

    @staticmethod
    def _sim_fs_snap_2_lsm(sim_fs_snap):
        snap_id = SimArray._sim_id_to_lsm_id(sim_fs_snap['id'], 'FS_SNAP')
        return FsSnapshot(
            snap_id, sim_fs_snap['name'], sim_fs_snap['timestamp'])

    @_handle_errors
    def fs_snapshots(self, fs_id, flags=0):
        return list(
            SimArray._sim_fs_snap_2_lsm(s)
            for s in self.bs_obj.sim_fs_snaps(
                SimArray._sim_fs_id_of(fs_id)))

    @_handle_errors
    def fs_snapshot_create(self, fs_id, snap_name, flags=0):
        self.bs_obj.trans_begin()
        sim_fs_snap_id = self.bs_obj.sim_fs_snap_create(
            SimArray._sim_fs_id_of(fs_id), snap_name)
        job_id = self._job_create(
            BackStore.JOB_DATA_TYPE_FS_SNAP, sim_fs_snap_id)
        self.bs_obj.trans_commit()
        return job_id, None

    @_handle_errors
    def fs_snapshot_delete(self, fs_id, snap_id, flags=0):
        self.bs_obj.trans_begin()
        self.bs_obj.sim_fs_snap_delete(
            SimArray._sim_fs_snap_id_of(snap_id),
            SimArray._sim_fs_id_of(fs_id))
        job_id = self._job_create()
        self.bs_obj.trans_commit()
        return job_id

    @_handle_errors
    def fs_snapshot_restore(self, fs_id, snap_id, files, restore_files,
                            flag_all_files, flags):
        self.bs_obj.trans_begin()
        sim_fs_snap_id = None
        if snap_id:
            sim_fs_snap_id = SimArray._sim_fs_snap_id_of(snap_id)

        self.bs_obj.sim_fs_snap_restore(
            SimArray._sim_fs_id_of(fs_id),
            sim_fs_snap_id, files, restore_files, flag_all_files)

        job_id = self._job_create()
        self.bs_obj.trans_commit()
        return job_id

    @_handle_errors
    def fs_child_dependency(self, fs_id, files, flags=0):
        sim_fs_id = SimArray._sim_fs_id_of(fs_id)
        self.bs_obj.trans_begin()
        if self.bs_obj.clone_dst_sim_fs_ids_of_src(sim_fs_id) == [] and \
           self.bs_obj.sim_fs_snaps(sim_fs_id) == []:
            self.bs_obj.trans_rollback()
            return False
        self.bs_obj.trans_rollback()
        return True

    @_handle_errors
    def fs_child_dependency_rm(self, fs_id, files, flags=0):
        """
        Assuming API defination is break all clone relationship and remove
        all snapshot of this source file system.
        """
        self.bs_obj.trans_begin()
        if self.fs_child_dependency(fs_id, files) is False:
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE,
                "No snapshot or fs clone target found for this file system")

        src_sim_fs_id = SimArray._sim_fs_id_of(fs_id)
        self.bs_obj.sim_fs_src_clone_break(src_sim_fs_id)
        self.bs_obj.sim_fs_snap_del_by_fs(src_sim_fs_id)
        job_id = self._job_create()
        self.bs_obj.trans_begin()
        return job_id

    @staticmethod
    def _sim_exp_2_lsm(sim_exp):
        exp_id = SimArray._sim_id_to_lsm_id(sim_exp['id'], 'EXP')
        fs_id = SimArray._sim_id_to_lsm_id(sim_exp['fs_id'], 'FS')
        return NfsExport(exp_id, fs_id, sim_exp['exp_path'],
                         sim_exp['auth_type'], sim_exp['root_hosts'],
                         sim_exp['rw_hosts'], sim_exp['ro_hosts'],
                         sim_exp['anon_uid'], sim_exp['anon_gid'],
                         sim_exp['options'])

    @_handle_errors
    def exports(self, flags=0):
        return [SimArray._sim_exp_2_lsm(e) for e in self.bs_obj.sim_exps()]

    @_handle_errors
    def fs_export(self, fs_id, exp_path, root_hosts, rw_hosts, ro_hosts,
                  anon_uid, anon_gid, auth_type, options, flags=0):
        self.bs_obj.trans_begin()
        sim_exp_id = self.bs_obj.sim_exp_create(
            SimArray._sim_fs_id_of(fs_id), exp_path, root_hosts, rw_hosts,
            ro_hosts, anon_uid, anon_gid, auth_type, options)
        sim_exp = self.bs_obj.sim_exp_of_id(sim_exp_id)
        self.bs_obj.trans_commit()
        return SimArray._sim_exp_2_lsm(sim_exp)

    @_handle_errors
    def fs_unexport(self, exp_id, flags=0):
        self.bs_obj.trans_begin()
        self.bs_obj.sim_exp_delete(SimArray._sim_exp_id_of(exp_id))
        self.bs_obj.trans_commit()
        return None

    @staticmethod
    def _sim_ag_2_lsm(sim_ag):
        ag_id = SimArray._sim_id_to_lsm_id(sim_ag['id'], 'AG')
        return AccessGroup(ag_id, sim_ag['name'], sim_ag['init_ids'],
                           sim_ag['init_type'], BackStore.SYS_ID)

    @_handle_errors
    def ags(self):
        return list(SimArray._sim_ag_2_lsm(a) for a in self.bs_obj.sim_ags())

    @_handle_errors
    def access_group_create(self, name, init_id, init_type, sys_id, flags=0):
        if sys_id != BackStore.SYS_ID:
            raise LsmError(
                ErrorNumber.NOT_FOUND_SYSTEM,
                "System not found")
        self.bs_obj.trans_begin()
        new_sim_ag_id = self.bs_obj.sim_ag_create(name, init_type, init_id)
        new_sim_ag = self.bs_obj.sim_ag_of_id(new_sim_ag_id)
        self.bs_obj.trans_commit()
        return SimArray._sim_ag_2_lsm(new_sim_ag)

    @_handle_errors
    def access_group_delete(self, ag_id, flags=0):
        self.bs_obj.trans_begin()
        self.bs_obj.sim_ag_delete(SimArray._sim_ag_id_of(ag_id))
        self.bs_obj.trans_commit()
        return None

    @_handle_errors
    def access_group_initiator_add(self, ag_id, init_id, init_type, flags=0):
        sim_ag_id = SimArray._sim_ag_id_of(ag_id)
        self.bs_obj.trans_begin()
        self.bs_obj.sim_ag_init_add(sim_ag_id, init_id, init_type)
        new_sim_ag = self.bs_obj.sim_ag_of_id(sim_ag_id)
        self.bs_obj.trans_commit()
        return SimArray._sim_ag_2_lsm(new_sim_ag)

    @_handle_errors
    def access_group_initiator_delete(self, ag_id, init_id, init_type,
                                      flags=0):
        sim_ag_id = SimArray._sim_ag_id_of(ag_id)
        self.bs_obj.trans_begin()
        self.bs_obj.sim_ag_init_delete(sim_ag_id, init_id)
        sim_ag = self.bs_obj.sim_ag_of_id(sim_ag_id)
        self.bs_obj.trans_commit()
        return SimArray._sim_ag_2_lsm(sim_ag)

    @_handle_errors
    def volume_mask(self, ag_id, vol_id, flags=0):
        self.bs_obj.trans_begin()
        self.bs_obj.sim_vol_mask(
            SimArray._sim_vol_id_of(vol_id),
            SimArray._sim_ag_id_of(ag_id))
        self.bs_obj.trans_commit()
        return None

    @_handle_errors
    def volume_unmask(self, ag_id, vol_id, flags=0):
        self.bs_obj.trans_begin()
        self.bs_obj.sim_vol_unmask(
            SimArray._sim_vol_id_of(vol_id),
            SimArray._sim_ag_id_of(ag_id))
        self.bs_obj.trans_commit()
        return None

    @_handle_errors
    def volumes_accessible_by_access_group(self, ag_id, flags=0):
        self.bs_obj.trans_begin()

        sim_vols = self.bs_obj.sim_vols(
            sim_ag_id=SimArray._sim_ag_id_of(ag_id))

        self.bs_obj.trans_rollback()
        return [SimArray._sim_vol_2_lsm(v) for v in sim_vols]

    @_handle_errors
    def access_groups_granted_to_volume(self, vol_id, flags=0):
        self.bs_obj.trans_begin()
        sim_ags = self.bs_obj.sim_ags(
            sim_vol_id=SimArray._sim_vol_id_of(vol_id))
        self.bs_obj.trans_rollback()
        return [SimArray._sim_ag_2_lsm(a) for a in sim_ags]

    @_handle_errors
    def iscsi_chap_auth(self, init_id, in_user, in_pass, out_user, out_pass,
                        flags=0):
        self.bs_obj.trans_begin()
        self.bs_obj.iscsi_chap_auth_set(
            init_id, in_user, in_pass, out_user, out_pass)
        self.bs_obj.trans_commit()
        return None

    @staticmethod
    def _sim_tgt_2_lsm(sim_tgt):
        tgt_id = "TGT_PORT_ID_%0*d" % (SimArray.ID_FMT, sim_tgt['id'])
        return TargetPort(
            tgt_id, sim_tgt['port_type'], sim_tgt['service_address'],
            sim_tgt['network_address'], sim_tgt['physical_address'],
            sim_tgt['physical_name'],
            BackStore.SYS_ID)

    @_handle_errors
    def target_ports(self):
        return list(SimArray._sim_tgt_2_lsm(t) for t in self.bs_obj.sim_tgts())

    @_handle_errors
    def volume_raid_info(self, lsm_vol):
        sim_pool = self.bs_obj.sim_pool_of_id(
            SimArray._lsm_id_to_sim_id(
                lsm_vol.pool_id,
                LsmError(ErrorNumber.NOT_FOUND_POOL, "Pool not found")))

        raid_type = sim_pool['raid_type']
        strip_size = Volume.STRIP_SIZE_UNKNOWN
        min_io_size = BackStore.BLK_SIZE
        opt_io_size = Volume.OPT_IO_SIZE_UNKNOWN
        disk_count = Volume.DISK_COUNT_UNKNOWN

        if sim_pool['member_type'] == Pool.MEMBER_TYPE_POOL:
            parent_sim_pool = self.bs_obj.sim_pool_of_id(
                sim_pool['parent_pool_id'])
            raid_type = parent_sim_pool['raid_type']

            disk_count = self.bs_obj.sim_pool_disks_count(
                parent_sim_pool['id'])
            data_disk_count = self.bs_obj.sim_pool_data_disks_count(
                parent_sim_pool['id'])
        else:
            disk_count = self.bs_obj.sim_pool_disks_count(
                sim_pool['id'])
            data_disk_count = self.bs_obj.sim_pool_data_disks_count(
                sim_pool['id'])

        if raid_type == Volume.RAID_TYPE_UNKNOWN or \
           raid_type == Volume.RAID_TYPE_OTHER:
            return [
                raid_type, strip_size, disk_count, min_io_size,
                opt_io_size]

        if raid_type == Volume.RAID_TYPE_MIXED:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "volume_raid_info(): Got unsupported RAID_TYPE_MIXED pool "
                "%s" % sim_pool['id'])

        if raid_type == Volume.RAID_TYPE_RAID1 or \
           raid_type == Volume.RAID_TYPE_JBOD:
            strip_size = BackStore.BLK_SIZE
            min_io_size = BackStore.BLK_SIZE
            opt_io_size = BackStore.BLK_SIZE
        else:
            strip_size = sim_pool['strip_size']
            min_io_size = strip_size
            opt_io_size = int(data_disk_count * strip_size)

        return [raid_type, strip_size, disk_count, min_io_size, opt_io_size]

    @_handle_errors
    def pool_member_info(self, lsm_pool):
        sim_pool = self.bs_obj.sim_pool_of_id(
            SimArray._lsm_id_to_sim_id(
                lsm_pool.id,
                LsmError(ErrorNumber.NOT_FOUND_POOL, "Pool not found")))
        member_type = sim_pool['member_type']
        member_ids = []
        if member_type == Pool.MEMBER_TYPE_POOL:
            member_ids = [
                SimArray._sim_id_to_lsm_id(
                    sim_pool['parent_pool_id'], 'POOL')]
        elif member_type == Pool.MEMBER_TYPE_DISK:
            member_ids = list(
                SimArray._sim_id_to_lsm_id(sim_disk_id, 'DISK')
                for sim_disk_id in self.bs_obj.sim_disk_ids_of_pool(
                    sim_pool['id']))
        else:
            member_type = Pool.MEMBER_TYPE_UNKNOWN

        return sim_pool['raid_type'], member_type, member_ids

    @_handle_errors
    def volume_raid_create_cap_get(self, system):
        if system.id != BackStore.SYS_ID:
            raise LsmError(
                ErrorNumber.NOT_FOUND_SYSTEM,
                "System not found")
        return (
            BackStore.SUPPORTED_VCR_RAID_TYPES,
            BackStore.SUPPORTED_VCR_STRIP_SIZES)

    @_handle_errors
    def volume_raid_create(self, name, raid_type, disks, strip_size):
        if raid_type not in BackStore.SUPPORTED_VCR_RAID_TYPES:
            raise LsmError(
                ErrorNumber.NO_SUPPORT,
                "Provided 'raid_type' is not supported")

        if strip_size == Volume.VCR_STRIP_SIZE_DEFAULT:
            strip_size = BackStore.DEFAULT_STRIP_SIZE
        elif strip_size not in BackStore.SUPPORTED_VCR_STRIP_SIZES:
            raise LsmError(
                ErrorNumber.NO_SUPPORT,
                "Provided 'strip_size' is not supported")

        self.bs_obj.trans_begin()
        pool_name = "Pool for volume %s" % name
        sim_disk_ids = [
            SimArray._lsm_id_to_sim_id(
                d.id,
                LsmError(ErrorNumber.NOT_FOUND_DISK, "Disk not found"))
            for d in disks]

        for disk in disks:
            if not disk.status & Disk.STATUS_FREE:
                raise LsmError(
                    ErrorNumber.DISK_NOT_FREE,
                    "Disk %s is not in DISK.STATUS_FREE mode" % disk.id)
        try:
            sim_pool_id = self.bs_obj.sim_pool_create_from_disk(
                name=pool_name,
                raid_type=raid_type,
                sim_disk_ids=sim_disk_ids,
                element_type=Pool.ELEMENT_TYPE_VOLUME,
                unsupported_actions=Pool.UNSUPPORTED_VOLUME_GROW |
                Pool.UNSUPPORTED_VOLUME_SHRINK,
                strip_size=strip_size)
        except sqlite3.IntegrityError as sql_error:
            raise LsmError(
                ErrorNumber.NAME_CONFLICT,
                "Name '%s' is already in use by other volume" % name)

        sim_pool = self.bs_obj.sim_pool_of_id(sim_pool_id)
        sim_vol_id = self.volume_create(
            SimArray._sim_id_to_lsm_id(sim_pool_id, 'POOL'), name,
            sim_pool['free_space'], Volume.PROVISION_FULL,
            _internal_use=True, _is_hw_raid_vol=1)
        sim_vol = self.bs_obj.sim_vol_of_id(sim_vol_id)
        self.bs_obj.trans_commit()
        return SimArray._sim_vol_2_lsm(sim_vol)
