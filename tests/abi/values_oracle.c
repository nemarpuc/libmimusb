// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
/* Prints the bytes of the structures tests/fill_values.h fills, the values the language bindings'
 * working-values functions (mimusb.values and its siblings) must equal. The test identity and baud
 * rate below are what test_fill_device_config sets; a binding is given the same ones. */
#include "fill_values.h"

#include <stdio.h>

#define DUMP(name, value)                                                                   \
    do {                                                                                    \
        const unsigned char* bytes = (const unsigned char*)&(value);                        \
        printf("%s=", name);                                                                \
        for (size_t i = 0; i < sizeof(value); ++i) printf("%02x", (unsigned)bytes[i]);      \
        printf("\n");                                                                       \
    } while (0)

int main(void) {
    mimusb_device_config config;
    mimusb_commit_timeouts timeouts;
    test_hid_node_config node;
    mimusb_keyboard_options keyboard;
    mimusb_mouse_options mouse;
    mimusb_toggle_options toggle;
    mimusb_gamepad_options gamepad;
    mimusb_touchscreen_options touchscreen;
    mimusb_touchpad_options touchpad;
    mimusb_pen_options pen;
    mimusb_digitizer_options digitizer;

    test_fill_device_config(&config);
    test_fill_commit_timeouts(&timeouts);
    test_fill_keyboard(&keyboard);
    test_fill_mouse(&mouse);
    test_fill_toggle(&toggle);
    test_fill_gamepad(&gamepad);
    test_fill_touchscreen(&touchscreen);
    test_fill_touchpad(&touchpad);
    test_fill_pen(&pen);
    test_fill_digitizer(&digitizer);
    DUMP("device_config", config);
    DUMP("commit_timeouts", timeouts);
    DUMP("keyboard_options", keyboard);
    DUMP("mouse_options", mouse);
    DUMP("toggle_options", toggle);
    DUMP("gamepad_options", gamepad);
    DUMP("touchscreen_options", touchscreen);
    DUMP("touchpad_options", touchpad);
    DUMP("pen_options", pen);
    DUMP("digitizer_options", digitizer);
    test_fill_node_config(&node, 0U);
    DUMP("hid_endpoint0", node.ep_in);
    test_fill_node_config(&node, 3U);
    DUMP("hid_endpoint3", node.ep_in);
    printf("gamepad_axes=%s,%s,%s,%s\n", gamepad.axes[0].expected_linux_code, gamepad.axes[1].expected_linux_code,
           gamepad.axes[2].expected_linux_code, gamepad.axes[3].expected_linux_code);
    printf("strings=%s,%s\n", config.usb_device.manufacturer, config.usb_device.product);
    return 0;
}
