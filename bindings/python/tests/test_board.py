#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libmimusb contributors
"""Board and its Nodes against the simulated board (tests/sim/sim_server.c): the real firmware core
on a pseudo-terminal, no hardware.

MIMUSB_LIBRARY names the shared libmimusb and MIMUSB_SIM_SERVER the built mimusb_sim_server.
"""

from __future__ import annotations

import os
import subprocess
import sys

import mimusb
from mimusb import UsbIdentity, native


def expect(error_type, call) -> None:
    try:
        call()
    except error_type:
        return
    raise AssertionError("expected " + error_type.__name__)


def main() -> int:
    library = os.environ.get("MIMUSB_LIBRARY")
    server = os.environ.get("MIMUSB_SIM_SERVER")
    if not library or not server:
        raise SystemExit("MIMUSB_LIBRARY and MIMUSB_SIM_SERVER must be set")
    identity = UsbIdentity.for_testing("Python board test")

    with subprocess.Popen([server], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True) as process:
        try:
            port = process.stdout.readline().strip()
            assert port.startswith("/dev/"), port
            with mimusb.Board(library, port, 115200, identity) as board:
                keyboard = board.keyboard()
                mouse = board.mouse()
                board.wait_ready()
                keyboard.tap(0xE1, hold_seconds=0)
                mouse.move(3, -3)
                mouse.button(1, True)
                mouse.submit_blocking()
                assert [p.placement.first_interface for p in board._placements] == [0, 1]
                # An options structure the library refuses comes back as its error, and the Board stays usable.
                expect(mimusb.Error, lambda: board.keyboard(mimusb.initial.keyboard_options(usage_minimum=0)))
            assert not board.raw
            # A missing baud rate or identity is a TypeError: nothing is filled in for the caller.
            expect(TypeError, lambda: mimusb.Board(library, port))
            # A library of another version is refused before the port is opened.
            real = native.MIMUSB_VERSION_PATCH
            native.MIMUSB_VERSION_PATCH = real + 1
            try:
                expect(mimusb.Error, lambda: mimusb.Board(library, port, 115200, identity))
            finally:
                native.MIMUSB_VERSION_PATCH = real
        finally:
            process.stdin.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
