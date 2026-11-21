// SPDX-License-Identifier: GPL-3.0-or-later
// i686 PE32 fixture: exercises WGL setup and client-side array marshalling.
#include <windows.h>
#include <GL/gl.h>

static int fail(unsigned code) { ExitProcess(code); return (int)code; }

static int streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static LRESULT CALLBACK guest_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    (void)window; (void)message; (void)wparam; (void)lparam; return 0;
}

void WINAPI entry(void) {
    WNDCLASSEXA wca = { sizeof(wca), 0, guest_window_proc, 0, 0, NULL, NULL, NULL, NULL, NULL, "X86GLWindowA", NULL };
    if (!RegisterClassExA(&wca)) fail(9);
    HWND window_a = CreateWindowExA(0, wca.lpszClassName, "x86 GL test", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
        NULL, NULL, wca.hInstance, NULL);
    if (!window_a || !DestroyWindow(window_a)) fail(10);
    WNDCLASSEXW wc = { sizeof(wc), 0, guest_window_proc, 0, 0, NULL, NULL, NULL, NULL, NULL, L"X86GLWindow", NULL };
    if (!RegisterClassExW(&wc)) fail(10);
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"x86 GL test", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
        NULL, NULL, wc.hInstance, NULL);
    if (!window) fail(11);
    HDC dc = GetDC(window);
    if (!dc) fail(12);
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 24, 8, 0,
        PFD_MAIN_PLANE, 0, 0, 0, 0 };
    int format = ChoosePixelFormat(dc, &pfd);
    if (!format || !SetPixelFormat(dc, format, &pfd)) fail(12);
    HGLRC context = wglCreateContext(dc);
    if (!context || !wglMakeCurrent(dc, context)) fail(13);

    typedef void (APIENTRY *ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    typedef void (APIENTRY *Clear)(GLbitfield);
    typedef void (APIENTRY *EnableClientState)(GLenum);
    typedef void (APIENTRY *VertexPointer)(GLint, GLenum, GLsizei, const GLvoid *);
    typedef void (APIENTRY *DrawArrays)(GLenum, GLint, GLsizei);
    typedef void (APIENTRY *DrawElements)(GLenum, GLsizei, GLenum, const GLvoid *);
    typedef void (APIENTRY *ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid *);
    typedef void (APIENTRY *ClearDepth)(GLdouble);
    typedef void (APIENTRY *GetInteger)(GLenum, GLint *);
    typedef void (APIENTRY *UniformHandle)(GLint, unsigned long long);
    typedef const GLubyte *(APIENTRY *GetStringFn)(GLenum);
    typedef const GLubyte *(APIENTRY *GetStringiFn)(GLenum, GLuint);
    typedef void *(APIENTRY *MapBufferFn)(GLenum, GLenum);
    typedef void *(APIENTRY *MapRange)(GLenum, long, long, GLbitfield);
    typedef GLboolean (APIENTRY *UnmapBuffer)(GLenum);
    typedef void *(APIENTRY *FenceSyncFn)(GLenum, GLbitfield);
    typedef void *(APIENTRY *MapNamedBufferFn)(GLuint, GLenum);
    typedef void (APIENTRY *GetPointervFn)(GLenum, void **);
    typedef void (APIENTRY *BindBuffer)(GLenum, GLuint);
    typedef void (APIENTRY *BufferData)(GLenum, long, const GLvoid *, GLenum);
    typedef void (APIENTRY *GenTextures)(GLsizei, GLuint *);
    typedef void (APIENTRY *TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
    typedef GLuint (APIENTRY *CreateShader)(GLenum);
    typedef void (APIENTRY *ShaderSource)(GLuint, GLsizei, const char *const *, const GLint *);
    typedef void (APIENTRY *CompileShader)(GLuint);
    ClearColor clear_color = (ClearColor)wglGetProcAddress("glClearColor");
    Clear clear = (Clear)wglGetProcAddress("glClear");
    EnableClientState enable = (EnableClientState)wglGetProcAddress("glEnableClientState");
    VertexPointer vertex = (VertexPointer)wglGetProcAddress("glVertexPointer");
    DrawArrays draw = (DrawArrays)wglGetProcAddress("glDrawArrays");
    DrawElements draw_elements = (DrawElements)wglGetProcAddress("glDrawElements");
    ReadPixels read = (ReadPixels)wglGetProcAddress("glReadPixels");
    ClearDepth clear_depth = (ClearDepth)wglGetProcAddress("glClearDepth");
    GetInteger get_integer = (GetInteger)wglGetProcAddress("glGetIntegerv");
    UniformHandle uniform_handle = (UniformHandle)wglGetProcAddress("glUniformHandleui64ARB");
    GetStringFn get_string = (GetStringFn)wglGetProcAddress("glGetString");
    GetStringiFn get_stringi = (GetStringiFn)wglGetProcAddress("glGetStringi");
    MapBufferFn map_buffer_arb = (MapBufferFn)wglGetProcAddress("glMapBufferARB");
    MapRange map_range = (MapRange)wglGetProcAddress("glMapBufferRange");
    UnmapBuffer unmap_buffer = (UnmapBuffer)wglGetProcAddress("glUnmapBuffer");
    UnmapBuffer unmap_buffer_arb = (UnmapBuffer)wglGetProcAddress("glUnmapBufferARB");
    FenceSyncFn fence_sync = (FenceSyncFn)wglGetProcAddress("glFenceSync");
    MapNamedBufferFn map_named_buffer = (MapNamedBufferFn)wglGetProcAddress("glMapNamedBuffer");
    GetPointervFn get_pointerv = (GetPointervFn)wglGetProcAddress("glGetPointerv");
    BindBuffer bind_buffer = (BindBuffer)wglGetProcAddress("glBindBuffer");
    BindBuffer bind_buffer_arb = (BindBuffer)wglGetProcAddress("glBindBufferARB");
    BufferData buffer_data = (BufferData)wglGetProcAddress("glBufferData");
    GenTextures gen_textures = (GenTextures)wglGetProcAddress("glGenTextures");
    TexImage2D tex_image = (TexImage2D)wglGetProcAddress("glTexImage2D");
    CreateShader create_shader = (CreateShader)wglGetProcAddress("glCreateShader");
    ShaderSource shader_source = (ShaderSource)wglGetProcAddress("glShaderSource");
    CompileShader compile_shader = (CompileShader)wglGetProcAddress("glCompileShader");
    if (!clear_color || !clear || !enable || !vertex || !draw || !draw_elements || !read || !clear_depth || !get_integer || !uniform_handle || !get_string || !get_stringi || !map_buffer_arb || !map_range || !unmap_buffer || !unmap_buffer_arb || !fence_sync || !map_named_buffer || !get_pointerv || !bind_buffer || !bind_buffer_arb || !buffer_data || !gen_textures || !tex_image || !create_shader || !shader_source || !compile_shader) fail(14);

    const char *ver1 = (const char *)get_string(GL_VERSION);
    const char *ver2 = (const char *)get_string(GL_VERSION);
    const char *vendor = (const char *)get_string(GL_VENDOR);
    const char *empty_ext = (const char *)get_string(GL_EXTENSIONS);
    if (!streq(ver1, "4.6 Mesa 24.0") || ver1 != ver2) fail(21);
    if (!streq(vendor, "Mesa") || vendor == ver1) fail(22);
    if (!empty_ext || empty_ext[0] != '\0') fail(23);
    if (get_string(0xDEADu) != NULL) fail(24);
    const char *ext0 = (const char *)get_stringi(GL_EXTENSIONS, 0);
    const char *ext0_again = (const char *)get_stringi(GL_EXTENSIONS, 0);
    const char *ext1 = (const char *)get_stringi(GL_EXTENSIONS, 1);
    if (!streq(ext0, "GL_ARB_vertex_buffer_object") || ext0 != ext0_again) fail(25);
    if (!streq(ext1, "GL_EXT_framebuffer_object") || ext1 == ext0) fail(26);
    if (get_stringi(GL_EXTENSIONS, 2) != NULL) fail(27);

    clear_depth(1.0);
    static const unsigned char texture_pixel[4] = { 0x12, 0x34, 0x56, 0xff };
    GLuint generated_texture = 0;
    gen_textures(1, &generated_texture);
    if (generated_texture != 0x1234u) fail(20);
    tex_image(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture_pixel);
    GLuint shader = create_shader(0x8B31u);
    const char *shader_text[] = { "void main() {", " gl_Position = gl_Vertex; }" };
    shader_source(shader, 2, shader_text, NULL);
    compile_shader(shader);
    GLint max_texture = 0;
    get_integer(GL_MAX_TEXTURE_SIZE, &max_texture);
    if (max_texture != 8192) fail(19);
    uniform_handle(0, 0x12345678abcdef01ull);

    // Mapping with no buffer bound (0) must be rejected.
    if (map_buffer_arb(0x8892u, 0x88BAu) != NULL) fail(33);
    bind_buffer(0x8892u, 10);
    // Invalid access enum must be rejected cleanly without mapping.
    if (map_buffer_arb(0x8892u, 0x1234u) != NULL) fail(34);
    unsigned char *mapped = (unsigned char *)map_range(0x8892u, 0, 4, 0x0002u);
    if (!mapped) fail(17);
    mapped[0] = 0x7f;
    if (!unmap_buffer(0x8892u)) fail(18);

    // Cross-target identity: map buffer 10 on ARRAY_BUFFER and buffer 20 on ELEMENT_ARRAY_BUFFER,
    // then bind buffer 20 to ARRAY_BUFFER via glBindBufferARB and unmap -> must upload buffer 20's bytes.
    unsigned char *mapped_a = (unsigned char *)map_buffer_arb(0x8892u, 0x88BAu);
    if (!mapped_a || mapped_a[0] != 0x11 || mapped_a[3] != 0x44) fail(28);
    mapped_a[0] = 0x55;
    mapped_a[3] = 0x99;
    bind_buffer_arb(0x8893u, 20);
    unsigned char *mapped_b = (unsigned char *)map_buffer_arb(0x8893u, 0x88BAu);
    if (!mapped_b || mapped_b[0] != 0xA1 || mapped_b[3] != 0xA4) fail(35);
    mapped_b[0] = 0xB1;
    mapped_b[3] = 0xB4;
    bind_buffer_arb(0x8892u, 20);
    if (!unmap_buffer_arb(0x8892u)) fail(29);
    bind_buffer(0x8892u, 10);
    if (!unmap_buffer_arb(0x8892u)) fail(36);
    bind_buffer(0x8893u, 0);

    // Copy-back upload failure on unmap must return GL_FALSE (0).
    bind_buffer_arb(0x8892u, 30);
    unsigned char *mapped_err = (unsigned char *)map_buffer_arb(0x8892u, 0x88B9u);
    if (!mapped_err || mapped_err[0] != 0xE1) fail(37);
    mapped_err[0] = 0xF1;
    if (unmap_buffer_arb(0x8892u) != 0) fail(38);
    bind_buffer(0x8892u, 0);

    SetLastError(0);
    if (fence_sync(0x9117u, 0) != NULL || GetLastError() != 120u) fail(30);
    SetLastError(0);
    if (map_named_buffer(1u, 0x88BAu) != NULL || GetLastError() != 120u) fail(31);
    SetLastError(0);
    void *sentinel = (void *)0x12345678u;
    get_pointerv(0x808Eu, &sentinel);
    if (GetLastError() != 120u || sentinel != (void *)0x12345678u) fail(32);

    clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT);
    static const GLfloat vertices[] = { -0.8f, -0.8f, 0.0f, 0.8f, -0.8f, 0.0f, 0.0f, 0.8f, 0.0f };
    enable(GL_VERTEX_ARRAY);
    vertex(3, GL_FLOAT, 0, vertices);
    draw(GL_TRIANGLES, 0, 3);
    static const GLushort indices[] = { 0, 1, 2 };
    draw_elements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, indices);
    BYTE pixel[4] = { 0, 0, 0, 0 };
    read(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 0x12 || pixel[1] != 0x34 || pixel[2] != 0x56 || pixel[3] != 0xff) fail(15);
    bind_buffer(0x8892u, 1);
    static const unsigned char buffer_bytes[4] = { 0xaa, 0xbb, 0xcc, 0xdd };
    buffer_data(0x8892u, 4, buffer_bytes, 0x88E4u);
    vertex(3, GL_FLOAT, 0, (const GLvoid *)0);
    draw(GL_TRIANGLES, 0, 3);
    bind_buffer(0x8892u, 0);
    if (!SwapBuffers(dc) || !wglMakeCurrent(NULL, NULL) || !wglDeleteContext(context) || !ReleaseDC(window, dc) || !DestroyWindow(window)) fail(16);
    ExitProcess(0);
}
