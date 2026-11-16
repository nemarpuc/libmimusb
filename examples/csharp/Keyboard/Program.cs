// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
// Presents a keyboard through the board on PORT and presses Left Shift once.
//
// The board's USB port is plugged into the target, which sees a new keyboard and a Shift key press
// and release. The values are those of the Init() methods (the ones the tests use, within docs/LIMITS.md);
// UsbIdentity.ForTesting is the vendor and product ID pid.codes sets aside for in-house testing: a
// device you hand out needs IDs of its own. KeyboardRaw is the same program with every call and
// value spelled out.
using MimUsb;

const ushort LeftShift = 0xE1; // HID Keyboard page usage

if (args.Length != 2 || !uint.TryParse(args[1], out var baudRate))
{
    Console.Error.WriteLine("usage: Keyboard PORT BAUD_RATE");
    return 2;
}

try
{
    using var board = new Board(args[0], baudRate, UsbIdentity.ForTesting("C# keyboard example"));
    var keyboard = board.Keyboard();
    board.WaitReady(TimeSpan.FromSeconds(5));
    keyboard.Tap(LeftShift);
    return 0;
}
catch (MimUsbException error)
{
    Console.Error.WriteLine(error.Message);
    return 1;
}
