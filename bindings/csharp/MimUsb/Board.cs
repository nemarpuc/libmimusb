// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
// A Board owns the Device on one serial port and lends out HID Nodes.
//
// Nothing is picked for the caller: the port, the baud rate and the USB identity are arguments, and the
// board is never searched for. Each HID Node gets the next interface number and IN endpoint
// (Setup.HidPlacement), checked against the board's limits. A Node is not closed by itself: closing one
// re-enumerates the board's USB device (docs/API.md mimusb_node_close), so disposing the Board closes the
// Device, which takes its Nodes with it. Calls are not thread-safe, as the C ABI requires.
using System.Runtime.InteropServices;

namespace MimUsb;

public sealed class Board : IDisposable
{
    /// <summary>Taking the board's USB device off the target drops the reports still queued on the board, so the last ones get this long to leave.</summary>
    public static readonly TimeSpan Flush = TimeSpan.FromMilliseconds(50);

    private readonly DeviceConfigOwner _config;
    private readonly CommitTimeouts _timeouts = CommitTimeouts.Init();
    private readonly List<HidPlacementOwner> _placements = new();
    private nint _device;
    private BoardLimits _limits = BoardLimits.Init();
    private byte _next;

    public Board(string port, uint baudRate, UsbIdentity identity)
    {
        var expected = (Native.MIMUSB_VERSION_MAJOR << 24) | (Native.MIMUSB_VERSION_MINOR << 16) | Native.MIMUSB_VERSION_PATCH;
        var found = Native.Version();
        if (found != expected)
            throw new MimUsbException(Result.Unsupported, "version",
                $"the library is version {found >> 24}.{(found >> 16) & 0xFF}.{found & 0xFFFF}, this package is "
                + $"{Native.MIMUSB_VERSION_MAJOR}.{Native.MIMUSB_VERSION_MINOR}.{Native.MIMUSB_VERSION_PATCH}");
        _config = Setup.DeviceConfig(baudRate, identity);
        var path = Marshal.StringToCoTaskMemUTF8(port);
        try
        {
            MimUsbException.Check(Native.DeviceOpen(path, in _config.Config, out _device));
        }
        catch
        {
            _config.Dispose();
            throw;
        }
        finally
        {
            Marshal.FreeCoTaskMem(path);
        }
        try
        {
            var info = new DeviceInfo();
            MimUsbException.Check(Native.DeviceBoardInfo(_device, ref info));
            if (info.FirmwareCompatible == 0)
                throw new MimUsbException(Result.Unsupported, "firmware",
                    $"the board runs firmware {info.VersionMajor}.{info.VersionMinor}.{info.VersionPatch}, another major or minor version than this library");
            MimUsbException.Check(Native.DeviceLimits(_device, ref _limits));
        }
        catch
        {
            Dispose();
            throw;
        }
    }

    public nint Handle => _device;

    public BoardLimits Limits => _limits;

    /// <summary>Waits until the target has mounted the board's USB device; open every Node first.</summary>
    public void WaitReady(TimeSpan timeout) => MimUsbException.Check(Native.DeviceWaitReady(_device, (uint)timeout.TotalMilliseconds));

    private T Open<T>(nint spec, Func<Board, nint, T> make)
    {
        try
        {
            if (_next >= _limits.MaxInterfacesAny || _next + 1 > _limits.MaxEndpointNumber)
                throw new MimUsbException(Result.Unsupported, "nodes", "the board has no interface or endpoint number left for another Node");
            var placement = Setup.HidPlacement(_next);
            _placements.Add(placement);
            var timeouts = _timeouts;
            MimUsbException.Check(Native.NodeOpen(_device, spec, in placement.Placement, in timeouts, out var raw));
            _next++;
            return make(this, raw);
        }
        finally
        {
            Native.SpecRelease(spec); // the Node keeps its own reference
        }
    }

    public KeyboardNode Keyboard(KeyboardOptions? options = null)
    {
        var o = options ?? KeyboardOptions.Init();
        MimUsbException.Check(Native.SpecCreateKeyboard(in o, out var spec));
        return Open(spec, (b, raw) => new KeyboardNode(b, raw));
    }

    public MouseNode Mouse(MouseOptions? options = null)
    {
        var o = options ?? MouseOptions.Init();
        MimUsbException.Check(Native.SpecCreateMouse(in o, out var spec));
        return Open(spec, (b, raw) => new MouseNode(b, raw));
    }

    public ToggleNode Toggle(ToggleOptions options)
    {
        MimUsbException.Check(Native.SpecCreateToggle(in options, out var spec));
        return Open(spec, (b, raw) => new ToggleNode(b, raw));
    }

    public GamepadNode Gamepad(GamepadOptions? options = null)
    {
        var o = options ?? GamepadOptions.Init();
        MimUsbException.Check(Native.SpecCreateGamepad(in o, out var spec));
        return Open(spec, (b, raw) => new GamepadNode(b, raw));
    }

    public TouchscreenNode Touchscreen(TouchscreenOptions? options = null)
    {
        var o = options ?? TouchscreenOptions.Init();
        MimUsbException.Check(Native.SpecCreateTouchscreen(in o, out var spec));
        return Open(spec, (b, raw) => new TouchscreenNode(b, raw));
    }

    public TouchpadNode Touchpad(TouchpadOptions? options = null)
    {
        var o = options ?? TouchpadOptions.Init();
        MimUsbException.Check(Native.SpecCreateTouchpad(in o, out var spec));
        return Open(spec, (b, raw) => new TouchpadNode(b, raw));
    }

    public PenNode Pen(PenOptions? options = null)
    {
        var o = options ?? PenOptions.Init();
        MimUsbException.Check(Native.SpecCreatePen(in o, out var spec));
        return Open(spec, (b, raw) => new PenNode(b, raw));
    }

    public DigitizerNode Digitizer(DigitizerOptions? options = null)
    {
        var o = options ?? DigitizerOptions.Init();
        MimUsbException.Check(Native.SpecCreateDigitizer(in o, out var spec));
        return Open(spec, (b, raw) => new DigitizerNode(b, raw));
    }

    public void Dispose()
    {
        if (_device != 0)
        {
            Thread.Sleep(Flush);
            Native.DeviceClose(_device, _timeouts.ControlTimeoutMs, 1000);
            _device = 0;
        }
        foreach (var placement in _placements)
            placement.Dispose();
        _placements.Clear();
        _config.Dispose();
    }
}

/// <summary>A HID Node lent by a Board; it lives until the Board is disposed.</summary>
public class BoardNode
{
    protected BoardNode(Board board, nint handle)
    {
        Board = board;
        Handle = handle;
    }

    public Board Board { get; }

    public nint Handle { get; }

    /// <summary>Queues the pending state without waiting for it to be sent.</summary>
    public void Submit(uint timeoutMs = 500) => MimUsbException.Check(Native.NodeSubmit(Handle, timeoutMs));

    public void SubmitBlocking(uint timeoutMs = 500) => MimUsbException.Check(Native.NodeSubmitBlocking(Handle, timeoutMs));
}

public sealed class KeyboardNode : BoardNode
{
    internal KeyboardNode(Board board, nint handle) : base(board, handle) { }

    public void Key(ushort usage, bool down) => MimUsbException.Check(Native.KeyboardKey(Handle, usage, down ? 1u : 0u));

    /// <summary>Press, send, wait <paramref name="hold"/>, release, send.</summary>
    public void Tap(ushort usage, TimeSpan? hold = null)
    {
        Key(usage, true);
        SubmitBlocking();
        Thread.Sleep(hold ?? TimeSpan.FromMilliseconds(50));
        Key(usage, false);
        SubmitBlocking();
    }
}

public sealed class MouseNode : BoardNode
{
    internal MouseNode(Board board, nint handle) : base(board, handle) { }

    public void Move(int dx, int dy) => MimUsbException.Check(Native.MouseMove(Handle, dx, dy));

    public void Scroll(int wheel, int pan = 0) => MimUsbException.Check(Native.MouseScroll(Handle, wheel, pan));

    public void Button(uint button, bool down) => MimUsbException.Check(Native.MouseButton(Handle, button, down ? 1u : 0u));
}

public sealed class ToggleNode : BoardNode
{
    internal ToggleNode(Board board, nint handle) : base(board, handle) { }

    public void Usage(ushort usage, bool down) => MimUsbException.Check(Native.ToggleUsage(Handle, usage, down ? 1u : 0u));
}

public sealed class GamepadNode : BoardNode
{
    internal GamepadNode(Board board, nint handle) : base(board, handle) { }

    public void Button(uint button, bool down) => MimUsbException.Check(Native.GamepadButton(Handle, button, down ? 1u : 0u));

    public void Axis(nuint axisIndex, int value) => MimUsbException.Check(Native.GamepadAxis(Handle, axisIndex, value));

    public void Dpad(bool up, bool down, bool right, bool left) =>
        MimUsbException.Check(Native.GamepadDpad(Handle, up ? 1u : 0u, down ? 1u : 0u, right ? 1u : 0u, left ? 1u : 0u));
}

public sealed class TouchscreenNode : BoardNode
{
    internal TouchscreenNode(Board board, nint handle) : base(board, handle) { }

    public void Contact(uint contactId, bool down, int x, int y) =>
        MimUsbException.Check(Native.TouchscreenContact(Handle, contactId, down ? 1u : 0u, x, y, 0));
}

public sealed class TouchpadNode : BoardNode
{
    internal TouchpadNode(Board board, nint handle) : base(board, handle) { }

    public void Contact(uint contactId, bool down, int x, int y) =>
        MimUsbException.Check(Native.TouchpadContact(Handle, contactId, down ? 1u : 0u, x, y, 0));

    public void Button(uint button, bool down) => MimUsbException.Check(Native.TouchpadButton(Handle, button, down ? 1u : 0u));
}

public sealed class PenNode : BoardNode
{
    internal PenNode(Board board, nint handle) : base(board, handle) { }

    public void Update(in PenSample sample) => MimUsbException.Check(Native.PenUpdate(Handle, in sample));
}

public sealed class DigitizerNode : BoardNode
{
    internal DigitizerNode(Board board, nint handle) : base(board, handle) { }

    public void Update(in DigitizerSample sample) => MimUsbException.Check(Native.DigitizerUpdate(Handle, in sample));
}
