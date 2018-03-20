// Copyright (C) 2017-2018 Red Hat, Inc.
//
// Permission is hereby granted, free of charge, to any
// person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the
// Software without restriction, including without
// limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software
// is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice
// shall be included in all copies or substantial portions
// of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
// ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
// TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
// SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
// IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
// Author: Gris Ge <fge@redhat.com>

use serde::{Deserialize, Deserializer, Serializer};
use std::mem::transmute;

fn gen_system_class_string() -> String {
    "System".to_string()
}

fn gen_pool_class_string() -> String {
    "Pool".to_string()
}

fn gen_vol_class_string() -> String {
    "Volume".to_string()
}

fn gen_ag_class_string() -> String {
    "AccessGroup".to_string()
}

fn gen_fs_class_string() -> String {
    "FileSystem".to_string()
}
fn gen_fs_snap_class_string() -> String {
    "FsSnapshot".to_string()
}

fn gen_exp_class_string() -> String {
    "NfsExport".to_string()
}

fn gen_disk_class_string() -> String {
    "Disk".to_string()
}

/// Represent a storage system. Examples:
///
///  * A hardware RAID card, LSI `MegaRAID`
///
///  * A storage area network (SAN), e.g. `EMC` VNX, `NetApp` Filer
///
///  * A software solution running on commodity hardware, targetd, Nexenta
///
///  * A Linux system running NFS service
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct System {
    #[serde(default = "gen_system_class_string")] class: String,
    /// Identifier.
    pub id: String,
    /// Human friendly name.
    pub name: String,
    /// System status stored in bitmap. Valid status value are:
    ///
    ///  * [`System::STATUS_UNKNOWN`][1]
    ///  * [`System::STATUS_OK`][2]
    ///  * [`System::STATUS_ERROR`][3]
    ///  * [`System::STATUS_DEGRADED`][4]
    ///  * [`System::STATUS_PREDICTIVE_FAILURE`][5]
    ///  * [`System::STATUS_OTHER`][6]
    ///
    /// ```rust
    /// use lsm::{Client, System};
    ///
    /// let mut c = Client::new("sim://", None, None).unwrap();
    /// let syss = c.systems().unwrap();
    ///
    /// for s in syss {
    ///     if (s.status & System::STATUS_OK) == 0 {
    ///         println!("System is not healthy");
    ///     }
    /// }
    /// ```
    /// [1]: #associatedconstant.STATUS_UNKNOWN
    /// [2]: #associatedconstant.STATUS_OK
    /// [3]: #associatedconstant.STATUS_ERROR
    /// [4]: #associatedconstant.STATUS_DEGRADED
    /// [5]: #associatedconstant.STATUS_PREDICTIVE_FAILURE
    /// [6]: #associatedconstant.STATUS_OTHER
    pub status: u32,
    /// Additional message for status.
    pub status_info: String,
    plugin_data: Option<String>,
    /// Firmware version.
    pub fw_version: String,
    /// Read cache percentage of the system. Valid values are:
    ///
    /// * `>0 and < 100` means only a part of whole cache are used for read.
    /// * `0` means no read cache.
    /// * `100` means all cache are used for read.
    /// * [`System::READ_CACHE_PCT_NO_SUPPORT`][1] means no support.
    /// * [`System::READ_CACHE_PCT_UNKNOWN`][2] means plugin failed to
    ///   detect this value.
    ///
    /// [1]: #associatedconstant.READ_CACHE_PCT_NO_SUPPORT
    /// [2]: #associatedconstant.READ_CACHE_PCT_UNKNOWN
    pub read_cache_pct: i8,
    #[serde(deserialize_with = "int_to_sys_mod")]
    #[serde(serialize_with = "sys_mod_to_int")]
    /// System mode, currently only supports hardware RAID cards.
    pub mode: SystemMode,
}

impl System {
    /// Plugin does not support querying read cache percentage.
    pub const READ_CACHE_PCT_NO_SUPPORT: i8 = -2;
    /// Plugin failed to query read cache percentage.
    pub const READ_CACHE_PCT_UNKNOWN: i8 = -1;

    /// Plugin failed to query system status.
    pub const STATUS_UNKNOWN: u32 = 1;
    /// System is up and healthy.
    pub const STATUS_OK: u32 = 1 << 1;
    /// System is in error state.
    pub const STATUS_ERROR: u32 = 1 << 2;
    /// System is degraded.
    pub const STATUS_DEGRADED: u32 = 1 << 3;
    /// System has protential failure.
    pub const STATUS_PREDICTIVE_FAILURE: u32 = 1 << 4;
    /// Vendor specific status.
    pub const STATUS_OTHER: u32 = 1 << 5;
}

#[repr(i8)]
#[derive(Debug, Clone, PartialEq, Copy)]
pub enum SystemMode {
    /// Plugin failed to query system mode.
    Unknown = -2,
    /// Plugin does not support querying system mode.
    NoSupport = -1,
    /// The storage system is a hardware RAID card(like HP SmartArray and LSI
    /// MegaRAID) and could expose the logical volume(aka, RAIDed virtual disk)
    /// to OS while hardware RAID card is handling the RAID algorithm. In this
    /// mode, storage system cannot expose physical disk directly to OS.
    HardwareRaid = 0,
    /// The physical disks can be exposed to OS directly without any
    /// configurations. SCSI enclosure service might be exposed to OS also.
    Hba = 1,
}

fn int_to_sys_mod<'de, D: Deserializer<'de>>(
    deserializer: D,
) -> ::std::result::Result<SystemMode, D::Error> {
    let i: i8 = Deserialize::deserialize(deserializer)?;
    match i {
        -1 | 0 | 1 => unsafe { Ok(transmute(i)) },
        _ => Ok(SystemMode::Unknown),
    }
}

fn sys_mod_to_int<S: Serializer>(
    m: &SystemMode,
    serializer: S,
) -> ::std::result::Result<S::Ok, S::Error> {
    serializer.serialize_i8(*m as i8)
}

/// Represent a storage volume. Also known as LUN(Logical Unit Number) or
/// Storage Volume or Virtual Disk. The host OS treats it as block devices (one
/// volume can be exposed as many disks when [multipath I/O][1] is enabled).
///
/// [1]: https://en.wikipedia.org/wiki/Multipath_I/O
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct Volume {
    #[serde(default = "gen_vol_class_string")] class: String,
    /// Identifier.
    pub id: String,
    /// Human friendly name.
    pub name: String,
    #[serde(deserialize_with = "int_to_bool")]
    #[serde(serialize_with = "bool_to_int")]
    #[serde(rename = "admin_state")]
    /// Whether volume is online or offline(I/O access disabled by
    /// administrator.
    pub enabled: bool,
    /// Block size.
    pub block_size: u64,
    /// Number of blocks.
    pub num_of_blocks: u64,
    plugin_data: Option<String>,
    /// SCSI VPD 0x83 NAA type identifier.
    /// Udev treat it as `ID_WWN_WITH_EXTENSION`
    pub vpd83: String,
    /// Identifier of owner system.
    pub system_id: String,
    /// Identifier of owner pool.
    pub pool_id: String,
}

impl Volume {
    /// Retried the usable size of volume in bytes.
    pub fn size_bytes(&self) -> u64 {
        self.block_size * self.num_of_blocks
    }
}

/// Represent a volume replication type.
pub enum VolumeReplicateType {
    /// Plugin failed to detect volume replication type.
    Unknown = -1,
    /// Point in time read writeable space efficient copy of data. Also know as
    /// read writeable snapshot.
    Clone = 2,
    /// Full bitwise copy of the data (occupies full space).
    Copy = 3,
    /// I/O will be blocked until I/O reached both source and target storage
    /// systems. There will be no data difference between source and target
    /// storage systems.
    MirrorSync = 4,
    /// I/O will be blocked until I/O reached source storage systems.  The
    /// source storage system will use copy the changes data to target system
    /// in a predefined interval. There will be a small data differences
    /// between source and target.
    MirrorAsync = 5,
}

#[repr(i32)]
#[derive(Debug, Clone, PartialEq, Copy)]
/// Represent a RAID type.
pub enum RaidType {
    /// Plugin failed to detect RAID type.
    Unknown = -1,
    /// [RAID 0](https://en.wikipedia.org/wiki/Standard_RAID_levels#RAID_0)
    Raid0 = 0,
    /// Two disk mirror.
    Raid1 = 1,
    /// Byte-level striping with dedicated parity.
    Raid3 = 3,
    /// Block-level striping with dedicated parity.
    Raid4 = 4,
    /// Block-level striping with distributed parity.
    Raid5 = 5,
    /// Block-level striping with two distributed parities. Also known as
    /// RAID-DP.
    Raid6 = 6,
    /// Stripe of mirrors.
    Raid10 = 10,
    /// Parity of mirrors.
    Raid15 = 15,
    /// Dual parity of mirrors.
    Raid16 = 16,
    /// Stripe of parities.
    Raid50 = 50,
    /// Stripe of dual parities.
    Raid60 = 60,
    /// Mirror of parities.
    Raid51 = 51,
    /// Mirror of dual parities.
    Raid61 = 61,
    /// Just bunch of disks, no parity, no striping.
    Jbod = 20,
    /// This volume contains multiple RAID settings.
    Mixed = 21,
    /// Vendor specific RAID type
    Other = 22,
}

impl From<i32> for RaidType {
    fn from(i: i32) -> RaidType {
        match i {
            0...6 | 10 | 15 | 16 | 50 | 60 | 51 | 61 | 20...22 => unsafe {
                transmute(i)
            },
            _ => RaidType::Unknown,
        }
    }
}

#[derive(Debug, Clone)]
/// Represent a Pool member.
pub enum PoolMember {
    /// Pool is created from disks.
    Disk(Disk),
    /// Pool is allocationed from other pool.
    Pool(Pool),
}

#[derive(Debug, Clone)]
/// Represent pool membership informtion.
pub struct PoolMemberInfo {
    /// RAID type
    pub raid_type: RaidType,
    /// Pool members.
    pub members: Vec<PoolMember>,
}

#[derive(Debug, Clone)]
/// Represent volume RAID informtion.
pub struct VolumeRaidInfo {
    /// RAID type
    pub raid_type: RaidType,
    /// The size of strip on each disk or other storage extent.
    /// For RAID1/JBOD, it should be set as block size.  If plugin failed to
    /// detect strip size, it should be set as 0.
    pub strip_size: u32,
    /// The count of disks used for assembling the RAID group(s) where this
    /// volume allocated from. For any RAID system using the slice of disk,
    /// this value indicate how many disk slices are used for the RAID.  For
    /// example, on LVM RAID, the 'disk_count' here indicate the count of PVs
    /// used for certain volume. Another example, on EMC VMAX, the 'disk_count'
    /// here indicate how many hyper volumes are used for this volume.  For any
    /// RAID system using remote LUN for data storing, each remote LUN should
    /// be count as a disk.  If the plugin failed to detect disk_count, it
    /// should be set as 0.
    pub disk_count: u32,
    /// The minimum I/O size, device preferred I/O size for random I/O. Any I/O
    /// size not equal to a multiple of this value may get significant speed
    /// penalty.  Normally it refers to strip size of each disk(extent).  If
    /// plugin failed to detect min_io_size, it should try these values in the
    /// sequence of: logical sector size -> physical sector size -> 0
    pub min_io_size: u32,
    /// The optimal I/O size, device preferred I/O size for sequential I/O.
    /// Normally it refers to RAID group stripe size.  If plugin failed to
    /// detect opt_io_size, it should be set to 0.
    pub opt_io_size: u32,
}

fn int_to_bool<'de, D: Deserializer<'de>>(
    deserializer: D,
) -> ::std::result::Result<bool, D::Error> {
    let i: i32 = Deserialize::deserialize(deserializer)?;
    match i {
        1 => Ok(true),
        _ => Ok(false),
    }
}

fn bool_to_int<S: Serializer>(
    b: &bool,
    serializer: S,
) -> ::std::result::Result<S::Ok, S::Error> {
    if *b {
        serializer.serialize_i8(1i8)
    } else {
        serializer.serialize_i8(0i8)
    }
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct Pool {
    #[serde(default = "gen_pool_class_string")] class: String,
    /// Identifier.
    pub id: String,
    /// Human friendly name.
    pub name: String,
    /// The type of elements this pool could create.
    /// Valid element types are:
    ///
    ///  * [`Pool::ELEMENT_TYPE_POOL`][1]
    ///  * [`Pool::ELEMENT_TYPE_VOLUME`][2]
    ///  * [`Pool::ELEMENT_TYPE_FS`][3]
    ///  * [`Pool::ELEMENT_TYPE_DELTA`][4]
    ///  * [`Pool::ELEMENT_TYPE_VOLUME_FULL`][5]
    ///  * [`Pool::ELEMENT_TYPE_VOLUME_THIN`][6]
    ///  * [`Pool::ELEMENT_TYPE_SYS_RESERVED`][7]
    ///
    /// The values are stored in bitmap:
    ///
    /// ```rust
    /// use lsm::{Client, Pool};
    ///
    /// let mut c = Client::new("sim://", None, None).unwrap();
    /// let ps = c.pools().unwrap();
    ///
    /// for p in ps {
    ///     if (p.element_type & Pool::ELEMENT_TYPE_VOLUME) == 0 {
    ///         println!("Pool {} could create volume", p.name);
    ///     }
    /// }
    /// ```
    /// [1]: #associatedconstant.ELEMENT_TYPE_POOL
    /// [2]: #associatedconstant.ELEMENT_TYPE_VOLUME
    /// [3]: #associatedconstant.ELEMENT_TYPE_FS
    /// [4]: #associatedconstant.ELEMENT_TYPE_DELTA
    /// [5]: #associatedconstant.ELEMENT_TYPE_VOLUME_FULL
    /// [6]: #associatedconstant.ELEMENT_TYPE_VOLUME_THIN
    /// [7]: #associatedconstant.ELEMENT_TYPE_SYS_RESERVED
    pub element_type: u64,
    /// The actions does not supported by this pool.
    /// Valid values are:
    ///
    ///  * [`Pool::UNSUPPORTED_VOLUME_GROW`][1]
    ///  * [`Pool::UNSUPPORTED_VOLUME_SHRINK`][2]
    ///
    /// The values are stored in bitmap:
    ///
    /// ```rust
    /// use lsm::{Client, Pool};
    ///
    /// let mut c = Client::new("sim://", None, None).unwrap();
    /// let ps = c.pools().unwrap();
    ///
    /// for p in ps {
    ///     if (p.unsupported_actions & Pool::UNSUPPORTED_VOLUME_GROW) == 0 {
    ///         println!("Pool {} cannot grow size of volume", p.name);
    ///     }
    /// }
    /// ```
    /// [1]: #associatedconstant.UNSUPPORTED_VOLUME_GROW
    /// [2]: #associatedconstant.UNSUPPORTED_VOLUME_SHRINK
    pub unsupported_actions: u64,
    /// Total space in bytes.
    pub total_space: u64,
    /// Free space in bytes.
    pub free_space: u64,
    /// Pool status stored in bitmap. Valid status value are:
    ///
    ///  * [`Pool::STATUS_UNKNOWN`][1]
    ///  * [`Pool::STATUS_OK`][2]
    ///  * [`Pool::STATUS_OTHER`][3]
    ///  * [`Pool::STATUS_DEGRADED`][4]
    ///  * [`Pool::STATUS_ERROR`][5]
    ///  * [`Pool::STATUS_STOPPED`][6]
    ///  * [`Pool::STATUS_STARTING`][7]
    ///  * [`Pool::STATUS_RECONSTRUCTING`][8]
    ///  * [`Pool::STATUS_VERIFYING`][9]
    ///  * [`Pool::STATUS_INITIALIZING`][10]
    ///  * [`Pool::STATUS_GROWING`][11]
    ///
    /// ```rust
    /// use lsm::{Client, Pool};
    ///
    /// let mut c = Client::new("sim://", None, None).unwrap();
    /// let ps = c.pools().unwrap();
    ///
    /// for p in ps {
    ///     if (p.status & Pool::STATUS_OK) == 0 {
    ///         println!("Pool is not healthy");
    ///     }
    /// }
    /// ```
    /// [1]: #associatedconstant.STATUS_UNKNOWN
    /// [2]: #associatedconstant.STATUS_OK
    /// [3]: #associatedconstant.STATUS_OTHER
    /// [4]: #associatedconstant.STATUS_DEGRADED
    /// [5]: #associatedconstant.STATUS_ERROR
    /// [6]: #associatedconstant.STATUS_STOPPED
    /// [7]: #associatedconstant.STATUS_STARTING
    /// [8]: #associatedconstant.STATUS_RECONSTRUCTING
    /// [9]: #associatedconstant.STATUS_VERIFYING
    /// [10]: #associatedconstant.STATUS_INITIALIZING
    /// [11]: #associatedconstant.STATUS_GROWING
    pub status: u64,
    /// Additional message for status.
    pub status_info: Option<String>,
    plugin_data: Option<String>,
    /// Identifier of owner system.
    pub system_id: String,
}

impl Pool {
    /// This pool could allocate space for sub-pool.
    pub const ELEMENT_TYPE_POOL: u64 = 1 << 1;
    /// This pool could create volume.
    pub const ELEMENT_TYPE_VOLUME: u64 = 1 << 2;
    /// This pool could create file system.
    pub const ELEMENT_TYPE_FS: u64 = 1 << 3;
    /// This pool could hold delta data for snapshots.
    pub const ELEMENT_TYPE_DELTA: u64 = 1 << 4;
    /// This pool could create fully allocated volume.
    pub const ELEMENT_TYPE_VOLUME_FULL: u64 = 1 << 5;
    /// This pool could create thin provisioned volume.
    pub const ELEMENT_TYPE_VOLUME_THIN: u64 = 1 << 6;
    /// This pool is reserved for system internal use.
    pub const ELEMENT_TYPE_SYS_RESERVED: u64 = 1 << 10;

    /// This pool cannot grow size of its volume.
    pub const UNSUPPORTED_VOLUME_GROW: u64 = 1;
    /// This pool cannot shrink size of its volume.
    pub const UNSUPPORTED_VOLUME_SHRINK: u64 = 1 << 1;

    /// Plugin failed to query pool status.
    pub const STATUS_UNKNOWN: u64 = 1;
    /// The data of this pool is accessible with not data lose. But it might
    /// along with `Pool::STATUS_DEGRADED` to indicate redundancy lose.
    pub const STATUS_OK: u64 = 1 << 1;
    /// Vendor specific status. The `Pool.status_info` property will explain
    /// the detail.
    pub const STATUS_OTHER: u64 = 1 << 2;
    /// Pool is lost data redundancy due to I/O error or offline of one or more
    /// RAID member. Often come with `Pool::STATUS_OK` to indicate data is
    /// still accessible with not data lose. Example:
    ///
    ///  * RAID 6 pool lost access to 1 disk or 2 disks.
    ///
    ///  * RAID 5 pool lost access to 1 disk.
    pub const STATUS_DEGRADED: u64 = 1 << 4;
    /// Pool data is not accessible due to some members offline. Example:
    ///
    ///  * RAID 5 pool lost access to 2 disks.
    ///
    ///  * RAID 0 pool lost access to 1 disks.
    pub const STATUS_ERROR: u64 = 1 << 5;
    ///  Pool is stopping by administrator. Pool data is not accessible.
    pub const STATUS_STOPPED: u64 = 1 << 9;
    ///  Pool is reviving from STOPPED status. Pool data is not accessible yet.
    pub const STATUS_STARTING: u64 = 1 << 10;
    /// Pool is reconstructing the hash data or mirror data. Mostly happen
    /// when disk revive from offline or disk replaced. `Pool.status_info` may
    /// contain progress of this reconstruction job. Often come with
    /// `Pool::STATUS_DEGRADED` and `Pool::STATUS_OK`.
    pub const STATUS_RECONSTRUCTING: u64 = 1 << 12;
    /// Array is running integrity check on data of current pool. It might be
    /// started by administrator or array itself. The I/O performance will be
    /// impacted. Pool.status_info may contain progress of this verification
    /// job. Often come with `Pool::STATUS_OK` to indicate data is still
    /// accessible.
    pub const STATUS_VERIFYING: u64 = 1 << 13;
    /// Pool is not accessable and performing initializing task. Often happen
    /// on newly created pool.
    pub const STATUS_INITIALIZING: u64 = 1 << 14;
    /// Pool is growing its size and doing internal jobs. `Pool.status_info`
    /// can contain progress of this growing job. Often come with
    /// `Pool::STATUS_OK` to indicate data is still accessible.
    pub const STATUS_GROWING: u64 = 1 << 15;
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct Disk {
    #[serde(default = "gen_disk_class_string")] class: String,
    /// Identifier.
    pub id: String,
    /// Human friendly name.
    pub name: String,
    #[serde(deserialize_with = "int_to_disk_type")]
    #[serde(serialize_with = "disk_type_to_int")]
    /// Disk type.
    pub disk_type: DiskType,
    /// Block size in bytes.
    pub block_size: u64,
    /// Count of block.
    pub num_of_blocks: u64,
    /// Disk status stored in bitmap. Valid status value are:
    ///
    ///  * [`Disk::STATUS_UNKNOWN`][1]
    ///  * [`Disk::STATUS_OK`][2]
    ///  * [`Disk::STATUS_OTHER`][3]
    ///  * [`Disk::STATUS_PREDICTIVE_FAILURE`][4]
    ///  * [`Disk::STATUS_ERROR`][5]
    ///  * [`Disk::STATUS_REMOVED`][6]
    ///  * [`Disk::STATUS_STARTING`][7]
    ///  * [`Disk::STATUS_STOPPING`][8]
    ///  * [`Disk::STATUS_STOPPED`][9]
    ///  * [`Disk::STATUS_INITIALIZING`][10]
    ///  * [`Disk::STATUS_MAINTENANCE_MODE`][11]
    ///  * [`Disk::STATUS_SPARE_DISK`][12]
    ///  * [`Disk::STATUS_RECONSTRUCT`][13]
    ///  * [`Disk::STATUS_FREE`][14]
    ///
    /// ```rust
    /// use lsm::{Client, Disk};
    ///
    /// let mut c = Client::new("sim://", None, None).unwrap();
    /// let ds = c.disks().unwrap();
    ///
    /// for d in ds {
    ///     if (d.status & Disk::STATUS_OK) == 0 {
    ///         println!("Disk is not healthy");
    ///     }
    /// }
    /// ```
    /// [1]: #associatedconstant.STATUS_UNKNOWN
    /// [2]: #associatedconstant.STATUS_OK
    /// [3]: #associatedconstant.STATUS_OTHER
    /// [4]: #associatedconstant.STATUS_PREDICTIVE_FAILURE
    /// [5]: #associatedconstant.STATUS_ERROR
    /// [6]: #associatedconstant.STATUS_REMOVED
    /// [7]: #associatedconstant.STATUS_STARTING
    /// [8]: #associatedconstant.STATUS_STOPPING
    /// [9]: #associatedconstant.STATUS_STOPPED
    /// [10]: #associatedconstant.STATUS_INITIALIZING
    /// [11]: #associatedconstant.STATUS_MAINTENANCE_MODE
    /// [12]: #associatedconstant.STATUS_SPARE_DISK
    /// [13]: #associatedconstant.STATUS_RECONSTRUCT
    /// [14]: #associatedconstant.STATUS_FREE
    pub status: u64,
    plugin_data: Option<String>,
    /// Identifier of owner system.
    pub system_id: String,
    /// Disk location in storage topology.
    pub location: Option<String>,
    /// Disk rotation speed - revolutions per minute(RPM):
    ///
    ///  * `-1` -- Unknown RPM speed.
    ///
    ///  * `0` -- Non-rotating medium (e.g., SSD).
    ///
    ///  * `1` -- Rotational disk with unknown speed.
    ///
    ///  * `> 1` -- Normal rotational disk (e.g., HDD).
    pub rpm: Option<i32>,
    #[serde(deserialize_with = "int_to_disk_link_type")]
    #[serde(serialize_with = "disk_link_type_to_int")]
    /// Disk data link type.
    pub link_type: Option<DiskLinkType>,
    /// SCSI VPD 0x83 NAA type identifier.
    /// Udev treat it as `ID_WWN_WITH_EXTENSION`
    pub vpd83: Option<String>,
}

#[repr(i32)]
#[derive(Debug, Clone, PartialEq, Copy)]
/// Represent disk type.
pub enum DiskType {
    /// Plugin failed to query disk type.
    Unknown = 0,
    /// Vendor specific disk type.
    Other = 1,
    /// IDE disk.
    Ata = 3,
    /// SATA disk.
    Sata = 4,
    /// SAS disk.
    Sas = 5,
    /// FC disk.
    Fc = 6,
    /// SCSI over PCI-Express.
    Sop = 7,
    /// SCSI disk.
    Scsi = 8,
    /// Remote LUN from SAN array.
    Lun = 9,
    /// Near-Line SAS, just SATA disk + SAS port.
    NlSas = 51,
    /// Normal HDD, fall back value if failed to detect HDD type(SAS/SATA/etc).
    Hdd = 52,
    /// Solid State Drive.
    Ssd = 53,
    /// Hybrid disk uses a combination of HDD and SSD.
    Hybrid = 54,
}

fn int_to_disk_type<'de, D: Deserializer<'de>>(
    deserializer: D,
) -> ::std::result::Result<DiskType, D::Error> {
    let i: i32 = Deserialize::deserialize(deserializer)?;
    match i {
        0 | 1 | 3...9 | 51...54 => unsafe { Ok(transmute(i)) },
        _ => Ok(DiskType::Unknown),
    }
}

fn disk_type_to_int<S: Serializer>(
    t: &DiskType,
    serializer: S,
) -> ::std::result::Result<S::Ok, S::Error> {
    serializer.serialize_i32(*t as i32)
}

#[repr(i32)]
#[derive(Debug, Clone, PartialEq, Copy)]
/// Represent disk data link type.
pub enum DiskLinkType {
    /// Plugin does not support querying disk link type.
    NoSupport = -2,
    /// Plugin failed to query disk link type.
    Unknown = -1,
    /// Fibre Channel.
    Fc = 0,
    /// Serial Storage Architecture, Old IBM tech.
    Ssa = 2,
    /// Serial Bus Protocol, used by IEEE 1394.
    Sbp = 3,
    /// SCSI RDMA Protocol.
    Srp = 4,
    /// Internet Small Computer System Interface
    Iscsi = 5,
    /// Serial Attached SCSI.
    Sas = 6,
    /// Automation/Drive Interface Transport. Often used by tape.
    Adt = 7,
    /// PATA/IDE or SATA.
    Ata = 8,
    /// USB
    Usb = 9,
    /// SCSI over PCI-E.
    Sop = 10,
    /// PCI-E, e.g. NVMe.
    PciE = 11,
}

fn int_to_disk_link_type<'de, D: Deserializer<'de>>(
    deserializer: D,
) -> ::std::result::Result<Option<DiskLinkType>, D::Error> {
    let i: i32 = Deserialize::deserialize(deserializer)?;
    match i {
        -2...11 => unsafe { Ok(Some(transmute(i))) },
        _ => Ok(Some(DiskLinkType::Unknown)),
    }
}

fn disk_link_type_to_int<S: Serializer>(
    t: &Option<DiskLinkType>,
    serializer: S,
) -> ::std::result::Result<S::Ok, S::Error> {
    match *t {
        Some(i) => serializer.serialize_i32(i as i32),
        None => serializer.serialize_i32(DiskLinkType::Unknown as i32),
    }
}

impl Disk {
    /// Plugin failed to query out the status of disk.
    pub const STATUS_UNKNOWN: u64 = 1;
    /// Disk is up and healthy.
    pub const STATUS_OK: u64 = 1 << 1;
    /// Vendor specific status.
    pub const STATUS_OTHER: u64 = 1 << 2;
    /// Disk is still functional but will fail soon.
    pub const STATUS_PREDICTIVE_FAILURE: u64 = 1 << 3;
    /// Error make disk not functional.
    pub const STATUS_ERROR: u64 = 1 << 4;
    /// Disk was removed by administrator.
    pub const STATUS_REMOVED: u64 = 1 << 5;
    /// Disk is starting up.
    pub const STATUS_STARTING: u64 = 1 << 6;
    /// Disk is shutting down.
    pub const STATUS_STOPPING: u64 = 1 << 7;
    /// Disk is stopped by administrator.
    pub const STATUS_STOPPED: u64 = 1 << 8;
    ///  Disk is not functional yet, internal storage system is initializing
    ///  this disk, it could be:
    ///
    ///   * Initialising new disk.
    ///
    ///   * Zeroing disk.
    ///
    ///   * Scrubbing disk data.
    pub const STATUS_INITIALIZING: u64 = 1 << 9;
    /// In maintenance for bad sector scan, integrity check and etc It might be
    /// combined with `Disk::STATUS_OK` or `Disk::STATUS_STOPPED` for online
    /// maintenance or offline maintenance.
    pub const STATUS_MAINTENANCE_MODE: u64 = 1 << 10;
    /// Disk is configured as spare disk.
    pub const STATUS_SPARE_DISK: u64 = 1 << 11;
    /// Disk is reconstructing its data.
    pub const STATUS_RECONSTRUCT: u64 = 1 << 12;
    /// Indicate the whole disk is not holding any data or acting as a dedicate
    /// spare disk. This disk could be assigned as a dedicated spare disk or
    /// used for creating pool. If any spare disk(like those on NetApp ONTAP)
    /// does not require any explicit action when assigning to pool, it should
    /// be treated as free disk and marked as
    /// `Disk::STATUS_FREE | Disk::STATUS_SPARE_DISK`.
    pub const STATUS_FREE: u64 = 1 << 13;
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct FileSystem {
    #[serde(default = "gen_fs_class_string")] class: String,
    /// Identifier.
    pub id: String,
    /// Human friendly name.
    pub name: String,
    /// Total space in bytes.
    pub total_space: u64,
    /// Free space in bytes.
    pub free_space: u64,
    plugin_data: Option<String>,
    /// Identifier of owner system.
    pub system_id: String,
    /// Identifier of owner pool.
    pub pool_id: String,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct FileSystemSnapShot {
    #[serde(default = "gen_fs_snap_class_string")] class: String,
    /// Identifier.
    pub id: String,
    /// Human friendly name.
    pub name: String,
    /// POSIX time(epoch time) on creation.
    pub ts: u64,
    plugin_data: Option<String>,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct NfsExport {
    #[serde(default = "gen_exp_class_string")] class: String,
    /// Identifier.
    pub id: String,
    /// Identifier of file system.
    pub fs_id: String,
    /// NFS export path.
    pub export_path: String,
    /// NFS authentication type.
    pub auth: String,
    /// Host list with root access.
    pub root: Vec<String>,
    /// Host list with read and write access.
    pub rw: Vec<String>,
    /// Host list with read only access.
    pub ro: Vec<String>,
    /// User ID for anonymous access.
    pub anonuid: i64,
    /// Group ID for anonymous access.
    pub anongid: i64,
    /// NFS extra options.
    pub options: String,
    plugin_data: Option<String>,
}

impl NfsExport {
    /// Default user and group ID for anonymous access.
    pub const ANON_UID_GID_NA: i64 = -1;
}

#[derive(Serialize, Deserialize, Debug, Clone)]
/// Access group is also known as host group on some storage system, it defines
/// a group of initiators sharing the same access to the volume.
pub struct AccessGroup {
    #[serde(default = "gen_ag_class_string")] class: String,
    /// Identifier
    pub id: String,
    /// Human friendly name.
    pub name: String,
    /// Initiator list.
    pub init_ids: Vec<String>,
    #[serde(deserialize_with = "int_to_init_type")]
    #[serde(serialize_with = "init_type_to_int")]
    /// Initiator type.
    pub init_type: InitiatorType,
    plugin_data: Option<String>,
    pub system_id: String,
}

#[repr(i32)]
#[derive(Debug, Clone, PartialEq, Copy)]
pub enum InitiatorType {
    /// Plugin failed to query initiator type.
    Unknown = 0,
    /// Vendor specific initiator type.
    Other = 1,
    /// FC or FCoE WWPN
    Wwpn = 2,
    /// iSCSI IQN
    IscsiIqn = 5,
    /// This access group contains more 1 type of initiator.
    Mixed = 7,
}

fn int_to_init_type<'de, D: Deserializer<'de>>(
    deserializer: D,
) -> ::std::result::Result<InitiatorType, D::Error> {
    let i: i32 = Deserialize::deserialize(deserializer)?;
    match i {
        0 | 1 | 2 | 5 | 7 => unsafe { Ok(transmute(i)) },
        _ => Ok(InitiatorType::Unknown),
    }
}

fn init_type_to_int<S: Serializer>(
    i: &InitiatorType,
    serializer: S,
) -> ::std::result::Result<S::Ok, S::Error> {
    serializer.serialize_i32(*i as i32)
}

#[derive(Deserialize, Debug, Clone)]
/// Represent a target port which is the front-end port of storage system which
/// storage user/client connect to and get storage service from.
pub struct TargetPort {
    /// Identifier.
    pub id: String,
    #[serde(deserialize_with = "int_to_port_type")]
    /// Type of port.
    pub port_type: PortType,
    /// The address used by upper layer like FC and iSCSI:
    ///
    ///  * FC and FCoE:    WWPN
    ///
    ///  * iSCSI:          IQN
    /// The string is in lower case, split with `:` every two digits if WWPN.
    pub service_address: String,
    /// The address used by network layer like FC and TCP/IP:
    ///
    ///  * FC/FCoE:        WWPN
    ///
    ///  * iSCSI:          `IPv4:Port` or `[IPv6]:Port`
    /// The string is in lower case, split with `:` every two digits if WWPN.
    pub network_address: String,
    /// The address used by physical layer like FC-0 and MAC:
    ///
    ///  * FC and FCoE :   WWPN
    ///
    ///  * iSCSI:          MAC
    /// The string is in Lower case, split with `:` every two digits.
    pub physical_address: String,
    /// The name of physical port. Administrator could use this name to locate
    /// the port on storage system. E.g. 'eth0'
    pub physical_name: String,
    plugin_data: Option<String>,
    /// Identifier of owner system.
    pub system_id: String,
}

#[repr(i32)]
#[derive(Debug, Clone, PartialEq, Copy)]
pub enum PortType {
    /// Vendor specific initiator type.
    Other = 1,
    /// FC port
    Fc = 2,
    /// FCoE port
    FCoE = 3,
    /// iSCSI port
    Iscsi = 4,
}

fn int_to_port_type<'de, D: Deserializer<'de>>(
    deserializer: D,
) -> ::std::result::Result<PortType, D::Error> {
    let i: i32 = Deserialize::deserialize(deserializer)?;
    match i {
        1 | 2 | 3 | 4 => unsafe { Ok(transmute(i)) },
        _ => Ok(PortType::Other),
    }
}

#[derive(Deserialize, Debug, Clone)]
/// Represent a battery.
pub struct Battery {
    /// Identifier.
    pub id: String,
    /// Human friendly name.
    pub name: String,
    #[serde(rename = "type")]
    #[serde(deserialize_with = "int_to_battery_type")]
    /// Battery type.
    pub battery_type: BatteryType,
    /// Battery status stored in bitmap. Valid status value are:
    ///
    ///  * [`Battery::STATUS_UNKNOWN`][1]
    ///  * [`Battery::STATUS_OTHER`][2]
    ///  * [`Battery::STATUS_OK`][3]
    ///  * [`Battery::STATUS_DISCHARGING`][4]
    ///  * [`Battery::STATUS_CHARGING`][5]
    ///  * [`Battery::STATUS_LEARNING`][6]
    ///  * [`Battery::STATUS_DEGRADED`][7]
    ///  * [`Battery::STATUS_ERROR`][8]
    ///
    /// ```rust
    /// use lsm::{Client, Battery};
    ///
    /// let mut c = Client::new("sim://", None, None).unwrap();
    /// let bs = c.batteries().unwrap();
    ///
    /// for b in bs {
    ///     if (b.status & Battery::STATUS_OK) == 0 {
    ///         println!("Battery is not healthy");
    ///     }
    /// }
    /// ```
    /// [1]: #associatedconstant.STATUS_UNKNOWN
    /// [2]: #associatedconstant.STATUS_OTHER
    /// [3]: #associatedconstant.STATUS_OK
    /// [4]: #associatedconstant.STATUS_DISCHARGING
    /// [5]: #associatedconstant.STATUS_CHARGING
    /// [6]: #associatedconstant.STATUS_LEARNING
    /// [7]: #associatedconstant.STATUS_DEGRADED
    /// [8]: #associatedconstant.STATUS_ERROR
    pub status: u64,
    plugin_data: Option<String>,
    /// Identifier of owner system.
    pub system_id: String,
}

impl Battery {
    /// Plugin failed to query battery status.
    pub const STATUS_UNKNOWN: u64 = 1;
    /// Vendor specific status.
    pub const STATUS_OTHER: u64 = 1 << 1;
    /// Battery is healthy and charged.
    pub const STATUS_OK: u64 = 1 << 2;
    /// Battery is disconnected from power source and discharging.
    pub const STATUS_DISCHARGING: u64 = 1 << 3;
    /// Battery is not fully charged and charging.
    pub const STATUS_CHARGING: u64 = 1 << 4;
    /// System is trying to discharge and recharge the battery to learn its
    /// capability.
    pub const STATUS_LEARNING: u64 = 1 << 5;
    /// Battery is degraded and should be checked or replaced.
    pub const STATUS_DEGRADED: u64 = 1 << 6;
    /// Battery is dead and should be replaced.
    pub const STATUS_ERROR: u64 = 1 << 7;
}

#[repr(i32)]
#[derive(Debug, Clone, PartialEq, Copy)]
pub enum BatteryType {
    /// Plugin failed to detect battery type.
    Unknown = 1,
    /// Vendor specific battery type.
    Other = 2,
    /// Chemical battery, e.g. Li-ion battery.
    Chemical = 3,
    /// Super capacitor.
    Capacitor = 4,
}

fn int_to_battery_type<'de, D: Deserializer<'de>>(
    deserializer: D,
) -> ::std::result::Result<BatteryType, D::Error> {
    let i: i32 = Deserialize::deserialize(deserializer)?;
    match i {
        1 | 2 | 3 | 4 => unsafe { Ok(transmute(i)) },
        _ => Ok(BatteryType::Unknown),
    }
}

#[derive(Deserialize, Debug, Clone)]
/// Represent capabilities supported by specific system.
pub struct Capabilities {
    cap: String,
}

// TODO(Gris Ge): link function to their document URL.
#[repr(usize)]
/// Represent a capability supported by specific system.
pub enum Capability {
    /// Support `Client::volumes()`.
    Volumes = 20,
    /// Support `Client::volume_create()`.
    VolumeCreate = 21,
    /// Support `Client::volume_resize()`.
    VolumeResize = 22,
    /// Support `Client::volume_replicate()`.
    VolumeReplicate = 23,
    /// Support `Client::volume_replicate()` with
    /// `VolumeReplicateType::Clone`.
    VolumeReplicateClone = 24,
    /// Support `Client::volume_replicate()` with
    /// `VolumeReplicateType::Copy`.
    VolumeReplicateCopy = 25,
    /// Support `Client::volume_replicate()` with
    /// `VolumeReplicateType::MirrorAsync`.
    VolumeReplicateMirrorAsync = 26,
    /// Support `Client::volume_replicate()` with
    /// `VolumeReplicateType::MirrorSync`.
    VolumeReplicateMirrorSync = 27,
    /// Support `Client::volume_rep_range_blk_size()`.
    VolumeRepRangeBlockSize = 28,
    /// Support `Client::volume_rep_range()`.
    VolumeRepRange = 29,
    /// Support `Client::volume_rep_range()` with `VolumeReplicateType::Clone`.
    VolumeRepRangeClone = 30,
    /// Support `Client::volume_rep_range()` with `VolumeReplicateType::Copy`.
    VolumeRepRangeCopy = 31,
    /// Support `Client::volume_delete()`.
    VolumeDelete = 33,
    /// Support `Client::volume_enable()`.
    VolumeEnable = 34,
    /// Support `Client::volume_disable()`.
    VolumeDisable = 35,
    /// Support `Client::volume_mask()`.
    VolumeMask = 36,
    /// Support `Client::volume_unmask()`.
    VolumeUnmask = 37,
    /// Support `Client::access_groups()`.
    AccessGroups = 38,
    /// Support `Client::access_group_create()` with `InitiatorType::Wwpn`.
    AccessGroupCreateWwpn = 39,
    /// Support `Client::access_group_delete()`.
    AccessGroupDelete = 40,
    /// Support `Client::access_group_init_add()` with `InitiatorType::Wwpn`.
    AccessGroupInitAddWwpn = 41,
    /// Support `Client::access_group_init_del()`.
    AccessGroupInitDel = 42,
    /// Support `Client::vols_masked_to_ag()`.
    VolsMaskedToAg = 43,
    /// Support `Client::ags_granted_to_vol()`.
    AgsGrantedToVol = 44,
    /// Support `Client::vol_has_child_dep()`.
    VolHasChildDep = 45,
    /// Support `Client::vol_child_dep_rm()`.
    VolChildDepRm = 46,
    /// Support `Client::access_group_create()` with `InitiatorType::IscsiIqn`.
    AccessGroupCreateIscsiIqn = 47,
    /// Support `Client::access_group_init_add()` with
    /// `InitiatorType::IscsiIqn`.
    AccessGroupInitAddIscsiIqn = 48,
    /// Support `Client::iscsi_chap_auth_set()`.
    IscsiChapAuthSet = 53,
    /// Support `Client::vol_raid_info()`.
    VolRaidInfo = 54,
    /// Support `Client::volume_crate()` with
    /// `thinp=VolumeCreateArgThinP::Thin` argument.
    VolumeThin = 55,
    /// Support `Client::batteries()`.
    Batteries = 56,
    /// Support `Client::vol_cache_info()`.
    VolCacheInfo = 57,
    /// Support `Client::vol_phy_disk_cache_set().`
    VolPhyDiskCacheSet = 58,
    /// Indicate the `Client::vol_phy_disk_cache_set()` will change system
    /// settings which are effective on all volumes in this storage system.
    /// For example, on HPE SmartArray, the physical disk cache setting is a
    /// controller level setting.
    VolPhysicalDiskCacheSetSystemLevel = 59,
    /// Support `Client::vol_write_cache_set()` with
    /// `wcp=Cache::Enabled`.
    VolWriteCacheSetEnable = 60,
    /// Support `Client::vol_write_cache_set()` with
    /// `wcp=Cache::Auto`.
    VolWriteCacheSetAuto = 61,
    /// Support `Client::vol_write_cache_set()` with
    /// `wcp=Cache::Disabled`.
    VolWriteCacheSetDisabled = 62,
    /// Indicate the `Client::vol_write_cache_set()` might also impact read
    /// cache policy.
    VolWriteCacheSetImpactRead = 63,
    /// Indicate the `Client::vol_write_cache_set()` with
    /// `wcp=Cache::Enabled` might impact other volumes in the same
    /// system.
    VolWriteCacheSetWbImpactOther = 64,
    /// Support `Client::vol_read_cache_set()`.
    VolReadCacheSet = 65,
    /// Indicate the `Client::vol_read_cache_set()` might also impact write
    /// cache policy.
    VolReadCacheSetImpactWrite = 66,
    /// Support `Client::fs()`.
    Fs = 100,
    /// Support `Client::fs_delete()`.
    FsDelete = 101,
    /// Support `Client::fs_resize()`.
    FsResize = 102,
    /// Support `Client::fs_create()`.
    FsCreate = 103,
    /// Support `Client::fs_clone()`.
    FsClone = 104,
    /// Support `Client::fs_file_clone()`.
    FsFileClone = 105,
    /// Support `Client::fs_snapshots()`.
    FsSnapshots = 106,
    /// Support `Client::fs_snapshot_create()`.
    FsSnapshotCreate = 107,
    /// Support `Client::fs_snapshot_delete()`.
    FsSnapshotDelete = 109,
    /// Support `Client::fs_snapshot_restore()`.
    FsSnapshotRestore = 110,
    /// Support `Client::fs_snapshot_restore()` with `files` arugment.
    FsSnapshotRestoreSpecificFiles = 111,
    /// Support `Client::fs_has_child_dep()`.
    FsHasChildDep = 112,
    /// Support `Client::fs_child_dep_rm()`.
    FsChildDepRm = 113,
    /// Support `Client::fs_child_dep_rm()` with `files` argument.
    FsChildDepRmSpecificFiles = 114,
    /// Support `Client:::nfs_exp_auth_type_list()`.
    NfsExportAuthTypeList = 120,
    /// Support `Client::nfs_exports()`.
    NfsExports = 121,
    /// Support `Client::fs_export()`.
    FsExport = 122,
    /// Support `Client::fs_unexport()`.
    FsUnexport = 123,
    /// Support `Client::fs_export()` with `export_path` argument.
    FsExportCustomPath = 124,
    /// Support `Client::system_read_cache_pct_set()`
    SysReadCachePctSet = 158,
    /// Support `Client::systems()` with valid `read_cache_pct` property.
    SysReadCachePctGet = 159,
    /// Support `Client::systems()` with valid `fw_version` property.
    SysFwVersionGet = 160,
    /// Support `Client::systems()` with valid `mode` property.
    SysModeGet = 161,
    /// Support `Client::disks()` with valid `location` property.
    DiskLocation = 163,
    /// Support `Client::disks()` with valid `rpm` property.
    DiskRpm = 164,
    /// Support `Client::disks()` with valid `link_type` property.
    DiskLinkType = 165,
    /// Support `Client::vol_ident_led_on()` and `Client::vol_ident_led_off()`.
    VolumeLed = 171,
    /// Support `Client::target_ports()`.
    TargetPorts = 216,
    /// Support `Client::disks()`.
    Disks = 220,
    /// Support `Client::pool_member_info()`.
    PoolMemberInfo = 221,
    /// Support `Client::vol_raid_create_cap_get()` and
    /// `Client::vol_raid_create()`.
    VolumeRaidCreate = 222,
    /// Support `Client::disks()` with valid `vpd83` property.
    DiskVpd83Get = 223,
}

impl Capabilities {
    /// Check wether certain [`Capacity`][1] is supported or not.
    ///
    /// [1]: struct.Capacity.html
    pub fn is_supported(&self, cap: Capability) -> bool {
        let cap_num = cap as usize;
        let val = &self.cap[cap_num * 2..cap_num * 2 + 2];
        println!("val {}", val);
        match val {
            "01" => true,
            _ => false,
        }
    }
}

#[derive(Serialize, Debug, Clone)]
// TODO(Gris Ge): Set URL link.
/// Represent a block range used `Client::volume_replicate_range()`.
pub struct BlockRange {
    class: String,
    #[serde(rename = "src_block")] src_blk_addr: u64,
    #[serde(rename = "dest_block")] dst_blk_addr: u64,
    #[serde(rename = "block_count")] blk_count: u64,
}

impl BlockRange {
    /// Create a block range.
    pub fn new(
        src_blk_addr: u64,
        dst_blk_addr: u64,
        blk_count: u64,
    ) -> BlockRange {
        BlockRange {
            class: "BlockRange".to_string(),
            src_blk_addr,
            dst_blk_addr,
            blk_count,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Copy)]
/// Represent a volume cache policy.
pub enum CachePolicy {
    /// Cache is enabled.
    Enabled,
    /// Storage system will determin whethere to use cache based on
    /// battery/capacitor health.
    Auto, // Only for write cache
    /// Cache is disabeld.
    Disabled,
    /// Plugin failed to query cache setting.
    Unknown,
    /// Physical disk cache is determined by the disk vendor via physical
    /// disks' SCSI caching mode page(`0x08` page).
    UseDiskSetting, // Only for physical disk cache
}

#[derive(Debug, Clone)]
/// Represent volume cache informtion.
pub struct VolumeCacheInfo {
    /// Write cache setting.
    pub write_cache_setting: CachePolicy,
    /// Write cache status.
    pub write_cache_status: CachePolicy,
    /// Read cache setting.
    pub read_cache_setting: CachePolicy,
    /// Read cache status
    pub read_cache_status: CachePolicy,
    /// Physcial disk cache status.
    pub physical_disk_cache_status: CachePolicy,
}

#[derive(Debug, Clone)]
/// Represent NFS access control information.
pub struct NfsAccess<'a> {
    /// List of hosts with root access.
    pub root_list: &'a [&'a str],
    /// List of hosts with read and write access.
    pub rw_list: &'a [&'a str],
    /// List of hosts with read only access.
    pub ro_list: &'a [&'a str],
    /// UID to map to anonymous
    pub anon_uid: Option<i64>,
    /// GID to map to anonymous
    pub anon_gid: Option<i64>,
}

// TODO(Gris Ge): Update URL of volume_create() here
/// For argument `thinp` of `Client::volume_create()`
pub enum VolumeCreateArgThinP {
    /// Create fully allocationed volume.
    Full,
    /// Create thin provisioning volume.
    Thin,
    /// Let storage array to decide the volume provisioning type.
    Default,
}
