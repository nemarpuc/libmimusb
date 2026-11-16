# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libmimusb contributors
"""The initial values of this binding's option structures.

The C ``mimusb_*_init`` functions only zero a structure and set ``struct_size``, by decision, and a
``ctypes`` structure created without arguments (``mimusb.native``) does the same. The functions here
return the structure with the values the tests (tests/fill_values.h) and the examples have always used,
each within the limits of docs/LIMITS.md; tests/test_initial.py compares them byte for byte with what
that header produces. They mean nothing for a product: a device you hand out needs a USB identity of
its own, so ``vendor_id``, ``product_id`` and the strings have no value here and
``UsbIdentity.for_testing`` has to be named.

Each function returns a plain ctypes structure; keyword arguments of the profile functions replace
single fields, and any nested field can be changed on the result.
"""

from __future__ import annotations

import ctypes as c
from dataclasses import dataclass

from . import native as n

__all__ = [
    "UsbIdentity", "device_config", "commit_timeouts", "HidPlacement", "hid_placement", "integer_field",
    "hid_function", "keyboard_options", "mouse_options", "toggle_options", "gamepad_options",
    "touchscreen_options", "touchpad_options", "pen_options", "digitizer_options",
]


@dataclass(frozen=True)
class UsbIdentity:
    """What the target sees: the vendor and product IDs and the two strings."""

    vendor_id: int
    product_id: int
    manufacturer: str
    product: str

    @classmethod
    def for_testing(cls, product: str) -> "UsbIdentity":
        """0x1209:0x0001, the pair pid.codes sets aside for in-house testing; never for a device you hand out."""
        return cls(0x1209, 0x0001, "libmimusb", product)


def device_config(baud_rate: int, identity: UsbIdentity) -> n.DeviceConfig:
    """A bus-powered USB 2.0 full-speed device with remote wakeup, 64-byte reports and the Linux HID limits of docs/LIMITS.md."""
    config = n.DeviceConfig()
    transport = config.transport
    transport.baud_rate = baud_rate
    transport.handshake_timeout_ms = 50
    # 64 is the largest report every board takes (docs/LIMITS.md "Report size": a full-speed board's
    # largest endpoint packet), so it is valid on all of them.
    transport.maximum_report_bytes = 64
    transport.log_level = n.MIMUSB_LOG_DISABLED
    hid = config.target.linux_hid
    hid.descriptor_bytes, hid.fields_per_report, hid.global_stack_depth = 4096, 256, 4
    hid.report_size_bits, hid.usages, hid.report_data_bits = 256, 12288, 65528
    usb = config.usb_device
    usb.bcd_usb, usb.ep0_max_packet_size, usb.bcd_device, usb.language_id = 0x0200, 64, 0x0100, 0x0409
    usb.vendor_id, usb.product_id = identity.vendor_id, identity.product_id
    usb.manufacturer, usb.product = identity.manufacturer.encode(), identity.product.encode()
    usb.serial_from_chip_id = 1  # a fixed serial would be the same on every board
    usb_config = config.usb_config
    usb_config.configuration_value, usb_config.bm_attributes, usb_config.bmax_power = 1, 0xA0, 50
    return config


def commit_timeouts() -> n.CommitTimeouts:
    timeouts = n.CommitTimeouts()
    timeouts.control_timeout_ms = 500
    timeouts.reconnect_timeout_ms = 3000
    return timeouts


class HidPlacement:
    """Where a HID Node goes: bInterfaceNumber ``interface_number``, polled every 1 ms through IN endpoint
    0x81 + ``interface_number`` with 64-byte packets, drop-newest with a depth of 7, and no OUT endpoint.
    ``.placement`` is the structure for ``mimusb_node_open``; the endpoint it points at lives as long as this object."""

    def __init__(self, interface_number: int) -> None:
        endpoint = n.PlacedEndpoint()
        endpoint.role = n.MIMUSB_ROLE_HID_IN
        endpoint.endpoint.address = 0x81 + interface_number
        endpoint.endpoint.transfer_type = n.MIMUSB_TRANSFER_INTERRUPT
        endpoint.endpoint.interval = 1
        endpoint.endpoint.max_packet_size = 64
        endpoint.endpoint.policy.on_full = n.MIMUSB_FULL_DROP_NEWEST
        endpoint.endpoint.policy.depth = 7
        self.endpoint = endpoint
        self.placement = n.NodePlacement()
        self.placement.first_interface = interface_number
        self.placement.endpoints = c.pointer(endpoint)
        self.placement.endpoint_count = 1


def hid_placement(interface_number: int) -> HidPlacement:
    return HidPlacement(interface_number)


def integer_field(logical_minimum: int, logical_maximum: int, bit_width: int) -> n.IntegerField:
    field = n.IntegerField()
    field.logical_minimum, field.logical_maximum, field.bit_width = logical_minimum, logical_maximum, bit_width
    return field


def hid_function() -> n.HidFunction:
    """HID 1.11, no boot interface, not localized."""
    function = n.HidFunction()
    function.bcd_hid = 0x0111
    return function


def _build(cls: type, values: dict, overrides: dict):
    options = cls()
    for name, value in {"function": hid_function(), **values, **overrides}.items():
        setattr(options, name, value)
    return options


def keyboard_options(**overrides):
    return _build(n.KeyboardOptions, {"usage_minimum": 0x04, "usage_maximum": 0xDD}, overrides)


def mouse_options(**overrides):
    return _build(n.MouseOptions, {
        "button_count": 5, "x": integer_field(-32767, 32767, 16), "y": integer_field(-32767, 32767, 16),
        "enable_wheel": 1, "wheel": integer_field(-127, 127, 8),
        "enable_pan": 1, "pan": integer_field(-127, 127, 8)}, overrides)


def toggle_options(**overrides):
    """The keys themselves (allowed_usages and the arrays beside it) are the caller's to list."""
    return _build(n.ToggleOptions, {"application_page": 0x0C, "application_usage": 0x01, "field_page": 0x0C}, overrides)


def _axis(role: int, usage: int, code: bytes) -> n.GamepadAxisOption:
    axis = n.GamepadAxisOption()
    axis.role, axis.usage_page, axis.usage = role, 0x01, usage
    axis.value = integer_field(-127, 127, 8)
    axis.expected_linux_code = code
    return axis


_GAMEPAD_AXES = (n.GamepadAxisOption * 4)(
    _axis(n.MIMUSB_AXIS_X, 0x30, b"ABS_X"), _axis(n.MIMUSB_AXIS_Y, 0x31, b"ABS_Y"),
    _axis(n.MIMUSB_AXIS_Z, 0x32, b"ABS_Z"), _axis(n.MIMUSB_AXIS_RZ, 0x35, b"ABS_RZ"))
_BARREL_USAGES = (c.c_uint16 * 1)(0x44)


def gamepad_options(**overrides):
    return _build(n.GamepadOptions, {
        "axes": c.cast(_GAMEPAD_AXES, c.POINTER(n.GamepadAxisOption)), "axis_count": 4, "button_count": 12,
        "button_usage_minimum": 1, "dpad_representation": n.MIMUSB_DPAD_HAT,
        "hat_logical_minimum": 0, "hat_logical_maximum": 7, "hat_bit_width": 4}, overrides)


def touchscreen_options(**overrides):
    return _build(n.TouchscreenOptions, {
        "maximum_contacts": 10, "contacts_per_report": 10, "contact_id": integer_field(0, 15, 4),
        "x": integer_field(0, 32767, 16), "y": integer_field(0, 32767, 16),
        "contact_count": integer_field(0, 10, 4)}, overrides)


def touchpad_options(**overrides):
    return _build(n.TouchpadOptions, {
        "maximum_contacts": 5, "contacts_per_report": 5, "contact_id": integer_field(0, 15, 4),
        "x": integer_field(0, 32767, 16), "y": integer_field(0, 32767, 16),
        "contact_count": integer_field(0, 5, 4), "button_count": 1}, overrides)


def _pen_values() -> dict:
    return {"x": integer_field(0, 32767, 16), "y": integer_field(0, 32767, 16), "enable_pressure": 1,
            "pressure": integer_field(0, 4095, 12),
            "barrel_usages": c.cast(_BARREL_USAGES, c.POINTER(c.c_uint16)), "barrel_usage_count": 1,
            "enable_hover": 1}


def pen_options(**overrides):
    return _build(n.PenOptions, _pen_values(), overrides)


def digitizer_options(**overrides):
    return _build(n.DigitizerOptions, _pen_values(), overrides)
