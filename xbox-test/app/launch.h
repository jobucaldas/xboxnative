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
struct LpCtx { std::wstring root; std::vector<std::wstring> names; int gen = 0; uint32_t appid = 0; int mapped = 0; unsigned resolved = 0, stubbed = 0; std::string firstMissing, failName, fault; bool ok = false, timedOut = false; };
static int pe_machine(const std::wstring& p) {   // 0 = unreadable/not PE, else IMAGE_FILE_MACHINE_*
    FILE* f = _wfopen(p.c_str(), L"rb"); if (!f) return 0; unsigned char h[1024]; size_t n = fread(h, 1, sizeof h, f); fclose(f);
    if (n < 0x40 || h[0] != 'M' || h[1] != 'Z') return 0; uint32_t o = *(uint32_t*)(h + 0x3c); if (o + 6 > n || memcmp(h + o, "PE\0\0", 4)) return 0; return *(uint16_t*)(h + o + 4);
}
static void lp_run_impl(LpCtx& c) {
    wine_gap::report(c.root);
    if(c.names.empty()) {c.failName="no executable";return;}
    auto exe=c.names.back();
    if(pe_machine(c.root+L"\\"+exe)==IMAGE_FILE_MACHINE_I386) {
        std::string summary;
        bool inspected=pe32_probe::map(c.root+L"\\"+exe,c.root,summary);
        c.mapped=inspected?1:0;
        c.failName=inspected?summary+"; 32-bit execution unsupported":"PE32 data-only inspection failed; execution unsupported";
        lp_sink("PRUN FAIL 32-bit execution unsupported; PE32 inspection is data-only");return;
    }
    // These are behavior/ABI prerequisites, not optional progress diagnostics.
    // Never expose donor exports to executable guests after a failed probe.
    if(!donor_pe::probe(lp_sink)) {c.failName="Wine donor initialization/behavior";lp_sink("DWPE FAIL donor behavior gate; no game code executed");return;}
    mlp::donor_resolve=donor_pe::exported;
    if(!user32_core::test(lp_sink)) {c.failName="u32c-test.exe";lp_sink("U32C FAIL regression PE; no game code executed");return;}
    if(!play_runtime::start(c.root,exe,L"-screen-fullscreen 0 -force-d3d11 -logFile vis-player.log",lp_sink)) {c.failName="guest already started; restart launcher before next Play";return;}
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
static std::vector<std::wstring> pick_targets(uint32_t appid, const std::wstring& root, std::string& why) {
    if (appid == 3405340) { why = "Megabonk.exe with recursive dependency loading"; return {L"Megabonk.exe"}; }
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((root + L"\\*.exe").c_str(), &fd); std::wstring best; uint64_t bs = 0;
    if (h != INVALID_HANDLE_VALUE) { do { std::wstring n = fd.cFileName; std::wstring l = n; for (auto& ch : l) ch = (wchar_t)towlower(ch);
            if (l.find(L"unins") != std::wstring::npos || l.find(L"crash") != std::wstring::npos || l.find(L"redist") != std::wstring::npos || l.find(L"setup") != std::wstring::npos) continue;
            uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow; if (sz > bs) { bs = sz; best = n; } } while (FindNextFileW(h, &fd)); FindClose(h); }
    if (best.empty()) { why = "no launchable .exe in install dir"; return {}; }
    why = "largest top-level .exe"; return {best};
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
    std::string args = appid == 3405340 ? "-screen-fullscreen 0 -force-d3d11 -logFile vis-player.log" : "";
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
            std::wstring root = g_dir + L"\\" + widen(dir); std::string why; auto names = pick_targets(appid, root, why);
            logf("launch: runtime root=LocalState\\%s targets=%zu (%s)", dir.c_str(), names.size(), why.c_str());
            if (dir.empty() || names.empty()) { finish("cannot start: " + (dir.empty() ? std::string("install directory unknown") : why), false, 0, 0); return; }
            { std::lock_guard<std::mutex> l(g_lpm); if (g_lpf) fclose(g_lpf); g_lpf = _wfopen((g_dir + L"\\launch_probe.log").c_str(), L"wb"); g_lpMissing = g_lpStubbed = g_lpStubCalls = g_lpLines = 0; g_lpLastFail.clear(); }
            LpCtx c; c.root = root; c.names = names;c.gen=gen;c.appid=appid; bool noFault = lp_run_seh(&c);
            { std::lock_guard<std::mutex> l(g_lpm); if (g_lpf) { fclose(g_lpf); g_lpf = nullptr; } }
            int total=play_runtime::state?int(play_runtime::state->moduleCount):int(names.size());
            if(!noFault) {finish("loader SEH exception; last event: "+(play_runtime::state?play_runtime::state->snapshot():std::string("loader not entered")),false,c.mapped,total);return;}
            lp_finish(gen,appid,c.fault.empty()?c.failName:c.fault,c.ok,c.mapped,total,c.timedOut?"timeout":nullptr);
        } catch (std::exception const& e) { finish(std::string("loader threw: ") + e.what(), false, 0, 0); }
        catch (...) { finish("loader threw an unknown exception.", false, 0, 0); }
    }).detach();
}
