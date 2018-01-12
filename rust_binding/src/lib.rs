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

//! # `LibStorageMgmt`
//!
//! `LibStorageMgmt` provides a set of API for programmatically manage their
//! storage hardware in a vendor neutral way supporting these actions:
//!
//!  * List storage pools, volumes, access groups, or file systems.
//!
//!  * Create and delete volumes, access groups, file systems, or NFS exports.
//!
//!  * Grant and remove access to volumes, access groups, or initiators.
//!
//!  * Replicate volumes with snapshots, clones, and copies.
//!
//!  * Create and delete access groups and edit members of a group.
//!
//!  * List Linux local SCSI/ATA/NVMe disks.
//!
//!  * Control IDENT/FAULT LED of local disk via SES(SCSI Enclosure Service).
//!
//! To use `LibStorageMgmt`, you need:
//!
//!  * Start the libstoragemgmt daemon(`lsmd`)
//!
//!  * Chose a URI after reading [`LibStorageMgmt` user guide][1]
//!
//!  * Make a connection to plugin via [`lsm::Client`][2].
//!
//!  * Check required [`capacity`][3] is supported.
//!
//!  * Invoke required method of [`lsm::Client`][2].
//!
//! # Example code using simulator plugin
//!
//! ```rust
//! extern crate lsm;
//! use lsm::{Client, LsmError};
//! fn main() {
//!     let mut c: Client = match Client::new("sim://", None, None) {
//!         Ok(i) => i,
//!         Err(e) => {
//!             match e {
//!                 // Error handling goes here
//!                 LsmError::DaemonNotRunning(_) =>
//!                     panic!("Please start the libstoragemgmt daemon"),
//!                 _ => panic!(e)
//!             };
//!         },
//!     };
//!     let syss = match c.systems() {
//!         Ok(i) => i,
//!         Err(e) => panic!(e)         // Please use error handling as above.
//!     };
//!     for s in syss {
//!         let cap = match c.capabilities(&s) {
//!             Ok(i) => i,
//!             Err(e) => panic!(e)     // Please use error handling as above.
//!         };
//!         if cap.is_supported(lsm::Capability::Volumes) {
//!             let vols = match c.volumes() {
//!                 Ok(i) => i,
//!                 Err(e) => panic!(e) // Please use error handling as above.
//!             };
//!             for vol in vols {
//!                 println!("Got volume: {} {}", vol.name, vol.id);
//!             }
//!         }
//!     }
//! }
//! ```
//!
//! [1]: https://libstorage.github.io/libstoragemgmt-doc/doc/user_guide.html
//! [2]: struct.Client.html
//! [3]: struct.Capacities.html

extern crate serde;
#[macro_use]
extern crate serde_derive;
extern crate serde_json;
extern crate url;
extern crate regex;

pub use self::error::LsmError;
pub use self::client::{Client, available_plugins, PluginInfo};
pub use self::data::*;
pub use self::misc::{size_bytes_2_size_human, size_human_2_size_bytes};

mod error;
mod client;
mod ipc;
mod data;
mod misc;
