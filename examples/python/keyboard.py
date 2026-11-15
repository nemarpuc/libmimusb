#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libmimusb contributors
"""Present a keyboard through the board on PORT and press Left Shift once.

The board's USB port is plugged into the target, which sees a new keyboard and a Shift key press
and release. Every value below is the program's own choice; none is a default of the library
(the limits they sit within are in docs/LIMITS.md). The vendor and product ID are the pair
pid.codes sets aside for in-house testing: a device you hand out needs IDs of its own.
"""

from __future__ import annotations

import ctypes as c
import sys
import time

from mimusb import Error, check
from mimusb.native import (
    MIMUSB_FULL_DROP_NEWEST,
    MIMUSB_LOG_DISABLED,
    MIMUSB_ROLE_HID_IN,
    MIMUSB_TRANSFER_INTERRUPT,
    CommitTimeouts,
    DeviceConfig,
    DeviceP,
    KeyboardOptions,
    NodeP,
    NodePlacement,
    PlacedEndpoint,
    SpecP,
    load,
)

LEFT_SHIFT = 0xE1  # HID Keyboard page usage


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: keyboard.py LIBMIMUSB_PATH PORT BAUD_RATE", file=sys.stderr)
        return 2
    library = load(sys.argv[1])
    port = sys.argv[2].encode()
    baud_rate = int(sys.argv[3])

    config = DeviceConfig()
    config.transport.baud_rate = baud_rate
    config.transport.handshake_timeout_ms = 50
    config.transport.maximum_report_bytes = 64
    config.transport.log_level = MIMUSB_LOG_DISABLED
    config.target.linux_hid.descriptor_bytes = 4096
    config.target.linux_hid.fields_per_report = 256
    config.target.linux_hid.global_stack_depth = 4
    config.target.linux_hid.report_size_bits = 256
    config.target.linux_hid.usages = 12288
    config.target.linux_hid.report_data_bits = 65528
    config.usb_device.bcd_usb = 0x0200
    config.usb_device.vendor_id = 0x1209
    config.usb_device.product_id = 0x0001
    config.usb_device.ep0_max_packet_size = 64
    config.usb_device.bcd_device = 0x0100
    config.usb_device.language_id = 0x0409
    config.usb_device.manufacturer = b"libmimusb"
    config.usb_device.product = b"Python keyboard example"
    config.usb_device.serial_from_chip_id = 1
    config.usb_config.configuration_value = 1
    config.usb_config.bm_attributes = 0xA0
    config.usb_config.bmax_power = 50

    options = KeyboardOptions()
    options.function.bcd_hid = 0x0111  # HID 1.11
    options.usage_minimum = 0x04
    options.usage_maximum = 0xDD

    timeouts = CommitTimeouts()
    timeouts.control_timeout_ms = 500
    timeouts.reconnect_timeout_ms = 3000

    endpoint = PlacedEndpoint()
    endpoint.role = MIMUSB_ROLE_HID_IN
    endpoint.endpoint.address = 0x81
    endpoint.endpoint.transfer_type = MIMUSB_TRANSFER_INTERRUPT
    endpoint.endpoint.interval = 1
    endpoint.endpoint.max_packet_size = 64
    endpoint.endpoint.policy.on_full = MIMUSB_FULL_DROP_NEWEST
    endpoint.endpoint.policy.depth = 7
    placement = NodePlacement()
    placement.first_interface = 0
    placement.endpoints = c.pointer(endpoint)
    placement.endpoint_count = 1

    device = DeviceP()
    spec = SpecP()
    node = NodeP()
    try:
        check(library, library.mimusb_device_open(port, c.byref(config), c.byref(device)))
        check(library, library.mimusb_spec_create_keyboard(c.byref(options), c.byref(spec)))
        check(library, library.mimusb_node_open(device, spec, c.byref(placement),
                                                c.byref(timeouts), c.byref(node)))
        check(library, library.mimusb_device_wait_ready(device, 5000))
        for down in (1, 0):
            check(library, library.mimusb_keyboard_key(node, LEFT_SHIFT, down))
            check(library, library.mimusb_node_submit(node, 500))
            time.sleep(0.05)
    except Error as error:
        print(error, file=sys.stderr)
        return 1
    finally:
        if spec:
            library.mimusb_spec_release(spec)
        if device:
            # Taking the USB device off the target drops reports still queued on the board.
            time.sleep(0.05)
            library.mimusb_device_close(device, 500, 1000)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
