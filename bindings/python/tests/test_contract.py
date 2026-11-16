# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libmimusb contributors
"""What the raw layer promises: a small surface, and no value chosen for the caller.

The working values are mimusb.values (test_values.py); a structure made without them stays as here.

The layout against the compiled C is test_layout.py, which is generated.
"""

import mimusb
from mimusb import native
from mimusb.native import DeviceConfig, KeyboardOptions


def test_package_exports_only_the_binding_surface():
    assert "load" in mimusb.__all__
    assert "MIMUSB_OK" in mimusb.__all__
    assert not hasattr(mimusb, "c")
    assert not hasattr(mimusb, "os")
    assert not hasattr(mimusb, "Union")


def test_a_new_option_structure_has_only_struct_size_set():
    # struct_size is the structure's size, as the C *_init sets it; every choice is the caller's.
    for structure in (KeyboardOptions, DeviceConfig):
        instance = structure()
        assert instance.struct_size == native.c.sizeof(structure)
        for name, *_ in structure._fields_:
            value = getattr(instance, name)
            assert name == "struct_size" or not isinstance(value, int) or value == 0, name
        assert structure(struct_size=7).struct_size == 7


def test_device_config_exposes_the_serial_and_usb_choices():
    names = {name for name, *_ in DeviceConfig._fields_}
    assert {"struct_size", "transport", "target", "usb_device", "usb_config"} <= names
    transport = {name for name, *_ in native.TransportOptions._fields_}
    assert {"baud_rate", "handshake_timeout_ms", "maximum_report_bytes",
            "validate_reports", "keep_attached", "event_sink"} <= transport
    target = {name for name, *_ in native.TargetLimits._fields_}
    assert {"linux_hid"} <= target


if __name__ == "__main__":
    test_package_exports_only_the_binding_surface()
    test_a_new_option_structure_has_only_struct_size_set()
    test_device_config_exposes_the_serial_and_usb_choices()
