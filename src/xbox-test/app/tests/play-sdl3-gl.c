// SPDX-License-Identifier: GPL-3.0-or-later
// Owned SDL3 program run through Play: the OpenGL path an SDL3 GL game takes
// (SDL_GL_LoadLibrary -> OPENGL32.DLL, a window with SDL_WINDOW_OPENGL, a
// context, then a cleared frame presented with SDL_GL_SwapWindow). Stages are
// reported so a failure names the first missing piece:
//   exit 42  frame presented ("PLAY-SDL3-GL PASS frame-presented")
//   exit 43  OPENGL32.DLL loaded but no GL window/context/driver: BLOCKED, never a pass
//   other    a stage before that failed (the SDL error is printed)
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>
#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef float GLfloat;
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
typedef const unsigned char* (APIENTRY* PFN_glGetString)(GLenum);
typedef void (APIENTRY* PFN_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY* PFN_glClear)(GLbitfield);
typedef void (APIENTRY* PFN_glFinish)(void);
static int fail(int code, const char* stage) {
    printf("PLAY-SDL3-GL FAIL %s: %s\n", stage, SDL_GetError());
    fflush(stdout);
    return code;
}
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return fail(10, "SDL_Init");
    if (!SDL_GL_LoadLibrary(NULL)) return fail(20, "SDL_GL_LoadLibrary");
    printf("PLAY-SDL3-GL STAGE gl-library-loaded\n");
    SDL_Window* window = SDL_CreateWindow("play-sdl3-gl", 640, 360, SDL_WINDOW_OPENGL);
    if (!window) {
        printf("PLAY-SDL3-GL BLOCKED window-creation: %s\n", SDL_GetError());
        fflush(stdout);
        return 43;
    }
    printf("PLAY-SDL3-GL STAGE window-created\n");
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        printf("PLAY-SDL3-GL BLOCKED context-creation: %s\n", SDL_GetError());
        fflush(stdout);
        return 43;
    }
    if (!SDL_GL_MakeCurrent(window, context)) return fail(30, "SDL_GL_MakeCurrent");
    PFN_glGetString getString = (PFN_glGetString)SDL_GL_GetProcAddress("glGetString");
    PFN_glClearColor clearColor = (PFN_glClearColor)SDL_GL_GetProcAddress("glClearColor");
    PFN_glClear clear = (PFN_glClear)SDL_GL_GetProcAddress("glClear");
    PFN_glFinish finish = (PFN_glFinish)SDL_GL_GetProcAddress("glFinish");
    if (!getString || !clearColor || !clear || !finish) return fail(31, "GL entry points");
    const char* renderer = (const char*)getString(GL_RENDERER);
    const char* version = (const char*)getString(GL_VERSION);
    printf("PLAY-SDL3-GL STAGE context-current renderer=\"%s\" version=\"%s\"\n", renderer ? renderer : "?", version ? version : "?");
    fflush(stdout);
    for (int frame = 0; frame < 3; ++frame) {
        clearColor(0.0f, 0.4f, 0.0f, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        if (!SDL_GL_SwapWindow(window)) return fail(32, "SDL_GL_SwapWindow");
    }
    finish();
    printf("PLAY-SDL3-GL PASS frame-presented\n");
    fflush(stdout);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 42;
}
