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

static bool adapter_float_ok, adapter_double_ok, adapter_int64_ok, adapter_pointer_ok, adapter_output_ok, adapter_return_ok, adapter_ptr_return_ok;
static void adapter_clear_color(float r, float g, float b, float a) { adapter_float_ok = r == 0.25f && g == 0.5f && b == 0.75f && a == 1.0f; }
static void adapter_clear_depth(double value) { adapter_double_ok = value == 0.625; }
static void adapter_uniform_handle(uint32_t location, uint64_t handle) { adapter_int64_ok = location == 3 && handle == 0x12345678abcdef01ull; }
static void adapter_draw_elements(uint32_t mode, int32_t count, uint32_t type, const void* indices) { adapter_pointer_ok = mode == 4 && count == 3 && type == 0x1403u && indices == reinterpret_cast<const void*>(uintptr_t(0x1234)); }
static void adapter_get_integer(uint32_t pname, void* data) { adapter_output_ok = pname == 0x0BA2u && data != nullptr; if (data) static_cast<int32_t*>(data)[0] = 321; }
static uint32_t adapter_create_shader(uint32_t type) { adapter_return_ok = type == 0x8B31u; return 7; }
static const void* adapter_get_string(uint32_t name) { return name == 0x1F02u ? reinterpret_cast<const void*>(uintptr_t(0x123456789abcdef0ull)) : nullptr; }
static const void* adapter_get_stringi(uint32_t name, uint32_t index) { return (name == 0x1F03u && index == 3u) ? reinterpret_cast<const void*>(uintptr_t(0x23456789abcdef08ull)) : nullptr; }

static bool verify_gl_host_errors_and_sizes() {
    x86::Memory mem; mem.map(0x1000, 0x30000, x86::PROT_R | x86::PROT_W); mem.map(x86::TEB_ADDR, 0x1000, x86::PROT_R | x86::PROT_W);
    x86::ThunkTable table; x86::Host host; x86::Registry registry; x86::register_gl(registry, host);
    x86::Cpu cpu(mem, table); cpu.reg[x86::ESP] = 0x2f000; cpu.fs_base = x86::TEB_ADDR;
    bool packed = false, matrix = false;
    unsigned vectors = 0;
    host.gl_call = [&](x86::Host::GlCall& q) {
        if (q.name == "glTexImage2D") { packed = q.input.size() == 2 && q.input[0] == 0x12 && q.input[1] == 0x34; return; }
        if (q.name == "glUniformMatrix2x3fv") { matrix = q.input.size() == 48; return; }
        if (q.name == "glUniform4uiv" || q.name == "glUniform4fvARB") { vectors += q.input.size() == 16; return; }
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
    for (const char* vector : {"glUniform4uiv", "glUniform4fvARB"}) {
        auto v = route(vector);
        if (!v) return false;
        args({3, 1, 0x5000}); v->fn(cpu);
    }
    auto clear = route("glClear");
    if (!clear) return false;
    mem.w32(cpu.fs_base + 0x34, 777); args({0x4000}); clear->fn(cpu);
    return packed && matrix && vectors == 2 && mem.r32(cpu.fs_base + 0x34) == 5;
}

static bool verify_gl_pointer_returns() {
    auto find_export = [](const char* name) -> int {
        for (unsigned i = 0; i < sizeof(x86::kGlExports) / sizeof(x86::kGlExports[0]); ++i)
            if (std::strcmp(x86::kGlExports[i].name, name) == 0) return int(i);
        return -1;
    };
    for (const char* ptr_fn : {
        "glGetString", "glGetStringi", "glMapBuffer", "glMapBufferARB", "glMapBufferRange",
        "glMapNamedBuffer", "glMapNamedBufferEXT", "glMapNamedBufferRange", "glMapNamedBufferRangeEXT",
        "glFenceSync", "glCreateSyncFromCLeventARB", "glImportSyncEXT"
    }) {
        int idx = find_export(ptr_fn);
        if (idx < 0 || x86::kGlReturnKinds[idx] != x86::GlReturnKind::Pointer) return false;
    }
    for (const char* out_ptr_fn : {"glGetPointerv", "glGetBufferPointerv", "glGetVertexAttribPointerv"}) {
        int idx = find_export(out_ptr_fn);
        if (idx < 0 || x86::kGlSignatureIds[idx] != 65535) return false;
    }

    x86::Memory mem; mem.map(0x1000, 0x30000, x86::PROT_R | x86::PROT_W); mem.map(x86::TEB_ADDR, 0x1000, x86::PROT_R | x86::PROT_W);
    x86::ThunkTable table; x86::Host host; x86::Registry registry; x86::register_gl(registry, host);
    x86::Cpu cpu(mem, table); cpu.reg[x86::ESP] = 0x2f000; cpu.fs_base = x86::TEB_ADDR;

    bool unbridged_host_called = false;
    bool rejected_map_called_host = false;
    bool read_only_unmap_empty = false;
    bool saw_bind_arb = false;
    std::map<uint32_t, uint32_t> host_bound;
    std::map<uint32_t, std::vector<uint8_t>> host_buffers = {
        {10u, {0x11, 0x22, 0x33, 0x44}},
        {20u, {0xA1, 0xA2, 0xA3, 0xA4}},
        {30u, {0xE1, 0xE2, 0xE3, 0xE4}},
        {40u, {0x01, 0x02, 0x03, 0x04}},
        {50u, {0x51, 0x52, 0x53, 0x54}},
        {60u, {0x61, 0x62, 0x63, 0x64}},
        {70u, {0x71, 0x72, 0x73, 0x74}},
        {80u, {0x81, 0x82, 0x83, 0x84}},
    };
    // Host-side VAO model: GL_ELEMENT_ARRAY_BUFFER is per-VAO state, so a VAO switch swaps the host binding.
    std::map<uint32_t, uint32_t> host_vao_element;
    uint32_t host_vao = 0, host_range_calls = 0;
    bool binding_mismatch = false;
    auto set_cstr = [](x86::Host::GlCall& q, const char* s) {
        size_t n = std::strlen(s);
        q.output.assign(reinterpret_cast<const uint8_t*>(s), reinterpret_cast<const uint8_t*>(s) + n + 1);
        q.result = 1;
    };
    host.gl_call = [&](x86::Host::GlCall& q) {
        if (q.name == "glGetString") {
            if (q.args[0] == 0x1F02u) set_cstr(q, "4.6 Mesa 24.0");
            else if (q.args[0] == 0x1F03u) set_cstr(q, "");
            else if (q.args[0] == 0xBAD1u) q.result = uintptr_t(0x123456789abcdef0ull); // misbehaving raw-pointer host
            else q.result = 0; // legitimate NULL
            return;
        }
        if (q.name == "glGetStringi") {
            if (q.args[0] == 0x1F03u && q.args[1] == 0u) set_cstr(q, "GL_ARB_vertex_buffer_object");
            else if (q.args[0] == 0x1F03u && q.args[1] == 1u) set_cstr(q, "GL_EXT_framebuffer_object");
            else q.result = 0;
            return;
        }
        if (q.name == "glBindBuffer" || q.name == "glBindBufferARB") {
            if (q.name == "glBindBufferARB") saw_bind_arb = true;
            host_bound[uint32_t(q.args[0])] = uint32_t(q.args[1]);
            if (q.args[0] == 0x8893u) host_vao_element[host_vao] = uint32_t(q.args[1]);
            return;
        }
        if (q.name == "glBindVertexArray") {
            host_vao = uint32_t(q.args[0]); host_bound[0x8893u] = host_vao_element[host_vao];
            return;
        }
        if (q.name == "glDeleteVertexArrays") {
            if (host_vao == 2u) { host_vao = 0; host_bound[0x8893u] = host_vao_element[0]; }
            return;
        }
        if (q.name == "glBindBufferBase" || q.name == "glBindBufferRange" || q.name == "glBindBufferOffsetEXT") {
            ++host_range_calls;
            if (q.args[1] >= 4u) { q.rejected = true; return; } // GL_INVALID_VALUE: index out of range
            host_bound[uint32_t(q.args[0])] = uint32_t(q.args[2]);
            return;
        }
        if (q.name == "glDeleteBuffersARB") { return; }
        if (q.name == "glMapBuffer" || q.name == "glMapBufferARB" || q.name == "glMapBufferRange") {
            const uint32_t target = uint32_t(q.args[0]);
            const uint32_t buf = host_bound[target];
            const uint32_t access = uint32_t(q.args[q.name == "glMapBufferRange" ? 3 : 1]);
            if (!buf || (q.name != "glMapBufferRange" && access != 0x88B8u && access != 0x88B9u && access != 0x88BAu)) {
                rejected_map_called_host = true;
                return;
            }
            if (q.buffer != buf) { binding_mismatch = true; q.gap = true; return; } // launch.h ground-truth check
            q.output = host_buffers[buf];
            return;
        }
        if (q.name == "x86GlUnmapBuffer") {
            const uint32_t target = uint32_t(q.args[0]);
            const uint32_t buf = host_bound[target];
            if (buf == 30u) { q.result = 0; return; } // simulate glBufferSubData GL error -> GL_FALSE
            if (q.input.empty()) { read_only_unmap_empty = true; q.result = 1u; return; }
            if (q.buffer != buf) { binding_mismatch = true; q.result = 0; return; }
            host_buffers[buf] = q.input;
            q.result = 1u;
            return;
        }
        unbridged_host_called = true;
        q.result = uintptr_t(0x123456789abcdef0ull);
    };

    auto route = [&](const char* name) -> x86::Thunk* {
        auto it = registry.map.find({"opengl32.dll", x86::Registry::key_sym(name)});
        return it == registry.map.end() ? nullptr : &it->second;
    };
    auto args = [&](std::initializer_list<uint32_t> values) {
        unsigned slot = 0; for (uint32_t value : values) mem.w32(cpu.reg[x86::ESP] + 4 + 4 * slot++, value);
    };

    auto get_string = route("glGetString");
    auto get_stringi = route("glGetStringi");
    auto bind_buf = route("glBindBuffer");
    auto bind_arb = route("glBindBufferARB");
    auto del_arb = route("glDeleteBuffersARB");
    auto map_core = route("glMapBuffer");
    auto map_arb = route("glMapBufferARB");
    auto map_range = route("glMapBufferRange");
    auto unmap_core = route("glUnmapBuffer");
    auto unmap_arb = route("glUnmapBufferARB");
    auto bind_vao = route("glBindVertexArray");
    auto del_vao = route("glDeleteVertexArrays");
    auto bind_base = route("glBindBufferBase");
    auto bind_range = route("glBindBufferRange");
    auto bind_offset = route("glBindBufferOffsetEXT");
    if (!get_string || !get_stringi || !bind_buf || !bind_arb || !del_arb || !map_core || !map_arb || !map_range || !unmap_core || !unmap_arb ||
        !bind_vao || !del_vao || !bind_base || !bind_range || !bind_offset) return false;

    // glGetString returns valid read-only guest string, cached across repeated calls without leaking.
    args({0x1F02u}); uint32_t ver_addr = get_string->fn(cpu);
    if (!ver_addr || mem.read_cstr(ver_addr) != "4.6 Mesa 24.0") return false;
    if ((mem.protect_of(ver_addr) & (x86::PROT_R | x86::PROT_W)) != x86::PROT_R) return false;
    const uint32_t cursor_after_first = host.gl_string_cursor;
    for (unsigned i = 0; i < 100; ++i) {
        args({0x1F02u});
        if (get_string->fn(cpu) != ver_addr) return false;
    }
    if (host.gl_string_cursor != cursor_after_first || host.gl_strings.size() != 1) return false;

    // Non-NULL empty string returns a valid guest pointer to NUL.
    args({0x1F03u}); uint32_t empty_addr = get_string->fn(cpu);
    if (!empty_addr || empty_addr == ver_addr || mem.r8(empty_addr) != 0) return false;

    // Legitimate NULL returns 0 without recording a gap.
    host.err.clear();
    args({0xDEADu});
    if (get_string->fn(cpu) != 0 || !host.err.empty()) return false;

    // Misbehaving host returning raw 64-bit result with empty output fails closed (gap + 0).
    host.err.clear(); mem.w32(cpu.fs_base + 0x34, 0);
    args({0xBAD1u});
    if (get_string->fn(cpu) != 0 || mem.r32(cpu.fs_base + 0x34) != 120u || host.err.find("x86 GL gap: glGetString") == std::string::npos) return false;

    // glGetStringi returns valid cached guest strings per index and 0 on NULL.
    host.err.clear();
    args({0x1F03u, 0u}); uint32_t ext0 = get_stringi->fn(cpu);
    args({0x1F03u, 0u}); uint32_t ext0_again = get_stringi->fn(cpu);
    args({0x1F03u, 1u}); uint32_t ext1 = get_stringi->fn(cpu);
    args({0x1F03u, 2u}); uint32_t ext_null = get_stringi->fn(cpu);
    if (!ext0 || ext0 != ext0_again || mem.read_cstr(ext0) != "GL_ARB_vertex_buffer_object") return false;
    if (!ext1 || ext1 == ext0 || mem.read_cstr(ext1) != "GL_EXT_framebuffer_object") return false;
    if (ext_null != 0 || !host.err.empty()) return false;

    // Reject mapping with no buffer bound (buffer 0) and invalid access enums without calling host.
    args({0x8892u, 0x88BAu});
    if (map_arb->fn(cpu) != 0 || !host.gl_maps.empty()) return false;
    args({0x8892u, 10u}); bind_buf->fn(cpu);
    for (uint32_t bad_access : {0u, 0x1234u, 0x88B7u, 0x88BBu}) {
        args({0x8892u, bad_access});
        if (map_arb->fn(cpu) != 0 || map_core->fn(cpu) != 0 || !host.gl_maps.empty()) return false;
    }
    args({0x8892u, 0u, 4u, 0u}); if (map_range->fn(cpu) != 0) return false;
    args({0x8892u, 0u, 4u, 0x0042u}); if (map_range->fn(cpu) != 0) return false;
    // Invalid glMapBufferRange flag combinations: READ with INVALIDATE_RANGE/INVALIDATE_BUFFER/UNSYNCHRONIZED,
    // and FLUSH_EXPLICIT without WRITE.
    for (uint32_t bad_flags : {0x0001u | 0x0004u, 0x0001u | 0x0008u, 0x0001u | 0x0020u, 0x0001u | 0x0010u, 0x0001u | 0x0002u | 0x0004u}) {
        args({0x8892u, 0u, 4u, bad_flags}); if (map_range->fn(cpu) != 0) return false;
    }
    if (rejected_map_called_host || !host.gl_maps.empty()) return false;
    // WRITE with FLUSH_EXPLICIT is a valid combination.
    args({0x8892u, 0u, 4u, 0x0002u | 0x0010u}); uint32_t addr_flush = map_range->fn(cpu);
    if (!addr_flush) return false;
    args({0x8892u}); if (unmap_arb->fn(cpu) != 1u || !host.gl_maps.empty()) return false;

    // Cross-target buffer identity: map A (10) on ARRAY_BUFFER, map B (20) on ELEMENT_ARRAY_BUFFER,
    // then bind B (20) to ARRAY_BUFFER via glBindBufferARB and unmap -> must upload B's bytes into B.
    args({0x8892u, 0x88BAu}); uint32_t addr_a = map_arb->fn(cpu);
    if (!addr_a || mem.r8(addr_a) != 0x11 || mem.r8(addr_a + 3) != 0x44) return false;
    mem.w8(addr_a, 0x55); mem.w8(addr_a + 3, 0x99);
    args({0x8893u, 20u}); bind_arb->fn(cpu);
    args({0x8893u, 0x88BAu}); uint32_t addr_b = map_arb->fn(cpu);
    if (!addr_b || addr_b == addr_a || mem.r8(addr_b) != 0xA1 || mem.r8(addr_b + 3) != 0xA4) return false;
    mem.w8(addr_b, 0xB1); mem.w8(addr_b + 3, 0xB4);
    args({0x8892u, 20u}); bind_arb->fn(cpu);
    // Double-mapping B on another target while B is already mapped must fail.
    args({0x8892u, 0x88BAu}); if (map_arb->fn(cpu) != 0) return false;
    args({0x8892u});
    if (unmap_arb->fn(cpu) != 1u || mem.mapped(addr_b) || !mem.mapped(addr_a)) return false;
    if (host_buffers[20u] != std::vector<uint8_t>({0xB1, 0xA2, 0xA3, 0xB4})) return false;
    if (host_buffers[10u] != std::vector<uint8_t>({0x11, 0x22, 0x33, 0x44})) return false;
    // Unmap A via ELEMENT_ARRAY_BUFFER -> uploads A's bytes into A.
    args({0x8893u, 10u}); bind_buf->fn(cpu);
    args({0x8893u});
    if (unmap_core->fn(cpu) != 1u || mem.mapped(addr_a) || !host.gl_maps.empty()) return false;
    if (host_buffers[10u] != std::vector<uint8_t>({0x55, 0x22, 0x33, 0x99})) return false;
    if (!saw_bind_arb) return false;

    // GL_READ_ONLY (0x88B8) unmap does not copy back writes.
    args({0x8892u, 10u}); bind_buf->fn(cpu);
    args({0x8892u, 0x88B8u}); uint32_t addr_ro = map_arb->fn(cpu);
    if (!addr_ro) return false;
    mem.w8(addr_ro, 0xFF);
    args({0x8892u});
    if (unmap_arb->fn(cpu) != 1u || !read_only_unmap_empty || mem.mapped(addr_ro)) return false;
    if (host_buffers[10u] != std::vector<uint8_t>({0x55, 0x22, 0x33, 0x99})) return false;

    // Copy-back upload error on unmap returns GL_FALSE (0) and still cleans up guest scratch map.
    args({0x8892u, 30u}); bind_arb->fn(cpu);
    args({0x8892u, 0x88B9u}); uint32_t addr_err = map_arb->fn(cpu);
    if (!addr_err || mem.r8(addr_err) != 0xE1) return false;
    mem.w8(addr_err, 0xF1);
    args({0x8892u});
    if (unmap_arb->fn(cpu) != 0u || mem.mapped(addr_err) || !host.gl_maps.empty()) return false;

    // glDeleteBuffersARB unbinds and cleans up any active mapping for deleted buffer IDs.
    args({0x8892u, 40u}); bind_arb->fn(cpu);
    args({0x8892u, 0x88BAu}); uint32_t addr_del = map_arb->fn(cpu);
    if (!addr_del || !mem.mapped(addr_del)) return false;
    mem.w32(0x6100, 40u);
    args({1u, 0x6100u}); del_arb->fn(cpu);
    if (mem.mapped(addr_del) || !host.gl_maps.empty() || host.gl_bound_buffers.count(0x8892u)) return false;

    // GL_ELEMENT_ARRAY_BUFFER is VAO-local: vao1 -> 50, vao2 -> 60; each unmap must upload its own VAO's buffer.
    args({0x8893u, 0u}); bind_buf->fn(cpu);
    args({1u}); bind_vao->fn(cpu);
    args({0x8893u, 50u}); bind_buf->fn(cpu);
    args({0x8893u, 0x88BAu}); uint32_t addr_v1 = map_arb->fn(cpu);
    if (!addr_v1 || mem.r8(addr_v1) != 0x51) return false;
    mem.w8(addr_v1, 0xC1);
    args({2u}); bind_vao->fn(cpu);
    args({0x8893u, 60u}); bind_buf->fn(cpu);
    args({0x8893u, 0x88BAu}); uint32_t addr_v2 = map_arb->fn(cpu);
    if (!addr_v2 || mem.r8(addr_v2) != 0x61) return false;
    mem.w8(addr_v2, 0xC2);
    args({0x8893u}); if (unmap_arb->fn(cpu) != 1u) return false;
    if (host_buffers[60u][0] != 0xC2 || host_buffers[50u][0] != 0x51) return false;
    args({1u}); bind_vao->fn(cpu);
    args({0x8893u}); if (unmap_arb->fn(cpu) != 1u) return false;
    if (host_buffers[50u][0] != 0xC1 || host_buffers[60u][0] != 0xC2 || !host.gl_maps.empty()) return false;
    // Deleting the active VAO resets the active VAO and drops its element binding.
    args({2u}); bind_vao->fn(cpu);
    mem.w32(0x6100, 2u); args({1u, 0x6100u}); del_vao->fn(cpu);
    if (host.gl_bound_vao != 0 || host.gl_vao_element_buffers.count(2u) || host.gl_vao_element_buffers.count(1u) != 1) return false;
    args({0x8893u, 0x88BAu}); if (map_arb->fn(cpu) != 0) return false;
    args({1u}); bind_vao->fn(cpu); args({0u}); bind_vao->fn(cpu);

    // Rejected or invalid indexed binds must leave the previous binding intact so unmap still targets it.
    args({0x8A11u, 70u}); bind_buf->fn(cpu);
    host_range_calls = 0;
    args({0x8A11u, 99u, 80u, 0u, 4u}); bind_range->fn(cpu);       // host rejects: index out of range
    args({0x8A11u, 99u, 80u}); bind_base->fn(cpu);                // host rejects: index out of range
    args({0x8A11u, 99u, 80u, 0u}); bind_offset->fn(cpu);          // host rejects: index out of range
    if (host_range_calls != 3) return false;
    args({0x8A11u, 0u, 80u, 0u, 0u}); bind_range->fn(cpu);        // size <= 0: not forwarded
    args({0x8A11u, 0u, 80u, 0xFFFFFFFCu, 4u}); bind_range->fn(cpu); // offset < 0: not forwarded
    args({0x8A11u, 0u, 80u, 0xFFFFFFFCu}); bind_offset->fn(cpu);  // offset < 0: not forwarded
    if (host_range_calls != 3) return false;
    args({0x8A11u, 0x88BAu}); uint32_t addr_r = map_arb->fn(cpu);
    if (!addr_r || mem.r8(addr_r) != 0x71) return false;
    mem.w8(addr_r, 0xD1);
    args({0x8A11u}); if (unmap_arb->fn(cpu) != 1u) return false;
    if (host_buffers[70u][0] != 0xD1 || host_buffers[80u][0] != 0x81 || binding_mismatch) return false;
    // A valid range bind is accepted and retargets the shadow binding.
    args({0x8A11u, 0u, 80u, 0u, 4u}); bind_range->fn(cpu);
    args({0x8A11u, 0x88BAu}); addr_r = map_arb->fn(cpu);
    if (!addr_r || mem.r8(addr_r) != 0x81) return false;
    args({0x8A11u}); if (unmap_arb->fn(cpu) != 1u || binding_mismatch || host.gl_bound_buffers[0x8A11u] != 80u) return false;

    // Unbridged pointer-returning GL calls and void** output-pointer calls must fail closed
    // without calling host.gl_call or returning a truncated host pointer.
    for (const char* blocked : {
        "glFenceSync", "glMapNamedBuffer", "glMapNamedBufferEXT",
        "glMapNamedBufferRange", "glMapNamedBufferRangeEXT",
        "glCreateSyncFromCLeventARB", "glImportSyncEXT",
        "glGetPointerv", "glGetBufferPointerv"
    }) {
        auto fn = route(blocked);
        if (!fn) return false;
        host.err.clear(); mem.w32(cpu.fs_base + 0x34, 0);
        mem.w32(0x6000, 0xCAFEBABEu);
        args({1u, 0x6000u, 4u, 2u});
        uint32_t res = fn->fn(cpu);
        if (res != 0 || unbridged_host_called || mem.r32(cpu.fs_base + 0x34) != 120u ||
            mem.r32(0x6000) != 0xCAFEBABEu ||
            host.err.find(std::string("x86 GL gap: ") + blocked) == std::string::npos) return false;
    }
    return true;
}

int main(int argc, char** argv) {
    if (!verify_gl_host_errors_and_sizes()) { std::fprintf(stderr, "X86-GL FAIL host error or packed/matrix marshalling\n"); return 1; }
    if (!verify_gl_pointer_returns()) { std::fprintf(stderr, "X86-GL FAIL pointer-return marshalling or fail-closed guard\n"); return 1; }
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
    uintptr_t str_words[1] = {0x1F02u};
    uintptr_t stri_words[2] = {0x1F03u, 3u};
    adapter_ptr_return_ok =
        x86::gl_call_generated(signature("glGetString"), reinterpret_cast<uintptr_t>(&adapter_get_string), str_words) == uintptr_t(0x123456789abcdef0ull) &&
        x86::gl_call_generated(signature("glGetStringi"), reinterpret_cast<uintptr_t>(&adapter_get_stringi), stri_words) == uintptr_t(0x23456789abcdef08ull);
    auto file = read_file(argv[1]);
    if (file.empty()) { std::fprintf(stderr, "X86-GL FAIL fixture is empty\n"); return 1; }
    x86::Memory mem; x86::ThunkTable table; x86::Host host; x86::Registry reg;
    const uintptr_t host_window = uintptr_t(0x34567890u);
    const uintptr_t host_dc = uintptr_t(0x1234567887654321ull);
    const uintptr_t host_context = uintptr_t(0x23456789abcdef01ull);
    bool got_class = false, got_window = false, got_class_a = false, got_window_a = false, got_dc = false, got_context = false, destroyed_window = false;
    bool cleared = false, vertex_enabled = false, drew = false, drew_elements = false, buffer_drew = false, read_pixel = false;
    bool texture_uploaded = false, shader_created = false, shader_source_set = false, shader_compiled = false, buffer_uploaded = false, texture_generated = false;
    bool saw_float = false, saw_scalar = false, saw_pointer = false, saw_double = false, saw_int64 = false, saw_output_pointer = false, map_unmapped = false, map_arb_cross_a = false, map_arb_cross_b = false, map_arb_upload_err = false, saw_bind_buffer_arb = false;
    bool saw_get_string = false, saw_get_stringi = false, blocked_escaped = false;
    std::map<uint32_t, uint32_t> bound_buffers;
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
        auto set_cstr = [&](const char* s) {
            size_t n = std::strlen(s);
            q.output.assign(reinterpret_cast<const uint8_t*>(s), reinterpret_cast<const uint8_t*>(s) + n + 1);
            q.result = 1;
        };
        if (q.name == "glFenceSync" || q.name == "glMapNamedBuffer" || q.name == "glGetPointerv") { blocked_escaped = true; q.result = uintptr_t(0x123456789abcdef0ull); return; }
        if (q.name == "glGetString") {
            saw_get_string = true;
            if (q.args.size() == 1 && q.args[0] == 0x1F02u) set_cstr("4.6 Mesa 24.0");
            else if (q.args.size() == 1 && q.args[0] == 0x1F00u) set_cstr("Mesa");
            else if (q.args.size() == 1 && q.args[0] == 0x1F03u) set_cstr("");
            else q.result = 0;
            return;
        }
        if (q.name == "glGetStringi") {
            saw_get_stringi = true;
            if (q.args.size() == 2 && q.args[0] == 0x1F03u && q.args[1] == 0u) set_cstr("GL_ARB_vertex_buffer_object");
            else if (q.args.size() == 2 && q.args[0] == 0x1F03u && q.args[1] == 1u) set_cstr("GL_EXT_framebuffer_object");
            else q.result = 0;
            return;
        }
        if (q.name == "ChoosePixelFormat") { if (q.args.size() != 1 || q.args[0] != host_dc || q.input.size() != 40) return; q.result = 1; return; }
        if (q.name == "SetPixelFormat") { if (q.args.size() != 2 || q.args[0] != host_dc || q.args[1] != 1 || q.input.size() != 40) return; q.result = 1; return; }
        if (q.name == "wglCreateContext") { if (q.args.size() == 1 && q.args[0] == host_dc) q.result = host_context; return; }
        if (q.name == "wglMakeCurrent") {
            if (q.args.size() == 2 && q.args[0] == host_dc && q.args[1] == host_context) { got_context = true; q.result = 1; }
            else q.result = q.args.size() == 2 && q.args[0] == 0 && q.args[1] == 0;
            return;
        }
        if (q.name == "glBindBuffer" || q.name == "glBindBufferARB") {
            if (q.name == "glBindBufferARB") saw_bind_buffer_arb = true;
            if (q.args.size() == 2) bound_buffers[uint32_t(q.args[0])] = uint32_t(q.args[1]);
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
        if (q.name == "glMapBufferRange") { if (q.args.size() == 4 && q.args[0] == 0x8892u && bound_buffers[0x8892u] == 10u && q.args[2] == 4) q.output = {1, 2, 3, 4}; return; }
        if (q.name == "glMapBufferARB") {
            const uint32_t buf = q.args.size() == 2 ? bound_buffers[uint32_t(q.args[0])] : 0u;
            if (buf == 10u && q.args[1] == 0x88BAu) q.output = {0x11, 0x22, 0x33, 0x44};
            else if (buf == 20u && q.args[1] == 0x88BAu) q.output = {0xA1, 0xA2, 0xA3, 0xA4};
            else if (buf == 30u && q.args[1] == 0x88B9u) q.output = {0xE1, 0xE2, 0xE3, 0xE4};
            return;
        }
        if (q.name == "x86GlUnmapBuffer") {
            const uint32_t buf = q.args.size() == 2 ? bound_buffers[uint32_t(q.args[0])] : 0u;
            if (buf == 10u && q.input == std::vector<uint8_t>({0x7f, 2, 3, 4})) { map_unmapped = true; q.result = 1; return; }
            if (buf == 20u && q.args[0] == 0x8892u && q.input == std::vector<uint8_t>({0xB1, 0xA2, 0xA3, 0xB4})) { map_arb_cross_b = true; q.result = 1; return; }
            if (buf == 10u && q.args[0] == 0x8892u && q.input == std::vector<uint8_t>({0x55, 0x22, 0x33, 0x99})) { map_arb_cross_a = true; q.result = 1; return; }
            if (buf == 30u && q.input == std::vector<uint8_t>({0xF1, 0xE2, 0xE3, 0xE4})) { map_arb_upload_err = true; q.result = 0; return; }
            q.result = 0; return;
        }
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
    std::string tee_log;
    auto result = x86::run_image(file, "x86-gl.exe", "x86-gl.exe", [&](const std::string& line) { tee_log += line; }, nullptr, 20'000'000, nullptr, false,
        nullptr, nullptr, [&](x86::Host& configured) { configured.register_window_class = host.register_window_class; configured.create_window = host.create_window;
            configured.destroy_window = host.destroy_window; configured.get_dc = host.get_dc; configured.release_dc = host.release_dc; configured.gl_call = host.gl_call; });
    const bool gaps_logged =
        tee_log.find("x86 GL gap: glFenceSync") != std::string::npos &&
        tee_log.find("x86 GL gap: glMapNamedBuffer") != std::string::npos &&
        tee_log.find("x86 GL gap: glGetPointerv") != std::string::npos;
    if (!result.loaded || !result.finished || result.exit_code != 0 || !got_class || !got_window || !got_class_a || !got_window_a || !got_dc || !got_context || !drew || !drew_elements || !buffer_drew || !read_pixel || !destroyed_window || !texture_uploaded || !shader_created || !shader_source_set || !shader_compiled || !buffer_uploaded || !texture_generated ||
        !saw_float || !saw_scalar || !saw_pointer || !saw_double || !saw_int64 || !saw_output_pointer || !map_unmapped || !map_arb_cross_a || !map_arb_cross_b || !map_arb_upload_err || !saw_bind_buffer_arb || !saw_get_string || !saw_get_stringi || blocked_escaped || !gaps_logged ||
        !adapter_float_ok || !adapter_double_ok || !adapter_int64_ok || !adapter_pointer_ok || !adapter_output_ok || !adapter_return_ok || !adapter_ptr_return_ok) {
        std::fprintf(stderr, "X86-GL FAIL loaded=%d finished=%d exit=%u fault=%s class=%d window=%d dc=%d context=%d draw=%d read=%d destroy=%d tex=%d shader=%d/%d/%d buffer=%d output=%d strings=%d/%d map_arb=%d/%d/%d bind_arb=%d gaps=%d escaped=%d adapters=%d/%d/%d/%d\n",
            result.loaded, result.finished, result.exit_code, result.fault.c_str(), got_class, got_window, got_dc, got_context, drew, read_pixel, destroyed_window, texture_uploaded, shader_created, shader_source_set, shader_compiled, buffer_uploaded, saw_output_pointer, saw_get_string, saw_get_stringi, map_arb_cross_a, map_arb_cross_b, map_arb_upload_err, saw_bind_buffer_arb, gaps_logged, blocked_escaped, adapter_float_ok, adapter_double_ok, adapter_int64_ok, adapter_ptr_return_ok);
        return 1;
    }
    std::puts("X86-GL PASS: PE32 WGL context, guest-memory triangle array, pointer-return marshalling, and readback pixel");
    return 0;
}
