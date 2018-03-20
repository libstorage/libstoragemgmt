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
use std::result;
use std::fmt;

#[derive(Debug)]
pub enum LsmError {
    LibBug(String),
    PluginBug(String),
    TimeOut(String),
    DaemonNotRunning(String),
    PermissionDenied(String),
    NameConflict(String),
    ExistsInitiator(String),
    InvalidArgument(String),
    NoStateChange(String),
    NetworkConRefused(String),
    NetworkHostDown(String),
    NetworkError(String),
    NoMemory(String),
    NoSupport(String),
    IsMasked(String),
    HasChildDependency(String),
    NotFoundAccessGroup(String),
    NotFoundFs(String),
    NotFoundJob(String),
    NotFoundPool(String),
    NotFoundFsSnapshot(String),
    NotFoundVolume(String),
    NotFoundNfsExport(String),
    NotFoundSystem(String),
    NotFoundDisk(String),
    NotLicensed(String),
    NoSupportOnlineChange(String),
    NoSupportOfflineChange(String),
    PluginAuthFailed(String),
    PluginIpcFail(String),
    PluginSocketPermission(String),
    PluginNotExist(String),
    NoEnoughSpace(String),
    TransportCommunication(String),
    TransportSerialization(String),
    TransportInvalidArg(String),
    LastInitInAccessGroup(String),
    UnSupportedSearchKey(String),
    EmptyAccessGroup(String),
    PoolNotReady(String),
    DiskNotFree(String),
}

impl ::std::error::Error for LsmError {
    fn description(&self) -> &str {
        match *self {
            LsmError::LibBug(_) => "Library bug",
            LsmError::PluginBug(_) => "Plugin bug",
            LsmError::TimeOut(_) => "Timeout",
            LsmError::DaemonNotRunning(_) => {
                "LibStoragemgmt daemon is not running"
            }
            LsmError::PermissionDenied(_) => "Permission denied",
            LsmError::NameConflict(_) => "Name conflict",
            LsmError::ExistsInitiator(_) => "Initiator exists and in use",
            LsmError::InvalidArgument(_) => "Invalid argument",
            LsmError::NoStateChange(_) => "No state change",
            LsmError::NetworkConRefused(_) => "Network connection refused",
            LsmError::NetworkHostDown(_) => "Network host down",
            LsmError::NetworkError(_) => "Network error",
            LsmError::NoMemory(_) => "Plugin ran out of memory",
            LsmError::NoSupport(_) => "Not supported",
            LsmError::IsMasked(_) => "Volume masked to access group",
            LsmError::HasChildDependency(_) => {
                "Volume or file system has child dependency"
            }
            LsmError::NotFoundAccessGroup(_) => "Access group not found",
            LsmError::NotFoundFs(_) => "File system not found",
            LsmError::NotFoundJob(_) => "Job not found",
            LsmError::NotFoundPool(_) => "Pool not found",
            LsmError::NotFoundFsSnapshot(_) => "File system snapshot not found",
            LsmError::NotFoundVolume(_) => "Volume not found",
            LsmError::NotFoundNfsExport(_) => "NFS export not found",
            LsmError::NotFoundSystem(_) => "System not found",
            LsmError::NotFoundDisk(_) => "Disk not found",
            LsmError::NotLicensed(_) => {
                "Specified feature is not licensed in storage system"
            }
            LsmError::NoSupportOnlineChange(_) => {
                "Specified action require item in offline mode"
            }
            LsmError::NoSupportOfflineChange(_) => {
                "Specified action require item in online mode"
            }
            LsmError::PluginAuthFailed(_) => "Authentication failed in plugin",
            LsmError::PluginIpcFail(_) => "IPC communication to plugin failed",
            LsmError::PluginSocketPermission(_) => {
                "Permission deny on IPC communication to plugin"
            }
            LsmError::PluginNotExist(_) => "Specified plugin does not exist",
            LsmError::NoEnoughSpace(_) => "No enough space",
            LsmError::TransportCommunication(_) => {
                "Error when communicating with plug-in"
            }
            LsmError::TransportSerialization(_) => {
                "Incorrect transport serialization"
            }
            LsmError::TransportInvalidArg(_) => "Invalid transport argument",
            LsmError::LastInitInAccessGroup(_) => {
                "Refused to remove the last initiator from access group"
            }
            LsmError::UnSupportedSearchKey(_) => {
                "Specified search key is not supported"
            }
            LsmError::EmptyAccessGroup(_) => {
                "Refused to mask volume to empty access group"
            }
            LsmError::PoolNotReady(_) => {
                "Pool is not ready for specified action"
            }
            LsmError::DiskNotFree(_) => "Disk is not free for specified action",
        }
    }
}

impl fmt::Display for LsmError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "{}",
            match *self {
                LsmError::LibBug(ref x)
                | LsmError::PluginBug(ref x)
                | LsmError::TimeOut(ref x)
                | LsmError::DaemonNotRunning(ref x)
                | LsmError::PermissionDenied(ref x)
                | LsmError::NameConflict(ref x)
                | LsmError::ExistsInitiator(ref x)
                | LsmError::InvalidArgument(ref x)
                | LsmError::NoStateChange(ref x)
                | LsmError::NetworkConRefused(ref x)
                | LsmError::NetworkHostDown(ref x)
                | LsmError::NetworkError(ref x)
                | LsmError::NoMemory(ref x)
                | LsmError::NoSupport(ref x)
                | LsmError::IsMasked(ref x)
                | LsmError::HasChildDependency(ref x)
                | LsmError::NotFoundAccessGroup(ref x)
                | LsmError::NotFoundFs(ref x)
                | LsmError::NotFoundJob(ref x)
                | LsmError::NotFoundPool(ref x)
                | LsmError::NotFoundFsSnapshot(ref x)
                | LsmError::NotFoundVolume(ref x)
                | LsmError::NotFoundNfsExport(ref x)
                | LsmError::NotFoundSystem(ref x)
                | LsmError::NotFoundDisk(ref x)
                | LsmError::NotLicensed(ref x)
                | LsmError::NoSupportOnlineChange(ref x)
                | LsmError::NoSupportOfflineChange(ref x)
                | LsmError::PluginAuthFailed(ref x)
                | LsmError::PluginIpcFail(ref x)
                | LsmError::PluginSocketPermission(ref x)
                | LsmError::PluginNotExist(ref x)
                | LsmError::NoEnoughSpace(ref x)
                | LsmError::TransportCommunication(ref x)
                | LsmError::TransportSerialization(ref x)
                | LsmError::TransportInvalidArg(ref x)
                | LsmError::LastInitInAccessGroup(ref x)
                | LsmError::UnSupportedSearchKey(ref x)
                | LsmError::EmptyAccessGroup(ref x)
                | LsmError::PoolNotReady(ref x)
                | LsmError::DiskNotFree(ref x) => x,
            }
        )
    }
}

pub type Result<T> = result::Result<T, LsmError>;

impl From<::std::string::FromUtf8Error> for LsmError {
    fn from(e: ::std::string::FromUtf8Error) -> Self {
        LsmError::TransportSerialization(format!(
            "Failed to convert IPC message to UTF-8 string: {}",
            e
        ))
    }
}

impl From<::std::num::ParseIntError> for LsmError {
    fn from(e: ::std::num::ParseIntError) -> Self {
        LsmError::TransportSerialization(format!(
            "Failed to convert IPC message to UTF-8 integer: {}",
            e
        ))
    }
}

impl From<::std::str::Utf8Error> for LsmError {
    fn from(e: ::std::str::Utf8Error) -> Self {
        LsmError::TransportSerialization(format!(
            "Failed to convert IPC message to UTF-8 string: {}",
            e
        ))
    }
}

impl From<::serde_json::Error> for LsmError {
    fn from(e: ::serde_json::Error) -> Self {
        LsmError::TransportSerialization(format!(
            "Failed to convert IPC message to libstoragemgmt \
             struct: {}",
            e
        ))
    }
}

impl From<::std::io::Error> for LsmError {
    fn from(e: ::std::io::Error) -> Self {
        LsmError::TransportCommunication(format!("{}", e))
    }
}

impl From<::regex::Error> for LsmError {
    fn from(e: ::regex::Error) -> Self {
        LsmError::LibBug(format!("Regex error: {}", e))
    }
}

//const ERROR_NUMBER_OK: i32 = 0; // will never used in IPC
const ERROR_NUMBER_LIB_BUG: i32 = 1;
const ERROR_NUMBER_PLUGIN_BUG: i32 = 2;
//const ERROR_NUMBER_JOB_STARTED: i32 = 7; // will never used in IPC
const ERROR_NUMBER_TIMEOUT: i32 = 11;
//const ERROR_NUMBER_DAEMON_NOT_RUNNING: i32 = 12; // will never used in IPC
//const ERROR_NUMBER_PERMISSION_DENIED: i32 = 13; // will never used in IPC
const ERROR_NUMBER_NAME_CONFLICT: i32 = 50;
const ERROR_NUMBER_EXISTS_INITIATOR: i32 = 52;
const ERROR_NUMBER_INVALID_ARGUMENT: i32 = 101;
const ERROR_NUMBER_NO_STATE_CHANGE: i32 = 125;
const ERROR_NUMBER_NETWORK_CONNREFUSED: i32 = 140;
const ERROR_NUMBER_NETWORK_HOSTDOWN: i32 = 141;
const ERROR_NUMBER_NETWORK_ERROR: i32 = 142;
const ERROR_NUMBER_NO_MEMORY: i32 = 152;
const ERROR_NUMBER_NO_SUPPORT: i32 = 153;
const ERROR_NUMBER_IS_MASKED: i32 = 160;
const ERROR_NUMBER_HAS_CHILD_DEPENDENCY: i32 = 161;
const ERROR_NUMBER_NOT_FOUND_ACCESS_GROUP: i32 = 200;
const ERROR_NUMBER_NOT_FOUND_FS: i32 = 201;
const ERROR_NUMBER_NOT_FOUND_JOB: i32 = 202;
const ERROR_NUMBER_NOT_FOUND_POOL: i32 = 203;
const ERROR_NUMBER_NOT_FOUND_FS_SS: i32 = 204;
const ERROR_NUMBER_NOT_FOUND_VOLUME: i32 = 205;
const ERROR_NUMBER_NOT_FOUND_NFS_EXPORT: i32 = 206;
const ERROR_NUMBER_NOT_FOUND_SYSTEM: i32 = 208;
const ERROR_NUMBER_NOT_FOUND_DISK: i32 = 209;
const ERROR_NUMBER_NOT_LICENSED: i32 = 226;
const ERROR_NUMBER_NO_SUPPORT_ONLINE_CHANGE: i32 = 250;
const ERROR_NUMBER_NO_SUPPORT_OFFLINE_CHANGE: i32 = 251;
const ERROR_NUMBER_PLUGIN_AUTH_FAILED: i32 = 300;
const ERROR_NUMBER_PLUGIN_IPC_FAIL: i32 = 301;
const ERROR_NUMBER_PLUGIN_SOCKET_PERMISSION: i32 = 307;
const ERROR_NUMBER_PLUGIN_NOT_EXIST: i32 = 311;
const ERROR_NUMBER_NOT_ENOUGH_SPACE: i32 = 350;
const ERROR_NUMBER_TRANSPORT_COMMUNICATION: i32 = 400;
const ERROR_NUMBER_TRANSPORT_SERIALIZATION: i32 = 401;
const ERROR_NUMBER_TRANSPORT_INVALID_ARG: i32 = 402;
const ERROR_NUMBER_LAST_INIT_IN_ACCESS_GROUP: i32 = 502;
const ERROR_NUMBER_UNSUPPORTED_SEARCH_KEY: i32 = 510;
const ERROR_NUMBER_EMPTY_ACCESS_GROUP: i32 = 511;
const ERROR_NUMBER_POOL_NOT_READY: i32 = 512;
const ERROR_NUMBER_DISK_NOT_FREE: i32 = 513;

#[derive(Deserialize, Debug)]
pub(crate) struct LsmErrorIpc {
    pub(crate) code: i32,
    pub(crate) message: String,
    pub(crate) data: Option<String>,
}

impl From<LsmErrorIpc> for LsmError {
    fn from(e: LsmErrorIpc) -> Self {
        match e.code {
            ERROR_NUMBER_LIB_BUG => LsmError::LibBug(format!("{}", e.message)),
            ERROR_NUMBER_PLUGIN_BUG => {
                LsmError::PluginBug(format!("{}", e.message))
            }
            ERROR_NUMBER_TIMEOUT => LsmError::TimeOut(format!("{}", e.message)),
            ERROR_NUMBER_NAME_CONFLICT => {
                LsmError::NameConflict(format!("{}", e.message))
            }
            ERROR_NUMBER_EXISTS_INITIATOR => {
                LsmError::ExistsInitiator(format!("{}", e.message))
            }
            ERROR_NUMBER_INVALID_ARGUMENT => {
                LsmError::InvalidArgument(format!("{}", e.message))
            }
            ERROR_NUMBER_NO_STATE_CHANGE => {
                LsmError::NoStateChange(format!("{}", e.message))
            }
            ERROR_NUMBER_NETWORK_CONNREFUSED => {
                LsmError::NetworkConRefused(format!("{}", e.message))
            }
            ERROR_NUMBER_NETWORK_HOSTDOWN => {
                LsmError::NetworkHostDown(format!("{}", e.message))
            }
            ERROR_NUMBER_NETWORK_ERROR => {
                LsmError::NetworkError(format!("{}", e.message))
            }
            ERROR_NUMBER_NO_MEMORY => {
                LsmError::NoMemory(format!("{}", e.message))
            }
            ERROR_NUMBER_NO_SUPPORT => {
                LsmError::NoSupport(format!("{}", e.message))
            }
            ERROR_NUMBER_IS_MASKED => {
                LsmError::IsMasked(format!("{}", e.message))
            }
            ERROR_NUMBER_HAS_CHILD_DEPENDENCY => {
                LsmError::HasChildDependency(format!("{}", e.message))
            }
            ERROR_NUMBER_NOT_FOUND_ACCESS_GROUP => {
                LsmError::NotFoundAccessGroup(format!("{}", e.message))
            }
            ERROR_NUMBER_NOT_FOUND_FS => {
                LsmError::NotFoundFs(format!("{}", e.message))
            }
            ERROR_NUMBER_NOT_FOUND_JOB => {
                LsmError::NotFoundJob(format!("{}", e.message))
            }
            ERROR_NUMBER_NOT_FOUND_POOL => {
                LsmError::NotFoundPool(format!("{}", e.message))
            }
            ERROR_NUMBER_NOT_FOUND_FS_SS => {
                LsmError::NotFoundFsSnapshot(format!("{}", e.message))
            }
            ERROR_NUMBER_NOT_FOUND_VOLUME => {
                LsmError::NotFoundVolume(format!("{}", e.message))
            }
            ERROR_NUMBER_NOT_FOUND_NFS_EXPORT => {
                LsmError::NotFoundNfsExport(format!("{}", e.message))
            }
            ERROR_NUMBER_NOT_FOUND_SYSTEM => {
                LsmError::NotFoundSystem(format!("{}", e.message))
            }
            ERROR_NUMBER_NOT_FOUND_DISK => {
                LsmError::NotFoundDisk(format!("{}", e.message))
            }
            ERROR_NUMBER_NOT_LICENSED => {
                LsmError::NotLicensed(format!("{}", e.message))
            }
            ERROR_NUMBER_NO_SUPPORT_ONLINE_CHANGE => {
                LsmError::NoSupportOnlineChange(format!("{}", e.message))
            }
            ERROR_NUMBER_NO_SUPPORT_OFFLINE_CHANGE => {
                LsmError::NoSupportOfflineChange(format!("{}", e.message))
            }
            ERROR_NUMBER_PLUGIN_AUTH_FAILED => {
                LsmError::PluginAuthFailed(format!("{}", e.message))
            }
            ERROR_NUMBER_PLUGIN_IPC_FAIL => {
                LsmError::PluginIpcFail(format!("{}", e.message))
            }
            ERROR_NUMBER_PLUGIN_SOCKET_PERMISSION => {
                LsmError::PluginSocketPermission(format!("{}", e.message))
            }
            ERROR_NUMBER_PLUGIN_NOT_EXIST => {
                LsmError::PluginNotExist(format!("{}", e.message))
            }
            ERROR_NUMBER_NOT_ENOUGH_SPACE => {
                LsmError::NoEnoughSpace(format!("{}", e.message))
            }
            ERROR_NUMBER_TRANSPORT_COMMUNICATION => {
                LsmError::TransportCommunication(format!("{}", e.message))
            }
            ERROR_NUMBER_TRANSPORT_SERIALIZATION => {
                LsmError::TransportSerialization(format!("{}", e.message))
            }
            ERROR_NUMBER_TRANSPORT_INVALID_ARG => {
                LsmError::TransportInvalidArg(format!("{}", e.message))
            }
            ERROR_NUMBER_LAST_INIT_IN_ACCESS_GROUP => {
                LsmError::LastInitInAccessGroup(format!("{}", e.message))
            }
            ERROR_NUMBER_UNSUPPORTED_SEARCH_KEY => {
                LsmError::UnSupportedSearchKey(format!("{}", e.message))
            }
            ERROR_NUMBER_EMPTY_ACCESS_GROUP => {
                LsmError::EmptyAccessGroup(format!("{}", e.message))
            }
            ERROR_NUMBER_POOL_NOT_READY => {
                LsmError::PoolNotReady(format!("{}", e.message))
            }
            ERROR_NUMBER_DISK_NOT_FREE => {
                LsmError::DiskNotFree(format!("{}", e.message))
            }
            _ => LsmError::LibBug(format!("Invalid error: {:?}", e)),
        }
    }
}
