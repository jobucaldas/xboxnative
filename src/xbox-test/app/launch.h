// Launcher Play: write the request, map the guest dependency graph and execute
// TLS/DllMain/EXE entry through play_runtime.h. Missing static imports stop with
// their exact names instead of returning probe placeholders. The common result
// publisher records return, exception, loader failure or watchdog timeout in
// boot.log and launch_result.json; detailed events remain in launch_probe.log.
#pragma once
#include <memory>
#include <set>
#include <winrt/Windows.System.h>
#include "host/wxl_mapper.h"
#include "host/mlp_probe.h"
#include "../uwph/user32/probe.h"
#include "launch_status.h"
#include "launch_target.h"
#include "x86_route.h"
#include "installs.h"

struct LaunchState {
    std::atomic<int> phase{0};          // 0 never, 1 starting, 2 finished, 3 guest running
    std::atomic<uint32_t> appid{0};
    std::atomic<int> gen{0};
    double started = 0;                 // render-thread read only (set before phase=1)
    std::atomic<double> loaderStarted{0}; // when the guest loader began (0: still preparing Steam/client)
    std::atomic<bool> quitRequested{false}; // gquit requested a graceful guest exit
    // The 32-bit path (host/x86) has no play_runtime::State: the interpreter runs
    // the guest synchronously and reports these itself, so launch_result.json
    // says whether game code ran and how it ended for both engines.
    std::atomic<bool> pe32Executed{false}, pe32Finished{false};
    std::atomic<uint32_t> pe32ExitCode{0};
    std::mutex m; std::string result, attempt; bool ok=false;
};
static LaunchState g_launch;
// Set while the console self-test (selftest.h) owns Play: it starts a guest
// without a launch, so a Play from the UI must not start a second one or take
// the probe sink away from the run.
static std::atomic<bool> g_selftest_busy{false};

// ---- install state (marker dl_<appid>.done written by the downloader; see installs.h)
using InstallInfo = installs::Info;
static InstallInfo read_install(uint32_t appid) { return installs::read(g_dir, appid); }
static std::string json_esc(const std::string& s) {
    std::string o; for (unsigned char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += (char)c; } else if (c < 32) o += ' '; else o += (char)c; } return o;
}
static std::string utc_iso() { SYSTEMTIME t; GetSystemTime(&t); char b[40]; snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02dZ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond); return b; }
static void write_text(const std::wstring& p, const std::string& s) { FILE* f = _wfopen(p.c_str(), L"wb"); if (f) { fwrite(s.data(), 1, s.size(), f); fclose(f); } }

// ---- probe sink: every line to launch_probe.log, key lines to boot.log
static std::mutex g_lpm; static FILE* g_lpf = nullptr;
static unsigned g_lpMissing = 0, g_lpStubbed = 0, g_lpStubCalls = 0, g_lpLines = 0; static std::string g_lpLastFail;
static void lp_sink(const char* s) {
    std::lock_guard<std::mutex> l(g_lpm);
    ++g_lpLines;
    if (g_lpf) { fprintf(g_lpf, "%s\n", s); fflush(g_lpf); }
    if (!strncmp(s, "MLP MISSING", 11)) ++g_lpMissing;
    else if (!strncmp(s, "MLP STUB-CALL", 13)) ++g_lpStubCalls;
    else if (!strncmp(s, "MLP STUB ", 9)) ++g_lpStubbed;
    if (strstr(s, "FAIL") && !strstr(s, "PROBE-FAILED")) g_lpLastFail = s;
    bool key = strstr(s, "PRUN EVENT") || strstr(s, "GAP") || strstr(s, "FAIL") || strstr(s, "SUMMARY") || strstr(s, "MLP IMAGE") || strstr(s, "mapped") || strstr(s, "PROBE") || strstr(s, "STUB-CALL") || strstr(s, "x86 gap:") || strstr(s, "draw state");
    if (key) logf("launch-probe: %.300s", s);
}
#include "wine_gap.h"
#include "host/wxl_pe32.h"
#include "host/x86/x86_play.h"
#include "pe32_probe.h"
#include "donors/probe.h"

#include "dxil_runtime.h"
#include "host/play_runtime.h"
#include "host/mono_host.h"
#include "../uwph/shell32/folder_chain.h"
#include "steam_client_package.h"
#include "steam_token_store.h"
#include "steam_genuine.h"
#include "steam_auth.h"

static void lp_publish(int gen,uint32_t appid,const std::string& result,bool ok,int mapped,int total,const char* status,bool terminal);
struct LpCtx { std::wstring root, installRoot, args, cwd; std::vector<std::wstring> names; int gen = 0; uint32_t appid = 0; int mapped = 0; unsigned resolved = 0, stubbed = 0; std::string firstMissing, failName, fault; bool ok = false, timedOut = false; };
static int pe_machine(const std::wstring& p) {   // 0 = unreadable/not PE, else IMAGE_FILE_MACHINE_*
    FILE* f = _wfopen(p.c_str(), L"rb"); if (!f) return 0; unsigned char h[1024]; size_t n = fread(h, 1, sizeof h, f); fclose(f);
    if (n < 0x40 || h[0] != 'M' || h[1] != 'Z') return 0; uint32_t o = *(uint32_t*)(h + 0x3c); if (o + 6 > n || memcmp(h + o, "PE\0\0", 4)) return 0; return *(uint16_t*)(h + o + 4);
}
// ---- Steam Auto-Cloud for the provider-served game (steam_cloud.h): download
// the player's newer cloud saves before launch, then upload changed saves every
// 5 minutes and once more when the guest finishes (the console can close the
// app without notice). Saves written in the last 30 s wait for the next pass.
#include "steam_cloud.h"
static int cloud_http(bool put,bool https,const std::string& host,const std::string& path,
                      const steam_cloud::Headers& headers,const std::string& upload,std::string& body) {
    try {
        Http::HttpRequestMessage req(put?Http::HttpMethod::Put():Http::HttpMethod::Get(),
            winrt::Windows::Foundation::Uri(to_hstring((https?"https://":"http://")+host+path)));
        std::string contentType;
        for(auto& h:headers) {
            if(!_stricmp(h.first.c_str(),"Content-Type")){contentType=h.second;continue;}
            if(!_stricmp(h.first.c_str(),"Content-Length"))continue;
            req.Headers().TryAppendWithoutValidation(to_hstring(h.first),to_hstring(h.second));
        }
        if(put) {
            winrt::Windows::Storage::Streams::DataWriter writer;
            writer.WriteBytes(array_view<const uint8_t>((const uint8_t*)upload.data(),(const uint8_t*)upload.data()+upload.size()));
            Http::HttpBufferContent content(writer.DetachBuffer());
            if(!contentType.empty())content.Headers().TryAppendWithoutValidation(L"Content-Type",to_hstring(contentType));
            req.Content(content);
        }
        auto resp=client().SendRequestAsync(req).get();
        auto buf=resp.Content().ReadAsBufferAsync().get();
        body.resize(buf.Length());
        if(buf.Length())winrt::Windows::Storage::Streams::DataReader::FromBuffer(buf).ReadBytes(
            array_view<uint8_t>((uint8_t*)&body[0],(uint8_t*)&body[0]+body.size()));
        return (int)resp.StatusCode();
    } catch(...) {return 0;}
}
static std::string read_bytes(const std::wstring& p) {
    std::string out; FILE* f=_wfopen(p.c_str(),L"rb"); if(!f) return out;
    char b[65536]; size_t n; while((n=fread(b,1,sizeof b,f))>0) out.append(b,n); fclose(f); return out;
}
// Ownership gate (the ownership rule): before any emulated Steam component is
// staged, Steam itself must issue the signed-in account an app ownership
// ticket for this app (CM ClientGetAppOwnershipTicket). Steam refuses it for an
// app the account does not own. The genuine ticket is also what Steam's DRM
// wrapper checks (play_runtime set_app_ticket); it is kept in memory only.
static bool owner_ticket(uint32_t appid,std::string& ticket) {
    ticket.clear();
    auto session=user_session();
    if(!session||session->anon||!session->steamid) return false;
    std::string body; pb_u(body,1,appid);
    JobMsg m; if(!session->call(857,body,"",15000,m)||m.emsg!=858) return false;   // ClientGetAppOwnershipTicket(Response)
    uint64_t eresult=2,app=0; std::string bytes;
    for(auto& f:pb_parse(m.body)) { if(f.f==1)eresult=f.u; else if(f.f==2)app=f.u; else if(f.f==3)bytes=f.s; }
    steam_app_ticket::Layout t;
    if(eresult!=1||app!=appid||!steam_app_ticket::parse(bytes,t)||t.appid!=appid||t.steamid!=session->steamid) return false;
    ticket=bytes; return true;
}
// Steam's DRM wrapper (SteamStub) lives in a ".bind" section of the game's
// executable on disk: the section table answers "is this game wrapped"
// without running anything.
static bool steamstub_wrapped(const std::wstring& exe) {
    FILE* f=_wfopen(exe.c_str(),L"rb"); if(!f) return false;
    unsigned char h[4096]; size_t n=fread(h,1,sizeof h,f); fclose(f);
    if(n<0x40||h[0]!='M'||h[1]!='Z') return false;
    uint32_t o=*(uint32_t*)(h+0x3c); if(o+24>n||memcmp(h+o,"PE\0\0",4)) return false;
    uint16_t sections=*(uint16_t*)(h+o+6), optional=*(uint16_t*)(h+o+20);
    size_t table=o+24+optional;
    for(uint16_t i=0;i<sections&&table+40*(i+1)<=n;++i) if(!memcmp(h+table+40*i,".bind\0",6)) return true;
    return false;
}
static std::string sha256_hex(const std::string& data) {
    BCRYPT_HASH_HANDLE h=nullptr; UCHAR d[32]={};
    if(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&h,nullptr,0,nullptr,0,0)<0) return {};
    for(size_t at=0;at<data.size();) { ULONG c=(ULONG)std::min<size_t>(data.size()-at,1u<<30); BCryptHashData(h,(PUCHAR)data.data()+at,c,0); at+=c; }
    BCryptFinishHash(h,d,32,0); BCryptDestroyHash(h);
    static const char* x="0123456789abcdef"; std::string out;
    for(UCHAR b:d){out+=x[b>>4];out+=x[b&15];}
    return out;
}
static bool stage_valve_runtime_package(const std::string& manifest, const char* package,
                                       const char* stamp_name, const char* const* files, size_t count) {
    namespace pkg=steam_client_package;
    pkg::Entry entry;
    if(!pkg::manifest_entry(manifest,package,entry)) return false;
    const std::wstring dir=g_dir+L"\\steam\\client";
    const std::wstring stamp=dir+L"\\"+widen(stamp_name);
    bool present=read_bytes(stamp)==entry.sha256;
    for(size_t i=0;i<count&&present;++i) {
        std::string rel=files[i];
        std::replace(rel.begin(),rel.end(),'/','\\');
        const std::wstring path=dir+L"\\"+widen(rel);
        present=!read_bytes(path).empty();
    }
    if(present) return true;
    std::string zip;
    if(cloud_http(false,true,pkg::host,std::string("/client/")+entry.file,{},{},zip)!=200||sha256_hex(zip)!=entry.sha256) return false;
    DeleteFileW(stamp.c_str()); // rewritten only after every member is stored; a partial store must not look cached
    auto inflater=[](const char* data,size_t size,size_t expected,std::string& out){
        out.resize(expected); z_stream z{}; if(inflateInit2(&z,-15)!=Z_OK) return false;
        z.next_in=(Bytef*)data; z.avail_in=(uInt)size; z.next_out=(Bytef*)&out[0]; z.avail_out=(uInt)expected;
        int rc=inflate(&z,Z_FINISH); bool ok=rc==Z_STREAM_END&&z.total_out==expected; inflateEnd(&z); return ok;};
    for(size_t i=0;i<count;++i) {
        std::string contents;
        if(!pkg::zip_member(zip,files[i],inflater,contents)||contents.empty()) return false;
        std::string rel=files[i];
        const size_t slash=rel.find_last_of('/');
        if(slash!=std::string::npos) CreateDirectoryW((dir+L"\\"+widen(rel.substr(0,slash))).c_str(),nullptr);
        std::replace(rel.begin(),rel.end(),'/','\\');
        const std::wstring target=dir+L"\\"+widen(rel), part=target+L".part";
        write_text(part,contents);
        if(read_bytes(part)!=contents||!MoveFileExW(part.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(part.c_str()); return false;
        }
    }
    write_text(stamp,entry.sha256);
    return read_bytes(stamp)==entry.sha256;
}
// Valve's signed steamclient64.dll in LocalState\steam\client, fetched from
// Valve (steam_client_package.h) once per Steam client update. With Valve's
// manifest unreachable, a cached copy that still carries Valve's signature
// block is used.
static std::wstring valve_client() {
    namespace pkg=steam_client_package;
    const std::wstring dir=g_dir+L"\\steam\\client", dll=dir+L"\\steamclient64.dll", stamp=dir+L"\\package.sha256";
    for(auto d:{g_dir+L"\\steam",dir})CreateDirectoryW(d.c_str(),nullptr);
    auto cached=[&]{
        for(size_t i=0;i<pkg::runtime_file_count;++i) {
            std::string rel=pkg::runtime_files[i];std::replace(rel.begin(),rel.end(),'/','\\');
            if(read_bytes(dir+L"\\"+widen(rel)).empty()) return false;
        }
        for(size_t i=0;i<pkg::codec_runtime_file_count;++i) if(read_bytes(dir+L"\\"+widen(pkg::codec_runtime_files[i])).empty()) return false;
        for(size_t i=0;i<pkg::misc_runtime_file_count;++i) {
            std::string rel=pkg::misc_runtime_files[i];std::replace(rel.begin(),rel.end(),'/','\\');
            if(read_bytes(dir+L"\\"+widen(rel)).empty()) return false;
        }
        return read_bytes(stamp).size()==64&&read_bytes(dir+L"\\codecs.sha256").size()==64&&read_bytes(dir+L"\\misc.sha256").size()==64&&pkg::valve_signature_block(read_bytes(dll));
    };
    std::string text; pkg::Entry e;
    if(cloud_http(false,true,pkg::host,pkg::manifest_path,{},{},text)!=200||!pkg::manifest_entry(text,e)) {
        if(cached()){lp_sink("STEAM client: Valve's client manifest unreachable; using the cached signed client and runtime dependencies");return dll;}
        lp_sink("STEAM client: Valve's client manifest unreachable and nothing cached");return {};
    }
    if(read_bytes(stamp)==e.sha256&&cached()) {
        if(stage_valve_runtime_package(text,pkg::codec_package,"codecs.sha256",pkg::codec_runtime_files,pkg::codec_runtime_file_count)&&
           stage_valve_runtime_package(text,pkg::misc_package,"misc.sha256",pkg::misc_runtime_files,pkg::misc_runtime_file_count)) return dll;
        lp_sink("STEAM client: required Valve codec/helper package could not be verified or staged");return {};
    }
    lp_sink("STEAM client: fetching Valve's signed client from Valve (once per Steam client update)");
    std::string zip;
    if(cloud_http(false,true,pkg::host,std::string("/client/")+e.file,{},{},zip)!=200){lp_sink("STEAM client: package download failed");return {};}
    if(sha256_hex(zip)!=e.sha256){lp_sink("STEAM client: package SHA-256 differs from Valve's manifest; discarded");return {};}
    DeleteFileW(stamp.c_str()); // rewritten only after every member is stored; a partial store must not look cached
    auto inflater=[](const char* data,size_t size,size_t expected,std::string& out){
        out.resize(expected); z_stream z{}; if(inflateInit2(&z,-15)!=Z_OK) return false;
        z.next_in=(Bytef*)data; z.avail_in=(uInt)size; z.next_out=(Bytef*)&out[0]; z.avail_out=(uInt)expected;
        int rc=inflate(&z,Z_FINISH); bool ok=rc==Z_STREAM_END&&z.total_out==expected; inflateEnd(&z); return ok;};
    for(size_t i=0;i<pkg::runtime_file_count;++i) {
        const char* member=pkg::runtime_files[i];
        std::string contents;
        if(!pkg::zip_member(zip,member,inflater,contents)||contents.empty()||
           (strcmp(member,pkg::member)==0&&!pkg::valve_signature_block(contents))) {
            lp_sink("STEAM client: package is missing a required signed-client runtime DLL; discarded");return {};
        }
        std::string rel=member;
        const size_t slash=rel.find_last_of('/');
        if(slash!=std::string::npos) CreateDirectoryW((dir+L"\\"+widen(rel.substr(0,slash))).c_str(),nullptr);
        std::replace(rel.begin(),rel.end(),'/','\\');
        std::wstring target=dir+L"\\"+widen(rel);
        std::wstring part=target+L".part";
        write_text(part,contents);
        if(read_bytes(part)!=contents||!MoveFileExW(part.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            DeleteFileW(part.c_str());lp_sink("STEAM client: could not store a required runtime DLL");return {};
        }
    }
    write_text(stamp,e.sha256);
    if(!stage_valve_runtime_package(text,pkg::codec_package,"codecs.sha256",pkg::codec_runtime_files,pkg::codec_runtime_file_count)||
       !stage_valve_runtime_package(text,pkg::misc_package,"misc.sha256",pkg::misc_runtime_files,pkg::misc_runtime_file_count)) {
        lp_sink("STEAM client: required Valve codec/helper package could not be verified or staged");return {};
    }
    lp_sink("STEAM client: Valve's signed client, codec DLLs and driver-query helpers stored (manifest SHA-256 matched)");
    return dll;
}
// Emulated Steam for the signed-in account's game (the ownership rule), only
// after Steam issued this account an ownership ticket for the app (above). The
// packaged, pinned gbe_fork files are copied to LocalState\steam\<appid> with
// steam_settings for this account; the game's own files are untouched. The
// SteamID is written only to that on-device settings file, never to a log.
//  - Most games: gbe's steam_api64.dll is mapped in place of the game's.
//  - SteamStub-wrapped games: the game keeps its own steam_api64.dll, and the
//    client it loads is Valve's signed steamclient64.dll answered by gbe's
//    emulated client; the DRM gets the genuine ownership ticket.
// Without a packaged emulator, a signed-in account or Steam's ticket nothing
// is staged and the game's Steam calls fail naturally.
static std::wstring g_signedClient;   // set when the client route is staged; registry seeded after regb::init
static uint32_t g_clientAccount=0;
struct GenuineSteamApi {
    using CreatePipe=int(*)(); using CreateUser=int(*)(int*);
    using LoggedOn=bool(*)(int,int); using LogOn=void(*)(int,int,uint64_t); using LogOff=void(*)(int,int);
    using ReleasePipe=bool(*)(int); using ReleaseUser=void(*)(int,int);
    CreatePipe createPipe=nullptr; CreateUser createUser=nullptr; LoggedOn loggedOn=nullptr;
    LogOn logOn=nullptr; LogOff logOff=nullptr; ReleasePipe releasePipe=nullptr; ReleaseUser releaseUser=nullptr;
};
struct GenuineSteamCall { HMODULE module=nullptr; GenuineSteamApi api{}; int pipe=0,user=0,logged=0; uint64_t steamid=0; };
static HMODULE g_genuineSteamModule=nullptr;
static GenuineSteamApi g_genuineSteamLiveApi{};
static int g_genuineSteamUser=0,g_genuineSteamPipe=0;
// This leaf frame intentionally contains no C++ objects: Valve faults are contained by SEH.
static bool genuine_steam_invoke(GenuineSteamCall* c) {
    ULONGLONG started=GetTickCount64(); bool ok=false;
    __try {
        c->pipe=c->api.createPipe(); ok=c->pipe!=0; lp_sink(ok?"STEAM genuine: step=create-pipe ok=1":"STEAM genuine: step=create-pipe ok=0");
        if(ok) { c->user=c->api.createUser(&c->pipe); ok=c->user!=0; lp_sink(ok?"STEAM genuine: step=create-global-user ok=1":"STEAM genuine: step=create-global-user ok=0"); }
        if(ok) {
            c->api.logOn(c->user,c->pipe,c->steamid); lp_sink("STEAM genuine: step=log-on ok=1");
            ULONGLONG deadline=GetTickCount64()+30000;
            while(GetTickCount64()<deadline){if(c->api.loggedOn(c->user,c->pipe)){c->logged=1;break;}Sleep(100);}
            char line[96];snprintf(line,sizeof line,"STEAM genuine: logged-on=%d after=%llu",c->logged,(unsigned long long)(GetTickCount64()-started));lp_sink(line);
            ok=c->logged!=0;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { lp_sink("STEAM genuine: step=valve-exception ok=0"); ok=false; }
    return ok;
}
static void genuine_steam_cleanup(GenuineSteamCall* c) {
    __try {if(c->user&&c->api.logOff)c->api.logOff(c->user,c->pipe);if(c->user&&c->api.releaseUser)c->api.releaseUser(c->pipe,c->user);if(c->pipe&&c->api.releasePipe)c->api.releasePipe(c->pipe);}
    __except(EXCEPTION_EXECUTE_HANDLER) {lp_sink("STEAM genuine: step=cleanup-exception ok=0");}
}
static bool genuine_steam_load(GenuineSteamCall* c,const wchar_t* dll) {
    bool ok=false;
    __try {
        c->module=LoadLibraryExW(dll,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        c->api.createPipe=reinterpret_cast<GenuineSteamApi::CreatePipe>(c->module?GetProcAddress(c->module,"Steam_CreateSteamPipe"):nullptr);
        c->api.createUser=reinterpret_cast<GenuineSteamApi::CreateUser>(c->module?GetProcAddress(c->module,"Steam_CreateGlobalUser"):nullptr);
        c->api.loggedOn=reinterpret_cast<GenuineSteamApi::LoggedOn>(c->module?GetProcAddress(c->module,"Steam_BLoggedOn"):nullptr);
        c->api.logOn=reinterpret_cast<GenuineSteamApi::LogOn>(c->module?GetProcAddress(c->module,"Steam_LogOn"):nullptr);
        c->api.logOff=reinterpret_cast<GenuineSteamApi::LogOff>(c->module?GetProcAddress(c->module,"Steam_LogOff"):nullptr);
        c->api.releasePipe=reinterpret_cast<GenuineSteamApi::ReleasePipe>(c->module?GetProcAddress(c->module,"Steam_BReleaseSteamPipe"):nullptr);
        c->api.releaseUser=reinterpret_cast<GenuineSteamApi::ReleaseUser>(c->module?GetProcAddress(c->module,"Steam_ReleaseUser"):nullptr);
        ok=c->module&&c->api.createPipe&&c->api.createUser&&c->api.loggedOn&&c->api.logOn&&c->api.logOff&&c->api.releasePipe&&c->api.releaseUser;
    } __except(EXCEPTION_EXECUTE_HANDLER) {lp_sink("STEAM genuine: step=load-exception ok=0");}
    return ok;
}
static void genuine_steam_free(HMODULE module) {
    __try {if(module)FreeLibrary(module);}
    __except(EXCEPTION_EXECUTE_HANDLER) {lp_sink("STEAM genuine: step=unload-exception ok=0");}
}
static void genuine_steam_shutdown() {
    if(!g_genuineSteamModule)return;
    GenuineSteamCall c{};c.module=g_genuineSteamModule;c.api=g_genuineSteamLiveApi;c.user=g_genuineSteamUser;c.pipe=g_genuineSteamPipe;
    genuine_steam_cleanup(&c);genuine_steam_free(c.module);
    g_genuineSteamModule=nullptr;g_genuineSteamLiveApi={};g_genuineSteamUser=0;g_genuineSteamPipe=0;
}
// SetDllDirectoryW is outside the UWP header partition; resolve it at runtime.
static BOOL genuine_set_dll_directory(LPCWSTR dir) {
    using Fn=BOOL(WINAPI*)(LPCWSTR);
    static HMODULE base=LoadLibraryExW(L"kernelbase.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    static HMODULE k32=LoadLibraryExW(L"kernel32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    static Fn fn=[]{FARPROC p=base?GetProcAddress(base,"SetDllDirectoryW"):nullptr;if(!p&&k32)p=GetProcAddress(k32,"SetDllDirectoryW");return (Fn)p;}();
    if(!fn){SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return FALSE;}
    return fn(dir);
}
static bool genuine_steam_call(const std::wstring& dll,const std::string& token,uint64_t steamid) {
    GenuineSteamCall c{};c.steamid=steamid;
    // Flat export prototypes: https://github.com/SteamRE/open-steamworks/blob/master/Open%20Steamworks/Steamclient.h
    // Steam_CreateSteamPipe(), Steam_CreateGlobalUser(HSteamPipe*), Steam_LogOn(HSteamUser,HSteamPipe,uint64).
    // These are not private vtable slots. No pinned DLL copy is available off-device for disassembly.
    const std::wstring root=g_dir+L"\\steam\\client";
    for(const auto& d:{g_dir+L"\\steam",root,root+L"\\config"})CreateDirectoryW(d.c_str(),nullptr);
    char config[32768],users[2048],autoUser[64],sid[32];
    snprintf(sid,sizeof sid,"%llu",(unsigned long long)steamid);
    bool layout=steam_token_store_config_vdf(token.c_str(),sid,sid,1500000000ULL,config,sizeof config)>0&&
        steam_genuine::loginusers_vdf(sid,users,sizeof users)&&steam_genuine::autologin_user(sid,autoUser,sizeof autoUser);
    std::wstring cv=root+L"\\config\\config.vdf",lv=root+L"\\config\\loginusers.vdf";
    if(layout){write_text(cv,std::string(config));write_text(lv,std::string(users));layout=read_bytes(cv)==config&&read_bytes(lv)==users;}
    lp_sink(layout?"STEAM genuine: step=layout ok=1":"STEAM genuine: step=layout ok=0");
    HKEY key=nullptr; DWORD disp=0,remember=1;
    bool regok=layout&&regb::RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,&disp)==ERROR_SUCCESS;
    if(regok){std::wstring user(autoUser,autoUser+strlen(autoUser));
        auto set=[&](const wchar_t* name,const std::wstring& value){return regb::RegSetValueExW(key,name,0,REG_SZ,(const BYTE*)value.c_str(),DWORD((value.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS;};
        regok=set(L"SteamPath",root)&&set(L"InstallPath",root)&&set(L"AutoLoginUser",user)&&
            regb::RegSetValueExW(key,L"RememberPassword",0,REG_DWORD,(const BYTE*)&remember,sizeof remember)==ERROR_SUCCESS;
        regb::RegCloseKey(key);}
    lp_sink(regok?"STEAM genuine: step=registry ok=1":"STEAM genuine: step=registry ok=0");
    wchar_t oldDir[2048]{};DWORD oldDirSize=GetCurrentDirectoryW(2048,oldDir);bool ok=false;
    if(regok){
        bool environment=SetEnvironmentVariableW(L"SteamPath",root.c_str())&&genuine_set_dll_directory(root.c_str())&&SetCurrentDirectoryW(root.c_str());
        lp_sink(environment?"STEAM genuine: step=environment ok=1":"STEAM genuine: step=environment ok=0");
        if(environment){
            bool exports=genuine_steam_load(&c,dll.c_str());
            lp_sink(exports?"STEAM genuine: step=exports ok=1":"STEAM genuine: step=exports ok=0");
            if(exports)ok=genuine_steam_invoke(&c);
        }
    }

    if(oldDirSize&&oldDirSize<2048)SetCurrentDirectoryW(oldDir);
    if(!ok)SetEnvironmentVariableW(L"SteamPath",nullptr);
    genuine_set_dll_directory(nullptr);
    if(ok){g_genuineSteamModule=c.module;g_genuineSteamLiveApi=c.api;g_genuineSteamUser=c.user;g_genuineSteamPipe=c.pipe;return true;}
    genuine_steam_cleanup(&c);genuine_steam_free(c.module);return false;
}
static bool genuine_steam_opt_in() {
    std::string text;return read_file_s(g_dir+L"\\genuinesteam.txt",text)&&steam_genuine::opt_in(text);
}
static bool stage_steam_provider(uint32_t appid,const std::wstring& exePath) {
    wchar_t exe[2048];DWORD n=GetModuleFileNameW(nullptr,exe,2048);
    if(!n||n>=2048)return false;
    std::wstring package(exe,n);package.resize(package.find_last_of(L"\\/")+1);
    const bool wrapped=steamstub_wrapped(exePath);
    const std::wstring source=package+(wrapped?L"steam-provider\\gbe\\steamclient64.dll":L"steam-provider\\gbe\\steam_api64.dll");
    if(GetFileAttributesW(source.c_str())==INVALID_FILE_ATTRIBUTES){lp_sink("STEAM provider: none packaged; game Steam calls fail naturally");return false;}
    auto session=user_session();
    if(!session||session->anon||!session->steamid){lp_sink("STEAM provider: no signed-in account; not staged");return false;}
    std::string ticket;
    if(!owner_ticket(appid,ticket)){char line[160];snprintf(line,sizeof line,"STEAM provider: Steam issued no ownership ticket for app %u to this account (not owned or offline); nothing staged",appid);lp_sink(line);return false;}
    const std::wstring dir=g_dir+L"\\steam\\"+widen(std::to_string(appid));
    for(auto d:{g_dir+L"\\steam",dir,dir+L"\\steam_settings",dir+L"\\saves"})CreateDirectoryW(d.c_str(),nullptr);
    std::wstring valve;
    if(wrapped&&(valve=valve_client()).empty()){lp_sink("STEAM provider: SteamStub game, but Valve's signed client is unavailable; nothing staged");return false;}
    if(wrapped&&genuine_steam_opt_in()) {
        std::string refresh,access;
        if(load_token2(refresh,access)) {
            if(genuine_steam_call(valve,refresh,session->steamid)) {
                play_runtime::set_steam_client(valve);play_runtime::set_app_ticket(ticket);
                g_signedClient=valve;g_clientAccount=uint32_t(session->steamid);
                lp_sink("STEAM provider: genuine Valve client logged on for the owner");
                return true;
            }
        } else lp_sink("STEAM genuine: step=token ok=0");
    }
    const std::wstring target=dir+(wrapped?L"\\steamclient64.dll":L"\\steam_api64.dll");
    COPYFILE2_EXTENDED_PARAMETERS copy{};copy.dwSize=sizeof copy;
    HRESULT hr=CopyFile2(source.c_str(),target.c_str(),&copy);
    if(FAILED(hr)){char line[96];snprintf(line,sizeof line,"STEAM provider: copy failed hr=0x%08lx; not staged",(unsigned long)hr);lp_sink(line);return false;}
    write_text(dir+L"\\steam_settings\\steam_appid.txt",std::to_string(appid)+"\n");
    write_text(dir+L"\\steam_settings\\configs.user.ini","[user::general]\naccount_name=Player\naccount_steamid="+
        std::to_string(session->steamid)+"\nlanguage=english\n[user::saves]\nlocal_save_path=./saves\n");
    // No LAN/peer networking yet: achievements and cloud saves sync through the launcher.
    write_text(dir+L"\\steam_settings\\configs.main.ini","[main::connectivity]\ndisable_networking=1\n");
    char line[200];
    if(wrapped) {
        play_runtime::set_steam_client(target,valve);play_runtime::set_app_ticket(ticket);
        g_signedClient=valve;g_clientAccount=uint32_t(session->steamid);
        snprintf(line,sizeof line,"STEAM provider: SteamStub game; gbe_fork client behind Valve's signed client staged in LocalState\\steam\\%u for the owner",appid);
    } else {
        play_runtime::set_steam_provider(target);
        snprintf(line,sizeof line,"STEAM provider: gbe_fork staged in LocalState\\steam\\%u for the owner",appid);
    }
    lp_sink(line);
    return true;
}
static std::wstring provider_dir(uint32_t appid);
static bool stage_steam_provider_x86(uint32_t appid,const std::wstring& exePath) {
    auto session=user_session();
    const bool signedIn=session&&!session->anon&&session->steamid;
    std::string ticket; bool ownershipPassed=false;
    bool staged=false;
    const bool wrapped=steamstub_wrapped(exePath);
    bool owned=x86::stage_owned(signedIn,[&]{ownershipPassed=owner_ticket(appid,ticket);return ownershipPassed;},[&]{
        wchar_t exe[2048];DWORD n=GetModuleFileNameW(nullptr,exe,2048);
        if(!n||n>=2048)return false;
        std::wstring package(exe,n);package.resize(package.find_last_of(L"\\/")+1);
        const std::wstring source=package+(wrapped?L"steam-provider\\gbe\\steamclient.dll":L"steam-provider\\gbe\\steam_api.dll");
        if(GetFileAttributesW(source.c_str())==INVALID_FILE_ATTRIBUTES){lp_sink("STEAM provider: none packaged; game Steam calls fail naturally");return false;}
        const std::wstring dir=provider_dir(appid);
        for(auto d:{g_dir+L"\\steam",dir,dir+L"\\steam_settings",dir+L"\\saves"})CreateDirectoryW(d.c_str(),nullptr);
        const std::wstring target=dir+(wrapped?L"\\steamclient.dll":L"\\steam_api.dll");
        COPYFILE2_EXTENDED_PARAMETERS copy{};copy.dwSize=sizeof copy;
        HRESULT hr=CopyFile2(source.c_str(),target.c_str(),&copy);
        if(FAILED(hr)){char b[96];snprintf(b,sizeof b,"STEAM provider: copy failed hr=0x%08lx; not staged",(unsigned long)hr);lp_sink(b);return false;}
        write_text(dir+L"\\steam_settings\\steam_appid.txt",std::to_string(appid)+"\n");
        write_text(dir+L"\\steam_settings\\configs.user.ini","[user::general]\naccount_name=Player\naccount_steamid="+
            std::to_string(session->steamid)+"\nlanguage=english\n[user::saves]\nlocal_save_path=./saves\n");
        write_text(dir+L"\\steam_settings\\configs.main.ini","[main::connectivity]\ndisable_networking=1\n");
        if(wrapped)x86::set_steam_client_registry(uint32_t(session->steamid),play_runtime::narrow(target));
        staged=true;
        return true;
    });
    if(!signedIn){lp_sink("STEAM provider: no signed-in account; not staged");return false;}
    if(!ownershipPassed){char line[160];snprintf(line,sizeof line,"STEAM provider: Steam issued no ownership ticket for app %u to this account (not owned or offline); nothing staged",appid);lp_sink(line);return false;}
    if(!owned)return false;
    char line[200];
    if(wrapped)snprintf(line,sizeof line,"STEAM provider: SteamStub game; gbe_fork x86 client staged in LocalState\\steam\\%u for the owner",appid);
    else snprintf(line,sizeof line,"STEAM provider: gbe_fork x86 staged in LocalState\\steam\\%u for the owner",appid);
    lp_sink(line);
    return staged;
}
static std::wstring provider_dir(uint32_t appid) { return g_dir+L"\\steam\\"+widen(std::to_string(appid)); }
// Where gbe_fork keeps the files a game saves through ISteamRemoteStorage:
// <local_save_path>\<appid>\remote, with local_save_path=./saves staged in
// stage_steam_provider (achievements use the same <saves>\<appid> folder).
static std::filesystem::path provider_remote_dir(uint32_t appid) {
    return std::filesystem::path(provider_dir(appid))/"saves"/std::to_string(appid)/"remote";
}
// The known folders the game resolves. There is no separate guest mapping to
// follow: the guest's SHGetKnownFolderPath / SHGetFolderPathW are routed to the
// system shell32 in this very process and package container (K32T
// "system-route=1"; see issue #171), so asking that function here yields the
// guest's LocalLow/Local/Roaming/Documents/Saved Games by construction.
// The package folders the shared folder chain reads (winrt is used only here).
// A throwing lookup reports false, so the chain moves on to its next step.
static bool appPackageFolder(folder_chain::Package kind,std::wstring& out) {
    try {
        auto data=winrt::Windows::Storage::ApplicationData::Current();
        switch(kind) {
        case folder_chain::Package::Roaming: out=data.RoamingFolder().Path().c_str(); break;
        case folder_chain::Package::LocalCache: out=data.LocalCacheFolder().Path().c_str(); break;
        case folder_chain::Package::Local: out=data.LocalFolder().Path().c_str(); break;
        }
        return true;
    } catch(...) { return false; }
}
[[maybe_unused]] static const bool appPackageHookInstalled=(folder_chain::packageHook()=appPackageFolder,true);
static steam_cloud::Roots cloud_roots(uint32_t appid,const std::wstring& gameRoot) {
    static const GUID localLow={0xA520A1A4,0x1780,0x4FF6,{0xBD,0x18,0x16,0x73,0x43,0xC5,0xAF,0x16}};
    static const GUID local={0xF1B32785,0x6FBA,0x4FCF,{0x9D,0x55,0x7B,0x8E,0x7F,0x15,0x70,0x91}};
    static const GUID roaming={0x3EB685DB,0x65F9,0x4CF6,{0xA0,0x3A,0xE3,0xEF,0x65,0x72,0x9F,0x3D}};
    static const GUID documents={0xFDD39AD0,0x238F,0x46AF,{0xAD,0xB4,0x6C,0x85,0x48,0x03,0x69,0xC7}};
    static const GUID savedGames={0x4C5C32FF,0xBB9D,0x43B0,{0xB5,0xB4,0x2D,0x72,0xE5,0x4E,0xAA,0xA4}};
    using Known=HRESULT(WINAPI*)(const GUID&,DWORD,HANDLE,PWSTR*);
    static HMODULE shell=LoadLibraryExW(L"shell32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto known=shell?(Known)GetProcAddress(shell,"SHGetKnownFolderPath"):nullptr;
    steam_cloud::Roots roots;
    auto providers=folder_chain::systemProviders(known);
    auto add=[&](const char* name,const GUID& id){PWSTR path=nullptr;if(known&&SUCCEEDED(known(id,0,nullptr,&path))&&path)roots[name]=path;if(path)CoTaskMemFree(path);};
    add("WinAppDataLocalLow",localLow);add("WinAppDataLocal",local);
    add("WinMyDocuments",documents);add("WinSavedGames",savedGames);
    // A packaged app resolves Local/LocalLow to its AppContainer folder (AC),
    // but the Roaming known folder can fail outright here — and a missing root
    // makes every %WinAppDataRoaming% cloud file "not under a configured root",
    // so Stardew Valley's saves were skipped instead of downloaded. The shared
    // folder chain (uwph/shell32/folder_chain.h) tries the known folder, then the
    // packaged-app locations in the order Windows itself would, and the log says
    // which one answered so the next console run shows where the saves land.
    auto roam=folder_chain::resolve(providers,roaming,0,folder_chain::roamingChain());
    if(roam.known) roots["WinAppDataRoaming"]=roam.path;
    else if(!roam.path.empty()) {
        roots["WinAppDataRoaming"]=roam.path;
        lp_sink((std::string("STEAM cloud: WinAppDataRoaming from ")+roam.what+" = "+steam_cloud::path_text(roam.path)).c_str());
    } else lp_sink("STEAM cloud: no WinAppDataRoaming root; %WinAppDataRoaming% files will be skipped");
    // Evidence for the next console run: what the guest sees for the two
    // environment variables a game falls back to when the known-folder call
    // fails. Paths only, no account names.
    for(const wchar_t* v:{L"APPDATA",L"USERPROFILE"}) {
        wchar_t buf[MAX_PATH]{}; DWORD n=GetEnvironmentVariableW(v,buf,MAX_PATH);
        const std::wstring var(v);
        lp_sink((std::string("STEAM cloud: env ")+std::string(var.begin(),var.end())+" = "+
                 (n&&n<MAX_PATH?steam_cloud::path_text(buf):std::string("(unset)"))).c_str());
    }
    roots["GameInstall"]=gameRoot;
    roots[steam_cloud::kRemoteRoot]=provider_remote_dir(appid);
    return roots;
}
static std::mutex g_cloudRulesM;
static std::vector<steam_cloud::Rule> g_cloudRules;   // from the pre-launch download pass
// One Steam Cloud pass at a time (launch download, final upload, the detail
// page's button), so two never write the same save file.
static std::mutex g_cloudRunM;
struct CloudOutcome { bool ran=false; steam_cloud::Result r; std::string why; };
// When a pass last completed with no failures, kept in the app's provider folder
// so the detail page can say "synced <time>" across restarts.
static std::wstring cloud_stamp_path(uint32_t appid) { return provider_dir(appid)+L"\\cloud_synced.txt"; }
static void cloud_note_synced(uint32_t appid) {
    for(auto d:{g_dir+L"\\steam",provider_dir(appid)})CreateDirectoryW(d.c_str(),nullptr);
    write_text(cloud_stamp_path(appid),std::to_string((unsigned long long)time(nullptr))+"\n");
}
// Per cloud file, the SHA-1 both sides agreed on at the last sync (hex, one
// "<sha>\t<name>" line each): what tells "changed here" from "changed there".
static std::wstring cloud_state_path(uint32_t appid) { return provider_dir(appid)+L"\\cloud_state.txt"; }
static steam_cloud::Known cloud_known_load(uint32_t appid) {
    steam_cloud::Known k;auto text=read_bytes(cloud_state_path(appid));size_t at=0;
    while(at<text.size()){size_t end=text.find('\n',at);if(end==std::string::npos)end=text.size();
        auto line=text.substr(at,end-at);at=end+1;size_t tab=line.find('\t');
        if(tab!=40)continue;std::string sha;
        for(size_t i=0;i<40;i+=2)sha+=char(strtoul(line.substr(i,2).c_str(),nullptr,16));
        k[line.substr(41)]=sha;}
    return k;
}
static void cloud_known_save(uint32_t appid,const steam_cloud::Known& k) {
    for(auto d:{g_dir+L"\\steam",provider_dir(appid)})CreateDirectoryW(d.c_str(),nullptr);
    std::string text;static const char* hex="0123456789abcdef";
    for(auto& [name,sha]:k){if(sha.size()!=20||name.find('\n')!=std::string::npos)continue;
        for(unsigned char c:sha){text+=hex[c>>4];text+=hex[c&15];}text+='\t';text+=name;text+='\n';}
    write_text(cloud_state_path(appid),text);
}
static uint64_t cloud_last_synced(uint32_t appid) {
    auto text=read_bytes(cloud_stamp_path(appid));
    return text.empty()?0:strtoull(text.c_str(),nullptr,10);
}
// dry: list the cloud and count the files that are newer there, changing nothing.
// A game that is starting or running owns its save files: the detail page's
// passes (fromUi) refuse then, the launch's own passes do not.
static bool cloud_blocked() { return g_launch.phase==1||g_launch.phase==3||play_runtime::observed.load(std::memory_order_acquire)!=nullptr; }
static bool cloud_sync(uint32_t appid,const std::wstring& gameRoot,bool upload,uint64_t quiet,CloudOutcome* out=nullptr,bool dry=false,bool fromUi=false,steam_cloud::Prefer prefer=steam_cloud::Prefer::Auto) {
    CloudOutcome local;CloudOutcome& o=out?*out:local;o=CloudOutcome{};
    auto session=user_session();
    if(!session||session->anon){o.why="not signed in";return false;}
    std::lock_guard<std::mutex> pass(g_cloudRunM);
    if(fromUi&&cloud_blocked()){o.why="a game is running";return false;}
    std::string text=pics_appinfo(*session,appid);
    while(!text.empty()&&text.back()==0)text.pop_back();
    std::vector<steam_cloud::Rule> rules;
    Vdf root;
    if(text.empty()||!vdf_parse(text,root))lp_sink("STEAM cloud: no app info; Auto-Cloud rules unknown (API files still sync)");
    else {
        const Vdf* app=root.get("appinfo");if(!app)app=&root;
        rules=steam_cloud::rules(*app,session->steamid);
        if(rules.empty())lp_sink("STEAM cloud: app has no Auto-Cloud save rules (files saved through the Steam API still sync)");
    }
    {std::lock_guard<std::mutex> lock(g_cloudRulesM);g_cloudRules=rules;}
    steam_cloud::Io io;
    io.service=[session](const std::string& method,const std::string& request,int& eresult,std::string& response){
        JobMsg m;if(!session->service(method,request,30000,m))return false;
        eresult=m.eresult;response=m.body;return true;};
    io.get=[](bool https,const std::string& host,const std::string& path,const steam_cloud::Headers& h,std::string& body){return cloud_http(false,https,host,path,h,{},body);};
    io.put=[](bool https,const std::string& host,const std::string& path,const steam_cloud::Headers& h,const std::string& body){std::string ignored;return cloud_http(true,https,host,path,h,body,ignored);};
    io.sha1=[](const std::string& data){return sha1_of(data.data(),data.size());};
    io.inflate=[](const char* data,size_t size,size_t expected,std::string& out){
        out.resize(expected); z_stream z{}; if(inflateInit2(&z,-15)!=Z_OK) return false;
        z.next_in=(Bytef*)data; z.avail_in=(uInt)size; z.next_out=(Bytef*)&out[0]; z.avail_out=(uInt)expected;
        int rc=inflate(&z,Z_FINISH); bool ok=rc==Z_STREAM_END&&z.total_out==expected; inflateEnd(&z); return ok;};
    // Cloud names and paths can carry the account's SteamID (e.g. a per-user save folder).
    io.log=[](const std::string& line){lp_sink(redact::steam_ids("STEAM "+line).c_str());};
    auto roots=cloud_roots(appid,gameRoot);
    for(auto& [name,path]:roots)lp_sink(redact::steam_ids("STEAM cloud: root "+name+" = "+steam_cloud::path_text(path)).c_str());
    auto known=cloud_known_load(appid);
    auto r=upload?steam_cloud::upload(io,appid,roots,rules,"Xbox",quiet,&known,prefer):steam_cloud::download(io,appid,roots,dry,&known,prefer);
    if(!dry)cloud_known_save(appid,known);
    char line[220];snprintf(line,sizeof line,"STEAM cloud %s: downloaded=%u uploaded=%u unchanged=%u kept=%u busy=%u newer=%u conflict=%u failed=%u",
        upload?"upload":dry?"check":"download",r.downloaded,r.uploaded,r.unchanged,r.kept,r.busy,r.newer,r.conflict,r.failed);lp_sink(line);
    o.ran=true;o.r=r;
    if(!dry&&r.failed==0&&r.conflict==0)cloud_note_synced(appid);
    return r.failed==0;
}
// ---- Cloud status on the game's detail page, and its "Sync cloud saves" button.
struct CloudStatus { int phase=0; std::string text; int tone=0; };   // phase: 0 unchecked, 1 working, 2 done; tone: 0 dim, 1 ok, 2 attention
static std::mutex g_cloudStatusM;
static std::map<uint32_t,CloudStatus> g_cloudStatus;
static std::atomic<bool> g_cloudBusy{false};
static CloudStatus cloud_status(uint32_t appid) { std::lock_guard<std::mutex> l(g_cloudStatusM); auto it=g_cloudStatus.find(appid); return it==g_cloudStatus.end()?CloudStatus{}:it->second; }
static void cloud_set(uint32_t appid,int phase,const std::string& text,int tone) { std::lock_guard<std::mutex> l(g_cloudStatusM); g_cloudStatus[appid]={phase,text,tone}; }
static std::string cloud_time(uint64_t t) {
    time_t tt=(time_t)t;tm local{};char b[48];
    if(localtime_s(&local,&tt)!=0||!strftime(b,sizeof b,"%Y-%m-%d %H:%M",&local))return "at an unknown time";
    return b;
}
static std::string cloud_plural(unsigned n,const char* one,const char* many) { return std::to_string(n)+" "+(n==1?one:many); }
static void cloud_describe(uint32_t appid,bool sync,const CloudOutcome& down,const CloudOutcome& up) {
    if(!down.ran){cloud_set(appid,2,"Cloud: not synced ("+down.why+")",2);return;}
    unsigned failed=down.r.failed+(up.ran?up.r.failed:0);
    unsigned conflict=std::max(down.r.conflict,up.ran?up.r.conflict:0u);
    if(conflict){cloud_set(appid,2,"Cloud: "+cloud_plural(conflict,"save differs","saves differ")+" here and in the cloud. LB: use cloud, RB: keep Xbox",2);return;}
    if(sync) {
        if(!up.ran){cloud_set(appid,2,"Cloud: not synced ("+(up.why.empty()?std::string("upload skipped"):up.why)+")",2);return;}
        if(failed){cloud_set(appid,2,"Cloud: not synced ("+cloud_plural(failed,"file","files")+" failed; see the launch log)",2);return;}
        cloud_set(appid,2,"Cloud: synced "+cloud_time((uint64_t)time(nullptr))+" ("+std::to_string(down.r.downloaded)+" down, "+std::to_string(up.r.uploaded)+" up)",1);
        return;
    }
    if(down.r.newer){cloud_set(appid,2,"Cloud: "+cloud_plural(down.r.newer,"file is","files are")+" newer in the cloud",2);return;}
    if(down.r.failed){cloud_set(appid,2,"Cloud: not synced ("+cloud_plural(down.r.failed,"file","files")+" could not be checked; see the launch log)",2);return;}
    if(auto last=cloud_last_synced(appid)){cloud_set(appid,2,"Cloud: synced "+cloud_time(last),1);return;}
    cloud_set(appid,2,"Cloud: up to date",1);
}
// Off the UI thread. sync=false only checks (counts the files newer in the
// cloud); sync=true downloads those and uploads newer local files.
static void cloud_async(uint32_t appid,const std::wstring& gameRoot,bool sync,steam_cloud::Prefer prefer=steam_cloud::Prefer::Auto) {
    if(cloud_blocked()){if(sync)cloud_set(appid,2,"Cloud: not synced (a game is running)",2);return;}
    if(g_cloudBusy.exchange(true))return;
    cloud_set(appid,1,sync?"Cloud: syncing...":"Cloud: checking...",0);
    std::thread([=]{
        init_apartment(apartment_type::multi_threaded);
        try {
            CloudOutcome down,up;
            cloud_sync(appid,gameRoot,false,0,&down,!sync,true,prefer);
            if(sync&&down.ran)cloud_sync(appid,gameRoot,true,0,&up,false,true,prefer);
            cloud_describe(appid,sync,down,up);
        } catch(...) {lp_sink("STEAM cloud: exception");cloud_set(appid,2,"Cloud: not synced (unexpected error)",2);}
        g_cloudBusy=false;
    }).detach();
}
// ---- Steam presence (steam_presence.h): the account shows the game to friends.
#include "steam_presence.h"
// ---- Steam leaderboards for the provider-served game (steam_leaderboards.h):
// the game's Find call is observed at the provider's export boundary, and the
// board's real rows are written into the provider's leaderboard cache file
// BEFORE the provider answers. The provider reads that file once, the first
// time a board is found in the process, so the game's own rank screen lists
// real players. Its own Find still runs and still hands the game its handle.
// A later Find refetches when the rows are older than the bridge's TTL or when
// this account uploaded a score, and the fresher rows are handed to the game
// from GetDownloadedLeaderboardEntry (the provider never re-reads its file).
#include "steam_leaderboards.h"
// ---- Steam persona names (steam_persona.h): the rows name SteamIDs that are
// not friends, and the provider answers "Unknown User" for those. The launcher
// asks Steam for the names on its own session and answers GetFriendPersonaName,
// GetPersonaName and RequestUserInformation in memory.
#include "steam_persona.h"
struct LeaderboardBridge {
    std::mutex m;
    steam_leaderboards::Bridge bridge;   // seed policy and board ids, per launch
    steam_persona::Bridge persona;       // names the game may draw, per launch
    std::shared_ptr<CmSession> session;
    uint32_t appid = 0;
};
static LeaderboardBridge g_leaderboards;
static double leaderboard_now() { return now_s(); }
static steam_leaderboards::Io leaderboard_io(std::shared_ptr<CmSession> session) {
    steam_leaderboards::Io io;
    // The seed runs on the game's thread, which is blocked inside its own Find
    // call, so a request that does not answer quickly is abandoned rather than
    // stalling the game.
    io.call=[session](uint32_t emsg,uint32_t routing,const std::string& body,std::string& out){
        JobMsg m;if(!session->call(emsg,body,"",3000,m,routing))return false;
        out=m.body;return true;};
    io.log=[](const std::string& line){lp_sink(redact::steam_ids("STEAM "+line).c_str());};
    return io;
}
static steam_persona::Io persona_io(std::shared_ptr<CmSession> session) {
    steam_persona::Io io;
    // CMsgClientRequestFriendData has no job id; the answer is an unsolicited
    // CMsgClientPersonaState push on the same session, which the keeper's push
    // handler routes into the bridge.
    io.send=[session](uint32_t emsg,const std::string& body){
        if(!session||!session->up)return false;
        session->send_raw(emsg,body,0,"");
        return true;};
    io.log=[](const std::string& line){lp_sink(redact::steam_ids("STEAM "+line).c_str());};
    return io;
}
// The game's Find, observed from the game's thread before the provider answers.
// The provider reads a board's cache file only on its first Find in the
// process, so this is the one moment the rows can be put in place; later Finds
// refetch through the bridge and the fresher rows reach the game from the
// GetDownloadedLeaderboardEntry tap.
static bool leaderboard_tap_seed(const std::string& name,int sort,int display,bool create) {
    if(name.empty()) return false;
    std::shared_ptr<CmSession> session; uint32_t appid = 0;
    { std::lock_guard<std::mutex> l(g_leaderboards.m);
      if(!g_leaderboards.appid) return false;
      appid = g_leaderboards.appid; session = g_leaderboards.session; }
    if(!session || !session->up) session = user_session();   // the keeper may have re-established it
    // No session yet is not a verdict on the board: the keeper can reconnect,
    // and the next Find should try again.
    if(!session || !session->up || session->anon) return false;
    auto io = leaderboard_io(session);
    namespace fs = std::filesystem;
    fs::path saves = fs::path(provider_dir(appid))/"saves";
    bool ok = g_leaderboards.bridge.seed(io,appid,saves,name,sort,display,create,leaderboard_now());
    // The rows name players; ask Steam for their persona names now, so the rank
    // screen the game is about to draw can read them instead of "Unknown User".
    // This runs inside the game's Find, so the wait is one frame's worth, not
    // Steam's round trip: names that land later are handed out by the tap.
    if(auto rows = g_leaderboards.bridge.entries(name)) {
        std::vector<uint64_t> ids;
        for(auto& e : *rows) ids.push_back(e.steamid);
        auto pio = persona_io(session);
        g_leaderboards.persona.ensure(pio,ids,int(steam_persona::Bridge::SeedWaitMs));
    }
    return ok;
}
// The rows the game is about to read. The provider answers from the copy it
// read at the board's first Find; the launcher's copy is the one a refresh (or
// this account's own upload) updated.
static std::optional<std::vector<play_runtime::LeaderboardTap::Row>> leaderboard_tap_rows(const std::string& name) {
    auto rows = g_leaderboards.bridge.entries(name);
    if(!rows) return std::nullopt;
    std::vector<play_runtime::LeaderboardTap::Row> out;
    for(auto& e : *rows) out.push_back({e.steamid,e.rank,e.score,e.details});
    return out;
}
// A score the game uploaded, observed after the provider answered. The board
// name is the one this thread last found, which is the handle the game passed.
static void leaderboard_tap_upload(const std::string& name,int32_t score,const int32_t* details,int count,int method) {
    std::shared_ptr<CmSession> session; uint32_t appid = 0;
    { std::lock_guard<std::mutex> l(g_leaderboards.m);
      if(!g_leaderboards.appid) return;
      session = g_leaderboards.session; appid = g_leaderboards.appid; }
    // As with the seed, the keeper may have replaced the connection since the
    // launch began; the score belongs to the account, not to one socket.
    if(!session || !session->up || session->anon) session = user_session();
    if(!session || !session->up || session->anon) return;
    auto io = leaderboard_io(session);
    g_leaderboards.bridge.upload(io,appid,name,score,details,count,method);
}
// The game's GetFriendPersonaName / GetPersonaName / RequestUserInformation,
// observed from the game's thread. Only names Steam answered for are handed
// back; anything else leaves the provider's own answer in place.
static const char* persona_tap_name(uint64_t steamid) { return g_leaderboards.persona.name(steamid); }
static const char* persona_tap_self() { return g_leaderboards.persona.self_name(); }
static void persona_tap_want(uint64_t steamid) {
    std::lock_guard<std::mutex> l(g_leaderboards.m);
    if(!g_leaderboards.appid) return;
    g_leaderboards.persona.want({steamid});
}
// The names from the last launch, so a row drawn before Steam answers still
// reads correctly. Private on-device state under the app's own LocalState.
static std::filesystem::path persona_cache_path(uint32_t appid) {
    return std::filesystem::path(provider_dir(appid))/"persona_names.txt";
}
static void leaderboard_start(uint32_t appid) {
    namespace fs = std::filesystem;
    {
        std::lock_guard<std::mutex> l(g_leaderboards.m);
        g_leaderboards.appid = appid; g_leaderboards.bridge.reset(); g_leaderboards.persona.reset();
        g_leaderboards.session.reset();
        auto now = user_session();
        if(now && now->up && !now->anon) {
            g_leaderboards.session = now;
            g_leaderboards.persona.self = now->steamid;
        }
        std::map<uint64_t,std::string> cached;
        if(steam_persona::load_cache(persona_cache_path(appid),uint64_t(time(nullptr)),
                                     uint64_t(7*24*3600),cached))
            g_leaderboards.persona.adopt(cached);
    }
    play_runtime::set_leaderboard_tap({[](const std::string& name,int sort,int display,bool create){return leaderboard_tap_seed(name,sort,display,create);},
                                       [](const std::string& name,int32_t score,const int32_t* details,int count,int method){leaderboard_tap_upload(name,score,details,count,method);},
                                       [](const std::string& name){return leaderboard_tap_rows(name);}});
    play_runtime::set_persona_tap({[](uint64_t id){return persona_tap_name(id);},
                                   []{return persona_tap_self();},
                                   [](uint64_t id){persona_tap_want(id);}});
}
static void leaderboard_end() {
    uint32_t appid = 0;
    {
        std::lock_guard<std::mutex> l(g_leaderboards.m);
        appid = g_leaderboards.appid;
        play_runtime::set_leaderboard_tap({});
        play_runtime::set_persona_tap({});
        if(appid) {
            // Names learned this launch outlive it: the next launch draws them
            // before Steam answers.
            auto names = g_leaderboards.persona.snapshot();
            if(!names.empty())
                steam_persona::save_cache(persona_cache_path(appid),uint64_t(time(nullptr)),names);
        }
        g_leaderboards.appid = 0;
        g_leaderboards.bridge.reset(); g_leaderboards.persona.reset(); g_leaderboards.session.reset();
    }
}
// ---- Steam achievements/stats for the provider-served game (steam_achievements.h)
#include "steam_achievements.h"
// The guide's achievements tab (main.cpp): filled once gbe's files are prepared.
void guide_achievements_prepared(uint32_t appid,const steam_achievements::Schema& schema,
                                 const std::map<std::string,steam_achievements::Earned>& earned);
static steam_achievements::Io achievement_io(std::shared_ptr<CmSession> session) {
    steam_achievements::Io io;
    io.call=[session](uint32_t emsg,const std::string& body,std::string& out){
        JobMsg m;if(!session->call(emsg,body,"",30000,m))return false;
        out=m.body;return true;};
    io.log=[](const std::string& line){lp_sink(("STEAM "+line).c_str());};
    return io;
}
// The account is "in game" while the guest runs (status, playtime), as with the
// real client; an empty list ends it. Friends only see a game on an online
// persona, so the account goes Online first, as the real client does on start.
static void games_played(uint32_t appid) {
    auto session=user_session();
    if(!session||session->anon)return;
    if(appid){session->send_raw(steam_presence::ClientChangeStatus,steam_presence::change_status_body(),0,"");}
    session->send_raw(steam_presence::ClientGamesPlayedWithDataBlob,steam_presence::games_played_body(appid),0,"");
}
static void achievements_sync(uint32_t appid,bool store) {
    auto session=user_session();
    if(!session||session->anon)return;
    auto io=achievement_io(session);
    auto user=steam_achievements::fetch(io,appid,session->steamid);
    if(!user.ok)return;
    auto schema=steam_achievements::parse_schema(user.schema);
    if(!schema){lp_sink("STEAM achievements: schema unreadable");return;}
    namespace fs=std::filesystem;
    fs::path dir(provider_dir(appid));
    fs::path saves=dir/"saves"/std::to_string(appid);
    if(store){steam_achievements::store(io,appid,session->steamid,*schema,user,saves);return;}
    unsigned unlocked=0;for(auto& [block,times]:user.unlocks)for(auto t:times)unlocked+=t!=0;
    bool ok=steam_achievements::prepare(*schema,user,dir/"steam_settings",saves);
    char line[160];snprintf(line,sizeof line,"STEAM achievements: %s achievements=%zu stats=%zu unlocked=%u",
        ok?"prepared":"prepare failed",schema->achievements.size(),schema->stats.size(),unlocked);lp_sink(line);
    // The guide's achievements tab and unlock poller read gbe's saves file
    // (ui_unlocks.h); the hook receives the schema and the merged earned set.
    if(ok)guide_achievements_prepared(appid,*schema,steam_achievements::parse_earned(steam_achievements::read(saves/"achievements.json").value_or("{}")));
}
// ---- Auth-session tickets while the guest runs (steam_auth.h).
// The provider owns ticket generation. This bridge only forwards the exact
// bytes returned to the game over the launcher's signed-in CM session.
static void presence_push(const JobMsg& m);
struct AuthKeeper {
    std::mutex m;
    uint32_t appid = 0;
    std::shared_ptr<CmSession> session;
    bool warned = false;
    std::atomic<unsigned> gen{0};
    steam_auth::Bridge bridge;
};
static AuthKeeper g_auth;
static bool auth_bind_locked(const std::shared_ptr<CmSession>& session) {
    if(!session || !session->up || session->anon || !g_auth.appid || g_auth.session == session) return false;
    steam_auth::Io io;
    io.send = [session](uint32_t emsg,const std::string& body){
        if(session->up) session->send_raw(emsg,body,0,"");
    };
    g_auth.session = session;
    g_auth.bridge.start(g_auth.appid,std::move(io));
    session->set_push(presence_push);
    return true;
}
static void auth_ticket_issue(uint32_t handle,const std::string& bytes) {
    std::shared_ptr<CmSession> session = user_session();
    bool warn = false;
    {
        std::lock_guard<std::mutex> l(g_auth.m);
        if(!g_auth.appid) return;
        if(!session || !session->up || session->anon) {
            if(!g_auth.warned) { g_auth.warned = true; warn = true; }
        } else {
            auth_bind_locked(session);
            g_auth.bridge.issue(handle,bytes);
        }
    }
    if(warn) lp_sink("STEAM auth tickets: no signed-in CM session; ticket not registered");
}
static void auth_ticket_cancel(uint32_t handle) {
    std::lock_guard<std::mutex> l(g_auth.m);
    if(!g_auth.appid) return;
    g_auth.bridge.cancel(handle);
}
static void auth_start(uint32_t appid) {
    unsigned gen;
    {
        std::lock_guard<std::mutex> l(g_auth.m);
        gen = ++g_auth.gen;
        g_auth.appid = appid; g_auth.session.reset(); g_auth.warned = false; g_auth.bridge.reset();
    }
    play_runtime::set_auth_ticket_tap({[](uint32_t handle,const std::string& bytes){auth_ticket_issue(handle,bytes);},
                                       [](uint32_t handle){auth_ticket_cancel(handle);}});
    // A reconnect must replay tickets even when the game does not issue another one.
    std::thread([gen]{
        init_apartment(apartment_type::multi_threaded);
        while(g_auth.gen.load() == gen) {
            Sleep(500);
            auto session = user_session();
            std::lock_guard<std::mutex> l(g_auth.m);
            if(g_auth.gen.load() != gen || !g_auth.appid) break;
            auth_bind_locked(session);
        }
    }).detach();
}
static void auth_end() {
    play_runtime::set_auth_ticket_tap({});
    std::lock_guard<std::mutex> l(g_auth.m);
    ++g_auth.gen;
    g_auth.bridge.end();
    g_auth.appid = 0; g_auth.session.reset(); g_auth.warned = false; g_auth.bridge.reset();
}
// ---- Presence while the guest runs (steam_presence.h).
// Steam drops "in game" and rich presence when the CM session that sent them
// ends, and the launcher reconnects by itself after a network drop. The keeper
// re-sends the game state on every new session, uploads the game's rich
// presence once a burst of SetRichPresence calls settles, and answers Steam's
// playing-session notice (another of the account's clients starting a game
// blocks this one; saying nothing would leave friends seeing a stale game).
// It never sends a games-played while blocked: Steam logs such a session off.
struct PresenceKeeper {
    std::mutex m;
    uint32_t appid = 0;
    steam_presence::RichPresence rp;
    std::shared_ptr<CmSession> session;   // the session the state was last sent on
    bool blocked = false, ended = false, warned = false;
    std::atomic<unsigned> gen{0};         // a new launch retires the previous keeper
};
static PresenceKeeper g_presence;
// `full` carries the game state (a new session: Steam forgot it); a coalesced
// rich-presence change only uploads the key set.
static void presence_send(std::shared_ptr<CmSession> s,uint32_t appid,const std::map<std::string,std::string>& kv,bool full,bool richUpdate) {
    if(!s||!s->up||!appid) return;
    if(full) {
        s->send_raw(steam_presence::ClientChangeStatus,steam_presence::change_status_body(),0,"");
        s->send_raw(steam_presence::ClientGamesPlayedWithDataBlob,steam_presence::games_played_body(appid),0,"");
    }
    if(richUpdate) s->send_raw(steam_presence::ClientRichPresenceUpload,
        steam_presence::rich_presence_upload_body(kv),0,"",appid);
}
// The game's SetRichPresence/ClearRichPresence, observed from the game's thread
// (play_runtime::set_rich_presence_tap). Only the key/value set is kept here.
static void presence_tap_set(const std::string& key,const char* value) {
    std::lock_guard<std::mutex> l(g_presence.m);
    if(!g_presence.appid) return;
    g_presence.rp.set(key,value,now_s());
}
static void presence_tap_clear() {
    std::lock_guard<std::mutex> l(g_presence.m);
    if(!g_presence.appid) return;
    g_presence.rp.clear(now_s());
}
// What Steam says about this session's game state: a persona-state reply to the
// self-check below, or another client starting a game. Runs on the CM reader
// thread, so it never blocks: the tap's lock is only taken to read state.
static void presence_push(const JobMsg& m) {
    if(m.emsg==steam_auth::ClientAuthListAck) {
        std::lock_guard<std::mutex> l(g_auth.m);
        if(g_auth.appid) g_auth.bridge.ack(m.body);
        return;
    }
    if(m.emsg==steam_presence::ClientPlayingSessionState) {
        auto ps = steam_presence::parse_playing_state(m.body);
        bool started = false, stopped = false;
        {
            std::lock_guard<std::mutex> l(g_presence.m);
            if(!g_presence.appid) return;
            started = ps.blocked && !g_presence.blocked;
            stopped = !ps.blocked && g_presence.blocked;
            if(stopped) g_presence.session.reset();   // the keeper re-sends on its next pass
            g_presence.blocked = ps.blocked;
        }
        if(started) {
            char line[160];snprintf(line,sizeof line,
                "STEAM presence: another of this account's clients started app %u; this session stops reporting the game",ps.app);
            lp_sink(line);
        } else if(stopped) {
            lp_sink("STEAM presence: the other client stopped; reporting the game again");
        }
        return;
    }
    if(m.emsg!=steam_presence::ClientPersonaState) return;
    // Every persona push is also a name answer: the leaderboard rows asked for
    // these SteamIDs, and the game draws them on its own thread. Collected
    // before the self-check below, which only looks at this account's own entry
    // and returns early otherwise.
    {
        size_t added = g_leaderboards.persona.on_state(m.body);
        if(added) {
            char line[120];snprintf(line,sizeof line,"STEAM persona: Steam answered %zu names",added);
            lp_sink(redact::steam_ids(line).c_str());
        }
    }
    uint64_t self = 0; { auto s = user_session(); if(s) self = s->steamid; }
    if(!self) return;
    auto p = steam_presence::parse_persona(m.body,self);
    if(!p.found) return;
    uint32_t appid = 0; { std::lock_guard<std::mutex> l(g_presence.m); appid = g_presence.appid; }
    if(!appid) return;
    // Steam's own view, as friends see it: online persona, this app, our keys.
    char line[220];snprintf(line,sizeof line,"STEAM presence: Steam reports persona_state=%u game=%u name='%s' rich_presence_keys=%zu",
        p.state,p.game_played_app_id,p.game_name.c_str(),p.rich_presence.size());
    lp_sink(redact::steam_ids(line).c_str());
    bool ok = p.state == steam_presence::PersonaOnline && p.game_played_app_id == appid;
    { std::lock_guard<std::mutex> l(g_presence.m); if(ok) g_presence.warned = false; else if(g_presence.warned) return; else g_presence.warned = true; }
    if(!ok) lp_sink("STEAM presence: Steam does not show this session as in-game (see the logon and games-played lines above)");
}
static void presence_start(uint32_t appid) {
    unsigned gen;
    {
        std::lock_guard<std::mutex> l(g_presence.m);
        gen = ++g_presence.gen;
        g_presence.appid = appid; g_presence.rp.reset(appid);
        g_presence.session.reset(); g_presence.blocked = false; g_presence.ended = false; g_presence.warned = false;
    }
    play_runtime::set_rich_presence_tap({[](const std::string& key,const char* value){presence_tap_set(key,value);},
                                         []{presence_tap_clear();}});
    // A dropped session is re-established by the main loop; the keeper sends
    // the game state again as soon as a new one is up, and takes the Steam
    // notifications on that new session too.
    std::thread([gen]{
        init_apartment(apartment_type::multi_threaded);
        unsigned uploads = 0;
        while(g_presence.gen.load() == gen) {
            Sleep(500);
            std::shared_ptr<CmSession> session; std::map<std::string,std::string> kv; uint32_t appid = 0; bool send = false, full = false, richUpdate = false;
            {   std::lock_guard<std::mutex> l(g_presence.m);
                if(g_presence.gen.load() != gen || !g_presence.appid || g_presence.ended) break;
                auto now = user_session();
                if(!now || !now->up) continue;              // reconnecting
                const bool newSession = now != g_presence.session;
                if(newSession) {                           // subscribe even while Steam blocks this game
                    g_presence.session = now;
                    now->set_push(presence_push);
                }
                if(g_presence.blocked) continue;
                appid = g_presence.appid;
                if(newSession) {                            // a new session: Steam forgot the game
                    send = full = true;
                    richUpdate = g_presence.rp.needs_upload(true);
                    if(g_presence.rp.coalescer.dirty) g_presence.rp.coalescer.sent();
                    kv = g_presence.rp.kv;
                } else if(g_presence.rp.coalescer.ready(now_s())) {
                    richUpdate = g_presence.rp.needs_upload(false);
                    g_presence.rp.coalescer.sent(); send = true; kv = g_presence.rp.kv;
                }
                session = now;
            }
            // Names the game asked about outside the seeded leaderboard rows.
            // Requested here, never on the game's thread, and answered by the
            // same persona push the keeper already takes on this session.
            try {
                auto waiting = g_leaderboards.persona.take_pending();
                if(!waiting.empty()) persona_io(session).send(steam_persona::ClientRequestFriendData,
                                                             steam_persona::request_body(waiting));
            } catch(...) {}
            if(!send) continue;
            try { presence_send(session,appid,kv,full,richUpdate); } catch(...) { continue; }
            if(!richUpdate) continue;
            char line[120];snprintf(line,sizeof line,"STEAM presence: rich presence uploaded (%zu keys, #%u)",kv.size(),++uploads);lp_sink(line);
        }
    }).detach();
    // Ask Steam for this account's own persona state: the only way to see from
    // the console what friends see. Sent once the keeper has carried the game
    // state, so the answer reflects it.
    std::thread([gen]{
        init_apartment(apartment_type::multi_threaded);
        for(int i=0;i<120&&g_presence.gen.load()==gen;++i) {
            Sleep(500);
            auto s = user_session();
            if(!s || s->anon || !s->steamid || !s->up) continue;
            if(i < 20) continue;   // let the keeper carry the game state first
            s->send_raw(steam_presence::ClientRequestFriendData,steam_presence::persona_request_body(s->steamid),0,"");
            return;
        }
    }).detach();
}
static void presence_end() {
    bool blocked;
    {
        std::lock_guard<std::mutex> l(g_presence.m);
        ++g_presence.gen;   // retires the keeper and the self-check
        g_presence.ended = true; g_presence.appid = 0; g_presence.session.reset();
        blocked = g_presence.blocked;
    }
    play_runtime::set_rich_presence_tap({});
    // While another of the account's clients is playing, even the empty list is
    // a games-played message this session must not send: that other client owns
    // the state, and Steam logs a blocked session off for sending one.
    if(!blocked) games_played(0);
}
// Saves and achievements go up once, when the game ends (it quits, closes by
// itself or is quit from the launcher menu), never while it is playing.
static std::atomic<bool> g_finalSyncDone{false};
static std::function<void()> g_finalSync;   // set before the guest starts; used by the overlay's Force quit
static void final_sync(uint32_t appid,const std::wstring& gameRoot) {
    if(g_finalSyncDone.exchange(true))return;
    lp_sink("STEAM sync: game ended; uploading saves and achievements");
    try{cloud_sync(appid,gameRoot,true,0);}catch(...){lp_sink("STEAM cloud upload: exception");}
    try{achievements_sync(appid,true);}catch(...){lp_sink("STEAM achievements store: exception");}
    try{auth_end();}catch(...){}
    try{presence_end();}catch(...){}
    try{leaderboard_end();}catch(...){}
}
// ---- Diagnostic: cmd.txt "stacks" logs every other thread's call stack to
// launch_probe.log (module+offset via Play's guest module map). Live process
// dumps are unavailable on the console. Each thread is suspended only long
// enough to copy its registers; the stack is walked after it resumes (a hung
// thread's stack is unchanged), so nothing allocates while another thread is
// stopped.
// The thread runs again while its copied registers are unwound (suspending
// it across RtlLookupFunctionEntry could deadlock on a lock it holds), so its
// stack may change underneath: an unreadable address raises inside this SEH
// frame and a torn walk just ends early. No C++ objects here (SEH frame).
static int unwind_frames(CONTEXT ctx,DWORD64* rips,int max) {
    int n=0;
    __try {
        while(n<max&&ctx.Rip) {
            rips[n++]=ctx.Rip;
            DWORD64 base=0;auto fn=RtlLookupFunctionEntry(ctx.Rip,&base,nullptr);
            if(fn){void* handler=nullptr;DWORD64 frameBase=0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,ctx.Rip,fn,&ctx,&handler,&frameBase,nullptr);continue;}
            if(!ctx.Rsp)break;   // leaf frame: return address at [rsp]
            ctx.Rip=*(volatile DWORD64*)ctx.Rsp;ctx.Rsp+=8;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}
static std::string stack_frame(uintptr_t address) {
    char b[300];
    if(auto st=play_runtime::state) for(auto& m:st->modules)
        if(address>=(uintptr_t)m->image.base&&address-(uintptr_t)m->image.base<m->image.size){
            snprintf(b,sizeof b,"%s+0x%llx",m->key.c_str(),(unsigned long long)(address-(uintptr_t)m->image.base));return b;}
    MEMORY_BASIC_INFORMATION mbi{};
    using File=DWORD(WINAPI*)(HMODULE,LPWSTR,DWORD);static auto file=(File)kernel_tail::native("GetModuleFileNameW");
    wchar_t path[512]{};
    if(VirtualQuery((void*)address,&mbi,sizeof mbi)&&file&&file((HMODULE)mbi.AllocationBase,path,512)){
        const wchar_t* name=wcsrchr(path,L'\\');name=name?name+1:path;
        snprintf(b,sizeof b,"%ls+0x%llx",name,(unsigned long long)(address-(uintptr_t)mbi.AllocationBase));return b;}
    snprintf(b,sizeof b,"0x%llx",(unsigned long long)address);return b;
}
static void dump_thread_stacks() {
    using Snapshot=HANDLE(WINAPI*)(DWORD,DWORD);
    using Open=HANDLE(WINAPI*)(DWORD,BOOL,DWORD);
    using Suspend=DWORD(WINAPI*)(HANDLE);
    using Context=BOOL(WINAPI*)(HANDLE,CONTEXT*);
    auto snapshot=(Snapshot)kernel_tail::native("CreateToolhelp32Snapshot");
    auto open=(Open)kernel_tail::native("OpenThread");
    auto suspend=(Suspend)kernel_tail::native("SuspendThread");
    auto resume=(Suspend)kernel_tail::native("ResumeThread");
    auto context=(Context)kernel_tail::native("GetThreadContext");
    if(!snapshot||!open||!suspend||!resume||!context){lp_sink("STACKS unavailable: thread APIs missing");return;}
    HANDLE snap=snapshot(4/*TH32CS_SNAPTHREAD*/,0);
    if(snap==INVALID_HANDLE_VALUE){lp_sink("STACKS unavailable: snapshot failed");return;}
    std::vector<DWORD> ids;
    kernel_tail::ThreadEntry entry{};entry.size=sizeof entry;
    for(BOOL ok=kernel_tail::first(snap,&entry);ok;ok=kernel_tail::next(snap,&entry))
        if(entry.process==GetCurrentProcessId()&&entry.id!=GetCurrentThreadId())ids.push_back(entry.id);
    CloseHandle(snap);
    char line[512];snprintf(line,sizeof line,"STACKS begin threads=%zu",ids.size());lp_sink(line);
    for(DWORD id:ids) {
        HANDLE thread=open(0x0002|0x0008|0x0040,FALSE,id); // SUSPEND_RESUME|GET_CONTEXT|QUERY_INFORMATION
        if(!thread)continue;
        CONTEXT ctx{};ctx.ContextFlags=CONTEXT_FULL;
        bool got=false;
        if(suspend(thread)!=DWORD(-1)){got=context(thread,&ctx)!=FALSE;resume(thread);}
        CloseHandle(thread);
        if(!got)continue;
        snprintf(line,sizeof line,"STACK tid=%lu",(unsigned long)id);lp_sink(line);
        DWORD64 rips[24];int frames=unwind_frames(ctx,rips,24);
        for(int frame=0;frame<frames;++frame){snprintf(line,sizeof line,"  #%02d %s",frame,stack_frame(rips[frame]).c_str());lp_sink(line);}
    }
    lp_sink("STACKS end");
}
// A 32-bit (PE32) guest: the in-process x86 interpreter (host/x86) runs the
// executable with the guest OS surface serving its imports. This is a separate
// path from the 64-bit loader on purpose: the guest never shares the launcher's
// address space (guest addresses are indices into the interpreter's own page
// table), so nothing here can be mistaken for the native route, and the log
// says which engine ran which PE. Steam/provider and cloud staging are explicit
// and remain behind the launch-time ownership gate.
static void lp_run_pe32(LpCtx& c,const std::wstring& path,const std::wstring& exe) {
    char line[512];
    regb::init(g_dir,lp_sink); // guest registry (PlayerPrefs) for the PE32 route too: LocalState\registry.sqlite
    x86::set_steam_client_registry(0,"");
    const bool steamstub=steamstub_wrapped(path);
    const bool steamProvider=stage_steam_provider_x86(c.appid,path);
    if(steamProvider) {
        try{cloud_sync(c.appid,c.root,false,0);}catch(...){lp_sink("STEAM cloud download: exception");}
        try{achievements_sync(c.appid,false);}catch(...){lp_sink("STEAM achievements: exception");}
    }
    std::string name=play_runtime::narrow(exe);
    g_launch.pe32Executed=false;g_launch.pe32Finished=false;g_launch.pe32ExitCode=0;
    // The DLLs a game ships beside its executable are what Windows' search
    // finds first, so probe and donor loading use the executable's own directory.
    auto slash=path.find_last_of(L"\\/");
    std::wstring exeDir=slash==std::wstring::npos?std::wstring():path.substr(0,slash);
    { std::string summary;
      if(pe32_probe::map(path,exeDir,summary)){snprintf(line,sizeof line,"PE32 %s",summary.c_str());lp_sink(line);} }
    snprintf(line,sizeof line,"PRUN PE32 engine=x86-interpreter exe=%s",name.c_str());lp_sink(line);
    auto tee=[&name](const std::string& s) {
        // Guest output goes to the probe log line by line, bounded per line.
        size_t start=0;
        while(start<s.size()) {
            size_t nl=s.find('\n',start);if(nl==std::string::npos)nl=s.size();
            std::string row=s.substr(start,nl-start);
            if(row.size()>400)row.resize(400);
            char b[512];snprintf(b,sizeof b,"PE32 OUT %s: %s",name.c_str(),row.c_str());lp_sink(b);
            start=nl+1;
        }
    };
    std::string cmdline="\""+name+"\"";
    if(!c.args.empty())cmdline+=" "+play_runtime::narrow(c.args);
    // The guest runs on this thread (the interpreter is synchronous), so the
    // progress report happens once the image is mapped and before the entry
    // point: from here on the launch is "running", not "preparing".
    auto loaded=[&](const x86::PlayResult& r) {
        g_launch.loaderStarted=now_s();
        g_launch.pe32Executed=true;
        char b[96];snprintf(b,sizeof b,"PE32 guest entry 0x%08x",r.entry);
        lp_publish(c.gen,c.appid,b,false,1,1,"running",false);
    };
    // A game is not a fixture: the interpreter's default 500 million instruction
    // cap is a few seconds of work, so a guest that really runs would be stopped
    // with InstructionLimit. The cap is raised to a value no real session
    // reaches, and stays finite so a runaway guest still ends in a reported
    // fault instead of spinning on the launch thread forever.
    auto readPe32Dll=[](const std::wstring& p) {
        std::vector<uint8_t> bytes;
        FILE* f=_wfopen(p.c_str(),L"rb"); if(!f)return bytes;
        _fseeki64(f,0,SEEK_END); long long n=_ftelli64(f); rewind(f);
        if(n>0&&n<=128ll*1024*1024) { bytes.resize(size_t(n)); if(fread(bytes.data(),1,bytes.size(),f)!=bytes.size())bytes.clear(); }
        fclose(f); return bytes;
    };
    wchar_t self[2048]; DWORD selfLen=GetModuleFileNameW(nullptr,self,2048);
    std::wstring packageDir=selfLen&&selfLen<2048?std::wstring(self,selfLen):std::wstring();
    auto packageSlash=packageDir.find_last_of(L"\\/");
    if(packageSlash!=std::wstring::npos)packageDir.resize(packageSlash);else packageDir.clear();
    auto gameDlls=[&](const std::string& name) {
        std::wstring leaf(name.begin(),name.end()); return readPe32Dll(exeDir+L"\\"+leaf);
    };
    auto runtimeDlls=[&](const std::string& name) {
        if(packageDir.empty())return std::vector<uint8_t>{};
        std::wstring leaf(name.begin(),name.end()); return readPe32Dll(packageDir+L"\\runtime\\x86\\"+leaf);
    };
    auto donorOrigin=[&](const std::string& name,const char* origin) {
        snprintf(line,sizeof line,"x86 donor: %s from %s",name.c_str(),origin); lp_sink(line);
    };
    x86::DllSource vcSource=x86::make_vc110_vc120_source(gameDlls,runtimeDlls,donorOrigin);
    x86::DllSource d3dxSource=x86::make_d3dx9_source(gameDlls,runtimeDlls,vcSource,donorOrigin);
    auto stagedSteam=[&](const std::string& name) {
        const std::wstring p=provider_dir(c.appid)+L"\\"+std::wstring(name.begin(),name.end());
        return readPe32Dll(p);
    };
    x86::DllSource dllSource=x86::make_steam_dll_source(gameDlls,stagedSteam,d3dxSource,steamProvider,steamstub);
    x86::DllLoadObserver donorGaps=[&](const std::string& dll,const std::vector<std::string>& unresolved) {
        if(!x86::is_vc110_vc120_dll(dll)&&!x86::is_d3dx9_donor_dll(dll))return;
        std::string kernel,other; unsigned kernelCount=0,otherCount=0,kernelMore=0,otherMore=0;
        for(const auto& item:unresolved) {
            bool isKernel=item.rfind("kernel32.dll!",0)==0;
            std::string& group=isKernel?kernel:other;
            unsigned& count=isKernel?kernelCount:otherCount;
            unsigned& more=isKernel?kernelMore:otherMore;
            if(count>=8) { ++more; continue; }
            if(!group.empty())group+=",";
            group+=item; ++count;
        }
        std::string summary="x86 donor gaps: "+dll;
        if(!kernel.empty())summary+=" kernel32=["+kernel+(kernelMore?",+"+std::to_string(kernelMore):"")+ "]";
        if(!other.empty())summary+=" other=["+other+(otherMore?",+"+std::to_string(otherMore):"")+ "]";
        if(summary.size()>480)summary.resize(480);
        lp_sink(summary.c_str());
    };
    bool traceFaultContext=GetFileAttributesW((g_dir+L"\\x86trace.txt").c_str())!=INVALID_FILE_ATTRIBUTES;
    const bool dbtRequested=GetFileAttributesW((g_dir+L"\\x86dbt.txt").c_str())!=INVALID_FILE_ATTRIBUTES;
    auto toU16=[](const std::wstring& s){return std::u16string(s.begin(),s.end());};
    static const GUID localLow={0xA520A1A4,0x1780,0x4FF6,{0xBD,0x18,0x16,0x73,0x43,0xC5,0xAF,0x16}};
    static const GUID local={0xF1B32785,0x6FBA,0x4FCF,{0x9D,0x55,0x7B,0x8E,0x7F,0x15,0x70,0x91}};
    using Known=HRESULT(WINAPI*)(const GUID&,DWORD,HANDLE,PWSTR*);
    static HMODULE shell=LoadLibraryExW(L"shell32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto known=shell?(Known)GetProcAddress(shell,"SHGetKnownFolderPath"):nullptr;
    const auto stateRoot=x86_file_guard::normalize(toU16(g_dir),{});
    std::vector<std::u16string> protectedRoots{toU16(g_dir),toU16(c.installRoot),toU16(packageDir)};
    std::vector<std::u16string> allowedRoots{toU16(provider_dir(c.appid))};
    auto gameRoot=x86_file_guard::normalize(toU16(c.root),{});
    if(gameRoot!=stateRoot)allowedRoots.push_back(std::move(gameRoot));
    for(const auto& folder:{std::pair<const char*,const GUID*>{"Local",&local},{"LocalLow",&localLow}}) {
        PWSTR path=nullptr;const bool found=known&&SUCCEEDED(known(*folder.second,0,nullptr,&path))&&path;
        if(found) {
            std::wstring resolved(path);CoTaskMemFree(path);
            lp_sink((std::string("x86 file guard: known folder ")+folder.first+"="+play_runtime::narrow(resolved)).c_str());
            auto normalized=x86_file_guard::normalize(toU16(resolved),{});
            if(normalized!=stateRoot&&x86_file_guard::inside(normalized,stateRoot))allowedRoots.push_back(std::move(normalized));
        } else lp_sink((std::string("x86 file guard: known folder ")+folder.first+" unavailable").c_str());
    }
    x86_file_guard::Policy fileGuard(protectedRoots,allowedRoots,toU16(c.cwd.empty()?exeDir:c.cwd),[](const std::string& message){lp_sink(message.c_str());});
    auto configureX86Gl=[](x86::Host& host) {
        host.registry_call=[](x86::Host::RegistryCall& q) {
            auto key=reinterpret_cast<HKEY>(q.handle);
            std::wstring sub(q.subkey.begin(),q.subkey.end()),name(q.name.begin(),q.name.end());
            auto narrow=[](const std::u16string& value) { int count=WideCharToMultiByte(CP_ACP,0,reinterpret_cast<LPCWCH>(value.data()),int(value.size()),nullptr,0,nullptr,nullptr);std::string out(count>0?count:0,'\0');if(count>0)WideCharToMultiByte(CP_ACP,0,reinterpret_cast<LPCWCH>(value.data()),int(value.size()),out.data(),count,nullptr,nullptr);return out; };
            auto widen=[](const std::string& value) { int count=MultiByteToWideChar(CP_ACP,0,value.data(),int(value.size()),nullptr,0);std::u16string out(count>0?count:0,u'\0');if(count>0)MultiByteToWideChar(CP_ACP,0,value.data(),int(value.size()),reinterpret_cast<LPWSTR>(out.data()),count);return out; };
            std::string sub_a=narrow(q.subkey),name_a=narrow(q.name);
            switch(q.kind) {
            case x86::Host::RegistryCall::Kind::Open: { HKEY out=nullptr;q.status=q.ansi?regb::RegOpenKeyExA(reinterpret_cast<HKEY>(uintptr_t(q.root)),sub_a.empty()?nullptr:sub_a.c_str(),q.options,q.access,&out):regb::RegOpenKeyExW(reinterpret_cast<HKEY>(uintptr_t(q.root)),sub.empty()?nullptr:sub.c_str(),q.options,q.access,&out);q.result_handle=reinterpret_cast<uintptr_t>(out);break; }
            case x86::Host::RegistryCall::Kind::Create: { HKEY out=nullptr;DWORD disposition=0;q.status=q.ansi?regb::RegCreateKeyExA(reinterpret_cast<HKEY>(uintptr_t(q.root)),sub_a.empty()?nullptr:sub_a.c_str(),0,nullptr,q.options,q.access,nullptr,&out,&disposition):regb::RegCreateKeyExW(reinterpret_cast<HKEY>(uintptr_t(q.root)),sub.empty()?nullptr:sub.c_str(),0,nullptr,q.options,q.access,nullptr,&out,&disposition);q.result_handle=reinterpret_cast<uintptr_t>(out);q.disposition=disposition;break; }
            case x86::Host::RegistryCall::Kind::Query: { DWORD size=0,type=0;q.status=q.ansi?regb::RegQueryValueExA(key,name_a.empty()?nullptr:name_a.c_str(),nullptr,&type,nullptr,&size):regb::RegQueryValueExW(key,name.empty()?nullptr:name.c_str(),nullptr,&type,nullptr,&size);if(q.status==ERROR_SUCCESS){q.data.resize(size);if(size)q.status=q.ansi?regb::RegQueryValueExA(key,name_a.empty()?nullptr:name_a.c_str(),nullptr,&type,q.data.data(),&size):regb::RegQueryValueExW(key,name.empty()?nullptr:name.c_str(),nullptr,&type,q.data.data(),&size);q.data.resize(size);}q.type=type;break; }
            case x86::Host::RegistryCall::Kind::Set: q.status=q.ansi?regb::RegSetValueExA(key,name_a.empty()?nullptr:name_a.c_str(),0,q.type,q.data.empty()?nullptr:q.data.data(),DWORD(q.data.size())):regb::RegSetValueExW(key,name.empty()?nullptr:name.c_str(),0,q.type,q.data.empty()?nullptr:q.data.data(),DWORD(q.data.size()));break;
            case x86::Host::RegistryCall::Kind::DeleteValue: q.status=q.ansi?regb::RegDeleteValueA(key,name_a.empty()?nullptr:name_a.c_str()):regb::RegDeleteValueW(key,name.empty()?nullptr:name.c_str());break;
            case x86::Host::RegistryCall::Kind::DeleteKey: q.status=q.ansi?regb::RegDeleteKeyA(key,sub_a.empty()?nullptr:sub_a.c_str()):regb::RegDeleteKeyW(key,sub.empty()?nullptr:sub.c_str());break;
            case x86::Host::RegistryCall::Kind::EnumKey: { if(q.ansi){std::string out(32768,'\0');DWORD size=DWORD(out.size());q.status=regb::RegEnumKeyExA(key,q.index,out.data(),&size,nullptr,nullptr,nullptr,nullptr);if(q.status==ERROR_SUCCESS)q.name=widen(std::string(out.data(),size));}else{std::wstring out(32768,L'\0');DWORD size=DWORD(out.size());q.status=regb::RegEnumKeyExW(key,q.index,out.data(),&size,nullptr,nullptr,nullptr,nullptr);if(q.status==ERROR_SUCCESS)q.name.assign(out.begin(),out.begin()+size);}break; }
            case x86::Host::RegistryCall::Kind::EnumValue: { DWORD size=0,type=0;if(q.ansi){std::string out(32768,'\0');DWORD chars=DWORD(out.size());q.status=regb::RegEnumValueA(key,q.index,out.data(),&chars,nullptr,&type,nullptr,&size);if(q.status==ERROR_SUCCESS){q.name=widen(std::string(out.data(),chars));q.data.resize(size);if(size){chars=DWORD(out.size());q.status=regb::RegEnumValueA(key,q.index,out.data(),&chars,nullptr,&type,q.data.data(),&size);q.data.resize(size);}q.type=type;}}else{std::wstring out(32768,L'\0');DWORD chars=DWORD(out.size());q.status=regb::RegEnumValueW(key,q.index,out.data(),&chars,nullptr,&type,nullptr,&size);if(q.status==ERROR_SUCCESS){q.name.assign(out.begin(),out.begin()+chars);q.data.resize(size);if(size){chars=DWORD(out.size());q.status=regb::RegEnumValueW(key,q.index,out.data(),&chars,nullptr,&type,q.data.data(),&size);q.data.resize(size);}q.type=type;}}break; }
            case x86::Host::RegistryCall::Kind::Close: q.status=regb::RegCloseKey(key);break;
            }
        };
        host.random_bytes=[](uint8_t* data,uint32_t size) { return BCryptGenRandom(nullptr,data,size,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0; };
        static HMODULE records=LoadPackagedLibrary(L"uwph-u32c.dll",0);
        static HMODULE mesa=LoadPackagedLibrary(L"runtime\\opengl32.dll",0);
        static HMODULE d3d9=LoadPackagedLibrary(L"runtime\\d3d9.dll",0);
        using GetMetrics=int(WINAPI*)(int);
        auto get_metrics=records?reinterpret_cast<GetMetrics>(GetProcAddress(records,"GetSystemMetrics")):nullptr;
        host.system_metrics=[get_metrics](int index,int& value) {
            if(!get_metrics)return false;
            value=get_metrics(index);return true;
        };
        static HMODULE dsound=LoadPackagedLibrary(L"runtime\\dsound.dll",0);
        using CreateSound=HRESULT (WINAPI*)(LPCGUID,void**,LPUNKNOWN);
        auto createSound=dsound?reinterpret_cast<CreateSound>(GetProcAddress(dsound,"DirectSoundCreate8")):nullptr;
        host.dsound_factory=[createSound](const void* id,uintptr_t* result)->uintptr_t {
            if(!result)return E_POINTER;*result=0;if(!createSound)return DSERR_NODRIVER;
            void* object=nullptr;HRESULT hr=createSound(static_cast<const GUID*>(id),&object,nullptr);*result=reinterpret_cast<uintptr_t>(object);return uint32_t(hr);
        };
        host.dsound_export=[](const char* name)->uintptr_t {return dsound&&name?reinterpret_cast<uintptr_t>(GetProcAddress(dsound,name)):0;};
        using Create9=void* (WINAPI*)(UINT);
        using Create9Ex=HRESULT (WINAPI*)(UINT,void**);
        auto create9=d3d9?reinterpret_cast<Create9>(GetProcAddress(d3d9,"Direct3DCreate9")):nullptr;
        auto create9ex=d3d9?reinterpret_cast<Create9Ex>(GetProcAddress(d3d9,"Direct3DCreate9Ex")):nullptr;
        host.d3d9_factory=[create9,create9ex](bool ex,uint32_t version,uintptr_t* result)->uintptr_t {
            if(!result)return E_POINTER;
            *result=0;
            if(ex){if(!create9ex)return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);void* object=nullptr;HRESULT hr=create9ex(version,&object);*result=reinterpret_cast<uintptr_t>(object);return uint32_t(hr);}
            if(!create9)return 0;*result=reinterpret_cast<uintptr_t>(create9(version));return 0;
        };
        // Guest windows created through the x86 user32 routes are tokens in
        // window_handles; map them to the host record window. Anything else is
        // passed through: Play's validated DXGI factory maps user32 record
        // windows onto the CoreWindow when the shim creates its chain.
        host.d3d9_window=[&host](uint32_t hwnd)->uintptr_t{auto it=host.window_handles.find(hwnd);return it!=host.window_handles.end()?it->second:uintptr_t(hwnd);};
        using GetDc=HDC(WINAPI*)(HWND);using ReleaseDc=int(WINAPI*)(HWND,HDC);using GetClientRect=BOOL(WINAPI*)(HWND,RECT*);using MapWindowPoint=BOOL(WINAPI*)(HWND,POINT*);
        auto client_to_screen=records?reinterpret_cast<MapWindowPoint>(GetProcAddress(records,"ClientToScreen")):nullptr;
        auto screen_to_client=records?reinterpret_cast<MapWindowPoint>(GetProcAddress(records,"ScreenToClient")):nullptr;
        auto get_client_rect=records?reinterpret_cast<GetClientRect>(GetProcAddress(records,"GetClientRect")):nullptr;
        struct GlWndClassExW { UINT cbSize, style; LRESULT(CALLBACK* lpfnWndProc)(HWND,UINT,WPARAM,LPARAM); int cbClsExtra, cbWndExtra; HINSTANCE hInstance; HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; LPCWSTR lpszMenuName, lpszClassName; HICON hIconSm; };
        using RegisterClass=ATOM(WINAPI*)(const GlWndClassExW*);using CreateWindow=HWND(WINAPI*)(DWORD,LPCWSTR,LPCWSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID);
        using DestroyWindow=int(WINAPI*)(HWND);using UnregisterClass=int(WINAPI*)(LPCWSTR,HINSTANCE);
        using AdjustWindowRect=BOOL(WINAPI*)(RECT*,DWORD,BOOL,DWORD);
        using GetWindowLong=LONG(WINAPI*)(HWND,int);using GetWindowLongPtr=LONG_PTR(WINAPI*)(HWND,int);
        using SetWindowLong=LONG(WINAPI*)(HWND,int,LONG);using SetWindowLongPtr=LONG_PTR(WINAPI*)(HWND,int,LONG_PTR);
        auto set_window_long=records?reinterpret_cast<SetWindowLong>(GetProcAddress(records,"SetWindowLongW")):nullptr;
        auto set_window_long_ptr=records?reinterpret_cast<SetWindowLongPtr>(GetProcAddress(records,"SetWindowLongPtrW")):nullptr;
        auto get_window_long=records?reinterpret_cast<GetWindowLong>(GetProcAddress(records,"GetWindowLongW")):nullptr;
        auto get_window_long_ptr=records?reinterpret_cast<GetWindowLongPtr>(GetProcAddress(records,"GetWindowLongPtrW")):nullptr;
        auto adjust_window_rect=records?reinterpret_cast<AdjustWindowRect>(GetProcAddress(records,"AdjustWindowRectEx")):nullptr;
        auto unregister_class=records?reinterpret_cast<UnregisterClass>(GetProcAddress(records,"UnregisterClassW")):nullptr;
        auto get_dc=records?reinterpret_cast<GetDc>(GetProcAddress(records,"GetDC")):nullptr;
        auto release_dc=records?reinterpret_cast<ReleaseDc>(GetProcAddress(records,"ReleaseDC")):nullptr;
        auto register_class=records?reinterpret_cast<RegisterClass>(GetProcAddress(records,"RegisterClassExW")):nullptr;
        auto create_window=records?reinterpret_cast<CreateWindow>(GetProcAddress(records,"CreateWindowExW")):nullptr;
        auto destroy_window=records?reinterpret_cast<DestroyWindow>(GetProcAddress(records,"DestroyWindow")):nullptr;
        host.register_window_class=[register_class](const std::u16string& name){
            if(!register_class||name.empty())return false;std::wstring wide(name.begin(),name.end());GlWndClassExW wc{};wc.cbSize=sizeof(wc);
            wc.lpfnWndProc=[](HWND h,UINT m,WPARAM w,LPARAM l)->LRESULT{auto def=records?reinterpret_cast<LRESULT(WINAPI*)(HWND,UINT,WPARAM,LPARAM)>(GetProcAddress(records,"DefWindowProcW")):nullptr;return def?def(h,m,w,l):0;};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=wide.c_str();
            return register_class(&wc)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS;
        };
        host.unregister_window_class=[unregister_class](const std::u16string& name){
            if(!unregister_class||name.empty())return false;std::wstring wide(name.begin(),name.end());
            return unregister_class(wide.c_str(),GetModuleHandleW(nullptr))!=0;
        };
        host.map_window_point=[client_to_screen,screen_to_client](uintptr_t hwnd,int32_t& x,int32_t& y,bool to_screen){
            auto fn=to_screen?client_to_screen:screen_to_client;if(!fn)return false;POINT p{x,y};if(!fn(reinterpret_cast<HWND>(hwnd),&p))return false;x=p.x;y=p.y;return true;
        };
        host.get_client_rect=[get_client_rect](uintptr_t hwnd,x86::Host::WindowRect& q){
            if(!get_client_rect)return false;RECT r{};if(!get_client_rect(reinterpret_cast<HWND>(hwnd),&r))return false;
            q={r.left,r.top,r.right,r.bottom};return true;
        };
        host.adjust_window_rect=[adjust_window_rect](x86::Host::WindowRect& q,uint32_t style,bool menu,uint32_t ex){
            if(!adjust_window_rect)return false;RECT r{q.left,q.top,q.right,q.bottom};
            if(!adjust_window_rect(&r,style,menu,ex))return false;
            q={r.left,r.top,r.right,r.bottom};return true;
        };
        host.set_window_long=[set_window_long,set_window_long_ptr](uintptr_t hwnd,int index,intptr_t value,bool pointer)->intptr_t{
            if(pointer)return set_window_long_ptr?set_window_long_ptr(reinterpret_cast<HWND>(hwnd),index,value):0;
            return set_window_long?set_window_long(reinterpret_cast<HWND>(hwnd),index,LONG(value)):0;
        };
        host.get_window_long=[get_window_long,get_window_long_ptr](uintptr_t hwnd,int index,bool pointer)->intptr_t{
            if(pointer)return get_window_long_ptr?get_window_long_ptr(reinterpret_cast<HWND>(hwnd),index):0;
            return get_window_long?get_window_long(reinterpret_cast<HWND>(hwnd),index):0;
        };
        host.create_window=[create_window](const x86::Host::WindowCreate& q)->uintptr_t{
            if(!create_window)return 0;std::wstring cls(q.class_name.begin(),q.class_name.end()),title(q.title.begin(),q.title.end());
            return reinterpret_cast<uintptr_t>(create_window(q.ex_style,cls.c_str(),title.c_str(),q.style,int(q.x),int(q.y),int(q.width),int(q.height),
                reinterpret_cast<HWND>(q.parent),nullptr,GetModuleHandleW(nullptr),nullptr));
        };
        host.destroy_window=[destroy_window](uintptr_t hwnd){return destroy_window&&destroy_window(reinterpret_cast<HWND>(hwnd))!=0;};
        host.get_dc=[get_dc](uintptr_t hwnd)->uintptr_t{return get_dc?reinterpret_cast<uintptr_t>(get_dc(reinterpret_cast<HWND>(hwnd))):0;};
        host.release_dc=[release_dc](uintptr_t hwnd,uintptr_t hdc){return release_dc&&release_dc(reinterpret_cast<HWND>(hwnd),reinterpret_cast<HDC>(hdc))!=0;};
        host.window_call=[](x86::Host::WindowCall& q) {
            auto p=records?GetProcAddress(records,q.name.c_str()):nullptr;
            auto result=[&](auto value){q.result=uintptr_t(value);q.supported=true;};
            auto bytes=[&](const void* source,size_t size){q.output.resize(size);memcpy(q.output.data(),source,size);};
            if(q.name=="ShowWindow"){using Fn=BOOL(WINAPI*)(HWND,int);if(p)result(reinterpret_cast<Fn>(p)((HWND)q.args[0],int(q.args[1])));}
            else if(q.name=="UpdateWindow"){using Fn=BOOL(WINAPI*)(HWND);if(p)result(reinterpret_cast<Fn>(p)((HWND)q.args[0]));}
            else if(q.name=="SetWindowPos"){using Fn=BOOL(WINAPI*)(HWND,HWND,int,int,int,int,UINT);if(p)result(reinterpret_cast<Fn>(p)((HWND)q.args[0],(HWND)q.args[1],int(q.args[2]),int(q.args[3]),int(q.args[4]),int(q.args[5]),UINT(q.args[6])));}
            else if(q.name=="MoveWindow"){using Fn=BOOL(WINAPI*)(HWND,int,int,int,int,BOOL);if(p)result(reinterpret_cast<Fn>(p)((HWND)q.args[0],int(q.args[1]),int(q.args[2]),int(q.args[3]),int(q.args[4]),BOOL(q.args[5])));}
            else if(q.name=="GetClientRect"||q.name=="GetWindowRect"){using Fn=BOOL(WINAPI*)(HWND,RECT*);RECT r{};if(p){BOOL ok=reinterpret_cast<Fn>(p)((HWND)q.args[0],&r);bytes(&r,sizeof(r));q.result=ok;q.supported=true;}}
            else if(q.name=="AdjustWindowRect"||q.name=="AdjustWindowRectEx"){RECT r{};if(q.input.size()==sizeof(r)){memcpy(&r,q.input.data(),sizeof(r));if(q.name=="AdjustWindowRect"){using Fn=BOOL(WINAPI*)(RECT*,DWORD,BOOL);if(p){BOOL ok=reinterpret_cast<Fn>(p)(&r,DWORD(q.args[1]),BOOL(q.args[2]));bytes(&r,sizeof(r));q.result=ok;q.supported=true;}}else{using Fn=BOOL(WINAPI*)(RECT*,DWORD,BOOL,DWORD);if(p){BOOL ok=reinterpret_cast<Fn>(p)(&r,DWORD(q.args[1]),BOOL(q.args[2]),DWORD(q.args[3]));bytes(&r,sizeof(r));q.result=ok;q.supported=true;}}}}
            else if(q.name=="ClientToScreen"||q.name=="ScreenToClient"){POINT v{};if(q.input.size()==sizeof(v)){memcpy(&v,q.input.data(),sizeof(v));using Fn=BOOL(WINAPI*)(HWND,POINT*);if(p){BOOL ok=reinterpret_cast<Fn>(p)((HWND)q.args[0],&v);bytes(&v,sizeof(v));q.result=ok;q.supported=true;}}}
            else if(q.name=="SetWindowTextA"||q.name=="SetWindowTextW"){if(q.name.back()=='W'){std::wstring s(q.input.size()/2,L'\0');if(!q.input.empty())memcpy(s.data(),q.input.data(),q.input.size());using Fn=BOOL(WINAPI*)(HWND,LPCWSTR);if(p)result(reinterpret_cast<Fn>(p)((HWND)q.args[0],s.c_str()));}else{std::string s(q.input.begin(),q.input.end());using Fn=BOOL(WINAPI*)(HWND,LPCSTR);if(p)result(reinterpret_cast<Fn>(p)((HWND)q.args[0],s.c_str()));}}
            else if(q.name=="LoadCursorA"||q.name=="LoadIconA"){using Fn=HANDLE(WINAPI*)(HINSTANCE,LPCSTR);if(p)result(reinterpret_cast<Fn>(p)((HINSTANCE)q.args[0],q.input.empty()?(LPCSTR)q.args[1]:reinterpret_cast<LPCSTR>(q.input.data())));}
            else if(q.name=="LoadCursorW"||q.name=="LoadIconW"){using Fn=HANDLE(WINAPI*)(HINSTANCE,LPCWSTR);if(p)result(reinterpret_cast<Fn>(p)((HINSTANCE)q.args[0],q.input.empty()?(LPCWSTR)q.args[1]:reinterpret_cast<LPCWSTR>(q.input.data())));}
            else if(q.name=="SetCursor"){using Fn=HCURSOR(WINAPI*)(HCURSOR);if(p)result(reinterpret_cast<Fn>(p)((HCURSOR)q.args[0]));}
            else if(q.name=="ShowCursor"){using Fn=int(WINAPI*)(BOOL);if(p)result(reinterpret_cast<Fn>(p)(BOOL(q.args[0])));}
            else if(q.name=="ClipCursor"){using Fn=BOOL(WINAPI*)(const RECT*);RECT r{};if(q.input.size()==sizeof(r))memcpy(&r,q.input.data(),sizeof(r));if(p)result(reinterpret_cast<Fn>(p)(q.input.empty()?nullptr:&r));}
            else if(q.name=="GetClipCursor"){using Fn=BOOL(WINAPI*)(RECT*);RECT r{};if(p){BOOL ok=reinterpret_cast<Fn>(p)(&r);bytes(&r,sizeof(r));q.result=ok;q.supported=true;}}
            else if(q.name=="GetCursorPos"){using Fn=BOOL(WINAPI*)(POINT*);POINT v{};if(p){BOOL ok=reinterpret_cast<Fn>(p)(&v);bytes(&v,sizeof(v));q.result=ok;q.supported=true;}}
            else if(q.name=="IsWindowVisible"||q.name=="IsIconic"||q.name=="SetForegroundWindow"){using Fn=BOOL(WINAPI*)(HWND);if(p)result(reinterpret_cast<Fn>(p)((HWND)q.args[0]));}
            else if(q.name=="GetParent"){using Fn=HWND(WINAPI*)(HWND);if(p)result(reinterpret_cast<Fn>(p)((HWND)q.args[0]));}
            else if(q.name=="AllowSetForegroundWindow"){using Fn=BOOL(WINAPI*)(DWORD);if(p)result(reinterpret_cast<Fn>(p)(DWORD(q.args[0])));}
            else if(q.name=="GetForegroundWindow"||q.name=="GetFocus"||q.name=="GetActiveWindow"){using Fn=HWND(WINAPI*)();if(p)result(reinterpret_cast<Fn>(p)());}
            else if(q.name=="SetFocus"){using Fn=HWND(WINAPI*)(HWND);if(p)result(reinterpret_cast<Fn>(p)((HWND)q.args[0]));}
            else if(q.name=="PostMessageA"||q.name=="PostMessageW"){using Fn=BOOL(WINAPI*)(HWND,UINT,WPARAM,LPARAM);if(p)result(reinterpret_cast<Fn>(p)((HWND)q.args[0],UINT(q.args[1]),WPARAM(q.args[2]),LPARAM(q.args[3])));}
            else if(q.name=="PostQuitMessage"){using Fn=void(WINAPI*)(int);if(p){reinterpret_cast<Fn>(p)(int(q.args[0]));q.result=0;q.supported=true;}}
            else if(q.name=="PeekMessageA"||q.name=="PeekMessageW"||q.name=="GetMessageA"||q.name=="GetMessageW"){MSG m{};BOOL ok=FALSE;if(q.name.find("Peek")==0){using Fn=BOOL(WINAPI*)(MSG*,HWND,UINT,UINT,UINT);if(p)ok=reinterpret_cast<Fn>(p)(&m,(HWND)q.args[1],UINT(q.args[2]),UINT(q.args[3]),UINT(q.args[4]));}else{using Fn=BOOL(WINAPI*)(MSG*,HWND,UINT,UINT);if(p)ok=reinterpret_cast<Fn>(p)(&m,(HWND)q.args[1],UINT(q.args[2]),UINT(q.args[3]));}if(p){struct GuestMsg{uintptr_t hwnd;uint32_t message,wp,lp,time;int32_t x,y;} gm{uintptr_t(m.hwnd),m.message,uint32_t(m.wParam),uint32_t(m.lParam),m.time,m.pt.x,m.pt.y};bytes(&gm,sizeof(gm));q.result=ok;q.supported=true;}}
            else if(q.name=="TranslateMessage"){using Fn=BOOL(WINAPI*)(const MSG*);MSG m{};if(p){m.hwnd=(HWND)q.args[0];m.message=UINT(q.args[1]);m.wParam=WPARAM(q.args[2]);m.lParam=LPARAM(q.args[3]);result(reinterpret_cast<Fn>(p)(&m));}}
            else if(q.name=="DefWindowProcA"||q.name=="DefWindowProcW"){using Fn=LRESULT(WINAPI*)(HWND,UINT,WPARAM,LPARAM);if(p)result(reinterpret_cast<Fn>(p)((HWND)q.args[0],UINT(q.args[1]),WPARAM(q.args[2]),LPARAM(q.args[3])));}
        };
        auto proc=[](const char* name)->uintptr_t {
            uintptr_t p=play_runtime::openglExport(name);
            if(!p&&mesa){p=reinterpret_cast<uintptr_t>(GetProcAddress(mesa,name));if(!p){using GetProc=PROC(WINAPI*)(LPCSTR);auto get=reinterpret_cast<GetProc>(GetProcAddress(mesa,"wglGetProcAddress"));if(get)p=reinterpret_cast<uintptr_t>(get(name));}}
            if(p) SetLastError(ERROR_SUCCESS); // a failed GetProcAddress before the wgl fallback must not leak into the guest
            return p;
        };
        host.gl_call=[proc](x86::Host::GlCall& q) {
            SetLastError(ERROR_SUCCESS);
            struct ErrorCapture { x86::Host::GlCall& call; ~ErrorCapture() { call.last_error=GetLastError(); } } capture{q};
            auto missing=[&](){SetLastError(ERROR_PROC_NOT_FOUND);q.result=0;q.gap=true;};
            auto pointer=[&](const char* name){return proc(name);};
            auto float_arg=[&](size_t index){uint32_t bits=uint32_t(q.args[index]);float value=0;memcpy(&value,&bits,sizeof(value));return value;};
            if(q.name=="ChoosePixelFormat"||q.name=="SetPixelFormat"||q.name=="DescribePixelFormat"||q.name=="SwapBuffers") {
                const char* name=q.name=="ChoosePixelFormat"?"wglChoosePixelFormat":q.name=="SetPixelFormat"?"wglSetPixelFormat":q.name=="DescribePixelFormat"?"wglDescribePixelFormat":"wglSwapBuffers";
                auto p=pointer(name);if(!p){missing();return;}
                if(q.name=="ChoosePixelFormat"){using Fn=int(WINAPI*)(HDC,const PIXELFORMATDESCRIPTOR*);PIXELFORMATDESCRIPTOR d{};if(q.input.size()!=sizeof(d)){missing();return;}memcpy(&d,q.input.data(),sizeof(d));q.result=reinterpret_cast<Fn>(p)(reinterpret_cast<HDC>(q.args[0]),&d);}
                else if(q.name=="SetPixelFormat"){using Fn=BOOL(WINAPI*)(HDC,int,const PIXELFORMATDESCRIPTOR*);PIXELFORMATDESCRIPTOR d{};if(q.input.size()!=sizeof(d)){missing();return;}memcpy(&d,q.input.data(),sizeof(d));q.result=reinterpret_cast<Fn>(p)(reinterpret_cast<HDC>(q.args[0]),int(q.args[1]),&d);}
                else if(q.name=="DescribePixelFormat"){using Fn=int(WINAPI*)(HDC,int,UINT,PIXELFORMATDESCRIPTOR*);PIXELFORMATDESCRIPTOR d{};q.result=reinterpret_cast<Fn>(p)(reinterpret_cast<HDC>(q.args[0]),int(q.args[1]),UINT(q.output.size()),q.output.empty()?nullptr:&d);if(q.result&&!q.output.empty())memcpy(q.output.data(),&d,q.output.size());}
                else {using Fn=BOOL(WINAPI*)(HDC);q.result=reinterpret_cast<Fn>(p)(reinterpret_cast<HDC>(q.args[0]));}return;
            }
            if(q.name=="wglCreateContext"){using Fn=HGLRC(WINAPI*)(HDC);auto p=pointer(q.name.c_str());q.result=p?reinterpret_cast<uintptr_t>(reinterpret_cast<Fn>(p)(reinterpret_cast<HDC>(q.args[0]))):0;return;}
            if(q.name=="wglDeleteContext"){using Fn=BOOL(WINAPI*)(HGLRC);auto p=pointer(q.name.c_str());q.result=p&&reinterpret_cast<Fn>(p)(reinterpret_cast<HGLRC>(q.args[0]));return;}
            if(q.name=="wglMakeCurrent"){using Fn=BOOL(WINAPI*)(HDC,HGLRC);auto p=pointer(q.name.c_str());q.result=p&&reinterpret_cast<Fn>(p)(reinterpret_cast<HDC>(q.args[0]),reinterpret_cast<HGLRC>(q.args[1]));return;}
            if(q.name=="wglGetCurrentContext"){using Fn=HGLRC(WINAPI*)();auto p=pointer(q.name.c_str());q.result=p?reinterpret_cast<uintptr_t>(reinterpret_cast<Fn>(p)()):0;return;}
            if(q.name=="wglGetCurrentDC"){using Fn=HDC(WINAPI*)();auto p=pointer(q.name.c_str());q.result=p?reinterpret_cast<uintptr_t>(reinterpret_cast<Fn>(p)()):0;return;}
            if(q.name=="glClearColor"){using Fn=void(APIENTRY*)(float,float,float,float);auto p=pointer(q.name.c_str());if(p)reinterpret_cast<Fn>(p)(float_arg(0),float_arg(1),float_arg(2),float_arg(3));else missing();return;}
            if(q.name=="glClear"){using Fn=void(APIENTRY*)(unsigned);auto p=pointer(q.name.c_str());if(p)reinterpret_cast<Fn>(p)(unsigned(q.args[0]));else missing();return;}
            if(q.name=="glViewport"){using Fn=void(APIENTRY*)(int,int,int,int);auto p=pointer(q.name.c_str());if(p)reinterpret_cast<Fn>(p)(int(q.args[0]),int(q.args[1]),int(q.args[2]),int(q.args[3]));else missing();return;}
            if(q.name=="glEnableClientState"){using Fn=void(APIENTRY*)(unsigned);auto p=pointer(q.name.c_str());if(p)reinterpret_cast<Fn>(p)(unsigned(q.args[0]));else missing();return;}
            if(q.name=="glBindBuffer"){using Fn=void(APIENTRY*)(unsigned,unsigned);auto p=pointer(q.name.c_str());if(p)reinterpret_cast<Fn>(p)(unsigned(q.args[0]),unsigned(q.args[1]));else missing();return;}
            if(q.name=="glVertexPointer"){using Fn=void(APIENTRY*)(int,unsigned,int,const void*);auto p=pointer(q.name.c_str());if(p)reinterpret_cast<Fn>(p)(int(q.args[0]),unsigned(q.args[1]),int(q.args[2]),reinterpret_cast<const void*>(uintptr_t(q.args[3])));else missing();return;}
            if(q.name=="glDrawArrays"){using Vertex=void(APIENTRY*)(int,unsigned,int,const void*);using Draw=void(APIENTRY*)(unsigned,int,int);auto vp=pointer("glVertexPointer"),dp=pointer(q.name.c_str());if(!dp){missing();return;}if(!q.input.empty()){if(!vp){missing();return;}reinterpret_cast<Vertex>(vp)(int(q.args[3]),unsigned(q.args[4]),0,q.input.data());}reinterpret_cast<Draw>(dp)(unsigned(q.args[0]),int(q.args[1]),int(q.args[2]));return;}
            if(q.name=="x86GlDrawElementsClient"){using Vertex=void(APIENTRY*)(int,unsigned,int,const void*);using Draw=void(APIENTRY*)(unsigned,int,int);auto vp=pointer("glVertexPointer"),dp=pointer("glDrawArrays");if(!vp||!dp){missing();return;}reinterpret_cast<Vertex>(vp)(int(q.args[2]),unsigned(q.args[3]),0,q.input.data());reinterpret_cast<Draw>(dp)(unsigned(q.args[0]),0,int(q.args[1]));return;}
            if(q.name=="glDrawElements"){using Fn=void(APIENTRY*)(unsigned,int,unsigned,const void*);auto p=pointer(q.name.c_str());if(p)reinterpret_cast<Fn>(p)(unsigned(q.args[0]),int(q.args[1]),unsigned(q.args[2]),reinterpret_cast<const void*>(uintptr_t(q.args[3])));else missing();return;}
            if(q.name=="glReadPixels"){using Fn=void(APIENTRY*)(int,int,int,int,unsigned,unsigned,void*);auto p=pointer(q.name.c_str());if(p)reinterpret_cast<Fn>(p)(int(q.args[0]),int(q.args[1]),int(q.args[2]),int(q.args[3]),unsigned(q.args[4]),unsigned(q.args[5]),q.output.empty()?nullptr:q.output.data());else missing();return;}
            if(q.name=="glMapBufferRange"||q.name=="glMapBuffer") {
                using GetSize=void(APIENTRY*)(unsigned,unsigned,int*);using GetData=void(APIENTRY*)(unsigned,intptr_t,intptr_t,void*);
                auto get_size=pointer("glGetBufferParameteriv"),get_data=pointer("glGetBufferSubData");if(!get_size||!get_data){missing();return;}
                int size=q.name=="glMapBufferRange"?int(q.args[2]):0;
                if(q.name=="glMapBuffer"){reinterpret_cast<GetSize>(get_size)(unsigned(q.args[0]),0x8764,&size);if(size<=0||size>(1<<26)){missing();return;}}
                if(size<=0||size>(1<<26)){missing();return;}q.output.resize(size);
                intptr_t offset=q.name=="glMapBufferRange"?intptr_t(int32_t(q.args[1])):0;
                reinterpret_cast<GetData>(get_data)(unsigned(q.args[0]),offset,size,q.output.data());return;
            }
            if(q.name=="x86GlUnmapBuffer") {
                using PutData=void(APIENTRY*)(unsigned,intptr_t,intptr_t,const void*);
                auto put=pointer("glBufferSubData");if(!put){missing();return;}
                if(!q.input.empty())reinterpret_cast<PutData>(put)(unsigned(q.args[0]),intptr_t(q.args[1]),intptr_t(q.input.size()),q.input.data());
                q.result=1;return; // GL_TRUE: glMapBuffer never host-mapped the buffer, so the copy-back above is the whole unmap
            }
            if(q.name=="glGetError"){using Fn=unsigned(APIENTRY*)();auto p=pointer(q.name.c_str());q.result=p?reinterpret_cast<Fn>(p)():0;return;}
            if(q.name=="glTexImage2D"){using Fn=void(APIENTRY*)(unsigned,int,int,int,int,int,unsigned,unsigned,const void*);auto p=pointer(q.name.c_str());if(!p){missing();return;}reinterpret_cast<Fn>(p)(unsigned(q.args[0]),int(q.args[1]),int(q.args[2]),int(q.args[3]),int(q.args[4]),int(q.args[5]),unsigned(q.args[6]),unsigned(q.args[7]),q.input.empty()?nullptr:q.input.data());return;}
            if(q.name=="glCreateShader"){using Fn=unsigned(APIENTRY*)(unsigned);auto p=pointer(q.name.c_str());if(p)q.result=reinterpret_cast<Fn>(p)(unsigned(q.args[0]));else missing();return;}
            if(q.name=="glShaderSource"){using Fn=void(APIENTRY*)(unsigned,int,const char*const*,const int*);auto p=pointer(q.name.c_str());if(!p||q.shader_sources.empty()){missing();return;}std::vector<const char*> sources; sources.reserve(q.shader_sources.size());for(const auto& source:q.shader_sources)sources.push_back(reinterpret_cast<const char*>(source.data()));const int* lengths=q.shader_lengths.empty()?nullptr:q.shader_lengths.data();reinterpret_cast<Fn>(p)(unsigned(q.args[0]),int(sources.size()),sources.data(),lengths);return;}
            if(q.name=="glCompileShader"){using Fn=void(APIENTRY*)(unsigned);auto p=pointer(q.name.c_str());if(p)reinterpret_cast<Fn>(p)(unsigned(q.args[0]));else missing();return;}
            if(q.name=="glGetIntegerv"||q.name=="glGetFloatv"){auto p=pointer(q.name.c_str());if(!p||q.output.empty()){missing();return;}q.args[1]=reinterpret_cast<uintptr_t>(q.output.data());q.result=x86::gl_call_generated(q.signature,p,q.args.data());return;}
            if(q.name.compare(0,9,"glUniform")==0&&!q.input.empty()){auto p=pointer(q.name.c_str());if(!p){missing();return;}size_t pointer_arg=q.name.find("Matrix")!=std::string::npos?3u:2u;q.args[pointer_arg]=reinterpret_cast<uintptr_t>(q.input.data());q.result=x86::gl_call_generated(q.signature,p,q.args.data());return;}
            if(q.signature!=65535){auto p=pointer(q.name.c_str());if(!p){missing();return;}q.result=x86::gl_call_generated(q.signature,p,q.args.empty()?nullptr:q.args.data());return;}
            missing();
        };
        host.xinput_call=[](x86::Host::XInputCall& request) {
            using Kind=x86::Host::XInputCall::Kind;
            switch(request.kind) {
            case Kind::GetState: {
                XINPUT_STATE state{};request.result=xinput_core::get_state(request.index,&state);
                if(!request.result){const auto* bytes=reinterpret_cast<const uint8_t*>(&state);request.output.assign(bytes,bytes+sizeof(state));}
                break;
            }
            case Kind::GetStateEx: {
                if(request.index>=xinput_core::slots.size()){request.result=ERROR_DEVICE_NOT_CONNECTED;break;}
                std::lock_guard<std::mutex> lock(xinput_core::mutex);
                const auto& slot=xinput_core::slots[request.index];
                if(!slot.connected){request.result=ERROR_DEVICE_NOT_CONNECTED;break;}
                const auto* bytes=reinterpret_cast<const uint8_t*>(&slot.state);request.output.assign(bytes,bytes+sizeof(slot.state));request.result=ERROR_SUCCESS;
                break;
            }
            case Kind::SetState: {
                if(request.input.size()!=sizeof(XINPUT_VIBRATION)){request.result=ERROR_INVALID_PARAMETER;break;}
                XINPUT_VIBRATION vibration{};memcpy(&vibration,request.input.data(),sizeof(vibration));
                request.result=xinput_core::set_state(request.index,&vibration);break;
            }
            case Kind::GetCapabilities: {
                XINPUT_CAPABILITIES capabilities{};request.result=xinput_core::capabilities(request.index,request.flags,&capabilities);
                if(!request.result){const auto* bytes=reinterpret_cast<const uint8_t*>(&capabilities);request.output.assign(bytes,bytes+sizeof(capabilities));}
                break;
            }
            case Kind::Enable:xinput_core::enable(BOOL(request.flags));request.result=ERROR_SUCCESS;break;
            case Kind::GetBatteryInformation: {
                XINPUT_BATTERY_INFORMATION battery{};request.result=xinput_core::battery(request.index,BYTE(request.device_type),&battery);
                if(!request.result){const auto* bytes=reinterpret_cast<const uint8_t*>(&battery);request.output.assign(bytes,bytes+sizeof(battery));}
                break;
            }
            case Kind::GetKeystroke: {
                XINPUT_STATE state{};request.result=xinput_core::get_state(request.index,&state);
                if(!request.result)request.result=ERROR_EMPTY;
                break;
            }
            }
        };
    };
    auto r=x86::run(path,c.cwd,cmdline,tee,loaded,x86::kGuestRunCap,dllSource,traceFaultContext,[&](const x86::PlayResult& mapped) {
        gap_summary::Binder imports;
        for(const auto& item:mapped.gap_imports)
            imports.record(std::get<0>(item),std::get<1>(item),std::get<2>(item));
        for(const auto& line:imports.report(play_runtime::narrow(exe),"x86").lines) lp_sink(line.c_str());
    },donorGaps,configureX86Gl,[&fileGuard,toU16](const char* operation,const std::u16string& guestPath){
        wchar_t current[32768]{};DWORD n=GetCurrentDirectoryW(32768,current);
        if(!n||n>=32768)return false;
        return fileGuard.allow(operation,guestPath,toU16(std::wstring(current,n)));
    },dbtRequested);
    snprintf(line,sizeof line,"x86 dbt: %s reason=%s",r.dbt_enabled?"on":"off",r.dbt_reason.empty()?"unavailable":r.dbt_reason.c_str());lp_sink(line);
    snprintf(line,sizeof line,"x86 dbt: translated=%llu interpreted=%llu blocks",(unsigned long long)r.translated_blocks,(unsigned long long)r.interpreted_blocks);lp_sink(line);
    if(traceFaultContext&&steamstub_wrapped(path)) {
        snprintf(line,sizeof line,"PE32 SteamStub probe: .bind=1 x86SteamBridge=%s ActiveProcess=%s PEB.BeingDebugged=%u PEB.NtGlobalFlag=0x%08x",
                 steamProvider?"staged":"not-staged",steamProvider?"seeded":"not-seeded",unsigned(r.peb_being_debugged),r.peb_nt_global_flag);lp_sink(line);
    }
    if(steamProvider) {
        try{cloud_sync(c.appid,c.root,true,0);}catch(...){lp_sink("STEAM cloud upload: exception");}
        try{achievements_sync(c.appid,true);}catch(...){lp_sink("STEAM achievements: upload exception");}
    }
    x86::set_steam_client_registry(0,"");
    c.mapped=r.loaded?1:0;
    g_launch.pe32Finished=r.finished;
    g_launch.pe32ExitCode=r.exit_code;
    snprintf(line,sizeof line,"PRUN PE32 %s base=0x%08x entry=0x%08x imports=%u bound=%u relocs=%u instructions=%llu",
             r.loaded?"loaded":"NOT-LOADED",r.base,r.entry,r.imports,r.bound,r.relocations,(unsigned long long)r.instructions);lp_sink(line);
    for(auto& u:r.unresolved){snprintf(line,sizeof line,"PE32 UNRESOLVED %s",u.c_str());lp_sink(line);}
    // The result text and the "PRUN EVENT x86 route:" lines come from one place
    // (x86_route.h): a clean exit is success, and every other ending names the
    // first unresolved import or the unsupported opcode — never a silent fallback.
    auto rep=x86_route::report(r,name);
    for(auto& l:rep.lines)lp_sink(l.c_str());
    for(auto& l:rep.gap)lp_sink(l.c_str());
    c.ok=rep.ok;
    c.failName=rep.ok?std::string():rep.result;
}
static void lp_run_managed(LpCtx& c,const std::wstring& path,const std::wstring& exe) {
    char line[512];
    g_launch.pe32Executed=false;g_launch.pe32Finished=false;g_launch.pe32ExitCode=0;
    wchar_t self[2048];DWORD n=GetModuleFileNameW(nullptr,self,2048);
    std::wstring package=n&&n<2048?std::wstring(self,n):std::wstring();
    auto slash=package.find_last_of(L"\\/");
    if(slash==std::wstring::npos){c.failName="Mono runtime: launcher path unavailable";return;}
    package.resize(slash);
    std::string runtime=play_runtime::narrow(package+L"\\runtime\\mono");
    std::string assembly=play_runtime::narrow(path);
    std::vector<std::string> args;
    for(const auto& token:play_runtime::command_line_tokens(c.args))args.push_back(play_runtime::narrow(token));
    snprintf(line,sizeof line,"PRUN CLR engine=Mono-x64 exe=%s",play_runtime::narrow(exe).c_str());lp_sink(line);
    const std::wstring monoPath=package+L"\\runtime\\mono\\mono-2.0-sgen.dll";
    // Mono imports version.dll from the Wine donor: probe and wire it before mapping, as the native path does.
    if(!donor_pe::probe(lp_sink)) {c.failName="Wine donor initialization/behavior";lp_sink("DWPE FAIL donor behavior gate; no game code executed");return;}
    mlp::donor_resolve=donor_pe::exported;
    auto mono=play_runtime::map_managed_runtime(package,monoPath,path,c.args,c.cwd,lp_sink);
    if(!mono){c.failName="Mono: Play loader could not map mono-2.0-sgen.dll";return;}
    auto r=mono_host::run(runtime,assembly,args,[&]{
        g_launch.loaderStarted=now_s();g_launch.pe32Executed=true;
        lp_publish(c.gen,c.appid,"Mono loaded managed assembly",false,1,1,"running",false);
    },[mono](const char* name){return (void*)play_runtime::managed_export(mono,name);},
        [](const char* library,const char* symbol){return play_runtime::managed_pinvoke(library,symbol);});
    c.mapped=r.loaded?1:0;
    if(r.executed){g_launch.pe32Finished=true;g_launch.pe32ExitCode=uint32_t(r.exit_code);}
    c.ok=r.executed&&r.exit_code==0;
    if(!r.error.empty())c.failName="Mono: "+r.error;
    else if(r.executed)c.failName="Mono managed entry exited with code "+std::to_string(r.exit_code);
    snprintf(line,sizeof line,"PRUN CLR loaded=%d executed=%d exit=%d",r.loaded?1:0,r.executed?1:0,r.exit_code);lp_sink(line);
}
static void lp_run_impl(LpCtx& c) {
    // Release a genuine client retained by the previous completed game.
    genuine_steam_shutdown();
    // A previous launch that never ran game code was released; nothing of its
    // Steam routing or final upload carries over to this one.
    play_runtime::reset_launch_config();g_signedClient.clear();g_clientAccount=0;
    g_finalSync=nullptr;g_finalSyncDone=false;
    wine_gap::report(c.root);
    if(c.names.empty()) {c.failName="no executable";return;}
    // A Steam launch option may name an executable in a subdirectory: the guest
    // runs from that directory (DLL search and current directory), as on
    // Windows; Steam Cloud keeps the install root.
    auto rel=c.names.back();auto slash=rel.find_last_of(L'\\');
    std::wstring exe=slash==std::wstring::npos?rel:rel.substr(slash+1);
    std::wstring exeDir=slash==std::wstring::npos?c.root:c.root+L"\\"+rel.substr(0,slash);
    std::wstring exePath=exeDir+L"\\"+exe;
    if(mono_host::managed_pe_file(play_runtime::narrow(exePath))) {
        lp_run_managed(c,exePath,exe);return;
    }
    if(pe_machine(exePath)==IMAGE_FILE_MACHINE_I386) {
        lp_run_pe32(c,exeDir+L"\\"+exe,exe);return;
    }
    // These are behavior/ABI prerequisites, not optional progress diagnostics.
    // Never expose donor exports to executable guests after a failed probe.
    if(!donor_pe::probe(lp_sink)) {c.failName="Wine donor initialization/behavior";lp_sink("DWPE FAIL donor behavior gate; no game code executed");return;}
    mlp::donor_resolve=donor_pe::exported;
    // By-name loads of probed donors (the D3D10 shim's d3dcompiler_47) are
    // served the gate-published image; the probe above owns publication.
    play_runtime::set_donor_bridge([](const wchar_t* name){return donor_pe::module(name);},
                                   [](HMODULE h,const char* name,WORD ordinal){return donor_pe::from_handle(h,name,ordinal);});
    if(!user32_core::test(lp_sink)) {c.failName="u32c-test.exe";lp_sink("U32C FAIL regression PE; no game code executed");return;}
    regb::init(g_dir,lp_sink); // guest registry (PlayerPrefs): LocalState\registry.sqlite
    const bool steamProvider=stage_steam_provider(c.appid,exeDir+L"\\"+exe);
    if(steamProvider) {
        try{cloud_sync(c.appid,c.root,false,0);}catch(...){lp_sink("STEAM cloud download: exception");}
        try{achievements_sync(c.appid,false);}catch(...){lp_sink("STEAM achievements: exception");}
        try{auth_start(c.appid);}catch(...){lp_sink("STEAM auth tickets: bridge did not start");}
        try{presence_start(c.appid);}catch(...){lp_sink("STEAM presence: keeper did not start");}
        try{leaderboard_start(c.appid);}catch(...){lp_sink("STEAM leaderboards: bridge did not start");}
    }
    if(steamProvider){uint32_t appid=c.appid;std::wstring root=c.root;g_finalSync=[appid,root]{final_sync(appid,root);};}
    if(!g_signedClient.empty()&&!play_runtime::seed_steam_client_registry(g_signedClient,g_clientAccount))
        lp_sink("STEAM client: guest registry not seeded; the game's Steam client lookup will fail");
    {   // Wine's C++ runtime packaged beside the launcher (build.sh: layout\runtime).
        wchar_t self[2048];DWORD n=GetModuleFileNameW(nullptr,self,2048);
        if(n&&n<2048){std::wstring package(self,n);package.resize(package.find_last_of(L"\\/")+1);play_runtime::set_runtime_directory(package+L"runtime");}
    }
    // The watchdog may already have reported this launch as failed while Steam
    // was being prepared; then the game must not start behind that result.
    {std::lock_guard<std::mutex> lock(g_launch.m);
     if(g_launch.gen!=c.gen||g_launch.phase!=1){c.failName="launch abandoned by the watchdog before the game started";return;}
     g_launch.loaderStarted=now_s();}
    const bool crashContextOptIn=GetFileAttributesW((g_dir+L"\\crashctx.txt").c_str())!=INVALID_FILE_ATTRIBUTES;
    if(!play_runtime::start(exeDir,exe,c.args,lp_sink,c.cwd,crashContextOptIn)) {c.failName="guest already started; restart launcher before next Play";return;}
    auto r=play_runtime::state;
    ULONGLONG end=GetTickCount64()+110000; bool reportedRunning=false;
    for(;;) {
        bool finished=r->finished.load(std::memory_order_acquire);
        auto outcome=launch_status::observe(finished,finished&&r->success&&r->code==0,r->milestone.load(),GetTickCount64()>=end);
        if(launch_status::terminal(outcome)) {
            c.ok=outcome==launch_status::Outcome::completed;
            c.timedOut=outcome==launch_status::Outcome::timedOut;
            c.fault=finished?r->result:"watchdog timeout 110 s; last event: "+r->snapshot();
            break;
        }
        if(outcome==launch_status::Outcome::running && !reportedRunning) {
            lp_publish(c.gen,c.appid,r->snapshot(),false,r->mappedCount,r->moduleCount,"running",false);
            reportedRunning=true;
        }
        if(r->worker)WaitForSingleObject(r->worker,50);else Sleep(50);
    }
    if(unsigned n=r->invalidHandles.load()){char line[96];snprintf(line,sizeof line,"PRUN invalid-handle raises continued=%u",n);lp_sink(line);}
    {auto& st=rwx_emulation::state().stats;
     if(unsigned n=st.regions.load()){char line[160];snprintf(line,sizeof line,"PRUN rwx-emulation regions=%u executeFlips=%u writeFlips=%u retries=%u failures=%u",n,st.executeFlips.load(),st.writeFlips.load(),st.retries.load(),st.failures.load());lp_sink(line);}}
    if(steamProvider)final_sync(c.appid,c.root);
    c.mapped=r->mappedCount;
    for(auto& row:mlp::summaries) {
        c.resolved+=row.second.resolved;c.stubbed+=row.second.stubbed;
        char line[512];snprintf(line,sizeof line,"MLP SUMMARY dll=%s resolved=%u stubbed=%u first-missing=%s",row.first.c_str(),row.second.resolved,row.second.stubbed,row.second.first.c_str());lp_sink(line);
    }
    mlp::report_kernel();
}
// C++ try/catch cannot share a frame with SEH __try, so the final sync gets its own.
static void lp_final_sync() { if(g_finalSync) try { g_finalSync(); } catch(...) {} }
static bool lp_run_seh(LpCtx* c) {     // no C++ objects with destructors in this frame: SEH guard so a probe bug cannot take the launcher down
    bool noFault = true;
    __try { lp_run_impl(*c); }
    __except (play_runtime::state?play_runtime::filter(GetExceptionInformation()):EXCEPTION_EXECUTE_HANDLER) { noFault = false; }
    lp_final_sync();
    return noFault;
}
// Cached appinfo (the downloader writes it); read only, no network on this path.
static bool load_appinfo(uint32_t appid,Vdf& root,const Vdf*& app) {
    FILE* f=_wfopen((g_dir+L"\\appinfo_"+widen(std::to_string(appid))+L".vdf").c_str(),L"rb");
    if(!f)return false;
    std::string text;char b[65536];size_t n;
    while((n=fread(b,1,sizeof b,f))>0&&text.size()<(16u<<20))text.append(b,n);
    fclose(f);
    while(!text.empty()&&text.back()==0)text.pop_back();
    if(text.empty()||!vdf_parse(text,root))return false;
    app=root.get("appinfo");if(!app)app=&root;
    return true;
}
struct LaunchTarget { std::wstring exe; std::string args, why, workdir; };   // exe is install-relative
// PLAY_FORCE_VULKAN: start a Unity title on the Vulkan renderer instead of the
// default D3D11 (launch_target.h), so the packaged Vulkan stack is exercised by
// a real game. Per game: LocalState\vulkan_<appid>.txt; launcher-wide:
// LocalState\vulkan.txt; otherwise the PLAY_FORCE_VULKAN environment variable.
// A file that exists decides for its scope, so a per-game "0" keeps D3D11 even
// when the global setting asks for Vulkan. The default is D3D11.
static bool force_vulkan(uint32_t appid) {
    std::string text;
    if(read_file_s(g_dir+L"\\vulkan_"+widen(std::to_string(appid))+L".txt",text)) return launch_target::vulkan_setting(text);
    if(read_file_s(g_dir+L"\\vulkan.txt",text)) return launch_target::vulkan_setting(text);
    wchar_t value[8]{};
    DWORD n=GetEnvironmentVariableW(L"PLAY_FORCE_VULKAN",value,8);
    return n && n < 8 && launch_target::vulkan_flag(play_runtime::narrow(value));
}
// The game's own Steam launch option when known, as the Steam client picks it;
// otherwise the largest top-level .exe (a 64-bit one first). Engine options come
// from the game's files (Unity: UnityPlayer.dll beside the executable), never
// from its app ID. A 64-bit option still wins over a 32-bit one typed "default"
// (Hades ships x64 and x86 entries), because the 64-bit guest runs natively
// while a 32-bit one runs in the x86 interpreter. wanted (the picker's choice)
// starts that option instead of the preferred one.
static LaunchTarget pick_target(uint32_t appid, const std::wstring& root, const std::string* wanted=nullptr) {
    LaunchTarget t; Vdf info; const Vdf* app=nullptr; const launch_target::Entry* chosen=nullptr;
    std::vector<launch_target::Entry> options;
    auto pe_arch=[&](const std::string& executable) {
        std::string rel; if(!launch_target::relative_path(executable,rel)) return int(launch_target::archUnknown);
        switch(pe_machine(root+L"\\"+widen(rel))) {
            case IMAGE_FILE_MACHINE_AMD64: return int(launch_target::arch64);
            case IMAGE_FILE_MACHINE_I386: return int(launch_target::arch32);
            default: return int(launch_target::archUnknown);
        }
    };
    if(load_appinfo(appid,info,app)) {
        options=launch_target::entries(*app);
        std::string rel;
        auto order=launch_target::ranked(options,pe_arch);
        if(wanted) {   // the picker's row: only that option, never a silent fallback to another
            const launch_target::Entry* only=nullptr;
            for(auto e:order) if(launch_target::entry_key(*e)==*wanted ||
                                  (wanted->find('\t')==std::string::npos && e->executable==*wanted)) { only=e; break; }
            order.clear();
            if(only) order.push_back(only);
            else t.why="the chosen Steam launch option is no longer in the app's appinfo; ";
        }
        if(order.empty()&&t.why.empty()) t.why="no Windows Steam launch option; ";
        for(auto e:order) {
            if(!launch_target::relative_path(e->executable,rel)) {t.why="Steam launch option \""+e->executable+"\" is not an install-relative path; ";continue;}
            if(GetFileAttributesW((root+L"\\"+widen(rel)).c_str())==INVALID_FILE_ATTRIBUTES) {t.why="Steam launch option \""+rel+"\" is not installed; ";continue;}
            t.exe=widen(rel);chosen=e;
            t.why="Steam launch option \""+rel+"\""+(launch_target::arch_of(*e,pe_arch)==launch_target::arch64?" (64-bit)":"");
            break;
        }
    } else t.why="no cached app info; ";
    if(t.exe.empty()&&wanted) return t;   // a picked option that cannot start must not become another .exe
    if(t.exe.empty()) {
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((root + L"\\*.exe").c_str(), &fd); std::vector<launch_target::Candidate> exes;
        if (h != INVALID_HANDLE_VALUE) { do { std::wstring n = fd.cFileName; std::wstring l = n; for (auto& ch : l) ch = (wchar_t)towlower(ch);
                if (l.find(L"unins") != std::wstring::npos || l.find(L"crash") != std::wstring::npos || l.find(L"redist") != std::wstring::npos || l.find(L"setup") != std::wstring::npos) continue;
                uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                int machine=pe_machine(root+L"\\"+n);
                int arch=machine==IMAGE_FILE_MACHINE_AMD64?launch_target::arch64:
                         machine==IMAGE_FILE_MACHINE_I386?launch_target::arch32:launch_target::archUnknown;
                exes.push_back({play_runtime::narrow(n),sz,arch}); } while (FindNextFileW(h, &fd)); FindClose(h); }
        auto best=launch_target::largest(exes);
        if (!best) { t.why += "no launchable .exe in install dir"; return t; }
        t.exe=widen(best->name);t.why+=best->arch==launch_target::arch64?"largest 64-bit top-level .exe":"largest top-level .exe";
    }
    auto slash=t.exe.find_last_of(L'\\');
    std::wstring dir=slash==std::wstring::npos?root:root+L"\\"+t.exe.substr(0,slash);
    bool unity=GetFileAttributesW((dir+L"\\UnityPlayer.dll").c_str())!=INVALID_FILE_ATTRIBUTES;
    const bool vulkan=unity&&force_vulkan(appid);
    t.args=launch_target::arguments(chosen,unity,vulkan);
    // The option's own working directory (install-relative), as Steam honours it;
    // the executable's directory when the option names none.
    if(chosen&&!chosen->workingdir.empty()) { std::string wd; if(launch_target::relative_path(chosen->workingdir,wd)) t.workdir=wd; }
    if(unity)t.why+=vulkan?"; Unity player, PLAY_FORCE_VULKAN":"; Unity player";
    return t;
}

// Serialize progress, completion and watchdog publication. A terminal result
// cannot be replaced by a late milestone or an obsolete loader generation.
static void lp_publish(int gen,uint32_t appid,const std::string& result,bool ok,int mapped,int total,const char* status,bool terminal) {
    std::lock_guard<std::mutex> lock(g_launch.m);
    if(g_launch.gen!=gen || (g_launch.phase!=1 && g_launch.phase!=3))return;
    auto runtime=play_runtime::observed.load(std::memory_order_acquire);
    // Either engine: the native loader's state, or the 32-bit interpreter's own
    // report (it has no play_runtime::State; see LaunchState::pe32*).
    bool executed=(runtime && runtime->executed) || g_launch.pe32Executed.load();
    bool finished=runtime ? runtime->finished.load(std::memory_order_acquire) : g_launch.pe32Finished.load();
    bool success=runtime ? runtime->success : g_launch.pe32Finished.load();
    uint32_t code=runtime ? runtime->code : g_launch.pe32ExitCode.load();
    std::string last=runtime?runtime->snapshot():result;
    std::string json="{\"appid\":"+std::to_string(appid)+",\"attemptId\":\""+g_launch.attempt+"\",\"time\":\""+utc_iso()+"\",\"stage\":\""+(executed?"guest-execution":"loader")+"\",\"state\":\""+status+"\",\"terminal\":"+(terminal?"true":"false")+",\"ok\":"+(ok?"true":"false")+",\"mapped\":"+std::to_string(mapped)+",\"total\":"+std::to_string(total)+",\"gameCodeExecuted\":"+(executed?"true":"false")+",\"exitCode\":"+(finished&&success?std::to_string(code):"null")+",\"lastEvent\":\""+json_esc(last)+"\",\"result\":\""+json_esc(result)+"\"}\n";
    write_text(g_dir+L"\\launch_result.json",json);
    g_launch.result=result;g_launch.ok=ok;
    logf("launch: %s attempt=%s ok=%d mapped=%d/%d gameCodeExecuted=%s: %s",terminal?"RESULT":"RUNNING",g_launch.attempt.c_str(),ok?1:0,mapped,total,executed?"true":"false",result.c_str());
    if(terminal) {   // LocalState\launch_history.log: one line per finished launch, newest last
        const std::wstring history=g_dir+L"\\launch_history.log";
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if(GetFileAttributesExW(history.c_str(),GetFileExInfoStandard,&a)&&(a.nFileSizeHigh||a.nFileSizeLow>(512u<<10)))
            MoveFileExW(history.c_str(),(history+L".old").c_str(),MOVEFILE_REPLACE_EXISTING);
        if(FILE* f=_wfopen(history.c_str(),L"ab")) {
            std::string line=utc_iso()+" appid="+std::to_string(appid)+" attempt="+g_launch.attempt+" state="+status+" ok="+(ok?"1":"0")+
                " gameCodeExecuted="+(executed?"1":"0")+" mapped="+std::to_string(mapped)+"/"+std::to_string(total)+" result="+result;
            for(auto& ch:line) if(ch=='\r'||ch=='\n') ch=' ';
            line+="\n";fwrite(line.data(),1,line.size(),f);fclose(f);
        }
    }
    g_launch.phase=terminal?2:3;
}
static void lp_finish(int gen,uint32_t appid,const std::string& result,bool ok,int mapped,int total,const char* status=nullptr) {
    auto runtime=play_runtime::observed.load(std::memory_order_acquire);
    const bool guestExited=(runtime&&runtime->finished.load(std::memory_order_acquire))||g_launch.pe32Finished.load(std::memory_order_acquire);
    if(!status&&guestExited&&g_launch.quitRequested.load(std::memory_order_acquire)) {
        lp_publish(gen,appid,"guest exited after quit request: "+result,ok,mapped,total,"quit",true);
        return;
    }
    lp_publish(gen,appid,result,ok,mapped,total,status?status:ok?"completed":"failed",true);
}
// 120 s from the guest loader's start to a first frame or result. Preparing
// Steam before that (ownership ticket, a first download of Valve's client,
// cloud saves) may take longer, so it gets 600 s; past that the launch is
// abandoned and the launch thread never starts the guest (lp_run_impl).
static void lp_watchdog() {
    if(g_launch.phase!=1)return;
    const double loader=g_launch.loaderStarted.load();
    if(loader ? now_s()-loader<=120 : now_s()-g_launch.started<=600)return;
    auto r=play_runtime::observed.load(std::memory_order_acquire);
    if(r && (r->finished.load(std::memory_order_acquire) || r->milestone.load()))return;
    // The 32-bit interpreter's milestone is its own: the guest is running, and
    // a game that runs for minutes must not be reported as a timeout.
    if(!r && g_launch.pe32Executed.load())return;
    lp_finish(g_launch.gen,g_launch.appid,loader?"watchdog timeout 120 s; last event: "+(r?r->snapshot():std::string("preparing guest loader")):
        std::string("watchdog timeout 600 s preparing Steam before the game started; the game was not started"),false,0,0,"timeout");
}

// Item D22: B on the launch banner. A launch is cancelled only while it is
// still preparing (phase 1) and no game code has started: the guard flags are
// checked under the same lock lp_publish uses, so a cancel can never race a
// publish that already reported the guest running. The worker notices the
// abandoned generation at its next checkpoint (before the guest starts, in
// the watchdog, and in lp_publish) and stops without starting it; the launch
// is reported as cancelled so the page and launch_result.json agree.
static bool cancel_launch(const char* via) {
    std::lock_guard<std::mutex> lock(g_launch.m);
    if(g_launch.phase!=1) return false;
    if(g_launch.loaderStarted.load()!=0 || g_launch.pe32Executed.load()) return false;   // game code already started
    if(play_runtime::observed.load(std::memory_order_acquire)) return false;
    g_launch.result="Launch cancelled";g_launch.ok=false;g_launch.phase=2;
    logf("launch: cancelled by the player before the game started (%s) attempt=%s",via,g_launch.attempt.c_str());
    // lp_publish refuses a stale generation, so the terminal record is written
    // here, in the same shape the worker writes it.
    std::string json="{\"appid\":"+std::to_string(g_launch.appid.load())+",\"attemptId\":\""+g_launch.attempt+"\",\"time\":\""+utc_iso()+"\",\"stage\":\"loader\",\"state\":\"cancelled\",\"terminal\":true,\"ok\":false,\"mapped\":0,\"total\":0,\"gameCodeExecuted\":false,\"exitCode\":null,\"lastEvent\":\"launch cancelled\",\"result\":\"Launch cancelled\"}\n";
    write_text(g_dir+L"\\launch_result.json",json);
    return true;
}

// Entry: called on the render thread; returns immediately (work happens on a worker thread).
// option (the picker's full appinfo identity) starts that launch option instead
// of the preferred one; nullptr picks the preferred option as before.
// Returns why Play was refused, or empty when the launch started: the detail
// page shows that reason, since a refusal leaves no launch panel to show it.
static std::string start_launch(uint32_t appid, const std::string& name, const std::string& dir, const char* via, const std::string* option=nullptr) {
    if(g_selftest_busy.load(std::memory_order_acquire)) {
        logf("launch: Play rejected: the console self-test is running");
        return "The launcher self-test is running. Wait for it to finish, or restart the launcher.";
    }
    if(auto guest=play_runtime::observed.load(std::memory_order_acquire)) {
        std::string why="Game code from the last launch is still loaded; the launcher restarts to end it. Last event: "+guest->snapshot();
        { std::lock_guard<std::mutex> lock(g_launch.m); g_launch.result=why; }
        logf("launch: Play rejected: game code from the last launch is still loaded");
        return why;
    }
    int gen = ++g_launch.gen; g_launch.appid = appid; g_launch.started = now_s(); g_launch.loaderStarted = 0;
    // The 32-bit interpreter's report belongs to the launch that produced it:
    // clear it here, not only in lp_run_pe32, or a later 64-bit launch would
    // publish a previous PE32 run as this one's progress.
    g_launch.pe32Executed = false; g_launch.pe32Finished = false; g_launch.pe32ExitCode = 0;
    g_launch.quitRequested.store(false,std::memory_order_release);
    { std::lock_guard<std::mutex> l(g_launch.m); g_launch.result.clear();g_launch.ok=false;g_launch.attempt=std::to_string(GetCurrentProcessId())+":"+std::to_string(gen); } g_launch.phase = 1;
    const InstallInfo inst = read_install(appid);   // the game folder may be on another drive (marker root)
    const std::wstring folder = !dir.empty() && inst.installed && inst.dir == dir ? inst.folder : g_dir + L"\\" + widen(dir);
    const LaunchTarget target = dir.empty() ? LaunchTarget{} : pick_target(appid, folder, option);
    const std::string args = target.args;
    // The chosen option's executable, arguments and working directory, so the
    // request says exactly what Play starts (a picker choice included).
    char b[2048]; snprintf(b, sizeof b, "{\"appid\":%u,\"attemptId\":\"%s\",\"name\":\"%s\",\"installDir\":\"%s\",\"time\":\"%s\",\"exe\":\"%s\",\"args\":\"%s\",\"workingDir\":\"%s\",\"via\":\"%s\"}\n", appid, g_launch.attempt.c_str(), json_esc(name).c_str(), json_esc(dir).c_str(), utc_iso().c_str(), json_esc(play_runtime::narrow(target.exe)).c_str(), args.c_str(), json_esc(target.workdir).c_str(), via);
    write_text(g_dir + L"\\launch_request.json", b);
    logf("launch: request written LocalState\\launch_request.json appid=%u attempt=%s dir=%s via=%s", appid, g_launch.attempt.c_str(), dir.c_str(), via);
    logf("launch: target exe=\"%s\" args=\"%s\" workingDir=\"%s\" (%s)", play_runtime::narrow(target.exe).c_str(), target.args.c_str(), target.workdir.c_str(), target.why.c_str());
    std::thread([=] {
        init_apartment(apartment_type::multi_threaded);
        auto finish = [&](const std::string& r,bool ok,int mapped,int total) {lp_finish(gen,appid,r,ok,mapped,total);};
        try {
            // 1. app activation of a game-host package: only if one registers a protocol (none does today)
            try { winrt::Windows::Foundation::Uri uri(L"xboxsteam-host://launch");
                  auto q = winrt::Windows::System::Launcher::QueryUriSupportAsync(uri, winrt::Windows::System::LaunchQuerySupportType::Uri).get();
                  logf("launch: activation query xboxsteam-host:// -> %d (0=Available,1=AppNotInstalled,2=AppUnavailable,3=NotSupported,4=Unknown); %s", (int)q, (int)q == 0 ? "would activate host" : "no host package registers it, using in-process guest runtime");
                  if ((int)q == 0) { std::wstring u = L"xboxsteam-host://launch?appid=" + widen(std::to_string(appid)) + L"&dir=" + widen(dir); bool ok = winrt::Windows::System::Launcher::LaunchUriAsync(winrt::Windows::Foundation::Uri(u)).get(); logf("launch: host activation LaunchUriAsync ok=%d", ok ? 1 : 0); if (ok) { lp_finish(gen,appid,"handed over to host package via xboxsteam-host://; guest execution unverified",false,0,0,"handed-off"); return; } }
            } catch (winrt::hresult_error const& e) { logf("launch: activation query failed hr=0x%08x", (unsigned)e.code().value); } catch (...) { logf("launch: activation query failed"); }
            // 2. in-process guest runtime
            std::wstring root = folder; std::string why = target.why; std::vector<std::wstring> names; if (!target.exe.empty()) names.push_back(target.exe);
            logf("launch: runtime root=%s targets=%zu (%s)", game_library::join_root("LocalState", inst.root, dir).c_str(), names.size(), why.c_str());
            // A launch that cannot find its files says which of the two it is:
            // the marker names a drive that is gone, or the drive is there but
            // the payload folder is missing/empty (see installs::payload_state).
            if (dir.empty() || names.empty()) {
                std::string gone;
                if (!dir.empty() && names.empty()) {
                    if (!inst.root.empty() && GetFileAttributesW(installs::ascii(inst.root.substr(0, 3)).c_str()) == INVALID_FILE_ATTRIBUTES)
                        gone = "the drive holding this game (" + inst.root.substr(0, 2) + ") is not connected";
                    else if (installs::payload_state(folder) != installs::Health::ok)
                        gone = "the install files are missing from " + game_library::join_root("LocalState", inst.root, dir) + " (reinstall from the game page)";
                    else gone = "no runnable executable found";
                } else gone = dir.empty() ? std::string("install directory unknown") : why;
                finish("cannot start: " + gone, false, 0, 0); return;
            }
            // Keep the previous attempt's log beside this one (launch_probe.previous.log).
            MoveFileExW((g_dir + L"\\launch_probe.log").c_str(), (g_dir + L"\\launch_probe.previous.log").c_str(), MOVEFILE_REPLACE_EXISTING);
            { std::lock_guard<std::mutex> l(g_lpm); if (g_lpf) fclose(g_lpf); g_lpf = _wfopen((g_dir + L"\\launch_probe.log").c_str(), L"wb"); g_lpMissing = g_lpStubbed = g_lpStubCalls = g_lpLines = 0; g_lpLastFail.clear(); }
            LpCtx c; c.root = root; c.installRoot = inst.root.empty() ? std::wstring() : installs::ascii(inst.root); c.args = widen(args); c.cwd = target.workdir.empty() ? std::wstring() : root + L"\\" + widen(target.workdir); c.names = names;c.gen=gen;c.appid=appid; bool noFault = lp_run_seh(&c);
            { std::lock_guard<std::mutex> l(g_lpm); if (g_lpf) { fclose(g_lpf); g_lpf = nullptr; } }
            int total=play_runtime::state?int(play_runtime::state->moduleCount):int(names.size());
            const std::string fault=noFault?(c.fault.empty()?c.failName:c.fault):
                "loader SEH exception; last event: "+(play_runtime::state?play_runtime::state->snapshot():std::string("loader not entered"));
            // Before publishing: the UI offers Play again only once nothing is retained.
            if(play_runtime::discard_unstarted()) logf("launch: no game code ran; loader released, another game can be played");
            lp_finish(gen,appid,fault,noFault&&c.ok,c.mapped,total,noFault&&c.timedOut?"timeout":nullptr);
        } catch (std::exception const& e) { finish(std::string("loader threw: ") + e.what(), false, 0, 0); }
        catch (...) { finish("loader threw an unknown exception.", false, 0, 0); }
    }).detach();
    return std::string();
}
