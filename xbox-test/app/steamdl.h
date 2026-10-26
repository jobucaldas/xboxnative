// steamdl.h - Steam CM session + depot downloader. Included by main.cpp (single translation unit), after the CM helpers.
// Everything here is the client side of the public Steam content protocol (CM websocket + CDN); no DRM handling of any kind.
// Tokens are never logged.
#pragma once
#include <bcrypt.h>

// ---------------------------------------------------------------- crypto (CNG)
struct Sha1 {
    BCRYPT_HASH_HANDLE h = nullptr;
    Sha1() { BCryptCreateHash(BCRYPT_SHA1_ALG_HANDLE, &h, nullptr, 0, nullptr, 0, 0); }
    ~Sha1() { if (h) BCryptDestroyHash(h); }
    void upd(const void* p, size_t n) { while (n) { ULONG c = (ULONG)std::min<size_t>(n, 1u << 30); BCryptHashData(h, (PUCHAR)p, c, 0); p = (const char*)p + c; n -= c; } }
    std::string fin() { UCHAR d[20] = {}; BCryptFinishHash(h, d, 20, 0); return std::string((char*)d, 20); }
};
static std::string sha1_of(const void* p, size_t n) { Sha1 s; s.upd(p, n); return s.fin(); }
static std::string hex(const std::string& b) { static const char* x = "0123456789abcdef"; std::string o; for (unsigned char c : b) { o += x[c >> 4]; o += x[c & 15]; } return o; }

// Steam "symmetric encrypt" format: first 16 bytes = IV encrypted with AES-256-ECB, rest = AES-256-CBC with PKCS7.
struct AesKey {
    BCRYPT_ALG_HANDLE aecb = nullptr, acbc = nullptr; BCRYPT_KEY_HANDLE kecb = nullptr, kcbc = nullptr;
    bool init(const std::string& key) {
        if (key.size() != 32) return false;
        if (BCryptOpenAlgorithmProvider(&aecb, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) return false;
        if (BCryptOpenAlgorithmProvider(&acbc, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) return false;
        if (BCryptSetProperty(aecb, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0) < 0) return false;
        if (BCryptSetProperty(acbc, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0) < 0) return false;
        if (BCryptGenerateSymmetricKey(aecb, &kecb, nullptr, 0, (PUCHAR)key.data(), 32, 0) < 0) return false;
        if (BCryptGenerateSymmetricKey(acbc, &kcbc, nullptr, 0, (PUCHAR)key.data(), 32, 0) < 0) return false;
        return true;
    }
    ~AesKey() { if (kecb) BCryptDestroyKey(kecb); if (kcbc) BCryptDestroyKey(kcbc); if (aecb) BCryptCloseAlgorithmProvider(aecb, 0); if (acbc) BCryptCloseAlgorithmProvider(acbc, 0); }
    bool decrypt(const std::string& in, std::string& out) {
        if (in.size() < 32 || ((in.size() - 16) % 16)) return false;
        UCHAR iv[16]; ULONG cb = 0;
        if (BCryptDecrypt(kecb, (PUCHAR)in.data(), 16, nullptr, nullptr, 0, iv, 16, &cb, 0) < 0) return false;
        out.resize(in.size() - 16); cb = 0;
        if (BCryptDecrypt(kcbc, (PUCHAR)in.data() + 16, (ULONG)(in.size() - 16), nullptr, iv, 16, (PUCHAR)&out[0], (ULONG)out.size(), &cb, BCRYPT_BLOCK_PADDING) < 0) return false;
        out.resize(cb); return true;
    }
};

// ---------------------------------------------------------------- decompression (zip, Valve LZMA "VZa", Valve zstd "VSZa")
static void* lz_alloc(ISzAllocPtr, size_t n) { return malloc(n); }
static void lz_free(ISzAllocPtr, void* p) { free(p); }
static uint32_t rd16(const std::string& s, size_t o) { return (uint32_t)(uint8_t)s[o] | ((uint32_t)(uint8_t)s[o + 1] << 8); }
static bool unzip_first(const std::string& z, std::string& out) {
    if (z.size() < 22) return false;
    size_t e = std::string::npos; for (size_t i = z.size() - 22 + 1; i-- > 0;) { if (get_u32le(z, i) == 0x06054b50) { e = i; break; } if (z.size() - i > 70000) break; }
    if (e == std::string::npos) return false;
    size_t cd = get_u32le(z, e + 16); if (cd + 46 > z.size() || get_u32le(z, cd) != 0x02014b50) return false;
    uint32_t method = rd16(z, cd + 10), csize = get_u32le(z, cd + 20), usize = get_u32le(z, cd + 24); size_t lo = get_u32le(z, cd + 42);
    if (lo + 30 > z.size() || get_u32le(z, lo) != 0x04034b50) return false;
    size_t data = lo + 30 + rd16(z, lo + 26) + rd16(z, lo + 28); if (data + csize > z.size()) return false;
    out.resize(usize);
    if (method == 0) { if (csize != usize) return false; memcpy(&out[0], z.data() + data, usize); return true; }
    if (method != 8) return false;
    z_stream s{}; if (inflateInit2(&s, -15) != Z_OK) return false;
    s.next_in = (Bytef*)z.data() + data; s.avail_in = csize; s.next_out = (Bytef*)&out[0]; s.avail_out = usize;
    int rc = inflate(&s, Z_FINISH); bool ok = rc == Z_STREAM_END && s.total_out == usize; inflateEnd(&s); return ok;
}
static bool chunk_decompress(const std::string& d, size_t expect, std::string& out) {
    if (d.size() > 4 && memcmp(d.data(), "VSZa", 4) == 0) {
        if (d.size() < 8 + 15) return false;
        size_t dn = d.size() - 8 - 15; out.resize(expect);
        size_t r = ZSTD_decompress(&out[0], expect, d.data() + 8, dn);
        return !ZSTD_isError(r) && r == expect;
    }
    if (d.size() > 3 && memcmp(d.data(), "VZa", 3) == 0) {
        if (d.size() < 12 + 10) return false;
        uint32_t osz = get_u32le(d, d.size() - 6); if (osz != expect) return false;
        out.resize(osz); SizeT dl = osz, sl = d.size() - 12 - 10; ELzmaStatus st; ISzAlloc al = {lz_alloc, lz_free};
        SRes r = LzmaDecode((Byte*)&out[0], &dl, (const Byte*)d.data() + 12, &sl, (const Byte*)d.data() + 7, 5, LZMA_FINISH_ANY, &st, &al);
        return r == SZ_OK && dl == osz;
    }
    if (d.size() > 2 && d[0] == 'P' && d[1] == 'K') { if (!unzip_first(d, out)) return false; return out.size() == expect; }
    return false;
}

// ---------------------------------------------------------------- VDF text parser (PICS appinfo buffer)
struct Vdf {
    std::string val; bool obj = false; std::vector<std::string> keys; std::vector<Vdf> kids;
    const Vdf* get(const std::string& k) const { for (size_t i = 0; i < keys.size(); i++) if (keys[i] == k) return &kids[i]; return nullptr; }
    const Vdf* at(std::initializer_list<const char*> p) const { const Vdf* c = this; for (auto k : p) { if (!c) return nullptr; c = c->get(k); } return c; }
    std::string str(std::initializer_list<const char*> p, const std::string& d = "") const { const Vdf* c = at(p); return c && !c->obj ? c->val : d; }
};
static bool vdf_tok(const std::string& s, size_t& i, std::string& tok, int& kind) {   // kind: 0 string, 1 '{', 2 '}'
    for (;;) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n' || s[i] == 0)) i++;
        if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') { while (i < s.size() && s[i] != '\n') i++; continue; }
        break;
    }
    if (i >= s.size()) return false;
    if (s[i] == '{') { kind = 1; i++; return true; }
    if (s[i] == '}') { kind = 2; i++; return true; }
    kind = 0; tok.clear();
    if (s[i] == '"') {
        i++;
        while (i < s.size() && s[i] != '"') { if (s[i] == '\\' && i + 1 < s.size()) { i++; char c = s[i]; tok += c == 'n' ? '\n' : c == 't' ? '\t' : c; } else tok += s[i]; i++; }
        i++; return true;
    }
    while (i < s.size() && !strchr(" \t\r\n{}\"", s[i])) tok += s[i++];
    return true;
}
static bool vdf_block(const std::string& s, size_t& i, Vdf& out, int depth) {   // parses until '}' or EOF
    if (depth > 64) return false; out.obj = true;
    for (;;) {
        std::string k; int kind; if (!vdf_tok(s, i, k, kind)) return true;
        if (kind == 2) return true; if (kind == 1) return false;
        std::string v; if (!vdf_tok(s, i, v, kind)) return false;
        out.keys.push_back(k); out.kids.emplace_back();
        if (kind == 1) { if (!vdf_block(s, i, out.kids.back(), depth + 1)) return false; }
        else if (kind == 0) { out.kids.back().val = v; }
        else return false;
    }
}
static bool vdf_parse(const std::string& s, Vdf& root) { size_t i = 0; return vdf_block(s, i, root, 0); }

// ---------------------------------------------------------------- Steam CM session (persistent websocket, job dispatch, heartbeat)
enum { EMSG_ServiceMethodResponse = 147, EMSG_ServiceMethodCall = 151, EMSG_ClientHeartBeat = 703, EMSG_GetDepotKey = 5438, EMSG_GetDepotKeyResp = 5439,
       EMSG_PICSProductInfo = 8903, EMSG_PICSProductInfoResp = 8904, EMSG_PICSAccessToken = 8905, EMSG_PICSAccessTokenResp = 8906 };
static const uint64_t STEAMID_ANON = (1ull << 56) | (10ull << 52);
struct JobMsg { uint32_t emsg = 0; int32_t eresult = 0; std::string body, errmsg; };

struct CmSession : std::enable_shared_from_this<CmSession> {
    CmConn conn; std::atomic<bool> up{false}, stop{false}; bool anon = false;
    uint64_t steamid = 0; int32_t sessionId = 0; int hbSec = 30; std::string host;
    std::mutex jm; std::condition_variable jcv; std::map<uint64_t, std::deque<JobMsg>> jobs; std::mutex sendm; std::atomic<uint64_t> nextJob{1};

    void reader() {
        for (;;) {
            if (stop) break;
            std::string frame;
            if (!conn.recv(frame, 1000)) { bool cl; { std::lock_guard<std::mutex> l(conn.m); cl = conn.closed && conn.inbox.empty(); } if (cl) break; continue; }
            std::vector<std::string> subs; cm_flatten(frame, subs);
            for (auto& sf : subs) {
                CmHeader h; if (!cm_parse_frame(sf, h)) continue;
                auto hf = pb_parse(h.headerBytes); JobMsg m; m.emsg = h.emsg; m.body = std::move(h.bodyBytes);
                if (auto e = pb_get(hf, 13)) m.eresult = (int32_t)e->u; else m.eresult = 2;
                if (auto e = pb_get(hf, 14)) m.errmsg = e->s;
                uint64_t tgt = UINT64_MAX; if (auto t = pb_get(hf, 11)) tgt = t->u;
                if (m.emsg == EMSG_ClientLogOnResponse) {
                    if (auto s = pb_get(hf, 1)) steamid = s->u; if (auto s = pb_get(hf, 2)) sessionId = (int32_t)s->u;
                    { std::lock_guard<std::mutex> l(jm); jobs[0].push_back(std::move(m)); } jcv.notify_all(); continue;
                }
                if (tgt == UINT64_MAX) continue;
                { std::lock_guard<std::mutex> l(jm); auto it = jobs.find(tgt); if (it != jobs.end()) it->second.push_back(std::move(m)); }
                jcv.notify_all();
            }
        }
        up = false; jcv.notify_all(); logf("cm: reader exiting (connection closed or stopped)");
    }
    void heartbeat() {
        double last = now_s();
        while (!stop && up) {
            Sleep(500);
            if (now_s() - last >= hbSec) { last = now_s(); send_raw(EMSG_ClientHeartBeat, "", 0, ""); }
        }
    }
    void send_raw(uint32_t emsg, const std::string& body, uint64_t job, const std::string& jobname) {
        std::string h; pb_fixed64(h, 1, steamid); pb_u(h, 2, (uint64_t)(uint32_t)sessionId);
        pb_fixed64(h, 10, job ? job : UINT64_MAX); pb_fixed64(h, 11, UINT64_MAX); if (!jobname.empty()) pb_s(h, 12, jobname);
        std::string f = cm_build_frame(emsg, h, body);
        std::lock_guard<std::mutex> l(sendm); try { conn.send(f); } catch (...) { up = false; }
    }
    uint64_t begin_job() { uint64_t id = nextJob++; std::lock_guard<std::mutex> l(jm); jobs[id]; return id; }
    void end_job(uint64_t id) { std::lock_guard<std::mutex> l(jm); jobs.erase(id); }
    bool wait_job(uint64_t id, int timeoutMs, JobMsg& out) {
        std::unique_lock<std::mutex> l(jm);
        bool ok = jcv.wait_for(l, std::chrono::milliseconds(timeoutMs), [&] { return !jobs[id].empty() || (!up && id != 0); });
        auto& q = jobs[id]; if (!ok || q.empty()) return false; out = std::move(q.front()); q.pop_front(); return true;
    }
    // one request -> one response
    bool call(uint32_t emsg, const std::string& body, const std::string& jobname, int timeoutMs, JobMsg& out) {
        if (!up) return false;
        uint64_t id = begin_job(); send_raw(emsg, body, id, jobname); bool ok = wait_job(id, timeoutMs, out); end_job(id); return ok;
    }
    bool service(const std::string& method, const std::string& body, int timeoutMs, JobMsg& out) { return call(EMSG_ServiceMethodCall, body, method, timeoutMs, out); }
    void close() { stop = true; up = false; conn.close(); jcv.notify_all(); }
};

// Connect + log on (refresh empty => anonymous). Returns the session on eresult 1, else null with `eres` set (0 = no connection / no response).
static std::shared_ptr<CmSession> cm_logon(const std::string& refresh, int& eres, std::string& err) {
    eres = 0; err.clear();
    std::vector<CmServer> servers; if (!fetch_cm_list(servers)) { err = "GetCMListForConnect failed"; return nullptr; }
    uint64_t sid = STEAMID_ANON; bool anon = refresh.empty();
    if (!anon) { std::string s = jwt_sub(refresh); if (s.empty()) { err = "bad refresh token"; eres = 5; return nullptr; } sid = strtoull(s.c_str(), nullptr, 10); }
    for (size_t i = 0; i < servers.size() && i < 6; i++) {
        auto s = std::make_shared<CmSession>(); s->anon = anon; s->steamid = sid; s->host = servers[i].host;
        try { s->conn.start(servers[i].host); }
        catch (hresult_error const& e) { logf("cm: connect to %s failed 0x%08x", servers[i].host.c_str(), (unsigned)e.code().value); err = "connect failed"; continue; }
        catch (...) { logf("cm: connect to %s failed (exception)", servers[i].host.c_str()); err = "connect failed"; continue; }
        s->up = true; { auto self = s; std::thread([self] { init_apartment(apartment_type::multi_threaded); self->reader(); }).detach(); }
        std::string body;
        pb_u(body, 1, 65580); pb_u(body, 3, 0); if (!anon) pb_s(body, 6, "english"); pb_u(body, 7, 20); pb_u(body, 33, 2);
        if (anon) pb_s(body, 80, "anonymous");
        if (!anon) { pb_u(body, 8, 1); pb_u(body, 102, 1); pb_s(body, 108, refresh); }
        { std::string h; pb_fixed64(h, 1, sid); pb_u(h, 2, 0); pb_fixed64(h, 10, UINT64_MAX); pb_fixed64(h, 11, UINT64_MAX);
          std::lock_guard<std::mutex> l(s->sendm); try { s->conn.send(cm_build_frame(EMSG_ClientLogon, h, body)); } catch (...) { s->close(); continue; } }
        logf("cm: %s ClientLogon sent to %s", anon ? "anonymous" : "user", servers[i].host.c_str());
        JobMsg m; if (!s->wait_job(0, 12000, m)) { logf("cm: no logon response from %s", servers[i].host.c_str()); err = "no response"; s->close(); continue; }
        auto f = pb_parse(m.body); int er = 2; if (auto e = pb_get(f, 1)) er = (int)e->u; if (auto hb = pb_get(f, 3)) s->hbSec = std::max(5, (int)hb->u);
        logf("cm: ClientLogOnResponse eresult=%d heartbeat=%ds cm=%s", er, s->hbSec, servers[i].host.c_str());
        if (er == 1) { eres = 1; { auto self = s; std::thread([self] { init_apartment(apartment_type::multi_threaded); self->heartbeat(); }).detach(); } return s; }
        s->close(); eres = er;
        if (er == 15 || er == 5 || er == 84 || er == 63 || er == 65 || er == 18 || er == 50 || er == 43) { err = "logon refused"; return nullptr; }   // definitive: other CMs will say the same
    }
    return nullptr;
}

// ---------------------------------------------------------------- download state (shared with the UI)
struct DlState {
    std::atomic<int> state{0};      // 0 idle, 1 running, 2 done, 3 failed, 4 cancelled
    std::atomic<uint32_t> appid{0};
    std::atomic<uint64_t> total{0}, done{0};
    std::atomic<int> files{0}, filesDone{0};
    std::atomic<double> speed{0};
    std::atomic<bool> cancel{false};
    std::mutex m; std::string name, msg;
};
static DlState g_dl;
static void dl_msg(const std::string& s) { std::lock_guard<std::mutex> l(g_dl.m); g_dl.msg = s; }

// ---------------------------------------------------------------- downloader
struct ChunkInfo { std::string sha; uint32_t cbOrig = 0, cbComp = 0; uint64_t off = 0; };
struct DlFile { std::string path; std::wstring wpath; uint64_t size = 0; uint32_t flags = 0; std::string sha; std::string link; std::vector<ChunkInfo> chunks; std::atomic<int> remaining{0}; };

static std::string b64dec(const std::string& s) {
    std::string o; int v = 0, bits = 0;
    for (char c : s) { int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : (c == '+' || c == '-') ? 62 : (c == '/' || c == '_') ? 63 : -1; if (d < 0) continue; v = (v << 6) | d; bits += 6; if (bits >= 8) { bits -= 8; o += (char)((v >> bits) & 0xFF); } }
    return o;
}
static std::wstring widen(const std::string& s) { if (s.empty()) return L""; int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0); std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n); return w; }
static bool all_digits(const std::string& s) { if (s.empty()) return false; for (char c : s) if (c < '0' || c > '9') return false; return true; }

static bool http_bin(const std::string& url, std::string& out, int& status, std::string& err) {   // quiet GET for CDN traffic
    try {
        Http::HttpRequestMessage req(Http::HttpMethod::Get(), Windows::Foundation::Uri(to_hstring(url)));
        auto resp = client().SendRequestAsync(req, Http::HttpCompletionOption::ResponseHeadersRead).get();
        status = (int)resp.StatusCode(); if (status != 200) { err = "HTTP " + std::to_string(status); return false; }
        auto buf = resp.Content().ReadAsBufferAsync().get(); auto rd = Windows::Storage::Streams::DataReader::FromBuffer(buf);
        out.resize(buf.Length()); if (buf.Length()) rd.ReadBytes(array_view<uint8_t>((uint8_t*)&out[0], (uint8_t*)&out[0] + out.size()));
        return true;
    } catch (hresult_error const& e) { char b[64]; snprintf(b, sizeof b, "hresult 0x%08x", (unsigned)e.code().value); err = b; return false; }
    catch (...) { err = "exception"; return false; }
}

struct CdnServer { std::string host; int load = 0; };
static std::vector<CdnServer> fetch_cdn_servers() {
    std::vector<CdnServer> v;
    Resp r = http(false, "https://api.steampowered.com/IContentServerDirectoryService/GetServersForSteamPipe/v1/?cell_id=0&max_servers=20&format=json", "", "GetServersForSteamPipe");
    if (r.status != 200) return v;
    try {
        auto arr = Windows::Data::Json::JsonObject::Parse(to_hstring(r.body)).GetNamedObject(L"response").GetNamedArray(L"servers");
        for (auto const& it : arr) {
            auto o = it.GetObject(); std::string type = o.HasKey(L"type") ? to_string(o.GetNamedString(L"type")) : "";
            if (type != "CDN" && type != "SteamCache") continue;
            if (o.HasKey(L"allowed_app_ids")) continue;
            std::string https = o.HasKey(L"https_support") ? to_string(o.GetNamedString(L"https_support")) : "";
            if (https == "none") continue;
            CdnServer s; s.host = to_string(o.GetNamedString(L"host")); s.load = (int)o.GetNamedNumber(L"weighted_load", 0); v.push_back(s);
        }
    } catch (...) { logf("cdn: server list parse failed"); }
    std::stable_sort(v.begin(), v.end(), [](const CdnServer& a, const CdnServer& b) { return a.load < b.load; });
    logf("cdn: %zu usable servers (first: %s)", v.size(), v.empty() ? "-" : v[0].host.c_str());
    return v;
}

static bool parse_manifest(const std::string& raw, AesKey& key, std::vector<std::unique_ptr<DlFile>>& files, bool& encNames, uint64_t& totalBytes) {
    size_t i = 0; encNames = false; totalBytes = 0;
    while (i + 4 <= raw.size()) {
        uint32_t magic = get_u32le(raw, i); i += 4;
        if (magic == 0x32C415AB) break;
        if (i + 4 > raw.size()) return false; uint32_t len = get_u32le(raw, i); i += 4; if (i + len > raw.size()) return false;
        std::string sec = raw.substr(i, len); i += len;
        if (magic == 0x1F4812BE) { auto m = pb_parse(sec); if (auto e = pb_get(m, 4)) encNames = e->u != 0; }
        else if (magic == 0x71F617D0) {
            for (auto& mf : pb_parse(sec)) {
                if (mf.f != 1) continue;
                auto d = std::make_unique<DlFile>();
                for (auto& x : pb_parse(mf.s)) {
                    if (x.f == 1) d->path = x.s; else if (x.f == 2) d->size = x.u; else if (x.f == 3) d->flags = (uint32_t)x.u; else if (x.f == 5) d->sha = x.s; else if (x.f == 7) d->link = x.s;
                    else if (x.f == 6) { ChunkInfo c; for (auto& y : pb_parse(x.s)) { if (y.f == 1) c.sha = y.s; else if (y.f == 3) c.off = y.u; else if (y.f == 4) c.cbOrig = (uint32_t)y.u; else if (y.f == 5) c.cbComp = (uint32_t)y.u; } d->chunks.push_back(c); }
                }
                files.push_back(std::move(d));
            }
        }
    }
    if (encNames) for (auto& f : files) { std::string p; if (!key.decrypt(b64dec(f->path), p)) return false; size_t z = p.find('\0'); f->path = p.substr(0, z); }
    for (auto& f : files) totalBytes += f->size;
    return true;
}

static std::wstring g_dlRoot() { return g_dir + L"\\"; }
static bool mkdirs(const std::wstring& dir) {
    if (dir.size() <= g_dir.size()) return true;
    if (GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    size_t p = dir.find_last_of(L'\\'); if (p != std::wstring::npos && p > g_dir.size()) mkdirs(dir.substr(0, p));
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}
static bool read_at(HANDLE h, uint64_t off, void* buf, DWORD n) { LARGE_INTEGER li; li.QuadPart = (LONGLONG)off; if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) return false; DWORD got = 0; return ReadFile(h, buf, n, &got, nullptr) && got == n; }
static bool write_at(HANDLE h, uint64_t off, const void* buf, DWORD n) { LARGE_INTEGER li; li.QuadPart = (LONGLONG)off; if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) return false; DWORD got = 0; return WriteFile(h, buf, n, &got, nullptr) && got == n; }

struct DlCtx {
    uint32_t appid = 0, depot = 0; std::string depotKey; std::vector<CdnServer> servers;
    std::vector<std::unique_ptr<DlFile>>* files = nullptr;
    std::vector<std::vector<char>> doneChunk; std::vector<std::pair<int, int>> tasks; std::atomic<size_t> next{0};
    std::atomic<int> fatal{0}; std::mutex em; std::string firstErr;
};
static void dl_worker(DlCtx* cx, int wid) {
    init_apartment(apartment_type::multi_threaded);
    AesKey key; if (!key.init(cx->depotKey)) { cx->fatal = 1; std::lock_guard<std::mutex> l(cx->em); cx->firstErr = "AES init failed"; return; }
    HANDLE h = INVALID_HANDLE_VALUE; int hf = -1;
    for (;;) {
        if (cx->fatal || g_dl.cancel) break;
        size_t t = cx->next++; if (t >= cx->tasks.size()) break;
        int fi = cx->tasks[t].first, ci = cx->tasks[t].second; DlFile& f = *(*cx->files)[fi]; ChunkInfo& c = f.chunks[ci];
        std::string plain, err, hexsha = hex(c.sha); bool ok = false;
        for (int attempt = 0; attempt < 6 && !ok && !g_dl.cancel; attempt++) {
            const CdnServer& s = cx->servers[(wid + attempt) % cx->servers.size()];
            std::string enc, e; int st = 0;
            if (!http_bin("https://" + s.host + "/depot/" + std::to_string(cx->depot) + "/chunk/" + hexsha, enc, st, e)) { err = s.host + ": " + e; if (attempt) Sleep(300 * attempt); continue; }
            std::string dec; if (!key.decrypt(enc, dec)) { err = "decrypt failed"; continue; }
            if (!chunk_decompress(dec, c.cbOrig, plain)) { err = "decompress failed"; continue; }
            if (sha1_of(plain.data(), plain.size()) != c.sha) { err = "chunk SHA1 mismatch"; continue; }
            ok = true;
        }
        if (!ok) { if (!g_dl.cancel) { logf("dl: chunk FAILED file=%s chunk=%d: %s", f.path.c_str(), ci, err.c_str()); std::lock_guard<std::mutex> l(cx->em); if (cx->firstErr.empty()) cx->firstErr = "chunk failed: " + err; cx->fatal = 1; } break; }
        if (hf != fi) { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); h = CreateFile2(f.wpath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, nullptr); hf = fi; }
        if (h == INVALID_HANDLE_VALUE || !write_at(h, c.off, plain.data(), (DWORD)plain.size())) { logf("dl: write FAILED %s (err %lu)", f.path.c_str(), GetLastError()); std::lock_guard<std::mutex> l(cx->em); if (cx->firstErr.empty()) cx->firstErr = "disk write failed (disk full?)"; cx->fatal = 1; break; }
        g_dl.done += c.cbOrig;
        if (--f.remaining == 0) { g_dl.filesDone++; logf("dl: file done %d/%d %s (%llu bytes, %zu chunks)", (int)g_dl.filesDone, (int)g_dl.files, f.path.c_str(), (unsigned long long)f.size, f.chunks.size()); }
    }
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

static std::string tolower_s(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

// Downloads one app (all Windows depots of the public branch) into LocalState\<installdir>. Returns true when every file is hash-verified.
static bool download_app(CmSession& cm, uint32_t appid) {
    g_dl.cancel = false; g_dl.appid = appid; g_dl.done = 0; g_dl.total = 0; g_dl.files = 0; g_dl.filesDone = 0; g_dl.speed = 0; g_dl.state = 1; { std::lock_guard<std::mutex> l(g_dl.m); g_dl.name.clear(); g_dl.msg = "Contacting Steam..."; }
    double t0 = now_s();
    auto fail = [&](const std::string& m) { logf("dl: FAILED appid %u: %s", appid, m.c_str()); dl_msg(m); g_dl.state = g_dl.cancel ? 4 : 3; return false; };
    logf("dl: ===== start appid %u (session: %s) =====", appid, cm.anon ? "anonymous" : "user");
    // 1. PICS access token + product info
    uint64_t token = 0; JobMsg m;
    { std::string b; pb_u(b, 2, appid); if (cm.call(EMSG_PICSAccessToken, b, "", 15000, m) && m.emsg == EMSG_PICSAccessTokenResp) { for (auto& f : pb_parse(m.body)) if (f.f == 3) for (auto& x : pb_parse(f.s)) if (x.f == 2) token = x.u; } }
    logf("dl: PICS access token %s", token ? "obtained" : "none (public app)");
    std::string appBuf;
    { std::string req; { std::string a; pb_u(a, 1, appid); if (token) pb_u(a, 2, token); pb_s(req, 2, a); }
      uint64_t id = cm.begin_job(); cm.send_raw(EMSG_PICSProductInfo, req, id, "");
      for (int guard = 0; guard < 20; guard++) {
          if (!cm.wait_job(id, 20000, m)) break; bool pending = false;
          for (auto& f : pb_parse(m.body)) { if (f.f == 1) { uint32_t aid = 0; std::string buf; for (auto& x : pb_parse(f.s)) { if (x.f == 1) aid = (uint32_t)x.u; else if (x.f == 5) buf = x.s; } if (aid == appid) appBuf = buf; } else if (f.f == 6) pending = f.u != 0; }
          if (!pending) break;
      }
      cm.end_job(id); }
    if (appBuf.empty()) return fail("Steam has no product info for this app (not owned?)");
    Vdf root; while (!appBuf.empty() && appBuf.back() == 0) appBuf.pop_back();
    if (!vdf_parse(appBuf, root)) return fail("could not parse product info");
    const Vdf* ai = root.get("appinfo"); if (!ai) ai = &root;
    std::string name = ai->str({"common", "name"}, "App " + std::to_string(appid)), installdir = ai->str({"config", "installdir"}, "app" + std::to_string(appid)), build = ai->str({"depots", "branches", "public", "buildid"}, "?");
    if (appid == 3405340) installdir = "megabonk";
    for (auto& c : installdir) if (!(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) c = '_';
    installdir = tolower_s(installdir);
    { std::lock_guard<std::mutex> l(g_dl.m); g_dl.name = name; }
    logf("dl: app '%s' installdir=%s buildid=%s", name.c_str(), installdir.c_str(), build.c_str());
    struct Dep { uint32_t id; std::string gid; };
    std::vector<Dep> deps; const Vdf* dv = ai->get("depots");
    if (dv) for (size_t i = 0; i < dv->keys.size(); i++) {
        if (!all_digits(dv->keys[i])) continue; const Vdf& d = dv->kids[i]; if (!d.obj) continue;
        uint32_t id = (uint32_t)strtoul(dv->keys[i].c_str(), nullptr, 10);
        std::string os = d.str({"config", "oslist"}); if (!os.empty() && os.find("windows") == std::string::npos) { logf("dl: depot %u skipped (os=%s)", id, os.c_str()); continue; }
        if (d.get("dlcappid")) { logf("dl: depot %u skipped (DLC depot)", id); continue; }
        std::string gid; const Vdf* pm = d.at({"manifests", "public"}); if (pm) gid = pm->obj ? pm->str({"gid"}) : pm->val;
        if (gid.empty()) { logf("dl: depot %u skipped (no public manifest)", id); continue; }
        deps.push_back({id, gid});
    }
    if (deps.empty()) return fail("no downloadable Windows depot found");
    auto servers = fetch_cdn_servers(); if (servers.empty()) return fail("no Steam content servers available");
    // 2. per depot: key, manifest
    struct Prep { uint32_t depot; std::string key; std::vector<std::unique_ptr<DlFile>> files; uint64_t bytes = 0; };
    std::vector<Prep> preps;
    for (auto& d : deps) {
        if (g_dl.cancel) return fail("cancelled");
        dl_msg("Getting depot " + std::to_string(d.id) + " key...");
        std::string b; pb_u(b, 1, d.id); pb_u(b, 2, appid); JobMsg km;
        if (!cm.call(EMSG_GetDepotKey, b, "", 15000, km)) return fail("depot key request timed out");
        auto kf = pb_parse(km.body); int ker = pb_get(kf, 1) ? (int)pb_get(kf, 1)->u : 2; auto kk = pb_get(kf, 3);
        if (ker != 1 || !kk || kk->s.size() != 32) return fail(std::string(cm.anon ? "not available without signing in" : "Steam refused the depot key") + " (eresult " + std::to_string(ker) + ")");
        logf("dl: depot %u key OK (32 bytes, not shown)", d.id);
        std::string rb; pb_u(rb, 1, appid); pb_u(rb, 2, d.id); pb_u(rb, 3, strtoull(d.gid.c_str(), nullptr, 10)); pb_s(rb, 4, "public"); JobMsg rm;
        if (!cm.service("ContentServerDirectory.GetManifestRequestCode#1", rb, 15000, rm) || rm.eresult != 1) return fail("manifest request code refused (eresult " + std::to_string(rm.eresult) + ")");
        uint64_t code = 0; for (auto& f : pb_parse(rm.body)) if (f.f == 1) code = f.u;
        std::string zip, e; int st = 0; bool got = false;
        for (size_t si = 0; si < servers.size() && si < 4 && !got; si++) {
            std::string url = "https://" + servers[si].host + "/depot/" + std::to_string(d.id) + "/manifest/" + d.gid + "/5/" + std::to_string(code);
            got = http_bin(url, zip, st, e); if (!got) logf("dl: manifest fetch from %s failed: %s", servers[si].host.c_str(), e.c_str());
        }
        if (!got) return fail("manifest download failed: " + e);
        std::string raw; if (!unzip_first(zip, raw)) return fail("manifest unzip failed");
        Prep p; p.depot = d.id; p.key = kk->s; AesKey ak; if (!ak.init(p.key)) return fail("AES init failed"); bool enc;
        if (!parse_manifest(raw, ak, p.files, enc, p.bytes)) return fail("manifest parse failed");
        logf("dl: depot %u manifest %s: %zu entries, %llu bytes%s", d.id, d.gid.c_str(), p.files.size(), (unsigned long long)p.bytes, enc ? " (names were encrypted)" : "");
        preps.push_back(std::move(p));
    }
    uint64_t total = 0; int nfiles = 0; for (auto& p : preps) { total += p.bytes; for (auto& f : p.files) if (!(f->flags & 64) && f->link.empty()) nfiles++; }
    g_dl.total = total; g_dl.files = nfiles;
    // 3. free space
    std::wstring destRoot = g_dir + L"\\" + widen(installdir);
    { ULARGE_INTEGER fb{}, tb{}, tf{}; if (GetDiskFreeSpaceExW(g_dir.c_str(), &fb, &tb, &tf)) {
        logf("dl: need %.1f MB, free space %.1f MB", total / 1048576.0, fb.QuadPart / 1048576.0);
        { // test guard: LocalState\dlmax.txt = size cap in MB (0 = dry run: report size only); one-shot, consumed when read
          std::wstring gp = g_dir + L"\\dlmax.txt"; FILE* gf = _wfopen(gp.c_str(), L"rb");
          if (gf) { char gb[32] = {}; fread(gb, 1, sizeof gb - 1, gf); fclose(gf); DeleteFileW(gp.c_str()); double cap = atof(gb);
            logf("dl: dlmax.txt (test guard, consumed) cap=%.0f MB need=%.1f MB", cap, total / 1048576.0);
            if (total / 1048576.0 > cap || cap <= 0) { char b[160]; snprintf(b, sizeof b, "Test guard: %.1f MB exceeds cap %.0f MB (dlmax.txt) - nothing downloaded", total / 1048576.0, cap); return fail(b); } } }
        if (fb.QuadPart < total + (total / 20) + (64ull << 20)) { char b[160]; snprintf(b, sizeof b, "Not enough free space: need %.0f MB, have %.0f MB", total / 1048576.0, fb.QuadPart / 1048576.0); return fail(b); } } }
    if (!mkdirs(destRoot)) return fail("cannot create install folder");
    // 4. per depot: lay out files, resume-check, download
    for (auto& p : preps) {
        DlCtx cx; cx.appid = appid; cx.depot = p.depot; cx.depotKey = p.key; cx.servers = servers; cx.files = &p.files;
        uint64_t alreadyOk = 0; size_t nresumed = 0;
        for (size_t fi = 0; fi < p.files.size(); fi++) {
            DlFile& f = *p.files[fi];
            std::string rel = f.path; for (auto& c : rel) if (c == '\\') c = '/';
            while (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
            if (rel.find("..") != std::string::npos) { logf("dl: skipping suspicious path"); f.size = 0; f.flags |= 64; f.path.clear(); cx.doneChunk.emplace_back(); continue; }
            std::wstring w = widen(rel); for (auto& c : w) if (c == L'/') c = L'\\';
            f.wpath = destRoot + L"\\" + w; cx.doneChunk.emplace_back(f.chunks.size(), 0);
            if (f.flags & 64) { mkdirs(f.wpath); continue; }
            if (!f.link.empty()) { logf("dl: symlink entry skipped: %s", f.path.c_str()); continue; }
            size_t sl = f.wpath.find_last_of(L'\\'); if (sl != std::wstring::npos) mkdirs(f.wpath.substr(0, sl));
            bool existed = file_exists(f.wpath);
            HANDLE h = CreateFile2(f.wpath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_ALWAYS, nullptr);
            if (h == INVALID_HANDLE_VALUE) return fail("cannot create file " + f.path);
            LARGE_INTEGER sz{}; GetFileSizeEx(h, &sz);
            if ((uint64_t)sz.QuadPart != f.size) { LARGE_INTEGER li; li.QuadPart = (LONGLONG)f.size; SetFilePointerEx(h, li, nullptr, FILE_BEGIN); SetEndOfFile(h); }
            if (f.size && existed) {   // resume: verify what is already on disk chunk by chunk
                std::string buf; size_t okc = 0;
                for (size_t ci = 0; ci < f.chunks.size() && !g_dl.cancel; ci++) {
                    buf.resize(f.chunks[ci].cbOrig); if (!read_at(h, f.chunks[ci].off, &buf[0], f.chunks[ci].cbOrig)) continue;
                    if (sha1_of(buf.data(), buf.size()) == f.chunks[ci].sha) { cx.doneChunk[fi][ci] = 1; okc++; alreadyOk += f.chunks[ci].cbOrig; }
                }
                if (okc) { nresumed++; logf("dl: resume %s: %zu/%zu chunks already valid", f.path.c_str(), okc, f.chunks.size()); }
            }
            CloseHandle(h);
            int rem = 0; for (size_t ci = 0; ci < f.chunks.size(); ci++) if (!cx.doneChunk[fi][ci]) { cx.tasks.push_back({(int)fi, (int)ci}); rem++; }
            f.remaining = rem; if (rem == 0 && f.size) { g_dl.filesDone++; }
        }
        g_dl.done += alreadyOk;
        if (g_dl.cancel) return fail("cancelled");
        logf("dl: depot %u: %zu chunks to fetch (%llu bytes already valid on disk, %zu files resumed)", p.depot, cx.tasks.size(), (unsigned long long)alreadyOk, nresumed);
        std::vector<std::thread> th; int nw = (int)std::min<size_t>(8, std::max<size_t>(1, cx.tasks.size()));
        for (int i = 0; i < nw; i++) th.emplace_back(dl_worker, &cx, i);
        double lastLog = now_s(), lastT = now_s(); uint64_t lastB = g_dl.done;
        for (;;) {
            Sleep(250); bool alive = cx.next < cx.tasks.size() + nw; (void)alive;
            double t = now_s();
            if (t - lastT >= 1.0) { double sp = (double)(g_dl.done - lastB) / (t - lastT); g_dl.speed = g_dl.speed * 0.6 + sp * 0.4; lastB = g_dl.done; lastT = t; }
            if (t - lastLog >= 5.0) { lastLog = t; logf("dl: progress %.1f/%.1f MB (%.0f%%) files %d/%d speed %.1f MB/s", g_dl.done / 1048576.0, g_dl.total / 1048576.0, g_dl.total ? 100.0 * g_dl.done / g_dl.total : 100.0, (int)g_dl.filesDone, (int)g_dl.files, g_dl.speed / 1048576.0); }
            if (cx.fatal || g_dl.cancel || cx.next >= cx.tasks.size()) break;
        }
        for (auto& x : th) x.join();
        if (g_dl.cancel) return fail("cancelled");
        if (cx.fatal) return fail(cx.firstErr.empty() ? "download failed" : cx.firstErr);
    }
    // 5. verify every file against the manifest SHA1
    dl_msg("Verifying files...");
    int verified = 0, bad = 0; uint64_t vbytes = 0; std::string buf; buf.resize(4 << 20);
    for (auto& p : preps) for (auto& fp : p.files) {
        DlFile& f = *fp; if ((f.flags & 64) || !f.link.empty() || f.path.empty()) continue;
        if (f.size == 0) { verified++; continue; }
        HANDLE h = CreateFile2(f.wpath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, nullptr);
        bool ok = false;
        if (h != INVALID_HANDLE_VALUE) { Sha1 s; uint64_t left = f.size; ok = true; while (left) { DWORD n = (DWORD)std::min<uint64_t>(left, buf.size()), got = 0; if (!ReadFile(h, &buf[0], n, &got, nullptr) || got != n) { ok = false; break; } s.upd(buf.data(), n); left -= n; } if (ok) ok = s.fin() == f.sha; CloseHandle(h); }
        if (ok) { verified++; vbytes += f.size; } else { bad++; logf("dl: VERIFY FAILED %s", f.path.c_str()); }
        if (g_dl.cancel) return fail("cancelled");
    }
    logf("dl: VERIFY files_ok=%d files_bad=%d bytes_ok=%llu", verified, bad, (unsigned long long)vbytes);
    if (bad) return fail(std::to_string(bad) + " files failed verification");
    { std::wstring mk = g_dir + L"\\dl_" + widen(std::to_string(appid)) + L".done"; FILE* mf = _wfopen(mk.c_str(), L"wb"); if (mf) { fprintf(mf, "app %u build %s files %d bytes %llu installdir %s\n", appid, build.c_str(), verified, (unsigned long long)vbytes, installdir.c_str()); fclose(mf); } }
    char sm[200]; snprintf(sm, sizeof sm, "Done: %d files, %.1f MB verified", verified, vbytes / 1048576.0); dl_msg(sm);
    logf("dl: ===== DONE appid %u '%s' -> LocalState\\%s : %d files, %llu bytes, SHA1-verified, %.0f s =====", appid, name.c_str(), installdir.c_str(), verified, (unsigned long long)vbytes, now_s() - t0);
    g_dl.state = 2; return true;
}
