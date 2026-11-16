// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
// Presents a keyboard through the board on PORT and presses Left Shift once.
//
// The board's USB port is plugged into the target, which sees a new keyboard and a Shift key
// press and release. Every value below is the program's own choice; none is a default of the
// library (the limits they sit within are in docs/LIMITS.md). The vendor and product ID are the
// pair pid.codes sets aside for in-house testing: a device you hand out needs IDs of its own.
using System.Runtime.InteropServices;
using MimUsb;

const ushort LeftShift = 0xE1; // HID Keyboard page usage

if (args.Length != 2 || !uint.TryParse(args[1], out var baudRate))
{
    Console.Error.WriteLine("usage: KeyboardRaw PORT BAUD_RATE");
    return 2;
}

var port = Marshal.StringToCoTaskMemUTF8(args[0]);
var manufacturer = Marshal.StringToCoTaskMemUTF8("libmimusb");
var product = Marshal.StringToCoTaskMemUTF8("C# keyboard example");
var placedEndpoint = Marshal.AllocHGlobal(Marshal.SizeOf<PlacedEndpoint>());
nint device = 0, spec = 0, node = 0;
try
{
    var config = DeviceConfig.Init();
    config.Transport.BaudRate = baudRate;
    config.Transport.HandshakeTimeoutMs = 50;
    config.Transport.MaximumReportBytes = 64;
    config.Transport.LogLevel = LogLevel.Disabled;
    config.Target.LinuxHid.DescriptorBytes = 4096;
    config.Target.LinuxHid.FieldsPerReport = 256;
    config.Target.LinuxHid.GlobalStackDepth = 4;
    config.Target.LinuxHid.ReportSizeBits = 256;
    config.Target.LinuxHid.Usages = 12288;
    config.Target.LinuxHid.ReportDataBits = 65528;
    config.UsbDevice.BcdUsb = 0x0200;
    config.UsbDevice.VendorId = 0x1209;
    config.UsbDevice.ProductId = 0x0001;
    config.UsbDevice.Ep0MaxPacketSize = 64;
    config.UsbDevice.BcdDevice = 0x0100;
    config.UsbDevice.LanguageId = 0x0409;
    config.UsbDevice.Manufacturer = manufacturer;
    config.UsbDevice.Product = product;
    config.UsbDevice.SerialFromChipId = 1;
    config.UsbConfig.ConfigurationValue = 1;
    config.UsbConfig.BmAttributes = 0xA0;
    config.UsbConfig.BmaxPower = 50;

    var options = KeyboardOptions.Init();
    options.Function.BcdHid = 0x0111; // HID 1.11
    options.UsageMinimum = 0x04;
    options.UsageMaximum = 0xDD;

    var timeouts = CommitTimeouts.Init();
    timeouts.ControlTimeoutMs = 500;
    timeouts.ReconnectTimeoutMs = 3000;

    var endpoint = new PlacedEndpoint { Role = EndpointRole.HidIn };
    endpoint.Endpoint.Address = 0x81;
    endpoint.Endpoint.TransferType = (byte)Native.MIMUSB_TRANSFER_INTERRUPT;
    endpoint.Endpoint.Interval = 1;
    endpoint.Endpoint.MaxPacketSize = 64;
    endpoint.Endpoint.Policy.OnFull = (byte)Native.MIMUSB_FULL_DROP_NEWEST;
    endpoint.Endpoint.Policy.Depth = 7;
    Marshal.StructureToPtr(endpoint, placedEndpoint, false);
    var placement = NodePlacement.Init();
    placement.FirstInterface = 0;
    placement.Endpoints = placedEndpoint;
    placement.EndpointCount = 1;

    MimUsbException.Check(Native.DeviceOpen(port, in config, out device));
    MimUsbException.Check(Native.SpecCreateKeyboard(in options, out spec));
    MimUsbException.Check(Native.NodeOpen(device, spec, in placement, in timeouts, out node));
    MimUsbException.Check(Native.DeviceWaitReady(device, 5000));
    foreach (var down in new uint[] { 1, 0 })
    {
        MimUsbException.Check(Native.KeyboardKey(node, LeftShift, down));
        MimUsbException.Check(Native.NodeSubmit(node, 500));
        Thread.Sleep(50);
    }
    return 0;
}
catch (MimUsbException error)
{
    Console.Error.WriteLine(error.Message);
    return 1;
}
finally
{
    if (spec != 0)
        Native.SpecRelease(spec);
    if (device != 0)
    {
        // Taking the USB device off the target drops reports still queued on the board.
        Thread.Sleep(50);
        Native.DeviceClose(device, 500, 1000);
    }
    Marshal.FreeCoTaskMem(port);
    Marshal.FreeCoTaskMem(manufacturer);
    Marshal.FreeCoTaskMem(product);
    Marshal.FreeHGlobal(placedEndpoint);
}
