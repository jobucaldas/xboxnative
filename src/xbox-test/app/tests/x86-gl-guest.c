// SPDX-License-Identifier: GPL-3.0-or-later
// i686 PE32 fixture: exercises WGL setup and client-side array marshalling.
#include <windows.h>
#include <GL/gl.h>

static int fail(unsigned code) { ExitProcess(code); return (int)code; }

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
    typedef void *(APIENTRY *MapRange)(GLenum, long, long, GLbitfield);
    typedef GLboolean (APIENTRY *UnmapBuffer)(GLenum);
    typedef void (APIENTRY *BindBuffer)(GLenum, GLuint);
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
    MapRange map_range = (MapRange)wglGetProcAddress("glMapBufferRange");
    UnmapBuffer unmap_buffer = (UnmapBuffer)wglGetProcAddress("glUnmapBuffer");
    BindBuffer bind_buffer = (BindBuffer)wglGetProcAddress("glBindBuffer");
    if (!clear_color || !clear || !enable || !vertex || !draw || !draw_elements || !read || !clear_depth || !get_integer || !uniform_handle || !map_range || !unmap_buffer || !bind_buffer) fail(14);

    clear_depth(1.0);
    GLint max_texture = 0;
    get_integer(GL_MAX_TEXTURE_SIZE, &max_texture);
    uniform_handle(0, 0x12345678abcdef01ull);
    unsigned char *mapped = (unsigned char *)map_range(0x8892u, 0, 4, 0x0002u);
    if (!mapped) fail(17);
    mapped[0] = 0x7f;
    if (!unmap_buffer(0x8892u)) fail(18);
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
    vertex(3, GL_FLOAT, 0, (const GLvoid *)0);
    draw(GL_TRIANGLES, 0, 3);
    bind_buffer(0x8892u, 0);
    if (!SwapBuffers(dc) || !wglMakeCurrent(NULL, NULL) || !wglDeleteContext(context) || !ReleaseDC(window, dc) || !DestroyWindow(window)) fail(16);
    ExitProcess(0);
}
