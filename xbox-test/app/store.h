// Store info for the detail screen: Steam public store API over the same http() path as the covers, cached in LocalState\storecache\<appid>.json.
#pragma once
struct StoreInfo {
    uint32_t appid = 0;
    std::atomic<int> state{0};      // 0 loading, 1 ok (fields below immutable), 2 unavailable
    std::string name, desc, release, dev, genres, price, why; bool free = false;
    std::atomic<int> img{0};        // 0 loading, 1 ok, 2 failed
    Game hdr;                       // header image pixels (iw, ih, pix)
};
// The bitmap font is ASCII only: strip tags, decode entities, map common Unicode to ASCII, '?' otherwise.
static void utf8_put(std::string& o, unsigned cp) {
    if (cp < 0x80) o += (char)cp; else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 63)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
}
static std::string ascii_clean(const std::string& in) {
    std::string u;   // stage 1: tags + entities -> UTF-8
    for (size_t i = 0; i < in.size(); i++) {
        char c = in[i];
        if (c == '<') { size_t e = in.find('>', i); if (e != std::string::npos && e - i < 200) {
                std::string tg = in.substr(i + 1, std::min<size_t>(e - i - 1, 6)); for (auto& ch : tg) ch = (char)tolower((unsigned char)ch);
                if (tg.rfind("br", 0) == 0 || tg.rfind("/p", 0) == 0 || tg.rfind("/li", 0) == 0 || tg.rfind("/h", 0) == 0 || tg.rfind("/ul", 0) == 0) u += '\n';
                else if (tg.rfind("li", 0) == 0) u += "* ";
                i = e; continue; } }
        if (c == '&') { size_t e = in.find(';', i); if (e != std::string::npos && e - i <= 10) {
                std::string en = in.substr(i + 1, e - i - 1); unsigned cp = 0; bool ok = true;
                if (en == "amp") cp = '&'; else if (en == "quot") cp = '"'; else if (en == "lt") cp = '<'; else if (en == "gt") cp = '>'; else if (en == "apos") cp = '\''; else if (en == "nbsp") cp = ' ';
                else if (en.size() > 1 && en[0] == '#') cp = en[1] == 'x' || en[1] == 'X' ? (unsigned)strtoul(en.c_str() + 2, nullptr, 16) : (unsigned)strtoul(en.c_str() + 1, nullptr, 10);
                else ok = false;
                if (ok && cp) { utf8_put(u, cp); i = e; continue; } } }
        u += c;
    }
    std::string o;   // stage 2: UTF-8 -> ASCII
    static const char* L1 = "AAAAAAACEEEEIIIIDNOOOOO*OUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    for (size_t i = 0; i < u.size();) {
        unsigned char c = (unsigned char)u[i]; unsigned cp; int n;
        if (c < 0x80) { cp = c; n = 1; } else if ((c >> 5) == 6) { cp = c & 31; n = 2; } else if ((c >> 4) == 14) { cp = c & 15; n = 3; } else if ((c >> 3) == 30) { cp = c & 7; n = 4; } else { i++; o += '?'; continue; }
        if (i + n > u.size()) { o += '?'; break; }
        for (int k = 1; k < n; k++) cp = (cp << 6) | ((unsigned char)u[i + k] & 63);
        i += n;
        if (cp < 0x80) { if (cp == '\r') continue; if (cp == '\t') cp = ' '; if (cp >= 32 || cp == '\n') o += (char)cp; }
        else if (cp == 0x2018 || cp == 0x2019 || cp == 0x2032) o += '\'';
        else if (cp == 0x201C || cp == 0x201D || cp == 0x2033) o += '"';
        else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212) o += '-';
        else if (cp == 0x2026) o += "...";
        else if (cp == 0x20AC) o += "EUR "; else if (cp == 0xA3) o += "GBP "; else if (cp == 0xA5) o += "JPY ";
        else if (cp == 0xA0 || cp == 0x2009 || cp == 0x200A || cp == 0x202F) o += ' ';
        else if (cp == 0x200B || cp == 0xFE0F) {}
        else if (cp == 0x2122) o += "(TM)"; else if (cp == 0xAE) o += "(R)"; else if (cp == 0xA9) o += "(C)";
        else if (cp == 0x2022 || cp == 0xB7 || cp == 0x25CF) o += '*';
        else if (cp >= 0xC0 && cp <= 0xFF) o += L1[cp - 0xC0];
        else o += '?';
    }
    std::string r; int nl = 0; bool sp = true;   // collapse whitespace
    for (char c : o) { if (c == '\n') { sp = true; if (++nl <= 2 && !r.empty()) r += '\n'; } else if (c == ' ') { if (!sp) r += ' '; sp = true; } else { r += c; sp = false; nl = 0; } }
    while (!r.empty() && (r.back() == '\n' || r.back() == ' ')) r.pop_back();
    return r;
}
static std::vector<std::string> wrap_lines(const std::string& s, size_t cols) {
    std::vector<std::string> out; size_t pos = 0;
    while (pos <= s.size()) {
        size_t e = s.find('\n', pos); if (e == std::string::npos) e = s.size();
        std::string para = s.substr(pos, e - pos); pos = e + 1;
        if (para.empty()) { out.push_back(""); continue; }
        while (para.size() > cols) {
            size_t cut = para.rfind(' ', cols); if (cut == std::string::npos || cut < cols / 3) cut = cols;
            out.push_back(para.substr(0, cut)); size_t st = cut; while (st < para.size() && para[st] == ' ') st++; para = para.substr(st);
        }
        out.push_back(para);
    }
    return out;
}
static bool read_file_s(const std::wstring& p, std::string& out) {
    FILE* f = _wfopen(p.c_str(), L"rb"); if (!f) return false; char b[16384]; size_t n; out.clear(); while ((n = fread(b, 1, sizeof b, f)) > 0) out.append(b, n); fclose(f); return true;
}
static bool store_parse(const std::string& body, uint32_t appid, StoreInfo& si, std::string& why) {
    try {
        auto root = Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(body));
        auto key = winrt::to_hstring(std::to_string(appid));
        if (!root.HasKey(key)) { why = "no entry for appid"; return false; }
        auto e = root.GetNamedObject(key);
        if (!e.GetNamedBoolean(L"success", false) || !e.HasKey(L"data") || e.GetNamedValue(L"data").ValueType() != Windows::Data::Json::JsonValueType::Object) { why = "app not in the Steam store (or region-locked)"; return false; }
        auto d = e.GetNamedObject(L"data");
        auto S = [&](const wchar_t* k) { return d.HasKey(k) && d.GetNamedValue(k).ValueType() == Windows::Data::Json::JsonValueType::String ? ascii_clean(winrt::to_string(d.GetNamedString(k))) : std::string(); };
        si.name = S(L"name"); si.desc = S(L"short_description");
        if (si.desc.empty()) si.desc = S(L"about_the_game");
        if (si.desc.size() > 1500) si.desc = si.desc.substr(0, 1497) + "...";
        si.free = d.HasKey(L"is_free") && d.GetNamedValue(L"is_free").ValueType() == Windows::Data::Json::JsonValueType::Boolean && d.GetNamedBoolean(L"is_free");
        if (d.HasKey(L"release_date") && d.GetNamedValue(L"release_date").ValueType() == Windows::Data::Json::JsonValueType::Object) {
            auto r = d.GetNamedObject(L"release_date"); si.release = ascii_clean(winrt::to_string(r.GetNamedString(L"date", L"")));
            if (r.GetNamedBoolean(L"coming_soon", false) && si.release.empty()) si.release = "Coming soon"; }
        if (d.HasKey(L"developers") && d.GetNamedValue(L"developers").ValueType() == Windows::Data::Json::JsonValueType::Array) {
            for (auto v : d.GetNamedArray(L"developers")) { if (v.ValueType() != Windows::Data::Json::JsonValueType::String) continue; if (!si.dev.empty()) si.dev += ", "; si.dev += ascii_clean(winrt::to_string(v.GetString())); } }
        if (d.HasKey(L"genres") && d.GetNamedValue(L"genres").ValueType() == Windows::Data::Json::JsonValueType::Array) {
            for (auto v : d.GetNamedArray(L"genres")) { if (v.ValueType() != Windows::Data::Json::JsonValueType::Object) continue; if (!si.genres.empty()) si.genres += ", "; si.genres += ascii_clean(winrt::to_string(v.GetObject().GetNamedString(L"description", L""))); } }
        if (d.HasKey(L"price_overview") && d.GetNamedValue(L"price_overview").ValueType() == Windows::Data::Json::JsonValueType::Object) {
            auto p = d.GetNamedObject(L"price_overview"); si.price = ascii_clean(winrt::to_string(p.GetNamedString(L"final_formatted", L"")));
            if (p.GetNamedNumber(L"discount_percent", 0) > 0) si.price += "  (-" + std::to_string((int)p.GetNamedNumber(L"discount_percent", 0)) + "%)"; }
        if (si.price.empty()) si.price = si.free ? "Free to play" : "Price not listed";
        return true;
    } catch (winrt::hresult_error const& ex) { char b[64]; snprintf(b, sizeof b, "parse error hr=0x%08x", (unsigned)ex.code().value); why = b; return false; }
    catch (...) { why = "parse error"; return false; }
}
static void store_worker(std::shared_ptr<StoreInfo> si) {
    init_apartment(apartment_type::multi_threaded);
    uint32_t id = si->appid; std::wstring dir = g_dir + L"\\storecache"; mkdirs(dir);
    std::wstring cp = dir + L"\\" + widen(std::to_string(id)) + L".json"; std::string body, why; bool ok = false, fromCache = false;
    WIN32_FILE_ATTRIBUTE_DATA fa{}; double ageH = 1e9;
    if (GetFileAttributesExW(cp.c_str(), GetFileExInfoStandard, &fa)) { FILETIME nowft; GetSystemTimeAsFileTime(&nowft); ULARGE_INTEGER a, b2; a.LowPart = nowft.dwLowDateTime; a.HighPart = nowft.dwHighDateTime; b2.LowPart = fa.ftLastWriteTime.dwLowDateTime; b2.HighPart = fa.ftLastWriteTime.dwHighDateTime; ageH = (double)(a.QuadPart - b2.QuadPart) / 1e7 / 3600.0; }
    std::string cached; bool haveCache = read_file_s(cp, cached) && cached.size() > 10;
    if (haveCache && ageH < 24) { StoreInfo tmp; if (store_parse(cached, id, tmp, why)) { body = cached; fromCache = true; } }
    if (body.empty()) {
        Resp r = http(false, "https://store.steampowered.com/api/appdetails?appids=" + std::to_string(id) + "&l=english", "", "store");
        if (r.status == 200 && r.body.size() > 10) { StoreInfo tmp; if (store_parse(r.body, id, tmp, why)) { body = r.body; FILE* f = _wfopen(cp.c_str(), L"wb"); if (f) { fwrite(body.data(), 1, body.size(), f); fclose(f); } } }
        else why = "HTTP " + std::to_string(r.status) + (r.err.empty() ? "" : " " + r.err);
        if (body.empty() && haveCache) { StoreInfo tmp; if (store_parse(cached, id, tmp, why)) { body = cached; fromCache = true; logf("store: appid %u network failed, using stale cache", id); } }
    }
    if (!body.empty()) {
        StoreInfo tmp; ok = store_parse(body, id, tmp, why);
        if (ok) { si->name = tmp.name; si->desc = tmp.desc; si->release = tmp.release; si->dev = tmp.dev; si->genres = tmp.genres; si->price = tmp.price; si->free = tmp.free; }
    }
    si->why = why; si->state = ok ? 1 : 2;
    logf("store: appid %u %s (%s) name='%s' desc=%zu chars genres='%s' release='%s' dev='%s' price='%s'%s", id, ok ? "info ready" : "info UNAVAILABLE", ok ? (fromCache ? "cache" : "network") : why.c_str(), si->name.c_str(), si->desc.size(), si->genres.c_str(), si->release.c_str(), si->dev.c_str(), si->price.c_str(), "");
    // header image (same CDN path as the covers)
    const char* kinds[] = {"capsule_616x353.jpg", "header.jpg"}; bool iok = false;
    for (int k = 0; k < 2 && !iok; k++) {
        Resp r = http(false, "https://cdn.cloudflare.steamstatic.com/steam/apps/" + std::to_string(id) + "/" + kinds[k], "", "storeimg");
        if (r.status != 200 || r.body.size() < 100) continue;
        int hr = 0; if (wic_decode(r.body, si->hdr, hr, 616, 353)) iok = true; else logf("store: header image WIC decode failed hr=0x%08x", (unsigned)hr);
    }
    si->img = iok ? 1 : 2; logf("store: appid %u header image %s (%dx%d)", id, iok ? "ready" : "unavailable", si->hdr.iw, si->hdr.ih);
}
