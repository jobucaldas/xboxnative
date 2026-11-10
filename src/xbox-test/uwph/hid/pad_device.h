// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The HID interface a pad Play publishes in an XInput slot is presented under
// to a guest's legacy joystick enumeration. No Windows dependency: the record
// DLL (uwph-u32c.dll), the launcher's hid.dll route and a host-compiler test
// all read these same tables.
//
// Why this exists: Unity's <RI> path (UnityPlayer 2019.4 RawInput) enumerates
// GetRawInputDeviceList, keeps only RIM_TYPEHID records, asks
// GetRawInputDeviceInfoW for RID_DEVICEINFO (page 1 usage 4..6 or page 2), then
// RIDI_DEVICENAME, then opens the name and requires HidD_GetPreparsedData,
// HidD_GetProductString, HidP_GetCaps, HidP_GetButtonCaps, HidP_GetValueCaps,
// HidP_MaxDataListLength and HidD_GetAttributes to succeed before a device joins
// its joystick list ("Joystick Names:"). The name must contain "IG_": that is
// the XInput-compatible HID interface, and Unity then marks the device as an
// XInput pad and reads it through XInputGetState, which Play already feeds.
//
// The layouts below are the documented Win32 ABI (RID_DEVICE_INFO, hidpi.h).
// They are mirrored rather than included because the app family hides those
// declarations and the record DLL must not depend on the console's headers.
#include <cstddef>
#include <cstdint>

namespace pad_device {
constexpr unsigned slot_count = 4;

// A wired Xbox 360 controller: the identity Unity's built-in XInput profile and
// InControl's Xbox360WinProfile both recognise.
constexpr std::uint32_t vendor_id = 0x045e;
constexpr std::uint32_t product_id = 0x028e;
constexpr std::uint32_t version_number = 0x0114;
constexpr std::uint16_t usage_page = 1;   // Generic Desktop
constexpr std::uint16_t usage = 5;        // Game Pad

// RID_DEVICE_INFO (winuser.h) is 32 bytes: cbSize, dwType, then a union sized by
// RIM_TYPEKEYBOARD (24 bytes). RIM_TYPEHID = 2.
constexpr std::uint32_t rid_type_hid = 2;
constexpr std::uint32_t ridi_device_name = 0x20000007;
constexpr std::uint32_t ridi_device_info = 0x2000000b;

struct RidDeviceInfo {
    std::uint32_t size;
    std::uint32_t type;
    std::uint32_t vendor;
    std::uint32_t product;
    std::uint32_t version;
    std::uint16_t usage_page;
    std::uint16_t usage;
    std::uint32_t tail[2];
};
static_assert(sizeof(RidDeviceInfo) == 32, "RID_DEVICE_INFO is 32 bytes");
static_assert(offsetof(RidDeviceInfo, usage_page) == 0x14 && offsetof(RidDeviceInfo, usage) == 0x16,
    "RID_DEVICE_INFO HID usage page/usage offsets");

inline RidDeviceInfo device_info() {
    RidDeviceInfo info{};
    info.size = sizeof(RidDeviceInfo);
    info.type = rid_type_hid;
    info.vendor = vendor_id;
    info.product = product_id;
    info.version = version_number;
    info.usage_page = usage_page;
    info.usage = usage;
    return info;
}

// The device interface path. Real Xbox 360 pads carry "IG_" in theirs; Unity's
// wcsstr gate (0x180dc1890) requires it, and the slot makes each pad distinct so
// Unity's by-name device dedupe keeps them apart. The two digits after "IG_" are
// the slot, the same shape a real pad's instance id has ("IG_00").
inline constexpr char16_t path_prefix[] = u"\\\\?\\HID#VID_045E&PID_028E&IG_";
inline constexpr char16_t path_suffix[] = u"#{4d1e55b2-f16f-11cf-88cb-001111000030}";
// Writes the path for `slot` into `out` (capacity in char16_t) and returns its
// length excluding the terminator, or 0 when it does not fit.
inline unsigned path(unsigned slot, char16_t* out, unsigned capacity) {
    if (!out || !capacity) return 0;
    unsigned n = 0;
    auto put = [&](char16_t c) { if (n + 1 >= capacity) return false; out[n++] = c; return true; };
    for (const char16_t* p = path_prefix; *p; ++p) if (!put(*p)) return 0;
    // Always two digits, so every slot's path has the same shape and the
    // reader can take the pair without checking for a single-digit form.
    if (!put(char16_t(u'0' + slot / 10))) return 0;
    if (!put(char16_t(u'0' + slot % 10))) return 0;
    for (const char16_t* p = path_suffix; *p; ++p) if (!put(*p)) return 0;
    out[n] = 0;
    return n;
}

// True for a path this module issued, so an open of it can be answered without
// touching a device that is not there.
inline bool is_pad_path(const char16_t* path) {
    if (!path) return false;
    for (const char16_t* p = path_prefix; *p; ++p, ++path) {
        char16_t a = *path, b = *p;
        if (a >= u'a' && a <= u'z') a = char16_t(a - u'a' + u'A');
        if (b >= u'a' && b <= u'z') b = char16_t(b - u'a' + u'A');
        if (a != b) return false;
    }
    return true;
}

// The product string Unity reads through HidD_GetProductString; InControl and
// Unity both match "Xbox 360" to pick an XInput mapping.
inline constexpr char16_t product_string[] = u"Xbox 360 Controller for Windows";
inline constexpr char16_t manufacturer_string[] = u"Microsoft";
inline constexpr char16_t serial_string[] = u"";

// ---- hidpi.h layouts ------------------------------------------------------
// HIDP_BUTTON_CAPS and HIDP_VALUE_CAPS are both 72 bytes; HIDP_CAPS is 64.
// The status codes are HIDP_ERROR_CODES(severity, code) with facility 0x11:
// (severity << 28) | 0x11 << 16 | code. Values below are the documented ones
// from hidpi.h, in table order.
constexpr std::uint32_t hidp_status_success = 0x00110000;
constexpr std::uint32_t hidp_status_invalid_preparsed_data = 0xc0110001;
constexpr std::uint32_t hidp_status_invalid_report_type = 0xc0110002;
constexpr std::uint32_t hidp_status_invalid_report_length = 0xc0110003;
constexpr std::uint32_t hidp_status_usage_not_found = 0xc0110004;
constexpr std::uint32_t hidp_status_value_out_of_range = 0xc0110005;
constexpr std::uint32_t hidp_status_bad_log_phy_values = 0xc0110006;
constexpr std::uint32_t hidp_status_buffer_too_small = 0xc0110007;

struct ButtonCaps {
    std::uint16_t usage_page;
    std::uint8_t report_id;
    std::uint8_t is_alias;
    std::uint16_t bit_field;
    std::uint16_t link_collection;
    std::uint16_t link_usage;
    std::uint16_t link_usage_page;
    std::uint8_t is_range, is_string_range, is_designator_range, is_absolute;
    std::uint32_t reserved[10];
    std::uint16_t usage_min, usage_max;
    std::uint16_t string_min, string_max;
    std::uint16_t designator_min, designator_max;
    std::uint16_t data_index_min, data_index_max;
};
static_assert(sizeof(ButtonCaps) == 72, "HIDP_BUTTON_CAPS is 72 bytes");
static_assert(offsetof(ButtonCaps, is_range) == 0x0c && offsetof(ButtonCaps, usage_min) == 0x38 &&
    offsetof(ButtonCaps, data_index_min) == 0x44, "HIDP_BUTTON_CAPS offsets");

struct ValueCaps {
    std::uint16_t usage_page;
    std::uint8_t report_id;
    std::uint8_t is_alias;
    std::uint16_t bit_field;
    std::uint16_t link_collection;
    std::uint16_t link_usage;
    std::uint16_t link_usage_page;
    std::uint8_t is_range, is_string_range, is_designator_range, is_absolute, has_null;
    std::uint8_t reserved;
    std::uint16_t bit_size;
    std::uint16_t report_count;
    std::uint16_t reserved2[5];
    std::uint32_t units_exp;
    std::uint32_t units;
    std::int32_t logical_min, logical_max;
    std::int32_t physical_min, physical_max;
    std::uint16_t usage_min, usage_max;
    std::uint16_t string_min, string_max;
    std::uint16_t designator_min, designator_max;
    std::uint16_t data_index_min, data_index_max;
};
static_assert(sizeof(ValueCaps) == 72, "HIDP_VALUE_CAPS is 72 bytes");
static_assert(offsetof(ValueCaps, is_range) == 0x0c && offsetof(ValueCaps, bit_size) == 0x12 &&
    offsetof(ValueCaps, report_count) == 0x14 && offsetof(ValueCaps, logical_min) == 0x28 &&
    offsetof(ValueCaps, logical_max) == 0x2c && offsetof(ValueCaps, usage_min) == 0x38 &&
    offsetof(ValueCaps, data_index_min) == 0x44, "HIDP_VALUE_CAPS offsets");

struct Caps {
    std::uint16_t usage;
    std::uint16_t usage_page;
    std::uint16_t input_report_byte_length;
    std::uint16_t output_report_byte_length;
    std::uint16_t feature_report_byte_length;
    std::uint16_t reserved[17];
    std::uint16_t link_collection_nodes;
    std::uint16_t input_button_caps;
    std::uint16_t input_value_caps;
    std::uint16_t input_data_indices;
    std::uint16_t output_button_caps, output_value_caps, output_data_indices;
    std::uint16_t feature_button_caps, feature_value_caps, feature_data_indices;
};
static_assert(sizeof(Caps) == 64, "HIDP_CAPS is 64 bytes");
static_assert(offsetof(Caps, usage_page) == 2 && offsetof(Caps, input_report_byte_length) == 4 &&
    offsetof(Caps, input_button_caps) == 0x2e && offsetof(Caps, input_value_caps) == 0x30 &&
    offsetof(Caps, input_data_indices) == 0x32, "HIDP_CAPS offsets");

// One report ID, 13 buttons, four axes and the hat switch.
constexpr std::uint8_t report_id = 1;
constexpr unsigned button_count = 13;   // page 1 usages 1..13
constexpr unsigned value_count = 5;     // X, Y, Z, Rz, hat
constexpr unsigned data_index_count = button_count + value_count;

inline Caps caps() {
    Caps c{};
    c.usage = usage;
    c.usage_page = usage_page;
    c.input_report_byte_length = 20;
    c.link_collection_nodes = 1;
    c.input_button_caps = 1;
    c.input_value_caps = value_count;
    c.input_data_indices = data_index_count;
    return c;
}

// Buttons 1..13 as one range, the shape a real Xbox 360 report descriptor has.
inline ButtonCaps button_caps() {
    ButtonCaps b{};
    b.usage_page = usage_page;
    b.report_id = report_id;
    b.is_range = 1;
    b.is_absolute = 1;
    b.usage_min = 1;
    b.usage_max = button_count;
    b.data_index_min = 1;
    b.data_index_max = button_count;
    return b;
}

// The four axes and the hat. Logical ranges are the descriptor's: sticks are
// signed 16-bit, triggers unsigned 8-bit, the hat a 0..7 null state.
inline ValueCaps value_caps(unsigned index) {
    ValueCaps v{};
    v.usage_page = usage_page;
    v.report_id = report_id;
    v.is_absolute = 1;
    v.report_count = 1;
    v.data_index_min = v.data_index_max = std::uint16_t(button_count + 1 + index);
    switch (index) {
    case 0: v.usage_min = v.usage_max = 0x30; v.bit_size = 16; v.logical_min = -32768; v.logical_max = 32767; break; // X
    case 1: v.usage_min = v.usage_max = 0x31; v.bit_size = 16; v.logical_min = -32768; v.logical_max = 32767; break; // Y
    case 2: v.usage_min = v.usage_max = 0x32; v.bit_size = 8;  v.logical_min = 0; v.logical_max = 255; break;      // Z
    case 3: v.usage_min = v.usage_max = 0x35; v.bit_size = 8;  v.logical_min = 0; v.logical_max = 255; break;      // Rz
    default: v.usage_min = v.usage_max = 0x39; v.bit_size = 4; v.logical_min = 0; v.logical_max = 7; v.has_null = 1; break; // hat
    }
    v.physical_min = v.logical_min;
    v.physical_max = v.logical_max;
    return v;
}

// Unity's device gate (0x18052f0a6): page 1 usages 4..6, or page 2.
inline bool accepts(std::uint16_t page, std::uint16_t use) {
    if (page == 1) return use >= 4 && use <= 6;
    return page == 2;
}
}
