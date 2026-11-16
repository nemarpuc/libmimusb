// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
//! Structures filled with working values; the C `mimusb_*_init` functions (and so `Default` of the
//! `mimusb-sys` structures) fill none.
//!
//! The numbers are the ones the tests (tests/fill_values.h) and the examples have always used, each
//! within the limits of docs/LIMITS.md; tests/values.rs compares them with what that header produces.
//! They mean nothing for a product: a device you hand out needs a USB identity of its own, so the
//! IDs and strings have no value here and `UsbIdentity::for_testing` has to be named.

use crate::sys;
use std::ffi::CString;

/// What the target sees: the vendor and product IDs and the two strings.
#[derive(Clone, Debug)]
pub struct UsbIdentity {
    pub vendor_id: u16,
    pub product_id: u16,
    pub manufacturer: CString,
    pub product: CString,
}

impl UsbIdentity {
    /// 0x1209:0x0001, the pair pid.codes sets aside for in-house testing; never for a device you hand out.
    pub fn for_testing(product: &str) -> Self {
        Self {
            vendor_id: 0x1209,
            product_id: 0x0001,
            manufacturer: c"libmimusb".to_owned(),
            product: CString::new(product).expect("a product name has no NUL"),
        }
    }
}

/// A device configuration and the strings its pointers refer to.
pub struct DeviceConfig {
    pub config: sys::mimusb_device_config,
    _identity: UsbIdentity,
}

impl DeviceConfig {
    pub fn as_ptr(&self) -> *const sys::mimusb_device_config {
        &self.config
    }
}

fn sized<T>() -> T {
    // SAFETY: the structures here hold integers, pointers and optional function pointers, and
    // all-zero bytes are valid for each of them.
    let mut value: T = unsafe { core::mem::zeroed() };
    // SAFETY: every structure that calls this starts with a `u32` struct_size.
    unsafe { *(&mut value as *mut T as *mut u32) = core::mem::size_of::<T>() as u32 };
    value
}

/// A bus-powered USB 2.0 full-speed device with remote wakeup, 64-byte reports and the Linux HID
/// limits of docs/LIMITS.md. 64 bytes is the largest report every board takes (a full-speed board's
/// largest endpoint packet), so it is valid on all of them.
pub fn device_config(baud_rate: u32, identity: UsbIdentity) -> DeviceConfig {
    let mut config: sys::mimusb_device_config = sized();
    config.transport.baud_rate = baud_rate;
    config.transport.handshake_timeout_ms = 50;
    config.transport.maximum_report_bytes = 64;
    config.transport.log_level = sys::MIMUSB_LOG_DISABLED;
    config.target.linux_hid = sys::mimusb_linux_hid_policy {
        descriptor_bytes: 4096,
        fields_per_report: 256,
        global_stack_depth: 4,
        report_size_bits: 256,
        usages: 12288,
        report_data_bits: 65528,
    };
    let usb = &mut config.usb_device;
    usb.bcd_usb = 0x0200;
    usb.ep0_max_packet_size = 64;
    usb.bcd_device = 0x0100;
    usb.language_id = 0x0409;
    usb.vendor_id = identity.vendor_id;
    usb.product_id = identity.product_id;
    usb.manufacturer = identity.manufacturer.as_ptr();
    usb.product = identity.product.as_ptr();
    usb.serial_from_chip_id = 1; // a fixed serial would be the same on every board
    config.usb_config.configuration_value = 1;
    config.usb_config.bm_attributes = 0xA0;
    config.usb_config.bmax_power = 50;
    DeviceConfig { config, _identity: identity }
}

pub fn commit_timeouts() -> sys::mimusb_commit_timeouts {
    sys::mimusb_commit_timeouts { control_timeout_ms: 500, reconnect_timeout_ms: 3000, ..sized() }
}

/// Where a HID Node goes: bInterfaceNumber `interface_number`, polled every 1 ms through IN
/// endpoint 0x81 + `interface_number` with 64-byte packets, drop-newest with a depth of 7, and no
/// OUT endpoint. The endpoint lives in a `Box` so the pointer in the placement stays valid.
pub struct HidPlacement {
    pub placement: sys::mimusb_node_placement,
    pub endpoint: Box<sys::mimusb_placed_endpoint>,
}

pub fn hid_placement(interface_number: u8) -> HidPlacement {
    let mut endpoint: Box<sys::mimusb_placed_endpoint> = Box::new(unsafe { core::mem::zeroed() });
    endpoint.role = sys::MIMUSB_ROLE_HID_IN;
    endpoint.endpoint.address = 0x81 + interface_number;
    endpoint.endpoint.transfer_type = sys::MIMUSB_TRANSFER_INTERRUPT as u8;
    endpoint.endpoint.interval = 1;
    endpoint.endpoint.max_packet_size = 64;
    endpoint.endpoint.policy.on_full = sys::MIMUSB_FULL_DROP_NEWEST as u8;
    endpoint.endpoint.policy.depth = 7;
    let mut placement: sys::mimusb_node_placement = sized();
    placement.first_interface = interface_number;
    placement.endpoints = &*endpoint;
    placement.endpoint_count = 1;
    HidPlacement { placement, endpoint }
}

pub fn integer_field(logical_minimum: i32, logical_maximum: i32, bit_width: u32) -> sys::mimusb_integer_field {
    // SAFETY: all-zero bytes are valid for an integer field.
    let mut field: sys::mimusb_integer_field = unsafe { core::mem::zeroed() };
    field.logical_minimum = logical_minimum;
    field.logical_maximum = logical_maximum;
    field.bit_width = bit_width;
    field
}

/// HID 1.11, no boot interface, not localized.
pub fn hid_function() -> sys::mimusb_hid_function {
    sys::mimusb_hid_function { bcd_hid: 0x0111, ..unsafe { core::mem::zeroed() } }
}

pub fn keyboard_options() -> sys::mimusb_keyboard_options {
    sys::mimusb_keyboard_options { function: hid_function(), usage_minimum: 0x04, usage_maximum: 0xDD, ..sized() }
}

pub fn mouse_options() -> sys::mimusb_mouse_options {
    sys::mimusb_mouse_options {
        function: hid_function(),
        button_count: 5,
        x: integer_field(-32767, 32767, 16),
        y: integer_field(-32767, 32767, 16),
        enable_wheel: 1,
        wheel: integer_field(-127, 127, 8),
        enable_pan: 1,
        pan: integer_field(-127, 127, 8),
        ..sized()
    }
}

/// The keys themselves (`allowed_usages` and the arrays beside it) are the caller's to list.
pub fn toggle_options() -> sys::mimusb_toggle_options {
    sys::mimusb_toggle_options {
        function: hid_function(),
        application_page: 0x0C,
        application_usage: 0x01,
        field_page: 0x0C,
        ..sized()
    }
}

struct Axes([sys::mimusb_gamepad_axis_option; 4]);
// SAFETY: read-only, and the pointers inside point at string literals.
unsafe impl Sync for Axes {}

const fn axis(role: i32, usage: u16, code: *const core::ffi::c_char) -> sys::mimusb_gamepad_axis_option {
    sys::mimusb_gamepad_axis_option {
        role,
        usage_page: 0x01,
        usage,
        value: sys::mimusb_integer_field {
            logical_minimum: -127,
            logical_maximum: 127,
            bit_width: 8,
            physical: sys::mimusb_physical_properties { enabled: 0, minimum: 0, maximum: 0, unit_exponent: 0, unit: 0 },
        },
        neutral_value: 0,
        expected_linux_code: code,
    }
}

static GAMEPAD_AXES: Axes = Axes([
    axis(sys::MIMUSB_AXIS_X, 0x30, c"ABS_X".as_ptr()),
    axis(sys::MIMUSB_AXIS_Y, 0x31, c"ABS_Y".as_ptr()),
    axis(sys::MIMUSB_AXIS_Z, 0x32, c"ABS_Z".as_ptr()),
    axis(sys::MIMUSB_AXIS_RZ, 0x35, c"ABS_RZ".as_ptr()),
]);

/// The axis table is a static, so the options stay valid for the life of the process.
pub fn gamepad_options() -> sys::mimusb_gamepad_options {
    sys::mimusb_gamepad_options {
        function: hid_function(),
        axes: GAMEPAD_AXES.0.as_ptr(),
        axis_count: 4,
        button_count: 12,
        button_usage_minimum: 1,
        dpad_representation: sys::MIMUSB_DPAD_HAT,
        hat_logical_minimum: 0,
        hat_logical_maximum: 7,
        hat_bit_width: 4,
        ..sized()
    }
}

pub fn touchscreen_options() -> sys::mimusb_touchscreen_options {
    sys::mimusb_touchscreen_options {
        function: hid_function(),
        maximum_contacts: 10,
        contacts_per_report: 10,
        contact_id: integer_field(0, 15, 4),
        x: integer_field(0, 32767, 16),
        y: integer_field(0, 32767, 16),
        contact_count: integer_field(0, 10, 4),
        ..sized()
    }
}

pub fn touchpad_options() -> sys::mimusb_touchpad_options {
    sys::mimusb_touchpad_options {
        function: hid_function(),
        maximum_contacts: 5,
        contacts_per_report: 5,
        contact_id: integer_field(0, 15, 4),
        x: integer_field(0, 32767, 16),
        y: integer_field(0, 32767, 16),
        contact_count: integer_field(0, 5, 4),
        button_count: 1,
        ..sized()
    }
}

static BARREL_USAGES: [u16; 1] = [0x44];

pub fn pen_options() -> sys::mimusb_pen_options {
    sys::mimusb_pen_options {
        function: hid_function(),
        x: integer_field(0, 32767, 16),
        y: integer_field(0, 32767, 16),
        enable_pressure: 1,
        pressure: integer_field(0, 4095, 12),
        barrel_usages: BARREL_USAGES.as_ptr(),
        barrel_usage_count: 1,
        enable_hover: 1,
        ..sized()
    }
}

pub fn digitizer_options() -> sys::mimusb_digitizer_options {
    sys::mimusb_digitizer_options {
        function: hid_function(),
        x: integer_field(0, 32767, 16),
        y: integer_field(0, 32767, 16),
        enable_pressure: 1,
        pressure: integer_field(0, 4095, 12),
        barrel_usages: BARREL_USAGES.as_ptr(),
        barrel_usage_count: 1,
        enable_hover: 1,
        ..sized()
    }
}
