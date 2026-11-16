// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
//! Presents a keyboard through the board on PORT and presses Left Shift once.
//!
//! The board's USB port is plugged into the target, which sees a new keyboard and a Shift key press
//! and release. The values are those of the `Default` impls of `mimusb-sys` (the ones the tests use, within
//! docs/LIMITS.md); `UsbIdentity::for_testing` is the vendor and product ID pid.codes sets aside for
//! in-house testing: a device you hand out needs IDs of its own. `mimusb-sys`'s `keyboard_raw`
//! example is the same program with every call and value spelled out.

use mimusb::values::{self, UsbIdentity};
use mimusb::Board;
use mimusb_sys as sys;
use std::ffi::CString;
use std::{thread, time::Duration};

const LEFT_SHIFT: u16 = 0xE1; // HID Keyboard page usage

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 3 {
        eprintln!("usage: keyboard_owned PORT BAUD_RATE");
        std::process::exit(2);
    }
    let port = CString::new(args[1].as_str()).expect("a port name has no NUL");
    let baud_rate: u32 = args[2].parse().expect("BAUD_RATE is a number");
    let config = values::device_config(baud_rate, UsbIdentity::for_testing("Rust keyboard example"));
    let board = Board::open(&port, config).unwrap_or_else(|error| {
        eprintln!("{error:?}");
        std::process::exit(1);
    });
    let keyboard = board.keyboard(&sys::mimusb_keyboard_options::default()).expect("keyboard");
    board.wait_ready(Duration::from_secs(5)).expect("the target mounted the board");
    for down in [true, false] {
        keyboard.key(LEFT_SHIFT, down).expect("key");
        keyboard.submit_blocking(500).expect("submit");
        thread::sleep(Duration::from_millis(50));
    }
}
