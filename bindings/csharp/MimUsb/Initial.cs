// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
// The initial values of the structures HAND_INITIAL names in tools/bindings/generate.py. The C
// mimusb_*_init functions only zero a structure and set struct_size, by decision; these Init() methods
// also fill the values the tests (tests/fill_values.h) and the examples have always used, each within
// the limits of docs/LIMITS.md. MimUsb.Tests/HandChecks.cs compares them with what that header
// produces. They mean nothing for a product, and DeviceConfig.Init() leaves the baud rate, the vendor
// and product IDs and the strings empty: those are the caller's (Setup.DeviceConfig fills them in).
using System.Runtime.InteropServices;

namespace MimUsb;

/// <summary>Helpers for the Init() values.</summary>
public static class Initial
{
    internal static uint SizeOf<T>() where T : struct => (uint)Marshal.SizeOf<T>();

    public static IntegerField Field(int logicalMinimum, int logicalMaximum, uint bitWidth) =>
        new() { LogicalMinimum = logicalMinimum, LogicalMaximum = logicalMaximum, BitWidth = bitWidth };

    /// <summary>HID 1.11, no boot interface, not localized.</summary>
    internal static HidFunction HidFunction() => new() { BcdHid = 0x0111 };

    private static GamepadAxisOption Axis(AxisRole role, ushort usage, string code) => new()
    {
        Role = role, UsagePage = 0x01, Usage = usage, Value = Field(-127, 127, 8), NeutralValue = 0,
        ExpectedLinuxCode = Marshal.StringToCoTaskMemUTF8(code),
    };

    internal static readonly nint GamepadAxes = MakeGamepadAxes();

    private static nint MakeGamepadAxes()
    {
        var axes = new[]
        {
            Axis(AxisRole.X, 0x30, "ABS_X"), Axis(AxisRole.Y, 0x31, "ABS_Y"),
            Axis(AxisRole.Z, 0x32, "ABS_Z"), Axis(AxisRole.Rz, 0x35, "ABS_RZ"),
        };
        var size = Marshal.SizeOf<GamepadAxisOption>();
        var block = Marshal.AllocHGlobal(size * axes.Length);
        for (var i = 0; i < axes.Length; i++)
            Marshal.StructureToPtr(axes[i], block + i * size, false);
        return block;
    }

    internal static readonly nint BarrelUsages = MakeBarrelUsages();

    private static nint MakeBarrelUsages()
    {
        var block = Marshal.AllocHGlobal(sizeof(ushort));
        Marshal.WriteInt16(block, 0x44);
        return block;
    }

}

public partial struct DeviceConfig
{
    /// <summary>A bus-powered USB 2.0 full-speed device with remote wakeup, 64-byte reports (the largest report every board takes: a full-speed board's largest endpoint packet) and the Linux HID limits of docs/LIMITS.md. The baud rate, IDs and strings stay empty.</summary>
    public static DeviceConfig Init()
    {
        var config = new DeviceConfig { StructSize = Initial.SizeOf<DeviceConfig>() };
        config.Transport.HandshakeTimeoutMs = 50;
        config.Transport.MaximumReportBytes = 64;
        config.Transport.LogLevel = LogLevel.Disabled;
        config.Target.LinuxHid = new LinuxHidPolicy
        {
            DescriptorBytes = 4096, FieldsPerReport = 256, GlobalStackDepth = 4, ReportSizeBits = 256,
            Usages = 12288, ReportDataBits = 65528,
        };
        config.UsbDevice.BcdUsb = 0x0200;
        config.UsbDevice.Ep0MaxPacketSize = 64;
        config.UsbDevice.BcdDevice = 0x0100;
        config.UsbDevice.LanguageId = 0x0409;
        config.UsbDevice.SerialFromChipId = 1; // a fixed serial would be the same on every board
        config.UsbConfig.ConfigurationValue = 1;
        config.UsbConfig.BmAttributes = 0xA0;
        config.UsbConfig.BmaxPower = 50;
        return config;
    }
}

public partial struct CommitTimeouts
{
    public static CommitTimeouts Init() => new()
    {
        StructSize = Initial.SizeOf<CommitTimeouts>(), ControlTimeoutMs = 500, ReconnectTimeoutMs = 3000,
    };
}

public partial struct KeyboardOptions
{
    public static KeyboardOptions Init() => new()
    {
        StructSize = Initial.SizeOf<KeyboardOptions>(), Function = Initial.HidFunction(),
        UsageMinimum = 0x04, UsageMaximum = 0xDD,
    };
}

public partial struct MouseOptions
{
    public static MouseOptions Init() => new()
    {
        StructSize = Initial.SizeOf<MouseOptions>(), Function = Initial.HidFunction(),
        ButtonCount = 5, X = Initial.Field(-32767, 32767, 16), Y = Initial.Field(-32767, 32767, 16),
        EnableWheel = 1, Wheel = Initial.Field(-127, 127, 8), EnablePan = 1, Pan = Initial.Field(-127, 127, 8),
    };
}

public partial struct ToggleOptions
{
    /// <summary>The keys themselves (AllowedUsages and the arrays beside it) are the caller's to list.</summary>
    public static ToggleOptions Init() => new()
    {
        StructSize = Initial.SizeOf<ToggleOptions>(), Function = Initial.HidFunction(),
        ApplicationPage = 0x0C, ApplicationUsage = 0x01, FieldPage = 0x0C,
    };
}

public partial struct GamepadOptions
{
    public static GamepadOptions Init() => new()
    {
        StructSize = Initial.SizeOf<GamepadOptions>(), Function = Initial.HidFunction(),
        Axes = Initial.GamepadAxes, AxisCount = 4, ButtonCount = 12, ButtonUsageMinimum = 1,
        DpadRepresentation = DpadRepresentation.Hat, HatLogicalMinimum = 0, HatLogicalMaximum = 7, HatBitWidth = 4,
    };
}

public partial struct TouchscreenOptions
{
    public static TouchscreenOptions Init() => new()
    {
        StructSize = Initial.SizeOf<TouchscreenOptions>(), Function = Initial.HidFunction(),
        MaximumContacts = 10, ContactsPerReport = 10, ContactId = Initial.Field(0, 15, 4),
        X = Initial.Field(0, 32767, 16), Y = Initial.Field(0, 32767, 16), ContactCount = Initial.Field(0, 10, 4),
    };
}

public partial struct TouchpadOptions
{
    public static TouchpadOptions Init() => new()
    {
        StructSize = Initial.SizeOf<TouchpadOptions>(), Function = Initial.HidFunction(),
        MaximumContacts = 5, ContactsPerReport = 5, ContactId = Initial.Field(0, 15, 4),
        X = Initial.Field(0, 32767, 16), Y = Initial.Field(0, 32767, 16), ContactCount = Initial.Field(0, 5, 4), ButtonCount = 1,
    };
}

public partial struct PenOptions
{
    public static PenOptions Init() => new()
    {
        StructSize = Initial.SizeOf<PenOptions>(), Function = Initial.HidFunction(),
        X = Initial.Field(0, 32767, 16), Y = Initial.Field(0, 32767, 16), EnablePressure = 1, Pressure = Initial.Field(0, 4095, 12),
        BarrelUsages = Initial.BarrelUsages, BarrelUsageCount = 1, EnableHover = 1,
    };
}

public partial struct DigitizerOptions
{
    public static DigitizerOptions Init() => new()
    {
        StructSize = Initial.SizeOf<DigitizerOptions>(), Function = Initial.HidFunction(),
        X = Initial.Field(0, 32767, 16), Y = Initial.Field(0, 32767, 16), EnablePressure = 1, Pressure = Initial.Field(0, 4095, 12),
        BarrelUsages = Initial.BarrelUsages, BarrelUsageCount = 1, EnableHover = 1,
    };
}
