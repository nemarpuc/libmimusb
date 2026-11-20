// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
/*
 * The events the board reports, turned into the caller's events on the reader thread: mount and
 * suspend, control setups and data, lost transfers, halts and interface changes.
 */

#include "api/support.hpp"
#include "transport/link.hpp"

#include <algorithm>
#include <mutex>
#include <vector>

namespace tr = mimusb::transport;

namespace mimusb::detail {

namespace {

// The request matches the rule: each field with its mask equals its value.
bool rule_matches(const mimusb_control_rule& r, const std::uint8_t* setup) {
    const std::uint16_t value = static_cast<std::uint16_t>(setup[2] | (setup[3] << 8));
    const std::uint16_t index = static_cast<std::uint16_t>(setup[4] | (setup[5] << 8));
    return (setup[0] & r.request_type_mask) == r.request_type_value &&
           (setup[1] & r.request_mask) == r.request_value &&
           (value & r.value_mask) == r.value_value && (index & r.index_mask) == r.index_value;
}

} // namespace

// The public id of the live Node that owns an endpoint address, and the interface it is on; 0 when
// none does.
std::uint16_t node_of_endpoint(mimusb_device* device, const std::uint32_t address,
                               std::uint32_t* interface_number) {
    std::lock_guard<std::mutex> lock(device->nodes_mutex);
    for (const mimusb_node* node : device->slots) {
        if (node == nullptr || node->closed)
            continue;
        for (const GenericInterface& itf : node->interfaces) {
            bool owns = false;
            itf.for_each_endpoint([&](const mimusb_usb_endpoint& ep) {
                if (ep.address == address)
                    owns = true;
            });
            if (owns) {
                *interface_number = itf.number;
                return public_node_id(node);
            }
        }
    }
    // A Device given as bytes has no Node: the id is 0, and the interface is the one the
    // configuration puts the endpoint on.
    if (device->descriptors_set) {
        const auto found = device->bytes.interface_of.find(static_cast<std::uint8_t>(address));
        if (found != device->bytes.interface_of.end())
            *interface_number = found->second;
    }
    return 0U;
}

// The public id of the live Node that owns an interface; 0 when none does (a Device given as
// bytes has no Node).
std::uint16_t node_of_interface(mimusb_device* device, const std::uint8_t interface_number) {
    std::lock_guard<std::mutex> lock(device->nodes_mutex);
    for (const mimusb_node* node : device->slots) {
        if (node == nullptr || node->closed)
            continue;
        for (const std::uint8_t number : node->interface_numbers) {
            if (number == interface_number)
                return public_node_id(node);
        }
    }
    return 0U;
}

// The class of the live Node that owns an endpoint address (a static object, valid for as long
// as the library is loaded), and the Node's first interface; null when no Node owns it.
static const FunctionClass* class_of_endpoint(mimusb_device* device, const std::uint32_t address,
                                              std::uint8_t* iface) {
    std::lock_guard<std::mutex> lock(device->nodes_mutex);
    for (const mimusb_node* node : device->slots) {
        if (node == nullptr || node->closed)
            continue;
        if (std::find(node->endpoint_addresses.begin(), node->endpoint_addresses.end(), address) !=
            node->endpoint_addresses.end()) {
            *iface = node->iface;
            return node->spec->function_class;
        }
    }
    return nullptr;
}

void deliver_event(mimusb_device* device, const tr::BoardEvent& board) noexcept {
    const std::uint8_t* d = board.data.data();

    // The wait policy's count is kept whether or not anyone listens for events.
    if (board.kind == MIMUSB_EV_EP_DONE) {
        if (board.length < 3U)
            return;
        const std::uint32_t done = static_cast<std::uint32_t>(d[1] | (d[2] << 8));
        {
            // As reset_endpoint_counts: nodes_mutex for the whole walk, then endpoint_mutex.
            std::lock_guard<std::mutex> nodes(device->nodes_mutex);
            std::lock_guard<std::mutex> lock(device->endpoint_mutex);
            for (mimusb_node* node : device->slots) {
                if (node == nullptr || node->closed)
                    continue;
                for (EndpointRuntime& ep : node->in_endpoints) {
                    if (ep.address == d[0])
                        ep.outstanding = ep.outstanding > done ? ep.outstanding - done : 0U;
                }
            }
            for (EndpointRuntime& ep : device->bytes.in_endpoints) {
                if (ep.address == d[0])
                    ep.outstanding = ep.outstanding > done ? ep.outstanding - done : 0U;
            }
        }
        device->endpoint_cv.notify_all();
        return;
    }
    // The target left: nothing the board held for it will be reported done (a flush of its own).
    if (board.kind == MIMUSB_EV_UNMOUNTED) {
        reset_endpoint_counts(device);
        device->pending.clear();
    }
    if (board.kind == MIMUSB_EV_MOUNTED) {
        device->pending.clear();
        reset_live_answers(device);
    }
    mimusb_event event{};
    event.struct_size = sizeof(event);
    switch (board.kind) {
    case MIMUSB_EV_MOUNTED:
        event.kind = MIMUSB_EVENT_MOUNTED;
        break;
    case MIMUSB_EV_UNMOUNTED:
        event.kind = MIMUSB_EVENT_UNMOUNTED;
        break;
    case MIMUSB_EV_SUSPEND:
        event.kind = MIMUSB_EVENT_SUSPEND;
        event.value = board.length >= 1U ? d[0] : 0U;
        break;
    case MIMUSB_EV_RESUME:
        event.kind = MIMUSB_EVENT_RESUME;
        break;
    case MIMUSB_EV_CONTROL_SETUP: {
        if (board.length < 10U)
            return;
        const std::uint8_t* setup = d + 2;
        if ((d[1] & MIMUSB_CONTROL_F_ACCEPTED) != 0U) {
            // An ACCEPT rule took it. When a class wrote that rule, the class says what the
            // program hears of it.
            usb::BoardRule taken;
            {
                std::lock_guard<std::mutex> lock(device->rules_mutex);
                for (const usb::BoardRule& entry : device->table) {
                    if (rule_matches(entry.rule, setup)) {
                        taken.owner = entry.owner;
                        taken.tag = entry.tag;
                        taken.iface = entry.iface;
                        taken.rule = entry.rule;
                        break;
                    }
                }
            }
            if (taken.owner != nullptr) {
                const Taken what = taken.owner->on_accepted(device, taken, setup, &event);
                if (what == Taken::wait_data) {
                    // The data stage follows as CONTROL_DATA with this token.
                    if (device->pending.size() >= 8U)
                        device->pending.erase(device->pending.begin());
                    device->pending.push_back(
                        PendingData{d[0], taken.owner, taken.tag, taken.iface,
                                    static_cast<std::uint16_t>(setup[2] | (setup[3] << 8))});
                    return;
                }
                if (what == Taken::deliver)
                    break;
                event = mimusb_event{};
                event.struct_size = sizeof(event);
            }
        }
        if (device->config.transport.event_sink == nullptr)
            return;
        event.kind = MIMUSB_EVENT_CONTROL_SETUP;
        event.control_token = d[0];
        event.control_flags = d[1];
        event.data = d + 2;
        event.length = 8U;
        break;
    }
    case MIMUSB_EV_CONTROL_DATA: {
        if (board.length < 1U)
            return;
        bool delivered = false;
        for (auto it = device->pending.begin(); it != device->pending.end(); ++it) {
            if (it->token != d[0])
                continue;
            const PendingData found = *it;
            device->pending.erase(it);
            delivered =
                found.owner->on_control_data(device, found, d + 1, board.length - 1U, &event);
            break;
        }
        if (delivered)
            break;
        if (device->config.transport.event_sink == nullptr)
            return;
        event = mimusb_event{};
        event.struct_size = sizeof(event);
        event.kind = MIMUSB_EVENT_CONTROL_DATA;
        event.control_token = d[0];
        event.data = d + 1;
        event.length = board.length - 1U;
        break;
    }
    case MIMUSB_EV_TRANSFER_LOST: {
        if (board.length < 3U)
            return;
        event.kind = MIMUSB_EVENT_TRANSFER_LOST;
        event.value = static_cast<std::uint32_t>(d[1] | (d[2] << 8));
        event.endpoint_address = d[0];
        event.node_id = node_of_endpoint(device, d[0], &event.interface_number);
        break;
    }
    case MIMUSB_EV_EP_HALT:
        if (board.length < 2U)
            return;
        event.kind = MIMUSB_EVENT_ENDPOINT_HALT;
        event.value = d[1];
        event.endpoint_address = d[0];
        event.node_id = node_of_endpoint(device, d[0], &event.interface_number);
        break;
    case MIMUSB_EV_SET_INTERFACE: {
        if (board.length < 2U)
            return;
        // The board switched the interface's isochronous endpoints and dropped what waited for
        // the ones that stopped: the writers that wait on those endpoints start counting again.
        {
            std::lock_guard<std::mutex> nodes(device->nodes_mutex);
            std::lock_guard<std::mutex> lock(device->endpoint_mutex);
            for (EndpointRuntime& ep : device->bytes.in_endpoints) {
                const auto found = device->bytes.interface_of.find(ep.address);
                if (found != device->bytes.interface_of.end() && found->second == d[0]) {
                    ep.outstanding = 0U;
                    ++ep.epoch;
                }
            }
            // The same for the IN endpoints of the Nodes' interfaces: the writers that wait on
            // them start counting again.
            for (mimusb_node* node : device->slots) {
                if (node == nullptr || node->closed)
                    continue;
                for (const GenericInterface& itf : node->interfaces) {
                    if (itf.number != d[0])
                        continue;
                    itf.for_each_endpoint([&](const mimusb_usb_endpoint& e) {
                        for (EndpointRuntime& ep : node->in_endpoints) {
                            if (ep.address == e.address) {
                                ep.outstanding = 0U;
                                ++ep.epoch;
                            }
                        }
                    });
                }
            }
        }
        device->endpoint_cv.notify_all();
        event.kind = MIMUSB_EVENT_INTERFACE_ALTERNATE;
        event.interface_number = d[0];
        event.value = d[1];
        event.node_id = node_of_interface(device, d[0]);
        break;
    }
    default:
        if (board.kind > MIMUSB_T_EP_OUT_BASE && board.kind <= MIMUSB_T_EP_OUT_LAST) {
            // A packet on an OUT endpoint, as it came (a zero-length one has length 0). The class
            // of the Node that owns the endpoint may say what it is (an Output report).
            const auto ep_address = static_cast<std::uint8_t>(board.kind & 0x0FU);
            std::uint8_t iface = 0U;
            const FunctionClass* owner_class = class_of_endpoint(device, ep_address, &iface);
            if (owner_class != nullptr &&
                owner_class->on_endpoint_data(device, iface, ep_address, d, board.length, &event))
                break;
            event = mimusb_event{};
            event.struct_size = sizeof(event);
            event.kind = MIMUSB_EVENT_ENDPOINT_DATA;
            event.endpoint_address = ep_address;
            event.data = d;
            event.length = board.length;
            event.node_id =
                node_of_endpoint(device, event.endpoint_address, &event.interface_number);
            break;
        }
        return;
    }
    if (device->config.transport.event_sink == nullptr)
        return;
    try {
        device->config.transport.event_sink(device->config.transport.event_user, &event);
    } catch (...) {
        // A sink is foreign callback code and must not unwind through the reader thread.
    }
}

} // namespace mimusb::detail
