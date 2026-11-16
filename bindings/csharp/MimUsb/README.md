# MimUsb .NET binding

This NuGet package contains only managed code: the literal P/Invoke declarations of the libmimusb
C ABI (`Native`), initial values for them (the `Init()` methods) and an owning `Board`. It does not contain `mimusb.dll` or `libmimusb.so`.
Install the matching libmimusb shared distribution separately and make it available to the .NET
native-library loader under the name `mimusb`.

```csharp
using var board = new Board("/dev/ttyACM0", 115200, UsbIdentity.ForTesting("My keyboard")); // 0x1209:0x0001, for tests only
var keyboard = board.Keyboard();
board.WaitReady(TimeSpan.FromSeconds(5));   // after every Node is open
keyboard.Tap(0xE1);                         // Left Shift down, send, up, send
```

- **Raw layer.** The C header's `*_init` functions only zero a structure and set `struct_size`, by
  decision. `Native` chooses nothing, and the generated `Init()` of a structure the bindings give no
  values (for example `NodeOptions`) returns it zeroed with `StructSize` set.
- **Initial values.** `Init()` of `DeviceConfig`, `CommitTimeouts` and the profile options returns the
  numbers the library's own tests use (`tests/fill_values.h`), each within `docs/LIMITS.md`; a test
  compares them byte for byte with that header. They mean nothing for a product. `DeviceConfig.Init()`
  leaves the baud rate and the USB identity empty: give your own IDs and strings, or name
  `UsbIdentity.ForTesting`. A required field left zero is refused with `Result.UnsetField`.
- **Board.** `Board` takes the port, the baud rate and the identity (nothing is searched for or
  guessed), checks the library and firmware versions and the board's limits, gives each Node the next
  interface number and IN endpoint, and closes the Device on `Dispose` (after 50 ms for the last
  reports to leave). Nodes are not closed one by one: closing a Node re-enumerates the board's USB
  device. Opening a Node is a commit too, so a board enumerates once per Node. `Handle` gives the raw
  handle for `Native` calls. Calls are not thread-safe.

The NuGet package version and native libmimusb version must match; `Board` checks it. Complete native
archives, dependency notices, and SPDX documents are published with the corresponding GitHub Release.

## Callback lifetime

`DeviceConfig.Transport.EventSink` and `LogSink` hold native function pointers,
typically obtained with `Marshal.GetFunctionPointerForDelegate(sink)`. The
garbage collector does not know the native side holds them: keep the delegate
referenced for as long as the Device is open.

## Pointers

Functions that take a pointer to a structure have two forms: one with `in` or
`ref` for the common case, and one taking `nint` where `null` is allowed
(`nint.Zero`). The declarations are generated from `include/mimusb.h` by
`tools/bindings/generate.py`; do not edit `Native.cs` by hand.

## Failed calls

`MimUsbException.Check(result)` does nothing for `Result.Ok` and otherwise throws
with the result and the `Field` and `Reason` of the library's last error. Call
it right after the call that returned `result`.
