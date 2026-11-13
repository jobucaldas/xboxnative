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
#include <winrt/Windows.System.h>
// The account picture fallback (UserInformation::GetAccountPicture). The
// projection ships this header under either casing depending on the SDK, so
// take whichever one this build has.
#if __has_include(<winrt/Windows.System.UserProfile.h>)
#include <winrt/Windows.System.UserProfile.h>
#else
#include <winrt/windows.system.userprofile.h>
#endif
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
#include "playtime.h"
#include <xinput.h>
#include "../uwph/display/gamepad.h"
#include "../uwph/xinput/gpad.h"

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
// Item D4: truncation for the UI font cuts at a UTF-8 character boundary and
// marks the cut with U+2026 (Selawik has the glyph), never mid-glyph and
// never with a bare "." that reads as part of the name.
static std::string clip(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    size_t cut = n >= 3 ? n - 3 : 0;   // room for the 3-byte ellipsis
    while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) --cut;
    return s.substr(0, cut) + "\xE2\x80\xA6";
}
// Same for the bitmap fallback font, which has ASCII glyphs only.
static std::string clip8(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 2) + ".."; }

// ---------------------------------------------------------------- HTTP (Windows.Web.Http, default TLS validation)
struct Resp { int status = 0; int eresult = -1; std::string body; std::string err; };
// One HttpClient per thread: the object is agile, but its session state (the
// filter chain and connection pool) failed with E_CHANGED_STATE 0x8000000C
// ("a concurrent or interleaved operation changed the state") when many worker
// threads sent through one process-wide instance at burst peaks (seen on the
// cover and achievement-icon fetches). A per-thread client gives each thread
// its own pool and headers; the User-Agent stays identical on every thread.
// Every thread that reaches here has called init_apartment(multi_threaded)
// first, so constructing WinRT objects on that thread is safe.
static Http::HttpClient& client() {
    thread_local Http::HttpClient c = [] { Http::HttpClient h; h.DefaultRequestHeaders().UserAgent().TryParseAdd(L"Mozilla/5.0 (Windows; U; Windows NT 10.0; en-US; Valve Steam Client/default/1665786434;) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/85.0.4183.121 Safari/537.36"); return h; }();
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
#include "xbox_gamertag.h"    // the console's Xbox gamertag: XSTS claims, profile response, validation

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

// ---------------------------------------------------------------- Steam CM (websocket) client: connect + ClientLogon only
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
#include "playable.h"   // item 32: the owner-verified playable set + LocalState\playable.txt
#include "move_game.h"  // item 33: the storage move job (copy, verify, marker rewrite)
#include "favorites.h"  // item D10: per-game favorites, LocalState\favorites.txt
#include "ui_nav.h"
#include "ui_launcher.h"

// The account's persona name for the sidebar profile row, parsed from
// ClientPersonaState's player_name (field 15; steam_presence.h's parse_persona
// carries it). Empty until Steam answers: the sidebar then shows the honest
// default, never an invented name.
static std::mutex g_personaM; static std::string g_personaName;
static std::string persona_name() { std::lock_guard<std::mutex> l(g_personaM); return g_personaName; }

// Item D14: the console's signed-in XBOX user (Windows.System.User), read once
// on a worker thread. The sidebar's profile row prefers it and labels which
// account it is showing ("Xbox" / "Steam"); with none it falls back to the
// Steam persona. Names are the owner's own: never logged, never written to
// disk, and the tests use fake names only.
static std::mutex g_xboxM; static std::vector<std::string> g_xboxNames;
static std::atomic<int> g_xboxState{0};   // 0 none, 1 loading, 2 ready, 3 unavailable
static std::vector<std::string> xbox_user_names() { std::lock_guard<std::mutex> l(g_xboxM); return g_xboxNames; }
// Item D25: the console user's own picture (Windows.System.User.GetPictureAsync),
// decoded once on the same worker. The library's profile row shows the XBOX
// identity — name and avatar — and the Steam persona lives on the profile page.
static std::mutex g_xboxPicM; static ui::Image g_xboxPic; static std::vector<uint32_t> g_xboxPicBuf;
static std::atomic<int> g_xboxPicState{0};   // 0 none, 1 loading, 2 ready, 3 failed
static bool wic_decode_buf(const std::string& jpg, std::vector<uint32_t>& pix, int& w, int& h, int maxW, int maxH, int& hrOut);   // defined below
static ui::Image xbox_user_picture() {
    std::lock_guard<std::mutex> l(g_xboxPicM);
    return g_xboxPicState.load(std::memory_order_relaxed) == 2 ? g_xboxPic : ui::Image{};
}
static void xbox_picture_fetch(winrt::Windows::System::User user) {
    init_apartment(apartment_type::multi_threaded);
    std::vector<uint32_t> pix; int w = 0, h = 0, hr = 0;
    std::string raw;
    try {
        auto ref = user.GetPictureAsync(winrt::Windows::System::UserPictureSize::Size64x64).get();
        if (ref) {
            auto stream = ref.OpenReadAsync().get();
            const uint32_t n = stream.Size();
            if (n >= 64 && n <= (4u << 20)) {
                // The stream is read through a DataReader: an IBuffer cannot be
                // built from a raw array_view, and the reader gives one buffer.
                winrt::Windows::Storage::Streams::DataReader rd(stream);
                rd.LoadAsync(n).get();
                std::vector<uint8_t> bytes(n);
                rd.ReadBytes(winrt::array_view<uint8_t>(bytes.data(), bytes.data() + n));
                raw.assign(reinterpret_cast<const char*>(bytes.data()), n);
            } else logf("accounts: Xbox picture size %u rejected", n);
        } else logf("accounts: GetPictureAsync returned no stream");
    } catch (winrt::hresult_error const& e) {
        logf("accounts: GetPictureAsync failed hr=0x%08x", (unsigned)e.code().value);
    } catch (...) {}
    // Fallback: the account picture file the profile APIs expose. On a console
    // GetPictureAsync can answer with nothing even when a picture exists, and
    // this is the same image the system profile page shows.
    if (raw.empty()) {
        try {
            auto file = winrt::Windows::System::UserProfile::UserInformation::GetAccountPicture(
                winrt::Windows::System::UserProfile::AccountPictureKind::SmallImage);
            if (file) {
                auto buf = winrt::Windows::Storage::FileIO::ReadBufferAsync(file).get();
                raw.assign(reinterpret_cast<char const*>(buf.data()), buf.Length());
                logf("accounts: account picture from UserInformation (%u bytes)", (unsigned)buf.Length());
            }
        } catch (winrt::hresult_error const& e) {
            logf("accounts: GetAccountPicture failed hr=0x%08x", (unsigned)e.code().value);
        } catch (...) {}
    }
    if (raw.empty()) { g_xboxPicState = 3; return; }
    if (!wic_decode_buf(raw, pix, w, h, 128, 128, hr)) { logf("accounts: Xbox picture decode failed hr=0x%08x", (unsigned)hr); g_xboxPicState = 3; return; }
    std::lock_guard<std::mutex> l(g_xboxPicM);
    g_xboxPicBuf = std::move(pix);
    g_xboxPic = ui::Image{g_xboxPicBuf.data(), w, h, w};
    g_xboxPicState = 2;
    logf("accounts: Xbox picture ready (%dx%d)", w, h);
}
// Item D32: the console's Xbox GAMERTAG. Windows.System.User's documented
// properties carry no gamertag — DisplayName is the Microsoft account name,
// which is what the library was showing — but GetPropertyAsync takes a property
// NAME, and the console's user property store answers "Gamertag" with the Xbox
// gamertag. That is the route that works on a sideloaded app; the Xbox Live
// identity APIs (XblIdpAuthManager, the xboxlive.com account provider) answer
// E_ACCESSDENIED to an app that is not a trusted partner, so they are not used.
// The name itself is never logged.
static void xbox_users_fetch() {
    init_apartment(apartment_type::multi_threaded);
    std::vector<std::string> names;
    try {
        // GetDefault is the documented way to get the user running this process
        // ("available from Windows 10, version 2104"); the UserType overloads
        // are deprecated and return an EMPTY list on Xbox, which is why the
        // library showed the Steam persona instead of the console's gamertag.
        // FindAllAsync() (no filter) stays as the fallback for a session that
        // reports more than one signed-in user.
        std::vector<winrt::Windows::System::User> users;
        try {
            auto self = winrt::Windows::System::User::GetDefault();
            if (self) { users.push_back(self); logf("accounts: User.GetDefault() returned the process user"); }
        } catch (winrt::hresult_error const& e) { logf("accounts: User.GetDefault failed hr=0x%08x", (unsigned)e.code().value); }
        if (users.empty()) {
            auto any = winrt::Windows::System::User::FindAllAsync().get();
            logf("accounts: FindAllAsync() reported %u user(s)", any.Size());
            for (auto const& u : any) users.push_back(u);
        }
        for (auto const& u : users) {
            // The console's user property store answers by NAME, and the Xbox
            // properties are not in KnownUserProperties. Ask for the Xbox ones
            // first (a console that has them answers with the gamertag), then
            // the two documented properties: AccountName is the Xbox account
            // name on a console, DisplayName the Microsoft account's name.
            // xbox_gamertag::choose_name_from_properties picks between them and
            // an address is never a name.
            static const char* const kPropertyNames[] = {
                "Gamertag", "XboxGamertag", "ModernGamertag", "XboxUserName",
                "AccountName", "DisplayName"};
            std::vector<xbox_gamertag::Property> props;
            for (const char* propName : kPropertyNames) {
                std::string value;
                try {
                    auto v = u.GetPropertyAsync(winrt::to_hstring(propName)).get();
                    if (v) value = winrt::to_string(winrt::unbox_value<winrt::hstring>(v));
                } catch (winrt::hresult_error const& e) {
                    // A property the store does not carry is the expected answer
                    // for the Xbox names on a build without them; log only that
                    // it was absent, never a value.
                    logf("accounts: property %s unavailable hr=0x%08x", propName, (unsigned)e.code().value);
                } catch (...) {}
                if (!value.empty()) props.push_back({propName, value});
            }
            std::string chosenFrom;
            std::string name = xbox_gamertag::choose_name_from_properties(props, &chosenFrom);
            if (!chosenFrom.empty()) logf("accounts: name came from the %s property", chosenFrom.c_str());
            if (name.empty()) name = "Xbox user";
            names.push_back(name);
            if (names.size() == 1) {   // the first user is the one the sidebar names
                g_xboxPicState = 1;
                winrt::Windows::System::User first = u;
                std::thread([first] { xbox_picture_fetch(first); }).detach();
            }
        }
    } catch (...) { logf("accounts: Windows.System.User unavailable on this console"); }
    { std::lock_guard<std::mutex> l(g_xboxM); g_xboxNames = names; }
    g_xboxState = names.empty() ? 3 : 2;
    logf("accounts: %zu Xbox user(s) found (names not logged)", names.size());
}
static void start_xbox_users_fetch() {
    int st = 0;
    if (g_xboxState.compare_exchange_strong(st, 1)) std::thread(xbox_users_fetch).detach();
}
// Item D14: the account list's rows — every Xbox user the console reports,
// plus the Steam row (item D16: the account when signed in, the sign-in action
// when not). The order is Xbox users first, Steam last, so the row the sidebar
// names is first. The count does not change with the sign-in state.
static int accounts_count() { return (int)xbox_user_names().size() + 1; }

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
struct Game { uint32_t appid = 0; std::string name; uint32_t playtime = 0; uint64_t lastPlayed = 0; std::atomic<int> cover{0}; int iw = 0, ih = 0; std::vector<uint32_t> pix;
              float nameW = 0;   // measured tile-label width (item 11 marquee); 0 = not measured yet (remeasured once the font loads)
};
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

// Item D21: the download queue's rows show the same tall library art the grid
// tile uses. The queue is short and its rows are always on screen, so the
// covers are fetched on demand here (the grid's lazy worker only knows the
// visible grid rows) and cached per appid for the session.
static std::mutex g_qcoverM;
static std::map<uint32_t, std::shared_ptr<Game>> g_qcovers;   // guarded by g_qcoverM
static void queue_cover_fetch(uint32_t appid) {
    init_apartment(apartment_type::multi_threaded);
    auto g = std::make_shared<Game>();
    g->appid = appid;
    const char* kinds[] = {"library_600x900.jpg", "header.jpg"}; bool ok = false;
    for (int k = 0; k < 2 && !ok; k++) {
        std::string url = "https://cdn.cloudflare.steamstatic.com/steam/apps/" + std::to_string(appid) + "/" + kinds[k];
        Resp r = http(false, url, "", "qcover");
        if (r.status != 200 || r.body.size() < 100) continue;
        int hr = 0; if (wic_decode(r.body, *g, hr)) ok = true; else logf("qcover appid=%u %s WIC decode failed hr=0x%08x", appid, kinds[k], (unsigned)hr);
    }
    if (!ok) { g->pix.clear(); logf("qcover: appid=%u no art", appid); }
    g->cover = ok ? 1 : 2;
    std::lock_guard<std::mutex> l(g_qcoverM);
    g_qcovers[appid] = std::move(g);
}
static ui::Image queue_cover(uint32_t appid) {   // every frame from the draw: starts the fetch once, never blocks
    std::lock_guard<std::mutex> l(g_qcoverM);
    auto it = g_qcovers.find(appid);
    if (it == g_qcovers.end()) { g_qcovers[appid] = nullptr; std::thread(queue_cover_fetch, appid).detach(); return {}; }
    if (!it->second || it->second->cover != 1 || it->second->pix.empty()) return {};
    return ui::Image{it->second->pix.data(), it->second->iw, it->second->ih, it->second->iw};
}

// The signed-in account's Steam avatar (issue 51 item 24): hex form of the
// persona push's avatar_hash, the decoded ARGB pixels, and the fetch state —
// same lazy one-shot pattern as the detail page's hero banner. The pixels are
// fetched on a worker thread (WinHTTP + WIC) and never block the UI; the
// decoded copy is cached for the session. The hash and the pixels are the
// owner's own: never logged, never written outside the process.
static std::mutex g_avatarM; static std::string g_avatarHex;
static ui::Image g_avatarPix;   // px points into g_avatarBuf (guarded by g_avatarM)
static std::vector<uint32_t> g_avatarBuf;
static std::atomic<int> g_avatarState{0};   // 0 none, 1 loading, 2 ready, 3 failed
static void avatar_fetch() {
    init_apartment(apartment_type::multi_threaded);
    std::string hex;
    { std::lock_guard<std::mutex> l(g_avatarM); hex = g_avatarHex; }
    if (hex.empty()) { g_avatarState = 3; return; }
    std::string url = steam_presence::avatar_url(hex);
    Resp r = http(false, url, "", "avatar");
    if (r.status != 200 || r.body.size() < 100) { logf("avatar: fetch failed (status %d)", r.status); g_avatarState = 3; return; }
    std::vector<uint32_t> pix; int w = 0, h = 0, hr = 0;
    if (!wic_decode_buf(r.body, pix, w, h, 184, 184, hr)) { logf("avatar: WIC decode failed hr=0x%08x", (unsigned)hr); g_avatarState = 3; return; }
    auto held = std::make_shared<std::vector<uint32_t>>(std::move(pix));
    std::lock_guard<std::mutex> l(g_avatarM);
    g_avatarBuf = std::move(*held);
    g_avatarPix = ui::Image{g_avatarBuf.data(), w, h, w};
    g_avatarState = 2;
}
static void start_avatar_fetch() {
    int state = g_avatarState.load(std::memory_order_relaxed);
    // 0 none or 3 failed: (re)start. A failed fetch retries on the next call;
    // loading (1) and ready (2) stay.
    while (state == 0 || state == 3) {
        if (g_avatarState.compare_exchange_weak(state, 1)) {
            std::thread(avatar_fetch).detach();
            return;
        }
    }
}
// Item D25: the decoded Steam avatar for the PROFILE page (the account list).
// The library's sidebar row shows the Xbox user's picture instead, so this is
// the only place the Steam identity's face appears.
static ui::Image steam_avatar() {
    std::lock_guard<std::mutex> l(g_avatarM);
    return g_avatarState.load(std::memory_order_relaxed) == 2 ? g_avatarPix : ui::Image{};
}

// ---------------------------------------------------------------- detail-page hero banner (item 13)
// Steam's wide library_hero.jpg for the game page background, fetched
// asynchronously when the detail page opens (header.jpg fallback). One
// decoded copy per appid, kept while the page shows it, freed on close.
struct Hero { std::shared_ptr<std::vector<uint32_t>> pix; int w = 0, h = 0; int state = 0; };   // state: 0 none, 1 loading, 2 ready, 3 failed
static std::mutex g_heroM;
static std::map<uint32_t, Hero> g_heroes;   // guarded by g_heroM
static void hero_fetch(uint32_t appid) {
    init_apartment(apartment_type::multi_threaded);
    const char* kinds[] = {"library_hero.jpg", "header.jpg"};
    for (int k = 0; k < 2; k++) {
        std::string url = "https://cdn.fastly.steamstatic.com/steam/apps/" + std::to_string(appid) + "/" + kinds[k];
        Resp r = http(false, url, "", "hero");
        if (r.status != 200 || r.body.size() < 100) continue;
        std::vector<uint32_t> pix; int w = 0, h = 0, hr = 0;
        if (!wic_decode_buf(r.body, pix, w, h, 1920, 620, hr)) { logf("hero appid=%u %s WIC decode failed hr=0x%08x", appid, kinds[k], (unsigned)hr); continue; }
        auto held = std::make_shared<std::vector<uint32_t>>(std::move(pix));
        { std::lock_guard<std::mutex> l(g_heroM); Hero& h2 = g_heroes[appid]; h2.pix = held; h2.w = w; h2.h = h; h2.state = 2; }
        return;
    }
    logf("hero: appid=%u no hero image (fallback failed too)", appid);
    { std::lock_guard<std::mutex> l(g_heroM); g_heroes[appid].state = 3; }
}
static Hero hero_get(uint32_t appid) {   // every frame from the draw: starts the fetch once, never blocks
    std::lock_guard<std::mutex> l(g_heroM);
    Hero& h = g_heroes[appid];
    if (h.state == 0) { h.state = 1; std::thread(hero_fetch, appid).detach(); }
    return h;   // shared_ptr copy, no pixel copy
}
static void hero_release(uint32_t appid) { std::lock_guard<std::mutex> l(g_heroM); g_heroes.erase(appid); }
static std::shared_ptr<CmSession> user_session();   // store.h asks Steam for the depot size on the account's own session
#include "store.h"

static std::atomic<int> g_sortMode{ui_nav::kSortAlphabetical};   // item 25: the launcher opens on Alphabetical
// item 25: mode order 0 Alphabetical, 1 Most played, 2 Recently played —
// the same indexes the sort popup shows (ui_nav::kSortAlphabetical...).
static const char* SORT_NAMES[] = {"Alphabetical", "Most played", "Recently played"};
static std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static void sort_games(int mode) {   // caller holds g_gm
    logf("grid list resort/reload mode=%d count=%zu (position unchanged)", mode, g_games.size());
    std::stable_sort(g_games.begin(), g_games.end(), [mode](const std::shared_ptr<Game>& a, const std::shared_ptr<Game>& b) {
        if (mode == ui_nav::kSortMostPlayed) return a->playtime > b->playtime;
        if (mode == ui_nav::kSortRecentlyPlayed) return a->lastPlayed > b->lastPlayed;
        return lower(a->name) < lower(b->name); });
}

// ---------------------------------------------------------------- app state + network thread
enum Screen { S_BOOT, S_LOGIN, S_LOADING, S_LIBRARY, S_DETAIL, S_QUEUE, S_STORAGE };
// Item D16: the library is the first screen. Steam's session is restored in the
// background and never gates the UI, so no startup splash is shown on launch.
static std::atomic<int> g_screen{S_LIBRARY};
static std::mutex g_sm;                       // guards the strings below
static std::string g_status = "Starting...", g_qrUrl, g_err;
static std::atomic<bool> g_scanned{false}, g_refreshReq{false}, g_logoutReq{false}, g_reloadReq{false};
// Item D16: the Steam session runs in the BACKGROUND — the library is the
// first screen and never waits on it. These atomics say what it is doing:
//   g_steamSignedIn: a Steam account is attached (session restored or QR done)
//   g_steamRestore:  0 not tried, 1 in flight, 2 done, 3 failed (retryable), 4 no saved session
//   g_signInReq:     the account popup asked for the QR screen
static std::atomic<bool> g_steamSignedIn{false};
static std::atomic<int> g_steamRestore{0};
static std::atomic<bool> g_signInReq{false};
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
    // Item D16: the library is already on screen — it fills in when the fetch
    // lands, and a failure only sets the error line (the grid stays usable).
    if (!fetch_library(access, steamid)) { { std::lock_guard<std::mutex> l(g_sm); g_err = "Could not load your games"; } g_screen = S_LIBRARY; return; }
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
    // Item D16: same as enter_library — the library is already up and fills in;
    // a failure leaves the grid and sets the error line, never a splash.
    if (!fetch_library_cm(cm)) { { std::lock_guard<std::mutex> l(g_sm); g_err = "Could not load your games"; } g_screen = S_LIBRARY; return; }
    { std::lock_guard<std::mutex> l(g_sm); g_err.clear(); }
    start_covers(); g_screen = S_LIBRARY;
}

// Persona name + avatar hash for the sidebar profile row (issue 51 item 24).
// Root cause of the empty name fixed here: player_name is field 15 of
// CMsgClientPersonaState's friends[] entry (avatar_hash 31), not field 4 —
// steam_persona.h's parser and issue #180 both carry
// 15, and the hand-built protobuf test in tests/steam_presence_test.cpp pins
// the shape. Runs on the CM reader thread. The keeper in launch.h replaces
// the push handler when a game launches; the launcher restarts when the game
// ends, so the name keeps working for every library screen. Nothing here is
// ever logged: the name and the hash are the owner's own.
static void persona_push(const JobMsg& m) {
    if (m.emsg != steam_presence::ClientPersonaState) return;
    uint64_t self = 0; { auto s = user_session(); if (s) self = s->steamid; }
    if (!self) return;
    auto p = steam_presence::parse_persona(m.body, self);
    if (!p.found) return;
    bool changed = false;
    if (!p.player_name.empty()) {
        std::lock_guard<std::mutex> l(g_personaM);
        if (g_personaName != p.player_name) { g_personaName = p.player_name; changed = true; logf("persona: name updated (%zu chars, not logged)", p.player_name.size()); }
    }
    if (!p.avatar_hash.empty()) {
        std::string hex = steam_presence::avatar_hash_hex(p.avatar_hash);
        if (!hex.empty()) {
            std::lock_guard<std::mutex> l(g_avatarM);
            if (g_avatarHex != hex) { g_avatarHex = hex; changed = true; logf("persona: avatar hash updated (not logged)"); }
        }
    }
    if (changed) start_avatar_fetch();
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
    // Item D16: the library is the FIRST screen and never waits on Steam. The
    // saved session is restored here, in the background; the sidebar's account
    // row carries the state ("Connecting to Steam...", "Steam offline", "Sign
    // in required"). The QR screen is only ever reached from the account popup.
    g_screen = S_LIBRARY;
    std::string savedAccess; double lastRelog = 0;
    bool haveToken = load_token2(refresh, savedAccess);
    if (haveToken) g_steamRestore = 1; else g_steamRestore = 4;   // nothing saved: signed out, not "offline"
    g_authResolved = true;   // item D16: the session question is answered by the flags above; nothing waits on it now
    bool restorePending = haveToken;
    int restoreAttempts = 0;
    while (!g_quit) {
        // ---- the background restore (one attempt per loop pass, never blocking the UI)
        if (restorePending) {
            int er = 0; std::string err; auto s = cm_logon(refresh, er, err);
            auto result = restore_result(bool(s), er, refresh);
            if (result == session_restore::Result::restored) {
                logf("session restored via CM logon (refresh token)"); adopt_user_session(s);
                g_steamSignedIn = true; g_steamRestore = 2; restorePending = false;
                enter_library_cm(*s);
            } else if (result == session_restore::Result::qr) {
                // the saved token is dead: stay on the library, signed out, and
                // let the account popup offer Sign in (never force the QR screen)
                g_steamRestore = 4; g_steamSignedIn = false; restorePending = false; refresh.clear(); access.clear();
                set_status("Sign in to Steam");
            } else if (++restoreAttempts >= 3) {
                logf("session restore: giving up after %d attempts (eresult %d); library stays usable", restoreAttempts, er);
                g_steamRestore = 3; restorePending = false;
            } else {
                logf("session restore: transient failure (eresult %d %s), retry %d", er, err.c_str(), restoreAttempts);
                for (int k = 0; k < (er == 84 ? 600 : 200) && !g_quit; k++) Sleep(100);   // then the next pass retries
            }
            if (restorePending) { Sleep(100); continue; }
        }
        // ---- the QR screen, only on request from the account popup
        if (g_signInReq.exchange(false)) {
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
                    if (s) { adopt_user_session(s); g_steamSignedIn = true; g_steamRestore = 2; enter_library_cm(*s); }
                    else { logf("post-QR CM logon failed (eresult %d %s); trying HTTPS library", er, err.c_str()); g_steamSignedIn = true; g_steamRestore = 2; enter_library(access, jwt_sub(refresh)); }
                    g_authResolved = true; break;
                }
            }
            // B on the QR screen returns to the library, whatever the outcome
            if (g_screen == S_LOGIN) { g_screen = S_LIBRARY; logf("sign-in screen closed; back to the library"); }
            continue;
        }
        // ---- the live screens: reload, logout, session keep-alive
        {
            if (g_reloadReq.exchange(false)) {   // item D15: A on the failure line retries the library fetch now
                auto u = user_session();
                if (u && u->up) { { std::lock_guard<std::mutex> l(g_gm); g_games.clear(); } enter_library_cm(*u); continue; }
                // item D16: no live session — retry the background restore with the saved token
                if (!refresh.empty()) { restoreAttempts = 0; restorePending = true; g_steamRestore = 1; continue; }
            }
            if (g_logoutReq.exchange(false)) {
                delete_token(); if (auto u = user_session()) { u->close(); set_user_session(nullptr); }
                g_steamSignedIn = false; g_steamRestore = 4; refresh.clear(); access.clear();
                { std::lock_guard<std::mutex> l(g_gm); g_games.clear(); }
                g_screen = S_LIBRARY; set_status("Sign in to Steam"); logf("logged out: library stays, account row offers Sign in");
                continue;
            }
            if (!refresh.empty() && !g_demo) {
                auto u = user_session();
                bool want = false;
                if ((!u || !u->up) && now_s() - lastRelog > 20) {   // session dropped: log on again with the same refresh token
                    lastRelog = now_s(); int er = 0; std::string err; auto s = cm_logon(refresh, er, err);
                    if (s) { logf("session re-established via CM logon"); adopt_user_session(s); u = s; want = true; g_steamSignedIn = true; g_steamRestore = 2; }
                    else if (restore_result(false, er, refresh) == session_restore::Result::qr) { g_steamSignedIn = false; g_steamRestore = 4; refresh.clear(); continue; }
                }
                if (want && u && u->up) { { std::lock_guard<std::mutex> l(g_gm); g_games.clear(); } enter_library_cm(*u); }
            }
        }
        Sleep(100);
    }
}

// ---------------------------------------------------------------- gamepad
struct Pad { GamepadButtons prev = GamepadButtons::None; double heldSince = 0, lastRep = 0; GamepadButtons held = GamepadButtons::None; bool stickEdge[4] = {}; };
static bool has(GamepadButtons b, GamepadButtons m) { return (b & m) != GamepadButtons::None; }

struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
    CoreWindow m_window{nullptr};
    CoreDispatcher m_graphicsDispatcher{nullptr};
    bool m_guestSurface=false;
    gpad::Runner m_gpad;   // cmd.txt gpad/gpadseq presses; armed only with a guest running
    int m_guestEndGen=0;
    corewindow_input::Shared m_input; // keyboard/mouse to the guest: the record DLL's one feed, shared with guest SendInput
    bool m_inputLogged=false;
    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::UI::Popups::IUICommand> m_guestDialog{nullptr};
    inline static App* graphicsApp=nullptr;
    bool m_closed = false;
    com_ptr<ID3D11Device> m_dev; com_ptr<ID3D11DeviceContext> m_ctx; com_ptr<IDXGISwapChain1> m_sc; com_ptr<ID3D11Texture2D> m_bb;
    Pad m_pad; int m_prevPads = -1; uint64_t m_frame = 0;
    int m_sel = 0, m_top = 0; double m_scroll = 0, m_scrollT = 0; bool m_btnFocus = true, m_confirmLogout = false;
    bool m_logoutYes = false;   // item 14: logout sheet focus, false = Cancel (never defaults to the destructive button)
    // Item D14: the account list popup (A on the profile row). Rows: the
    // console's Xbox users, the Steam account, Log out of Steam, Cancel last
    // and focused on open.
    bool m_accountsOpen = false;
    int m_accountsSel = 0;
    // library header selectors (item 1): sort dropdown, filters panel, search
    // overlay; m_hdrFocus follows ui_nav::kHeaderControlCount (0 sort, 1
    // filters, 2 search, -1 grid/sidebar).
    int m_hdrFocus = -1; bool m_sortOpen = false; int m_sortFocus = 0;
    bool m_filtersOpen = false; int m_filtersFocus = 0;
    bool m_searchOpen = false; int m_searchKey = 0;
    ui_nav::SearchState m_search;   // item D13: query + caret + caps lock
    bool m_onlyInstalled = false;                            // tab: Installed (the tab stays; Playable filters within it, item 32)
    bool m_filterPlayable = false;                           // item 32: the 'Playable' filter (resets on app start only)
    bool m_filterFavorites = false;                          // item D10: the 'Favorites' filter row
    static constexpr int kFilterRows = 2;                    // Playable, Favorites (the panel's row order)
    ui_nav::Focus m_focus = ui_nav::Focus::grid;             // library: grid tiles or the sidebar
    int m_sidebar = 0;                                       // sidebar row the focus sits on
    int m_qSel = 0;                                          // queue screen: focused row (0 = the Auto-update row, item 28)
    bool m_autoUpdate = true;                                // item 28: the queue's Auto-update toggle state
    int m_stSel = 0;                                         // storage screen: focused install root
    bool m_stOpen = false;                                   // storage: the focused drive's games list is open (item 22)
    int m_stGameSel = 0;                                     // storage games list focus: 0 = "Use for new installs", 1.. games
    int m_stGameScroll = 0;                                  // storage games list: first visible row (draw_storage clamps)
    uint32_t m_stConfirmApp = 0;                             // storage: the appid the delete sheet is open for
    bool m_stDelYes = false;                                 // storage delete sheet focus, false = Cancel (item 14)
    // item 33: the storage action sheet (per game) and the move job
    bool m_stActionOpen = false;                             // action sheet up for the focused game
    int m_stActionSel = 0;                                   // highlighted sheet option (Cancel default)
    uint32_t m_stActionApp = 0;                              // the appid the action sheet is open for
    uint64_t m_stActionBytes = 0;                            // the game's install size (sheet subtitle)
    std::vector<std::string> m_stActionLabels, m_stActionReasons;   // sheet rows (owned strings)
    std::vector<int> m_actionDst;   // per Move-row dst root index; -1 = Delete/Cancel
    bool m_moveSheetOpen = false;                            // progress sheet up while a move runs
    struct MoveJob {
        std::atomic<bool> running{false};
        std::atomic<bool> cancel{false};
        std::atomic<bool> failed{false};                     // copy/verify refused or failed
        std::atomic<bool> done{false};
        uint32_t appid = 0;
        uint64_t total = 0;
        std::atomic<uint64_t> copied{0};
        std::string dstName;                                 // target drive label for the sheet
    };
    MoveJob m_move;
    std::mutex m_moveM;   // guards the move's path strings below
    std::wstring m_moveSrc, m_moveDst, m_moveMarkerSrc, m_moveMarkerDst;
    std::string m_moveMarkerText;
    move_game::Progress m_moveProgress;   // the worker fills it; the progress sheet reads it

    // item 33: open the action sheet for the focused game row (A on a game in
    // a drive's list). Options: one 'Move to <label>' per other usable root
    // (disabled with the plan refusal's reason), then Delete, then Cancel
    // (default focus, item 14). Nothing runs until A on an enabled row.
    void storage_open_action(uint32_t appid, uint64_t bytes, const char* via) {
        bool busy = false;
        { std::lock_guard<std::mutex> l(g_dqm); busy = g_fsDownloading || g_fsDeleting || !g_dq.empty(); }
        m_stConfirmApp = 0;
        m_stActionApp = appid;
        m_stActionBytes = bytes;
        m_stActionLabels.clear();
        m_stActionReasons.clear();
        m_actionDst.clear();
        if (m_stSel < (int)m_roots.size()) {
            const std::string src_root = m_roots[m_stSel].root;
            installs::Info ii = installs::read(g_dir, appid);
            for (size_t i = 0; i < m_roots.size(); ++i) {
                if (i == (size_t)m_stSel) continue;   // no 'Move to' onto the same drive
                const storage_roots::Choice& dst = m_roots[i];
                std::wstring dstDir = (dst.root.empty() ? g_dir : installs::ascii(dst.root)) + L"\\" + installs::ascii(ii.dir);
                bool dst_exists = !ii.dir.empty() && GetFileAttributesW(dstDir.c_str()) != INVALID_FILE_ATTRIBUTES;
                auto ref = move_game::plan_check(busy, src_root, dst.root, bytes, dst_exists, dst.freeBytes);
                m_actionDst.push_back((int)i);   // Move row → its target drive index
                m_stActionLabels.push_back("Move to " + dst.name);
                m_stActionReasons.push_back(ref == move_game::Refusal::none ? std::string() : move_game::refusal_text(ref));
            }
        }
        m_actionDst.push_back(-1);   // Delete row
        m_stActionLabels.push_back("Delete");
        m_stActionReasons.push_back(std::string());
        m_actionDst.push_back(-1);   // Cancel row
        m_stActionLabels.push_back("Cancel");
        m_stActionReasons.push_back(std::string());
        m_stActionSel = (int)m_stActionLabels.size() - 1;   // item 14: default focus on Cancel
        m_stActionOpen = true;
        logf("storage: action sheet open appid=%u (%s)", appid, via);
    }

    // item 33: start the background move (A on an enabled 'Move to' row). The
    // paths are captured under m_moveM; the worker runs move_game::run with
    // stop = m_move.cancel and reports done/failed on the job.
    void storage_start_move(size_t dst_index, const char* via) {
        if (m_move.running.load(std::memory_order_acquire)) return;
        if (dst_index >= m_roots.size() || m_stSel >= m_roots.size()) return;
        const storage_roots::Choice& dst = m_roots[dst_index];   // the row's target drive
        installs::Info ii = installs::read(g_dir, m_stActionApp);
        if (!ii.installed) { logf("storage: move refused appid=%u, no install marker", m_stActionApp); return; }
        {
            std::lock_guard<std::mutex> l(m_moveM);
            m_moveSrc = ii.folder;
            m_moveDst = (dst.root.empty() ? g_dir : installs::ascii(dst.root)) + L"\\" + installs::ascii(ii.dir);
            m_moveMarkerSrc = installs::marker_path(g_dir, m_stActionApp);
            m_moveMarkerDst = m_moveMarkerSrc;   // the marker stays in LocalState; only its text changes
            // the marker text keeps installdir and bytes but names the new root
            m_moveMarkerText = "app " + std::to_string(m_stActionApp) +
                               (dst.root.empty() ? std::string() : " root " + dst.root) +
                               " installdir " + ii.dir + "\nbytes " + std::to_string(ii.bytes) + "\n";
        }
        m_move.running = true; m_move.cancel = false; m_move.failed = false; m_move.done = false;
        m_move.appid = m_stActionApp;
        m_move.total = ii.bytes;
        m_move.copied = 0;
        m_move.dstName = dst.name;
        m_moveProgress.copied = 0;   // Progress holds atomics; reset fields in place
        m_moveProgress.files = 0;
        m_moveProgress.total = ii.bytes;
        m_moveSheetOpen = true;
        std::wstring src, dstw, msrc, mdst; std::string mtext;
        { std::lock_guard<std::mutex> l(m_moveM); src = m_moveSrc; dstw = m_moveDst; msrc = m_moveMarkerSrc; mdst = m_moveMarkerDst; mtext = m_moveMarkerText; }
        std::thread([this, src, dstw, msrc, mdst, mtext] {
            bool ok = move_game::run(src, dstw, msrc, mdst, mtext, m_moveProgress, &m_move.cancel);
            m_move.failed = !ok;
            m_move.done = true;
            m_move.running = false;
        }).detach();
        logf("storage: move started appid=%u (%s)", m_stActionApp, via);
    }

    // item 33: per-frame tick — a finished move refreshes the roots and the
    // installed set, closes the sheets and logs one line (no names/paths).
    void move_tick(double t) {
        if (!m_moveSheetOpen || !m_move.done.load(std::memory_order_acquire)) return;
        m_moveSheetOpen = false;
        bool ok = !m_move.failed.load(std::memory_order_acquire);
        refresh_roots("move finished");
        refresh_installed(t);
        if (ok) logf("storage: move done appid=%u bytes=%llu", m_move.appid, (unsigned long long)m_move.total);
        else logf("storage: move FAILED appid=%u (source left intact)", m_move.appid);
        if (m_stActionOpen) m_stActionOpen = false;
        m_stActionApp = 0;
    }

    std::set<uint32_t> m_installed; double m_installedT = -10; int m_installedDel = -1, m_installedDl = -1;
    void refresh_installed(double t) {                       // dl_<appid>.done markers; rescanned every 2 s and after a download/delete
        if (t - m_installedT < 2 && m_installedDel == g_del.gen && m_installedDl == g_dlSeq) return;
        m_installed = installed_appids(); m_installedT = t; m_installedDel = g_del.gen; m_installedDl = g_dlSeq;
    }
    // item 32: the Playable set — built-in owner-verified appids plus any
    // extras from LocalState\playable.txt (one appid per line, '#' comments).
    // Loaded lazily once; the file is owner-written on the console, no watch.
    std::set<uint32_t> m_playableExtra;
    bool m_playableLoaded = false;
    bool is_playable(uint32_t appid) {
        if (playable::is_builtin(appid)) return true;
        if (!m_playableLoaded) {
            m_playableLoaded = true;
            std::string text;
            if (read_file_s(g_dir + L"\\playable.txt", text))
                for (uint32_t id : playable::parse_txt(text)) m_playableExtra.insert(id);
            if (!m_playableExtra.empty()) logf("playable: %zu extra appid(s) from playable.txt", m_playableExtra.size());
        }
        return m_playableExtra.count(appid) != 0;
    }
    // item 32: the tab list, then the Playable filter WITHIN it, then the
    // live search query (case-insensitive substring on the name). The caller
    // sorts afterwards (sort_games), so order is the library's.
    std::vector<std::shared_ptr<Game>> shown_games() {   // caller holds g_gm
        auto tab = game_library::shown(g_games, m_onlyInstalled, m_installed);
        if (m_filterPlayable) tab = ui_nav::tab_games(tab, true, [this](uint32_t a) { return is_playable(a); });
        if (m_filterFavorites) {   // item D10: the Favorites filter row
            load_favorites();
            std::vector<std::shared_ptr<Game>> only;
            for (auto& g : tab) if (favorites::contains(m_favorites, g->appid)) only.push_back(g);
            tab.swap(only);
        }
        if (!m_search.query.empty()) {
            std::string q;
            q.reserve(m_search.query.size());
            for (char c : m_search.query) q += (char)std::tolower((unsigned char)c);
            std::vector<std::shared_ptr<Game>> out;
            for (auto& g : tab) {
                std::string lower = g->name;
                for (char& c : lower) c = (char)std::tolower((unsigned char)c);
                if (lower.find(q) != std::string::npos) out.push_back(g);
            }
            tab.swap(out);
        }
        return tab;
    }
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
    bool m_delYes = false;   // item 14: delete sheet focus, false = Cancel (never defaults to the destructive button)
    double m_dlStartT = -1e9;   // when this page started the install: A cancels a download only after detail_input::cancel_guard
    uint32_t m_dlWatch = 0; int m_dlWatchSeq = 0;   // the install this page started: on finish it refreshes, never launches
    game_library::DeleteKind m_delKind = game_library::DeleteKind::folder;   // decided when the confirm dialog opens
    InstallInfo m_dInst; double m_dInstT = -10;
    bool m_dBroken = false;   // the install marker's payload is missing/empty (draw_detail re-checks; A reinstalls)
    std::vector<storage_roots::Choice> m_roots; size_t m_rootSel = 0;   // install locations of the detail page (X cycles them before Install)
    bool m_rootsProbed = false;   // the sidebar widget probed the roots once on the library
    // Launch-option picker (launch_picker.h): a game with several run options
    // asks before Play instead of picking one silently.
    launch_picker::State m_pick;
    // New-style detail focus (ui_nav icon slots): 0 primary, 1 trash, 2 cloud,
    // 3 cog; the cog opens the install-location cycle, the cloud opens the
    // cloud sheet (m_cloudOpen / m_cloudFocus).
    int m_dFocus = 0;
    bool m_cloudOpen = false; int m_cloudFocus = 0;
    // Item D10: the game options popup — ONE sheet, opened by the detail
    // cog, by Menu on the detail page and by Menu on the library's focused
    // tile. Rows: Favorite/Unfavorite, Move to <drive> (installed), Sync
    // cloud saves (installed), Delete (installed), Cancel. Destructive rows
    // still go through their own Cancel-default confirm.
    bool m_optOpen = false;
    int m_optSel = 0;                     // highlighted row (Cancel by default)
    uint32_t m_optGame = 0;               // the game the popup is open for (the launch-option cache below is m_optApp)
    std::string m_optName;                // its name (sheet title)
    std::vector<std::string> m_optLabels, m_optReasons;   // rows (owned strings)
    std::vector<int> m_optKind, m_optDst; // per row: OptKind, and the Move row's drive index
    enum OptKind { kOptFavorite = 0, kOptMove = 1, kOptCloud = 2, kOptDelete = 3, kOptLocation = 4, kOptCancel = 5 };
    // Item D10: favorites live in LocalState\favorites.txt and survive
    // restarts; the set is the source of truth for the tile/header markers.
    std::set<uint32_t> m_favorites;
    bool m_favoritesLoaded = false;
    void load_favorites() {
        if (m_favoritesLoaded) return;
        m_favoritesLoaded = true;
        std::string text;
        if (read_file_s(g_dir + L"\\favorites.txt", text)) m_favorites = favorites::parse(text);
        logf("favorites: %zu game(s) loaded", m_favorites.size());
    }
    void save_favorites() { write_text(g_dir + L"\\favorites.txt", favorites::format(m_favorites)); }
    bool is_favorite(uint32_t appid) { load_favorites(); return favorites::contains(m_favorites, appid); }
    void toggle_favorite(uint32_t appid, const char* via) {
        load_favorites();
        const bool now = favorites::toggle(m_favorites, appid);
        save_favorites();
        logf("favorites: appid=%u -> %s (%s)", appid, now ? "favorite" : "not favorite", via);
    }
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
        m_dApp = id; m_dName = nm.empty() ? "App " + std::to_string(id) : nm; m_dNote.clear(); m_dScroll = 0; m_dInstT = -10; m_dBroken = false; m_confirmDelete = false; m_delYes = false; m_dlStartT = -1e9; m_dlWatch = 0;
        launch_picker::close(m_pick);   // a picker left open belongs to the previous game
        // header popups are closed with the screen (item 15: only one can be open)
        m_sortOpen = false; m_sortFocus = 0; m_hdrFocus = -1; m_filtersOpen = false; m_filtersFocus = 0; m_searchOpen = false;
        // After a restart that unloaded a failed game, its page says why (this session's launches show in the launch panel).
        if (g_launch.phase == 0) { std::string why = launch_report::failure(read_bytes(g_dir + L"\\launch_result.json"), id); if (!why.empty()) m_dNote = "Last launch failed: " + why; }
        m_roots.clear(); m_rootSel = 0; refresh_roots("detail open");
        m_optApp = 0;   // a fresh page: the run-option count is read again (a download may have cached appinfo)
        auto si = std::make_shared<StoreInfo>(); si->appid = id; si->want_depot = !read_install(id).installed; m_dSi = si; std::thread(store_worker, si).detach();
        { InstallInfo ii = read_install(id); if (ii.installed && !g_demo) cloud_async(id, ii.folder, false); }   // status line: what the cloud holds
        m_confirmLogout = false; m_logoutYes = false; m_accountsOpen = false; m_cloudOpen = false; m_cloudFocus = 0; m_dFocus = 0;
        g_screen = S_DETAIL; logf("detail: open appid=%u name='%s' via=%s", id, m_dName.c_str(), via);
    }
    void close_detail(const char* why) {
        // nothing of this page leaks into the next one (CONSOLE-ISSUES 3)
        logf("detail: back to library (%s)", why); hero_release(m_dApp); m_dSi.reset(); m_confirmDelete = false; m_delYes = false; m_cloudOpen = false; m_cloudFocus = 0; m_dFocus = 0; g_screen = S_LIBRARY;
    }
    // A on a sidebar entry, shared by the library, queue and storage screens
    // (the sidebar stays live on every screen that draws it). Item 14's new
    // order: 0 PROFILE (opens the logout sheet), 1 Search (opens the search
    // overlay), 2/3 library views (All games / Installed), 4 download queue,
    // 5 storage management.
    void sidebar_activate(double t, const char* source) {
        int target = ui_nav::sidebar_target(m_sidebar);
        if (target == ui_nav::kSidebarProfile) {   // item D14: the account list, not the logout sheet directly
            g_screen = S_LIBRARY; m_accountsOpen = true; m_accountsSel = ui_nav::accounts_open_sel(accounts_count());
            logf("sidebar: profile row -> accounts popup, focus on Cancel (%s)", source);
        }
        else if (target == ui_nav::kSidebarSearch) { g_screen = S_LIBRARY; m_hdrFocus = -1; m_focus = ui_nav::Focus::grid; ui_nav::popup_exclusive(m_sortOpen, m_filtersOpen, m_searchOpen, 2); m_searchKey = 0; ui_nav::search_clamp_caret(m_search); logf("sidebar: search overlay open (%s)", source); }   // the overlay draws on the library
        else if (target == 2 || target == 3) {
            bool want = target == 3;
            if (m_onlyInstalled != want) { m_onlyInstalled = want; m_installedT = -10; refresh_installed(t); grid_position(0, 0, "sidebar view"); }
            g_screen = S_LIBRARY;
            logf("sidebar: %s view (%zu installed)", m_onlyInstalled ? "installed" : "all games", m_installed.size());
        } else if (target == 4) { m_qSel = 0; m_focus = ui_nav::Focus::sidebar; g_screen = S_QUEUE; logf("sidebar: download queue (%s)", source); }
        else if (target == 5) { refresh_roots("storage screen"); m_stSel = m_rootSel < m_roots.size() ? (int)m_rootSel : 0; m_stOpen = false; m_stGameSel = 0; m_focus = ui_nav::Focus::grid; g_screen = S_STORAGE; logf("sidebar: storage management, drive list focused (%s)", source); }
    }
    int m_dInstDel = -1;   // g_del.gen seen by m_dInst: a finished delete refreshes it at once
    void refresh_install(double t) { if (t - m_dInstT > 0.5 || m_dInstDel != g_del.gen) { m_dInstDel = g_del.gen; m_dInst = read_install(m_dApp); m_dInstT = t; m_dBroken = false; } }
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
    // ---- Item D10: the game options popup (detail cog, Menu on detail and
    // Menu on the library's focused tile all open THIS one).
    // Rows: Favorite/Unfavorite always; for an installed game also one
    // 'Move to <drive>' per other usable root (disabled with the plan's
    // reason), 'Sync cloud saves' and 'Delete'; Cancel last and focused
    // first. Destructive rows keep their own Cancel-default confirm.
    void open_options(uint32_t appid, const char* via) {
        if (!appid) return;
        load_favorites();
        refresh_install(now_s());
        InstallInfo ii = appid == m_dApp ? m_dInst : read_install(appid);
        m_optGame = appid;
        m_optName = game_name(appid);
        if (m_optName.empty()) m_optName = "App " + std::to_string(appid);
        m_optLabels.clear(); m_optReasons.clear(); m_optKind.clear(); m_optDst.clear();
        m_optLabels.push_back(favorites::contains(m_favorites, appid) ? "Remove from favorites" : "Add to favorites");
        m_optReasons.push_back(std::string()); m_optKind.push_back(kOptFavorite); m_optDst.push_back(-1);
        if (ii.installed) {
            refresh_roots("game options");
            bool busy = false;
            { std::lock_guard<std::mutex> l(g_dqm); busy = g_fsDownloading || g_fsDeleting || !g_dq.empty(); }
            for (size_t i = 0; i < m_roots.size(); ++i) {
                const storage_roots::Choice& dst = m_roots[i];
                if (dst.root == ii.root) continue;   // no 'Move to' onto the drive it already sits on
                std::wstring dstDir = (dst.root.empty() ? g_dir : installs::ascii(dst.root)) + L"\\" + installs::ascii(ii.dir);
                bool dst_exists = !ii.dir.empty() && GetFileAttributesW(dstDir.c_str()) != INVALID_FILE_ATTRIBUTES;
                auto ref = move_game::plan_check(busy, ii.root, dst.root, ii.bytes, dst_exists, dst.freeBytes);
                m_optLabels.push_back("Move to " + dst.name);
                m_optReasons.push_back(ref == move_game::Refusal::none ? std::string() : move_game::refusal_text(ref));
                m_optKind.push_back(kOptMove); m_optDst.push_back((int)i);
            }
            m_optLabels.push_back("Sync cloud saves");
            m_optReasons.push_back(std::string()); m_optKind.push_back(kOptCloud); m_optDst.push_back(-1);
            m_optLabels.push_back("Delete");
            m_optReasons.push_back(std::string()); m_optKind.push_back(kOptDelete); m_optDst.push_back(-1);
        } else {
            // the cog used to cycle the install location; that option lives
            // in this popup now, so the cog no longer hides it
            m_optLabels.push_back("Change install location");
            m_optReasons.push_back(std::string()); m_optKind.push_back(kOptLocation); m_optDst.push_back(-1);
        }
        m_optLabels.push_back("Cancel");
        m_optReasons.push_back(std::string()); m_optKind.push_back(kOptCancel); m_optDst.push_back(-1);
        m_optSel = ui_nav::options_open_sel((int)m_optLabels.size());   // item 14/D10: Cancel is the default focus
        m_optOpen = true;
        logf("options: sheet open appid=%u rows=%zu (%s)", appid, m_optLabels.size(), via);
    }
    // A on an enabled row. Move/Delete/Log out style actions never run from
    // the popup itself: they hand over to the existing sheets.
    void options_activate(const char* via) {
        if (m_optSel < 0 || m_optSel >= (int)m_optKind.size()) return;
        if (ui_nav::options_refused(m_optReasons[(size_t)m_optSel].c_str())) {
            logf("options: row refused (disabled): %s", m_optReasons[(size_t)m_optSel].c_str());
            return;
        }
        const int kind = m_optKind[(size_t)m_optSel];
        const uint32_t appid = m_optGame;
        m_optOpen = false;
        if (kind == kOptCancel) { logf("options: closed (%s)", via); return; }
        if (kind == kOptFavorite) { toggle_favorite(appid, via); return; }
        if (kind == kOptLocation) { detail_storage(via); return; }
        if (kind == kOptMove) {
            const int dsti = m_optDst[(size_t)m_optSel];
            if (dsti < 0 || dsti >= (int)m_roots.size()) return;
            // the move job's source is the game's own drive, not the storage
            // screen's focus: find it in the roots list
            m_stSel = 0;
            for (size_t i = 0; i < m_roots.size(); ++i) {
                installs::Info ii = installs::read(g_dir, appid);
                if (m_roots[i].root == ii.root) { m_stSel = (int)i; break; }
            }
            m_stActionApp = appid;
            m_stActionBytes = read_install(appid).bytes;
            storage_start_move((size_t)dsti, via);
            return;
        }
        if (appid != m_dApp) { g_screen = S_DETAIL; m_dApp = appid; m_dName = m_optName; m_dInstT = -10; m_dFocus = 0; }
        if (kind == kOptCloud) { detail_cloud("game options"); return; }
        if (kind == kOptDelete) { detail_delete("game options"); return; }
    }
    // Up/down walk every row (disabled ones stay focusable, like the storage
    // sheet); B closes. Returns true when the popup owned the press.
    bool options_input(GamepadButtons edge, const char* source) {
        if (!m_optOpen) return false;
        const int n = (int)m_optLabels.size();
        if (!n) { m_optOpen = false; return true; }
        if (has(edge, GamepadButtons::DPadUp)) { ui_nav::options_move(m_optSel, n, false); logf("options: row %d/%d (%s)", m_optSel + 1, n, source); }
        else if (has(edge, GamepadButtons::DPadDown)) { ui_nav::options_move(m_optSel, n, true); logf("options: row %d/%d (%s)", m_optSel + 1, n, source); }
        else if (has(edge, GamepadButtons::A)) options_activate(source);
        else if (has(edge, GamepadButtons::B) || has(edge, GamepadButtons::Menu)) { m_optOpen = false; logf("options: closed (%s)", source); }
        return true;
    }
    // ---- Item D14: the account list popup (A on the profile row). Rows are
    // the console's Xbox users and the Steam row, then the Steam action row
    // and Cancel; it opens on Cancel, so a stray A never logs out. Item D16:
    // the action row is 'Log out of Steam' when signed in (hands over to the
    // Cancel-default confirm sheet) and 'Sign in to Steam' when not (opens the
    // QR screen; B there returns to the library).
    bool accounts_input(GamepadButtons edge, const char* source) {
        if (!m_accountsOpen) return false;
        const int n = accounts_count();
        if (has(edge, GamepadButtons::DPadUp)) { ui_nav::accounts_move(m_accountsSel, n, false); logf("accounts: row %d/%d (%s)", m_accountsSel + 1, n + 2, source); }
        else if (has(edge, GamepadButtons::DPadDown)) { ui_nav::accounts_move(m_accountsSel, n, true); logf("accounts: row %d/%d (%s)", m_accountsSel + 1, n + 2, source); }
        else if (has(edge, GamepadButtons::A)) {
            switch (ui_nav::accounts_activate(m_accountsSel, n)) {
            case ui_nav::AccountAction::cancel: m_accountsOpen = false; logf("accounts: cancelled (%s)", source); break;
            case ui_nav::AccountAction::steam:
                m_accountsOpen = false;
                if (g_steamSignedIn.load()) { m_confirmLogout = true; m_logoutYes = false; logf("accounts: log out -> confirmation sheet, focus on Cancel (%s)", source); }
                else { g_signInReq = true; logf("accounts: sign in -> QR screen (%s)", source); }
                break;
            default: logf("accounts: row %d is information only (%s)", m_accountsSel, source); break;
            }
        }
        else if (has(edge, GamepadButtons::B) || has(edge, GamepadButtons::Menu)) { m_accountsOpen = false; logf("accounts: closed (%s)", source); }
        return true;
    }
    void draw_accounts(ui::Canvas& c) {
        static std::vector<std::string> names, sources;
        static std::vector<const char*> namePtrs, sourcePtrs;
        static std::vector<ui::Image> avatars;
        names.clear(); sources.clear(); namePtrs.clear(); sourcePtrs.clear(); avatars.clear();
        start_xbox_users_fetch();
        const ui::Image xbPic = xbox_user_picture();
        for (const std::string& n : xbox_user_names()) { names.push_back(n); sources.push_back("Xbox"); avatars.push_back(xbPic); }
        // Item D16: the Steam row is the persona when signed in, else the
        // sign-in action; the action row below it says what A will do.
        const bool signedIn = g_steamSignedIn.load();
        const std::string steam = signedIn ? persona_name() : std::string();
        names.push_back(signedIn ? (steam.empty() ? "Steam" : steam) : "Sign in to Steam");
        sources.push_back(signedIn ? "Steam" : "Not signed in");
        // Item D25: the Steam account's own face — this is the profile page,
        // the one place the Steam identity is shown.
        if (signedIn) start_avatar_fetch();
        avatars.push_back(signedIn ? steam_avatar() : ui::Image{});
        for (size_t i = 0; i < names.size(); ++i) { namePtrs.push_back(names[i].c_str()); sourcePtrs.push_back(sources[i].c_str()); }
        ui_launcher::AccountVM v;
        v.names = namePtrs.empty() ? nullptr : namePtrs.data();
        v.sources = sourcePtrs.empty() ? nullptr : sourcePtrs.data();
        v.avatars = avatars.empty() ? nullptr : avatars.data();
        v.n = (int)namePtrs.size();
        v.action_label = ui_nav::account_steam_label(signedIn);
        v.sel = m_accountsSel;
        ui_launcher::draw_accounts_sheet(c, g_font, v);
    }
    // The shared sheet fill (draw side): title = game name, subtitle = size
    // and drive when installed, else the library's only line.
    void draw_options(ui::Canvas& c) {
        ui_launcher::ActionSheetVM a;
        static std::string aTitle, aSubtitle;
        static std::vector<const char*> labelPtrs, reasonPtrs;
        aTitle = clip(m_optName.empty() ? "App " + std::to_string(m_optGame) : m_optName, 36);
        InstallInfo ii = read_install(m_optGame);
        if (ii.installed) {
            std::string where = "Internal";
            for (auto& rc : m_roots) if (rc.root == ii.root) { where = rc.name; break; }
            aSubtitle = (ii.bytes ? storage_roots::size_text(ii.bytes) : std::string("Installed")) + " \xC2\xB7 " + where;
        } else aSubtitle = "Not installed";
        labelPtrs.clear(); reasonPtrs.clear();
        for (const std::string& l : m_optLabels) labelPtrs.push_back(l.c_str());
        for (const std::string& r : m_optReasons) reasonPtrs.push_back(r.c_str());
        a.title = aTitle.c_str(); a.subtitle = aSubtitle.c_str();
        a.labels = labelPtrs.data(); a.reasons = reasonPtrs.data();
        a.n = (int)labelPtrs.size();
        a.sel = m_optSel;
        ui_launcher::draw_action_sheet(c, g_font, a);
    }
    // ---- storage screen (item 22): the focused drive's installed-games list
    // Games installed under the focused root: appid -> (dir, root, bytes) from
    // the dl_<appid>.done markers, name from the library cache, size formatted
    // by storage_roots::size_text at draw time.
    std::vector<ui_nav::StorageGameRow> storage_game_list() {
        std::string drive_root = m_stSel < (int)m_roots.size() ? m_roots[m_stSel].root : std::string();
        std::vector<std::tuple<uint32_t, std::string, std::string, uint64_t, std::string>> recs;
        { std::lock_guard<std::mutex> l(g_gm);
          for (uint32_t id : m_installed) {
              installs::Info ii = read_install(id);
              if (!ii.installed) continue;
              std::string name; for (auto& g : g_games) if (g->appid == id) { name = g->name; break; }
              recs.emplace_back(id, ii.dir, ii.root, ii.bytes, name);
          } }
        auto rows = ui_nav::storage_game_rows(drive_root, recs);
        std::stable_sort(rows.begin(), rows.end(), [](const ui_nav::StorageGameRow& a, const ui_nav::StorageGameRow& b) { return lower(a.name) < lower(b.name); });
        return rows;
    }
    // A on 'Yes, delete' in the storage screen's sheet: the same uninstall as
    // the game page (start_delete), then the list refreshes on the next draw.
    void storage_delete_confirm(const char* via) {
        m_confirmDelete = false;
        std::string why = start_delete(m_stConfirmApp);
        if (!why.empty()) logf("storage: delete not started appid=%u (%s): %s", m_stConfirmApp, via, why.c_str());
        else { logf("storage: delete started appid=%u (%s)", m_stConfirmApp, via); m_stGameSel = 0; }
        m_stConfirmApp = 0;
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
        // a running or queued download is cancelled first, broken install or
        // not: A never queues a second download over one already going
        if (dl_is_mine() || is_queued(m_dApp)) {
            detail_input::State st;
            st.installed = m_dInst.installed;
            st.download = dl_is_mine() ? detail_input::Download::running : detail_input::Download::queued;
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
            default: break;
            }
        }
        // a marker with no payload behind it is not playable: A reinstalls (the
        // status row says the install is broken), it never starts a launch
        if (m_dInst.installed && installs::health_of(g_dir, m_dInst) != installs::Health::ok) {
            m_dNote.clear(); logf("detail: A on a broken install appid=%u (%s) -> reinstall", m_dApp, via);
            if (g_demo) { m_dNote = "Demo list: installing is disabled."; return; }
            if (deleting_now()) { m_dNote = "Wait for the delete to finish."; return; }
            refresh_roots("reinstall");
            const storage_roots::Choice& where = m_roots[m_rootSel];
            if (where.freeBytes < (64ull << 20)) { m_dNote = storage_roots::refusal_text(where, m_roots, m_rootSel); return; }
            set_install_root(m_dApp, where.root);
            // the stale marker would keep its drive (download_app reads the
            // marker before the chosen root): remove it, the payload it named
            // is gone, and the download records itself fresh (begin_partial)
            if (!installs::clear_marker(g_dir, m_dApp)) { m_dNote = "Cannot clear the broken install record (file in use?)"; return; }
            m_dInst.installed = false; m_dBroken = false; m_dInstT = -10;
            m_dlStartT = now_s(); m_dlWatch = m_dApp; m_dlWatchSeq = g_dlSeq;
            logf("detail: reinstall pressed appid=%u (%s) to %s", m_dApp, via, storage_roots::label(where).c_str()); enqueue_download(m_dApp);
            return;
        }
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
    GamepadButtons m_menuPrev=GamepadButtons::None; bool m_leaving=false;
    std::atomic<bool> m_quitClosing{false};   // a graceful close is in flight
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
        double present=0;
        // Both populations go through the pure, total stats helpers (no
        // hand-rolled percentile indexing anywhere): frameMs as its own
        // sample set, and the present-to-present interval (consecutive
        // atMs deltas only) as the other — the delivery cadence the display
        // sees, the number that should sit at the slot (16.7 ms at
        // fpscap=60 on a 60 Hz chain).
        std::vector<double> v,ats;
        for(auto& x:m_frameWindow){v.push_back(x.frameMs);ats.push_back(x.atMs);present+=x.presentMs;}
        present_pacing::SampleStats fr=present_pacing::sample_stats(std::move(v));
        present_pacing::IntervalStats pi=present_pacing::interval_stats(ats);
        logf("frames: n=%u avg=%.1fms p50=%.1f p99=%.1f max=%.1f >25ms=%u >50ms=%u present_avg=%.1fms present_int n=%u avg=%.1fms p50=%.1f p99=%.1f >25ms=%u resizes=%u (last %ux%u count=%u hr=0x%08lx)",fr.n,fr.avg,
             fr.p50,fr.p99,fr.max,fr.over25,fr.over50,present/fr.n,
             pi.n,pi.avg,pi.p50,pi.p99,pi.over25,
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
        // Turning the FPS counter off can strand the selection on the graph
        // row (Settings row 1), which disappears; re-clamp into range.
        auto reclamp=[this](bool on){ if(!on&&ui_guideTab.load()==2&&selection.load()>0)selection=0; };
        // `selection` is the active tab's focused row (guide_press keeps the
        // atomics in sync); map it to the tab's commands.
        const int tab=ui_guideTab.load(),row=selection.load();
        const char* names[3][2]={{"Resume","Quit game"},{"",""},{"FPS counter","Frame-time graph"}};
        const char* name=tab<3&&row<2?names[tab][row]:"";
        if(busy)return;
        // cmd.txt synthetic commands (gquit/gforce/gfps) act without selection.
        if(c=="quit"||c=="force"){menuOpen=true;}
        if(c=="quit"){quit_game(via);return;}
        if(c=="force"){force_quit();return;}
        if(c=="fps"){fpsOn=!fpsOn;write_text(g_dir+L"\\overlay.txt",fpsOn?"fps=1\n":"fps=0\n");logf("overlay: FPS counter now %s",fpsOn?"on":"off");reclamp(fpsOn);return;}
        if(c=="select"&&*name)logf("overlay: %s (tab %d row %d) (%s)",name,tab,row,via);
        if(c=="select"&&tab==0) {
            if(row==0){menuOpen=false;return;}   // Resume: input returns once buttons are released
            if(row==1) {   // Quit game
                quit_game(via);
                return;
            }
            force_quit();   // row 2: Force quit
            return;
        }
        if(c=="select"&&tab==2) {
            if(row==0){fpsOn=!fpsOn;write_text(g_dir+L"\\overlay.txt",fpsOn?"fps=1\n":"fps=0\n");logf("overlay: FPS counter now %s",fpsOn?"on":"off");reclamp(fpsOn);return;}
            if(row==1&&fpsOn){graphOn=!graphOn;logf("overlay: frame-time graph now %s",graphOn?"on":"off");return;}   // reachable only while the counter is on
            return;
        }
        // tab 1 (Achievements): the rows are not commands.
    }
    // Quit game: ask the guest's windows to close on a worker thread and return
    // at once. The menu stays usable (no busy latch), so the player can pick
    // Force quit if the game does not go away; guest_ended() restarts the
    // launcher when the game does exit (the save upload runs there).
    void quit_game(const char* via) {
        if(m_leaving || m_quitClosing.exchange(true)) return;
        guest_overlay::set_status("Closing the game...");
        logf("overlay: quit game -> WM_CLOSE on a worker (%s)", via);
        std::thread([this] {
            const unsigned n = guest_overlay::close_guest_windows();
            logf("overlay: WM_CLOSE posted to %u guest window(s)", n);
            m_quitClosing = false;
            if (!n) { if (auto s = d3d11_diag::sink) s("overlay: no guest window answered WM_CLOSE"); }
        }).detach();
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
        std::string result;bool ok;
        {std::lock_guard<std::mutex> lock(g_launch.m);ok=g_launch.ok;result=g_launch.result;}
        // item D22: a launch the player cancelled never started game code, so
        // there is nothing to unload and no crash to explain — the page just
        // shows the cancellation note. (Checked before the guest-surface guard:
        // a cancelled launch has no surface and no runtime.)
        if(result=="Launch cancelled"){m_guestEndGen=g_launch.gen;logf("launch: cancelled launch ended; staying on the game page");m_dNote=result;return;}
        if(!m_guestSurface && !play_runtime::observed.load(std::memory_order_acquire))return;
        m_guestEndGen=g_launch.gen;
        m_gpad.clear();   // an armed gpad program never outlives its game
        achievement_poller_stop();   // the guest is exiting; stop watching its achievements.json
        m_achPollStarted=false;      // the next launch starts its own poller
        if(m_leaving)return;   // Force quit is already syncing and restarting
        m_leaving=true;m_quitClosing=false;
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
        if (gm && gm->playtime > 0) vTime = playtime::format_minutes(gm->playtime); else vTime = "Not played yet";
        // item D6: the achievement count comes from the files the last sync
        // prepared (schema names + gbe's earned set); without them the row says
        // so instead of showing a made-up figure.
        {
            const std::string ach = steam_achievements::count_from_files(
                std::filesystem::path(provider_dir(m_dApp)) / "steam_settings" / "achievements.json",
                std::filesystem::path(provider_dir(m_dApp)) / "saves" / std::to_string(m_dApp) / "achievements.json");
            vAch = ach.empty() ? "Not tracked yet" : ach;
        }
        if (gm && gm->lastPlayed > 0) { time_t lt = (time_t)gm->lastPlayed; char b[32]; struct tm tmv{}; localtime_s(&tmv, &lt); strftime(b, sizeof b, "%d %b %Y", &tmv); vLast = b; } else vLast = "Never";
        if (deleting) vStatus = "Deleting...";
        else if (down) { char b[48]; snprintf(b, sizeof b, "Downloading %.0f%%", pct * 100); vStatus = b; }
        else if (queued) vStatus = "Queued...";
        else if (m_dInst.installed) {
            // a marker whose payload is gone (deploy wipe, a move that left the
            // marker, a crash) is not an installed game: say so and offer the
            // reinstall instead of a Play that cannot find its files.
            installs::Health h = installs::health_of(g_dir, m_dInst);
            if (h == installs::Health::ok) { vStatus = "Installed"; m_dBroken = false; }
            else { vStatus = h == installs::Health::empty ? "Install broken: files missing" : "Install broken: folder missing"; m_dBroken = true; }
        }
        else vStatus = "Not installed";
        // Item D9: ONE size row. Installed -> the size on disk. Not installed
        // -> the depots' own download size (what the download will fetch), the
        // store's "Storage:" line while that is still being asked for, and
        // "Unknown" when neither is available.
        if (m_dInst.installed) vSize = m_dInst.bytes ? storage_roots::size_text(m_dInst.bytes) : "Unknown";
        else if (down) { char b[48]; snprintf(b, sizeof b, "%s of %s", storage_roots::size_text(g_dl.done).c_str(),
                                              g_dl.total ? storage_roots::size_text(g_dl.total).c_str() : "?"); vSize = b; }
        else if (si && si->depot == 1) vSize = storage_roots::size_text(si->depot_bytes);
        else if (si && si->depot == 2) vSize = (si->state == 1 && !si->download_size.empty()) ? si->download_size : "Unknown";
        else if (si && st == 2 && !si->want_depot) vSize = "Unknown";   // the store could not be read
        else vSize = "Calculating...";
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
        // the marker still claims an install (installed stays true so the
        // delete/cloud rows behave); broken flips the primary button to
        // REINSTALL and takes A's reinstall path instead of a launch
        pv.installed = m_dInst.installed;
        pv.broken = m_dBroken;
        pv.download = down ? detail_input::Download::running : queued ? detail_input::Download::queued : detail_input::Download::none;
        pv.launching = launching; pv.deleting = deleting; pv.restart_required = retained;
        pv.install_pct = float(pct);

        ui_launcher::GameVM g;
        g.title = vTitle.c_str(); g.subtitle = vSub.c_str();
        g.time_played = vTime.c_str(); g.achievements = vAch.c_str(); g.last_played = vLast.c_str();
        g.install_status = vStatus.c_str(); g.game_size = vSize.c_str(); g.location = vLoc.c_str();
        g.last_updated = vUpdated.c_str(); g.cloud_saves = vCloud.c_str();
        g.about = vAbout.c_str(); g.players = vPlayers.c_str(); g.language = vLang.c_str();
        g.primary = pv; g.primary_focus = m_dFocus == 0; g.install_pct = float(pct);
        g.favorite = is_favorite(m_dApp);   // item D10: the star beside the title
        // item 13: the tall box on the right is the TALL library capsule (the
        // same portrait art the tile uses), not the cropped header. A game that
        // is not installed has no library entry to read the cover from, so it
        // is fetched here through the same on-demand path the download rows
        // use; until it lands the box stays empty rather than showing a slab.
        if (gm && gm->cover == 1 && !gm->pix.empty()) { g.has_art = true; g.no_cover = false; g.art = ui::Image{gm->pix.data(), gm->iw, gm->ih, gm->iw}; }
        else {
            ui::Image qa = queue_cover(m_dApp);
            if (qa.px && qa.w > 0 && qa.h > 0) { g.has_art = true; g.no_cover = false; g.art = qa; }
            else { g.has_art = false; g.no_cover = true; }   // nothing drawn: no grey slab over the hero
        }
        // item 13: the wide background is Steam's library_hero.jpg (header.jpg
        // fallback), fetched asynchronously on page open, cached per page.
        Hero hm = hero_get(m_dApp);
        if (hm.state == 2 && hm.pix && !hm.pix->empty()) { g.has_hero = true; g.hero = ui::Image{hm.pix->data(), hm.w, hm.h, hm.w}; }
        else { g.has_hero = false; }
        if (m_focusAch) {   // latest achievement row when the schema is around (steam_achievements.h cache)
            g.achievement = m_achName.c_str(); g.achievement_sub = m_achSub.c_str();
            g.progress_pct = m_achPct;
        }

        // transient note: refusals, cloud warnings, launch panel (one line, newlines flattened)
        vNote.clear();
        if (!m_dNote.empty()) { vNote = m_dNote; for (auto& ch : vNote) if (ch == '\n') ch = ' '; }
        else if (m_dBroken && m_dInst.installed) vNote = "The files behind the install are gone. Press A to reinstall.";
        else if (launching) { if (g_launch.phase == 3) vNote = "Guest running (playability unverified)"; else { char b[64]; snprintf(b, sizeof b, "Launching... (%.0f s)", now_s() - g_launch.started); vNote = b; } }
        else if (retained) vNote = "Launch failed - restarting the launcher to unload it";
        else if (delDone && !m_dInst.installed) vNote = g_del.state == 2 ? "Deleted. Saves were kept." : "Deleted with items left behind; see boot.log";
        else if (deleting) { char b[64]; snprintf(b, sizeof b, "%llu files removed", (unsigned long long)g_del.progress.removed.load()); vNote = b; }
        else if (mine && (dl == 3 || dl == 4) && !m_dInst.installed) { std::string ms; { std::lock_guard<std::mutex> l(g_dl.m); ms = g_dl.msg; } vNote = (dl == 4 ? "Cancelled: " : "Failed: ") + ms; }
        g.focus = m_dFocus;
        // item 7: the Cloud saves row takes the shortened value ("Synced 10 Nov")
        static char cloudShort[64];
        ui_launcher::cloud_saves_short(cloudShort, sizeof cloudShort, vCloud.c_str());
        g.cloud_saves = cloudShort;

        ui::Canvas c(g_fb.data(), W, H, W);
        ui_launcher::draw_game(c, g_font, g, !(m_confirmDelete || m_cloudOpen || m_optOpen));   // item 17: a sheet brings its own hint bar
        ui_launcher::draw_game_note(c, g_font, g, vNote.c_str());
        if (m_pick.open) draw_picker(c);
        if (m_optOpen) draw_options(c);   // item D10: the game options popup
        if (m_confirmDelete) {
            ui_launcher::DeleteVM d;
            static std::string dTitle, dBody;
            dTitle = "Delete " + clip(m_dName, 30) + "?";
            if (m_delKind == game_library::DeleteKind::shared) dBody = "Its files are shared with another installed game and stay.";
            else if (m_delKind == game_library::DeleteKind::unknown_folder) dBody = "Leaves the installed list. Its folder is unknown, so files stay.";
            else if (m_dInst.bytes >= 107374182) dBody = "Frees " + storage_roots::size_text(m_dInst.bytes) + " on this console. Saves are kept.";
            else if (m_dInst.bytes) dBody = "Frees " + storage_roots::size_text(m_dInst.bytes) + " on this console. Saves are kept.";
            else dBody = "Removes the game files from this console. Saves are kept.";
            d.title = dTitle.c_str(); d.body = dBody.c_str(); d.focus_delete = m_delYes;   // item 14: false = the ring sits on Cancel
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
    // 32-bit in the x86 interpreter). Item D18: drawn as the same sheet as the
    // account list and the confirm sheets, not the old bitmap popup.
    void draw_picker(ui::Canvas& c) {
        static std::vector<const char*> labels, notes;
        static std::vector<char> selectable;   // vector<bool> has no data()
        static std::string subtitle;
        labels.clear(); notes.clear(); selectable.clear();
        for (const launch_target::Choice& c : m_pick.rows) {
            labels.push_back(c.label.c_str());
            const char* note = launch_target::arch_note(c.arch);
            notes.push_back(note ? note : (c.arch == launch_target::arch64 ? "64-bit" : ""));
            selectable.push_back(c.selectable ? 1 : 0);
        }
        subtitle = clip(m_dName, 40) + " offers more than one run option";
        ui_launcher::PickerVM v;
        v.subtitle = subtitle.c_str();
        v.labels = labels.data(); v.notes = notes.data(); v.selectable = selectable.data();
        v.n = (int)labels.size();
        v.sel = m_pick.sel;
        v.note = m_pick.note.c_str();
        ui_launcher::draw_picker_sheet(c, g_font, v);
    }
    // One-shot synthetic command file LocalState\cmd.txt (consumed on read). Lines: open <appid> | press (=A) | back (=B) | view | up | down | left | right | lb | rb | l3 | x | y | sort | filters | search | type <text> | profile | sleep <s>.
    // Semantics follow the new screens: B does nothing on the library root (item 14 — logout is
    // the sidebar PROFILE row's A, with a Cancel-default confirm sheet); on the detail page X/Y
    // are no longer shortcuts (focus the trash/cloud/cog buttons with left/right, then A).
    // lb/rb keep their conflict-pick role. 'view' is accepted as a no-op alias (the old filter
    // chip is gone; the sidebar's Installed entry switches the view).
    // Never periodic, never present unless a tester/script writes it; every executed line is logged as SYNTHETIC.
    void cmdhook(double t) {
        static double next = 0, holdUntil = 0; static std::deque<std::string> q;
        if (t < next) return; next = t + 0.25;
        // Item D16: the library is the live screen from the first frame, so
        // cmd.txt is accepted there; the QR screen stays scriptable too.
        if (g_screen == S_LOGIN) return;
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
        // Item D20: the guide's tabs are switched by the pad's shoulders, which a
        // script cannot press; 'gtab N' selects Home/Achievements/Settings the
        // same way guide_press does, so the console can capture each tab.
        if (c.rfind("gtab ", 0) == 0) {
            if (!guest_overlay::menuOpen) { logf("cmd.txt: gtab ignored (guide closed)"); return; }
            const int n = atoi(c.c_str() + 5);
            if (n < 0 || n > 2) { logf("cmd.txt: gtab %d out of range", n); return; }
            guest_overlay::ui_guideTab = n; guest_overlay::selection = 0; guest_overlay::achTop = 0;
            logf("cmd.txt: guide tab -> %d (synthetic)", n); return;
        }
        if (c == "stacks") { dump_thread_stacks(); logf("cmd.txt: thread stacks written to launch_probe.log"); return; }
        // gpad / gpadseq: drive the running game's pad (see uwph/xinput/gpad.h).
        // Safety: armed only from this cmd.txt channel and only while a guest
        // is on screen; the game and the network cannot reach this path. The
        // armed program is cleared when the guest ends (guest_ended).
        if (c.rfind("gpad ", 0) == 0 || c == "gpad") {
            if (!m_guestSurface) { logf("cmd.txt: gpad ignored, no game running"); return; }
            const char* perr = nullptr;
            std::vector<gpad::Press> prog;
            if (!gpad::parse_gpad(c.c_str() + 4, c.size() - 4, &prog, &perr) || prog.empty()) {
                logf("cmd.txt: gpad rejected: %s", perr ? perr : "empty");
                return;
            }
            m_gpad.arm(std::move(prog), GetTickCount64());
            logf("cmd.txt: gpad armed (%zu step(s))", m_gpad.program.size());
            return;
        }
        if (c.rfind("gpadseq", 0) == 0 && (c.size() == 7 || c[7] == ' ')) {
            if (!m_guestSurface) { logf("cmd.txt: gpadseq ignored, no game running"); return; }
            const char* name = c.size() > 8 ? c.c_str() + 8 : "";
            if (!*name || strchr(name, '/') || strchr(name, '\\') || strchr(name, '.') || strstr(name, "..")) {
                logf("cmd.txt: gpadseq: bad program name");
                return;
            }
            std::string body;
            if (!read_file_s(g_dir + L"\\gpadseq\\" + widen(name) + L".txt", body)) {
                logf("cmd.txt: gpadseq '%s' not found (LocalState\\gpadseq\\%s.txt)", name, name);
                return;
            }
            const char* perr = nullptr;
            std::vector<gpad::Press> prog;
            if (!gpad::parse_seq(body.data(), body.size(), &prog, &perr)) {
                logf("cmd.txt: gpadseq '%s' rejected: %s", name, perr ? perr : "no steps");
                return;
            }
            m_gpad.arm(std::move(prog), GetTickCount64());
            logf("cmd.txt: gpadseq '%s' armed, %zu step(s)", name, m_gpad.program.size());
            return;
        }
        // item 19: named shortcuts for testers — harmless openers only.
        if (c == "sort") { if (g_screen == S_LIBRARY) { ui_nav::popup_exclusive(m_sortOpen, m_filtersOpen, m_searchOpen, 0); m_sortFocus = g_sortMode.load(); logf("cmd.txt: sort dropdown open"); } return; }
        if (c == "filters") { if (g_screen == S_LIBRARY) { ui_nav::popup_exclusive(m_sortOpen, m_filtersOpen, m_searchOpen, 1); m_filtersFocus = 0; logf("cmd.txt: filters panel open"); } return; }
        if (c == "search") { if (g_screen == S_LIBRARY) { ui_nav::popup_exclusive(m_sortOpen, m_filtersOpen, m_searchOpen, 2); m_searchKey = 0; ui_nav::search_clamp_caret(m_search); logf("cmd.txt: search overlay open"); } return; }
        // Item D13: 'type <text>' sets the on-screen keyboard's query through
        // the same state machine A uses, so a tester can enter (or clear, with
        // an empty text) a query without 30 D-pad steps. Only while the
        // keyboard is up.
        if (c.rfind("type", 0) == 0 && (c.size() == 4 || c[4] == ' ')) {
            if (!m_searchOpen) { logf("cmd.txt: type ignored (keyboard closed)"); return; }
            m_search.query.clear(); m_search.caret = 0;
            for (char ch : c.size() > 5 ? c.substr(5) : std::string()) ui_nav::search_insert(m_search, ch);
            grid_position(0, 0, "cmd.txt type");
            logf("cmd.txt: query now '%s' (%zu chars)", m_search.query.c_str(), m_search.query.size());
            return;
        }
        if (c == "profile") { if (g_screen == S_LIBRARY) { m_focus = ui_nav::Focus::sidebar; m_sidebar = ui_nav::kSidebarProfile; logf("cmd.txt: profile row focused (nothing opened)"); } return; }
        // item D10: 'menu' opens the game options popup for the focused tile
        // (library) or the open page's game (detail) — the same sheet Menu
        // and the cog open.
        if (c == "menu") {
            if (g_screen == S_LIBRARY) {
                std::vector<std::shared_ptr<Game>> list; { std::lock_guard<std::mutex> l(g_gm); list = shown_games(); }
                if (!list.empty() && m_sel < (int)list.size()) open_options(list[(size_t)m_sel]->appid, "cmd.txt menu");
                else logf("cmd.txt: menu ignored (no focused tile)");
            } else if (g_screen == S_DETAIL) open_options(m_dApp, "cmd.txt menu");
            else logf("cmd.txt: menu ignored on this screen");
            return;
        }
        GamepadButtons m = GamepadButtons::None;
        if (c == "press" || c == "a") m = GamepadButtons::A; else if (c == "back" || c == "b") m = GamepadButtons::B;
        else if (c == "up") m = GamepadButtons::DPadUp; else if (c == "down") m = GamepadButtons::DPadDown; else if (c == "left") m = GamepadButtons::DPadLeft; else if (c == "right") m = GamepadButtons::DPadRight;
        else if (c == "lb") m = GamepadButtons::LeftShoulder; else if (c == "rb") m = GamepadButtons::RightShoulder; else if (c == "x") m = GamepadButtons::X; else if (c == "y") m = GamepadButtons::Y;
        else if (c == "l3") m = GamepadButtons::LeftThumbstick;   // item D13: caps lock on the search keyboard
        else if (c == "view") { logf("cmd.txt: 'view' is a no-op (the View filter chip is gone; use 'profile'/'filters'/'search')"); return; }
        if (m == GamepadButtons::None) { logf("cmd.txt: unknown command ignored"); return; }
        GamepadReading fake{}; fake.Buttons = m; input(t, fake, true, "cmd.txt (synthetic)"); input(t + 0.001, GamepadReading{}, true, "cmd.txt release (synthetic)");
    }

    IFrameworkView CreateView() { return *this; }
    void Initialize(CoreApplicationView const& v) {
        open_log(); logf("boot: Initialize (pid=%lu) launcher build", GetCurrentProcessId());
        // Kill switch: LocalState\d3d11diag.txt present (contents ignored) ->
        // the D3D11 vtable diagnostics stay off for this whole run (A/B
        // testing a device-removal repro without them). Read once at boot;
        // set_enabled is idempotent.
        {
            DWORD attrs = GetFileAttributesW((g_dir + L"\\d3d11diag.txt").c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES) d3d11_diag::set_enabled(false);
        }
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
        // cmd.txt gpad presses ride the same publish as the physical pad: ORed
        // in, so an injected press can never release a button the player holds.
        m_gpad.tick(GetTickCount64());
        uint16_t injButtons; uint8_t injLt, injRt;
        m_gpad.held(&injButtons, &injLt, &injRt);
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
                    // The launcher menu owns the pad while it is open (XInput
                    // games too): the guest must see an empty pad, automation
                    // included (documented precedence: the overlay wins).
                    if(guest_overlay::inputBlocked)state=XINPUT_GAMEPAD{};
                    else {
                        // Injected buttons OR over the physical reading (both
                        // can be held; neither can release the other).
                        state.wButtons|=injButtons;
                        state.bLeftTrigger|=injLt;state.bRightTrigger|=injRt;
                    }
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
        // gpad injection with no physical pad attached: keep slot 0 connected
        // for the whole program (press, release and gap alike), or the guest
        // would see the pad disconnect between frames and packet numbers
        // would reset. m_gpad.armed spans exactly the program (arm() to the
        // tick that clears a finished program), so the release and gap frames
        // stay connected and the pad disappears once the program is done.
        // The overlay's input block wins here too: while the launcher menu is
        // open the guest must see an empty pad, not automation.
        if (m_gpad.armed && !assigned[0]) {
            XINPUT_GAMEPAD injected{};
            if (!guest_overlay::inputBlocked.load(std::memory_order_relaxed)) {
                injected.wButtons=injButtons;
                injected.bLeftTrigger=injLt;
                injected.bRightTrigger=injRt;
            }
            xinput_core::publish(0,&injected);
        }
        // The record DLL's raw-input enumeration presents one HID device per
        // connected pad (uwph_u32c_pads), which is what a guest's legacy
        // joystick enumeration looks for; with no pad it reports none. The
        // enumeration carries connection state only — buttons flow through
        // XInputGetState above — but while gpad injection holds buttons with
        // no physical pad connected, slot 0 must still enumerate, or the
        // guest never finds the IG_ device it would read through XInput.
        static auto reportPads=reinterpret_cast<void (WINAPI*)(unsigned)>(
            user32_core::resolve("user32.dll","uwph_u32c_pads"));
        if(reportPads) {
            unsigned mask=0;
            for(unsigned i=0;i<assigned.size();++i)if(assigned[i])mask|=1u<<i;
            reportPads(gpad::publish_mask(mask,injButtons||injLt||injRt));
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
                            // RCA experiment: lockmode.txt present => the chain keeps its
                            // first mode (later ResizeBuffers suppressed with S_OK).
                            guest_overlay::lockFirstMode=read_file_s(g_dir+L"\\lockmode.txt",saved);
                            // Opt-in present pacing (LocalState\present.txt): sync=N forces
                            // the guest's SyncInterval, fpscap=N spaces the guest's Presents.
                            // Absent or `off` leaves the game's Presents untouched.
                            std::string pf;
                            guest_overlay::pacingCfg=read_file_s(g_dir+L"\\present.txt",pf)?present_pacing::parse(pf):present_pacing::Config{};
                            guest_overlay::pacingLimiter.set_cap(guest_overlay::pacingCfg.fpscap);
                            if(guest_overlay::pacingCfg.enabled) {
                                std::string desc;
                                if(guest_overlay::pacingCfg.syncSet)desc+="sync="+std::to_string(guest_overlay::pacingCfg.sync);
                                if(guest_overlay::pacingCfg.fpscap){if(!desc.empty())desc+=" ";desc+="fpscap="+std::to_string(guest_overlay::pacingCfg.fpscap);}
                                logf("present pacing: %s",desc.c_str());
                            } else
                                logf("present pacing: off");
                            DeleteFileW((g_dir+L"\\frametimes.csv").c_str());
                            logf("overlay: present hooks %s (View+Menu opens the launcher menu)%s",guest_overlay::install(*chain)?"installed":"unavailable",
                                 guest_overlay::lockFirstMode.load()?" [lockmode: chain locked to its first mode]":"");
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

    // Item D15: the launch/loading screen (S_BOOT and S_LOADING) in the new
    // design language. The net thread owns every transition and retry; this
    // only reports what it is doing, in the same words the log uses.
    void draw_startup(double t) {
        static std::string status_buf, detail_buf;
        { std::lock_guard<std::mutex> l(g_sm); status_buf = g_status; }
        std::string err; { std::lock_guard<std::mutex> l(g_sm); err = g_err; }
        ui_launcher::StartupVM v;
        v.status = status_buf.c_str();
        v.failed = !err.empty();
        if (v.failed) { v.status = err.c_str(); detail_buf = "Press A to retry, or check boot.log"; }
        else if (!g_steamSignedIn.load()) {   // item D16: the transient connecting state
            v.status = ui_nav::session_hint(ui_nav::session_state(false, g_steamRestore.load()));
            detail_buf = "The library is ready - this only fills in your games";
        }
        else detail_buf = "This can take a few seconds";
        v.detail = detail_buf.c_str();
        v.phase = float(std::fmod(t * 0.5, 1.0));
        // the console's user, when Windows.System.User has answered
        start_xbox_users_fetch();
        static std::string who;
        { std::vector<std::string> xb = xbox_user_names(); who = xb.empty() ? "Not signed in" : xb.front(); }
        v.persona = who.c_str();
        ui::Canvas c(g_fb.data(), W, H, W);
        ui_launcher::draw_startup(c, g_font, v);
    }

    void draw_login() {
        m_btnFocus = true; m_confirmLogout = false; m_logoutYes = false; m_accountsOpen = false; m_sortOpen = false; m_sortFocus = 0; m_hdrFocus = -1; m_filtersOpen = false; m_searchOpen = false; // item 4: reset every sheet state on screen changes
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
        // Item D25: the library's profile row is the CONSOLE's signed-in Xbox
        // user — name and avatar — because that is who is holding the pad. The
        // Steam persona is the profile page's identity, not the library's; it
        // only stands in here when the console reports no user at all.
        start_xbox_users_fetch();
        static std::string persona; static const char* personaSource = "";
        persona = persona_name();
        {
            std::vector<std::string> xb = xbox_user_names();
            if (!xb.empty()) { persona = xb.front(); personaSource = "Xbox"; }
            else if (!persona.empty()) personaSource = "Steam";
            else personaSource = "";
        }
        sb.persona = persona.c_str();
        sb.persona_source = personaSource;
        // Item D16: the background Steam session's state, in the account row.
        sb.session_hint = ui_nav::session_hint(ui_nav::session_state(g_steamSignedIn.load(), g_steamRestore.load()));
        // Item D25: the Xbox user's own picture; the Steam avatar is the
        // profile page's, so it never stands in for the console user here.
        sb.avatar = xbox_user_picture();
        { std::lock_guard<std::mutex> l(g_gm);
          sb.all_count = (int)g_games.size();
          sb.installed_count = (int)m_installed.size();
          size_t q; { std::lock_guard<std::mutex> ql(g_dqm); q = g_dq.size(); }
          sb.queue_count = (int)q; }   // item D11: jobs waiting behind the running one
        // Item D11: the Downloads block shows the RUNNING download (name,
        // progress, speed) and how many jobs wait behind it; idle when none is
        // running. The strings are owned statics, like the queue screen's rows.
        {
            static std::string dlName, dlStatus;
            dlName.clear(); dlStatus.clear();
            int state = g_dl.state.load();
            if (state == 1) {
                { std::lock_guard<std::mutex> l(g_dl.m); dlName = g_dl.name; }
                if (dlName.empty()) dlName = "App " + std::to_string(g_dl.appid.load());
                const uint64_t total = g_dl.total.load(), done = g_dl.done.load();
                sb.dl_pct = total ? float(std::min<double>(1.0, double(done) / double(total))) : 0.0f;
                char b[48];
                std::snprintf(b, sizeof b, "%.1f MB/s", g_dl.speed.load() / 1048576.0);
                dlStatus = b;
            }
            size_t q; { std::lock_guard<std::mutex> ql(g_dqm); q = g_dq.size(); }
            sb.dl_name = dlName.c_str();
            sb.dl_status = dlStatus.c_str();
        }
        // item 14 mapping: 0 profile, 1 search, 2 all, 3 installed, 4 queue, 5 storage.
        // On the library screen the active row follows the view; the queue and
        // storage screens override it with their own entry.
        sb.active = m_onlyInstalled ? 3 : 2;
        sb.focus = sidebar_focus_row();
        // Item D13: the sidebar field shows the live query (the keyboard types
        // into the same string the grid filters on).
        sb.search_query = m_search.query.c_str();
        sb.profile_focus = sidebar_focus_row() == ui_nav::kSidebarProfile;   // the UI worker draws the logout affordance from this
        // item 31: the disk widget is sidebar entry 5 — same focus mechanism,
        // plus its own states (focused when the pad is on it, active while the
        // Storage screen is shown).
        sb.disk_focus = sidebar_focus_row() == ui_nav::kSidebarStorage;
        sb.disk_active = g_screen == S_STORAGE;
        // Item D1: the widget reports the drive new installs go to (the same
        // m_roots[m_rootSel] the Storage screen marks 'New installs go here'),
        // through the SAME figures draw_storage uses, so the two can never
        // disagree. The roots are probed once on the first draw (the library
        // never opened the detail page or Storage yet) and refreshed by every
        // refresh_roots() call after that.
        if (m_roots.empty() && !m_rootsProbed) { m_rootsProbed = true; refresh_roots("sidebar widget"); }
        if (m_rootSel < m_roots.size()) {
            int free_gb_x10 = 0, used_pct = 0;
            if (storage_roots::widget_figures(m_roots[m_rootSel], free_gb_x10, used_pct)) {
                sb.free_gb_x10 = free_gb_x10;
                sb.used_pct = used_pct;
            }
        }
        return sb;
    }

    // item D22: the fullscreen launch banner. The wide art is the same
    // library_hero.jpg the game page uses (header.jpg fallback), fetched on
    // demand and cached per appid for the session.
    void draw_launch_banner(double t) {
        ui::Canvas c(g_fb.data(), W, H, W);
        static std::map<uint32_t, ui::Image> banners;   // pixel memory the VM points into
        const uint32_t appid = g_launch.appid.load();
        ui::Image art;
        auto it = banners.find(appid);
        if (it == banners.end()) {
            Hero hm = hero_get(appid);
            if (hm.state == 2 && hm.pix && !hm.pix->empty()) {
                art = ui::Image{hm.pix->data(), hm.w, hm.h, hm.w};
                banners[appid] = art;
            }
        } else {
            art = it->second;
        }
        static std::string title, status;
        title = game_name(appid);
        if (title.empty()) title = "App " + std::to_string(appid);
        char b[96];
        // Item D29: the banner now covers the guest's own start too, so its
        // line says which half of the launch this is instead of a bare timer.
        if (g_launch.phase.load() == 3) snprintf(b, sizeof b, "Starting the game...  (%.0f s)", now_s() - g_launch.started);
        else snprintf(b, sizeof b, "Launching...  (%.0f s)", now_s() - g_launch.started);
        status = b;
        ui_launcher::LaunchBannerVM v;
        v.title = title.c_str();
        v.status = status.c_str();
        v.art = art;
        v.phase = float(t) * 0.5f;
        v.cancellable = ui_nav::launch_cancel_allowed(g_launch.phase.load()) && g_launch.loaderStarted.load() == 0;
        ui_launcher::draw_launch_banner(c, g_font, v);
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
          // Item D16: "Loading..." only while the background restore is still
          // running; signed out (or offline) the grid is simply empty.
          loading = g_games.empty() && g_steamRestore.load() == 1;
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
              tv.favorite = is_favorite(g.appid);   // item D10: the star badge
              if (g.cover == 1 && !g.pix.empty()) { arts.push_back(ui::Image{g.pix.data(), g.iw, g.ih, g.iw}); tv.art = arts.back(); }
              // item 12: cover state 2 (failed) or 0 (not loaded) -> grey gradient tile
              tv.no_cover = g.cover != 1;
              // item 11: measure the label once per game with the Selawik font
              // and cache it in the Game struct; a game measured before the
              // font loaded (nameW 0 while g_fontOk was false) remeasures.
              if (g_fontOk && g.nameW <= 0) g.nameW = g_font.measure(ui::Face::Regular, 20, g.name);
              tv.name_w = g.nameW;
              tiles.push_back(tv);
          }
        }
        ui_launcher::SidebarVM sb = sidebar_vm();
        ui_launcher::LibraryVM lib;
        lib.tiles = tiles.data(); lib.tile_count = (int)tiles.size();
        lib.sel = m_sel; lib.top_row = m_top; lib.focus_tile = m_focus == ui_nav::Focus::grid;
        lib.header_focus = m_hdrFocus;
        lib.sort_open = m_sortOpen; lib.sort_focus = m_sortFocus;
        lib.filters_open = m_filtersOpen; lib.filters_focus = m_filtersFocus;
        lib.filter_playable = m_filterPlayable;   // the panel's Playable row shows the real state
        lib.filter_favorites = m_filterFavorites; // item D10: the Favorites row
        bool filterOn[kFilterRows] = {m_filterPlayable, m_filterFavorites};   // item 26/32: the Filters button's '· N' count
        lib.filters_active = ui_nav::filters_active(filterOn, kFilterRows);
        lib.search_open = m_searchOpen; lib.search_key = m_searchKey;
        lib.search_caret = m_search.caret; lib.search_caps = m_search.caps;
        static std::string query_buf; query_buf = m_search.query;
        lib.query = query_buf.c_str();
        lib.t = float(t);   // item 11: marquee clock for the focused tile
        static std::string sort_label; sort_label = std::string("Sort by ") + SORT_NAMES[g_sortMode.load(std::memory_order_relaxed)];
        lib.sort_label = sort_label.c_str();
        static char count[128];
        // Item D7: a filtered grid says how much of the library is showing
        // ("12 of 772 games"), so the count is never mistaken for the total.
        const bool searching = !m_search.query.empty();
        if (loading) lib.loading = true;
        else if (m_onlyInstalled) snprintf(count, sizeof count, "%zu installed of %d games", listed, (int)g_ownedCount.load()), lib.count_label = count;
        else { snprintf(count, sizeof count, "%s", game_library::count_label(listed, (size_t)g_ownedCount.load(), searching).c_str()); lib.count_label = count; }
        // Item D16: an empty grid says what the session is doing, so the first
        // frame reads as a real screen instead of a blank page.
        static std::string emptyTitle, emptyDetail;
        emptyTitle.clear(); emptyDetail.clear();
        if (listed == 0 && !m_onlyInstalled && !searching) {
            switch (ui_nav::session_state(g_steamSignedIn.load(), g_steamRestore.load())) {
            case ui_nav::SessionState::connecting: emptyTitle = "Connecting to Steam..."; emptyDetail = "Your library appears here when the session is ready"; break;
            case ui_nav::SessionState::offline: emptyTitle = "Steam is offline"; emptyDetail = "Check the network, then sign in from the profile row"; break;
            case ui_nav::SessionState::signed_out: emptyTitle = "No Steam account"; emptyDetail = "A on the profile row, then Sign in to Steam"; break;
            default: break;
            }
        }
        lib.empty_title = emptyTitle.c_str(); lib.empty_detail = emptyDetail.c_str();
        ui_launcher::draw_library(c, g_font, sb, lib, !(m_confirmLogout || m_optOpen || m_accountsOpen));
        if (m_optOpen) draw_options(c);   // item D10: Menu on the focused tile
        if (m_accountsOpen) draw_accounts(c);   // item D14: A on the profile row
        if (g_demo) {   // demo marker replaces the count line (honest: not your account)
            c.fill_rect_alpha(524, 168, 430, 60, ui::theme::page, 255);
            c.fill_round_rect(ui::rect(524, 168, 430, 60), ui::theme::radiusControl, ui::theme::control);
            ui_launcher::text(c, g_font, ui::Face::Regular, 24, 548, 183.5f, "DEMO LIST - not your account", ui::theme::text);
        }
        std::string err; { std::lock_guard<std::mutex> l(g_sm); err = g_err; }
        if (!err.empty()) {   // load failure line above the grid (item D16: A retries, the grid stays usable)
            ui_launcher::text(c, g_font, ui::Face::Regular, 24, 524, 216, err.c_str(), ui::theme::danger);
            ui_launcher::text(c, g_font, ui::Face::Regular, 22, 524, 248, "Press A to try again", ui::theme::textDim);
        }
        if (m_confirmLogout) {
            // item 8/14: the SAME confirmation sheet component as Delete —
            // title, body, two real buttons — over the dimmed library. The
            // ring starts on CANCEL (item 14): only A on 'Log out' logs out.
            ui_launcher::LogoutVM lo;
            lo.focus_yes = m_logoutYes;
            ui_launcher::draw_logout_sheet(c, g_font, lo);
        }
    }

    // Download queue screen (mockup 06): rows from g_dq + the active download;
    // pause has no backend (disabled stub is not drawn: rows show CANCEL only
    // via A), auto-update pill is a memory-only setting, Updates is always
    // empty (the launcher has no update backend).
    void draw_queue() {
        ui::Canvas c(g_fb.data(), W, H, W);
        refresh_installed(now_s());
        ui_launcher::SidebarVM sb = sidebar_vm(); sb.active = ui_nav::kSidebarQueue;
        // row model under the same locks the queue state uses
        static std::vector<ui_launcher::QueueRowVM> rows;
        static std::vector<std::string> rowNames, rowStatus;
        static std::vector<ui::Image> rowArts;   // item D21: pixel memory the rows point into
        rows.clear(); rowNames.clear(); rowStatus.clear(); rowArts.clear();
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
            rowArts.push_back(queue_cover(dlApp));   // item D21: the tall library art
            r.cover = rowArts.back();
            rows.push_back(r);
        }
        for (uint32_t id : dq) {
            rowNames.push_back(game_name(id).empty() ? "App " + std::to_string(id) : game_name(id));
            rowStatus.push_back("Queued");
            ui_launcher::QueueRowVM r;
            r.name = rowNames.back().c_str();
            r.status = "Queued";
            rowArts.push_back(queue_cover(id));
            r.cover = rowArts.back();
            rows.push_back(r);
        }
        ui_launcher::QueueVM q;
        q.auto_update = m_autoUpdate;
        q.rows = rows.data(); q.row_count = (int)rows.size();
        q.updates = nullptr; q.update_count = 0;
        q.focus = m_focus == ui_nav::Focus::grid ? m_qSel : -1;   // item 28: 0 IS the Auto-update row now; rows shift to 1..
        ui_launcher::draw_download_queue(c, g_font, sb, q);
        if (rows.empty()) {   // honest empty state over the (empty) section
            ui_launcher::text(c, g_font, ui::Face::Regular, 24, 524, 340, "No downloads.", ui::theme::textDim);
            ui_launcher::text(c, g_font, ui::Face::Regular, 22, 524, 748, "No updates available.", ui::theme::textDim);
        }
        // item 29: draw_download_queue already draws its own hint bar — no
        // second draw_hint_bar here (the console showed two stacked bars).
    }

    // Storage management (items 22/31/33): the disk widget in the sidebar opens
    // this screen (item 31: no view row active; the widget wears the active
    // look). ui_launcher::draw_storage paints both levels; main.cpp fills the
    // VMs and owns the item-33 action sheet (per game: move to another drive /
    // delete / cancel) and the move job's progress sheet.
    void draw_storage() {
        ui::Canvas c(g_fb.data(), W, H, W);
        ui_launcher::SidebarVM sb = sidebar_vm(); sb.active = -1;   // item 31: Storage is the disk widget's screen — no view row is active
        std::vector<storage_roots::Choice> roots = m_roots.empty() ? std::vector<storage_roots::Choice>{storage_report::internal_root(g_dir)} : m_roots;

        // view models (statics keep the c_str()s alive while drawing)
        static std::vector<ui_launcher::StorageDriveVM> drives;
        static std::vector<std::string> driveTitles, driveDetails;
        drives.clear(); driveTitles.clear(); driveDetails.clear();
        for (size_t i = 0; i < roots.size(); ++i) {
            const storage_roots::Choice& r = roots[i];
            driveTitles.push_back(r.name);
            int pct = -1;
            if (r.space.ok && r.space.total) pct = (int)(100 - 100.0 * r.space.freeTotal / (double)r.space.total);
            std::string used = pct >= 0
                ? std::to_string(pct) + "% used of " + storage_roots::size_text(r.space.total)
                : std::string("free ") + storage_roots::size_text(r.space.freeTotal);
            driveDetails.push_back(used);
            ui_launcher::StorageDriveVM d;
            d.title = driveTitles.back().c_str();
            d.detail = driveDetails.back().c_str();
            d.used_pct = pct;
            d.install_target = (int)i == (int)m_rootSel;
            d.focused = m_focus == ui_nav::Focus::grid && (int)i == m_stSel;
            drives.push_back(d);
        }

        ui_launcher::StorageVM st;
        st.drives = drives.data();
        st.n_drives = (int)drives.size();
        st.open_drive = m_stOpen ? m_stSel : -1;
        static std::string driveTitle;
        driveTitle = m_stOpen && m_stSel < (int)roots.size() ? roots[m_stSel].name : std::string("Drive");
        st.drive_title = driveTitle.c_str();

        static std::vector<ui_launcher::StorageGameVM> gameRows;
        static std::vector<std::string> gameNames, gameSizes;
        gameRows.clear(); gameNames.clear(); gameSizes.clear();
        if (m_stOpen) {
            // row 0 is the "Use for new installs" action row (empty size string
            // switches draw_storage's accent styling on); the games follow.
            gameNames.push_back("Use for new installs");
            gameSizes.push_back(std::string());
            auto rows = storage_game_list();
            for (size_t i = 0; i < rows.size(); ++i) {
                gameNames.push_back(rows[i].name);
                gameSizes.push_back(storage_roots::size_text(rows[i].bytes));
            }
            for (size_t i = 0; i < gameNames.size(); ++i) {
                ui_launcher::StorageGameVM gm;
                gm.name = gameNames[i].c_str();
                gm.size = gameSizes[i].c_str();
                gm.focused = m_focus == ui_nav::Focus::grid && m_stGameSel == (int)i;   // 0 = the action row
                gameRows.push_back(gm);
            }
            st.games = gameRows.data();
            st.n_games = (int)gameRows.size();
            st.game_scroll = m_stGameScroll;
        }

        // item 29: exactly one hint bar — draw_storage draws its own unless a
        // sheet is up (each sheet draws its own).
        ui_launcher::draw_storage(c, g_font, sb, st,
                                  !(m_confirmDelete || m_stActionOpen || m_moveSheetOpen));

        if (m_stActionOpen) {   // item 33: the action sheet over the dimmed page
            ui_launcher::ActionSheetVM a;
            static std::string aTitle, aSubtitle;
            std::string nm = game_name(m_stActionApp);
            aTitle = clip(!nm.empty() ? nm : "App " + std::to_string(m_stActionApp), 36);
            aSubtitle = storage_roots::size_text(m_stActionBytes) + " \xC2\xB7 " +
                        (m_stSel < (int)roots.size() ? roots[m_stSel].name : std::string("Drive"));
            static std::vector<const char*> labelPtrs, reasonPtrs;
            labelPtrs.clear(); reasonPtrs.clear();
            for (const std::string& l : m_stActionLabels) labelPtrs.push_back(l.c_str());
            for (const std::string& r : m_stActionReasons) reasonPtrs.push_back(r.c_str());
            a.title = aTitle.c_str(); a.subtitle = aSubtitle.c_str();
            a.labels = labelPtrs.data(); a.reasons = reasonPtrs.data();
            a.n = (int)labelPtrs.size();
            a.sel = m_stActionSel;
            ui_launcher::draw_action_sheet(c, g_font, a);
        } else if (m_moveSheetOpen) {   // item 33: the move job's progress sheet
            ui_launcher::ProgressSheetVM ps;
            static std::string psTitle, psDetail;
            std::string nm = game_name(m_move.appid);
            psTitle = clip(!nm.empty() ? nm : "App " + std::to_string(m_move.appid), 36);
            psDetail = "Moving to " + m_move.dstName;
            ps.title = psTitle.c_str(); ps.detail = psDetail.c_str();
            ps.pct = m_moveProgress.pct();   // the worker's Progress (m_move.copied is never written)
            ps.cancel_focus = true;
            ui_launcher::draw_progress_sheet(c, g_font, ps);
        } else if (m_confirmDelete) {   // the SAME sheet component as the game page, focus on Cancel
            ui_launcher::DeleteVM d;
            static std::string dTitle, dBody;
            std::string nm = game_name(m_stConfirmApp);
            dTitle = "Delete " + clip(nm.empty() ? "App " + std::to_string(m_stConfirmApp) : nm, 30) + "?";
            dBody = "Removes the game files from this console. Saves are kept.";
            d.title = dTitle.c_str(); d.body = dBody.c_str(); d.focus_delete = m_stDelYes;
            ui_launcher::draw_delete_sheet(c, g_font, d);
        }
    }

    void draw(double t) {
        int s = g_screen;
        if (!g_fontOk) {   // graceful fallback: the old bitmap screens stay drawable
            if (s == S_LOGIN) { fill(0, 0, W, H, C3(14, 17, 22)); std::string st; { std::lock_guard<std::mutex> l(g_sm); st = g_status; } text8(80, 480, st.c_str(), 4, C3(255, 255, 255)); }
            else { fill(0, 0, W, H, C3(14, 17, 22)); text8(80, 480, "Fonts failed to load - see boot.log", 4, C3(255, 120, 120)); }
            return;
        }
        // The 1920x1080 g_fb persists across screens: every frame starts from
        // a clean page so no screen draws over the previous one's remnants.
        fill(0, 0, W, H, ui::theme::page);
        // item D22: Play covers the screen with the game's wide art and the
        // launch status until the guest's own window is up (or the launch
        // ends). It sits above every screen, since Play can start from the
        // library or the game page.
        if (ui_nav::launch_banner_up(g_launch.phase.load(), m_guestSurface)) { draw_launch_banner(t); return; }
        if (s == S_LOGIN) draw_login();
        else if (s == S_BOOT || s == S_LOADING) draw_startup(t);   // item D15: the launch/loading screens
        else if (s == S_LIBRARY) draw_library(t);
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
        // item D22: the launch banner owns the screen while a launch prepares;
        // B cancels it (only before game code starts), A and every other
        // button are ignored so the game page underneath cannot act.
        if (ui_nav::launch_banner_up(g_launch.phase.load(), m_guestSurface)) {
            if (has(edge, GamepadButtons::B)) {
                if (cancel_launch(source)) logf("launch banner: cancelled (%s)", source);
                else logf("launch banner: B ignored, the game already started (%s)", source);
            }
            return;
        }
        // A running guest owns the controller (it reads the same pad through XInput/WGI).
        // While its surface is up the launcher UI is not visible either (also during the
        // final save upload before the restart), so it must not act on those presses.
        if (auto guest = play_runtime::observed.load(std::memory_order_acquire); m_guestSurface || (guest && !guest->finished.load(std::memory_order_acquire))) {
            logf("input: game surface up; launcher UI ignores controller"); return;
        }
        int s = g_screen;
        if (s == S_LOGIN) {   // item D16: the QR screen is reached from the account popup; B returns to the library
            if (has(edge, GamepadButtons::A) || has(edge, GamepadButtons::X)) { g_refreshReq = true; set_status("Refreshing QR..."); logf("Refresh button pressed"); }
            else if (has(edge, GamepadButtons::B)) { g_screen = S_LIBRARY; logf("sign-in screen: B back to the library (%s)", source); }
        }
        else if (s == S_BOOT || s == S_LOADING) {   // item D15: the failure state's 'A Retry' (nothing else is actionable while loading)
            std::string err; { std::lock_guard<std::mutex> l(g_sm); err = g_err; }
            if (!err.empty() && has(edge, GamepadButtons::A)) { g_reloadReq = true; set_status("Retrying..."); logf("startup: retry requested (%s)", source); }
        }
        else if (s == S_DETAIL) {
            // item D10: the game options popup owns the pad while it is up
            if (options_input(edge, source)) return;
            if (m_pick.open) {   // the launch-option picker owns the pad while it is up
                if (has(edge, GamepadButtons::DPadUp)) { launch_picker::move(m_pick, -1); logf("detail: launch option %d/%zu (%s)", m_pick.sel + 1, m_pick.rows.size(), source); }
                else if (has(edge, GamepadButtons::DPadDown)) { launch_picker::move(m_pick, 1); logf("detail: launch option %d/%zu (%s)", m_pick.sel + 1, m_pick.rows.size(), source); }
                else if (has(edge, GamepadButtons::A)) detail_picker_a(source);
                else if (has(edge, GamepadButtons::B)) { launch_picker::close(m_pick); logf("detail: launch-option picker cancelled (%s)", source); }
                return;
            }
            if (m_confirmDelete) {   // item 14: focus starts on Cancel; left/right toggles; A on 'Delete' deletes
                if (has(edge, GamepadButtons::DPadLeft) || has(edge, GamepadButtons::DPadRight)) { m_delYes = !m_delYes; logf("detail: delete sheet focus -> %s (%s)", m_delYes ? "Delete" : "Cancel", source); }
                else if (has(edge, GamepadButtons::A)) { if (m_delYes) detail_delete_confirm(source); else { m_confirmDelete = false; logf("detail: delete cancelled (%s)", source); } }
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
                else open_options(m_dApp, "detail cog");          // item D10: the cog opens the game options popup
            }
            if (has(edge, GamepadButtons::B)) detail_back(source);
            // item D10: Menu opens the SAME options popup the cog does (the
            // hint bar already labels it "Manage").
            if (has(edge, GamepadButtons::Menu)) open_options(m_dApp, "detail Menu");
            // item 14: Menu/View no longer leaves the page — B is the one back
            // route, like every other screen (a single press that leaves the
            // page invited stray confirms on the next screen).
        }
        else if (s == S_LIBRARY) {
            std::vector<std::shared_ptr<Game>> list; { std::lock_guard<std::mutex> l(g_gm); list = shown_games(); } size_t n = list.size();
            // item D10: the game options popup owns the pad while it is up
            if (options_input(edge, source)) return;
            if (accounts_input(edge, source)) return;   // item D14: the account list
            if (m_confirmLogout) {   // item 14: focus starts on Cancel; left/right toggles; only A on 'Log out' logs out
                if (has(edge, GamepadButtons::DPadLeft) || has(edge, GamepadButtons::DPadRight)) { m_logoutYes = !m_logoutYes; logf("logout sheet: focus -> %s (%s)", m_logoutYes ? "Log out" : "Cancel", source); }
                else if (has(edge, GamepadButtons::A)) {
                    if (m_logoutYes) { m_confirmLogout = false; g_logoutReq = true; grid_position(0, 0, "logout confirmed"); logf("logout confirmed: deleting local session file only (no revoke call)"); }
                    else { m_confirmLogout = false; logf("logout cancelled (%s)", source); }
                }
                else if (has(edge, GamepadButtons::B)) { m_confirmLogout = false; logf("logout cancelled"); }
                return;
            }
            // item D16: with the library load failed and nothing focused to act
            // on, A retries the fetch (the grid stays usable either way). After
            // the modal sheets, so A on a sheet never doubles as a retry.
            { std::string err; { std::lock_guard<std::mutex> l(g_sm); err = g_err; }
              if (!err.empty() && n == 0 && has(edge, GamepadButtons::A)) { g_reloadReq = true; set_status("Retrying..."); logf("library: retry requested (%s)", source); return; } }
            // item 1: the sort dropdown owns the pad while it is up (A confirms,
            // B closes, up/down moves with wrap).
            if (m_sortOpen) {
                if (has(edge, GamepadButtons::DPadUp)) { ui_nav::sort_open_move(m_sortFocus, ui_nav::kSortOptionCount, false); logf("sort: up -> option %d (%s)", m_sortFocus, source); }
                else if (has(edge, GamepadButtons::DPadDown)) { ui_nav::sort_open_move(m_sortFocus, ui_nav::kSortOptionCount, true); logf("sort: down -> option %d (%s)", m_sortFocus, source); }
                else if (has(edge, GamepadButtons::A)) {
                    int selected = g_sortMode.load();
                    if (ui_nav::sort_confirm(m_sortFocus, selected, m_sortOpen)) {
                        g_sortMode = selected; { std::lock_guard<std::mutex> l(g_gm); sort_games(selected); } grid_position(0, 0, "sort confirm"); logf("sort mode -> %s", SORT_NAMES[selected]);                    }
                }
                else if (has(edge, GamepadButtons::B)) { ui_nav::sort_close(m_sortOpen); logf("sort popup closed (%s)", source); }
                return;
            }
            // item 1: the filters panel owns the pad while it is up (A toggles
            // the focused row, B closes, up/down moves with wrap).
            if (m_filtersOpen) {
                bool on[kFilterRows] = {m_filterPlayable, m_filterFavorites};   // item D10: Playable, Favorites
                if (has(edge, GamepadButtons::DPadUp)) { ui_nav::filters_move(m_filtersFocus, kFilterRows, false); logf("filters: up -> row %d (%s)", m_filtersFocus, source); }
                else if (has(edge, GamepadButtons::DPadDown)) { ui_nav::filters_move(m_filtersFocus, kFilterRows, true); logf("filters: down -> row %d (%s)", m_filtersFocus, source); }
                else if (has(edge, GamepadButtons::A)) {
                    ui_nav::filters_toggle(on, kFilterRows, m_filtersFocus);
                    if (m_filterPlayable != on[0] || m_filterFavorites != on[1]) {
                        m_filterPlayable = on[0]; m_filterFavorites = on[1];
                        grid_position(0, 0, "filters toggle");
                        logf("filters: playable -> %s, favorites -> %s (%s)", m_filterPlayable ? "on" : "off", m_filterFavorites ? "on" : "off", source);
                    }
                }
                else if (has(edge, GamepadButtons::B)) { m_filtersOpen = false; logf("filters panel closed (%s)", source); }
                return;
            }
            // item 1: the search overlay owns the pad while it is up. Item D13:
            // D-pad moves the key cursor, A types the focused key, X deletes,
            // LB/RB move the caret, Y applies (closes, keeps the query), L3
            // toggles caps lock, B closes.
            if (m_searchOpen) {
                if (has(edge, GamepadButtons::DPadUp)) { ui_nav::search_move(m_searchKey, 0, -1); logf("search: key %d (%s)", m_searchKey, source); }
                else if (has(edge, GamepadButtons::DPadDown)) { ui_nav::search_move(m_searchKey, 0, 1); logf("search: key %d (%s)", m_searchKey, source); }
                else if (has(edge, GamepadButtons::DPadLeft)) { ui_nav::search_move(m_searchKey, -1, 0); logf("search: key %d (%s)", m_searchKey, source); }
                else if (has(edge, GamepadButtons::DPadRight)) { ui_nav::search_move(m_searchKey, 1, 0); logf("search: key %d (%s)", m_searchKey, source); }
                else if (has(edge, GamepadButtons::A)) { if (ui_nav::search_activate(m_searchKey, m_search, m_searchOpen)) grid_position(0, 0, "search type"); logf("search: activate (%s)", source); }
                else if (has(edge, GamepadButtons::X)) { if (ui_nav::search_backspace(m_search)) grid_position(0, 0, "search delete"); logf("search: X delete (%s)", source); }
                else if (has(edge, GamepadButtons::LeftShoulder)) { ui_nav::search_caret_move(m_search, -1); logf("search: caret -> %d (%s)", m_search.caret, source); }
                else if (has(edge, GamepadButtons::RightShoulder)) { ui_nav::search_caret_move(m_search, 1); logf("search: caret -> %d (%s)", m_search.caret, source); }
                else if (has(edge, GamepadButtons::Y)) { m_searchOpen = false; logf("search: applied '%s' (%s)", m_search.query.c_str(), source); }
                else if (has(edge, GamepadButtons::LeftThumbstick)) { logf("search: caps -> %s (%s)", ui_nav::search_toggle_caps(m_search) ? "on" : "off", source); }
                else if (has(edge, GamepadButtons::B)) { m_searchOpen = false; logf("search overlay closed (%s)", source); }
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
                else if (has(edge, GamepadButtons::A)) sidebar_activate(t, source);
                // Item D7: X on the Search row clears the query, so a filtered
                // grid is always one press from the whole library again.
                else if (has(edge, GamepadButtons::X) && m_sidebar == ui_nav::kSidebarSearch && !m_search.query.empty()) {
                    m_search.query.clear(); m_search.caret = 0;
                    grid_position(0, 0, "search cleared");
                    logf("search: query cleared (X on the sidebar Search row, %s)", source);
                }
                return;
            }
            // Header controls row (item 1): left/right walk Sort/Filters/Search,
            // down returns to the grid, left from Sort to the sidebar. A opens
            // the focused control; B on an open popup closes it.
            if (m_hdrFocus >= 0) {
                if (has(edge, GamepadButtons::DPadRight)) { m_hdrFocus = ui_nav::header_move(m_hdrFocus, true); logf("header: focus %d (%s)", m_hdrFocus, source); }
                else if (has(edge, GamepadButtons::DPadLeft)) {
                    if (m_hdrFocus == 0) { m_hdrFocus = -1; m_focus = ui_nav::Focus::sidebar; logf("header: focus -> sidebar (%s)", source); }
                    else { m_hdrFocus = ui_nav::header_move(m_hdrFocus, false); logf("header: focus %d (%s)", m_hdrFocus, source); }
                }
                else if (has(edge, GamepadButtons::DPadDown)) { m_hdrFocus = -1; logf("header: focus -> grid (%s)", source); }
                else if (has(edge, GamepadButtons::A)) {
                    // item 15: opening one popup closes the others (ui_nav::popup_exclusive)
                    if (m_hdrFocus == 0) { ui_nav::popup_exclusive(m_sortOpen, m_filtersOpen, m_searchOpen, 0); m_sortFocus = g_sortMode.load(); logf("sort popup open (%s)", source); }
                    else if (m_hdrFocus == 1) { ui_nav::popup_exclusive(m_sortOpen, m_filtersOpen, m_searchOpen, 1); m_filtersFocus = 0; logf("filters panel open (%s)", source); }
                    else if (m_hdrFocus == 2) { ui_nav::popup_exclusive(m_sortOpen, m_filtersOpen, m_searchOpen, 2); m_searchKey = 0; ui_nav::search_clamp_caret(m_search); logf("search overlay open (%s)", source); }
                }
                // Item D24: B anywhere on the main screen lands on the sidebar
                // rail — from the header controls too, not back into the grid.
                else if (has(edge, GamepadButtons::B)) { ui_nav::header_back(m_hdrFocus, m_focus); logf("header: B -> sidebar rail (item D24) (%s)", source); }
                return;
            }
            // Grid focus. The grid draws kGridCols columns (ui_launcher.h geometry);
            // movement uses the same constant so the ring never lies.
            const int COLS = ui_launcher::kGridCols;
            int sel = m_sel;
            if (has(edge, GamepadButtons::DPadRight) && sel + 1 < (int)n) sel++;
            if (has(edge, GamepadButtons::DPadLeft)) { if (!ui_nav::lateral(m_focus, sel % COLS, true)) { if (sel > 0) sel--; } else { logf("grid: focus -> sidebar (%s)", source); } }
            if (has(edge, GamepadButtons::DPadDown) && (sel / COLS + 1) * COLS < (int)n) sel = std::min((int)n - 1, sel + COLS);
            if (has(edge, GamepadButtons::DPadUp)) { if (sel - COLS >= 0) sel -= COLS; else { m_hdrFocus = 0; logf("grid: focus -> header Sort (%s)", source); } }
            if (has(edge, GamepadButtons::RightShoulder)) sel = std::min((int)n - 1, sel + 2 * COLS);
            if (has(edge, GamepadButtons::LeftShoulder)) sel = std::max(0, sel - 2 * COLS);
            if (n == 0) sel = 0;
            sel = std::max(0, std::min(sel, (int)n - 1));
            int top = m_top, r = sel / COLS; if (r < top) top = r; if (r > top + 1) top = r - 1;
            grid_position(sel, top, source);
            if (has(edge, GamepadButtons::A) && n) { uint32_t aid; std::string nm; { std::lock_guard<std::mutex> l(g_gm); aid = list[m_sel]->appid; nm = list[m_sel]->name; } open_detail(aid, nm, source); return; }
            // item D10: Menu on the focused tile opens the SAME game options
            // popup the detail cog does (the hint bar labels it "Manage").
            if (has(edge, GamepadButtons::Menu) && n) { uint32_t aid = 0; { std::lock_guard<std::mutex> l(g_gm); aid = list[m_sel]->appid; } open_options(aid, "library Menu"); return; }
            // item 14: B on the library root does NOTHING destructive (Xbox
            // convention) — no popup/sheet is open here (each popup's handler
            // returned above). Item D5: the root has nowhere to go back TO, so
            // B moves the focus to the sidebar rail instead of doing nothing:
            // a visible answer that matches the "B Back" hint. Logout lives on
            // the sidebar's PROFILE row (A) and is never reachable from B.
            if (has(edge, GamepadButtons::B)) {
                if (ui_nav::back_at_root(m_focus, true)) logf("library: B -> sidebar rail (item D5) (%s)", source);
                else logf("library: B ignored on the sidebar rail (item 14) (%s)", source);
            }
        }
        else if (s == S_QUEUE) {
            // The shared sidebar stays live (up/down/A as on the library);
            // right moves to the rows. In the rows: up/down move the focus, A
            // cancels the focused row (the running download or a queued job),
            // B back to the library. Pause has no backend and is not offered.
            std::deque<uint32_t> dq; { std::lock_guard<std::mutex> l(g_dqm); dq = g_dq; }
            uint32_t dlApp = g_dl.appid.load(); int dlState = g_dl.state.load();
            bool activeRow = dlState == 1 || (dlState != 0 && dlApp);   // the same row draw_queue puts first
            int nrows = ui_nav::queue_row_total((activeRow ? 1 : 0) + (int)dq.size(), 0);   // row 0 Auto-update, then downloads, then the queue (0 updates)
            if (m_focus == ui_nav::Focus::sidebar) {
                // item D19: B leaves the queue from EITHER focus — with the
                // sidebar live (the entry point) it was a dead key.
                if (has(edge, GamepadButtons::B)) { g_screen = S_LIBRARY; logf("queue: back to the library (%s)", source); }
                else if (has(edge, GamepadButtons::DPadUp)) { ui_nav::sidebar_move(m_sidebar, false); logf("sidebar: entry %d (%s)", m_sidebar, source); }
                else if (has(edge, GamepadButtons::DPadDown)) { ui_nav::sidebar_move(m_sidebar, true); logf("sidebar: entry %d (%s)", m_sidebar, source); }
                else if (has(edge, GamepadButtons::DPadRight) && ui_nav::lateral(m_focus, 0, false)) { m_qSel = 0; }
                else if (has(edge, GamepadButtons::A)) sidebar_activate(t, source);
            } else {
                // item 28: row 0 is the Auto-update row (a real focusable row);
                // up/down walk it and the downloads with wrap (ui_nav helpers).
                m_qSel = ui_nav::queue_row_clamp(m_qSel, nrows);
                if (has(edge, GamepadButtons::B)) { g_screen = S_LIBRARY; logf("queue: back to the library (%s)", source); }
                else if (has(edge, GamepadButtons::DPadLeft)) { ui_nav::lateral(m_focus, 0, true); logf("queue: focus -> sidebar (%s)", source); }
                else if (has(edge, GamepadButtons::DPadDown)) { ui_nav::queue_row_move(m_qSel, nrows, true); logf("queue: row %d/%d (%s)", m_qSel, nrows, source); }
                else if (has(edge, GamepadButtons::DPadUp)) { ui_nav::queue_row_move(m_qSel, nrows, false); logf("queue: row %d/%d (%s)", m_qSel, nrows, source); }
                else if (has(edge, GamepadButtons::A)) {
                    if (m_qSel == 0) { m_autoUpdate = !m_autoUpdate; logf("queue: auto-update -> %s (A on the row, %s)", m_autoUpdate ? "on" : "off", source); }   // item 28: A on Auto-update toggles
                    else if (activeRow && m_qSel == 1) { g_dl.cancel = true; logf("queue: download cancel requested appid=%u (%s)", dlApp, source); }
                    else {
                        int qi = ui_nav::queue_row_index(m_qSel, activeRow);
                        if (qi >= 0 && qi < (int)dq.size()) { uint32_t id = dq[(size_t)qi]; cancel_queued(id); logf("queue: cancel asked appid=%u (%s)", id, source); }
                    }
                }
            }
        }
        else if (s == S_STORAGE) {
            // The shared sidebar stays live (up/down/A); right moves to the
            // root list. Item 22: A on a drive opens its games list (the first
            // row is 'Set as install location'); up/down move, A on a game
            // opens the detail page's Delete confirm sheet, B backs out one
            // level (games list -> drive list -> library).
            if (m_moveSheetOpen) {
                // item 33: while a move runs the sheet owns the pad — A/B set
                // the cancel flag (the worker removes the partial copy and
                // keeps the source); everything else is ignored.
                if (has(edge, GamepadButtons::A) || has(edge, GamepadButtons::B)) {
                    m_move.cancel = true;
                    logf("storage: move cancel requested (%s)", source);
                }
                return;
            }
            if (m_stActionOpen) {
                // item 33: the action sheet owns the pad. Up/down walk all
                // rows (disabled ones stay focusable); A activates an enabled
                // row; B closes.
                int n = (int)m_stActionLabels.size();
                if (!n) { m_stActionOpen = false; return; }
                if (has(edge, GamepadButtons::DPadUp)) { m_stActionSel = (m_stActionSel + n - 1) % n; logf("storage: action option %d/%d (%s)", m_stActionSel + 1, n, source); }
                else if (has(edge, GamepadButtons::DPadDown)) { m_stActionSel = (m_stActionSel + 1) % n; logf("storage: action option %d/%d (%s)", m_stActionSel + 1, n, source); }
                else if (has(edge, GamepadButtons::B)) { m_stActionOpen = false; logf("storage: action sheet closed (%s)", source); }
                else if (has(edge, GamepadButtons::A) && m_stActionSel >= 0 && m_stActionSel < n) {
                    if (!m_stActionReasons[(size_t)m_stActionSel].empty()) {
                        logf("storage: action refused (disabled): %s", m_stActionReasons[(size_t)m_stActionSel].c_str());
                    } else if (m_stActionSel < (int)m_actionDst.size() && m_actionDst[(size_t)m_stActionSel] >= 0) {
                        int dsti = m_actionDst[(size_t)m_stActionSel];
                        m_stActionOpen = false;
                        storage_start_move((size_t)dsti, source);
                    } else if (m_stActionLabels[(size_t)m_stActionSel] == "Delete") {
                        m_stActionOpen = false;
                        m_stConfirmApp = m_stActionApp;
                        m_stDelYes = false;   // the delete confirm's focus starts on Cancel
                        m_confirmDelete = true;
                        logf("storage: delete asked from action sheet appid=%u (%s)", m_stActionApp, source);
                    } else {   // Cancel
                        m_stActionOpen = false;
                        logf("storage: action sheet cancelled (%s)", source);
                    }
                }
                return;
            }
            if (m_confirmDelete) {   // item 14: focus starts on Cancel; left/right toggles; A on 'Yes, delete' uninstalls
                if (has(edge, GamepadButtons::DPadLeft) || has(edge, GamepadButtons::DPadRight)) { m_stDelYes = !m_stDelYes; logf("storage: delete sheet focus -> %s (%s)", m_stDelYes ? "Delete" : "Cancel", source); }
                else if (has(edge, GamepadButtons::A)) { if (m_stDelYes) storage_delete_confirm(source); else { m_confirmDelete = false; logf("storage: delete cancelled (%s)", source); } }
                else if (has(edge, GamepadButtons::B)) { m_confirmDelete = false; logf("storage: delete cancelled (%s)", source); }
                return;
            }
            if (m_focus == ui_nav::Focus::sidebar) {
                // item D19: B leaves the screen from EITHER focus (same dead key as the queue).
                if (has(edge, GamepadButtons::B)) { g_screen = S_LIBRARY; logf("storage: back to the library (%s)", source); }
                else if (has(edge, GamepadButtons::DPadUp)) { ui_nav::sidebar_move(m_sidebar, false); logf("sidebar: entry %d (%s)", m_sidebar, source); }
                else if (has(edge, GamepadButtons::DPadDown)) { ui_nav::sidebar_move(m_sidebar, true); logf("sidebar: entry %d (%s)", m_sidebar, source); }
                else if (has(edge, GamepadButtons::DPadRight) && ui_nav::lateral(m_focus, 0, false)) { logf("storage: focus -> roots (%s)", source); }
                else if (has(edge, GamepadButtons::A)) sidebar_activate(t, source);
            } else if (m_stOpen) {
                size_t ngames = storage_game_list().size();
                if ((size_t)m_stGameSel > ngames) m_stGameSel = (int)ngames;   // the list may have shrunk (a delete finished)
                if (has(edge, GamepadButtons::B)) { m_stOpen = false; m_stGameSel = 0; logf("storage: back to the drive list (%s)", source); }
                else if (has(edge, GamepadButtons::DPadDown) && ngames) { m_stGameSel = (m_stGameSel + 1) % (int)(ngames + 1); m_stGameScroll = m_stGameSel; logf("storage: game row %d/%zu (%s)", m_stGameSel, ngames, source); }
                else if (has(edge, GamepadButtons::DPadUp) && ngames) { m_stGameSel = (m_stGameSel + (int)ngames) % (int)(ngames + 1); m_stGameScroll = m_stGameSel; logf("storage: game row %d/%zu (%s)", m_stGameSel, ngames, source); }
                else if (has(edge, GamepadButtons::A)) {
                    if (m_stGameSel == 0) { m_rootSel = (size_t)m_stSel; m_stOpen = false; logf("storage: new installs -> %s (%s)", m_roots[m_stSel].name.c_str(), source); }
                    else {
                        auto rows = storage_game_list();
                        if (m_stGameSel <= (int)rows.size()) {
                            const auto& g = rows[(size_t)m_stGameSel - 1];
                            storage_open_action(g.appid, g.bytes, source);   // item 33: action sheet (move/delete/cancel)
                        }
                    }
                }
            } else {
                int nroots = (int)m_roots.size();
                if (m_stSel >= nroots) m_stSel = nroots ? nroots - 1 : 0;
                if (has(edge, GamepadButtons::B)) { g_screen = S_LIBRARY; logf("storage: back to the library (%s)", source); }
                else if (has(edge, GamepadButtons::DPadLeft)) { ui_nav::lateral(m_focus, 0, true); logf("storage: focus -> sidebar (%s)", source); }
                else if (has(edge, GamepadButtons::DPadDown) && nroots) { ui_nav::storage_drive_move(m_stSel, nroots, true); logf("storage: root %d/%d (%s)", m_stSel, nroots, source); }
                else if (has(edge, GamepadButtons::DPadUp) && nroots) { ui_nav::storage_drive_move(m_stSel, nroots, false); logf("storage: root %d/%d (%s)", m_stSel, nroots, source); }
                else if (has(edge, GamepadButtons::A) && nroots) { m_stOpen = true; m_stGameSel = 0; logf("storage: drive %s games list open (%s)", m_roots[m_stSel].name.c_str(), source); }
            }
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
            autotest(t); cmdhook(t); detail_tick(t); move_tick(t);   // item 33: the move job's completion tick
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
