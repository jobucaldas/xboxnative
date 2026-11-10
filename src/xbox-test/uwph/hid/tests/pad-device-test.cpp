// SPDX-License-Identifier: GPL-3.0-or-later
// Host-compiler check of the synthetic pad's HID surface: the ABI layouts a
// guest reads through GetRawInputDeviceInfoW and the HidP_* family, the device
// path Unity's "IG_" gate needs, and the cap shape Unity's device attach
// accepts. No Windows headers, so CI runs it without Wine or a cross build.
#include "../pad_device.h"
#include <cstdio>
#include <cstring>
#include <string>

using namespace pad_device;
static int failures;
static void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", what); ++failures; }
}

int main() {
    // ABI: the sizes and offsets the guest reads back by number.
    check(sizeof(RidDeviceInfo) == 0x20, "RID_DEVICE_INFO size");
    check(sizeof(ButtonCaps) == 0x48, "HIDP_BUTTON_CAPS size");
    check(sizeof(ValueCaps) == 0x48, "HIDP_VALUE_CAPS size");
    check(sizeof(Caps) == 0x40, "HIDP_CAPS size");
    check(offsetof(ButtonCaps, is_range) == 0x0c, "button IsRange");
    check(offsetof(ButtonCaps, usage_min) == 0x38 && offsetof(ButtonCaps, usage_max) == 0x3a, "button usage range");
    check(offsetof(ButtonCaps, data_index_min) == 0x44, "button data index");
    check(offsetof(ValueCaps, bit_size) == 0x12 && offsetof(ValueCaps, report_count) == 0x14, "value bit size");
    check(offsetof(ValueCaps, logical_min) == 0x28 && offsetof(ValueCaps, logical_max) == 0x2c, "value logical range");
    check(offsetof(ValueCaps, usage_min) == 0x38 && offsetof(ValueCaps, data_index_min) == 0x44, "value usage range");
    check(offsetof(Caps, input_button_caps) == 0x2e && offsetof(Caps, input_value_caps) == 0x30, "caps counts");

    // HIDP status codes: HIDP_ERROR_CODES(severity, code) with facility 0x11.
    // The guest compares these against the documented values, so each one is
    // pinned here rather than only named.
    check(hidp_status_success == 0x00110000, "HIDP_STATUS_SUCCESS");
    check(hidp_status_invalid_preparsed_data == 0xc0110001, "HIDP_STATUS_INVALID_PREPARSED_DATA");
    check(hidp_status_invalid_report_type == 0xc0110002, "HIDP_STATUS_INVALID_REPORT_TYPE");
    check(hidp_status_invalid_report_length == 0xc0110003, "HIDP_STATUS_INVALID_REPORT_LENGTH");
    check(hidp_status_usage_not_found == 0xc0110004, "HIDP_STATUS_USAGE_NOT_FOUND");
    check(hidp_status_value_out_of_range == 0xc0110005, "HIDP_STATUS_VALUE_OUT_OF_RANGE");
    check(hidp_status_bad_log_phy_values == 0xc0110006, "HIDP_STATUS_BAD_LOG_PHY_VALUES");
    check(hidp_status_buffer_too_small == 0xc0110007, "HIDP_STATUS_BUFFER_TOO_SMALL");
    check(hidp_status_buffer_too_small != hidp_status_value_out_of_range, "buffer-too-small is not value-out-of-range");

    // RID_DEVICE_INFO: a HID game pad on page 1 usage 5, which Unity accepts.
    RidDeviceInfo info = device_info();
    check(info.size == 0x20 && info.type == 2, "device info type");
    check(info.vendor == 0x045e && info.product == 0x028e, "device info ids");
    check(info.usage_page == 1 && info.usage == 5, "device info usage");
    check(accepts(info.usage_page, info.usage), "Unity accepts the reported usage");

    // The device path carries "IG_" (Unity's wcsstr gate) and is per slot.
    char16_t path0[128], path1[128], short_buffer[8];
    unsigned n0 = path(0, path0, 128), n1 = path(1, path1, 128);
    check(n0 > 0 && n1 > 0, "path fits");
    check(n0 == std::char_traits<char16_t>::length(path0), "path length");
    check(std::u16string(path0).find(u"IG_") != std::u16string::npos, "path has IG_");
    check(std::u16string(path0) != std::u16string(path1), "paths are distinct per slot");
    check(std::u16string(path0).rfind(u"\\\\?\\HID#", 0) == 0, "path is a device interface");
    check(path(0, short_buffer, 8) == 0, "a short buffer is refused");
    check(is_pad_path(path0) && is_pad_path(path1), "issued paths are recognised");
    check(!is_pad_path(u"\\\\?\\HID#VID_045E&PID_02EA&IG_00#7&1&0&0"), "another product is not ours");
    check(!is_pad_path(u"\\\\?\\HID#VID_045E&PID_028E#7&1&0&0"), "a path without IG_ is not");

    // Caps: one button range covering 1..13 and five value caps, all on the
    // report ID Unity reads.
    Caps c = caps();
    check(c.usage_page == 1 && c.usage == 5, "caps usage");
    check(c.input_button_caps == 1 && c.input_value_caps == value_count, "caps counts");
    check(c.input_data_indices == data_index_count, "caps data indices");
    check(c.input_report_byte_length > 0, "caps report length");

    ButtonCaps b = button_caps();
    check(b.usage_page == 1 && b.is_range, "button range");
    check(b.usage_min == 1 && b.usage_max == button_count, "button usages");
    check(b.data_index_min == 1 && b.data_index_max == button_count, "button data indices");
    check(b.report_id == report_id, "button report id");

    // Unity's cap builder (0x180528d30) classifies page 1 usages 0x30..0x35 as
    // axes, 0x39 as the hat (needs a logical span of exactly 7 and fewer than
    // four existing hats), and page 2 0xba/0xbb as the two triggers.
    unsigned hats = 0, axes = 0;
    for (unsigned i = 0; i < value_count; ++i) {
        ValueCaps v = value_caps(i);
        check(v.usage_page == 1 && v.report_id == report_id, "value page and report id");
        check(v.usage_min == v.usage_max, "value single usage");
        check(v.data_index_min == v.data_index_max, "value single data index");
        check(v.data_index_min == button_count + 1 + i, "value data index order");
        check(v.logical_max > v.logical_min, "value logical span");
        if (v.usage_min == 0x39) {
            ++hats;
            check(v.logical_max - v.logical_min == 7, "hat span is 7");
        } else {
            ++axes;
            check(v.usage_min >= 0x30 && v.usage_min <= 0x35, "axis usage");
        }
    }
    check(hats == 1, "exactly one hat");
    check(axes == 4, "four axes");

    // Unity's page/usage gate: page 1 usages 4..6, or page 2.
    check(accepts(1, 4) && accepts(1, 5) && accepts(1, 6), "page 1 game controls");
    check(!accepts(1, 3) && !accepts(1, 7) && !accepts(1, 0x80), "page 1 non-game usages rejected");
    check(accepts(2, 1) && accepts(2, 0xba), "page 2 accepted");
    check(!accepts(0x0c, 1), "consumer page rejected");

    if (failures) { std::fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    std::puts("pad-device OK");
    return 0;
}
