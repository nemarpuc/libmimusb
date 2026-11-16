// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
// What a Board needs besides the Init() values (Initial.cs): the USB identity and baud rate, which have
// no value of their own, and the placement of a HID Node. They mean nothing for a product: a device you
// hand out needs a USB identity of its own, so UsbIdentity.ForTesting has to be named.
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

public static class Setup
{
    /// <summary>A device configuration from <c>DeviceConfig.Init()</c> with the baud rate and the identity filled in; the identity's strings are unmanaged memory the owner frees.</summary>
    public static DeviceConfigOwner DeviceConfig(uint baudRate, UsbIdentity identity)
    {
        var config = MimUsb.DeviceConfig.Init();
        config.Transport.BaudRate = baudRate;
        config.UsbDevice.VendorId = identity.VendorId;
        config.UsbDevice.ProductId = identity.ProductId;
        var manufacturer = Marshal.StringToCoTaskMemUTF8(identity.Manufacturer);
        var product = Marshal.StringToCoTaskMemUTF8(identity.Product);
        config.UsbDevice.Manufacturer = manufacturer;
        config.UsbDevice.Product = product;
        return new DeviceConfigOwner(config, manufacturer, product);
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
}
