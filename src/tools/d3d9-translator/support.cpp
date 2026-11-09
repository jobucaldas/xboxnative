// SPDX-License-Identifier: GPL-3.0-or-later
// The donor's config and log modules, answered without stdio.
//
// DXUP's own util/config.cpp reads an ini file and util/log.cpp formats with
// stdio; this DLL links no CRT and has no file system, so both are replaced
// here. Logging goes through OutputDebugStringA, which Play routes to the
// launch log, so a shader the donor cannot translate is reported there instead
// of failing silently. Every config knob the donor reads (ShaderSpew,
// ShaderDump, ...) is a developer aid rather than behaviour, so all are off
// except Log.
//
// The donor's headers are on the include path as "util/..." and "dx9asm/...".
#include <cstdint>
#include <string>
#include <windows.h>
#include "util/config.h"
#include "util/log.h"

namespace dxup {

namespace config {

float getFloat(var) { return 0.0f; }
int64_t getInt(var) { return 0; }
const std::string& getString(var) { static std::string empty; return empty; }
bool getBool(var variable) { return variable == Log; }

}

namespace log {

void internal_write(const char* prefix, const char* fmt, va_list) {
    OutputDebugStringA("D3D9 translator: ");
    OutputDebugStringA(prefix);
    OutputDebugStringA(" ");
    OutputDebugStringA(fmt);
    OutputDebugStringA("\n");
}

}

}
