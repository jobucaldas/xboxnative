// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// hid.dll for the synthetic pad (hid/pad_device.h, hid/pad_registry.h). The app
// lane routes hid.dll to the Wine donor, which knows nothing about the device
// Play presents, so these entry points answer first for a handle the pad
// registry issued and forward every other call to the donor unchanged.
//
// The guest's device attach (Unity's <RI.Hid> path) needs HidD_GetPreparsedData,
// HidD_GetProductString, HidP_GetCaps, HidP_GetButtonCaps, HidP_GetValueCaps,
// HidP_MaxDataListLength and HidD_GetAttributes to succeed; HidP_GetData is
// never called for a device whose path carries "IG_", because the guest reads
// such a pad through XInputGetState instead.
#include <windows.h>
#include <cstdint>
#include "pad_registry.h"

namespace hid_core {
// The lane's existing provider for a name this table does not own (the Wine
// donor in the app lane). Null when there is none, in which case the entry
// point answers exactly as the donor would for a handle that is not a pad.
using Resolve = uintptr_t (*)(const char*, const char*, WORD);
inline Resolve fallback = nullptr;

// Each entry point keeps its own cache: the three string answers share one
// function-pointer type, so a cache keyed by type alone would give the
// manufacturer and serial answers the product-string export.
template <class Fn> inline Fn donor(Fn& cache, const char* name) {
    if (!cache && fallback) cache = reinterpret_cast<Fn>(fallback("hid.dll", name, 0));
    return cache;
}

using GetPreparsed = BOOLEAN (WINAPI*)(HANDLE, void**);
using FreePreparsed = BOOLEAN (WINAPI*)(void*);
using GetString = BOOLEAN (WINAPI*)(HANDLE, void*, ULONG);
using GetAttributesFn = BOOLEAN (WINAPI*)(HANDLE, pad_hid::Attributes*);
using GetCapsFn = ULONG (WINAPI*)(const void*, pad_device::Caps*);
using GetButtonCapsFn = ULONG (WINAPI*)(int, pad_device::ButtonCaps*, unsigned short*, const void*);
using GetValueCapsFn = ULONG (WINAPI*)(int, pad_device::ValueCaps*, unsigned short*, const void*);
using MaxDataFn = ULONG (WINAPI*)(int, const void*);
using GetDataFn = ULONG (WINAPI*)(int, void*, unsigned long*, const void*, char*, unsigned long);
using SetUsagesFn = ULONG (WINAPI*)(int, unsigned short, unsigned short, unsigned short*, unsigned long*, const void*, char*, unsigned long);
using SetUsageValueFn = ULONG (WINAPI*)(int, unsigned short, unsigned short, unsigned short, unsigned long, const void*, char*, unsigned long);

// One cache per export, because the three string answers share a type.
inline GetPreparsed donor_get_preparsed = nullptr;
inline FreePreparsed donor_free_preparsed = nullptr;
inline GetString donor_get_product = nullptr;
inline GetString donor_get_manufacturer = nullptr;
inline GetString donor_get_serial = nullptr;
inline GetAttributesFn donor_get_attributes = nullptr;
inline GetCapsFn donor_get_caps = nullptr;
inline GetButtonCapsFn donor_get_button_caps = nullptr;
inline GetValueCapsFn donor_get_value_caps = nullptr;
inline MaxDataFn donor_max_data = nullptr;
inline GetDataFn donor_get_data = nullptr;
inline SetUsagesFn donor_set_usages = nullptr;
inline SetUsageValueFn donor_set_usage_value = nullptr;

inline BOOLEAN WINAPI get_preparsed_data(HANDLE device, void** preparsed) {
    if (!pad_hid::owned(device)) {
        auto fn = donor(donor_get_preparsed, "HidD_GetPreparsedData");
        if (!fn) { if (preparsed) *preparsed = nullptr; SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
        return fn(device, preparsed);
    }
    return pad_hid::get_preparsed_data(device, preparsed);
}
inline BOOLEAN WINAPI free_preparsed_data(void* preparsed) {
    if (!pad_hid::preparsed_ok(preparsed)) {
        auto fn = donor(donor_free_preparsed, "HidD_FreePreparsedData");
        if (!fn) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
        return fn(preparsed);
    }
    return pad_hid::free_preparsed_data(preparsed);
}
// The three string entry points differ only in which string they answer with,
// so each names its own donor export.
inline BOOLEAN WINAPI get_product_string(HANDLE device, void* buffer, ULONG length) {
    if (pad_hid::owned(device)) return pad_hid::get_product_string(device, buffer, length);
    auto fn = donor(donor_get_product, "HidD_GetProductString");
    if (!fn) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    return fn(device, buffer, length);
}
inline BOOLEAN WINAPI get_manufacturer_string(HANDLE device, void* buffer, ULONG length) {
    if (pad_hid::owned(device)) return pad_hid::get_manufacturer_string(device, buffer, length);
    auto fn = donor(donor_get_manufacturer, "HidD_GetManufacturerString");
    if (!fn) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    return fn(device, buffer, length);
}
inline BOOLEAN WINAPI get_serial_number_string(HANDLE device, void* buffer, ULONG length) {
    if (pad_hid::owned(device)) return pad_hid::get_serial_number_string(device, buffer, length);
    auto fn = donor(donor_get_serial, "HidD_GetSerialNumberString");
    if (!fn) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    return fn(device, buffer, length);
}
inline BOOLEAN WINAPI get_attributes(HANDLE device, pad_hid::Attributes* attributes) {
    if (!pad_hid::owned(device)) {
        auto fn = donor(donor_get_attributes, "HidD_GetAttributes");
        if (!fn) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
        return fn(device, attributes);
    }
    return pad_hid::get_attributes(device, attributes);
}
inline ULONG WINAPI get_caps(const void* preparsed, pad_device::Caps* caps) {
    if (!pad_hid::preparsed_ok(preparsed)) {
        auto fn = donor(donor_get_caps, "HidP_GetCaps");
        if (!fn) return pad_device::hidp_status_invalid_preparsed_data;
        return fn(preparsed, caps);
    }
    return pad_hid::get_caps(preparsed, caps);
}
inline ULONG WINAPI get_button_caps(int report_type, pad_device::ButtonCaps* caps, unsigned short* length,
                                    const void* preparsed) {
    if (!pad_hid::preparsed_ok(preparsed)) {
        auto fn = donor(donor_get_button_caps, "HidP_GetButtonCaps");
        if (!fn) return pad_device::hidp_status_invalid_preparsed_data;
        return fn(report_type, caps, length, preparsed);
    }
    if (report_type != 0) return pad_device::hidp_status_invalid_report_type;
    return pad_hid::get_button_caps(preparsed, caps, length);
}
inline ULONG WINAPI get_value_caps(int report_type, pad_device::ValueCaps* caps, unsigned short* length,
                                   const void* preparsed) {
    if (!pad_hid::preparsed_ok(preparsed)) {
        auto fn = donor(donor_get_value_caps, "HidP_GetValueCaps");
        if (!fn) return pad_device::hidp_status_invalid_preparsed_data;
        return fn(report_type, caps, length, preparsed);
    }
    if (report_type != 0) return pad_device::hidp_status_invalid_report_type;
    return pad_hid::get_value_caps(preparsed, caps, length);
}
inline ULONG WINAPI max_data_list_length(int report_type, const void* preparsed) {
    if (!pad_hid::preparsed_ok(preparsed)) {
        auto fn = donor(donor_max_data, "HidP_MaxDataListLength");
        return fn ? fn(report_type, preparsed) : 0;
    }
    return report_type == 0 ? pad_hid::max_data_list_length(preparsed) : 0;
}
// The report-reading and report-building entry points. A pad's reports are
// never produced here (input reaches the guest through XInputGetState), so a
// read finds nothing and a write cannot address a report: both answer with the
// documented status rather than handing our token to the donor, which would
// dereference it.
inline ULONG WINAPI get_data(int report_type, void* list, unsigned long* count, const void* preparsed,
                             char* report, unsigned long length) {
    if (!pad_hid::preparsed_ok(preparsed)) {
        auto fn = donor(donor_get_data, "HidP_GetData");
        return fn ? fn(report_type, list, count, preparsed, report, length)
                  : pad_device::hidp_status_invalid_preparsed_data;
    }
    if (count) *count = 0;
    return report_type == 0 ? pad_device::hidp_status_success : pad_device::hidp_status_invalid_report_type;
}
inline ULONG WINAPI set_usages(int report_type, unsigned short usage_page, unsigned short usage,
                               unsigned short* usage_list, unsigned long* count, const void* preparsed,
                               char* report, unsigned long length) {
    if (!pad_hid::preparsed_ok(preparsed)) {
        auto fn = donor(donor_set_usages, "HidP_SetUsages");
        return fn ? fn(report_type, usage_page, usage, usage_list, count, preparsed, report, length)
                  : pad_device::hidp_status_invalid_preparsed_data;
    }
    return report_type == 0 ? pad_device::hidp_status_usage_not_found : pad_device::hidp_status_invalid_report_type;
}
inline ULONG WINAPI set_usage_value(int report_type, unsigned short usage_page, unsigned short usage,
                                    unsigned short link_collection, unsigned long value, const void* preparsed,
                                    char* report, unsigned long length) {
    if (!pad_hid::preparsed_ok(preparsed)) {
        auto fn = donor(donor_set_usage_value, "HidP_SetUsageValue");
        return fn ? fn(report_type, usage_page, usage, link_collection, value, preparsed, report, length)
                  : pad_device::hidp_status_invalid_preparsed_data;
    }
    return report_type == 0 ? pad_device::hidp_status_usage_not_found : pad_device::hidp_status_invalid_report_type;
}

inline uintptr_t resolve(const char* dll, const char* name) {
    if (!dll || !name || _stricmp(dll, "hid.dll")) return 0;
    if (!strcmp(name, "HidD_GetPreparsedData")) return reinterpret_cast<uintptr_t>(&get_preparsed_data);
    if (!strcmp(name, "HidD_FreePreparsedData")) return reinterpret_cast<uintptr_t>(&free_preparsed_data);
    if (!strcmp(name, "HidD_GetProductString")) return reinterpret_cast<uintptr_t>(&get_product_string);
    if (!strcmp(name, "HidD_GetManufacturerString")) return reinterpret_cast<uintptr_t>(&get_manufacturer_string);
    if (!strcmp(name, "HidD_GetSerialNumberString")) return reinterpret_cast<uintptr_t>(&get_serial_number_string);
    if (!strcmp(name, "HidD_GetAttributes")) return reinterpret_cast<uintptr_t>(&get_attributes);
    if (!strcmp(name, "HidP_GetCaps")) return reinterpret_cast<uintptr_t>(&get_caps);
    if (!strcmp(name, "HidP_GetButtonCaps")) return reinterpret_cast<uintptr_t>(&get_button_caps);
    if (!strcmp(name, "HidP_GetValueCaps")) return reinterpret_cast<uintptr_t>(&get_value_caps);
    if (!strcmp(name, "HidP_MaxDataListLength")) return reinterpret_cast<uintptr_t>(&max_data_list_length);
    if (!strcmp(name, "HidP_GetData")) return reinterpret_cast<uintptr_t>(&get_data);
    if (!strcmp(name, "HidP_SetUsages")) return reinterpret_cast<uintptr_t>(&set_usages);
    if (!strcmp(name, "HidP_SetUsageValue")) return reinterpret_cast<uintptr_t>(&set_usage_value);
    return 0;
}
}
