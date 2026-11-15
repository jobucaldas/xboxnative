// SPDX-License-Identifier: GPL-3.0-or-later
// Runs the real i686 GL/WGL fixture through the PE32 interpreter with a strict
// host shim. The shim checks that guest addresses never escape and receives the
// client array copied at draw time before returning a deterministic pixel.
#include <algorithm>
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
    const uintptr_t host_window = uintptr_t(0x34567890u);
    const uintptr_t host_dc = uintptr_t(0x1234567887654321ull);
    const uintptr_t host_context = uintptr_t(0x23456789abcdef01ull);
    bool got_class = false, got_window = false, got_class_a = false, got_window_a = false, got_dc = false, got_context = false, destroyed_window = false;
    bool cleared = false, vertex_enabled = false, drew = false, drew_elements = false, buffer_drew = false, read_pixel = false;
    bool saw_float = false, saw_scalar = false, saw_pointer = false, saw_double = false, saw_int64 = false, saw_output_pointer = false, map_unmapped = false;
    for (const auto& item : x86::kGlExports) if (std::strcmp(item.name, "glVertexPointer") == 0)
        saw_pointer = x86::kGlArgs[item.first_arg + item.arg_count - 1].kind == x86::GlArgKind::Pointer;
    host.register_window_class = [&](const std::u16string& name) {
        got_class = got_class || name == u"X86GLWindow"; got_class_a = got_class_a || name == u"X86GLWindowA"; return got_class || got_class_a;
    };
    host.create_window = [&](const x86::Host::WindowCreate& q) -> uintptr_t {
        got_window = got_window || (got_class && q.class_name == u"X86GLWindow" && q.title == u"x86 GL test" && q.width == 64 && q.height == 64);
        got_window_a = got_window_a || (got_class_a && q.class_name == u"X86GLWindowA" && q.title == u"x86 GL test" && q.width == 64 && q.height == 64);
        return (got_window || got_window_a) ? host_window : 0;
    };
    host.destroy_window = [&](uintptr_t window) { destroyed_window = window == host_window; return destroyed_window; };
    host.get_dc = [&](uintptr_t window) -> uintptr_t { got_dc = window == host_window; return got_dc ? host_dc : 0; };
    host.release_dc = [&](uintptr_t window, uintptr_t dc) { return window == host_window && dc == host_dc; };
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
            saw_float = q.arg_kinds.size() == 4 && std::all_of(q.arg_kinds.begin(), q.arg_kinds.end(), [](x86::GlArgKind kind) { return kind == x86::GlArgKind::Float; });
            cleared = q.args.size() == 4 && q.args[0] == 0 && q.args[1] == 0 && q.args[2] == 0 && q.args[3] == 0x3f800000u;
            return;
        }
        if (q.name == "glClear") { saw_scalar = q.arg_kinds.size() == 1 && q.arg_kinds[0] == x86::GlArgKind::Scalar; cleared = cleared && q.args.size() == 1 && q.args[0] == 0x4000u; return; }
        if (q.name == "glClearDepth") { saw_double = q.arg_kinds.size() == 1 && q.arg_kinds[0] == x86::GlArgKind::Double && q.arg_slots[0] == 2 && q.args.size() == 2; return; }
        if (q.name == "glUniformHandleui64ARB") { saw_int64 = q.arg_kinds.size() == 2 && q.arg_kinds[1] == x86::GlArgKind::Int64 && q.arg_slots[1] == 2 && q.args.size() == 3; return; }
        if (q.name == "glGetIntegerv") { saw_output_pointer = q.arg_kinds.size() == 2 && q.arg_kinds[1] == x86::GlArgKind::OutputPointer; return; }
        if (q.name == "glMapBufferRange") { if (q.args.size() == 4 && q.args[0] == 0x8892u && q.args[2] == 4) q.output = {1, 2, 3, 4}; return; }
        if (q.name == "x86GlUnmapBuffer") { map_unmapped = q.args.size() == 2 && q.input == std::vector<uint8_t>({0x7f, 2, 3, 4}); q.result = map_unmapped; return; }
        if (q.name == "glEnableClientState") { vertex_enabled = q.args.size() == 1 && q.args[0] == 0x8074u; return; }
        if (q.name == "x86GlDrawElementsClient") { drew_elements = q.args.size() == 5 && q.args[0] == 0x0004 && q.args[1] == 3 && q.input.size() == 36; return; }
        if (q.name == "glDrawArrays") {
            if (cleared && vertex_enabled && q.args.size() == 6 && q.args[0] == 0x0004 && q.args[1] == 0 && q.args[2] == 3 &&
                q.args[3] == 3 && q.args[4] == 0x1406 && q.input.size() == 36) {
                float v[9]{}; std::memcpy(v, q.input.data(), sizeof(v));
                drew = v[0] == -0.8f && v[1] == -0.8f && v[3] == 0.8f && v[7] == 0.8f;
            } else if (q.args.size() == 3 && q.args[0] == 0x0004 && q.args[2] == 3 && q.input.empty()) buffer_drew = true;
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
        nullptr, nullptr, [&](x86::Host& configured) { configured.register_window_class = host.register_window_class; configured.create_window = host.create_window;
            configured.destroy_window = host.destroy_window; configured.get_dc = host.get_dc; configured.release_dc = host.release_dc; configured.gl_call = host.gl_call; });
    if (!result.loaded || !result.finished || result.exit_code != 0 || !got_class || !got_window || !got_class_a || !got_window_a || !got_dc || !got_context || !drew || !drew_elements || !buffer_drew || !read_pixel || !destroyed_window ||
        !saw_float || !saw_scalar || !saw_pointer || !saw_double || !saw_int64 || !saw_output_pointer || !map_unmapped) {
        std::fprintf(stderr, "X86-GL FAIL loaded=%d finished=%d exit=%u fault=%s class=%d window=%d dc=%d context=%d draw=%d read=%d destroy=%d\n",
            result.loaded, result.finished, result.exit_code, result.fault.c_str(), got_class, got_window, got_dc, got_context, drew, read_pixel, destroyed_window);
        return 1;
    }
    std::puts("X86-GL PASS: PE32 WGL context, guest-memory triangle array, and readback pixel");
    return 0;
}
