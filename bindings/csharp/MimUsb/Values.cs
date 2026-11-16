// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
// Structures filled with working values; the C mimusb_*_init functions (and so the generated Init()
// methods) fill none. The numbers are the ones the tests (tests/fill_values.h) and the examples have
// always used, each within the limits of docs/LIMITS.md; MimUsb.Tests/HandChecks.cs compares them with
// what that header produces. They mean nothing for a product: a device you hand out needs a USB
// identity of its own, so the IDs and strings have no value here and UsbIdentity.ForTesting has to
// be named.
using System.Runtime.InteropServices;

namespace MimUsb;

/// <summary>What the target sees: the vendor and product IDs and the two strings.</summary>
public sealed record UsbIdentity(ushort VendorId, ushort ProductId, string Manufacturer, string Product)
{
    /// <summary>0x1209:0x0001, the pair pid.codes sets aside for in-house testing; never for a device you hand out.</summary>
    public static UsbIdentity ForTesting(string product) => new(0x1209, 0x0001, "libmimusb", product);
}

/// <summary>A device configuration and the unmanaged strings its pointers refer to.</summary>
public sealed class DeviceConfigOwner : IDisposable
{
    private readonly nint _manufacturer;
    private readonly nint _product;

    public DeviceConfig Config;

    internal DeviceConfigOwner(DeviceConfig config, nint manufacturer, nint product)
    {
        Config = config;
        _manufacturer = manufacturer;
        _product = product;
    }

    public void Dispose()
    {
        Marshal.FreeCoTaskMem(_manufacturer);
        Marshal.FreeCoTaskMem(_product);
    }
}

/// <summary>Where a HID Node goes: bInterfaceNumber <c>interfaceNumber</c>, polled every 1 ms through IN endpoint
/// 0x81 + <c>interfaceNumber</c> with 64-byte packets, drop-newest with a depth of 7, and no OUT endpoint.</summary>
public sealed class HidPlacementOwner : IDisposable
{
    private nint _endpoint;

    public NodePlacement Placement;

    internal HidPlacementOwner(NodePlacement placement, nint endpoint)
    {
        Placement = placement;
        _endpoint = endpoint;
    }

    public void Dispose()
    {
        if (_endpoint != 0)
        {
            Marshal.FreeHGlobal(_endpoint);
            _endpoint = 0;
        }
    }
}

public static class Values
{
    /// <summary>A bus-powered USB 2.0 full-speed device with remote wakeup, 64-byte reports and the Linux HID limits of docs/LIMITS.md. 64 bytes is the largest report every board takes (a full-speed board's largest endpoint packet), so it is valid on all of them.</summary>
    public static DeviceConfigOwner DeviceConfig(uint baudRate, UsbIdentity identity)
    {
        var config = MimUsb.DeviceConfig.Init();
        config.Transport.BaudRate = baudRate;
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
        config.UsbDevice.VendorId = identity.VendorId;
        config.UsbDevice.ProductId = identity.ProductId;
        var manufacturer = Marshal.StringToCoTaskMemUTF8(identity.Manufacturer);
        var product = Marshal.StringToCoTaskMemUTF8(identity.Product);
        config.UsbDevice.Manufacturer = manufacturer;
        config.UsbDevice.Product = product;
        config.UsbDevice.SerialFromChipId = 1; // a fixed serial would be the same on every board
        config.UsbConfig.ConfigurationValue = 1;
        config.UsbConfig.BmAttributes = 0xA0;
        config.UsbConfig.BmaxPower = 50;
        return new DeviceConfigOwner(config, manufacturer, product);
    }

    public static CommitTimeouts CommitTimeouts()
    {
        var timeouts = MimUsb.CommitTimeouts.Init();
        timeouts.ControlTimeoutMs = 500;
        timeouts.ReconnectTimeoutMs = 3000;
        return timeouts;
    }

    public static HidPlacementOwner HidPlacement(byte interfaceNumber)
    {
        var endpoint = new PlacedEndpoint
        {
            Role = EndpointRole.HidIn,
            Endpoint = new UsbEndpoint
            {
                Address = (byte)(0x81 + interfaceNumber), TransferType = (byte)Native.MIMUSB_TRANSFER_INTERRUPT,
                Interval = 1, MaxPacketSize = 64,
                Policy = new EndpointPolicy { OnFull = (byte)Native.MIMUSB_FULL_DROP_NEWEST, Depth = 7 },
            },
        };
        var block = Marshal.AllocHGlobal(Marshal.SizeOf<PlacedEndpoint>());
        Marshal.StructureToPtr(endpoint, block, false);
        var placement = NodePlacement.Init();
        placement.FirstInterface = interfaceNumber;
        placement.Endpoints = block;
        placement.EndpointCount = 1;
        return new HidPlacementOwner(placement, block);
    }

    public static IntegerField Field(int logicalMinimum, int logicalMaximum, uint bitWidth) =>
        new() { LogicalMinimum = logicalMinimum, LogicalMaximum = logicalMaximum, BitWidth = bitWidth };

    /// <summary>HID 1.11, no boot interface, not localized.</summary>
    public static HidFunction HidFunction() => new() { BcdHid = 0x0111 };

    public static KeyboardOptions KeyboardOptions()
    {
        var o = MimUsb.KeyboardOptions.Init();
        o.Function = HidFunction();
        o.UsageMinimum = 0x04;
        o.UsageMaximum = 0xDD;
        return o;
    }

    public static MouseOptions MouseOptions()
    {
        var o = MimUsb.MouseOptions.Init();
        o.Function = HidFunction();
        o.ButtonCount = 5;
        o.X = Field(-32767, 32767, 16);
        o.Y = Field(-32767, 32767, 16);
        o.EnableWheel = 1;
        o.Wheel = Field(-127, 127, 8);
        o.EnablePan = 1;
        o.Pan = Field(-127, 127, 8);
        return o;
    }

    /// <summary>The keys themselves (AllowedUsages and the arrays beside it) are the caller's to list.</summary>
    public static ToggleOptions ToggleOptions()
    {
        var o = MimUsb.ToggleOptions.Init();
        o.Function = HidFunction();
        o.ApplicationPage = 0x0C;
        o.ApplicationUsage = 0x01;
        o.FieldPage = 0x0C;
        return o;
    }

    private static GamepadAxisOption Axis(AxisRole role, ushort usage, string code) => new()
    {
        Role = role, UsagePage = 0x01, Usage = usage, Value = Field(-127, 127, 8), NeutralValue = 0,
        ExpectedLinuxCode = Marshal.StringToCoTaskMemUTF8(code),
    };

    private static readonly nint GamepadAxes = MakeGamepadAxes();

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

    private static readonly nint BarrelUsages = MakeBarrelUsages();

    private static nint MakeBarrelUsages()
    {
        var block = Marshal.AllocHGlobal(sizeof(ushort));
        Marshal.WriteInt16(block, 0x44);
        return block;
    }

    /// <summary>The axis table is allocated once and lives for the life of the process.</summary>
    public static GamepadOptions GamepadOptions()
    {
        var o = MimUsb.GamepadOptions.Init();
        o.Function = HidFunction();
        o.Axes = GamepadAxes;
        o.AxisCount = 4;
        o.ButtonCount = 12;
        o.ButtonUsageMinimum = 1;
        o.DpadRepresentation = DpadRepresentation.Hat;
        o.HatLogicalMinimum = 0;
        o.HatLogicalMaximum = 7;
        o.HatBitWidth = 4;
        return o;
    }

    public static TouchscreenOptions TouchscreenOptions()
    {
        var o = MimUsb.TouchscreenOptions.Init();
        o.Function = HidFunction();
        o.MaximumContacts = 10;
        o.ContactsPerReport = 10;
        o.ContactId = Field(0, 15, 4);
        o.X = Field(0, 32767, 16);
        o.Y = Field(0, 32767, 16);
        o.ContactCount = Field(0, 10, 4);
        return o;
    }

    public static TouchpadOptions TouchpadOptions()
    {
        var o = MimUsb.TouchpadOptions.Init();
        o.Function = HidFunction();
        o.MaximumContacts = 5;
        o.ContactsPerReport = 5;
        o.ContactId = Field(0, 15, 4);
        o.X = Field(0, 32767, 16);
        o.Y = Field(0, 32767, 16);
        o.ContactCount = Field(0, 5, 4);
        o.ButtonCount = 1;
        return o;
    }

    public static PenOptions PenOptions()
    {
        var o = MimUsb.PenOptions.Init();
        o.Function = HidFunction();
        o.X = Field(0, 32767, 16);
        o.Y = Field(0, 32767, 16);
        o.EnablePressure = 1;
        o.Pressure = Field(0, 4095, 12);
        o.BarrelUsages = BarrelUsages;
        o.BarrelUsageCount = 1;
        o.EnableHover = 1;
        return o;
    }

    public static DigitizerOptions DigitizerOptions()
    {
        var o = MimUsb.DigitizerOptions.Init();
        o.Function = HidFunction();
        o.X = Field(0, 32767, 16);
        o.Y = Field(0, 32767, 16);
        o.EnablePressure = 1;
        o.Pressure = Field(0, 4095, 12);
        o.BarrelUsages = BarrelUsages;
        o.BarrelUsageCount = 1;
        o.EnableHover = 1;
        return o;
    }
}
