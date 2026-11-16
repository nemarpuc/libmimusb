// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
// The initial values of the structures HAND_INITIAL names in tools/bindings/generate.py. The C
// `mimusb_*_init` functions only zero a structure and set `struct_size`, by decision; these `Default`
// impls also fill the values the tests (tests/fill_values.h) and the examples have always used, each
// within the limits of docs/LIMITS.md. mimusb/tests/initial.rs compares them with what that header
// produces. They mean nothing for a product, and `mimusb_device_config::default()` leaves the baud
// rate, the vendor and product IDs and the strings empty: those are the caller's.
// The pointer targets are statics, valid for the life of the process.

use crate::*;

fn sized<T>() -> T {
    // SAFETY: the structures here hold integers, pointers and optional function pointers, and
    // all-zero bytes are valid for each of them.
    let mut value: T = unsafe { core::mem::zeroed() };
    // SAFETY: every structure that calls this starts with a `u32` struct_size.
    unsafe { *(&mut value as *mut T as *mut u32) = core::mem::size_of::<T>() as u32 };
    value
}

/// A field with no physical properties.
pub fn integer_field(logical_minimum: i32, logical_maximum: i32, bit_width: u32) -> mimusb_integer_field {
    // SAFETY: all-zero bytes are valid for an integer field.
    let mut field: mimusb_integer_field = unsafe { core::mem::zeroed() };
    field.logical_minimum = logical_minimum;
    field.logical_maximum = logical_maximum;
    field.bit_width = bit_width;
    field
}

/// HID 1.11, no boot interface, not localized.
fn hid_function() -> mimusb_hid_function {
    // SAFETY: all-zero bytes are valid for this structure.
    mimusb_hid_function { bcd_hid: 0x0111, ..unsafe { core::mem::zeroed() } }
}

impl Default for mimusb_device_config {
    /// A bus-powered USB 2.0 full-speed device with remote wakeup, 64-byte reports (the largest
    /// report every board takes: a full-speed board's largest endpoint packet) and the Linux HID
    /// limits of docs/LIMITS.md. The baud rate, IDs and strings stay empty.
    fn default() -> Self {
        let mut config: Self = sized();
        config.transport.handshake_timeout_ms = 50;
        config.transport.maximum_report_bytes = 64;
        config.transport.log_level = MIMUSB_LOG_DISABLED;
        config.target.linux_hid = mimusb_linux_hid_policy {
            descriptor_bytes: 4096,
            fields_per_report: 256,
            global_stack_depth: 4,
            report_size_bits: 256,
            usages: 12288,
            report_data_bits: 65528,
        };
        config.usb_device.bcd_usb = 0x0200;
        config.usb_device.ep0_max_packet_size = 64;
        config.usb_device.bcd_device = 0x0100;
        config.usb_device.language_id = 0x0409;
        config.usb_device.serial_from_chip_id = 1; // a fixed serial would be the same on every board
        config.usb_config.configuration_value = 1;
        config.usb_config.bm_attributes = 0xA0;
        config.usb_config.bmax_power = 50;
        config
    }
}

impl Default for mimusb_commit_timeouts {
    fn default() -> Self {
        Self { control_timeout_ms: 500, reconnect_timeout_ms: 3000, ..sized() }
    }
}

impl Default for mimusb_keyboard_options {
    fn default() -> Self {
        Self { function: hid_function(), usage_minimum: 0x04, usage_maximum: 0xDD, ..sized() }
    }
}

impl Default for mimusb_mouse_options {
    fn default() -> Self {
        Self {
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
}

impl Default for mimusb_toggle_options {
    /// The keys themselves (`allowed_usages` and the arrays beside it) are the caller's to list.
    fn default() -> Self {
        Self { function: hid_function(), application_page: 0x0C, application_usage: 0x01, field_page: 0x0C, ..sized() }
    }
}

struct Axes([mimusb_gamepad_axis_option; 4]);
// SAFETY: read-only, and the pointers inside point at string literals.
unsafe impl Sync for Axes {}

const fn axis(role: i32, usage: u16, code: *const core::ffi::c_char) -> mimusb_gamepad_axis_option {
    mimusb_gamepad_axis_option {
        role,
        usage_page: 0x01,
        usage,
        value: mimusb_integer_field {
            logical_minimum: -127,
            logical_maximum: 127,
            bit_width: 8,
            physical: mimusb_physical_properties { enabled: 0, minimum: 0, maximum: 0, unit_exponent: 0, unit: 0 },
        },
        neutral_value: 0,
        expected_linux_code: code,
    }
}

static GAMEPAD_AXES: Axes = Axes([
    axis(MIMUSB_AXIS_X, 0x30, c"ABS_X".as_ptr()),
    axis(MIMUSB_AXIS_Y, 0x31, c"ABS_Y".as_ptr()),
    axis(MIMUSB_AXIS_Z, 0x32, c"ABS_Z".as_ptr()),
    axis(MIMUSB_AXIS_RZ, 0x35, c"ABS_RZ".as_ptr()),
]);

impl Default for mimusb_gamepad_options {
    /// The axis table is a static, so the options stay valid for the life of the process.
    fn default() -> Self {
        Self {
            function: hid_function(),
            axes: GAMEPAD_AXES.0.as_ptr(),
            axis_count: 4,
            button_count: 12,
            button_usage_minimum: 1,
            dpad_representation: MIMUSB_DPAD_HAT,
            hat_logical_minimum: 0,
            hat_logical_maximum: 7,
            hat_bit_width: 4,
            ..sized()
        }
    }
}

impl Default for mimusb_touchscreen_options {
    fn default() -> Self {
        Self {
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
}

impl Default for mimusb_touchpad_options {
    fn default() -> Self {
        Self {
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
}

static BARREL_USAGES: [u16; 1] = [0x44];

impl Default for mimusb_pen_options {
    fn default() -> Self {
        Self {
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
}

impl Default for mimusb_digitizer_options {
    fn default() -> Self {
        Self {
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
}
