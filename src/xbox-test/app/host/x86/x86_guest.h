// SPDX-License-Identifier: GPL-3.0-or-later
// Guest OS surface for the PE32 interpreter (milestone 1/2): the kernel32 and
// msvcrt calls a real i686 CRT startup and a console program make. Every guest
// pointer is bounds- and permission-checked through Memory, so a bad pointer is
// a Fault and never a host access. Buffers the guest hands us are copied in and
// out; no guest address is ever treated as a host pointer.
//
// Only the (dll, symbol) pairs registered here are served. Anything else stays a
// named UnresolvedImport Fault, so an unimplemented API is reported by name
// instead of being silently stubbed. That is deliberate: a milestone subset is
// never mistaken for general Win32 support.
#pragma once
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <random>
#include <tuple>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#endif
#include "x86_win32.h"
#include "x86_dinput8.h"
#include "x86_d3d9.h"
#include "x86_dsound.h"
namespace x86 {
// Loaded guest DLLs, shared between the loader and the Guest surface. Kept in
// x86_pe.h's Dll form so GetProcAddress reads the same export tables the
// import binder filled.
struct Dll;
struct SteamClientRegistry { uint32_t account=0; std::string client; };
inline SteamClientRegistry& steam_client_registry_config() { static SteamClientRegistry config; return config; }
inline void set_steam_client_registry(uint32_t account,const std::string& client) {
    steam_client_registry_config()={account,client};
}

// ---- guest heap: first-fit free list over a lazily mapped guest arena ----
struct GuestHeap {
    Memory& mem;
    uint32_t base, mapped_end = 0, top = 0;
    struct Block { uint32_t addr, size; bool free; };
    std::vector<Block> blocks;
    GuestHeap(Memory& m, uint32_t at) : mem(m), base(at) {}
    void map_upto(uint32_t end) {
        if (end <= mapped_end) return;
        uint32_t a = mapped_end ? mapped_end : base;
        uint32_t want = ((end - a) + 0xffffu) & ~0xffffu;
        mem.map(a, want, PROT_R | PROT_W);
        mapped_end = a + want;
    }
    uint32_t alloc(uint32_t n) {
        if (!n) n = 1;
        n = (n + 7u) & ~7u;
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (!blocks[i].free || blocks[i].size < n) continue;
            uint32_t a = blocks[i].addr, sz = blocks[i].size;
            if (sz >= n + 16) {
                blocks[i].size = n; blocks[i].free = false;
                blocks.insert(blocks.begin() + long(i) + 1, Block{a + n, sz - n, true});
            } else blocks[i].free = false;
            return a;
        }
        if (!top) top = base;
        map_upto(top + n);
        uint32_t a = top;
        blocks.push_back(Block{a, n, false});
        top = a + n;
        return a;
    }
    uint32_t size_of(uint32_t addr) const {
        for (const Block& b : blocks) if (b.addr == addr) return b.size;
        return 0;
    }
    void release(uint32_t addr) {
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (blocks[i].addr != addr) continue;
            blocks[i].free = true;
            for (size_t j = i + 1; j < blocks.size() && blocks[j].free; ++j) {  // coalesce forward
                blocks[i].size += blocks[j].size; blocks.erase(blocks.begin() + long(j)); --j;
            }
            if (i && blocks[i - 1].free) {  // coalesce backward
                blocks[i - 1].size += blocks[i].size; blocks.erase(blocks.begin() + long(i));
            }
            return;
        }
    }
    uint32_t realloc(uint32_t addr, uint32_t n) {
        uint32_t old = size_of(addr);
        uint32_t p = alloc(n);
        if (old) mem.write_bytes(p, mem.read_bytes(addr, old < n ? old : n).data(), old < n ? old : n);
        if (addr) release(addr);
        return p;
    }
};

// ---- the guest process surface ----
struct Guest {
    using Clock = std::chrono::steady_clock;
    Memory& mem; Host& host; Registry& reg;
    GuestHeap heap;
    static uint64_t steady_millis() {
        return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
    }
    static uint64_t steady_micros() {
        return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count());
    }
    uint64_t tick_now() const { return host.tick_count ? host.tick_count() : steady_millis(); }
    static void sleep_until(Clock::time_point deadline) {
        if (Clock::now() < deadline) std::this_thread::sleep_until(deadline);
    }
    static uint32_t remaining_timeout(Clock::time_point deadline, uint32_t timeout) {
        if (timeout == 0xffffffffu) return timeout;
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        return left > 0 ? uint32_t(left > 0xffffffffll ? 0xffffffffu : left) : 0u;
    }
    std::string exe_path, cmdline;
    SteamClientRegistry steam_registry=steam_client_registry_config();
    std::map<uint32_t,bool> steam_registry_keys;
    uint32_t steam_registry_serial=0;
    std::map<uint32_t,uintptr_t> registry_handles;
    uint32_t registry_serial=0;
    std::map<uint32_t,bool> crypto_contexts;
    uint32_t crypto_serial=0;
    std::map<std::string, uint32_t> modules;   // lowercase module name -> guest base
    std::vector<uint32_t> atexit_fns;
    std::vector<uint8_t> iob;                  // 3 FILE structs, guest-visible
    uint32_t iob_addr = 0x7FFD0000;
    uint32_t initenv = 0, commode = 0, fmode = 0, mb_cur_max = 0, errno_ptr = 0;
    uint32_t startupinfo = 0, cmdline_ptr = 0, env_ptr = 0, env_w_ptr = 0, module_name_ptr = 0;
    uint32_t image_base = 0;
    std::map<std::string, std::string> guest_environment;
    std::map<std::string, std::string> guest_environment_names;
    struct ResourceRef { uint32_t module=0, data=0, size=0; };
    std::map<uint32_t,ResourceRef> resource_handles;
    uint32_t resource_serial=0;
    // The exe's implicit TLS directory (the runner copies it from its Image, so
    // each new thread gets its own block) and the TEB of the running thread,
    // mirroring Cpu.fs_base for host helpers that have no Cpu.
    Image::Tls exe_tls;
    uint32_t cur_teb = TEB_ADDR;
    // EncodePointer/DecodePointer (and the System variants): the security
    // cookie; VC/UCRT startup encodes every function pointer it stores and
    // decodes it on use, so the pair must be a real inverse or a decoded
    // pointer is garbage. The key is per-process ("random-looking" to the
    // guest) and the encoding is XOR, so decode(encode(p)) == p and
    // encode(NULL) == decode(NULL) == NULL hold exactly as the documented
    // pointer == NULL exactly as the documented invariant requires (a plain
    // XOR answers the cookie for NULL and breaks every "encoded pointer not
    // yet set == 0" check the CRT startup makes). A conditional XOR is still
    // a real inverse pair (Bugbot 4559749432).
    uint32_t pointer_cookie = 0x3FD2A71Eu ^ uint32_t(reinterpret_cast<uintptr_t>(this));
    uint32_t encode_ptr(uint32_t p) const { return p ? p ^ pointer_cookie : 0; }
    unsigned heap_serial = 0;
    std::map<uint32_t, uint32_t> heap_handles;  // handle -> arena base
    std::map<uint32_t, uint32_t> critical_section_debug;  // RTL_CRITICAL_SECTION -> debug record
    std::map<uint32_t, uint32_t> virtual_alloc; // guest addr -> size
    uint32_t virtual_top = 0x60000000u;  // above the DLL relocation arena (0x38000000-0x60000000)
    unsigned next_tls = 0, thread_serial = 0;
    uint64_t tls_used = 1;  // TlsAlloc/FlsAlloc slot bitmap; slot 0 reserved (see TlsAlloc)
    std::map<uint32_t, uint32_t> fls_callbacks;  // FLS index -> guest cleanup callback
    struct SrwObject { uint32_t writer = 0; std::map<uint32_t, uint32_t> readers; };
    std::map<uint32_t, SrwObject> srw_locks;  // guest lock address -> current owner(s)
    std::map<uint32_t, uint32_t> sections;  // handle -> size in bytes
    uint32_t section_serial = 0, section_top = 0x64000000u;
    // Thread bodies CreateThread recorded (handle, start, param); each body
    // runs at the guest's first blocking wait (see the CreateThread comment).
    // The handle rides along so the body's exit code can be recorded under the
    // handle GetExitCodeThread is called with.
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> pending_threads;
    // ExitThread ends only the calling thread (MSDN: "Ends the calling thread"),
    // so a body that calls it is unwound to run_pending_threads by a ThreadExit
    // exception and the guest code after the call never runs. Outside a body
    // (the main thread) ExitThread only records its code: the process keeps
    // going (Bugbot 4559267305). ExitProcess/do_exit ends the process.
    struct ThreadExit {};
    bool in_thread_body = false;
    uint32_t thread_exit_code = 0;
    std::map<uint32_t, uint32_t> thread_exit_codes;  // handle -> exit code
    std::map<uint32_t, uint32_t> thread_ids;         // handle -> thread identifier
    enum class SyncKind { Event, Mutex, Semaphore };
    struct SyncObject { SyncKind kind; std::u16string name; uint32_t refs = 0, owner = 0, recursion = 0, count = 0, maximum = 0; uintptr_t native_event = 0; bool manual = false, signaled = false, abandoned = false; };
    std::map<uint32_t, uint32_t> sync_handles;        // guest handle -> object id
    std::map<uint32_t, SyncObject> sync_objects;
    std::map<std::u16string, uint32_t> sync_names;    // names are case-sensitive (Win32)
    std::map<uint32_t, uint64_t> file_handles;         // guest handle -> Play host HANDLE
    std::map<int32_t, uint32_t> std_handles{{-10, Host::H_STDIN}, {-11, Host::H_STDOUT}, {-12, Host::H_STDERR}};
    std::map<uint32_t, uint64_t> find_handles;         // guest search handle -> native find HANDLE
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> com_apartments; // guest thread id -> model, init count
    struct GdiObject { uint32_t kind=0, width=0, height=0, bits=0, bpp=0, color=0; bool top_down=true, stock=false; uint32_t pitch=0; };
    struct GdiDc { uint32_t bitmap=0, brush=0, pen=0, font=0, bk_color=0x00ffffffu, text_color=0; int bk_mode=2; };
    std::map<uint32_t, GdiObject> gdi_objects;
    std::map<uint32_t, GdiDc> gdi_dcs;
    uint32_t gdi_serial=0;
    uint32_t file_serial = 0, find_serial = 0, sync_serial = 0, sync_handle_serial = 0;
    std::map<uint32_t, bool> mm_timers;
    uint32_t mm_timer_serial = 0;
    // Guest DLLs loaded by the process (lowercase module name -> Dll) and the
    // order they were mapped in (DllMain PROCESS_DETACH runs in reverse).
    // `dll_source` supplies DLL bytes by module name; empty = not found.
    std::map<std::string, Dll> dlls;
    std::vector<std::string> dll_order;
    DllSource dll_source;
    std::function<void(uint32_t, uint32_t)> invalidate_dbt;
    // DllMain absolute entry addresses by module name, run by the Cpu after
    // the thunk page is mapped; the loader records them, run_dll_mains
    // dispatches. (Image::entry is already absolute: base + entry RVA.)
    std::map<std::string, uint32_t> dll_entries;
    // The heap arena starts at 0x08000000: above the exe (0x400000) and its
    // stack, below the 0x10000000 base mingw/MSVC give every DLL by default —
    // a DLL at its preferred base and later heap growth must never fight over
    // the same pages (mem.map throws on overlap; Bugbot 4559749442). The
    // 128 MiB headroom is the heap's before it could reach the first DLL.
    Guest(Memory& m, Host& h, Registry& r) : mem(m), host(h), reg(r), heap(m, 0x08000000u) {}
    void register_all();
    // Module names are case-insensitive on Windows, and GetModuleHandleA
    // lowercases its lookup, so every insert goes through here: a mixed-case
    // executable name ("Proteus.exe") must still be found by the guest.
    void add_module(std::string name, uint32_t base) {
        for (auto& ch : name) ch = char(std::tolower((unsigned char)ch));
        modules[name] = base;
    }
    // --- guest-memory helpers ---
    std::string str(uint32_t p) const { return p ? mem.read_cstr(p) : std::string(); }
    std::u16string wstr(uint32_t p) const {
        std::u16string s;
        if (p) for (uint32_t i = 0;; ++i) { uint16_t w = uint16_t(mem.r(p + i * 2, 2)); if (!w) break; s.push_back(w); }
        return s;
    }
    uint32_t put_str(const std::string& s) {
        uint32_t p = heap.alloc(uint32_t(s.size() + 1));
        mem.write_bytes(p, s.c_str(), uint32_t(s.size() + 1));
        return p;
    }
    uint32_t put_wstr(const std::u16string& s) {
        uint32_t p = heap.alloc(uint32_t((s.size() + 1) * 2));
        for (size_t i = 0; i < s.size(); ++i) { mem.w8(p + uint32_t(i * 2), uint8_t(s[i])); mem.w8(p + uint32_t(i * 2 + 1), uint8_t(s[i] >> 8)); }
        mem.w8(p + uint32_t(s.size() * 2), 0); mem.w8(p + uint32_t(s.size() * 2 + 1), 0);
        return p;
    }
    void copy_out(uint32_t dst, const void* src, uint32_t n) { if (dst && n) mem.write_bytes(dst, src, n); }
    void copy_in(void* dst, uint32_t src, uint32_t n) { if (src && n) { auto b = mem.read_bytes(src, n); std::memcpy(dst, b.data(), n); } }
    uint32_t last_error() const { return mem.r32(cur_teb + 0x34); }
    void set_last_error(uint32_t v) { mem.w32(cur_teb + 0x34, v); }
    std::string env_key(std::string s) const { for(char& ch:s) ch=char(std::tolower((unsigned char)ch)); return s; }
    void rebuild_environment() {
        std::string a; std::u16string w;
        for(const auto& item:guest_environment) { const auto& name=guest_environment_names[item.first];a+=name+"="+item.second; a.push_back(0); for(unsigned char ch:name)w.push_back(ch);w.push_back(u'=');for(unsigned char ch:item.second)w.push_back(ch);w.push_back(0); }
        if(guest_environment.empty()){a.push_back(0);w.push_back(0);}a.push_back(0);w.push_back(0);
        env_ptr=heap.alloc(uint32_t(a.size()));mem.write_bytes(env_ptr,a.data(),uint32_t(a.size()));
        env_w_ptr=heap.alloc(uint32_t(w.size()*2));for(size_t i=0;i<w.size();++i)mem.w(env_w_ptr+uint32_t(i*2),w[i],2);
    }
    uint32_t add_sync_handle(uint32_t id) { uint32_t h=0x70000000u+(++sync_handle_serial);sync_handles[h]=id;++sync_objects[id].refs;return h; }
    std::u16string sync_name(uint32_t namep, bool wide) const {
        if (wide) return wstr(namep);
        std::string narrow = str(namep); std::u16string name;
        for (unsigned char ch : narrow) name.push_back(char16_t(ch));
        return name;
    }
    uint32_t create_sync(SyncKind kind, uint32_t namep, bool wide, bool manual, bool initial, int32_t count, int32_t maximum, uint32_t owner) {
        std::u16string name = sync_name(namep, wide); uint32_t id = 0;
        if (!name.empty()) { auto it = sync_names.find(name); if (it != sync_names.end()) { id = it->second; if (sync_objects[id].kind != kind) { set_last_error(6); return 0; } set_last_error(183); } }
        if (!id) { id = ++sync_serial; SyncObject o{}; o.kind=kind; o.name=name; o.manual=manual; o.signaled=initial; o.owner=owner; o.recursion=owner?1u:0u; o.count=uint32_t(count); o.maximum=uint32_t(maximum);
#if defined(_WIN32)
            if(kind==SyncKind::Event){HANDLE event=CreateEventExW(nullptr,nullptr,(manual?CREATE_EVENT_MANUAL_RESET:0)|(initial?CREATE_EVENT_INITIAL_SET:0),EVENT_ALL_ACCESS);if(!event){set_last_error(GetLastError());return 0;}o.native_event=reinterpret_cast<uintptr_t>(event);}
#endif
            sync_objects[id]=o; if (!name.empty()) sync_names[name]=id; set_last_error(0); }
        return add_sync_handle(id);
    }
    uint32_t open_sync(SyncKind kind, uint32_t namep, bool wide) { std::u16string name=sync_name(namep,wide);auto it=sync_names.find(name);if(it==sync_names.end()){set_last_error(2);return 0;}if(sync_objects[it->second].kind!=kind){set_last_error(6);return 0;}return add_sync_handle(it->second); }
    bool close_sync(uint32_t h) { auto it=sync_handles.find(h); if(it==sync_handles.end())return false; uint32_t id=it->second; sync_handles.erase(it); auto& o=sync_objects[id]; if(!--o.refs){if(!o.name.empty())sync_names.erase(o.name);
#if defined(_WIN32)
        if(o.native_event)CloseHandle(reinterpret_cast<HANDLE>(o.native_event));
#endif
        sync_objects.erase(id);} return true; }
    uint32_t add_file_handle(uint64_t native) { uint32_t h=0x50000000u+(++file_serial); file_handles[h]=native; return h; }
    bool call_file(Host::FileCall& call) { if(!host.file_call){call.error=120;return false;} host.file_call(call); return call.succeeded; }
    static std::string trim_profile(std::string s) {
        size_t a=s.find_first_not_of(" \t\r\n"), b=s.find_last_not_of(" \t\r\n");
        return a==std::string::npos?std::string():s.substr(a,b-a+1);
    }
    static std::string profile_value(const std::string& text,const std::string& section,const std::string& key,const std::string& fallback) {
        auto fold=[](std::string s){for(char& c:s)c=char(std::tolower((unsigned char)c));return s;};
        std::string current, want_section=fold(section), want_key=fold(key); size_t p=0;
        while(p<=text.size()) { size_t e=text.find_first_of("\r\n",p);if(e==std::string::npos)e=text.size();std::string line=trim_profile(text.substr(p,e-p));p=e+1;
            if(line.empty()||line[0]==';'||line[0]=='#')continue;
            if(line.front()=='['&&line.back()==']'){current=fold(trim_profile(line.substr(1,line.size()-2)));continue;}
            if(current!=want_section)continue;
            size_t eq=line.find('=');
            if(eq==std::string::npos||fold(trim_profile(line.substr(0,eq)))!=want_key)continue;
            std::string v=trim_profile(line.substr(eq+1));
            if(v.size()>=2&&((v.front()==char(34)&&v.back()==char(34))||(v.front()==char(39)&&v.back()==char(39))))v=v.substr(1,v.size()-2);
            return v;
        } return fallback;
    }
    bool profile_file(uint32_t pathp, bool wide, std::string& contents, bool write, uint32_t& error) {
        if(!pathp){error=87;return false;}
        Host::FileCall open{Host::FileCall::Kind::CreateW};if(wide)open.path_w=wstr(pathp);else{std::string path=str(pathp);open.path_w.assign(path.begin(),path.end());}
        open.args[0]=write?0x40000000u:0x80000000u;open.args[1]=3;open.args[2]=write?2u:3u;open.args[3]=0x80;
        if(!call_file(open)){error=open.error?open.error:120;return false;}
        uint64_t handle=open.value;bool ok=false;
        if(write){Host::FileCall q{Host::FileCall::Kind::Write};q.handle=handle;q.data.assign(contents.begin(),contents.end());ok=call_file(q)&&q.args[0]==q.data.size();if(!ok)error=q.error?q.error:5;}
        else {Host::FileCall size{Host::FileCall::Kind::Size};size.handle=handle;if(call_file(size)&&size.value<=16u*1024u*1024u){Host::FileCall read{Host::FileCall::Kind::Read};read.handle=handle;read.args[0]=uint32_t(size.value);ok=call_file(read)&&read.args[1]==size.value;if(ok)contents.assign(read.data.begin(),read.data.end());else error=read.error?read.error:5;}else error=size.error?size.error:223;}
        Host::FileCall close{Host::FileCall::Kind::Close};close.handle=handle;if(!call_file(close)&&ok){ok=false;error=close.error?close.error:5;}
        return ok;
    }
    // WaitForSingleObject runs pending guest bodies first. Windows event objects
    // also have a native handle so asynchronous XAudio callbacks can signal a wait;
    // mutexes and semaphores remain synchronous guest-state checks.
    uint32_t sync_wait(Cpu& c, uint32_t h, uint32_t timeout) {
        auto it=sync_handles.find(h); if(it==sync_handles.end())return 0xffffffffu; auto& o=sync_objects[it->second];
        uint32_t tid=c.fs_base?c.mem.r32(c.fs_base+0x24):1u;
#if defined(_WIN32)
        if(o.kind==SyncKind::Event&&o.native_event){DWORD waited=WaitForSingleObject(reinterpret_cast<HANDLE>(o.native_event),timeout);if(waited==WAIT_OBJECT_0){o.signaled=o.manual;return 0;}if(waited==WAIT_TIMEOUT)return 258u;return 0xffffffffu;}
#endif
        if(o.kind==SyncKind::Event&&o.signaled){if(!o.manual)o.signaled=false;return 0;}
        if(o.kind==SyncKind::Semaphore&&o.count){--o.count;return 0;}
        if(o.kind==SyncKind::Mutex&&(!o.owner||o.owner==tid)){uint32_t r=o.abandoned?0x80u:0u;o.abandoned=false;o.owner=tid;++o.recursion;return r;}
        if(timeout==0xffffffffu)throw Fault(FaultKind::Unsupported,c.eip,c.eip,"WaitForSingleObject: unsignaled infinite wait on handle "+std::to_string(h));
        if(timeout) std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
        return 258u;
    }
    uint32_t sync_wait_multiple(Cpu& c, uint32_t count, uint32_t handles, bool wait_all, uint32_t timeout, bool allow_empty=false) {
        if((!count&&!allow_empty)||count>64||(count&&!handles)){set_last_error(87);return 0xffffffffu;}
        Clock::time_point deadline{};
        if(timeout!=0xffffffffu) deadline=Clock::now()+std::chrono::milliseconds(timeout);
        run_pending_threads(c);
        std::vector<uint32_t> values(count); uint32_t ready_count=0, ready_index=0;
        for(uint32_t i=0;i<count;++i){uint32_t h=mem.r32(handles+i*4);values[i]=h;
            if(thread_ids.count(h)||thread_exit_codes.count(h)){if(!ready_count)ready_index=i;++ready_count;continue;}
            auto it=sync_handles.find(h);if(it==sync_handles.end()){set_last_error(6);return 0xffffffffu;}
            const auto& o=sync_objects[it->second];uint32_t tid=c.fs_base?mem.r32(c.fs_base+0x24):1u;
            bool ready=(o.kind==SyncKind::Event&&o.signaled)||(o.kind==SyncKind::Semaphore&&o.count)||(o.kind==SyncKind::Mutex&&(!o.owner||o.owner==tid));
            if(ready){if(!ready_count)ready_index=i;++ready_count;}
        }
        if(wait_all?ready_count==count:ready_count!=0){
            if(wait_all){for(uint32_t h:values)if(sync_handles.count(h)){uint32_t r=sync_wait(c,h,0);if(r==0xffffffffu)return r;}return 0;}
            uint32_t h=values[ready_index];if(sync_handles.count(h)){uint32_t r=sync_wait(c,h,0);if(r==0xffffffffu)return r;}
            return ready_index;
        }
        if(timeout==0xffffffffu)throw Fault(FaultKind::Unsupported,c.eip,c.eip,"WaitForMultipleObjects: unsignaled infinite wait");
        sleep_until(deadline);
        return 258u;
    }
    void abandon_mutexes(uint32_t tid) { for(auto& kv:sync_objects)if(kv.second.kind==SyncKind::Mutex&&kv.second.owner==tid){kv.second.owner=kv.second.recursion=0;kv.second.abandoned=true;} }
    // A FILE* is one of our three iob entries; anything else is a bad handle.
    // The index is the C stream number: 0 stdin, 1 stdout, 2 stderr.
    int file_index(uint32_t f) const {
        if (f >= iob_addr && f < iob_addr + 3 * 32 && (f - iob_addr) % 32 == 0) return int((f - iob_addr) / 32);
        return -1;
    }
    uint32_t file_ptr(int i) const { return iob_addr + uint32_t(i) * 32; }
    // `which` is a C stream number (0/1/2), the same one file_index returns, so
    // stderr reaches host.err on both the FILE* and the WriteFile path.
    void emit(int which, const std::string& s) {
        (which == 2 ? host.err : host.out) += s;
        if (host.tee) host.tee(s);
    }
    void write_file(int which, const std::string& s) { emit(which, s); }
    // printf-family core. `next` yields the next 32-bit argument slot: the guest
    // stack for printf/fprintf/sprintf, or a walk over the guest va_list for the
    // v* forms (on i386 a va_list is a char* to the first vararg). The i386
    // calling convention aligns every argument to 4 bytes only: a double is two
    // consecutive slots wherever they fall, with no padding before it (mingw's
    // va_arg and msvcrt's own printf both read it that way; measured, not
    // assumed). A `long long`/`__int64` is likewise two slots, low dword first.
    struct FmtArgs { std::function<uint32_t()> next; };
    std::string format(FmtArgs a, const std::string& fmt);
    // Copy a formatted result into guest memory. A negative `cap` means
    // unbounded (sprintf); 0 writes nothing (snprintf with a zero size). With
    // append_nul the result is NUL-terminated inside cap. Returns the full
    // length the format would have produced, as C99 snprintf does.
    int store(const std::string& s, uint32_t out_ptr, int cap, bool append_nul);
    std::string format_stack(Cpu& c, const std::string& fmt, unsigned first_arg) {
        unsigned ai = first_arg;
        return format({[&c, &ai]() { return c.arg(ai++); }}, fmt);
    }
    std::string format_va(const std::string& fmt, uint32_t& ap) {
        return format({[this, &ap]() { uint32_t v = mem.r32(ap); ap += 4; return v; }}, fmt);
    }
    // sprintf/_snprintf/snprintf take their varargs on the stack, not a va_list:
    // only the v* forms walk a pointer.
    int format_stack_to(Cpu& c, uint32_t fmt_ptr, unsigned first_arg, uint32_t out_ptr, int cap, bool append_nul) {
        return store(format_stack(c, str(fmt_ptr), first_arg), out_ptr, cap, append_nul);
    }
    int format_va_to(uint32_t fmt_ptr, uint32_t argp, uint32_t out_ptr, int cap, bool append_nul) {
        return store(format_va(str(fmt_ptr), argp), out_ptr, cap, append_nul);
    }
    // SYSTEMTIME is eight WORDs (year, month, dayOfWeek, day, hour, minute,
    // second, milliseconds), 16 bytes total, so every field is a 2-byte store.
    void fill_system_time(uint32_t p) {
        if (!p) return;
        static const uint16_t f[8] = {2026, 11, 0, 9, 12, 0, 0, 0};
        for (unsigned i = 0; i < 8; ++i) mem.w(p + i * 2, f[i], 2);
    }
    void run_atexit(Cpu& c);
    void do_exit(Cpu& c, uint32_t code);
    // Runs every thread body CreateThread recorded, in creation order, before
    // the blocking wait (or yield) that triggered it proceeds. Each body runs on
    // its own TEB (a page of its own, fs_base pointing at it), so the SEH chain,
    // stack bounds, thread id, LastError and TlsSlots are that thread's, as
    // Windows gives every thread its own TEB. The exe's implicit TLS is per
    // thread too: the TEB's ThreadLocalStoragePointer (fs:[0x2C]) addresses a
    // block copied from the image's TLS template, so __declspec(thread) data is
    // separate per thread (MSDN "Using Thread Local Storage"). TLS callbacks run
    // with DLL_THREAD_ATTACH before the body and DLL_THREAD_DETACH after it, on
    // the thread's own TEB; the TEB and block are freed when the thread ends.
    // The main thread's TEB is restored afterwards.
    static constexpr uint64_t kThreadBudget = 100ull * 1000 * 1000;  // instructions per thread body run
    static constexpr uint32_t kThreadRegionBase = 0x7E000000u;      // thread TEBs: above the DLL and section arenas
    struct ThreadTeb { uint32_t base, size; };
    ThreadTeb map_thread_teb(Cpu& c, uint32_t tid) {
        uint32_t tls_bytes = exe_tls.present ? 16 + (exe_tls.end - exe_tls.start) + exe_tls.zero_fill : 0;
        uint32_t size = Memory::PAGE + (tls_bytes + Memory::PAGE - 1) / Memory::PAGE * Memory::PAGE;
        uint32_t base = kThreadRegionBase;
        while (!c.mem.free_range(base, size)) {
            base += 0x10000u;
            if (base >= 0x7FF00000u) throw std::runtime_error("no address space for a thread TEB");
        }
        c.mem.map(base, size, PROT_R | PROT_W);
        uint32_t main = cur_teb;
        c.mem.w32(base + 0x00, 0xFFFFFFFFu);                    // ExceptionList: empty chain
        c.mem.w32(base + 0x04, c.mem.r32(main + 0x04));         // StackBase: the process stack
        c.mem.w32(base + 0x08, c.mem.r32(main + 0x08));         // StackLimit
        c.mem.w32(base + 0x18, base);                           // Self
        c.mem.w32(base + 0x20, c.mem.r32(main + 0x20));         // ClientId.UniqueProcess
        c.mem.w32(base + 0x24, tid);                            // ClientId.UniqueThread
        c.mem.w32(base + 0x30, c.mem.r32(main + 0x30));         // ProcessEnvironmentBlock
        // LastError (0x34) and TlsSlots (0xE10..) start zeroed: this thread's own.
        if (exe_tls.present) {
            uint32_t slot = base + Memory::PAGE, block = slot + 16;
            uint32_t raw = exe_tls.end - exe_tls.start;
            c.mem.w32(slot, block);                             // ThreadLocalStoragePointer[index 0]
            c.mem.w32(base + 0x2C, slot);
            for (uint32_t i = 0; i < raw; ++i) c.mem.w8(block + i, c.mem.r8(exe_tls.start + i));  // template copy; the rest is zero
        }
        return ThreadTeb{base, size};
    }
    // The exe's TLS callbacks for `reason` (DLL_THREAD_ATTACH 2 / DLL_THREAD_DETACH 3),
    // called as (image base, reason, NULL) like the loader does.
    void tls_callbacks_call(Cpu& c, uint32_t reason) {
        if (!exe_tls.present) return;
        for (uint32_t p = exe_tls.callbacks; p; p += 4) {
            uint32_t fn = c.mem.r32(p);
            if (!fn) break;
            c.call(fn, {image_base, reason, 0});
        }
    }
    // The worker's context lives exactly as long as this object: the worker's TEB
    // is the current thread, its in-body flag holds and its instruction budget
    // applies. The destructor restores the main thread's state, so every exit
    // path (normal return, ExitThread unwind, host exception) restores it once and
    // no worker TEB is ever left active.
    struct ThreadScope {
        Guest& g; Cpu& c;
        uint32_t main_teb, main_fs; uint64_t main_limit; bool main_in_body;
        ThreadScope(Guest& gg, Cpu& cc, uint32_t teb, uint64_t budget)
            : g(gg), c(cc), main_teb(gg.cur_teb), main_fs(cc.fs_base), main_limit(cc.limit), main_in_body(gg.in_thread_body) {
            g.cur_teb = teb; c.fs_base = teb; g.in_thread_body = true;
            // Each body gets `budget` instructions per run. The interpreter is
            // single-threaded, so a body that never returns (a worker that waits on
            // the main thread) must fail the run by name instead of hanging the host.
            if (c.executed + budget < c.limit) c.limit = c.executed + budget;
        }
        ~ThreadScope() { g.cur_teb = main_teb; c.fs_base = main_fs; c.limit = main_limit; g.in_thread_body = main_in_body; }
    };
    // Order per thread: DLL_THREAD_ATTACH callbacks, the body, DLL_THREAD_DETACH
    // callbacks (all on the thread's own TEB), then the TEB and TLS block are
    // unmapped, and the scope restores the main thread. ExitThread in the body
    // ends the thread; ExitThread in a detach callback ends only that callback:
    // the thread is already exiting and keeps its body's exit code.
    void run_pending_threads(Cpu& c) {
        while (!pending_threads.empty()) {
            auto t = pending_threads.front(); pending_threads.erase(pending_threads.begin());
            auto id = thread_ids.find(std::get<0>(t));
            ThreadTeb th = map_thread_teb(c, id != thread_ids.end() ? id->second : 0);
            ThreadScope scope(*this, c, th.base, kThreadBudget);
            uint32_t saved_regs[8]; std::memcpy(saved_regs, c.reg, sizeof saved_regs);
            uint32_t saved_eip = c.eip, code = 0;
            try {
                tls_callbacks_call(c, 2);  // DLL_THREAD_ATTACH
                code = c.call(std::get<1>(t), {std::get<2>(t)});
            }
            catch (const ThreadExit&) {
                // Drop the body's frames: restore the scheduler's registers so
                // the wait that started the body returns to its caller. The
                // body's exit code is what ExitThread was given (MSDN ExitThread).
                std::memcpy(c.reg, saved_regs, sizeof saved_regs); c.eip = saved_eip;
                code = thread_exit_code;
            }
            catch (...) {
                if (scope.main_limit > c.limit && c.executed > c.limit) {
                    emit(2, "x86: thread body did not return within its instruction budget\n");
                    throw Fault(FaultKind::InstructionLimit, c.eip, std::get<1>(t), "thread body exceeded its instruction budget");
                }
                throw;
            }
            // The exit code is the body's return value (EAX) or the ExitThread code:
            // a returned body is finished too, so GetExitCodeThread answers it, not
            // STILL_ACTIVE (MSDN GetExitCodeThread). Recorded under the HANDLE.
            thread_exit_codes[std::get<0>(t)] = code;
            // DLL_THREAD_DETACH on the thread's own TEB, before its memory goes.
            std::memcpy(saved_regs, c.reg, sizeof saved_regs); saved_eip = c.eip;
            try { tls_callbacks_call(c, 3); }
            catch (const ThreadExit&) { std::memcpy(c.reg, saved_regs, sizeof saved_regs); c.eip = saved_eip; }
            abandon_mutexes(id != thread_ids.end() ? id->second : 0);
            c.mem.unmap(th.base, th.size);
        }
    }
    // Guest DLL loading (stage 3). `run_dll_mains` runs each loaded DLL's
    // DllMain(DLL_PROCESS_ATTACH) in load order, like the Windows loader, and
    // is called by the runner after init_process. A DLL without DllMain is
    // skipped; a DllMain that faults propagates (never silently ignored).
    void run_dll_mains(Cpu& c, uint32_t reason) {
        (void)reason;  // only DLL_PROCESS_ATTACH (1) is dispatched today
        for (const auto& name : dll_order) {
            if (attached[name]) continue;
            attached[name] = true;
            auto e = dll_entries.find(name);
            if (e == dll_entries.end()) continue;
            uint32_t base = dlls[name].image.base;
            c.call(e->second, {base, static_cast<uint32_t>(1 /*DLL_PROCESS_ATTACH*/), 0});
        }
    }
    // LoadLibrary core: maps `file` as a guest DLL (or bumps the refcount of a
    // loaded one), binds imports recursively, records DllMain, and returns the
    // guest base, or 0 with ERROR_MOD_NOT_FOUND / ERROR_BAD_EXE_FORMAT. DllMain
    // runs here when the CPU is live (a LoadLibrary from guest code); the
    // loader path passes cpu == nullptr and defers to run_dll_mains.
    // Modules the OS surface itself serves (kernel32, msvcrt, ntdll) have no
    // image: LoadLibrary answers a pseudo-handle and GetProcAddress on it
    // binds the same host thunk the import binder would have, as Windows
    // answers for its own modules.
    std::map<std::string, uint32_t> os_modules;  // lowercase dll -> pseudo base
    std::map<std::pair<std::string, std::string>, uint32_t> os_export_addrs;
    static constexpr uint32_t kOsModuleBase = 0x68000000u, kOsModuleStride = 0x10000u;
    // kernelbase exports nothing here (the CRT probes it and falls back when
    // the lookup misses), but LoadLibrary must succeed, as on Windows.
    bool registry_serves(const std::string& dll) const {
        if (dll == "kernelbase.dll") return true;
        for (auto& kv : reg.map) if (kv.first.first == dll) return true;
        return false;
    }
    // Map a read-only PE32 image at each OS pseudo-handle. Its exports mirror
    // the route registry, so guest export walkers and GetProcAddress agree.
    void map_os_module_image(const std::string& dll, uint32_t base, ThunkTable& table) {
        struct Export { std::string name; uint32_t address; };
        std::vector<Export> exports;
        for (const auto& kv : reg.map) if (kv.first.first == dll)
            exports.push_back({kv.second.name, os_export_addr(table, dll, kv.first.second)});
        for (const auto& kv : reg.data) if (kv.first.first == dll)
            exports.push_back({kv.first.second, kv.second});
        std::sort(exports.begin(), exports.end(), [](const Export& a, const Export& b) { return a.name < b.name; });
        constexpr uint32_t image_size = kOsModuleStride, nt = 0x80, opt = nt + 24;
        constexpr uint32_t exp = 0x200, funcs = 0x240;
        uint32_t names = funcs + uint32_t(exports.size()) * 4;
        uint32_t ords = names + uint32_t(exports.size()) * 4;
        uint32_t strings = ords + uint32_t(exports.size()) * 2;
        mem.map(base, image_size, PROT_R | PROT_W);
        mem.w(base, 0x5A4D, 2); mem.w32(base + 0x3c, nt);
        mem.w32(base + nt, 0x00004550);
        mem.w(base + nt + 4, 0x014c, 2); mem.w(base + nt + 6, 0, 2);
        mem.w(base + nt + 20, 0x00e0, 2); mem.w(base + nt + 22, 0x2102, 2);
        mem.w(base + opt, 0x010b, 2); mem.w32(base + opt + 28, base);
        mem.w32(base + opt + 32, 0x1000); mem.w32(base + opt + 36, 0x200);
        mem.w32(base + opt + 56, image_size); mem.w32(base + opt + 60, 0x200);
        mem.w32(base + opt + 92, 16); mem.w32(base + opt + 96, exp);
        // IMAGE_EXPORT_DIRECTORY; arrays contain all route names in sorted order.
        mem.w32(base + exp + 12, strings);
        mem.w32(base + exp + 16, 1);
        mem.w32(base + exp + 20, uint32_t(exports.size()));
        mem.w32(base + exp + 24, uint32_t(exports.size()));
        mem.w32(base + exp + 28, funcs); mem.w32(base + exp + 32, names); mem.w32(base + exp + 36, ords);
        uint32_t cursor = strings;
        mem.write_bytes(base + cursor, dll.c_str(), uint32_t(dll.size() + 1));
        cursor += uint32_t(dll.size() + 1);
        for (uint32_t i = 0; i < exports.size(); ++i) {
            mem.w32(base + funcs + i * 4, exports[i].address - base);
            mem.w32(base + names + i * 4, cursor);
            mem.w(base + ords + i * 2, i, 2);
            mem.write_bytes(base + cursor, exports[i].name.c_str(), uint32_t(exports[i].name.size() + 1));
            cursor += uint32_t(exports[i].name.size() + 1);
        }
        mem.w32(base + opt + 100, cursor - exp);
        mem.protect(base, image_size, PROT_R);
    }
    // Modules every process has from its start, served without an image.
    bool os_served(const std::string& dll) const {
        return dll == "kernel32.dll" || dll == "ntdll.dll" || dll == "msvcrt.dll" || registry_serves(dll);
    }
    uint32_t os_module_handle(const std::string& dll, ThunkTable& table) {
        auto it = os_modules.find(dll);
        if (it != os_modules.end()) return it->second;
        uint32_t h = kOsModuleBase + uint32_t(os_modules.size()) * kOsModuleStride;
        map_os_module_image(dll, h, table);
        os_modules[dll] = h;
        add_module(dll, h);  // GetModuleHandleA("ntdll.dll") answers it too
        return h;
    }
    // The GetModuleHandleA/W/ExW lookup (MSDN GetModuleHandleA): case-insensitive;
    // a name without an extension gets ".dll" appended, and a trailing "." means
    // the name has no extension. It answers a loaded image, the main module, or
    // an OS-served module (kernel32/ntdll/msvcrt/kernelbase are in the process
    // from its start, so they answer without LoadLibrary, with the same stable
    // handle LoadLibrary returns). A miss is NULL with ERROR_MOD_NOT_FOUND.
    // The one name rule GetModuleHandle and LoadLibrary share (module_key).
    static std::string module_key(std::string n) {
        size_t sep = n.find_last_of("\\/");  // a path names its file (host and guest modules alike)
        if (sep != std::string::npos) n = n.substr(sep + 1);
        for (auto& ch : n) ch = char(std::tolower((unsigned char)ch));
        if (!n.empty() && n.back() == '.') n.pop_back();
        else if (n.find('.') == std::string::npos) n += ".dll";
        return n;
    }
    uint32_t module_by_name(const std::string& name, ThunkTable& table) {
        std::string n = module_key(name);
        auto it = modules.find(n);
        if (it != modules.end()) return it->second;
        auto d = dlls.find(n);
        if (d != dlls.end()) return d->second.image.base;
        if (os_served(n)) return os_module_handle(n, table);
        set_last_error(126);  // ERROR_MOD_NOT_FOUND
        return 0;
    }
    // Resolves an export of an OS-served module (kernel32/msvcrt/ntdll) that a
    // guest DLL's forwarding export names ("ntdll.memcpy"): a host thunk, or a
    // registered data export's address. The forwarder's module part carries no
    // ".dll" (Windows forward strings are "ntdll.memcpy", "NTDLL.memcpy"), so
    // the name is lowercased and suffixed before the registry lookup — the
    // registry's keys are lowercase full module names. An ordinal forward
    // ("ntdll.#123") is answered 0: the surface registers no ordinals, and
    // guessing one would bind a different function silently. Returns 0 when
    // the surface has none (the IAT slot stays an unresolved stub that faults
    // by name); resolution stays one hop, so no chain can recurse here.
    uint32_t os_export_addr(ThunkTable& table, const std::string& dll, const std::string& sym) {
        std::string l = dll; for (auto& ch : l) ch = char(std::tolower((unsigned char)ch));
        std::string s = sym; for (auto& ch : s) ch = char(std::tolower((unsigned char)ch));
        if (l.rfind(".dll") != l.size() - 4) l += ".dll";
        if (s.rfind("#", 0) == 0) return 0;  // ordinal forward: no ordinal surface
        auto it = reg.map.find({l, s});
        if (it != reg.map.end()) {
            auto key = std::make_pair(l, s);
            auto cached = os_export_addrs.find(key);
            if (cached != os_export_addrs.end()) return cached->second;
            uint32_t address = table.add(it->second);
            os_export_addrs[key] = address;
            return address;
        }
        auto dt = reg.data.find({l, s});
        if (dt != reg.data.end()) return dt->second;
        return 0;
    }
    uint32_t load_guest_dll(Cpu* cpu, const std::string& requested, ThunkTable& table) {
        std::string lower = module_key(requested);
        if (host.route_trace) host.route_trace("LoadLibrary " + lower);
        auto it = dlls.find(lower);
        if (it != dlls.end()) { it->second.refs++; return it->second.image.base; }
        if (os_served(lower)) return os_module_handle(lower, table);
        if (!dll_source) { set_last_error(126); return 0; }  // ERROR_MOD_NOT_FOUND
        std::vector<uint8_t> bytes = dll_source(lower);
        if (bytes.empty()) { set_last_error(126); return 0; }
        try {
            load_dll(mem, table, reg, bytes, dll_source, dlls, 0, &dll_order,
                     [this, &table](const std::string& dll, const std::string& sym) {
                         return os_export_addr(table, dll, sym);
                     },
                     lower);
        } catch (const std::exception&) {
            // A malformed image (garbage file, PE64) is a failed load, not a
            // crash: LoadLibrary answers 0 with ERROR_BAD_EXE_FORMAT (193),
            // as LoadLibraryA documents (Bugbot 4559267311).
            set_last_error(193);
            return 0;
        }
        // load_dll reserved and completed the slot in `dlls` itself (cycles
        // reuse it): read the base from the table instead of copying the Dll.
        uint32_t base = dlls[lower].image.base;
        dll_order.push_back(lower);
        add_module(lower, base);  // GetModuleHandleW must find LoadLibrary'd DLLs
        // AddressOfEntryPoint == 0 means no entry point (a resource-only or
        // data DLL): `entry` is absolute (base + RVA) and always nonzero, so
        // the decision tests the RVA (Bugbot 4559749424).
        if (dlls[lower].image.entry_rva) dll_entries[lower] = dlls[lower].image.entry;  // absolute
        // DllMain(DLL_PROCESS_ATTACH) for every module this call newly mapped,
        // dependencies first (load_dll pushed them onto dll_order in that
        // order), then the requested module — as the Windows loader attaches
        // each module as it maps it (Bugbot 4559267301).
        if (cpu) run_new_dll_mains(*cpu);
        return base;
    }
    uint32_t resolve_delay_loaded(Cpu& c) {
        const uint32_t base=c.arg(0), descriptor=c.arg(1), thunk=c.arg(4);
        const Image* owner=nullptr;
        for(const auto& entry:dlls)if(entry.second.image.base==base){owner=&entry.second.image;break;}
        auto inside=[&](uint32_t address,uint32_t size) {
            return owner&&address>=owner->base&&size<=owner->size-(address-owner->base);
        };
        if(!inside(descriptor,32)||!inside(thunk,4))return 0;
        const uint32_t name_rva=mem.r32(descriptor+4),iat_rva=mem.r32(descriptor+12),int_rva=mem.r32(descriptor+16);
        if(!inside(base+name_rva,1)||!iat_rva||!int_rva||thunk<base+iat_rva)return 0;
        const uint32_t index=(thunk-(base+iat_rva))/4;
        const uint32_t entry=base+int_rva+index*4;
        if(!inside(entry,4))return 0;
        const uint32_t imported=mem.r32(entry);
        const std::string delayed_name=(imported&0x80000000u)?"#"+std::to_string(imported&0xffffu):str(base+imported+2);
        if(host.route_trace)host.route_trace("ResolveDelayLoadedAPI module="+str(base+name_rva)+"!"+delayed_name);
        uint32_t module=load_guest_dll(&c,str(base+name_rva),c.thunks);
        if(!module)return 0;
        return get_proc_address(module,imported&0x80000000u?imported&0xffffu:base+imported+2,c.thunks);
    }
    // Runs DllMain(DLL_PROCESS_ATTACH) for every module of `dll_order` that has
    // an entry and has not been attached yet, in load order (dependencies
    // first). `attached` marks the dispatch so a later LoadLibrary of a
    // dependency does not re-attach it (a module attaches exactly once per
    // process, as the Windows loader guarantees).
    std::map<std::string, bool> attached;
    void run_new_dll_mains(Cpu& c) {
        for (const auto& name : dll_order) {
            if (attached[name]) continue;
            attached[name] = true;
            auto e = dll_entries.find(name);
            if (e == dll_entries.end()) continue;
            uint32_t base = dlls[name].image.base;
            c.call(e->second, {base, static_cast<uint32_t>(1 /*DLL_PROCESS_ATTACH*/), 0});
        }
    }
    bool free_library(Cpu& c, uint32_t h) {
        if (h == image_base) return true;
        for (auto it = dlls.begin(); it != dlls.end(); ++it) if (it->second.image.base == h) {
            if (it->second.refs > 1) { --it->second.refs; return true; }
            uint32_t base = it->second.image.base, size = it->second.image.size;
            auto entry = dll_entries.find(it->first);
            if (entry != dll_entries.end()) c.call(entry->second, {base, static_cast<uint32_t>(0 /*DLL_PROCESS_DETACH*/), 0});
            if (invalidate_dbt) invalidate_dbt(base, size);
            mem.unmap(base, size);
            for (auto module = modules.begin(); module != modules.end();) {
                if (module->second == base) module = modules.erase(module); else ++module;
            }
            dll_entries.erase(it->first);
            attached.erase(it->first);
            dll_order.erase(std::remove(dll_order.begin(), dll_order.end(), it->first), dll_order.end());
            dlls.erase(it);
            return true;
        }
        for (const auto& m : modules) if (m.second == h) return true;
        set_last_error(126);
        return false;
    }
    // GetProcAddress core: `name_ptr` is a string pointer or an ordinal in the
    // MAKEINTRESOURCE form (a value below 65536 is an ordinal, as Windows
    // answers). Returns the guest address, or 0 with ERROR_PROC_NOT_FOUND.
    uint32_t get_proc_address(uint32_t module, uint32_t name_ptr, ThunkTable& table) {
        const std::string requested = name_ptr < 0x10000u ? "#" + std::to_string(name_ptr) : str(name_ptr);
        auto trace = [this, &requested](const std::string& dll, uint32_t address) {
            if (host.route_trace) host.route_trace("GetProcAddress " + dll + "!" + requested +
                " -> " + (address ? "resolved" : "missing"));
        };
        for (auto& kv : dlls) {
            if (kv.second.image.base != module) continue;
            uint32_t a = 0;
            if (name_ptr < 0x10000u) {  // ordinal form: the export address table by index
                // An ordinal's EAT RVA can be a forwarder string (inside the
                // export directory range): resolve it like a name does, never
                // rebase ASCII (Bugbot 4559267308).
                auto fo = kv.second.image.forwarded_by_ord.find(name_ptr);
                if (fo != kv.second.image.forwarded_by_ord.end()) {
                    size_t dot = fo->second.find('.');
                    if (dot != std::string::npos) {
                        std::string target = forward_module_key(fo->second.substr(0, dot)), tsym = fo->second.substr(dot + 1);
                        auto m = dlls.find(target);
                        if (m != dlls.end()) a = dll_export_addr(m->second, tsym, dlls,
                                                [this, &table](const std::string& dll, const std::string& s) {
                                                    return os_export_addr(table, dll, s);
                                                });
                        else a = os_export_addr(table, target, tsym);
                    }
                } else for (auto& o : kv.second.image.ordinals)
                    if (o.first == name_ptr) { a = kv.second.image.base + o.second; break; }
            } else {
                std::string sym = str(name_ptr);
                for (auto& ch : sym) ch = char(std::tolower((unsigned char)ch));
                a = dll_export_addr(kv.second, sym, dlls,
                                    [this, &table](const std::string& dll, const std::string& s) {
                                        return os_export_addr(table, dll, s);
                                    });
            }
            if (!a) set_last_error(127);  // ERROR_PROC_NOT_FOUND
            trace(kv.first, a);
            return a;
        }
        // An OS-served module: the export is a host thunk, added on demand —
        // the same stub the import binder installs for it. A data export
        // (msvcrt's __initenv and friends) answers the variable's address.
        for (auto& kv : os_modules) {
            if (kv.second != module) continue;
            if (name_ptr < 0x10000u) {
                auto ordinal = reg.ordinals.find({kv.first, uint16_t(name_ptr)});
                if (ordinal != reg.ordinals.end()) {
                    uint32_t address = os_export_addr(table, kv.first, ordinal->second);
                    trace(kv.first, address); return address;
                }
            } else {
                std::string sym = Registry::key_sym(str(name_ptr));
                auto it = reg.map.find({kv.first, sym});
                if (it != reg.map.end()) {
                    uint32_t address = os_export_addr(table, kv.first, sym);
                    trace(kv.first, address); return address;
                }
                auto dt = reg.data.find({kv.first, sym});
                if (dt != reg.data.end()) { trace(kv.first, dt->second); return dt->second; }
                // Also answer the undecorated stdcall name for a decorated
                // registry entry (a game may ask either form).
                std::string dec = sym + "@";
                for (auto& e : reg.map)
                    if (e.first.first == kv.first && e.first.second.rfind(dec, 0) == 0) {
                        uint32_t address = os_export_addr(table, kv.first, e.first.second);
                        trace(kv.first, address); return address;
                    }
            }
            set_last_error(127);  // ERROR_PROC_NOT_FOUND
            return 0;
        }
        set_last_error(126);  // ERROR_MOD_NOT_FOUND: not a module we track
        return 0;
    }
};

// ---- number formatting shared by the printf family ----
namespace guest_fmt {
inline std::string pad(const std::string& body, int width, bool left, char fill) {
    if (width <= int(body.size())) return body;
    std::string p(size_t(width - int(body.size())), fill);
    return left ? body + p : p + body;
}
inline std::string int_to(uint64_t v, int base, bool upper, bool neg) {
    const char* d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string s;
    do { s.push_back(d[v % uint64_t(base)]); v /= uint64_t(base); } while (v);
    if (neg) s.push_back('-');
    return std::string(s.rbegin(), s.rend());
}
inline std::string uint_to(uint64_t v, int base, bool upper) {
    const char* d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string s;
    do { s.push_back(d[v % uint64_t(base)]); v /= uint64_t(base); } while (v);
    return std::string(s.rbegin(), s.rend());
}
}

inline std::string Guest::format(FmtArgs a, const std::string& fmt) {
    auto& next = a.next;
    std::string out;
    size_t i = 0;
    while (i < fmt.size()) {
        char ch = fmt[i++];
        if (ch != '%') { out.push_back(ch); continue; }
        if (i < fmt.size() && fmt[i] == '%') { out.push_back('%'); ++i; continue; }
        bool left = false, plus = false, space = false, alt = false, zero = false;
        for (;;) {
            if (i >= fmt.size()) break;
            char f = fmt[i];
            if (f == '-') left = true; else if (f == '+') plus = true; else if (f == ' ') space = true;
            else if (f == '#') alt = true; else if (f == '0') zero = true; else break;
            ++i;
        }
        int width = 0;
        if (i < fmt.size() && fmt[i] == '*') { width = int(next()); if (width < 0) { left = true; width = -width; } ++i; }
        else while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') width = width * 10 + (fmt[i++] - '0');
        int prec = -1;
        if (i < fmt.size() && fmt[i] == '.') {
            ++i; prec = 0;
            if (i < fmt.size() && fmt[i] == '*') { prec = int(next()); ++i; if (prec < 0) prec = -1; }
            else while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') prec = prec * 10 + (fmt[i++] - '0');
        }
        enum { LEN_NONE, LEN_H, LEN_HH, LEN_L, LEN_LL, LEN_LDBL, LEN_Z, LEN_I64 } len = LEN_NONE;
        if (i < fmt.size()) {
            char l = fmt[i];
            if (l == 'h') { ++i; if (i < fmt.size() && fmt[i] == 'h') { ++i; len = LEN_HH; } else len = LEN_H; }
            else if (l == 'l') { ++i; if (i < fmt.size() && fmt[i] == 'l') { ++i; len = LEN_LL; } else len = LEN_L; }
            else if (l == 'L') { ++i; len = LEN_LDBL; }
            else if (l == 'z') { ++i; len = LEN_Z; }
            else if (l == 'I') {
                ++i;
                if (i + 1 < fmt.size() && fmt[i] == '6' && fmt[i + 1] == '4') { i += 2; len = LEN_I64; }
                else if (i + 1 < fmt.size() && fmt[i] == '3' && fmt[i + 1] == '2') { i += 2; len = LEN_NONE; }
                else if (i < fmt.size() && fmt[i] == 'd') { len = LEN_Z; }  // %Id
            }
        }
        if (i >= fmt.size()) break;
        char conv = fmt[i++];
        switch (conv) {
        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': {
            // On Win32 `long` is 32 bits, so %l is one slot; %ll and %I64 are
            // two (low dword first). %z is size_t, 32 bits on i386.
            uint64_t v = next();
            bool wide = len == LEN_LL || len == LEN_I64;
            if (wide) v |= uint64_t(next()) << 32;
            else v &= 0xffffffffu;
            if (len == LEN_H) v &= 0xffffu;
            if (len == LEN_HH) v &= 0xffu;
            bool neg = false; std::string body;
            if (conv == 'd' || conv == 'i') {
                int64_t s = wide ? int64_t(v) : int64_t(int32_t(uint32_t(v)));
                if (len == LEN_H) s = int16_t(v);
                if (len == LEN_HH) s = int8_t(v);
                neg = s < 0;
                body = guest_fmt::uint_to(neg ? uint64_t(0) - uint64_t(s) : uint64_t(s), 10, false);
            } else body = guest_fmt::uint_to(v, conv == 'o' ? 8 : conv == 'u' ? 10 : 16, conv == 'X');
            if (prec >= 0 && int(body.size()) < prec) body = std::string(size_t(prec - int(body.size())), '0') + body;
            if (alt) {
                if (conv == 'x') body = "0x" + body;
                else if (conv == 'X') body = "0X" + body;
                else if (conv == 'o' && (body.empty() || body[0] != '0')) body = "0" + body;
            }
            if (neg) body = "-" + body;
            else if (plus && (conv == 'd' || conv == 'i')) body = "+" + body;
            else if (space && (conv == 'd' || conv == 'i')) body = " " + body;
            out += guest_fmt::pad(body, width, left, zero && !left && prec < 0 ? '0' : ' ');
            break; }
        case 'c': { char v = char(next()); out += guest_fmt::pad(std::string(1, v), width, left, ' '); break; }
        case 's': {
            uint32_t p = next(); std::string s = p ? str(p) : std::string("(null)");
            if (prec >= 0 && int(s.size()) > prec) s.resize(size_t(prec));
            out += guest_fmt::pad(s, width, left, ' ');
            break; }
        case 'p': {
            uint32_t p = next();
            std::string body = "00000000" + guest_fmt::uint_to(p, 16, false);
            out += "0x" + body.substr(body.size() - 8);
            break; }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            // A double vararg occupies two 32-bit stack slots (little-endian: low
            // dword first) with no alignment padding on i386. long double is 12
            // bytes on i386, but no CRT printf path in a game passes one without
            // an explicit cast, and MSVC's %Lf is not used by the fixtures; treat
            // it as double and say so.
            double v;
            uint32_t lo = next(), hi = next(); uint64_t bits = uint64_t(hi) << 32 | lo; std::memcpy(&v, &bits, 8);
            char spec[32]; int k = 0;
            spec[k++] = '%';
            if (left) spec[k++] = '-';
            if (plus) spec[k++] = '+';
            if (space) spec[k++] = ' ';
            if (alt) spec[k++] = '#';
            if (zero) spec[k++] = '0';
            if (width) k += std::snprintf(spec + k, sizeof spec - size_t(k), "%d", width);
            if (prec >= 0) k += std::snprintf(spec + k, sizeof spec - size_t(k), ".%d", prec);
            spec[k++] = conv; if (conv == 'F') spec[k - 1] = 'f'; if (conv == 'E') spec[k - 1] = 'e'; if (conv == 'G') spec[k - 1] = 'g';
            spec[k] = 0;
            char buf[512];
            int n = std::snprintf(buf, sizeof buf, spec, v);
            if (n < 0) n = 0;
            if (size_t(n) >= sizeof buf) n = int(sizeof buf) - 1;
            out.append(buf, size_t(n));
            break; }
        case 'n': { uint32_t p = next(); if (p) mem.w32(p, uint32_t(out.size())); break; }
        default: out.push_back('%'); out.push_back(conv); break;
        }
    }
    return out;
}

inline int Guest::store(const std::string& s, uint32_t out_ptr, int cap, bool append_nul) {
    int n = int(s.size());
    if (cap != 0) {
        // cap < 0 is an unbounded buffer: every byte fits, plus the terminator.
        int room = cap < 0 ? n : (append_nul ? cap - 1 : cap);
        if (room < 0) room = 0;
        int put = n < room ? n : room;
        if (put > 0) mem.write_bytes(out_ptr, s.data(), uint32_t(put));
        if (append_nul) mem.w8(out_ptr + uint32_t(put), 0);
    }
    return n;
}

inline void Guest::run_atexit(Cpu& c) {
    while (!atexit_fns.empty()) {
        uint32_t fn = atexit_fns.back(); atexit_fns.pop_back();
        if (fn) c.call(fn, {});
    }
}
inline void Guest::do_exit(Cpu& c, uint32_t code) {
    try { run_atexit(c); } catch (...) { c.exited = true; c.exit_code = code; throw; }
    c.exited = true; c.exit_code = code;
}

// ---- registration ----
inline void Guest::register_all() {
    reg.map.clear();
    reg.ordinals.clear();
    register_win32(reg, host);  // the stage-1 allowlist stays the base surface
    register_xinput(reg, host);
    register_dinput8(reg, host);
    register_gl(reg, host);
    register_d3d9(reg, host);
    host.dsound_event=[this](uint32_t token)->uintptr_t {
        auto handle=sync_handles.find(token);if(handle==sync_handles.end())return 0;
        auto object=sync_objects.find(handle->second);if(object==sync_objects.end()||object->second.kind!=SyncKind::Event)return 0;
        return object->second.native_event;
    };
    register_dsound(reg, host);
    register_ws2(reg, host);
    // GDI handles are guest tokens, never native pointers. DIB pixels live in
    // interpreter memory; only memory-DC SRCCOPY is implemented. Window DC
    // presentation and text rasterization have no host backend and fail closed.
    auto gdi_handle = [this]() { return 0x72000000u + ++gdi_serial; };
    auto make_bitmap = [this, gdi_handle](uint32_t w, uint32_t h, uint32_t bpp, bool top_down) -> uint32_t {
        if (!w || !h || w > 16384 || h > 16384 || bpp != 32 || uint64_t(w) * h * 4 > 0x4000000u) { set_last_error(87); return 0; }
        uint32_t handle = gdi_handle(), bits = heap.alloc(w * h * 4);
        std::vector<uint8_t> zero(size_t(w) * h * 4, 0); mem.write_bytes(bits, zero.data(), uint32_t(zero.size()));
        gdi_objects[handle] = GdiObject{1, w, h, bits, bpp, 0, top_down, false, w * 4}; return handle;
    };
    reg.add("gdi32.dll", "GetDeviceCaps", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        if(!gdi_dcs.count(c.arg(0))&&!host.dc_handles.count(c.arg(0))){set_last_error(6);return 0;}
        int index=int32_t(c.arg(1)), value=0;
        if(index==8||index==10) { if(host.system_metrics&&host.system_metrics(index==8?0:1,value))return uint32_t(value); set_last_error(120);return 0; }
        switch(index) { case 12:return 32; case 14:return 1; case 88: case 90:return 96; case 116:return 60; case 38:return 0; default:set_last_error(120);return 0; }
    });
    reg.add("gdi32.dll", "CreateCompatibleDC", Conv::Stdcall, 1, [this,gdi_handle](Cpu& c) -> uint32_t { if(c.arg(0)&&!gdi_dcs.count(c.arg(0))&&!host.dc_handles.count(c.arg(0))){set_last_error(6);return 0;}uint32_t h=gdi_handle();if(!gdi_objects.count(0x73000001u)){uint32_t bits=heap.alloc(4);mem.w32(bits,0);gdi_objects[0x73000001u]=GdiObject{1,1,1,bits,32,0,true,true};}if(!gdi_objects.count(0x73001000u))gdi_objects[0x73001000u]=GdiObject{2,0,0,0,0,0x00ffffffu,true,true};if(!gdi_objects.count(0x7300100du))gdi_objects[0x7300100du]=GdiObject{3,0,0,0,0,0,true,true};if(!gdi_objects.count(0x73001007u))gdi_objects[0x73001007u]=GdiObject{4,0,0,0,0,0,true,true};GdiDc dc{};dc.bitmap=0x73000001u;dc.brush=0x73001000u;dc.pen=0x73001007u;dc.font=0x7300100du;gdi_dcs[h]=dc;return h; });
    reg.add("gdi32.dll", "DeleteDC", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { return gdi_dcs.erase(c.arg(0))?1u:(set_last_error(6),0u); });
    reg.add("gdi32.dll", "CreateCompatibleBitmap", Conv::Stdcall, 3, [this,make_bitmap](Cpu& c) -> uint32_t { return make_bitmap(c.arg(1),c.arg(2),32,true); });
    reg.add("gdi32.dll", "CreateBitmap", Conv::Stdcall, 5, [this,gdi_handle](Cpu& c) -> uint32_t {
        uint32_t w=c.arg(0), h=c.arg(1), planes=c.arg(2), bpp=c.arg(3);
        if (!w || !h || w>16384 || h>16384 || planes!=1 || !(bpp==1 || bpp==4 || bpp==8 || bpp==16 || bpp==24 || bpp==32) || uint64_t(w)*bpp>0x7ffffff0u) { set_last_error(87); return 0; }
        uint32_t pitch=uint32_t(((uint64_t(w)*bpp+31)/32)*4);
        if (uint64_t(pitch)*h>0x4000000u) { set_last_error(87); return 0; }
        uint32_t handle=gdi_handle(), bits=heap.alloc(pitch*h);
        std::vector<uint8_t> zero(size_t(pitch)*h,0); mem.write_bytes(bits,zero.data(),uint32_t(zero.size()));
        if (c.arg(4)) mem.write_bytes(bits,c.mem.read_bytes(c.arg(4),uint32_t(zero.size())).data(),uint32_t(zero.size()));
        gdi_objects[handle]=GdiObject{1,w,h,bits,bpp,0,false,false,pitch}; return handle;
    });
    reg.add("gdi32.dll", "CreateDIBSection", Conv::Stdcall, 6, [this,make_bitmap](Cpu& c) -> uint32_t {
        uint32_t info=c.arg(1); if(!info||c.arg(2)>1||c.arg(4)){set_last_error(c.arg(4)?50:87);return 0;}
        uint32_t w=c.mem.r32(info+4), raw_h=c.mem.r32(info+8), planes=c.mem.r(info+12,2), bpp=c.mem.r(info+14,2), compression=c.mem.r32(info+16);
        int32_t signed_h=int32_t(raw_h);if(!planes||(compression!=0&&compression!=3)){set_last_error(87);return 0;}
        if (compression==3 && (bpp!=32 || c.mem.r32(info+40)!=0x00ff0000u || c.mem.r32(info+44)!=0x0000ff00u || c.mem.r32(info+48)!=0x000000ffu)) { set_last_error(87); return 0; }
        uint32_t h=make_bitmap(w,uint32_t(signed_h<0?-int64_t(signed_h):signed_h),bpp,signed_h<0);
        if(h&&c.arg(3)) c.mem.w32(c.arg(3),gdi_objects[h].bits);
        return h;
    });
    reg.add("gdi32.dll", "DeleteObject", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { auto i=gdi_objects.find(c.arg(0));if(i==gdi_objects.end()){set_last_error(6);return 0;}if(i->second.stock)return 1;for(auto& d:gdi_dcs)if(d.second.bitmap==c.arg(0)||d.second.brush==c.arg(0)||d.second.pen==c.arg(0)||d.second.font==c.arg(0)){set_last_error(170);return 0;}gdi_objects.erase(i);return 1; });
    reg.add("gdi32.dll", "SelectObject", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { auto d=gdi_dcs.find(c.arg(0));auto o=gdi_objects.find(c.arg(1));if(d==gdi_dcs.end()||o==gdi_objects.end()){set_last_error(6);return 0;}if(o->second.kind==1){uint32_t old=d->second.bitmap;d->second.bitmap=c.arg(1);return old;}if(o->second.kind==2){uint32_t old=d->second.brush;d->second.brush=c.arg(1);return old;}if(o->second.kind==4){uint32_t old=d->second.pen;d->second.pen=c.arg(1);return old;}if(o->second.kind==3){uint32_t old=d->second.font;d->second.font=c.arg(1);return old;}set_last_error(6);return 0; });
    auto get_object=[this](Cpu& c)->uint32_t { auto i=gdi_objects.find(c.arg(0));if(i==gdi_objects.end())return 0;auto& o=i->second;uint32_t size=o.kind==1?24u:o.kind==2?12u:0u;if(!size)return 0;if(c.arg(1)<size)return 0;if(!c.arg(2))return size;uint32_t p=c.arg(2);if(o.kind==1){c.mem.w32(p,0);c.mem.w32(p+4,o.width);c.mem.w32(p+8,o.height);c.mem.w32(p+12,o.pitch?o.pitch:o.width*4);c.mem.w(p+16,1,2);c.mem.w(p+18,uint16_t(o.bpp),2);c.mem.w32(p+20,o.bits);}else{c.mem.w32(p,0);c.mem.w32(p+4,o.color);c.mem.w32(p+8,0);}return size; };
    reg.add("gdi32.dll", "GetObjectA", Conv::Stdcall, 3, [get_object](Cpu& c){return get_object(c);});
    reg.add("gdi32.dll", "GetObjectW", Conv::Stdcall, 3, [get_object](Cpu& c){return get_object(c);});
    reg.add("gdi32.dll", "GetDIBits", Conv::Stdcall, 7, [this](Cpu& c) -> uint32_t {
        auto i=gdi_objects.find(c.arg(1)); uint32_t info=c.arg(5), bits=c.arg(4), start=c.arg(2), lines=c.arg(3);
        if (i==gdi_objects.end() || i->second.kind!=1 || !info || c.arg(6)>1 || start>i->second.height) { set_last_error(87); return 0; }
        auto& o=i->second; if (start+lines>o.height) lines=o.height-start;
        uint32_t requested_bpp=c.mem.r(info+14,2);
        c.mem.w32(info+4,o.width); c.mem.w32(info+8,o.top_down?-int32_t(o.height):int32_t(o.height)); c.mem.w(info+12,1,2); c.mem.w(info+14,uint16_t(requested_bpp?requested_bpp:o.bpp),2); c.mem.w32(info+16,requested_bpp?0:3);
        if (!bits || !lines) {
            if (!requested_bpp && o.bpp==32) { c.mem.w32(info+40,0x00ff0000); c.mem.w32(info+44,0x0000ff00); c.mem.w32(info+48,0x000000ff); }
            return lines;
        }
        if (o.bpp!=32 || requested_bpp!=32) { set_last_error(87); return 0; }
        for (uint32_t y=0;y<lines;++y) { uint32_t src=o.bits+(o.top_down?start+y:o.height-1-start-y)*o.pitch; c.mem.write_bytes(bits+y*o.width*4,c.mem.read_bytes(src,o.width*4).data(),o.width*4); }
        return lines;
    });
    reg.add("user32.dll", "CreateIconIndirect", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t info=c.arg(0); if (!info) { set_last_error(87); return 0; }
        uint32_t mask=c.mem.r32(info+12), color=c.mem.r32(info+16);
        if (!gdi_objects.count(mask) || !gdi_objects.count(color)) { set_last_error(87); return 0; }
        return 0x74000000u+(++gdi_serial);
    });
    reg.add("user32.dll", "CopyImage", Conv::Stdcall, 5, [this](Cpu& c) -> uint32_t {
        if (!c.arg(0) || (c.arg(1)!=1 && c.arg(1)!=2)) { set_last_error(87); return 0; }
        return 0x74000000u+(++gdi_serial);
    });
    reg.add("user32.dll", "DestroyIcon", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t { return c.arg(0)?1u:0u; });
    reg.add("user32.dll", "SystemParametersInfoW", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t {
        if (c.arg(0)!=0x30 || !c.arg(2)) { set_last_error(120); return 0; }
        c.mem.w32(c.arg(2),0); c.mem.w32(c.arg(2)+4,0); c.mem.w32(c.arg(2)+8,1280); c.mem.w32(c.arg(2)+12,720); return 1;
    });
    reg.add("user32.dll", "RegisterWindowMessageA", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        std::string name=str(c.arg(0)); uint32_t hash=2166136261u;
        for (unsigned char ch:name) hash=(hash^ch)*16777619u;
        return 0xc000u+(hash&0x3fffu);
    });
    reg.add("kernel32.dll", "SetThreadExecutionState", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t { return c.arg(0); });
    reg.add("user32.dll", "GetKeyboardState", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        if (!c.arg(0)) { set_last_error(87); return 0; }
        std::vector<uint8_t> state(256,0); c.mem.write_bytes(c.arg(0),state.data(),uint32_t(state.size())); return 1;
    });
    reg.add("user32.dll", "ToUnicode", Conv::Stdcall, 6, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "VerSetConditionMask", Conv::Stdcall, 4, [](Cpu& c) -> uint32_t {
        uint64_t mask=uint64_t(c.arg(0))|(uint64_t(c.arg(1))<<32); uint32_t types=c.arg(2); uint8_t condition=uint8_t(c.arg(3))&7;
        for (unsigned bit=0;bit<22;++bit) if (types&(1u<<bit)) {
            uint64_t field=uint64_t(7)<<(bit*3);
            mask=(mask&~field)|(uint64_t(condition)<<(bit*3));
        }
        c.reg[EDX]=uint32_t(mask>>32); return uint32_t(mask);
    });
    auto verify_version_info=[this](Cpu& c)->uint32_t {
        uint32_t p=c.arg(0),types=c.arg(1);
        uint64_t conditions=uint64_t(c.arg(2))|(uint64_t(c.arg(3))<<32);
        if(!p || !types){set_last_error(87);return 0;}
        uint32_t size=mem.r32(p);
        bool wide=size>=284;
        if(!wide && size<156){set_last_error(87);return 0;}
        auto check=[&](unsigned bit,uint32_t actual,uint32_t requested,bool flags=false)->bool {
            if(!(types&(1u<<bit)))return true;
            uint32_t condition=uint32_t((conditions>>(bit*3))&7);
            switch(condition){
            case 1:return actual==requested;
            case 2:return actual>requested;
            case 3:return actual>=requested;
            case 4:return actual<requested;
            case 5:return actual<=requested;
            case 6:return flags?(actual&requested)==requested:false;
            case 7:return flags?(actual&requested)!=0:false;
            default:return false;
            }
        };
        uint32_t major=mem.r32(p+4),minor=mem.r32(p+8),build=mem.r32(p+12),platform=mem.r32(p+16);
        uint32_t spminor=mem.r(p+(wide?278:150),2),spmajor=mem.r(p+(wide?276:148),2);
        uint32_t suite=mem.r(p+(wide?280:152),2),product=mem.r8(p+(wide?282:154));
        bool matches=check(0,0,minor)&&check(1,10,major)&&check(2,19045,build)&&check(3,2,platform)&&
            check(4,0,spminor)&&check(5,0,spmajor)&&check(6,0,suite,true)&&
            check(7,1,product)&&check(8,0,mem.r8(p+(wide?283:155)));
        if(!matches){set_last_error(1150);return 0;}
        return 1;
    };
    reg.add("kernel32.dll", "VerifyVersionInfoA", Conv::Stdcall, 4, verify_version_info);
    reg.add("kernel32.dll", "VerifyVersionInfoW", Conv::Stdcall, 4, verify_version_info);
    reg.add("gdi32.dll", "GetStockObject", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { int32_t n=int32_t(c.arg(0));if(!(n>=0&&n<=8)&&!(n>=10&&n<=14)&&n!=16&&n!=17)return 0;uint32_t h=0x73001000u+uint32_t(n);if(!gdi_objects.count(h)){uint32_t kind=n<=5?2u:n<=8?4u:3u,color=n==0?0x00ffffffu:n==4||n==7?0u:0u;gdi_objects[h]=GdiObject{kind,0,0,0,0,color,true,true};}return h; });
    reg.add("gdi32.dll", "CreateSolidBrush", Conv::Stdcall, 1, [this,gdi_handle](Cpu& c) -> uint32_t { uint32_t h=gdi_handle();gdi_objects[h]=GdiObject{2,0,0,0,0,c.arg(0),true};return h; });
    reg.add("gdi32.dll", "CreatePen", Conv::Stdcall, 3, [this,gdi_handle](Cpu& c) -> uint32_t { uint32_t h=gdi_handle();gdi_objects[h]=GdiObject{4,0,0,0,0,c.arg(2),true};return h; });
    reg.add("gdi32.dll", "SetBkMode", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { auto d=gdi_dcs.find(c.arg(0));if(d==gdi_dcs.end()){set_last_error(6);return 0;}int old=d->second.bk_mode;d->second.bk_mode=int32_t(c.arg(1));return uint32_t(old); });
    reg.add("gdi32.dll", "SetTextColor", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { auto d=gdi_dcs.find(c.arg(0));if(d==gdi_dcs.end()){set_last_error(6);return 0xffffffffu;}uint32_t old=d->second.text_color;d->second.text_color=c.arg(1);return old; });
    reg.add("gdi32.dll", "SetBkColor", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { auto d=gdi_dcs.find(c.arg(0));if(d==gdi_dcs.end()){set_last_error(6);return 0xffffffffu;}uint32_t old=d->second.bk_color;d->second.bk_color=c.arg(1);return old; });
    reg.add("gdi32.dll", "GdiFlush", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 1; });
    auto blit=[this](Cpu& c,bool stretch)->uint32_t { uint32_t dst=c.arg(0),src=c.arg(5);auto d=gdi_dcs.find(dst),s=gdi_dcs.find(src);if(d==gdi_dcs.end()||s==gdi_dcs.end()||!d->second.bitmap||!s->second.bitmap||c.arg(stretch?10:8)!=0x00cc0020u){set_last_error(120);return 0;}auto& db=gdi_objects[d->second.bitmap];auto& sb=gdi_objects[s->second.bitmap];if(db.bpp!=32||sb.bpp!=32){set_last_error(120);return 0;}int32_t x=int32_t(c.arg(1)),y=int32_t(c.arg(2)),w=int32_t(c.arg(3)),h=int32_t(c.arg(4)),sx=int32_t(c.arg(6)),sy=int32_t(c.arg(7)),sw=stretch?int32_t(c.arg(8)):w,sh=stretch?int32_t(c.arg(9)):h;if(w<=0||h<=0||sw<=0||sh<=0||w>16384||h>16384||sw>16384||sh>16384){set_last_error(87);return 0;}for(int32_t yy=0;yy<h;++yy)for(int32_t xx=0;xx<w;++xx){int32_t dx=x+xx,dy=y+yy,px=sx+xx*sw/w,py=sy+yy*sh/h;if(dx<0||dy<0||px<0||py<0||uint32_t(dx)>=db.width||uint32_t(dy)>=db.height||uint32_t(px)>=sb.width||uint32_t(py)>=sb.height)continue;uint32_t dr=db.bits+((db.top_down?uint32_t(dy):db.height-1-uint32_t(dy))*db.width+uint32_t(dx))*4,sr=sb.bits+((sb.top_down?uint32_t(py):sb.height-1-uint32_t(py))*sb.width+uint32_t(px))*4;c.mem.w32(dr,c.mem.r32(sr));}return 1;};
    reg.add("gdi32.dll", "BitBlt", Conv::Stdcall, 9, [blit](Cpu& c){return blit(c,false);});
    reg.add("gdi32.dll", "StretchBlt", Conv::Stdcall, 11, [blit](Cpu& c){return blit(c,true);});
    auto unsupported_gdi=[this](Cpu&) { set_last_error(120); return uint32_t(0); };
    auto unsupported_dib=[this](Cpu&) { set_last_error(120); return 0xffffffffu; };
    reg.add("gdi32.dll", "StretchDIBits", Conv::Stdcall, 13, unsupported_dib);
    reg.add("gdi32.dll", "SetDIBitsToDevice", Conv::Stdcall, 12, unsupported_dib);
    reg.add("gdi32.dll", "CreateFontA", Conv::Stdcall, 14, unsupported_gdi);
    reg.add("gdi32.dll", "CreateFontW", Conv::Stdcall, 14, unsupported_gdi);
    reg.add("gdi32.dll", "CreateFontIndirectA", Conv::Stdcall, 1, unsupported_gdi);
    reg.add("gdi32.dll", "CreateFontIndirectW", Conv::Stdcall, 1, unsupported_gdi);
    reg.add("gdi32.dll", "GetTextMetricsA", Conv::Stdcall, 2, unsupported_gdi);
    reg.add("gdi32.dll", "GetTextMetricsW", Conv::Stdcall, 2, unsupported_gdi);
    reg.add("gdi32.dll", "GetTextExtentPoint32A", Conv::Stdcall, 4, unsupported_gdi);
    reg.add("gdi32.dll", "GetTextExtentPoint32W", Conv::Stdcall, 4, unsupported_gdi);
    reg.add("gdi32.dll", "TextOutA", Conv::Stdcall, 5, unsupported_gdi);
    reg.add("gdi32.dll", "TextOutW", Conv::Stdcall, 5, unsupported_gdi);
    // Fixed guest structures: three FILE structs for stdin/stdout/stderr.
    mem.map(iob_addr, 4096, PROT_R | PROT_W);
    iob.assign(3 * 32, 0);
    mem.write_bytes(iob_addr, iob.data(), uint32_t(iob.size()));
    for (int i = 0; i < 3; ++i) mem.w32(iob_addr + uint32_t(i * 32), 0xFFFFFFFFu);  // _file = -1
    initenv = heap.alloc(16); commode = heap.alloc(16); fmode = heap.alloc(16);
    mb_cur_max = heap.alloc(16); errno_ptr = heap.alloc(16);
    mem.w32(commode, 0); mem.w32(fmode, 0); mem.w32(mb_cur_max, 1); mem.w32(errno_ptr, 0);
    startupinfo = heap.alloc(68); mem.write_bytes(startupinfo, std::vector<uint8_t>(68, 0).data(), 68);
    mem.w32(startupinfo, 68);  // cb
    cmdline_ptr = put_str(cmdline.empty() ? exe_path : cmdline);
    rebuild_environment();
    module_name_ptr = put_str(exe_path);
    // Data imports (mingw's win32-threaded CRT startup references msvcrt's
    // variables directly instead of through the __p__ accessors): the IAT slot
    // holds the variable's guest address. __initenv starts NULL and the CRT
    // startup writes it through __getmainargs' env out-param; __mb_cur_max is 1.
    reg.data.clear();
    reg.add_data("msvcrt.dll", "__initenv", initenv);
    reg.add_data("msvcrt.dll", "__winitenv", initenv);
    reg.add_data("msvcrt.dll", "__mb_cur_max", mb_cur_max);
    reg.add_data("msvcrt.dll", "_iob", iob_addr);
    // __iob_func is a FUNCTION (FILE* __iob_func(void), stdio.h): its IAT slot
    // must hold a callable thunk, not the table address — a data binding makes
    // a call through it jump into the iob bytes. It returns the table pointer.
    reg.add("msvcrt.dll", "__iob_func", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return iob_addr; });
    // MSVC 11's CRT uses this startup helper from its own DLL name. Honor the
    // STARTUPINFO flag and value rather than treating the zeroed block as a
    // request to hide the window.
    reg.add("msvcr110.dll", "__crtGetShowWindowMode", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t {
        return (mem.r32(startupinfo + 44) & 1u) ? uint16_t(mem.r(startupinfo + 48, 2)) : 10u;
    });
    auto C = [](const char* n) { return n; };
    (void)C;
    auto steam_reg_value=[this](std::string name,bool wide,uint32_t& type,std::vector<uint8_t>& bytes) {
        for(char& ch:name){if(wide&&ch>='A'&&ch<='Z')ch=char(ch+32);else if(!wide&&ch>='A'&&ch<='Z')ch=char(ch+32);}
        auto dword=[&](uint32_t value){type=4;bytes.resize(4);memcpy(bytes.data(),&value,4);};
        auto text=[&](const std::string& value){type=1;if(wide){bytes.resize((value.size()+1)*2);for(size_t i=0;i<value.size();++i){bytes[i*2]=uint8_t(value[i]);bytes[i*2+1]=0;}}else bytes.assign(value.begin(),value.end());bytes.resize(bytes.size()+(wide?2:1),0);};
        if(name=="pid")dword(0x1000);
        else if(name=="activeuser")dword(steam_registry.account);
        else if(name=="steamclientdll")text(steam_registry.client);
        else if(name=="universe")text("Public");
        else return false;
        return true;
    };
    auto registry_name=[this](Cpu& c,uint32_t p,bool wide)->std::u16string {
        (void)c;
        if(wide)return wstr(p);
        std::u16string s;for(unsigned char ch:str(p))s.push_back(char16_t(ch));return s;
    };
    auto registry_host=[this](Host::RegistryCall& q)->uint32_t {
        if(!host.registry_call)return 120;
        host.registry_call(q);
        return q.status;
    };
    auto registry_handle=[this](uint32_t h)->uintptr_t {auto it=registry_handles.find(h);return it==registry_handles.end()?0:it->second;};
    auto registry_open=[this,registry_name,registry_host,registry_handle](Cpu& c,bool wide,bool create)->uint32_t {
        uint32_t out=create?c.arg(7):c.arg(4);if(!out)return 87;
        Host::RegistryCall q{};q.kind=create?Host::RegistryCall::Kind::Create:Host::RegistryCall::Kind::Open;
        uint32_t root=c.arg(0);q.root=root>=0x80000000u&&root<=0x80000003u?root:uint32_t(registry_handle(root));
        if(root<0x80000000u&&!registry_handle(root))return 6;
        q.subkey=registry_name(c,c.arg(1),wide);q.ansi=!wide;
        if(create){if(c.arg(2)||c.arg(6))return c.arg(2)?87u:50u;q.options=c.arg(4);q.access=c.arg(5);}else{if(c.arg(2))return 87;q.options=c.arg(2);q.access=c.arg(3);}
        uint32_t status=registry_host(q);
        if(status){
            if(!create&&!host.registry_call&&c.arg(0)==0x80000001u&&q.subkey==u"Software\\Valve\\Steam\\ActiveProcess"&&!steam_registry.client.empty()){
                uint32_t key=0x71000000u+(++steam_registry_serial);steam_registry_keys[key]=true;mem.w32(out,key);return 0;
            }
            return status;
        }
        uint32_t key=0x72000000u+(++registry_serial);registry_handles[key]=q.result_handle;mem.w32(out,key);
        if(create&&c.arg(8))mem.w32(c.arg(8),q.disposition);
        return 0;
    };
    auto registry_query=[this,registry_name,registry_handle,registry_host,steam_reg_value](Cpu& c,bool wide)->uint32_t {
        uint32_t h=c.arg(0),cap_ptr=c.arg(5);if(c.arg(2)||!cap_ptr)return 87;
        if(steam_registry_keys.count(h)){
            std::string name;for(char16_t ch:registry_name(c,c.arg(1),wide))name.push_back(char(ch));
            uint32_t type=0;std::vector<uint8_t> bytes;if(!steam_reg_value(name,wide,type,bytes))return 2;
            if(c.arg(3))mem.w32(c.arg(3),type);
            uint32_t cap=mem.r32(cap_ptr);mem.w32(cap_ptr,uint32_t(bytes.size()));
            if(!c.arg(4))return 0;
            if(cap<bytes.size())return 234;
            mem.write_bytes(c.arg(4),bytes.data(),uint32_t(bytes.size()));return 0;
        }
        uintptr_t native=registry_handle(h);if(!native)return 6;
        Host::RegistryCall q{};q.kind=Host::RegistryCall::Kind::Query;q.handle=native;q.name=registry_name(c,c.arg(1),wide);q.ansi=!wide;
        uint32_t status=registry_host(q);if(status)return status;
        if(c.arg(3))mem.w32(c.arg(3),q.type);
        uint32_t cap=mem.r32(cap_ptr);mem.w32(cap_ptr,uint32_t(q.data.size()));
        if(!c.arg(4))return 0;
        if(cap<q.data.size())return 234;
        if(!q.data.empty())mem.write_bytes(c.arg(4),q.data.data(),uint32_t(q.data.size()));
        return 0;
    };
    auto reg_open_a=[registry_open](Cpu& c){return registry_open(c,false,false);};
    auto reg_open_w=[registry_open](Cpu& c){return registry_open(c,true,false);};
    auto reg_create_a=[registry_open](Cpu& c){return registry_open(c,false,true);};
    auto reg_create_w=[registry_open](Cpu& c){return registry_open(c,true,true);};
    reg.add("advapi32.dll","RegOpenKeyExA",Conv::Stdcall,5,[reg_open_a](Cpu& c){return reg_open_a(c);});
    reg.add("advapi32.dll","RegOpenKeyExW",Conv::Stdcall,5,[reg_open_w](Cpu& c){return reg_open_w(c);});
    reg.add("advapi32.dll","RegCreateKeyExA",Conv::Stdcall,9,[reg_create_a](Cpu& c){return reg_create_a(c);});
    reg.add("advapi32.dll","RegCreateKeyExW",Conv::Stdcall,9,[reg_create_w](Cpu& c){return reg_create_w(c);});
    reg.add("advapi32.dll","RegQueryValueExA",Conv::Stdcall,6,[registry_query](Cpu& c){return registry_query(c,false);});
    reg.add("advapi32.dll","RegQueryValueExW",Conv::Stdcall,6,[registry_query](Cpu& c){return registry_query(c,true);});
    auto registry_set=[registry_name,registry_handle,registry_host](Cpu& c,bool wide)->uint32_t {
        uintptr_t native=registry_handle(c.arg(0));if(!native)return 6;if(c.arg(2))return 87;
        uint32_t size=c.arg(5);if(size>(1u<<24)||(!c.arg(4)&&size))return 87;
        Host::RegistryCall q{};q.kind=Host::RegistryCall::Kind::Set;q.handle=native;q.name=registry_name(c,c.arg(1),wide);q.type=c.arg(3);q.ansi=!wide;
        if(size)q.data=c.mem.read_bytes(c.arg(4),size);
        return registry_host(q);
    };
    reg.add("advapi32.dll","RegSetValueExA",Conv::Stdcall,6,[registry_set](Cpu& c){return registry_set(c,false);});
    reg.add("advapi32.dll","RegSetValueExW",Conv::Stdcall,6,[registry_set](Cpu& c){return registry_set(c,true);});
    auto registry_delete=[registry_name,registry_handle,registry_host](Cpu& c,bool wide,bool key)->uint32_t {
        uintptr_t native=registry_handle(c.arg(0));if(!native)return 6;Host::RegistryCall q{};q.kind=key?Host::RegistryCall::Kind::DeleteKey:Host::RegistryCall::Kind::DeleteValue;
        q.handle=native;q.subkey=registry_name(c,c.arg(1),wide);q.name=q.subkey;q.ansi=!wide;return registry_host(q);
    };
    reg.add("advapi32.dll","RegDeleteValueA",Conv::Stdcall,2,[registry_delete](Cpu& c){return registry_delete(c,false,false);});
    reg.add("advapi32.dll","RegDeleteValueW",Conv::Stdcall,2,[registry_delete](Cpu& c){return registry_delete(c,true,false);});
    reg.add("advapi32.dll","RegDeleteKeyA",Conv::Stdcall,2,[registry_delete](Cpu& c){return registry_delete(c,false,true);});
    reg.add("advapi32.dll","RegDeleteKeyW",Conv::Stdcall,2,[registry_delete](Cpu& c){return registry_delete(c,true,true);});
    auto registry_enum=[this,registry_handle,registry_host](Cpu& c,bool wide,bool value)->uint32_t {
        uintptr_t native=registry_handle(c.arg(0));if(!native)return 6;uint32_t namep=c.arg(2),lenp=c.arg(3);if(!lenp)return 87;
        if(c.arg(4))return 87;
        Host::RegistryCall q{};q.kind=value?Host::RegistryCall::Kind::EnumValue:Host::RegistryCall::Kind::EnumKey;q.handle=native;q.index=c.arg(1);q.ansi=!wide;
        uint32_t status=registry_host(q);if(status)return status;
        uint32_t required=uint32_t(q.name.size());uint32_t cap=mem.r32(lenp);
        if(namep&&cap<required+1){mem.w32(lenp,required+1);return 234;}
        if(namep){if(wide){for(uint32_t i=0;i<required;++i)mem.w(namep+i*2,q.name[i],2);mem.w(namep+required*2,0,2);}else{for(uint32_t i=0;i<required;++i)mem.w8(namep+i,uint8_t(q.name[i]));mem.w8(namep+required,0);}}
        mem.w32(lenp,required);if(c.arg(5))mem.w32(c.arg(5),q.type);
        if(!value){if(c.arg(6))mem.w32(c.arg(6),0);if(c.arg(7))mem.w32(c.arg(7),0);return 0;}
        if(c.arg(6)&&!c.arg(7))return 87;
        if(c.arg(7)){uint32_t data_cap=mem.r32(c.arg(7));mem.w32(c.arg(7),uint32_t(q.data.size()));if(c.arg(6)){if(data_cap<q.data.size())return 234;if(!q.data.empty())mem.write_bytes(c.arg(6),q.data.data(),uint32_t(q.data.size()));}}
        return 0;
    };
    reg.add("advapi32.dll","RegEnumKeyExA",Conv::Stdcall,8,[registry_enum](Cpu& c){return registry_enum(c,false,false);});
    reg.add("advapi32.dll","RegEnumKeyExW",Conv::Stdcall,8,[registry_enum](Cpu& c){return registry_enum(c,true,false);});
    reg.add("advapi32.dll","RegEnumValueA",Conv::Stdcall,8,[registry_enum](Cpu& c){return registry_enum(c,false,true);});
    reg.add("advapi32.dll","RegEnumValueW",Conv::Stdcall,8,[registry_enum](Cpu& c){return registry_enum(c,true,true);});
    reg.add("advapi32.dll","RegCloseKey",Conv::Stdcall,1,[this,registry_host](Cpu& c)->uint32_t {
        uint32_t h=c.arg(0);if(steam_registry_keys.erase(h))return 0;auto it=registry_handles.find(h);if(it==registry_handles.end())return 6;
        Host::RegistryCall q{};q.kind=Host::RegistryCall::Kind::Close;q.handle=it->second;uint32_t status=registry_host(q);if(!status)registry_handles.erase(it);return status;
    });
    auto get_user_name=[this](Cpu& c,bool wide)->uint32_t {
        uint32_t out=c.arg(0),sizep=c.arg(1);if(!out||!sizep){set_last_error(87);return 0;}
        uint32_t required=wide?7u:7u,cap=mem.r32(sizep);if(cap<required){mem.w32(sizep,required);set_last_error(122);return 0;}
        if(wide){const char16_t player[]=u"Player";for(uint32_t i=0;i<7;++i)mem.w(out+i*2,player[i],2);}
        else mem.write_bytes(out,"Player",7);
        mem.w32(sizep,required);return 1;
    };
    reg.add("advapi32.dll","GetUserNameA",Conv::Stdcall,2,[get_user_name](Cpu& c){return get_user_name(c,false);});
    reg.add("advapi32.dll","GetUserNameW",Conv::Stdcall,2,[get_user_name](Cpu& c){return get_user_name(c,true);});
    auto crypt_acquire=[this](Cpu& c,bool wide)->uint32_t {
        uint32_t out=c.arg(0),provider_type=c.arg(3),flags=c.arg(4);if(!out){set_last_error(87);return 0;}
        if((flags&0xF0000000u)!=0xF0000000u||provider_type!=1u){set_last_error(87);return 0;}
        (void)wide;uint32_t handle=0x73000000u+(++crypto_serial);crypto_contexts[handle]=true;mem.w32(out,handle);return 1;
    };
    reg.add("advapi32.dll","CryptAcquireContextA",Conv::Stdcall,5,[crypt_acquire](Cpu& c){return crypt_acquire(c,false);});
    reg.add("advapi32.dll","CryptAcquireContextW",Conv::Stdcall,5,[crypt_acquire](Cpu& c){return crypt_acquire(c,true);});
    reg.add("advapi32.dll","CryptGenRandom",Conv::Stdcall,3,[this](Cpu& c)->uint32_t {
        uint32_t handle=c.arg(0),size=c.arg(1),out=c.arg(2);if(!crypto_contexts.count(handle)){set_last_error(0x80090001u);return 0;}
        if(size>(1u<<20)||(!out&&size)){set_last_error(87);return 0;}std::vector<uint8_t> bytes(size);
        if(size&&(!host.random_bytes||!host.random_bytes(bytes.data(),size))){set_last_error(host.random_bytes?31u:120u);return 0;}
        if(size)mem.write_bytes(out,bytes.data(),size);
        return 1;
    });
    reg.add("advapi32.dll","CryptReleaseContext",Conv::Stdcall,2,[this](Cpu& c)->uint32_t {
        if(c.arg(1)){set_last_error(87);return 0;}if(!crypto_contexts.erase(c.arg(0))){set_last_error(0x80090001u);return 0;}return 1;
    });
    reg.add("shell32.dll", "SHGetFolderPathW", Conv::Stdcall, 5, [this](Cpu& c) -> uint32_t {
#if defined(_WIN32)
        using Fn = HRESULT (WINAPI*)(HWND, int, HANDLE, DWORD, LPWSTR);
        static HMODULE shell = LoadLibraryExW(L"shell32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto fn = shell ? reinterpret_cast<Fn>(GetProcAddress(shell, "SHGetFolderPathW")) : nullptr;
        if (!fn || c.arg(2)) return 0x80004001u;  // E_NOTIMPL: unavailable on this partition/token
        std::vector<wchar_t> path(260);
        HRESULT hr = fn(nullptr, int(c.arg(1)), nullptr, c.arg(3), path.data());
        if (SUCCEEDED(hr)) {
            for (size_t i = 0; i < path.size() && path[i]; ++i) mem.w(c.arg(4) + uint32_t(i * 2), uint16_t(path[i]), 2);
            mem.w(c.arg(4) + uint32_t(path.size() * 2 - 2), 0, 2);
        }
        return uint32_t(hr);
#else
        (void)c;
        return 0x80004001u;  // E_NOTIMPL: no native Shell API on the off-device host
#endif
    });
    auto shell_path_w=[this](Cpu& c)->uint32_t {
#if defined(_WIN32)
        if(!c.arg(1))return 0;static HMODULE shell=LoadLibraryExW(L"shell32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);auto fn=shell?GetProcAddress(shell,"SHGetSpecialFolderPathW"):nullptr;if(!fn)return 0;
        using Fn=BOOL(WINAPI*)(HWND,LPWSTR,int,BOOL);std::vector<wchar_t> path(260);if(!reinterpret_cast<Fn>(fn)(nullptr,path.data(),int(c.arg(2)),c.arg(3)))return 0;size_t n=0;while(n<path.size()&&path[n])++n;for(size_t i=0;i<n;++i)mem.w(c.arg(1)+uint32_t(i*2),uint16_t(path[i]),2);mem.w(c.arg(1)+uint32_t(n*2),0,2);return 1;
#else
        (void)c;return 0;
#endif
    };
    reg.add("shell32.dll","SHGetSpecialFolderPathW",Conv::Stdcall,4,[shell_path_w](Cpu& c)->uint32_t { return shell_path_w(c); });
    reg.add("shell32.dll","SHGetSpecialFolderPathA",Conv::Stdcall,4,[this](Cpu& c)->uint32_t {
#if defined(_WIN32)
        if(!c.arg(1))return 0;static HMODULE shell=LoadLibraryExW(L"shell32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);auto fn=shell?GetProcAddress(shell,"SHGetSpecialFolderPathA"):nullptr;if(!fn)return 0;
        using Fn=BOOL(WINAPI*)(HWND,LPSTR,int,BOOL);char path[260]{};if(!reinterpret_cast<Fn>(fn)(nullptr,path,int(c.arg(2)),c.arg(3)))return 0;mem.write_bytes(c.arg(1),path,uint32_t(std::strlen(path)+1));return 1;
#else
        (void)c;return 0;
#endif
    });
    reg.add("shell32.dll","SHGetFolderPathA",Conv::Stdcall,5,[this](Cpu& c)->uint32_t {
#if defined(_WIN32)
        if(!c.arg(4)||c.arg(2))return 0x80004003u;static HMODULE shell=LoadLibraryExW(L"shell32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);auto fn=shell?GetProcAddress(shell,"SHGetFolderPathA"):nullptr;if(!fn)return 0x80004001u;using Fn=HRESULT(WINAPI*)(HWND,int,HANDLE,DWORD,LPSTR);char path[260]{};HRESULT hr=reinterpret_cast<Fn>(fn)(nullptr,int(c.arg(1)),nullptr,c.arg(3),path);if(SUCCEEDED(hr))mem.write_bytes(c.arg(4),path,uint32_t(std::strlen(path)+1));return uint32_t(hr);
#else
        (void)c;return 0x80004001u;
#endif
    });
    reg.add("shell32.dll","SHGetKnownFolderPath",Conv::Stdcall,4,[this](Cpu& c)->uint32_t {
        if(!c.arg(3)){set_last_error(87);return 0x80004003u;}mem.w32(c.arg(3),0);
#if defined(_WIN32)
        if(!c.arg(0))return 0x80070057u;GUID id{};auto bytes=mem.read_bytes(c.arg(0),16);std::memcpy(&id,bytes.data(),16);static HMODULE shell=LoadLibraryExW(L"shell32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);auto fn=shell?GetProcAddress(shell,"SHGetKnownFolderPath"):nullptr;static HMODULE ole=LoadLibraryExW(L"ole32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);auto free_fn=ole?GetProcAddress(ole,"CoTaskMemFree"):nullptr;if(!fn||!free_fn)return 0x80004001u;using Fn=HRESULT(WINAPI*)(const GUID&,DWORD,HANDLE,LPWSTR*);using FreeFn=void(WINAPI*)(LPVOID);LPWSTR path=nullptr;HRESULT hr=reinterpret_cast<Fn>(fn)(id,c.arg(1),nullptr,reinterpret_cast<LPWSTR*>(&path));if(FAILED(hr)||!path)return uint32_t(hr);std::u16string value;for(size_t i=0;path[i];++i)value.push_back(char16_t(path[i]));uint32_t guest=put_wstr(value);reinterpret_cast<FreeFn>(free_fn)(path);mem.w32(c.arg(3),guest);return uint32_t(hr);
#else
        (void)c;return 0x80004001u;
#endif
    });
    auto shell_create_directory=[this](Cpu& c,bool wide)->uint32_t {
        if(!c.arg(1)){set_last_error(87);return 87;}
        std::u16string path=wide?wstr(c.arg(1)):std::u16string();std::string narrow=wide?std::string():str(c.arg(1));std::u16string guard_path=path;if(!wide)for(unsigned char ch:narrow)guard_path.push_back(char16_t(ch));if(!host.destructive_file_guard||!host.destructive_file_guard("SHCreateDirectoryEx",guard_path)){set_last_error(5);return 5;}
        Host::FileCall q{wide?Host::FileCall::Kind::CreateDirectoryW:Host::FileCall::Kind::AttributesA};if(wide)q.path_w=path;else{q.operation=Host::FileCall::Operation::CreateDirectoryA;q.path_a=narrow;}if(!host.file_call){set_last_error(120);return 120;}host.file_call(q);if(!q.succeeded){set_last_error(q.error?q.error:120);return q.error?q.error:120;}return q.error;
    };
    reg.add("shell32.dll","SHCreateDirectoryExA",Conv::Stdcall,3,[shell_create_directory](Cpu& c){return shell_create_directory(c,false);});
    reg.add("shell32.dll","SHCreateDirectoryExW",Conv::Stdcall,3,[shell_create_directory](Cpu& c){return shell_create_directory(c,true);});
    auto shell_execute=[this](Cpu& c,bool wide)->uint32_t { if(!c.arg(2))return 0; (void)wide;return 31; };
    reg.add("shell32.dll","ShellExecuteA",Conv::Stdcall,6,[shell_execute](Cpu& c){return shell_execute(c,false);});
    reg.add("shell32.dll","ShellExecuteW",Conv::Stdcall,6,[shell_execute](Cpu& c){return shell_execute(c,true);});
    reg.add("shell32.dll","CommandLineToArgvW",Conv::Stdcall,2,[this](Cpu& c)->uint32_t {
        if(!c.arg(0)||!c.arg(1)){set_last_error(87);return 0;}std::u16string input=wstr(c.arg(0));std::vector<std::u16string> args;size_t i=0;
        while(i<input.size()){while(i<input.size()&&(input[i]==u' '||input[i]==u'\t'))++i;if(i==input.size())break;std::u16string arg;bool quoted=false;
            while(i<input.size()){if(input[i]==u'\\'){size_t start=i;while(i<input.size()&&input[i]==u'\\')++i;size_t slashes=i-start;if(i<input.size()&&input[i]==u'"'){arg.append(slashes/2,u'\\');if(slashes%2){arg.push_back(u'"');++i;}else quoted=!quoted,++i;}else arg.append(slashes,u'\\');continue;}if(input[i]==u'"'){quoted=!quoted;++i;continue;}if(!quoted&&(input[i]==u' '||input[i]==u'\t'))break;arg.push_back(input[i++]);}args.push_back(std::move(arg));while(i<input.size()&&(input[i]==u' '||input[i]==u'\t'))++i;
        }
        uint64_t bytes=uint64_t(args.size())*4;for(const auto& arg:args)bytes+=uint64_t(arg.size()+1)*2;if(bytes>0x7fffffffu){set_last_error(8);return 0;}uint32_t base=heap.alloc(uint32_t(bytes?bytes:4)),strings=base+uint32_t(args.size()*4);for(size_t n=0;n<args.size();++n){uint32_t p=strings;for(size_t j=0;j<n;++j)p+=uint32_t((args[j].size()+1)*2);mem.w32(base+uint32_t(n*4),p);for(size_t j=0;j<args[n].size();++j)mem.w(p+uint32_t(j*2),args[n][j],2);mem.w(p+uint32_t(args[n].size()*2),0,2);}mem.w32(c.arg(1),uint32_t(args.size()));return base;
    });
    auto shell_file_operation=[this](Cpu& c,bool wide)->uint32_t {
        uint32_t op=c.arg(0);if(!op){set_last_error(87);return 87;}uint32_t func=mem.r32(op+4),from=mem.r32(op+8),to=mem.r32(op+12);if(!from){set_last_error(87);return 87;}if(func<1||func>4){set_last_error(120);return 120;}mem.w32(op+20,0);
        auto check_list=[&](uint32_t p)->bool {
            uint32_t cursor=p;
            for(unsigned item=0;item<4096;++item){
                std::u16string value;
                if(wide){
                    for(unsigned i=0;i<1u<<20;++i){uint16_t ch=uint16_t(mem.r(cursor+i*2,2));if(!ch)break;value.push_back(char16_t(ch));}
                    cursor+=uint32_t((value.size()+1)*2);
                }else{
                    for(unsigned i=0;i<1u<<20;++i){uint8_t ch=mem.r8(cursor+i);if(!ch)break;value.push_back(char16_t(ch));}
                    cursor+=uint32_t(value.size()+1);
                }
                if(value.empty())return true;
                if(!host.destructive_file_guard||!host.destructive_file_guard("SHFileOperation",value))return false;
            }
            return false;
        };
        if(!check_list(from)||(to&&!check_list(to))){mem.w32(op+20,1);set_last_error(5);return 5;}set_last_error(120);return 120;
    };
    reg.add("shell32.dll","SHFileOperationA",Conv::Stdcall,1,[shell_file_operation](Cpu& c){return shell_file_operation(c,false);});
    reg.add("shell32.dll","SHFileOperationW",Conv::Stdcall,1,[shell_file_operation](Cpu& c){return shell_file_operation(c,true);});

    // kernel32: process, error, memory, sync, TLS
    reg.add("kernel32.dll", "GetLastError", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return last_error(); });
    reg.add("kernel32.dll", "SetLastError", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { set_last_error(c.arg(0)); return 0; });
    reg.add("kernel32.dll", "GetCurrentProcess", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0xFFFFFFFFu; });
    reg.add("kernel32.dll", "GetCurrentProcessId", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0x1000; });
    reg.add("kernel32.dll", "GetCurrentThread", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0xFFFFFFFEu; });
    // The thread id lives in the running thread's TEB (ClientId.UniqueThread).
    reg.add("kernel32.dll", "GetCurrentThreadId", Conv::Stdcall, 0, [](Cpu& c) -> uint32_t { return c.mem.r32(c.fs_base + 0x24); });
    // Kernel synchronization contracts: CreateEventA, CreateMutexA, CreateSemaphoreA,
    // Set/Reset/PulseEvent, ReleaseMutex, ReleaseSemaphore, WaitForSingleObject, CloseHandle.
    // https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createeventa
    // https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexa
    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createsemaphorea
    // https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-openeventa
    // https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-setevent
    // https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-resetevent
    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-pulseevent
    // https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-releasemutex
    // https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-releasesemaphore
    // https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject
    // https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-closehandle
    // Named objects are process-local; session namespaces and ACL enforcement are outside this model.
    // lpSecurityAttributes is accepted and ignored for this single-user guest process.
    // PulseEvent is documented as unreliable; with no queued waiters, set-then-reset leaves it nonsignaled.
    auto sync_current_thread = [](Cpu& c) { uint32_t id=c.fs_base?c.mem.r32(c.fs_base+0x24):0; return id?id:1u; };
    reg.add("kernel32.dll","CreateEventA",Conv::Stdcall,4,[this](Cpu& c)->uint32_t{return create_sync(SyncKind::Event,c.arg(3),false,c.arg(1)!=0,c.arg(2)!=0,0,0,0);});
    reg.add("kernel32.dll","CreateEventW",Conv::Stdcall,4,[this](Cpu& c)->uint32_t{return create_sync(SyncKind::Event,c.arg(3),true,c.arg(1)!=0,c.arg(2)!=0,0,0,0);});
    reg.add("kernel32.dll","CreateMutexA",Conv::Stdcall,3,[this,sync_current_thread](Cpu& c)->uint32_t{return create_sync(SyncKind::Mutex,c.arg(2),false,false,false,0,0,c.arg(1)?sync_current_thread(c):0);});
    reg.add("kernel32.dll","CreateSemaphoreA",Conv::Stdcall,4,[this](Cpu& c)->uint32_t{int32_t initial=int32_t(c.arg(1)),maximum=int32_t(c.arg(2));if(initial<0||maximum<=0||initial>maximum){set_last_error(87);return 0;}return create_sync(SyncKind::Semaphore,c.arg(3),false,false,false,initial,maximum,0);});
    reg.add("kernel32.dll","SetEvent",Conv::Stdcall,1,[this](Cpu& c)->uint32_t{auto h=sync_handles.find(c.arg(0));if(h==sync_handles.end()||sync_objects[h->second].kind!=SyncKind::Event){set_last_error(6);return 0;}auto& o=sync_objects[h->second];o.signaled=true;
#if defined(_WIN32)
        if(o.native_event)SetEvent(reinterpret_cast<HANDLE>(o.native_event));
#endif
        return 1;});
    reg.add("kernel32.dll","ResetEvent",Conv::Stdcall,1,[this](Cpu& c)->uint32_t{auto h=sync_handles.find(c.arg(0));if(h==sync_handles.end()||sync_objects[h->second].kind!=SyncKind::Event){set_last_error(6);return 0;}auto& o=sync_objects[h->second];o.signaled=false;
#if defined(_WIN32)
        if(o.native_event)ResetEvent(reinterpret_cast<HANDLE>(o.native_event));
#endif
        return 1;});
    reg.add("kernel32.dll","PulseEvent",Conv::Stdcall,1,[this](Cpu& c)->uint32_t{auto h=sync_handles.find(c.arg(0));if(h==sync_handles.end()||sync_objects[h->second].kind!=SyncKind::Event){set_last_error(6);return 0;}auto& o=sync_objects[h->second];o.signaled=false;
#if defined(_WIN32)
        using PulseEventFn=BOOL(WINAPI*)(HANDLE);
        static HMODULE base=LoadLibraryExW(L"kernelbase.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        static HMODULE k32=LoadLibraryExW(L"kernel32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto pulse=base?reinterpret_cast<PulseEventFn>(GetProcAddress(base,"PulseEvent")):nullptr;
        if(!pulse&&k32)pulse=reinterpret_cast<PulseEventFn>(GetProcAddress(k32,"PulseEvent"));
        if(o.native_event&&pulse)pulse(reinterpret_cast<HANDLE>(o.native_event));
#endif
        return 1;});
    reg.add("kernel32.dll","ReleaseMutex",Conv::Stdcall,1,[this,sync_current_thread](Cpu& c)->uint32_t{auto h=sync_handles.find(c.arg(0));if(h==sync_handles.end()||sync_objects[h->second].kind!=SyncKind::Mutex){set_last_error(6);return 0;}auto& o=sync_objects[h->second];if(o.owner!=sync_current_thread(c)||!o.recursion){set_last_error(288);return 0;}if(!--o.recursion)o.owner=0;return 1;});
    reg.add("kernel32.dll","ReleaseSemaphore",Conv::Stdcall,3,[this](Cpu& c)->uint32_t{auto h=sync_handles.find(c.arg(0));int32_t n=int32_t(c.arg(1));if(h==sync_handles.end()||sync_objects[h->second].kind!=SyncKind::Semaphore){set_last_error(6);return 0;}auto& o=sync_objects[h->second];if(n<=0){set_last_error(87);return 0;}if(uint32_t(n)>o.maximum-o.count){set_last_error(298);return 0;}if(c.arg(2))mem.w32(c.arg(2),o.count);o.count+=uint32_t(n);return 1;});
    reg.add("kernel32.dll","GetCommandLineA", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return cmdline_ptr; });
    reg.add("kernel32.dll", "GetCommandLineW", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return put_wstr(u"x"); });
    reg.add("kernel32.dll", "GetStartupInfoA", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        auto b = mem.read_bytes(startupinfo, 68); copy_out(c.arg(0), b.data(), 68); return 0; });
    // GetStartupInfoW (processthreadsapi.h): the same 68-byte block. Its string
    // members are NULL here, so the ANSI and wide layouts agree byte for byte.
    reg.add("kernel32.dll", "GetStartupInfoW", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        auto b = mem.read_bytes(startupinfo, 68); copy_out(c.arg(0), b.data(), 68); return 0; });
    reg.add("kernel32.dll", "GetEnvironmentStringsA", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return env_ptr; });
    reg.add("kernel32.dll", "GetEnvironmentStrings", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return env_ptr; });
    reg.add("kernel32.dll", "GetEnvironmentStringsW", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return env_w_ptr; });
    reg.add("kernel32.dll", "FreeEnvironmentStringsA", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "FreeEnvironmentStringsW", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    auto get_env=[this](Cpu& c,bool wide)->uint32_t { std::string name;if(wide){auto w=wstr(c.arg(0));for(char16_t ch:w)name.push_back(char(ch));}else name=str(c.arg(0));auto it=guest_environment.find(env_key(name));if(it==guest_environment.end()){set_last_error(203);return 0;}uint32_t cap=c.arg(2),dst=c.arg(1);if(wide){std::u16string value(it->second.begin(),it->second.end());if(!dst||!cap)return uint32_t(value.size()+1);if(cap<=value.size()){set_last_error(122);return uint32_t(value.size()+1);}for(size_t i=0;i<value.size();++i)mem.w(dst+uint32_t(i*2),value[i],2);mem.w(dst+uint32_t(value.size()*2),0,2);set_last_error(0);return uint32_t(value.size());}const auto& value=it->second;if(!dst||!cap)return uint32_t(value.size()+1);if(cap<=value.size()){set_last_error(122);return uint32_t(value.size()+1);}mem.write_bytes(dst,value.c_str(),uint32_t(value.size()+1));set_last_error(0);return uint32_t(value.size());};
    reg.add("kernel32.dll","GetEnvironmentVariableA",Conv::Stdcall,3,[get_env](Cpu& c){return get_env(c,false);});
    reg.add("kernel32.dll","GetEnvironmentVariableW",Conv::Stdcall,3,[get_env](Cpu& c){return get_env(c,true);});
    auto set_env=[this](Cpu& c,bool wide)->uint32_t {std::string name,value;if(wide){auto wn=wstr(c.arg(0));for(char16_t ch:wn)name.push_back(char(ch));}else name=str(c.arg(0));if(name.empty()||name.find('=')!=std::string::npos){set_last_error(87);return 0;}std::string key=env_key(name);if(!c.arg(1)){guest_environment.erase(key);guest_environment_names.erase(key);}else {if(wide){auto wv=wstr(c.arg(1));for(char16_t ch:wv)value.push_back(char(ch));}else value=str(c.arg(1));if(!guest_environment.count(key))guest_environment_names[key]=name;guest_environment[key]=value;}rebuild_environment();set_last_error(0);return 1;};
    reg.add("kernel32.dll","SetEnvironmentVariableA",Conv::Stdcall,2,[set_env](Cpu& c){return set_env(c,false);});
    reg.add("kernel32.dll","SetEnvironmentVariableW",Conv::Stdcall,2,[set_env](Cpu& c){return set_env(c,true);});
    reg.add("kernel32.dll", "GetModuleHandleA", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        std::string n = str(c.arg(0)); if (n.empty()) return image_base;
        return module_by_name(n, c.thunks); });
    reg.add("kernel32.dll", "DisableThreadLibraryCalls", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t module=c.arg(0); if(module==image_base||modules.end()!=std::find_if(modules.begin(),modules.end(),[module](const auto& entry){return entry.second==module;})) return 1;
        for(const auto& dll:dlls) if(dll.second.image.base==module) return 1;
        set_last_error(6); return 0;
    });
    // FreeLibrary decrements a guest DLL's loader reference count. When the
    // count reaches zero, drop the image mapping and invalidate DBT blocks whose
    // guest code may have pointed into the unloaded image.
    reg.add("kernel32.dll", "FreeLibrary", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        return free_library(c, c.arg(0)) ? 1u : 0u;
    });
    reg.add("kernel32.dll", "FreeLibraryAndExitThread", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        if(!in_thread_body)throw Fault(FaultKind::Unsupported,c.eip,c.arg(0),"FreeLibraryAndExitThread: only guest worker-thread exit is modeled");
        free_library(c, c.arg(0));
        thread_exit_code=c.arg(1);throw ThreadExit{};
    });
    reg.add("kernel32.dll", "DebugBreak", Conv::Stdcall, 0, [](Cpu& c) -> uint32_t {
        throw Fault(FaultKind::Unsupported,c.eip,c.eip,"DebugBreak: guest debugger/breakpoint delivery is unavailable");
    });
    // IsDBCSLeadByteEx: the code pages this surface answers (1252, 437, 65001)
    // are all single-byte or UTF-8, so no byte is a lead byte.
    reg.add("kernel32.dll", "IsDBCSLeadByteEx", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "IsDBCSLeadByte", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    // (hModule, lpFilename, nSize): arg(0) is the module handle, not the buffer.
    reg.add("kernel32.dll", "GetModuleFileNameA", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t buf = c.arg(1), n = c.arg(2); if (!buf || !n) return 0;
        uint32_t put = uint32_t(exe_path.size()) < n ? uint32_t(exe_path.size()) : n - 1;
        mem.write_bytes(buf, exe_path.data(), put); mem.w8(buf + put, 0); return put; });
    reg.add("kernel32.dll", "GetModuleFileNameW", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t buf = c.arg(1), n = c.arg(2); if (!buf || !n) return 0;
        std::u16string w(exe_path.begin(), exe_path.end());
        uint32_t put = uint32_t(w.size()) < n ? uint32_t(w.size()) : n - 1;
        for (uint32_t i = 0; i < put; ++i) { mem.w8(buf + i * 2, uint8_t(w[i])); mem.w8(buf + i * 2 + 1, uint8_t(w[i] >> 8)); }
        mem.w8(buf + put * 2, 0); mem.w8(buf + put * 2 + 1, 0); return put; });
    reg.add("kernel32.dll", "GetFullPathNameW", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t {
#if defined(_WIN32)
        using Fn = DWORD (WINAPI*)(LPCWSTR, DWORD, LPWSTR, LPWSTR*);
        static HMODULE base = LoadLibraryExW(L"kernelbase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        static HMODULE k32 = LoadLibraryExW(L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        FARPROC proc = base ? GetProcAddress(base, "GetFullPathNameW") : nullptr;
        if (!proc && k32) proc = GetProcAddress(k32, "GetFullPathNameW");
        if (!proc) { set_last_error(ERROR_CALL_NOT_IMPLEMENTED); return 0; }
        std::u16string input = wstr(c.arg(0)); input.push_back(0);
        uint32_t cap = c.arg(1), out = c.arg(2);
        std::vector<wchar_t> path(cap);
        wchar_t* part = nullptr;
        DWORD result = reinterpret_cast<Fn>(proc)(reinterpret_cast<LPCWSTR>(input.data()), cap,
            path.empty() ? nullptr : path.data(), c.arg(3) ? &part : nullptr);
        if (result && result < cap && out) {
            for (uint32_t i = 0; i <= result; ++i) mem.w(out + i * 2, uint16_t(path[i]), 2);
            if (c.arg(3)) mem.w32(c.arg(3), out + uint32_t(part - path.data()) * 2);
        }
        return result;
#else
        Host::FileCall q{Host::FileCall::Kind::GetCurrentDirectoryW}; q.args[0] = 32768;
        if (!call_file(q) || q.data.size() < 2) { set_last_error(q.error ? q.error : 120); return 0; }
        std::u16string cwd;
        for (size_t i = 0; i + 1 < q.data.size(); i += 2) {
            uint16_t ch = uint16_t(q.data[i]) | uint16_t(q.data[i + 1]) << 8;
            if (!ch) break;
            cwd.push_back(char16_t(ch));
        }
        std::u16string input = wstr(c.arg(0));
        bool absolute = input.size() >= 3 && ((input[0] >= u'A' && input[0] <= u'Z') || (input[0] >= u'a' && input[0] <= u'z')) && input[1] == u':' && (input[2] == u'\\' || input[2] == u'/');
        std::u16string full = absolute ? input : cwd + u"\\" + input;
        for (char16_t& ch : full) if (ch == u'/') ch = u'\\';
        std::vector<std::u16string> parts;
        size_t start = full.find(u':'); start = start == std::u16string::npos ? 0 : start + 2;
        for (size_t i = start; i <= full.size();) {
            size_t end = full.find(u'\\', i);
            if (end == std::u16string::npos) end = full.size();
            std::u16string part = full.substr(i, end - i);
            if (part == u"..") { if (!parts.empty()) parts.pop_back(); }
            else if (!part.empty() && part != u".") parts.push_back(part);
            if (end == full.size()) break;
            i = end + 1;
        }
        std::u16string normalized;
        if (full.size() >= 2 && full[1] == u':') normalized = full.substr(0, 2) + u"\\";
        else normalized = u"\\";
        for (size_t i = 0; i < parts.size(); ++i) { if (i) normalized.push_back(u'\\'); normalized += parts[i]; }
        uint32_t cap = c.arg(1), out = c.arg(2), need = uint32_t(normalized.size() + 1);
        if (!cap || cap <= normalized.size() || !out) return need;
        for (size_t i = 0; i < normalized.size(); ++i) mem.w(out + uint32_t(i * 2), normalized[i], 2);
        mem.w(out + uint32_t(normalized.size() * 2), 0, 2);
        if (c.arg(3)) {
            size_t slash = normalized.find_last_of(u'\\');
            mem.w32(c.arg(3), out + uint32_t((slash == std::u16string::npos ? 0 : slash + 1) * 2));
        }
        set_last_error(0); return uint32_t(normalized.size());
#endif
    });
    reg.add("kernel32.dll", "ExitProcess", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { do_exit(c, c.arg(0)); return 0; });
    reg.add("kernel32.dll", "TerminateProcess", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { do_exit(c, c.arg(1)); return 1; });
    // Yield points of the cooperative scheduler: run pending bodies, then wait
    // for the remainder of the requested real-time interval. A worker can consume
    // part of the caller's interval while it runs (Bugbot 4561794148).
    auto guest_sleep = [this](Cpu& c) -> uint32_t {
        uint32_t timeout=c.arg(0); auto deadline=Clock::now()+std::chrono::milliseconds(timeout);
        run_pending_threads(c); if(timeout) sleep_until(deadline); return 0;
    };
    reg.add("kernel32.dll", "Sleep", Conv::Stdcall, 1, guest_sleep);
    reg.add("kernel32.dll", "SleepEx", Conv::Stdcall, 2, [guest_sleep](Cpu& c) -> uint32_t {
        guest_sleep(c); return 0; });  // 0: not an alertable wakeup
    // Threads: the surface runs one guest thread per CPU, so a thread body
    // cannot run inside CreateThread — mcfgthread's creator->thread handshake
    // (__MCF_event_await_change on the new thread's `ready` flag) spins forever
    // there. Instead the (start, param) pair is recorded and each body runs at
    // the guest's first blocking wait (keyed event / single object), with the
    // TEB TLS slots and SEH chain saved and restored around it: a real thread
    // has its own, and the synchronous model shares one TEB.
    reg.add("kernel32.dll", "CreateThread", Conv::Stdcall, 6, [this](Cpu& c) -> uint32_t {
        uint32_t handle = 0x60000000u + ++thread_serial;
        // lpThreadId receives the thread identifier (MSDN CreateThread; a DWORD
        // that identifies the thread while it lives). It must be nonzero and
        // unique among threads, and GetThreadId answers the same value.
        uint32_t id = 0x2000u + 4u * thread_serial;
        thread_ids[handle] = id;
        pending_threads.push_back({handle, c.arg(2), c.arg(3)});
        if (c.arg(5)) mem.w32(c.arg(5), id);
        return handle; });
    // ExitThread ends only the calling thread (winbase.h: "Ends the calling
    // thread"): a body that calls it is unwound to run_pending_threads (the
    // ThreadExit exception) and the process keeps going (Bugbot 4559267305) —
    // do_exit here would end the whole process.
    reg.add("kernel32.dll", "ExitThread", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        thread_exit_code = c.arg(0);
        if (in_thread_body) throw ThreadExit{};
        return thread_exit_code; });
    // SwitchToThread: nonzero when another thread ran (MSDN), i.e. bodies were pending.
    reg.add("kernel32.dll", "SwitchToThread", Conv::Stdcall, 0, [this](Cpu& c) -> uint32_t {
        bool ran = !pending_threads.empty(); run_pending_threads(c); return ran ? 1u : 0u; });
    // Timed waits share the same deadline accounting as Sleep: pending guest
    // bodies run first, and only the unconsumed part of the timeout is waited.
    reg.add("kernel32.dll", "WaitForSingleObject", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t h=c.arg(0), timeout=c.arg(1); auto deadline=timeout==0xffffffffu?Clock::time_point{}:Clock::now()+std::chrono::milliseconds(timeout);
        run_pending_threads(c);
        if(thread_ids.count(h)||thread_exit_codes.count(h))return 0;
        if(!sync_handles.count(h)){set_last_error(6);return 0xffffffffu;}
        uint32_t result=sync_wait(c,h,remaining_timeout(deadline,timeout));if(result==0xffffffffu)set_last_error(6);return result; });
    reg.add("kernel32.dll", "WaitForSingleObjectEx", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t h=c.arg(0), timeout=c.arg(1); auto deadline=timeout==0xffffffffu?Clock::time_point{}:Clock::now()+std::chrono::milliseconds(timeout);
        run_pending_threads(c);
        if(thread_ids.count(h)||thread_exit_codes.count(h))return 0;
        if(!sync_handles.count(h)){set_last_error(6);return 0xffffffffu;}
        uint32_t result=sync_wait(c,h,remaining_timeout(deadline,timeout));if(result==0xffffffffu)set_last_error(6);return result; });
    auto wait_multiple = [this](Cpu& c, uint32_t timeout, bool wait_all, bool allow_empty=false) -> uint32_t {
        return sync_wait_multiple(c,c.arg(0),c.arg(1),wait_all,timeout,allow_empty);
    };
    reg.add("kernel32.dll", "WaitForMultipleObjects", Conv::Stdcall, 4, [wait_multiple](Cpu& c) -> uint32_t {
        return wait_multiple(c,c.arg(3),c.arg(2)!=0); });
    reg.add("kernel32.dll", "WaitForMultipleObjectsEx", Conv::Stdcall, 5, [wait_multiple](Cpu& c) -> uint32_t {
        return wait_multiple(c,c.arg(3),c.arg(2)!=0); });
    reg.add("user32.dll", "MsgWaitForMultipleObjects", Conv::Stdcall, 5, [wait_multiple](Cpu& c) -> uint32_t {
        return wait_multiple(c,c.arg(3),false,true); });
    reg.add("user32.dll", "MsgWaitForMultipleObjectsEx", Conv::Stdcall, 5, [wait_multiple](Cpu& c) -> uint32_t {
        return wait_multiple(c,c.arg(2),(c.arg(4)&1u)!=0,true); });
    reg.add("kernel32.dll", "GetExitCodeThread", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        // A poll is a yield point: pending bodies run before the code is read.
        run_pending_threads(c);
        // Thread bodies run to completion before any wait returns, so the exit
        // code is the body's own: its return value or its ExitThread code, both
        // recorded by run_pending_threads (STILL_ACTIVE is 259, the documented
        // answer for a running thread, used only for a handle with no body run).
        if (c.arg(1)) mem.w32(c.arg(1), thread_exit_codes.count(c.arg(0)) ? thread_exit_codes[c.arg(0)] : 259u);
        return 1; });
    reg.add("kernel32.dll", "ResumeThread", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });  // was not suspended
    // The identifier of a thread this surface created. Any other handle (the
    // GetCurrentThread pseudo handle) answers the running thread's own id.
    reg.add("kernel32.dll", "GetThreadId", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        auto it = thread_ids.find(c.arg(0));
        return it != thread_ids.end() ? it->second : c.mem.r32(c.fs_base + 0x24); });
    reg.add("kernel32.dll", "DuplicateHandle", Conv::Stdcall, 7, [this](Cpu& c) -> uint32_t {
        // Preserve legacy identity semantics for other modeled handles. Sync handles
        // share the process handle namespace and receive an independently closable value.
        auto it=sync_handles.find(c.arg(1));
        if(it!=sync_handles.end()) { if(!c.arg(3)){set_last_error(87);return 0;} mem.w32(c.arg(3),add_sync_handle(it->second)); }
        else if (c.arg(3)) mem.w32(c.arg(3), c.arg(1));
        return 1; });
    // Condition variables: single-threaded surface — init zero-fills, wakes are
    // no-ops, and a sleep runs pending thread bodies then succeeds.
    reg.add("kernel32.dll", "InitializeConditionVariable", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        if (c.arg(0)) mem.w32(c.arg(0), 0);
        return 1; });
    reg.add("kernel32.dll", "WakeConditionVariable", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "WakeAllConditionVariable", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "SleepConditionVariableCS", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        run_pending_threads(c); return 1; });  // nonzero: woken, not timed out
    // GetTimeZoneInformation: UTC surface — bias 0, no standard/daylight names,
    // TIME_ZONE_ID_UNKNOWN (0), as a system set to UTC answers.
    reg.add("kernel32.dll", "GetTimeZoneInformation", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (p) mem.write_bytes(p, std::vector<uint8_t>(172, 0).data(), 172);
        return 0; });
    reg.add("kernel32.dll", "GetCurrentProcessorNumberEx", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (p) { mem.w(p, 0, 2); mem.w(p + 2, 0, 2); } return 0; });
    // Thread affinity/priority: one CPU, normal priority, affinity mask 1.
    // BOOL GetProcessAffinityMask(HANDLE, PDWORD, PDWORD): process and system
    // masks are the 2nd/3rd parameters (winbase.h), not the handle.
    reg.add("kernel32.dll", "GetProcessAffinityMask", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        if (c.arg(1)) { mem.w32(c.arg(1), 1); }
        if (c.arg(2)) { mem.w32(c.arg(2), 1); }
        return 1; });
    reg.add("kernel32.dll", "SetThreadAffinityMask", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { return c.arg(1) ? 1u : 0u; });
    reg.add("kernel32.dll", "GetThreadGroupAffinity", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(1); if (p) { mem.w32(p, 1); mem.w32(p + 4, 0); mem.w(p + 8, 0, 2); } return 1; });
    reg.add("kernel32.dll", "GetThreadPriority", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "SetThreadPriority", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 1; });
    // EncodePointer/DecodePointer (and the System variants): VC/UCRT startup
    // encodes every function pointer it stores and decodes it on use, so the
    // pair must be a real inverse or a decoded pointer is garbage. The key is
    // per-process ("random-looking" to the guest) and the encoding is XOR, so
    // decode(encode(p)) == p and encode(NULL) == decode(NULL) == NULL hold
    // exactly as the documented invariants require (Bugbot 4559749432).
    reg.add("kernel32.dll", "EncodePointer", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { return encode_ptr(c.arg(0)); });
    reg.add("kernel32.dll", "DecodePointer", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { return encode_ptr(c.arg(0)); });
    reg.add("kernel32.dll", "EncodeSystemPointer", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { return encode_ptr(c.arg(0)); });
    reg.add("kernel32.dll", "DecodeSystemPointer", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { return encode_ptr(c.arg(0)); });
    // FormatMessage: no message tables; a caller must handle the failure, as
    // on Windows when no message is found.
    reg.add("kernel32.dll", "FormatMessageA", Conv::Stdcall, 7, [this](Cpu&) -> uint32_t { set_last_error(15100); return 0; });
    reg.add("kernel32.dll", "FormatMessageW", Conv::Stdcall, 7, [this](Cpu&) -> uint32_t { set_last_error(15100); return 0; });
    // File info / module enumeration used by C++ runtimes' diagnostics paths:
    // answer "not available" the way the API does on a real failure.
    reg.add("kernel32.dll", "GetFileInformationByHandle", Conv::Stdcall, 2, [this](Cpu&) -> uint32_t { set_last_error(6); return 0; });
    reg.add("kernel32.dll", "K32EnumProcessModules", Conv::Stdcall, 4, [this](Cpu&) -> uint32_t { set_last_error(6); return 0; });
    reg.add("kernel32.dll", "GetLogicalProcessorInformationEx", Conv::Stdcall, 3, [this](Cpu&) -> uint32_t { set_last_error(122); return 0; });
    // BOOL GetModuleHandleExW(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE *phModule)
    // (libsrc/winbase.h): three parameters — flags, name, out handle. The
    // documented flag contract (Bugbot 4559749437): NULL name means the main
    // module (the exe's base); GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS (0x4)
    // treats lpModuleName as an address and answers the module containing it;
    // UNCHANGED_REFCOUNT (0x2) and PIN (0x1) only affect refcounting, which
    // this surface's FreeLibrary never unmaps anyway. A lookup that misses
    // writes NULL and fails, as Windows does.
    reg.add("kernel32.dll", "GetModuleHandleExW", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t flags = c.arg(0), p = c.arg(1), out = c.arg(2);
        uint32_t base = 0;
        if (flags & 4) {  // FROM_ADDRESS: find the module containing the address
            if (p >= image_base && p < image_base + 0x1000000u) base = image_base;
            else for (auto& kv : dlls) {
                if (p >= kv.second.image.base && p < kv.second.image.base + kv.second.image.size) { base = kv.second.image.base; break; }
            }
        } else if (!p) {  // NULL name: the main module
            base = image_base;
        } else {
            std::string n;
            for (uint32_t i = 0;; ++i) { uint16_t w = uint16_t(mem.r(p + i * 2, 2)); if (!w) break; n.push_back(char(w & 0xff)); }
            base = module_by_name(n, c.thunks);
        }
        if (out) mem.w32(out, base);
        if (!base) set_last_error(126);  // ERROR_MOD_NOT_FOUND, as Windows answers
        return base ? 1 : 0;
    });
    reg.add("kernel32.dll", "GetModuleHandleExA", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t flags=c.arg(0), p=c.arg(1), out=c.arg(2), base=0;
        if(flags&4){
            if(p>=image_base&&p<image_base+0x1000000u)base=image_base;
            else for(const auto& kv:dlls)if(p>=kv.second.image.base&&p<kv.second.image.base+kv.second.image.size){base=kv.second.image.base;break;}
        }else if(!p)base=image_base;
        else base=module_by_name(str(p),c.thunks);
        if(out)mem.w32(out,base);
        if(!base)set_last_error(126);
        return base?1u:0u;
    });
    // Time/clock queries the C++ runtime's once-init and this_thread use.
    reg.add("kernel32.dll", "QueryUnbiasedInterruptTime", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (p) { mem.w32(p, 0x00200000u); mem.w32(p + 4, 0); } return 1; });
    // RtlUnwind walks the live FS:[0] registration chain and invokes each
    // language handler with unwind flags before transferring to the target IP.
    // The x86 record/CONTEXT/dispatcher blocks live in checked guest memory, and
    // unsupported handler dispositions fail loudly rather than skipping frames.
    auto rtl_unwind = [this](Cpu& c) -> uint32_t {
        uint32_t target=c.arg(0), target_ip=c.arg(1), record=c.arg(2), return_value=c.arg(3);
        if(!c.fs_base||!target_ip||!target||target==0xffffffffu)
            throw Fault(FaultKind::Unsupported,c.eip,target,"RtlUnwind: requires a non-null target frame and instruction pointer");
        std::vector<std::pair<uint32_t,uint32_t>> frames; uint32_t frame=mem.r32(c.fs_base);
        bool found=false;
        for(unsigned count=0;count<1024&&frame!=0xffffffffu;++count){
            for(const auto& seen:frames)if(seen.first==frame)throw Fault(FaultKind::Unsupported,c.eip,frame,"RtlUnwind: cyclic SEH registration chain");
            uint32_t next=mem.r32(frame),handler=mem.r32(frame+4);if(!handler)throw Fault(FaultKind::Unsupported,c.eip,frame,"RtlUnwind: null SEH handler");
            frames.push_back({frame,handler});if(frame==target){found=true;break;}frame=next;
        }
        if(!found)throw Fault(FaultKind::Unsupported,c.eip,target,"RtlUnwind: target frame is not on the guest SEH chain");
        if(!record){record=heap.alloc(80);std::vector<uint8_t> zero_record(80);mem.write_bytes(record,zero_record.data(),80);mem.w32(record,0xC0000027u);}
        uint32_t context=heap.alloc(716),dispatcher=heap.alloc(36);
        std::vector<uint8_t> zero_context(716),zero_dispatcher(36);mem.write_bytes(context,zero_context.data(),716);mem.write_bytes(dispatcher,zero_dispatcher.data(),36);
        mem.w32(context,0x00010007u);mem.w32(context+156,c.reg[EDI]);mem.w32(context+160,c.reg[ESI]);mem.w32(context+164,c.reg[EBX]);
        mem.w32(context+168,c.reg[EDX]);mem.w32(context+172,c.reg[ECX]);mem.w32(context+176,c.reg[EAX]);mem.w32(context+180,c.reg[EBP]);
        mem.w32(context+184,c.eip);mem.w32(context+192,c.eflags);mem.w32(context+196,c.reg[ESP]);
        uint32_t prior_flags=mem.r32(record+4);
        for(const auto& entry:frames){
            uint32_t current=entry.first,handler=entry.second,next=mem.r32(current);
            if(mem.r32(c.fs_base)!=current)throw Fault(FaultKind::Unsupported,c.eip,current,"RtlUnwind: handler changed the SEH chain (collided unwind unsupported)");
            mem.w32(record+4,prior_flags|2u|(current==target?0x20u:0u));
            mem.w32(dispatcher,c.eip);mem.w32(dispatcher+4,image_base);mem.w32(dispatcher+8,0);mem.w32(dispatcher+12,current);
            mem.w32(dispatcher+16,target_ip);mem.w32(dispatcher+20,context);mem.w32(dispatcher+24,handler);mem.w32(dispatcher+28,0);mem.w32(dispatcher+32,0);
            uint32_t disposition=c.call(handler,{record,current,context,dispatcher});
            if(disposition!=1u)throw Fault(FaultKind::Unsupported,c.eip,current,"RtlUnwind: handler disposition is not CONTINUE_SEARCH");
            if(current!=target)mem.w32(c.fs_base,next);
        }
        mem.w32(c.fs_base,target);c.reg[EAX]=return_value;c.resume_at(target_ip,target);return 0;
    };
    reg.add("kernel32.dll","RtlUnwind",Conv::Stdcall,4,rtl_unwind);
    reg.add("ntdll.dll","RtlUnwind",Conv::Stdcall,4,rtl_unwind);
    // RaiseFailFastException: a fatal runtime error (fail-fast); end the run
    // with the crash exit code instead of continuing with broken state.
    reg.add("kernel32.dll", "RaiseFailFastException", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t { do_exit(c, 0x0DEAD10Cu); return 0; });
    // Console output: WriteConsoleW is the wide path WriteFile takes for a
    // console handle; the UTF-16 buffer is narrowed (the surface's output is
    // ASCII, as the module names it serves are).
    reg.add("kernel32.dll", "WriteConsoleW", Conv::Stdcall, 5, [this](Cpu& c) -> uint32_t {
        uint32_t handle = c.arg(0), buf = c.arg(1), n = c.arg(2), written = c.arg(3);
        if (handle != Host::H_STDOUT && handle != Host::H_STDERR) { set_last_error(6); return 0; }
        std::string s;
        for (uint32_t i = 0; i < n; ++i) s.push_back(char(mem.r(buf + i * 2, 2) & 0xff));
        emit(handle == Host::H_STDOUT ? 1 : 2, s);
        if (written) mem.w32(written, n);
        return 1; });
    reg.add("kernel32.dll", "SetUnhandledExceptionFilter", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "UnhandledExceptionFilter", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "RaiseException", Conv::Stdcall, 4, [](Cpu& c) -> uint32_t {
        throw Fault(FaultKind::Unsupported, c.eip, c.arg(0), "RaiseException (guest SEH not implemented)"); });
    reg.add("kernel32.dll", "MulDiv", Conv::Stdcall, 3, [](Cpu& c) -> uint32_t {
        int64_t product=int64_t(int32_t(c.arg(0)))*int32_t(c.arg(1)); int32_t divisor=int32_t(c.arg(2));
        if(!divisor)return 0xffffffffu;
        bool negative=(product<0)!=(divisor<0);uint64_t numerator=uint64_t(product<0?-product:product);
        uint64_t denominator=uint64_t(divisor<0?-int64_t(divisor):divisor);
        uint64_t rounded=(numerator+denominator/2)/denominator;
        if(rounded>(negative?0x80000000ull:0x7fffffffull))return 0xffffffffu;
        return uint32_t(negative?-int64_t(rounded):int64_t(rounded));
    });
    // GetVersion packs major in the low byte and minor in the next: 10.0.19045.
    reg.add("kernel32.dll", "GetVersion", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0x4A65000Au; });
    reg.add("kernel32.dll", "GetVersionExA", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (!p) return 0;
        uint32_t cb = mem.r32(p); if (cb < 148) { set_last_error(122); return 0; }
        mem.w32(p + 4, 10); mem.w32(p + 8, 0); mem.w32(p + 12, 19045); mem.w32(p + 16, 2);
        // OSVERSIONINFOA: the five dwords, then szCSDVersion[128] at offset 20,
        // so the struct is 148 bytes. OSVERSIONINFOEXA appends wServicePackMajor,
        // wServicePackMinor, wSuiteMask, wProductType and wReserved (156 bytes).
        // Nothing is written past the size the caller declared.
        std::string cs = "Service Pack 0";
        mem.write_bytes(p + 20, cs.c_str(), uint32_t(cs.size() + 1));
        if (cb >= 156) {
            mem.w(p + 148, 0, 2);  // wServicePackMajor
            mem.w(p + 150, 0, 2);  // wServicePackMinor
            mem.w(p + 152, 0, 2);  // wSuiteMask
            mem.w(p + 154, 1, 2);  // wProductType: VER_NT_WORKSTATION
            mem.w8(p + 155, 0);    // wReserved
        }
        return 1; });
    reg.add("kernel32.dll", "GetVersionExW", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (!p) return 0;
        uint32_t cb = mem.r32(p); if (cb < 276) { set_last_error(122); return 0; }
        mem.w32(p + 4, 10); mem.w32(p + 8, 0); mem.w32(p + 12, 19045); mem.w32(p + 16, 2);
        const char* cs = "Service Pack 0";
        for (uint32_t i = 0; cs[i]; ++i) mem.w(p + 20 + i * 2, uint8_t(cs[i]), 2);
        mem.w(p + 20 + uint32_t(std::strlen(cs)) * 2, 0, 2);
        if (cb >= 284) {
            mem.w(p + 276, 0, 2); mem.w(p + 278, 0, 2); mem.w(p + 280, 0, 2);
            mem.w8(p + 282, 1); mem.w8(p + 283, 0);
        }
        return 1;
    });
    // Physical host memory is not a truthful proxy for the guest's virtual
    // address-space limits; expose the documented unsupported error instead.
    reg.add("kernel32.dll", "GlobalMemoryStatusEx", Conv::Stdcall, 1, [this](Cpu&) -> uint32_t { set_last_error(120); return 0; });
    auto format_locale = [this](bool date, Cpu& c) -> uint32_t {
        Host::LocaleCall q; q.date=date; q.locale=c.arg(0); q.flags=c.arg(1);
        q.has_system_time=c.arg(2)!=0; q.has_format=c.arg(3)!=0; q.has_output_buffer=c.arg(4)!=0; q.buffer_chars=int32_t(c.arg(5));
        if(q.has_system_time) for(unsigned i=0;i<8;++i) q.system_time[i]=uint16_t(mem.r(c.arg(2)+i*2,2));
        if(q.has_format) q.format=wstr(c.arg(3));
        if(!host.locale_call){set_last_error(120);return 0;}
        host.locale_call(q);
        if(!q.succeeded){set_last_error(q.error?q.error:87);return 0;}
        if(q.result>0&&c.arg(4)){if(q.data.size()!=size_t(q.result)*2){set_last_error(87);return 0;}mem.write_bytes(c.arg(4),q.data.data(),uint32_t(q.data.size()));}
        return uint32_t(q.result);
    };
    reg.add("kernel32.dll", "GetTimeFormatW", Conv::Stdcall, 6, [format_locale](Cpu& c) -> uint32_t { return format_locale(false,c); });
    reg.add("kernel32.dll", "GetDateFormatW", Conv::Stdcall, 6, [format_locale](Cpu& c) -> uint32_t { return format_locale(true,c); });
    reg.add("kernel32.dll", "IsProcessorFeaturePresent", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t {
        return c.arg(0) == 10 ? 1u : 0u; });  // PF_XMMI64_INSTRUCTIONS_AVAILABLE (SSE2)
    auto nls_input = [this](uint32_t src, int32_t length, uint32_t unit_size, Host::NlsRequest& request) {
        request.input_null = src == 0;
        request.input_length = length;
        if (!src || length < -1) return;
        if (length == -1) {
            if (unit_size == 1) {
                std::string value = str(src);
                request.input.assign(value.begin(), value.end()); request.input.push_back(0);
            } else {
                std::u16string value = wstr(src);
                request.input.resize((value.size() + 1) * 2);
                for (size_t i = 0; i < value.size(); ++i) { request.input[i * 2] = uint8_t(value[i]); request.input[i * 2 + 1] = uint8_t(value[i] >> 8); }
            }
        } else if (uint64_t(uint32_t(length)) * unit_size <= (1u << 24)) {
            request.input = mem.read_bytes(src, uint32_t(length) * unit_size);
        } else throw Fault(FaultKind::AccessViolation, 0, src, "NLS input exceeds copy limit");
    };
    reg.add("kernel32.dll", "IsValidCodePage", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t cp = c.arg(0);
        if (host.nls_call) {
            Host::NlsRequest request{}; request.operation = Host::NlsOperation::IsValidCodePage; request.code_page = cp;
            Host::NlsResponse response = host.nls_call(request); set_last_error(response.last_error); return response.result;
        }
        return cp == 20127 || cp == 28591 ? 1u : 0u;
    });
    reg.add("kernel32.dll", "GetACP", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 1252; });
    reg.add("kernel32.dll", "GetOEMCP", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 437; });
    reg.add("kernel32.dll", "GetCPInfo", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t cp = c.arg(0), p = c.arg(1); if (!p) { set_last_error(87); return 0; }
        if (host.nls_call) {
            Host::NlsRequest request{}; request.operation = Host::NlsOperation::GetCPInfo; request.code_page = cp;
            Host::NlsResponse response = host.nls_call(request); set_last_error(response.last_error);
            if (response.result && response.output.size() == 20) copy_out(p, response.output.data(), 20);
            return response.result;
        }
        mem.w32(p, cp == 65001 ? 65001u : 0u); mem.w32(p + 4, 1); mem.w8(p + 8, '?'); mem.w8(p + 9, 0);
        for (int i = 10; i < 20; ++i) mem.w8(p + uint32_t(i), 0);
        return 1; });
    reg.add("kernel32.dll", "MultiByteToWideChar", Conv::Stdcall, 6, [this, nls_input](Cpu& c) -> uint32_t {
        uint32_t cp = c.arg(0), flags = c.arg(1), src = c.arg(2), dst = c.arg(4), cap = c.arg(5);
        int32_t n = int32_t(c.arg(3));
        if (host.nls_call) {
            Host::NlsRequest request{}; request.operation = Host::NlsOperation::MultiByteToWideChar;
            request.code_page = cp; request.flags = flags; request.output_null = dst == 0; request.output_capacity = cap;
            nls_input(src, n, 1, request);
            Host::NlsResponse response = host.nls_call(request); set_last_error(response.last_error);
            if (response.result && dst && cap && response.output.size() == size_t(response.result) * 2) copy_out(dst, response.output.data(), uint32_t(response.output.size()));
            return response.result;
        }
        (void)cp; std::string s = uint32_t(n) == 0xFFFFFFFFu ? str(src) : std::string();
        if (uint32_t(n) != 0xFFFFFFFFu) { auto b = mem.read_bytes(src, uint32_t(n)); s.assign(b.begin(), b.end()); }
        if (!dst || !cap) return uint32_t(s.size() + 1);
        uint32_t put = uint32_t(s.size()) < cap ? uint32_t(s.size()) : cap - 1;
        for (uint32_t i = 0; i < put; ++i) { mem.w8(dst + i * 2, uint8_t(s[i])); mem.w8(dst + i * 2 + 1, 0); }
        if (put < cap) { mem.w8(dst + put * 2, 0); mem.w8(dst + put * 2 + 1, 0); }
        return put; });
    auto nls_output = [this](Host::NlsRequest& request, uint32_t output_ptr, uint32_t expected_bytes) {
        if (!host.nls_call) { set_last_error(87); return 0u; }
        Host::NlsResponse response = host.nls_call(request); set_last_error(response.last_error);
        const uint32_t bytes = expected_bytes == 0xFFFFFFFFu ? response.result * 2 : expected_bytes;
        if (response.result && output_ptr && response.output.size() == bytes)
            copy_out(output_ptr, response.output.data(), bytes);
        return response.result;
    };
    auto get_locale_info=[this](Cpu& c,Host::NlsOperation op)->uint32_t { Host::NlsRequest request{};request.operation=op;request.locale=c.arg(0);request.type=c.arg(1);request.output_capacity=c.arg(3);request.output_null=c.arg(2)==0;if(op==Host::NlsOperation::GetLocaleInfoEx){request.locale_name_null=c.arg(0)==0;if(!request.locale_name_null)request.locale_name=wstr(c.arg(0));}if(!host.nls_call){set_last_error(120);return 0;}Host::NlsResponse response=host.nls_call(request);set_last_error(response.last_error);uint32_t unit=op==Host::NlsOperation::GetLocaleInfoA?1u:2u;if(response.result&&c.arg(2)&&response.output.size()==size_t(response.result)*unit)copy_out(c.arg(2),response.output.data(),uint32_t(response.output.size()));return response.result;};
    reg.add("kernel32.dll","GetLocaleInfoA",Conv::Stdcall,4,[get_locale_info](Cpu& c){return get_locale_info(c,Host::NlsOperation::GetLocaleInfoA);});
    reg.add("kernel32.dll","GetLocaleInfoW",Conv::Stdcall,4,[get_locale_info](Cpu& c){return get_locale_info(c,Host::NlsOperation::GetLocaleInfoW);});
    reg.add("kernel32.dll","GetLocaleInfoEx",Conv::Stdcall,4,[get_locale_info](Cpu& c){return get_locale_info(c,Host::NlsOperation::GetLocaleInfoEx);});
    auto get_lcid = [this](Cpu&, const char* api) -> uint32_t {
#if defined(_WIN32)
        using Fn = LCID (WINAPI*)();
        static HMODULE k32 = LoadLibraryExW(L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        FARPROC proc = k32 ? GetProcAddress(k32, api) : nullptr;
        if (!proc) { set_last_error(ERROR_CALL_NOT_IMPLEMENTED); return 0; }
        set_last_error(0); return reinterpret_cast<Fn>(proc)();
#else
        (void)api; set_last_error(120); return 0;
#endif
    };
    reg.add("kernel32.dll","GetUserDefaultLCID",Conv::Stdcall,0,[get_lcid](Cpu& c)->uint32_t{return get_lcid(c,"GetUserDefaultLCID");});
    reg.add("kernel32.dll","GetSystemDefaultLCID",Conv::Stdcall,0,[get_lcid](Cpu& c)->uint32_t{return get_lcid(c,"GetSystemDefaultLCID");});
    reg.add("kernel32.dll","GetThreadLocale",Conv::Stdcall,0,[get_lcid](Cpu& c)->uint32_t{return get_lcid(c,"GetThreadLocale");});
    reg.add("kernel32.dll","GetUserDefaultLocaleName",Conv::Stdcall,2,[this](Cpu& c)->uint32_t {
        const uint32_t output=c.arg(0),capacity=c.arg(1);
        if(!output||!capacity){set_last_error(87);return 0;}
        if(!host.nls_call){set_last_error(120);return 0;}
        Host::NlsRequest request{};request.operation=Host::NlsOperation::GetUserDefaultLocaleName;
        request.output_capacity=capacity;request.output_null=false;
        Host::NlsResponse response=host.nls_call(request);set_last_error(response.last_error);
        if(response.result&&!response.output.empty()&&response.output.size()<=size_t(capacity)*2)
            copy_out(output,response.output.data(),uint32_t(response.output.size()));
        else if(response.result){set_last_error(87);return 0;}
        return response.result;
    });
    // Guest calls are serialized by the interpreter, matching the existing Interlocked* routes.
    auto slist_init=[this](Cpu& c)->uint32_t {uint32_t h=c.arg(0);mem.w32(h,0);mem.w32(h+4,0);return 0;};
    auto slist_push=[this](Cpu& c)->uint32_t {uint32_t h=c.arg(0),entry=c.arg(1),old=mem.r32(h);mem.w32(entry,old);uint32_t depth=uint32_t(mem.r(h+4,2)),seq=uint32_t(mem.r(h+6,2));mem.w32(h,entry);mem.w(h+4,depth==0xffff?depth:depth+1,2);mem.w(h+6,(seq+1)&0xffff,2);return old;};
    auto slist_pop=[this](Cpu& c)->uint32_t {uint32_t h=c.arg(0),old=mem.r32(h);if(!old)return 0;uint32_t next=mem.r32(old),depth=uint32_t(mem.r(h+4,2)),seq=uint32_t(mem.r(h+6,2));mem.w32(h,next);mem.w(h+4,depth?depth-1:0,2);mem.w(h+6,(seq+1)&0xffff,2);return old;};
    auto slist_flush=[this](Cpu& c)->uint32_t {uint32_t h=c.arg(0),old=mem.r32(h),seq=uint32_t(mem.r(h+6,2));mem.w32(h,0);mem.w(h+4,0,2);mem.w(h+6,(seq+1)&0xffff,2);return old;};
    auto add_slist=[&](const char* dll){reg.add(dll,"InitializeSListHead",Conv::Stdcall,1,slist_init);reg.add(dll,"RtlInitializeSListHead",Conv::Stdcall,1,slist_init);reg.add(dll,"InterlockedPushEntrySList",Conv::Stdcall,2,slist_push);reg.add(dll,"RtlInterlockedPushEntrySList",Conv::Stdcall,2,slist_push);reg.add(dll,"InterlockedPopEntrySList",Conv::Stdcall,1,slist_pop);reg.add(dll,"RtlInterlockedPopEntrySList",Conv::Stdcall,1,slist_pop);reg.add(dll,"InterlockedFlushSList",Conv::Stdcall,1,slist_flush);reg.add(dll,"RtlInterlockedFlushSList",Conv::Stdcall,1,slist_flush);reg.add(dll,"QueryDepthSList",Conv::Stdcall,1,[this](Cpu& c){return uint32_t(mem.r(c.arg(0)+4,2));});reg.add(dll,"RtlQueryDepthSList",Conv::Stdcall,1,[this](Cpu& c){return uint32_t(mem.r(c.arg(0)+4,2));});};
    add_slist("kernel32.dll");add_slist("ntdll.dll");
    auto is_bad_string_ptr = [this](Cpu& c, bool wide) -> uint32_t {
        uint32_t p = c.arg(0), limit = c.arg(1);
        if (!p) return 1;
        if (!limit) return 0;
        limit = std::min(limit, 1u << 20); // same bound as guest string reads
        try {
            for (uint32_t i = 0; i < limit; ++i)
                if (!(wide ? mem.r(p + i * 2, 2) : mem.r8(p + i))) return 0;
        } catch (const Fault&) { return 1; }
        return 1;
    };
    reg.add("kernel32.dll", "IsBadStringPtrA", Conv::Stdcall, 2, [is_bad_string_ptr](Cpu& c) { return is_bad_string_ptr(c, false); });
    reg.add("kernel32.dll", "IsBadStringPtrW", Conv::Stdcall, 2, [is_bad_string_ptr](Cpu& c) { return is_bad_string_ptr(c, true); });
    reg.add("kernel32.dll", "GetStringTypeA", Conv::Stdcall, 5, [this, nls_input, nls_output](Cpu& c) -> uint32_t {
        Host::NlsRequest request{}; request.operation = Host::NlsOperation::GetStringTypeA;
        request.locale = c.arg(0); request.type = c.arg(1); request.output_null = c.arg(4) == 0;
        nls_input(c.arg(2), int32_t(c.arg(3)), 1, request);
        const uint32_t chars = request.input_length == -1 ? uint32_t(request.input.size()) : uint32_t(request.input_length);
        request.output_capacity = chars;
        return nls_output(request, c.arg(4), chars * 2);
    });
    reg.add("kernel32.dll", "GetStringTypeW", Conv::Stdcall, 4, [this, nls_input, nls_output](Cpu& c) -> uint32_t {
        Host::NlsRequest request{}; request.operation = Host::NlsOperation::GetStringTypeW;
        request.type = c.arg(0); request.output_null = c.arg(3) == 0; nls_input(c.arg(1), int32_t(c.arg(2)), 2, request);
        const uint32_t chars = request.input_length == -1 ? uint32_t(request.input.size() / 2) : uint32_t(request.input_length);
        request.output_capacity = chars;
        return nls_output(request, c.arg(3), chars * 2);
    });
    reg.add("kernel32.dll", "GetStringTypeExW", Conv::Stdcall, 5, [this, nls_input, nls_output](Cpu& c) -> uint32_t {
        Host::NlsRequest request{}; request.operation = Host::NlsOperation::GetStringTypeExW;
        request.locale = c.arg(0); request.type = c.arg(1); request.output_null = c.arg(4) == 0;
        nls_input(c.arg(2), int32_t(c.arg(3)), 2, request);
        const uint32_t chars = request.input_length == -1 ? uint32_t(request.input.size() / 2) : uint32_t(request.input_length);
        request.output_capacity = chars;
        return nls_output(request, c.arg(4), chars * 2);
    });
    reg.add("kernel32.dll", "LCMapStringW", Conv::Stdcall, 6, [this, nls_input, nls_output](Cpu& c) -> uint32_t {
        Host::NlsRequest request{}; request.operation = Host::NlsOperation::LCMapStringW;
        request.locale = c.arg(0); request.flags = c.arg(1); request.output_capacity = c.arg(5); request.output_null = c.arg(4) == 0;
        nls_input(c.arg(2), int32_t(c.arg(3)), 2, request);
        return nls_output(request, c.arg(4), 0xFFFFFFFFu);
    });
    reg.add("kernel32.dll", "LCMapStringEx", Conv::Stdcall, 9, [this, nls_input, nls_output](Cpu& c) -> uint32_t {
        Host::NlsRequest request{}; request.operation = Host::NlsOperation::LCMapStringEx;
        request.flags = c.arg(1); request.output_capacity = c.arg(5); request.output_null = c.arg(4) == 0;
        request.locale_name_null = c.arg(0) == 0; if (!request.locale_name_null) request.locale_name = wstr(c.arg(0));
        nls_input(c.arg(2), int32_t(c.arg(3)), 2, request);
        return nls_output(request, c.arg(4), 0xFFFFFFFFu);
    });
    reg.add("kernel32.dll", "CompareStringW", Conv::Stdcall, 6, [this, nls_input, nls_output](Cpu& c) -> uint32_t {
        Host::NlsRequest request{}; request.operation = Host::NlsOperation::CompareStringW;
        request.locale = c.arg(0); request.flags = c.arg(1); nls_input(c.arg(2), int32_t(c.arg(3)), 2, request);
        Host::NlsRequest second{}; nls_input(c.arg(4), int32_t(c.arg(5)), 2, second);
        request.input2 = std::move(second.input); request.input2_null = second.input_null; request.input2_length = second.input_length;
        return nls_output(request, 0, 0);
    });
    reg.add("kernel32.dll", "CompareStringEx", Conv::Stdcall, 9, [this, nls_input, nls_output](Cpu& c) -> uint32_t {
        Host::NlsRequest request{}; request.operation = Host::NlsOperation::CompareStringEx;
        request.flags = c.arg(1); request.locale_name_null = c.arg(0) == 0;
        if (!request.locale_name_null) request.locale_name = wstr(c.arg(0));
        nls_input(c.arg(2), int32_t(c.arg(3)), 2, request);
        Host::NlsRequest second{}; nls_input(c.arg(4), int32_t(c.arg(5)), 2, second);
        request.input2 = std::move(second.input); request.input2_null = second.input_null; request.input2_length = second.input_length;
        return nls_output(request, 0, 0);
    });
    reg.add("kernel32.dll", "FoldStringW", Conv::Stdcall, 5, [this, nls_input, nls_output](Cpu& c) -> uint32_t {
        Host::NlsRequest request{}; request.operation = Host::NlsOperation::FoldStringW;
        request.flags = c.arg(0); request.output_capacity = c.arg(4); request.output_null = c.arg(3) == 0;
        nls_input(c.arg(1), int32_t(c.arg(2)), 2, request);
        return nls_output(request, c.arg(3), 0xFFFFFFFFu);
    });
    reg.add("kernel32.dll", "WideCharToMultiByte", Conv::Stdcall, 8, [this, nls_input](Cpu& c) -> uint32_t {
        uint32_t cp = c.arg(0), flags = c.arg(1), src = c.arg(2), dst = c.arg(4), cap = c.arg(5), def = c.arg(6), used = c.arg(7);
        int32_t n = int32_t(c.arg(3));
        if (host.nls_call) {
            Host::NlsRequest request{}; request.operation = Host::NlsOperation::WideCharToMultiByte;
            request.code_page = cp; request.flags = flags; request.output_null = dst == 0; request.output_capacity = cap; request.wants_used_default = used != 0;
            nls_input(src, n, 2, request);
            if (def) request.default_char = mem.read_bytes(def, 1);
            Host::NlsResponse response = host.nls_call(request); set_last_error(response.last_error);
            if (response.result && dst && cap && response.output.size() == response.result) copy_out(dst, response.output.data(), response.result);
            if (used) mem.w32(used, response.used_default ? 1u : 0u);
            return response.result;
        }
        std::string s;
        if (uint32_t(n) == 0xFFFFFFFFu) { for (uint32_t i = 0;; ++i) { uint16_t w = uint16_t(mem.r(i * 2 + src, 2)); if (!w) break; s.push_back(char(w & 0xff)); } }
        else for (uint32_t i = 0; i < uint32_t(n); ++i) s.push_back(char(mem.r(src + i * 2, 2) & 0xff));
        if (!dst || !cap) return uint32_t(s.size() + 1);
        uint32_t put = uint32_t(s.size()) < cap ? uint32_t(s.size()) : cap - 1;
        mem.write_bytes(dst, s.data(), put); if (put < cap) mem.w8(dst + put, 0);
        return put; });
    // Heap
    reg.add("kernel32.dll", "GetProcessHeap", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0x50000000u; });
    reg.add("kernel32.dll", "HeapCreate", Conv::Stdcall, 3, [this](Cpu&) -> uint32_t {
        uint32_t h = 0x50000001u + heap_serial++; heap_handles[h] = 0; return h; });
    reg.add("kernel32.dll", "HeapDestroy", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { heap_handles.erase(c.arg(0)); return 1; });
    reg.add("kernel32.dll", "HeapAlloc", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t n = c.arg(2); if (!n) n = 1;
        uint32_t p = heap.alloc(n);
        if (c.arg(1) & 8) { auto z = std::vector<uint8_t>(n, 0); mem.write_bytes(p, z.data(), n); }  // HEAP_ZERO_MEMORY
        return p; });
    reg.add("kernel32.dll", "HeapFree", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t { heap.release(c.arg(2)); return 1; });
    reg.add("kernel32.dll", "HeapReAlloc", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t { return heap.realloc(c.arg(2), c.arg(3)); });
    reg.add("kernel32.dll", "HeapSize", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t { return heap.size_of(c.arg(2)); });
    reg.add("kernel32.dll", "HeapSetInformation", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t {
        uint32_t h = c.arg(0), info = c.arg(1), buffer = c.arg(2), size = c.arg(3);
        if ((h != 0x50000000u && !heap_handles.count(h)) ||
            (info == 0 && (!buffer || size != 4)) ||
            (info == 1 && (buffer || size != 0)) || info > 1) { set_last_error(87); return 0; }
        if (info == 0) {
            uint32_t compatibility = mem.r32(buffer);
            if (compatibility != 0 && compatibility != 2) { set_last_error(87); return 0; }
        }
        set_last_error(0); return 1;
    });
    reg.add("kernel32.dll", "HeapQueryInformation", Conv::Stdcall, 5, [this](Cpu& c) -> uint32_t {
        uint32_t h = c.arg(0), info = c.arg(1), buffer = c.arg(2), size = c.arg(3), returned = c.arg(4);
        if ((h != 0x50000000u && !heap_handles.count(h)) || info != 0 || !buffer || size < 4) {
            set_last_error(87); return 0;
        }
        mem.w32(buffer, 2);  // HEAP_COMPATIBILITY_INFORMATION: LFH enabled
        if (returned) mem.w32(returned, 4);
        set_last_error(0); return 1;
    });
    reg.add("kernel32.dll", "LocalAlloc", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { return heap.alloc(c.arg(1)); });
    reg.add("kernel32.dll", "LocalFree", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { heap.release(c.arg(0)); return 0; });
    reg.add("kernel32.dll", "GlobalAlloc", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { return heap.alloc(c.arg(1)); });
    reg.add("kernel32.dll", "GlobalFree", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { heap.release(c.arg(0)); return 0; });
    // Virtual memory
    reg.add("kernel32.dll", "VirtualAlloc", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t {
        uint32_t want = (c.arg(1) + 0xfffu) & ~0xfffu; if (!want) want = 0x1000;
        uint32_t p = virtual_top; virtual_top += want;
        mem.map(p, want, PROT_R | PROT_W); virtual_alloc[p] = want;
        if (c.arg(3) & 0x40) { auto z = std::vector<uint8_t>(want, 0); mem.write_bytes(p, z.data(), want); }
        return p; });
    reg.add("kernel32.dll", "VirtualFree", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        auto it = virtual_alloc.find(c.arg(0)); if (it == virtual_alloc.end()) return 0;
        virtual_alloc.erase(it); return 1; });
    reg.add("kernel32.dll", "VirtualProtect", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t {
        uint32_t a = c.arg(0) & ~0xfffu, n = (c.arg(1) + 0xfffu) & ~0xfffu, prot = c.arg(2), old = c.arg(3);
        // Page-protection flags (winnt.h): 0x02 READONLY, 0x04 READWRITE,
        // 0x08 WRITECOPY, 0x10 EXECUTE, 0x20 EXECUTE_READ, 0x40
        // EXECUTE_READWRITE, 0x80 EXECUTE_WRITECOPY. mingw's
        // _mark_section_writable passes PAGE_EXECUTE_READWRITE (0x40): a
        // check for only 0x04/0x20 drops X and the return into .text faults.
        uint8_t p = PROT_R;
        if (prot & 0x04 || prot & 0x08 || prot & 0x40 || prot & 0x80) p |= PROT_W;
        if (prot & 0x10 || prot & 0x20 || prot & 0x40 || prot & 0x80) p |= PROT_X;
        if (!n) n = 0x1000;
        try { mem.protect(a, n, p); } catch (...) { set_last_error(487); return 0; }
        if (old) mem.w32(old, 0x04);
        return 1; });
    reg.add("kernel32.dll", "VirtualQuery", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t a = c.arg(0), p = c.arg(1); if (!p) return 0;
        uint32_t base = a & ~0xfffu, size = 0x1000;
        // The real per-page protection: callers (mingw's pseudo-relocator)
        // skip their own VirtualProtect when the answer says the page is
        // already writable, so a blanket READWRITE answer makes their next
        // write fault.
        uint8_t pr = mem.mapped(a) ? mem.protect_of(a) : 0;
        uint32_t win = 0x04;  // PAGE_READWRITE is the sane default
        if (!mem.mapped(a)) win = 0;  // free: State MEM_FREE below
        else if (!(pr & PROT_W)) win = (pr & PROT_X) ? 0x20 /*PAGE_EXECUTE_READ*/ : 0x02 /*PAGE_READONLY*/;
        else if (pr & PROT_X) win = 0x40 /*PAGE_EXECUTE_READWRITE*/;
        // AllocationProtect (mbi+8) must NOT read as already-fully-writable:
        // mingw's _mark_section_writable skips its VirtualProtect when this
        // field says EXECUTE_READWRITE, then its pseudo-reloc memcpy faults.
        // Real images allocate as writecopy (0x80); report that.
        // MEMORY_BASIC_INFORMATION32: BaseAddress(0) AllocationBase(4)
        // AllocationProtect(8) RegionSize(12) State(16) Protect(20) Type(24).
        mem.w32(p + 0, base); mem.w32(p + 4, base); mem.w32(p + 8, win ? 0x80u : 0u);
        mem.w32(p + 12, size);
        mem.w32(p + 16, mem.mapped(a) ? 0x1000u : 0x10000u);  // MEM_COMMIT / MEM_FREE
        mem.w32(p + 20, win);                                  // Protect: the real protection
        mem.w32(p + 24, 0x20000);                              // Type: MEM_PRIVATE
        return 28; });
    // Critical sections have no contention in the single-thread interpreter, but CRTs inspect their initialized fields.
    auto initialize_critical_section = [this](Cpu& c, uint32_t spin_count, uint32_t flags) -> uint32_t {
        uint32_t p = c.arg(0), debug = 0;
        if (!p) return 0;
        auto old = critical_section_debug.find(p);
        if (old != critical_section_debug.end()) { heap.release(old->second); critical_section_debug.erase(old); }
        if (!(flags & 0x01000000u) || (flags & 0x10000000u)) {
            debug = heap.alloc(0x20);
            for (uint32_t offset = 0; offset < 0x20; offset += 4) mem.w32(debug + offset, 0);
            mem.w32(debug + 4, p);
            mem.w32(debug + 8, debug + 8); mem.w32(debug + 12, debug + 8);
            critical_section_debug[p] = debug;
        }
        mem.w32(p, debug); mem.w32(p + 4, 0xffffffffu); mem.w32(p + 8, 0);
        mem.w32(p + 12, 0); mem.w32(p + 16, 0); mem.w32(p + 20, spin_count);
        return 1;
    };
    reg.add("kernel32.dll", "InitializeCriticalSection", Conv::Stdcall, 1,
            [initialize_critical_section](Cpu& c) { initialize_critical_section(c, 0, 0); return 0; });
    reg.add("kernel32.dll", "EnterCriticalSection", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "LeaveCriticalSection", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "DeleteCriticalSection", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); auto it = critical_section_debug.find(p);
        if (it != critical_section_debug.end()) { heap.release(it->second); critical_section_debug.erase(it); }
        if (p) mem.w32(p, 0);
        return 0;
    });
    reg.add("kernel32.dll", "InitializeCriticalSectionAndSpinCount", Conv::Stdcall, 2,
            [initialize_critical_section](Cpu& c) { return initialize_critical_section(c, c.arg(1), 0); });
    reg.add("kernel32.dll", "TryEnterCriticalSection", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "InitializeCriticalSectionEx", Conv::Stdcall, 3,
            [initialize_critical_section](Cpu& c) { return initialize_critical_section(c, c.arg(1), c.arg(2)); });
    auto acquire_srw = [this](Cpu& c, bool shared, bool attempt) -> uint32_t {
        uint32_t p = c.arg(0), tid = mem.r32(c.fs_base + 0x24), raw = mem.r32(p);
        auto it = srw_locks.find(p);
        if (it == srw_locks.end() && raw) throw Fault(FaultKind::Unsupported, c.eip, p, "SRW lock has an unsupported preinitialized state");
        SrwObject& lock = srw_locks[p];
        bool free = !lock.writer && (shared || lock.readers.empty());
        if (!free) {
            if (attempt) return 0;
            throw Fault(FaultKind::Unsupported, c.eip, p, "SRW lock contention would block in the single-thread interpreter");
        }
        if (shared) ++lock.readers[tid]; else lock.writer = tid;
        mem.w32(p, 1);
        return 1;
    };
    auto release_srw = [this](Cpu& c, bool shared) -> uint32_t {
        uint32_t p = c.arg(0), tid = mem.r32(c.fs_base + 0x24);
        auto it = srw_locks.find(p);
        if (it == srw_locks.end()) throw Fault(FaultKind::Unsupported, c.eip, p, "release of an unowned SRW lock");
        if (shared) {
            auto reader = it->second.readers.find(tid);
            if (reader == it->second.readers.end()) throw Fault(FaultKind::Unsupported, c.eip, p, "release of an unowned shared SRW lock");
            if (!--reader->second) it->second.readers.erase(reader);
        } else {
            if (it->second.writer != tid) throw Fault(FaultKind::Unsupported, c.eip, p, "release of an unowned exclusive SRW lock");
            it->second.writer = 0;
        }
        if (!it->second.writer && it->second.readers.empty()) { mem.w32(p, 0); srw_locks.erase(it); }
        return 0;
    };
    reg.add("kernel32.dll", "InitializeSRWLock", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        srw_locks.erase(c.arg(0)); mem.w32(c.arg(0), 0); return 0; });
    reg.add("kernel32.dll", "AcquireSRWLockExclusive", Conv::Stdcall, 1, [acquire_srw](Cpu& c) -> uint32_t { acquire_srw(c, false, false); return 0; });
    reg.add("kernel32.dll", "AcquireSRWLockShared", Conv::Stdcall, 1, [acquire_srw](Cpu& c) -> uint32_t { acquire_srw(c, true, false); return 0; });
    reg.add("kernel32.dll", "TryAcquireSRWLockExclusive", Conv::Stdcall, 1, [acquire_srw](Cpu& c) -> uint32_t { return acquire_srw(c, false, true); });
    reg.add("kernel32.dll", "TryAcquireSRWLockShared", Conv::Stdcall, 1, [acquire_srw](Cpu& c) -> uint32_t { return acquire_srw(c, true, true); });
    reg.add("kernel32.dll", "ReleaseSRWLockExclusive", Conv::Stdcall, 1, [release_srw](Cpu& c) -> uint32_t { return release_srw(c, false); });
    reg.add("kernel32.dll", "ReleaseSRWLockShared", Conv::Stdcall, 1, [release_srw](Cpu& c) -> uint32_t { return release_srw(c, true); });
    // OpenEventA opens a same-process named event; process/session namespaces and ACLs are outside this guest.
    reg.add("kernel32.dll", "OpenEventA", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t { return open_sync(SyncKind::Event,c.arg(2),false); });
    reg.add("kernel32.dll", "OpenEventW", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t { return open_sync(SyncKind::Event,c.arg(2),true); });
    // TLS
    // Windows semantics: TlsAlloc hands out the lowest free slot and zeroes it,
    // TlsFree releases it, TlsGetValue clears LastError on success (callers tell
    // a NULL value from failure that way). Slot 0 stays taken, as it is in any
    // real process by the time application code runs, because code that treats
    // index 0 as "not allocated yet" otherwise never stores its value.
    reg.add("kernel32.dll", "TlsAlloc", Conv::Stdcall, 0, [this](Cpu& c) -> uint32_t {
        for (uint32_t i = 1; i < 64; ++i) if (!(tls_used >> i & 1)) {
            tls_used |= uint64_t(1) << i; if (next_tls <= i) next_tls = i + 1;
            mem.w32(c.fs_base + 0xE10 + i * 4, 0); return i; }
        set_last_error(8); return 0xFFFFFFFFu; });
    reg.add("kernel32.dll", "TlsFree", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t i = c.arg(0); if (i == 0 || i >= 64 || !(tls_used >> i & 1)) { set_last_error(87); return 0; }
        tls_used &= ~(uint64_t(1) << i); mem.w32(c.fs_base + 0xE10 + i * 4, 0); return 1; });
    reg.add("kernel32.dll", "TlsGetValue", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t i = c.arg(0); if (i >= 64) { set_last_error(87); return 0; }
        set_last_error(0); return mem.r32(c.fs_base + 0xE10 + i * 4); });
    reg.add("kernel32.dll", "TlsSetValue", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t i = c.arg(0); if (i >= 64) { set_last_error(87); return 0; }
        mem.w32(c.fs_base + 0xE10 + i * 4, c.arg(1)); return 1; });
    // Fiber-local storage shares the TEB value slots with TLS on this single
    // interpreter thread; FlsFree invokes its registered guest callback.
    reg.add("kernel32.dll", "FlsAlloc", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        for (uint32_t i = 1; i < 64; ++i) if (!(tls_used >> i & 1)) {
            tls_used |= uint64_t(1) << i; if (next_tls <= i) next_tls = i + 1;
            mem.w32(c.fs_base + 0xE10 + i * 4, 0); fls_callbacks[i] = c.arg(0); return i; }
        set_last_error(8); return 0xFFFFFFFFu; });
    reg.add("kernel32.dll", "FlsGetValue", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t i = c.arg(0); if (!fls_callbacks.count(i)) { set_last_error(87); return 0; }
        set_last_error(0); return mem.r32(c.fs_base + 0xE10 + i * 4); });
    reg.add("kernel32.dll", "FlsSetValue", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t i = c.arg(0); if (!fls_callbacks.count(i)) { set_last_error(87); return 0; }
        mem.w32(c.fs_base + 0xE10 + i * 4, c.arg(1)); return 1; });
    reg.add("kernel32.dll", "FlsFree", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t i = c.arg(0); auto it = fls_callbacks.find(i);
        if (it == fls_callbacks.end()) { set_last_error(87); return 0; }
        uint32_t value = mem.r32(c.fs_base + 0xE10 + i * 4);
        if (value && it->second) c.call(it->second, {value});
        mem.w32(c.fs_base + 0xE10 + i * 4, 0); fls_callbacks.erase(it); tls_used &= ~(uint64_t(1) << i); return 1; });
    // Interlocked
    reg.add("kernel32.dll", "InterlockedIncrement", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t v = mem.r32(c.arg(0)) + 1; mem.w32(c.arg(0), v); return v; });
    reg.add("kernel32.dll", "InterlockedDecrement", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t v = mem.r32(c.arg(0)) - 1; mem.w32(c.arg(0), v); return v; });
    reg.add("kernel32.dll", "InterlockedExchange", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t old = mem.r32(c.arg(0)); mem.w32(c.arg(0), c.arg(1)); return old; });
    reg.add("kernel32.dll", "InterlockedCompareExchange", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t old = mem.r32(c.arg(0)); if (old == c.arg(2)) mem.w32(c.arg(0), c.arg(1)); return old; });
    reg.add("kernel32.dll", "InterlockedExchangeAdd", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t old = mem.r32(c.arg(0)); mem.w32(c.arg(0), old + c.arg(1)); return old; });
    // Time: the fallback clock is monotonic too, so off-device guests observe
    // the same elapsed real time as the scheduler and the Play host clock.
    auto tick_count = [this]() -> uint64_t { return tick_now(); };
    reg.add("kernel32.dll", "GetTickCount", Conv::Stdcall, 0, [tick_count](Cpu&) -> uint32_t { return uint32_t(tick_count()); });
    reg.add("kernel32.dll", "GetTickCount64", Conv::Stdcall, 0, [tick_count](Cpu& c) -> uint32_t { uint64_t t=tick_count();c.reg[EDX]=uint32_t(t>>32);return uint32_t(t); });
    reg.add("winmm.dll", "waveInGetNumDevs", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0; });
    reg.add("winmm.dll", "timeBeginPeriod", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t { uint32_t period=c.arg(0);return period>=1&&period<=1000000?0u:97u; });
    reg.add("winmm.dll", "timeEndPeriod", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t { uint32_t period=c.arg(0);return period>=1&&period<=1000000?0u:97u; });
    reg.add("winmm.dll", "timeGetTime", Conv::Stdcall, 0, [tick_count](Cpu&) -> uint32_t { return tick_count(); });
    reg.add("winmm.dll", "timeGetSystemTime", Conv::Stdcall, 2, [this, tick_count](Cpu& c) -> uint32_t {
        uint32_t out=c.arg(0), size=c.arg(1); if(!out||size<8)return 97;
        mem.w32(out,1); mem.w32(out+4,tick_count()); return 0; });
    reg.add("winmm.dll", "timeGetDevCaps", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t p=c.arg(0);if(!p||c.arg(1)!=8)return 129;mem.w32(p,1);mem.w32(p+4,1000000);return 0; });
    // The interpreter has no asynchronous callback scheduler, so timer IDs are
    // deterministic bookkeeping handles; callbacks are never fabricated.
    reg.add("winmm.dll", "timeSetEvent", Conv::Stdcall, 5, [this](Cpu& c) -> uint32_t {
        // Multimedia timer callbacks run on a winmm thread; the guest has no such
        // thread here, so creating a timer that would never fire must fail (NULL),
        // never hand back an ID the game then waits on.
        (void)c; return 0; });
    reg.add("winmm.dll", "timeKillEvent", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        return mm_timers.erase(c.arg(0)) ? 0u : 97u; });
    // No joystick or wave device is exposed by this host. Keep enumeration empty
    // and return the documented no-driver/unplugged errors for every device call.
    reg.add("winmm.dll", "joyGetNumDevs", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0; });
    reg.add("winmm.dll", "joyGetPos", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 167; });
    reg.add("winmm.dll", "joyGetPosEx", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 167; });
    reg.add("winmm.dll", "joyGetDevCapsA", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 167; });
    reg.add("winmm.dll", "joyGetDevCapsW", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 167; });
    reg.add("winmm.dll", "waveOutGetNumDevs", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0; });
    reg.add("winmm.dll", "waveInGetNumDevs", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0; });
    reg.add("winmm.dll", "waveOutOpen", Conv::Stdcall, 6, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveInOpen", Conv::Stdcall, 6, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveOutClose", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveInClose", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveOutPrepareHeader", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveOutUnprepareHeader", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveInPrepareHeader", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveInUnprepareHeader", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveOutWrite", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveInAddBuffer", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveInStart", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveInStop", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveInReset", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveOutPause", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveOutRestart", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveOutReset", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveOutGetPosition", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveInGetPosition", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveInGetDevCapsA", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveInGetDevCapsW", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveOutGetDevCapsA", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveOutGetDevCapsW", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveOutSetVolume", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveOutMessage", Conv::Stdcall, 4, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveInMessage", Conv::Stdcall, 4, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveOutGetErrorTextA", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveOutGetErrorTextW", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveInGetErrorTextA", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "waveInGetErrorTextW", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 6; });
    reg.add("winmm.dll", "PlaySoundA", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 0; });
    reg.add("winmm.dll", "PlaySoundW", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "QueryPerformanceCounter", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); uint64_t t = steady_micros(); if (p) { mem.w32(p, uint32_t(t)); mem.w32(p + 4, uint32_t(t >> 32)); } return 1; });
    reg.add("kernel32.dll", "QueryPerformanceFrequency", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (p) { mem.w32(p, 1000000); mem.w32(p + 4, 0); } return 1; });
    reg.add("kernel32.dll", "GetSystemTimeAsFileTime", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (p) { mem.w32(p, 0x80000000u); mem.w32(p + 4, 0x01D00000u); } return 0; });
    reg.add("bcrypt.dll", "BCryptGenRandom", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t {
        uint32_t out = c.arg(1), size = c.arg(2);
        if (c.arg(0) || c.arg(3) != 2 || size > (1u << 20) || (!out && size)) return 0xC000000Du; // STATUS_INVALID_PARAMETER
        std::vector<uint8_t> bytes(size);
#if defined(_WIN32)
        using Fn = LONG (WINAPI*)(void*, unsigned char*, unsigned long, unsigned long);
        static HMODULE module = LoadLibraryExW(L"bcrypt.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto fn = module ? reinterpret_cast<Fn>(GetProcAddress(module, "BCryptGenRandom")) : nullptr;
        if (!fn) return 0xC0000001u; // STATUS_UNSUCCESSFUL
        LONG status = fn(nullptr, bytes.empty() ? nullptr : bytes.data(), size, 2);
        if (status < 0) return uint32_t(status);
#else
        try {
            std::random_device random;
            for (size_t i = 0; i < bytes.size();) {
                unsigned value = random();
                for (size_t n = 0; n < sizeof(value) && i < bytes.size(); ++n, ++i)
                    bytes[i] = uint8_t(value >> (n * 8));
            }
        } catch (...) { return 0xC0000001u; } // STATUS_UNSUCCESSFUL
#endif
        if (size) mem.write_bytes(out, bytes.data(), size);
        return 0;
    });
    reg.add("kernel32.dll", "GetSystemTime", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        fill_system_time(c.arg(0)); return 0; });
    reg.add("kernel32.dll", "GetLocalTime", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        fill_system_time(c.arg(0)); return 0; });
    reg.add("kernel32.dll", "GetSystemInfo", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (!p) return 0;
        mem.w32(p + 0, 0); mem.w32(p + 4, 4096); mem.w32(p + 8, 0x10000); mem.w32(p + 12, 0x7FFE0000u);
        mem.w32(p + 16, 1); mem.w32(p + 20, 1); mem.w32(p + 24, 0); mem.w32(p + 28, 0x10000); mem.w32(p + 32, 9);
        return 0; });
    auto profile_text = [this](uint32_t file, bool wide, std::string& text) { uint32_t error=0;if(!profile_file(file,wide,text,false,error)){set_last_error(error?error:2);return false;}set_last_error(0);return true; };
    auto narrow = [this](uint32_t p){auto w=wstr(p);return std::string(w.begin(),w.end());};
    auto profile_string = [this,profile_text,narrow](Cpu& c,bool wide)->uint32_t {
        uint32_t sectionp=c.arg(0),keyp=c.arg(1),defaultp=c.arg(2),buffer=c.arg(3),capacity=c.arg(4),file=c.arg(5);
        std::string section=wide?narrow(sectionp):str(sectionp);
        std::string key=wide?narrow(keyp):str(keyp);
        std::string fallback=wide?narrow(defaultp):str(defaultp), contents;
        if(!capacity||!buffer){set_last_error(87);return 0;}
        if(!profile_text(file,wide,contents)) contents.clear();
        std::string value=profile_value(contents,section,key,fallback);
        uint32_t n=0;
        if(wide){uint32_t max=capacity-1; n=uint32_t(std::min<size_t>(value.size(),max));for(uint32_t i=0;i<n;++i)mem.w(buffer+i*2,uint8_t(value[i]),2);mem.w(buffer+n*2,0,2);}
        else{n=uint32_t(std::min<size_t>(value.size(),capacity-1));if(n)mem.write_bytes(buffer,value.data(),n);mem.w8(buffer+n,0);}
        return n;
    };
    auto profile_write=[this,profile_text,narrow](Cpu& c,bool wide)->uint32_t {
        uint32_t sp=c.arg(0),kp=c.arg(1),vp=c.arg(2),fp=c.arg(3);if(!fp){set_last_error(87);return 0;}
        std::string section=sp?(wide?narrow(sp):str(sp)):std::string(),key=kp?(wide?narrow(kp):str(kp)):std::string(),value=vp?(wide?narrow(vp):str(vp)):std::string(),contents;
        profile_text(fp,wide,contents);
        auto fold=[](std::string s){for(char& ch:s)ch=char(std::tolower((unsigned char)ch));return s;};
        std::vector<std::string> lines;for(size_t p=0;p<contents.size();){size_t e=contents.find_first_of("\r\n",p);if(e==std::string::npos)e=contents.size();if(e>p)lines.push_back(contents.substr(p,e-p));p=e+1;}
        if(!sp)lines.clear();
        else {
            size_t begin=lines.size(),end=lines.size();
            for(size_t i=0;i<lines.size();++i){std::string line=trim_profile(lines[i]);if(line.size()>=2&&line.front()=='['&&line.back()==']'){if(begin!=lines.size()){end=i;break;}if(fold(trim_profile(line.substr(1,line.size()-2)))==fold(section))begin=i;}}
            if(begin!=lines.size()&&end==lines.size())end=lines.size();
            if(!kp){if(begin!=lines.size())lines.erase(lines.begin()+long(begin),lines.begin()+long(end));}
            else if(begin==lines.size()){lines.push_back("["+section+"]");if(vp)lines.push_back(key+"="+value);}
            else {size_t match=end;for(size_t i=begin+1;i<end;++i){size_t eq=lines[i].find('=');if(eq!=std::string::npos&&fold(trim_profile(lines[i].substr(0,eq)))==fold(key)){match=i;break;}}
                if(!vp){if(match<end)lines.erase(lines.begin()+long(match));}
                else if(match<end)lines[match]=key+"="+value;else lines.insert(lines.begin()+long(end),key+"="+value);
            }
        }
        contents.clear();for(const auto& line:lines){contents+=line;contents+="\r\n";}
        uint32_t error=0;if(!profile_file(fp,wide,contents,true,error)){set_last_error(error?error:5);return 0;}set_last_error(0);return 1;
    };
    for(bool wide:{false,true}) {
        const char* suffix=wide?"W":"A";std::string get="GetPrivateProfileString";get+=suffix;
        reg.add("kernel32.dll",get,Conv::Stdcall,6,[profile_string,wide](Cpu& c)->uint32_t{return profile_string(c,wide);});
        std::string integer="GetPrivateProfileInt";integer+=suffix;
        reg.add("kernel32.dll",integer,Conv::Stdcall,4,[this,profile_text,narrow,wide](Cpu& c)->uint32_t{
            uint32_t secp=c.arg(0),keyp=c.arg(1),fallback=c.arg(2),file=c.arg(3);std::string sec=wide?narrow(secp):str(secp),key=wide?narrow(keyp):str(keyp),contents;
            if(!profile_text(file,wide,contents))return fallback;
            std::string v=profile_value(contents,sec,key,"");if(v.empty())return fallback;
            char* end=nullptr;unsigned long n=std::strtoul(v.c_str(),&end,0);
            return end==v.c_str()?fallback:uint32_t(n);
        });
        std::string write="WritePrivateProfileString";write+=wide?"W":"A";
        reg.add("kernel32.dll",write,Conv::Stdcall,4,[profile_write,wide](Cpu& c)->uint32_t{return profile_write(c,wide);});
        std::string section_name="GetPrivateProfileSection";section_name+=wide?"W":"A";
        reg.add("kernel32.dll",section_name,Conv::Stdcall,4,[this,profile_text,narrow,wide](Cpu& c)->uint32_t{
            uint32_t sp=c.arg(0),buffer=c.arg(1),capacity=c.arg(2),file=c.arg(3);if(!sp||!buffer||!capacity){set_last_error(87);return 0;}if(capacity==1){if(wide)mem.w(buffer,0,2);else mem.w8(buffer,0);return 0;}
            std::string section=wide?narrow(sp):str(sp),contents;profile_text(file,wide,contents);std::string fold_section=section;for(char& ch:fold_section)ch=char(std::tolower((unsigned char)ch));
            std::vector<std::string> items;std::string current;for(size_t p=0;p<contents.size();){size_t e=contents.find_first_of("\r\n",p);if(e==std::string::npos)e=contents.size();std::string line=trim_profile(contents.substr(p,e-p));p=e+1;if(line.size()>=2&&line.front()=='['&&line.back()==']'){current=trim_profile(line.substr(1,line.size()-2));for(char& ch:current)ch=char(std::tolower((unsigned char)ch));continue;}if(current!=fold_section||line.empty()||line[0]==';'||line[0]=='#')continue;if(line.find('=')!=std::string::npos)items.push_back(line);}
            std::string packed;for(auto& item:items){packed+=item;packed.push_back(char(0));}packed.push_back(char(0));
            bool truncated=packed.size()>capacity;uint32_t copied=truncated?capacity-2:uint32_t(packed.size());
            for(uint32_t i=0;i<copied;++i){if(wide)mem.w(buffer+i*2,uint8_t(packed[i]),2);else mem.w8(buffer+i,uint8_t(packed[i]));}
            if(truncated){if(wide){mem.w(buffer+(capacity-2)*2,0,2);mem.w(buffer+(capacity-1)*2,0,2);}else{mem.w8(buffer+capacity-2,0);mem.w8(buffer+capacity-1,0);}return capacity-2;}
            return copied?copied-1:0;
        });
    }
    auto resource_key = [this](uint32_t directory,uint32_t root,uint32_t value,bool wide)->uint32_t {
        bool numeric=value<0x10000u;uint32_t id=value;
        if(!numeric){std::string wanted;if(wide){auto w=wstr(value);wanted.assign(w.begin(),w.end());}else wanted=str(value);
            if(wanted.size()>1&&wanted[0]=='#'){char* end=nullptr;unsigned long parsed=std::strtoul(wanted.c_str()+1,&end,10);if(end==wanted.c_str()+wanted.size()&&parsed<0x10000){numeric=true;id=uint32_t(parsed);}}
        }
        uint16_t named=uint16_t(mem.r(directory+12,2));
        if(!numeric){std::string wanted;if(wide){auto w=wstr(value);wanted.assign(w.begin(),w.end());}else wanted=str(value);
            for(uint32_t i=0;i<named;++i){uint32_t e=directory+16+i*8,name=mem.r32(e);if(!(name&0x80000000u))continue;uint32_t p=root+(name&0x7fffffffu);uint16_t n=uint16_t(mem.r(p,2));std::u16string found;for(uint32_t j=0;j<n;++j)found.push_back(char16_t(mem.r(p+2+j*2,2)));if(std::string(found.begin(),found.end())==wanted)return e;}
        }
        uint16_t ids=uint16_t(mem.r(directory+14,2));for(uint32_t i=0;i<ids;++i){uint32_t e=directory+16+(named+i)*8;if(mem.r32(e)==id)return e;}return 0;
    };
    auto resource_data = [this,resource_key](uint32_t base,uint32_t type,uint32_t name,bool wide,uint16_t requested,bool exact,uint32_t& data,uint32_t& size,uint32_t& error)->bool {
        uint32_t nt=base+mem.r32(base+0x3c);if(mem.r32(nt)!=0x00004550){error=1813;return false;}
        uint32_t opt=nt+24,rva=mem.r32(opt+96+16);if(!rva){error=1813;return false;}uint32_t root=base+rva;
        uint32_t te=resource_key(root,root,type,wide);if(!te){error=1813;return false;}uint32_t td=mem.r32(te+4);if(!(td&0x80000000u)){error=1813;return false;}uint32_t names=root+(td&0x7fffffffu);
        uint32_t ne=resource_key(names,root,name,wide);if(!ne){error=1814;return false;}uint32_t nd=mem.r32(ne+4);if(!(nd&0x80000000u)){error=1814;return false;}uint32_t langs=root+(nd&0x7fffffffu),count=uint32_t(mem.r(langs+12,2))+uint32_t(mem.r(langs+14,2)),chosen=0,neutral=0,user=0,first=0;
        uint16_t user_lang=0x0409;
#if defined(_WIN32)
        user_lang=uint16_t(::GetUserDefaultLangID());
#endif
        for(uint32_t i=0;i<count;++i){uint32_t e=langs+16+i*8,id=mem.r32(e);if(id&0x80000000u)continue;uint16_t lang=uint16_t(id);if(!first)first=e;if(lang==0)neutral=e;if(lang==user_lang)user=e;if(exact&&lang==requested){chosen=e;break;}}
        if(!chosen)chosen=neutral?neutral:(user?user:first);
        if(!chosen){error=1814;return false;}uint32_t dd=root+(mem.r32(chosen+4)&0x7fffffffu);data=base+mem.r32(dd);size=mem.r32(dd+4);error=0;return true;
    };
    auto find_resource = [this,resource_data](Cpu& c,bool wide,bool extended)->uint32_t {
        uint32_t module=c.arg(0),typep=extended?c.arg(1):c.arg(2),namep=extended?c.arg(2):c.arg(1),lang=extended?c.arg(3):0;
        uint32_t base=module?module:image_base;bool known=base==image_base;for(const auto& dll:dlls)if(dll.second.image.base==base)known=true;
        if(!base||!known){set_last_error(1813);return 0;}uint32_t data=0,size=0,error=0;
        if(!resource_data(base,typep,namep,wide,uint16_t(lang),extended,data,size,error)){set_last_error(error);return 0;}
        for(const auto& resource:resource_handles)if(resource.second.module==base&&resource.second.data==data&&resource.second.size==size){set_last_error(0);return resource.first;}
        uint32_t handle=0x74000000u+(++resource_serial);resource_handles[handle]={base,data,size};set_last_error(0);return handle;
    };
    auto enum_resource_names = [this,resource_key](Cpu& c,bool wide)->uint32_t {
        uint32_t module=c.arg(0),typep=c.arg(1),callback=c.arg(2),param=c.arg(3);
        uint32_t base=module?module:image_base;bool known=base==image_base;for(const auto& dll:dlls)if(dll.second.image.base==base)known=true;
        if(!known){set_last_error(1813);return 0;}if(!callback){set_last_error(87);return 0;}
        uint32_t nt=base+mem.r32(base+0x3c);if(mem.r32(nt)!=0x00004550){set_last_error(1813);return 0;}
        uint32_t opt=nt+24,rva=mem.r32(opt+96+16);if(!rva){set_last_error(1813);return 0;}uint32_t root=base+rva;
        uint32_t te=resource_key(root,root,typep,wide);if(!te){set_last_error(1813);return 0;}uint32_t td=mem.r32(te+4);if(!(td&0x80000000u)){set_last_error(1813);return 0;}
        uint32_t names=root+(td&0x7fffffffu),count=uint32_t(mem.r(names+12,2))+uint32_t(mem.r(names+14,2));if(!count){set_last_error(1813);return 0;}
        for(uint32_t i=0;i<count;++i){uint32_t e=names+16+i*8,n=mem.r32(e),name=0;
            if(n&0x80000000u){uint32_t p=root+(n&0x7fffffffu);uint16_t length=uint16_t(mem.r(p,2));std::u16string value;for(uint32_t j=0;j<length;++j)value.push_back(char16_t(mem.r(p+2+j*2,2)));
                if(wide)name=put_wstr(value);else{std::string narrow;narrow.reserve(value.size());for(char16_t ch:value)narrow.push_back(ch<=0xff?char(ch):'?');name=put_str(narrow);}
            }else name=n;
            if(!c.call(callback,{base,typep,name,param})){set_last_error(15106);return 0;}
        }
        return 1;
    };
    for(bool wide:{false,true}){
        std::string name="EnumResourceNames";name+=wide?"W":"A";
        reg.add("kernel32.dll",name,Conv::Stdcall,4,[enum_resource_names,wide](Cpu& c)->uint32_t{return enum_resource_names(c,wide);});
    }
    for(bool wide:{false,true})for(bool extended:{false,true}){std::string name=extended?"FindResourceEx":"FindResource";name+=wide?"W":"A";reg.add("kernel32.dll",name,Conv::Stdcall,extended?4:3,[find_resource,wide,extended](Cpu& c)->uint32_t{return find_resource(c,wide,extended);});}
    reg.add("kernel32.dll","LoadResource",Conv::Stdcall,2,[this](Cpu& c)->uint32_t{auto it=resource_handles.find(c.arg(1));if(it==resource_handles.end()||it->second.module!=(c.arg(0)?c.arg(0):image_base)){set_last_error(1814);return 0;}return c.arg(1);});
    reg.add("kernel32.dll","LockResource",Conv::Stdcall,1,[this](Cpu& c)->uint32_t{auto it=resource_handles.find(c.arg(0));return it==resource_handles.end()?0:it->second.data;});
    reg.add("kernel32.dll","SizeofResource",Conv::Stdcall,2,[this](Cpu& c)->uint32_t{auto it=resource_handles.find(c.arg(1));if(it==resource_handles.end()||it->second.module!=(c.arg(0)?c.arg(0):image_base)){set_last_error(1814);return 0;}return it->second.size;});
    reg.add("kernel32.dll","FreeResource",Conv::Stdcall,1,[this](Cpu& c)->uint32_t{return resource_handles.erase(c.arg(0))?0u:1u;});
    for(bool wide:{false,true}){std::string name=wide?"LoadStringW":"LoadStringA";reg.add("user32.dll",name,Conv::Stdcall,4,[this,resource_data,wide](Cpu& c)->uint32_t{
        uint32_t module=c.arg(0)?c.arg(0):image_base,id=c.arg(1),buffer=c.arg(2),capacity=c.arg(3);bool known=module==image_base;for(const auto& dll:dlls)if(dll.second.image.base==module)known=true;
        if(!known||!id||id>0xffff||!buffer||!capacity){set_last_error(87);return 0;}
        uint32_t data=0,size=0,error=0,block=id/16+1;if(!resource_data(module,6,block,false,0,false,data,size,error)){set_last_error(error);return 0;}
        uint32_t offset=0;for(uint32_t i=0;i<16;++i){if(offset>=size){set_last_error(1814);return 0;}uint32_t length=uint32_t(mem.r(data+offset,2));offset+=2;if(i==id%16){uint32_t copied=std::min<uint32_t>(length,capacity-1);for(uint32_t j=0;j<copied;++j){uint16_t ch=uint16_t(mem.r(data+offset+j*2,2));if(wide)mem.w(buffer+j*2,ch,2);else mem.w8(buffer+j,uint8_t(ch<=0xff?ch:'?'));}if(wide)mem.w(buffer+copied*2,0,2);else mem.w8(buffer+copied,0);set_last_error(0);return copied;}offset+=length*2;}
        return 0;
    });}
    // GUI launcher guests have no attached console. Console-title APIs therefore
    // fail with ERROR_INVALID_HANDLE, as they do for a Windows process without one.
    auto no_console = [this](Cpu&) -> uint32_t { set_last_error(6); return 0; };
    reg.add("kernel32.dll", "SetConsoleTitleA", Conv::Stdcall, 1, no_console);
    reg.add("kernel32.dll", "SetConsoleTitleW", Conv::Stdcall, 1, no_console);
    reg.add("kernel32.dll", "GetConsoleTitleA", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { (void)c; set_last_error(6); return 0; });
    reg.add("kernel32.dll", "GetConsoleTitleW", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { (void)c; set_last_error(6); return 0; });
    reg.add("kernel32.dll", "AllocConsole", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { set_last_error(5); return 0; });
    reg.add("kernel32.dll", "FreeConsole", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { set_last_error(87); return 0; });
    reg.add("kernel32.dll", "AttachConsole", Conv::Stdcall, 1, [this](Cpu&) -> uint32_t { set_last_error(6); return 0; });
    // Standard handles are process state; the initial values address the Play sinks.
    reg.add("kernel32.dll", "GetStdHandle", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        auto it = std_handles.find(int32_t(c.arg(0)));
        if (it == std_handles.end()) { set_last_error(87); return 0xFFFFFFFFu; }
        return it->second;
    });
    reg.add("kernel32.dll", "SetStdHandle", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        auto it = std_handles.find(int32_t(c.arg(0)));
        if (it == std_handles.end()) { set_last_error(87); return 0; }
        it->second = c.arg(1); return 1;
    });
    // The UWP guest has no attached console: these APIs fail with ERROR_INVALID_HANDLE,
    // rather than claiming that the host's redirected streams are console buffers.
    reg.add("kernel32.dll", "GetConsoleCP", Conv::Stdcall, 0, no_console);
    reg.add("kernel32.dll", "GetConsoleOutputCP", Conv::Stdcall, 0, no_console);
    reg.add("kernel32.dll", "GetConsoleMode", Conv::Stdcall, 2, no_console);
    reg.add("kernel32.dll", "SetConsoleMode", Conv::Stdcall, 2, no_console);
    // The guest has no attached debugger. Windows reports FALSE and discards
    // OutputDebugString calls when no debugger is present.
    reg.add("kernel32.dll", "IsDebuggerPresent", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "OutputDebugStringA", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "OutputDebugStringW", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    // The guest's file APIs use the Windows ANSI code-page path surface; no
    // SetFileApisToOEM route is exposed, so the process remains in ANSI mode.
    reg.add("kernel32.dll", "AreFileApisANSI", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 1; });
    auto guard_file = [this](const char* operation, const std::u16string& path) {
        if (host.destructive_file_guard && host.destructive_file_guard(operation, path)) return true;
        set_last_error(5); return false;  // ERROR_ACCESS_DENIED; missing policy fails closed
    };
    auto narrow_path = [](const std::string& path) { std::u16string out; out.reserve(path.size()); for (unsigned char c : path) out.push_back(char16_t(c)); return out; };
    auto native_file = [this](uint32_t h, Host::FileCall& call) {
        auto it=file_handles.find(h); if(it==file_handles.end()){set_last_error(6);return false;}
        call.handle=it->second; return true;
    };
    // Simple file operations preserve the native kernel32 result and last error.
    reg.add("kernel32.dll", "CreateDirectoryA", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        if(!c.arg(0)||c.arg(1)){set_last_error(87);return 0;}Host::FileCall q{Host::FileCall::Kind::AttributesA};q.operation=Host::FileCall::Operation::CreateDirectoryA;q.path_a=str(c.arg(0));
        if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}return 1; });
    reg.add("kernel32.dll", "CreateDirectoryW", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        if(!c.arg(0)||c.arg(1)){set_last_error(87);return 0;}Host::FileCall q{Host::FileCall::Kind::CreateDirectoryW};q.path_w=wstr(c.arg(0));if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}return 1; });
    reg.add("kernel32.dll", "RemoveDirectoryA", Conv::Stdcall, 1, [this,guard_file,narrow_path](Cpu& c) -> uint32_t {
        if(!c.arg(0)){set_last_error(87);return 0;}Host::FileCall q{Host::FileCall::Kind::RemoveDirectoryA};q.path_a=str(c.arg(0));if(!guard_file("RemoveDirectoryA",narrow_path(q.path_a)))return 0;if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}return 1; });
    reg.add("kernel32.dll", "RemoveDirectoryW", Conv::Stdcall, 1, [this,guard_file](Cpu& c) -> uint32_t {
        if(!c.arg(0)){set_last_error(87);return 0;}Host::FileCall q{Host::FileCall::Kind::RemoveDirectoryW};q.path_w=wstr(c.arg(0));if(!guard_file("RemoveDirectoryW",q.path_w))return 0;if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}return 1; });
    reg.add("kernel32.dll", "GetCurrentDirectoryA", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        Host::FileCall q{Host::FileCall::Kind::GetCurrentDirectoryA};q.args[0]=c.arg(0);if(c.arg(0)>(1u<<20)){set_last_error(87);return 0;}if(c.arg(0)&&!c.arg(1)){set_last_error(87);return 0;}if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}if(q.value<c.arg(0)&&q.value&&c.arg(1)){if(q.data.size()<q.value+1){set_last_error(87);return 0;}mem.write_bytes(c.arg(1),q.data.data(),uint32_t(q.value+1));}return uint32_t(q.value); });
    reg.add("kernel32.dll", "GetCurrentDirectoryW", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        Host::FileCall q{Host::FileCall::Kind::GetCurrentDirectoryW};q.args[0]=c.arg(0);if(c.arg(0)>(1u<<20)){set_last_error(87);return 0;}if(c.arg(0)&&!c.arg(1)){set_last_error(87);return 0;}if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}if(q.value<c.arg(0)&&q.value&&c.arg(1)){if(q.data.size()<(q.value+1)*2){set_last_error(87);return 0;}mem.write_bytes(c.arg(1),q.data.data(),uint32_t((q.value+1)*2));}return uint32_t(q.value); });
    reg.add("kernel32.dll", "SetCurrentDirectoryA", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        if(!c.arg(0)){set_last_error(87);return 0;}Host::FileCall q{Host::FileCall::Kind::SetCurrentDirectoryA};q.path_a=str(c.arg(0));if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}return 1; });
    reg.add("kernel32.dll", "SetCurrentDirectoryW", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        if(!c.arg(0)){set_last_error(87);return 0;}Host::FileCall q{Host::FileCall::Kind::SetCurrentDirectoryW};q.path_w=wstr(c.arg(0));if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}return 1; });
    auto find_first = [this](const char* name, Host::FileCall::Kind kind, bool wide, bool extended) {
        reg.add("kernel32.dll",name,Conv::Stdcall,extended?6:2,[this,kind,wide,extended](Cpu& c)->uint32_t {
            uint32_t path=c.arg(0),out=c.arg(extended?2:1);if(!path||!out||(extended&&c.arg(4))){set_last_error(87);return 0xffffffffu;}
            Host::FileCall q{kind};if(wide)q.path_w=wstr(path);else q.path_a=str(path);
            if(extended){q.args[0]=c.arg(1);q.args[1]=c.arg(3);q.args[2]=c.arg(5);}
            if(!call_file(q)){set_last_error(q.error?q.error:120);return 0xffffffffu;}
            const uint32_t size=wide?592u:320u;if(q.data.size()!=size){set_last_error(87);return 0xffffffffu;}
            mem.write_bytes(out,q.data.data(),size);set_last_error(q.error);uint32_t h=0x51000000u+(++find_serial);find_handles[h]=q.value;return h;
        });
    };
    find_first("FindFirstFileA",Host::FileCall::Kind::FindFirstA,false,false);
    find_first("FindFirstFileW",Host::FileCall::Kind::FindFirstW,true,false);
    find_first("FindFirstFileExA",Host::FileCall::Kind::FindFirstExA,false,true);
    find_first("FindFirstFileExW",Host::FileCall::Kind::FindFirstExW,true,true);
    auto find_next = [this](const char* name, Host::FileCall::Kind kind, bool wide) {
        reg.add("kernel32.dll",name,Conv::Stdcall,2,[this,kind,wide](Cpu& c)->uint32_t {
            auto it=find_handles.find(c.arg(0));if(it==find_handles.end()){set_last_error(6);return 0;}if(!c.arg(1)){set_last_error(87);return 0;}
            Host::FileCall q{kind};q.handle=it->second;if(!call_file(q)){set_last_error(q.error?q.error:18);return 0;}
            uint32_t size=wide?592u:320u;if(q.data.size()!=size){set_last_error(87);return 0;}mem.write_bytes(c.arg(1),q.data.data(),size);return 1;
        });
    };
    find_next("FindNextFileA",Host::FileCall::Kind::FindNextA,false);
    find_next("FindNextFileW",Host::FileCall::Kind::FindNextW,true);
    reg.add("kernel32.dll","FindClose",Conv::Stdcall,1,[this](Cpu& c)->uint32_t {
        auto it=find_handles.find(c.arg(0));if(it==find_handles.end()){set_last_error(6);return 0;}Host::FileCall q{Host::FileCall::Kind::FindClose};q.handle=it->second;if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}find_handles.erase(it);return 1; });
    reg.add("shell32.dll", "DragAcceptFiles", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 0; });
    auto guid_read=[this](uint32_t address,uint8_t* bytes)->bool {
        if(!address)return false;
        auto value=mem.read_bytes(address,16);std::memcpy(bytes,value.data(),16);return true;
    };
    auto guid_equal=[](const uint8_t* left,const uint8_t* right)->bool { return std::memcmp(left,right,16)==0; };
    auto guid_parse=[](const std::u16string& text,uint8_t* out)->bool {
        auto hex=[](char16_t c)->int { if(c>=u'0'&&c<=u'9')return int(c-u'0');if(c>=u'a'&&c<=u'f')return int(c-u'a'+10);if(c>=u'A'&&c<=u'F')return int(c-u'A'+10);return -1; };
        size_t at=0;if(text.size()==38&&text.front()==u'{'&&text.back()==u'}')at=1;else if(text.size()!=36)return false;
        const unsigned groups[]={8,4,4,4,12};size_t pos=at;unsigned byte=0;
        for(unsigned group=0;group<5;++group){if(group&&text[pos++]!=u'-')return false;for(unsigned i=0;i<groups[group];i+=2){int high=hex(text[pos++]),low=hex(text[pos++]);if(high<0||low<0)return false;out[byte++]=uint8_t((high<<4)|low);}}
        if((at==1&&pos!=text.size()-1)||(!at&&pos!=text.size()))return false;
        uint8_t swap=out[0];out[0]=out[3];out[3]=swap;swap=out[1];out[1]=out[2];out[2]=swap;
        swap=out[4];out[4]=out[5];out[5]=swap;swap=out[6];out[6]=out[7];out[7]=swap;
        return true;
    };
    auto guid_string=[](const uint8_t* bytes)->std::u16string {
        uint32_t d1=uint32_t(bytes[0])|uint32_t(bytes[1])<<8|uint32_t(bytes[2])<<16|uint32_t(bytes[3])<<24;
        uint16_t d2=uint16_t(bytes[4])|uint16_t(bytes[5])<<8,d3=uint16_t(bytes[6])|uint16_t(bytes[7])<<8;
        char text[39];std::snprintf(text,sizeof(text),"{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",d1,d2,d3,bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
        std::u16string result;for(unsigned i=0;i<38;++i)result.push_back(char16_t(uint8_t(text[i])));return result;
    };
    auto com_init=[this](uint32_t value,uint32_t model)->uint32_t {
        if(value)return 0x80070057u;
        uint32_t tid=mem.r32(cur_teb+0x24);if(!tid)tid=1;auto it=com_apartments.find(tid);
        if(it!=com_apartments.end()){if(it->second.first!=model)return 0x80010106u;++it->second.second;return 1;}com_apartments[tid]={model,1};return 0;
    };
    reg.add("ole32.dll","CoCreateInstance",Conv::Stdcall,5,[this,guid_read,guid_equal](Cpu& c)->uint32_t {
        // DirectInput8 (#340) first: SDL2's default joystick path creates it.
        if (c.arg(0) && c.arg(3) && c.arg(4)) {
            const uint32_t result = dinput8_cocreate(host.com, c);
            if (result != 0x80040154u) return result;
        }
        static const uint8_t clsid_sound8[]={0x3f,0xcc,0x01,0x39,0xb5,0x84,0xa4,0x4f,0xba,0x35,0xaa,0x81,0x72,0xb8,0xa0,0x9b};
        static const uint8_t clsid_sound[]={0x46,0xd9,0xd4,0x47,0xe8,0x62,0xcf,0x11,0x93,0xbc,0x44,0x45,0x53,0x54,0x00,0x00};
        static const uint8_t iid_unknown[]={0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
        static const uint8_t iid_sound[]={0x83,0xfa,0x9a,0x27,0x81,0x49,0xce,0x11,0xa5,0x21,0,0x20,0xaf,0x0b,0xe5,0x60};
        static const uint8_t iid_sound8[]={0x93,0x7e,0x0a,0xc5,0x95,0xf3,0x34,0x48,0x9e,0xf6,0x7f,0xa9,0x9d,0xe5,0x09,0x66};
        if(!c.arg(4)||!c.arg(3)||!c.arg(0)){set_last_error(87);return 0x80004003u;}mem.w32(c.arg(4),0);if(c.arg(1))return 0x80040110u;
        uint8_t clsid[16],iid[16];if(!guid_read(c.arg(0),clsid)||!guid_read(c.arg(3),iid))return 0x80004003u;
        if(!guid_equal(clsid,clsid_sound8)&&!guid_equal(clsid,clsid_sound))return 0x80040154u;
        if(!guid_equal(iid,iid_unknown)&&!guid_equal(iid,iid_sound)&&!guid_equal(iid,iid_sound8))return 0x80004002u;
#if defined(_WIN32)
        if(!host.dsound_factory)return 0x80004001u;uintptr_t result=0;uint32_t hr=uint32_t(host.dsound_factory(nullptr,&result));
        if((hr&0x80000000u)||!result)return (hr&0x80000000u)?hr:0x80004001u;
        uint32_t guest=dsound_wrap(host.com,c,"IDirectSound8",reinterpret_cast<IUnknown*>(result));if(!guest)return 0x80004001u;c.mem.w32(c.arg(4),guest);return hr;
#else
        return 0x80004001u;
#endif
    });
    reg.add("ole32.dll","CoSetProxyBlanket",Conv::Stdcall,8,[](Cpu&) -> uint32_t {
        return 0x80004001u; // E_NOTIMPL: the x86 guest has no proxy manager
    });
    reg.add("ole32.dll","CoInitialize",Conv::Stdcall,1,[com_init](Cpu& c)->uint32_t { return com_init(c.arg(0),2); });
    reg.add("ole32.dll","CoInitializeEx",Conv::Stdcall,2,[com_init](Cpu& c)->uint32_t {
        uint32_t flags=c.arg(1);if(flags&~0xEu)return 0x80070057u;return com_init(c.arg(0),flags&2u);
    });
    reg.add("ole32.dll","CoUninitialize",Conv::Stdcall,0,[this](Cpu&)->uint32_t {
        uint32_t tid=mem.r32(cur_teb+0x24);if(!tid)tid=1;auto it=com_apartments.find(tid);if(it!=com_apartments.end()&&!--it->second.second)com_apartments.erase(it);return 0;
    });
    reg.add("ole32.dll","OleInitialize",Conv::Stdcall,1,[com_init](Cpu& c)->uint32_t { return com_init(c.arg(0),2); });
    reg.add("ole32.dll","OleUninitialize",Conv::Stdcall,0,[this](Cpu&)->uint32_t {
        uint32_t tid=mem.r32(cur_teb+0x24);if(!tid)tid=1;auto it=com_apartments.find(tid);if(it!=com_apartments.end()&&!--it->second.second)com_apartments.erase(it);return 0;
    });
    reg.add("ole32.dll","CoTaskMemAlloc",Conv::Stdcall,1,[this](Cpu& c)->uint32_t { return heap.alloc(c.arg(0)); });
    reg.add("ole32.dll","CoTaskMemFree",Conv::Stdcall,1,[this](Cpu& c)->uint32_t { if(c.arg(0)&&heap.size_of(c.arg(0)))heap.release(c.arg(0));return 0; });
    reg.add("ole32.dll","CoTaskMemRealloc",Conv::Stdcall,2,[this](Cpu& c)->uint32_t {
        uint32_t old=c.arg(0),size=c.arg(1);if(!old)return size?heap.alloc(size):0;if(!heap.size_of(old))return 0;if(!size){heap.release(old);return 0;}return heap.realloc(old,size);
    });
    reg.add("ole32.dll","CoCreateGuid",Conv::Stdcall,1,[this](Cpu& c)->uint32_t {
        if(!c.arg(0)){set_last_error(87);return 0x80004003u;}uint8_t bytes[16]{};if(!host.random_bytes||!host.random_bytes(bytes,16))return 0x80004001u;bytes[6]=uint8_t((bytes[6]&0x0f)|0x40);bytes[8]=uint8_t((bytes[8]&0x3f)|0x80);mem.write_bytes(c.arg(0),bytes,16);return 0;
    });
    reg.add("ole32.dll","StringFromGUID2",Conv::Stdcall,3,[this,guid_read,guid_string](Cpu& c)->uint32_t {
        if(!c.arg(0)||!c.arg(1)||c.arg(2)<39){set_last_error(87);return 0;}uint8_t bytes[16];if(!guid_read(c.arg(0),bytes))return 0;auto text=guid_string(bytes);for(size_t i=0;i<text.size();++i)mem.w(c.arg(1)+uint32_t(i*2),text[i],2);mem.w(c.arg(1)+uint32_t(text.size()*2),0,2);return uint32_t(text.size()+1);
    });
    auto guid_from_string=[this,guid_parse](Cpu& c)->uint32_t {
        if(!c.arg(0)||!c.arg(1)){set_last_error(87);return 0x80004003u;}uint8_t bytes[16];if(!guid_parse(wstr(c.arg(0)),bytes))return 0x800401f3u;mem.write_bytes(c.arg(1),bytes,16);return 0;
    };
    reg.add("ole32.dll","CLSIDFromString",Conv::Stdcall,2,guid_from_string);
    reg.add("ole32.dll","IIDFromString",Conv::Stdcall,2,guid_from_string);
    reg.add("ole32.dll","PropVariantClear",Conv::Stdcall,1,[this](Cpu& c)->uint32_t {
        uint32_t address=c.arg(0);if(!address){set_last_error(87);return 0x80004003u;}uint16_t vt=uint16_t(mem.r(address,2));if(vt&0x4000u)return 0x80004001u;
        const uint16_t supported[]={0,1,2,3,4,5,6,7,10,11,16,17,18,19,20,21,22,23,25};for(uint16_t value:supported)if(vt==value){mem.w(address,0,2);return 0;}return 0x80004001u;
    });
    reg.add("oleaut32.dll","SysAllocString",Conv::Stdcall,1,[this](Cpu& c)->uint32_t {
        if(!c.arg(0))return 0;
        const auto text=wstr(c.arg(0));const uint64_t bytes=uint64_t(text.size())*2;
        if(bytes>0x7ffffff0u)return 0;
        const uint32_t base=heap.alloc(uint32_t(bytes+6));mem.w32(base,uint32_t(bytes));
        for(size_t i=0;i<text.size();++i)mem.w(base+4+uint32_t(i*2),text[i],2);
        mem.w(base+4+uint32_t(bytes),0,2);return base+4;
    });
    reg.add_ordinal("oleaut32.dll",2,"SysAllocString");
    reg.add("oleaut32.dll","SysFreeString",Conv::Stdcall,1,[this](Cpu& c)->uint32_t {
        const uint32_t bstr=c.arg(0);if(bstr&&bstr>=4&&heap.size_of(bstr-4))heap.release(bstr-4);return 0;
    });
    reg.add_ordinal("oleaut32.dll",6,"SysFreeString");
    reg.add("oleaut32.dll","VariantInit",Conv::Stdcall,1,[this](Cpu& c)->uint32_t {
        if(!c.arg(0))return 0;
        for(unsigned i=0;i<16;++i)mem.w8(c.arg(0)+i,0);
        return 0;
    });
    reg.add_ordinal("oleaut32.dll",8,"VariantInit");
    reg.add("oleaut32.dll","VariantClear",Conv::Stdcall,1,[this](Cpu& c)->uint32_t {
        const uint32_t address=c.arg(0);if(!address)return 0x80070057u;const uint16_t vt=uint16_t(mem.r(address,2));
        if(vt==0){for(unsigned i=0;i<16;++i)mem.w8(address+i,0);return 0;}
        if(vt==8){const uint32_t bstr=mem.r32(address+8);if(bstr&&bstr>=4&&heap.size_of(bstr-4))heap.release(bstr-4);for(unsigned i=0;i<16;++i)mem.w8(address+i,0);return 0;}
        return 0x80004001u;
    });
    reg.add_ordinal("oleaut32.dll",9,"VariantClear");
    reg.add("kernel32.dll", "DeleteFileA", Conv::Stdcall, 1, [this,guard_file,narrow_path](Cpu& c) -> uint32_t {
        if(!c.arg(0)){set_last_error(87);return 0;}Host::FileCall q{Host::FileCall::Kind::AttributesA};q.operation=Host::FileCall::Operation::DeleteA;q.path_a=str(c.arg(0));
        if(!guard_file("DeleteFileA",narrow_path(q.path_a)))return 0;
        if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}return 1; });
    reg.add("kernel32.dll", "DeleteFileW", Conv::Stdcall, 1, [this,guard_file](Cpu& c) -> uint32_t {
        if(!c.arg(0)){set_last_error(87);return 0;}Host::FileCall q{Host::FileCall::Kind::AttributesA};q.operation=Host::FileCall::Operation::DeleteW;q.path_w=wstr(c.arg(0));
        if(!guard_file("DeleteFileW",q.path_w))return 0;
        if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}return 1; });
    // File operations cross into Play's native kernel32_core layer through host HANDLEs;
    // guest pointers are decoded here, and no synthetic success is returned off-Play.
    reg.add("kernel32.dll", "CreateFileA", Conv::Stdcall, 7, [this,guard_file,narrow_path](Cpu& c) -> uint32_t {
        if(!c.arg(0)){set_last_error(87);return 0xffffffffu;}Host::FileCall q{Host::FileCall::Kind::CreateA}; q.path_a=str(c.arg(0));
        q.args[0]=c.arg(1);q.args[1]=c.arg(2);q.args[2]=c.arg(4);q.args[3]=c.arg(5);
        const uint32_t writes=0x40000000u|0x10000000u|0x02000000u|0x00000002u|0x00000004u|0x00000010u|0x00000100u|0x00010000u;
        if(((q.args[0]&writes)||q.args[2]==2||q.args[2]==5||(q.args[3]&0x04000000u))&&!guard_file("CreateFileA",narrow_path(q.path_a)))return 0xffffffffu;
        if(c.arg(6)){auto it=file_handles.find(c.arg(6));if(it==file_handles.end()){set_last_error(6);return 0xffffffffu;}q.template_handle=it->second;}
        if(!call_file(q)||q.value==UINT64_MAX){set_last_error(q.error?q.error:120);return 0xffffffffu;}set_last_error(q.error);return add_file_handle(q.value); });
    reg.add("kernel32.dll", "CreateFileW", Conv::Stdcall, 7, [this,guard_file](Cpu& c) -> uint32_t {
        if(!c.arg(0)){set_last_error(87);return 0xffffffffu;}Host::FileCall q{Host::FileCall::Kind::CreateW}; q.path_w=wstr(c.arg(0));
        q.args[0]=c.arg(1);q.args[1]=c.arg(2);q.args[2]=c.arg(4);q.args[3]=c.arg(5);
        const uint32_t writes=0x40000000u|0x10000000u|0x02000000u|0x00000002u|0x00000004u|0x00000010u|0x00000100u|0x00010000u;
        if(((q.args[0]&writes)||q.args[2]==2||q.args[2]==5||(q.args[3]&0x04000000u))&&!guard_file("CreateFileW",q.path_w))return 0xffffffffu;
        if(c.arg(6)){auto it=file_handles.find(c.arg(6));if(it==file_handles.end()){set_last_error(6);return 0xffffffffu;}q.template_handle=it->second;}
        if(!call_file(q)||q.value==UINT64_MAX){set_last_error(q.error?q.error:120);return 0xffffffffu;}set_last_error(q.error);return add_file_handle(q.value); });
    reg.add("kernel32.dll", "ReadFile", Conv::Stdcall, 5, [this,native_file](Cpu& c) -> uint32_t {
        uint32_t n=c.arg(2),out=c.arg(3); if(n>(1u<<24)||!out||c.arg(4)){set_last_error(87);return 0;}
        Host::FileCall q{Host::FileCall::Kind::Read};if(!native_file(c.arg(0),q))return 0;q.args[0]=n;
        if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}
        if(q.args[1]>n||q.data.size()<q.args[1]){set_last_error(87);return 0;}if(q.args[1])mem.write_bytes(c.arg(1),q.data.data(),q.args[1]);if(out)mem.w32(out,q.args[1]);return 1; });
    reg.add("kernel32.dll", "GetFileSize", Conv::Stdcall, 2, [this,native_file](Cpu& c) -> uint32_t {
        Host::FileCall q{Host::FileCall::Kind::Size};if(!native_file(c.arg(0),q))return 0xffffffffu;q.args[0]=c.arg(1)!=0;
        if(!call_file(q)){set_last_error(q.error?q.error:120);return 0xffffffffu;}if(c.arg(1))mem.w32(c.arg(1),q.args[1]);set_last_error(0);return uint32_t(q.value); });
    reg.add("kernel32.dll", "GetFileTime", Conv::Stdcall, 4, [this,native_file](Cpu& c) -> uint32_t {
        Host::FileCall q{Host::FileCall::Kind::Time};if(!native_file(c.arg(0),q))return 0;
        for(unsigned i=0;i<3;++i) q.args[i]=c.arg(i+1)!=0;
        if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}
        for(unsigned i=0;i<3;++i) { uint32_t dst=c.arg(i+1); if(dst) mem.write_bytes(dst,q.data.data()+8*i,8); }
        return 1; });
    reg.add("kernel32.dll", "GetFileAttributesA", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        Host::FileCall q{Host::FileCall::Kind::AttributesA};q.path_a=str(c.arg(0));if(!call_file(q)){set_last_error(q.error?q.error:120);return 0xffffffffu;}return uint32_t(q.value); });
    reg.add("kernel32.dll", "GetFileAttributesW", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        std::u16string wide = wstr(c.arg(0));
        std::string narrow_path; bool ascii = true;
        for (char16_t ch : wide) { if (ch > 0x7f) { ascii = false; break; } narrow_path.push_back(char(ch)); }
        if (host.file_call && ascii) {
            Host::FileCall q{Host::FileCall::Kind::AttributesA}; q.path_a = narrow_path;
            if (call_file(q)) return uint32_t(q.value);
            set_last_error(q.error ? q.error : 120); return 0xFFFFFFFFu;
        }
#if defined(_WIN32)
        using Fn = DWORD (WINAPI*)(LPCWSTR);
        static HMODULE k32 = LoadLibraryExW(L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto fn = k32 ? reinterpret_cast<Fn>(GetProcAddress(k32, "GetFileAttributesW")) : nullptr;
        if (!fn) { set_last_error(ERROR_CALL_NOT_IMPLEMENTED); return 0xFFFFFFFFu; }
        std::u16string input = wstr(c.arg(0)); input.push_back(0);
        DWORD attributes = fn(reinterpret_cast<LPCWSTR>(input.data()));
        set_last_error(GetLastError());
        return attributes;
#else
        (void)c;
        set_last_error(120);  // ERROR_CALL_NOT_IMPLEMENTED on the off-device host
        return 0xFFFFFFFFu;
#endif
    });
    reg.add("kernel32.dll", "GetFileAttributesExW", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        std::u16string wide = wstr(c.arg(0));
        std::string narrow_path; bool ascii = true;
        for (char16_t ch : wide) { if (ch > 0x7f) { ascii = false; break; } narrow_path.push_back(char(ch)); }
        if (host.file_call && ascii) {
            Host::FileCall q{Host::FileCall::Kind::AttributesA}; q.path_a = narrow_path;
            q.args[0] = c.arg(1); q.args[1] = 1;
            if (!call_file(q)) { set_last_error(q.error ? q.error : 120); return 0; }
            if (c.arg(2) && !q.data.empty()) mem.write_bytes(c.arg(2), q.data.data(), uint32_t(q.data.size()));
            return 1;
        }
#if defined(_WIN32)
        using Fn = BOOL (WINAPI*)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
        static HMODULE k32 = LoadLibraryExW(L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto fn = k32 ? reinterpret_cast<Fn>(GetProcAddress(k32, "GetFileAttributesExW")) : nullptr;
        if (!fn) { set_last_error(ERROR_CALL_NOT_IMPLEMENTED); return 0; }
        std::u16string input = wstr(c.arg(0)); input.push_back(0);
        WIN32_FILE_ATTRIBUTE_DATA data{};
        BOOL ok = fn(reinterpret_cast<LPCWSTR>(input.data()), GET_FILEEX_INFO_LEVELS(c.arg(1)), &data);
        set_last_error(GetLastError());
        if (ok) mem.write_bytes(c.arg(2), &data, sizeof(data));
        return ok != FALSE;
#else
        (void)c;
        set_last_error(120);  // ERROR_CALL_NOT_IMPLEMENTED on the off-device host
        return 0;
#endif
    });
    reg.add("kernel32.dll", "SetFilePointer", Conv::Stdcall, 4, [this,native_file](Cpu& c) -> uint32_t {
        Host::FileCall q{Host::FileCall::Kind::Seek};if(!native_file(c.arg(0),q))return 0xffffffffu;
        q.args[0]=c.arg(1);q.args[1]=c.arg(2)?mem.r32(c.arg(2)):0;q.args[2]=c.arg(3);q.args[3]=c.arg(2)!=0;
        if(!call_file(q)){set_last_error(q.error?q.error:120);return 0xffffffffu;}if(c.arg(2))mem.w32(c.arg(2),q.args[1]);set_last_error(0);return uint32_t(q.value); });
    // x86 stdcall passes LARGE_INTEGER liDistanceToMove by value in two stack slots:
    // (hFile, distLow, distHigh, lpNewFilePointer, dwMoveMethod) = 20 bytes popped.
    reg.add("kernel32.dll", "SetFilePointerEx", Conv::Stdcall, 5, [this,native_file](Cpu& c) -> uint32_t {
        Host::FileCall q{Host::FileCall::Kind::SeekEx};if(!native_file(c.arg(0),q))return 0;
        q.distance=int64_t(uint64_t(c.arg(1))|(uint64_t(c.arg(2))<<32));q.method=c.arg(4);q.args[0]=c.arg(3)!=0;
        if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}if(c.arg(3)){mem.w32(c.arg(3),uint32_t(q.position));mem.w32(c.arg(3)+4,uint32_t(uint64_t(q.position)>>32));}return 1; });
    reg.add("kernel32.dll", "SetEndOfFile", Conv::Stdcall, 1, [this,native_file](Cpu& c) -> uint32_t {
        Host::FileCall q{Host::FileCall::Kind::End};if(!native_file(c.arg(0),q))return 0;if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}return 1; });
    reg.add("kernel32.dll", "FileTimeToSystemTime", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        Host::FileCall q{Host::FileCall::Kind::TimeConvert};q.data=mem.read_bytes(c.arg(0),8);if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}mem.write_bytes(c.arg(1),q.data.data(),16);return 1; });
    reg.add("kernel32.dll", "FileTimeToLocalFileTime", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        Host::FileCall q{Host::FileCall::Kind::TimeLocal};q.data=mem.read_bytes(c.arg(0),8);if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}mem.write_bytes(c.arg(1),q.data.data(),8);return 1; });
    reg.add("kernel32.dll", "SystemTimeToFileTime", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        if(!c.arg(0)||!c.arg(1)){set_last_error(87);return 0;}Host::FileCall q{Host::FileCall::Kind::TimeConvert};q.operation=Host::FileCall::Operation::SystemToFileTime;q.data=mem.read_bytes(c.arg(0),16);
        if(!call_file(q)){set_last_error(q.error?q.error:87);return 0;}mem.write_bytes(c.arg(1),q.data.data(),8);return 1; });
    reg.add("kernel32.dll", "SystemTimeToTzSpecificLocalTime", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        if(!c.arg(1)||!c.arg(2)){set_last_error(87);return 0;}Host::FileCall q{Host::FileCall::Kind::TimeLocal};q.operation=Host::FileCall::Operation::SystemToTzLocal;q.data=mem.read_bytes(c.arg(1),16);q.args[0]=c.arg(0)!=0;
        if(c.arg(0)){auto tz=mem.read_bytes(c.arg(0),172);q.data.insert(q.data.end(),tz.begin(),tz.end());}
        if(!call_file(q)){set_last_error(q.error?q.error:87);return 0;}mem.write_bytes(c.arg(2),q.data.data(),16);return 1; });
    reg.add("kernel32.dll", "WriteFile", Conv::Stdcall, 5, [this](Cpu& c) -> uint32_t {
        uint32_t handle = c.arg(0), buf = c.arg(1), n = c.arg(2), written = c.arg(3);
        if (handle != Host::H_STDOUT && handle != Host::H_STDERR) { set_last_error(6); return 0; }  // ERROR_INVALID_HANDLE
        if (n > (1u << 24)) { set_last_error(87); return 0; }
        auto bytes = mem.read_bytes(buf, n);
        emit(handle == Host::H_STDOUT ? 1 : 2, std::string(bytes.begin(), bytes.end()));
        if (written) mem.w32(written, n);
        return 1; });
    reg.add("kernel32.dll", "GetFileType", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t h = c.arg(0);
        if (h == Host::H_STDIN || h == Host::H_STDOUT || h == Host::H_STDERR) return 2u;  // FILE_TYPE_CHAR
        if (file_handles.count(h)) return 1u;  // FILE_TYPE_DISK
        set_last_error(6);  // ERROR_INVALID_HANDLE
        return 0u;  // FILE_TYPE_UNKNOWN
    });
    reg.add("kernel32.dll", "SetConsoleCtrlHandler", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "FlushFileBuffers", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "CloseHandle", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        auto it=file_handles.find(c.arg(0));if(it!=file_handles.end()){Host::FileCall q{Host::FileCall::Kind::Close};q.handle=it->second;if(!call_file(q)){set_last_error(q.error?q.error:120);return 0;}file_handles.erase(it);return 1;}
        close_sync(c.arg(0));return 1; });
    reg.add("kernel32.dll", "SetHandleCount", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t { return c.arg(0); });

    // d3dx9_36 math: D3DX uses row-major matrices and row-vector transforms.
    auto fp = [](uint32_t v) { float f; std::memcpy(&f, &v, 4); return f; };
    auto fb = [](float f) { uint32_t v; std::memcpy(&v, &f, 4); return v; };
    auto rd = [this, fp](uint32_t p, unsigned n) { std::vector<float> v(n); for (unsigned i = 0; i < n; ++i) v[i] = fp(mem.r32(p + 4 * i)); return v; };
    auto wr = [this, fb](uint32_t p, const float* v, unsigned n) { for (unsigned i = 0; i < n; ++i) mem.w32(p + 4 * i, fb(v[i])); };
    auto matmul = [](const float* a, const float* b, float* o) { for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { o[i*4+j] = 0; for (int k = 0; k < 4; ++k) o[i*4+j] += a[i*4+k] * b[k*4+j]; } };
    auto mat = [this, fp, fb, rd, wr, matmul](const char* name, unsigned argc, auto build) {
        reg.add("d3dx9_36.dll", name, Conv::Stdcall, argc, [this, fp, fb, rd, wr, matmul, build](Cpu& c) -> uint32_t {
            uint32_t out = c.arg(0); float m[16]; build(c, m, rd, fp, matmul); wr(out, m, 16); return out;
        });
    };
    auto ident = [](float* m) { std::fill(m, m + 16, 0.0f); m[0] = m[5] = m[10] = m[15] = 1; };
    mat("D3DXMatrixLookAtLH", 4, [ident](Cpu& c, float* m, auto rd, auto fp, auto) { auto e=rd(c.arg(1),3), a=rd(c.arg(2),3), u=rd(c.arg(3),3); float z[3],x[3],y[3]; auto norm=[](float* v){float n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);for(int i=0;i<3;++i)v[i]/=n;}; for(int i=0;i<3;++i)z[i]=a[i]-e[i]; norm(z); x[0]=u[1]*z[2]-u[2]*z[1];x[1]=u[2]*z[0]-u[0]*z[2];x[2]=u[0]*z[1]-u[1]*z[0];norm(x); y[0]=z[1]*x[2]-z[2]*x[1];y[1]=z[2]*x[0]-z[0]*x[2];y[2]=z[0]*x[1]-z[1]*x[0]; ident(m); for(int i=0;i<3;++i){m[4*i]=x[i];m[4*i+1]=y[i];m[4*i+2]=z[i];} m[12]=-(x[0]*e[0]+x[1]*e[1]+x[2]*e[2]);m[13]=-(y[0]*e[0]+y[1]*e[1]+y[2]*e[2]);m[14]=-(z[0]*e[0]+z[1]*e[1]+z[2]*e[2]); (void)fp; });
    mat("D3DXMatrixMultiply", 3, [](Cpu& c,float* o,auto rd,auto,auto mul){auto a=rd(c.arg(1),16),b=rd(c.arg(2),16);mul(a.data(),b.data(),o);});
    mat("D3DXMatrixPerspectiveFovLH", 5, [ident](Cpu& c,float* m,auto,auto fp,auto){float y=1/std::tan(fp(c.arg(1))/2),a=fp(c.arg(2)),n=fp(c.arg(3)),f=fp(c.arg(4));std::fill(m,m+16,0.0f);m[0]=y/a;m[5]=y;m[10]=f/(f-n);m[11]=1;m[14]=-n*f/(f-n);(void)ident;});
    auto rotation = [mat](const char* name, unsigned argc, int axis) { mat(name, argc, [axis](Cpu& c,float* m,auto,auto fp,auto){float a=fp(c.arg(1)),s=std::sin(a),v=std::cos(a);std::fill(m,m+16,0.0f);m[15]=1;if(axis==0){m[0]=1;m[5]=m[10]=v;m[6]=s;m[9]=-s;}else if(axis==1){m[5]=1;m[0]=m[10]=v;m[2]=-s;m[8]=s;}else{m[10]=1;m[0]=m[5]=v;m[1]=s;m[4]=-s;}}); };
    rotation("D3DXMatrixRotationX",2,0); rotation("D3DXMatrixRotationY",2,1); rotation("D3DXMatrixRotationZ",2,2);
    mat("D3DXMatrixRotationYawPitchRoll", 4, [](Cpu& c,float* m,auto,auto fp,auto){float y=fp(c.arg(1)),p=fp(c.arg(2)),r=fp(c.arg(3)),sy=sin(y),cy=cos(y),sp=sin(p),cp=cos(p),sr=sin(r),cr=cos(r);float v[16]={cy*cr+sy*sp*sr,sr*cp,-sy*cr+cy*sp*sr,0,-cy*sr+sy*sp*cr,cr*cp,sr*sy+cy*sp*cr,0,sy*cp,-sp,cy*cp,0,0,0,0,1};std::copy(v,v+16,m);});
    mat("D3DXMatrixScaling", 4, [ident](Cpu& c,float* m,auto,auto fp,auto){ident(m);m[0]=fp(c.arg(1));m[5]=fp(c.arg(2));m[10]=fp(c.arg(3));});
    mat("D3DXMatrixTranslation", 4, [ident](Cpu& c,float* m,auto,auto fp,auto){ident(m);m[12]=fp(c.arg(1));m[13]=fp(c.arg(2));m[14]=fp(c.arg(3));});
    auto vec = [this, fp, fb, rd, wr](const char* name,unsigned argc,auto fn){reg.add("d3dx9_36.dll",name,Conv::Stdcall,argc,[this,fp,fb,rd,wr,fn](Cpu& c)->uint32_t{uint32_t out=c.arg(0);fn(c,out,rd,fp,wr);return out;});};
    vec("D3DXVec3Normalize",2,[](Cpu& c,uint32_t o,auto rd,auto,auto wr){auto v=rd(c.arg(1),3);float n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);for(float& x:v)x=n?x/n:0;wr(o,v.data(),3);});
    auto transform = [vec](const char* name,unsigned argc,bool coord,bool array){vec(name,argc,[coord,array](Cpu& c,uint32_t o,auto rd,auto fp,auto wr){uint32_t in=c.arg(array?2:1),mp=c.arg(array?4:2),count=array?c.arg(5):1,os=array?c.arg(1):12,is=array?c.arg(3):12;auto m=rd(mp,16);for(uint32_t n=0;n<count;++n){auto v=rd(in+n*is,3);float q[4]={0,0,0,0};for(int j=0;j<4;++j)q[j]=v[0]*m[j]+v[1]*m[4+j]+v[2]*m[8+j]+m[12+j];if(coord){float d=q[3];for(int j=0;j<3;++j)q[j]/=d;wr(o+n*os,q,3);}else wr(o+n*os,q,4);(void)fp;}});};
    transform("D3DXVec3Transform",3,false,false); transform("D3DXVec3TransformArray",6,false,true);
    transform("D3DXVec3TransformCoord",3,true,false); transform("D3DXVec3TransformCoordArray",6,true,true);
    // These D3DX operations need a native D3D9 resource or shader compiler. Keep
    // their exports resolvable, but report the documented failure rather than
    // claiming to have created or modified a host resource.
    auto d3dx_notimpl = [](Cpu&) -> uint32_t { return 0x80004001u; }; // E_NOTIMPL
    reg.add("d3dx9_36.dll", "D3DXCreateEffect", Conv::Stdcall, 9, d3dx_notimpl);
    reg.add("d3dx9_36.dll", "D3DXCreateFontA", Conv::Stdcall, 12, d3dx_notimpl);
    reg.add("d3dx9_36.dll", "D3DXCreateTexture", Conv::Stdcall, 7, d3dx_notimpl);
    reg.add("d3dx9_36.dll", "D3DXCreateTextureFromFileInMemoryEx", Conv::Stdcall, 15, d3dx_notimpl);
    reg.add("d3dx9_36.dll", "D3DXFillTexture", Conv::Stdcall, 3, d3dx_notimpl);
    reg.add("d3dx9_36.dll", "D3DXGetImageInfoFromFileInMemory", Conv::Stdcall, 3, d3dx_notimpl);
    reg.add("d3dx9_36.dll", "D3DXLoadSurfaceFromSurface", Conv::Stdcall, 8, d3dx_notimpl);
    reg.add("d3dx9_36.dll", "D3DXSaveSurfaceToFileA", Conv::Stdcall, 5, d3dx_notimpl);
    // Shlwapi contracts: filename lookup returns a pointer into the input; remove-extension is void; remove-file-spec returns nonzero only when something was removed. See https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathfindfilenamea, https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathremoveextensiona, and https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathremovefilespeca.
    reg.add("shlwapi.dll","PathFindFileNameA",Conv::Stdcall,1,[this](Cpu& c)->uint32_t{uint32_t p=c.arg(0);if(!p)return 0;std::string s=str(p);size_t i=s.find_last_of("\\\\/:");return p+(i==std::string::npos?0:uint32_t(i+1));});
    reg.add("shlwapi.dll","PathRemoveExtensionA",Conv::Stdcall,1,[this](Cpu& c)->uint32_t{uint32_t p=c.arg(0);if(!p)return 0;std::string s=str(p);size_t start=s.find_last_of("\\\\/:"),dot=s.find_last_of('.');if(dot!=std::string::npos&&(start==std::string::npos||dot>start))mem.w8(p+uint32_t(dot),0);return 0;});
    reg.add("shlwapi.dll","PathRemoveFileSpecA",Conv::Stdcall,1,[this](Cpu& c)->uint32_t{uint32_t p=c.arg(0);if(!p)return 0;std::string s=str(p);auto sep=[](char x){return x=='\\'||x=='/';};size_t n=s.size();bool root=n==1&&sep(s[0]);root|=n==3&&s[1]==':'&&sep(s[2]);if(n>=2&&sep(s[0])&&sep(s[1])){size_t a=s.find_first_not_of("\\\\/",2),b=a==std::string::npos?a:s.find_first_of("\\\\/",a),d=b==std::string::npos?b:s.find_first_not_of("\\\\/",b),e=d==std::string::npos?d:s.find_first_of("\\\\/",d);root|=a!=std::string::npos&&b!=std::string::npos&&d!=std::string::npos&&(e==std::string::npos||e==n-1);}if(root)return 0;if(n&&sep(s[n-1])){mem.w8(p+uint32_t(n-1),0);return 1;}size_t i=s.find_last_of("\\\\/");if(i==std::string::npos)return 0;if(i==0)mem.w8(p+1,0);else if(i==2&&s[1]==':')mem.w8(p+3,0);else mem.w8(p+uint32_t(i),0);return 1;});
    reg.add("shlwapi.dll","PathRemoveFileSpecW",Conv::Stdcall,1,[this](Cpu& c)->uint32_t{uint32_t p=c.arg(0);if(!p)return 0;std::u16string s=wstr(p);auto sep=[](char16_t x){return x==u'\\'||x==u'/';};size_t n=s.size();bool root=n==1&&sep(s[0]);root|=n==3&&s[1]==u':'&&sep(s[2]);if(n>=2&&sep(s[0])&&sep(s[1])){size_t a=s.find_first_not_of(u"\\\\/",2),b=a==std::u16string::npos?a:s.find_first_of(u"\\\\/",a),d=b==std::u16string::npos?b:s.find_first_not_of(u"\\\\/",b),e=d==std::u16string::npos?d:s.find_first_of(u"\\\\/",d);root|=a!=std::u16string::npos&&b!=std::u16string::npos&&d!=std::u16string::npos&&(e==std::u16string::npos||e==n-1);}if(root)return 0;if(n&&sep(s[n-1])){mem.w(p+uint32_t((n-1)*2),0,2);return 1;}size_t i=s.find_last_of(u"\\\\/");if(i==std::u16string::npos)return 0;if(i==0)mem.w(p+2,0,2);else if(i==2&&s[1]==u':')mem.w(p+6,0,2);else mem.w(p+uint32_t(i*2),0,2);return 1;});
    auto write_a = [this](uint32_t dst, const std::string& text) { for (size_t i=0;i<=text.size();++i) mem.w8(dst+uint32_t(i),i<text.size()?uint8_t(text[i]):0); };
    auto write_w = [this](uint32_t dst, const std::u16string& text) { for (size_t i=0;i<=text.size();++i) mem.w(dst+uint32_t(i*2),i<text.size()?uint16_t(text[i]):0,2); };
    auto path_file_exists_a = [this](Cpu& c)->uint32_t {
#if defined(_WIN32)
        std::string path=str(c.arg(0)); return !path.empty()&&::GetFileAttributesA(path.c_str())!=INVALID_FILE_ATTRIBUTES;
#else
        (void)c; return 0;
#endif
    };
    auto path_file_exists_w = [this](Cpu& c)->uint32_t {
#if defined(_WIN32)
        std::u16string path=wstr(c.arg(0)); std::wstring native(path.begin(),path.end());
        return !native.empty()&&::GetFileAttributesW(native.c_str())!=INVALID_FILE_ATTRIBUTES;
#else
        (void)c; return 0;
#endif
    };
    reg.add("shlwapi.dll","PathFileExistsA",Conv::Stdcall,1,path_file_exists_a);
    reg.add("shlwapi.dll","PathFileExistsW",Conv::Stdcall,1,path_file_exists_w);
    for(bool wide : {false,true}) {
        const std::string suffix=wide?"W":"A";
        reg.add("shlwapi.dll","PathCombine"+suffix,Conv::Stdcall,3,[this,wide,write_a,write_w](Cpu& c)->uint32_t {
            uint32_t dst=c.arg(0);if(!dst)return 0;
            if(wide) {
                std::u16string base=wstr(c.arg(1)),leaf=wstr(c.arg(2)),joined;
                const bool absolute=(!leaf.empty()&&(leaf[0]==u'\\'||leaf[0]==u'/'))||(leaf.size()>1&&leaf[1]==u':');
                if(absolute)joined=leaf;else {joined=base;if(!joined.empty()&&!leaf.empty()&&joined.back()!=u'\\'&&joined.back()!=u'/')joined.push_back(u'\\');joined+=leaf;}
                write_w(dst,joined);
            } else {
                std::string base=str(c.arg(1)),leaf=str(c.arg(2)),joined;
                const bool absolute=(!leaf.empty()&&(leaf[0]=='\\'||leaf[0]=='/'))||(leaf.size()>1&&leaf[1]==':');
                if(absolute)joined=leaf;else {joined=base;if(!joined.empty()&&!leaf.empty()&&joined.back()!='\\'&&joined.back()!='/')joined.push_back('\\');joined+=leaf;}
                write_a(dst,joined);
            }
            return dst;
        });
        reg.add("shlwapi.dll","PathAppend"+suffix,Conv::Stdcall,2,[this,wide,write_a,write_w](Cpu& c)->uint32_t {
            uint32_t dst=c.arg(0);if(!dst)return 0;
            if(wide) {std::u16string base=wstr(dst),leaf=wstr(c.arg(1));if(!base.empty()&&!leaf.empty()&&base.back()!=u'\\'&&base.back()!=u'/')base.push_back(u'\\');base+=leaf;write_w(dst,base);}
            else {std::string base=str(dst),leaf=str(c.arg(1));if(!base.empty()&&!leaf.empty()&&base.back()!='\\'&&base.back()!='/')base.push_back('\\');base+=leaf;write_a(dst,base);}
            return 1;
        });
        reg.add("shlwapi.dll","PathFindFileName"+suffix,Conv::Stdcall,1,[this,wide](Cpu& c)->uint32_t {
            uint32_t p=c.arg(0);if(!p)return 0;
            if(wide){auto s=wstr(p);size_t i=s.find_last_of(u"\\\\/:");return p+uint32_t((i==std::u16string::npos?0:i+1)*2);}
            auto s=str(p);size_t i=s.find_last_of("\\\\/:");return p+uint32_t(i==std::string::npos?0:i+1);
        });
        reg.add("shlwapi.dll","PathFindExtension"+suffix,Conv::Stdcall,1,[this,wide](Cpu& c)->uint32_t {
            uint32_t p=c.arg(0);if(!p)return 0;
            if(wide){auto s=wstr(p);size_t slash=s.find_last_of(u"\\\\/"),dot=s.find_last_of(u'.');return p+uint32_t((dot!=std::u16string::npos&&(slash==std::u16string::npos||dot>slash)?dot:s.size())*2);}
            auto s=str(p);size_t slash=s.find_last_of("\\\\/"),dot=s.find_last_of('.');return p+uint32_t(dot!=std::string::npos&&(slash==std::string::npos||dot>slash)?dot:s.size());
        });
        reg.add("shlwapi.dll","PathIsRelative"+suffix,Conv::Stdcall,1,[this,wide](Cpu& c)->uint32_t {
            if(wide){auto s=wstr(c.arg(0));return s.empty()||((s[0]!=u'\\'&&s[0]!=u'/')&&!(s.size()>2&&s[1]==u':'&&(s[2]==u'\\'||s[2]==u'/')));}
            auto s=str(c.arg(0));return s.empty()||((s[0]!='\\'&&s[0]!='/')&&!(s.size()>2&&s[1]==':'&&(s[2]=='\\'||s[2]=='/')));
        });
        reg.add("shlwapi.dll","StrCmpI"+suffix,Conv::Stdcall,2,[this,wide](Cpu& c)->uint32_t {
            if(wide){auto a=wstr(c.arg(0)),b=wstr(c.arg(1));for(auto& ch:a)if(ch>=u'A'&&ch<=u'Z')ch+=u'a'-u'A';for(auto& ch:b)if(ch>=u'A'&&ch<=u'Z')ch+=u'a'-u'A';return a<b?uint32_t(-1):a>b?1u:0u;}
            auto a=str(c.arg(0)),b=str(c.arg(1));for(char& ch:a)if(ch>='A'&&ch<='Z')ch=char(ch+'a'-'A');for(char& ch:b)if(ch>='A'&&ch<='Z')ch=char(ch+'a'-'A');return a<b?uint32_t(-1):a>b?1u:0u;
        });
        reg.add("shlwapi.dll","StrStrI"+suffix,Conv::Stdcall,2,[this,wide](Cpu& c)->uint32_t {
            uint32_t p=c.arg(0),q=c.arg(1);if(!p||!q)return 0;
            if(wide){auto hay=wstr(p),needle=wstr(q);auto lower=[](char16_t x){return x>=u'A'&&x<=u'Z'?char16_t(x+u'a'-u'A'):x;};for(size_t i=0;i+needle.size()<=hay.size();++i){size_t j=0;for(;j<needle.size()&&lower(hay[i+j])==lower(needle[j]);++j){}if(j==needle.size())return p+uint32_t(i*2);}return 0;}
            auto hay=str(p),needle=str(q);auto lower=[](unsigned char x){return x>='A'&&x<='Z'?static_cast<unsigned char>(x+'a'-'A'):x;};for(size_t i=0;i+needle.size()<=hay.size();++i){size_t j=0;for(;j<needle.size()&&lower(uint8_t(hay[i+j]))==lower(uint8_t(needle[j]));++j){}if(j==needle.size())return p+uint32_t(i);}return 0;
        });
    }

    // msvcrt: CRT startup, heap, strings, stdio
    reg.add("msvcrt.dll", "_lock", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "_unlock", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "__lc_codepage", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return mb_cur_max; });
    reg.add("msvcrt.dll", "wcslen", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        uint32_t n = 0; while (mem.r(c.arg(0) + n * 2, 2)) ++n; return n; });
    reg.add("msvcrt.dll", "strerror", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        static const char* msgs[] = {"No error", "Operation not permitted", "No such file or directory", "No such process"};
        uint32_t e = c.arg(0); return put_str(e < 4 ? msgs[e] : "Unknown error"); });
    reg.add("msvcrt.dll", "localeconv", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t {
        // struct lconv: 10 char* fields then 8 chars; all but the decimal point are "".
        uint32_t p = heap.alloc(48); std::vector<uint8_t> z(48, 0); mem.write_bytes(p, z.data(), 48);
        uint32_t empty = put_str(""), dot = put_str(".");
        for (int i = 0; i < 10; ++i) mem.w32(p + uint32_t(i * 4), i == 0 ? dot : empty);
        return p; });
    // setlocale(category, locale): the only locale this surface implements is
    // "C"/"" (the process default), so a query or a request for it answers
    // "C" and anything else answers NULL, as msvcrt does for an unavailable
    // locale. mingw's CRT calls it from its startup path.
    reg.add("msvcrt.dll", "setlocale", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        uint32_t name = c.arg(1);
        if (name) {
            std::string want = str(name);
            if (!want.empty() && want != "C" && want != "POSIX") return 0;
        }
        return put_str("C"); });
    // Stage 3: guest DLL loading. `table` is captured so a LoadLibrary from
    // guest code can add thunks for the new module's imports.
    reg.add("kernel32.dll", "ResolveDelayLoadedAPI", Conv::Stdcall, 6, [this](Cpu& c) -> uint32_t {
        return resolve_delay_loaded(c);
    });
    reg.add("kernel32.dll", "GetProcAddress", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t module = c.arg(0), name = c.arg(1);
        if (!module || !name) { set_last_error(126); return 0; }
        return get_proc_address(module, name, c.thunks); });
    reg.add("kernel32.dll", "LoadLibraryA", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        return load_guest_dll(&c, str(c.arg(0)), c.thunks); });
    reg.add("kernel32.dll", "LoadLibraryW", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        // UTF-16 in, UTF-8 out: the module names this surface serves are ASCII.
        uint32_t p = c.arg(0); std::string n;
        for (uint32_t i = 0;; ++i) { uint16_t w = uint16_t(mem.r(p + i * 2, 2)); if (!w) break; n.push_back(char(w & 0xff)); }
        return load_guest_dll(&c, n, c.thunks); });
    reg.add("kernel32.dll", "LoadLibraryExA", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        return load_guest_dll(&c, str(c.arg(0)), c.thunks); });
    reg.add("kernel32.dll", "LoadLibraryExW", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); std::string n;
        for (uint32_t i = 0;; ++i) { uint16_t w = uint16_t(mem.r(p + i * 2, 2)); if (!w) break; n.push_back(char(w & 0xff)); }
        return load_guest_dll(&c, n, c.thunks); });
    reg.add("kernel32.dll", "GetModuleHandleW", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        if (!c.arg(0)) return image_base;
        std::string n;
        for (uint32_t i = 0;; ++i) { uint16_t w = uint16_t(mem.r(c.arg(0) + i * 2, 2)); if (!w) break; n.push_back(char(w & 0xff)); }
        return module_by_name(n, c.thunks); });
    reg.add("msvcrt.dll", "__set_app_type", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "__setusermatherr", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "_amsg_exit", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { do_exit(c, c.arg(0)); return 0; });
    reg.add("msvcrt.dll", "__p___initenv", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return initenv; });
    reg.add("msvcrt.dll", "__p__commode", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return commode; });
    reg.add("msvcrt.dll", "__p__fmode", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return fmode; });
    reg.add("msvcrt.dll", "__p___mb_cur_max", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return mb_cur_max; });
    reg.add("msvcrt.dll", "_errno", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return errno_ptr; });
    reg.add("msvcrt.dll", "__p__iob", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return iob_addr; });
    // CRT startup: __getmainargs fills _argc/_argv/_environ and the startupinfo
    // pointer. argv[0] is the exe path, as on Windows.
    reg.add("msvcrt.dll", "__getmainargs", Conv::Cdecl, 5, [this](Cpu& c) -> uint32_t {
        uint32_t argc_p = c.arg(0), argv_p = c.arg(1), env_p = c.arg(2), si_p = c.arg(4);
        if (!argc_p || !argv_p) return 0xFFFFFFFFu;
        uint32_t name = put_str(exe_path);
        uint32_t argv = heap.alloc(8); mem.w32(argv, name); mem.w32(argv + 4, 0);
        mem.w32(argc_p, 1); mem.w32(argv_p, argv); if (env_p) mem.w32(env_p, env_ptr);
        if (si_p) mem.w32(si_p, startupinfo);
        return 0; });
    reg.add("msvcrt.dll", "__wgetmainargs", Conv::Cdecl, 5, [this](Cpu& c) -> uint32_t {
        uint32_t argc_p = c.arg(0), argv_p = c.arg(1), env_p = c.arg(2), si_p = c.arg(4);
        if (!argc_p || !argv_p) return 0xFFFFFFFFu;
        uint32_t name = put_wstr(std::u16string(exe_path.begin(), exe_path.end()));
        uint32_t argv = heap.alloc(8); mem.w32(argv, name); mem.w32(argv + 4, 0);
        mem.w32(argc_p, 1); mem.w32(argv_p, argv); if (env_p) mem.w32(env_p, env_ptr);
        if (si_p) mem.w32(si_p, startupinfo);
        return 0; });
    reg.add("msvcrt.dll", "_get_osfhandle", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        uint32_t fd = c.arg(0);
        return fd == 0 ? Host::H_STDIN : fd == 1 ? Host::H_STDOUT : fd == 2 ? Host::H_STDERR : 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "_set_fmode", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { mem.w32(fmode, c.arg(0)); return 0; });
    reg.add("msvcrt.dll", "_initterm", Conv::Cdecl, 2, [](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0), end = c.arg(1);
        for (; p < end; p += 4) { uint32_t fn = c.mem.r32(p); if (fn) c.call(fn, {}); }
        return 0; });
    reg.add("msvcrt.dll", "_initterm_e", Conv::Cdecl, 2, [](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0), end = c.arg(1);
        for (; p < end; p += 4) { uint32_t fn = c.mem.r32(p); if (fn) { uint32_t r = c.call(fn, {}); if (r) return r; } }
        return 0; });
    reg.add("msvcrt.dll", "atexit", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { atexit_fns.push_back(c.arg(0)); return 0; });
    reg.add("msvcrt.dll", "_onexit", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { atexit_fns.push_back(c.arg(0)); return c.arg(0); });
    reg.add("msvcrt.dll", "_cexit", Conv::Cdecl, 0, [this](Cpu& c) -> uint32_t { run_atexit(c); return 0; });
    reg.add("msvcrt.dll", "exit", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { do_exit(c, c.arg(0)); return 0; });
    reg.add("msvcrt.dll", "_exit", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { c.exited = true; c.exit_code = c.arg(0); return 0; });
    reg.add("msvcrt.dll", "abort", Conv::Cdecl, 0, [this](Cpu& c) -> uint32_t { c.exited = true; c.exit_code = 3; return 0; });
    reg.add("msvcrt.dll", "signal", Conv::Cdecl, 2, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "setvbuf", Conv::Cdecl, 4, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "fflush", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    // heap
    reg.add("msvcrt.dll", "malloc", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return heap.alloc(c.arg(0)); });
    reg.add("msvcrt.dll", "calloc", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        uint32_t n = c.arg(0), sz = c.arg(1); uint64_t total = uint64_t(n) * sz;
        if (total > 0x10000000ull) return 0;
        uint32_t p = heap.alloc(uint32_t(total));
        if (total) { auto z = std::vector<uint8_t>(size_t(total), 0); mem.write_bytes(p, z.data(), uint32_t(total)); }
        return p; });
    reg.add("msvcrt.dll", "free", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { heap.release(c.arg(0)); return 0; });
    reg.add("msvcrt.dll", "realloc", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t { return heap.realloc(c.arg(0), c.arg(1)); });
    reg.add("msvcrt.dll", "_malloc", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return heap.alloc(c.arg(0)); });
    reg.add("msvcrt.dll", "_free", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { heap.release(c.arg(0)); return 0; });
    // rotate intrinsics the MSVC CRT exports (games call them via _lrotl)
    reg.add("msvcrt.dll", "_lrotl", Conv::Cdecl, 2, [](Cpu& c) -> uint32_t { uint32_t v = c.arg(0), n = c.arg(1) & 31; return n ? (v << n) | (v >> (32 - n)) : v; });
    reg.add("msvcrt.dll", "_lrotr", Conv::Cdecl, 2, [](Cpu& c) -> uint32_t { uint32_t v = c.arg(0), n = c.arg(1) & 31; return n ? (v >> n) | (v << (32 - n)) : v; });
    // strings/memory
    reg.add("msvcrt.dll", "strlen", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return uint32_t(str(c.arg(0)).size()); });
    reg.add("msvcrt.dll", "strnlen", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)); return uint32_t(s.size()) < c.arg(1) ? uint32_t(s.size()) : c.arg(1); });
    reg.add("msvcrt.dll", "strcmp", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string a = str(c.arg(0)), b = str(c.arg(1)); int r = a.compare(b); return uint32_t(r < 0 ? -1 : r > 0 ? 1 : 0); });
    reg.add("msvcrt.dll", "strncmp", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        std::string a = str(c.arg(0)), b = str(c.arg(1)); uint32_t n = c.arg(2);
        int r = a.compare(0, n, b, 0, n); return uint32_t(r < 0 ? -1 : r > 0 ? 1 : 0); });
    reg.add("msvcrt.dll", "strcpy", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(1)); mem.write_bytes(c.arg(0), s.c_str(), uint32_t(s.size() + 1)); return c.arg(0); });
    reg.add("msvcrt.dll", "strncpy", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(1)); uint32_t n = c.arg(2);
        std::vector<uint8_t> b(n, 0); for (uint32_t i = 0; i < n && i < s.size(); ++i) b[i] = uint8_t(s[i]);
        mem.write_bytes(c.arg(0), b.data(), n); return c.arg(0); });
    reg.add("msvcrt.dll", "strcat", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string a = str(c.arg(0)), b = str(c.arg(1)); a += b;
        mem.write_bytes(c.arg(0), a.c_str(), uint32_t(a.size() + 1)); return c.arg(0); });
    // strchr(s, 0) returns the terminator, so the search covers the NUL that
    // read_cstr drops.
    reg.add("msvcrt.dll", "strchr", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)); char ch = char(c.arg(1));
        size_t p = ch ? s.find(ch) : s.size();
        return p == std::string::npos ? 0u : c.arg(0) + uint32_t(p); });
    reg.add("msvcrt.dll", "strrchr", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)); char ch = char(c.arg(1));
        size_t p = ch ? s.rfind(ch) : s.size();
        return p == std::string::npos ? 0u : c.arg(0) + uint32_t(p); });
    reg.add("msvcrt.dll", "strstr", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)), n = str(c.arg(1));
        size_t p = s.find(n); return p == std::string::npos ? 0u : c.arg(0) + uint32_t(p); });
    reg.add("msvcrt.dll", "memcpy", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t d = c.arg(0), s = c.arg(1), n = c.arg(2);
        if (n) mem.write_bytes(d, mem.read_bytes(s, n).data(), n);
        return d; });
    reg.add("msvcrt.dll", "memmove", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t d = c.arg(0), s = c.arg(1), n = c.arg(2);
        if (n) { auto b = mem.read_bytes(s, n); mem.write_bytes(d, b.data(), n); } return d; });
    reg.add("msvcrt.dll", "memset", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t d = c.arg(0), n = c.arg(2); uint8_t v = uint8_t(c.arg(1));
        std::vector<uint8_t> b(n, v); if (n) mem.write_bytes(d, b.data(), n); return d; });
    reg.add("msvcrt.dll", "memcmp", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t n = c.arg(2); auto a = mem.read_bytes(c.arg(0), n), b = mem.read_bytes(c.arg(1), n);
        int r = n ? std::memcmp(a.data(), b.data(), n) : 0; return uint32_t(r < 0 ? -1 : r > 0 ? 1 : 0); });
    reg.add("msvcrt.dll", "memchr", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t n = c.arg(2); auto b = mem.read_bytes(c.arg(0), n); uint8_t v = uint8_t(c.arg(1));
        for (uint32_t i = 0; i < n; ++i) if (b[i] == v) return c.arg(0) + i;
        return 0; });
    reg.add("msvcrt.dll", "atoi", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return uint32_t(std::atoi(str(c.arg(0)).c_str())); });
    reg.add("msvcrt.dll", "atol", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return uint32_t(std::atol(str(c.arg(0)).c_str())); });
    reg.add("msvcrt.dll", "toupper", Conv::Cdecl, 1, [](Cpu& c) -> uint32_t { int v = int(c.arg(0)); return uint32_t(v >= 'a' && v <= 'z' ? v - 32 : v); });
    reg.add("msvcrt.dll", "tolower", Conv::Cdecl, 1, [](Cpu& c) -> uint32_t { int v = int(c.arg(0)); return uint32_t(v >= 'A' && v <= 'Z' ? v + 32 : v); });
    reg.add("msvcrt.dll", "_stricmp", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string a = str(c.arg(0)), b = str(c.arg(1));
        for (auto& ch : a) ch = char(tolower((unsigned char)ch));
        for (auto& ch : b) ch = char(tolower((unsigned char)ch));
        int r = a.compare(b); return uint32_t(r < 0 ? -1 : r > 0 ? 1 : 0); });
    // stdio
    reg.add("msvcrt.dll", "printf", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        std::string s = format_stack(c, str(c.arg(0)), 1); emit(1, s); return uint32_t(s.size()); });
    reg.add("msvcrt.dll", "vprintf", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        uint32_t ap = c.arg(1);
        std::string s = format_va(str(c.arg(0)), ap); emit(1, s); return uint32_t(s.size()); });
    reg.add("msvcrt.dll", "fprintf", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(0)); if (fi < 0) return 0xFFFFFFFFu;
        std::string s = format_stack(c, str(c.arg(1)), 2); emit(fi, s); return uint32_t(s.size()); });
    reg.add("msvcrt.dll", "vfprintf", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(0)); if (fi < 0) return 0xFFFFFFFFu;
        uint32_t ap = c.arg(2);  // an i386 va_list is a pointer to the first vararg
        std::string s = format_va(str(c.arg(1)), ap); emit(fi, s); return uint32_t(s.size()); });
    // The non-v forms take their varargs on the stack (first one at arg 2 or 3);
    // only the v* forms receive a va_list pointer.
    reg.add("msvcrt.dll", "sprintf", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        return uint32_t(format_stack_to(c, c.arg(1), 2, c.arg(0), -1, true)); });
    reg.add("msvcrt.dll", "_snprintf", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        return uint32_t(format_stack_to(c, c.arg(2), 3, c.arg(0), int(c.arg(1)), true)); });
    reg.add("msvcrt.dll", "snprintf", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        return uint32_t(format_stack_to(c, c.arg(2), 3, c.arg(0), int(c.arg(1)), true)); });
    reg.add("msvcrt.dll", "vsprintf", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        return uint32_t(format_va_to(c.arg(1), c.arg(2), c.arg(0), -1, true)); });
    reg.add("msvcrt.dll", "_vsnprintf", Conv::Cdecl, 4, [this](Cpu& c) -> uint32_t {
        return uint32_t(format_va_to(c.arg(2), c.arg(3), c.arg(0), int(c.arg(1)), true)); });
    reg.add("msvcrt.dll", "puts", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)); emit(1, s + "\n"); return uint32_t(s.size() + 1); });
    reg.add("msvcrt.dll", "fputs", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(1)); if (fi < 0) return 0xFFFFFFFFu;
        emit(fi, str(c.arg(0))); return 0; });
    reg.add("msvcrt.dll", "putchar", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        emit(1, std::string(1, char(c.arg(0)))); return c.arg(0) & 0xff; });
    reg.add("msvcrt.dll", "fputc", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(1)); if (fi < 0) return 0xFFFFFFFFu;
        emit(fi, std::string(1, char(c.arg(0)))); return c.arg(0) & 0xff; });
    reg.add("msvcrt.dll", "fwrite", Conv::Cdecl, 4, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(3)); if (fi < 0) return 0;
        uint32_t sz = c.arg(1), n = c.arg(2), total = sz * n;
        if (total) { auto b = mem.read_bytes(c.arg(0), total); emit(fi, std::string(b.begin(), b.end())); }
        return n; });
    reg.add("msvcrt.dll", "fread", Conv::Cdecl, 4, [this](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "ferror", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "feof", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("msvcrt.dll", "_fileno", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { int fi = file_index(c.arg(0)); return fi < 0 ? 0xFFFFFFFFu : uint32_t(fi); });
    reg.add("msvcrt.dll", "_isatty", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("msvcrt.dll", "setbuf", Conv::Cdecl, 2, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "_setmode", Conv::Cdecl, 2, [](Cpu&) -> uint32_t { return 0; });
    // The remaining FILE*-based stdio the C++ streams layer calls on the three
    // standard streams. There is no filesystem: fopen answers NULL (the C++
    // fixture never opens files), and the position/character calls on the
    // standard streams behave like their unbuffered counterparts above.
    reg.add("msvcrt.dll", "fopen", Conv::Cdecl, 2, [this](Cpu&) -> uint32_t { set_last_error(2); return 0; });  // ENOENT
    reg.add("msvcrt.dll", "_wfopen", Conv::Cdecl, 2, [this](Cpu&) -> uint32_t { set_last_error(2); return 0; });
    reg.add("msvcrt.dll", "fclose", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return file_index(c.arg(0)) < 0 ? 0xFFFFFFFFu : 0u; });
    reg.add("msvcrt.dll", "getc", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { (void)c; return 0xFFFFFFFFu; });  // EOF: no input
    reg.add("msvcrt.dll", "putc", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(1)); if (fi < 0) return 0xFFFFFFFFu;
        emit(fi, std::string(1, char(c.arg(0)))); return c.arg(0) & 0xff; });
    reg.add("msvcrt.dll", "getwc", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "putwc", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(1)); if (fi < 0) return 0xFFFFFFFFu;
        uint16_t w = uint16_t(c.arg(0)); emit(fi, std::string(1, char(w & 0xff))); return w; });
    reg.add("msvcrt.dll", "ungetc", Conv::Cdecl, 2, [this](Cpu&) -> uint32_t { return 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "ungetwc", Conv::Cdecl, 2, [](Cpu&) -> uint32_t { return 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "fgetpos", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        // fpos_t on i386 msvcrt is a 64-bit value; a console stream has no position.
        if (c.arg(1)) { mem.w32(c.arg(1), 0); mem.w32(c.arg(1) + 4, 0); } return 0; });
    reg.add("msvcrt.dll", "fsetpos", Conv::Cdecl, 2, [this](Cpu&) -> uint32_t { set_last_error(9); return 0xFFFFFFFFu; });  // EBADF
    reg.add("msvcrt.dll", "_lseeki64", Conv::Cdecl, 4, [this](Cpu&) -> uint32_t { set_last_error(9); return 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "_read", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t { (void)c; return 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "_write", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t fd = c.arg(0), buf = c.arg(1), n = c.arg(2);
        if (fd != 1 && fd != 2) { set_last_error(9); return 0xFFFFFFFFu; }
        if (n) { auto b = mem.read_bytes(buf, n); emit(int(fd), std::string(b.begin(), b.end())); }
        return n; });
    reg.add("msvcrt.dll", "_fdopen", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        uint32_t fd = c.arg(0); return fd <= 2 ? file_ptr(int(fd)) : 0u; });
    // File stat/length on a handle we do not serve: fail like the real API.
    // All three names the mingw line imports: _fstati64 (nix), _fstat64
    // (Debian mingw 13) and _fstat (the 32-bit form CI's runner hit).
    reg.add("msvcrt.dll", "_fstati64", Conv::Cdecl, 2, [this](Cpu&) -> uint32_t { set_last_error(9); return 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "_fstat64", Conv::Cdecl, 2, [this](Cpu&) -> uint32_t { set_last_error(9); return 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "_fstat", Conv::Cdecl, 2, [this](Cpu&) -> uint32_t { set_last_error(9); return 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "_filelengthi64", Conv::Cdecl, 1, [this](Cpu&) -> uint32_t { set_last_error(9); return 0xFFFFFFFFu; });
    // wide ctype / collation: the "C" locale answers ASCII-only tables.
    reg.add("msvcrt.dll", "iswctype", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        uint16_t w = uint16_t(c.arg(0)); uint32_t t = c.arg(1);
        // _SPACE=0x1 _PRINT=0x2 _LOWER=0x4 _UPPER=0x8 _DIGIT=0x10 _ALPHA=0x100
        int r = 0;
        if (w == ' ' || (w >= 9 && w <= 13)) r |= 0x1;
        if (w >= 0x20 && w < 0x7f) r |= 0x2;
        if (w >= 'a' && w <= 'z') r |= 0x4 | 0x100;
        if (w >= 'A' && w <= 'Z') r |= 0x8 | 0x100;
        if (w >= '0' && w <= '9') r |= 0x10 | 0x100;
        return uint32_t(t & 0x1ff ? (r & t ? 1 : 0) : 0); });
    reg.add("msvcrt.dll", "towlower", Conv::Cdecl, 1, [](Cpu& c) -> uint32_t {
        uint16_t w = uint16_t(c.arg(0)); return uint32_t(w >= 'A' && w <= 'Z' ? w + 32 : w); });
    reg.add("msvcrt.dll", "towupper", Conv::Cdecl, 1, [](Cpu& c) -> uint32_t {
        uint16_t w = uint16_t(c.arg(0)); return uint32_t(w >= 'a' && w <= 'z' ? w - 32 : w); });
    // strcoll/wcscoll/strxfrm/wcsxfrm: the "C" locale collates by strcmp order.
    reg.add("msvcrt.dll", "strcoll", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string a = str(c.arg(0)), b = str(c.arg(1)); int r = a.compare(b); return uint32_t(r < 0 ? -1 : r > 0 ? 1 : 0); });
    reg.add("msvcrt.dll", "wcscoll", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        auto wa = wstr(c.arg(0)), wb = wstr(c.arg(1)); int r = wa < wb ? -1 : wb < wa ? 1 : 0; return uint32_t(r); });
    reg.add("msvcrt.dll", "strxfrm", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(1)); uint32_t n = c.arg(2);
        if (c.arg(0) && n) { uint32_t put = uint32_t(s.size()) < n ? uint32_t(s.size()) : n - 1;
            mem.write_bytes(c.arg(0), s.data(), put); mem.w8(c.arg(0) + put, 0); }
        return uint32_t(s.size()); });
    reg.add("msvcrt.dll", "wcsxfrm", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        auto w = wstr(c.arg(1)); uint32_t n = c.arg(2);
        if (c.arg(0) && n) { uint32_t put = uint32_t(w.size()) < n ? uint32_t(w.size()) : n - 1;
            for (uint32_t i = 0; i < put; ++i) { mem.w8(c.arg(0) + i * 2, uint8_t(w[i])); mem.w8(c.arg(0) + i * 2 + 1, uint8_t(w[i] >> 8)); }
            mem.w8(c.arg(0) + put * 2, 0); mem.w8(c.arg(0) + put * 2 + 1, 0); }
        return uint32_t(w.size()); });
    reg.add("msvcrt.dll", "strftime", Conv::Cdecl, 4, [this](Cpu& c) -> uint32_t {
        // size_t strftime(char *dest, size_t maxsize, const char *format,
        // const struct tm *tm) — dest/maxsize first (time.h), format third.
        // A real (deterministic) formatter: reads the guest struct tm
        // (int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday,
        // tm_yday, tm_isdst — the layout time.h has had since 1989) and
        // renders the common specifiers. With the old arg order (format read
        // from the destination) the output was garbage, which the fixture's
        // exact comparison discriminates.
        uint32_t out = c.arg(0), cap = c.arg(1);
        std::string fmt = str(c.arg(2));
        uint32_t tm_p = c.arg(3);
        static const char* wday_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        static const char* mon_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        int sec = 0, minute = 0, hour = 0, mday = 1, mon = 0, year = 1970, wday = 4, yday = 0;
        if (tm_p) {
            sec = int32_t(mem.r32(tm_p)); minute = int32_t(mem.r32(tm_p + 4));
            hour = int32_t(mem.r32(tm_p + 8)); mday = int32_t(mem.r32(tm_p + 12));
            mon = int32_t(mem.r32(tm_p + 16)); year = int32_t(mem.r32(tm_p + 20));
            wday = int32_t(mem.r32(tm_p + 24)); yday = int32_t(mem.r32(tm_p + 28));
        }
        std::string r;
        char num[12];
        for (size_t i = 0; i < fmt.size(); ++i) {
            if (fmt[i] != '%') { r.push_back(fmt[i]); continue; }
            if (++i >= fmt.size()) break;
            switch (fmt[i]) {
            case 'Y': snprintf(num, sizeof num, "%d", year + 1900); r += num; break;
            case 'm': snprintf(num, sizeof num, "%02d", mon + 1); r += num; break;
            case 'd': snprintf(num, sizeof num, "%02d", mday); r += num; break;
            case 'H': snprintf(num, sizeof num, "%02d", hour); r += num; break;
            case 'M': snprintf(num, sizeof num, "%02d", minute); r += num; break;
            case 'S': snprintf(num, sizeof num, "%02d", sec); r += num; break;
            case 'a': if (wday >= 0 && wday < 7) r += wday_names[wday]; break;
            case 'b': case 'h': if (mon >= 0 && mon < 12) r += mon_names[mon]; break;
            case 'j': snprintf(num, sizeof num, "%03d", yday + 1); r += num; break;
            case '%': r.push_back('%'); break;
            default: r.push_back('%'); r.push_back(fmt[i]); break;  // unknown: keep both, like a failed conversion
            }
        }
        if (out && cap && r.size() < cap) {
            mem.write_bytes(out, r.data(), uint32_t(r.size()));
            mem.w8(out + uint32_t(r.size()), 0);
            return uint32_t(r.size());
        }
        if (out && cap) mem.w8(out, 0);  // too big: empty string, as the CRT answers
        return 0; });
    // size_t wcsftime(wchar_t *dest, size_t maxsize, const wchar_t *format,
    // const struct tm *tm) — the wide form; a failed conversion answers 0,
    // which the CRT treats as "no time string" (a legal answer).
    reg.add("msvcrt.dll", "wcsftime", Conv::Cdecl, 4, [this](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "strtoul", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)); char* e = nullptr;
        unsigned long v = std::strtoul(s.c_str(), &e, int(c.arg(2)));
        if (c.arg(1)) mem.w32(c.arg(1), c.arg(0) + uint32_t(e ? e - s.c_str() : 0));
        return uint32_t(v); });
    reg.add("msvcrt.dll", "getenv", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "clock", Conv::Cdecl, 0, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "time", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        uint32_t v = 1760000000u; if (c.arg(0)) mem.w32(c.arg(0), v); return v; });
    reg.add("msvcrt.dll", "qsort", Conv::Cdecl, 4, [](Cpu& c) -> uint32_t {
        uint32_t base = c.arg(0), n = c.arg(1), size = c.arg(2), cmp = c.arg(3);
        if (size == 0 || size > 4096 || uint64_t(n) * size > (1u << 26)) throw Fault(FaultKind::AccessViolation, c.eip, base, "qsort: bad size");
        auto less = [&](uint32_t i, uint32_t j) { return int32_t(c.call(cmp, {base + i * size, base + j * size})) < 0; };
        auto swap = [&](uint32_t i, uint32_t j) { auto a = c.mem.read_bytes(base + i * size, size), b = c.mem.read_bytes(base + j * size, size);
            c.mem.write_bytes(base + i * size, b.data(), size); c.mem.write_bytes(base + j * size, a.data(), size); };
        auto sift = [&](uint32_t root, uint32_t end) {
            for (;;) { uint32_t child = 2 * root + 1; if (child >= end) return;
                if (child + 1 < end && less(child, child + 1)) ++child;
                if (!less(root, child)) return;
                swap(root, child); root = child; } };
        if (n > 1) { for (uint32_t i = n / 2; i-- > 0;) sift(i, n); for (uint32_t e = n - 1; e > 0; --e) { swap(0, e); sift(0, e); } }
        return 0; });

    // ntdll: the primitives a fully static mingw C++ runtime leans on.
    // Keyed events are NT's futex: a single-threaded surface acquires and
    // releases without contention, so both waits answer "woke" and both
    // releases succeed. Everything here matches what mcfgthread's static
    // build and the CRT's once-init call at startup and exit.
    // The mem family: real ntdll.dll exports memcpy/memmove/memset/memchr
    // (msvcrt forwards to it there), so a guest forwarder naming
    // "ntdll.memcpy" resolves to the same behaviour as msvcrt's.
    reg.add("ntdll.dll", "memcpy", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t d = c.arg(0), s = c.arg(1), n = c.arg(2);
        if (n) mem.write_bytes(d, mem.read_bytes(s, n).data(), n);
        return d; });
    reg.add("ntdll.dll", "memmove", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t d = c.arg(0), s = c.arg(1), n = c.arg(2);
        if (n) { auto b = mem.read_bytes(s, n); mem.write_bytes(d, b.data(), n); } return d; });
    reg.add("ntdll.dll", "memset", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t d = c.arg(0), n = c.arg(2); uint8_t v = uint8_t(c.arg(1));
        std::vector<uint8_t> b(n, v); if (n) mem.write_bytes(d, b.data(), n); return d; });
    reg.add("ntdll.dll", "NtWaitForKeyedEvent", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t {
        run_pending_threads(c); return 0; });
    reg.add("ntdll.dll", "NtReleaseKeyedEvent", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t {
        run_pending_threads(c); return 0; });
    reg.add("ntdll.dll", "NtWaitForSingleObject", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        run_pending_threads(c); return 0; });  // STATUS_WAIT_0
    reg.add("ntdll.dll", "NtDelayExecution", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        run_pending_threads(c); return 0; });  // STATUS_SUCCESS: a delay is a yield point
    reg.add("ntdll.dll", "NtClose", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("ntdll.dll", "NtDuplicateObject", Conv::Stdcall, 7, [](Cpu&) -> uint32_t { return 0; });
    // Section objects: NtCreateSection/NtMapViewOfSection back a shared
    // memory block (mcfgthread's globals arena). One process, so a section is
    // just a zero-filled guest mapping; the handle records its size.
    // (Section mapping itself backs the loader's memory module path; this
    // surface maps images itself, so an unmapped-section answer keeps callers
    // off that path.)
    // images itself, so an unmapped section answer keeps callers off that path.
    // NtCreateSection(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PLARGE_INTEGER,
    // ULONG, ULONG, HANDLE) — seven parameters (winnt.h/winternl.h); the size
    // pointer is the 4th.
    reg.add("ntdll.dll", "NtCreateSection", Conv::Stdcall, 7, [this](Cpu& c) -> uint32_t {
        uint32_t out = c.arg(0), size_p = c.arg(3);
        uint64_t size = size_p ? (uint64_t(mem.r32(size_p + 4)) << 32) | mem.r32(size_p) : 0x10000u;
        if (!size || size > 0x10000000ull) return 0xC0000043u;  // invalid page protection/size
        if (!out) return 0xC000000Du;  // invalid parameter
        uint32_t h = 0x61000000u + ++section_serial;
        sections[h] = uint32_t(size);
        mem.w32(out, h);
        return 0; });
    reg.add("ntdll.dll", "NtMapViewOfSection", Conv::Stdcall, 10, [this](Cpu& c) -> uint32_t {
        auto s = sections.find(c.arg(0));
        if (s == sections.end()) return 0xC0000008u;  // invalid handle
        uint32_t base_out = c.arg(2), view_size_out = c.arg(6);
        uint32_t size = (s->second + 0xfffu) & ~0xfffu;
        if (section_top + size < section_top) return 0xC0000017u;  // no memory
        uint32_t a = section_top; section_top += size;
        mem.map(a, size, PROT_R | PROT_W);
        auto z = std::vector<uint8_t>(size, 0); mem.write_bytes(a, z.data(), size);
        if (base_out) mem.w32(base_out, a);
        if (view_size_out) mem.w32(view_size_out, s->second);
        return 0; });
    reg.add("ntdll.dll", "NtUnmapViewOfSection", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 0; });
    reg.add("ntdll.dll", "NtQuerySystemTime", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        if (c.arg(0)) { mem.w32(c.arg(0), 0x00A8B800u); mem.w32(c.arg(0) + 4, 0x01DC0000u); } return 0; });
    reg.add("ntdll.dll", "NtQueryInformationFile", Conv::Stdcall, 5, [](Cpu&) -> uint32_t { return 0xC0000008u; });  // invalid handle
    reg.add("ntdll.dll", "NtRaiseHardError", Conv::Stdcall, 6, [this](Cpu& c) -> uint32_t { do_exit(c, 3); return 0; });
    reg.add("ntdll.dll", "RtlDllShutdownInProgress", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0; });
    // Wine-built CRT donors use Wine's private debug ABI, not a Windows ntdll
    // API. Keep every channel disabled: no host logging is fabricated, and the
    // header's -1 tells Wine's debug macros not to format a line.
    const uint32_t wine_debug_empty = put_str("");
    reg.add("ntdll.dll", "__wine_dbg_get_channel_flags", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("ntdll.dll", "__wine_dbg_header", Conv::Cdecl, 3, [](Cpu&) -> uint32_t { return uint32_t(-1); });
    reg.add("ntdll.dll", "__wine_dbg_output", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("ntdll.dll", "__wine_dbg_strdup", Conv::Cdecl, 1, [wine_debug_empty](Cpu&) -> uint32_t { return wine_debug_empty; });

    // kernel32/ntdll bridge: the CRT startup asks for the named-object
    // directory handle; a fake handle keeps the caller off the real path.
    reg.add("kernel32.dll", "BaseGetNamedObjectDirectory", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return 0x60000100u; });
}

}
