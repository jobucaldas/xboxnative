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
#include "../host/x86/x86_play.h"
#include "../host/x86/x86_file_guard.h"
using namespace x86;
static bool verify_system_info() {
    Memory mem; mem.map(0x1000, 0x2000, PROT_R | PROT_W);
    Host host; Registry reg; Guest guest(mem, host, reg); guest.register_all();
    ThunkTable thunks; Cpu cpu(mem, thunks); cpu.reg[ESP] = 0x2000;
    auto it = reg.map.find({"kernel32.dll", "getsysteminfo"});
    if (it == reg.map.end()) return false;
    mem.w32(cpu.reg[ESP] + 4, 0x1000); it->second.fn(cpu);
    return mem.r32(0x1018) == 586 && mem.r32(0x101c) == 65536;
}
static bool verify_conversion_terminators() {
    Memory mem; mem.map(0x1000, 0x2000, PROT_R | PROT_W);
    Host host; Registry reg; Guest guest(mem, host, reg); guest.register_all();
    ThunkTable thunks; Cpu cpu(mem, thunks); cpu.reg[ESP] = 0x2000;
    mem.w8(0x1000, 'A'); mem.w8(0x1001, 0);
    auto mb = reg.map.find({"kernel32.dll", "multibytetowidechar"});
    auto wc = reg.map.find({"kernel32.dll", "widechartomultibyte"});
    if (mb == reg.map.end() || wc == reg.map.end()) return false;
    auto args = [&](std::initializer_list<uint32_t> values) { unsigned i=0; for (auto v: values) mem.w32(cpu.reg[ESP]+4+4*i++,v); };
    args({1252,0,0x1000,0xffffffffu,0x1100,2});
    if (mb->second.fn(cpu) != 2 || mem.r(0x1100,2) != 'A' || mem.r(0x1102,2) != 0) return false;
    args({1252,0,0x1100,0xffffffffu,0x1200,2,0,0});
    return wc->second.fn(cpu) == 2 && mem.r(0x1200,2) == 'A';
}
static bool verify_cpinfo_layout() {
    Memory mem; mem.map(0x1000, 0x2000, PROT_R | PROT_W);
    Host host; Registry reg; Guest guest(mem, host, reg); guest.register_all();
    ThunkTable thunks; Cpu cpu(mem, thunks); cpu.reg[ESP] = 0x2000;
    auto it = reg.map.find({"kernel32.dll", "getcpinfo"});
    if (it == reg.map.end()) return false;
    for (uint32_t cp : {1252u, 65001u}) {
        mem.w32(cpu.reg[ESP] + 4, cp); mem.w32(cpu.reg[ESP] + 8, 0x1000);
        if (!it->second.fn(cpu) || mem.r32(0x1000) != (cp == 65001 ? 4u : 1u) ||
            mem.r(0x1004, 2) != '?' || mem.r(0x1006, 2) != 0) return false;
    }
    return true;
}
static bool verify_printf_padding() {
    Memory mem; mem.map(0x1000, 0x1000, PROT_R | PROT_W);
    Host host; Registry reg; Guest guest(mem, host, reg);
    mem.w32(0x1000, uint32_t(-42)); mem.w32(0x1004, 42);
    uint32_t ap = 0x1000;
    return guest.format_va("%05d|%#08x", ap) == "-0042|0x00002a";
}
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
static bool verify_xinput_routes() {
    Memory mem; mem.map(0x1000, 0x20000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable table; Host host; Registry reg; Guest guest(mem, host, reg); guest.register_all();
    Cpu cpu(mem, table); cpu.reg[ESP]=0x1f000; cpu.fs_base=TEB_ADDR;
    bool saw_set=false, saw_enable=false;
    host.xinput_call=[&](Host::XInputCall& q) {
        q.result=q.kind==Host::XInputCall::Kind::Enable||q.index==1?0:1167;
        if(q.result)return;
        switch(q.kind) {
        case Host::XInputCall::Kind::GetState:
        case Host::XInputCall::Kind::GetStateEx:
            q.output.resize(16);memcpy(q.output.data(),std::array<uint32_t,4>{9,0x1000,0,0}.data(),16);break;
        case Host::XInputCall::Kind::SetState:saw_set=q.input.size()==4&&q.input[0]==0x34;break;
        case Host::XInputCall::Kind::GetCapabilities:
        case Host::XInputCall::Kind::GetCapabilitiesEx:q.output.resize(20);q.output[0]=1;break;
        case Host::XInputCall::Kind::Enable:saw_enable=q.flags==1;break;
        case Host::XInputCall::Kind::GetBatteryInformation:q.result=50;break;
        case Host::XInputCall::Kind::GetKeystroke:q.result=4306;break;
        }
    };
    auto route=[&](const char* dll,const char* name,unsigned argc)->Thunk* {
        auto it=reg.map.find({dll,Registry::key_sym(name)});
        return it!=reg.map.end()&&it->second.argc==argc?&it->second:nullptr;
    };
    for(const char* dll:{"xinput1_3.dll","xinput1_4.dll","xinput9_1_0.dll"}) {
        if(!route(dll,"XInputGetState",2)||!route(dll,"XInputSetState",2)||!route(dll,"XInputGetCapabilities",3)||
           !route(dll,"XInputEnable",1)||!route(dll,"XInputGetBatteryInformation",3)||!route(dll,"XInputGetKeystroke",3))return false;
    }
    auto args=[&](std::initializer_list<uint32_t> values) {unsigned i=0;for(auto value:values)mem.w32(cpu.reg[ESP]+4+4*i++,value);};
    auto* get=route("xinput1_4.dll","XInputGetState",2);args({1,0x3000});
    if(get->fn(cpu)!=0||mem.r32(0x3000)!=9||mem.r32(0x3004)!=0x1000)return false;
    args({0,0x3000});if(get->fn(cpu)!=1167)return false;
    auto* set=route("xinput1_4.dll","XInputSetState",2);mem.w32(0x3100,0x12340034);args({1,0x3100});
    if(set->fn(cpu)!=0||!saw_set)return false;
    auto* caps=route("xinput1_4.dll","XInputGetCapabilities",3);args({1,0,0x3200});
    if(caps->fn(cpu)!=0||mem.r(0x3200,1)!=1)return false;
    auto* enable=route("xinput1_4.dll","XInputEnable",1);args({1});if(enable->fn(cpu)!=0||!saw_enable)return false;
    auto* battery=route("xinput1_4.dll","XInputGetBatteryInformation",3);args({1,0,0x3300});
    if(battery->fn(cpu)!=50)return false;
    auto* key=route("xinput1_4.dll","XInputGetKeystroke",3);args({1,0,0x3400});if(key->fn(cpu)!=4306)return false;
    uint32_t module=guest.load_guest_dll(nullptr,"xinput1_4.dll",table);
    uint32_t ex=guest.get_proc_address(module,100,table);
    const Thunk* ex_thunk=table.at(ex);
    auto ordinal=reg.ordinals.find({"xinput1_4.dll",uint16_t(100)});
    if(!module||!ex_thunk||ex_thunk->name!="XInputGetStateEx"||ex_thunk->argc!=2||ordinal==reg.ordinals.end())return false;
    uint32_t caps_ex=guest.get_proc_address(module,108,table);
    const Thunk* caps_ex_thunk=table.at(caps_ex);
    auto caps_ordinal=reg.ordinals.find({"xinput1_4.dll",uint16_t(108)});
    if(!caps_ex_thunk||caps_ex_thunk->name!="XInputGetCapabilitiesEx"||caps_ex_thunk->argc!=4||caps_ordinal==reg.ordinals.end())return false;
    mem.write_bytes(0x15000,"XInputGetState",15);
    uint32_t named=guest.get_proc_address(module,0x15000,table);
    if(!named||!table.at(named)||table.at(named)->name!="XInputGetState")return false;
    std::fprintf(stderr,"X86-XINPUT PASS: 32-bit routes, fake connected/empty slots, LoadLibrary, GetProcAddress ordinals 100/108\n");
    return true;
}
static bool verify_guest_bugbot_routes() {
    Memory mem; mem.map(0x1000, 0x20000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable table; Host host; Registry reg; Guest guest(mem, host, reg); guest.register_all();
    Cpu cpu(mem, table); cpu.reg[ESP] = 0x1f000; cpu.fs_base = TEB_ADDR;
    auto args = [&](std::initializer_list<uint32_t> values) {
        unsigned i=0; for(uint32_t value:values) mem.w32(cpu.reg[ESP]+4+4*i++,value);
    };
    auto route = [&](const char* name) -> Thunk* {
        auto it=reg.map.find({"kernel32.dll",Registry::key_sym(name)});
        return it==reg.map.end()?nullptr:&it->second;
    };
    Thunk* set_heap=route("HeapSetInformation"); Thunk* query_heap=route("HeapQueryInformation");
    if(!set_heap||!query_heap)return false;
    args({0,1,0,0}); if(set_heap->fn(cpu)!=1){std::fprintf(stderr,"guest-route: NULL HeapSetInformation failed\n");return false;}
    args({0,0,0x3000,4,0x3004});
    if(query_heap->fn(cpu)!=1||mem.r32(0x3000)!=2||mem.r32(0x3004)!=4)return false;

    host.nls_call=[](const Host::NlsRequest& request) {
        Host::NlsResponse response{};
        if(request.operation==Host::NlsOperation::LCMapStringW&&(request.flags&0x400u)) {
            response.result=3; response.output={0x41,0x00,0x00};
        }
        return response;
    };
    mem.w(0x4000,'A',2); mem.w32(0x5000,0xccccccccu);
    args({0x0409,0x400,0x4000,1,0x5000,3});
    Thunk* map=route("LCMapStringW");
    if(!map||map->fn(cpu)!=3||mem.r8(0x5000)!=0x41||mem.r8(0x5001)!=0||mem.r8(0x5002)!=0||mem.r8(0x5003)!=0xcc)return false;

    mem.write_bytes(0x6000,"folder",7); bool created=false, find_closed=false;
    host.file_call=[&](Host::FileCall& call) {
        if(call.operation==Host::FileCall::Operation::CreateDirectoryA) {
            created=call.path_a=="folder"; call.succeeded=true;
        } else if(call.kind==Host::FileCall::Kind::FindClose) {
            find_closed=call.handle==77; call.succeeded=true;
        }
    };
    args({0x6000,0x7000}); Thunk* create_dir=route("CreateDirectoryA");
    if(!create_dir||create_dir->fn(cpu)!=1||!created)return false;
    guest.find_handles[0x51000001]=77; args({0x51000001}); Thunk* close=route("CloseHandle");
    if(!close||close->fn(cpu)!=1||!find_closed||guest.find_handles.count(0x51000001))return false;

    Thunk* find_resource_ex=route("FindResourceExW"); Thunk* load_resource=route("LoadResource"); Thunk* lock_resource=route("LockResource");
    if(!find_resource_ex||find_resource_ex->argc!=4||!load_resource||!lock_resource)return false;
    guest.image_base=0x400000; guest.resource_handles[0x74000001]={0x400000,0x8000,4};
    args({0x400000,0x74000001});
    if(load_resource->fn(cpu)!=0x74000001)return false;
    args({0x74000001});
    if(lock_resource->fn(cpu)!=0x8000)return false;
    std::fprintf(stderr,"X86-GUEST-ROUTES PASS: NULL heaps, sort-key bytes, security attributes, find CloseHandle, and resource handles\n");
    return true;
}
static bool verify_guest_bugbot_b2_routes() {
    Memory mem; mem.map(0x1000, 0x20000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable table; Host host; Registry reg; Guest guest(mem, host, reg); guest.register_all();
    Cpu cpu(mem, table); cpu.reg[ESP] = 0x1f000; cpu.fs_base = TEB_ADDR;
    auto args = [&](std::initializer_list<uint32_t> values) {
        unsigned i=0; for(uint32_t value:values) mem.w32(cpu.reg[ESP]+4+4*i++,value);
    };
    auto route = [&](const char* dll, const char* name) -> Thunk* {
        auto it=reg.map.find({dll,Registry::key_sym(name)});
        return it==reg.map.end()?nullptr:&it->second;
    };
    auto text8 = [&](uint32_t p) { std::string s; for(uint32_t i=0;mem.r8(p+i);++i) s.push_back(char(mem.r8(p+i))); return s; };
    auto fail = [](const char* what) { std::fprintf(stderr,"X86-GUEST-B2 FAIL: %s\n",what); return false; };

    for(uint32_t i=0;i<16;++i) mem.w8(0x7000+i,'A');
    args({0x7000,16});
    if(route("kernel32.dll","IsBadStringPtrA")->fn(cpu)!=0) return fail("IsBadStringPtrA readable run without a terminator");

    mem.write_bytes(0x7100,"C:foo",6);
    args({0x7100});
    if(route("shlwapi.dll","PathIsRelativeA")->fn(cpu)!=0) return fail("PathIsRelativeA treats C:foo as relative");

    mem.write_bytes(0x7200,"C:\\dir",7); mem.write_bytes(0x7300,"D:\\other",9);
    args({0x7200,0x7300});
    if(route("shlwapi.dll","PathAppendA")->fn(cpu)!=1||text8(0x7200)!="D:\\other") return fail("PathAppendA kept the destination for an absolute suffix");

    host.random_bytes=[](uint8_t* bytes,auto n){for(decltype(n) i=0;i<n;++i)bytes[i]=0xff;return true;};
    args({0x7400});
    if(route("ole32.dll","CoCreateGuid")->fn(cpu)!=0||mem.r8(0x7407)!=0x4f||mem.r8(0x7408)!=0xbf) return fail("CoCreateGuid version nibble or variant byte");
    args({0x7400,0x7500,39});
    if(route("ole32.dll","StringFromGUID2")->fn(cpu)!=39||guest.wstr(0x7500)!=u"{FFFFFFFF-FFFF-4FFF-BFFF-FFFFFFFFFFFF}") return fail("StringFromGUID2 is not uppercase or misplaces the version");

    mem.w32(0x7600+4,1); mem.w32(0x7600+8,0x7700); mem.w32(0x7600+12,0);
    mem.w32(0x7700,0); mem.w32(0x7600+18,0xffffffffu); mem.w32(0x7600+22,0x1234);
    args({0x7600});
    route("shell32.dll","SHFileOperationW")->fn(cpu);
    if(mem.r32(0x7600+18)!=0||mem.r32(0x7600+22)!=0x1234) return fail("SHFILEOPSTRUCT abort flag is not at packed offset 18");

    args({0,1,0,0}); uint32_t manual_event=route("kernel32.dll","CreateEventA")->fn(cpu);
    args({0,1,1,0}); uint32_t signaled_event=route("kernel32.dll","CreateEventA")->fn(cpu);
    mem.w32(0x7800,manual_event); mem.w32(0x7804,signaled_event);
    args({2,0x7800,1,0,0xff});
    if(route("user32.dll","MsgWaitForMultipleObjects")->fn(cpu)!=258) return fail("MsgWaitForMultipleObjects ignored fWaitAll");
    args({2,0x7800,0,0,0xff});
    if(route("user32.dll","MsgWaitForMultipleObjects")->fn(cpu)!=1) return fail("MsgWaitForMultipleObjects wait-any control");
    args({0,0x7800,0,0,1});
    if(route("user32.dll","MsgWaitForMultipleObjectsEx")->fn(cpu)!=258) return fail("empty MWMO_WAITALL returned without waiting");

    mem.w32(TEB_ADDR+0x24,7);
    args({0,1,0}); uint32_t mutex=route("kernel32.dll","CreateMutexA")->fn(cpu);
    guest.abandon_mutexes(7);
    mem.w32(0x7808,mutex); args({1,0x7808,0,0});
    if(route("kernel32.dll","WaitForMultipleObjects")->fn(cpu)!=0x80u) return fail("WaitForMultipleObjects dropped WAIT_ABANDONED_0");

    args({0x72000222u});
    uint32_t bits=guest.heap.alloc(16);
    guest.gdi_objects[0x72000222u]=Guest::GdiObject{1,4,4,bits,32,0,false,false,16};
    if(route("gdi32.dll","DeleteObject")->fn(cpu)!=1||guest.heap.alloc(16)!=bits) return fail("DeleteObject leaked the DIB pixel buffer");

    guest.gdi_objects[0x72000111u]=Guest::GdiObject{1,4,2,0,32,0,false,false,16};
    args({0,0x72000111u,0,2,0,0x7900,0});
    route("gdi32.dll","GetDIBits")->fn(cpu);
    route("gdi32.dll","GetDIBits")->fn(cpu);
    if(mem.r32(0x7900+16)!=3||mem.r32(0x7900+40)!=0x00ff0000u) return fail("GetDIBits requery dropped BI_BITFIELDS masks");

    auto put16=[&](uint32_t p,const char16_t* s){for(size_t i=0;;++i){mem.w(p+uint32_t(i*2),s[i],2);if(!s[i])break;}};
    put16(0x7b00,u"x \"b\"\"c\" d");
    args({0x7b00,0x7c00});
    uint32_t argv=route("shell32.dll","CommandLineToArgvW")->fn(cpu);
    if(!argv||mem.r32(0x7c00)!=3||guest.wstr(mem.r32(argv))!=u"x"||guest.wstr(mem.r32(argv+4))!=u"b\"c"||mem.r32(argv+12)!=0) return fail("CommandLineToArgvW argv terminator or doubled quotes");
    return true;
}
static bool verify_nls_size_query_capture() {
    Host::NlsResponse response{}; response.result=5;
    if(!play_capture_nls_output(response,nullptr,0,10,false,0)||response.result!=5||!response.output.empty())return false;
    std::fprintf(stderr,"X86-NLS PASS: zero-capacity size query does not read host output\n");
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
    if (!verify_system_info()) { std::puts("X86-GUEST FAIL: system info layout"); return 1; }
    if (!verify_conversion_terminators()) { std::puts("X86-GUEST FAIL: conversion terminator counts"); return 1; }
    if (!verify_cpinfo_layout()) { std::puts("X86-GUEST FAIL: GetCPInfo layout"); return 1; }
    if (!verify_printf_padding()) { std::puts("X86-GUEST FAIL: printf sign and prefix zero padding"); return 1; }
    if (!verify_critical_section_routes()) { std::puts("X86-CRITICAL-SECTION FAIL: CRT forced debug-info initialization"); return 1; }
    if (!verify_display_routes()) return 1;
    if (!verify_xinput_routes()) { std::puts("X86-XINPUT FAIL: guest route, layout, ordinal, or fake-pad contract"); return 1; }
    if (!verify_guest_bugbot_routes()) { std::puts("X86-GUEST-ROUTES FAIL: heap, NLS, directory, find-handle, or resource contract"); return 1; }
    if (!verify_guest_bugbot_b2_routes()) { std::puts("X86-GUEST-B2 FAIL: see stderr for the failing route"); return 1; }
    if (!verify_nls_size_query_capture()) { std::puts("X86-NLS FAIL: size-query output capture"); return 1; }
    if (!verify_file_guard()) return 1;
    if (argc < 2) { printf("usage: x86-guest <fixture-crt.exe>\n"); return 2; }
    auto file = slurp(argv[1]);
    Memory mem; ThunkTable table; Host host; Registry reg;
    host.nls_call=[](const Host::NlsRequest& request) {
        Host::NlsResponse response{};
        if(request.operation==Host::NlsOperation::GetUserDefaultLocaleName&&request.output_capacity==85u&&!request.output_null) {
            response.result=6;response.output={'e',0,'n',0,'-',0,'U',0,'S',0,0,0};
        }
        return response;
    };
    host.system_metrics=[](int index,int& value) {
        if(index==0){value=1280;return true;}
        if(index==1){value=720;return true;}
        return false;
    };
    std::map<std::u16string,std::map<std::u16string,std::pair<uint32_t,std::vector<uint8_t>>>> registry;
    std::map<uintptr_t,std::u16string> registry_handles;uintptr_t registry_next=1;
    registry[u"HKCU"]={};
    host.registry_call=[&](Host::RegistryCall& q) {
        auto full_path=[&](const std::u16string& base,const std::u16string& sub){return sub.empty()?base:base+u"\\"+sub;};
        auto resolve=[&](uintptr_t handle)->std::u16string {auto it=registry_handles.find(handle);return it==registry_handles.end()?std::u16string():it->second;};
        if(q.kind==Host::RegistryCall::Kind::Open||q.kind==Host::RegistryCall::Kind::Create){
            std::u16string base=q.root==0x80000001u?u"HKCU":resolve(q.root);
            std::u16string path=full_path(base,q.subkey);
            if(q.kind==Host::RegistryCall::Kind::Open&&!registry.count(path)){q.status=2;return;}
            bool created=!registry.count(path);if(q.kind==Host::RegistryCall::Kind::Create){std::u16string prefix;for(char16_t ch:path){prefix.push_back(ch);if(ch==u'\\')registry.emplace(prefix.substr(0,prefix.size()-1),std::map<std::u16string,std::pair<uint32_t,std::vector<uint8_t>>>{});}registry.emplace(path,std::map<std::u16string,std::pair<uint32_t,std::vector<uint8_t>>>{});}
            if(!created&&q.kind==Host::RegistryCall::Kind::Create)q.disposition=2;else q.disposition=1;
            q.result_handle=++registry_next;registry_handles[q.result_handle]=path;q.status=0;return;
        }
        std::u16string base=resolve(q.handle);if(base.empty()){q.status=6;return;}
        if(q.kind==Host::RegistryCall::Kind::Close){q.status=registry_handles.erase(q.handle)?0:6;return;}
        auto values=registry.find(base);if(values==registry.end()){q.status=6;return;}
        if(q.kind==Host::RegistryCall::Kind::Set){values->second[q.name]={q.type,q.data};q.status=0;return;}
        if(q.kind==Host::RegistryCall::Kind::Query){auto v=values->second.find(q.name);if(v==values->second.end()){q.status=2;return;}q.type=v->second.first;q.data=v->second.second;q.status=0;return;}
        if(q.kind==Host::RegistryCall::Kind::DeleteValue){q.status=values->second.erase(q.name)?0:2;return;}
        if(q.kind==Host::RegistryCall::Kind::DeleteKey){std::u16string path=full_path(base,q.subkey),prefix=path+u"\\";for(const auto& k:registry)if(k.first.compare(0,prefix.size(),prefix)==0){q.status=5;return;}q.status=registry.erase(path)?0:2;return;}
        if(q.kind==Host::RegistryCall::Kind::EnumValue){if(q.index>=values->second.size()){q.status=259;return;}auto it=values->second.begin();std::advance(it,q.index);q.name=it->first;q.type=it->second.first;q.data=it->second.second;q.status=0;return;}
        if(q.kind==Host::RegistryCall::Kind::EnumKey){std::u16string prefix=base+u"\\";std::vector<std::u16string> children;for(const auto& k:registry)if(k.first.compare(0,prefix.size(),prefix)==0){auto tail=k.first.substr(prefix.size());if(tail.find(u'\\')==std::u16string::npos)children.push_back(tail);}if(q.index>=children.size()){q.status=259;return;}q.name=children[q.index];q.status=0;return;}
        q.status=120;
    };
    host.random_bytes=[](uint8_t* out,uint32_t size){for(uint32_t i=0;i<size;++i)out[i]=uint8_t(i+1);return true;};
    Guest guest(mem, host, reg);
    guest.exe_path = "fixture-crt.exe"; guest.cmdline = guest.exe_path;
    guest.register_all();
    reg.add("msvcrt.dll","wcscmp",Conv::Cdecl,2,[&guest](Cpu& c)->uint32_t {
        auto left=guest.wstr(c.arg(0)),right=guest.wstr(c.arg(1));
        return left<right?uint32_t(-1):left>right?1u:0u;
    });
    Image img = load_pe32(mem, table, reg, file);
    guest.image_base = img.base;
    guest.exe_tls = img.tls;
    guest.add_module("fixture-crt.exe", img.base);
    map_thunk_page(mem, table);
    Cpu cpu(mem, table); // X86_DBT=1 enables the same per-process cache used by the thread/TLS fixtures.
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
