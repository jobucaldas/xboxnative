// Graphics-API badge for the grid tiles and the detail screen (D3D11 first; everything else is shown muted / not at all).
// Installed games: scan the PE import tables of every .exe/.dll in the install dir (depth <= 3) on a worker thread; result cached in
// LocalState\apibadge.cache keyed by the install marker (mtime+size), so the grid never scans on the render thread and never rescans twice.
// Not installed: only what Steam's store API already told us (LocalState\storecache\<appid>.json pc_requirements "DirectX Version N"); else nothing.
#pragma once
#include <condition_variable>
#include <deque>
#include <map>
#include "apibadge_core.h"
namespace apib {
struct Result { int kind = K_UNKNOWN; int src = 0; /*0 none, 1 game files, 2 store text*/ std::string note; bool ready = false; };
struct Entry { Result r; std::string sig; double checked = 0; bool pending = false; };
static std::mutex g_m; static std::map<uint32_t, Entry> g_e; static std::deque<uint32_t> g_q; static std::condition_variable g_cv; static bool g_started = false; static int g_off = -1; static long g_jobs = 0, g_throws = 0; static double g_jobSec = 0, g_lkMax = 0, g_lkSum = 0, g_lastStat = 0; static long g_lkN = 0; static size_t g_qmax = 0;   // g_off: LocalState\nobadge.txt starting with '1' => badges disabled (A/B perf diagnostic)
struct Disk { std::string sig; int kind; std::string note; }; static std::map<uint32_t, Disk> g_disk; static bool g_diskLoaded = false;
static std::wstring cache_path() { return g_dir + L"\\apibadge.cache"; }
static void disk_load() {
    g_diskLoaded = true; std::string s; if (!read_file_s(cache_path(), s)) return;
    size_t p = 0; while (p < s.size()) { size_t e = s.find('\n', p); if (e == std::string::npos) e = s.size(); std::string ln = s.substr(p, e - p); p = e + 1;
        std::vector<std::string> f; size_t q = 0; while (true) { size_t t = ln.find('\t', q); if (t == std::string::npos) { f.push_back(ln.substr(q)); break; } f.push_back(ln.substr(q, t - q)); q = t + 1; }
        if (f.size() >= 4) g_disk[(uint32_t)strtoul(f[0].c_str(), nullptr, 10)] = {f[1], atoi(f[2].c_str()), f[3]}; }
}
static void disk_save() { std::string s; for (auto& kv : g_disk) s += std::to_string(kv.first) + "\t" + kv.second.sig + "\t" + std::to_string(kv.second.kind) + "\t" + kv.second.note + "\n"; write_text(cache_path(), s); }
static std::string marker_sig(uint32_t appid) {
    std::wstring mk = g_dir + L"\\dl_" + widen(std::to_string(appid)) + L".done"; WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(mk.c_str(), GetFileExInfoStandard, &fa)) return "";
    char b[96]; snprintf(b, sizeof b, "%08x%08x-%u", (unsigned)fa.ftLastWriteTime.dwHighDateTime, (unsigned)fa.ftLastWriteTime.dwLowDateTime, (unsigned)fa.nFileSizeLow); return b;
}
static void walk(const std::wstring& dir, int depth, std::set<std::string>& dlls, std::set<std::string>& perFile, int& files, int& notPe) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return;
    do { std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue; std::wstring lw = n; for (auto& c : lw) c = (wchar_t)towlower(c);
        std::string l8(lw.begin(), lw.end());
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (depth < 3 && lw.find(L"redist") == std::wstring::npos && lw.find(L"directx") == std::wstring::npos && lw.find(L"dotnet") == std::wstring::npos) walk(dir + L"\\" + n, depth + 1, dlls, perFile, files, notPe); continue; }
        if (lw.size() < 4 || (lw.substr(lw.size() - 4) != L".exe" && lw.substr(lw.size() - 4) != L".dll")) continue;
        if (is_vendor_gfx_name(l8) || files >= 400) continue;
        FILE* f = _wfopen((dir + L"\\" + n).c_str(), L"rb"); if (!f) continue;
        std::set<std::string> d; bool ok = pe_dlls(f, d); fclose(f); files++; if (!ok) { notPe++; continue; }
        for (auto& x : d) dlls.insert(x);
        for (auto n2 : {"d3d11.dll", "d3d12.dll", "dxgi.dll", "d3d9.dll", "d3d8.dll", "ddraw.dll", "d3d10.dll", "d3d10_1.dll", "d3d10core.dll", "opengl32.dll"}) if (d.count(n2)) perFile.insert(std::string(n2) + " in " + l8);
    } while (FindNextFileW(h, &fd)); FindClose(h);
}
static std::string store_text_for(uint32_t appid) {   // pc_requirements minimum+recommended of the cached store response ("" when no cache / no text)
    std::string body; if (!read_file_s(g_dir + L"\\storecache\\" + widen(std::to_string(appid)) + L".json", body) || body.size() < 10) return "";
    try {
        auto root = winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(body)); auto key = winrt::to_hstring(std::to_string(appid)); if (!root.HasKey(key)) return "";
        auto e = root.GetNamedObject(key); if (!e.GetNamedBoolean(L"success", false) || !e.HasKey(L"data")) return ""; auto d = e.GetNamedObject(L"data");
        if (!d.HasKey(L"pc_requirements") || d.GetNamedValue(L"pc_requirements").ValueType() != winrt::Windows::Data::Json::JsonValueType::Object) return "";
        auto pr = d.GetNamedObject(L"pc_requirements"); std::string t;
        for (auto k : {L"minimum", L"recommended"}) if (pr.HasKey(k) && pr.GetNamedValue(k).ValueType() == winrt::Windows::Data::Json::JsonValueType::String) t += winrt::to_string(pr.GetNamedString(k)) + "\n";
        return t;
    } catch (...) { return ""; }
}
static Result from_store(uint32_t appid) { Result r; std::string note; int k = classify_store_text(store_text_for(appid), &note); if (k != K_UNKNOWN) { r.kind = k; r.src = 2; r.note = note + " (Steam store)"; } r.ready = true; return r; }
static void job(uint32_t appid, Entry old) {
    Result r; std::string sig; InstallInfo ii = read_install(appid);
    if (ii.installed && !ii.dir.empty()) {
        sig = marker_sig(appid); bool hit = false;
        { std::lock_guard<std::mutex> l(g_m); if (!g_diskLoaded) disk_load(); auto it = g_disk.find(appid); if (it != g_disk.end() && it->second.sig == sig) { r.kind = it->second.kind; r.note = it->second.note; r.src = 1; hit = true; } }
        if (!hit) {
            std::set<std::string> dlls, per; int files = 0, notPe = 0; double t0 = now_s(); walk(g_dir + L"\\" + widen(ii.dir), 0, dlls, per, files, notPe);
            r.kind = files == 0 ? K_UNKNOWN : classify_dlls(dlls); r.src = 1; std::string ev; for (auto& s : per) { if (ev.size() > 90) break; if (!ev.empty()) ev += "; "; ev += s; }
            r.note = ev.empty() ? (files ? "no D3D/GL imports in " + std::to_string(files) + " PE files" : "no PE files found") : ev;
            if (r.kind == K_UNKNOWN) r.src = 0;
            logf("badge: scan appid=%u dir=%s files=%d notpe=%d kind=%s evidence='%s' took=%.2fs", appid, ii.dir.c_str(), files, notPe, kind_name(r.kind), r.note.c_str(), now_s() - t0);
            std::lock_guard<std::mutex> l(g_m); g_disk[appid] = {sig, r.kind, r.note}; disk_save();
        }
        if (r.kind == K_UNKNOWN || r.kind == K_NONE) { Result s = from_store(appid); if (s.kind != K_UNKNOWN) r = s; }
    } else r = from_store(appid);
    r.ready = true;
    std::lock_guard<std::mutex> l(g_m); Entry& e = g_e[appid]; if (e.r.kind != r.kind || e.r.src != r.src) logf("badge: appid=%u -> %s (src %d) %s", appid, kind_name(r.kind), r.src, r.note.c_str());
    e.r = r; e.sig = sig; e.checked = now_s(); e.pending = false;
}
static void worker() {
    init_apartment(apartment_type::multi_threaded);
    while (true) { uint32_t id; Entry old; { std::unique_lock<std::mutex> l(g_m); g_cv.wait(l, [] { return !g_q.empty(); }); id = g_q.front(); g_q.pop_front(); old = g_e[id]; }
        double t0 = now_s(); try { job(id, old); } catch (...) { std::lock_guard<std::mutex> l(g_m); g_throws++; g_e[id].pending = false; g_e[id].checked = now_s(); g_e[id].r.ready = true; } std::lock_guard<std::mutex> l(g_m); g_jobs++; g_jobSec += now_s() - t0; }
}
// Render-thread safe: never blocks on I/O. Not ready yet => r.ready false (draw nothing). Re-checked every 8 s (cheap: cache hit).
static Result lookup(uint32_t appid) {
    double t00 = now_s(); std::lock_guard<std::mutex> l(g_m); double t01 = now_s() - t00; g_lkN++; g_lkSum += t01; if (t01 > g_lkMax) g_lkMax = t01;
    if (t00 - g_lastStat > 5.0) { g_lastStat = t00; logf("badge: stats jobs=%ld throws=%ld jobsec=%.3f queue=%zu lookups=%ld lockwait_avg=%.6f lockwait_max=%.4f", g_jobs, g_throws, g_jobSec, g_q.size(), g_lkN, g_lkN ? g_lkSum / g_lkN : 0.0, g_lkMax); g_lkMax = 0; }
    if (g_off < 0) { std::string nb; g_off = read_file_s(g_dir + L"\\nobadge.txt", nb) && !nb.empty() && nb[0] == '1' ? 1 : 0; }
    if (g_off) return Result();
    if (!g_started) { g_started = true; std::thread(worker).detach(); }
    Entry& e = g_e[appid]; double t = now_s();
    if (!e.pending && (!e.r.ready || t - e.checked > 8.0)) { e.pending = true; g_q.push_back(appid); g_cv.notify_one(); }
    return e.r;
}
static Result combine(Result r, int storeKind, const std::string& storeNote) {   // detail screen: the freshly loaded store text fills in when game files say nothing
    if ((r.kind == K_UNKNOWN || r.kind == K_NONE) && storeKind != K_UNKNOWN) { r.kind = storeKind; r.src = 2; r.note = storeNote + " (Steam store)"; r.ready = true; }
    return r;
}
// Tile/detail badge. Returns drawn width (0 when nothing to show). Filled green = scanned from game files, outlined green = per Steam store text.
static int draw(int x, int y, int sc, const Result& r) {
    const char* lab = r.kind == K_D3D11 ? "D3D11" : r.kind == K_D3D12 ? "D3D12" : r.kind == K_LEGACY ? "D3D9/LEGACY" : nullptr; if (!lab) return 0;
    int w = twidth(lab, sc) + 8 * sc, h = 8 * sc + 6 * sc / 2 + 2;
    if (r.kind == K_D3D11) { if (r.src == 1) { fill(x, y, w, h, C3(30, 150, 70)); text(x + 4 * sc, y + 2 + 3 * sc / 4, lab, sc, C3(255, 255, 255)); }
                             else { fill(x, y, w, h, C3(12, 30, 20)); frame(x, y, w, h, 2, C3(60, 200, 110)); text(x + 4 * sc, y + 2 + 3 * sc / 4, lab, sc, C3(120, 255, 160)); } }
    else { fill(x, y, w, h, C3(52, 58, 70)); frame(x, y, w, h, 1, C3(85, 92, 106)); text(x + 4 * sc, y + 2 + 3 * sc / 4, lab, sc, C3(135, 143, 158)); }
    return w;
}
static std::string describe(const Result& r) {
    switch (r.kind) {
    case K_D3D11: return r.src == 1 ? "D3D11 (from game files)" : "D3D11 (per Steam store)";
    case K_D3D12: return "D3D12 - not supported yet";
    case K_LEGACY: return "D3D9/legacy - unsupported for now";
    case K_NONE: return "no graphics API found in files";
    default: return r.ready ? "unknown" : "checking...";
    }
}
}
