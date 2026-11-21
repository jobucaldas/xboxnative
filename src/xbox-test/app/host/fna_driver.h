// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdlib>
#include <optional>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif
// FNA3D picks its graphics backend from FNA3D_FORCE_DRIVER. The console only
// has the D3D11 path, so a managed launch pins it unless the caller (a test or
// an explicit override) already chose a driver. The pin lasts for one launch:
// the destructor restores the previous state on every return path.
namespace fna_driver {
inline std::string current() {
    const char* v = std::getenv("FNA3D_FORCE_DRIVER");
    return v ? v : "";
}
inline void put(const std::optional<std::string>& value) {
#ifdef _WIN32
    SetEnvironmentVariableA("FNA3D_FORCE_DRIVER", value ? value->c_str() : nullptr);
    _putenv_s("FNA3D_FORCE_DRIVER", value ? value->c_str() : "");
#else
    if (value) setenv("FNA3D_FORCE_DRIVER", value->c_str(), 1);
    else unsetenv("FNA3D_FORCE_DRIVER");
#endif
}
class ForceD3D11 {
public:
    ForceD3D11() {
        const char* v = std::getenv("FNA3D_FORCE_DRIVER");
        if (v && *v) return; // explicit override stays untouched
        if (v) previous_ = std::string(); // present but empty: restore it as empty
        put(std::string("D3D11"));
        pinned_ = true;
    }
    ~ForceD3D11() { if (pinned_) put(previous_); }
    ForceD3D11(const ForceD3D11&) = delete;
    ForceD3D11& operator=(const ForceD3D11&) = delete;
    bool pinned() const { return pinned_; }
private:
    std::optional<std::string> previous_;
    bool pinned_ = false;
};
}
