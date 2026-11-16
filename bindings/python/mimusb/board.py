# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libmimusb contributors
"""A Board owns the Device on one serial port and the HID Nodes opened on it.

Nothing is picked for the caller: the port, the baud rate and the USB identity are arguments, and
the board is never searched for. Each HID Node gets the next interface number and IN endpoint
(``values.HidPlacement``), checked against the board's limits. A Node is not closed by itself:
closing one re-enumerates the board's USB device (docs/API.md ``mimusb_node_close``), so ``close``
of the Board closes the Device, which takes its Nodes with it. Calls are not thread-safe, as the C
ABI requires. Every object has ``.raw`` (the ctypes handle) to drop to the raw calls.
"""

from __future__ import annotations

import ctypes as c
import time
from typing import Callable, Optional, cast

from . import native as n, values

__all__ = ["Board", "Node"]

# Taking the board's USB device off the target drops the reports still queued on the board, so the
# last ones get this long to leave (the examples wait the same).
FLUSH_SECONDS = 0.05


def _error(field: str, reason: str) -> n.Error:
    return n.Error(n.MIMUSB_ERR_UNSUPPORTED, "MIMUSB_ERR_UNSUPPORTED", field, reason)


class Board:
    def __init__(self, library_path, port: str, baud_rate: int, identity: values.UsbIdentity, *,
                 config: Optional[n.DeviceConfig] = None, timeouts: Optional[n.CommitTimeouts] = None,
                 wait_ready_ms: int = 5000) -> None:
        self.library = n.load(library_path)
        expected = (n.MIMUSB_VERSION_MAJOR << 24) | (n.MIMUSB_VERSION_MINOR << 16) | n.MIMUSB_VERSION_PATCH
        found = self.library.mimusb_version()
        if found != expected:
            raise _error("version", f"the library is version {found >> 24}.{(found >> 16) & 0xFF}.{found & 0xFFFF}, "
                         f"this package is {n.MIMUSB_VERSION_MAJOR}.{n.MIMUSB_VERSION_MINOR}.{n.MIMUSB_VERSION_PATCH}")
        self.wait_ready_ms = wait_ready_ms
        self.timeouts = timeouts if timeouts is not None else values.commit_timeouts()
        self._config = config if config is not None else values.device_config(baud_rate, identity)
        self._placements: list[values.HidPlacement] = []
        self._nodes: list[Node] = []
        self.raw = n.DeviceP()
        self._check(self.library.mimusb_device_open(port.encode(), c.byref(self._config), c.byref(self.raw)))
        try:
            info = n.DeviceInfo()
            self._check(self.library.mimusb_device_board_info(self.raw, c.byref(info)))
            if not info.firmware_compatible:
                raise _error("firmware", f"the board runs firmware {info.version_major}.{info.version_minor}."
                             f"{info.version_patch}; this library needs the same major and minor version")
            self.limits = n.BoardLimits()
            self._check(self.library.mimusb_device_limits(self.raw, c.byref(self.limits)))
        except BaseException:
            self.close()
            raise

    def _check(self, result: int) -> int:
        return n.check(self.library, result)

    def node(self, kind: str, options=None) -> "Node":
        """Open a HID Node of ``kind`` (a key of ``NODE_KINDS``) on the next interface and endpoint."""
        make, create, cls = NODE_KINDS[kind]
        number = len(self._placements)
        if number >= self.limits.max_interfaces_any or 0x81 + number - 0x80 > self.limits.max_endpoint_number:
            raise _error("nodes", f"the board has {self.limits.max_interfaces_any} interfaces and endpoint "
                         f"numbers up to {self.limits.max_endpoint_number}; Node {number + 1} does not fit")
        options = options if options is not None else make()
        spec = n.SpecP()
        self._check(getattr(self.library, create)(c.byref(options), c.byref(spec)))
        try:
            placement = values.hid_placement(number)
            raw = n.NodeP()
            self._check(self.library.mimusb_node_open(self.raw, spec, c.byref(placement.placement),
                                                      c.byref(self.timeouts), c.byref(raw)))
        finally:
            self.library.mimusb_spec_release(spec)
        self._placements.append(placement)
        node = cls(self, raw)
        self._nodes.append(node)
        return node

    def keyboard(self, options=None) -> "KeyboardNode":
        return cast(KeyboardNode, self.node("keyboard", options))

    def mouse(self, options=None) -> "MouseNode":
        return cast(MouseNode, self.node("mouse", options))

    def wait_ready(self, timeout_ms: Optional[int] = None) -> None:
        """Wait until the target has mounted the board's USB device; open every Node first."""
        self._check(self.library.mimusb_device_wait_ready(
            self.raw, self.wait_ready_ms if timeout_ms is None else timeout_ms))

    def close(self) -> None:
        if self.raw:
            time.sleep(FLUSH_SECONDS)
            self.library.mimusb_device_close(self.raw, self.timeouts.control_timeout_ms, 1000)
            self.raw = n.DeviceP()
        for node in self._nodes:
            node.raw = None
        self._nodes.clear()

    def __enter__(self) -> "Board":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


class Node:
    def __init__(self, board: Board, raw) -> None:
        self.board = board
        self.raw = raw
        self._library = board.library
        self._check = board._check

    def submit(self, timeout_ms: int = 500) -> None:
        """Queue the pending state without waiting for it to be sent."""
        self._check(self._library.mimusb_node_submit(self.raw, timeout_ms))

    def submit_blocking(self, timeout_ms: int = 500) -> None:
        self._check(self._library.mimusb_node_submit_blocking(self.raw, timeout_ms))

    def battery(self, strength: int) -> None:
        self._check(self._library.mimusb_battery_update(self.raw, strength))


class KeyboardNode(Node):
    def key(self, usage: int, down: bool) -> None:
        self._check(self._library.mimusb_keyboard_key(self.raw, usage, int(down)))

    def tap(self, usage: int, hold_seconds: float = 0.05) -> None:
        """Press, send, wait ``hold_seconds``, release, send."""
        self.key(usage, True)
        self.submit_blocking()
        time.sleep(hold_seconds)
        self.key(usage, False)
        self.submit_blocking()


class MouseNode(Node):
    def move(self, dx: int, dy: int) -> None:
        self._check(self._library.mimusb_mouse_move(self.raw, dx, dy))

    def scroll(self, wheel: int, pan: int = 0) -> None:
        self._check(self._library.mimusb_mouse_scroll(self.raw, wheel, pan))

    def button(self, button: int, down: bool) -> None:
        self._check(self._library.mimusb_mouse_button(self.raw, button, int(down)))


class ToggleNode(Node):
    def usage(self, usage: int, down: bool) -> None:
        self._check(self._library.mimusb_toggle_usage(self.raw, usage, int(down)))


class GamepadNode(Node):
    def button(self, button: int, down: bool) -> None:
        self._check(self._library.mimusb_gamepad_button(self.raw, button, int(down)))

    def axis(self, axis_index: int, value: int) -> None:
        self._check(self._library.mimusb_gamepad_axis(self.raw, axis_index, value))

    def dpad(self, up: bool, down: bool, right: bool, left: bool) -> None:
        self._check(self._library.mimusb_gamepad_dpad(self.raw, int(up), int(down), int(right), int(left)))


class _ContactNode(Node):
    _contact: str

    def contact(self, contact_id: int, down: bool, x: int, y: int, extra=None) -> None:
        call = getattr(self._library, self._contact)
        self._check(call(self.raw, contact_id, int(down), x, y, None if extra is None else c.byref(extra)))


class TouchscreenNode(_ContactNode):
    _contact = "mimusb_touchscreen_contact"


class TouchpadNode(_ContactNode):
    _contact = "mimusb_touchpad_contact"

    def button(self, button: int, down: bool) -> None:
        self._check(self._library.mimusb_touchpad_button(self.raw, button, int(down)))


class PenNode(Node):
    def update(self, sample: n.PenSample) -> None:
        self._check(self._library.mimusb_pen_update(self.raw, c.byref(sample)))


class DigitizerNode(Node):
    def update(self, sample: n.DigitizerSample) -> None:
        self._check(self._library.mimusb_digitizer_update(self.raw, c.byref(sample)))


NODE_KINDS: dict[str, tuple[Callable, str, type]] = {
    "keyboard": (values.keyboard_options, "mimusb_spec_create_keyboard", KeyboardNode),
    "mouse": (values.mouse_options, "mimusb_spec_create_mouse", MouseNode),
    "toggle": (values.toggle_options, "mimusb_spec_create_toggle", ToggleNode),
    "gamepad": (values.gamepad_options, "mimusb_spec_create_gamepad", GamepadNode),
    "touchscreen": (values.touchscreen_options, "mimusb_spec_create_touchscreen", TouchscreenNode),
    "touchpad": (values.touchpad_options, "mimusb_spec_create_touchpad", TouchpadNode),
    "pen": (values.pen_options, "mimusb_spec_create_pen", PenNode),
    "digitizer": (values.digitizer_options, "mimusb_spec_create_digitizer", DigitizerNode),
}
