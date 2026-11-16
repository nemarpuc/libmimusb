#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libmimusb contributors
"""Present a keyboard through the board on PORT and press Left Shift once.

The board's USB port is plugged into the target, which sees a new keyboard and a Shift key press
and release. The values are those of mimusb.initial (the ones the tests use, within docs/LIMITS.md);
UsbIdentity.for_testing is the vendor and product ID pid.codes sets aside for in-house testing: a
device you hand out needs IDs of its own. keyboard_raw.py is the same program with every call and
value spelled out.
"""

import sys

import mimusb

LEFT_SHIFT = 0xE1  # HID Keyboard page usage

if len(sys.argv) != 4:
    sys.exit("usage: keyboard.py LIBMIMUSB_PATH PORT BAUD_RATE")

try:
    identity = mimusb.UsbIdentity.for_testing("Python keyboard example")
    with mimusb.Board(sys.argv[1], sys.argv[2], int(sys.argv[3]), identity) as board:
        keyboard = board.keyboard()
        board.wait_ready()
        keyboard.tap(LEFT_SHIFT)
except mimusb.Error as error:
    sys.exit(str(error))
