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

use regex::Regex;

use super::error::*;
use super::data::InitiatorType;

struct SizeUnit<'a> {
    unit: &'a str,
    bytes: u64,
}

const SIZE_CONVS: [SizeUnit<'static>; 6] = [
    SizeUnit {
        unit: "EiB",
        bytes: 1u64 << 60,
    },
    SizeUnit {
        unit: "PiB",
        bytes: 1u64 << 50,
    },
    SizeUnit {
        unit: "TiB",
        bytes: 1u64 << 40,
    },
    SizeUnit {
        unit: "GiB",
        bytes: 1u64 << 30,
    },
    SizeUnit {
        unit: "MiB",
        bytes: 1u64 << 20,
    },
    SizeUnit {
        unit: "KiB",
        bytes: 1u64 << 10,
    },
];

const EXTRA_SIZE_CONVS: [SizeUnit<'static>; 7] = [
    SizeUnit {
        unit: "B",
        bytes: 1u64,
    },
    SizeUnit {
        unit: "KB",
        bytes: 1_000u64,
    },
    SizeUnit {
        unit: "MB",
        bytes: 1_000_000u64,
    },
    SizeUnit {
        unit: "GB",
        bytes: 1_000_000_000u64,
    },
    SizeUnit {
        unit: "TB",
        bytes: 1_000_000_000_000u64,
    },
    SizeUnit {
        unit: "PB",
        bytes: 1_000_000_000_000_000u64,
    },
    SizeUnit {
        unit: "EB",
        bytes: 1_000_000_000_000_000_000u64,
    },
];

/// Convert human readable size string into integer size in bytes.
///
/// Following [rules of IEC binary prefixes on size][1].
/// Supported size string formats:
///
///  * `1.9KiB` gets `(1024 * 1.9) as u64`.
///  * `1 KiB` gets `1 * (1 << 10)`.
///  * `1B` gets  `1u64`.
///  * `2K` is the same as `2KiB`.
///  * `2k` is the same as `2KiB`.
///  * `2KB` gets `2 * 1000`.
///
/// Return 0 if error.
///
/// [1]: https://en.wikipedia.org/wiki/Gibibyte
pub fn size_human_2_size_bytes(s: &str) -> u64 {
    let regex_size_human = match Regex::new(
        r"(?x)
        ^
        ([0-9\.]+)          # 1: number
        [\ \t]*             # might have space between number and unit
        ([a-zA-Z]*)         # 2: units
        $
        ",
    ) {
        Ok(r) => r,
        Err(_) => return 0,
    };

    let cap = match regex_size_human.captures(s) {
        Some(c) => c,
        None => return 0,
    };
    let number = match cap.get(1) {
        Some(n) => n.as_str().parse::<f64>().unwrap_or(0f64),
        None => return 0,
    };
    let unit = match cap.get(2) {
        Some(u) => {
            let tmp = u.as_str().to_uppercase();
            println!("tmp: {}", tmp);
            if ! tmp.ends_with('B') {
                format!("{}IB", tmp)
            } else {
                tmp
            }
        }
        None => return 0,
    };
    for size_conv in &SIZE_CONVS {
        if size_conv.unit.to_uppercase() == unit {
            return (size_conv.bytes as f64 * number) as u64;
        }
    }
    for size_conv in &EXTRA_SIZE_CONVS {
        if size_conv.unit == unit {
            return (size_conv.bytes as f64 * number) as u64;
        }
    }
    0
}

pub fn size_bytes_2_size_human(i: u64) -> String {
    let mut unit = "B";
    let mut num: f64 = 0f64;
    for size_conv in &SIZE_CONVS {
        if i >= size_conv.bytes {
            num = (i as f64) / (size_conv.bytes as f64);
            unit = size_conv.unit;
            break;
        }
    }
    if num == 0f64 {
        num = i as f64;
    }
    format!("{:.2}{}", num, unit)
}

pub(crate) fn verify_init_id_str(
    init_id: &str,
    init_type: InitiatorType,
) -> Result<()> {
    let valid: bool = match init_type {
        InitiatorType::Wwpn => {
            let regex_wwpn = Regex::new(
                r"(?x)
                ^(?:0x|0X)?(?:[0-9A-Fa-f]{2})
                (?:(?:[\.:\-])?[0-9A-Fa-f]{2}){7}$
                ",
            )?;
            regex_wwpn.is_match(init_id)
        }
        InitiatorType::IscsiIqn => {
            init_id.starts_with("iqn") || init_id.starts_with("eui")
                || init_id.starts_with("naa")
        }
        _ => {
            return Err(LsmError::InvalidArgument(format!(
                "Invalid init_type {}, should be \
                 InitiatorType::Wwpn or InitiatorType::IscsiIqn.",
                init_type as i32
            )))
        }
    };
    if valid {
        Ok(())
    } else {
        Err(LsmError::InvalidArgument(format!(
            "Invalid initiator ID string '{}'",
            init_id
        )))
    }
}
