// SPDX-License-Identifier: GPL-3.0-or-later
// Host-compiler check of the synthetic pad's handle registry and hid.dll
// answers: the state machine the kernel32 file route and the hid route share.
// The fake windows.h in tests/fake stands in for the platform, so this runs in
// CI without Wine or a cross build.
#include "../pad_registry.h"
#include "../route.h"
#include <cstdio>
#include <cwchar>

using namespace pad_hid;
static int failures;
static void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", what); ++failures; }
}

int main() {
    // No pad handle exists yet: a random handle is not ours, and closing it
    // falls through to the native close.
    check(slot_of(reinterpret_cast<HANDLE>(0x1234)) < 0, "unknown handle is not a pad");
    check(!close(reinterpret_cast<HANDLE>(0x1234)), "unknown handle is not closed by the registry");
    check(!owned(reinterpret_cast<HANDLE>(0x1234)), "unknown handle is not owned");

    // A path the pad presents opens; a foreign path does not.
    char16_t path0[128], path1[128];
    pad_device::path(0, path0, 128);
    pad_device::path(1, path1, 128);
    HANDLE first = open(reinterpret_cast<const wchar_t*>(path0));
    check(first != INVALID_HANDLE_VALUE, "slot 0 opens");
    check(slot_of(first) == 0, "the first handle names slot 0");
    check(owned(first), "the first handle is owned");

    // Opening the same device again yields a second handle, both live: Unity
    // queries through one and reads through another.
    HANDLE second = open(reinterpret_cast<const wchar_t*>(path0));
    check(second != INVALID_HANDLE_VALUE && second != first, "a second open is a second handle");
    check(slot_of(second) == 0 && slot_of(first) == 0, "both handles name slot 0");
    HANDLE other = open(reinterpret_cast<const wchar_t*>(path1));
    check(slot_of(other) == 1, "the second device names slot 1");

    // Closing one handle leaves the others alone, and a closed handle is not
    // ours any more (so a recycled value cannot be mistaken for a pad).
    check(close(first), "the first handle closes through the registry");
    check(slot_of(first) < 0, "a closed handle is not a pad");
    check(slot_of(second) == 0 && slot_of(other) == 1, "the other handles survive");
    check(!close(first), "closing a closed handle is not ours");

    // Reads: an overlapped read stays pending, a synchronous one is refused.
    DWORD transferred = 99;
    OVERLAPPED overlapped{};
    check(!read(second, nullptr, 8, &transferred, &overlapped), "an overlapped read does not complete");
    check(fake_last_error == ERROR_IO_PENDING && transferred == 0, "the overlapped read is pending");
    check(!read(second, nullptr, 8, &transferred, nullptr), "a synchronous read is refused");
    check(fake_last_error == ERROR_INVALID_PARAMETER, "the synchronous read reports the parameter");
    check(!overlapped_result(second, &overlapped, &transferred, FALSE), "a pending read has no result");
    check(fake_last_error == ERROR_IO_INCOMPLETE, "the pending read is incomplete");

    // The hid.dll answers Unity's device attach needs, for a pad handle only.
    void* preparsed = nullptr;
    check(get_preparsed_data(second, &preparsed) && preparsed, "preparsed data for a pad handle");
    check(preparsed_ok(preparsed), "the token round-trips");
    void* foreign = reinterpret_cast<void*>(0x1234);
    check(!get_preparsed_data(reinterpret_cast<HANDLE>(0x1234), &foreign) && !foreign,
          "no preparsed data for a foreign handle");
    check(fake_last_error == ERROR_INVALID_HANDLE, "the foreign handle reports invalid handle");

    pad_device::Caps caps{};
    check(get_caps(preparsed, &caps) == pad_device::hidp_status_success, "caps succeed");
    check(caps.usage_page == 1 && caps.usage == 5, "caps describe a game pad");
    check(get_caps(reinterpret_cast<void*>(0x1234), &caps) == pad_device::hidp_status_invalid_preparsed_data,
          "a foreign token is refused");

    pad_device::ButtonCaps buttons[4]{};
    unsigned short buttonCount = 4;
    check(get_button_caps(preparsed, buttons, &buttonCount) == pad_device::hidp_status_success, "button caps succeed");
    check(buttonCount == 1 && buttons[0].usage_min == 1 && buttons[0].usage_max == pad_device::button_count,
          "one button range covers every button");

    pad_device::ValueCaps values[8]{};
    unsigned short valueCount = 8;
    check(get_value_caps(preparsed, values, &valueCount) == pad_device::hidp_status_success, "value caps succeed");
    check(valueCount == pad_device::value_count, "every axis and the hat are reported");
    check(max_data_list_length(preparsed) == pad_device::data_index_count, "the data index count is reported");

    // Report reads find nothing (input arrives through XInputGetState) and a
    // report write cannot address a usage; neither reaches the donor with our
    // token, which the donor would dereference.
    unsigned long dataCount = 7;
    char report[16]{};
    check(hid_core::get_data(0, nullptr, &dataCount, preparsed, report, sizeof(report)) == pad_device::hidp_status_success,
          "a report read succeeds with nothing new");
    check(dataCount == 0, "the report read reports no data");
    check(hid_core::get_data(1, nullptr, &dataCount, preparsed, report, sizeof(report)) == pad_device::hidp_status_invalid_report_type,
          "an output report type is refused");
    unsigned short usageList[2] = {1, 2};
    unsigned long usageCount = 2;
    check(hid_core::set_usages(0, 1, 0, usageList, &usageCount, preparsed, report, sizeof(report)) ==
              pad_device::hidp_status_usage_not_found,
          "a usage write cannot address our report");
    check(hid_core::set_usage_value(0, 1, 0x30, 0, 0, preparsed, report, sizeof(report)) ==
              pad_device::hidp_status_usage_not_found,
          "a value write cannot address our report");
    check(hid_core::get_data(0, nullptr, &dataCount, reinterpret_cast<void*>(0x1234), report, sizeof(report)) ==
              pad_device::hidp_status_invalid_preparsed_data,
          "a foreign token never reaches the donor");

    wchar_t product[64]{};
    check(get_product_string(second, product, sizeof(product)), "the product string is answered");
    check(std::wcsstr(product, L"Xbox 360") != nullptr, "the product string names the pad");
    Attributes attributes{sizeof(Attributes), 0, 0, 0};
    check(get_attributes(second, &attributes), "attributes are answered");
    check(attributes.vendor == 0x045e && attributes.product == 0x028e, "attributes name the pad");

    // Closing the device makes its handles stop answering.
    check(close(second) && close(other), "the remaining handles close");
    check(!get_product_string(second, product, sizeof(product)), "a closed handle has no product string");
    check(!read(second, nullptr, 8, &transferred, &overlapped), "a closed handle does not read");
    check(!preparsed_ok(nullptr), "a null token is not preparsed data");

    // A path this module did not issue is not opened.
    check(open(L"\\\\?\\HID#VID_045E&PID_02EA&IG_00#x") == INVALID_HANDLE_VALUE, "a foreign device path is not opened");
    check(open(L"\\\\?\\HID#VID_045E&PID_028E&IG_99#x") == INVALID_HANDLE_VALUE, "an out-of-range slot is not opened");

    if (!failures) std::printf("pad-registry OK\n");
    return failures ? 1 : 0;
}
