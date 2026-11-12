// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression for the Xbox gamertag parsing: the JWT claims the console's
// XSTS token carries, the profile response xodus reads, and the validation that
// keeps a hostile or truncated payload from becoming the name the library
// shows. Names here are fake.
#include "../xbox_gamertag.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace xbox_gamertag;

static std::string b64url(const std::string& raw) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    unsigned acc = 0;
    int bits = 0;
    for (unsigned char c : raw) {
        acc = (acc << 8) | c;
        bits += 8;
        while (bits >= 6) { bits -= 6; out += alphabet[(acc >> bits) & 0x3F]; }
    }
    if (bits) out += alphabet[(acc << (6 - bits)) & 0x3F];
    return out;
}

static std::string jwt(const std::string& payload) {
    return b64url(R"({"alg":"RS256","typ":"JWT"})") + "." + b64url(payload) + ".signature";
}

int main() {
    // ---- the JSON reader
    Json doc;
    assert(parse_json(R"({"a":1,"b":"two","c":[true,null,{"d":"e"}],"f":-1.5e3})", doc));
    assert(doc.member("a") && doc.member("a")->type == Json::Number && doc.member("a")->text == "1");
    assert(doc.member("b") && *doc.member("b")->as_string() == "two");
    assert(doc.member("c") && doc.member("c")->at(2) && doc.member("c")->at(2)->member("d"));
    assert(doc.member("f") && doc.member("f")->text == "-1.5e3");
    assert(!doc.member("missing"));
    assert(!parse_json("", doc));
    assert(!parse_json("{", doc));
    assert(!parse_json(R"({"a":1} trailing)", doc));
    assert(!parse_json(R"({"a":})", doc));
    // escaped strings, including a surrogate pair, decode to UTF-8
    assert(parse_json(R"({"s":"a\"b\\cé€😀"})", doc));
    assert(*doc.member("s")->as_string() == "a\"b\\c\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
    // depth is bounded
    std::string deep(64, '[');
    deep += std::string(64, ']');
    assert(!parse_json(deep, doc));

    // ---- base64url
    std::string raw;
    assert(decode_base64url("aGVsbG8", raw) && raw == "hello");
    assert(decode_base64url("aGVsbG8=", raw) && raw == "hello");
    assert(decode_base64url("-_8", raw) && raw.size() == 2);
    assert(!decode_base64url("a b", raw));

    // ---- JWT claims
    const std::string token = jwt(R"({"gtg":"Example Gamer","xid":"123","xui":[{"uhs":"0123456789"}]})");
    assert(jwt_string_claim(token, "gtg") == "Example Gamer");
    assert(jwt_user_hash(token) == "0123456789");
    assert(jwt_string_claim(token, "missing").empty());
    assert(jwt_string_claim("not-a-jwt", "gtg").empty());
    assert(jwt_string_claim("a.b", "gtg").empty());          // no payload segment
    assert(jwt_user_hash(jwt(R"({"xui":[]})")).empty());
    assert(jwt_user_hash(jwt(R"({"xui":"nope"})")).empty());

    // ---- gamertag cleaning
    assert(clean_gamertag("  Example Gamer \r\n") == "Example Gamer");
    assert(clean_gamertag("   ").empty());
    assert(clean_gamertag("").empty());
    assert(clean_gamertag(std::string(257, 'a')).empty());
    assert(clean_gamertag(std::string(256, 'a')).size() == 256);
    assert(clean_gamertag("bad\x01name").empty());
    assert(clean_gamertag("jobucaldas") == "jobucaldas");

    // ---- the profile response xodus reads
    std::string tag, pic;
    assert(parse_profile(R"({"profileUsers":[{"id":"x","settings":[
        {"id":"Gamertag","value":"Example Gamer"},
        {"id":"PublicGamerpic","value":"https://gamerpic.xboxlive.com/a.png"}]}]})", tag, pic));
    assert(tag == "Example Gamer" && pic == "https://gamerpic.xboxlive.com/a.png");
    // no user, two users, no settings, no gamertag, blank gamertag: all rejected
    assert(!parse_profile(R"({"profileUsers":[]})", tag, pic));
    assert(!parse_profile(R"({"profileUsers":[{},{}]})", tag, pic));
    assert(!parse_profile(R"({"profileUsers":[{"settings":"x"}]})", tag, pic));
    assert(!parse_profile(R"({"profileUsers":[{"settings":[{"id":"PublicGamerpic","value":"u"}]}]})", tag, pic));
    assert(!parse_profile(R"({"profileUsers":[{"settings":[{"id":"Gamertag","value":"  "}]}]})", tag, pic));
    assert(!parse_profile("not json", tag, pic));
    // a rejected parse leaves the outputs empty, never half-filled
    assert(tag.empty() && pic.empty());

    // ---- picture URL allowlist
    assert(allowed_picture_url("https://gamerpic.xboxlive.com/avatar.png"));
    assert(allowed_picture_url("https://images-eds-ssl.xboxlive.com/image?x=1"));
    assert(allowed_picture_url("https://avatar-ssl.xboxlive.com/a"));
    assert(allowed_picture_url("https://xboxavatargamerpic.azureedge.net/a.png"));
    assert(!allowed_picture_url("http://gamerpic.xboxlive.com/a.png"));
    assert(!allowed_picture_url("https://evil.test/a.png"));
    assert(!allowed_picture_url("https://gamerpic.xboxlive.com.evil.test/a"));
    assert(!allowed_picture_url("https://user:secret@gamerpic.xboxlive.com/a"));
    assert(!allowed_picture_url("https://gamerpic.xboxlive.com:8443/a"));
    assert(!allowed_picture_url(""));

    // ---- authorization header
    assert(authorization("0123456789", "tok") == "XBL3.0 x=0123456789;tok");
    assert(authorization("", "tok").empty());
    assert(authorization("uhs", "").empty());

    std::puts("xbox_gamertag: ok");
    return 0;
}
