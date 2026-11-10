// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The smallest stand-in for <windows.h> that hid/pad_registry.h and hid/route.h
// need, so their state machine runs under the host compiler in CI without Wine
// or a cross build. Only the declarations those two headers use are here, and
// nothing below is an implementation of Windows: CreateEventW hands out unique
// non-null values and CloseHandle forgets them, which is all the registry asks.
#include <cstdint>
#include <cstring>
#include <strings.h>
#ifndef _stricmp
#define _stricmp strcasecmp
#endif
using ULONG = std::uint32_t;
using USHORT = std::uint16_t;
using WORD = std::uint16_t;
using BOOLEAN = std::uint8_t;
using BOOL = int;
using DWORD = std::uint32_t;
using LPVOID = void*;
using LPDWORD = DWORD*;
using HANDLE = void*;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)(std::intptr_t)-1)
struct OVERLAPPED { std::uintptr_t internal; };
using LPOVERLAPPED = OVERLAPPED*;
constexpr DWORD ERROR_SUCCESS = 0;
constexpr DWORD ERROR_TOO_MANY_OPEN_FILES = 4;
constexpr DWORD ERROR_INVALID_HANDLE = 6;
constexpr DWORD ERROR_INVALID_PARAMETER = 87;
constexpr DWORD ERROR_IO_INCOMPLETE = 996;
constexpr DWORD ERROR_IO_PENDING = 997;
inline DWORD fake_last_error;
inline void SetLastError(DWORD error) { fake_last_error = error; }
// Unique non-null handles, and a close that only reports success: the registry
// owns the lifetime bookkeeping this test inspects.
inline std::uintptr_t fake_event_next = 0x1000;
inline HANDLE CreateEventW(void*, BOOL, BOOL, const wchar_t*) {
    return reinterpret_cast<HANDLE>(++fake_event_next);
}
inline BOOL CloseHandle(HANDLE) { return TRUE; }
