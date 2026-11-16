// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device run of a real i686 CRT program (mingw-built, PE32) through the
// interpreter and the guest OS surface in host/x86/x86_guest.h. The same exe
// runs under Wine (the cross-check): identical stdout and exit code. Usage:
// x86-guest <fixture-crt.exe> [--quiet]
#include <cstdio>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <chrono>
#include "../host/x86/x86_guest.h"
#include "../host/x86/x86_file_guard.h"
using namespace x86;
static bool verify_critical_section_routes() {
    Memory mem; mem.map(0x1000, 0x20000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable table; Host host; Registry reg; Guest guest(mem, host, reg); guest.register_all();
    Cpu cpu(mem, table); cpu.reg[ESP] = 0x1f000;
    auto init = reg.map.find({"kernel32.dll", "initializecriticalsectionex"});
    if (init == reg.map.end() || init->second.argc != 3) return false;
    try {
        for (uint32_t i = 0; i < 64; ++i) {
            const uint32_t section = 0x2000 + i * 24;
            mem.w32(cpu.reg[ESP] + 4, section); mem.w32(cpu.reg[ESP] + 8, 0);
            mem.w32(cpu.reg[ESP] + 12, 0x10000000u); // RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO
            if (!init->second.fn(cpu) || mem.r32(section + 4) != 0xffffffffu || mem.r32(section + 8) != 0) return false;
            const uint32_t debug = mem.r32(section);
            if (!debug || mem.r32(debug + 4) != section || mem.r32(debug + 8) != debug + 8 ||
                mem.r32(debug + 12) != debug + 8) return false;
            mem.w32(debug + 24, 0x12345678u); // msvcr110!_msvcrt_init_mt_locks writes Spare[0]
            if (mem.r32(debug + 24) != 0x12345678u) return false;
        }
    } catch (const Fault&) { return false; }
    std::fprintf(stderr, "X86-CRITICAL-SECTION PASS: VC110 forced debug records support all 64 CRT locks\n");
    return true;
}
static bool verify_display_routes() {
    Memory mem; mem.map(0x1000, 0x20000, PROT_R | PROT_W | PROT_X); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable table; Host host; Registry reg; register_win32(reg, host);
    host.system_metrics=[](int index,int& value) {
        if(index==0){value=1280;return true;}
        if(index==1){value=720;return true;}
        return false;
    };
    Cpu cpu(mem, table); cpu.reg[ESP]=0x1f000; cpu.fs_base=TEB_ADDR;
    auto route = [&](const char* name, unsigned argc, const char* dll="user32.dll") -> Thunk* {
        auto it=reg.map.find({dll,Registry::key_sym(name)});
        if(it==reg.map.end()||it->second.conv!=Conv::Stdcall||it->second.argc!=argc) return nullptr;
        return &it->second;
    };
    auto args = [&](std::initializer_list<uint32_t> values) {
        unsigned i=0; for(uint32_t value:values) mem.w32(cpu.reg[ESP]+4+4*i++,value);
    };
    auto check = [](bool condition,const char* message) {
        if(!condition) std::fprintf(stderr,"X86-DISPLAY FAIL: %s\n",message);
        return condition;
    };
    Thunk* devices=route("EnumDisplayDevicesW",4);
    if(!check(devices!=nullptr,"EnumDisplayDevicesW stdcall argc=4")) return false;
    mem.w32(0x4000,840); args({0,0,0x4000,0});
    if(!check(devices->fn(cpu)==1&&mem.r32(0x4000+324)==1,"DISPLAY_DEVICEW cb=840, StateFlags, and return")) return false;
    if(!check(mem.r(0x4004,2)==92&&mem.r(0x4006,2)==92&&mem.r(0x4004+64,2)=='x',"DISPLAY_DEVICEW name and description fields")) return false;

    Thunk* settings=route("EnumDisplaySettingsW",3);
    Thunk* settings_ex=route("EnumDisplaySettingsExW",4);
    if(!check(settings&&settings_ex,"EnumDisplaySettingsW argc=3 and EnumDisplaySettingsExW argc=4")) return false;
    mem.w32(0x5000+68,220); args({0,0xffffffffu,0x5000});
    if(!check(settings->fn(cpu)==1&&mem.r(0x5000+68,2)==220&&mem.r32(0x5000+72)==0x005c0000u&&
        mem.r32(0x5000+168)==32&&mem.r32(0x5000+172)==1280&&mem.r32(0x5000+176)==720&&mem.r32(0x5000+184)==60,
        "DEVMODEW dmSize=220 and display fields")) return false;
    mem.w32(0x5000+68,219); args({0,0xffffffffu,0x5000,0x40});
    if(!check(settings_ex->fn(cpu)==0,"EnumDisplaySettingsExW rejects undersized DEVMODEW")) return false;
    mem.w32(0x5000+68,220); args({0,0xffffffffu,0x5000,0x40});
    if(!check(settings_ex->fn(cpu)==1,"EnumDisplaySettingsExW four-slot call")) return false;

    Thunk* monitors=route("EnumDisplayMonitors",4);
    Thunk* info=route("GetMonitorInfoW",2);
    if(!check(monitors&&info,"EnumDisplayMonitors argc=4 and GetMonitorInfoW argc=2")) return false;
    const uint8_t callback[]={0x8b,0x44,0x24,0x10,0x3d,0xbe,0xba,0xfe,0xca,0x0f,0x94,0xc0,0x0f,0xb6,0xc0,0xc2,0x10,0x00};
    for(unsigned i=0;i<sizeof(callback);++i) mem.w(0x6000+i,callback[i],1);
    args({0,0,0x6000,0xcafebabeu});
    if(!check(monitors->fn(cpu)==1,"EnumDisplayMonitors passes four callback slots")) return false;
    mem.w32(0x7000,40); mem.w32(0x7028,0xfeedbeefu); args({0xce000001u,0x7000});
    if(!check(info->fn(cpu)==1&&mem.r32(0x700c)==1280&&mem.r32(0x7010)==720&&mem.r32(0x7024)==1&&
        mem.r32(0x7028)==0xfeedbeefu,"MONITORINFO cbSize=40 writes only 40 bytes")) return false;
    mem.w32(0x7000,104); args({0xce000001u,0x7000});
    if(!check(info->fn(cpu)==1&&mem.r(0x7028,2)==92&&mem.r(0x702a,2)==92&&mem.r(0x704c,2)==0,
        "MONITORINFOEXW cbSize=104 writes the 64-WCHAR device field")) return false;

    Thunk* from_window=route("MonitorFromWindow",2);
    Thunk* from_point=route("MonitorFromPoint",3);
    Thunk* from_rect=route("MonitorFromRect",2);
    if(!check(from_window&&from_point&&from_rect,"MonitorFromWindow/Point/Rect stdcall slot counts")) return false;
    args({0x1234,2}); if(!check(from_window->fn(cpu)==0xce000001u,"MonitorFromWindow arguments")) return false;
    args({640,360,2}); if(!check(from_point->fn(cpu)==0xce000001u,"MonitorFromPoint POINT-by-value uses two slots")) return false;
    args({0x7000,2}); if(!check(from_rect->fn(cpu)==0xce000001u,"MonitorFromRect arguments")) return false;
    Thunk* map_key=route("MapVirtualKeyW",2);
    if(!check(map_key!=nullptr,"MapVirtualKeyW argc=2")) return false;
    args({'A',0}); if(!check(map_key->fn(cpu)==0x1e,"MapVirtualKeyW virtual-key to scan-code mapping")) return false;
    Thunk* key_state=route("GetKeyState",1);
    if(!check(key_state!=nullptr,"GetKeyState argc=1")) return false;
    args({0x41}); if(!check(key_state->fn(cpu)==0,"GetKeyState neutral key state")) return false;
    Thunk* load_cursor=route("LoadCursorW",2);
    if(!check(load_cursor!=nullptr,"LoadCursorW argc=2")) return false;
    args({0,0x7f00}); if(!check(load_cursor->fn(cpu)==0xce000002u,"LoadCursorW standard cursor resource")) return false;
    std::fprintf(stderr,"X86-DISPLAY PASS: Win32 display route arity and 840/220/40/104-byte structures\n");
    return true;
}
static bool verify_file_guard() {
    namespace fs = std::filesystem;
    const fs::path temp = fs::temp_directory_path() / ("x86-file-guard-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::create_directories(temp / "game", ec); fs::create_directories(temp / "LocalState", ec);
    fs::create_directories(temp / "otherGame", ec); fs::create_directories(temp / "outside", ec);
    fs::create_directories(temp / "known-folder-like", ec);
    const auto inside = temp / "game" / "owned.dat";
    const auto sibling = temp / "LocalState" / "steam_session.bin";
    const auto other = temp / "otherGame" / "file.dat";
    const auto outside = temp / "outside" / "file.dat";
    const auto knownFolder = temp / "known-folder-like" / "settings.ini";
    for (const auto& p : {inside, sibling, other, outside}) { std::ofstream f(p, std::ios::binary); f << "keep"; }
    unsigned refusals = 0;
    std::vector<std::u16string> protectedRoots{u"C:\\Data\\LocalState", u"D:\\XboxNativeGames", u"C:\\Program Files\\WindowsApps\\XboxNative"};
    std::vector<std::u16string> allowedRoots{u"D:\\XboxNativeGames\\Game", u"C:\\Data\\LocalState\\steam\\1234"};
    x86_file_guard::Policy guard(protectedRoots, allowedRoots, u"D:\\XboxNativeGames\\Game",
        [&](const std::string& message) { if (message.find("x86 file guard: refused ") == 0) ++refusals; });
    if (!guard.allow("DeleteFileA", u"\\\\?\\c:\\data\\xboxnativegames\\game\\sub/../owned.dat")) {
        fs::remove_all(temp, ec); std::fprintf(stderr, "X86-GUARD FAIL: normalized in-game path refused\\n"); return false;
    }
    if (guard.allow("DeleteFileA", u"owned.dat")) fs::remove(inside, ec);
    if (!guard.allow("CreateFileA(CREATE_ALWAYS)", u"C:\\Data\\Users\\Player\\AppData\\LocalLow\\Game\\settings.ini")) {
        fs::remove_all(temp, ec); std::fprintf(stderr, "X86-GUARD FAIL: known-folder write refused\\n"); return false;
    }
    { std::ofstream f(knownFolder, std::ios::binary | std::ios::trunc); f << "settings"; }
    if (!fs::exists(knownFolder) || !guard.allow("DeleteFileA", u"C:\\Data\\Users\\Player\\AppData\\LocalLow\\Game\\settings.ini")) {
        fs::remove_all(temp, ec); std::fprintf(stderr, "X86-GUARD FAIL: known-folder write/delete\n"); return false;
    }
    fs::remove(knownFolder, ec);
    if (fs::exists(knownFolder) || fs::exists(inside) ||
        !guard.allow("CreateFileA(CREATE_ALWAYS)", u"C:\\Data\\LocalState\\steam\\1234\\settings.ini") ||
        guard.allow("DeleteFileA", u"C:\\Data\\LocalState\\steam_session.bin") ||
        guard.allow("DeleteFileA", u"c:/data/localstate/STEAM_SESSION.bin") ||
        guard.allow("DeleteFileA", u"C:\\Data\\LocalState") ||
        guard.allow("DeleteFileA", u"C:\\Data\\LocalState\\logs\\launch.txt") ||
        guard.allow("DeleteFileA", u"..\\otherGame\\file.dat") ||
        guard.allow("DeleteFileA", u"D:\\XboxNativeGames\\OtherGame2\\file.dat") ||
        guard.allow("DeleteFileA", u"\\\\?\\D:\\XboxNativeGames\\outside\\file.dat") ||
        guard.allow("DeleteFileA", u"C:\\Program Files\\WindowsApps\\XboxNative\\main.exe") ||
        !fs::exists(sibling) || !fs::exists(other) || !fs::exists(outside) || refusals != 7) {
        fs::remove_all(temp, ec); std::fprintf(stderr, "X86-GUARD FAIL: containment or refusal logging\n"); return false;
    }
    fs::remove_all(temp, ec);
    std::puts("X86-GUARD PASS: in-game delete allowed; LocalState sibling, traversal and outside paths refused");
    return true;
}
static std::vector<uint8_t> slurp(const char* path) {
    std::ifstream f(path, std::ios::binary); if (!f) { printf("FAIL cannot open %s\n", path); exit(2); }
    return std::vector<uint8_t>{std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char** argv) {
    if (!verify_critical_section_routes()) { std::puts("X86-CRITICAL-SECTION FAIL: CRT forced debug-info initialization"); return 1; }
    if (!verify_display_routes()) return 1;
    if (!verify_file_guard()) return 1;
    if (argc < 2) { printf("usage: x86-guest <fixture-crt.exe>\n"); return 2; }
    auto file = slurp(argv[1]);
    Memory mem; ThunkTable table; Host host; Registry reg;
    host.nls_call=[](const Host::NlsRequest& request) {
        Host::NlsResponse response{};
        if(request.operation==Host::NlsOperation::GetUserDefaultLocaleName&&request.output_capacity==85u&&!request.output_null) {
            response.result=1;response.output={'e',0,'n',0,'-',0,'U',0,'S',0,0,0};
        }
        return response;
    };
    host.system_metrics=[](int index,int& value) {
        if(index==0){value=1280;return true;}
        if(index==1){value=720;return true;}
        return false;
    };
    Guest guest(mem, host, reg);
    guest.exe_path = "fixture-crt.exe"; guest.cmdline = guest.exe_path;
    guest.register_all();
    Image img = load_pe32(mem, table, reg, file);
    guest.image_base = img.base;
    guest.exe_tls = img.tls;
    guest.add_module("fixture-crt.exe", img.base);
    map_thunk_page(mem, table);
    Cpu cpu(mem, table);
    init_process(mem, cpu, img);
    if (mem.r32(PEB_ADDR + 0x18) != 0x50000000u) {
        std::fprintf(stderr, "X86-PEB-HEAP FAIL: ProcessHeap does not match GetProcessHeap\n"); return 1;
    }
    std::string fault;
    try { run_entry(cpu, img); } catch (const Fault& f) { fault = f.what(); }
    printf("guest: base=0x%08x entry=0x%08x imports=%u bound=%u relocs=%u\n", img.base, img.entry, img.imports, img.bound, img.relocations);
    printf("guest: instructions=%llu exit=%u%s%s\n", (unsigned long long)cpu.executed, cpu.exit_code,
           cpu.exited ? "" : " (not exited)", fault.empty() ? "" : (" fault: " + fault).c_str());
    fwrite(host.out.data(), 1, host.out.size(), stdout);
    if (!host.err.empty()) { fputs("--- stderr ---\n", stdout); fwrite(host.err.data(), 1, host.err.size(), stdout); }
    if (!fault.empty() || !cpu.exited) return 1;
    return int(cpu.exit_code);
}
