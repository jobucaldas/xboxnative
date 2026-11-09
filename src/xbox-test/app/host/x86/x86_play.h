// SPDX-License-Identifier: GPL-3.0-or-later
// Run a 32-bit (PE32) executable in the in-process x86 interpreter, with the
// guest OS surface in x86_guest.h serving its imports.
//
// This is the launcher's PE32 entry point. It is deliberately one function with
// a structured result: a guest that faults reports the fault kind, the guest
// EIP and the instruction bytes, and the imports it could not bind are listed by
// name, so a milestone subset is never mistaken for general Win32 support.
// Nothing here touches the host's address space with a guest pointer: the guest
// address space is the interpreter's own page table (x86_mem.h).
#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "x86_guest.h"
namespace x86 {
struct PlayResult {
    bool loaded = false;        // the PE32 image mapped and its imports were bound
    bool finished = false;      // the guest called ExitProcess (or its entry returned)
    uint32_t exit_code = 0;
    uint64_t instructions = 0;
    std::string fault;          // empty when finished; else the Fault text
    std::string fault_kind;     // AccessViolation, Unsupported, UnresolvedImport, ...
    uint32_t fault_eip = 0;
    uint32_t base = 0, entry = 0;
    unsigned imports = 0, bound = 0, relocations = 0;
    std::vector<std::string> unresolved;   // "dll!symbol", in IAT order
};
// `name` is the executable's file name as the guest sees it (GetModuleFileName,
// argv[0]); `cmdline` is the command line (quoted exe plus the option's
// arguments). `tee` receives guest stdout and stderr as it is written, so the
// launcher log shows it live. `dir` is the guest's working directory: the
// interpreter keeps no host current directory, so it is only reported.
// `on_loaded` runs once the image is mapped and its imports bound, before the
// entry point: the launcher reports progress from there, so a guest that runs
// for a long time is not mistaken for one that never started.
inline PlayResult run_image(const std::vector<uint8_t>& file, const std::string& name, const std::string& cmdline,
                            const std::function<void(const std::string&)>& tee = nullptr,
                            const std::function<void(const PlayResult&)>& on_loaded = nullptr) {
    PlayResult r;
    try {
        Memory mem; ThunkTable table; Host host; Registry reg;
        host.tee = tee;
        Guest guest(mem, host, reg);
        guest.exe_path = name;
        guest.cmdline = cmdline.empty() ? name : cmdline;
        guest.register_all();
        Image img = load_pe32(mem, table, reg, file);
        r.loaded = true; r.base = img.base; r.entry = img.entry;
        r.imports = img.imports; r.bound = img.bound; r.relocations = img.relocations; r.unresolved = img.unresolved;
        guest.image_base = img.base;
        guest.modules[name] = img.base;
        map_thunk_page(mem, table);
        Cpu cpu(mem, table);
        init_process(mem, cpu, img);
        if (on_loaded) on_loaded(r);
        try {
            run_entry(cpu, img);
            r.finished = cpu.exited;
            r.exit_code = cpu.exit_code;
        } catch (const Fault& fault) {
            r.fault = fault.what(); r.fault_eip = fault.eip;
            switch (fault.kind) {
            case FaultKind::AccessViolation: r.fault_kind = "AccessViolation"; break;
            case FaultKind::ExecuteViolation: r.fault_kind = "ExecuteViolation"; break;
            case FaultKind::DivideError: r.fault_kind = "DivideError"; break;
            case FaultKind::InvalidOpcode: r.fault_kind = "InvalidOpcode"; break;
            case FaultKind::Unsupported: r.fault_kind = "Unsupported"; break;
            case FaultKind::Breakpoint: r.fault_kind = "Breakpoint"; break;
            case FaultKind::InstructionLimit: r.fault_kind = "InstructionLimit"; break;
            case FaultKind::BadThunk: r.fault_kind = "BadThunk"; break;
            case FaultKind::UnresolvedImport: r.fault_kind = "UnresolvedImport"; break;
            }
        }
        r.instructions = cpu.executed;
    } catch (const std::exception& e) {
        r.fault = std::string("loader: ") + e.what();
        r.fault_kind = "Loader";
    }
    return r;
}
#if defined(_WIN32)
// Reads the executable and runs it. `exe` is the full path, `dir` the guest's
// working directory (the launch option's, else the executable's own).
inline PlayResult run(const std::wstring& exe, const std::wstring& dir, const std::string& cmdline,
                      const std::function<void(const std::string&)>& tee = nullptr,
                      const std::function<void(const PlayResult&)>& on_loaded = nullptr) {
    PlayResult r;
    FILE* f = _wfopen(exe.c_str(), L"rb");
    if (!f) { r.fault = "cannot open the executable"; r.fault_kind = "Loader"; return r; }
    _fseeki64(f, 0, SEEK_END);
    long long length = _ftelli64(f);
    rewind(f);
    if (length < 0 || length > 256ll * 1024 * 1024) { fclose(f); r.fault = "executable size"; r.fault_kind = "Loader"; return r; }
    std::vector<uint8_t> file(static_cast<size_t>(length));
    size_t got = fread(file.data(), 1, file.size(), f);
    fclose(f);
    if (got != file.size()) { r.fault = "short read of the executable"; r.fault_kind = "Loader"; return r; }
    std::string name;
    for (size_t i = exe.find_last_of(L"\\/") + 1; i < exe.size(); ++i) name += char(exe[i]);
    (void)dir;
    return run_image(file, name, cmdline, tee, on_loaded);
}
#endif
}
