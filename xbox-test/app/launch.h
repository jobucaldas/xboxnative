// Launch step of the detail screen ("Play"): launch_request.json + a REAL in-process attempt via the wxl PE mapper + MLP probe
// (host/*.h, vendored unchanged from lane/tlm xbox-test/uwph/host). What it does today, honestly:
//   - maps the game's x64 PE images (relocations, imports resolved against real system DLLs / mapped siblings / probe-only stubs, final page protections),
//   - NEVER calls an entry point / DllMain / TLS callback: no game code executes. There is no Wine loader/process environment in the launcher yet.
// Everything is logged to boot.log (key lines) and LocalState\launch_probe.log (all lines); the result is shown on screen as "Launch attempted: ...".
#pragma once
#include <memory>
#include <winrt/Windows.System.h>
#include "host/wxl_mapper.h"
#include "host/mlp_probe.h"
#include "../uwph/user32/probe.h"

struct LaunchState {
    std::atomic<int> phase{0};          // 0 never, 1 running, 2 finished
    std::atomic<uint32_t> appid{0};
    std::atomic<int> gen{0};
    double started = 0;                 // render-thread read only (set before phase=1)
    std::mutex m; std::string result;   // result text (after "Launch attempted: ")
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
    bool key = strstr(s, "GAP") || strstr(s, "FAIL") || strstr(s, "SUMMARY") || strstr(s, "MLP IMAGE") || strstr(s, "mapped") || strstr(s, "PROBE") || strstr(s, "STUB-CALL");
    if (key) logf("launch-probe: %.300s", s);
}
#include "wine_gap.h"
#include "host/wxl_pe32.h"
#include "pe32_probe.h"

struct LpCtx { std::wstring root; std::vector<std::wstring> names; int mapped = 0; unsigned resolved = 0, stubbed = 0; std::string firstMissing, failName, fault, pe32Summary; bool ok = false; };
static int pe_machine(const std::wstring& p) {   // 0 = unreadable/not PE, else IMAGE_FILE_MACHINE_*
    FILE* f = _wfopen(p.c_str(), L"rb"); if (!f) return 0; unsigned char h[1024]; size_t n = fread(h, 1, sizeof h, f); fclose(f);
    if (n < 0x40 || h[0] != 'M' || h[1] != 'Z') return 0; uint32_t o = *(uint32_t*)(h + 0x3c); if (o + 6 > n || memcmp(h + o, "PE\0\0", 4)) return 0; return *(uint16_t*)(h + o + 4);
}
static void lp_run_impl(LpCtx& c) {
    mlp::log = lp_sink; wxl::logger = lp_sink; mlp::summaries.clear(); mlp::assets.clear(); mlp::stub_names.clear();
    wine_gap::report(c.root);
    lp_sink("MLP PROBE begin; NO entry/DllMain/TLS callbacks; zero RWX; failure placeholders are probe-only");
    if(!user32_core::test(lp_sink)){ c.failName="u32c-test.exe"; lp_sink("U32C FAIL regression PE"); return; }
    std::vector<std::unique_ptr<wxl::Image>> imgs;
    for (auto& nm : c.names) {
        std::string n8(nm.begin(), nm.end()); mlp::summaries.clear(); lp_sink(("MLP IMAGE " + n8).c_str());
        std::wstring path = c.root + L"\\" + nm;
        if (!file_exists(path)) { c.failName = n8; lp_sink(("LAUNCH FAIL file missing: " + n8).c_str()); return; }
        if (pe_machine(path) == 0x14c) {
            if (!pe32_probe::map(path, c.root, c.pe32Summary)) { c.failName=n8; return; }
            ++c.mapped; continue;
        }
        imgs.emplace_back(new wxl::Image());
        if (!wxl::run(path.c_str(), lp_sink, imgs.back().get(), mlp::resolve)) { c.failName = n8; lp_sink("MLP-PROBE-FAILED"); mlp::assets.clear(); return; }
        std::string key = n8; for (auto& ch : key) if (ch >= 'A' && ch <= 'Z') ch += 32;
        mlp::assets[key] = imgs.back().get(); c.mapped++;
        for (auto& row : mlp::summaries) {
            c.resolved += row.second.resolved; c.stubbed += row.second.stubbed;
            if (c.firstMissing.empty() && row.second.first != "none") c.firstMissing = row.first + "!" + row.second.first;
            char m[1024]; snprintf(m, sizeof m, "MLP SUMMARY image=%s dll=%s resolved=%u stubbed=%u first-missing=%s", n8.c_str(), row.first.c_str(), row.second.resolved, row.second.stubbed, row.second.first.c_str()); lp_sink(m);
        }
        mlp::report_kernel();
    }
    lp_sink("MLP-PROBE-DONE no guest code executed; TLS/unwind deferred; zero RWX requested");
    mlp::assets.clear(); c.ok = true;
}
static bool lp_run_seh(LpCtx* c) {     // no C++ objects with destructors in this frame: SEH guard so a probe bug cannot take the launcher down
    __try { lp_run_impl(*c); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static std::vector<std::wstring> pick_targets(uint32_t appid, const std::wstring& root, std::string& why) {
    if (appid == 3405340) { why = "Unity preset (baselib.dll, UnityPlayer.dll, Megabonk.exe)"; return {L"baselib.dll", L"UnityPlayer.dll", L"Megabonk.exe"}; }
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((root + L"\\*.exe").c_str(), &fd); std::wstring best; uint64_t bs = 0;
    if (h != INVALID_HANDLE_VALUE) { do { std::wstring n = fd.cFileName; std::wstring l = n; for (auto& ch : l) ch = (wchar_t)towlower(ch);
            if (l.find(L"unins") != std::wstring::npos || l.find(L"crash") != std::wstring::npos || l.find(L"redist") != std::wstring::npos || l.find(L"setup") != std::wstring::npos) continue;
            uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow; if (sz > bs) { bs = sz; best = n; } } while (FindNextFileW(h, &fd)); FindClose(h); }
    if (best.empty()) { why = "no launchable .exe in install dir"; return {}; }
    why = "largest top-level .exe"; return {best};
}

// Entry: called on the render thread; returns immediately (work happens on a worker thread).
static void start_launch(uint32_t appid, const std::string& name, const std::string& dir, const char* via) {
    int gen = ++g_launch.gen; g_launch.appid = appid; g_launch.started = now_s();
    { std::lock_guard<std::mutex> l(g_launch.m); g_launch.result.clear(); } g_launch.phase = 1;
    std::string args = appid == 3405340 ? "-screen-fullscreen 1 -force-d3d11" : "";
    char b[1500]; snprintf(b, sizeof b, "{\"appid\":%u,\"name\":\"%s\",\"installDir\":\"%s\",\"time\":\"%s\",\"args\":\"%s\",\"via\":\"%s\"}\n", appid, json_esc(name).c_str(), json_esc(dir).c_str(), utc_iso().c_str(), args.c_str(), via);
    write_text(g_dir + L"\\launch_request.json", b);
    logf("launch: request written LocalState\\launch_request.json appid=%u dir=%s via=%s", appid, dir.c_str(), via);
    std::thread([=] {
        init_apartment(apartment_type::multi_threaded);
        auto finish = [&](const std::string& r, bool ok, int mapped, int total) {
            if (g_launch.gen != gen) { logf("launch: late result ignored (watchdog already reported)"); return; }
            { std::lock_guard<std::mutex> l(g_launch.m); g_launch.result = r; } g_launch.phase = 2;
            logf("launch: RESULT ok=%d mapped=%d/%d: %s", ok ? 1 : 0, mapped, total, r.c_str());
            char j[1800]; snprintf(j, sizeof j, "{\"appid\":%u,\"time\":\"%s\",\"stage\":\"%s\",\"ok\":%s,\"mapped\":%d,\"total\":%d,\"gameCodeExecuted\":false,\"result\":\"%s\"}\n", appid, utc_iso().c_str(), ok ? "images-mapped" : "failed", ok ? "true" : "false", mapped, total, json_esc(r).c_str());
            write_text(g_dir + L"\\launch_result.json", j);
        };
        try {
            // 1. app activation of a game-host package: only if one registers a protocol (none does today; see LANE_NOTES)
            try { winrt::Windows::Foundation::Uri uri(L"xboxsteam-host://launch");
                  auto q = winrt::Windows::System::Launcher::QueryUriSupportAsync(uri, winrt::Windows::System::LaunchQuerySupportType::Uri).get();
                  logf("launch: activation query xboxsteam-host:// -> %d (0=Available,1=AppNotInstalled,2=AppUnavailable,3=NotSupported,4=Unknown); %s", (int)q, (int)q == 0 ? "would activate host" : "no host package registers it, using in-process probe");
                  if ((int)q == 0) { std::wstring u = L"xboxsteam-host://launch?appid=" + widen(std::to_string(appid)) + L"&dir=" + widen(dir); bool ok = winrt::Windows::System::Launcher::LaunchUriAsync(winrt::Windows::Foundation::Uri(u)).get(); logf("launch: host activation LaunchUriAsync ok=%d", ok ? 1 : 0); if (ok) { finish("handed over to host package via xboxsteam-host://", true, 0, 0); return; } }
            } catch (winrt::hresult_error const& e) { logf("launch: activation query failed hr=0x%08x", (unsigned)e.code().value); } catch (...) { logf("launch: activation query failed"); }
            // 2. in-process mapper probe
            std::wstring root = g_dir + L"\\" + widen(dir); std::string why; auto names = pick_targets(appid, root, why);
            logf("launch: probe root=LocalState\\%s targets=%zu (%s)", dir.c_str(), names.size(), why.c_str());
            if (dir.empty() || names.empty()) { finish("cannot start: " + (dir.empty() ? std::string("install directory unknown") : why) + ". No game code executed.", false, 0, 0); return; }
            { std::lock_guard<std::mutex> l(g_lpm); if (g_lpf) fclose(g_lpf); g_lpf = _wfopen((g_dir + L"\\launch_probe.log").c_str(), L"wb"); g_lpMissing = g_lpStubbed = g_lpStubCalls = g_lpLines = 0; g_lpLastFail.clear(); }
            LpCtx c; c.root = root; c.names = names; bool noFault = lp_run_seh(&c);
            { std::lock_guard<std::mutex> l(g_lpm); if (g_lpf) { fclose(g_lpf); g_lpf = nullptr; } }
            char m[700]; int total = (int)names.size();
            if (!noFault) { finish("probe CRASHED (SEH exception) while mapping; launcher survived. No game code executed.", false, c.mapped, total); return; }
            if (!c.ok) { std::string f; { std::lock_guard<std::mutex> l(g_lpm); f = g_lpLastFail; } snprintf(m, sizeof m, "failed at %s: %s. No game code executed.", c.failName.c_str(), f.empty() ? "see launch_probe.log" : f.c_str()); finish(m, false, c.mapped, total); return; }
            if (!c.pe32Summary.empty()) {
                finish(c.pe32Summary+". Game code NOT executed; IAT unbound, TLS/entry not called.", true, c.mapped, total); return;
            }
            unsigned miss, stubs, calls; { std::lock_guard<std::mutex> l(g_lpm); miss = g_lpMissing; stubs = g_lpStubbed; calls = g_lpStubCalls; }
            snprintf(m, sizeof m, "mapped %d/%d images, %u imports resolved, %u missing%s%.100s. Game code NOT executed (no Wine loader/process yet): no process, exit code none.",
                     c.mapped, total, c.resolved, miss, c.firstMissing.empty() ? "" : ", first ", c.firstMissing.c_str()); (void)stubs; (void)calls;
            finish(m, true, c.mapped, total);
        } catch (std::exception const& e) { finish(std::string("probe threw: ") + e.what() + ". No game code executed.", false, 0, 0); }
        catch (...) { finish("probe threw an unknown exception. No game code executed.", false, 0, 0); }
    }).detach();
}
