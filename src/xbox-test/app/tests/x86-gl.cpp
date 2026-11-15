// SPDX-License-Identifier: GPL-3.0-or-later
// Runs the real i686 GL/WGL fixture through the PE32 interpreter with a strict
// host shim. The shim checks that guest addresses never escape and receives the
// client array copied at draw time before returning a deterministic pixel.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "../host/x86/x86_play.h"

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<uint8_t>{std::istreambuf_iterator<char>(stream), {}};
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: x86-gl <fixture.exe>\n"); return 2; }
    auto file = read_file(argv[1]);
    if (file.empty()) { std::fprintf(stderr, "X86-GL FAIL fixture is empty\n"); return 1; }
    x86::Memory mem; x86::ThunkTable table; x86::Host host; x86::Registry reg;
    const uintptr_t host_dc = uintptr_t(0x1234567887654321ull);
    const uintptr_t host_context = uintptr_t(0x23456789abcdef01ull);
    bool got_dc = false, got_context = false, cleared = false, vertex_enabled = false, drew = false, read_pixel = false;
    host.get_dc = [&](uint32_t hwnd) -> uintptr_t { got_dc = hwnd == 0; return got_dc ? host_dc : 0; };
    host.release_dc = [&](uint32_t hwnd, uintptr_t dc) { return hwnd == 0 && dc == host_dc; };
    host.gl_call = [&](x86::Host::GlCall& q) {
        if (q.name == "ChoosePixelFormat") { if (q.args.size() != 1 || q.args[0] != host_dc || q.input.size() != 40) return; q.result = 1; return; }
        if (q.name == "SetPixelFormat") { if (q.args.size() != 2 || q.args[0] != host_dc || q.args[1] != 1 || q.input.size() != 40) return; q.result = 1; return; }
        if (q.name == "wglCreateContext") { if (q.args.size() == 1 && q.args[0] == host_dc) q.result = host_context; return; }
        if (q.name == "wglMakeCurrent") {
            if (q.args.size() == 2 && q.args[0] == host_dc && q.args[1] == host_context) { got_context = true; q.result = 1; }
            else q.result = q.args.size() == 2 && q.args[0] == 0 && q.args[1] == 0;
            return;
        }
        if (q.name == "glClearColor") {
            cleared = q.args.size() == 4 && q.args[0] == 0 && q.args[1] == 0 && q.args[2] == 0 && q.args[3] == 0x3f800000u;
            return;
        }
        if (q.name == "glClear") { cleared = cleared && q.args.size() == 1 && q.args[0] == 0x4000u; return; }
        if (q.name == "glEnableClientState") { vertex_enabled = q.args.size() == 1 && q.args[0] == 0x8074u; return; }
        if (q.name == "glDrawArrays") {
            if (cleared && vertex_enabled && q.args.size() == 6 && q.args[0] == 0x0004 && q.args[1] == 0 && q.args[2] == 3 &&
                q.args[3] == 3 && q.args[4] == 0x1406 && q.input.size() == 36) {
                float v[9]{}; std::memcpy(v, q.input.data(), sizeof(v));
                drew = v[0] == -0.8f && v[1] == -0.8f && v[3] == 0.8f && v[7] == 0.8f;
            }
            return;
        }
        if (q.name == "glReadPixels") {
            if (drew && q.args.size() == 7 && q.args[0] == 1 && q.args[1] == 1 && q.output.size() == 4) {
                q.output = {0x12, 0x34, 0x56, 0xff}; read_pixel = true;
            }
            return;
        }
        if (q.name == "SwapBuffers") { q.result = q.args.size() == 1 && q.args[0] == host_dc; return; }
        if (q.name == "wglDeleteContext") { q.result = q.args.size() == 1 && q.args[0] == host_context; return; }
    };
    auto result = x86::run_image(file, "x86-gl.exe", "x86-gl.exe", nullptr, nullptr, 20'000'000, nullptr, false,
        nullptr, nullptr, [&](x86::Host& configured) { configured.get_dc = host.get_dc; configured.release_dc = host.release_dc; configured.gl_call = host.gl_call; });
    if (!result.loaded || !result.finished || result.exit_code != 0 || !got_dc || !got_context || !drew || !read_pixel) {
        std::fprintf(stderr, "X86-GL FAIL loaded=%d finished=%d exit=%u fault=%s dc=%d context=%d draw=%d read=%d\n",
            result.loaded, result.finished, result.exit_code, result.fault.c_str(), got_dc, got_context, drew, read_pixel);
        return 1;
    }
    std::puts("X86-GL PASS: PE32 WGL context, guest-memory triangle array, and readback pixel");
    return 0;
}
