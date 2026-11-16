# mimusb Python binding

This wheel contains only Python code: the literal `ctypes` declarations of the libmimusb C ABI
(`mimusb.native`), initial values for them (`mimusb.initial`) and an owning `mimusb.Board`. It
deliberately contains no `.so`, `.dll`, or `.dylib`. Install the matching libmimusb shared
distribution separately, then pass its exact path to `mimusb.load(path)` or `mimusb.Board(path, ...)`.
The binding never guesses a native-library path.

```python
import mimusb

identity = mimusb.UsbIdentity.for_testing("My keyboard")   # 0x1209:0x0001, for tests only
with mimusb.Board("/path/to/libmimusb.so", "/dev/ttyACM0", 115200, identity) as board:
    keyboard = board.keyboard()
    board.wait_ready()                                     # after every Node is open
    keyboard.tap(0xE1)                                     # Left Shift down, send, up, send
```

- **Raw layer.** The C header's `*_init` functions only zero a structure and set `struct_size`, by
  decision, and so does a `ctypes` structure created without positional arguments: set every required
  field yourself. A required field left zero is refused with `MIMUSB_ERR_UNSET_FIELD`.
- **Initial values.** `mimusb.initial` (`keyboard_options()`, `device_config(baud_rate, identity)`, ...) holds the numbers the library's own tests use
  (`tests/fill_values.h`), each within `docs/LIMITS.md`; a test compares them byte for byte with that
  header. They mean nothing for a product. The USB identity has no value: name `UsbIdentity.for_testing`
  or give your own IDs and strings.
- **Board.** `Board` takes the port, the baud rate and the identity (nothing is searched for or
  guessed), checks the library and firmware versions and the board's limits, gives each Node the next
  interface number and IN endpoint, and closes the Device on `close()` (after 50 ms for the last reports
  to leave). Nodes are not closed one by one: closing a Node re-enumerates the board's USB device.
  Opening a Node is a commit too, so a board enumerates once per Node. Every object has `.raw` to drop to
  the raw calls. Calls are not thread-safe.

The Python package version and the native libmimusb version must match. Complete
native archives, dependency notices, and an SPDX SBOM are published with the
corresponding GitHub Release.

The declarations are generated from `include/mimusb.h` by
`tools/bindings/generate.py`; do not edit `mimusb/native.py` by hand.

`mimusb.check(library, result)` returns `result` when it is `MIMUSB_OK` and
otherwise raises `mimusb.Error` carrying the result's name and the `field` and
`reason` of the library's last error. Call it right after the call.
