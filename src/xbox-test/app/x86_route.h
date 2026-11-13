// SPDX-License-Identifier: GPL-3.0-or-later
// Pure result-building for the launcher's PE32 route (launch.h lp_run_pe32):
// turns the x86 interpreter's PlayResult into the launch result text and the
// "PRUN EVENT x86 route: ..." log lines. No Windows or interpreter state here,
// so the mapping from a run to what the player sees is unit-testable off-device
// (tests/x86-route.cpp): a clean exit is success, anything else names the first
// unresolved import or the unsupported opcode instead of falling back silently.
#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "host/x86/x86_play.h"

namespace x86_route {
struct Report {
    bool ok = false;               // clean exit code 0, the only success
    std::string result;            // the launch result text (success or failure)
    std::vector<std::string> lines;   // "PRUN EVENT x86 route: ..." log lines in order
    std::vector<std::string> gap;     // "x86 gap: dll!name" lines, then "x86 gap: total=N"; empty unless mapped
};

// Every unresolved import of a mapped image, as "x86 gap: dll!name": one line
// per name, grouped by DLL (IAT order inside a DLL), then "x86 gap: total=N"
// with the full count. Names stop at kGapMaxLines so a 270-entry import table
// cannot flood the launch log; the total is never capped.
constexpr size_t kGapMaxLines = 600;
inline void add_gap(Report& rep, const x86::PlayResult& r) {
    auto dll_of = [](const std::string& s) { size_t p = s.find('!'); return p == std::string::npos ? s : s.substr(0, p); };
    std::vector<std::string> names = r.unresolved;
    std::stable_sort(names.begin(), names.end(),
                     [&](const std::string& a, const std::string& b) { return dll_of(a) < dll_of(b); });
    for (size_t i = 0; i < names.size() && i < kGapMaxLines; ++i) rep.gap.push_back("x86 gap: " + names[i]);
    if (names.size() > kGapMaxLines) rep.gap.push_back("x86 gap: truncated after " + std::to_string(kGapMaxLines) + " names");
    rep.gap.push_back("x86 gap: total=" + std::to_string(names.size()));
}

// The first unresolved import, "dll!symbol", or empty. The launcher's result
// names it: a guest that calls an missing import must say which one, not just
// "fault".
inline std::string first_unresolved(const x86::PlayResult& r) {
    return r.unresolved.empty() ? std::string() : r.unresolved.front();
}

inline Report report(const x86::PlayResult& r, const std::string& exe) {
    Report rep;
    char line[512];
    if(!r.loaded) {
        // The loader refused the image (not PE32, bad headers, size). No guest
        // code ran; the failure names the loader's own reason.
        snprintf(line,sizeof line,"PRUN EVENT x86 route: %s was not mapped by the x86 loader: %s",exe.c_str(),r.fault.c_str());
        rep.lines.push_back(line);
        rep.result="the x86 loader could not map "+exe+": "+r.fault;
        return rep;
    }
    add_gap(rep,r);
    if(r.finished) {
        // Same rule as the native loader: only a clean exit 0 is success; a
        // nonzero code is reported as the failure it is.
        rep.ok=r.exit_code==0;
        snprintf(line,sizeof line,"PRUN EVENT x86 route: %s exited code=%u instructions=%llu",exe.c_str(),r.exit_code,(unsigned long long)r.instructions);
        rep.lines.push_back(line);
        if(rep.ok) rep.result=exe+" completed in the x86 interpreter (exit code 0)";
        else rep.result="32-bit guest "+exe+" exited with code "+std::to_string(r.exit_code);
        // An unresolved import that was never called still matters: name the
        // first one so the next work item is obvious from the result alone.
        if(auto u=first_unresolved(r);!u.empty()) {
            snprintf(line,sizeof line,"PRUN EVENT x86 route: first unresolved import %s",u.c_str());
            rep.lines.push_back(line);
            if(!rep.ok) rep.result+="; first unresolved import "+u;
        }
        return rep;
    }
    // The guest faulted (or hit the instruction cap) mid-run. A fault on an
    // unresolved import is the first call to it; an Unsupported fault carries
    // the opcode bytes. Name the import or the fault text exactly — no silent
    // fallback to a generic message.
    std::string why;
    if(r.fault_kind=="UnresolvedImport") {
        // The fault text itself names the cause ("call to unresolved import
        // dll!sym" from the IAT stub, "GetProcAddress: ..." from the export
        // lookup); GetProcAddress raises the same kind without being an IAT
        // call, so the fault text wins and the first unresolved IAT entry is
        // only the fallback when the text carries no name.
        const std::string marker="call to unresolved import ";
        size_t at=r.fault.find(marker);
        why=at==std::string::npos?"called unresolved import "+(first_unresolved(r).empty()?r.fault:r.fault)
                                 :"called "+r.fault.substr(at);
    } else why=r.fault.empty()?"the 32-bit guest did not exit":r.fault;
    snprintf(line,sizeof line,"PRUN EVENT x86 route: %s fault kind=%s eip=0x%08x: %s",exe.c_str(),r.fault_kind.c_str(),r.fault_eip,why.c_str());
    rep.lines.push_back(line);
    rep.result="32-bit guest "+exe+": "+why;
    return rep;
}
} // namespace x86_route
