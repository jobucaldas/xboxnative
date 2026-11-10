// SPDX-License-Identifier: GPL-3.0-or-later
// The console self-test: runs the fixtures app/build.sh packs into layout\diag
// through the same Play runtime a game uses, and writes one result line per
// fixture to LocalState\selftest.log (fetched with dp/get-file.sh).
//
// Why it exists: the installed package ships xbtest.exe and the Play runtime,
// not the off-device runner (tests/play-runner.cpp) or its fixtures, so the
// Vulkan, D3D9, D3D10 and 32-bit paths had no way to be exercised on a console.
//
// How it is asked for: a one-shot LocalState\selftest.txt with one mode per
// line ("all" for every fixture), the same synthetic-input idiom as cmd.txt.
// Play starts one 64-bit guest per launcher process, so the queue is written to
// LocalState\selftest_queue.txt and the launcher restarts between fixtures
// (CoreApplication::RequestRestartAsync, as back_to_games does); the log is
// appended across those restarts, so it is the whole request's record.
//
// The fixtures exit 42 when every step succeeded. A fixture's own stdout is not
// reliable on the console (there is no console handle), so the verdict comes
// from the exit code and Play's own PRUN EVENT lines, both of which the log
// carries. Nothing here is a visual or playability claim.
#pragma once
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>
#include "selftest_plan.h"

namespace selftest {
// A launcher restart that makes no progress must not loop: each fixture gets a
// few attempts (a fixture that faults before game code is released and retried
// once), and a queue older than this is abandoned with a FAIL line.
static const int kMaxRestarts = 4;
static const double kRequestMaxAge = 1800.0;

static std::wstring log_path() { return g_dir + L"\\selftest.log"; }
static std::wstring queue_path() { return g_dir + L"\\selftest_queue.txt"; }
static std::wstring request_path() { return g_dir + L"\\selftest.txt"; }

static void append_log(const std::string& line) {
    std::string text = line;
    if (text.empty() || text.back() != '\n') text += '\n';
    FILE* f = _wfopen(log_path().c_str(), L"ab");
    if (!f) return;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
}
// Play's own lines (PRUN EVENT, mapping, the fixture's stdout) go to the same
// probe sink a launch uses, so they land in LocalState\selftest_probe.log and
// the gateway can fetch the evidence behind each verdict.
static void open_probe() {
    std::lock_guard<std::mutex> l(g_lpm);
    if (g_lpf) fclose(g_lpf);
    g_lpf = _wfopen((g_dir + L"\\selftest_probe.log").c_str(), L"ab");
}
static void close_probe() {
    std::lock_guard<std::mutex> l(g_lpm);
    if (g_lpf) { fclose(g_lpf); g_lpf = nullptr; }
}
// One line per fixture, and the summary the gateway reads. The mode name is
// the request's own, so an unknown name still gets a line (SKIP). The detail is
// flattened to one line and bounded: a loader message can carry newlines.
static void result(const std::string& mode, const std::string& verdict, const std::string& detail) {
    std::string line = "SELFTEST RESULT " + mode + " " + verdict;
    if (!detail.empty()) {
        std::string flat;
        for (char c : detail) flat += (c == '\r' || c == '\n' || c == '\t') ? ' ' : c;
        if (flat.size() > 300) flat.resize(300);
        line += " " + flat;
    }
    append_log(line);
    logf("selftest: %s", line.c_str());
}
static void summary() {
    std::string text;
    read_file_s(log_path(), text);
    auto t = selftest_plan::tally(text);
    char line[160];
    snprintf(line, sizeof line, "SELFTEST SUMMARY pass=%d fail=%d blocked=%d skip=%d", t.pass, t.fail, t.blocked, t.skip);
    append_log(line);
    logf("selftest: %s", line);
}
static bool read_queue(selftest_plan::Queue& q) {
    std::string text;
    return read_file_s(queue_path(), text) && selftest_plan::parse_queue(text, q);
}
static void write_queue(const selftest_plan::Queue& q) { write_text(queue_path(), selftest_plan::format_queue(q)); }
static void clear_queue() { DeleteFileW(queue_path().c_str()); }
// Seconds since the queue was written, or -1 when there is none.
static double queue_age() {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(queue_path().c_str(), GetFileExInfoStandard, &a)) return -1;
    ULARGE_INTEGER written{};
    written.LowPart = a.ftLastWriteTime.dwLowDateTime;
    written.HighPart = a.ftLastWriteTime.dwHighDateTime;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER current{};
    current.LowPart = now.dwLowDateTime;
    current.HighPart = now.dwHighDateTime;
    if (current.QuadPart <= written.QuadPart) return 0;
    return double(current.QuadPart - written.QuadPart) / 1e7;
}

// The packaged fixture, or empty when this build carries none (PLAY_SELFTEST=0,
// or a package from before the diag layout).
static std::wstring fixture(const selftest_plan::Mode& mode) {
    wchar_t self[2048];
    DWORD n = GetModuleFileNameW(nullptr, self, 2048);
    if (!n || n >= 2048) return std::wstring();
    std::wstring package(self, n);
    auto slash = package.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return std::wstring();
    package.resize(slash + 1);
    std::wstring path = package + L"diag\\" + play_runtime::wide(mode.exe);
    return GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES ? std::wstring() : path;
}

// The 32-bit fixture runs in the interpreter on this thread, exactly as
// launch.h's PE32 path runs a game: same entry point, same guest cap, same
// import probe. It needs no launcher restart, so it is the first fixture.
static void run_pe32(const std::string& mode, const std::wstring& path) {
    std::string name = play_runtime::narrow(path.substr(path.find_last_of(L"\\/") + 1));
    std::string summary;
    auto slash = path.find_last_of(L"\\/");
    if (pe32_probe::map(path, slash == std::wstring::npos ? std::wstring() : path.substr(0, slash), summary))
        lp_sink(("SELFTEST " + summary).c_str());
    auto tee = [&name](const std::string& s) {
        size_t start = 0;
        while (start < s.size()) {
            size_t nl = s.find('\n', start);
            if (nl == std::string::npos) nl = s.size();
            std::string row = s.substr(start, nl - start);
            if (row.size() > 400) row.resize(400);
            lp_sink(("SELFTEST OUT " + name + ": " + row).c_str());
            start = nl + 1;
        }
    };
    std::string cmdline = "\"" + name + "\"";
    auto r = x86::run(path, std::wstring(), cmdline, tee, nullptr, x86::kGuestRunCap);
    char line[256];
    snprintf(line, sizeof line, "SELFTEST PRUN PE32 %s base=0x%08x entry=0x%08x imports=%u bound=%u relocs=%u instructions=%llu",
             r.loaded ? "loaded" : "NOT-LOADED", r.base, r.entry, r.imports, r.bound, r.relocations,
             (unsigned long long)r.instructions);
    lp_sink(line);
    for (auto& u : r.unresolved) lp_sink(("SELFTEST PE32 UNRESOLVED " + u).c_str());
    if (!r.fault.empty()) lp_sink(("SELFTEST PE32 fault kind=" + r.fault_kind + " eip=0x" + std::to_string(r.fault_eip)).c_str());
    result(mode, selftest_plan::verdict(mode, r.finished, r.loaded, r.exit_code),
           r.fault.empty() ? std::string() : r.fault_kind + " " + r.fault);
}

// A 64-bit fixture: the same gates, runtime directory and Play start a launch
// uses (launch.h lp_run_impl), without Steam, cloud or presence — a fixture is
// not a game and must not touch the player's account.
static void run_64(const std::string& mode, const std::wstring& path) {
    const std::wstring dir = path.substr(0, path.find_last_of(L"\\/"));
    const std::wstring exe = path.substr(path.find_last_of(L"\\/") + 1);
    if (!donor_pe::probe(lp_sink)) { result(mode, "FAIL donor-gate", ""); return; }
    mlp::donor_resolve = donor_pe::exported;
    if (!user32_core::test(lp_sink)) { result(mode, "FAIL u32c-test", ""); return; }
    regb::init(g_dir, lp_sink);
    {   // Wine's C++ runtime beside the launcher, as a launch sets it.
        wchar_t self[2048];
        DWORD n = GetModuleFileNameW(nullptr, self, 2048);
        if (n && n < 2048) {
            std::wstring package(self, n);
            package.resize(package.find_last_of(L"\\/") + 1);
            play_runtime::set_runtime_directory(package + L"runtime");
        }
    }
    if (!play_runtime::start(dir, exe, L"", lp_sink)) { result(mode, "FAIL guest-already-started", ""); return; }
    auto r = play_runtime::state;
    const ULONGLONG end = GetTickCount64() + 120000;
    for (;;) {
        bool finished = r->finished.load(std::memory_order_acquire);
        if (finished || GetTickCount64() >= end) break;
        if (r->worker) WaitForSingleObject(r->worker, 50); else Sleep(50);
    }
    bool finished = r->finished.load(std::memory_order_acquire);
    bool executed = r->executed.load(std::memory_order_acquire);
    std::string detail = finished ? (r->success ? std::string() : r->result) : std::string("timeout 120 s");
    result(mode, selftest_plan::verdict(mode, finished, executed, r->code), detail);
}

// One step per launcher process. Returns true when the launcher must restart:
// a 64-bit guest cannot be unloaded while its process lives, so the next
// fixture (and, after the last one, the normal launcher) needs a fresh process.
// The 32-bit fixtures run in the interpreter and leave nothing behind, so they
// all run here without a restart.
static bool step() {
    selftest_plan::Queue q;
    if (!read_queue(q) || q.pending.empty()) return false;
    if (q.restarts >= kMaxRestarts) {
        for (auto& mode : q.pending) result(mode, "FAIL restart-limit", "the launcher restarted without making progress");
        clear_queue();
        summary();
        return false;
    }
    bool mapped = false;   // a 64-bit guest is now in this process
    open_probe();
    while (!q.pending.empty()) {
        // The first pending mode is this process's fixture; the rest wait.
        std::string mode = q.pending.front();
        const selftest_plan::Mode* m = selftest_plan::find(mode);
        std::wstring path = m ? fixture(*m) : std::wstring();
        if (m && !path.empty() && !m->pe32) {
            logf("selftest: %s running; the launcher restarts to unload it", mode.c_str());
            q.pending.erase(q.pending.begin());
            run_64(mode, path);
            mapped = true;
            break;   // no second guest in this process
        }
        q.pending.erase(q.pending.begin());
        if (!m) result(mode, "SKIP unknown-mode", "");
        else if (path.empty()) result(mode, "SKIP not-packaged", m->exe);
        else run_pe32(mode, path);
    }
    close_probe();
    if (q.pending.empty()) { clear_queue(); summary(); }
    else { q.restarts++; write_queue(q); }
    return mapped || !q.pending.empty();
}

// Called every frame from the render loop. A request file starts a run; until
// it is consumed, every frame is a no-op. Returns true when a step must run now
// (the caller runs it on a worker thread and restarts the launcher afterwards).
// A step already in flight owns the queue: a request that arrives meanwhile
// must not start a second one (resume() and tick() both reach run_async).
static std::atomic<bool> g_running{false};
static bool g_requested = false;   // this process has consumed a request
static bool tick() {
    static double next = 0;
    if (g_requested || g_running.load(std::memory_order_acquire)) return false;
    // A launch owns the probe sink and the one 64-bit guest slot: leave the
    // request in place until the launcher is idle rather than failing it (or
    // redirecting a running launch's evidence into selftest_probe.log).
    if (g_launch.phase == 1 || g_launch.phase == 3 || play_runtime::observed.load(std::memory_order_acquire)) return false;
    double t = now_s();
    if (t < next) return false;
    next = t + 1.0;
    std::string text;
    if (!read_file_s(request_path(), text)) return false;
    g_requested = true;
    if (!DeleteFileW(request_path().c_str())) { g_requested = false; logf("selftest: request file could not be consumed; no run"); return false; }
    selftest_plan::Queue q;
    q.pending = selftest_plan::parse_request(text);
    // An empty request is a FAIL line the summary counts, not a silent no-op.
    if (q.pending.empty()) { append_log("SELFTEST RESULT request FAIL empty-request"); summary(); g_requested = false; return false; }
    // A new request starts a new record: the log is per request, not per app.
    DeleteFileW(log_path().c_str());
    append_log("SELFTEST BEGIN request=" + std::to_string(q.pending.size()) + " modes");
    write_queue(q);
    return true;
}
// A queue left by a previous process whose restart never came (a crash, or a
// shell that refused the restart) resumes on the next start, but only while it
// is fresh: an abandoned request must not run fixtures days later.
static bool resume() {
    selftest_plan::Queue q;
    if (!read_queue(q) || q.pending.empty()) return false;
    double age = queue_age();
    if (age < 0 || age > kRequestMaxAge) {
        for (auto& mode : q.pending) result(mode, "FAIL stale-request", "the launcher did not resume the run in time");
        clear_queue();
        summary();
        return false;
    }
    logf("selftest: resuming an interrupted run (%zu fixture(s) left)", q.pending.size());
    return true;
}
// Runs this process's step on a worker thread, then asks the UI thread to
// restart the launcher when more fixtures are queued. Never runs a second guest
// in one process. `dispatcher` is the CoreWindow's, taken on the UI thread.
static void run_async(const winrt::Windows::UI::Core::CoreDispatcher& dispatcher) {
    if (g_running.exchange(true, std::memory_order_acq_rel)) { logf("selftest: a step is already running; request ignored"); return; }
    std::thread([dispatcher] {
        init_apartment(apartment_type::multi_threaded);
        bool more = false;
        try { more = step(); } catch (...) { logf("selftest: step threw"); }
        g_running.store(false, std::memory_order_release);
        // No guest is in this process (only 32-bit modes ran, or nothing was
        // packaged), so another request may be taken without a restart.
        if (!more) { g_requested = false; logf("selftest: run finished"); return; }
        // Same rule as back_to_games: a pending restart replaces the app, so
        // Exit() only when the shell did not take it (the queue then resumes
        // on the next start). RestartPending means the restart is coming.
        try {
            dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal, [] {
                logf("selftest: requesting app restart for the next fixture");
                auto exitOnUi = [] {
                    winrt::Windows::ApplicationModel::Core::CoreApplication::Exit();
                };
                try {
                    winrt::Windows::ApplicationModel::Core::CoreApplication::RequestRestartAsync(L"").Completed(
                        [exitOnUi](auto const& operation, auto status) {
                            auto reason = winrt::Windows::ApplicationModel::Core::AppRestartFailureReason::Other;
                            try {
                                if (status == winrt::Windows::Foundation::AsyncStatus::Completed) reason = operation.GetResults();
                            } catch (...) {}
                            logf("selftest: restart status=%d reason=%d", (int)status, (int)reason);
                            if (reason != winrt::Windows::ApplicationModel::Core::AppRestartFailureReason::RestartPending) exitOnUi();
                        });
                } catch (...) { logf("selftest: restart unavailable; closing, the queue resumes on the next start"); exitOnUi(); }
            });
        } catch (...) { logf("selftest: restart request failed; the queue resumes on the next start"); }
    }).detach();
}
}
