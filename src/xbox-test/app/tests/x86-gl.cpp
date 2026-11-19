// SPDX-License-Identifier: GPL-3.0-or-later
// Runs the real i686 GL/WGL fixture through the PE32 interpreter with a strict
// host shim. The shim checks that guest addresses never escape and receives the
// client array copied at draw time before returning a deterministic pixel.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <vector>
#include "../host/x86/x86_play.h"

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<uint8_t>{std::istreambuf_iterator<char>(stream), {}};
}

static bool adapter_float_ok, adapter_double_ok, adapter_int64_ok, adapter_pointer_ok, adapter_output_ok, adapter_return_ok;
static void adapter_clear_color(float r, float g, float b, float a) { adapter_float_ok = r == 0.25f && g == 0.5f && b == 0.75f && a == 1.0f; }
static void adapter_clear_depth(double value) { adapter_double_ok = value == 0.625; }
static void adapter_uniform_handle(uint32_t location, uint64_t handle) { adapter_int64_ok = location == 3 && handle == 0x12345678abcdef01ull; }
static void adapter_draw_elements(uint32_t mode, int32_t count, uint32_t type, const void* indices) { adapter_pointer_ok = mode == 4 && count == 3 && type == 0x1403u && indices == reinterpret_cast<const void*>(uintptr_t(0x1234)); }
static void adapter_get_integer(uint32_t pname, void* data) { adapter_output_ok = pname == 0x0BA2u && data != nullptr; if (data) static_cast<int32_t*>(data)[0] = 321; }
static uint32_t adapter_create_shader(uint32_t type) { adapter_return_ok = type == 0x8B31u; return 7; }

static bool verify_gl_host_errors_and_sizes() {
    x86::Memory mem; mem.map(0x1000, 0x30000, x86::PROT_R | x86::PROT_W); mem.map(x86::TEB_ADDR, 0x1000, x86::PROT_R | x86::PROT_W);
    x86::ThunkTable table; x86::Host host; x86::Registry registry; x86::register_gl(registry, host);
    x86::Cpu cpu(mem, table); cpu.reg[x86::ESP] = 0x2f000; cpu.fs_base = x86::TEB_ADDR;
    bool packed = false, matrix = false;
    host.gl_call = [&](x86::Host::GlCall& q) {
        if (q.name == "glTexImage2D") { packed = q.input.size() == 2 && q.input[0] == 0x12 && q.input[1] == 0x34; return; }
        if (q.name == "glUniformMatrix2x3fv") { matrix = q.input.size() == 48; return; }
        if (q.name == "glClear") { q.gap = true; q.last_error = 5; }
    };
    auto route = [&](const char* name) -> x86::Thunk* {
        auto it = registry.map.find({"opengl32.dll", x86::Registry::key_sym(name)});
        return it == registry.map.end() ? nullptr : &it->second;
    };
    auto args = [&](std::initializer_list<uint32_t> values) {
        unsigned slot = 0; for (uint32_t value : values) mem.w32(cpu.reg[x86::ESP] + 4 + 4 * slot++, value);
    };
    for (unsigned i = 0; i < 48; ++i) mem.w8(0x5000 + i, uint8_t(i));
    auto tex = route("glTexImage2D");
    if (!tex) return false;
    mem.w8(0x4000, 0x12); mem.w8(0x4001, 0x34);
    args({0x0DE1, 0, 0, 1, 1, 0, 0x1907, 0x8363, 0x4000}); tex->fn(cpu);
    auto uniform = route("glUniformMatrix2x3fv");
    if (!uniform) return false;
    args({1, 2, 0, 0x5000}); uniform->fn(cpu);
    auto clear = route("glClear");
    if (!clear) return false;
    mem.w32(cpu.fs_base + 0x34, 777); args({0x4000}); clear->fn(cpu);
    return packed && matrix && mem.r32(cpu.fs_base + 0x34) == 5;
}

int main(int argc, char** argv) {
    if (!verify_gl_host_errors_and_sizes()) { std::fprintf(stderr, "X86-GL FAIL host error or packed/matrix marshalling\n"); return 1; }
    if (argc != 2) { std::fprintf(stderr, "usage: x86-gl <fixture.exe>\n"); return 2; }
    auto signature = [](const char* name) -> unsigned short { for (unsigned i = 0; i < sizeof(x86::kGlExports) / sizeof(x86::kGlExports[0]); ++i) if (std::strcmp(x86::kGlExports[i].name, name) == 0) return x86::kGlSignatureIds[i]; return 65535; };
    float color[] = {0.25f, 0.5f, 0.75f, 1.0f}; uintptr_t color_words[4]{};
    for (unsigned i = 0; i < 4; ++i) { uint32_t bits = 0; std::memcpy(&bits, &color[i], sizeof(bits)); color_words[i] = bits; }
    x86::gl_call_generated(signature("glClearColor"), reinterpret_cast<uintptr_t>(&adapter_clear_color), color_words);
    double depth = 0.625; uint64_t depth_bits = 0; std::memcpy(&depth_bits, &depth, sizeof(depth));
    uintptr_t depth_words[2] = {uint32_t(depth_bits), uint32_t(depth_bits >> 32)};
    x86::gl_call_generated(signature("glClearDepth"), reinterpret_cast<uintptr_t>(&adapter_clear_depth), depth_words);
    uintptr_t handle_words[3] = {3, 0xabcdef01u, 0x12345678u};
    x86::gl_call_generated(signature("glUniformHandleui64ARB"), reinterpret_cast<uintptr_t>(&adapter_uniform_handle), handle_words);
    uintptr_t pointer_words[4] = {4, 3, 0x1403u, 0x1234u};
    x86::gl_call_generated(signature("glDrawElements"), reinterpret_cast<uintptr_t>(&adapter_draw_elements), pointer_words);
    int32_t output_value = 0; uintptr_t output_words[2] = {0x0BA2u, reinterpret_cast<uintptr_t>(&output_value)};
    x86::gl_call_generated(signature("glGetIntegerv"), reinterpret_cast<uintptr_t>(&adapter_get_integer), output_words);
    uintptr_t shader_words[1] = {0x8B31u};
    uintptr_t shader_result = x86::gl_call_generated(signature("glCreateShader"), reinterpret_cast<uintptr_t>(&adapter_create_shader), shader_words);
    adapter_return_ok = adapter_return_ok && shader_result == 7 && output_value == 321;
    auto file = read_file(argv[1]);
    if (file.empty()) { std::fprintf(stderr, "X86-GL FAIL fixture is empty\n"); return 1; }
    x86::Memory mem; x86::ThunkTable table; x86::Host host; x86::Registry reg;
    const uintptr_t host_window = uintptr_t(0x34567890u);
    const uintptr_t host_dc = uintptr_t(0x1234567887654321ull);
    const uintptr_t host_context = uintptr_t(0x23456789abcdef01ull);
    bool got_class = false, got_window = false, got_class_a = false, got_window_a = false, got_dc = false, got_context = false, destroyed_window = false;
    bool cleared = false, vertex_enabled = false, drew = false, drew_elements = false, buffer_drew = false, read_pixel = false;
    bool texture_uploaded = false, shader_created = false, shader_source_set = false, shader_compiled = false, buffer_uploaded = false, texture_generated = false;
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
        if (q.name == "glGetIntegerv") { saw_output_pointer = q.arg_kinds.size() == 2 && q.arg_kinds[1] == x86::GlArgKind::OutputPointer && q.args.size() == 2 && q.args[1] == 0; q.output = {0x00, 0x20, 0x00, 0x00}; return; }
        if (q.name == "glGenTextures") { if (q.pointers.size() == 1 && q.pointers[0].output && q.pointers[0].bytes.size() == 4) { q.pointers[0].bytes = {0x34, 0x12, 0, 0}; texture_generated = true; } return; }
        if (q.name == "glTexImage2D") { texture_uploaded = q.args.size() == 9 && q.args[0] == 0x0DE1u && q.args[3] == 1 && q.args[4] == 1 && q.args[8] == 0 && q.input == std::vector<uint8_t>({0x12, 0x34, 0x56, 0xff}); return; }
        if (q.name == "glBufferData") { buffer_uploaded = q.args.size() == 4 && q.args[0] == 0x8892u && q.args[1] == 4 && q.args[2] != 0 && q.args[3] == 0x88E4u && q.pointers.size() == 1 && q.pointers[0].bytes == std::vector<uint8_t>({0xaa, 0xbb, 0xcc, 0xdd}); return; }
        if (q.name == "glCreateShader") { q.result = q.args.size() == 1 && q.args[0] == 0x8B31u ? 7u : 0u; shader_created = q.result == 7u; return; }
        if (q.name == "glShaderSource") { static const char first[] = "void main() {", second[] = " gl_Position = gl_Vertex; }"; shader_source_set = q.args.size() == 4 && q.args[0] == 7u && q.args[1] == 2 && q.args[2] == 0 && q.args[3] == 0 && q.shader_sources.size() == 2 && q.shader_lengths.empty() && q.shader_sources[0].size() == sizeof(first) && q.shader_sources[1].size() == sizeof(second) && std::memcmp(q.shader_sources[0].data(), first, sizeof(first)) == 0 && std::memcmp(q.shader_sources[1].data(), second, sizeof(second)) == 0; return; }
        if (q.name == "glCompileShader") { shader_compiled = q.args.size() == 1 && q.args[0] == 7u; return; }
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
    if (!result.loaded || !result.finished || result.exit_code != 0 || !got_class || !got_window || !got_class_a || !got_window_a || !got_dc || !got_context || !drew || !drew_elements || !buffer_drew || !read_pixel || !destroyed_window || !texture_uploaded || !shader_created || !shader_source_set || !shader_compiled || !buffer_uploaded || !texture_generated ||
        !saw_float || !saw_scalar || !saw_pointer || !saw_double || !saw_int64 || !saw_output_pointer || !map_unmapped || !adapter_float_ok || !adapter_double_ok || !adapter_int64_ok || !adapter_pointer_ok || !adapter_output_ok || !adapter_return_ok) {
        std::fprintf(stderr, "X86-GL FAIL loaded=%d finished=%d exit=%u fault=%s class=%d window=%d dc=%d context=%d draw=%d read=%d destroy=%d tex=%d shader=%d/%d/%d buffer=%d output=%d adapters=%d/%d/%d\n",
            result.loaded, result.finished, result.exit_code, result.fault.c_str(), got_class, got_window, got_dc, got_context, drew, read_pixel, destroyed_window, texture_uploaded, shader_created, shader_source_set, shader_compiled, buffer_uploaded, saw_output_pointer, adapter_float_ok, adapter_double_ok, adapter_int64_ok);
        return 1;
    }
    std::puts("X86-GL PASS: PE32 WGL context, guest-memory triangle array, and readback pixel");
    return 0;
}
