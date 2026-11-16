// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
//! `Board` against the simulated board (tests/sim/sim_server.c): the real firmware core on a
//! pseudo-terminal, no hardware. MIMUSB_SIM_SERVER names the built mimusb_sim_server.

use mimusb::values::UsbIdentity;
use mimusb::{values, Board};
use std::ffi::CString;
use std::io::{BufRead, BufReader};
use std::process::{Command, Stdio};
use std::time::Duration;

#[test]
fn board_opens_nodes_sends_and_closes() {
    let server = std::env::var("MIMUSB_SIM_SERVER").expect("MIMUSB_SIM_SERVER is not set");
    let mut child = Command::new(server).stdin(Stdio::piped()).stdout(Stdio::piped()).spawn().expect("start the simulated board");
    let mut port = String::new();
    BufReader::new(child.stdout.take().unwrap()).read_line(&mut port).unwrap();
    let port = CString::new(port.trim()).unwrap();

    {
        let config = values::device_config(115200, UsbIdentity::for_testing("Rust board test"));
        let board = Board::open(&port, config).expect("board");
        let keyboard = board.keyboard(&values::keyboard_options()).expect("keyboard");
        let mouse = board.mouse(&values::mouse_options()).expect("mouse");
        // SAFETY: the values options point at statics.
        let gamepad = unsafe { board.gamepad(&values::gamepad_options()) }.expect("gamepad");
        board.wait_ready(Duration::from_secs(5)).expect("ready");
        keyboard.key(0xE1, true).unwrap();
        keyboard.submit_blocking(500).unwrap();
        keyboard.key(0xE1, false).unwrap();
        keyboard.submit_blocking(500).unwrap();
        mouse.move_by(3, -3).unwrap();
        mouse.submit_blocking(500).unwrap();
        gamepad.axis(0, 100).unwrap();
        gamepad.submit_blocking(500).unwrap();
        // An options structure the library refuses comes back as its error.
        let mut bad = values::keyboard_options();
        bad.usage_minimum = 0;
        assert!(board.keyboard(&bad).is_err());
    } // the Board is dropped: the Device closes with its Nodes

    drop(child.stdin.take()); // the simulated board exits when its stdin closes
    child.wait().unwrap();
}
