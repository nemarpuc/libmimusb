// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
//! A `Board` owns the Device on one serial port and lends out HID Nodes.
//!
//! Nothing is picked for the caller: the port, the baud rate and the USB identity are arguments,
//! and the board is never searched for. Each HID Node gets the next interface number and IN
//! endpoint (`values::hid_placement`), checked against the board's limits. A Node is not closed by
//! itself: closing one re-enumerates the board's USB device (docs/API.md `mimusb_node_close`), so
//! dropping the Board closes the Device, which takes its Nodes with it; the borrow keeps a Node
//! from outliving it.

use crate::values::{self, DeviceConfig, HidPlacement};
use crate::*;
use core::cell::{Cell, RefCell};
use core::marker::PhantomData;
use core::ops::Deref;
use std::ffi::CStr;
use std::time::Duration;

/// Taking the board's USB device off the target drops the reports still queued on the board, so
/// the last ones get this long to leave (the examples wait the same).
pub const FLUSH: Duration = Duration::from_millis(50);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BoardError {
    Usb(Error),
    /// The library is another version than this crate (`mimusb_version`).
    Version(u32),
    /// The board runs firmware of another major or minor version (`firmware_compatible` is 0).
    Firmware { major: u8, minor: u8, patch: u8 },
    /// The board has no interface or endpoint number left for another Node.
    NoRoom,
}

impl From<Error> for BoardError {
    fn from(error: Error) -> Self {
        Self::Usb(error)
    }
}

pub struct Board {
    raw: NonNull<sys::mimusb_device>,
    timeouts: sys::mimusb_commit_timeouts,
    limits: sys::mimusb_board_limits,
    next: Cell<u8>,
    placements: RefCell<Vec<HidPlacement>>,
    _config: DeviceConfig,
}

/// A HID Node lent by a Board; it dereferences to the typed Node reference.
pub struct Node<'b, R> {
    node: R,
    _board: PhantomData<&'b Board>,
}

impl<R> Deref for Node<'_, R> {
    type Target = R;

    fn deref(&self) -> &R {
        &self.node
    }
}

impl Board {
    /// Opens the board on `port` with `config` (see `values::device_config`) and the commit
    /// timeouts of `values::commit_timeouts`.
    pub fn open(port: &CStr, config: DeviceConfig) -> Result<Self, BoardError> {
        let expected = (sys::MIMUSB_VERSION_MAJOR << 24) | (sys::MIMUSB_VERSION_MINOR << 16) | sys::MIMUSB_VERSION_PATCH;
        let found = unsafe { sys::mimusb_version() };
        if found != expected {
            return Err(BoardError::Version(found));
        }
        let mut raw = core::ptr::null_mut();
        result(unsafe { sys::mimusb_device_open(port.as_ptr(), config.as_ptr(), &mut raw) })?;
        let raw = NonNull::new(raw).ok_or(Error(sys::MIMUSB_ERR_INTERNAL))?;
        // From here on the Board's Drop closes the Device on every early return.
        let mut board = Self {
            raw,
            timeouts: values::commit_timeouts(),
            limits: Default::default(),
            next: Cell::new(0),
            placements: RefCell::new(Vec::new()),
            _config: config,
        };
        let mut info: sys::mimusb_device_info = unsafe { core::mem::zeroed() };
        result(unsafe { sys::mimusb_device_board_info(board.raw.as_ptr(), &mut info) })?;
        if info.firmware_compatible == 0 {
            return Err(BoardError::Firmware { major: info.version_major, minor: info.version_minor, patch: info.version_patch });
        }
        result(unsafe { sys::mimusb_device_limits(board.raw.as_ptr(), &mut board.limits) })?;
        Ok(board)
    }

    pub fn as_ptr(&self) -> *mut sys::mimusb_device {
        self.raw.as_ptr()
    }

    pub fn limits(&self) -> &sys::mimusb_board_limits {
        &self.limits
    }

    /// Waits until the target has mounted the board's USB device; open every Node first.
    pub fn wait_ready(&self, timeout: Duration) -> Result<(), Error> {
        result(unsafe { sys::mimusb_device_wait_ready(self.as_ptr(), timeout.as_millis() as u32) })
    }

    fn open_spec<R>(&self, spec: *mut sys::mimusb_spec,
                    typed: impl FnOnce(*mut sys::mimusb_node) -> Option<R>) -> Result<Node<'_, R>, BoardError> {
        let number = self.next.get();
        if u32::from(number) >= self.limits.max_interfaces_any || u32::from(number) + 1 > self.limits.max_endpoint_number {
            return Err(BoardError::NoRoom);
        }
        let placement = values::hid_placement(number);
        let mut raw = core::ptr::null_mut();
        result(unsafe { sys::mimusb_node_open(self.as_ptr(), spec, &placement.placement, &self.timeouts, &mut raw) })?;
        self.next.set(number + 1);
        self.placements.borrow_mut().push(placement);
        let node = typed(raw).ok_or(Error(sys::MIMUSB_ERR_INTERNAL))?;
        Ok(Node { node, _board: PhantomData })
    }
}

macro_rules! open_safe {
    ($method:ident, $spec:ident, $options:ty, $node:ident) => {
        pub fn $method(&self, options: &$options) -> Result<Node<'_, $node>, BoardError> {
            let spec = $spec::create(options)?;
            self.open_spec(spec.as_ptr(), |node| unsafe { $node::from_raw(node) })
        }
    };
}

macro_rules! open_unsafe {
    ($method:ident, $spec:ident, $options:ty, $node:ident) => {
        /// # Safety
        /// Every pointer/count pair and nested C string in `options` must identify readable
        /// storage for the duration of this call (the `values` options always do).
        pub unsafe fn $method(&self, options: &$options) -> Result<Node<'_, $node>, BoardError> {
            let spec = unsafe { $spec::create(options) }?;
            self.open_spec(spec.as_ptr(), |node| unsafe { $node::from_raw(node) })
        }
    };
}

impl Board {
    open_safe!(keyboard, KeyboardSpec, sys::mimusb_keyboard_options, KeyboardNodeRef);
    open_safe!(mouse, MouseSpec, sys::mimusb_mouse_options, MouseNodeRef);
    open_safe!(touchscreen, TouchscreenSpec, sys::mimusb_touchscreen_options, TouchscreenNodeRef);
    open_safe!(touchpad, TouchpadSpec, sys::mimusb_touchpad_options, TouchpadNodeRef);
    open_unsafe!(toggle, ToggleSpec, sys::mimusb_toggle_options, ToggleNodeRef);
    open_unsafe!(gamepad, GamepadSpec, sys::mimusb_gamepad_options, GamepadNodeRef);
    open_unsafe!(pen, PenSpec, sys::mimusb_pen_options, PenNodeRef);
    open_unsafe!(digitizer, DigitizerSpec, sys::mimusb_digitizer_options, DigitizerNodeRef);
}

impl Drop for Board {
    fn drop(&mut self) {
        std::thread::sleep(FLUSH);
        unsafe { sys::mimusb_device_close(self.as_ptr(), self.timeouts.control_timeout_ms, 1000) };
    }
}
