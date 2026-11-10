// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The synthetic HID device Play presents for each connected XInput slot, as the
// guest's legacy joystick enumeration sees it: a device interface path that
// opens, a preparsed-data token, and the HidD_/HidP_ answers Unity's device
// attach requires. One registry per process, shared by the kernel32 file route
// (CreateFileW/ReadFile/GetOverlappedResult/CloseHandle) and the hid.dll route.
//
// The handle is a real event object, so a guest's CloseHandle, DuplicateHandle
// and wait calls behave as they do on a device handle. Reads never complete:
// Unity marks a device whose path carries "IG_" as an XInput pad and reads it
// through XInputGetState instead of HidP_GetData, and the attach only needs the
// first overlapped ReadFile to return ERROR_IO_PENDING.
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include "pad_device.h"

namespace pad_hid {
// The opaque preparsed-data pointer handed to the guest. It is never
// dereferenced by this module; it only has to round-trip through HidP_*.
struct Preparsed { unsigned magic = 0x50414448; };

// Every handle a guest opened for a pad, with the slot it belongs to. A device
// is opened more than once (Unity queries through one handle and reads through
// a second overlapped one), so handles are tracked individually and only ever
// closed by the guest's own CloseHandle: closing one behind the guest's back
// would let a recycled handle value be closed twice.
struct Entry { HANDLE handle = nullptr; int slot = -1; };
struct Registry {
    std::mutex lock;
    Entry entries[16];
    std::atomic<unsigned> open_count{0};   // fast path: no pad handle exists
    Preparsed preparsed;
};
inline Registry& registry() { static Registry r; return r; }

// The slot a device path names, or -1 when the path is not one this module
// issued. The slot is the two digits between the "IG_00#" prefix and '#'.
inline int slot_from_path(const wchar_t* path) {
    if (!pad_device::is_pad_path(reinterpret_cast<const char16_t*>(path))) return -1;
    const wchar_t* p = path + (sizeof(pad_device::path_prefix) / sizeof(char16_t) - 1);
    if (p[0] < L'0' || p[0] > L'9' || p[1] < L'0' || p[1] > L'9') return -1;
    int slot = (p[0] - L'0') * 10 + (p[1] - L'0');
    return slot < int(pad_device::slot_count) ? slot : -1;
}

// CreateFileW for a pad path. Each open gets its own handle, as opening one
// device twice does; the caller owns it and closes it with CloseHandle.
inline HANDLE open(const wchar_t* path) {
    int slot = slot_from_path(path);
    if (slot < 0) return INVALID_HANDLE_VALUE;
    HANDLE handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!handle) return INVALID_HANDLE_VALUE;
    Registry& r = registry();
    std::lock_guard<std::mutex> guard(r.lock);
    for (unsigned i = 0; i < sizeof(r.entries) / sizeof(r.entries[0]); ++i) {
        if (r.entries[i].handle) continue;
        r.entries[i].handle = handle;
        r.entries[i].slot = slot;
        r.open_count.fetch_add(1, std::memory_order_release);
        return handle;
    }
    CloseHandle(handle);
    SetLastError(ERROR_TOO_MANY_OPEN_FILES);
    return INVALID_HANDLE_VALUE;
}

// The slot a handle names, or -1. A handle the guest already closed is not ours
// any more, so a recycled value cannot be mistaken for a pad.
inline int slot_of(HANDLE handle) {
    if (!handle || handle == INVALID_HANDLE_VALUE) return -1;
    Registry& r = registry();
    if (!r.open_count.load(std::memory_order_relaxed)) return -1;
    std::lock_guard<std::mutex> guard(r.lock);
    for (auto& entry : r.entries)
        if (entry.handle == handle) return entry.slot;
    return -1;
}

// CloseHandle for a pad handle: true when the handle was ours (and is now
// closed), false when the caller should use the native close.
inline bool close(HANDLE handle) {
    if (!handle || handle == INVALID_HANDLE_VALUE) return false;
    Registry& r = registry();
    if (!r.open_count.load(std::memory_order_relaxed)) return false;
    std::lock_guard<std::mutex> guard(r.lock);
    for (auto& entry : r.entries) {
        if (entry.handle != handle) continue;
        entry.handle = nullptr;
        entry.slot = -1;
        r.open_count.fetch_sub(1, std::memory_order_release);
        CloseHandle(handle);
        return true;
    }
    return false;
}

// ReadFile for a pad handle: the device is idle, so an overlapped read stays
// pending (as a HID device with no new report does) and a synchronous one waits
// forever. Unity only ever issues the overlapped form.
inline BOOL read(HANDLE handle, LPVOID, DWORD, LPDWORD transferred, LPOVERLAPPED overlapped) {
    if (slot_of(handle) < 0) return FALSE;
    if (transferred) *transferred = 0;
    if (overlapped) { SetLastError(ERROR_IO_PENDING); return FALSE; }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

// GetOverlappedResult for a pending pad read.
inline BOOL overlapped_result(HANDLE handle, LPOVERLAPPED, LPDWORD transferred, BOOL) {
    if (slot_of(handle) < 0) return FALSE;
    if (transferred) *transferred = 0;
    SetLastError(ERROR_IO_INCOMPLETE);
    return FALSE;
}

// ---- hid.dll answers for a pad handle -------------------------------------
inline bool owned(HANDLE handle) { return slot_of(handle) >= 0; }

inline BOOLEAN get_preparsed_data(HANDLE handle, void** preparsed) {
    if (!preparsed) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    *preparsed = nullptr;
    if (!owned(handle)) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    *preparsed = &registry().preparsed;
    return TRUE;
}

inline BOOLEAN free_preparsed_data(void* preparsed) {
    if (preparsed != &registry().preparsed) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    return TRUE;
}

inline BOOLEAN get_product_string(HANDLE handle, void* buffer, ULONG length) {
    if (!owned(handle)) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (!buffer || length < sizeof(pad_device::product_string)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    memcpy(buffer, pad_device::product_string, sizeof(pad_device::product_string));
    return TRUE;
}

inline BOOLEAN get_manufacturer_string(HANDLE handle, void* buffer, ULONG length) {
    if (!owned(handle)) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (!buffer || length < sizeof(pad_device::manufacturer_string)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    memcpy(buffer, pad_device::manufacturer_string, sizeof(pad_device::manufacturer_string));
    return TRUE;
}

inline BOOLEAN get_serial_number_string(HANDLE handle, void* buffer, ULONG length) {
    if (!owned(handle)) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (!buffer || length < sizeof(pad_device::serial_string)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    memcpy(buffer, pad_device::serial_string, sizeof(pad_device::serial_string));
    return TRUE;
}

// HIDD_ATTRIBUTES: ULONG Size, then three USHORTs (12 bytes with padding).
struct Attributes { ULONG size; USHORT vendor, product, version; };
static_assert(sizeof(Attributes) == 12, "HIDD_ATTRIBUTES is 12 bytes");
inline BOOLEAN get_attributes(HANDLE handle, Attributes* attributes) {
    if (!owned(handle)) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (!attributes || attributes->size != sizeof(Attributes)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    attributes->vendor = USHORT(pad_device::vendor_id);
    attributes->product = USHORT(pad_device::product_id);
    attributes->version = USHORT(pad_device::version_number);
    return TRUE;
}

inline bool preparsed_ok(const void* preparsed) { return preparsed == &registry().preparsed; }

inline ULONG get_caps(const void* preparsed, pad_device::Caps* caps) {
    if (!preparsed_ok(preparsed)) return pad_device::hidp_status_invalid_preparsed_data;
    if (!caps) return pad_device::hidp_status_buffer_too_small;
    *caps = pad_device::caps();
    return pad_device::hidp_status_success;
}

inline ULONG get_button_caps(const void* preparsed, pad_device::ButtonCaps* caps, unsigned short* length) {
    if (!preparsed_ok(preparsed)) return pad_device::hidp_status_invalid_preparsed_data;
    if (!length) return pad_device::hidp_status_buffer_too_small;
    if (!caps || *length < 1) { *length = 1; return pad_device::hidp_status_buffer_too_small; }
    *caps = pad_device::button_caps();
    *length = 1;
    return pad_device::hidp_status_success;
}

inline ULONG get_value_caps(const void* preparsed, pad_device::ValueCaps* caps, unsigned short* length) {
    if (!preparsed_ok(preparsed)) return pad_device::hidp_status_invalid_preparsed_data;
    if (!length) return pad_device::hidp_status_buffer_too_small;
    if (!caps || *length < pad_device::value_count) { *length = pad_device::value_count; return pad_device::hidp_status_buffer_too_small; }
    for (unsigned i = 0; i < pad_device::value_count; ++i) caps[i] = pad_device::value_caps(i);
    *length = pad_device::value_count;
    return pad_device::hidp_status_success;
}

// One data index per button and per axis, as HIDP_CAPS reports.
inline ULONG max_data_list_length(const void* preparsed) {
    return preparsed_ok(preparsed) ? pad_device::data_index_count : 0;
}
}
