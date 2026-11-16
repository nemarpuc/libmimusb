#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libmimusb contributors
"""mimusb.values must equal the structures tests/fill_values.h fills, byte for byte.

MIMUSB_VALUES_ORACLE names the built mimusb_abi_values_oracle program. The oracle uses the test
identity (strings "libmimusb", IDs 0x1209:0x0001) and baud rate 115200, so the binding is given the same.
Pointer fields are compared by being null or not, and strings by content.
"""

from __future__ import annotations

import ctypes as c
import os
import subprocess
import sys

from mimusb import UsbIdentity, native, values


def pointer_spans(cls, base: int = 0):
    spans = []
    for name, kind, *_ in cls._fields_:
        offset = base + getattr(cls, name).offset
        if issubclass(kind, c.Structure):
            spans += pointer_spans(kind, offset)
        elif issubclass(kind, c.Array) and issubclass(kind._type_, c.Structure):
            for i in range(kind._length_):
                spans += pointer_spans(kind._type_, offset + i * c.sizeof(kind._type_))
        elif kind is c.c_char_p or kind is c.c_void_p or issubclass(kind, c._Pointer) or issubclass(kind, c._CFuncPtr):
            spans.append((offset, c.sizeof(kind)))
    return spans


def masked(raw: bytes, spans) -> bytes:
    data = bytearray(raw)
    for offset, size in spans:
        data[offset:offset + size] = b"\x01" * size if any(raw[offset:offset + size]) else b"\x00" * size
    return bytes(data)


def main() -> int:
    path = os.environ.get("MIMUSB_VALUES_ORACLE")
    if not path:
        raise SystemExit("MIMUSB_VALUES_ORACLE is not set")
    text = subprocess.run([path], check=True, capture_output=True, text=True).stdout
    lines = dict(line.split("=", 1) for line in text.splitlines())
    identity = UsbIdentity(0x1209, 0x0001, "libmimusb", "libmimusb")
    built = {
        "device_config": values.device_config(115200, identity),
        "commit_timeouts": values.commit_timeouts(),
        "keyboard_options": values.keyboard_options(),
        "mouse_options": values.mouse_options(),
        "toggle_options": values.toggle_options(),
        "gamepad_options": values.gamepad_options(),
        "touchscreen_options": values.touchscreen_options(),
        "touchpad_options": values.touchpad_options(),
        "pen_options": values.pen_options(),
        "digitizer_options": values.digitizer_options(),
        "hid_endpoint0": values.hid_placement(0).endpoint.endpoint,
        "hid_endpoint3": values.hid_placement(3).endpoint.endpoint,
    }
    failures = []
    for name, got in built.items():
        want = bytes.fromhex(lines[name])
        raw = bytes(got)
        spans = pointer_spans(type(got))
        if len(raw) != len(want) or masked(raw, spans) != masked(want, spans):
            failures.append(f"{name}: bytes differ")
    axes = values.gamepad_options().axes
    if lines["gamepad_axes"] != ",".join(axes[i].expected_linux_code.decode() for i in range(4)):
        failures.append("gamepad axes differ")
    usb = built["device_config"].usb_device
    if lines["strings"] != f"{usb.manufacturer.decode()},{usb.product.decode()}":
        failures.append("device strings differ")
    for failure in failures:
        print("values mismatch:", failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
