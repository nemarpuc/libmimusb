// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
/* Raw descriptor validation: report size arithmetic and the failure paths of the parser, and
 * the items it leaves to the target. */
#include "hid/validator.hpp"
#include "raw_descriptor_support.hpp"
#include "test.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

using namespace mimusb_test;

namespace {

// The wire length of a report is all the library takes from a data item: Report Size times Report
// Count, summed per Report ID. A range, a width or a flag the target parser would object to is
// the target's answer, given when the device is created.
void test_raw_arithmetic_and_failure_paths() {
    {
        // With a nonnegative Logical Minimum the Maximum item is unsigned (HID 1.11 section
        // 6.2.2.7), and none of the extents changes the length.
        auto descriptor = application_preamble();
        descriptor.insert(descriptor.end(),
                          {0x15U, 0x00U, 0x27U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x75U, 0x20U, 0x95U,
                           0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        const mimusb_hid_report report = idless_report(4U);
        MIMUSB_CHECK(
            mimusb::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
            MIMUSB_OK);
        descriptor[14U] = 0x1FU; // Report Size 31 cannot hold 0xffffffff; the length is the same.
        MIMUSB_CHECK(
            mimusb::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
            MIMUSB_OK);
        // A Logical Maximum below the Minimum is likewise left to the target.
        descriptor[9U] = 0x00U;
        descriptor[10U] = 0x00U;
        descriptor[11U] = 0x00U;
        descriptor[12U] = 0x80U;
        MIMUSB_CHECK(
            mimusb::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
            MIMUSB_OK);
    }
    {
        // A field wider than 32 bits, or one that spans five bytes, only adds bits.
        auto wide = application_preamble();
        append_globals(&wide, 33U, 1U);
        wide.insert(wide.end(), {0x09U, 0x01U, 0x81U, 0x01U, 0xC0U});
        const mimusb_hid_report five = idless_report(5U);
        mimusb::hid::DescriptorRequirements requirements{};
        MIMUSB_CHECK(
            mimusb::hid::validate_raw(wide.data(), wide.size(), &five, 1U, nullptr, &requirements)
                .result == MIMUSB_OK);
        MIMUSB_CHECK(requirements.maximum_report_size_bits == 33U);
        MIMUSB_CHECK(requirements.maximum_report_data_bits == 33U);

        auto span = application_preamble();
        append_globals(&span, 31U, 2U);
        span.insert(span.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        const mimusb_hid_report eight = idless_report(8U);
        MIMUSB_CHECK(mimusb::hid::validate_raw(span.data(), span.size(), &eight, 1U).result ==
                     MIMUSB_OK);
    }
    {
        auto descriptor = application_preamble();
        append_globals(&descriptor, 32U, std::numeric_limits<std::uint32_t>::max());
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        const mimusb_hid_report report = idless_report(1U);
        // On a 64-bit host the computed wire length exceeds the public u32
        // table field; on a 32-bit host the Size*Count product overflows
        // size_t first. Both paths are an arithmetic overflow, not a generic
        // table mismatch.
        MIMUSB_CHECK(
            mimusb::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
            MIMUSB_ERR_OVERFLOW);
    }
    {
        // An Output item needs requires_output: the reports it describes come back to the caller.
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x91U, 0x02U, 0xC0U});
        const mimusb::hid::ValidationIssue result =
            mimusb::hid::validate_raw(descriptor.data(), descriptor.size(), nullptr, 0U);
        MIMUSB_CHECK(result.result == MIMUSB_ERR_UNSUPPORTED);
    }
    {
        // A long item (HID 1.11 section 6.2.2.3) is stepped over; one that is cut off is refused
        // because its end is not known.
        auto descriptor = application_preamble();
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        const mimusb_hid_report report = idless_report(1U);
        auto with_long = descriptor;
        with_long.insert(with_long.end(), {0xFEU, 0x02U, 0x10U, 0xAAU, 0xBBU});
        MIMUSB_CHECK(
            mimusb::hid::validate_raw(with_long.data(), with_long.size(), &report, 1U).result ==
            MIMUSB_OK);
        auto cut = descriptor;
        cut.insert(cut.end(), {0xFEU, 0x05U, 0x10U, 0xAAU});
        MIMUSB_CHECK(mimusb::hid::validate_raw(cut.data(), cut.size(), &report, 1U).result ==
                     MIMUSB_ERR_PARAM);
        auto header_cut = descriptor;
        header_cut.insert(header_cut.end(), {0xFEU, 0x00U});
        MIMUSB_CHECK(
            mimusb::hid::validate_raw(header_cut.data(), header_cut.size(), &report, 1U).result ==
            MIMUSB_ERR_PARAM);
    }
    {
        // Items the parser has no use for are passed over: a reserved Global tag, Physical and Unit
        // items with or without their companions, a transient Unit, a Unit Exponent of any size.
        for (const std::vector<std::uint8_t>& extra :
             {std::vector<std::uint8_t>{0xC5U, 0x00U},
              std::vector<std::uint8_t>{0x35U, 0x00U, 0x45U, 0x01U, 0x55U, 0x0CU, 0x65U, 0x11U},
              std::vector<std::uint8_t>{0x65U, 0x11U, 0x65U, 0x00U},
              std::vector<std::uint8_t>{0x65U, 0x11U}, std::vector<std::uint8_t>{0x65U, 0x05U},
              std::vector<std::uint8_t>{0x67U, 0x11U, 0x00U, 0x00U, 0x10U},
              std::vector<std::uint8_t>{0x55U, 0xFCU},
              std::vector<std::uint8_t>{0x07U, 0x00U, 0x00U, 0x01U, 0x00U},
              std::vector<std::uint8_t>{0x15U, 0x02U, 0x25U, 0x01U}}) {
            auto descriptor = application_preamble();
            append_globals(&descriptor, 1U, 1U);
            descriptor.insert(descriptor.end(), extra.begin(), extra.end());
            descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
            const mimusb_hid_report report = idless_report(1U);
            MIMUSB_CHECK(
                mimusb::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U)
                    .result == MIMUSB_OK);
        }
    }
    {
        // Pushes and Pops that do not pair up leave the state as it is.
        auto descriptor = application_preamble();
        descriptor.insert(descriptor.end(), {0xB4U, 0xA4U});
        append_globals(&descriptor, 1U, 1U);
        descriptor.insert(descriptor.end(), {0x09U, 0x01U, 0x81U, 0x02U, 0xC0U});
        const mimusb_hid_report report = idless_report(1U);
        MIMUSB_CHECK(
            mimusb::hid::validate_raw(descriptor.data(), descriptor.size(), &report, 1U).result ==
            MIMUSB_OK);
    }
}

} // namespace

void test_validator_arithmetic() { test_raw_arithmetic_and_failure_paths(); }
