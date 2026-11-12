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
static void text(int x, int y, const std::string& s, int sc, uint32_t c) {
    for (size_t k = 0; k < s.size(); k++) {
        unsigned char ch = (unsigned char)s[k]; if (ch >= 128) ch = '?';
        for (int r = 0; r < 8; r++) { unsigned char row = (unsigned char)font8x8_basic[ch][r];
            for (int b = 0; b < 8; b++) if (row & (1 << b)) fill(x + (int)k * 8 * sc + b * sc, y + r * sc, sc, sc, c); }
    }
}
static void wrap2(const std::string& s, size_t n, std::string& l1, std::string& l2) {   // two lines max, word wrap, ellipsis on overflow
    l1.clear(); l2.clear(); if (s.size() <= n) { l1 = s; return; }
    size_t cut = s.rfind(' ', n); if (cut == std::string::npos || cut < n / 2) cut = n;
    l1 = s.substr(0, cut); size_t st = cut; while (st < s.size() && s[st] == ' ') st++; l2 = s.substr(st);
    if (l2.size() > n) l2 = l2.substr(0, n - 2) + "..";
}
static int twidth(const std::string& s, int sc) { return (int)s.size() * 8 * sc; }
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
static long long jwt_exp(const std::string& jwt) {
    size_t a = jwt.find('.'); if (a == std::string::npos) return 0; size_t b = jwt.find('.', a + 1); if (b == std::string::npos) return 0;
    std::string p = unb64url(jwt.substr(a + 1, b - a - 1)); size_t k = p.find("\"exp\""); if (k == std::string::npos) return 0;
    size_t c = p.find(':', k); return c == std::string::npos ? 0 : strtoll(p.c_str() + c + 1, nullptr, 10);
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

static std::string cm_build_header(uint64_t steamid64, int32_t sessionId) {
    std::string h; pb_fixed64(h, 1, steamid64); pb_u(h, 2, (uint64_t)(uint32_t)sessionId);
    pb_fixed64(h, 10, UINT64_MAX); pb_fixed64(h, 11, UINT64_MAX); return h;
}
static std::string cm_build_client_logon(const std::string& refreshToken) {
    std::string b;
    pb_u(b, 1, 65580);          // protocol_version
    pb_u(b, 3, 0);              // cell_id
    pb_s(b, 6, "english");      // client_language
    pb_u(b, 7, 20);             // client_os_type (Win11)
    pb_u(b, 8, 1);              // should_remember_password
    pb_u(b, 33, 2);             // chat_mode
    pb_u(b, 102, 1);            // supports_rate_limit_response
    pb_s(b, 108, refreshToken); // access_token (CM login takes the refresh token here)
    return b;
}
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
static CmResult cm_login_test(const std::string& refreshToken) {   // connect to a CM, send ClientLogon, log the eresult, disconnect. Never logs the token.
    CmResult res; std::vector<CmServer> servers;
    if (!fetch_cm_list(servers)) { res.err = "GetCMListForConnect failed"; return res; }
    logf("cm: %zu websocket CM candidates", servers.size());
    std::string sid = jwt_sub(refreshToken); if (sid.empty()) { res.err = "bad refresh token (no sub)"; return res; }
    uint64_t steamid64 = strtoull(sid.c_str(), nullptr, 10);
    for (size_t i = 0; i < servers.size() && i < 5 && !res.gotResponse; i++) {
        CmConn conn;
        try { conn.start(servers[i].host); }
        catch (hresult_error const& e) { logf("cm: connect to %s failed 0x%08x %s", servers[i].host.c_str(), (unsigned)e.code().value, to_string(e.message()).c_str()); continue; }
        catch (...) { logf("cm: connect to %s failed (exception)", servers[i].host.c_str()); continue; }
        res.connected = true; res.cm = servers[i].host;
        conn.send(cm_build_frame(EMSG_ClientLogon, cm_build_header(steamid64, 0), cm_build_client_logon(refreshToken)));
        logf("cm: ClientLogon sent to %s", servers[i].host.c_str());
        auto t0 = std::chrono::steady_clock::now();
        for (;;) {
            int remain = 10000 - (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            std::string frame;
            if (remain <= 0 || !conn.recv(frame, remain)) { logf("cm: timed out waiting for a response from %s", servers[i].host.c_str()); break; }
            std::vector<std::string> subs; cm_flatten(frame, subs);
            for (auto& sf : subs) {
            CmHeader h; if (!cm_parse_frame(sf, h)) continue;
            if (h.emsg == EMSG_ClientLogOnResponse) {
                auto f = pb_parse(h.bodyBytes);
                if (auto e = pb_get(f, 1)) res.eresult = (int32_t)e->u;
                if (auto hb = pb_get(f, 3)) res.heartbeatSec = (int32_t)hb->u;
                res.gotResponse = true; logf("cm: ClientLogOnResponse eresult=%d heartbeat=%ds (from %s)", res.eresult, res.heartbeatSec, servers[i].host.c_str());
                break;
            } else logf("cm: got EMsg %u (%zu bytes), ignoring (phase 1)", h.emsg, h.bodyBytes.size());
            }
            if (res.gotResponse) break;
        }
        conn.close();
    }
    if (!res.gotResponse && res.err.empty()) res.err = "no response from any CM";
    return res;
}

// ---------------------------------------------------------------- token store (LocalState is private to the AppContainer; content never logged)
static std::wstring token_path() { return g_dir + L"\\steam_session.bin"; }
static bool load_token2(std::string& refresh, std::string& access) {   // file = refresh [\n access]
    std::string t; FILE* f = _wfopen((g_dir + L"\\steam_session.bin").c_str(), L"rb"); if (!f) return false; char b[8192]; size_t n = fread(b, 1, sizeof b, f); fclose(f); t.assign(b, n);
    size_t nl = t.find('\n'); refresh = t.substr(0, nl); access = nl == std::string::npos ? "" : t.substr(nl + 1); while (!access.empty() && (access.back() == '\n' || access.back() == '\r')) access.pop_back(); return refresh.size() > 20;
}
static bool load_token(std::string& t) { FILE* f = _wfopen(token_path().c_str(), L"rb"); if (!f) return false; char b[4096]; size_t n = fread(b, 1, sizeof b, f); fclose(f); t.assign(b, n); return n > 20; }
static void save_token(const std::string& t) { FILE* f = _wfopen(token_path().c_str(), L"wb"); if (f) { fwrite(t.data(), 1, t.size(), f); fclose(f); } logf("session token saved (%zu bytes, not shown)", t.size()); }
static void save_token2(const std::string& refresh, const std::string& access) { std::string t = refresh; if (!access.empty()) t += "\n" + access; save_token(t); }
static void delete_token() { DeleteFileW(token_path().c_str()); logf("session token deleted"); }
static bool file_exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
#include "steamdl.h"
#include "storage_report.h"

// ---------------------------------------------------------------- library model + covers
struct Game { uint32_t appid = 0; std::string name; uint32_t playtime = 0; uint64_t lastPlayed = 0; std::atomic<int> cover{0}; int iw = 0, ih = 0; std::vector<uint32_t> pix; };
static const int COVER_W = 260, COVER_H = 390;
static std::mutex g_gm;
static std::vector<std::shared_ptr<Game>> g_games;
static std::atomic<int> g_ownedCount{0};
static std::atomic<bool> g_demo{false};

static bool wic_decode(const std::string& jpg, Game& g, int& hrOut, int maxW = COVER_W, int maxH = COVER_H) {
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
    g.pix.assign((size_t)tw * th, 0);
    if (FAILED(hr = sc->CopyPixels(nullptr, tw * 4, (UINT)g.pix.size() * 4, (BYTE*)g.pix.data()))) { hrOut = hr; return false; }
    g.iw = tw; g.ih = th; hrOut = 0; return true;
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
enum Screen { S_BOOT, S_LOGIN, S_LOADING, S_LIBRARY, S_DETAIL };
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
#include "../uwph/user32/corewindow_feed.h"
#include "game_delete.h"
#include "apibadge.h"

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
            if (result == session_restore::Result::restored) { logf("session restored via CM logon (refresh token)"); set_user_session(s); enter_library_cm(*s); break; }
            if (result == session_restore::Result::qr) break;
            set_status(er == 84 ? "Steam asks to wait - retrying (saved login kept)..." : "Cannot reach Steam - retrying (saved login kept)...");
            logf("session restore: transient failure (eresult %d %s), retry %d", er, err.c_str(), attempt + 1);
            for (int k = 0; k < (er == 84 ? 600 : 80) && !g_quit; k++) Sleep(100);
        }
    }
    g_authResolved = true;
    while (!g_quit) {
        if (g_screen == S_LIBRARY || g_screen == S_LOADING || g_screen == S_DETAIL) {
            if (g_logoutReq.exchange(false)) { delete_token(); if (auto u = user_session()) { u->close(); set_user_session(nullptr); } g_screen = S_LOGIN; refresh.clear(); access.clear(); { std::lock_guard<std::mutex> l(g_gm); g_games.clear(); } continue; }
            if (!refresh.empty() && !g_demo) {
                auto u = user_session();
                bool want = g_reloadReq.exchange(false);
                if ((!u || !u->up) && now_s() - lastRelog > 20) {   // session dropped: log on again with the same refresh token
                    lastRelog = now_s(); int er = 0; std::string err; auto s = cm_logon(refresh, er, err);
                    if (s) { logf("session re-established via CM logon"); set_user_session(s); u = s; want = true; }
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
                if (s) { set_user_session(s); enter_library_cm(*s); }
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
    std::set<uint32_t> m_installed; double m_installedT = -10; int m_installedDel = -1, m_installedDl = -1;
    void refresh_installed(double t) {                       // dl_<appid>.done markers; rescanned every 2 s and after a download/delete
        if (t - m_installedT < 2 && m_installedDel == g_del.gen && m_installedDl == g_dlSeq) return;
        m_installed = installed_appids(); m_installedT = t; m_installedDel = g_del.gen; m_installedDl = g_dlSeq;
    }
    std::vector<std::shared_ptr<Game>> shown_games() { return game_library::shown(g_games, m_onlyInstalled, m_installed); }   // caller holds g_gm
    static constexpr int COLS = 6;

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
    void guest_menu(double t,GamepadButtons b) {
        if(guest_overlay::inputBlocked.load(std::memory_order_relaxed))m_input.release_all(); // nothing stays held behind the menu
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
            if(pressed(GamepadButtons::DPadUp))guest_overlay::selection=(guest_overlay::selection+guest_overlay::ItemCount-1)%guest_overlay::ItemCount;
            else if(pressed(GamepadButtons::DPadDown))guest_overlay::selection=(guest_overlay::selection+1)%guest_overlay::ItemCount;
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
        if(c=="toggle"){if(busy)return;menuOpen=!menuOpen;selection=Resume;if(menuOpen)block_input(true);logf("overlay: menu %s (%s)",menuOpen?"opened":"closed",via);return;}
        int item=c=="select"?selection.load():c=="quit"?Quit:c=="force"?ForceQuit:c=="fps"?Fps:-1;
        if(item<0||busy)return;
        if(item!=Fps)logf("overlay: %s (%s)",label(item),via);
        if(item==Resume){menuOpen=false;return;}   // input returns once buttons are released
        if(item==Fps){fpsOn=!fpsOn;write_text(g_dir+L"\\overlay.txt",fpsOn?"fps=1\n":"fps=0\n");logf("overlay: FPS counter now %s",fpsOn?"on":"off");return;}
        menuOpen=true;busy=true;
        if(item==Quit) {
            unsigned n=close_guest_windows();
            logf("overlay: WM_CLOSE posted to %u guest window(s)",n);
            guest_overlay::set_status(n?"Closing the game...":"No game window; force quitting");
            if(!n){force_quit();return;}
            m_quitDeadline=now_s()+15;return;
        }
        force_quit();
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
        const uint32_t BG = C3(14, 17, 22), TXT = C3(255, 255, 255), DIM = C3(170, 180, 195), YEL = C3(255, 210, 63), GRN = C3(120, 255, 140), RED = C3(255, 110, 110);
        fill(0, 0, W, H, BG);
        auto si = m_dSi; int st = si ? (int)si->state : 2;
        std::string title = (si && st == 1 && !si->name.empty()) ? si->name : m_dName;
        int tsc = title.size() <= 44 ? 5 : 3; text(60, 40, clip(title, (size_t)(1800 / (8 * tsc))), tsc, TXT);
        { char b[80]; snprintf(b, sizeof b, "App %u", m_dApp); text(60, 92, b, 2, DIM); }
        // header image
        fill(60, 130, 616, 353, C3(30, 36, 46));
        if (si && si->img == 1) blit(si->hdr.pix, si->hdr.iw, si->hdr.ih, 60 + (616 - si->hdr.iw) / 2, 130 + (353 - si->hdr.ih) / 2);
        else text(60 + 308 - twidth(si && si->img == 2 ? "no image" : "loading image...", 3) / 2, 290, si && si->img == 2 ? "no image" : "loading image...", 3, DIM);
        frame(60, 130, 616, 353, 3, C3(60, 70, 88));
        apib::Result gfx = apib::combine(apib::lookup(m_dApp), si && st == 1 ? si->dxKind : 0, si && st == 1 ? si->dxNote : std::string()); apib::draw(70, 140, 3, gfx);
        // install state
        int dl = g_dl.state; bool mine = g_dl.appid == m_dApp, down = mine && dl == 1, queued = !down && is_queued(m_dApp), launching = (g_launch.phase == 1 || g_launch.phase == 3) && g_launch.appid == m_dApp;
        bool deleting = deleting_mine(), delDone = g_del.appid == m_dApp && (g_del.state == 2 || g_del.state == 3);
        std::string status; uint32_t sc = TXT;
        if (deleting) { status = "Deleting..."; sc = YEL; }
        else if (down) { double p = g_dl.total ? 100.0 * g_dl.done / g_dl.total : 0; char b[64]; snprintf(b, sizeof b, "Downloading %.0f%%", p); status = b; sc = YEL; }
        else if (queued) { status = "Queued..."; sc = YEL; }
        else if (m_dInst.installed) { double gb = m_dInst.bytes / 1073741824.0; char b[64]; if (m_dInst.bytes == 0) snprintf(b, sizeof b, "Installed"); else if (gb < 0.1) snprintf(b, sizeof b, "Installed - %.1f MB", m_dInst.bytes / 1048576.0); else snprintf(b, sizeof b, "Installed - %.2f GB", gb); status = b; sc = GRN; }
        else { status = "Not installed"; sc = TXT; }
        text(60, 505, status, 4, sc);
        if (down) {
            double p = g_dl.total ? (double)g_dl.done / g_dl.total : 0; fill(60, 548, 616, 26, C3(35, 43, 58)); fill(60, 548, (int)(616 * std::min(1.0, p)), 26, C3(45, 160, 90)); frame(60, 548, 616, 26, 2, C3(60, 70, 88));
            char b[120]; snprintf(b, sizeof b, "%.1f / %.1f MB  %.1f MB/s", g_dl.done / 1048576.0, g_dl.total / 1048576.0, g_dl.speed / 1048576.0); text(60, 582, b, 2, DIM);
            std::string ms; { std::lock_guard<std::mutex> l(g_dl.m); ms = g_dl.msg; } text(60, 606, clip(ms, 38), 2, DIM);
        } else if (mine && (dl == 3 || dl == 4) && !m_dInst.installed) {
            std::string ms; { std::lock_guard<std::mutex> l(g_dl.m); ms = g_dl.msg; } std::string l1, l2; wrap2((dl == 4 ? "Cancelled: " : "Failed: ") + ms, 38, l1, l2);
            text(60, 548, l1, 2, RED); text(60, 572, l2, 2, RED);
        } else if (!m_dNote.empty()) { std::string l1, l2; storage_roots::note_lines(m_dNote, 42, l1, l2); text(60, 548, l1, 2, RED); text(60, 572, l2, 2, RED); }
        else if (deleting) { char b[80]; snprintf(b, sizeof b, "%llu files removed", (unsigned long long)g_del.progress.removed.load()); text(60, 548, b, 2, DIM); }
        else if (delDone && !m_dInst.installed) {
            if (g_del.state == 2) { auto k = g_del.kind.load(); text(60, 548, k == game_library::DeleteKind::folder ? "Deleted. Saves were kept." : k == game_library::DeleteKind::shared ? "Removed. Shared files kept." : "Removed; folder unknown, files kept.", 2, GRN); }
            else { char b[80]; snprintf(b, sizeof b, "Deleted; %u items left behind.", g_del.progress.failed.load()); text(60, 548, b, 2, RED); }
        } else if (delDone && g_del.untouched) text(60, 548, "Delete failed. Nothing was removed.", 2, RED);
        // main button: one A press cancels a download (detail_input.h), never B or the d-pad
        detail_input::State di; di.installed = m_dInst.installed;
        di.download = down ? detail_input::Download::running : queued ? detail_input::Download::queued : detail_input::Download::none;
        di.sinceStart = now_s() - m_dlStartT;
        bool retained=play_runtime::observed.load(std::memory_order_acquire)!=nullptr;
        const char* label = launching ? (g_launch.phase==3?"GUEST RUNNING":"LAUNCHING...") : retained ? "RESTART REQUIRED" : deleting ? "DELETING..." : detail_input::label(di);
        bool busy = launching || retained || deleting || down || queued;
        if (m_dInst.installed && !busy) {   // secondary action: delete (X)
            const char* dl2 = "X: Delete game"; int w = twidth(dl2, 3) + 30; fill(60, 590, w, 46, C3(120, 40, 40)); text(75, 601, dl2, 3, TXT);
        }
        if (!m_dInst.installed && !busy && !g_demo && m_rootSel < m_roots.size()) {   // where a fresh install goes (X cycles)
            std::string sl = m_roots.size() > 1 ? "X: Install to " + storage_roots::label_capacity(m_roots[m_rootSel]) : storage_roots::label_capacity(m_roots[m_rootSel]) + ", no USB found";
            int w = twidth(sl, 2) + 28; fill(60, 590, w, 46, C3(40, 90, 150)); text(74, 604, sl, 2, TXT);
        }
        if (m_dInst.installed && !busy && !g_demo) {   // secondary action: sync cloud saves (Y)
            bool working = cloud_status(m_dApp).phase == 1; const char* cl = working ? "Syncing cloud..." : "Y: Sync cloud"; int w = twidth(cl, 2) + 28;
            fill(440, 590, w, 46, working ? C3(60, 66, 78) : C3(40, 90, 150)); text(454, 604, cl, 2, TXT);
        }
        if (m_dInst.installed && !g_demo) {   // cloud status line
            CloudStatus cs = cloud_status(m_dApp);
            if (cs.phase != 0) { std::string l1, l2; wrap2(cs.text, 38, l1, l2); uint32_t cc = cs.tone == 1 ? GRN : cs.tone == 2 ? YEL : DIM; text(60, 776, l1, 2, cc); text(60, 794, l2, 2, cc); }
        }
        fill(60, 650, launch_button::width, 120, (down || queued) ? C3(45, 90, 200) : busy ? C3(60, 66, 78) : m_dInst.installed ? C3(40, 150, 80) : C3(45, 90, 200)); frame(60, 650, launch_button::width, 120, 8, YEL);
        auto labelLayout=launch_button::label_layout(std::char_traits<char>::length(label));
        text(60 + labelLayout.x, 650 + labelLayout.y, label, labelLayout.scale, TXT);
        text(60 + 308 - twidth(detail_input::hint(di), 2) / 2, 734, detail_input::hint(di), 2, C3(210, 220, 240));
        // Play offers a choice: say so before it is pressed (screenshot-able).
        if (m_dInst.installed && !busy && !g_demo && option_count() > 1) {
            char b[96]; snprintf(b, sizeof b, "Play asks which of %zu run options to start", option_count());
            text(60, 786, b, 2, C3(210, 220, 240));
        }
        // right column: meta + description
        std::vector<std::string> meta;
        auto addm = [&](const std::string& k, const std::string& v) { if (v.empty()) return; for (auto& l : wrap_lines(k + v, 46)) meta.push_back(l); };
        if (st == 1) { addm("Released: ", si->release); addm("Developer: ", si->dev); addm("Genres: ", si->genres); }
        std::string lib = owned(m_dApp) ? "Owned (in your library)" : g_demo ? "" : "Not in your library list";
        if (!lib.empty()) meta.insert(meta.begin(), "Library: " + lib);   // first row: the yellow accent marks ownership
        { std::string gl = "Graphics: " + apib::describe(gfx); if (!gfx.note.empty() && gfx.src == 1 && gfx.kind != apib::K_D3D11) gl += " [" + gfx.note + "]"; for (auto& l : wrap_lines(gl, 46)) meta.push_back(l); }
        int y = 130; for (size_t i = 0; i < meta.size() && i < 8; i++, y += 36) text(740, y, meta[i], 3, i == 0 && st == 1 ? YEL : meta[i].rfind("Graphics: ", 0) == 0 ? (gfx.kind == apib::K_D3D11 ? GRN : gfx.kind == apib::K_LEGACY || gfx.kind == apib::K_D3D12 ? DIM : TXT) : TXT);
        y += 14; int dy0 = y, dy1 = 790;
        if (st == 0) text(740, dy0, "Loading store info...", 3, DIM);
        else if (st == 2) { text(740, dy0, "Store info unavailable", 3, RED); text(740, dy0 + 40, "You can still install and play.", 3, DIM); if (si && !si->why.empty()) text(740, dy0 + 80, clip(si->why, 70), 2, DIM); }
        else if (!si->desc.empty()) {
            auto lines = wrap_lines(si->desc, 46); int vis = (dy1 - dy0) / 34, maxScroll = std::max(0, (int)lines.size() - vis);
            m_dScroll = std::max(0, std::min(m_dScroll, maxScroll));
            for (int i = 0; i < vis && m_dScroll + i < (int)lines.size(); i++) text(740, dy0 + i * 34, lines[m_dScroll + i], 3, C3(210, 218, 230));
            if (maxScroll > 0) { int th = dy1 - dy0, thumb = std::max(30, th * vis / (int)lines.size()); fill(1880, dy0, 8, th, C3(35, 43, 58)); fill(1880, dy0 + (th - thumb) * m_dScroll / maxScroll, 8, thumb, YEL);
                if (m_dScroll < maxScroll) text(1700, dy1 + 2, "more (D-pad down)", 2, DIM); }
        }
        // launch panel
        if (g_launch.phase != 0 && g_launch.appid == m_dApp) {
            fill(60, 810, 1800, 180, C3(10, 12, 16)); frame(60, 810, 1800, 180, 3, g_launch.phase == 1 ? YEL : C3(60, 70, 88));
            if (g_launch.phase == 1) { char b[100]; snprintf(b, sizeof b, "Launching... (%.0f s) running guest loader", now_s() - g_launch.started); text(80, 826, b, 3, YEL); }
            else { std::string r; { std::lock_guard<std::mutex> l(g_launch.m); r = g_launch.result; }
                bool ok;{std::lock_guard<std::mutex> l(g_launch.m);ok=g_launch.ok;}
                text(80, 822, g_launch.phase==3?"Guest running (playability unverified)":ok?"Guest completed (playability unverified)":retained?"Launch failed - restarting launcher to unload it":"Launch failed - nothing was left running; press A or pick another game", 3, g_launch.phase==3?YEL:ok?GRN:RED); auto ls = wrap_lines(r, 73); for (size_t i = 0; i < ls.size() && i < 4; i++) text(80, 858 + (int)i * 34, ls[i], 3, TXT); }
        }
        text(60, 1050, "A: Install / Play / cancel download   X: Delete   Y: Cloud sync   B: back   Menu: back   D-pad up/down: scroll", 2, C3(120, 130, 145));
        if (m_pick.open) draw_picker();
        if (m_confirmDelete) {
            std::string q = "Delete " + clip(m_dName, 30) + "?"; char sz[96];
            if (m_delKind == game_library::DeleteKind::shared) snprintf(sz, sizeof sz, "Its files are shared with another installed game and stay.");
            else if (m_delKind == game_library::DeleteKind::unknown_folder) snprintf(sz, sizeof sz, "Leaves the installed list. Its folder is unknown, so files stay.");
            else if (m_dInst.bytes >= 107374182) snprintf(sz, sizeof sz, "Frees %.2f GB on this console. Saves are kept.", m_dInst.bytes / 1073741824.0);
            else if (m_dInst.bytes) snprintf(sz, sizeof sz, "Frees %.1f MB on this console. Saves are kept.", m_dInst.bytes / 1048576.0); else snprintf(sz, sizeof sz, "Removes the game files from this console. Saves are kept.");
            fill(360, 380, 1200, 300, C3(10, 12, 16)); frame(360, 380, 1200, 300, 6, YEL);
            int qs = twidth(q, 5) <= 1120 ? 5 : 3; text(960 - twidth(q, qs) / 2, 420, q, qs, TXT);
            text(960 - twidth(sz, 2) / 2, 500, sz, 2, DIM);
            text(960 - twidth("A: Yes, delete        B: Cancel", 3) / 2, 600, "A: Yes, delete        B: Cancel", 3, TXT);
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
        text(x + 40, y + 30, "How should this game start?", 4, TXT);
        text(x + 40, y + 84, clip(m_dName, 44) + " offers more than one run option. D-pad up/down to choose, A to run, B to cancel.", 2, DIM);
        int ry = y + 132;
        for (int i = 0; i < rows; i++) {
            const launch_target::Choice& c = m_pick.rows[i];
            bool sel = i == m_pick.sel;
            fill(x + 30, ry, w - 60, rowH - 10, sel ? C3(45, 90, 200) : C3(27, 35, 48));
            if (sel) frame(x + 30, ry, w - 60, rowH - 10, 3, YEL);
            text(x + 50, ry + 14, clip(c.label, 60), 3, c.selectable ? TXT : DIM);
            const char* note = launch_target::arch_note(c.arch);
            if (note) text(x + 50, ry + 42, note, 2, DIM);
            else if (c.arch == launch_target::arch64) text(x + 50, ry + 42, "64-bit", 2, GRN);
            ry += rowH;
        }
        if (!m_pick.note.empty()) text(x + 40, ry + 8, clip(m_pick.note, 90), 2, RED);
        text(x + 40, y + h - 40, "A: run this option        B: cancel", 3, TXT);
    }
    // One-shot synthetic command file LocalState\cmd.txt (consumed on read). Lines: open <appid> | press (=A) | back (=B) | view | up | down | left | right | lb | rb | x | y | sleep <s>.
    // Never periodic, never present unless a tester/script writes it; every executed line is logged as SYNTHETIC.
    void cmdhook(double t) {
        static double next = 0, holdUntil = 0; static std::deque<std::string> q;
        if (t < next) return; next = t + 0.25;
        if (g_screen != S_LIBRARY && g_screen != S_DETAIL) return;
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
                            // RCA experiment: lockmode.txt present => the chain keeps its
                            // first mode (later ResizeBuffers suppressed with S_OK).
                            guest_overlay::lockFirstMode=read_file_s(g_dir+L"\\lockmode.txt",saved);
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

    // ---- drawing
    void draw_login() {
        m_btnFocus = true; m_confirmLogout = false; // Refresh is the sole login action.
        fill(0, 0, W, H, C3(14, 17, 22));
        text(80, 50, "STEAM LOGIN", 6, C3(255, 255, 255));
        text(80, 130, "Scan the QR code with the Steam mobile app and approve the sign-in", 3, C3(170, 180, 195));
        std::string url, st; { std::lock_guard<std::mutex> l(g_sm); url = g_qrUrl; st = g_status; }
        int qx = 120, qy = 240, size = 0;
        if (!url.empty()) {
            auto qr = qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
            int sc = 12, n = qr.getSize(), q = 4; size = (n + 2 * q) * sc;
            fill(qx, qy, size, size, C3(255, 255, 255));
            for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) if (qr.getModule(x, y)) fill(qx + (x + q) * sc, qy + (y + q) * sc, sc, sc, C3(0, 0, 0));
        } else { size = 600; fill(qx, qy, size, size, C3(30, 36, 46)); text(qx + 40, qy + 280, "waiting for QR...", 3, C3(170, 180, 195)); }
        int bx = qx + size + 80, by = qy + 40;
        fill(bx, by, 560, 130, m_btnFocus ? C3(45, 90, 200) : C3(40, 46, 58));
        frame(bx, by, 560, 130, 8, C3(255, 210, 63));
        text(bx + 280 - twidth("REFRESH QR", 4) / 2, by + 24, "REFRESH QR", 4, C3(255, 255, 255));
        text(bx + 280 - twidth("press A or X", 3) / 2, by + 78, "press A or X", 3, C3(210, 220, 240));
        text(bx, by + 190, st, 3, g_scanned ? C3(120, 255, 140) : C3(255, 255, 255));
        double left = 270 - (now_s() - g_qrStart); if (url.empty() || left < 0) left = 0;
        char b[64]; snprintf(b, sizeof b, "QR refreshes automatically in %d:%02d", (int)left / 60, (int)left % 60);
        text(bx, by + 250, b, 3, C3(170, 180, 195));
        text(bx, by + 320, "Your login token is stored only on this console.", 2, C3(120, 130, 145));
        text(bx, by + 350, "It is never shown or written to logs.", 2, C3(120, 130, 145));
    }
    void draw_library(double t) {
        fill(0, 0, W, H, C3(14, 17, 22));
        refresh_installed(t);
        int owned = g_ownedCount; size_t listed, total; { std::lock_guard<std::mutex> l(g_gm); total = g_games.size(); listed = m_onlyInstalled ? shown_games().size() : total; }
        const int CW = 280, CH = 452, GX = 20, X0 = 60, Y0 = 120, PITCH = CH + 16;
        { // smooth row scroll: m_scroll (in rows) eases toward m_top; position changes still only come via grid_position()
          double dt = m_scrollT > 0 ? std::min(0.1, t - m_scrollT) : 0.0; m_scrollT = t;
          double d = (double)m_top - m_scroll;
          if (std::fabs(d) < 0.004) m_scroll = m_top; else m_scroll += d * std::min(1.0, dt * 14.0);
        }
        { std::lock_guard<std::mutex> l(g_gm);
          auto list = shown_games();
          if (!list.empty() && m_sel >= (int)list.size()) { int ns = game_library::clamp_selection(m_sel, list.size()); grid_position(ns, std::min(m_top, ns / COLS), "shown list shrank"); }
          int lo = std::max(0, (m_top - 1) * COLS), hi = (int)std::min<size_t>(list.size(), (size_t)(m_top + 3) * COLS);   // visible rows + 1 row margin each side
          auto evict = [](Game& g) { int c = g.cover; if (c == 1 || c == 2) { g.cover = 0; std::vector<uint32_t>().swap(g.pix); } };
          for (int i = 0; i < (int)list.size(); i++) {
              Game& g = *list[i];
              if (i >= lo && i < hi) { int z = 0; if (g.cover.compare_exchange_strong(z, 3)) { std::lock_guard<std::mutex> ql(g_qm); g_q.push_back(list[i]); g_qcv.notify_one(); } }
              else evict(g);
          }
          if (m_onlyInstalled) for (auto& g : g_games) if (!m_installed.count(g->appid)) evict(*g);   // hidden by the filter
          for (size_t i = 0; i < list.size(); i++) {
            int row = (int)i / COLS, col = (int)i % COLS;
            int x = X0 + col * (CW + GX), y = Y0 + (int)std::lround((row - m_scroll) * PITCH); Game& g = *list[i];
            if (y >= 1040 || y + CH <= 0) continue;
            bool sel = (int)i == m_sel;
            fill(x, y, CW, CH, C3(27, 35, 48));
            int cx = x + 10, cy = y + 8; fill(cx, cy, COVER_W, COVER_H, C3(20, 24, 32));
            int c = g.cover;
            if (c == 1) { int ox = cx + (COVER_W - g.iw) / 2, oy = cy + (COVER_H - g.ih) / 2;
                for (int j = 0; j < g.ih; j++) { int py = oy + j; if (py < 0 || py >= H) continue; memcpy(&g_fb[py * W + ox], &g.pix[j * g.iw], g.iw * 4); } }
            else text(cx + 20, cy + 180, c == 2 ? "no cover" : "loading...", 2, C3(120, 130, 145));
            bool dlOverlay = g_dl.appid == g.appid && g_dl.state != 0;
            if (!dlOverlay && m_installed.count(g.appid)) { const char* it = "INSTALLED"; fill(cx, cy + COVER_H - 30, twidth(it, 2) + 16, 30, C3(30, 120, 60)); text(cx + 8, cy + COVER_H - 23, it, 2, C3(255, 255, 255)); }
            if (dlOverlay) {
                int stt = g_dl.state; uint64_t tot = g_dl.total, dn = g_dl.done; double p = tot ? std::min(1.0, (double)dn / (double)tot) : 0;
                fill(cx, cy + COVER_H - 62, COVER_W, 62, C3(0, 0, 0));
                fill(cx + 8, cy + COVER_H - 18, (int)((COVER_W - 16) * (stt == 2 ? 1.0 : p)), 10, stt == 3 || stt == 4 ? C3(220, 70, 70) : C3(60, 200, 90)); frame(cx + 8, cy + COVER_H - 18, COVER_W - 16, 10, 1, C3(200, 200, 200));
                char sb[80]; if (stt == 1) snprintf(sb, sizeof sb, "%.0f%% %.1f/%.1f MB %.1f MB/s", p * 100, dn / 1048576.0, tot / 1048576.0, g_dl.speed / 1048576.0); else snprintf(sb, sizeof sb, "%s", stt == 2 ? detail_input::finished_text() : stt == 3 ? "Failed" : "Cancelled");
                text(cx + 8, cy + COVER_H - 56, sb, 1, C3(255, 255, 255));
                std::string dm; { std::lock_guard<std::mutex> ml(g_dl.m); dm = g_dl.msg; } text(cx + 8, cy + COVER_H - 40, clip(dm, 32), 1, C3(190, 200, 215));
            }
            { auto br = apib::lookup(g.appid); apib::draw(cx + 6, cy + 6, 2, br); }
            std::string l1, l2; wrap2(g.name, 16, l1, l2);
            text(x + 10, y + COVER_H + 16, l1, 2, C3(255, 255, 255));
            if (!l2.empty()) text(x + 10, y + COVER_H + 16 + 20, l2, 2, C3(190, 200, 215));
            if (sel) frame(x - 4, y - 4, CW + 8, CH + 8, 6, C3(255, 210, 63));
          }
          if (g_games.empty()) text(60, 300, g_screen == S_LOADING ? "Loading your games..." : "No games to show.", 4, C3(170, 180, 195));
          else if (list.empty()) { text(60, 300, "No installed games.", 4, C3(170, 180, 195)); text(60, 360, "Press View to show all games.", 3, C3(120, 130, 145)); }
        }
        fill(0, 0, W, Y0 - 8, C3(14, 17, 22)); fill(0, 1040, W, H - 1040, C3(14, 17, 22));   // mask partially scrolled rows
        text(60, 24, "LIBRARY", 5, C3(255, 255, 255));
        char b[200]; int first = (int)std::min<size_t>(listed, (size_t)m_top * COLS + 1), last = (int)std::min<size_t>(listed, (size_t)(m_top + 2) * COLS);
        if (m_onlyInstalled) snprintf(b, sizeof b, "%zu installed of %zu listed - showing %d-%d  (row %d of %d)", listed, total, first, last, m_sel / COLS + 1, (int)std::max<size_t>(1, (listed + COLS - 1) / COLS));
        else snprintf(b, sizeof b, "%d games owned - %zu listed - showing %d-%d  (row %d of %d)", (int)owned, listed, first, last, m_sel / COLS + 1, (int)((listed + COLS - 1) / COLS));
        text(60, 74, b, 3, C3(170, 180, 195));
        // chips, laid out right to left: logout (B), sort (Y), filter (View)
        { const char* lo = "Logout (B)"; int w2 = twidth(lo, 3) + 30, x = W - w2 - 60; fill(x, 24, w2, 50, C3(120, 40, 40)); text(x + 15, 38, lo, 3, C3(255, 255, 255));
          char c[64]; snprintf(c, sizeof c, "Sort: %s (Y)", SORT_NAMES[g_sortMode]); int w = twidth(c, 3) + 30; x -= w + 20; fill(x, 24, w, 50, C3(45, 90, 200)); frame(x, 24, w, 50, 3, C3(255, 210, 63)); text(x + 15, 38, c, 3, C3(255, 255, 255));
          const char* fl = m_onlyInstalled ? "Installed only (View)" : "All games (View)"; int w3 = twidth(fl, 3) + 30; x -= w3 + 20;
          fill(x, 24, w3, 50, m_onlyInstalled ? C3(30, 120, 60) : C3(45, 90, 200)); frame(x, 24, w3, 50, 3, C3(255, 210, 63)); text(x + 15, 38, fl, 3, C3(255, 255, 255)); }
        if (g_demo) text(900, 84, "DEMO LIST - not your account", 3, C3(255, 210, 63));
        std::string err; { std::lock_guard<std::mutex> l(g_sm); err = g_err; }
        if (!err.empty()) text(60, 104, err, 3, C3(255, 100, 100));
        { // scrollbar / position indicator
          int rows = (int)((listed + COLS - 1) / COLS); int tx = 1872, ty = Y0, th = 1040 - Y0;
          fill(tx, ty, 12, th, C3(35, 43, 58));
          if (rows > 0) { int vis = std::min(rows, 2); int thumb = std::max(40, th * vis / rows); double span = std::max(1, rows - 2);
            int py = ty + (int)((th - thumb) * std::min(1.0, std::max(0.0, m_scroll / span))); fill(tx, py, 12, thumb, C3(255, 210, 63)); }
        }
        text(60, 1050, "D-pad: move   LB/RB: page   A: Open   View: installed / all   Y: sort   X: reload   B: logout", 2, C3(120, 130, 145));
        if (m_confirmLogout) {
            fill(460, 380, 1000, 300, C3(10, 12, 16)); frame(460, 380, 1000, 300, 6, C3(255, 210, 63));
            text(960 - twidth("Log out of Steam?", 5) / 2, 420, "Log out of Steam?", 5, C3(255, 255, 255));
            text(960 - twidth("This deletes the saved login on this console only.", 2) / 2, 500, "This deletes the saved login on this console only.", 2, C3(170, 180, 195));
            text(960 - twidth("A: Yes, log out        B: Cancel", 3) / 2, 600, "A: Yes, log out        B: Cancel", 3, C3(255, 255, 255));
        }
    }
    void draw(double t) {
        int s = g_screen;
        if (s == S_LOGIN) draw_login();
        else if (s == S_LIBRARY || s == S_LOADING) draw_library(t);
        else if (s == S_DETAIL) draw_detail(t);
        else { fill(0, 0, W, H, C3(14, 17, 22)); std::string st; { std::lock_guard<std::mutex> l(g_sm); st = g_status; } text(80, 480, st, 4, C3(255, 255, 255)); }
        if (g_dl.state != 0 && s != S_BOOT && s != S_DETAIL) { char b[200]; std::string nm, ms; { std::lock_guard<std::mutex> l(g_dl.m); nm = g_dl.name; ms = g_dl.msg; }
            int stt = g_dl.state; double p = g_dl.total ? 100.0 * g_dl.done / g_dl.total : 0; snprintf(b, sizeof b, "DOWNLOAD %s: %s %.0f%% (%d/%d files, %.1f MB/s) %s", nm.c_str(), stt == 1 ? "running" : stt == 2 ? "DONE" : stt == 3 ? "FAILED" : "cancelled", p, (int)g_dl.filesDone, (int)g_dl.files, g_dl.speed / 1048576.0, clip(ms, 60).c_str());
            fill(0, 1000, W, 36, C3(0, 0, 0)); text(20, 1008, b, 2, stt == 3 ? C3(255, 120, 120) : C3(120, 255, 140)); }
        // heartbeat so frames visibly update
        fill((int)((m_frame * 4) % 1800), 4, 100, 6, C3(60, 200, 90));
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
            if (has(edge, GamepadButtons::X)) { if (m_dInst.installed) detail_delete(source); else detail_storage(source); return; }
            if (has(edge, GamepadButtons::Y)) { detail_cloud(source); return; }
            if (cloud_status(m_dApp).text.find("LB: use cloud") != std::string::npos) {
                if (has(edge, GamepadButtons::LeftShoulder)) { detail_cloud("LB use cloud", steam_cloud::Prefer::Cloud); return; }
                if (has(edge, GamepadButtons::RightShoulder)) { detail_cloud("RB keep Xbox", steam_cloud::Prefer::Local); return; }
            }
            if (has(edge, GamepadButtons::A)) detail_action(source);
            if (has(edge, GamepadButtons::B)) detail_back(source);
            if (has(edge, GamepadButtons::View) || has(edge, GamepadButtons::Menu)) close_detail("Menu/View");
            if (has(edge, GamepadButtons::DPadDown)) m_dScroll += 3;
            if (has(edge, GamepadButtons::DPadUp)) m_dScroll = std::max(0, m_dScroll - 3);
        }
        else if (s == S_LIBRARY) {
            if (!m_confirmLogout && has(edge, GamepadButtons::View)) {
                m_onlyInstalled = !m_onlyInstalled; m_installedT = -10; refresh_installed(t); grid_position(0, 0, "View filter");
                logf("library filter -> %s (%zu installed)", m_onlyInstalled ? "installed only" : "all games", m_installed.size()); return;
            }
            std::vector<std::shared_ptr<Game>> list; { std::lock_guard<std::mutex> l(g_gm); list = shown_games(); } size_t n = list.size();
            if (m_confirmLogout) {
                if (has(edge, GamepadButtons::A)) { m_confirmLogout = false; g_logoutReq = true; grid_position(0, 0, "logout confirmed"); logf("logout confirmed: deleting local session file only (no revoke call)"); }
                else if (has(edge, GamepadButtons::B)) { m_confirmLogout = false; logf("logout cancelled"); }
                return;
            }
            int sel = m_sel;
            if (has(edge, GamepadButtons::DPadRight) && sel + 1 < (int)n) sel++;
            if (has(edge, GamepadButtons::DPadLeft) && sel > 0) sel--;
            if (has(edge, GamepadButtons::DPadDown) && (sel / COLS + 1) * COLS < (int)n) sel = std::min((int)n - 1, sel + COLS);
            if (has(edge, GamepadButtons::DPadUp) && sel - COLS >= 0) sel -= COLS;
            if (has(edge, GamepadButtons::RightShoulder)) sel = std::min((int)n - 1, sel + 2 * COLS);
            if (has(edge, GamepadButtons::LeftShoulder)) sel = std::max(0, sel - 2 * COLS);
            if (n == 0) sel = 0;
            sel = std::max(0, std::min(sel, (int)n - 1));
            int top = m_top, r = sel / COLS; if (r < top) top = r; if (r > top + 1) top = r - 1;
            grid_position(sel, top, source);
            if (has(edge, GamepadButtons::A) && n) { uint32_t aid; std::string nm; { std::lock_guard<std::mutex> l(g_gm); aid = list[m_sel]->appid; nm = list[m_sel]->name; } open_detail(aid, nm, source); return; }
            if (has(edge, GamepadButtons::Y)) { int m = (g_sortMode + 1) % 3; g_sortMode = m; { std::lock_guard<std::mutex> l(g_gm); sort_games(m); } grid_position(0, 0, "Y resort"); logf("sort mode -> %s", SORT_NAMES[m]); }
            if (has(edge, GamepadButtons::X) && !g_demo) { g_reloadReq = true; grid_position(0, 0, "X library reload"); }
            if (has(edge, GamepadButtons::B) && !g_demo) m_confirmLogout = true;   // B never stops a download
        }
    }

    void Run() {
        logf("Run: entering"); m_window.Activate();
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
