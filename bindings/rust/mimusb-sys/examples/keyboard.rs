// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
//! Presents a keyboard through the board on PORT and presses Left Shift once.
//!
//! The board's USB port is plugged into the target, which sees a new keyboard and a Shift key
//! press and release. Every value below is the program's own choice; none is a default of the
//! library (the limits they sit within are in docs/LIMITS.md). The vendor and product ID are the
//! pair pid.codes sets aside for in-house testing: a device you hand out needs IDs of its own.

use core::ptr;
use mimusb_sys::*;
use std::ffi::{CStr, CString};
use std::{thread, time::Duration};

const LEFT_SHIFT: u16 = 0xE1; // HID Keyboard page usage

/// A zeroed structure with only struct_size set, as the C `*_init` functions do.
macro_rules! init {
    ($type:ty) => {{
        // SAFETY: every field of these repr(C) structures is an integer, an array of integers,
        // a raw pointer or an Option of a function pointer, for all of which zero is valid.
        let mut value: $type = unsafe { core::mem::zeroed() };
        value.struct_size = core::mem::size_of::<$type>() as u32;
        value
    }};
}

/// Turns a result other than MIMUSB_OK into the library's own words for it. Call it right after
/// the call: the details belong to the calling thread's last result.
fn check(result: mimusb_result) -> Result<(), String> {
    if result == MIMUSB_OK {
        return Ok(());
    }
    let text = |raw: *const core::ffi::c_char| {
        if raw.is_null() {
            String::new()
        } else {
            unsafe { CStr::from_ptr(raw) }.to_string_lossy().into_owned()
        }
    };
    let detail = unsafe { &*mimusb_last_error() };
    let (field, reason) = (text(detail.field), text(detail.reason));
    let name = text(unsafe { mimusb_result_name(result) });
    Err(format!("{name}: {field}: {reason}"))
}

fn run(port: &CStr, baud_rate: u32) -> Result<(), String> {
    let manufacturer = CString::new("libmimusb").unwrap();
    let product = CString::new("Rust keyboard example").unwrap();

    let mut config = init!(mimusb_device_config);
    config.transport.baud_rate = baud_rate;
    config.transport.handshake_timeout_ms = 50;
    config.transport.maximum_report_bytes = 64;
    config.transport.log_level = MIMUSB_LOG_DISABLED;
    config.target.linux_hid.descriptor_bytes = 4096;
    config.target.linux_hid.fields_per_report = 256;
    config.target.linux_hid.global_stack_depth = 4;
    config.target.linux_hid.report_size_bits = 256;
    config.target.linux_hid.usages = 12288;
    config.target.linux_hid.report_data_bits = 65528;
    config.usb_device.bcd_usb = 0x0200;
    config.usb_device.vendor_id = 0x1209;
    config.usb_device.product_id = 0x0001;
    config.usb_device.ep0_max_packet_size = 64;
    config.usb_device.bcd_device = 0x0100;
    config.usb_device.language_id = 0x0409;
    config.usb_device.manufacturer = manufacturer.as_ptr();
    config.usb_device.product = product.as_ptr();
    config.usb_device.serial_from_chip_id = 1;
    config.usb_config.configuration_value = 1;
    config.usb_config.bm_attributes = 0xA0;
    config.usb_config.bmax_power = 50;

    let mut options = init!(mimusb_keyboard_options);
    options.function.bcd_hid = 0x0111; // HID 1.11
    options.usage_minimum = 0x04;
    options.usage_maximum = 0xDD;

    let mut timeouts = init!(mimusb_commit_timeouts);
    timeouts.control_timeout_ms = 500;
    timeouts.reconnect_timeout_ms = 3000;

    // SAFETY: as for init!; this structure has no struct_size to set.
    let mut endpoint: mimusb_placed_endpoint = unsafe { core::mem::zeroed() };
    endpoint.role = MIMUSB_ROLE_HID_IN;
    endpoint.endpoint.address = 0x81;
    endpoint.endpoint.transfer_type = MIMUSB_TRANSFER_INTERRUPT as u8;
    endpoint.endpoint.interval = 1;
    endpoint.endpoint.max_packet_size = 64;
    endpoint.endpoint.policy.on_full = MIMUSB_FULL_DROP_NEWEST as u8;
    endpoint.endpoint.policy.depth = 7;
    let mut placement = init!(mimusb_node_placement);
    placement.first_interface = 0;
    placement.endpoints = &endpoint;
    placement.endpoint_count = 1;

    let mut device = ptr::null_mut();
    let mut spec = ptr::null_mut();
    let mut node = ptr::null_mut();
    let outcome = (|| unsafe {
        check(mimusb_device_open(port.as_ptr(), &config, &mut device))?;
        check(mimusb_spec_create_keyboard(&options, &mut spec))?;
        check(mimusb_node_open(device, spec, &placement, &timeouts, &mut node))?;
        check(mimusb_device_wait_ready(device, 5000))?;
        for down in [1, 0] {
            check(mimusb_keyboard_key(node, LEFT_SHIFT, down))?;
            check(mimusb_node_submit(node, 500))?;
            thread::sleep(Duration::from_millis(50));
        }
        Ok(())
    })();
    unsafe {
        if !spec.is_null() {
            mimusb_spec_release(spec);
        }
        if !device.is_null() {
            // Taking the USB device off the target drops reports still queued on the board.
            thread::sleep(Duration::from_millis(50));
            mimusb_device_close(device, 500, 1000);
        }
    }
    outcome
}

fn main() {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    let parsed = match arguments.as_slice() {
        [port, baud] => CString::new(port.as_str()).ok().zip(baud.parse::<u32>().ok()),
        _ => None,
    };
    let Some((port, baud_rate)) = parsed else {
        eprintln!("usage: keyboard PORT BAUD_RATE");
        std::process::exit(2);
    };
    if let Err(message) = run(&port, baud_rate) {
        eprintln!("{message}");
        std::process::exit(1);
    }
}
