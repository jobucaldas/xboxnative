// SPDX-License-Identifier: GPL-3.0-or-later
// steam_client_package.h: Valve's client manifest entry, one zip member by
// name, and Valve's signature block, on synthetic data.
#include "../steam_client_package.h"
#include <cstdio>
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)
static void put16(std::string& z, uint16_t v) { z.append((const char*)&v, 2); }
static void put32(std::string& z, uint32_t v) { z.append((const char*)&v, 4); }
// A stored (method 0) zip with the given members, in order.
static std::string zip(const std::vector<std::pair<std::string, std::string>>& members) {
    std::string z, cd; std::vector<uint32_t> offsets;
    for (auto& m : members) {
        offsets.push_back(uint32_t(z.size()));
        put32(z, 0x04034b50); put16(z, 20); put16(z, 0); put16(z, 0); put32(z, 0); put32(z, 0);
        put32(z, uint32_t(m.second.size())); put32(z, uint32_t(m.second.size())); put16(z, uint16_t(m.first.size())); put16(z, 0);
        z += m.first; z += m.second;
    }
    for (size_t i = 0; i < members.size(); ++i) {
        auto& m = members[i];
        put32(cd, 0x02014b50); put16(cd, 20); put16(cd, 20); put16(cd, 0); put16(cd, 0); put32(cd, 0); put32(cd, 0);
        put32(cd, uint32_t(m.second.size())); put32(cd, uint32_t(m.second.size())); put16(cd, uint16_t(m.first.size()));
        put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0); put32(cd, 0); put32(cd, offsets[i]); cd += m.first;
    }
    uint32_t at = uint32_t(z.size()); z += cd;
    put32(z, 0x06054b50); put16(z, 0); put16(z, 0); put16(z, uint16_t(members.size())); put16(z, uint16_t(members.size()));
    put32(z, uint32_t(cd.size())); put32(z, at); put16(z, 0);
    return z;
}
static std::string signedDll(size_t size, uint32_t signedLength) {
    std::string d(size, '\0'); d[0] = 'M'; d[1] = 'Z'; memcpy(&d[0x40], "VLV\0", 4);
    uint32_t one = 1; memcpy(&d[0x44], &one, 4); memcpy(&d[0x48], &signedLength, 4);
    return d;
}
int main() {
    const std::string sha(64, 'a');
    std::string manifest = "\"win64\"\n{\n\t\"version\" \"1\"\n\t\"bins_win64\"\n\t{\n\t\t\"file\" \"bins_win64.zip.0123\"\n\t\t\"sha2\" \"" + sha +
        "\"\n\t\t\"sha2vz\" \"" + std::string(64, 'b') + "\"\n\t}\n}\n\"kvsignatures\"\n{\n\t\"win64\" \"x\"\n}\n";
    steam_client_package::Entry e;
    CHECK(steam_client_package::manifest_entry(manifest, e) && e.file == "bins_win64.zip.0123" && e.sha256 == sha);
    std::string traversal = manifest; traversal.replace(traversal.find("bins_win64.zip.0123"), 19, "../bins_win64.zip.0123");
    CHECK(!steam_client_package::manifest_entry(traversal, e));
    std::string badSha = manifest; badSha.replace(badSha.find(sha), 64, std::string(64, 'Z'));
    CHECK(!steam_client_package::manifest_entry(badSha, e));
    auto dll = signedDll(0x400, 0x300);
    auto z = zip({{"steamclient.dll", "x86"}, {"steamclient64.dll", dll}, {"tier0_s64.dll", "t"}});
    auto noInflate = [](const char*, size_t, size_t, std::string&) { return false; };
    std::string out;
    CHECK(steam_client_package::zip_member(z, "steamclient64.dll", noInflate, out) && out == dll);
    CHECK(!steam_client_package::zip_member(z, "steamclient64", noInflate, out));
    CHECK(!steam_client_package::zip_member(z.substr(0, z.size() / 2), "steamclient64.dll", noInflate, out));
    CHECK(steam_client_package::valve_signature_block(dll));
    CHECK(!steam_client_package::valve_signature_block(signedDll(0x400, 0x500)));   // signed length beyond the file
    auto builtin = dll; memcpy(&builtin[0x40], "Wine", 4);
    CHECK(!steam_client_package::valve_signature_block(builtin));
    printf("STEAM CLIENT PACKAGE checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
