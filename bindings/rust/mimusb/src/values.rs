// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
//! What a `Board` needs besides the `Default` values of `mimusb-sys` (its `initial.rs`): the USB
//! identity and baud rate, which have no value of their own, and the placement of a HID Node. They
//! mean nothing for a product: a device you hand out needs a USB identity of its own, so
//! `UsbIdentity::for_testing` has to be named.

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

/// A device configuration from `mimusb_device_config::default()` with the baud rate and the identity
/// filled in; the identity's strings live as long as the returned value.
pub fn device_config(baud_rate: u32, identity: UsbIdentity) -> DeviceConfig {
    let mut config = sys::mimusb_device_config::default();
    config.transport.baud_rate = baud_rate;
    config.usb_device.vendor_id = identity.vendor_id;
    config.usb_device.product_id = identity.product_id;
    config.usb_device.manufacturer = identity.manufacturer.as_ptr();
    config.usb_device.product = identity.product.as_ptr();
    DeviceConfig { config, _identity: identity }
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
    let mut placement = sys::mimusb_node_placement::default();
    placement.first_interface = interface_number;
    placement.endpoints = &*endpoint;
    placement.endpoint_count = 1;
    HidPlacement { placement, endpoint }
}
