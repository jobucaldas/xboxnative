// Xbox Steam launcher UI (UWP, no XAML): CoreWindow + D3D11 swap chain at 1080p, software-composed UI.
//  Login screen: Steam IAuthenticationService QR login over HTTPS (Windows.Web.Http, normal TLS validation).
//  Library screen: IPlayerService/GetOwnedGames + Steam CDN covers (WIC decode), D-pad grid, A = real download (steamdl.h).
// Tokens are stored in LocalState only and are NEVER logged or drawn. Activation args are ignored.
#define _CRT_SECURE_NO_WARNINGS
#include <cmath>
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <algorithm>
#include <deque>
#include <set>
#include <memory>
#include <condition_variable>
#include <functional>
#include <ctime>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Popups.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.Devices.Input.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Networking.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <chrono>
#include "qrcodegen.hpp"
extern "C" {
#include "vendor/zlib/zlib.h"
#include "vendor/lzma/LzmaDec.h"
}
extern "C" size_t ZSTD_decompress(void* dst, size_t dstCapacity, const void* src, size_t compressedSize);
extern "C" unsigned ZSTD_isError(size_t code);
#include "font8x8_basic.h"
#include "launch_button.h"
#include "detail_input.h"
#include <xinput.h>
#include "../uwph/display/gamepad.h"

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::Gaming::Input;
namespace Http = winrt::Windows::Web::Http;

// ---------------------------------------------------------------- logging (never pass secrets here)
static FILE* g_log = nullptr;
static std::mutex g_logm;
static std::wstring g_dir;
static LARGE_INTEGER g_t0, g_freq;
static double now_s() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return double(t.QuadPart - g_t0.QuadPart) / double(g_freq.QuadPart); }
static void logf(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    OutputDebugStringA(buf); OutputDebugStringA("\n");
    std::lock_guard<std::mutex> l(g_logm);
    if (g_log) { fprintf(g_log, "[%8.3f] %s\n", now_s(), buf); fflush(g_log); }
}
static void open_log() {
    QueryPerformanceFrequency(&g_freq); QueryPerformanceCounter(&g_t0);
    try { g_dir = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str();
          MoveFileExW((g_dir + L"\\boot.log").c_str(), (g_dir + L"\\boot.prev.log").c_str(), MOVEFILE_REPLACE_EXISTING); // keep the session before a restart
          g_log = _wfopen((g_dir + L"\\boot.log").c_str(), L"wb"); } catch (...) {}
}

// ---------------------------------------------------------------- framebuffer + bitmap text
static const int W = 1920, H = 1080;
static std::vector<uint32_t> g_fb(W * H);
static inline uint32_t C3(int r, int g, int b) { return 0xFF000000u | (r << 16) | (g << 8) | b; }
static void fill(int x, int y, int w, int h, uint32_t c) {
    int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(W, x + w), y1 = std::min(H, y + h);
    for (int j = y0; j < y1; j++) { uint32_t* p = &g_fb[j * W]; for (int i = x0; i < x1; i++) p[i] = c; }
}
static void frame(int x, int y, int w, int h, int t, uint32_t c) { fill(x, y, w, t, c); fill(x, y + h - t, w, t, c); fill(x, y, t, h, c); fill(x + w - t, y, t, h, c); }
// Bitmap-font text, named text8 so the name `text` stays free for ui::theme's
// color constants (ui_guide.h draws with `using namespace ui::theme` and must
// not see an ambiguous global). Used by the font-failure fallback and the
// launch-option picker.
static void text8(int x, int y, const std::string& s, int sc, uint32_t c) {
    for (size_t k = 0; k < s.size(); k++) {
        unsigned char ch = (unsigned char)s[k]; if (ch >= 128) ch = '?';
        for (int r = 0; r < 8; r++) { unsigned char row = (unsigned char)font8x8_basic[ch][r];
            for (int b = 0; b < 8; b++) if (row & (1 << b)) fill(x + (int)k * 8 * sc + b * sc, y + r * sc, sc, sc, c); }
    }
}
static std::string clip(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 2) + ".."; }

// ---------------------------------------------------------------- HTTP (Windows.Web.Http, default TLS validation)
struct Resp { int status = 0; int eresult = -1; std::string body; std::string err; };
static Http::HttpClient& client() {
    static Http::HttpClient c = [] { Http::HttpClient h; h.DefaultRequestHeaders().UserAgent().TryParseAdd(L"Mozilla/5.0 (Windows; U; Windows NT 10.0; en-US; Valve Steam Client/default/1665786434;) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/85.0.4183.121 Safari/537.36"); return h; }();
    return c;
}
static Resp http(bool post, const std::string& url, const std::string& form, const char* tag) {
    Resp r;
    try {
        Http::HttpRequestMessage req(post ? Http::HttpMethod::Post() : Http::HttpMethod::Get(), winrt::Windows::Foundation::Uri(to_hstring(url)));
        if (post) req.Content(Http::HttpStringContent(to_hstring(form), winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8, L"application/x-www-form-urlencoded"));
        auto resp = client().SendRequestAsync(req).get();
        r.status = (int)resp.StatusCode();
        auto hs = resp.Headers(); if (hs.HasKey(L"x-eresult")) r.eresult = _wtoi(hs.Lookup(L"x-eresult").c_str());
        auto buf = resp.Content().ReadAsBufferAsync().get();
        auto rd = winrt::Windows::Storage::Streams::DataReader::FromBuffer(buf);
        r.body.resize(buf.Length()); if (buf.Length()) rd.ReadBytes(array_view<uint8_t>((uint8_t*)&r.body[0], (uint8_t*)&r.body[0] + r.body.size()));
    } catch (hresult_error const& e) { r.err = to_string(e.message()); logf("http %s hresult_error 0x%08x %s", tag, (unsigned)e.code().value, r.err.c_str()); }
    catch (...) { r.err = "exception"; }
    logf("http %s -> HTTP %d eresult=%d bytes=%zu%s%s", tag, r.status, r.eresult, r.body.size(), r.err.empty() ? "" : " err=", r.err.c_str());
    return r;
}
static std::string urlenc(const std::string& s) {
    static const char* hx = "0123456789ABCDEF"; std::string o;
    for (unsigned char c : s) { if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c; else { o += '%'; o += hx[c >> 4]; o += hx[c & 15]; } }
    return o;
}
static std::string b64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string o; size_t i = 0;
    for (; i + 2 < in.size(); i += 3) { uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2]; o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63]; }
    if (i + 1 == in.size()) { uint32_t v = (uint8_t)in[i] << 16; o += t[v >> 18]; o += t[(v >> 12) & 63]; o += "=="; }
    else if (i + 2 == in.size()) { uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8); o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += '='; }
    return o;
}
static std::string unb64url(const std::string& s) {
    std::string o; uint32_t acc = 0; int bits = 0;
    for (char c : s) { int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : (c == '+' || c == '-') ? 62 : (c == '/' || c == '_') ? 63 : -1;
        if (v < 0) continue; acc = (acc << 6) | v; bits += 6; if (bits >= 8) { bits -= 8; o += (char)((acc >> bits) & 255); } }
    return o;
}

#include "steam_wire.h"
#include "steam_presence.h"   // ClientPersonaState / persona_request_body for the sidebar's profile row

// ---------------------------------------------------------------- Steam auth (matches steam-session AuthenticationClient, platform SteamClient)
static const std::string API = "https://api.steampowered.com/";
static Resp steam_call(const char* iface, const char* method, const std::string& req) {
    std::string url = API + iface + "/" + method + "/v1/";
    return http(true, url, "input_protobuf_encoded=" + urlenc(b64(req)), method);
}
struct QrSession { uint64_t clientId = 0; std::string requestId, url; float interval = 5; };
static bool begin_qr(QrSession& q) {
    std::string dd; pb_s(dd, 1, "Xbox Native"); pb_u(dd, 2, 1 /*EAuthTokenPlatformType.SteamClient*/); pb_u(dd, 3, 20 /*EOSType.Win11*/); pb_u(dd, 4, 1 /*gaming_device_type*/);
    std::string req; pb_s(req, 3, dd);
    Resp r = steam_call("IAuthenticationService", "BeginAuthSessionViaQR", req);
    if (r.status != 200 || r.eresult != 1) return false;
    auto f = pb_parse(r.body); auto c = pb_get(f, 1), u = pb_get(f, 2), id = pb_get(f, 3), iv = pb_get(f, 4);
    if (!c || !u || !id) return false;
    q.clientId = c->u; q.url = u->s; q.requestId = id->s;
    if (iv) { uint32_t b = (uint32_t)iv->u; float fl; memcpy(&fl, &b, 4); if (fl >= 1 && fl <= 30) q.interval = fl; }
    return true;
}
enum PollRes { POLL_WAIT, POLL_SCANNED, POLL_OK, POLL_FAIL };
static PollRes poll_qr(QrSession& q, std::string& refresh, std::string& access) {
    std::string req; pb_u(req, 1, q.clientId); pb_s(req, 2, q.requestId);
    Resp r = steam_call("IAuthenticationService", "PollAuthSessionStatus", req);
    if (r.status != 200 || r.eresult != 1) return POLL_FAIL;
    auto f = pb_parse(r.body);
    if (auto n = pb_get(f, 1)) q.clientId = n->u;
    if (auto n = pb_get(f, 2)) { q.url = n->s; logf("poll: Steam rotated challenge url"); }
    auto rt = pb_get(f, 3), at = pb_get(f, 4), ri = pb_get(f, 5);
    if (rt && !rt->s.empty()) { refresh = rt->s; if (at) access = at->s; return POLL_OK; }
    return (ri && ri->u) ? POLL_SCANNED : POLL_WAIT;
}
static std::string jwt_sub(const std::string& jwt) {
    size_t a = jwt.find('.'); if (a == std::string::npos) return {}; size_t b = jwt.find('.', a + 1); if (b == std::string::npos) return {};
    std::string p = unb64url(jwt.substr(a + 1, b - a - 1)); size_t k = p.find("\"sub\""); if (k == std::string::npos) return {};
    size_t q1 = p.find('"', k + 5), q2 = q1 == std::string::npos ? q1 : p.find('"', q1 + 1); if (q2 == std::string::npos) return {};
    return p.substr(q1 + 1, q2 - q1 - 1);
}
static bool g_genTransient = false;   // last gen_access failure was network/server (not an invalid token)
static bool gen_access(const std::string& refresh, std::string& access) {
    std::string sid = jwt_sub(refresh); if (sid.empty()) return false;
    std::string req; pb_s(req, 1, refresh); pb_fixed64(req, 2, strtoull(sid.c_str(), nullptr, 10));
    Resp r = steam_call("IAuthenticationService", "GenerateAccessTokenForApp", req);
    g_genTransient = (r.status == 0 || r.status >= 500);
    if (r.status != 200 || r.eresult != 1) return false;
    auto f = pb_parse(r.body); auto a = pb_get(f, 1); if (!a || a->s.empty()) return false;
    access = a->s; return true;
}

// ---------------------------------------------------------------- Steam CM (websocket) client: connect + ClientLogon only (BACKLOG dl-megabonk, phase 1)
// Wire format matches steam-user's CM websocket transport (wss://<cm>:443/cmsocket/, binary frames; no
// ChannelEncrypt handshake since TLS already secures the channel). Each frame = [u32le rawEMsg][u32le
// protoHeaderLen][protoHeaderBytes][bodyBytes], rawEMsg = EMsg | 0x80000000. Reuses pb_* from above.
namespace Sock = winrt::Windows::Networking::Sockets;
enum { EMSG_Multi = 1, EMSG_ClientLogon = 5514, EMSG_ClientLogOnResponse = 751 };
static const uint32_t PB_PROTOMASK = 0x80000000u;
static void put_u32le(std::string& o, uint32_t v) { for (int i = 0; i < 4; i++) o += (char)((v >> (8 * i)) & 0xFF); }
static uint32_t get_u32le(const std::string& s, size_t off) { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)(uint8_t)s[off + i] << (8 * i); return v; }

struct CmServer { std::string host; };
static bool fetch_cm_list(std::vector<CmServer>& out) {
    Resp r = http(false, "https://api.steampowered.com/ISteamDirectory/GetCMListForConnect/v1/?cellid=0&cmtype=websockets&format=json", "", "GetCMListForConnect");
    if (r.status != 200) return false;
    auto add_one = [&](winrt::Windows::Data::Json::JsonObject const& o) {
        std::string realm = o.HasKey(L"realm") ? to_string(o.GetNamedString(L"realm")) : "";
        std::string type = o.HasKey(L"type") ? to_string(o.GetNamedString(L"type")) : "";
        if (realm != "steamglobal" || type != "websockets" || !o.HasKey(L"endpoint")) return;
        std::string ep = to_string(o.GetNamedString(L"endpoint")); size_t c = ep.find(':');
        out.push_back({c == std::string::npos ? ep : ep.substr(0, c)});
    };
    try {
        auto root = winrt::Windows::Data::Json::JsonObject::Parse(to_hstring(r.body)).GetNamedObject(L"response");
        if (!root.HasKey(L"serverlist")) return false;
        try { for (auto const& it : root.GetNamedArray(L"serverlist")) add_one(it.GetObject()); }
        catch (...) { for (auto const& kv : root.GetNamedObject(L"serverlist")) add_one(kv.Value().GetObject()); }
    } catch (...) { return false; }
    return !out.empty();
}

// cm_build_header/cm_build_client_logon were removed: steamdl.h's CmSession
// builds its own logon/header frames now (send_raw / cm_logon).

static std::string cm_build_frame(uint32_t emsg, const std::string& header, const std::string& body) {
    std::string f; put_u32le(f, emsg | PB_PROTOMASK); put_u32le(f, (uint32_t)header.size()); f += header; f += body; return f;
}
struct CmHeader { uint32_t emsg = 0; bool isProto = false; std::string headerBytes, bodyBytes; };
static bool cm_parse_frame(const std::string& f, CmHeader& h) {
    if (f.size() < 8) return false;
    uint32_t raw = get_u32le(f, 0); h.isProto = (raw & PB_PROTOMASK) != 0; h.emsg = raw & ~PB_PROTOMASK;
    if (!h.isProto) return false;   // phase 1 only speaks protobuf-framed messages (true for all CM websocket traffic)
    uint32_t hlen = get_u32le(f, 4); if (f.size() < (size_t)8 + hlen) return false;
    h.headerBytes = f.substr(8, hlen); h.bodyBytes = f.substr(8 + hlen); return true;
}

static bool gunzip(const std::string& in, std::string& out, size_t hint) {   // gzip or zlib-wrapped (auto-detect), Steam Multi payloads are gzip
    z_stream z{}; if (inflateInit2(&z, 16 + 15) != Z_OK) return false;
    out.clear(); out.resize(hint ? hint : in.size() * 4 + 1024);
    z.next_in = (Bytef*)in.data(); z.avail_in = (uInt)in.size(); size_t have = 0; int rc;
    for (;;) {
        if (have == out.size()) out.resize(out.size() * 2);
        z.next_out = (Bytef*)&out[have]; z.avail_out = (uInt)(out.size() - have);
        rc = inflate(&z, Z_NO_FLUSH); have = out.size() - z.avail_out;
        if (rc == Z_STREAM_END) break; if (rc != Z_OK && rc != Z_BUF_ERROR) { inflateEnd(&z); return false; }
        if (rc == Z_BUF_ERROR && z.avail_out != 0) { inflateEnd(&z); return false; }
    }
    inflateEnd(&z); out.resize(have); return true;
}
static void cm_flatten(const std::string& frame, std::vector<std::string>& out, int depth = 0) {   // expands EMsg Multi (1) recursively into individual frames
    CmHeader h; if (!cm_parse_frame(frame, h)) { out.push_back(frame); return; }
    if (h.emsg != EMSG_Multi || depth > 4) { out.push_back(frame); return; }
    auto f = pb_parse(h.bodyBytes); auto sz = pb_get(f, 1), body = pb_get(f, 2); if (!body) return;
    std::string raw;
    if (sz && sz->u) { if (!gunzip(body->s, raw, (size_t)sz->u)) { logf("cm: Multi gunzip failed"); return; } } else raw = body->s;
    size_t i = 0; while (i + 4 <= raw.size()) { uint32_t n = get_u32le(raw, i); i += 4; if (i + n > raw.size()) break; cm_flatten(raw.substr(i, n), out, depth + 1); i += n; }
}

struct CmConn {
    Sock::MessageWebSocket ws{nullptr};
    winrt::Windows::Storage::Streams::DataWriter writer{nullptr};
    std::mutex m; std::condition_variable cv;
    std::deque<std::string> inbox;
    bool closed = false;
    void start(const std::string& host) {
        ws = Sock::MessageWebSocket();
        ws.Control().MessageType(Sock::SocketMessageType::Binary);
        ws.MessageReceived([this](auto&&, Sock::MessageWebSocketMessageReceivedEventArgs const& args) {
            auto dr = args.GetDataReader(); uint32_t n = dr.UnconsumedBufferLength(); std::string s; s.resize(n);
            if (n) dr.ReadBytes(array_view<uint8_t>((uint8_t*)&s[0], (uint8_t*)&s[0] + n));
            { std::lock_guard<std::mutex> l(m); inbox.push_back(std::move(s)); } cv.notify_all();
        });
        ws.Closed([this](auto&&, Sock::WebSocketClosedEventArgs const&) { { std::lock_guard<std::mutex> l(m); closed = true; } cv.notify_all(); });
        ws.ConnectAsync(winrt::Windows::Foundation::Uri(to_hstring("wss://" + host + ":443/cmsocket/"))).get();
        writer = winrt::Windows::Storage::Streams::DataWriter(ws.OutputStream());
    }
    void send(const std::string& buf) { writer.WriteBytes(array_view<const uint8_t>((const uint8_t*)buf.data(), (const uint8_t*)buf.data() + buf.size())); writer.StoreAsync().get(); }
    bool recv(std::string& out, int timeoutMs) {
        std::unique_lock<std::mutex> l(m);
        if (!cv.wait_for(l, std::chrono::milliseconds(timeoutMs), [this] { return !inbox.empty() || closed; }) || inbox.empty()) return false;
        out = std::move(inbox.front()); inbox.pop_front(); return true;
    }
    void close() { try { ws.Close(1000, L"done"); } catch (...) {} }
};

struct CmResult { bool connected = false, gotResponse = false; int32_t eresult = 0, heartbeatSec = 0; std::string err, cm; };
// ---------------------------------------------------------------- token store (LocalState is private to the AppContainer; content never logged)
static std::wstring token_path() { return g_dir + L"\\steam_session.bin"; }
static bool load_token2(std::string& refresh, std::string& access) {   // file = refresh [\n access]
    std::string t; FILE* f = _wfopen((g_dir + L"\\steam_session.bin").c_str(), L"rb"); if (!f) return false; char b[8192]; size_t n = fread(b, 1, sizeof b, f); fclose(f); t.assign(b, n);
    size_t nl = t.find('\n'); refresh = t.substr(0, nl); access = nl == std::string::npos ? "" : t.substr(nl + 1); while (!access.empty() && (access.back() == '\n' || access.back() == '\r')) access.pop_back(); return refresh.size() > 20;
}
static void save_token(const std::string& t) { FILE* f = _wfopen(token_path().c_str(), L"wb"); if (f) { fwrite(t.data(), 1, t.size(), f); fclose(f); } logf("session token saved (%zu bytes, not shown)", t.size()); }
static void save_token2(const std::string& refresh, const std::string& access) { std::string t = refresh; if (!access.empty()) t += "\n" + access; save_token(t); }
static void delete_token() { DeleteFileW(token_path().c_str()); logf("session token deleted"); }
static bool file_exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// ---------------------------------------------------------------- Xbox-style screens (ui_launcher.h) + navigation (ui_nav.h)
#include "ui_nav.h"
#include "ui_launcher.h"

// The account's persona name for the sidebar profile row, parsed from
// ClientPersonaState field 4 (player_name) here, because steam_presence.h's
// Persona has no name field. Empty until Steam answers: the sidebar then
// shows the honest default, never an invented name.
static std::mutex g_personaM; static std::string g_personaName;
static std::string persona_name() { std::lock_guard<std::mutex> l(g_personaM); return g_personaName; }

// Selawik fonts for the ui:: toolkit, loaded once from the package's fonts/
// folder (build.sh copies out/ui-assets into layout/fonts/). The bytes must
// outlive the ui::Font, so they live in static storage next to it. Loading
// failure is logged once; the old bitmap screens stay drawable (font8x8
// text8() above) and the draw dispatcher falls back to them.
static std::vector<uint8_t> g_fontReg, g_fontSemi, g_fontBold;
static ui::Font g_font;
static bool g_fontOk = false;
static std::wstring exe_dir() {
    wchar_t self[2048]; DWORD n = GetModuleFileNameW(nullptr, self, 2048);
    if (!n || n >= 2048) return L".";
    std::wstring p(self, n); return p.substr(0, p.find_last_of(L"\\/"));
}
static bool load_font_file(const wchar_t* name, std::vector<uint8_t>& buf) {
    FILE* f = _wfopen((exe_dir() + L"\\fonts\\" + name).c_str(), L"rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 12) { fclose(f); return false; }
    buf.resize(size_t(n));
    bool ok = fread(buf.data(), 1, buf.size(), f) == buf.size();
    fclose(f); return ok;
}
static void load_fonts() {
    if (!load_font_file(L"selawk.ttf", g_fontReg) || !load_font_file(L"selawksb.ttf", g_fontSemi) ||
        !load_font_file(L"selawkb.ttf", g_fontBold)) {
        logf("fonts: selawk.ttf/selawksb.ttf/selawkb.ttf not found beside the launcher; bitmap text stays");
        return;
    }
    g_fontOk = g_font.load(ui::Face::Regular, g_fontReg.data(), g_fontReg.size()) &&
               g_font.load(ui::Face::Semibold, g_fontSemi.data(), g_fontSemi.size()) &&
               g_font.load(ui::Face::Bold, g_fontBold.data(), g_fontBold.size());
    logf("fonts: ui::Font %s (three Selawik faces)", g_fontOk ? "loaded" : "load FAILED; bitmap text stays");
}

#include "steamdl.h"
#include "storage_report.h"

// ---------------------------------------------------------------- library model + covers
struct Game { uint32_t appid = 0; std::string name; uint32_t playtime = 0; uint64_t lastPlayed = 0; std::atomic<int> cover{0}; int iw = 0, ih = 0; std::vector<uint32_t> pix; };
static const int COVER_W = 260, COVER_H = 390;
static std::mutex g_gm;
static std::vector<std::shared_ptr<Game>> g_games;
static std::atomic<int> g_ownedCount{0};
static std::atomic<bool> g_demo{false};

static bool wic_decode_buf(const std::string& jpg, std::vector<uint32_t>& pix, int& w, int& h, int maxW, int maxH, int& hrOut) {
    static thread_local com_ptr<IWICImagingFactory> fac;
    if (!fac) {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), fac.put_void());
        if (FAILED(hr)) { static const GUID f2 = {0x317d06e8, 0x5f24, 0x433d, {0xbd, 0xf7, 0x79, 0xce, 0x68, 0xd8, 0xab, 0xc2}}; hr = CoCreateInstance(f2, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), fac.put_void()); }
        if (FAILED(hr)) { hrOut = hr; return false; }
    }
    com_ptr<IWICStream> st; com_ptr<IWICBitmapDecoder> dec; com_ptr<IWICBitmapFrameDecode> fr; com_ptr<IWICFormatConverter> cv; com_ptr<IWICBitmapScaler> sc;
    HRESULT hr;
    if (FAILED(hr = fac->CreateStream(st.put())) || FAILED(hr = st->InitializeFromMemory((BYTE*)jpg.data(), (DWORD)jpg.size())) ||
        FAILED(hr = fac->CreateDecoderFromStream(st.get(), nullptr, WICDecodeMetadataCacheOnDemand, dec.put())) || FAILED(hr = dec->GetFrame(0, fr.put()))) { hrOut = hr; return false; }
    UINT sw = 0, sh = 0; fr->GetSize(&sw, &sh); if (!sw || !sh) { hrOut = E_FAIL; return false; }
    double s = std::min((double)maxW / sw, (double)maxH / sh); UINT tw = std::max(1, (int)(sw * s)), th = std::max(1, (int)(sh * s));
    if (FAILED(hr = fac->CreateFormatConverter(cv.put())) || FAILED(hr = cv->Initialize(fr.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(hr = fac->CreateBitmapScaler(sc.put())) || FAILED(hr = sc->Initialize(cv.get(), tw, th, WICBitmapInterpolationModeFant))) { hrOut = hr; return false; }
    pix.assign((size_t)tw * th, 0);
    if (FAILED(hr = sc->CopyPixels(nullptr, tw * 4, (UINT)pix.size() * 4, (BYTE*)pix.data()))) { hrOut = hr; return false; }
    w = tw; h = th; hrOut = 0; return true;
}
static bool wic_decode(const std::string& jpg, Game& g, int& hrOut, int maxW = COVER_W, int maxH = COVER_H) {
    return wic_decode_buf(jpg, g.pix, g.iw, g.ih, maxW, maxH, hrOut);
}
// Lazy covers: the render thread asks for covers of visible rows (+1 row margin) and evicts the rest.
// cover: 0 = not loaded, 3 = queued/loading, 1 = ready, 2 = failed
static std::atomic<bool> g_quit{false};
static std::mutex g_qm; static std::condition_variable g_qcv; static std::deque<std::shared_ptr<Game>> g_q;
static void cover_worker() {
    init_apartment(apartment_type::multi_threaded);
    for (;;) {
        std::shared_ptr<Game> g;
        { std::unique_lock<std::mutex> l(g_qm); g_qcv.wait(l, [] { return !g_q.empty() || g_quit; }); if (g_quit) return; g = g_q.front(); g_q.pop_front(); }
        const char* kinds[] = {"library_600x900.jpg", "header.jpg"}; bool ok = false;
        for (int k = 0; k < 2 && !ok; k++) {
            std::string url = "https://cdn.cloudflare.steamstatic.com/steam/apps/" + std::to_string(g->appid) + "/" + kinds[k];
            Resp r = http(false, url, "", "cover");
            if (r.status != 200 || r.body.size() < 100) continue;
            int hr = 0; if (wic_decode(r.body, *g, hr)) ok = true; else logf("cover appid=%u %s WIC decode failed hr=0x%08x", g->appid, kinds[k], (unsigned)hr);
        }
        if (!ok) { g->pix.clear(); g->pix.shrink_to_fit(); }
        g->cover = ok ? 1 : 2;
    }
}
static void start_covers() { static bool started = false; if (started) return; started = true; for (int i = 0; i < 4; i++) std::thread(cover_worker).detach(); }
#include "store.h"

static std::atomic<int> g_sortMode{0};
static const char* SORT_NAMES[] = {"Most played", "Recently played", "Alphabetical"};
static std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static void sort_games(int mode) {   // caller holds g_gm
    logf("grid list resort/reload mode=%d count=%zu (position unchanged)", mode, g_games.size());
    std::stable_sort(g_games.begin(), g_games.end(), [mode](const std::shared_ptr<Game>& a, const std::shared_ptr<Game>& b) {
        if (mode == 0) return a->playtime > b->playtime;
        if (mode == 1) return a->lastPlayed > b->lastPlayed;
        return lower(a->name) < lower(b->name); });
}

// ---------------------------------------------------------------- app state + network thread
enum Screen { S_BOOT, S_LOGIN, S_LOADING, S_LIBRARY, S_DETAIL, S_QUEUE, S_STORAGE };
static std::atomic<int> g_screen{S_BOOT};
static std::mutex g_sm;                       // guards the strings below
static std::string g_status = "Starting...", g_qrUrl, g_err;
static std::atomic<bool> g_scanned{false}, g_refreshReq{false}, g_logoutReq{false}, g_reloadReq{false};
static std::atomic<double> g_qrStart{0};
static std::atomic<int> g_qrGen{0};
static void set_status(const std::string& s) { std::lock_guard<std::mutex> l(g_sm); g_status = s; }

static bool fetch_library(const std::string& access, const std::string& steamid) {
    set_status("Loading your games...");
    std::string url = API + "IPlayerService/GetOwnedGames/v1/?access_token=" + urlenc(access) + "&steamid=" + steamid + "&include_appinfo=true&include_played_free_games=true&format=json";
    Resp r = http(false, url, "", "GetOwnedGames");
    if (r.status != 200) return false;
    try {
        auto root = winrt::Windows::Data::Json::JsonObject::Parse(to_hstring(r.body)).GetNamedObject(L"response");
        int count = (int)root.GetNamedNumber(L"game_count", 0);
        std::vector<std::shared_ptr<Game>> v;
        if (root.HasKey(L"games")) for (auto const& it : root.GetNamedArray(L"games")) {
            auto o = it.GetObject(); auto g = std::make_shared<Game>(); g->appid = (uint32_t)o.GetNamedNumber(L"appid");
            g->name = o.HasKey(L"name") ? to_string(o.GetNamedString(L"name")) : "App " + std::to_string(g->appid);
            g->playtime = (uint32_t)o.GetNamedNumber(L"playtime_forever", 0); g->lastPlayed = (uint64_t)o.GetNamedNumber(L"rtime_last_played", 0); v.push_back(std::move(g));
        }
        g_ownedCount = count; { std::lock_guard<std::mutex> l(g_gm); g_games = std::move(v); sort_games(g_sortMode); }
        logf("library: owned=%d listed=%zu (all, sorted on screen)", count, g_games.size());
        return true;
    } catch (...) { logf("library: JSON parse failed"); return false; }
}
static void demo_library() {
    static const struct { uint32_t id; const char* n; } d[] = {{3405340, "Megabonk"}, {1145360, "Hades"}, {367520, "Hollow Knight"}, {504230, "Celeste"}, {413150, "Stardew Valley"}, {1794680, "Vampire Survivors"}, {2379780, "Balatro"}, {588650, "Dead Cells"}, {105600, "Terraria"}, {620, "Portal 2"}, {70, "Half-Life"}, {440, "Team Fortress 2"}, {1091500, "Cyberpunk 2077"}, {892970, "Valheim"}};
    std::vector<std::shared_ptr<Game>> v; for (auto& e : d) { auto g = std::make_shared<Game>(); g->appid = e.id; g->name = e.n; v.push_back(std::move(g)); }
    g_ownedCount = (int)v.size(); { std::lock_guard<std::mutex> l(g_gm); g_games = std::move(v); }
}
static void enter_library(const std::string& access, const std::string& steamid) {
    g_screen = S_LOADING;
    if (!fetch_library(access, steamid)) { { std::lock_guard<std::mutex> l(g_sm); g_err = "Could not load your games (see boot.log)"; } g_screen = S_LIBRARY; return; }
    { std::lock_guard<std::mutex> l(g_sm); g_err.clear(); }
    start_covers(); g_screen = S_LIBRARY;
}
// ---- library over the CM session (Player.GetOwnedGames), plus the persistent sessions used by the downloader
static std::shared_ptr<CmSession> g_cmUser;      // logged-in account session (null until restored/QR-approved)
static std::mutex g_cmUserM;
static std::atomic<bool> g_authResolved{false};  // true once we know whether a user session exists (so autodownload may fall back to anonymous)
static std::shared_ptr<CmSession> user_session() { std::lock_guard<std::mutex> l(g_cmUserM); return g_cmUser; }
static void set_user_session(std::shared_ptr<CmSession> s) { std::lock_guard<std::mutex> l(g_cmUserM); g_cmUser = std::move(s); }

static bool fetch_library_cm(CmSession& cm) {
    set_status("Loading your games...");
    std::string b; pb_u(b, 1, cm.steamid); pb_u(b, 2, 1); pb_u(b, 3, 1); pb_u(b, 5, 1); pb_s(b, 7, "english");
    JobMsg m; if (!cm.service("Player.GetOwnedGames#1", b, 30000, m)) { logf("library(cm): request timed out"); return false; }
    if (m.eresult != 1) { logf("library(cm): eresult=%d", m.eresult); return false; }
    uint32_t count = 0; std::vector<std::shared_ptr<Game>> v;
    for (auto& f : pb_parse(m.body)) {
        if (f.f == 1) count = (uint32_t)f.u;
        else if (f.f == 2) {
            auto g = std::make_shared<Game>(); std::string nm;
            for (auto& x : pb_parse(f.s)) { if (x.f == 1) g->appid = (uint32_t)x.u; else if (x.f == 2) nm = x.s; else if (x.f == 4) g->playtime = (uint32_t)x.u; else if (x.f == 11) g->lastPlayed = x.u; }
            g->name = nm.empty() ? "App " + std::to_string(g->appid) : nm; v.push_back(std::move(g));
        }
    }
    g_ownedCount = (int)count; { std::lock_guard<std::mutex> l(g_gm); g_games = std::move(v); sort_games(g_sortMode); }
    logf("library(cm): owned=%u listed=%zu (all, sorted on screen)", count, g_games.size());
    { std::string t; { std::lock_guard<std::mutex> l(g_gm); for (auto& g : g_games) t += std::to_string(g->appid) + "\t" + g->name + "\n"; }   // test aid: LocalState\library.txt (appid<TAB>name)
      FILE* f = _wfopen((g_dir + L"\\library.txt").c_str(), L"wb"); if (f) { fwrite(t.data(), 1, t.size(), f); fclose(f); } }
    return true;
}
static void enter_library_cm(CmSession& cm) {
    g_screen = S_LOADING;
    if (!fetch_library_cm(cm)) { { std::lock_guard<std::mutex> l(g_sm); g_err = "Could not load your games (see boot.log)"; } g_screen = S_LIBRARY; return; }
    { std::lock_guard<std::mutex> l(g_sm); g_err.clear(); }
    start_covers(); g_screen = S_LIBRARY;
}

// Persona name for the sidebar profile row (field 4 of ClientPersonaState's
// friends[] entry; steam_presence.h's Persona has no name field). Runs on the
// CM reader thread. The keeper in launch.h replaces the push handler when a
// game launches; the launcher restarts when the game ends, so the name keeps
// working for every library screen.
static void persona_push(const JobMsg& m) {
    if (m.emsg != steam_presence::ClientPersonaState) return;
    uint64_t self = 0; { auto s = user_session(); if (s) self = s->steamid; }
    if (!self) return;
    for (auto& friendField : pb_parse(m.body)) {
        if (friendField.f != 2) continue;
        uint64_t id = 0; std::string name;
        for (auto& f : pb_parse(friendField.s)) {
            if (f.f == 1) id = f.u;
            else if (f.f == 4) name = f.s;
        }
        if (id == self && !name.empty()) {
            std::lock_guard<std::mutex> l(g_personaM);
            if (g_personaName != name) { g_personaName = name; logf("persona: name updated (%zu chars, not logged)", name.size()); }
            return;
        }
    }
}
// Installs the persona push handler on a freshly (re-)established user
// session, then asks Steam for this account's own persona state.
static void adopt_user_session(std::shared_ptr<CmSession> s) {
    set_user_session(s);
    if (!s || !s->up) return;
    s->set_push(persona_push);
    try { s->send_raw(steam_presence::ClientRequestFriendData, steam_presence::persona_request_body(s->steamid), 0, ""); } catch (...) {}
}

// ---- download queue (A on a game, or LocalState\autodownload.txt)
static std::mutex g_dqm; static std::condition_variable g_dqcv; static std::deque<uint32_t> g_dq;
static void enqueue_download(uint32_t appid) { { std::lock_guard<std::mutex> l(g_dqm); if (std::find(g_dq.begin(), g_dq.end(), appid) != g_dq.end()) return; g_dq.push_back(appid); } g_dqcv.notify_all(); logf("download queued: appid %u", appid); }
static bool is_queued(uint32_t appid) { std::lock_guard<std::mutex> l(g_dqm); return std::find(g_dq.begin(), g_dq.end(), appid) != g_dq.end(); }
// Install folders: one download or one delete (game_delete.h) at a time, never both,
// because another app may share the folder and its marker must not appear while the
// folder is removed. Both flags live under g_dqm, so popping a queued job and claiming
// the folders is one step, and a delete only starts with nothing running or queued.
static bool g_fsDownloading = false, g_fsDeleting = false;   // guarded by g_dqm
static bool begin_download() {   // autodownload.txt path: wait out a running delete
    std::unique_lock<std::mutex> l(g_dqm);
    while (g_fsDeleting && !g_quit) g_dqcv.wait_for(l, std::chrono::milliseconds(200));
    if (g_quit) return false; g_fsDownloading = true; return true;
}
static void end_download() { { std::lock_guard<std::mutex> l(g_dqm); g_fsDownloading = false; } g_dqcv.notify_all(); }
static bool try_begin_delete() { std::lock_guard<std::mutex> l(g_dqm); if (g_fsDownloading || g_fsDeleting || !g_dq.empty()) return false; g_fsDeleting = true; return true; }
static void end_delete() { { std::lock_guard<std::mutex> l(g_dqm); g_fsDeleting = false; } g_dqcv.notify_all(); }
static bool deleting_now() { std::lock_guard<std::mutex> l(g_dqm); return g_fsDeleting; }
static std::set<uint32_t> g_dqCancel;   // guarded by g_dqm: popped by the queue thread, cancelled before it started
static bool cancel_queued(uint32_t appid) {   // A on the detail page: drop a job that has not started
    std::lock_guard<std::mutex> l(g_dqm);
    auto it = std::find(g_dq.begin(), g_dq.end(), appid);
    if (it != g_dq.end()) { g_dq.erase(it); return true; }
    if (g_fsDownloading) g_dqCancel.insert(appid);   // popped, but download_app has not reset g_dl.cancel yet
    return false;
}
static bool take_queued_cancel(uint32_t appid) { std::lock_guard<std::mutex> l(g_dqm); return g_dqCancel.erase(appid) != 0; }
static std::atomic<int> g_dlSeq{0}; static std::atomic<uint32_t> g_dlLastApp{0}; static std::atomic<bool> g_dlLastOk{false};   // bumped when a queued download finishes (detail page refreshes, never launches)
static std::shared_ptr<CmSession> g_cmAnon;
static std::shared_ptr<CmSession> session_for_download() {
    auto u = user_session(); if (u && u->up) return u;
    if (g_cmAnon && g_cmAnon->up) return g_cmAnon;
    int er; std::string err; auto a = cm_logon("", er, err); if (a) g_cmAnon = a; else logf("dl: anonymous logon failed eresult=%d %s", er, err.c_str());
    return a;
}
static void dl_thread() {
    init_apartment(apartment_type::multi_threaded);
    if (begin_download()) {   // downloads cut off by a crash or power loss: remove their files (installs.h)
        for (auto& w : installs::sweep_partials(g_dir, &g_quit)) logf("dl: unfinished download of appid %u (%s) from an earlier run: %s", w.appid, game_library::join_root("LocalState", w.root, w.dir).c_str(), installs::discard_name(w.result));
        // launcher-owned leftovers (old logs, caches): small, but they are ours to remove
        std::set<uint32_t> inst; for (auto& i : installs::scan(g_dir, false)) inst.insert(i.first);
        auto left = installs::launcher_leftovers(g_dir, inst);
        uint64_t before = 0; for (auto& l : left) before += l.bytes;
        uint64_t freed = installs::clean_leftovers(g_dir, inst);
        logf("storage: launcher leftovers: %zu files, %s; removed %s", left.size(), storage_roots::size_text(before).c_str(), storage_roots::size_text(freed).c_str());
        // every free-space figure for LocalState, once per start, for the console log and LocalState\storage_report.txt
        // (the other drives are probed, and reported, when the detail page opens)
        std::vector<storage_roots::Choice> roots = {storage_report::internal_root(g_dir)};
        std::string report = storage_report::write(g_dir, roots, inst);
        for (auto& line : storage_report::log_lines(report)) logf("storage: %s", line.c_str());
        end_download();
    }
    // LocalState\autodownload.txt: one appid per line; processed once the account state is known (user session, else anonymous)
    std::wstring ap = g_dir + L"\\autodownload.txt";
    if (file_exists(ap)) {
        FILE* f = _wfopen(ap.c_str(), L"rb"); std::string t; if (f) { char b[512]; size_t n = fread(b, 1, sizeof b, f); fclose(f); t.assign(b, n); }
        std::vector<uint32_t> ids; size_t i = 0; while (i < t.size()) { while (i < t.size() && !isdigit((unsigned char)t[i])) i++; size_t j = i; while (j < t.size() && isdigit((unsigned char)t[j])) j++; if (j > i) ids.push_back((uint32_t)strtoul(t.substr(i, j - i).c_str(), nullptr, 10)); i = j; }
        logf("autodownload.txt: %zu appids", ids.size());
        for (int k = 0; k < 600 && !g_authResolved && !g_quit; k++) Sleep(100);
        bool allOk = true;
        for (uint32_t id : ids) {
            if (g_quit || !begin_download()) break;
            auto s = session_for_download();
            if (!s) { allOk = false; logf("autodownload: no CM session for appid %u", id); }
            else if (!download_app(*s, id)) allOk = false;
            end_download();
        }
        logf("autodownload: finished (%s)", allOk ? "all ok" : "with failures");
        if (allOk) MoveFileExW(ap.c_str(), (g_dir + L"\\autodownload.done.txt").c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    while (!g_quit) {
        uint32_t id = 0;
        { std::unique_lock<std::mutex> l(g_dqm); g_dqcv.wait_for(l, std::chrono::milliseconds(500), [] { return (!g_dq.empty() && !g_fsDeleting) || g_quit; });
          if (g_quit || g_dq.empty() || g_fsDeleting) continue; id = g_dq.front(); g_dq.pop_front(); g_fsDownloading = true; }
        auto s = session_for_download(); if (!s) { g_dl.appid = id; g_dl.state = 3; dl_msg("Cannot reach Steam"); g_dlLastApp = id; g_dlLastOk = false; take_queued_cancel(id); end_download(); g_dlSeq++; continue; }
        bool dok = download_app(*s, id, take_queued_cancel(id)); end_download(); g_dlLastApp = id; g_dlLastOk = dok; g_dlSeq++;
    }
}

#include "launch.h"
#include "launch_picker.h"
#include "launch_report.h"
#include "selftest.h"
#include "guest_overlay.h"
#include "ui_unlocks.h"
#include "../uwph/user32/corewindow_feed.h"
#include "game_delete.h"
#include "apibadge.h"

// The in-game guide (guest_overlay.h) loads the same three Selawik TTFs from
// their package paths; without them it falls back to its 8x8 bitmap text.
static void load_guide_fonts() {
    const std::wstring dir = exe_dir() + L"\\fonts\\";
    const std::wstring paths[3] = {dir + L"selawk.ttf", dir + L"selawksb.ttf", dir + L"selawkb.ttf"};
    const wchar_t* files[3] = {paths[0].c_str(), paths[1].c_str(), paths[2].c_str()};
    logf("fonts: guide Selawik %s", guest_overlay::load_fonts(files, 3) ? "loaded" : "unavailable; 8x8 text stays");
}

// ---- Guide achievements (ui_unlocks.h + guest_overlay.h) --------------------
// launch.h's achievements_sync calls guide_achievements_prepared() after it
// prepared gbe's files (schema + the player's merged earned set); the hook
// fills the guide's list and starts the CDN icon fetches. While the guest
// runs, a 2 s poller diffs <saves>/<appid>/achievements.json and pushes a
// toast per newly earned achievement. The store() upload to the player's
// real Steam account (launch.h's final_sync) is untouched by all of this.
static std::mutex g_guideAchM;
static steam_achievements::Schema g_guideSchema;                     // guarded by g_guideAchM
static std::vector<ui_guide::AchievementView> g_guideViews;          // guarded by g_guideAchM
static std::atomic<unsigned> g_achPollGen{0};                        // retires the poller
static std::atomic<bool> g_achPollRunning{false};

static void guide_icon_fetch(uint32_t appid, size_t index, const std::string& url, bool unlockedIcon) {
    // Plain public CDN image, decoded with WIC like the cover fetch; never
    // blocks the launch and never fails it (one short line on an error).
    Resp r = http(false, url, "", "achicon");
    if (r.status != 200 || r.body.size() < 100) { logf("achicon: appid=%u HTTP %d", appid, r.status); return; }
    int hr = 0;
    std::vector<uint32_t> pix;
    int w = 0, h = 0;
    if (!wic_decode_buf(r.body, pix, w, h, 64, 64, hr)) { logf("achicon: appid=%u WIC decode failed hr=0x%08x", appid, (unsigned)hr); return; }
    auto held = std::make_shared<std::vector<uint32_t>>(std::move(pix));
    guest_overlay::achievements.set_icon(index, held, w, h, unlockedIcon);
}

// launch.h hook (achievements_sync, before the game starts): build the guide's
// achievement views for the launched game. Icons arrive later, off-thread.
static void guide_achievements_prepared(uint32_t appid, const steam_achievements::Schema& schema,
                                        const std::map<std::string, steam_achievements::Earned>& earned) {
    std::vector<ui_guide::AchievementView> views = ui_unlocks::build_views(schema, earned);
    {   std::lock_guard<std::mutex> l(g_guideAchM);
        g_guideSchema = schema;
        g_guideViews = views;
    }
    guest_overlay::achievements.set_achievements(views);
    // Icon pixels: unlocked icon on unlocked rows, grey icon on locked ones.
    for (size_t idx = 0; idx < schema.achievements.size() && idx < views.size(); ++idx) {
        const auto& a = schema.achievements[idx];
        const bool unlockedIcon = views[idx].unlocked;
        const std::string hash = unlockedIcon && !a.icon.empty() ? a.icon : a.iconGray;
        std::string url = ui_unlocks::icon_url(appid, hash);
        if (url.empty()) continue;
        std::thread([appid, idx, url, unlockedIcon] { guide_icon_fetch(appid, idx, url, unlockedIcon); }).detach();
    }
}

// Polls gbe's saves file while the guest runs (gbe writes earned:true +
// earned_time there on every unlock); one detached thread per launch, 2 s.
static void achievement_poller(uint32_t appid) {
    namespace fs = std::filesystem;
    const fs::path saves = fs::path(provider_dir(appid)) / "saves" / std::to_string(appid) / "achievements.json";
    const unsigned gen = ++g_achPollGen;
    g_achPollRunning = true;
    std::map<std::string, steam_achievements::Earned> seen;
    {   std::lock_guard<std::mutex> l(g_guideAchM);
        for (const auto& v : g_guideViews) if (v.unlocked) seen[v.name] = {true, 1};   // time unknown here; only the earned bit is compared
    }
    long long mtime = -1;
    while (!g_quit && g_achPollGen.load() == gen) {
        Sleep(2000);
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        if (!GetFileAttributesExW(saves.c_str(), GetFileExInfoStandard, &fa)) continue;
        const long long ft = (static_cast<long long>(fa.ftLastWriteTime.dwHighDateTime) << 32) | fa.ftLastWriteTime.dwLowDateTime;
        if (ft == mtime) continue;                     // unchanged: no re-parse
        mtime = ft;
        auto text = steam_achievements::read(saves);
        if (!text) continue;
        auto now = steam_achievements::parse_earned(*text);
        for (const auto& name : ui_unlocks::newly_earned(seen, now)) {
            steam_achievements::Schema schema; std::vector<ui_guide::AchievementView> views;
            {   std::lock_guard<std::mutex> l(g_guideAchM);
                schema = g_guideSchema; views = g_guideViews;
            }
            const int idx = ui_unlocks::index_of(schema, views, name);
            if (idx < 0) continue;
            const auto& e = now[name];
            const std::string date = e.time ? ui_guide::format_date(static_cast<long long>(e.time)) : std::string();
            const auto& sa = schema.achievements[size_t(idx)];
            const std::string realName = sa.display.empty() ? sa.name : sa.display;
            ui_guide::Toast t = guest_overlay::achievements.unlock(size_t(idx), realName, sa.description, date, now_s());
            guest_overlay::push_toast(t);
            // The toast's icon: the unlocked CDN image, fetched off-thread.
            const std::string url = ui_unlocks::icon_url(appid, sa.icon);
            if (!url.empty()) std::thread([appid, idx, url] { guide_icon_fetch(appid, size_t(idx), url, true); }).detach();
            logf("achievements: unlocked \"%s\"", realName.c_str());
        }
        seen = std::move(now);
    }
    g_achPollRunning = false;
}
// Stops the poller (the guest is exiting); the thread notices on its next tick.
static void achievement_poller_stop() { ++g_achPollGen; }

// definitive refusals mean the saved token is bad; anything else (network, CM busy) keeps the token and retries
#include "session_restore.h"
// Remove only the refresh token actually refused by CM; retain any separately stored access token.
static void clear_rejected_token(const std::string& rejected) {
    bool cleared = session_restore::clear_rejected(rejected, load_token2, save_token2, delete_token);
    logf(cleared ? "session restore: rejected refresh token cleared (other LocalState retained)"
                 : "session restore: rejected token no longer stored; storage unchanged");
}
static session_restore::Result restore_result(bool connected, int er, std::string& refresh) {
    return session_restore::complete(connected, er, refresh, [&](const std::string& rejected) {
        clear_rejected_token(rejected);
    }, [&] {
        logf("session restore: CM rejected refresh token (eresult %d); showing QR with Refresh focused", er);
        g_screen = S_LOGIN;
    });
}
// LocalState\mkdirs.txt: one relative directory per line; created (with parents), then renamed to mkdirs.done (Device Portal upload cannot create dirs)
static void run_mkdirs() {
    std::wstring mp = g_dir + L"\\mkdirs.txt"; if (!file_exists(mp)) return;
    FILE* f = _wfopen(mp.c_str(), L"rb"); if (!f) return;
    std::string all; char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) all.append(b, n); fclose(f);
    int made = 0, existed = 0, rejected = 0, failed = 0; size_t pos = 0;
    while (pos <= all.size()) {
        size_t e = all.find('\n', pos); if (e == std::string::npos) e = all.size();
        std::string line = all.substr(pos, e - pos); pos = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t st = 0; while (st < line.size() && (line[st] == ' ' || line[st] == '\t')) st++; line = line.substr(st);
        if (line.empty()) continue;
        for (auto& c : line) if (c == '/') c = '\\';
        bool bad = line.size() > 240 || line[0] == '\\' || line.find(':') != std::string::npos || line.find("..") != std::string::npos;
        for (unsigned char c : line) if (c < 32 || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') bad = true;
        if (bad) { rejected++; logf("mkdirs: rejected a path"); continue; }
        std::wstring w(line.begin(), line.end()), cur = g_dir; size_t p = 0; bool ok = true, any = false;
        while (p < w.size()) {
            size_t q = w.find(L'\\', p); if (q == std::wstring::npos) q = w.size();
            if (q > p) { cur += L"\\" + w.substr(p, q - p); if (CreateDirectoryW(cur.c_str(), nullptr)) any = true; else if (GetLastError() != ERROR_ALREADY_EXISTS) { ok = false; break; } }
            p = q + 1;
        }
        if (!ok) { failed++; logf("mkdirs: failed (err %lu) for %s", GetLastError(), line.c_str()); } else if (any) made++; else existed++;
    }
    logf("mkdirs: created %d, already existed %d, rejected %d, failed %d", made, existed, rejected, failed);
    MoveFileExW(mp.c_str(), (g_dir + L"\\mkdirs.done").c_str(), MOVEFILE_REPLACE_EXISTING);
}
static void net_thread() {
    init_apartment(apartment_type::multi_threaded);
    run_mkdirs();
    std::string refresh, access;
    std::thread(dl_thread).detach();
    if (file_exists(g_dir + L"\\demo.txt")) {   // preview mode (no account): fixed public app ids, clearly labelled on screen
        g_demo = true; logf("DEMO mode (demo.txt present): not using any Steam account"); demo_library(); start_covers(); g_screen = S_LIBRARY; g_authResolved = true;
        while (!g_quit) Sleep(200); return;
    }
    std::string savedAccess; double lastRelog = 0;
    if (load_token2(refresh, savedAccess)) {
        set_status("Restoring Steam session...");
        for (int attempt = 0; !g_quit; attempt++) {
            int er = 0; std::string err; auto s = cm_logon(refresh, er, err);
            auto result = restore_result(bool(s), er, refresh);
            if (result == session_restore::Result::restored) { logf("session restored via CM logon (refresh token)"); adopt_user_session(s); enter_library_cm(*s); break; }
            if (result == session_restore::Result::qr) break;
            set_status(er == 84 ? "Steam asks to wait - retrying (saved login kept)..." : "Cannot reach Steam - retrying (saved login kept)...");
            logf("session restore: transient failure (eresult %d %s), retry %d", er, err.c_str(), attempt + 1);
            for (int k = 0; k < (er == 84 ? 600 : 80) && !g_quit; k++) Sleep(100);
        }
    }
    g_authResolved = true;
    while (!g_quit) {
        if (g_screen == S_LIBRARY || g_screen == S_LOADING || g_screen == S_DETAIL || g_screen == S_QUEUE || g_screen == S_STORAGE) {
            if (g_logoutReq.exchange(false)) { delete_token(); if (auto u = user_session()) { u->close(); set_user_session(nullptr); } g_screen = S_LOGIN; refresh.clear(); access.clear(); { std::lock_guard<std::mutex> l(g_gm); g_games.clear(); } continue; }
            if (!refresh.empty() && !g_demo) {
                auto u = user_session();
                bool want = g_reloadReq.exchange(false);
                if ((!u || !u->up) && now_s() - lastRelog > 20) {   // session dropped: log on again with the same refresh token
                    lastRelog = now_s(); int er = 0; std::string err; auto s = cm_logon(refresh, er, err);
                    if (s) { logf("session re-established via CM logon"); adopt_user_session(s); u = s; want = true; }
                    else if (restore_result(false, er, refresh) == session_restore::Result::qr) continue;
                }
                if (want && u && u->up) { { std::lock_guard<std::mutex> l(g_gm); g_games.clear(); } enter_library_cm(*u); }
            }
            Sleep(100); continue;
        }
        g_screen = S_LOGIN; g_scanned = false; g_refreshReq = false;
        QrSession q; set_status("Requesting QR code...");
        if (!begin_qr(q)) { set_status("Cannot reach Steam (HTTPS). Retrying in 10 s - or press A/X"); for (int i = 0; i < 100 && !g_refreshReq && !g_quit; i++) Sleep(100); continue; }
        { std::lock_guard<std::mutex> l(g_sm); g_qrUrl = q.url; g_status = "Scan with the Steam mobile app, then approve"; }
        g_qrStart = now_s(); g_qrGen++; logf("QR session started (clientId set, interval %.1fs)", q.interval);
        for (;;) {
            for (int i = 0; i < (int)(q.interval * 10) && !g_refreshReq && !g_quit; i++) Sleep(100);
            if (g_quit || g_refreshReq.exchange(false)) { logf("QR refresh requested"); break; }
            if (now_s() - g_qrStart > 270) { logf("QR auto-refresh before expiry"); break; }
            PollRes pr = poll_qr(q, refresh, access);
            { std::lock_guard<std::mutex> l(g_sm); if (g_qrUrl != q.url) { g_qrUrl = q.url; g_qrStart = now_s(); g_qrGen++; } }
            if (pr == POLL_SCANNED && !g_scanned) { g_scanned = true; set_status("Scanned - approve the sign-in on your phone"); logf("QR scanned, waiting for approval"); }
            if (pr == POLL_FAIL) { logf("poll failed -> new QR"); break; }
            if (pr == POLL_OK) {
                if (access.empty()) gen_access(refresh, access);
                save_token2(refresh, access); logf("login approved");
                int er = 0; std::string err; auto s = cm_logon(refresh, er, err);
                if (s) { adopt_user_session(s); enter_library_cm(*s); }
                else { logf("post-QR CM logon failed (eresult %d %s); trying HTTPS library", er, err.c_str()); enter_library(access, jwt_sub(refresh)); }
                g_authResolved = true; break;
            }
        }
    }
}

// ---------------------------------------------------------------- gamepad
struct Pad { GamepadButtons prev = GamepadButtons::None; double heldSince = 0, lastRep = 0; GamepadButtons held = GamepadButtons::None; bool stickEdge[4] = {}; };
static bool has(GamepadButtons b, GamepadButtons m) { return (b & m) != GamepadButtons::None; }

struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
    CoreWindow m_window{nullptr};
    CoreDispatcher m_graphicsDispatcher{nullptr};
    bool m_guestSurface=false;
    int m_guestEndGen=0;
    corewindow_input::Shared m_input; // keyboard/mouse to the guest: the record DLL's one feed, shared with guest SendInput
    bool m_inputLogged=false;
    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::UI::Popups::IUICommand> m_guestDialog{nullptr};
    inline static App* graphicsApp=nullptr;
    bool m_closed = false;
    com_ptr<ID3D11Device> m_dev; com_ptr<ID3D11DeviceContext> m_ctx; com_ptr<IDXGISwapChain1> m_sc; com_ptr<ID3D11Texture2D> m_bb;
    Pad m_pad; int m_prevPads = -1; uint64_t m_frame = 0;
    int m_sel = 0, m_top = 0; double m_scroll = 0, m_scrollT = 0; bool m_btnFocus = true, m_confirmLogout = false;
    bool m_onlyInstalled = false;                            // View: library shows installed games only
    ui_nav::Focus m_focus = ui_nav::Focus::grid;             // library: grid tiles or the sidebar
    int m_sidebar = 0;                                       // sidebar row the focus sits on
    bool m_autoUpdate = true;                                // queue screen pill (no update backend yet; memory only)
    std::set<uint32_t> m_installed; double m_installedT = -10; int m_installedDel = -1, m_installedDl = -1;
    void refresh_installed(double t) {                       // dl_<appid>.done markers; rescanned every 2 s and after a download/delete
        if (t - m_installedT < 2 && m_installedDel == g_del.gen && m_installedDl == g_dlSeq) return;
        m_installed = installed_appids(); m_installedT = t; m_installedDel = g_del.gen; m_installedDl = g_dlSeq;
    }
    std::vector<std::shared_ptr<Game>> shown_games() { return game_library::shown(g_games, m_onlyInstalled, m_installed); }   // caller holds g_gm
    static constexpr int COLS = ui_launcher::kGridCols;   // same 7 columns the grid draws

    void grid_position(int sel, int top, const char* cause) {
        if (sel != m_sel || top != m_top)
            logf("grid change cause=%s m_sel=%d->%d m_top=%d->%d", cause, m_sel, sel, m_top, top);
        m_sel = sel; m_top = top;
    }
    // Explicit one-shot commands only. Legacy marker files must never generate input.
    void autotest(double t) {
        static double next = 0;
        static std::deque<GamepadButtons> commands;
        if (g_screen != S_LIBRARY || t < next) return;
        next = t + 1.0;
        std::wstring path = g_dir + L"\\autotest.txt";
        if (FILE* f = _wfopen(path.c_str(), L"rb")) {
            char line[80]; unsigned count = 0;
            while (fgets(line, sizeof line, f) && count < 64) {
                if (std::string(line) == "down\n" || std::string(line) == "down\r\n" || std::string(line) == "down") {
                    commands.push_back(GamepadButtons::DPadDown); ++count;
                }
            }
            fclose(f);
            if (!DeleteFileW(path.c_str())) { commands.clear(); logf("autotest: consume failed; no input"); }
            else logf("autotest: consumed file; queued %u explicit Down commands (legacy marker ignored)", count);
        }
        if (!commands.empty()) {
            GamepadReading fake{}; fake.Buttons = commands.front(); commands.pop_front();
            logf("autotest.txt hook: explicit DPadDown");
            input(t, fake, true, "autotest.txt");
            input(t + 0.001, GamepadReading{}, true, "autotest.txt release");
        }
    }

    // ---------------------------------------------------------------- detail screen (store info + Install/Play)
    uint32_t m_dApp = 0; std::string m_dName, m_dNote; int m_dScroll = 0; std::shared_ptr<StoreInfo> m_dSi; bool m_confirmDelete = false;
    double m_dlStartT = -1e9;   // when this page started the install: A cancels a download only after detail_input::cancel_guard
    uint32_t m_dlWatch = 0; int m_dlWatchSeq = 0;   // the install this page started: on finish it refreshes, never launches
    game_library::DeleteKind m_delKind = game_library::DeleteKind::folder;   // decided when the confirm dialog opens
    InstallInfo m_dInst; double m_dInstT = -10;
    std::vector<storage_roots::Choice> m_roots; size_t m_rootSel = 0;   // install locations of the detail page (X cycles them before Install)
    // Launch-option picker (launch_picker.h): a game with several run options
    // asks before Play instead of picking one silently.
    launch_picker::State m_pick;
    // New-style detail focus (ui_nav icon slots): 0 primary, 1 trash, 2 cloud,
    // 3 cog; the cog opens the install-location cycle, the cloud opens the
    // cloud sheet (m_cloudOpen / m_cloudFocus).
    int m_dFocus = 0;
    bool m_cloudOpen = false; int m_cloudFocus = 0;
    std::string m_achName, m_achSub; float m_achPct = 0; bool m_focusAch = false;   // latest achievement (no live schema feed yet; stays off)
    std::string remembered_option(uint32_t id) {   // LocalState\launch_choice_<appid>.txt beside the install marker
        std::string text; return read_file_s(g_dir + L"\\" + widen(launch_picker::remember_name(id)), text) ? launch_picker::parse_remember(text) : std::string();
    }
    void remember_option(uint32_t id, const std::string& executable) {
        if (executable.empty()) return;
        write_text(g_dir + L"\\" + widen(launch_picker::remember_name(id)), launch_picker::format_remember(executable));
    }
    // The options Play may offer for a game, in preference order. appinfo is
    // the downloader's cache; without it there is nothing to ask.
    std::vector<launch_target::Choice> launch_options(uint32_t id) {
        Vdf info; const Vdf* app = nullptr;
        if (!load_appinfo(id, info, app)) return {};
        return launch_target::choices(launch_target::entries(*app));
    }
    // A: Play. One runnable option (or none: the largest .exe fallback) starts
    // directly; several open the picker.
    size_t m_optApp = 0, m_optCount = 0;   // cached: how many run options Play offers (draw_detail hint)
    size_t option_count() {
        if (m_optApp != m_dApp) { m_optApp = m_dApp; m_optCount = launch_options(m_dApp).size(); }
        return m_optCount;
    }
    void detail_play(const char* via) {
        m_dInst = read_install(m_dApp);
        std::vector<launch_target::Choice> rows = launch_options(m_dApp);
        if (!launch_picker::needed(rows)) { logf("detail: Play pressed appid=%u (%s)", m_dApp, via); if (std::string why = start_launch(m_dApp, m_dName, m_dInst.dir, via); !why.empty()) m_dNote = why; return; }
        launch_picker::open(m_pick, std::move(rows), remembered_option(m_dApp));
        logf("detail: Play opens the launch-option picker appid=%u options=%zu selected=%d (%s)", m_dApp, m_pick.rows.size(), m_pick.sel, via);
    }
    void detail_picker_a(const char* via) {   // A: start the highlighted option
        if (launch_picker::press_a(m_pick) != launch_picker::Press::start) { logf("detail: launch option refused appid=%u: %s (%s)", m_dApp, m_pick.note.c_str(), via); return; }
        std::string exe = launch_picker::started(m_pick);
        launch_picker::close(m_pick);
        remember_option(m_dApp, exe);
        logf("detail: Play pressed appid=%u option=\"%s\" (%s)", m_dApp, exe.c_str(), via);
        if (std::string why = start_launch(m_dApp, m_dName, m_dInst.dir, via, &exe); !why.empty()) m_dNote = why;
    }
    std::string game_name(uint32_t id) { std::lock_guard<std::mutex> l(g_gm); for (auto& g : g_games) if (g->appid == id) return g->name; return id == 480 ? "Spacewar" : id == 3405340 ? "Megabonk" : id == 1007 ? "Steamworks SDK Redist" : ""; }
    bool owned(uint32_t id) { std::lock_guard<std::mutex> l(g_gm); for (auto& g : g_games) if (g->appid == id) return true; return false; }
    void open_detail(uint32_t id, const std::string& nm, const char* via) {
        m_dApp = id; m_dName = nm.empty() ? "App " + std::to_string(id) : nm; m_dNote.clear(); m_dScroll = 0; m_dInstT = -10; m_confirmDelete = false; m_dlStartT = -1e9; m_dlWatch = 0;
        launch_picker::close(m_pick);   // a picker left open belongs to the previous game
        // After a restart that unloaded a failed game, its page says why (this session's launches show in the launch panel).
        if (g_launch.phase == 0) { std::string why = launch_report::failure(read_bytes(g_dir + L"\\launch_result.json"), id); if (!why.empty()) m_dNote = "Last launch failed: " + why; }
        m_roots.clear(); m_rootSel = 0; refresh_roots("detail open");
        m_optApp = 0;   // a fresh page: the run-option count is read again (a download may have cached appinfo)
        auto si = std::make_shared<StoreInfo>(); si->appid = id; m_dSi = si; std::thread(store_worker, si).detach();
        { InstallInfo ii = read_install(id); if (ii.installed && !g_demo) cloud_async(id, ii.folder, false); }   // status line: what the cloud holds
        m_confirmLogout = false; g_screen = S_DETAIL; logf("detail: open appid=%u name='%s' via=%s", id, m_dName.c_str(), via);
    }
    void close_detail(const char* why) { logf("detail: back to library (%s)", why); m_dSi.reset(); g_screen = S_LIBRARY; }
    int m_dInstDel = -1;   // g_del.gen seen by m_dInst: a finished delete refreshes it at once
    void refresh_install(double t) { if (t - m_dInstT > 0.5 || m_dInstDel != g_del.gen) { m_dInstDel = g_del.gen; m_dInst = read_install(m_dApp); m_dInstT = t; } }
    bool dl_is_mine() { return g_dl.appid == m_dApp && g_dl.state == 1; }
    bool deleting_mine() { return g_del.appid == m_dApp && g_del.state == 1; }
    void refresh_roots(const char* via) {   // internal + every usable extra drive; keeps the pick when its drive is still there
        std::string keep = m_rootSel < m_roots.size() ? m_roots[m_rootSel].root : chosen_install_root(m_dApp);
        std::vector<std::string> note; m_roots = installs::list_roots(g_dir, &note);
        std::string report = storage_report::write(g_dir, m_roots, installed_appids());   // WinRT figures + LocalState\storage_report.txt
        m_rootSel = storage_roots::index_of(m_roots, keep);
        std::string l; for (auto& c : m_roots) l += (l.empty() ? "" : "; ") + storage_roots::label_capacity(c);
        logf("storage: %s: %s", via, l.c_str()); for (auto& n : note) logf("storage: %s", n.c_str());
        for (auto& line : storage_report::log_lines(report)) logf("storage: %s", line.c_str());
    }
    void detail_storage(const char* via) {      // X (not installed): next install location
        if (m_dInst.installed || g_demo || dl_is_mine() || is_queued(m_dApp)) return;
        refresh_roots("refresh"); m_rootSel = storage_roots::cycle(m_rootSel, m_roots.size(), 1); m_dNote.clear();
        logf("detail: install location appid=%u -> %s (%s)", m_dApp, storage_roots::label_capacity(m_roots[m_rootSel]).c_str(), via);
    }
    void detail_delete(const char* via) {     // X: ask to delete an installed game
        m_dInst = read_install(m_dApp); m_dInstT = now_s();
        if (!m_dInst.installed || deleting_mine()) return;
        if (g_launch.phase == 1 || g_launch.phase == 3 || play_runtime::observed.load(std::memory_order_acquire) != nullptr) { m_dNote = "A game was started: reopen the app to delete."; return; }
        m_delKind = delete_kind_for(m_dApp, m_dInst.dir, m_dInst.root); m_dNote.clear(); m_confirmDelete = true; logf("detail: delete asked appid=%u (%s)", m_dApp, via);
    }
    void detail_delete_confirm(const char* via) {
        m_confirmDelete = false; std::string why = start_delete(m_dApp);
        if (!why.empty()) { m_dNote = why; logf("detail: delete not started appid=%u (%s): %s", m_dApp, via, why.c_str()); }
        else { m_dNote.clear(); m_dInstT = -10; }
    }
    void detail_cloud(const char* via, steam_cloud::Prefer prefer = steam_cloud::Prefer::Auto) {      // Y: sync; LB/RB after a conflict: use the cloud / this console's copy
        m_dInst = read_install(m_dApp); m_dInstT = now_s();
        if (!m_dInst.installed || g_demo) return;
        if (cloud_blocked()) { m_dNote = "A game was started: reopen the app to sync cloud saves."; return; }
        if (g_cloudBusy) { logf("detail: Y ignored, cloud pass already running"); return; }
        m_dNote.clear(); logf("detail: cloud sync asked appid=%u (%s)", m_dApp, via);
        cloud_async(m_dApp, m_dInst.folder, true, prefer);
    }
    void detail_action(const char* via) {   // A: Install, Play, or cancel a download at once
        if (g_launch.phase == 1 || g_launch.phase == 3) { logf("detail: A ignored, launch attempt running"); return; }
        m_dInst = read_install(m_dApp);
        if (deleting_mine()) { logf("detail: A ignored, delete running"); return; }
        detail_input::State st;
        st.installed = m_dInst.installed;
        st.download = dl_is_mine() ? detail_input::Download::running : is_queued(m_dApp) ? detail_input::Download::queued : detail_input::Download::none;
        st.sinceStart = now_s() - m_dlStartT;
        switch (detail_input::press_a(st)) {
        case detail_input::Action::ignore:   // the press that started the install cannot also cancel it
            logf("detail: A ignored, %.2f s after the install started (%s)", st.sinceStart, via); return;
        case detail_input::Action::cancel:   // the same cleanup as any other cancelled download (steamdl.h fail())
            m_dlWatch = 0;
            if (st.download == detail_input::Download::running) { g_dl.cancel = true; logf("detail: download cancel requested (A, %s)", via); }
            else {
                bool dropped = cancel_queued(m_dApp);
                if (!dropped && g_dl.appid == m_dApp && g_dl.state == 1) g_dl.cancel = true;   // the queue started it meanwhile
                logf("detail: queued download cancel (A, %s): %s", via, dropped ? "dropped from the queue" : "already started, cancel requested");
            }
            return;
        case detail_input::Action::play:
            m_dNote.clear(); detail_play(via); return;
        case detail_input::Action::install:
        case detail_input::Action::back:
            break;
        }
        if (g_demo) { m_dNote = "Demo list: installing is disabled."; return; }
        if (deleting_now()) { m_dNote = "Wait for the delete to finish."; return; }
        refresh_roots("install");
        const storage_roots::Choice& where = m_roots[m_rootSel];
        if (where.freeBytes < (64ull << 20)) {
            m_dNote = storage_roots::refusal_text(where, m_roots, m_rootSel);
            std::string one = m_dNote; for (auto& ch : one) if (ch == '\n') ch = ' ';
            logf("detail: Install refused appid=%u, %s", m_dApp, one.c_str()); return;
        }
        set_install_root(m_dApp, where.root);
        m_dlStartT = now_s(); m_dlWatch = m_dApp; m_dlWatchSeq = g_dlSeq; m_dNote.clear();
        logf("detail: Install pressed appid=%u (%s) to %s - no launch when it finishes", m_dApp, via, storage_roots::label(where).c_str()); enqueue_download(m_dApp);
    }
    void detail_back(const char* via) {         // B: back to the library; a download keeps running in the background
        m_dlWatch = 0;
        launch_picker::close(m_pick);
        close_detail(via);
    }
    void detail_tick(double t) {   // runs every frame on every screen: refresh after a download, launch watchdog
        if (m_dlWatch && g_dlSeq != m_dlWatchSeq) {
            m_dlWatchSeq = g_dlSeq;
            if (g_dlLastApp == m_dlWatch) {
                uint32_t app = m_dlWatch; m_dlWatch = 0; InstallInfo ii = read_install(app);
                bool here = g_screen == S_DETAIL && m_dApp == app;
                if (detail_input::after_download(g_dlLastOk, ii.installed) == detail_input::AfterDownload::refresh && here) {
                    m_dInst = ii; m_dInstT = t; m_dNote.clear();
                    logf("detail: download finished appid=%u -> installed, no launch (press A to play)", app);
                } else logf("detail: download finished appid=%u, nothing to refresh (ok=%d installed=%d onDetail=%d)", app, g_dlLastOk ? 1 : 0, ii.installed ? 1 : 0, here ? 1 : 0);
            }
        }
        lp_watchdog();
        guest_ended();
    }
    // ---- In-game launcher menu (guest_overlay.h): View+Menu toggles it.
    GamepadButtons m_menuPrev=GamepadButtons::None; double m_quitDeadline=0; bool m_leaving=false;
    static bool held(GamepadButtons b,GamepadButtons m){return (b&m)==m;}
    bool m_inputHooks=false;
    bool m_achPollStarted=false;   // the unlock poller runs while the guest does
    void guest_menu(double t,GamepadButtons b) {
        if(guest_overlay::inputBlocked.load(std::memory_order_relaxed))m_input.release_all(); // nothing stays held behind the menu
        if(!m_achPollStarted) {   // gbe writes achievements.json while the game runs; watch it from a background thread
            m_achPollStarted=true;
            std::thread(achievement_poller,m_dApp).detach();
        }
        if(!m_inputHooks) {
            auto pads=Gamepad::Gamepads();
            if(pads.Size()){m_inputHooks=true;logf("overlay: input capture hooks %s",guest_overlay::install_input_block(pads.GetAt(0))?"installed":"unavailable");}
        }
        // Unblock only once the menu is closed and every button is up.
        if(guest_overlay::inputBlocked&&!guest_overlay::menuOpen&&b==GamepadButtons::None){guest_overlay::block_input(false);logf("overlay: game input restored");}
        const auto combo=GamepadButtons::View|GamepadButtons::Menu;
        auto pressed=[&](GamepadButtons m){return held(b,m)&&!held(m_menuPrev,m);};
        if(held(b,combo)&&!held(m_menuPrev,combo))menu_command("toggle","controller");
        else if(guest_overlay::menuOpen&&!guest_overlay::busy) {
            // Guide navigation (ui_guide.h's state machine, guest_overlay.h's
            // atomics): D-pad moves the selection, LB/RB switch tabs, A
            // activates, B closes. D-pad left/right also switch tabs unless
            // the focused row uses them (the Settings toggles flip in place).
            using ui_guide::Button;
            const bool rowUsesLeftRight=guest_overlay::ui_guideTab.load()==2;
            if(pressed(GamepadButtons::DPadUp))guest_overlay::guide_press(Button::Up,rowUsesLeftRight);
            else if(pressed(GamepadButtons::DPadDown))guest_overlay::guide_press(Button::Down,rowUsesLeftRight);
            else if(pressed(GamepadButtons::LeftShoulder))guest_overlay::guide_press(Button::LB,rowUsesLeftRight);
            else if(pressed(GamepadButtons::RightShoulder))guest_overlay::guide_press(Button::RB,rowUsesLeftRight);
            else if(pressed(GamepadButtons::DPadLeft))guest_overlay::guide_press(Button::Left,rowUsesLeftRight);
            else if(pressed(GamepadButtons::DPadRight))guest_overlay::guide_press(Button::Right,rowUsesLeftRight);
            else if(pressed(GamepadButtons::A))menu_command("select","controller");
            else if(pressed(GamepadButtons::B))menu_command("toggle","controller");
        }
        m_menuPrev=b;
        frame_log(t);
        if(m_quitDeadline&&t>=m_quitDeadline&&!m_leaving) {
            m_quitDeadline=0;
            // Once the game has exited, its final save upload is running and
            // guest_ended() restarts afterwards: never cut that upload short.
            auto r=play_runtime::observed.load(std::memory_order_acquire);
            if(r&&r->finished.load(std::memory_order_acquire))logf("overlay: game closed; waiting for the save upload");
            else {logf("overlay: game did not close in 15 s; force quitting");force_quit();}
        }
    }
    // Frame times from the game's Present (guest_overlay.h): raw rows to
    // LocalState\frametimes.csv every 2 s, a stutter summary every 10 s.
    double m_frameFlush=0,m_frameSummary=0;std::vector<guest_overlay::Frame> m_frameWindow;
    void frame_log(double t) {
        if(t<m_frameFlush)return;
        m_frameFlush=t+2;
        auto frames=guest_overlay::take_frames();
        if(!frames.empty()) {
            std::wstring path=g_dir+L"\\frametimes.csv";
            bool fresh=GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES;
            if(FILE* f=_wfopen(path.c_str(),L"ab")) {
                if(fresh)fputs("at_ms,frame_ms,present_ms\n",f);
                for(auto& x:frames)fprintf(f,"%.1f,%.2f,%.2f\n",x.atMs,x.frameMs,x.presentMs);
                fclose(f);
            }
            m_frameWindow.insert(m_frameWindow.end(),frames.begin(),frames.end());
        }
        if(t<m_frameSummary||m_frameWindow.empty())return;
        m_frameSummary=t+10;
        std::vector<double> v;double sum=0,present=0;unsigned over25=0,over50=0;
        for(auto& x:m_frameWindow){v.push_back(x.frameMs);sum+=x.frameMs;present+=x.presentMs;over25+=x.frameMs>25;over50+=x.frameMs>50;}
        std::sort(v.begin(),v.end());
        logf("frames: n=%zu avg=%.1fms p50=%.1f p99=%.1f max=%.1f >25ms=%u >50ms=%u present_avg=%.1fms resizes=%u (last %ux%u count=%u hr=0x%08lx)",v.size(),sum/v.size(),
             v[v.size()/2],v[std::min(v.size()-1,v.size()*99/100)],v.back(),over25,over50,present/v.size(),
             guest_overlay::resizes.load(),guest_overlay::lastResizeW.load(),guest_overlay::lastResizeH.load(),guest_overlay::lastResizeCount.load(),(unsigned long)guest_overlay::lastResizeHr.load());
        m_frameWindow.clear();
    }
    void menu_command(const std::string& c,const char* via) {
        using namespace guest_overlay;
        // The menu only opens where it can be drawn: blocking the pad behind an
        // invisible menu would leave the game with a dead controller.
        if(c=="toggle"&&!menuOpen&&!guest_overlay::drawable){logf("overlay: menu unavailable (present hooks or back-buffer format unsupported) (%s)",via);return;}
        if(c=="toggle"){if(busy)return;menuOpen=!menuOpen;guest_overlay::ui_guideTab=0;selection=0;guest_overlay::achTop=0;
                        if(menuOpen)block_input(true);logf("overlay: menu %s (Home) (%s)",menuOpen?"opened":"closed",via);return;}
        // `selection` is the active tab's focused row (guide_press keeps the
        // atomics in sync); map it to the tab's commands.
        const int tab=ui_guideTab.load(),row=selection.load();
        const char* names[3][2]={{"Resume","Quit game"},{"",""},{"FPS counter","Frame-time graph"}};
        const char* name=tab<3&&row<2?names[tab][row]:"";
        if(busy)return;
        // cmd.txt synthetic commands (gquit/gforce/gfps) act without selection.
        if(c=="quit"||c=="force"){menuOpen=true;}
        if(c=="quit"){busy=true;unsigned n=close_guest_windows();logf("overlay: WM_CLOSE posted to %u guest window(s)",n);
                      guest_overlay::set_status(n?"Closing the game...":"No game window; force quitting");
                      if(!n){force_quit();return;}m_quitDeadline=now_s()+15;return;}
        if(c=="force"){busy=true;force_quit();return;}
        if(c=="fps"){fpsOn=!fpsOn;write_text(g_dir+L"\\overlay.txt",fpsOn?"fps=1\n":"fps=0\n");logf("overlay: FPS counter now %s",fpsOn?"on":"off");return;}
        if(c=="select"&&*name)logf("overlay: %s (tab %d row %d) (%s)",name,tab,row,via);
        if(c=="select"&&tab==0) {
            if(row==0){menuOpen=false;return;}   // Resume: input returns once buttons are released
            menuOpen=true;busy=true;
            if(row==1) {   // Quit game
                unsigned n=close_guest_windows();
                logf("overlay: WM_CLOSE posted to %u guest window(s)",n);
                guest_overlay::set_status(n?"Closing the game...":"No game window; force quitting");
                if(!n){force_quit();return;}
                m_quitDeadline=now_s()+15;return;
            }
            force_quit();   // row 2: Force quit
            return;
        }
        if(c=="select"&&tab==2) {
            if(row==0){fpsOn=!fpsOn;write_text(g_dir+L"\\overlay.txt",fpsOn?"fps=1\n":"fps=0\n");logf("overlay: FPS counter now %s",fpsOn?"on":"off");return;}
            if(row==1&&fpsOn){graphOn=!graphOn;logf("overlay: frame-time graph now %s",graphOn?"on":"off");return;}   // reachable only while the counter is on
            return;
        }
        // tab 1 (Achievements): the rows are not commands.
    }
    void force_quit() {
        if(m_leaving)return;
        m_leaving=true;guest_overlay::set_status("Uploading saves...");
        auto sync=g_finalSync;
        // CoreWindow is thread-affine; its CoreDispatcher is agile. Take the
        // dispatcher here, on the UI thread, for the worker to post back with.
        auto dispatcher=m_window.Dispatcher();
        std::thread([this,sync,dispatcher]{
            try {
                init_apartment(apartment_type::multi_threaded);
                if(sync)sync();
            } catch(...) {logf("overlay: force quit upload failed");}
            try {dispatcher.RunAsync(CoreDispatcherPriority::Normal,[this]{back_to_games("force quit");});}
            catch(hresult_error const& e) {logf("overlay: force quit dispatch failed hr=0x%08x",(unsigned)e.code().value);}
        }).detach();
    }
    // Play does not restart a guest in-process: its modules, threads and
    // swapchain stay loaded. When the game ends, restart the launcher so it
    // comes back on the game list (Exit if the shell refuses a restart).
    void back_to_games(const char* why) {
        logf("launch: back to games (%s); requesting app restart",why);
        auto dispatcher=m_window.Dispatcher();
        auto exitOnUi=[dispatcher]{dispatcher.RunAsync(CoreDispatcherPriority::Normal,[]{logf("launch: closing launcher");CoreApplication::Exit();});};
        try {
            auto restart=CoreApplication::RequestRestartAsync(L"");
            restart.Completed([exitOnUi](auto const& operation,winrt::Windows::Foundation::AsyncStatus status) {
                auto reason=AppRestartFailureReason::Other;
                try{if(status==winrt::Windows::Foundation::AsyncStatus::Completed)reason=operation.GetResults();}catch(...){}
                logf("launch: restart request status=%d reason=%d",(int)status,(int)reason);
                if(reason!=AppRestartFailureReason::RestartPending)exitOnUi();
            });
        } catch(hresult_error const& error) {logf("launch: restart unavailable hr=0x%08x",(unsigned)error.code().value);exitOnUi();}
    }
    // A launch that ran no game code was already released (launch.h): its
    // page shows why and Play works again. Once game code ran, the launcher
    // restarts to unload it, with or without a game window.
    void guest_ended() {
        if(g_launch.phase!=2 || m_guestEndGen==g_launch.gen)return;
        if(!m_guestSurface && !play_runtime::observed.load(std::memory_order_acquire))return;
        std::string result;bool ok;
        {std::lock_guard<std::mutex> lock(g_launch.m);ok=g_launch.ok;result=g_launch.result;}
        m_guestEndGen=g_launch.gen;
        achievement_poller_stop();   // the guest is exiting; stop watching its achievements.json
        m_achPollStarted=false;      // the next launch starts its own poller
        if(m_leaving)return;   // Force quit is already syncing and restarting
        m_leaving=true;m_quitDeadline=0;
        if(ok){back_to_games("game quit");return;}
        // A crash: explain it above the retained guest frame, then go back.
        std::string message=result.find("real client prerequisite item 36")!=std::string::npos?
            "This game needs the genuine Steam client. This runtime cannot start that client yet.":
            !m_guestSurface?"The game failed while starting. The launcher restarts to unload it; then you can play another game.":
            "The game stopped unexpectedly. Saves and achievements were uploaded if Steam was reachable.";
        message+="\n\n"+clip(result,500);
        try {
            winrt::Windows::UI::Popups::MessageDialog dialog(to_hstring(message),L"Game ended");
            dialog.Commands().Append(winrt::Windows::UI::Popups::UICommand(L"Back to games",[this](auto&&){back_to_games("after crash dialog");}));
            dialog.DefaultCommandIndex(0);dialog.CancelCommandIndex(0);
            m_guestDialog=dialog.ShowAsync();
            m_guestDialog.Completed([](auto const& operation,winrt::Windows::Foundation::AsyncStatus status) {
                if(status==winrt::Windows::Foundation::AsyncStatus::Error)
                    logf("launch: crash dialog async error hr=0x%08x",(unsigned)operation.ErrorCode().value);
            });
            logf("launch: crash dialog requested");
        } catch(hresult_error const& error) {logf("launch: crash dialog failed hr=0x%08x",(unsigned)error.code().value);back_to_games("dialog unavailable");}
    }
    static void blit(const std::vector<uint32_t>& pix, int iw, int ih, int x, int y) {
        for (int j = 0; j < ih; j++) { int yy = y + j; if (yy < 0 || yy >= H) continue; for (int i = 0; i < iw; i++) { int xx = x + i; if (xx < 0 || xx >= W) continue; g_fb[yy * W + xx] = pix[(size_t)j * iw + i] | 0xFF000000u; } }
    }
    void draw_detail(double t) {
        refresh_install(t);
        auto si = m_dSi; int st = si ? (int)si->state : 2;
        bool installing = si && st == 1;
        apib::Result gfx = apib::combine(apib::lookup(m_dApp), si && st == 1 ? si->dxKind : 0, si && st == 1 ? si->dxNote : std::string());   // store fields are read only once published
        std::string title = (installing && !si->name.empty()) ? si->name : m_dName;
        int dl = g_dl.state; bool mine = g_dl.appid == m_dApp, down = mine && dl == 1, queued = !down && is_queued(m_dApp);
        bool launching = (g_launch.phase == 1 || g_launch.phase == 3) && g_launch.appid == m_dApp;
        bool retained = play_runtime::observed.load(std::memory_order_acquire) != nullptr;
        bool deleting = deleting_mine(), delDone = g_del.appid == m_dApp && (g_del.state == 2 || g_del.state == 3);
        double pct = down && g_dl.total ? std::min(1.0, (double)g_dl.done / (double)g_dl.total) : 0.0;

        // ---- view model (statics keep the c_str()s alive while drawing) ----
        static std::string vTitle, vSub, vTime, vAch, vLast, vStatus, vSize, vLoc, vUpdated, vCloud, vAbout, vPlayers, vLang, vNote;
        vTitle = title;
        if (installing) vSub = si->dev.empty() ? si->genres : si->dev + "  \xC2\xB7  " + si->genres;
        else vSub = "App " + std::to_string(m_dApp);
        { const std::string gd = apib::describe(gfx); if (!gd.empty() && gfx.ready) vSub += "  \xC2\xB7  " + gd; }   // graphics API (was the tile badge)
        Game* gm = nullptr; { std::lock_guard<std::mutex> l(g_gm); for (auto& g : g_games) if (g->appid == m_dApp) { gm = g.get(); break; } }
        if (gm && gm->playtime > 0) { char b[48]; snprintf(b, sizeof b, "%u min", gm->playtime); vTime = b; } else vTime = "Not played yet";
        vAch = "Not tracked yet";   // achievements sync after play (steam_achievements.h); no schema on this page yet
        if (gm && gm->lastPlayed > 0) { time_t lt = (time_t)gm->lastPlayed; char b[32]; struct tm tmv{}; localtime_s(&tmv, &lt); strftime(b, sizeof b, "%d %b %Y", &tmv); vLast = b; } else vLast = "Never";
        if (deleting) vStatus = "Deleting...";
        else if (down) { char b[48]; snprintf(b, sizeof b, "Downloading %.0f%%", pct * 100); vStatus = b; }
        else if (queued) vStatus = "Queued...";
        else if (m_dInst.installed) vStatus = m_dInst.bytes ? storage_roots::size_text(m_dInst.bytes) : "Installed";
        else vStatus = "Not installed";
        vSize = m_dInst.installed ? (m_dInst.bytes ? storage_roots::size_text(m_dInst.bytes) : "Unknown") : "Not installed";
        vLoc = "Internal storage";
        if (m_dInst.installed && !m_dInst.root.empty()) {
            vLoc = m_dInst.root; for (auto& rc : m_roots) if (rc.root == m_dInst.root) { vLoc = rc.name; break; }
        }
        vUpdated = "Not tracked";   // no install-timestamp marker exists yet
        CloudStatus cs = cloud_status(m_dApp);
        if (m_dInst.installed) { std::string ct = cs.text; vCloud = (cs.phase != 0 && !ct.empty()) ? ct : "Checking..."; }
        else vCloud = "Not installed";
        vAbout = (installing && !si->desc.empty()) ? si->desc : "No store description for this game yet.";
        vPlayers = "1"; vLang = "English";   // the store schema carries no players/language rows

        ui_launcher::PrimaryVM pv;
        pv.installed = m_dInst.installed;
        pv.download = down ? detail_input::Download::running : queued ? detail_input::Download::queued : detail_input::Download::none;
        pv.launching = launching; pv.deleting = deleting; pv.restart_required = retained;
        pv.install_pct = float(pct);

        ui_launcher::GameVM g;
        g.title = vTitle.c_str(); g.subtitle = vSub.c_str();
        g.time_played = vTime.c_str(); g.achievements = vAch.c_str(); g.last_played = vLast.c_str();
        g.install_status = vStatus.c_str(); g.size_on_disk = vSize.c_str(); g.location = vLoc.c_str();
        g.last_updated = vUpdated.c_str(); g.cloud_saves = vCloud.c_str();
        g.about = vAbout.c_str(); g.players = vPlayers.c_str(); g.language = vLang.c_str();
        g.primary = pv; g.primary_focus = m_dFocus == 0; g.install_pct = float(pct);
        if (si && si->img == 1 && !si->hdr.pix.empty()) { g.has_art = true; g.art = ui::Image{si->hdr.pix.data(), si->hdr.iw, si->hdr.ih, si->hdr.iw}; }
        if (m_focusAch) {   // latest achievement row when the schema is around (steam_achievements.h cache)
            g.achievement = m_achName.c_str(); g.achievement_sub = m_achSub.c_str();
            g.progress_pct = m_achPct;
        }

        // transient note: refusals, cloud warnings, launch panel (one line, newlines flattened)
        vNote.clear();
        if (!m_dNote.empty()) { vNote = m_dNote; for (auto& ch : vNote) if (ch == '\n') ch = ' '; }
        else if (launching) { if (g_launch.phase == 3) vNote = "Guest running (playability unverified)"; else { char b[64]; snprintf(b, sizeof b, "Launching... (%.0f s)", now_s() - g_launch.started); vNote = b; } }
        else if (retained) vNote = "Launch failed - restarting the launcher to unload it";
        else if (delDone && !m_dInst.installed) vNote = g_del.state == 2 ? "Deleted. Saves were kept." : "Deleted with items left behind; see boot.log";
        else if (deleting) { char b[64]; snprintf(b, sizeof b, "%llu files removed", (unsigned long long)g_del.progress.removed.load()); vNote = b; }
        else if (mine && (dl == 3 || dl == 4) && !m_dInst.installed) { std::string ms; { std::lock_guard<std::mutex> l(g_dl.m); ms = g_dl.msg; } vNote = (dl == 4 ? "Cancelled: " : "Failed: ") + ms; }
        g.focus = m_dFocus;

        ui::Canvas c(g_fb.data(), W, H, W);
        ui_launcher::draw_game(c, g_font, g);
        ui_launcher::draw_game_note(c, g_font, g, vNote.c_str());
        if (m_pick.open) draw_picker();
        if (m_confirmDelete) {
            ui_launcher::DeleteVM d;
            static std::string dTitle, dBody;
            dTitle = "Delete " + clip(m_dName, 30) + "?";
            if (m_delKind == game_library::DeleteKind::shared) dBody = "Its files are shared with another installed game and stay.";
            else if (m_delKind == game_library::DeleteKind::unknown_folder) dBody = "Leaves the installed list. Its folder is unknown, so files stay.";
            else if (m_dInst.bytes >= 107374182) dBody = "Frees " + storage_roots::size_text(m_dInst.bytes) + " on this console. Saves are kept.";
            else if (m_dInst.bytes) dBody = "Frees " + storage_roots::size_text(m_dInst.bytes) + " on this console. Saves are kept.";
            else dBody = "Removes the game files from this console. Saves are kept.";
            d.title = dTitle.c_str(); d.body = dBody.c_str(); d.focus_delete = true;
            ui_launcher::draw_delete_sheet(c, g_font, d);
        }
        if (m_cloudOpen) {
            ui_launcher::CloudVM v;
            static std::string synced; synced = cs.phase != 0 && !cs.text.empty() ? cs.text : std::string("Never synced");
            static ui_launcher::CloudSlotVM slot; slot.name = "Status"; slot.detail = synced.c_str();
            static ui_launcher::CloudOptVM opts[3];
            bool conflict = cs.text.find("LB: use cloud") != std::string::npos;
            opts[0] = {"Sync now", "", true};
            opts[1] = {"Use the cloud copy", "", conflict};
            opts[2] = {"Keep this console's copy", "", conflict};
            v.synced = synced.c_str(); v.slots = &slot; v.slot_count = 1;
            v.opts = opts; v.opt_count = 3; v.focus_opt = m_cloudFocus;
            ui_launcher::draw_cloud_sheet(c, g_font, v);
        }
    }
    // The launch-option picker (launch_picker.h): the game's own run options,
    // 64-bit first, each row saying which engine runs it (64-bit natively,
    // 32-bit in the x86 interpreter).
    void draw_picker() {
        const uint32_t TXT = C3(255, 255, 255), DIM = C3(170, 180, 195), YEL = C3(255, 210, 63), GRN = C3(120, 255, 140), RED = C3(255, 110, 110);
        const int x = 360, w = 1200, rowH = 74;
        int rows = (int)m_pick.rows.size();
        int h = 172 + rows * rowH + (m_pick.note.empty() ? 0 : 44);
        int y = std::max(40, (1080 - h) / 2);
        fill(x, y, w, h, C3(10, 12, 16)); frame(x, y, w, h, 6, YEL);
        text8(x + 40, y + 30, "How should this game start?", 4, TXT);
        text8(x + 40, y + 84, clip(m_dName, 44) + " offers more than one run option. D-pad up/down to choose, A to run, B to cancel.", 2, DIM);
        int ry = y + 132;
        for (int i = 0; i < rows; i++) {
            const launch_target::Choice& c = m_pick.rows[i];
            bool sel = i == m_pick.sel;
            fill(x + 30, ry, w - 60, rowH - 10, sel ? C3(45, 90, 200) : C3(27, 35, 48));
            if (sel) frame(x + 30, ry, w - 60, rowH - 10, 3, YEL);
            text8(x + 50, ry + 14, clip(c.label, 60), 3, c.selectable ? TXT : DIM);
            const char* note = launch_target::arch_note(c.arch);
            if (note) text8(x + 50, ry + 42, note, 2, DIM);
            else if (c.arch == launch_target::arch64) text8(x + 50, ry + 42, "64-bit", 2, GRN);
            ry += rowH;
        }
        if (!m_pick.note.empty()) text8(x + 40, ry + 8, clip(m_pick.note, 90), 2, RED);
        text8(x + 40, y + h - 40, "A: run this option        B: cancel", 3, TXT);
    }
    // One-shot synthetic command file LocalState\cmd.txt (consumed on read). Lines: open <appid> | press (=A) | back (=B) | view | up | down | left | right | lb | rb | x | y | sleep <s>.
    // Semantics follow the new screens: on the library X cycles the sort and Y reloads,
    // the View filter is gone (the sidebar's Installed entry switches the view); on the
    // detail page X/Y are no longer shortcuts (focus the trash/cloud/cog buttons with
    // left/right, then A). lb/rb keep their conflict-pick role.
    // Never periodic, never present unless a tester/script writes it; every executed line is logged as SYNTHETIC.
    void cmdhook(double t) {
        static double next = 0, holdUntil = 0; static std::deque<std::string> q;
        if (t < next) return; next = t + 0.25;
        if (g_screen != S_LIBRARY && g_screen != S_DETAIL && g_screen != S_QUEUE) return;
        std::wstring path = g_dir + L"\\cmd.txt"; std::string all;
        if (read_file_s(path, all)) {
            if (!DeleteFileW(path.c_str())) { logf("cmd.txt: consume failed; no input"); }
            else { unsigned n = 0; size_t pos = 0; while (pos <= all.size() && n < 64) { size_t e = all.find('\n', pos); if (e == std::string::npos) e = all.size(); std::string l = all.substr(pos, e - pos); pos = e + 1;
                    while (!l.empty() && isspace((unsigned char)l.back())) l.pop_back(); while (!l.empty() && isspace((unsigned char)l.front())) l.erase(l.begin()); if (l.empty() || l.size() > 40) continue; q.push_back(l); n++; }
                logf("cmd.txt: consumed file; queued %u SYNTHETIC command(s)", n); }
        }
        if (q.empty() || t < holdUntil) return;
        std::string c = q.front(); q.pop_front(); holdUntil = t + 0.6;
        logf("cmd.txt SYNTHETIC command: '%s' (screen=%d)", c.c_str(), (int)g_screen.load());
        if (c.rfind("open ", 0) == 0) { uint32_t id = (uint32_t)strtoul(c.c_str() + 5, nullptr, 10); if (id) open_detail(id, game_name(id), "cmd.txt open (synthetic)"); return; }
        if (c.rfind("sleep ", 0) == 0) { holdUntil = t + std::min(600.0, atof(c.c_str() + 6)); return; }
        if (c == "gmenu" || c == "gquit" || c == "gforce" || c == "gfps") {   // in-game menu (guest_overlay.h)
            if (!m_guestSurface) { logf("cmd.txt: no game running"); return; }
            menu_command(c == "gmenu" ? "toggle" : c == "gquit" ? "quit" : c == "gforce" ? "force" : "fps", "cmd.txt (synthetic)"); return;
        }
        if (c == "stacks") { dump_thread_stacks(); logf("cmd.txt: thread stacks written to launch_probe.log"); return; }
        GamepadButtons m = GamepadButtons::None;
        if (c == "press" || c == "a") m = GamepadButtons::A; else if (c == "back" || c == "b") m = GamepadButtons::B; else if (c == "view") m = GamepadButtons::View;
        else if (c == "up") m = GamepadButtons::DPadUp; else if (c == "down") m = GamepadButtons::DPadDown; else if (c == "left") m = GamepadButtons::DPadLeft; else if (c == "right") m = GamepadButtons::DPadRight;
        else if (c == "lb") m = GamepadButtons::LeftShoulder; else if (c == "rb") m = GamepadButtons::RightShoulder; else if (c == "x") m = GamepadButtons::X; else if (c == "y") m = GamepadButtons::Y;
        if (m == GamepadButtons::None) { logf("cmd.txt: unknown command ignored"); return; }
        GamepadReading fake{}; fake.Buttons = m; input(t, fake, true, "cmd.txt (synthetic)"); input(t + 0.001, GamepadReading{}, true, "cmd.txt release (synthetic)");
    }

    IFrameworkView CreateView() { return *this; }
    void Initialize(CoreApplicationView const& v) {
        open_log(); logf("boot: Initialize (pid=%lu) launcher build", GetCurrentProcessId());
        v.Activated([](auto&&, IActivatedEventArgs const& a) {
            logf("Activated kind=%d (activation args ignored)", (int)a.Kind());
            CoreWindow::GetForCurrentThread().Activate();
        });
    }
    // Keyboard and mouse reach the guest only while its swap chain is on screen,
    // the window is active and the launcher menu is closed.
    bool guest_input() {
        if(!m_guestSurface||guest_overlay::inputBlocked.load(std::memory_order_relaxed)){m_input.release_all();return false;}
        if(!m_input.complete()) {
            bool ready=m_input.bind([](const char* name){return user32_core::resolve("user32.dll",name);});
            if(!m_inputLogged){m_inputLogged=true;logf("keyboard/mouse feed %s",ready?"ready":"unavailable (user32 records missing)");}
            if(!ready)return false;
        }
        m_input.set_fallback(play_graphics::presented.load(std::memory_order_acquire));
        return true;
    }
    void pointer_event(PointerEventArgs const& e,bool wheel) {
        auto point=e.CurrentPoint();
        if(point.PointerDevice().PointerDeviceType()!=winrt::Windows::Devices::Input::PointerDeviceType::Mouse||!guest_input())return;
        auto p=point.Properties();auto bounds=m_window.Bounds();auto at=point.Position();
        bool fed;
        if(wheel)fed=m_input.wheel(at.X,at.Y,bounds.Width,bounds.Height,p.MouseWheelDelta(),p.IsHorizontalMouseWheel());
        else fed=m_input.pointer(at.X,at.Y,bounds.Width,bounds.Height,
            (p.IsLeftButtonPressed()?corewindow_input::Left:0)|(p.IsRightButtonPressed()?corewindow_input::Right:0)|
            (p.IsMiddleButtonPressed()?corewindow_input::Middle:0)|(p.IsXButton1Pressed()?corewindow_input::X1:0)|
            (p.IsXButton2Pressed()?corewindow_input::X2:0));
        if(fed)e.Handled(true);
    }
    void SetWindow(CoreWindow const& w) { m_window = w; w.Activated([this](auto&&, WindowActivatedEventArgs const& e) { bool active=e.WindowActivationState()!=CoreWindowActivationState::Deactivated; xinput_core::set_active(active); if(!active)m_input.release_all(); }); w.Closed([this](auto&&, auto&&) { m_closed = true; g_quit = true; logf("window closed"); });
        // Every key, including Alt and F10 (which never raise KeyDown), arrives here.
        w.Dispatcher().AcceleratorKeyActivated([this](auto&&, AcceleratorKeyEventArgs const& e) {
            auto type=e.EventType();
            bool down=type==CoreAcceleratorKeyEventType::KeyDown||type==CoreAcceleratorKeyEventType::SystemKeyDown;
            bool up=type==CoreAcceleratorKeyEventType::KeyUp||type==CoreAcceleratorKeyEventType::SystemKeyUp;
            if((!down&&!up)||!guest_input())return;
            auto k=e.KeyStatus();
            if(m_input.key(unsigned(e.VirtualKey()),k.ScanCode,k.IsExtendedKey,down,k.RepeatCount))e.Handled(true);
        });
        w.PointerMoved([this](auto&&, PointerEventArgs const& e){pointer_event(e,false);});
        w.PointerPressed([this](auto&&, PointerEventArgs const& e){pointer_event(e,false);});
        w.PointerReleased([this](auto&&, PointerEventArgs const& e){pointer_event(e,false);});
        w.PointerWheelChanged([this](auto&&, PointerEventArgs const& e){pointer_event(e,true);});
        // B on Xbox raises a system back request; unhandled, the shell sends the
        // app to Home. B belongs to the launcher UI and the running game.
        SystemNavigationManager::GetForCurrentView().BackRequested([](auto&&, BackRequestedEventArgs const& e) { e.Handled(true); }); logf("SetWindow bounds=%.0fx%.0f", w.Bounds().Width, w.Bounds().Height); }
    void Load(hstring const&) {}
    void Uninitialize() { logf("Uninitialize"); }

    void PollGuestControllers() {
        static std::array<Gamepad,4> assigned{nullptr,nullptr,nullptr,nullptr};
        try {
            auto pads=Gamepad::Gamepads();
            for(auto& pad:assigned)if(pad) {
                bool found=false;
                for(auto const& current:pads)if(current==pad){found=true;break;}
                if(!found)pad=nullptr;
            }
            for(auto const& pad:pads) {
                bool found=false;
                for(auto const& current:assigned)if(current==pad){found=true;break;}
                if(!found)for(unsigned i=0;i<assigned.size();++i)if(!assigned[i]){xinput_core::publish(i,nullptr);assigned[i]=pad;break;}
            }
            for(unsigned i=0;i<assigned.size();++i) {
                auto pad=assigned[i];
                if(!pad){xinput_core::publish(i,nullptr);continue;}
                try {
                    auto state=convert_pad(pad.GetCurrentReading());
                    // The launcher menu owns the pad while it is open (XInput games too).
                    if(guest_overlay::inputBlocked)state=XINPUT_GAMEPAD{};
                    xinput_core::publish(i,&state,[pad](WORD left,WORD right)->DWORD {
                        try {
                            GamepadVibration motors{};
                            motors.LeftMotor=double(left)/65535.;motors.RightMotor=double(right)/65535.;
                            pad.Vibration(motors);return ERROR_SUCCESS;
                        } catch(winrt::hresult_error const& error){return xinput_core::error_to_win32(error.code());}
                    });
                } catch(winrt::hresult_error const&){xinput_core::publish(i,nullptr);}
            }
        } catch(winrt::hresult_error const&) {
            for(unsigned i=0;i<assigned.size();++i)xinput_core::publish(i,nullptr);
        }
        // The record DLL's raw-input enumeration presents one HID device per
        // connected pad (uwph_u32c_pads), which is what a guest's legacy
        // joystick enumeration looks for; with no pad it reports none.
        static auto reportPads=reinterpret_cast<void (WINAPI*)(unsigned)>(
            user32_core::resolve("user32.dll","uwph_u32c_pads"));
        if(reportPads) {
            unsigned mask=0;
            for(unsigned i=0;i<assigned.size();++i)if(assigned[i])mask|=1u<<i;
            reportPads(mask);
        }
    }

    bool InitD3D() {
        D3D_FEATURE_LEVEL fls[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0}; D3D_FEATURE_LEVEL got{};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, fls, 2, D3D11_SDK_VERSION, m_dev.put(), &got, m_ctx.put());
        logf("D3D11CreateDevice hr=0x%08lx fl=0x%x", (unsigned long)hr, (unsigned)got); if (FAILED(hr)) return false;
        auto dxgiDev = m_dev.as<IDXGIDevice>(); com_ptr<IDXGIAdapter> ad; dxgiDev->GetAdapter(ad.put());
        com_ptr<IDXGIFactory2> fac; if (FAILED(ad->GetParent(__uuidof(IDXGIFactory2), fac.put_void()))) return false;
        DXGI_SWAP_CHAIN_DESC1 sd{}; sd.Width = W; sd.Height = H; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 3; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; sd.Scaling = DXGI_SCALING_STRETCH; sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        hr = fac->CreateSwapChainForCoreWindow(m_dev.get(), (::IUnknown*)winrt::get_abi(m_window), &sd, nullptr, m_sc.put());
        logf("CreateSwapChainForCoreWindow hr=0x%08lx %dx%d", (unsigned long)hr, W, H); if (FAILED(hr)) return false;
        return SUCCEEDED(m_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), m_bb.put_void()));
    }

    static HRESULT GuestSurface(IDXGIFactory2* factory, ::IUnknown* device,
            ::IUnknown* window, const DXGI_SWAP_CHAIN_DESC1* description,
            IDXGIOutput* output, IDXGISwapChain1** chain) {
        if(!graphicsApp)return E_NOTIMPL;
        auto app=graphicsApp;
        HANDLE done=CreateEventExW(nullptr,nullptr,0,EVENT_MODIFY_STATE|SYNCHRONIZE);
        if(!done)return HRESULT_FROM_WIN32(GetLastError());
        HRESULT result=E_FAIL;
        // The guest worker retains all call arguments until this UI task ends.
        // Release the launcher's flip chain before attaching the guest's chain
        // to the same CoreWindow. No guest HWND reaches native DXGI.
        try {
            app->m_graphicsDispatcher.RunAsync(CoreDispatcherPriority::Normal,
                [=,&result] {
                    try {
                        app->m_guestSurface=true;
                        app->m_ctx->ClearState();app->m_ctx->Flush();
                        app->m_bb=nullptr;app->m_sc=nullptr;
                        // The frame-latency waitable object is optional for the game
                        // (Unity falls back to Present pacing without it); on the console
                        // the game's main thread hung forever waiting on it.
                        DXGI_SWAP_CHAIN_DESC1 desc=*description;
                        desc.Flags&=~UINT(DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
                        // With two flip buffers, Present blocked ~1 s every ~2 s on the console
                        // (d3d11on12 waiting for a buffer the compositor still held); three
                        // buffers gave a steady 16.7 ms with no spikes.
                        desc.BufferCount=std::max<UINT>(desc.BufferCount,3);
                        // Test overrides (LocalState\swapchain.txt), e.g. "buffers=3 nomodeswitch sequential",
                        // to compare presentation behaviour on the console without a rebuild.
                        {std::string o;if(read_file_s(g_dir+L"\\swapchain.txt",o)) {
                            if(auto at=o.find("buffers=");at!=std::string::npos)desc.BufferCount=UINT(atoi(o.c_str()+at+8));
                            if(o.find("nomodeswitch")!=std::string::npos)desc.Flags&=~UINT(DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
                            if(o.find("sequential")!=std::string::npos)desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                            if(o.find("stretch")!=std::string::npos)desc.Scaling=DXGI_SCALING_STRETCH;
                            logf("guest swapchain overrides: %s",o.c_str());
                        }}
                        logf("guest swapchain %ux%u format=%u buffers=%u effect=%u scaling=%u alpha=%u flags=0x%x (requested 0x%x)",
                             desc.Width,desc.Height,(unsigned)desc.Format,desc.BufferCount,(unsigned)desc.SwapEffect,
                             (unsigned)desc.Scaling,(unsigned)desc.AlphaMode,desc.Flags,description->Flags);
                        result=factory->CreateSwapChainForCoreWindow(device,window,&desc,output,chain);
                        logf("guest surface handoff native hr=0x%08lx",(unsigned long)result);
                        if(SUCCEEDED(result)&&chain&&*chain) {
                            std::string saved;guest_overlay::fpsOn=read_file_s(g_dir+L"\\overlay.txt",saved)&&saved.find("fps=1")!=std::string::npos;
                            DeleteFileW((g_dir+L"\\frametimes.csv").c_str());
                            logf("overlay: present hooks %s (View+Menu opens the launcher menu)",guest_overlay::install(*chain)?"installed":"unavailable");
                        }
                    } catch(hresult_error const& e) {result=e.code();}
                    SetEvent(done);
                });
            WaitForSingleObject(done,INFINITE);
        } catch(hresult_error const& e) {result=e.code();}
        CloseHandle(done);return result;
    }

    // ---- drawing (Xbox-style screens; ui_launcher.h over ui::Canvas on g_fb) ----

    // QR code as horizontal runs of dark modules (ui_launcher::QrRun, 10px modules).
    inline static ui_launcher::QrRun g_qrRuns[1024];
    int g_qrRunCount = 0;
    inline static uint64_t g_qrRunGen = 0;   // g_qrGen value the runs were built for
    void build_qr_runs() {
        uint64_t gen = (uint64_t)g_qrGen.load();
        if (g_qrRunGen == gen) return;
        g_qrRunGen = gen;
        std::string url; { std::lock_guard<std::mutex> l(g_sm); url = g_qrUrl; }
        g_qrRunCount = 0;
        if (url.empty()) return;
        qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        int n = qr.getSize();
        for (int y = 0; y < n && g_qrRunCount < (int)(sizeof(g_qrRuns) / sizeof(g_qrRuns[0])); y++) {
            for (int x = 0; x < n;) {
                if (!qr.getModule(x, y)) { x++; continue; }
                int w = 0;
                while (x + w < n && qr.getModule(x + w, y)) w++;
                g_qrRuns[g_qrRunCount++] = {uint8_t(y), uint8_t(x), uint8_t(w > 255 ? 255 : w)};
                x += w;
            }
        }
    }

    void draw_login() {
        m_btnFocus = true; m_confirmLogout = false; // Refresh is the sole action
        build_qr_runs();
        static std::string status_buf; static char timer[64];
        {   // fill the view-model from the shared login state
            std::lock_guard<std::mutex> l(g_sm);
            status_buf = g_scanned ? "Scanned - approve the sign-in on your phone" : g_status;
        }
        double left = 270 - (now_s() - g_qrStart.load(std::memory_order_relaxed));
        if (left < 0) left = 0;
        snprintf(timer, sizeof timer, "The code refreshes in %d:%02d", (int)left / 60, (int)left % 60);
        ui::Canvas c(g_fb.data(), W, H, W);
        ui_launcher::SignVM s;
        s.status = status_buf.c_str();
        s.timer = timer;
        s.persona = "Not signed in";
        s.qr = g_qrRuns;
        s.qr_count = g_qrRunCount;
        s.refresh_focused = true;
        ui_launcher::draw_signin(c, g_font, s);
    }
    // Sidebar view-model, shared by the library and queue screens. Storage
    // figures come from the last storage report's internal root (the queue
    // screen does not probe drives); until one exists the defaults hold.
    // The sidebar row the D-pad sits on while the sidebar has focus (draw_sidebar's
    // pill); -1 = no row is focused. Derived at draw time from (m_focus, m_sidebar),
    // so it never lags the input handling.
    int sidebar_focus_row() const { return m_focus == ui_nav::Focus::sidebar ? m_sidebar : -1; }
    ui_launcher::SidebarVM sidebar_vm() {
        ui_launcher::SidebarVM sb;
        static std::string persona; persona = persona_name();
        sb.persona = persona.c_str();
        { std::lock_guard<std::mutex> l(g_gm);
          sb.all_count = (int)g_games.size();
          sb.installed_count = (int)m_installed.size();
          size_t q; { std::lock_guard<std::mutex> ql(g_dqm); q = g_dq.size(); }
          sb.queue_count = (int)q + (g_dl.state == 1 ? 1 : 0); }
        sb.active = m_onlyInstalled ? 1 : 0;
        sb.focus = sidebar_focus_row();
        return sb;
    }

    void draw_library(double t) {
        refresh_installed(t);
        ui::Canvas c(g_fb.data(), W, H, W);
        // smooth row scroll: m_scroll (in rows) eases toward m_top; position changes still only come via grid_position()
        { double dt = m_scrollT > 0 ? std::min(0.1, t - m_scrollT) : 0.0; m_scrollT = t;
          double d = (double)m_top - m_scroll;
          if (std::fabs(d) < 0.004) m_scroll = m_top; else m_scroll += d * std::min(1.0, dt * 14.0); }
        // tile view-models + cover management under g_gm (same lazy load/evict as before)
        static std::vector<ui_launcher::TileVM> tiles;
        static std::vector<ui::Image> arts;   // pixel memory the TileVMs point into
        size_t listed = 0; bool loading = false;
        { std::lock_guard<std::mutex> l(g_gm);
          auto list = shown_games();
          listed = list.size();
          loading = g_screen == S_LOADING || (g_games.empty() && !g_authResolved);
          if (!list.empty() && m_sel >= (int)list.size()) { int ns = game_library::clamp_selection(m_sel, list.size()); grid_position(ns, std::min(m_top, ns / COLS), "shown list shrank"); }
          int lo = std::max(0, (m_top - 1) * COLS), hi = (int)std::min<size_t>(list.size(), (size_t)(m_top + 3) * COLS);
          auto evict = [](Game& g) { int cc = g.cover; if (cc == 1 || cc == 2) { g.cover = 0; std::vector<uint32_t>().swap(g.pix); } };
          for (int i = 0; i < (int)list.size(); i++) {
              Game& g = *list[i];
              if (i >= lo && i < hi) { int z = 0; if (g.cover.compare_exchange_strong(z, 3)) { std::lock_guard<std::mutex> ql(g_qm); g_q.push_back(list[i]); g_qcv.notify_one(); } }
              else evict(g);
          }
          if (m_onlyInstalled) for (auto& g : g_games) if (!m_installed.count(g->appid)) evict(*g);
          tiles.clear(); arts.clear(); tiles.reserve(list.size()); arts.reserve(list.size());
          for (size_t i = 0; i < list.size(); i++) {
              Game& g = *list[i];
              ui_launcher::TileVM tv;
              tv.name = g.name.c_str();
              tv.installed = m_installed.count(g.appid) != 0;
              if (g.cover == 1 && !g.pix.empty()) { arts.push_back(ui::Image{g.pix.data(), g.iw, g.ih, g.iw}); tv.art = arts.back(); }
              tiles.push_back(tv);
          }
        }
        ui_launcher::SidebarVM sb = sidebar_vm();
        ui_launcher::LibraryVM lib;
        lib.tiles = tiles.data(); lib.tile_count = (int)tiles.size();
        lib.sel = m_sel; lib.top_row = m_top; lib.focus_tile = m_focus == ui_nav::Focus::grid;
        static std::string sort_label; sort_label = std::string("Sort by ") + SORT_NAMES[g_sortMode.load(std::memory_order_relaxed)];
        lib.sort_label = sort_label.c_str();
        static char count[128];
        if (loading) lib.loading = true;
        else if (m_onlyInstalled) snprintf(count, sizeof count, "%zu installed of %d games", listed, (int)g_ownedCount.load()), lib.count_label = count;
        else { snprintf(count, sizeof count, "%d games", (int)listed); lib.count_label = count; }
        ui_launcher::draw_library(c, g_font, sb, lib);
        if (g_demo) {   // demo marker replaces the count line (honest: not your account)
            c.fill_rect_alpha(524, 168, 430, 60, ui::theme::page, 255);
            c.fill_round_rect(ui::rect(524, 168, 430, 60), ui::theme::radiusControl, ui::theme::control);
            ui_launcher::text(c, g_font, ui::Face::Regular, 24, 548, 183.5f, "DEMO LIST - not your account", ui::theme::text);
        }
        std::string err; { std::lock_guard<std::mutex> l(g_sm); err = g_err; }
        if (!err.empty()) {   // load failure line above the grid
            ui_launcher::text(c, g_font, ui::Face::Regular, 24, 524, 216, err.c_str(), ui::theme::danger);
        }
        if (m_confirmLogout) {
            // restyled logout confirm over the dimmed library (delete-sheet layout)
            ui_launcher::draw_scrim(c);
            c.fill_round_rect(ui::rect(560, 368, 800, 344), ui::theme::radiusCard, ui::theme::panel);
            ui_launcher::text(c, g_font, ui::Face::Semibold, 36, 608, 416, "Log out of Steam?", ui::theme::text);
            ui_launcher::text(c, g_font, ui::Face::Regular, 24, 608, 475, "This deletes the saved login on this console only.", ui::theme::textDim);
            ui_launcher::text(c, g_font, ui::Face::Regular, 22, 608, 516, "You can sign in again with the QR code.", ui::theme::textMuted);
            const char* pairs[4] = {"A", "Yes, log out", "B", "Cancel"};
            ui_launcher::draw_hint_bar(c, g_font, pairs, 2);
        }
    }

    // Download queue screen (mockup 06): rows from g_dq + the active download;
    // pause has no backend (disabled stub is not drawn: rows show CANCEL only
    // via A), auto-update pill is a memory-only setting, Updates is always
    // empty (the launcher has no update backend).
    void draw_queue() {
        ui::Canvas c(g_fb.data(), W, H, W);
        refresh_installed(now_s());
        ui_launcher::SidebarVM sb = sidebar_vm(); sb.active = 2;
        // row model under the same locks the queue state uses
        static std::vector<ui_launcher::QueueRowVM> rows;
        static std::vector<std::string> rowNames, rowStatus;
        rows.clear(); rowNames.clear(); rowStatus.clear();
        std::deque<uint32_t> dq; uint32_t dlApp = 0; int dlState = 0; double dlPct = 0; std::string dlName, dlMsg;
        { std::lock_guard<std::mutex> l(g_dqm); dq = g_dq; }
        dlApp = g_dl.appid.load(); dlState = g_dl.state.load();
        dlPct = g_dl.total ? (double)g_dl.done / (double)g_dl.total : 0;
        { std::lock_guard<std::mutex> l(g_dl.m); dlName = g_dl.name; dlMsg = g_dl.msg; }
        if (dlState == 1 || (dlState != 0 && dlApp)) {
            rowNames.push_back(dlName.empty() ? "App " + std::to_string(dlApp) : dlName);
            char b[96];
            if (dlState == 1) snprintf(b, sizeof b, "Downloading  \xC2\xB7  %.0f%%  \xC2\xB7  %.1f MB/s", dlPct * 100, g_dl.speed / 1048576.0);
            else snprintf(b, sizeof b, "%s", ui_nav::dl_status_text(dlState, 0, dlMsg.c_str()));
            rowStatus.push_back(b);
            ui_launcher::QueueRowVM r;
            r.name = rowNames.back().c_str();
            r.status = rowStatus.back().c_str();
            r.pct = dlState == 1 ? float(dlPct) : 0.0f;
            r.busy = dlState == 1;
            rows.push_back(r);
        }
        for (uint32_t id : dq) {
            rowNames.push_back(game_name(id).empty() ? "App " + std::to_string(id) : game_name(id));
            rowStatus.push_back("Queued");
            ui_launcher::QueueRowVM r;
            r.name = rowNames.back().c_str();
            r.status = "Queued";
            rows.push_back(r);
        }
        ui_launcher::QueueVM q;
        q.auto_update = m_autoUpdate;
        q.rows = rows.data(); q.row_count = (int)rows.size();
        q.updates = nullptr; q.update_count = 0;
        q.focus = -1;
        ui_launcher::draw_download_queue(c, g_font, sb, q);
        if (rows.empty()) {   // honest empty state over the (empty) section
            ui_launcher::text(c, g_font, ui::Face::Regular, 24, 524, 340, "No downloads.", ui::theme::textDim);
            ui_launcher::text(c, g_font, ui::Face::Regular, 22, 524, 748, "No updates available.", ui::theme::textDim);
        }
        const char* pairs[4] = {"A", "Cancel", "B", "Back"};
        ui_launcher::draw_hint_bar(c, g_font, pairs, 2);
    }

    // Storage management: the sidebar entry opens the same figures the detail
    // page's storage report already collects (no new probing here).
    void draw_storage() {
        ui::Canvas c(g_fb.data(), W, H, W);
        ui_launcher::SidebarVM sb = sidebar_vm(); sb.active = 3;
        ui_launcher::draw_sidebar(c, g_font, sb);
        ui_launcher::text(c, g_font, ui::Face::Semibold, 44, 524, 73.5f, "Storage", ui::theme::text);
        std::vector<storage_roots::Choice> roots = m_roots.empty() ? std::vector<storage_roots::Choice>{storage_report::internal_root(g_dir)} : m_roots;
        float y = 200;
        for (auto& r : roots) {
            std::string line = storage_roots::label_capacity(r);
            ui_launcher::text(c, g_font, ui::Face::Regular, 24, 524, y, line.c_str(), ui::theme::text2);
            if (r.space.ok && r.space.total) {
                int pct = (int)(100 - 100.0 * r.space.freeTotal / (double)r.space.total);
                std::string used = std::to_string(pct) + "% used of " + storage_roots::size_text(r.space.total);
                ui_launcher::text(c, g_font, ui::Face::Regular, 20, 524, y + 34, used.c_str(), ui::theme::textDim);
            }
            y += 96;
        }
        ui_launcher::text(c, g_font, ui::Face::Regular, 20, 524, y + 16,
                          "The full report is in LocalState\\storage_report.txt", ui::theme::textMuted);
        const char* pairs[4] = {"A", "Select", "B", "Back"};
        ui_launcher::draw_hint_bar(c, g_font, pairs, 2);
    }

    void draw(double t) {
        int s = g_screen;
        if (!g_fontOk) {   // graceful fallback: the old bitmap screens stay drawable
            if (s == S_LOGIN) { fill(0, 0, W, H, C3(14, 17, 22)); std::string st; { std::lock_guard<std::mutex> l(g_sm); st = g_status; } text8(80, 480, st.c_str(), 4, C3(255, 255, 255)); }
            else { fill(0, 0, W, H, C3(14, 17, 22)); text8(80, 480, "Fonts failed to load - see boot.log", 4, C3(255, 120, 120)); }
            return;
        }
        if (s == S_LOGIN) draw_login();
        else if (s == S_LIBRARY || s == S_LOADING) draw_library(t);
        else if (s == S_QUEUE) draw_queue();
        else if (s == S_STORAGE) draw_storage();
        else if (s == S_DETAIL) draw_detail(t);
        else { fill(0, 0, W, H, C3(14, 17, 22)); std::string st; { std::lock_guard<std::mutex> l(g_sm); st = g_status; } text8(80, 480, st.c_str(), 4, C3(255, 255, 255)); }
    }

    // ---- input
    void input(double t, const GamepadReading& rd, bool connected, const char* source = "controller") {
        GamepadButtons b = connected ? rd.Buttons : GamepadButtons::None;
        GamepadButtons dir = GamepadButtons::None;
        if (connected) { if (rd.LeftThumbstickX > 0.6) dir |= GamepadButtons::DPadRight; if (rd.LeftThumbstickX < -0.6) dir |= GamepadButtons::DPadLeft; if (rd.LeftThumbstickY > 0.6) dir |= GamepadButtons::DPadUp; if (rd.LeftThumbstickY < -0.6) dir |= GamepadButtons::DPadDown; }
        b |= dir;
        GamepadButtons nav = b & (GamepadButtons::DPadUp | GamepadButtons::DPadDown | GamepadButtons::DPadLeft | GamepadButtons::DPadRight);
        GamepadButtons edge = b & ~m_pad.prev;
        if (nav != m_pad.held) { m_pad.held = nav; m_pad.heldSince = t; m_pad.lastRep = t; }
        else if (nav != GamepadButtons::None && t - m_pad.heldSince > 0.4 && t - m_pad.lastRep > 0.12) { edge |= nav; m_pad.lastRep = t; }
        m_pad.prev = b;
        if (edge == GamepadButtons::None) return;
        for (auto m : {GamepadButtons::A, GamepadButtons::B, GamepadButtons::X, GamepadButtons::Y, GamepadButtons::DPadUp, GamepadButtons::DPadDown, GamepadButtons::DPadLeft, GamepadButtons::DPadRight})
            if (has(edge, m)) logf("button event mask=0x%x screen=%d", (unsigned)m, (int)g_screen);
        logf("input source=%s edge=0x%x buttons=0x%x stick=(%.3f,%.3f) held=%.3f", source, (unsigned)edge, (unsigned)rd.Buttons, rd.LeftThumbstickX, rd.LeftThumbstickY, t - m_pad.heldSince);
        // A running guest owns the controller (it reads the same pad through XInput/WGI).
        // While its surface is up the launcher UI is not visible either (also during the
        // final save upload before the restart), so it must not act on those presses.
        if (auto guest = play_runtime::observed.load(std::memory_order_acquire); m_guestSurface || (guest && !guest->finished.load(std::memory_order_acquire))) {
            logf("input: game surface up; launcher UI ignores controller"); return;
        }
        int s = g_screen;
        if (s == S_LOGIN) { if (has(edge, GamepadButtons::A) || has(edge, GamepadButtons::X)) { g_refreshReq = true; set_status("Refreshing QR..."); logf("Refresh button pressed"); } }
        else if (s == S_DETAIL) {
            if (m_pick.open) {   // the launch-option picker owns the pad while it is up
                if (has(edge, GamepadButtons::DPadUp)) { launch_picker::move(m_pick, -1); logf("detail: launch option %d/%zu (%s)", m_pick.sel + 1, m_pick.rows.size(), source); }
                else if (has(edge, GamepadButtons::DPadDown)) { launch_picker::move(m_pick, 1); logf("detail: launch option %d/%zu (%s)", m_pick.sel + 1, m_pick.rows.size(), source); }
                else if (has(edge, GamepadButtons::A)) detail_picker_a(source);
                else if (has(edge, GamepadButtons::B)) { launch_picker::close(m_pick); logf("detail: launch-option picker cancelled (%s)", source); }
                return;
            }
            if (m_confirmDelete) {
                if (has(edge, GamepadButtons::A)) detail_delete_confirm(source);
                else if (has(edge, GamepadButtons::B)) { m_confirmDelete = false; logf("detail: delete cancelled (%s)", source); }
                return;
            }
            if (m_cloudOpen) {   // cloud sheet over the dimmed page: A activates, B closes
                CloudStatus cs = cloud_status(m_dApp);
                bool conflict = cs.text.find("LB: use cloud") != std::string::npos;
                if (has(edge, GamepadButtons::DPadUp)) { if (m_cloudFocus > 0) m_cloudFocus--; }
                else if (has(edge, GamepadButtons::DPadDown)) { if (m_cloudFocus < 2) m_cloudFocus++; }
                else if (has(edge, GamepadButtons::A)) {
                    if (m_cloudFocus == 0) detail_cloud("cloud sheet sync");
                    else if (m_cloudFocus == 1 && conflict) detail_cloud("cloud sheet use cloud", steam_cloud::Prefer::Cloud);
                    else if (m_cloudFocus == 2 && conflict) detail_cloud("cloud sheet keep local", steam_cloud::Prefer::Local);
                    m_cloudOpen = false;
                }
                else if (has(edge, GamepadButtons::B)) { m_cloudOpen = false; logf("detail: cloud sheet closed (%s)", source); }
                return;
            }
            // focus ring: 0 primary, 1 trash, 2 cloud, 3 cog (ui_nav slots)
            bool installed = m_dInst.installed;
            int slots = ui_nav::focus_count(installed);
            if (has(edge, GamepadButtons::DPadRight)) { m_dFocus = std::min(slots - 1, m_dFocus + 1); logf("detail: focus slot %d (%s)", m_dFocus, source); }
            if (has(edge, GamepadButtons::DPadLeft)) { m_dFocus = std::max(0, m_dFocus - 1); logf("detail: focus slot %d (%s)", m_dFocus, source); }
            if (has(edge, GamepadButtons::DPadDown)) m_dScroll += 3;   // d-pad never cancels: it scrolls the description
            if (has(edge, GamepadButtons::DPadUp)) m_dScroll = std::max(0, m_dScroll - 3);
            if (cloud_status(m_dApp).text.find("LB: use cloud") != std::string::npos) {
                if (has(edge, GamepadButtons::LeftShoulder)) { detail_cloud("LB use cloud", steam_cloud::Prefer::Cloud); return; }
                if (has(edge, GamepadButtons::RightShoulder)) { detail_cloud("RB keep Xbox", steam_cloud::Prefer::Local); return; }
            }
            if (has(edge, GamepadButtons::A)) {
                int icon = ui_nav::icon_for_slot(installed, m_dFocus);
                if (icon == 0) detail_action(source);             // the primary button (or its id)
                else if (icon == 1) detail_delete(source);        // trash
                else if (icon == 2) { m_cloudOpen = true; m_cloudFocus = 0; logf("detail: cloud sheet open (%s)", source); }   // cloud
                else detail_storage(source);                      // cog: cycle the install location
            }
            if (has(edge, GamepadButtons::B)) detail_back(source);
            if (has(edge, GamepadButtons::View) || has(edge, GamepadButtons::Menu)) close_detail("Menu/View");
        }
        else if (s == S_LIBRARY) {
            std::vector<std::shared_ptr<Game>> list; { std::lock_guard<std::mutex> l(g_gm); list = shown_games(); } size_t n = list.size();
            if (m_confirmLogout) {
                if (has(edge, GamepadButtons::A)) { m_confirmLogout = false; g_logoutReq = true; grid_position(0, 0, "logout confirmed"); logf("logout confirmed: deleting local session file only (no revoke call)"); }
                else if (has(edge, GamepadButtons::B)) { m_confirmLogout = false; logf("logout cancelled"); }
                return;
            }
            // Sidebar focus: up/down moves the entry, A switches the view, right
            // returns to the grid. The View filter chip is gone; Installed is a
            // sidebar entry now. The drawer derives its focused row from
            // (m_focus, m_sidebar) (sidebar_focus_row), on the queue/storage
            // screens that is the entry that was pressed, i.e. their active row.
            if (m_focus == ui_nav::Focus::sidebar) {
                if (has(edge, GamepadButtons::DPadUp)) { ui_nav::sidebar_move(m_sidebar, false); logf("sidebar: entry %d (%s)", m_sidebar, source); }
                else if (has(edge, GamepadButtons::DPadDown)) { ui_nav::sidebar_move(m_sidebar, true); logf("sidebar: entry %d (%s)", m_sidebar, source); }
                else if (has(edge, GamepadButtons::DPadRight)) { ui_nav::lateral(m_focus, 0, false); logf("sidebar: back to the grid (%s)", source); }
                else if (has(edge, GamepadButtons::A)) {
                    int target = ui_nav::sidebar_target(m_sidebar);
                    if (target == 0 || target == 1) {
                        bool want = target == 1;
                        if (m_onlyInstalled != want) { m_onlyInstalled = want; m_installedT = -10; refresh_installed(t); grid_position(0, 0, "sidebar view"); }
                        logf("sidebar: %s view (%zu installed)", m_onlyInstalled ? "installed" : "all games", m_installed.size());
                    } else if (target == 2) { g_screen = S_QUEUE; logf("sidebar: download queue (%s)", source); }
                    else if (target == 3) { refresh_roots("storage screen"); g_screen = S_STORAGE; logf("sidebar: storage management (%s)", source); }
                }
                return;
            }
            // Grid focus. The grid draws kGridCols columns (ui_launcher.h geometry);
            // movement uses the same constant so the ring never lies.
            const int COLS = ui_launcher::kGridCols;
            int sel = m_sel;
            if (has(edge, GamepadButtons::DPadRight) && sel + 1 < (int)n) sel++;
            if (has(edge, GamepadButtons::DPadLeft)) { if (!ui_nav::lateral(m_focus, sel % COLS, true)) { if (sel > 0) sel--; } else { logf("grid: focus -> sidebar (%s)", source); } }
            if (has(edge, GamepadButtons::DPadDown) && (sel / COLS + 1) * COLS < (int)n) sel = std::min((int)n - 1, sel + COLS);
            if (has(edge, GamepadButtons::DPadUp) && sel - COLS >= 0) sel -= COLS;
            if (has(edge, GamepadButtons::RightShoulder)) sel = std::min((int)n - 1, sel + 2 * COLS);
            if (has(edge, GamepadButtons::LeftShoulder)) sel = std::max(0, sel - 2 * COLS);
            if (n == 0) sel = 0;
            sel = std::max(0, std::min(sel, (int)n - 1));
            int top = m_top, r = sel / COLS; if (r < top) top = r; if (r > top + 1) top = r - 1;
            grid_position(sel, top, source);
            if (has(edge, GamepadButtons::A) && n) { uint32_t aid; std::string nm; { std::lock_guard<std::mutex> l(g_gm); aid = list[m_sel]->appid; nm = list[m_sel]->name; } open_detail(aid, nm, source); return; }
            if (has(edge, GamepadButtons::X)) {   // sort cycles the existing modes (kLibraryHints "X Sort")
                int m = ui_nav::cycle_sort(g_sortMode.load(), 3); g_sortMode = m; { std::lock_guard<std::mutex> l(g_gm); sort_games(m); } grid_position(0, 0, "sort cycle"); logf("sort mode -> %s", SORT_NAMES[m]);
            }
            if (has(edge, GamepadButtons::Y) && !g_demo) { g_reloadReq = true; grid_position(0, 0, "Y library reload"); }   // the old X reload moved to Y
            if (has(edge, GamepadButtons::B) && !g_demo && ui_nav::logout_request(m_confirmLogout, m_focus, m_sel, m_sidebar, true)) m_confirmLogout = true;   // B never stops a download
        }
        else if (s == S_QUEUE) {
            // A on a busy row cancels it (running download or queued job); B back to the library.
            std::deque<uint32_t> dq; { std::lock_guard<std::mutex> l(g_dqm); dq = g_dq; }
            uint32_t dlApp = g_dl.appid.load(); int dlState = g_dl.state.load();
            bool hasRows = (dlState == 1 || (dlState != 0 && dlApp)) || !dq.empty();
            if (has(edge, GamepadButtons::B)) { g_screen = S_LIBRARY; logf("queue: back to the library (%s)", source); return; }
            if (has(edge, GamepadButtons::A) && hasRows) {
                if (dlState == 1 || (dlState != 0 && dlApp)) { g_dl.cancel = true; logf("queue: download cancel requested appid=%u (%s)", dlApp, source); }
                else if (!dq.empty()) { uint32_t id = dq.front(); cancel_queued(id); logf("queue: cancel asked appid=%u (%s)", id, source); }
            }
            if (has(edge, GamepadButtons::Y)) { m_autoUpdate = !m_autoUpdate; logf("queue: auto-update -> %s (memory only, %s)", m_autoUpdate ? "on" : "off", source); }
        }
        else if (s == S_STORAGE) {
            if (has(edge, GamepadButtons::B)) { g_screen = S_LIBRARY; logf("storage: back to the library (%s)", source); }
        }
    }

    void Run() {
        logf("Run: entering"); m_window.Activate();
        load_fonts();   // Selawik faces from the package's fonts/ folder; on failure the bitmap fallback stays
        load_guide_fonts();   // the same faces for the in-game guide (guest_overlay.h)
        if (!InitD3D()) { logf("D3D init FAILED; idling"); while (!m_closed) { m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending); Sleep(50); } return; }
        graphicsApp=this;m_graphicsDispatcher=m_window.Dispatcher();
        play_graphics::configure((::IUnknown*)winrt::get_abi(m_window),GuestSurface,
            [](const char* message){if(play_runtime::state)play_runtime::state->event(message);});
        std::thread(net_thread).detach();
        // The console self-test (selftest.h): a LocalState\selftest.txt request,
        // or a queue a restart did not finish, runs the packaged diag fixtures
        // through Play and appends SELFTEST RESULT lines to LocalState\selftest.log.
        if (selftest::resume()) selftest::run_async(m_window.Dispatcher());
        double lastBeat = 0;
        while (!m_closed) {
            m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            PollGuestControllers();
            auto pads = Gamepad::Gamepads(); size_t n = pads.Size(); GamepadReading rd{};
            if ((int)n != m_prevPads) { logf("gamepads connected: %zu", n); m_prevPads = (int)n; }
            double t = now_s();
            if (n) rd = pads.GetAt(0).GetCurrentReading();
            input(t, rd, n > 0);
            autotest(t); cmdhook(t); detail_tick(t);
            if (selftest::tick()) selftest::run_async(m_window.Dispatcher());
            if(m_guestSurface){guest_menu(t,n?rd.Buttons:GamepadButtons::None);Sleep(1);continue;}
            draw(t);
            m_ctx->UpdateSubresource(m_bb.get(), 0, nullptr, g_fb.data(), W * 4, 0);
            HRESULT hr = m_sc->Present(1, 0);
            if (FAILED(hr)) { logf("Present hr=0x%08lx", (unsigned long)hr); Sleep(100); }
            m_frame++;
            if (m_frame == 1 || t - lastBeat >= 10.0) { logf("frame=%llu fps~%.1f screen=%d pads=%zu", (unsigned long long)m_frame, m_frame / (t > 0 ? t : 1), (int)g_screen, n); lastBeat = t; }
        }
    }
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    init_apartment(apartment_type::multi_threaded);
    CoreApplication::Run(make<App>());
    return 0;
}
