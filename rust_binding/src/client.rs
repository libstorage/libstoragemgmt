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

use std::thread::sleep;
use std::time::Duration;
use serde_json::{Map, Value};
use serde_json;
use url;
use std::fs::read_dir;

use super::data::*;
use super::ipc::{uds_path, TransPort};
use super::error::*;
use super::misc::verify_init_id_str;

const DEFAULT_TIMEOUT: u32 = 30_000;
const JOB_RETRY_INTERVAL: u64 = 1;

/// Represent the connection to plugin.
pub struct Client {
    tp: TransPort,
    plugin_name: String,
}

#[derive(Deserialize, Debug)]
struct Job {
    status: u32,
    percent: u8,
    data: Option<Value>,
}

const JOB_STATUS_INPROGRESS: u32 = 1;
const JOB_STATUS_COMPLETE: u32 = 2;
const JOB_STATUS_ERROR: u32 = 3;
const VOLUME_THINP_YES: u32 = 1;
const VOLUME_THINP_NO: u32 = 2;
const VOLUME_THINP_DEFAULT: u32 = 3;
const POOL_MEMBER_TYPE_DISK: u32 = 2;
const POOL_MEMBER_TYPE_POOL: u32 = 3;

const WRITE_CACHE_POLICY_WRITE_BACK: u8 = 2;
const WRITE_CACHE_POLICY_AUTO: u8 = 3;
const WRITE_CACHE_POLICY_WRITE_THROUGH: u8 = 4;

const WRITE_CACHE_STATUS_WRITE_BACK: u8 = 2;
const WRITE_CACHE_STATUS_WRITE_THROUGH: u8 = 3;

const READ_CACHE_POLICY_ENABLED: u8 = 2;
const READ_CACHE_POLICY_DISABLED: u8 = 3;

const READ_CACHE_STATUS_ENABLED: u8 = 2;
const READ_CACHE_STATUS_DISABLED: u8 = 3;

const PHYSICAL_DISK_CACHE_ENABLED: u8 = 2;
const PHYSICAL_DISK_CACHE_DISABLED: u8 = 3;
const PHYSICAL_DISK_CACHE_USE_DISK_SETTING: u8 = 4;

trait OkOrPlugBug<T> {
    fn ok_or_plugin_bug(self, val: &Value) -> Result<T>;
}

impl<T> OkOrPlugBug<T> for Option<T> {
    fn ok_or_plugin_bug(self, val: &Value) -> Result<T> {
        match self {
            Some(i) => Ok(i),
            None => Err(LsmError::PluginBug(format!(
                "Plugin return unexpected data: {:?}",
                val
            ))),
        }
    }
}

/// Represent a plugin information
#[derive(Debug)]
pub struct PluginInfo {
    /// Plugin version string.
    pub version: String,
    /// Plugin description.
    pub description: String,
    /// Plugin name.
    pub name: String,
}

/// Query all aviable plugin from libstoragemgmt daemon.
///
/// # Errors
///
///  * [`LsmError::DaemonNotRunning`][1]
///
/// [1]: enum.LsmError.html#variant.DaemonNotRunning
pub fn available_plugins() -> Result<Vec<PluginInfo>> {
    let mut ret = Vec::new();
    let uds_path = uds_path();
    match read_dir(&uds_path) {
        Err(_) => {
            return Err(LsmError::DaemonNotRunning(format!(
                "LibStorageMgmt daemon is not running for \
                 socket folder: '{}'",
                uds_path
            )))
        }
        Ok(paths) => {
            for path in paths {
                match path {
                    // Got error when interate, it might happen when
                    // daemon is starting.
                    //
                    Err(_) => continue,
                    Ok(dir_entry) => {
                        let plugin_name =
                            match dir_entry.file_name().into_string() {
                                Ok(i) => i,
                                Err(_) => continue,
                            };
                        let plugin_ipc_path = get_plugin_ipc_path(&plugin_name);
                        // We cannot use self.plugin_info() here, as we need
                        // to bypass the plugin_register() and
                        // plugin_unregister()
                        //
                        let mut tp = TransPort::new(&plugin_ipc_path)?;
                        let val = tp.invoke("plugin_info", None)?;
                        let data: Vec<String> =
                            serde_json::from_value(val.clone())?;
                        let desc = data.get(0).ok_or_plugin_bug(&val)?;
                        let version = data.get(1).ok_or_plugin_bug(&val)?;
                        ret.push(PluginInfo {
                            version: version.to_string(),
                            description: desc.to_string(),
                            name: plugin_name,
                        });
                    }
                };
            }
        }
    };

    Ok(ret)
}

fn get_plugin_ipc_path(plugin_name: &str) -> String {
    format!("{}/{}", uds_path(), plugin_name)
}

impl Client {
    /// Create a connection to plugin.
    /// Please refer to [libstoragemgmt user guide][1] for how to choose the
    /// URI and password.
    ///
    /// The `timeout` argument is in milliseconds.
    ///
    /// [1]: https://libstorage.github.io/libstoragemgmt-doc/doc/user_guide.html
    pub fn new(
        uri: &str,
        password: Option<&str>,
        timeout: Option<u32>,
    ) -> Result<Client> {
        let p = match url::Url::parse(uri) {
            Ok(p) => p,
            Err(e) => {
                return Err(LsmError::InvalidArgument(format!(
                    "Failed to parse URI: {}",
                    e
                )))
            }
        };
        let plugin_name = p.scheme().to_string();
        let plugin_ipc_path = get_plugin_ipc_path(&plugin_name);
        let mut tp = TransPort::new(&plugin_ipc_path)?;
        let mut args = Map::new();
        let timeout = timeout.unwrap_or(DEFAULT_TIMEOUT);
        args.insert("password".to_string(), serde_json::to_value(password)?);
        args.insert("uri".to_string(), serde_json::to_value(uri)?);
        args.insert("timeout".to_string(), serde_json::to_value(timeout)?);
        tp.invoke("plugin_register", Some(args))?;

        Ok(Client { tp, plugin_name })
    }

    /// Gets a list of systems on this connection.
    pub fn systems(&mut self) -> Result<Vec<System>> {
        Ok(serde_json::from_value(self.tp.invoke("systems", None)?)?)
    }

    /// Gets a list of volumes on this connection.
    pub fn volumes(&mut self) -> Result<Vec<Volume>> {
        Ok(serde_json::from_value(self.tp.invoke("volumes", None)?)?)
    }

    /// Gets a list of pools on this connection.
    pub fn pools(&mut self) -> Result<Vec<Pool>> {
        Ok(serde_json::from_value(self.tp.invoke("pools", None)?)?)
    }

    /// Gets a list of disks on this connection.
    pub fn disks(&mut self) -> Result<Vec<Disk>> {
        Ok(serde_json::from_value(self.tp.invoke("disks", None)?)?)
    }

    /// Gets a list of file systems on this connection.
    pub fn fs(&mut self) -> Result<Vec<FileSystem>> {
        Ok(serde_json::from_value(self.tp.invoke("fs", None)?)?)
    }

    /// Gets a list of NFS exports on this connection.
    pub fn nfs_exports(&mut self) -> Result<Vec<NfsExport>> {
        Ok(serde_json::from_value(self.tp.invoke("exports", None)?)?)
    }

    /// Gets a list of access group on this connection.
    pub fn access_groups(&mut self) -> Result<Vec<AccessGroup>> {
        Ok(serde_json::from_value(self.tp
            .invoke("access_groups", None)?)?)
    }

    /// Gets a list of target ports on this connection.
    pub fn target_ports(&mut self) -> Result<Vec<TargetPort>> {
        Ok(serde_json::from_value(self.tp
            .invoke("target_ports", None)?)?)
    }

    /// Gets a list of batteries on this connection.
    pub fn batteries(&mut self) -> Result<Vec<Battery>> {
        Ok(serde_json::from_value(self.tp.invoke("batteries", None)?)?)
    }

    fn _job_free(&mut self, job_id: &str) -> Result<()> {
        let mut args = Map::new();
        args.insert("job_id".to_string(), serde_json::to_value(job_id)?);
        self.tp.invoke("job_free", Some(args))?;
        Ok(())
    }

    fn _wait_job(&mut self, job_id: &str) -> Result<Value> {
        loop {
            let mut args = Map::new();
            args.insert("job_id".to_string(), serde_json::to_value(job_id)?);
            let j: Job = serde_json::from_value(self.tp
                .invoke("job_status", Some(args))?)?;

            match j.status {
                JOB_STATUS_INPROGRESS => {
                    sleep(Duration::new(JOB_RETRY_INTERVAL, 0));
                    continue;
                },
                JOB_STATUS_COMPLETE => match j.data {
                    Some(v) => {
                        self._job_free(job_id)?;
                        return Ok(v);
                    },
                    None => break
                },
                JOB_STATUS_ERROR =>
                    // The invoke command should already got error detail
                    // and returned. If not, got buggy plugin.
                    return Err(
                        LsmError::PluginBug(
                            "Got no error detail for failed job".to_string())),
                _ => return Err(
                    LsmError::PluginBug(
                        format!("Got invalid job status {}", j.status))),
            };
        }
        Ok(Value::Null)
    }

    fn wait_job_none(&mut self, job_id: &str) -> Result<()> {
        self._wait_job(job_id)?;
        Ok(())
    }

    fn wait_job_volume(&mut self, job_id: &str) -> Result<Volume> {
        match self._wait_job(job_id) {
            Ok(j) => {
                if j.is_null() {
                    Err(LsmError::PluginBug(
                        "Expecting a volume, but got None".to_string(),
                    ))
                } else {
                    let v: Volume = serde_json::from_value(j)?;
                    Ok(v)
                }
            }
            Err(e) => Err(e),
        }
    }

    fn wait_job_fs(&mut self, job_id: &str) -> Result<FileSystem> {
        match self._wait_job(job_id) {
            Ok(j) => {
                if j.is_null() {
                    Err(LsmError::PluginBug(
                        "Expecting a file system, but got None".to_string(),
                    ))
                } else {
                    let f: FileSystem = serde_json::from_value(j)?;
                    Ok(f)
                }
            }
            Err(e) => Err(e),
        }
    }

    fn wait_job_fs_snap(&mut self, job_id: &str) -> Result<FileSystemSnapShot> {
        match self._wait_job(job_id) {
            Ok(j) => {
                if j.is_null() {
                    Err(LsmError::PluginBug(
                        "Expecting a file system snapshot, but got None"
                            .to_string(),
                    ))
                } else {
                    let f: FileSystemSnapShot = serde_json::from_value(j)?;
                    Ok(f)
                }
            }
            Err(e) => Err(e),
        }
    }

    /// Create new volume.
    ///
    ///  * `pool` -- The pool where new volume should allocated from.
    ///  * `name` -- The name of new volume. It might be altered or
    ///    ignored.
    ///  * `size_bytes` -- Size in bytes of new volume. You may use function
    ///    [`size_human_2_size_bytes()`][1] to convert string like '1.1 GiB'
    ///    to integer size bytes.
    ///  * `thinp` -- Whether to create thin provisioning volume.
    ///    Check [VolumeCreateArgThinP][2]
    ///
    /// [1]: fn.size_human_2_size_bytes.html
    /// [2]: enum.VolumeCreateArgThinP.html
    pub fn volume_create(
        &mut self,
        pool: &Pool,
        name: &str,
        size_bytes: u64,
        thinp: &VolumeCreateArgThinP,
    ) -> Result<Volume> {
        let mut args = Map::new();
        let thinp_val = match *thinp {
            VolumeCreateArgThinP::Full => {
                serde_json::to_value(VOLUME_THINP_YES)?
            }
            VolumeCreateArgThinP::Thin => {
                serde_json::to_value(VOLUME_THINP_NO)?
            }
            VolumeCreateArgThinP::Default => {
                serde_json::to_value(VOLUME_THINP_DEFAULT)?
            }
        };
        args.insert("provisioning".to_string(), thinp_val);
        args.insert(
            "size_bytes".to_string(),
            serde_json::to_value(size_bytes)?,
        );
        args.insert("volume_name".to_string(), serde_json::to_value(name)?);
        args.insert("pool".to_string(), serde_json::to_value(pool)?);

        let ret = self.tp.invoke("volume_create", Some(args))?;
        self.get_vol_from_async(&ret)
    }

    /// Delete a volume
    ///
    ///
    /// # Errors
    ///
    ///  * [`LsmError::VolHasChildDep`][1] volume has child dependency. e.g.
    ///    specified volume is a replication source. Please use
    ///    [`Client::vol_child_dep_rm()`] to eliminate child dependency.
    ///
    /// [1]: enum.LsmError.html#variant.VolHasChildDep
    pub fn volume_delete(&mut self, vol: &Volume) -> Result<()> {
        let mut args = Map::new();
        args.insert("volume".to_string(), serde_json::to_value(vol)?);
        let ret = self.tp.invoke("volume_delete", Some(args))?;
        self.wait_if_async(&ret)
    }

    /// Set connection timeout value in milliseconds.
    pub fn time_out_set(&mut self, ms: u32) -> Result<()> {
        let mut args = Map::new();
        args.insert("ms".to_string(), serde_json::to_value(ms)?);
        self.tp.invoke("time_out_set", Some(args))?;
        Ok(())
    }

    /// Get connection timeout value.
    pub fn time_out_get(&mut self) -> Result<u32> {
        Ok(serde_json::from_value(self.tp
            .invoke("time_out_get", None)?)?)
    }

    /// Get system's capabilities.
    ///
    /// Capability is used to indicate whether certain functionality is
    /// supported by specified storage system. Please check desired function
    /// for required capability. To verify capability is supported, use
    /// [`Capabilities::is_supported()`][1]. If the functionality is not
    /// listed in the enumerated [`Capability`][2] type then that functionality
    /// is mandatory and required to exist.
    ///
    /// [1]: struct.Capabilities.html#method.is_supported
    /// [2]: enum.capability.html
    pub fn capabilities(&mut self, sys: &System) -> Result<Capabilities> {
        let mut args = Map::new();
        args.insert("system".to_string(), serde_json::to_value(sys)?);
        Ok(serde_json::from_value(self.tp
            .invoke("capabilities", Some(args))?)?)
    }

    /// Get plugin information.
    pub fn plugin_info(&mut self) -> Result<PluginInfo> {
        let val = self.tp.invoke("plugin_info", None)?;
        let data: Vec<String> = serde_json::from_value(val.clone())?;
        let desc = data.get(0).ok_or_plugin_bug(&val)?;
        let version = data.get(1).ok_or_plugin_bug(&val)?;
        Ok(PluginInfo {
            version: version.to_string(),
            description: desc.to_string(),
            name: self.plugin_name.clone(),
        })
    }

    /// Changes the read cache percentage for the specified system.
    ///
    /// # Errors
    ///
    ///  * [`LsmError::InvalidArgument`][1]: `read_pct` is larger than 100.
    ///
    /// [1]: enum.LsmError.html#variant.InvalidArgument
    pub fn sys_read_cache_pct_set(
        &mut self,
        sys: &System,
        read_pct: u32,
    ) -> Result<()> {
        if read_pct > 100 {
            return Err(LsmError::InvalidArgument(
                "Invalid read_pct, should be in range 0 - 100".to_string(),
            ));
        }
        let mut args = Map::new();
        args.insert("system".to_string(), serde_json::to_value(sys)?);
        args.insert("read_pct".to_string(), serde_json::to_value(read_pct)?);
        Ok(serde_json::from_value(self.tp
            .invoke("system_read_cache_pct_update", Some(args))?)?)
    }

    /// Set(override) iSCSI CHAP authentication.
    ///
    ///  * `init_id` -- Initiator ID.
    ///  * `in_user` -- The inbound authentication username. The inbound
    ///    authentication means the iSCSI initiator authenticates the iSCSI
    ///    target using CHAP.
    ///  * `in_pass` -- The inbond authentication password.
    ///  * `out_user` -- The outbound authentication username. The outbound
    ///    authentication means the iSCSI target authenticates the iSCSI
    ///    initiator using CHAP.
    ///  * `out_pass` -- The outbound authentication password.
    pub fn iscsi_chap_auth_set(
        &mut self,
        init_id: &str,
        in_user: Option<&str>,
        in_pass: Option<&str>,
        out_user: Option<&str>,
        out_pass: Option<&str>,
    ) -> Result<()> {
        let mut args = Map::new();
        args.insert("init_id".to_string(), serde_json::to_value(init_id)?);
        args.insert(
            "in_user".to_string(),
            serde_json::to_value(in_user.unwrap_or(&String::new()))?,
        );
        args.insert(
            "in_password".to_string(),
            serde_json::to_value(in_pass.unwrap_or(&String::new()))?,
        );
        args.insert(
            "out_user".to_string(),
            serde_json::to_value(out_user.unwrap_or(&String::new()))?,
        );
        args.insert(
            "out_password".to_string(),
            serde_json::to_value(out_pass.unwrap_or(&String::new()))?,
        );
        self.tp.invoke("iscsi_chap_auth", Some(args))?;
        Ok(())
    }

    /// Resize a volume.
    ///
    /// Please check whether pool allows volume resize via
    /// [`Pool.unsupported_actions`][1].
    ///
    /// [1]: struct.Pool.html#structfield.unsupported_actions
    pub fn volume_resize(
        &mut self,
        vol: &Volume,
        new_size_bytes: u64,
    ) -> Result<Volume> {
        let mut args = Map::new();
        args.insert("volume".to_string(), serde_json::to_value(vol)?);
        args.insert(
            "new_size_bytes".to_string(),
            serde_json::to_value(new_size_bytes)?,
        );
        let ret = self.tp.invoke("volume_resize", Some(args))?;
        self.get_vol_from_async(&ret)
    }

    fn wait_if_async(&mut self, ret: &Value) -> Result<()> {
        if ret.is_null() {
            return Ok(());
        }
        self.wait_job_none(ret.as_str().ok_or_plugin_bug(ret)?)
    }

    //TODO(Gris Ge): Merge get_fs_from_async() and get_vol_from_asyn().
    fn get_fs_from_async(&mut self, ret: &Value) -> Result<FileSystem> {
        let ret_array = ret.as_array().ok_or_plugin_bug(ret)?;
        if ret_array.len() != 2 {
            return Err(LsmError::PluginBug(format!(
                "Plugin return unexpected data: {:?}",
                ret
            )));
        }
        let job_id = ret_array.get(0).ok_or_plugin_bug(ret)?;
        if job_id.is_null() {
            Ok(serde_json::from_value(
                ret_array.get(1).ok_or_plugin_bug(ret)?.clone(),
            )?)
        } else {
            self.wait_job_fs(job_id.as_str().ok_or_plugin_bug(ret)?)
        }
    }

    fn get_vol_from_async(&mut self, ret: &Value) -> Result<Volume> {
        let ret_array = ret.as_array().ok_or_plugin_bug(ret)?;
        if ret_array.len() != 2 {
            return Err(LsmError::PluginBug(format!(
                "Plugin return unexpected data: {:?}",
                ret
            )));
        }
        let job_id = ret_array.get(0).ok_or_plugin_bug(ret)?;
        if job_id.is_null() {
            Ok(serde_json::from_value(
                ret_array.get(1).ok_or_plugin_bug(ret)?.clone(),
            )?)
        } else {
            self.wait_job_volume(job_id.as_str().ok_or_plugin_bug(ret)?)
        }
    }

    fn get_fs_snap_from_asyn(
        &mut self,
        ret: &Value,
    ) -> Result<FileSystemSnapShot> {
        let ret_array = ret.as_array().ok_or_plugin_bug(ret)?;
        if ret_array.len() != 2 {
            return Err(LsmError::PluginBug(format!(
                "Plugin return unexpected data: {:?}",
                ret
            )));
        }
        let job_id = ret_array.get(0).ok_or_plugin_bug(ret)?;
        if job_id.is_null() {
            Ok(serde_json::from_value(
                ret_array.get(1).ok_or_plugin_bug(ret)?.clone(),
            )?)
        } else {
            self.wait_job_fs_snap(job_id.as_str().ok_or_plugin_bug(ret)?)
        }
    }

    /// Replicate a volume.
    ///
    ///  * `pool` -- The pool where new replication target volume should be
    ///    allocated from. For `None`, will use the same pool of source volume.
    ///  * `rep_type` -- Replication type.
    ///  * `src_vol` -- Replication source volume.
    ///  * `name` -- Name for replication target volume. Might be altered or
    ///    ignored.
    pub fn volume_replicate(
        &mut self,
        pool: Option<Pool>,
        rep_type: VolumeReplicateType,
        src_vol: &Volume,
        name: &str,
    ) -> Result<Volume> {
        let mut args = Map::new();
        args.insert("pool".to_string(), serde_json::to_value(pool)?);
        args.insert("volume_src".to_string(), serde_json::to_value(src_vol)?);
        args.insert(
            "rep_type".to_string(),
            serde_json::to_value(rep_type as i32)?,
        );
        args.insert("name".to_string(), serde_json::to_value(name)?);
        let ret = self.tp.invoke("volume_replicate", Some(args))?;
        self.get_vol_from_async(&ret)
    }

    /// Block size for the [`Client::volume_replicate_range()`][1].
    ///
    /// [1]: #method.volume_replicate_range
    pub fn volume_rep_range_blk_size(&mut self, sys: &System) -> Result<i32> {
        let mut args = Map::new();
        args.insert("system".to_string(), serde_json::to_value(sys)?);
        Ok(serde_json::from_value(self.tp
            .invoke("volume_replicate_range_block_size", Some(args))?)?)
    }

    /// Replicates a portion of a volume to a volume.
    ///
    /// * `rep_type` -- Replication type.
    /// * `src_vol` -- Replication source volume.
    /// * `dst_vol` -- Replication target volume.
    /// * `ranges` -- Replication block ranges.
    pub fn volume_replicate_range(
        &mut self,
        rep_type: VolumeReplicateType,
        src_vol: &Volume,
        dst_vol: &Volume,
        ranges: &[BlockRange],
    ) -> Result<()> {
        let mut args = Map::new();
        args.insert(
            "rep_type".to_string(),
            serde_json::to_value(rep_type as i32)?,
        );
        args.insert("ranges".to_string(), serde_json::to_value(ranges)?);
        args.insert("volume_src".to_string(), serde_json::to_value(src_vol)?);
        args.insert("volume_dest".to_string(), serde_json::to_value(dst_vol)?);
        let ret = self.tp.invoke("volume_replicate_range", Some(args))?;
        self.wait_if_async(&ret)
    }

    /// Set a Volume to online.
    ///
    /// Enable the specified volume when that volume is disabled by
    /// administrator or via [`Client::volume_disable()`][1]
    ///
    /// [1]: #method.volume_disable
    pub fn volume_enable(&mut self, vol: &Volume) -> Result<()> {
        let mut args = Map::new();
        args.insert("volume".to_string(), serde_json::to_value(vol)?);
        self.tp.invoke("volume_enable", Some(args))?;
        Ok(())
    }

    /// Disable the read and write access to the specified volume.
    pub fn volume_disable(&mut self, vol: &Volume) -> Result<()> {
        let mut args = Map::new();
        args.insert("volume".to_string(), serde_json::to_value(vol)?);
        self.tp.invoke("volume_disable", Some(args))?;
        Ok(())
    }

    /// Grant access to a volume for the specified group, also known as LUN
    /// masking or mapping.
    ///
    /// # Errors
    ///
    ///  * [`LsmError::EmptyAccessGroup`][1]: Cannot mask voluem to empty
    ///    access group.
    ///
    /// [1]: enum.LsmError.html#variant.EmptyAccessGroup
    pub fn volume_mask(
        &mut self,
        vol: &Volume,
        ag: &AccessGroup,
    ) -> Result<()> {
        let mut args = Map::new();
        args.insert("volume".to_string(), serde_json::to_value(vol)?);
        args.insert("access_group".to_string(), serde_json::to_value(ag)?);
        self.tp.invoke("volume_mask", Some(args))?;
        Ok(())
    }

    /// Revokes access to a volume for the specified group
    pub fn volume_unmask(
        &mut self,
        vol: &Volume,
        ag: &AccessGroup,
    ) -> Result<()> {
        let mut args = Map::new();
        args.insert("volume".to_string(), serde_json::to_value(vol)?);
        args.insert("access_group".to_string(), serde_json::to_value(ag)?);
        self.tp.invoke("volume_unmask", Some(args))?;
        Ok(())
    }

    /// Create a access group.
    ///
    /// Creates a new access group with one initiator in it. You may expand
    /// the access group by adding more initiators via
    /// [`Client::access_group_init_add()`][1]
    ///
    /// # Errors
    ///
    ///  * [`LsmError::ExistsInitiator`][2]: Specified initiator is used by
    ///    other access group.
    ///
    /// [1]: #method.access_group_init_add
    /// [2]: enum.LsmError.html#variant.ExistsInitiator
    pub fn access_group_create(
        &mut self,
        name: &str,
        init_id: &str,
        init_type: InitiatorType,
        sys: &System,
    ) -> Result<AccessGroup> {
        verify_init_id_str(init_id, init_type)?;
        let mut args = Map::new();
        args.insert("name".to_string(), serde_json::to_value(name)?);
        args.insert("init_id".to_string(), serde_json::to_value(init_id)?);
        args.insert(
            "init_type".to_string(),
            serde_json::to_value(init_type as i32)?,
        );
        args.insert("system".to_string(), serde_json::to_value(sys)?);
        Ok(serde_json::from_value(self.tp
            .invoke("access_group_create", Some(args))?)?)
    }

    /// Delete an access group. Only access group with no volume masked can
    /// be deleted.
    ///
    /// # Errors
    ///
    ///  * [`LsmError::IsMasked`][1]: Access group has volume masked to.
    ///
    /// [1]: enum.LsmError.html#variant.IsMasked
    pub fn access_group_delete(&mut self, ag: &AccessGroup) -> Result<()> {
        let mut args = Map::new();
        args.insert("access_group".to_string(), serde_json::to_value(ag)?);
        self.tp.invoke("access_group_delete", Some(args))?;
        Ok(())
    }

    /// Add an initiator to the access group.
    ///
    /// # Errors
    ///
    ///  * [`LsmError::ExistsInitiator`][1]: Specified initiator is used by
    ///    other access group.
    ///
    /// [1]: enum.LsmError.html#variant.ExistsInitiator
    pub fn access_group_init_add(
        &mut self,
        ag: &AccessGroup,
        init_id: &str,
        init_type: InitiatorType,
    ) -> Result<(AccessGroup)> {
        verify_init_id_str(init_id, init_type)?;
        let mut args = Map::new();
        args.insert("access_group".to_string(), serde_json::to_value(ag)?);
        args.insert("init_id".to_string(), serde_json::to_value(init_id)?);
        args.insert(
            "init_type".to_string(),
            serde_json::to_value(init_type as i32)?,
        );
        Ok(serde_json::from_value(self.tp
            .invoke("access_group_initiator_add", Some(args))?)?)
    }

    /// Delete an initiator from an access group.
    ///
    /// # Errors
    ///
    ///  * [`LsmError::LastInitInAccessGroup`][1]: Specified initiator is the
    ///  last initiator of access group. Use
    ///  [`Client::access_group_delete()`][2] instead.
    ///
    /// [1]: enum.LsmError.html#variant.LastInitInAccessGroup
    /// [2]: #method.access_group_delete
    pub fn access_group_init_del(
        &mut self,
        ag: &AccessGroup,
        init_id: &str,
        init_type: InitiatorType,
    ) -> Result<(AccessGroup)> {
        verify_init_id_str(init_id, init_type)?;
        let mut args = Map::new();
        args.insert("access_group".to_string(), serde_json::to_value(ag)?);
        args.insert("init_id".to_string(), serde_json::to_value(init_id)?);
        args.insert(
            "init_type".to_string(),
            serde_json::to_value(init_type as i32)?,
        );
        Ok(serde_json::from_value(self.tp
            .invoke("access_group_initiator_delete", Some(args))?)?)
    }

    /// Query volumes that the specified access group has access to.
    pub fn vols_masked_to_ag(
        &mut self,
        ag: &AccessGroup,
    ) -> Result<Vec<Volume>> {
        let mut args = Map::new();
        args.insert("access_group".to_string(), serde_json::to_value(ag)?);
        Ok(serde_json::from_value(self.tp.invoke(
            "volumes_accessible_by_access_group",
            Some(args),
        )?)?)
    }

    /// Retrieves the access groups that have access to the specified volume.
    pub fn ags_granted_to_vol(
        &mut self,
        vol: &Volume,
    ) -> Result<Vec<AccessGroup>> {
        let mut args = Map::new();
        args.insert("volume".to_string(), serde_json::to_value(vol)?);
        Ok(serde_json::from_value(self.tp
            .invoke("access_groups_granted_to_volume", Some(args))?)?)
    }

    /// Check whether volume has child dependencies.
    pub fn vol_has_child_dep(&mut self, vol: &Volume) -> Result<bool> {
        let mut args = Map::new();
        args.insert("volume".to_string(), serde_json::to_value(vol)?);
        Ok(serde_json::from_value(self.tp
            .invoke("volume_child_dependency", Some(args))?)?)
    }

    /// Delete all child dependencies of the specified volume.
    ///
    /// Instruct storage system to remove all child dependencies of the
    /// specified volume by duplicating the required storage before breaking
    /// replication relationship. This function might take a long time(days or
    /// even weeks), you might want to invoke it in a thread.
    pub fn vol_child_dep_rm(&mut self, vol: &Volume) -> Result<()> {
        let mut args = Map::new();
        args.insert("volume".to_string(), serde_json::to_value(vol)?);
        let ret = self.tp.invoke("volume_child_dependency_rm", Some(args))?;
        self.wait_if_async(&ret)
    }

    /// Create a new file system.
    ///
    ///  * `pool` -- The pool where new file system should allocated from.
    ///  * `name` -- The name of new file system. It might be altered or
    ///    ignored.
    ///  * `size_bytes` -- Size in bytes of new file system. You may use
    ///    function [`size_human_2_size_bytes()`][1] to convert string like
    ///    '1.1 GiB' to integer size bytes.
    ///
    /// [1]: fn.size_human_2_size_bytes.html
    pub fn fs_create(
        &mut self,
        pool: &Pool,
        name: &str,
        size_bytes: u64,
    ) -> Result<FileSystem> {
        let mut args = Map::new();
        args.insert(
            "size_bytes".to_string(),
            serde_json::to_value(size_bytes)?,
        );
        args.insert("name".to_string(), serde_json::to_value(name)?);
        args.insert("pool".to_string(), serde_json::to_value(pool)?);

        let ret = self.tp.invoke("fs_create", Some(args))?;
        self.get_fs_from_async(&ret)
    }

    /// Resize of file system.
    pub fn fs_resize(
        &mut self,
        fs: &FileSystem,
        new_size_bytes: u64,
    ) -> Result<FileSystem> {
        let mut args = Map::new();
        args.insert("fs".to_string(), serde_json::to_value(fs)?);
        args.insert(
            "new_size_bytes".to_string(),
            serde_json::to_value(new_size_bytes)?,
        );
        let ret = self.tp.invoke("fs_resize", Some(args))?;
        self.get_fs_from_async(&ret)
    }

    /// Delete a file system.
    ///
    /// When file system has snapshot attached, all its snapshot will be
    /// deleted also. When file system is exported, all its exports will be
    /// deleted also. If specified file system is has child dependency, it
    /// cannot be deleted, please use [`Client::fs_has_child_dep()`][1] and
    /// [`Client::fs_child_dep_rm()`][2].
    ///
    /// [1]: #method.fs_has_child_dep()
    /// [2]: #method.fs_child_dep_rm()
    pub fn fs_delete(&mut self, fs: &FileSystem) -> Result<()> {
        let mut args = Map::new();
        args.insert("fs".to_string(), serde_json::to_value(fs)?);
        let ret = self.tp.invoke("fs_delete", Some(args))?;
        self.wait_if_async(&ret)
    }

    /// Clones an existing file system
    ///
    /// Create a point in time read writeable space efficient copy of specified
    /// file system, also know as read writeable snapshot. The new file system
    /// will reside in the same pool of specified file system.
    ///
    /// Optionally, new file system could be based on a snapshot specified by
    /// `snapshot` argument.
    pub fn fs_clone(
        &mut self,
        src_fs: &FileSystem,
        dst_fs_name: &str,
        snapshot: Option<&FileSystemSnapShot>,
    ) -> Result<FileSystem> {
        let mut args = Map::new();
        args.insert("src_fs".to_string(), serde_json::to_value(src_fs)?);
        args.insert(
            "dest_fs_name".to_string(),
            serde_json::to_value(dst_fs_name)?,
        );
        args.insert("snapshot".to_string(), serde_json::to_value(snapshot)?);

        let ret = self.tp.invoke("fs_clone", Some(args))?;
        self.get_fs_from_async(&ret)
    }

    /// Clones a file on a file system.
    ///
    /// Optionally, file contents could be based on a snapshot specified by
    /// `snapshot` argument.
    pub fn fs_file_clone(
        &mut self,
        fs: &FileSystem,
        src_file_name: &str,
        dst_file_name: &str,
        snapshot: Option<&FileSystemSnapShot>,
    ) -> Result<()> {
        let mut args = Map::new();
        args.insert("fs".to_string(), serde_json::to_value(fs)?);
        args.insert(
            "src_file_name".to_string(),
            serde_json::to_value(src_file_name)?,
        );
        args.insert(
            "dest_file_name".to_string(),
            serde_json::to_value(dst_file_name)?,
        );
        args.insert("snapshot".to_string(), serde_json::to_value(snapshot)?);

        let ret = self.tp.invoke("fs_file_clone", Some(args))?;
        self.wait_if_async(&ret)
    }

    /// Get a list of snapshots of specified file system.
    pub fn fs_snapshots(
        &mut self,
        fs: &FileSystem,
    ) -> Result<Vec<FileSystemSnapShot>> {
        let mut args = Map::new();
        args.insert("fs".to_string(), serde_json::to_value(fs)?);
        Ok(serde_json::from_value(self.tp
            .invoke("fs_snapshots", Some(args))?)?)
    }

    /// Create a file system snapshot.
    pub fn fs_snapshot_create(
        &mut self,
        fs: &FileSystem,
        name: &str,
    ) -> Result<FileSystemSnapShot> {
        let mut args = Map::new();
        args.insert("fs".to_string(), serde_json::to_value(fs)?);
        args.insert("snapshot_name".to_string(), serde_json::to_value(name)?);
        let ret = self.tp.invoke("fs_snapshot_create", Some(args))?;
        self.get_fs_snap_from_asyn(&ret)
    }

    /// Delete a file system snapshot.
    pub fn fs_snapshot_delete(
        &mut self,
        fs: &FileSystem,
        snapshot: &FileSystemSnapShot,
    ) -> Result<()> {
        let mut args = Map::new();
        args.insert("fs".to_string(), serde_json::to_value(fs)?);
        args.insert("snapshot".to_string(), serde_json::to_value(snapshot)?);
        let ret = self.tp.invoke("fs_snapshot_delete", Some(args))?;
        self.wait_if_async(&ret)
    }

    /// Restore a file system based on specified snapshot.
    ///
    ///  * `fs` -- File system to restore.
    ///  * `snapshot` -- Snapshot to use.
    ///  * `all_file` -- `true` for restore all files. `false` for restore
    ///    specified files only.
    ///  * `files` -- Only restored specified files. Ignored if `all_file` is
    ///    `true`.
    ///  * `restore_files` -- If not `None`, rename restored files to defined
    ///    file paths and names
    pub fn fs_snapshot_restore(
        &mut self,
        fs: &FileSystem,
        snapshot: &FileSystemSnapShot,
        all_file: bool,
        files: Option<&[&str]>,
        restore_files: Option<&[&str]>,
    ) -> Result<()> {
        let mut args = Map::new();
        if all_file {
            let files: [&str; 0] = [];
            let restore_files: [&str; 0] = [];
            args.insert("files".to_string(), serde_json::to_value(files)?);
            args.insert(
                "restore_files".to_string(),
                serde_json::to_value(restore_files)?,
            );
        } else {
            let files = files.unwrap_or(&[]);
            if files.is_empty() {
                return Err(LsmError::InvalidArgument(
                    "Invalid argument: `all_file` is false while \
                     `files` is empty"
                        .to_string(),
                ));
            }
            let restore_files = restore_files.unwrap_or(&[]);
            if !restore_files.is_empty() && files.len() != restore_files.len() {
                return Err(LsmError::InvalidArgument(
                    "Invalid argument: `all_file` and `restore_files` have \
                     different length"
                        .to_string(),
                ));
            }
            args.insert("files".to_string(), serde_json::to_value(files)?);
            args.insert(
                "restore_files".to_string(),
                serde_json::to_value(restore_files)?,
            );
        }

        args.insert("fs".to_string(), serde_json::to_value(fs)?);
        args.insert("snapshot".to_string(), serde_json::to_value(snapshot)?);
        args.insert("all_files".to_string(), serde_json::to_value(all_file)?);
        let ret = self.tp.invoke("fs_snapshot_restore", Some(args))?;
        self.wait_if_async(&ret)
    }

    /// Checks whether file system has a child dependency.
    pub fn fs_has_child_dep(
        &mut self,
        fs: &FileSystem,
        files: Option<Vec<&str>>,
    ) -> Result<bool> {
        let mut args = Map::new();
        args.insert("fs".to_string(), serde_json::to_value(fs)?);
        let files: Vec<&str> = files.unwrap_or_default();
        args.insert("files".to_string(), serde_json::to_value(files)?);
        Ok(serde_json::from_value(self.tp
            .invoke("fs_child_dependency", Some(args))?)?)
    }

    /// Delete all child dependencies of the specified file system.
    ///
    /// Instruct storage system to remove all child dependencies of the
    /// specified file system by duplicating the required storage before
    /// breaking replication relationship. This function might take a long
    /// time(days or even weeks), you might want to invoke it in a thread.
    pub fn fs_child_dep_rm(
        &mut self,
        fs: &FileSystem,
        files: Option<Vec<&str>>,
    ) -> Result<()> {
        let mut args = Map::new();
        args.insert("fs".to_string(), serde_json::to_value(fs)?);
        let files: Vec<&str> = files.unwrap_or_default();
        args.insert("files".to_string(), serde_json::to_value(files)?);
        let ret = self.tp.invoke("fs_child_dependency_rm", Some(args))?;
        self.wait_if_async(&ret)
    }

    /// Get supported NFS client authentication types.
    pub fn nfs_exp_auth_type_list(&mut self) -> Result<Vec<String>> {
        Ok(serde_json::from_value(self.tp
            .invoke("export_auth", None)?)?)
    }

    /// Create or modify an NFS export.
    ///
    /// * `fs` -- File system to export.
    /// * `export_path` -- Export path. If already exists, will modify exist NFS
    ///   export. If `None`, will let storage system to generate one.
    /// * `access` -- NFS access details.
    /// * `auth_type` -- NFS client authentication type. Get from
    ///   [`Client::nfs_exp_auth_type_list()`][1].
    /// * `options` -- Extra NFS options.
    ///
    /// [1]: #method.nfs_exp_auth_type_list
    pub fn fs_export(
        &mut self,
        fs: &FileSystem,
        export_path: Option<&str>,
        access: &NfsAccess,
        auth_type: Option<&str>,
        options: Option<&str>,
    ) -> Result<NfsExport> {
        let root_list = access.root_list;
        let rw_list = access.rw_list;
        let ro_list = access.ro_list;

        if rw_list.is_empty() && ro_list.is_empty() {
            return Err(LsmError::InvalidArgument(
                "At least one host should exists in `rw_list` or `ro_list`"
                    .to_string(),
            ));
        }
        for host in root_list {
            if !rw_list.contains(host) && !ro_list.contains(host) {
                return Err(LsmError::InvalidArgument(format!(
                    "Host defined in `root_list` should be also \
                     defined in `rw_list` or `ro_list`: '{}'",
                    host
                )));
            }
        }
        for host in rw_list {
            if ro_list.contains(host) {
                return Err(LsmError::InvalidArgument(format!(
                    "Host should not both in `rw_list` \
                     and `ro_list`: '{}'",
                    host
                )));
            }
        }

        let mut args = Map::new();
        args.insert("fs_id".to_string(), serde_json::to_value(&fs.id)?);
        args.insert(
            "export_path".to_string(),
            serde_json::to_value(export_path)?,
        );
        args.insert("root_list".to_string(), serde_json::to_value(&root_list)?);
        args.insert("rw_list".to_string(), serde_json::to_value(&rw_list)?);
        args.insert("ro_list".to_string(), serde_json::to_value(&ro_list)?);

        let anon_uid = access.anon_uid.unwrap_or(NfsExport::ANON_UID_GID_NA);
        let anon_gid = access.anon_gid.unwrap_or(NfsExport::ANON_UID_GID_NA);
        args.insert("anon_uid".to_string(), serde_json::to_value(anon_uid)?);
        args.insert("anon_gid".to_string(), serde_json::to_value(anon_gid)?);
        args.insert("auth_type".to_string(), serde_json::to_value(auth_type)?);
        args.insert("options".to_string(), serde_json::to_value(options)?);
        Ok(serde_json::from_value(self.tp
            .invoke("export_fs", Some(args))?)?)
    }

    /// Unexport specified NFS exports.
    pub fn fs_unexport(&mut self, exp: &NfsExport) -> Result<()> {
        let mut args = Map::new();
        args.insert("export".to_string(), serde_json::to_value(exp)?);
        Ok(serde_json::from_value(self.tp
            .invoke("export_remove", Some(args))?)?)
    }

    /// Get volume RAID information.
    pub fn vol_raid_info(&mut self, vol: &Volume) -> Result<VolumeRaidInfo> {
        let mut args = Map::new();
        args.insert("volume".to_string(), serde_json::to_value(vol)?);
        let ret: Vec<i32> = serde_json::from_value(self.tp
            .invoke("volume_raid_info", Some(args))?)?;
        if ret.len() != 5 {
            return Err(LsmError::PluginBug(format!(
                "vol_raid_info() is expecting 5 i64 from plugin, \
                 but got '{:?}'",
                ret
            )));
        }
        Ok(VolumeRaidInfo {
            raid_type: From::from(ret[0]),
            strip_size: ret[1] as u32,
            disk_count: ret[2] as u32,
            min_io_size: ret[3] as u32,
            opt_io_size: ret[4] as u32,
        })
    }

    /// Get pool member information.
    pub fn pool_member_info(&mut self, pool: &Pool) -> Result<PoolMemberInfo> {
        let mut args = Map::new();
        args.insert("pool".to_string(), serde_json::to_value(pool)?);
        let ret = self.tp.invoke("pool_member_info", Some(args))?;
        let ret_array = ret.as_array().ok_or_plugin_bug(&ret)?;
        if ret_array.len() != 3 {
            return Err(LsmError::PluginBug(format!(
                "Plugin return unexpected data: {:?}",
                ret
            )));
        }
        let raid_type: i32 = serde_json::from_value(
            ret_array.get(0).ok_or_plugin_bug(&ret)?.clone(),
        )?;
        let raid_type: RaidType = From::from(raid_type);
        let member_type: u32 = serde_json::from_value(
            ret_array.get(1).ok_or_plugin_bug(&ret)?.clone(),
        )?;
        let member_ids: Vec<String> = serde_json::from_value(
            ret_array.get(2).ok_or_plugin_bug(&ret)?.clone(),
        )?;
        let mut members: Vec<PoolMember> = Vec::new();
        match member_type {
            POOL_MEMBER_TYPE_DISK => for disk in self.disks()? {
                if member_ids.contains(&disk.id) {
                    members.push(PoolMember::Disk(disk));
                }
            },
            POOL_MEMBER_TYPE_POOL => for pool in self.pools()? {
                if member_ids.contains(&pool.id) {
                    members.push(PoolMember::Pool(pool));
                }
            },
            _ => (),
        };
        Ok(PoolMemberInfo { raid_type, members })
    }

    /// Get system capability on creating RAIDed volume. For hardware RAID
    /// only.
    ///
    /// Returns supported RAID types and strip sizes.
    pub fn vol_raid_create_cap_get(
        &mut self,
        sys: &System,
    ) -> Result<(Vec<RaidType>, Vec<u32>)> {
        let mut args = Map::new();
        args.insert("system".to_string(), serde_json::to_value(sys)?);
        let ret = self.tp.invoke("volume_raid_create_cap_get", Some(args))?;
        let ret_array = ret.as_array().ok_or_plugin_bug(&ret)?;
        if ret_array.len() != 2 {
            return Err(LsmError::PluginBug(format!(
                "vol_raid_create_cap_get() is expecting array with \
                 2 members from plugin, but got '{:?}'",
                ret
            )));
        }
        let raid_types: Vec<i32> = serde_json::from_value(
            ret_array.get(0).ok_or_plugin_bug(&ret)?.clone(),
        )?;
        let strip_sizes: Vec<u32> = serde_json::from_value(
            ret_array.get(1).ok_or_plugin_bug(&ret)?.clone(),
        )?;
        let mut new_raid_types: Vec<RaidType> = Vec::new();
        for raid_type in raid_types {
            new_raid_types.push(From::from(raid_type));
        }
        Ok((new_raid_types, strip_sizes))
    }

    /// Create RAIDed volume directly from disks. Only for hardware RAID.
    pub fn vol_raid_create(
        &mut self,
        name: &str,
        raid_type: RaidType,
        disks: &[Disk],
        strip_size: Option<u32>,
    ) -> Result<Volume> {
        if disks.is_empty() {
            return Err(LsmError::InvalidArgument(
                "no disk included".to_string(),
            ));
        }

        if raid_type == RaidType::Raid1 && disks.len() != 2 {
            return Err(LsmError::InvalidArgument(
                "RAID 1 only allow 2 disks".to_string(),
            ));
        }

        if raid_type == RaidType::Raid5 && disks.len() < 3 {
            return Err(LsmError::InvalidArgument(
                "RAID 5 require 3 or more disks".to_string(),
            ));
        }

        if raid_type == RaidType::Raid6 && disks.len() < 4 {
            return Err(LsmError::InvalidArgument(
                "RAID 6 require 4 or more disks".to_string(),
            ));
        }

        if raid_type == RaidType::Raid10
            && (disks.len() % 2 != 0 || disks.len() < 4)
        {
            return Err(LsmError::InvalidArgument(
                "RAID 10 require even disks count and 4 or more disks"
                    .to_string(),
            ));
        }

        if raid_type == RaidType::Raid50
            && (disks.len() % 2 != 0 || disks.len() < 6)
        {
            return Err(LsmError::InvalidArgument(
                "RAID 50 require even disks count and 6 or more disks"
                    .to_string(),
            ));
        }

        if raid_type == RaidType::Raid60
            && (disks.len() % 2 != 0 || disks.len() < 8)
        {
            return Err(LsmError::InvalidArgument(
                "RAID 60 require even disks count and 8 or more disks"
                    .to_string(),
            ));
        }

        let mut args = Map::new();
        args.insert("name".to_string(), serde_json::to_value(name)?);
        args.insert(
            "raid_type".to_string(),
            serde_json::to_value(raid_type as i32)?,
        );
        args.insert("disks".to_string(), serde_json::to_value(disks)?);
        let strip_size = strip_size.unwrap_or(0u32);
        args.insert(
            "strip_size".to_string(),
            serde_json::to_value(strip_size)?,
        );
        Ok(serde_json::from_value(self.tp
            .invoke("volume_raid_create", Some(args))?)?)
    }

    /// Turn on the identification LED for the specified volume.
    ///
    /// All its member disks' identification LED will be turned on.
    pub fn vol_ident_led_on(&mut self, vol: &Volume) -> Result<()> {
        let mut args = Map::new();
        args.insert("volume".to_string(), serde_json::to_value(vol)?);
        self.tp.invoke("vol_ident_led_on", Some(args))?;
        Ok(())
    }

    /// Turn off the identification LED for the specified volume.
    ///
    /// All its member disks' identification LED will be turned off.
    pub fn vol_ident_led_off(&mut self, vol: &Volume) -> Result<()> {
        let mut args = Map::new();
        args.insert("volume".to_string(), serde_json::to_value(vol)?);
        self.tp.invoke("vol_ident_led_off", Some(args))?;
        Ok(())
    }

    /// Get cache information on specified volume.
    pub fn vol_cache_info(&mut self, vol: &Volume) -> Result<VolumeCacheInfo> {
        let mut args = Map::new();
        args.insert("volume".to_string(), serde_json::to_value(vol)?);
        let ret: Vec<u8> = serde_json::from_value(self.tp
            .invoke("volume_cache_info", Some(args))?)?;
        if ret.len() != 5 {
            return Err(LsmError::PluginBug(format!(
                "vol_cache_info() is expecting 5 u8 from plugin, \
                 but got '{:?}'",
                ret
            )));
        }
        Ok(VolumeCacheInfo {
            write_cache_setting: match ret[0] {
                WRITE_CACHE_POLICY_WRITE_BACK => CachePolicy::Enabled,
                WRITE_CACHE_POLICY_WRITE_THROUGH => CachePolicy::Disabled,
                WRITE_CACHE_POLICY_AUTO => CachePolicy::Auto,
                _ => CachePolicy::Unknown,
            },
            write_cache_status: match ret[1] {
                WRITE_CACHE_STATUS_WRITE_BACK => CachePolicy::Enabled,
                WRITE_CACHE_STATUS_WRITE_THROUGH => CachePolicy::Disabled,
                _ => CachePolicy::Unknown,
            },
            read_cache_setting: match ret[2] {
                READ_CACHE_POLICY_ENABLED => CachePolicy::Enabled,
                READ_CACHE_POLICY_DISABLED => CachePolicy::Disabled,
                _ => CachePolicy::Unknown,
            },
            read_cache_status: match ret[3] {
                READ_CACHE_STATUS_ENABLED => CachePolicy::Enabled,
                READ_CACHE_STATUS_DISABLED => CachePolicy::Disabled,
                _ => CachePolicy::Unknown,
            },
            physical_disk_cache_status: match ret[4] {
                PHYSICAL_DISK_CACHE_ENABLED => CachePolicy::Enabled,
                PHYSICAL_DISK_CACHE_DISABLED => CachePolicy::Disabled,
                PHYSICAL_DISK_CACHE_USE_DISK_SETTING => {
                    CachePolicy::UseDiskSetting
                }
                _ => CachePolicy::Unknown,
            },
        })
    }

    /// Set volume physical disk cache policy.
    pub fn vol_phy_disk_cache_set(
        &mut self,
        vol: &Volume,
        pdc: CachePolicy,
    ) -> Result<()> {
        let pdc: u8 = match pdc {
            CachePolicy::Enabled => PHYSICAL_DISK_CACHE_ENABLED,
            CachePolicy::Disabled => PHYSICAL_DISK_CACHE_DISABLED,
            CachePolicy::UseDiskSetting => PHYSICAL_DISK_CACHE_USE_DISK_SETTING,
            _ => {
                return Err(LsmError::InvalidArgument(format!(
                    "Invalid pdc argument {:?}",
                    pdc
                )))
            }
        };
        let mut args = Map::new();
        args.insert("volume".to_string(), serde_json::to_value(vol)?);
        args.insert("pdc".to_string(), serde_json::to_value(pdc)?);
        self.tp
            .invoke("volume_physical_disk_cache_update", Some(args))?;
        Ok(())
    }

    /// Set volume write cache policy.
    pub fn vol_write_cache_set(
        &mut self,
        vol: &Volume,
        wcp: CachePolicy,
    ) -> Result<()> {
        let wcp: u8 = match wcp {
            CachePolicy::Enabled => WRITE_CACHE_POLICY_WRITE_BACK,
            CachePolicy::Disabled => WRITE_CACHE_POLICY_WRITE_THROUGH,
            CachePolicy::Auto => WRITE_CACHE_POLICY_AUTO,
            _ => {
                return Err(LsmError::InvalidArgument(format!(
                    "Invalid wcp argument {:?}",
                    wcp
                )))
            }
        };
        let mut args = Map::new();
        args.insert("volume".to_string(), serde_json::to_value(vol)?);
        args.insert("wcp".to_string(), serde_json::to_value(wcp)?);
        self.tp
            .invoke("volume_write_cache_policy_update", Some(args))?;
        Ok(())
    }

    /// Set volume read cache policy.
    pub fn vol_read_cache_set(
        &mut self,
        vol: &Volume,
        rcp: CachePolicy,
    ) -> Result<()> {
        let rcp: u8 = match rcp {
            CachePolicy::Enabled => READ_CACHE_POLICY_ENABLED,
            CachePolicy::Disabled => READ_CACHE_POLICY_DISABLED,
            _ => {
                return Err(LsmError::InvalidArgument(format!(
                    "Invalid rcp argument {:?}",
                    rcp
                )))
            }
        };
        let mut args = Map::new();
        args.insert("volume".to_string(), serde_json::to_value(vol)?);
        args.insert("rcp".to_string(), serde_json::to_value(rcp)?);
        self.tp
            .invoke("volume_read_cache_policy_update", Some(args))?;
        Ok(())
    }
}
