// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <optional>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#endif
// FNA3D picks its graphics backend from FNA3D_FORCE_DRIVER. The console only
// has the D3D11 path, so a managed launch pins it unless the caller (a test or
// an explicit override) already chose a driver.
//
// The pin is process-wide, so it is reference counted: the first active managed
// launch captures the original value and only the last one restores it. A retry
// that overlaps a winding-down worker therefore never mistakes the temporary
// D3D11 for an explicit override, and never drops it under a running worker.
// On Windows only Get/SetEnvironmentVariableW are used (the UWP header
// partition has no CRT _putenv_s, and the CRT copy of the environment is not
// the one an override set through Win32 lands in).
namespace fna_driver {
#ifdef _WIN32
using Str = std::wstring;
inline const wchar_t* var() { return L"FNA3D_FORCE_DRIVER"; }
inline Str d3d11() { return L"D3D11"; }
inline std::optional<Str> read() {
    Str buf(256, L'\0');
    for (;;) {
        SetLastError(0);
        DWORD n = GetEnvironmentVariableW(var(), &buf[0], DWORD(buf.size()));
        if (n == 0) {
            // 0 is both "not set" and "set to empty"; only the error tells them apart.
            if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) return std::nullopt;
            return Str();
        }
        if (n < buf.size()) { buf.resize(n); return buf; }
        buf.assign(n, L'\0'); // n is the size needed including the terminator
    }
}
inline void write(const std::optional<Str>& value) {
    SetEnvironmentVariableW(var(), value ? value->c_str() : nullptr);
}
inline std::string narrow(const Str& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? size_t(n) : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), &out[0], n, nullptr, nullptr);
    return out;
}
#else
using Str = std::string;
inline Str d3d11() { return "D3D11"; }
inline std::optional<Str> read() {
    const char* v = std::getenv("FNA3D_FORCE_DRIVER");
    if (!v) return std::nullopt;
    return Str(v);
}
inline void write(const std::optional<Str>& value) {
    if (value) setenv("FNA3D_FORCE_DRIVER", value->c_str(), 1);
    else unsetenv("FNA3D_FORCE_DRIVER");
}
inline std::string narrow(const Str& s) { return s; }
#endif
namespace detail {
struct State {
    std::atomic<bool> locked{false};
    unsigned active = 0;   // managed launches (threads) currently holding the pin
    bool pinned = false;   // the first holder set D3D11 (no explicit override)
    std::optional<Str> previous;
};
inline State state;
inline thread_local unsigned depth = 0; // this thread's own acquisitions
struct Lock {
    Lock() { while (state.locked.exchange(true, std::memory_order_acquire)) {} }
    ~Lock() { state.locked.store(false, std::memory_order_release); }
};
}
// The effective driver, for the launch log; empty when none is set.
inline std::string current() {
    auto v = read();
    return v ? narrow(*v) : std::string();
}
inline void acquire() {
    if (detail::depth++ != 0) return;
    detail::Lock l;
    if (detail::state.active++ != 0) return;
    auto cur = read();
    if (cur && !cur->empty()) { detail::state.pinned = false; return; } // explicit override stays untouched
    detail::state.previous = cur; // unset, or present but empty
    write(d3d11());
    detail::state.pinned = true;
}
inline void release() {
    if (detail::depth == 0 || --detail::depth != 0) return;
    detail::Lock l;
    if (--detail::state.active != 0) return;
    if (detail::state.pinned) write(detail::state.previous);
    detail::state.pinned = false;
    detail::state.previous.reset();
}
// Drops every acquisition of the calling thread. A SEH fault skips C++
// destructors, so lp_run_seh calls this after its __try/__except.
inline void release_all() {
    if (detail::depth == 0) return;
    detail::depth = 1;
    release();
}
// Scope guard for the normal and early-return paths.
class Scope {
public:
    Scope() { acquire(); }
    ~Scope() { release(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
}
