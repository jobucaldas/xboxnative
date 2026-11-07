// SPDX-License-Identifier: GPL-3.0-or-later
// Owned SDL3 program run through Play: the OpenGL path an SDL3 GL game takes
// (SDL_GL_LoadLibrary -> OPENGL32.DLL, a window with SDL_WINDOW_OPENGL, then a
// context). Stages are reported so a failure names the first missing piece:
//   exit 42  context created and made current (full OpenGL: not yet supported)
//   exit 43  OPENGL32.DLL loaded but no GL window/context: the expected state
//            until Play ships an OpenGL driver (no GL pixel formats)
//   other    a stage before that failed (the SDL error is printed)
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
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
    printf("PLAY-SDL3-GL PASS context-current\n");
    fflush(stdout);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 42;
}
