// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
// The checks of the hand-written code: the Init() values (Initial.cs) against the bytes tests/fill_values.h produces (args[1]
// names the built mimusb_abi_values_oracle; pointer fields are compared by being null or not), and Board
// against the simulated board (args[2] names the built mimusb_sim_server: the real firmware core on a
// pseudo-terminal; the native loader must find libmimusb as "mimusb").
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using MimUsb;

internal static class HandChecks
{
    public static List<string> Run(string[] args)
    {
        var failures = new List<string>();
        failures.AddRange(CheckValues(args[1]));
        failures.AddRange(CheckBoard(args[2]));
        return failures;
    }

    private static byte[] Bytes<T>(T value) where T : struct
    {
        var size = Marshal.SizeOf<T>();
        var block = Marshal.AllocHGlobal(size);
        try
        {
            for (var i = 0; i < size; i++)
                Marshal.WriteByte(block, i, 0);
            Marshal.StructureToPtr(value, block, false);
            var bytes = new byte[size];
            Marshal.Copy(block, bytes, 0, size);
            return bytes;
        }
        finally
        {
            Marshal.FreeHGlobal(block);
        }
    }

    private static byte[] Masked(byte[] raw, int[] pointers)
    {
        var data = (byte[])raw.Clone();
        foreach (var offset in pointers)
        {
            var set = raw.Skip(offset).Take(IntPtr.Size).Any(b => b != 0);
            for (var i = 0; i < IntPtr.Size; i++)
                data[offset + i] = 0;
            data[offset] = set ? (byte)1 : (byte)0;
        }
        return data;
    }

    private static int At<T>(string field) => (int)Marshal.OffsetOf<T>(field);

    private static List<string> CheckValues(string oracle)
    {
        var failures = new List<string>();
        var start = new ProcessStartInfo(oracle) { RedirectStandardOutput = true };
        using var process = Process.Start(start)!;
        var text = process.StandardOutput.ReadToEnd();
        process.WaitForExit();
        var lines = text.Split('\n', StringSplitOptions.RemoveEmptyEntries).Select(l => l.Split('=', 2)).ToDictionary(p => p[0], p => p[1]);

        void Compare<T>(string name, T value, params int[] pointers) where T : struct
        {
            var want = Convert.FromHexString(lines[name]);
            var got = Bytes(value);
            if (got.Length != want.Length || !Masked(got, pointers).SequenceEqual(Masked(want, pointers)))
                failures.Add($"values: {name} differs");
        }

        var identity = new UsbIdentity(0x1209, 0x0001, "libmimusb", "libmimusb");
        using (var config = Setup.DeviceConfig(115200, identity))
        {
            var transport = At<DeviceConfig>("Transport");
            var usb = At<DeviceConfig>("UsbDevice");
            Compare("device_config", config.Config,
                transport + At<TransportOptions>("EventSink"), transport + At<TransportOptions>("EventUser"),
                transport + At<TransportOptions>("LogSink"), transport + At<TransportOptions>("LogUser"),
                usb + At<UsbDevice>("Manufacturer"), usb + At<UsbDevice>("Product"), usb + At<UsbDevice>("Serial"),
                At<DeviceConfig>("UsbConfig") + At<UsbConfig>("String"));
        }
        var bare = DeviceConfig.Init();
        if (bare.Transport.BaudRate != 0 || bare.UsbDevice.VendorId != 0 || bare.UsbDevice.ProductId != 0
            || bare.UsbDevice.Manufacturer != 0 || bare.UsbDevice.Product != 0)
            failures.Add("values: DeviceConfig.Init() chose a baud rate or a USB identity");
        Compare("commit_timeouts", CommitTimeouts.Init());
        Compare("keyboard_options", KeyboardOptions.Init());
        Compare("mouse_options", MouseOptions.Init());
        Compare("toggle_options", ToggleOptions.Init(),
            At<ToggleOptions>("AllowedUsages"), At<ToggleOptions>("UsageSemantics"),
            At<ToggleOptions>("ExpectedLinuxEventTypes"), At<ToggleOptions>("ExpectedLinuxCodes"));
        Compare("gamepad_options", GamepadOptions.Init(), At<GamepadOptions>("Axes"));
        Compare("touchscreen_options", TouchscreenOptions.Init());
        Compare("touchpad_options", TouchpadOptions.Init());
        Compare("pen_options", PenOptions.Init(), At<PenOptions>("BarrelUsages"));
        Compare("digitizer_options", DigitizerOptions.Init(), At<DigitizerOptions>("BarrelUsages"));
        using (var placement = Setup.HidPlacement(0))
            Compare("hid_endpoint0", Marshal.PtrToStructure<PlacedEndpoint>(placement.Placement.Endpoints).Endpoint);
        using (var placement = Setup.HidPlacement(3))
            Compare("hid_endpoint3", Marshal.PtrToStructure<PlacedEndpoint>(placement.Placement.Endpoints).Endpoint);
        var gamepad = GamepadOptions.Init();
        var size = Marshal.SizeOf<GamepadAxisOption>();
        var codes = Enumerable.Range(0, (int)gamepad.AxisCount)
            .Select(i => Marshal.PtrToStringUTF8(Marshal.PtrToStructure<GamepadAxisOption>(gamepad.Axes + i * size).ExpectedLinuxCode));
        if (lines["gamepad_axes"] != string.Join(",", codes))
            failures.Add("values: gamepad axes differ");
        return failures;
    }

    private static List<string> CheckBoard(string server)
    {
        var failures = new List<string>();
        var start = new ProcessStartInfo(server) { RedirectStandardInput = true, RedirectStandardOutput = true };
        using var process = Process.Start(start)!;
        try
        {
            var port = process.StandardOutput.ReadLine()?.Trim() ?? "";
            if (!port.StartsWith("/dev/"))
            {
                failures.Add($"board: the simulated board printed {port}");
                return failures;
            }
            using (var board = new Board(port, 115200, UsbIdentity.ForTesting("C# board test")))
            {
                var keyboard = board.Keyboard();
                var mouse = board.Mouse();
                var gamepad = board.Gamepad();
                board.WaitReady(TimeSpan.FromSeconds(5));
                keyboard.Tap(0xE1, TimeSpan.Zero);
                mouse.Move(3, -3);
                mouse.SubmitBlocking();
                gamepad.Axis(0, 100);
                gamepad.SubmitBlocking();
                var bad = KeyboardOptions.Init();
                bad.UsageMinimum = 0;
                try
                {
                    board.Keyboard(bad);
                    failures.Add("board: options the library refuses were accepted");
                }
                catch (MimUsbException)
                {
                }
            }
        }
        catch (Exception error)
        {
            failures.Add($"board: {error.Message}");
        }
        finally
        {
            process.StandardInput.Close();
            process.WaitForExit();
        }
        return failures;
    }
}
