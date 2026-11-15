#include "../dxil_runtime.h"
#include <cstdio>
#include <fstream>

static std::string sha(const void* data, size_t size) {
    const std::string text(static_cast<const char*>(data), size);
    if (text == "fixture DXIL bytes") return "69629e9dad84ead76a62b74e0b28d272d0ea470244bc3e20108a78a98baa333a";
    if (text == "fixture Microsoft license") return "39ab32bcd7f3a9b60cb3d7dd8b76557adc658e6a2e0ad9fb204ce5cc859410ad";
    return {};
}
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char** argv) {
    CHECK(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> zip((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    CHECK(input.good() || input.eof());
    auto digest = [](const void* p, size_t n) { return sha(p, n); };
    std::vector<uint8_t> extracted; std::string reason;
    CHECK(dxil_runtime::extract(zip, "bin/x64/dxil.dll", "69629e9dad84ead76a62b74e0b28d272d0ea470244bc3e20108a78a98baa333a", extracted, digest, reason));
    CHECK(std::string(extracted.begin(), extracted.end()) == "fixture DXIL bytes");
    CHECK(!dxil_runtime::extract(zip, "bin/x64/dxil.dll", "0000000000000000000000000000000000000000000000000000000000000000", extracted, digest, reason));
    CHECK(reason == "dll hash mismatch");
    CHECK(dxil_runtime::extract(zip, "LICENSE-MS.txt", "", extracted, digest, reason));
    CHECK(std::string(extracted.begin(), extracted.end()) == "fixture Microsoft license");
    std::puts("PASS Mesa DXIL fixture ZIP extraction and tampered-hash rejection");
    return 0;
}
