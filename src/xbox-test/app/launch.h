// Launcher Play: write the request, map the guest dependency graph and execute
// TLS/DllMain/EXE entry through play_runtime.h. Missing static imports stop with
// their exact names instead of returning probe placeholders. The common result
// publisher records return, exception, loader failure or watchdog timeout in
// boot.log and launch_result.json; detailed events remain in launch_probe.log.
#pragma once
#include <memory>
#include <winrt/Windows.System.h>
#include "host/wxl_mapper.h"
#include "host/mlp_probe.h"
#include "../uwph/user32/probe.h"
#include "launch_status.h"
#include "launch_target.h"
#include "installs.h"

struct LaunchState {
    std::atomic<int> phase{0};          // 0 never, 1 starting, 2 finished, 3 guest running
    std::atomic<uint32_t> appid{0};
    std::atomic<int> gen{0};
    double started = 0;                 // render-thread read only (set before phase=1)
    std::mutex m; std::string result, attempt; bool ok=false;
};
static LaunchState g_launch;

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
    bool key = strstr(s, "PRUN EVENT") || strstr(s, "GAP") || strstr(s, "FAIL") || strstr(s, "SUMMARY") || strstr(s, "MLP IMAGE") || strstr(s, "mapped") || strstr(s, "PROBE") || strstr(s, "STUB-CALL");
    if (key) logf("launch-probe: %.300s", s);
}
#include "wine_gap.h"
#include "host/wxl_pe32.h"
#include "pe32_probe.h"
#include "donors/probe.h"

#include "host/play_runtime.h"
#include "steam_client_package.h"

static void lp_publish(int gen,uint32_t appid,const std::string& result,bool ok,int mapped,int total,const char* status,bool terminal);
struct LpCtx { std::wstring root, args; std::vector<std::wstring> names; int gen = 0; uint32_t appid = 0; int mapped = 0; unsigned resolved = 0, stubbed = 0; std::string firstMissing, failName, fault; bool ok = false, timedOut = false; };
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
// Ownership gate (AGENTS.md rule 3): before any emulated Steam component is
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
// Valve's signed steamclient64.dll in LocalState\steam\client, fetched from
// Valve (steam_client_package.h) once per Steam client update. With Valve's
// manifest unreachable, a cached copy that still carries Valve's signature
// block is used.
static std::wstring valve_client() {
    namespace pkg=steam_client_package;
    const std::wstring dir=g_dir+L"\\steam\\client", dll=dir+L"\\steamclient64.dll", stamp=dir+L"\\package.sha256";
    for(auto d:{g_dir+L"\\steam",dir})CreateDirectoryW(d.c_str(),nullptr);
    auto cached=[&]{return pkg::valve_signature_block(read_bytes(dll));};
    std::string text; pkg::Entry e;
    if(cloud_http(false,true,pkg::host,pkg::manifest_path,{},{},text)!=200||!pkg::manifest_entry(text,e)) {
        if(cached()){lp_sink("STEAM client: Valve's client manifest unreachable; using the cached signed client");return dll;}
        lp_sink("STEAM client: Valve's client manifest unreachable and nothing cached");return {};
    }
    if(read_bytes(stamp)==e.sha256&&cached()) return dll;
    lp_sink("STEAM client: fetching Valve's signed client from Valve (once per Steam client update)");
    std::string zip;
    if(cloud_http(false,true,pkg::host,std::string("/client/")+e.file,{},{},zip)!=200){lp_sink("STEAM client: package download failed");return {};}
    if(sha256_hex(zip)!=e.sha256){lp_sink("STEAM client: package SHA-256 differs from Valve's manifest; discarded");return {};}
    auto inflater=[](const char* data,size_t size,size_t expected,std::string& out){
        out.resize(expected); z_stream z{}; if(inflateInit2(&z,-15)!=Z_OK) return false;
        z.next_in=(Bytef*)data; z.avail_in=(uInt)size; z.next_out=(Bytef*)&out[0]; z.avail_out=(uInt)expected;
        int rc=inflate(&z,Z_FINISH); bool ok=rc==Z_STREAM_END&&z.total_out==expected; inflateEnd(&z); return ok;};
    std::string client;
    if(!pkg::zip_member(zip,pkg::member,inflater,client)||!pkg::valve_signature_block(client)){lp_sink("STEAM client: package has no signed steamclient64.dll; discarded");return {};}
    const std::wstring part=dll+L".part";
    write_text(part,client);
    if(read_bytes(part)!=client||!MoveFileExW(part.c_str(),dll.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(part.c_str());lp_sink("STEAM client: could not store the signed client");return {};}
    write_text(stamp,e.sha256);
    lp_sink("STEAM client: Valve's signed client stored (SHA-256 matched Valve's manifest)");
    return dll;
}
// Emulated Steam for the signed-in account's game (AGENTS.md rule 3), only
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
// The same known folders the game resolves in this process and container.
static steam_cloud::Roots cloud_roots(const std::wstring& gameRoot) {
    static const GUID localLow={0xA520A1A4,0x1780,0x4FF6,{0xBD,0x18,0x16,0x73,0x43,0xC5,0xAF,0x16}};
    static const GUID local={0xF1B32785,0x6FBA,0x4FCF,{0x9D,0x55,0x7B,0x8E,0x7F,0x15,0x70,0x91}};
    static const GUID roaming={0x3EB685DB,0x65F9,0x4CF6,{0xA0,0x3A,0xE3,0xEF,0x65,0x72,0x9F,0x3D}};
    static const GUID documents={0xFDD39AD0,0x238F,0x46AF,{0xAD,0xB4,0x6C,0x85,0x48,0x03,0x69,0xC7}};
    static const GUID savedGames={0x4C5C32FF,0xBB9D,0x43B0,{0xB5,0xB4,0x2D,0x72,0xE5,0x4E,0xAA,0xA4}};
    using Known=HRESULT(WINAPI*)(const GUID&,DWORD,HANDLE,PWSTR*);
    static HMODULE shell=LoadLibraryExW(L"shell32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto known=shell?(Known)GetProcAddress(shell,"SHGetKnownFolderPath"):nullptr;
    steam_cloud::Roots roots;
    auto add=[&](const char* name,const GUID& id){PWSTR path=nullptr;if(known&&SUCCEEDED(known(id,0,nullptr,&path))&&path)roots[name]=path;if(path)CoTaskMemFree(path);};
    add("WinAppDataLocalLow",localLow);add("WinAppDataLocal",local);add("WinAppDataRoaming",roaming);
    add("WinMyDocuments",documents);add("WinSavedGames",savedGames);
    roots["GameInstall"]=gameRoot;
    return roots;
}
static std::mutex g_cloudRulesM;
static std::vector<steam_cloud::Rule> g_cloudRules;   // from the pre-launch download pass
static bool cloud_sync(uint32_t appid,const std::wstring& gameRoot,bool upload,uint64_t quiet) {
    auto session=user_session();
    if(!session||session->anon)return false;
    std::string text=pics_appinfo(*session,appid);
    while(!text.empty()&&text.back()==0)text.pop_back();
    Vdf root;if(text.empty()||!vdf_parse(text,root)){lp_sink("STEAM cloud: no app info; not synced");return false;}
    const Vdf* app=root.get("appinfo");if(!app)app=&root;
    auto rules=steam_cloud::rules(*app);
    if(rules.empty()){lp_sink("STEAM cloud: app has no Auto-Cloud save rules");return false;}
    {std::lock_guard<std::mutex> lock(g_cloudRulesM);g_cloudRules=rules;}
    steam_cloud::Io io;
    io.service=[session](const std::string& method,const std::string& request,int& eresult,std::string& response){
        JobMsg m;if(!session->service(method,request,30000,m))return false;
        eresult=m.eresult;response=m.body;return true;};
    io.get=[](bool https,const std::string& host,const std::string& path,const steam_cloud::Headers& h,std::string& body){return cloud_http(false,https,host,path,h,{},body);};
    io.put=[](bool https,const std::string& host,const std::string& path,const steam_cloud::Headers& h,const std::string& body){std::string ignored;return cloud_http(true,https,host,path,h,body,ignored);};
    io.sha1=[](const std::string& data){return sha1_of(data.data(),data.size());};
    io.log=[](const std::string& line){lp_sink(("STEAM "+line).c_str());};
    auto roots=cloud_roots(gameRoot);
    auto r=upload?steam_cloud::upload(io,appid,roots,rules,"Xbox",quiet):steam_cloud::download(io,appid,roots);
    char line[200];snprintf(line,sizeof line,"STEAM cloud %s: downloaded=%u uploaded=%u unchanged=%u kept=%u busy=%u failed=%u",
        upload?"upload":"download",r.downloaded,r.uploaded,r.unchanged,r.kept,r.busy,r.failed);lp_sink(line);
    return r.failed==0;
}
// ---- Steam achievements/stats for the provider-served game (steam_achievements.h)
#include "steam_achievements.h"
static std::wstring provider_dir(uint32_t appid) { return g_dir+L"\\steam\\"+widen(std::to_string(appid)); }
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
    if(appid){std::string status;pb_u(status,1,1);session->send_raw(716,status,0,"");}   // ClientChangeStatus: k_EPersonaStateOnline
    std::string body;
    if(appid){std::string game;pb_fixed64(game,2,appid);pb_s(body,1,game);}
    pb_u(body,2,20);   // client_os_type, as in ClientLogon (Windows 11)
    session->send_raw(5410,body,0,""); // ClientGamesPlayedWithDataBlob
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
    try{games_played(0);}catch(...){}
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
static void lp_run_impl(LpCtx& c) {
    wine_gap::report(c.root);
    if(c.names.empty()) {c.failName="no executable";return;}
    // A Steam launch option may name an executable in a subdirectory: the guest
    // runs from that directory (DLL search and current directory), as on
    // Windows; Steam Cloud keeps the install root.
    auto rel=c.names.back();auto slash=rel.find_last_of(L'\\');
    std::wstring exe=slash==std::wstring::npos?rel:rel.substr(slash+1);
    std::wstring exeDir=slash==std::wstring::npos?c.root:c.root+L"\\"+rel.substr(0,slash);
    if(pe_machine(exeDir+L"\\"+exe)==IMAGE_FILE_MACHINE_I386) {
        std::string summary;
        bool inspected=pe32_probe::map(exeDir+L"\\"+exe,exeDir,summary);
        c.mapped=inspected?1:0;
        c.failName=inspected?summary+"; 32-bit execution unsupported":"PE32 data-only inspection failed; execution unsupported";
        lp_sink("PRUN FAIL 32-bit execution unsupported; PE32 inspection is data-only");return;
    }
    // These are behavior/ABI prerequisites, not optional progress diagnostics.
    // Never expose donor exports to executable guests after a failed probe.
    if(!donor_pe::probe(lp_sink)) {c.failName="Wine donor initialization/behavior";lp_sink("DWPE FAIL donor behavior gate; no game code executed");return;}
    mlp::donor_resolve=donor_pe::exported;
    if(!user32_core::test(lp_sink)) {c.failName="u32c-test.exe";lp_sink("U32C FAIL regression PE; no game code executed");return;}
    const bool steamProvider=stage_steam_provider(c.appid,exeDir+L"\\"+exe);
    if(steamProvider) {
        try{cloud_sync(c.appid,c.root,false,0);}catch(...){lp_sink("STEAM cloud download: exception");}
        try{achievements_sync(c.appid,false);}catch(...){lp_sink("STEAM achievements: exception");}
        games_played(c.appid);
    }
    if(steamProvider){uint32_t appid=c.appid;std::wstring root=c.root;g_finalSync=[appid,root]{final_sync(appid,root);};}
    regb::init(g_dir,lp_sink); // guest registry (PlayerPrefs): LocalState\registry.sqlite
    if(!g_signedClient.empty()&&!play_runtime::seed_steam_client_registry(g_signedClient,g_clientAccount))
        lp_sink("STEAM client: guest registry not seeded; the game's Steam client lookup will fail");
    {   // Wine's C++ runtime packaged beside the launcher (build.sh: layout\runtime).
        wchar_t self[2048];DWORD n=GetModuleFileNameW(nullptr,self,2048);
        if(n&&n<2048){std::wstring package(self,n);package.resize(package.find_last_of(L"\\/")+1);play_runtime::set_runtime_directory(package+L"runtime");}
    }
    if(!play_runtime::start(exeDir,exe,c.args,lp_sink)) {c.failName="guest already started; restart launcher before next Play";return;}
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
    if(steamProvider)final_sync(c.appid,c.root);
    c.mapped=r->mappedCount;
    for(auto& row:mlp::summaries) {
        c.resolved+=row.second.resolved;c.stubbed+=row.second.stubbed;
        char line[512];snprintf(line,sizeof line,"MLP SUMMARY dll=%s resolved=%u stubbed=%u first-missing=%s",row.first.c_str(),row.second.resolved,row.second.stubbed,row.second.first.c_str());lp_sink(line);
    }
    mlp::report_kernel();
}
static bool lp_run_seh(LpCtx* c) {     // no C++ objects with destructors in this frame: SEH guard so a probe bug cannot take the launcher down
    __try { lp_run_impl(*c); return true; }
    __except (play_runtime::state?play_runtime::filter(GetExceptionInformation()):EXCEPTION_EXECUTE_HANDLER) { return false; }
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
struct LaunchTarget { std::wstring exe; std::string args, why; };   // exe is install-relative
// The game's own Steam launch option when known, as the Steam client picks it;
// otherwise the largest top-level .exe. Engine options come from the game's
// files (Unity: UnityPlayer.dll beside the executable), never from its app ID.
static LaunchTarget pick_target(uint32_t appid, const std::wstring& root) {
    LaunchTarget t; Vdf info; const Vdf* app=nullptr; const launch_target::Entry* chosen=nullptr;
    std::vector<launch_target::Entry> options;
    if(load_appinfo(appid,info,app)) {
        options=launch_target::entries(*app);
        std::string rel;
        if(auto e=launch_target::choose(options)) {
            if(!launch_target::relative_path(e->executable,rel)) t.why="Steam launch option \""+e->executable+"\" is not an install-relative path; ";
            else if(GetFileAttributesW((root+L"\\"+widen(rel)).c_str())==INVALID_FILE_ATTRIBUTES) t.why="Steam launch option \""+rel+"\" is not installed; ";
            else {t.exe=widen(rel);chosen=e;t.why="Steam launch option \""+rel+"\"";}
        } else t.why="no Windows Steam launch option; ";
    } else t.why="no cached app info; ";
    if(t.exe.empty()) {
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((root + L"\\*.exe").c_str(), &fd); std::wstring best; uint64_t bs = 0;
        if (h != INVALID_HANDLE_VALUE) { do { std::wstring n = fd.cFileName; std::wstring l = n; for (auto& ch : l) ch = (wchar_t)towlower(ch);
                if (l.find(L"unins") != std::wstring::npos || l.find(L"crash") != std::wstring::npos || l.find(L"redist") != std::wstring::npos || l.find(L"setup") != std::wstring::npos) continue;
                uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow; if (sz > bs) { bs = sz; best = n; } } while (FindNextFileW(h, &fd)); FindClose(h); }
        if (best.empty()) { t.why += "no launchable .exe in install dir"; return t; }
        t.exe=best;t.why+="largest top-level .exe";
    }
    auto slash=t.exe.find_last_of(L'\\');
    std::wstring dir=slash==std::wstring::npos?root:root+L"\\"+t.exe.substr(0,slash);
    bool unity=GetFileAttributesW((dir+L"\\UnityPlayer.dll").c_str())!=INVALID_FILE_ATTRIBUTES;
    t.args=launch_target::arguments(chosen,unity);
    if(unity)t.why+="; Unity player";
    return t;
}

// Serialize progress, completion and watchdog publication. A terminal result
// cannot be replaced by a late milestone or an obsolete loader generation.
static void lp_publish(int gen,uint32_t appid,const std::string& result,bool ok,int mapped,int total,const char* status,bool terminal) {
    std::lock_guard<std::mutex> lock(g_launch.m);
    if(g_launch.gen!=gen || (g_launch.phase!=1 && g_launch.phase!=3))return;
    auto runtime=play_runtime::observed.load(std::memory_order_acquire);
    bool executed=runtime && runtime->executed;
    bool finished=runtime && runtime->finished.load(std::memory_order_acquire);
    std::string last=runtime?runtime->snapshot():result;
    std::string json="{\"appid\":"+std::to_string(appid)+",\"attemptId\":\""+g_launch.attempt+"\",\"time\":\""+utc_iso()+"\",\"stage\":\""+(executed?"guest-execution":"loader")+"\",\"state\":\""+status+"\",\"terminal\":"+(terminal?"true":"false")+",\"ok\":"+(ok?"true":"false")+",\"mapped\":"+std::to_string(mapped)+",\"total\":"+std::to_string(total)+",\"gameCodeExecuted\":"+(executed?"true":"false")+",\"exitCode\":"+(finished&&runtime->success?std::to_string(runtime->code):"null")+",\"lastEvent\":\""+json_esc(last)+"\",\"result\":\""+json_esc(result)+"\"}\n";
    write_text(g_dir+L"\\launch_result.json",json);
    g_launch.result=result;g_launch.ok=ok;
    logf("launch: %s attempt=%s ok=%d mapped=%d/%d gameCodeExecuted=%s: %s",terminal?"RESULT":"RUNNING",g_launch.attempt.c_str(),ok?1:0,mapped,total,executed?"true":"false",result.c_str());
    g_launch.phase=terminal?2:3;
}
static void lp_finish(int gen,uint32_t appid,const std::string& result,bool ok,int mapped,int total,const char* status=nullptr) {
    lp_publish(gen,appid,result,ok,mapped,total,status?status:ok?"completed":"failed",true);
}
static void lp_watchdog() {
    if(g_launch.phase!=1 || now_s()-g_launch.started<=120)return;
    auto r=play_runtime::observed.load(std::memory_order_acquire);
    if(r && (r->finished.load(std::memory_order_acquire) || r->milestone.load()))return;
    lp_finish(g_launch.gen,g_launch.appid,"watchdog timeout 120 s; last event: "+(r?r->snapshot():std::string("preparing guest loader")),false,0,0,"timeout");
}

// Entry: called on the render thread; returns immediately (work happens on a worker thread).
static void start_launch(uint32_t appid, const std::string& name, const std::string& dir, const char* via) {
    if(play_runtime::observed.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(g_launch.m);
        g_launch.result="Guest retained. Restart launcher before another Play. Last event: "+play_runtime::state->snapshot();
        logf("launch: Play rejected: guest retained; restart launcher before another Play");return;
    }
    int gen = ++g_launch.gen; g_launch.appid = appid; g_launch.started = now_s();
    { std::lock_guard<std::mutex> l(g_launch.m); g_launch.result.clear();g_launch.ok=false;g_launch.attempt=std::to_string(GetCurrentProcessId())+":"+std::to_string(gen); } g_launch.phase = 1;
    const LaunchTarget target = dir.empty() ? LaunchTarget{} : pick_target(appid, g_dir + L"\\" + widen(dir));
    const std::string args = target.args;
    char b[1500]; snprintf(b, sizeof b, "{\"appid\":%u,\"attemptId\":\"%s\",\"name\":\"%s\",\"installDir\":\"%s\",\"time\":\"%s\",\"args\":\"%s\",\"via\":\"%s\"}\n", appid, g_launch.attempt.c_str(), json_esc(name).c_str(), json_esc(dir).c_str(), utc_iso().c_str(), args.c_str(), via);
    write_text(g_dir + L"\\launch_request.json", b);
    logf("launch: request written LocalState\\launch_request.json appid=%u attempt=%s dir=%s via=%s", appid, g_launch.attempt.c_str(), dir.c_str(), via);
    std::thread([=] {
        init_apartment(apartment_type::multi_threaded);
        auto finish = [&](const std::string& r,bool ok,int mapped,int total) {lp_finish(gen,appid,r,ok,mapped,total);};
        try {
            // 1. app activation of a game-host package: only if one registers a protocol (none does today; see LANE_NOTES)
            try { winrt::Windows::Foundation::Uri uri(L"xboxsteam-host://launch");
                  auto q = winrt::Windows::System::Launcher::QueryUriSupportAsync(uri, winrt::Windows::System::LaunchQuerySupportType::Uri).get();
                  logf("launch: activation query xboxsteam-host:// -> %d (0=Available,1=AppNotInstalled,2=AppUnavailable,3=NotSupported,4=Unknown); %s", (int)q, (int)q == 0 ? "would activate host" : "no host package registers it, using in-process guest runtime");
                  if ((int)q == 0) { std::wstring u = L"xboxsteam-host://launch?appid=" + widen(std::to_string(appid)) + L"&dir=" + widen(dir); bool ok = winrt::Windows::System::Launcher::LaunchUriAsync(winrt::Windows::Foundation::Uri(u)).get(); logf("launch: host activation LaunchUriAsync ok=%d", ok ? 1 : 0); if (ok) { lp_finish(gen,appid,"handed over to host package via xboxsteam-host://; guest execution unverified",false,0,0,"handed-off"); return; } }
            } catch (winrt::hresult_error const& e) { logf("launch: activation query failed hr=0x%08x", (unsigned)e.code().value); } catch (...) { logf("launch: activation query failed"); }
            // 2. in-process guest runtime
            std::wstring root = g_dir + L"\\" + widen(dir); std::string why = target.why; std::vector<std::wstring> names; if (!target.exe.empty()) names.push_back(target.exe);
            logf("launch: runtime root=LocalState\\%s targets=%zu (%s)", dir.c_str(), names.size(), why.c_str());
            if (dir.empty() || names.empty()) { finish("cannot start: " + (dir.empty() ? std::string("install directory unknown") : why), false, 0, 0); return; }
            { std::lock_guard<std::mutex> l(g_lpm); if (g_lpf) fclose(g_lpf); g_lpf = _wfopen((g_dir + L"\\launch_probe.log").c_str(), L"wb"); g_lpMissing = g_lpStubbed = g_lpStubCalls = g_lpLines = 0; g_lpLastFail.clear(); }
            LpCtx c; c.root = root; c.args = widen(args); c.names = names;c.gen=gen;c.appid=appid; bool noFault = lp_run_seh(&c);
            { std::lock_guard<std::mutex> l(g_lpm); if (g_lpf) { fclose(g_lpf); g_lpf = nullptr; } }
            int total=play_runtime::state?int(play_runtime::state->moduleCount):int(names.size());
            if(!noFault) {finish("loader SEH exception; last event: "+(play_runtime::state?play_runtime::state->snapshot():std::string("loader not entered")),false,c.mapped,total);return;}
            lp_finish(gen,appid,c.fault.empty()?c.failName:c.fault,c.ok,c.mapped,total,c.timedOut?"timeout":nullptr);
        } catch (std::exception const& e) { finish(std::string("loader threw: ") + e.what(), false, 0, 0); }
        catch (...) { finish("loader threw an unknown exception.", false, 0, 0); }
    }).detach();
}
