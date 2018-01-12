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

use std::env;
use std::os::unix::net::UnixStream;
use std::str;
use std::io::prelude::{Read, Write};
use serde_json;
use serde_json::{Map, Number, Value};

use super::error::*;

const IPC_HDR_LEN: usize = 10; // length of u32 max string.
const IPC_JSON_ID: u8 = 100;
const IPC_BUFF_SIZE: usize = 8192;
static UDS_PATH_DEFAULT: &'static str = "/var/run/lsm/ipc";
static UDS_PATH_VAR_NAME: &'static str = "LSM_UDS_PATH";

pub(crate) struct TransPort {
    so: UnixStream,
}

impl TransPort {
    pub(crate) fn new(plugin_uds_path: &str) -> Result<TransPort> {
        let so = match UnixStream::connect(plugin_uds_path) {
            Ok(s) => s,
            Err(_) => {
                return Err(
                    // TODO(Gris Ge): need to tell DaemonNotRunning or
                    //                PluginNotExist
                    LsmError::DaemonNotRunning(format!(
                        "LibStorageMgmt daemon is not running for \
                         socket folder: '{}'",
                        plugin_uds_path
                    )),
                )
            }
        };
        Ok(TransPort { so })
    }

    fn send(&mut self, msg: &str) -> Result<()> {
        let msg =
            format!("{:0padding$}{}", msg.len(), msg, padding = IPC_HDR_LEN);

        self.so.write_all(msg.as_bytes())?;
        Ok(())
    }

    fn recv(&mut self) -> Result<String> {
        let mut msg_buff = [0u8; IPC_HDR_LEN];
        self.so.read_exact(&mut msg_buff)?;
        let msg_len = str::from_utf8(&msg_buff)?.parse::<usize>()?;
        let mut msg = Vec::with_capacity(msg_len);
        let mut got: usize = 0;
        let mut msg_buff = [0u8; IPC_BUFF_SIZE];
        while got < msg_len {
            let cur_got = self.so.read(&mut msg_buff)?;
            msg.extend_from_slice(&msg_buff[0..cur_got]);
            got += cur_got;
        }
        let msg = String::from_utf8(msg)?;
        Ok(msg)
    }

    pub(crate) fn invoke(
        &mut self,
        cmd: &str,
        args: Option<Map<String, Value>>,
    ) -> Result<Value> {
        let mut msg = Map::new();
        msg.insert("method".to_string(), Value::String(cmd.to_string()));
        msg.insert("id".to_string(), Value::Number(Number::from(IPC_JSON_ID)));
        let mut args = args.unwrap_or_default();
        args.insert("flags".to_string(), Value::Number(Number::from(0u8)));
        msg.insert("params".to_string(), Value::Object(args));
        let msg = &serde_json::to_string(&msg)?;
        self.send(msg)?;
        let val: Value = serde_json::from_str(&self.recv()?)?;
        let obj = match val.as_object() {
            Some(o) => o,
            None => {
                return Err(LsmError::PluginBug(format!(
                    "Invalid reply from socket: {}",
                    msg
                )))
            }
        };
        if let Some(e) = obj.get("error") {
            let lsm_err_ipc: LsmErrorIpc = serde_json::from_value(e.clone())?;
            return Err(From::from(lsm_err_ipc));
        };
        let result = match obj.get("result") {
            Some(r) => r,
            None => {
                return Err(LsmError::PluginBug(format!(
                    "Got no result from socket: {}",
                    msg
                )))
            }
        };
        Ok(result.clone())
    }
}

impl Drop for TransPort {
    fn drop(&mut self) {
        if self.invoke("plugin_unregister", None).is_ok() {
            ()
        }
    }
}

pub(crate) fn uds_path() -> String {
    match env::var(UDS_PATH_VAR_NAME) {
        Ok(v) => v.to_string(),
        Err(_) => UDS_PATH_DEFAULT.to_string(),
    }
}
