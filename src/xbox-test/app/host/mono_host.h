// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mono_host {
struct Result {
    bool loaded = false;
    bool executed = false;
    int exit_code = -1;
    std::string error;
};

inline uint16_t u16(const std::vector<uint8_t>& b, size_t p) {
    return uint16_t(b[p]) | (uint16_t(b[p + 1]) << 8);
}
inline uint32_t u32(const std::vector<uint8_t>& b, size_t p) {
    return uint32_t(b[p]) | (uint32_t(b[p + 1]) << 8) | (uint32_t(b[p + 2]) << 16) | (uint32_t(b[p + 3]) << 24);
}
inline bool managed_pe(const std::vector<uint8_t>& b) {
    if (b.size() < 0x40 || u16(b, 0) != 0x5a4d) return false;
    size_t nt = u32(b, 0x3c);
    if (nt > b.size() || b.size() - nt < 24 || u32(b, nt) != 0x00004550) return false;
    size_t opt = nt + 24;
    uint16_t opt_size = u16(b, nt + 20);
    if (opt > b.size() || opt_size > b.size() - opt || opt_size < 2) return false;
    uint16_t magic = u16(b, opt);
    size_t directory_count = magic == 0x10b ? 92 : magic == 0x20b ? 108 : 0;
    size_t directory_base = magic == 0x10b ? 96 : magic == 0x20b ? 112 : 0;
    if (!directory_base || opt_size < directory_base + 15 * 8 || directory_count + 4 > opt_size || u32(b, opt + directory_count) <= 14) return false;
    size_t clr = opt + directory_base + 14 * 8;
    return u32(b, clr) != 0 && u32(b, clr + 4) != 0;
}
inline bool managed_pe_file(const std::string& path) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> b(4096);
    f.read(reinterpret_cast<char*>(b.data()), b.size());
    b.resize(size_t(f.gcount()));
    return managed_pe(b);
}

#ifdef _WIN32
inline std::wstring wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    w.resize(size_t(n - 1));
    return w;
}
inline bool redirect_virtual_protect(HMODULE module) {
    auto base = reinterpret_cast<uint8_t*>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress == 0) return false;
    auto imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    bool found_alloc = false, found_protect = false;
    for (; imp->Name; ++imp) {
        auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            const char* imported = reinterpret_cast<const char*>(name->Name);
            ULONG_PTR replacement = 0;
            if (strcmp(imported, "VirtualAlloc") == 0) { replacement = reinterpret_cast<ULONG_PTR>(&VirtualAllocFromApp); found_alloc = true; }
            else if (strcmp(imported, "VirtualProtect") == 0) { replacement = reinterpret_cast<ULONG_PTR>(&VirtualProtectFromApp); found_protect = true; }
            if (!replacement) continue;
            DWORD old = 0;
            if (!VirtualProtectFromApp(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &old)) return false;
            slots->u1.Function = replacement;
            DWORD ignored = 0;
            if (!VirtualProtectFromApp(&slots->u1.Function, sizeof(slots->u1.Function), old, &ignored)) return false;
        }
    }
    return found_alloc && found_protect;
}
#endif

inline Result run(const std::string& runtime_dir, const std::string& assembly,
                  const std::vector<std::string>& args, const std::function<void()>& on_started = {},
                  const std::function<void*(const char*)>& mapped_symbol = {}) {
    Result r;
    std::string library = runtime_dir + "/mono-2.0-sgen.dll";
#ifdef _WIN32
    if (!mapped_symbol) { r.error = "Mono runtime must be mapped by Play"; return r; }
    auto symbol = mapped_symbol;
#else
    (void)mapped_symbol;
    library = runtime_dir + "/lib/libmonosgen-2.0.so";
    void* module = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module) { r.error = std::string("Mono runtime load failed: ") + dlerror(); return r; }
    auto symbol = [module](const char* name) { return dlsym(module, name); };
#endif
    using SetDirs = void (*)(const char*, const char*);
    using ConfigParse = void (*)(const char*);
    using SetAssembliesPath = void (*)(const char*);
    using JitInit = void* (*)(const char*, const char*);
    using AssemblyOpen = void* (*)(void*, const char*);
    using JitExec = int (*)(void*, void*, int, char**);
    using JitCleanup = void (*)(void*);
    auto set_dirs = reinterpret_cast<SetDirs>(symbol("mono_set_dirs"));
    auto config_parse = reinterpret_cast<ConfigParse>(symbol("mono_config_parse"));
    auto set_assemblies_path = reinterpret_cast<SetAssembliesPath>(symbol("mono_set_assemblies_path"));
    auto jit_init = reinterpret_cast<JitInit>(symbol("mono_jit_init_version"));
    auto assembly_open = reinterpret_cast<AssemblyOpen>(symbol("mono_domain_assembly_open"));
    auto jit_exec = reinterpret_cast<JitExec>(symbol("mono_jit_exec"));
    auto jit_cleanup = reinterpret_cast<JitCleanup>(symbol("mono_jit_cleanup"));
    if (!set_dirs || !config_parse || !set_assemblies_path || !jit_init || !assembly_open || !jit_exec || !jit_cleanup) {
        r.error = "Mono embedding API is incomplete";
#ifndef _WIN32
        dlclose(module);
#endif
        return r;
    }
    const std::string library_dir = runtime_dir + "/lib";
    const std::string config_dir = runtime_dir + "/etc";
    const auto assembly_path = std::filesystem::u8path(assembly).parent_path().u8string();
    const std::string assembly_dir(reinterpret_cast<const char*>(assembly_path.data()), assembly_path.size());
#ifdef _WIN32
    const std::string assembly_paths = assembly_dir + ";" + library_dir + "/mono/4.5";
#else
    const std::string assembly_paths = assembly_dir + ":" + library_dir + "/mono/4.5";
#endif
    set_dirs(library_dir.c_str(), config_dir.c_str());
    set_assemblies_path(assembly_paths.c_str());
    config_parse(nullptr);
    void* domain = jit_init("xboxnative", "v4.0.30319");
    if (!domain) { r.error = "Mono domain initialization failed"; }
    else {
        void* image = assembly_open(domain, assembly.c_str());
        if (!image) r.error = "Mono could not load managed assembly";
        else {
            r.loaded = true;
            if (on_started) on_started();
            std::vector<std::string> argv; argv.reserve(args.size() + 1); argv.push_back(assembly);
            argv.insert(argv.end(), args.begin(), args.end());
            std::vector<char*> raw; raw.reserve(argv.size());
            for (auto& arg : argv) raw.push_back(arg.data());
            r.exit_code = jit_exec(domain, image, int(raw.size()), raw.data());
            r.executed = true;
        }
        jit_cleanup(domain);
    }
#ifndef _WIN32
    dlclose(module);
#endif
    return r;
}
} // namespace mono_host
