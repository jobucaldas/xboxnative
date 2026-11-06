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

struct LaunchState {
    std::atomic<int> phase{0};          // 0 never, 1 starting, 2 finished, 3 guest running
    std::atomic<uint32_t> appid{0};
    std::atomic<int> gen{0};
    double started = 0;                 // render-thread read only (set before phase=1)
    std::mutex m; std::string result, attempt; bool ok=false;
};
static LaunchState g_launch;

// ---- install state helpers (marker dl_<appid>.done written by the downloader)
struct InstallInfo { bool installed = false; uint64_t bytes = 0; std::string dir; };
static InstallInfo read_install(uint32_t appid) {
    InstallInfo r; std::wstring mk = g_dir + L"\\dl_" + widen(std::to_string(appid)) + L".done";
    FILE* f = _wfopen(mk.c_str(), L"rb"); if (!f) return r;
    char b[512] = {}; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); std::string s(b, n);
    r.installed = true;
    size_t p = s.find(" bytes "); if (p != std::string::npos) r.bytes = strtoull(s.c_str() + p + 7, nullptr, 10);
    p = s.find(" installdir "); if (p != std::string::npos) { r.dir = s.substr(p + 12); while (!r.dir.empty() && (r.dir.back() == '\n' || r.dir.back() == '\r' || r.dir.back() == ' ')) r.dir.pop_back(); }
    if (r.dir.empty()) r.dir = appid == 480 ? "spacewar" : appid == 3405340 ? "megabonk" : appid == 1007 ? "steamworks_sdk_redist" : "";   // markers written before 0.5.0.0 carry no installdir
    return r;
}
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

static void lp_publish(int gen,uint32_t appid,const std::string& result,bool ok,int mapped,int total,const char* status,bool terminal);
struct LpCtx { std::wstring root, args; std::vector<std::wstring> names; int gen = 0; uint32_t appid = 0; int mapped = 0; unsigned resolved = 0, stubbed = 0; std::string firstMissing, failName, fault; bool ok = false, timedOut = false; };
static int pe_machine(const std::wstring& p) {   // 0 = unreadable/not PE, else IMAGE_FILE_MACHINE_*
    FILE* f = _wfopen(p.c_str(), L"rb"); if (!f) return 0; unsigned char h[1024]; size_t n = fread(h, 1, sizeof h, f); fclose(f);
    if (n < 0x40 || h[0] != 'M' || h[1] != 'Z') return 0; uint32_t o = *(uint32_t*)(h + 0x3c); if (o + 6 > n || memcmp(h + o, "PE\0\0", 4)) return 0; return *(uint16_t*)(h + o + 4);
}
// Steam API provider for the signed-in account's game (AGENTS.md rule 3): the
// packaged, pinned gbe_fork steam_api64.dll is copied to LocalState\steam\<appid>
// with steam_settings for this account, and Play maps it in place of the game's
// steam_api64.dll (the game's own file is untouched). The SteamID is written only
// to that on-device settings file, never to a log. Without a packaged provider or
// a signed-in account nothing is staged and Steam calls fail naturally.
// TODO(ownership gate): verify the account owns appid before staging (parked).
static bool stage_steam_provider(uint32_t appid) {
    wchar_t exe[2048];DWORD n=GetModuleFileNameW(nullptr,exe,2048);
    if(!n||n>=2048)return false;
    std::wstring package(exe,n);package.resize(package.find_last_of(L"\\/")+1);
    const std::wstring source=package+L"steam-provider\\gbe\\steam_api64.dll";
    if(GetFileAttributesW(source.c_str())==INVALID_FILE_ATTRIBUTES){lp_sink("STEAM provider: none packaged; game Steam calls fail naturally");return false;}
    auto session=user_session();
    if(!session||session->anon||!session->steamid){lp_sink("STEAM provider: no signed-in account; not staged");return false;}
    const std::wstring dir=g_dir+L"\\steam\\"+widen(std::to_string(appid));
    for(auto d:{g_dir+L"\\steam",dir,dir+L"\\steam_settings",dir+L"\\saves"})CreateDirectoryW(d.c_str(),nullptr);
    const std::wstring target=dir+L"\\steam_api64.dll";
    COPYFILE2_EXTENDED_PARAMETERS copy{};copy.dwSize=sizeof copy;
    HRESULT hr=CopyFile2(source.c_str(),target.c_str(),&copy);
    if(FAILED(hr)){char line[96];snprintf(line,sizeof line,"STEAM provider: copy failed hr=0x%08lx; not staged",(unsigned long)hr);lp_sink(line);return false;}
    write_text(dir+L"\\steam_settings\\steam_appid.txt",std::to_string(appid)+"\n");
    write_text(dir+L"\\steam_settings\\configs.user.ini","[user::general]\naccount_name=Player\naccount_steamid="+
        std::to_string(session->steamid)+"\nlanguage=english\n[user::saves]\nlocal_save_path=./saves\n");
    // No LAN/peer networking yet: achievements and cloud saves sync through the launcher.
    write_text(dir+L"\\steam_settings\\configs.main.ini","[main::connectivity]\ndisable_networking=1\n");
    play_runtime::set_steam_provider(target);
    char line[160];snprintf(line,sizeof line,"STEAM provider: gbe_fork staged in LocalState\\steam\\%u for the signed-in account",appid);lp_sink(line);
    return true;
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
// real client; an empty list ends it.
static void games_played(uint32_t appid) {
    auto session=user_session();
    if(!session||session->anon)return;
    std::string body;
    if(appid){std::string game;pb_fixed64(game,2,appid);pb_s(body,1,game);}
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
    const bool steamProvider=stage_steam_provider(c.appid);
    if(steamProvider) {
        try{cloud_sync(c.appid,c.root,false,0);}catch(...){lp_sink("STEAM cloud download: exception");}
        try{achievements_sync(c.appid,false);}catch(...){lp_sink("STEAM achievements: exception");}
        games_played(c.appid);
    }
    if(steamProvider){uint32_t appid=c.appid;std::wstring root=c.root;g_finalSync=[appid,root]{final_sync(appid,root);};}
    regb::init(g_dir,lp_sink); // guest registry (PlayerPrefs): LocalState\registry.sqlite
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
