// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
#ifndef MIMUSB_H
#define MIMUSB_H

/*
 * libmimusb: a microcontroller board, driven over a serial link, presents a USB device (functions
 * the library describes for you, interfaces of any class written out, or descriptors given as
 * bytes) to the computer its USB port is plugged into (the "target"). This header is the whole
 * stable C ABI. It exposes no serial handle and no C++ type.
 *
 * Objects
 *   Device    one board, opened on a serial port. It presents one USB device to the target, whose
 *             identity (descriptor, strings) the Device sets.
 *   Spec      an immutable description of one function, built by a factory
 *             (mimusb_spec_create_*). It needs no board and is reference counted. Every factory
 *             makes the same thing, a Spec, and mimusb_node_open takes any of them:
 *               keyboard, mouse, gamepad, touchpad, touchscreen, pen, digitizer, toggle
 *                         HID functions that keep a state: the library writes the report layout
 *                         and the HID rules, and the input calls (mimusb_keyboard_key, ...)
 *                         change the state it sends;
 *               hid       a HID function of the caller's own report descriptor, which the library
 *                         checks, with reports the caller submits (mimusb_hid_submit);
 *               cdc_acm   a serial port function: the library writes its interfaces, class
 *                         descriptors and rules; the data is the caller's;
 *               interfaces  any class written out interface by interface, with its own rules.
 *             A function of another class is one more factory of the same kind: it makes the
 *             interfaces and the control rules of its class, and never touches the data.
 *   Node      the interfaces one Spec presents to the target. mimusb_node_open opens a Spec at a
 *             placement (interface numbers, the endpoint of each role, the caller's own control
 *             rules), and the caller reads and writes its endpoints
 *             (mimusb_node_endpoint_write, MIMUSB_EVENT_ENDPOINT_DATA). The board runs every
 *             interface the same way, and what a class does on endpoint 0 (descriptors,
 *             GET_REPORT, SET_LINE_CODING, ...) is a list of control rules (mimusb_control_rule):
 *             those the Spec brings and, tried first, those of the placement.
 *             A Device can instead be given whole as bytes, with no Node
 *             (mimusb_device_set_descriptors), to present a copy of a real device.
 *   Discovery a snapshot of the boards found on the serial ports.
 *
 * Opening or closing a Node changes the USB device the board presents, so the board drops it from
 * the target and enumerates it again (a "commit"); mimusb_device_batch_begin and _end gather
 * several changes into one.
 *
 * Rules every function follows
 *   Comments   A function comment starts with a summary and lists Ownership, Blocking and
 *              Synchronization only where they differ from the defaults: nothing is kept or handed
 *              over, the call does not block, and it belongs to the domain of its Device. (Spec
 *              functions, mimusb_version and mimusb_result_name may be called from any thread.)
 *              Returns lists the results particular to the function.
 *   Results    Every call records the details of its result for the calling thread, read with
 *              mimusb_last_error(). Results that come from the serial link (the "link errors") are
 *              MIMUSB_ERR_TIMEOUT, _NO_DEVICE, _SHORT_TRANSFER and _IO; error_detail says the frame
 *              and holds the errno or Win32 error, or the board's refusal. Once the port fails or
 *              is unplugged, calls on the Device return MIMUSB_ERR_NO_DEVICE until it is closed.
 *              Any call can also return MIMUSB_ERR_INTERNAL (an allocation failed or an internal
 *              invariant broke). The Returns lines below list what is particular to each function.
 *   Outputs    A function that creates an object returns it through an out_* pointer, which is
 *              set to null first and stays null when the call fails.
 *   Closing    The *_close, *_destroy and *_release functions follow one rule:
 *                MIMUSB_ERR_PARAM or MIMUSB_ERR_UNSET_FIELD  nothing was done; the handle is still
 *                                                           the caller's.
 *                any other result                           the object is gone and its handle must
 *                                                           not be used again. An error says what
 *                                                           failed on the way out.
 *              Writes to the serial port finish within the call, so nothing is ever left pending.
 *   Threads    A Device and its Nodes form one domain: the caller makes sure no two calls in a
 *              domain run at the same time, unless a function says otherwise. Specs and Discovery
 *              snapshots are outside any domain. The event and log sinks run on the library's
 *              reader thread or on the calling thread; they must not call back into the library,
 *              except mimusb_device_control_reply from the event sink.
 *   Timeouts   The library keeps no timeout: every call that can wait takes its timeouts in
 *              milliseconds as arguments (mimusb_commit_timeouts for a commit). A timeout of zero
 *              is refused with MIMUSB_ERR_UNSET_FIELD before anything is sent.
 *   Structures Each options structure starts with struct_size (its sizeof) and reserved (zero).
 *              Its *_init function sets struct_size and zeroes the rest; it fills in no value.
 *              A required field that is zero is refused, and every enumeration starts at 1, so a
 *              field left zero never selects something by accident. A flag (enable_*, down, ...)
 *              is exactly 0 or 1.
 *
 * Specifications cited: USB 2.0, HID 1.11 and HID Usage Tables 1.7 (HUT), all from usb.org.
 * docs/API.md is the guide to this header; docs/PROTOCOL.md describes the serial link.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#if defined(MIMUSB_BUILDING_LIBRARY)
#define MIMUSB_API __declspec(dllexport)
#elif defined(MIMUSB_SHARED)
#define MIMUSB_API __declspec(dllimport)
#else
#define MIMUSB_API
#endif
#define MIMUSB_CALL __cdecl
#else
#define MIMUSB_API __attribute__((visibility("default")))
#define MIMUSB_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MIMUSB_VERSION_MAJOR 0
#define MIMUSB_VERSION_MINOR 7
#define MIMUSB_VERSION_PATCH 4
/* The largest report the library builds, whatever a board holds. */
#define MIMUSB_HOST_MAX_REPORT_BYTES 1024

typedef struct mimusb_discovery mimusb_discovery;
typedef struct mimusb_device mimusb_device;
typedef struct mimusb_spec mimusb_spec;
typedef struct mimusb_node mimusb_node;

/* ---- Constants --------------------------------------------------------------------------
 * Every enumeration is an int32_t typedef with anonymous constants, so structure layout does not
 * depend on how a compiler sizes an enum. */

/* A result libaoahid also has keeps the same number there. 5, 9 and 15 are not used. */
typedef int32_t mimusb_result;
enum {
    MIMUSB_OK = 0,
    MIMUSB_ERR_PARAM = 1,       /* a null pointer, a bad value or a handle in the wrong state */
    MIMUSB_ERR_UNSET_FIELD = 2, /* a required field or timeout is zero */
    MIMUSB_ERR_UNSUPPORTED = 3, /* not possible with these arguments or this board */
    MIMUSB_ERR_PROTOCOL = 4,    /* the board's firmware has another major or minor version */
    MIMUSB_ERR_ACCESS = 6,      /* no permission to open the serial port */
    MIMUSB_ERR_BUSY = 7,        /* in use: the port, a batch, an unsent edge, ... */
    MIMUSB_ERR_NO_DEVICE = 8,   /* the port failed or was unplugged; this stays until close */
    MIMUSB_ERR_TIMEOUT = 10,
    MIMUSB_ERR_SHORT_TRANSFER = 11,      /* a write moved fewer bytes than asked */
    MIMUSB_ERR_DESCRIPTOR_REJECTED = 12, /* the board refused the configuration */
    MIMUSB_ERR_IO = 13,
    MIMUSB_ERR_OVERFLOW = 14, /* a size, count or limit was exceeded */
    MIMUSB_ERR_INTERNAL = 16
};

typedef int32_t mimusb_log_level;
enum { MIMUSB_LOG_DISABLED = 1, MIMUSB_LOG_ERROR = 2, MIMUSB_LOG_INFO = 3, MIMUSB_LOG_TRACE = 4 };

/* The Application Collection of each profile is in docs/PROFILES.md. */
typedef int32_t mimusb_profile_kind;
enum {
    MIMUSB_PROFILE_KEYBOARD = 1,
    MIMUSB_PROFILE_MOUSE = 2,
    MIMUSB_PROFILE_TOUCHSCREEN = 3,
    MIMUSB_PROFILE_TOUCHPAD = 4,
    MIMUSB_PROFILE_PEN = 5,
    MIMUSB_PROFILE_DIGITIZER = 6,
    MIMUSB_PROFILE_GAMEPAD = 7,
    MIMUSB_PROFILE_TOGGLE = 8,      /* one-bit controls on a page of the caller's choice */
    MIMUSB_PROFILE_HID = 9,         /* mimusb_spec_create_hid: the caller's own report descriptor */
    MIMUSB_PROFILE_INTERFACES = 10, /* mimusb_spec_create_interfaces: any class */
    MIMUSB_PROFILE_CDC_ACM = 11     /* mimusb_spec_create_cdc_acm */
};

typedef int32_t mimusb_axis_role;
enum {
    MIMUSB_AXIS_X = 1,
    MIMUSB_AXIS_Y = 2,
    MIMUSB_AXIS_Z = 3,
    MIMUSB_AXIS_RX = 4,
    MIMUSB_AXIS_RY = 5,
    MIMUSB_AXIS_RZ = 6,
    MIMUSB_AXIS_SLIDER = 7,
    MIMUSB_AXIS_SIMULATION_ACCELERATOR = 8,
    MIMUSB_AXIS_SIMULATION_BRAKE = 9,
    MIMUSB_AXIS_SIMULATION_STEERING = 10,
    MIMUSB_AXIS_DIAL = 11,
    MIMUSB_AXIS_WHEEL = 12,
    MIMUSB_AXIS_SIMULATION_RUDDER = 13,
    MIMUSB_AXIS_SIMULATION_THROTTLE = 14
};

typedef int32_t mimusb_dpad_representation;
enum {
    MIMUSB_DPAD_NONE = 1,
    MIMUSB_DPAD_HAT = 2,    /* one eight-way Hat Switch */
    MIMUSB_DPAD_BUTTONS = 3 /* the four Generic Desktop D-pad Usages, one bit each */
};

/* The HUT control type of one Toggle Usage; it decides the descriptor encoding. Only the first six
 * can be pressed and released: a factory refuses the others with MIMUSB_ERR_UNSUPPORTED. */
typedef int32_t mimusb_usage_semantic;
enum {
    MIMUSB_USAGE_SELECTOR_BITMAP = 1,
    MIMUSB_USAGE_ON_OFF_TOGGLE = 2,
    MIMUSB_USAGE_ON_OFF_MAINTAINED = 3,
    MIMUSB_USAGE_MOMENTARY = 4,
    MIMUSB_USAGE_ONE_SHOT = 5,
    MIMUSB_USAGE_RETRIGGER = 6,
    MIMUSB_USAGE_ON_OFF_PAIR = 7,
    MIMUSB_USAGE_LINEAR = 8,
    MIMUSB_USAGE_DYNAMIC_VALUE = 9,
    MIMUSB_USAGE_NAMED_ARRAY = 10
};

/* ---- Diagnostics ------------------------------------------------------------------------ */

/* Called with one formatted line per event (see "Threads" above). */
typedef void(MIMUSB_CALL* mimusb_log_sink)(void* user, mimusb_log_level level, const char* message);

/* The details of the calling thread's last result. Fields that do not apply are zero or null. */
typedef struct mimusb_error_detail {
    mimusb_result code;
    const char* field;  /* the argument, option or step concerned, e.g. "node.close" */
    const char* reason; /* one English sentence */
    /* errno or the Win32 error of a failed serial call; or, when the board refused a frame, its
     * code: 1 bad value, 2 no room, 3 wrong state, 4 descriptor refused, 5 unsupported, 6 busy */
    int32_t transport_status;
    int32_t frame_type; /* the serial frame involved; mimusb_frame_type_name names it */
    uint16_t node_id;   /* the node id of the Node involved (mimusb_node_id) */
    uint16_t report_id;
    uint32_t offset;
    uint32_t length;
} mimusb_error_detail;

/* ---- Events -----------------------------------------------------------------------------
 * What the board reports about the target. Delivered to transport.event_sink (see "Threads"
 * above); data is valid only during the call, so copy what you keep. */

typedef int32_t mimusb_event_kind;
enum {
    MIMUSB_EVENT_MOUNTED = 1, /* the target configured the board's USB device */
    MIMUSB_EVENT_UNMOUNTED = 2,
    MIMUSB_EVENT_SUSPEND = 3, /* value: 1 when the target enabled remote wakeup */
    MIMUSB_EVENT_RESUME = 4,
    /* The target sent an Output report (a keyboard's LEDs, for example) or a Feature report to a
     * HID Node (mimusb_node_open), on its OUT endpoint or with SET_REPORT; and set the
     * protocol or the idle rate of its interface. The library makes these events from the
     * control rules and the packets it wrote for the Node (they are not CONTROL_SETUP events). */
    MIMUSB_EVENT_OUTPUT_REPORT = 5,
    MIMUSB_EVENT_FEATURE_REPORT = 6,
    MIMUSB_EVENT_SET_PROTOCOL = 7, /* value: 0 boot protocol, 1 report protocol */
    MIMUSB_EVENT_SET_IDLE = 8,     /* value: the idle rate in 4 ms units */
    /* A control transfer of the target on endpoint 0 that a rule of the caller reports (an ASK or
     * an ACCEPT rule, or every transfer: see mimusb_device_set_control_rules).
     * data: the 8 setup bytes (bmRequestType, bRequest, wValue, wIndex, wLength; little endian). */
    MIMUSB_EVENT_CONTROL_SETUP = 9,
    /* The data stage of a held host-to-device transfer, after the answer accepted it. The board
     * waits for a second answer, the verdict, with the same control_token. */
    MIMUSB_EVENT_CONTROL_DATA = 10,
    /* The board's queue of an IN endpoint was full and it dropped transfers: value is how many
     * since the last event of this kind, at most 65535; endpoint_address names the endpoint. On a
     * HID Node a key-up may be among them: send the current state again. With
     * MIMUSB_FULL_DROP_OLDEST the dropped ones are the oldest. Only after
     * mimusb_device_set_loss_reporting. */
    MIMUSB_EVENT_TRANSFER_LOST = 11,
    /* The target sent a packet on an OUT endpoint of a Node of a mimusb_spec_create_interfaces
     * Spec, or of a Device given as bytes (node_id 0; mimusb_device_set_descriptors): data and
     * length are the packet as it came (a zero-length packet has length 0); endpoint_address names
     * the endpoint and interface_number its interface. */
    MIMUSB_EVENT_ENDPOINT_DATA = 12,
    /* The target set (value 1) or cleared (value 0) the Halt feature of an endpoint of a Node
     * of a mimusb_spec_create_interfaces Spec (USB 2.0 9.4.1, 9.4.5); endpoint_address names it. */
    MIMUSB_EVENT_ENDPOINT_HALT = 13,
    /* The target selected an alternate setting (USB 2.0 9.4.10) of an interface of a Device given
     * as bytes: interface_number is the interface and value the setting. Sent also when that
     * setting was already selected. */
    MIMUSB_EVENT_INTERFACE_ALTERNATE = 14
};

/* mimusb_event.control_flags */
enum {
    MIMUSB_CONTROL_FLAG_HELD = 0x01,     /* the board waits for mimusb_device_control_reply */
    MIMUSB_CONTROL_FLAG_TOO_LONG = 0x02, /* a data stage longer than the board takes; not held */
    MIMUSB_CONTROL_FLAG_FIXED = 0x04,    /* SET_ADDRESS or SET_CONFIGURATION, which the board's own
                                            USB state needs; not held */
    MIMUSB_CONTROL_FLAG_LOST = 0x08, /* transfers before this one were not reported: queue full */
    MIMUSB_CONTROL_FLAG_ACCEPTED = 0x10 /* an ACCEPT rule took it; nothing is awaited */
};

typedef struct mimusb_event {
    uint32_t struct_size;
    uint32_t reserved;
    mimusb_event_kind kind;
    uint16_t node_id; /* the Node the event belongs to, or 0 for the whole device */
    uint16_t report_id;
    uint32_t value;
    const uint8_t* data; /* OUTPUT_REPORT and FEATURE_REPORT: the report without its Report ID */
    size_t length;
    uint32_t control_token;    /* CONTROL_SETUP and CONTROL_DATA: 1 to 255 */
    uint32_t control_flags;    /* MIMUSB_CONTROL_FLAG_* */
    uint32_t endpoint_address; /* TRANSFER_LOST, ENDPOINT_DATA, ENDPOINT_HALT: 0x81 to 0x8F or 0x01
                                  to 0x0F; otherwise 0 */
    uint32_t interface_number; /* ENDPOINT_DATA, INTERFACE_ALTERNATE and the HID events: the
                                  bInterfaceNumber the event belongs to; otherwise 0 */
} mimusb_event;

typedef void(MIMUSB_CALL* mimusb_event_sink)(void* user, const mimusb_event* event);

/* ---- Boards -----------------------------------------------------------------------------*/

/* The rate a board starts at, and returns to on a UART BREAK: the one rate two parties that have
 * never spoken share. Any other rate from here to the board's max_baud can be agreed; there is
 * no table of them. */
#define MIMUSB_BOOT_BAUD 115200u
/* transport.baud_rate: measure the fastest rate this port, adapter and board carry now (what
 * mimusb_discover does), and use it. The rate is in mimusb_device_info.baud_rate afterwards. */
#define MIMUSB_BAUD_FASTEST 4294967295u

/* mimusb_device_info.uart_flags */
#define MIMUSB_UART_FLAG_BREAK 0x01u /* the board returns to the boot rate on a BREAK */

/* mimusb_device_info.chip */
#define MIMUSB_CHIP_UNKNOWN 0
#define MIMUSB_CHIP_RP2040 1
#define MIMUSB_CHIP_RP2350 2
#define MIMUSB_CHIP_ESP32S3 3
#define MIMUSB_CHIP_NRF52840 4
#define MIMUSB_CHIP_STM32F4 5

/* A board found by mimusb_discover, or the one a Device has open. */
typedef struct mimusb_device_info {
    char port_name[64];    /* "/dev/ttyUSB0", "/dev/cu.usbserial-0001", "COM3" */
    char description[64];  /* the serial adapter's USB product string, where the OS gives one */
    uint8_t version_major; /* firmware version */
    uint8_t version_minor;
    uint8_t version_patch;
    uint8_t is_ready; /* 1 when the target has mounted the board's USB device */
    uint8_t
        firmware_compatible;  /* 1 when the firmware's major and minor version are this library's */
    uint8_t max_interfaces;   /* interfaces the board provides, of any class
                                 (mimusb_board_limits.max_interfaces_any) */
    uint8_t max_report_bytes; /* at most 255 here; mimusb_device_limits has the exact figure */
    uint16_t max_descriptor_bytes; /* the board's answer memory: the bytes of every ANSWER rule
                                      together (mimusb_board_limits.max_answer_bytes) */
    uint8_t has_unique_id;         /* 1 when the chip has a serial number of its own */
    uint8_t max_endpoint_number;   /* the highest endpoint number the board's USB controller has */
    /* discover: the fastest rate that carried a burst of test frames without loss on this port
     * (the board is left on MIMUSB_BOOT_BAUD); a Device: the rate the link runs at */
    uint32_t baud_rate;
    uint32_t max_baud;        /* the fastest rate the board's UART can make */
    uint32_t uart_flags;      /* MIMUSB_UART_FLAG_* */
    uint8_t chip;             /* MIMUSB_CHIP_* */
    uint8_t usb_high_speed;   /* 1 when the board's USB device runs at high speed */
    uint8_t unique_id_length; /* bytes of unique_id in use */
    uint8_t unique_id[16];
    uint8_t reserved_end[3];
} mimusb_device_info;

/* What the board holds, in bytes: 64-byte endpoint packets on a full-speed chip, 1024 on a
 * high-speed one. The data stage of a control transfer may be longer than a packet. */
typedef struct mimusb_board_limits {
    uint32_t struct_size;
    uint32_t reserved;
    uint32_t max_report_bytes;       /* largest report, Report ID included */
    uint32_t max_feature_bytes;      /* the data stage of a control transfer, as
                                        max_control_data_bytes: a Feature report answer fits */
    uint32_t max_endpoint_bytes;     /* largest ep_in and ep_out max_packet_size */
    uint32_t max_frame_payload;      /* largest serial frame payload the board takes */
    uint32_t max_control_data_bytes; /* largest data stage the host may read or answer */
    uint32_t max_control_rules;      /* control rules the board holds: those of every Node and of
                                        mimusb_device_set_control_rules together */
    uint32_t max_endpoint_number;    /* highest endpoint number of an endpoint's address */
    uint32_t max_interfaces_any;     /* interfaces of every class together, HID ones included */
    uint32_t max_answer_bytes;       /* the bytes of every ANSWER control rule together */
    uint32_t max_config_descriptor_bytes; /* the whole Configuration descriptor */
    uint32_t max_strings;                 /* string descriptors, the four of the Device included */
    uint32_t max_transfer_bytes;   /* largest payload of one frame to an endpoint of a Node opened
                                      with mimusb_node_open; a longer write is split */
    uint32_t endpoint_queue_slots; /* slots the queues of those endpoints are cut from: a queue
                                      of depth d takes d + 1 (MIMUSB_FULL_DROP_NEWEST and
                                      MIMUSB_FULL_WAIT) or 2 * d + 1 (MIMUSB_FULL_DROP_OLDEST) */
    uint32_t transfer_types;       /* bit n set: the board has endpoints of transfer type n
                                      (MIMUSB_TRANSFER_ISOCHRONOUS, MIMUSB_TRANSFER_BULK,
                                      MIMUSB_TRANSFER_INTERRUPT). A board with isochronous
                                      endpoints takes packets of them up to max_transfer_bytes
                                      (1023 at full speed, 1024 at high speed at most) */
    uint32_t max_bos_bytes;        /* the largest BOS descriptor, its header included */
} mimusb_board_limits;

/* What the board counted on its serial line since the last reset, each saturating at 65535. A
 * counter that only latches in the hardware counts once per look, so a count is a lower bound and
 * zero is exact. */
typedef struct mimusb_link_counters {
    uint32_t struct_size;
    uint32_t reserved;
    uint32_t bad_crc;      /* frames with a wrong CRC */
    uint32_t framing;      /* UART framing errors (not BREAKs) */
    uint32_t uart_overrun; /* bytes the UART lost because nobody read it in time */
    uint32_t ring_overrun; /* reads that found the receive ring three quarters full or more */
    uint32_t probes_good;  /* link-test frames received intact (only the search sends them) */
} mimusb_link_counters;

/* ---- USB identity and Device options ----------------------------------------------------
 * The fields are those of the USB 2.0 standard descriptors (section 9.6) and the HID class
 * descriptor (HID 1.11 section 6.2.1). A string is UTF-8 text without control characters (C0, DEL,
 * C1) that makes at most 126 UTF-16 code units (USB 2.0 9.6.7: a string descriptor is UTF-16LE and
 * 254 bytes long at most; U+10000 and above take two), or null to leave it out. language_id says
 * which language the strings are in; nothing is inferred from the text. */

/* The Device descriptor and its strings. bcd_usb, ep0_max_packet_size and language_id are
 * required (zero is refused), and so is vendor_id: zero is taken for a field that was never set (an
 * identity is the caller's, and mimusb_device_config_init fills nothing in). product_id may be
 * any value, zero included. A host asks for the BOS descriptor only when bcd_usb is 0x0201 or more
 * (USB 2.0 Link Power Management ECN; USB 3.2 9.6.2): give one with mimusb_device_set_bos or
 * mimusb_device_set_descriptors, or the request is stalled.
 * While the Device is given as bytes (mimusb_device_set_descriptors) this structure, usb_config
 * and the four strings it points to are not used: the bytes are the Device descriptor. */
typedef struct mimusb_usb_device {
    uint16_t bcd_usb;        /* e.g. 0x0200; 0x0201 or more: a BOS descriptor */
    uint8_t device_class;    /* 0: each interface says its own class */
    uint8_t device_subclass; /* 0 when device_class is 0 (USB 2.0 9.6.1) */
    uint8_t device_protocol;
    uint8_t ep0_max_packet_size; /* 8, 16, 32 or 64; 64 on a high-speed board (USB 2.0 9.6.1) */
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t bcd_device;
    uint16_t language_id; /* the LANGID of the strings, e.g. 0x0409 (English, United States) */
    const char* manufacturer;
    const char* product;
    const char* serial;
    uint32_t serial_from_chip_id; /* 1 with a null serial: the chip's unique id */
} mimusb_usb_device;

/* The Configuration descriptor. Not used while the Device is given as bytes
 * (mimusb_device_set_descriptors). */
typedef struct mimusb_usb_config {
    uint8_t configuration_value; /* 1: the board has one configuration, selected by that value */
    uint8_t bm_attributes;       /* bit 7 set; bit 6 self-powered; bit 5 remote wakeup; bits 4..0
                                    zero (USB 2.0 9.6.3) */
    uint8_t bmax_power;          /* 0 to 250, in 2 mA units */
    uint8_t reserved8;
    const char* string;
} mimusb_usb_config;

/* The Linux HID parser limits of the target (docs/LIMITS.md). A HID Spec that exceeds one is
 * refused by mimusb_node_open with MIMUSB_ERR_OVERFLOW. All are required to open a Node of a HID
 * Spec (MIMUSB_ERR_UNSET_FIELD when one is 0); a Device that opens no such Node leaves them 0. */
typedef struct mimusb_linux_hid_policy {
    uint32_t descriptor_bytes;   /* largest report descriptor */
    uint32_t fields_per_report;  /* fields in one report */
    uint32_t global_stack_depth; /* Push items in effect at once */
    uint32_t report_size_bits;   /* largest Report Size item */
    uint64_t usages;             /* Usages in one descriptor */
    uint64_t report_data_bits;   /* largest report, in bits */
} mimusb_linux_hid_policy;

/* What the target machine limits, whatever the function: one member for each kind of target
 * limit a Spec can exceed. The library checks a Spec against these when its Node is opened. */
typedef struct mimusb_target_limits {
    mimusb_linux_hid_policy linux_hid;
} mimusb_target_limits;

/* The serial link and what the library does on it. */
typedef struct mimusb_transport_options {
    /* The line rate: MIMUSB_BOOT_BAUD, any rate up to the board's max_baud (a rate
     * mimusb_discover reported is known to work), or MIMUSB_BAUD_FASTEST. */
    uint32_t baud_rate;
    /* Each wait for an answer while mimusb_device_open looks for the board: a few frames at the
     * boot rate plus the adapter's latency timer. */
    uint32_t handshake_timeout_ms;
    /* The largest report this Device sends, Report ID included: at most the board's
     * max_report_bytes and MIMUSB_HOST_MAX_REPORT_BYTES. */
    uint32_t maximum_report_bytes;
    uint32_t validate_reports; /* 1 checks every built report against its Spec before sending */
    /* 1 leaves the board's USB device on the target after mimusb_device_close */
    uint32_t keep_attached;
    mimusb_event_sink event_sink; /* may be null */
    void* event_user;
    mimusb_log_level log_level; /* anything but MIMUSB_LOG_DISABLED needs log_sink */
    mimusb_log_sink log_sink;
    void* log_user;
} mimusb_transport_options;

typedef struct mimusb_device_config {
    uint32_t struct_size;
    uint32_t reserved;
    mimusb_transport_options transport;
    mimusb_target_limits target;
    mimusb_usb_device usb_device;
    mimusb_usb_config usb_config;
} mimusb_device_config;

/* The waits of a call that configures the board and enumerates its USB device again (a commit). */
typedef struct mimusb_commit_timeouts {
    uint32_t struct_size;
    uint32_t reserved;
    uint32_t control_timeout_ms;   /* each configuration frame waits this long for the board's
                                      answer, and is sent up to three times */
    uint32_t reconnect_timeout_ms; /* the board's new enumeration */
} mimusb_commit_timeouts;

/* ---- Node options ----------------------------------------------------------------------- */

/* Transfer types of an endpoint: the values of bmAttributes bits 1..0 (USB 2.0 9.6.6). The board
 * has no control endpoints other than endpoint 0. Isochronous endpoints exist only on a board
 * whose mimusb_board_limits.transfer_types has bit MIMUSB_TRANSFER_ISOCHRONOUS, and only in a
 * setting of an interface other than setting 0, where they carry no bandwidth until the target
 * selects the setting (USB 2.0 5.6.3). */
enum { MIMUSB_TRANSFER_ISOCHRONOUS = 1, MIMUSB_TRANSFER_BULK = 2, MIMUSB_TRANSFER_INTERRUPT = 3 };

/* What an IN endpoint does when `depth` transfers wait on the board for the target to read them.
 * Both fields are required on every IN endpoint (MIMUSB_ERR_UNSET_FIELD when 0) and zero on an OUT
 * endpoint (MIMUSB_ERR_PARAM otherwise). */
enum {
    MIMUSB_FULL_DROP_NEWEST = 1, /* the transfer that arrives is dropped: for state reports, where
                                    late is useless */
    MIMUSB_FULL_DROP_OLDEST = 2, /* the oldest transfer that is not being sent is dropped */
    MIMUSB_FULL_WAIT = 3         /* nothing is dropped: mimusb_node_endpoint_write blocks while
                                    `depth` transfers are unfinished */
};

typedef struct mimusb_endpoint_policy {
    uint8_t on_full; /* MIMUSB_FULL_* */
    uint8_t depth;   /* transfers that may wait, 1 to 255; the board's pool bounds it
                        (mimusb_board_limits.endpoint_queue_slots) */
} mimusb_endpoint_policy;

/* Synchronization type and usage type of an isochronous endpoint: bmAttributes bits 3..2 and 5..4
 * (USB 2.0 Table 9-13). Both are zero on an endpoint of any other transfer type. */
enum {
    MIMUSB_ISO_SYNC_NONE = 0,
    MIMUSB_ISO_SYNC_ASYNCHRONOUS = 1,
    MIMUSB_ISO_SYNC_ADAPTIVE = 2,
    MIMUSB_ISO_SYNC_SYNCHRONOUS = 3
};
enum {
    MIMUSB_ISO_USAGE_DATA = 0,
    MIMUSB_ISO_USAGE_FEEDBACK = 1,
    MIMUSB_ISO_USAGE_IMPLICIT_FEEDBACK = 2
};

/* One endpoint of a setting (USB 2.0 9.6.6). The library writes the Endpoint descriptor from these
 * fields: 7 bytes, or 9 with audio_form 1 (bRefresh and bSynchAddress, the form of the audio and
 * MIDI streaming classes: USB Audio 1.0 4.6.1.1, USB MIDI 1.0 6.2.1). */
typedef struct mimusb_usb_endpoint {
    uint8_t address; /* IN: 0x81 to 0x8F; OUT: 0x01 to 0x0F: bit 7 the direction, bits 6..4 zero,
                        endpoint number 1 to 15 (USB 2.0 9.6.6); 0 for "no endpoint" where an
                        endpoint is optional */
    uint8_t transfer_type; /* MIMUSB_TRANSFER_ISOCHRONOUS, _BULK or _INTERRUPT */
    uint8_t interval;      /* bInterval as it goes on the bus (USB 2.0 9.6.6): interrupt endpoints
                              1 to 255 (frames) at full speed, 1 to 16 (an exponent) at high
                              speed; isochronous 1 at full speed, 1 to 16 at high speed; for bulk
                              it is sent as given */
    uint8_t audio_form;    /* 0: a 7-byte descriptor; 1: 9 bytes, with refresh and synch_address */
    uint16_t max_packet_size; /* 1 to the board's max_endpoint_bytes; bulk: 8, 16, 32 or 64 at full
                                 speed, 512 at high speed (USB 2.0 5.8.3); isochronous: at most
                                 1023 at full speed, 1024 at high speed, and the board's
                                 max_transfer_bytes */
    uint8_t refresh;          /* audio_form 1: bRefresh; otherwise zero */
    uint8_t synch_address;    /* audio_form 1: bSynchAddress; otherwise zero */
    uint8_t sync_type;        /* MIMUSB_ISO_SYNC_*; isochronous only */
    uint8_t usage_type;       /* MIMUSB_ISO_USAGE_*; isochronous only */
    mimusb_endpoint_policy policy; /* IN endpoints; all zero on an OUT endpoint */
} mimusb_usb_endpoint;

/* What a CONTROL_REPLY says (mimusb_device_control_reply) and what an ASK rule does when no reply
 * came in time (STALL or DROP). */
typedef int32_t mimusb_control_action;
enum {
    MIMUSB_CONTROL_ACTION_LEAVE = 1,  /* the board's own handling, as if it had not asked */
    MIMUSB_CONTROL_ACTION_ANSWER = 2, /* data (device to host) or an acknowledgement */
    MIMUSB_CONTROL_ACTION_STALL = 3,
    MIMUSB_CONTROL_ACTION_DROP = 4 /* no answer and no STALL, until the target gives up */
};

#define MIMUSB_CONTROL_MAX_TIMEOUT_MS 5000 /* USB 2.0 9.2.6.1: 5 seconds for any command */

/* What a rule does with the request it matches. */
typedef int32_t mimusb_control_rule_action;
enum {
    /* Returns `data` (cut to wLength), straight from the board's memory. Device to host requests
     * only. mimusb_node_set_rule_data / mimusb_device_set_rule_data change the bytes later, to
     * at most the length given when the rules were set. */
    MIMUSB_CONTROL_ANSWER = 1,
    /* Returns the last transfer the board started on IN endpoint endpoint_address (with
     * first_byte_filter 1: the last one whose first byte is first_byte), cut to wLength; stalls
     * when none was started since the configuration was committed. The transfer is kept by the
     * board as it sends it, so nothing is copied on the way to the target. Device to host only. */
    MIMUSB_CONTROL_ANSWER_LAST_SENT = 2,
    /* Takes the data stage (up to max_control_data_bytes) and completes the transfer; the program
     * hears of it as MIMUSB_EVENT_CONTROL_SETUP (flag ACCEPTED) and, when there was a data stage,
     * MIMUSB_EVENT_CONTROL_DATA. Nothing is awaited. */
    MIMUSB_CONTROL_ACCEPT = 3,
    /* Holds the transfer and asks the program (CONTROL_SETUP with flag HELD), which answers with
     * mimusb_device_control_reply within timeout_ms; after it the board does when_late. */
    MIMUSB_CONTROL_ASK = 4,
    MIMUSB_CONTROL_STALL = 5
};

/* A request matches when (field & mask) == value for each of the four fields; a mask of 0 matches
 * anything. Rules are tried in order and the first match decides. */
typedef struct mimusb_control_rule {
    uint8_t request_type_mask; /* bmRequestType: direction, type and recipient */
    uint8_t request_type_value;
    uint8_t request_mask; /* bRequest */
    uint8_t request_value;
    uint16_t value_mask; /* wValue */
    uint16_t value_value;
    uint16_t index_mask; /* wIndex */
    uint16_t index_value;
    mimusb_control_rule_action action; /* required */
    /* ANSWER: the bytes (copied when the rule is given; at most the board's max_answer_bytes for
     * all rules together) */
    const uint8_t* data;
    size_t length;
    /* ANSWER_LAST_SENT: an IN endpoint address (0x81 to 0x8F), and the filter on the first byte
     * (0 or 1; a Report ID is the first byte of a report on an interface that has them) */
    uint8_t endpoint_address;
    uint8_t first_byte_filter;
    uint8_t first_byte;
    uint8_t reserved8;
    /* ASK: 1 to MIMUSB_CONTROL_MAX_TIMEOUT_MS: how long the board waits for each answer. USB 2.0
     * allows 5 s for a whole control transfer (9.2.6.1) and 500 ms for the first data packet of
     * a standard request (9.2.6.4). */
    uint32_t timeout_ms;
    mimusb_control_action when_late; /* ASK: MIMUSB_CONTROL_ACTION_STALL or _DROP */
} mimusb_control_rule;

/* What an item of a setting is. A setting is a sequence of items in the order they go on the bus:
 * an endpoint (the library writes its Endpoint descriptor) or a descriptor given as bytes (a class
 * descriptor: a CDC functional descriptor, the HID descriptor, an audio or video class descriptor,
 * a class-specific endpoint descriptor after the endpoint it belongs to). */
enum { MIMUSB_ITEM_ENDPOINT = 1, MIMUSB_ITEM_DESCRIPTOR = 2 };

/* One item. kind MIMUSB_ITEM_ENDPOINT uses `endpoint` and leaves the other fields zero; kind
 * MIMUSB_ITEM_DESCRIPTOR uses `descriptor` and `descriptor_length` (one whole descriptor: bLength
 * at least 2 and equal to descriptor_length, the type not 4 or 5, which the library writes itself)
 * and leaves `endpoint` all zero. */
typedef struct mimusb_usb_item {
    uint32_t kind;     /* MIMUSB_ITEM_* */
    uint32_t reserved; /* zero */
    mimusb_usb_endpoint endpoint;
    const uint8_t* descriptor;
    size_t descriptor_length;
} mimusb_usb_item;

/* One alternate setting of an interface (USB 2.0 9.6.5): the Interface descriptor with its
 * bAlternateSetting, then the items in order. */
typedef struct mimusb_usb_setting {
    const mimusb_usb_item* items;
    size_t item_count; /* may be 0: a setting with no endpoints and no class descriptors */
} mimusb_usb_setting;

/* One interface of a Node, as the target sees it (USB 2.0 9.6.5). The library puts class_code,
 * subclass and protocol in the Interface descriptor of every setting and does not read them, except
 * that a class_code of 0 is MIMUSB_ERR_UNSET_FIELD. `number` is the bInterfaceNumber, never
 * renumbered: over all Nodes the numbers in use are 0, 1, 2, ... without a gap (MIMUSB_ERR_PARAM
 * otherwise, the missing number in mimusb_error_detail.offset). `settings` are bAlternateSetting
 * 0, 1, 2, ..., at least one. An endpoint address may appear in several settings of one
 * interface, in no other interface. A string index inside a descriptor comes from
 * mimusb_device_add_string. name, if not null, becomes the interface's string. */
typedef struct mimusb_usb_interface {
    uint8_t number;
    uint8_t class_code; /* required, never 0 */
    uint8_t subclass;
    uint8_t protocol;
    const char* name;
    const mimusb_usb_setting* settings;
    size_t setting_count; /* 1 or more */
} mimusb_usb_interface;

/* The interfaces of one Node. An interface association descriptor (USB-IF Interface Association
 * Descriptor ECN) is given whole: 8 bytes, type 0x0B, whose bFirstInterface and bInterfaceCount
 * name this Node's interfaces, which must then have consecutive numbers. It goes before the first
 * of them. Endpoint addresses are unique across the device. */
typedef struct mimusb_node_config {
    uint32_t struct_size;
    uint32_t reserved;
    const mimusb_usb_interface* interfaces;
    size_t interface_count;
    const uint8_t* association; /* null for none */
    size_t association_length;
    /* What the board does with the Node's control requests, as mimusb_control_rule says; copied.
     * They are tried before those of mimusb_device_set_control_rules, in the order of the Nodes
     * and then of this array. */
    const struct mimusb_control_rule* rules;
    size_t rule_count;
} mimusb_node_config;

/* What a Spec says about the endpoints it needs: each is a role, and the caller gives the address,
 * interval, packet size and policy of every role in the Node's placement. The roles of a Spec are
 * listed by mimusb_spec_roles and, per kind, in docs/API.md. A role is named for the kind of Spec
 * that has it; a Spec of mimusb_spec_create_interfaces has none. */
typedef int32_t mimusb_endpoint_role;
enum {
    MIMUSB_ROLE_HID_IN = 1,      /* a HID Spec's interrupt IN endpoint: required, with a policy */
    MIMUSB_ROLE_HID_OUT = 2,     /* its interrupt OUT endpoint: optional */
    MIMUSB_ROLE_CDC_NOTIFY = 3,  /* a CDC-ACM Spec's interrupt IN endpoint for notifications:
                                    required, with a policy */
    MIMUSB_ROLE_CDC_DATA_IN = 4, /* its bulk IN endpoint: required, with a policy */
    MIMUSB_ROLE_CDC_DATA_OUT = 5 /* its bulk OUT endpoint: required */
};

/* A control rule a Spec brings that a program may change later, named so that its place among
 * the Node's rules is not a number the program has to know (mimusb_node_find_rule). */
typedef int32_t mimusb_rule_slot;
enum {
    MIMUSB_RULE_SLOT_CDC_LINE_CODING = 1 /* a CDC-ACM Spec's answer to GET_LINE_CODING (7 bytes) */
};

/* One role of a Spec and whether the placement must give it. */
typedef struct mimusb_role_info {
    mimusb_endpoint_role role;
    uint32_t required; /* 1: required; 0: optional */
} mimusb_role_info;

/* The endpoint the caller puts in one role of a Spec. */
typedef struct mimusb_placed_endpoint {
    mimusb_endpoint_role role;
    uint32_t reserved; /* zero */
    mimusb_usb_endpoint endpoint;
} mimusb_placed_endpoint;

/* Where a Node goes: the bus numbers, the endpoints of its roles and the Node's own control rules
 * (mimusb_node_open). first_interface is the bInterfaceNumber of the Spec's first interface
 * (mimusb_spec_interface_count says how many a Spec takes). `name` is the string of the first
 * interface. HID Specs: Nodes with the same first_interface share one interface and must all have
 * different Report IDs and the same name and endpoints; both endpoints are interrupt endpoints; the
 * IN endpoint's depth is at most what the board's queues hold (MIMUSB_ERR_OVERFLOW). A Spec of
 * mimusb_spec_create_interfaces carries its own interface numbers, names and endpoints:
 * first_interface, name and endpoints are zero or null.
 * extra_rules are the caller's own control rules for this Node (as mimusb_control_rule says;
 * copied), tried before the rules the Spec brings. A rule's number in mimusb_node_set_rule_data is
 * its place in this array; the Spec's own rules follow (mimusb_node_find_rule). */
typedef struct mimusb_node_placement {
    uint32_t struct_size;
    uint32_t reserved;
    uint8_t first_interface;
    uint8_t reserved8[7]; /* zero */
    const char* name;     /* the interface's string, or null for none */
    const mimusb_placed_endpoint* endpoints;
    size_t endpoint_count;
    const mimusb_control_rule* extra_rules; /* null with extra_rule_count 0 */
    size_t extra_rule_count;
} mimusb_node_placement;

/* The HID function a Spec describes (HID 1.11 6.2.1, 4.2, 4.3): what the HID descriptor and the
 * Interface descriptor of the Node say. Every HID Spec's options carry one. */
typedef struct mimusb_hid_function {
    uint16_t bcd_hid;     /* required, e.g. 0x0111 */
    uint8_t subclass;     /* 0 none, 1 boot interface */
    uint8_t protocol;     /* 0 none, 1 keyboard, 2 mouse */
    uint8_t country_code; /* 0 when the hardware is not localized, up to 35 */
    uint8_t reserved8[3]; /* zero */
} mimusb_hid_function;

/* A whole USB device as the bytes the target reads (mimusb_device_set_descriptors), to present a
 * copy of a real device. Nothing is changed in them and nothing is filled in. */
typedef struct mimusb_device_descriptors {
    uint32_t struct_size;
    uint32_t reserved;
    const uint8_t* device; /* the 18-byte device descriptor (USB 2.0 9.6.1) */
    size_t device_length;
    const uint8_t* configuration; /* the whole configuration descriptor (USB 2.0 9.6.3) */
    size_t configuration_length;
    const uint8_t* bos; /* the whole BOS descriptor (USB 3.2 9.6.2), or null for none */
    size_t bos_length;
} mimusb_device_descriptors;

/* ---- Profile options --------------------------------------------------------------------
 * docs/PROFILES.md describes each profile's descriptor and the limits of every field. A value
 * that may be zero (a logical minimum, a neutral value, a count of optional parts) is never
 * taken for "unset": what is optional has its own enable_* flag or count. */

/* The four HID 1.11 physical Global items for one field. They are emitted only when enabled is 1
 * and are reset right after the field, so they never apply to the next one. */
typedef struct mimusb_physical_properties {
    uint32_t enabled;
    int32_t minimum;
    int32_t maximum;
    int32_t unit_exponent;
    uint32_t unit;
} mimusb_physical_properties;

/* One report field. The logical range must fit bit_width (1 to 32 bits); a bit_width of zero is
 * an unset field. */
typedef struct mimusb_integer_field {
    int32_t logical_minimum;
    int32_t logical_maximum;
    uint32_t bit_width;
    mimusb_physical_properties physical;
} mimusb_integer_field;

/* With enabled 1, every report starts with value (1 to 255). */
typedef struct mimusb_report_id_option {
    uint32_t enabled;
    uint8_t value;
    uint8_t reserved8[3];
} mimusb_report_id_option;

/* Battery Strength (Generic Device Controls 0x06/0x20) as a second Input report in the profile's
 * Application Collection. Both this report_id and the profile's must be enabled and differ. */
typedef struct mimusb_battery_report {
    mimusb_report_id_option report_id;
    mimusb_integer_field strength;
} mimusb_battery_report;

/* Full N-key rollover: one bit per Usage from usage_minimum (at least 0x04) to usage_maximum, plus
 * the eight modifiers 0xE0 to 0xE7, which the range must not include. */
typedef struct mimusb_keyboard_options {
    uint32_t struct_size;
    uint32_t reserved;
    mimusb_report_id_option report_id;
    uint16_t usage_minimum;
    uint16_t usage_maximum;
    uint32_t enable_battery;
    mimusb_battery_report battery;
    mimusb_hid_function function; /* required */
} mimusb_keyboard_options;

/* X, Y, wheel and pan are relative; their ranges must hold negative and positive values. */
typedef struct mimusb_mouse_options {
    uint32_t struct_size;
    uint32_t reserved;
    mimusb_report_id_option report_id;
    uint32_t button_count; /* 1 to 65535 */
    mimusb_integer_field x;
    mimusb_integer_field y;
    uint32_t enable_wheel;
    mimusb_integer_field wheel;
    uint32_t enable_pan;
    mimusb_integer_field pan;
    uint32_t enable_battery;
    mimusb_battery_report battery;
    mimusb_hid_function function; /* required */
} mimusb_mouse_options;

/* One-bit controls on any HUT page: Consumer, System Control, Camera Control, Telephony, ...
 * allowed_usages, usage_semantics, expected_linux_event_types and expected_linux_codes are
 * parallel arrays of allowed_usage_count entries, all required. The expected Linux strings (such
 * as "EV_KEY" and "KEY_PLAYPAUSE") are kept with the Spec as a record; the library does not read
 * them. On the Camera Control page (0x90) only Auto-focus (0x20) and Shutter (0x21) exist, each
 * as MIMUSB_USAGE_ONE_SHOT. */
typedef struct mimusb_toggle_options {
    uint32_t struct_size;
    uint32_t reserved;
    mimusb_report_id_option report_id;
    uint16_t application_page; /* the Application Collection, e.g. 0x0C/0x01 Consumer Control */
    uint16_t application_usage;
    uint16_t field_page; /* the Usage Page of every allowed Usage */
    uint16_t reserved16;
    const uint16_t* allowed_usages;
    size_t allowed_usage_count;
    const mimusb_usage_semantic* usage_semantics;
    const char* const* expected_linux_event_types;
    const char* const* expected_linux_codes;
    uint32_t enable_battery;
    mimusb_battery_report battery;
    mimusb_hid_function function; /* required */
} mimusb_toggle_options;

/* One axis. usage_page and usage must be the ones of role (docs/PROFILES.md); neutral_value is the
 * value sent at rest and on close, and may be any value in the range, zero included. The expected
 * string (such as "ABS_X") is required and kept with the Spec as a record; the library does not
 * read it. */
typedef struct mimusb_gamepad_axis_option {
    mimusb_axis_role role;
    uint16_t usage_page;
    uint16_t usage;
    mimusb_integer_field value;
    int32_t neutral_value;
    const char* expected_linux_code;
} mimusb_gamepad_axis_option;

/* At least two axes, one of them MIMUSB_AXIS_X and one MIMUSB_AXIS_Y, each role once. A Hat must
 * be logical 0 to 7 in 4 bits; outside MIMUSB_DPAD_HAT the three hat_* fields are zero. */
typedef struct mimusb_gamepad_options {
    uint32_t struct_size;
    uint32_t reserved;
    mimusb_report_id_option report_id;
    const mimusb_gamepad_axis_option* axes;
    size_t axis_count;
    uint32_t button_count;         /* at least 1 */
    uint16_t button_usage_minimum; /* the Button page Usage of the first button, nonzero */
    mimusb_dpad_representation dpad_representation;
    int32_t hat_logical_minimum;
    int32_t hat_logical_maximum;
    uint32_t hat_bit_width;
    uint32_t enable_battery;
    mimusb_battery_report battery;
    mimusb_hid_function function; /* required */
} mimusb_gamepad_options;

/* Fixed-slot Multi-Touch under the Digitizers / Touch Screen Application Collection. */
typedef struct mimusb_touchscreen_options {
    uint32_t struct_size;
    uint32_t reserved;
    mimusb_report_id_option report_id;
    uint32_t maximum_contacts;          /* 1 to 16 */
    uint32_t contacts_per_report;       /* maximum_contacts, or fewer with multi-packet frames */
    mimusb_integer_field contact_id;    /* holds 0 and at least maximum_contacts values */
    mimusb_integer_field x;             /* holds 0 */
    mimusb_integer_field y;             /* holds 0 */
    mimusb_integer_field contact_count; /* holds 0 to maximum_contacts */
    uint32_t enable_pressure;
    mimusb_integer_field pressure;
    uint32_t enable_width;
    mimusb_integer_field width;
    uint32_t enable_height;
    mimusb_integer_field height;
    uint32_t enable_azimuth;
    mimusb_integer_field azimuth; /* counter-clockwise, minimum 0 (docs/LIMITS.md) */
    uint32_t enable_scan_time;
    mimusb_integer_field scan_time;
    uint32_t scan_time_unit_100us; /* must be 1 with enable_scan_time */
    /* 1 adds Contact Count Maximum as a Feature report, which the board answers itself. */
    uint32_t enable_contact_count_maximum_feature_declaration;
    uint32_t enable_multi_packet_frames;
    uint32_t enable_battery;
    mimusb_battery_report battery;
    mimusb_hid_function function; /* required */
} mimusb_touchscreen_options;

/* The touchscreen report under the Digitizers / Touch Pad Application Collection, plus
 * button_count click buttons (0 for a buttonless clickpad). */
typedef struct mimusb_touchpad_options {
    uint32_t struct_size;
    uint32_t reserved;
    mimusb_report_id_option report_id;
    uint32_t maximum_contacts;
    uint32_t contacts_per_report;
    mimusb_integer_field contact_id;
    mimusb_integer_field x;
    mimusb_integer_field y;
    mimusb_integer_field contact_count;
    uint32_t enable_pressure;
    mimusb_integer_field pressure;
    uint32_t enable_width;
    mimusb_integer_field width;
    uint32_t enable_height;
    mimusb_integer_field height;
    uint32_t enable_azimuth;
    mimusb_integer_field azimuth;
    uint32_t enable_scan_time;
    mimusb_integer_field scan_time;
    uint32_t scan_time_unit_100us;
    uint32_t enable_contact_count_maximum_feature_declaration;
    uint32_t enable_multi_packet_frames;
    uint32_t button_count;
    uint32_t enable_battery;
    mimusb_battery_report battery;
    mimusb_hid_function function; /* required */
} mimusb_touchpad_options;

/* A stylus under the Digitizers / Pen Application Collection (a direct, on-screen pen). */
typedef struct mimusb_pen_options {
    uint32_t struct_size;
    uint32_t reserved;
    mimusb_report_id_option report_id;
    mimusb_integer_field x;
    mimusb_integer_field y;
    uint32_t enable_pressure;
    mimusb_integer_field pressure;
    uint32_t enable_tilt; /* X Tilt and Y Tilt together */
    mimusb_integer_field tilt_x;
    mimusb_integer_field tilt_y;
    uint32_t enable_twist_target_specific; /* how a target uses Twist varies */
    mimusb_integer_field twist;
    /* Up to 32 unique Digitizers-page Usages, each Barrel Switch (0x44) or Secondary Barrel
     * Switch (0x5A); bit i of a sample's barrel_buttons is barrel_usages[i]. A count of 0 means
     * no barrel button. */
    const uint16_t* barrel_usages;
    size_t barrel_usage_count;
    /* 1 adds HUT Invert (0x3C), which selects the eraser end; not the Eraser Usage (0x45). */
    uint32_t enable_eraser;
    uint32_t enable_hover; /* 1 lets a sample be in range without touching */
    uint32_t enable_battery;
    mimusb_battery_report battery;
    mimusb_hid_function function; /* required */
} mimusb_pen_options;

/* The pen report under the Digitizers / Digitizer Application Collection (an indirect tablet). */
typedef struct mimusb_digitizer_options {
    uint32_t struct_size;
    uint32_t reserved;
    mimusb_report_id_option report_id;
    mimusb_integer_field x;
    mimusb_integer_field y;
    uint32_t enable_pressure;
    mimusb_integer_field pressure;
    uint32_t enable_tilt;
    mimusb_integer_field tilt_x;
    mimusb_integer_field tilt_y;
    uint32_t enable_twist_target_specific;
    mimusb_integer_field twist;
    const uint16_t* barrel_usages;
    size_t barrel_usage_count;
    uint32_t enable_eraser;
    uint32_t enable_hover;
    uint32_t enable_battery;
    mimusb_battery_report battery;
    mimusb_hid_function function; /* required */
} mimusb_digitizer_options;

/* An Input report a generic HID Node may send: its Report ID and its exact length on the wire. */
typedef struct mimusb_hid_report {
    mimusb_report_id_option report_id;
    uint32_t wire_length;
} mimusb_hid_report;

/* A descriptor of the caller's own; reports lists its Input reports and is empty when it has none.
 * A descriptor with Output items needs requires_output 1 (they
 * arrive as MIMUSB_EVENT_OUTPUT_REPORT), one with Feature items requires_feature_response 1 (the
 * board answers with mimusb_node_set_feature_response). */
typedef struct mimusb_hid_options {
    uint32_t struct_size;
    uint32_t reserved;
    const uint8_t* descriptor;
    size_t descriptor_length;
    const mimusb_hid_report* reports;
    size_t report_count;
    uint32_t requires_output;
    uint32_t requires_feature_response;
    mimusb_hid_function function; /* required */
} mimusb_hid_options;

/* ---- CDC-ACM options -------------------------------------------------------------------- */

/* A CDC-ACM function (USB CDC 1.2 5.2.3; PSTN 1.2 5.3, 6.3): two interfaces, the communication
 * interface (class 0x02, subclass 0x02; one interrupt IN endpoint for notifications) and the data
 * interface (class 0x0A; a bulk IN and a bulk OUT endpoint), with the Interface Association
 * descriptor that joins them and the rules that make it answer. The library writes all of that;
 * it does not touch the data on the bulk endpoints, which the program reads and writes with
 * mimusb_node_endpoint_write and MIMUSB_EVENT_ENDPOINT_DATA. */
typedef struct mimusb_cdc_acm_options {
    uint32_t struct_size;
    uint32_t reserved;
    uint16_t bcd_cdc; /* required, e.g. 0x0120 */
    uint8_t protocol; /* bInterfaceProtocol of the communication interface: 0 none, 1 AT commands
                         (V.250), ... as the program chooses */
    uint8_t call_management_capabilities; /* bmCapabilities of the Call Management descriptor,
                                             written as given */
    uint8_t acm_capabilities; /* bmCapabilities of the ACM descriptor, written as given */
    uint8_t reserved8[3];     /* zero */
    /* What GET_LINE_CODING answers until the program changes it with
     * mimusb_node_set_rule_data on MIMUSB_RULE_SLOT_CDC_LINE_CODING: dwDTERate (little-endian),
     * bCharFormat, bParityType, bDataBits. A dwDTERate of 0 is unset. */
    uint8_t line_coding[7];
    uint8_t reserved8b;    /* zero */
    const char* data_name; /* the data interface's string, or null for none */
} mimusb_cdc_acm_options;

typedef struct mimusb_report_capability {
    mimusb_report_id_option report_id;
    uint32_t wire_length;
} mimusb_report_capability;

/* What a Spec sends. Filled in by mimusb_spec_manifest and mimusb_node_manifest. */
typedef struct mimusb_capability_manifest {
    uint32_t struct_size;
    uint32_t reserved;
    uint32_t input_supported;
    uint32_t output_supported;            /* always 1: the board passes Output reports on */
    uint32_t feature_transport_supported; /* always 1: the board answers Feature reads */
    mimusb_profile_kind profile_kind;
    const mimusb_report_capability* reports; /* valid while the Spec is */
    size_t report_count;
    size_t descriptor_length;
} mimusb_capability_manifest;

/* ---- Input samples ---------------------------------------------------------------------- */

/* Optional values of one touch contact. A field the Spec did not enable must be 0. */
typedef struct mimusb_touchscreen_extra {
    int32_t pressure;
    int32_t width;
    int32_t height;
    int32_t azimuth;
} mimusb_touchscreen_extra;

typedef struct mimusb_touchpad_extra {
    int32_t pressure;
    int32_t width;
    int32_t height;
    int32_t azimuth;
} mimusb_touchpad_extra;

/* One pen state. tip 1 needs in_range 1; without enable_hover, in_range must equal tip. A field the
 * Spec did not enable must be 0. */
typedef struct mimusb_pen_sample {
    uint32_t in_range;
    uint32_t tip;
    uint32_t eraser;         /* 1 for the eraser end; tip may be 1 too */
    uint32_t barrel_buttons; /* bit i is barrel_usages[i] */
    int32_t x;
    int32_t y;
    int32_t pressure;
    int32_t tilt_x;
    int32_t tilt_y;
    int32_t twist;
} mimusb_pen_sample;

typedef struct mimusb_digitizer_sample {
    uint32_t in_range;
    uint32_t tip;
    uint32_t eraser;
    uint32_t barrel_buttons;
    int32_t x;
    int32_t y;
    int32_t pressure;
    int32_t tilt_x;
    int32_t tilt_y;
    int32_t twist;
} mimusb_digitizer_sample;

/* ---- Initializers -----------------------------------------------------------------------
 * Each *_init function zeroes a structure and sets struct_size, and nothing else: no value is
 * filled in for the caller. The USB identity, the strings, the timeouts, the baud rate, the
 * endpoints, the Linux HID limits and every profile's fields are the caller's to set; one left at
 * zero where zero is not valid is refused with MIMUSB_ERR_UNSET_FIELD (or MIMUSB_ERR_PARAM). A
 * value the caller sets is used as given, zero included where zero is valid. */
static inline void mimusb_struct_init(void* value, size_t size) {
    if (value != NULL) {
        const uint32_t struct_size = (uint32_t)size;
        memset(value, 0, size);
        memcpy(value, &struct_size, sizeof(struct_size));
    }
}

static inline mimusb_integer_field
mimusb_integer_field_make(int32_t logical_minimum, // NOLINT(bugprone-easily-swappable-parameters)
                          int32_t logical_maximum, uint32_t bit_width) {
    mimusb_integer_field field;
    memset(&field, 0, sizeof(field));
    field.logical_minimum = logical_minimum;
    field.logical_maximum = logical_maximum;
    field.bit_width = bit_width;
    return field;
}

/* An endpoint item of a setting. */
static inline mimusb_usb_item mimusb_usb_item_make_endpoint(mimusb_usb_endpoint endpoint) {
    mimusb_usb_item item;
    memset(&item, 0, sizeof(item));
    item.kind = MIMUSB_ITEM_ENDPOINT;
    item.endpoint = endpoint;
    return item;
}

/* A descriptor item of a setting: one whole descriptor, which the caller keeps until the Node is
 * opened (the library copies it). */
static inline mimusb_usb_item mimusb_usb_item_make_descriptor(const uint8_t* descriptor,
                                                              size_t descriptor_length) {
    mimusb_usb_item item;
    memset(&item, 0, sizeof(item));
    item.kind = MIMUSB_ITEM_DESCRIPTOR;
    item.descriptor = descriptor;
    item.descriptor_length = descriptor_length;
    return item;
}

static inline void mimusb_device_config_init(mimusb_device_config* config) {
    mimusb_struct_init(config, sizeof(*config));
}

static inline void mimusb_commit_timeouts_init(mimusb_commit_timeouts* timeouts) {
    mimusb_struct_init(timeouts, sizeof(*timeouts));
}

static inline void mimusb_node_config_init(mimusb_node_config* config) {
    mimusb_struct_init(config, sizeof(*config));
}

static inline void mimusb_node_placement_init(mimusb_node_placement* placement) {
    mimusb_struct_init(placement, sizeof(*placement));
}

static inline void mimusb_board_limits_init(mimusb_board_limits* limits) {
    mimusb_struct_init(limits, sizeof(*limits));
}

static inline void mimusb_link_counters_init(mimusb_link_counters* counters) {
    mimusb_struct_init(counters, sizeof(*counters));
}

static inline void mimusb_keyboard_options_init(mimusb_keyboard_options* options) {
    mimusb_struct_init(options, sizeof(*options));
}

static inline void mimusb_mouse_options_init(mimusb_mouse_options* options) {
    mimusb_struct_init(options, sizeof(*options));
}

static inline void mimusb_toggle_options_init(mimusb_toggle_options* options) {
    mimusb_struct_init(options, sizeof(*options));
}

static inline void mimusb_gamepad_options_init(mimusb_gamepad_options* options) {
    mimusb_struct_init(options, sizeof(*options));
}

static inline void mimusb_touchscreen_options_init(mimusb_touchscreen_options* options) {
    mimusb_struct_init(options, sizeof(*options));
}

static inline void mimusb_touchpad_options_init(mimusb_touchpad_options* options) {
    mimusb_struct_init(options, sizeof(*options));
}

static inline void mimusb_pen_options_init(mimusb_pen_options* options) {
    mimusb_struct_init(options, sizeof(*options));
}

static inline void mimusb_digitizer_options_init(mimusb_digitizer_options* options) {
    mimusb_struct_init(options, sizeof(*options));
}

static inline void mimusb_hid_options_init(mimusb_hid_options* options) {
    mimusb_struct_init(options, sizeof(*options));
}

static inline void mimusb_cdc_acm_options_init(mimusb_cdc_acm_options* options) {
    mimusb_struct_init(options, sizeof(*options));
}

static inline void mimusb_capability_manifest_init(mimusb_capability_manifest* manifest) {
    mimusb_struct_init(manifest, sizeof(*manifest));
}

/* ---- Diagnostics ------------------------------------------------------------------------ */

/* mimusb_last_error
 * The details of the calling thread's last result.
 * Ownership: Returns the calling thread's record, valid until that thread's next call.
 * Synchronization: The record belongs to the calling thread; do not pass it to another.
 * Returns: Never null. */
MIMUSB_API const mimusb_error_detail* MIMUSB_CALL mimusb_last_error(void);

/* mimusb_result_name
 * The name of a result constant.
 * Ownership: The string is static.
 * Returns: The constant's name, such as "MIMUSB_ERR_TIMEOUT", with MIMUSB_OK recorded; for a value
 * that is not a result, "MIMUSB_RESULT_UNKNOWN" with MIMUSB_ERR_PARAM recorded. */
MIMUSB_API const char* MIMUSB_CALL mimusb_result_name(mimusb_result result);

/* mimusb_frame_type_name
 * Names the serial frame type of mimusb_error_detail.frame_type (docs/PROTOCOL.md).
 * Ownership: The string is static.
 * Returns: The frame's name, such as "COMMIT" or "EP_IN", with MIMUSB_OK recorded; for a value
 * that is not a frame type, "UNKNOWN" with MIMUSB_ERR_PARAM recorded. */
MIMUSB_API const char* MIMUSB_CALL mimusb_frame_type_name(int32_t frame_type);

/* mimusb_version
 * The version of the library that is running.
 * Returns: (major << 24) | (minor << 16) | patch of the library that is running. */
MIMUSB_API uint32_t MIMUSB_CALL mimusb_version(void);

/* ---- Discovery -------------------------------------------------------------------------- */

/* mimusb_discover
 * Searches the serial ports for boards and returns a snapshot of them.
 * Ownership: The caller destroys the snapshot with mimusb_discovery_destroy.
 * Blocking: Searches every USB serial port at once, waiting up to timeout_ms for each answer; a
 * port another program has open is skipped. A serial device that is not a board receives a few
 * pings and a BREAK. On a board it also measures the fastest rate the link carries and leaves the
 * board on the boot rate (docs/API.md). A board whose firmware has another major or minor version
 * is listed with only its version and firmware_compatible 0.
 * Synchronization: Any thread.
 * Returns: MIMUSB_OK, also when no board was found; MIMUSB_ERR_PARAM for a null out_discovery;
 * MIMUSB_ERR_UNSET_FIELD for a zero timeout. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_discover(uint32_t timeout_ms,
                                                     mimusb_discovery** out_discovery);

/* mimusb_discovery_count
 * The number of boards in a snapshot.
 * Synchronization: Any thread, but not during mimusb_discovery_destroy.
 * Returns: The number of boards with MIMUSB_OK recorded, or 0 with MIMUSB_ERR_PARAM recorded for
 * a null snapshot. */
MIMUSB_API size_t MIMUSB_CALL mimusb_discovery_count(const mimusb_discovery* discovery);

/* mimusb_discovery_get
 * One board of a snapshot.
 * Ownership: The entry belongs to the snapshot.
 * Synchronization: As mimusb_discovery_count.
 * Returns: The entry with MIMUSB_OK recorded, or null with MIMUSB_ERR_PARAM recorded for a null
 * snapshot or an index out of range. */
MIMUSB_API const mimusb_device_info* MIMUSB_CALL
mimusb_discovery_get(const mimusb_discovery* discovery, size_t index);

/* mimusb_discovery_destroy
 * Frees a snapshot.
 * Ownership: Frees the snapshot and every entry of it.
 * Synchronization: No other call may use the snapshot meanwhile.
 * Returns: Nothing. MIMUSB_OK is recorded, or MIMUSB_ERR_PARAM for null (nothing is done). */
MIMUSB_API void MIMUSB_CALL mimusb_discovery_destroy(mimusb_discovery* discovery);

/* ---- Device ----------------------------------------------------------------------------- */

/* mimusb_device_open
 * Opens a board on a serial port.
 * Ownership: Copies config; the caller closes the Device with mimusb_device_close.
 * Blocking: Opens port_path for this process alone, looks for the board at the boot rate, reads its
 * limits, moves the link to transport.baud_rate and starts the reader thread (MIMUSB_BAUD_FASTEST
 * measures the rate instead). It sends no configuration: the board's USB device stays off the
 * target until a Node is opened.
 * Synchronization: Any thread, one call per port.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for a null pointer, a wrong struct_size or a bad value;
 * MIMUSB_ERR_UNSET_FIELD for a required field left zero; MIMUSB_ERR_UNSUPPORTED for a rate the
 * board cannot make or the link did not carry; MIMUSB_ERR_OVERFLOW for a maximum_report_bytes the
 * board does not take; MIMUSB_ERR_TIMEOUT when no board answered; MIMUSB_ERR_PROTOCOL for firmware
 * of another major or minor version; MIMUSB_ERR_ACCESS, MIMUSB_ERR_BUSY or MIMUSB_ERR_IO for a port
 * that cannot be opened or set up. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_open(const char* port_path,
                                                        const mimusb_device_config* config,
                                                        mimusb_device** out_device);

/* mimusb_device_close
 * Closes a Device and its Nodes.
 * Ownership: Follows the "Closing" rule; the Device's Nodes go with it.
 * Blocking: Sends the neutral report of every open Node within drain_timeout_ms, takes the board's
 * USB device off the target unless transport.keep_attached is 1, puts the board back on the boot
 * rate and closes the port. Each configuration frame waits up to control_timeout_ms. A failed step
 * does not stop the next one.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for a null or closing Device; MIMUSB_ERR_UNSET_FIELD for a
 * zero timeout; MIMUSB_ERR_NO_DEVICE when the link was already gone; otherwise the first link error
 * on the way out. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_close(mimusb_device* device,
                                                         uint32_t control_timeout_ms,
                                                         uint32_t drain_timeout_ms);

/* mimusb_device_set_usb_identity
 * Sets the USB device descriptor, configuration and strings the target sees.
 * Ownership: Copies usb_device and usb_config.
 * Blocking: Commits (see the top of this header), waiting as timeouts says; inside a batch it only
 * stages the change.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for a null pointer, a wrong struct_size or a bad value;
 * MIMUSB_ERR_UNSET_FIELD for a required field or timeout left zero; the results of
 * mimusb_device_commit. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_set_usb_identity(
    mimusb_device* device, const mimusb_usb_device* usb_device, const mimusb_usb_config* usb_config,
    const mimusb_commit_timeouts* timeouts);

/* mimusb_device_latched_error
 * Reads the error the reader thread recorded for the link, and forgets it unless it is
 * MIMUSB_ERR_NO_DEVICE, which stays.
 * Returns: MIMUSB_OK when there is none; MIMUSB_ERR_PARAM for an invalid Device;
 * MIMUSB_ERR_NO_DEVICE after the port failed or was unplugged; MIMUSB_ERR_IO for another link
 * error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_latched_error(mimusb_device* device);

/* mimusb_device_is_ready
 * Whether the target has mounted the board's USB device.
 * Synchronization: Any thread while the Device is open.
 * Returns: 1 while the target has the board's USB device mounted and takes reports, else 0. */
MIMUSB_API int32_t MIMUSB_CALL mimusb_device_is_ready(const mimusb_device* device);

/* mimusb_device_wait_ready
 * Waits until the target mounts the board's USB device.
 * Blocking: Until the target mounts the board's USB device, or timeout_ms.
 * Returns: MIMUSB_OK once mounted; MIMUSB_ERR_PARAM for an invalid Device; MIMUSB_ERR_TIMEOUT;
 * MIMUSB_ERR_NO_DEVICE when the link fails meanwhile. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_wait_ready(mimusb_device* device,
                                                              uint32_t timeout_ms);

/* mimusb_device_batch_begin
 * Starts gathering changes into one commit.
 * Blocking: None; nothing is sent. Until mimusb_device_batch_end, mimusb_node_open,
 * mimusb_node_close and mimusb_device_set_usb_identity only stage their change, and a Node opened
 * in the batch refuses reports with MIMUSB_ERR_BUSY.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device; MIMUSB_ERR_BUSY inside a batch. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_batch_begin(mimusb_device* device);

/* mimusb_device_batch_end
 * Commits every change made since mimusb_device_batch_begin.
 * Blocking: Commits every change staged since mimusb_device_batch_begin at once, waiting as
 * timeouts says.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device, a null timeouts or a wrong
 * struct_size; MIMUSB_ERR_UNSET_FIELD for a zero timeout; MIMUSB_ERR_BUSY outside a batch; the
 * results of mimusb_device_commit. */
MIMUSB_API mimusb_result MIMUSB_CALL
mimusb_device_batch_end(mimusb_device* device, const mimusb_commit_timeouts* timeouts);

/* mimusb_device_commit
 * Sends the configuration and makes the board enumerate again.
 * Blocking: Sends the configuration of every open Node and commits, waiting as timeouts says.
 * mimusb_node_open, mimusb_node_close and mimusb_device_batch_end do this already; call it to
 * retry after a failed commit, or for reconnect_delay_ms: with a nonzero delay on an attached
 * device the board keeps its USB device off the target (D+ at 0 V) for that many milliseconds.
 * Inside a batch it commits the staged changes and ends the batch.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device or timeouts; MIMUSB_ERR_UNSET_FIELD
 * for a zero timeout; MIMUSB_ERR_OVERFLOW when the Nodes do not fit the board;
 * MIMUSB_ERR_DESCRIPTOR_REJECTED when the board refuses the configuration; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_commit(mimusb_device* device,
                                                          uint32_t reconnect_delay_ms,
                                                          const mimusb_commit_timeouts* timeouts);

/* mimusb_device_connect
 * Puts the board's USB device on the target.
 * Blocking: A Device that is not on the target (not configured yet, or after
 * mimusb_device_disconnect) commits instead, which puts the board's USB device on the target (D+
 * pull-up on) and waits for the board's answer. A Device that is on the target already sends
 * nothing and returns MIMUSB_OK: the board refuses a commit that attaches while it is attached.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device or timeouts; MIMUSB_ERR_UNSET_FIELD
 * for a zero timeout; a link error; the results of mimusb_device_commit when it commits. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_connect(mimusb_device* device,
                                                           const mimusb_commit_timeouts* timeouts);

/* mimusb_device_disconnect
 * Takes the board's USB device off the target.
 * Blocking: Takes the board's USB device off the target (D+ at 0 V, as if unplugged), waiting up
 * to control_timeout_ms for the board's answer. The next connect or commit configures it again.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device; MIMUSB_ERR_UNSET_FIELD for a zero
 * timeout; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_disconnect(mimusb_device* device,
                                                              uint32_t control_timeout_ms);

/* mimusb_device_remote_wakeup
 * Wakes a target that suspended the board's USB device; the target must have enabled remote
 * wakeup (usb_config.bm_attributes bit 5 and MIMUSB_EVENT_SUSPEND with value 1).
 * Blocking: Waits up to control_timeout_ms for the board's answer.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device; MIMUSB_ERR_UNSET_FIELD for a zero
 * timeout; MIMUSB_ERR_BUSY when the device is not suspended; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_remote_wakeup(mimusb_device* device,
                                                                 uint32_t control_timeout_ms);

/* mimusb_device_enter_bootloader
 * Restarts the board into its chip's own bootloader, where new firmware is written. After
 * MIMUSB_OK the board no longer answers: the Device is good only for mimusb_device_close (which
 * may return MIMUSB_ERR_TIMEOUT; docs/API.md).
 * Blocking: Waits up to control_timeout_ms for the board's answer.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device; MIMUSB_ERR_UNSET_FIELD for a zero
 * timeout; MIMUSB_ERR_UNSUPPORTED on a board whose chip has no bootloader the firmware can enter;
 * a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_enter_bootloader(mimusb_device* device,
                                                                    uint32_t control_timeout_ms);

/* mimusb_device_board_info
 * What the board reported at open.
 * Ownership: Fills the caller's out_info.
 * Blocking: None; a copy of what the board reported at open.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device or a null out_info. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_board_info(const mimusb_device* device,
                                                              mimusb_device_info* out_info);

/* mimusb_device_limits
 * The limits of the board.
 * Ownership: Fills the caller's out_limits (set its struct_size; mimusb_board_limits_init does).
 * Blocking: None; a copy of what the board reported at open.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device, a null out_limits or a wrong
 * struct_size. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_limits(const mimusb_device* device,
                                                          mimusb_board_limits* out_limits);

/* mimusb_device_link_counters
 * With reset nonzero the board's counters are zeroed after being read.
 * Ownership: Fills the caller's out_counters (mimusb_link_counters_init sets its struct_size).
 * Blocking: One command to the board, waiting up to control_timeout_ms for its answer.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device, a null out_counters or a wrong
 * struct_size; MIMUSB_ERR_UNSET_FIELD for a zero timeout; MIMUSB_ERR_TIMEOUT or MIMUSB_ERR_IO when
 * the board did not answer. */
MIMUSB_API mimusb_result MIMUSB_CALL
mimusb_device_link_counters(mimusb_device* device, uint32_t reset, uint32_t control_timeout_ms,
                            mimusb_link_counters* out_counters);

/* ---- Control transfers of the target ----------------------------------------------------
 * Requests on endpoint 0 that USB itself fixes are served by the board; every other one is matched
 * against a list of rules: those of each Node (mimusb_node_config.rules; the profile Nodes write
 * theirs), then those of the Device (mimusb_device_set_control_rules). The first rule whose four
 * masked fields match decides; a request no rule matches is stalled. See docs/API.md. */

/* mimusb_device_set_control_rules
 * Replaces the Device's own rules, which come after those of the Nodes. report_all 1 reports every
 * control transfer (MIMUSB_EVENT_CONTROL_SETUP), whatever its rule. SET_ADDRESS and
 * SET_CONFIGURATION are never held, nor a host-to-device data stage longer than
 * max_control_data_bytes. The rules stay until replaced or mimusb_device_close.
 * Ownership: Copies rules and the bytes they point to.
 * Blocking: Sends the whole rule table again and waits up to control_timeout_ms for each frame.
 * Inside a batch, or before the first commit, nothing is sent: the rules go with the commit.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device, report_all, count or rule;
 * MIMUSB_ERR_UNSET_FIELD for a zero control_timeout_ms, an action left 0, an ASK without
 * timeout_ms or when_late; MIMUSB_ERR_OVERFLOW for more rules or answer bytes than the board holds
 * with those of the Nodes; a link error. The board refuses a rule that names an endpoint that is
 * not an IN endpoint of the committed configuration. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_set_control_rules(
    mimusb_device* device, uint32_t report_all, const mimusb_control_rule* rules, size_t rule_count,
    uint32_t control_timeout_ms);

/* mimusb_device_set_rule_data
 * Changes the bytes of ANSWER rule number rule_index of mimusb_device_set_control_rules, up to
 * the length its data had when the rules were set. Inside a batch, or before the first commit, it
 * is kept for the commit.
 * Ownership: Copies data.
 * Blocking: One command to the board, waiting up to control_timeout_ms; retried while the board is
 * answering from that rule.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device, a rule that is not an ANSWER rule, a
 * null data with a length or more bytes than the rule's capacity; MIMUSB_ERR_UNSET_FIELD for a zero
 * timeout; MIMUSB_ERR_BUSY when the board kept answering from the rule; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_set_rule_data(mimusb_device* device,
                                                                 size_t rule_index,
                                                                 const uint8_t* data, size_t length,
                                                                 uint32_t control_timeout_ms);

/* mimusb_node_set_rule_data
 * As mimusb_device_set_rule_data for rule number rule_index of the Node's own rules: the
 * extra_rules of its placement first, then the rules its Spec brings (mimusb_node_find_rule finds
 * one the Spec names).
 * Ownership: Copies data.
 * Blocking: As mimusb_device_set_rule_data.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Node, a rule that is not an ANSWER rule, a
 * null data with a length or more bytes than the rule's capacity; MIMUSB_ERR_UNSET_FIELD for a zero
 * timeout; MIMUSB_ERR_BUSY when the board kept answering from the rule; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_node_set_rule_data(mimusb_node* node, size_t rule_index,
                                                               const uint8_t* data, size_t length,
                                                               uint32_t control_timeout_ms);

/* mimusb_node_find_rule
 * Finds the rule the Spec of a Node names with `slot` (mimusb_rule_slot) among the Node's own
 * rules, so that mimusb_node_set_rule_data can change its bytes: *out_rule_index is its number,
 * which counts the Node's extra_rules first (a Node has them before the rules of its Spec).
 * Ownership: Writes into the caller's variable.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Node or a null out_rule_index;
 * MIMUSB_ERR_UNSUPPORTED when the Node's Spec has no rule of that slot. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_node_find_rule(const mimusb_node* node,
                                                           mimusb_rule_slot slot,
                                                           size_t* out_rule_index);

/* mimusb_device_control_reply
 * Answers the held transfer of token (from MIMUSB_EVENT_CONTROL_SETUP with the HELD flag, or from
 * MIMUSB_EVENT_CONTROL_DATA for the verdict of a host-to-device transfer) with action: ANSWER (data
 * is the data stage of a device-to-host request, empty otherwise, or the acknowledging verdict),
 * STALL, DROP (data empty) or LEAVE (the board's own handling). A reply the board no longer waits
 * for is dropped. The rules for each action are in docs/API.md.
 * Ownership: Copies data.
 * Blocking: Writes one frame, for up to send_timeout_ms without progress; it does not wait for the
 * board.
 * Synchronization: Device domain, and also from the event sink, which is the way to answer in time.
 * Returns: MIMUSB_OK once the frame is written; MIMUSB_ERR_PARAM for an invalid Device, a token of
 * 0 or above 255, an unknown action, or data with an action that takes none;
 * MIMUSB_ERR_UNSET_FIELD for a zero timeout; MIMUSB_ERR_OVERFLOW for data longer than
 * max_control_data_bytes; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_control_reply(mimusb_device* device,
                                                                 uint32_t token,
                                                                 mimusb_control_action action,
                                                                 const uint8_t* data, size_t length,
                                                                 uint32_t send_timeout_ms);

/* The largest interval_ms of mimusb_device_set_loss_reporting, which is the least time between two
 * MIMUSB_EVENT_TRANSFER_LOST events. */
#define MIMUSB_LOSS_MAX_INTERVAL_MS 60000U

/* mimusb_device_set_loss_reporting
 * enabled 1 makes the board report the transfers it drops because a queue is full
 * (MIMUSB_EVENT_TRANSFER_LOST), at most one event per interval_ms (0 to
 * MIMUSB_LOSS_MAX_INTERVAL_MS; 0 reports each time). enabled 0 is how the board starts and needs
 * interval_ms 0. The setting stays until replaced or mimusb_device_close.
 * Blocking: Waits up to control_timeout_ms for the board's answer.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device, enabled or interval_ms;
 * MIMUSB_ERR_UNSET_FIELD for a zero timeout; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_set_loss_reporting(mimusb_device* device,
                                                                      uint32_t enabled,
                                                                      uint32_t interval_ms,
                                                                      uint32_t control_timeout_ms);

/* mimusb_device_add_string
 * Stores a string descriptor (in the Device's language_id) and returns its index, for the caller's
 * own class descriptors. Indices 1 to 4 keep their meaning (manufacturer, product, serial number,
 * configuration); the first index given is 5. The string stays until mimusb_device_remove_string.
 * Ownership: Copies utf8.
 * Blocking: None; the string is sent with the next commit.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device, a null argument or text that is not
 * UTF-8 without control characters of at most 126 UTF-16 code units; MIMUSB_ERR_OVERFLOW when the
 * board's max_strings or the index space is used up. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_add_string(mimusb_device* device,
                                                              const char* utf8, uint8_t* out_index);

/* mimusb_device_remove_string
 * Forgets a string given by mimusb_device_add_string; the next commit no longer sends it.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device or an index that was not given. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_remove_string(mimusb_device* device,
                                                                 uint8_t index);

/* mimusb_device_set_string
 * Stores a string descriptor exactly as it goes on the bus: index 1 to 255, a nonzero language_id
 * (USB 2.0 9.6.7) and unit_count UTF-16 code units (1 to 126). The same index and language is
 * replaced. These are the only strings a Device given as bytes
 * (mimusb_device_set_descriptors) serves.
 * Ownership: Copies the units.
 * Blocking: None; the string is sent with the next commit.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device, a null units, an index of 0, a
 * language_id of 0 or a unit_count of 0 or above 126; MIMUSB_ERR_OVERFLOW when the board's
 * max_strings is used up. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_set_string(mimusb_device* device, uint8_t index,
                                                              uint16_t language_id,
                                                              const uint16_t* units,
                                                              size_t unit_count);

/* mimusb_device_clear_string
 * Forgets a string given by mimusb_device_set_string; the next commit no longer sends it.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device or an index and language_id that were
 * not given. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_clear_string(mimusb_device* device,
                                                                uint8_t index,
                                                                uint16_t language_id);

/* mimusb_device_set_bos
 * For a Device composed from Nodes: stores the Binary Object Store descriptor (USB 3.2 9.6.2);
 * length 0 removes it. Its header and capability lengths are checked, none of the capabilities is
 * read; at most the board's max_bos_bytes. The commit needs bcd_usb 0x0201 or more. A Device given
 * as bytes carries its BOS in mimusb_device_descriptors.
 * Ownership: Copies the bytes.
 * Blocking: None; the BOS descriptor is sent with the next commit.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device, a null bytes with a length, or a BOS
 * descriptor that does not hold together (mimusb_error_detail.offset is where);
 * MIMUSB_ERR_OVERFLOW for more than max_bos_bytes; MIMUSB_ERR_BUSY while the Device is given as
 * bytes. The commit refuses a bcd_usb below 0x0201 with MIMUSB_ERR_PARAM. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_set_bos(mimusb_device* device,
                                                           const uint8_t* bytes, size_t length);

/* ---- A Device given as bytes ------------------------------------------------------------
 * To present a copy of a real device, the descriptors are given as the bytes the target reads
 * (mimusb_device_set_descriptors) instead of being composed from Nodes. The two are exclusive on a
 * Device. Its endpoints are named by address (mimusb_device_endpoint_*), OUT data arrives as
 * MIMUSB_EVENT_ENDPOINT_DATA with node_id 0, and endpoint 0 is served by the Device's control
 * rules. Alternate settings and isochronous endpoints: see docs/API.md. */

/* mimusb_device_set_descriptors
 * Presents the bytes unchanged; usb_device, usb_config and their strings are not used. The
 * descriptors are checked structurally without reading any class (docs/API.md lists every check).
 * Every IN endpoint needs a policy given with mimusb_device_endpoint_set_policy first.
 * Ownership: Copies the bytes.
 * Blocking: Commits as a Node open does: sends the descriptors, the strings of
 * mimusb_device_set_string, the Device's control rules and the policy of every IN endpoint, then
 * waits as timeouts says. Inside a batch it only stages them.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device, a null argument, a wrong struct_size,
 * a nonzero reserved or a descriptor that does not hold together (the details name the field and
 * the offset); MIMUSB_ERR_UNSET_FIELD for a zero timeout, a bcdUSB of 0 or an IN endpoint with no
 * policy; MIMUSB_ERR_UNSUPPORTED for an isochronous endpoint on a board that has none;
 * MIMUSB_ERR_OVERFLOW when the board's limits are exceeded; MIMUSB_ERR_BUSY while a Node is open;
 * the results of mimusb_device_commit. */
MIMUSB_API mimusb_result MIMUSB_CALL
mimusb_device_set_descriptors(mimusb_device* device, const mimusb_device_descriptors* descriptors,
                              const mimusb_commit_timeouts* timeouts);

/* mimusb_device_clear_descriptors
 * Forgets the descriptors, the policies of their endpoints and what the board held for them; Nodes
 * can be opened again. The strings of mimusb_device_set_string stay. Inside a batch it only
 * stages the removal.
 * Blocking: Takes the Device off the target (a commit with nothing to present) as
 * mimusb_node_close does for the last Node.
 * Returns: MIMUSB_OK (also when no descriptors are set); MIMUSB_ERR_PARAM for an invalid Device or
 * timeouts; MIMUSB_ERR_UNSET_FIELD for a zero timeout; the results of mimusb_device_commit. */
MIMUSB_API mimusb_result MIMUSB_CALL
mimusb_device_clear_descriptors(mimusb_device* device, const mimusb_commit_timeouts* timeouts);

/* mimusb_device_endpoint_set_policy
 * The policy of an IN endpoint of a Device given as bytes. It must be given for every IN endpoint
 * before mimusb_device_set_descriptors and stays until mimusb_device_clear_descriptors.
 * Ownership: Copies policy.
 * Blocking: Before the descriptors are committed: none. After: as mimusb_node_endpoint_set_policy.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device, a null policy, a bad on_full or an
 * address that is not an IN address (0x81 to 0x8F); MIMUSB_ERR_UNSET_FIELD for on_full or depth 0
 * or a zero timeout; MIMUSB_ERR_OVERFLOW; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_endpoint_set_policy(
    mimusb_device* device, uint8_t address, const mimusb_endpoint_policy* policy,
    uint32_t control_timeout_ms);

/* mimusb_device_endpoint_write
 * As mimusb_node_endpoint_write, for an IN endpoint of a Device given as bytes. A transfer to an
 * isochronous endpoint is one packet and is not split.
 * Ownership: Copies data.
 * Blocking: As mimusb_node_endpoint_write.
 * Synchronization: Device domain; writes to different endpoints may run at the same time.
 * Returns: MIMUSB_OK; the errors of mimusb_node_endpoint_write; MIMUSB_ERR_PARAM also for an
 * address that is not an IN endpoint of the descriptors or more than an isochronous endpoint's
 * packet size; MIMUSB_ERR_BUSY also while the descriptors are staged in a batch that has not
 * ended. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_endpoint_write(mimusb_device* device,
                                                                  uint8_t address,
                                                                  const uint8_t* data,
                                                                  size_t length,
                                                                  uint32_t timeout_ms);

/* mimusb_device_endpoint_stall
 * As mimusb_node_endpoint_stall for an endpoint of a Device given as bytes.
 * Blocking: A command to the board, waiting up to control_timeout_ms.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device or an address that is not an endpoint
 * of the descriptors (none set included); MIMUSB_ERR_UNSET_FIELD for a zero timeout;
 * MIMUSB_ERR_BUSY when the board does not run the configuration; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_endpoint_stall(mimusb_device* device,
                                                                  uint8_t address,
                                                                  uint32_t control_timeout_ms);

/* mimusb_device_endpoint_clear_stall
 * As mimusb_node_endpoint_clear_stall for an endpoint of a Device given as bytes.
 * Blocking: A command to the board, waiting up to control_timeout_ms.
 * Returns: MIMUSB_OK; the errors of mimusb_device_endpoint_stall: MIMUSB_ERR_PARAM,
 * MIMUSB_ERR_UNSET_FIELD, MIMUSB_ERR_BUSY or a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_endpoint_clear_stall(
    mimusb_device* device, uint8_t address, uint32_t control_timeout_ms);

/* mimusb_device_endpoint_flush
 * As mimusb_node_endpoint_flush for an IN endpoint of a Device given as bytes.
 * Blocking: A command to the board, waiting up to control_timeout_ms.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Device or an address that is not an IN
 * endpoint of the descriptors (none set included); MIMUSB_ERR_UNSET_FIELD for a zero timeout;
 * MIMUSB_ERR_BUSY when the board does not run the configuration; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_device_endpoint_flush(mimusb_device* device,
                                                                  uint8_t address,
                                                                  uint32_t control_timeout_ms);

/* ---- Spec -------------------------------------------------------------------------------
 * The factories copy their options and every array and string the options point to. The Spec
 * holds one reference for the caller. All factories return:
 *   MIMUSB_OK; MIMUSB_ERR_PARAM for a null pointer, a wrong struct_size or a value out of range;
 *   MIMUSB_ERR_UNSET_FIELD for a required field left zero; MIMUSB_ERR_OVERFLOW when the descriptor
 *   or report would be too large; the factory's own results listed with it. */

/* mimusb_spec_create_keyboard
 * Makes a keyboard Spec.
 * Returns: As every factory, MIMUSB_OK through MIMUSB_ERR_OVERFLOW; MIMUSB_ERR_PARAM also for a
 * range that includes 0x00 to 0x03 or the modifiers. */
MIMUSB_API mimusb_result MIMUSB_CALL
mimusb_spec_create_keyboard(const mimusb_keyboard_options* options, mimusb_spec** out_spec);

/* mimusb_spec_create_mouse
 * Makes a mouse Spec.
 * Returns: As every factory, MIMUSB_OK through MIMUSB_ERR_OVERFLOW. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_spec_create_mouse(const mimusb_mouse_options* options,
                                                              mimusb_spec** out_spec);

/* mimusb_spec_create_toggle
 * Makes a Spec of one-bit controls on a HUT page.
 * Returns: As every factory, MIMUSB_OK through MIMUSB_ERR_OVERFLOW; MIMUSB_ERR_UNSUPPORTED for a
 * semantic that press and release cannot express, or a Camera Control Usage that is not
 * MIMUSB_USAGE_ONE_SHOT. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_spec_create_toggle(const mimusb_toggle_options* options,
                                                               mimusb_spec** out_spec);

/* mimusb_spec_create_gamepad
 * Makes a gamepad Spec.
 * Returns: As every factory, MIMUSB_OK through MIMUSB_ERR_OVERFLOW. */
MIMUSB_API mimusb_result MIMUSB_CALL
mimusb_spec_create_gamepad(const mimusb_gamepad_options* options, mimusb_spec** out_spec);

/* mimusb_spec_create_touchscreen
 * Makes a touchscreen Spec.
 * Returns: As every factory, MIMUSB_OK through MIMUSB_ERR_OVERFLOW. */
MIMUSB_API mimusb_result MIMUSB_CALL
mimusb_spec_create_touchscreen(const mimusb_touchscreen_options* options, mimusb_spec** out_spec);

/* mimusb_spec_create_touchpad
 * Makes a touchpad Spec.
 * Returns: As every factory, MIMUSB_OK through MIMUSB_ERR_OVERFLOW. */
MIMUSB_API mimusb_result MIMUSB_CALL
mimusb_spec_create_touchpad(const mimusb_touchpad_options* options, mimusb_spec** out_spec);

/* mimusb_spec_create_pen
 * Makes a pen Spec.
 * Returns: As every factory, MIMUSB_OK through MIMUSB_ERR_OVERFLOW. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_spec_create_pen(const mimusb_pen_options* options,
                                                            mimusb_spec** out_spec);

/* mimusb_spec_create_digitizer
 * Makes a digitizer Spec.
 * Returns: As every factory, MIMUSB_OK through MIMUSB_ERR_OVERFLOW. */
MIMUSB_API mimusb_result MIMUSB_CALL
mimusb_spec_create_digitizer(const mimusb_digitizer_options* options, mimusb_spec** out_spec);

/* mimusb_spec_create_hid
 * Makes a Spec from the caller's own HID report descriptor.
 * Returns: As every factory, MIMUSB_OK through MIMUSB_ERR_OVERFLOW; MIMUSB_ERR_PARAM also for a
 * descriptor with an item that runs past its end, a Report ID the table cannot name, or one that
 * does not match the report table; MIMUSB_ERR_UNSUPPORTED for Output or Feature items without
 * the matching requires_* flag. Nothing else about the descriptor is judged. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_spec_create_hid(const mimusb_hid_options* options,
                                                            mimusb_spec** out_spec);

/* mimusb_spec_create_cdc_acm
 * Makes a Spec of a CDC-ACM function (see mimusb_cdc_acm_options): two interfaces and the roles
 * MIMUSB_ROLE_CDC_NOTIFY (interrupt), MIMUSB_ROLE_CDC_DATA_IN and MIMUSB_ROLE_CDC_DATA_OUT (bulk),
 * all required. Its control rules (PSTN 1.2 6.3): ACCEPT for SET_LINE_CODING and
 * SET_CONTROL_LINE_STATE, ANSWER for GET_LINE_CODING (slot MIMUSB_RULE_SLOT_CDC_LINE_CODING).
 * Ownership: Copies options. The caller releases the returned reference with mimusb_spec_release.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for a null pointer, a wrong struct_size, a nonzero reserved
 * field or an invalid name; MIMUSB_ERR_UNSET_FIELD for a zero bcd_cdc or a zero dwDTERate. */
MIMUSB_API mimusb_result MIMUSB_CALL
mimusb_spec_create_cdc_acm(const mimusb_cdc_acm_options* options, mimusb_spec** out_spec);

/* mimusb_spec_create_interfaces
 * Makes a Spec of any class from interfaces written out. It carries its own interface numbers,
 * endpoints and rules, so its placement is empty. The values are checked when a Node is opened.
 * Ownership: Copies config and everything it points to. The caller releases the returned
 * reference with mimusb_spec_release.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for a null pointer, a wrong struct_size, a nonzero reserved
 * field, a list that is null with a count, or an association that is null with a length;
 * MIMUSB_ERR_OVERFLOW for more than 255 interfaces, settings or items, or 65535 rules. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_spec_create_interfaces(const mimusb_node_config* config,
                                                                   mimusb_spec** out_spec);

/* mimusb_spec_roles
 * Lists the endpoint roles of a Spec in order and whether each is required, so a caller sees what
 * its placement must give. *out_count is the number of roles the Spec has, also when capacity is
 * smaller (only capacity are written).
 * Ownership: Writes into the caller's array.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for a null Spec or out_count, or a null out_roles with a
 * nonzero capacity. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_spec_roles(const mimusb_spec* spec,
                                                       mimusb_role_info* out_roles, size_t capacity,
                                                       size_t* out_count);

/* mimusb_spec_interface_count
 * The number of interfaces a Node of the Spec takes on the bus: 1 for a HID Spec, 2 for a CDC-ACM
 * Spec, the length of the configuration for a Spec of mimusb_spec_create_interfaces. A placement's
 * first_interface is the first of that many consecutive numbers.
 * Ownership: Writes into the caller's variable.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for a null Spec or out_count. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_spec_interface_count(const mimusb_spec* spec,
                                                                 size_t* out_count);

/* mimusb_spec_rule_cost
 * What a Node of the Spec adds to the board's control rule memory, not counting the extra_rules of
 * its placement: the number of rules and the bytes of their ANSWER data.
 * Ownership: Writes into the caller's variables.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for a null Spec or output pointer. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_spec_rule_cost(const mimusb_spec* spec,
                                                           size_t* out_rules,
                                                           size_t* out_answer_bytes);

/* mimusb_spec_retain
 * Adds a reference to a Spec.
 * Ownership: Adds a reference for the caller, who must already hold one.
 * Returns: Nothing. MIMUSB_OK is recorded; MIMUSB_ERR_PARAM for null, or MIMUSB_ERR_OVERFLOW when
 * the count is full (no reference is added). */
MIMUSB_API void MIMUSB_CALL mimusb_spec_retain(mimusb_spec* spec);

/* mimusb_spec_release
 * Gives up a reference to a Spec.
 * Ownership: Gives up one of the caller's references; the last one frees the Spec. A Node keeps
 * its own reference until it closes.
 * Synchronization: Any thread, as long as the last release does not race any other use.
 * Returns: Nothing. MIMUSB_OK is recorded, or MIMUSB_ERR_PARAM for null (nothing is done). */
MIMUSB_API void MIMUSB_CALL mimusb_spec_release(mimusb_spec* spec);

/* mimusb_spec_descriptor
 * The report descriptor of a Spec.
 * Ownership: The bytes belong to the Spec.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for a null pointer. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_spec_descriptor(const mimusb_spec* spec,
                                                            const uint8_t** bytes, size_t* length);

/* mimusb_spec_manifest
 * What a Spec sends.
 * Ownership: Fills the caller's manifest; manifest->reports belongs to the Spec.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for a null pointer, a wrong struct_size or a nonzero
 * reserved. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_spec_manifest(const mimusb_spec* spec,
                                                          mimusb_capability_manifest* manifest);

/* ---- Node ------------------------------------------------------------------------------- */

/* mimusb_node_open
 * Opens a Node of any class: spec says what it is, placement says where it goes. A HID Spec's Node
 * gets the control rules HID 1.11 asks for and the events that follow from them
 * (MIMUSB_EVENT_OUTPUT_REPORT, _FEATURE_REPORT, _SET_IDLE, _SET_PROTOCOL); a CDC-ACM Spec's Node
 * gets its two interfaces and rules; a Spec of mimusb_spec_create_interfaces is opened as written.
 * Checked without knowing the class: interface numbers, endpoint addresses, transfer types, packet
 * sizes, intervals, policies, class descriptor lengths, the association and the totals against
 * mimusb_board_limits. The details name the field and the offending number; docs/API.md has the
 * full list.
 * Ownership: Copies placement and everything it points to; retains spec until the Node closes.
 * Blocking: As mimusb_device_commit: the whole configuration of the Device is sent and the board
 * enumerates again. Inside a batch the Node is only staged.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for a null argument, a wrong struct_size, a bad value, a
 * role the Spec does not have or one given twice, a gap or repeat in the interface numbers (the
 * gap's number is in mimusb_error_detail.offset; nothing is sent), a repeated endpoint address,
 * Nodes sharing an interface against the rules of mimusb_node_placement, or an association that
 * does not match; MIMUSB_ERR_UNSET_FIELD for a required field, a required role or a timeout left
 * zero (for a HID Spec, also a target.linux_hid member); MIMUSB_ERR_UNSUPPORTED for a transfer type
 * the board does not have; MIMUSB_ERR_OVERFLOW when a limit of the board is exceeded; the results
 * of mimusb_device_commit. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_node_open(mimusb_device* device, mimusb_spec* spec,
                                                      const mimusb_node_placement* placement,
                                                      const mimusb_commit_timeouts* timeouts,
                                                      mimusb_node** out_node);

/* mimusb_node_close
 * Closing a Node whose interface number is not the highest in use leaves a gap: the Node is gone,
 * the commit fails with MIMUSB_ERR_PARAM, and a later commit succeeds once the gap is closed.
 * Ownership: Follows the "Closing" rule.
 * Blocking: Sends the neutral report within drain_timeout_ms, then commits the remaining Nodes as
 * mimusb_node_open does; closing the last Node takes the USB device off the target. Inside a batch
 * it only stages the removal. A failed step does not stop the next one.
 * Synchronization: Device domain; not from the event sink.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for a null or closed Node, a closing Device, or a wrong
 * timeouts struct_size; MIMUSB_ERR_UNSET_FIELD for a zero timeout; otherwise the first error on
 * the way out. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_node_close(mimusb_node* node,
                                                       const mimusb_commit_timeouts* timeouts,
                                                       uint32_t drain_timeout_ms);

/* mimusb_node_id
 * The number the board knows a Node by.
 * Returns: The number the board knows the Node by, plus one (so it is nonzero), with MIMUSB_OK
 * recorded; or 0 with MIMUSB_ERR_PARAM recorded for a null or closed Node. */
MIMUSB_API uint16_t MIMUSB_CALL mimusb_node_id(const mimusb_node* node);

/* mimusb_node_set_feature_response
 * Sets what the board answers (the bytes after the Report ID, at most max_feature_bytes) when the
 * target reads Feature report report_id of this Node.
 * Ownership: Copies data.
 * Blocking: Waits up to control_timeout_ms for the board's answer; inside a batch, or for a Node
 * not yet committed, it is sent with the commit instead.
 * Synchronization: Device domain; profile Nodes only.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Node, null data with a nonzero length, more
 * than max_feature_bytes, or a Node of a mimusb_spec_create_interfaces Spec;
 * MIMUSB_ERR_UNSET_FIELD for a zero timeout; MIMUSB_ERR_OVERFLOW when the board has no room for
 * the rule or its bytes; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_node_set_feature_response(mimusb_node* node,
                                                                      uint8_t report_id,
                                                                      const uint8_t* data,
                                                                      size_t length,
                                                                      uint32_t control_timeout_ms);

/* mimusb_node_endpoint_set_policy
 * Changes what an IN endpoint does when its queue is full and how many transfers may wait. The
 * change takes effect at once only if the queue cut at the last commit holds it; otherwise the
 * board refuses it with MIMUSB_ERR_OVERFLOW (slot arithmetic: mimusb_board_limits). Nodes that
 * share an interface share the least depth any of them set. It resets the endpoint's wait count.
 * Ownership: Copies policy.
 * Blocking: A command to the board, waiting up to control_timeout_ms; none inside a batch or for a
 * Node not yet committed: the policy is then sent with the commit.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Node, a null policy, an address that is not
 * an IN endpoint of the Node or a bad on_full; MIMUSB_ERR_UNSET_FIELD for on_full or depth 0 or a
 * zero timeout; MIMUSB_ERR_OVERFLOW; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL
mimusb_node_endpoint_set_policy(mimusb_node* node, uint8_t address,
                                const mimusb_endpoint_policy* policy, uint32_t control_timeout_ms);

/* mimusb_node_endpoint_write
 * Sends data as one USB transfer on an IN endpoint of a Node. A length of 0 sends a zero-length
 * packet (USB 2.0 5.8.3). A length above max_transfer_bytes is split into frames that are multiples
 * of max_packet_size except the last; the library never adds a zero-length packet. Writes to one
 * endpoint are in order. The wait policy and the wait count are described in docs/API.md.
 * Ownership: Copies data.
 * Blocking: With MIMUSB_FULL_WAIT, until the board has room or timeout_ms has passed (required, 0
 * is MIMUSB_ERR_UNSET_FIELD); no Device lock is held while it waits. With the drop policies only
 * until the serial driver has the frames.
 * Synchronization: Device domain; writes to different endpoints may run at the same time.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Node, an address that is not an IN endpoint
 * of the Node, or null data with a nonzero length; MIMUSB_ERR_UNSET_FIELD as above;
 * MIMUSB_ERR_TIMEOUT when timeout_ms passed with nothing sent; MIMUSB_ERR_SHORT_TRANSFER when it
 * passed after some frames of a split write; MIMUSB_ERR_BUSY when a flush, stall, policy change,
 * unmount or commit reset the wait, for a Node staged in a batch, or while the board enumerates;
 * MIMUSB_ERR_OVERFLOW for a transfer of more than 65535 frames; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_node_endpoint_write(mimusb_node* node, uint8_t address,
                                                                const uint8_t* data, size_t length,
                                                                uint32_t timeout_ms);

/* mimusb_node_endpoint_stall
 * Sets the Halt feature of an endpoint of a Node (USB 2.0 9.4.5): the endpoint answers STALL until
 * it is cleared by mimusb_node_endpoint_clear_stall or by the target. On an IN endpoint what waits
 * on the board is dropped. It resets the wait count.
 * Blocking: A command to the board, waiting up to control_timeout_ms.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Node or an address that is not an endpoint
 * of the Node; MIMUSB_ERR_UNSET_FIELD for a zero timeout; MIMUSB_ERR_BUSY when the board does not
 * run the configuration; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_node_endpoint_stall(mimusb_node* node, uint8_t address,
                                                                uint32_t control_timeout_ms);

/* mimusb_node_endpoint_clear_stall
 * As mimusb_node_endpoint_stall, clearing the Halt feature: an IN endpoint goes on with what
 * waits, an OUT endpoint accepts packets again. This resets the wait count.
 * Blocking: A command to the board, waiting up to control_timeout_ms.
 * Returns: MIMUSB_OK; the errors of mimusb_node_endpoint_stall: MIMUSB_ERR_PARAM,
 * MIMUSB_ERR_UNSET_FIELD, MIMUSB_ERR_BUSY or a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_node_endpoint_clear_stall(mimusb_node* node,
                                                                      uint8_t address,
                                                                      uint32_t control_timeout_ms);

/* mimusb_node_endpoint_flush
 * Drops what waits on an IN endpoint of the board; a transfer the board's controller already has
 * completes. This resets the wait count.
 * Blocking: A command to the board, waiting up to control_timeout_ms.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for an invalid Node, an address that is not an IN endpoint
 * of the Node (an OUT endpoint too); MIMUSB_ERR_UNSET_FIELD for a zero timeout; MIMUSB_ERR_BUSY
 * when the board does not run the configuration; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_node_endpoint_flush(mimusb_node* node, uint8_t address,
                                                                uint32_t control_timeout_ms);

/* mimusb_node_manifest
 * What a Node sends.
 * Ownership: As mimusb_spec_manifest; manifest->reports is valid while the Node is open.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for a null pointer, a wrong struct_size or a nonzero
 * reserved. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_node_manifest(const mimusb_node* node,
                                                          mimusb_capability_manifest* manifest);

/* mimusb_node_submit
 * Sends the Node's current state as a report.
 * Ownership: The report is built in the library's own buffer.
 * Blocking: Writes one report of the Node's current state, if it changed, and returns once the
 * serial driver has it. It never waits for the target; send_timeout_ms only bounds a write that
 * makes no progress.
 * Returns: MIMUSB_OK, also when there was nothing to send; MIMUSB_ERR_PARAM for an invalid Node;
 * MIMUSB_ERR_UNSET_FIELD for a zero timeout; MIMUSB_ERR_UNSUPPORTED for a generic HID Node (use
 * mimusb_hid_submit); MIMUSB_ERR_BUSY for a Node opened in a batch that has not ended, while the
 * board enumerates, or while a report of the Node is being sent; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_node_submit(mimusb_node* node,
                                                        uint32_t send_timeout_ms);

/* mimusb_node_submit_blocking
 * Sends reports until the Node's state has been sent.
 * Ownership: As mimusb_node_submit.
 * Blocking: Writes reports until the Node's state has been sent, a touch frame of several reports
 * and a mouse's pending movement included, or timeout_ms has passed; each write is bounded by what
 * is left of it.
 * Returns: MIMUSB_OK; MIMUSB_ERR_TIMEOUT when timeout_ms passed first; the results of
 * mimusb_node_submit. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_node_submit_blocking(mimusb_node* node,
                                                                 uint32_t timeout_ms);

/* ---- Input ------------------------------------------------------------------------------
 * These calls change a Node's state; mimusb_node_submit sends it. None of them blocks, allocates
 * or does I/O. Each belongs to the Device domain and works only on a Node of its own profile.
 * A press and the release after it are never merged: until the report carrying the press has been
 * sent, the release is refused with MIMUSB_ERR_BUSY, so a quick tap is not lost. Every input call
 * returns:
 *   MIMUSB_OK; MIMUSB_ERR_PARAM for a Node of another profile or a value out of range;
 *   MIMUSB_ERR_BUSY while a report of the Node is being sent, an opposite edge is unsent, the
 *   Node was opened in a batch that has not ended, or the board enumerates;
 *   MIMUSB_ERR_NO_DEVICE once the link is gone; the call's own results listed with it. */

/* mimusb_keyboard_key
 * Presses (down 1) or releases (down 0) one Keyboard page Usage, a modifier or a key of the Spec's
 * range. Closing the Node releases every key still down.
 * Returns: As every input call, MIMUSB_OK through MIMUSB_ERR_NO_DEVICE. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_keyboard_key(mimusb_node* node, uint16_t usage,
                                                         uint32_t down);

/* mimusb_mouse_move
 * Adds to the pending X and Y movement. A report carries as much as the fields hold and keeps the
 * rest for the next one.
 * Returns: As every input call, MIMUSB_OK through MIMUSB_ERR_NO_DEVICE; MIMUSB_ERR_OVERFLOW when
 * the 64-bit pending total overflows. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_mouse_move(mimusb_node* node, int32_t dx, int32_t dy);

/* mimusb_mouse_scroll
 * Adds to the pending wheel and pan movement, as mimusb_mouse_move does.
 * Returns: As every input call, MIMUSB_OK through MIMUSB_ERR_NO_DEVICE; MIMUSB_ERR_UNSUPPORTED
 * for a nonzero value on a field the Spec lacks; MIMUSB_ERR_OVERFLOW when the 64-bit pending total
 * overflows. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_mouse_scroll(mimusb_node* node, int32_t wheel,
                                                         int32_t pan);

/* mimusb_mouse_button
 * Presses (down 1) or releases (down 0) button 1 to button_count.
 * Returns: As every input call, MIMUSB_OK through MIMUSB_ERR_NO_DEVICE. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_mouse_button(mimusb_node* node, uint32_t button,
                                                         uint32_t down);

/* mimusb_toggle_usage
 * Presses (down 1) or releases (down 0) one Usage of the Spec's allow-list; any number may be
 * pressed at once. Closing the Node releases every Usage still down.
 * Returns: As every input call, MIMUSB_OK through MIMUSB_ERR_NO_DEVICE. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_toggle_usage(mimusb_node* node, uint16_t usage,
                                                         uint32_t down);

/* mimusb_gamepad_button
 * Presses (down 1) or releases (down 0) button 1 to button_count.
 * Returns: As every input call, MIMUSB_OK through MIMUSB_ERR_NO_DEVICE. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_gamepad_button(mimusb_node* node, uint32_t button,
                                                           uint32_t down);

/* mimusb_gamepad_axis
 * Sets axes[axis_index] of the Spec to value, which must be in its logical range.
 * Returns: As every input call, MIMUSB_OK through MIMUSB_ERR_NO_DEVICE. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_gamepad_axis(mimusb_node* node, size_t axis_index,
                                                         int32_t value);

/* mimusb_gamepad_dpad
 * Sets the D-pad from four 0/1 directions. With a Hat, up and down (or left and right) together
 * are refused and no direction sends the Null state; with four buttons the bits go out as given.
 * Returns: As every input call, MIMUSB_OK through MIMUSB_ERR_NO_DEVICE; MIMUSB_ERR_PARAM also
 * when the Spec has no D-pad. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_gamepad_dpad(mimusb_node* node, uint32_t up,
                                                         uint32_t down, uint32_t right,
                                                         uint32_t left);

/* mimusb_touchscreen_contact
 * down 1 places contact_id in a free slot, or moves it if already down; down 0 lifts it at x, y.
 * A lifted contact_id can be placed again once its lift has been sent.
 * Ownership: Copies extra, which may be null (every optional field 0).
 * Synchronization: Device domain.
 * Returns: As every input call, MIMUSB_OK through MIMUSB_ERR_NO_DEVICE; MIMUSB_ERR_PARAM also for
 * lifting a contact_id that is not down; MIMUSB_ERR_BUSY also for placing one whose lift is unsent
 * or while a split frame is being sent; MIMUSB_ERR_OVERFLOW when every slot is taken. */
MIMUSB_API mimusb_result MIMUSB_CALL
mimusb_touchscreen_contact(mimusb_node* node, uint32_t contact_id, uint32_t down, int32_t x,
                           int32_t y, const mimusb_touchscreen_extra* extra);

/* mimusb_touchpad_contact
 * As mimusb_touchscreen_contact, for a Touchpad Node.
 * Ownership: Copies extra, which may be null.
 * Synchronization: Device domain.
 * Returns: As mimusb_touchscreen_contact, MIMUSB_OK through MIMUSB_ERR_OVERFLOW. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_touchpad_contact(mimusb_node* node, uint32_t contact_id,
                                                             uint32_t down, int32_t x, int32_t y,
                                                             const mimusb_touchpad_extra* extra);

/* mimusb_touchpad_button
 * Presses (down 1) or releases (down 0) click button 1 to button_count.
 * Synchronization: Device domain.
 * Returns: As every input call, MIMUSB_OK through MIMUSB_ERR_NO_DEVICE. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_touchpad_button(mimusb_node* node, uint32_t button,
                                                            uint32_t down);

/* mimusb_pen_update
 * Sets the whole pen state. in_range 0 takes the pen away: In Range, Tip, pressure, Invert and the
 * barrel bits go out as 0 with the sample's own X, Y, tilt and twist, so repeat the last position
 * to leave the pointer where it was. Switching between tip and eraser while in range first sends
 * an out-of-range report.
 * Ownership: Copies sample.
 * Returns: As every input call, MIMUSB_OK through MIMUSB_ERR_NO_DEVICE. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_pen_update(mimusb_node* node,
                                                       const mimusb_pen_sample* sample);

/* mimusb_digitizer_update
 * As mimusb_pen_update, for a Digitizer Node.
 * Ownership: Copies sample.
 * Returns: As every input call, MIMUSB_OK through MIMUSB_ERR_NO_DEVICE. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_digitizer_update(mimusb_node* node,
                                                             const mimusb_digitizer_sample* sample);

/* mimusb_battery_update
 * Sets the battery level of a Node whose Spec has enable_battery. It goes out with the next
 * mimusb_node_submit that has no input report to send, and only when it changed. A level not yet
 * sent is dropped on close.
 * Returns: As every input call, MIMUSB_OK through MIMUSB_ERR_NO_DEVICE; MIMUSB_ERR_PARAM also for
 * a Node without a battery report. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_battery_update(mimusb_node* node, int32_t strength);

/* ---- Reports of the caller's own -------------------------------------------------------- */

/* mimusb_hid_submit
 * Sends one report of a HID Spec's Node after checking it against the Spec's report table.
 * Ownership: Copies report.
 * Blocking: As mimusb_node_submit.
 * Returns: MIMUSB_OK; MIMUSB_ERR_PARAM for a Node that is not a HID Spec, a null report, a Report
 * ID or length the Spec does not accept, or nonzero padding bits; MIMUSB_ERR_UNSET_FIELD for a zero
 * timeout; MIMUSB_ERR_BUSY as for mimusb_node_submit; MIMUSB_ERR_OVERFLOW for a report longer than
 * maximum_report_bytes; a link error. */
MIMUSB_API mimusb_result MIMUSB_CALL mimusb_hid_submit(mimusb_node* node, const uint8_t* report,
                                                       size_t length, uint32_t send_timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
