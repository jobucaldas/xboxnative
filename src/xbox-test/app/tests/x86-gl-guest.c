// SPDX-License-Identifier: GPL-3.0-or-later
// i686 PE32 fixture: exercises WGL setup and client-side array marshalling.
#include <windows.h>
#include <GL/gl.h>

static int fail(unsigned code) { ExitProcess(code); return (int)code; }

void WINAPI entry(void) {
    HDC dc = GetDC(NULL);
    if (!dc) fail(11);
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
    typedef void (APIENTRY *ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid *);
    ClearColor clear_color = (ClearColor)wglGetProcAddress("glClearColor");
    Clear clear = (Clear)wglGetProcAddress("glClear");
    EnableClientState enable = (EnableClientState)wglGetProcAddress("glEnableClientState");
    VertexPointer vertex = (VertexPointer)wglGetProcAddress("glVertexPointer");
    DrawArrays draw = (DrawArrays)wglGetProcAddress("glDrawArrays");
    ReadPixels read = (ReadPixels)wglGetProcAddress("glReadPixels");
    if (!clear_color || !clear || !enable || !vertex || !draw || !read) fail(14);

    clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT);
    static const GLfloat vertices[] = { -0.8f, -0.8f, 0.0f, 0.8f, -0.8f, 0.0f, 0.0f, 0.8f, 0.0f };
    enable(GL_VERTEX_ARRAY);
    vertex(3, GL_FLOAT, 0, vertices);
    draw(GL_TRIANGLES, 0, 3);
    BYTE pixel[4] = { 0, 0, 0, 0 };
    read(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != 0x12 || pixel[1] != 0x34 || pixel[2] != 0x56 || pixel[3] != 0xff) fail(15);
    if (!SwapBuffers(dc) || !wglMakeCurrent(NULL, NULL) || !wglDeleteContext(context) || !ReleaseDC(NULL, dc)) fail(16);
    ExitProcess(0);
}
