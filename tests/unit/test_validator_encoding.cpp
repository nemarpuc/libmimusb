// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
/* Raw descriptor validation: the structure and encoding rules the library leaves to the target
 * (collections, control items, Usage ranges, flag bits) and how Usage ranges are counted. */
#include "hid/validator.hpp"
#include "raw_descriptor_support.hpp"
#include "test.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

using namespace mimusb_test;

namespace {

// Whether a collection has a Usage, whether control data carries its Usage Page, Usage and
// Logical extents, how the Collections nest, what the flag bits of a Main item are: the library
// does not check any of it.
void test_structure_the_target_decides() {
    const mimusb_hid_report report = idless_report(1U);
    const auto check_accepted = [&report](const std::vector<std::uint8_t>& descriptor,
                                          const bool has_input) {
        MIMUSB_CHECK(mimusb::hid::validate_raw(descriptor.data(), descriptor.size(),
                                               has_input ? &report : nullptr, has_input ? 1U : 0U)
                         .result == MIMUSB_OK);
    };

    check_accepted({0x05U, 0x01U, 0xA1U, 0x01U, 0xC0U}, false); // a Collection without a Usage
    check_accepted({0x05U, 0x01U, 0x09U, 0x06U}, false);        // no Collection at all
    check_accepted({0xC0U, 0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0xC0U}, false);
    check_accepted({0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U}, false);        // never closed
    check_accepted({0x05U, 0x01U, 0x09U, 0x01U, 0xA1U, 0x00U, 0xC0U}, false); // top level Physical

    // Control data without its Usage, Logical Minimum, Logical Maximum or Usage Page: the target
    // takes a data item with no Usage as padding.
    for (const std::vector<std::uint8_t>& globals :
         {std::vector<std::uint8_t>{0x15U, 0x00U, 0x25U, 0x01U, 0x75U, 0x01U, 0x95U, 0x01U},
          std::vector<std::uint8_t>{0x09U, 0x01U, 0x25U, 0x01U, 0x75U, 0x01U, 0x95U, 0x01U},
          std::vector<std::uint8_t>{0x09U, 0x01U, 0x15U, 0x00U, 0x75U, 0x01U, 0x95U, 0x01U}}) {
        auto descriptor = application_preamble();
        descriptor.insert(descriptor.end(), globals.begin(), globals.end());
        descriptor.insert(descriptor.end(), {0x81U, 0x02U, 0xC0U});
        check_accepted(descriptor, true);
    }
    check_accepted({0x0BU, 0x06U, 0x00U, 0x01U, 0x00U, 0xA1U, 0x01U, 0x0BU,
                    0x01U, 0x00U, 0x01U, 0x00U, 0x15U, 0x00U, 0x25U, 0x01U,
                    0x75U, 0x01U, 0x95U, 0x01U, 0x81U, 0x02U, 0xC0U},
                   true);

    // A reserved Main tag or item type is passed over, and so is a reserved Local tag.
    for (const std::vector<std::uint8_t>& extra :
         {std::vector<std::uint8_t>{0xD0U}, std::vector<std::uint8_t>{0x0DU, 0x00U},
          std::vector<std::uint8_t>{0x69U, 0x00U}, std::vector<std::uint8_t>{0x0CU}}) {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U});
        descriptor.insert(descriptor.end(), extra.begin(), extra.end());
        descriptor.insert(descriptor.end(), {0x81U, 0x02U, 0xC0U});
        check_accepted(descriptor, true);
    }

    // Reserved Collection types, reserved flag bits, Buffered Bytes off a byte boundary and
    // delimiters on an Input item.
    {
        auto descriptor = application_preamble();
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0xA1U, 0x07U});
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0xC0U, 0xC0U});
        check_accepted(descriptor, true);
    }
    for (const std::vector<std::uint8_t>& input :
         {std::vector<std::uint8_t>{0x82U, 0x80U, 0x00U},
          std::vector<std::uint8_t>{0x82U, 0x00U, 0x02U}}) {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U});
        descriptor.insert(descriptor.end(), input.begin(), input.end());
        descriptor.push_back(0xC0U);
        check_accepted(descriptor, true);
    }
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0x75U, 0x08U, 0x95U, 0x01U,
                                             0x09U, 0x02U, 0x82U, 0x00U, 0x01U, 0xC0U});
        const mimusb_hid_report two = idless_report(2U);
        MIMUSB_CHECK(
            mimusb::hid::validate_raw(descriptor.data(), descriptor.size(), &two, 1U).result ==
            MIMUSB_OK);
    }
    for (const std::vector<std::uint8_t>& delimited :
         {std::vector<std::uint8_t>{0xA9U, 0x02U, 0x09U, 0x01U, 0xA9U, 0x00U, 0x81U, 0x02U},
          std::vector<std::uint8_t>{0xA9U, 0x01U, 0x09U, 0x01U, 0xA9U, 0x00U, 0x81U, 0x01U}}) {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), delimited.begin(), delimited.end());
        descriptor.push_back(0xC0U);
        check_accepted(descriptor, true);
    }
    // A Usage Minimum that is extended while the Maximum is short.
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(),
                          {0x1BU, 0x01U, 0x00U, 0x01U, 0x00U, 0x29U, 0x02U, 0x81U, 0x02U, 0xC0U});
        check_accepted(descriptor, true);
    }
}

// How Usage ranges are counted, and the Local items the library ignores.
void test_usage_ranges_and_local_items() {
    MIMUSB_CHECK(mimusb::hid::unit_is_valid(0U));
    MIMUSB_CHECK(mimusb::hid::unit_is_valid(0x11U));
    MIMUSB_CHECK(mimusb::hid::unit_is_valid(0x14U));
    MIMUSB_CHECK(mimusb::hid::unit_is_valid(0x0FU));
    MIMUSB_CHECK(!mimusb::hid::unit_is_valid(0x05U));
    MIMUSB_CHECK(!mimusb::hid::unit_is_valid(0x10000011U));
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        // The specified target expands a short Usage range from the raw Local
        // item values, then hid_add_usage() concatenates the Usage Page. The
        // loop is therefore fffe..ffff (two entries), not a 32-bit wrap.
        descriptor.insert(descriptor.end(), {0x06U, 0xFFU, 0xFFU, 0x1AU, 0xFEU, 0xFFU, 0x2AU, 0xFFU,
                                             0xFFU, 0x81U, 0x02U, 0xC0U});
        const mimusb_hid_report report = idless_report(1U);
        mimusb::hid::DescriptorRequirements requirements{};
        MIMUSB_CHECK(mimusb::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U,
                                               nullptr, &requirements)
                         .result == MIMUSB_OK);
        MIMUSB_CHECK(requirements.maximum_usages == 2U);
    }
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        // HID 1.11 gives the short Minimum page 1 and the extended Maximum an
        // embedded page 1. The specified target nevertheless expands its raw
        // item_udata range 1..00010002 before adding pages. Record that exact
        // parser requirement so its 12,288-Usage policy can reject it before
        // registration instead of understating it as two entries.
        descriptor.insert(descriptor.end(),
                          {0x19U, 0x01U, 0x2BU, 0x02U, 0x00U, 0x01U, 0x00U, 0x81U, 0x02U, 0xC0U});
        const mimusb_hid_report report = idless_report(1U);
        mimusb::hid::DescriptorRequirements requirements{};
        MIMUSB_CHECK(mimusb::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U,
                                               nullptr, &requirements)
                         .result == MIMUSB_OK);
        MIMUSB_CHECK(requirements.maximum_usages == 65538U);
    }
    {
        // A Usage Maximum of UINT32_MAX is counted like any other.
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x1BU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x2BU, 0xFFU, 0xFFU,
                                             0xFFU, 0xFFU, 0x81U, 0x02U, 0xC0U});
        const mimusb_hid_report report = idless_report(1U);
        mimusb::hid::DescriptorRequirements requirements{};
        MIMUSB_CHECK(mimusb::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U,
                                               nullptr, &requirements)
                         .result == MIMUSB_OK);
        MIMUSB_CHECK(requirements.maximum_usages == 1U);
    }
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(),
                          {0x15U, 0x00U, 0x25U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0x75U, 0x08U,
                           0x95U, 0x01U, 0x09U, 0x02U, 0x82U, 0x00U, 0x01U, 0xC0U});
        const mimusb_hid_report report = idless_report(2U);
        MIMUSB_CHECK(
            mimusb::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
            MIMUSB_OK);
    }
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 8U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x82U, 0x00U, 0x01U, 0xC0U});
        const mimusb_hid_report report = idless_report(1U);
        MIMUSB_CHECK(
            mimusb::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
            MIMUSB_OK);
    }
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        // Designator Index (tag 3) and String Index (tag 7) are defined Local
        // items. The selected target ignores both, which does not change the
        // Input report layout.
        descriptor.insert(descriptor.end(),
                          {0x39U, 0x01U, 0x79U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        const mimusb_hid_report report = idless_report(1U);
        MIMUSB_CHECK(
            mimusb::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
            MIMUSB_OK);
    }
}

} // namespace

void test_validator_encoding() {
    test_structure_the_target_decides();
    test_usage_ranges_and_local_items();
}
