// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
//! `mimusb::values` must equal the structures tests/fill_values.h fills, byte for byte.
//! MIMUSB_VALUES_ORACLE names the built mimusb_abi_values_oracle program. The oracle uses the test
//! identity (strings "libmimusb", IDs 0x1209:0x0001) and baud rate 115200, so the crate is given the
//! same. Pointer fields are compared by being null or not.

use core::mem::{offset_of, size_of};
use mimusb::values::{self, UsbIdentity};
use mimusb_sys as sys;
use std::collections::BTreeMap;
use std::ffi::CStr;
use std::process::Command;

fn bytes_of<T>(value: &T) -> Vec<u8> {
    // SAFETY: a plain repr(C) structure read as bytes; the values functions start from zero.
    unsafe { core::slice::from_raw_parts(value as *const T as *const u8, size_of::<T>()).to_vec() }
}

fn hex(text: &str) -> Vec<u8> {
    (0..text.len() / 2).map(|i| u8::from_str_radix(&text[2 * i..2 * i + 2], 16).unwrap()).collect()
}

fn masked(mut data: Vec<u8>, pointers: &[usize]) -> Vec<u8> {
    for &offset in pointers {
        let width = size_of::<usize>();
        let set = data[offset..offset + width].iter().any(|&b| b != 0);
        data[offset..offset + width].fill(0);
        data[offset] = set as u8;
    }
    data
}

#[test]
fn values_equal_the_test_fill_values() {
    let path = std::env::var("MIMUSB_VALUES_ORACLE").expect("MIMUSB_VALUES_ORACLE is not set");
    let text = String::from_utf8(Command::new(path).output().expect("run the oracle").stdout).unwrap();
    let lines: BTreeMap<&str, &str> = text.lines().filter_map(|l| l.split_once('=')).collect();

    macro_rules! check {
        ($name:literal, $value:expr, [$($pointer:expr),*]) => {{
            let pointers: &[usize] = &[$($pointer),*];
            let value = $value;
            assert_eq!(masked(bytes_of(value), pointers), masked(hex(lines[$name]), pointers), "{}", $name);
        }};
    }

    let identity = UsbIdentity { vendor_id: 0x1209, product_id: 0x0001, manufacturer: c"libmimusb".to_owned(), product: c"libmimusb".to_owned() };
    let usb = offset_of!(sys::mimusb_device_config, usb_device);
    let config = values::device_config(115200, identity);
    check!("device_config", &config.config, [
        offset_of!(sys::mimusb_device_config, transport) + offset_of!(sys::mimusb_transport_options, event_sink),
        offset_of!(sys::mimusb_device_config, transport) + offset_of!(sys::mimusb_transport_options, event_user),
        offset_of!(sys::mimusb_device_config, transport) + offset_of!(sys::mimusb_transport_options, log_sink),
        offset_of!(sys::mimusb_device_config, transport) + offset_of!(sys::mimusb_transport_options, log_user),
        usb + offset_of!(sys::mimusb_usb_device, manufacturer), usb + offset_of!(sys::mimusb_usb_device, product),
        usb + offset_of!(sys::mimusb_usb_device, serial),
        offset_of!(sys::mimusb_device_config, usb_config) + offset_of!(sys::mimusb_usb_config, string)]);
    check!("commit_timeouts", &values::commit_timeouts(), []);
    check!("keyboard_options", &values::keyboard_options(), []);
    check!("mouse_options", &values::mouse_options(), []);
    check!("toggle_options", &values::toggle_options(), [
        offset_of!(sys::mimusb_toggle_options, allowed_usages), offset_of!(sys::mimusb_toggle_options, usage_semantics),
        offset_of!(sys::mimusb_toggle_options, expected_linux_event_types), offset_of!(sys::mimusb_toggle_options, expected_linux_codes)]);
    check!("gamepad_options", &values::gamepad_options(), [offset_of!(sys::mimusb_gamepad_options, axes)]);
    check!("touchscreen_options", &values::touchscreen_options(), []);
    check!("touchpad_options", &values::touchpad_options(), []);
    check!("pen_options", &values::pen_options(), [offset_of!(sys::mimusb_pen_options, barrel_usages)]);
    check!("digitizer_options", &values::digitizer_options(), [offset_of!(sys::mimusb_digitizer_options, barrel_usages)]);
    check!("hid_endpoint0", &values::hid_placement(0).endpoint.endpoint, []);
    check!("hid_endpoint3", &values::hid_placement(3).endpoint.endpoint, []);

    let gamepad = values::gamepad_options();
    let codes: Vec<String> = (0..gamepad.axis_count)
        .map(|i| unsafe { CStr::from_ptr((*gamepad.axes.add(i)).expected_linux_code) }.to_str().unwrap().to_owned())
        .collect();
    assert_eq!(lines["gamepad_axes"], codes.join(","));
}
