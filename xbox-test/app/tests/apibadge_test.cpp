// Off-device test of apibadge_core.h. usage: apibadge_test <pe-file> [expected-kind-name]...  (self-tests run first)
#include <cstdlib>
#include "../apibadge_core.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main(int argc, char** argv) {
    using namespace apib; std::string n;
    CHECK(classify_store_text("<strong>Minimum:</strong><br><ul><li>DirectX: Version 11</li>", &n) == K_D3D11); CHECK(n == "DirectX 11");
    CHECK(classify_store_text("DirectX Version: 9.0c", nullptr) == K_LEGACY);
    CHECK(classify_store_text("Graphics: GTX 970<br>DirectX 12 compatible", nullptr) == K_D3D12);
    CHECK(classify_store_text("DirectX: Version 10  ... Recommended: DirectX 11", nullptr) == K_D3D11);
    CHECK(classify_store_text("Windows 10, 4 GB RAM", nullptr) == K_UNKNOWN);
    CHECK(classify_store_text("", nullptr) == K_UNKNOWN);
    CHECK(classify_store_text("DirectX End-User Runtimes 2010", nullptr) == K_UNKNOWN);
    CHECK(classify_dlls({"kernel32.dll", "d3d11.dll", "opengl32.dll"}) == K_D3D11);
    CHECK(classify_dlls({"dxgi.dll"}) == K_D3D11);
    CHECK(classify_dlls({"d3d12.dll", "dxgi.dll"}) == K_D3D12);
    CHECK(classify_dlls({"d3d9.dll", "kernel32.dll"}) == K_LEGACY);
    CHECK(classify_dlls({"opengl32.dll"}) == K_LEGACY);
    CHECK(classify_dlls({"kernel32.dll"}) == K_NONE);
    for (int i = 1; i + 1 < argc + 1 && i < argc; i += 2) {
        FILE* f = fopen(argv[i], "rb"); std::set<std::string> d; bool ok = f && pe_dlls(f, d); if (f) fclose(f);
        int k = ok ? classify_dlls(d) : -1; printf("%s: pe=%d dlls=%zu kind=%s evidence=[%s]\n", argv[i], ok, d.size(), ok ? kind_name(k) : "NOT-PE", gfx_evidence(d).c_str());
        if (i + 1 < argc) { std::string want = argv[i + 1]; if (want != (ok ? kind_name(k) : "NOT-PE")) { printf("FAIL expected %s\n", want.c_str()); fails++; } }
    }
    printf(fails ? "apibadge_test: %d FAILURES\n" : "apibadge_test: ALL OK\n", fails); return fails ? 1 : 0;
}
