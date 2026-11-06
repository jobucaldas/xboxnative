// SPDX-License-Identifier: GPL-3.0-or-later
// Owned SDL2 program run through launcher Play with the official SDL2.dll, as
// an SDL2 game would: video + events + game controller init, a window, the
// Direct3D 11 renderer, frames presented, events polled, clean shutdown.
// Exit 42 = pass; other codes name the first failing stage.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
static int fail(int code, const char* stage) {
    printf("PLAY-SDL2 FAIL %s: %s\n", stage, SDL_GetError());
    fflush(stdout);
    return code;
}
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) return fail(10, "SDL_Init");
    if (SDL_GetNumVideoDisplays() < 1) return fail(11, "SDL_GetNumVideoDisplays");
    SDL_DisplayMode mode;
    SDL_zero(mode);
    int got = SDL_GetDesktopDisplayMode(0, &mode);
    if (got != 0 || mode.w <= 0 || mode.h <= 0) {
        printf("PLAY-SDL2 desktop mode result=%d %dx%d@%d\n", got, mode.w, mode.h, mode.refresh_rate);
        return fail(12, "SDL_GetDesktopDisplayMode");
    }
    SDL_Window* window = SDL_CreateWindow("play-sdl2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 360, 0);
    if (!window) return fail(20, "SDL_CreateWindow");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) return fail(21, "SDL_CreateRenderer(direct3d11)");
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(renderer, &info) != 0 || SDL_strcmp(info.name, "direct3d11")) return fail(22, "renderer is not direct3d11");
    int pads = SDL_NumJoysticks();
    if (pads < 0) return fail(23, "SDL_NumJoysticks");
    for (int frame = 0; frame < 60; ++frame) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) if (event.type == SDL_QUIT) return fail(30, "unexpected quit");
        SDL_SetRenderDrawColor(renderer, 14, 17, 22, 255);
        if (SDL_RenderClear(renderer) != 0) return fail(31, "SDL_RenderClear");
        SDL_Rect box = {20 + frame * 8, 120, 120, 120};
        SDL_SetRenderDrawColor(renderer, 255, 160, 64, 255);
        if (SDL_RenderFillRect(renderer, &box) != 0) return fail(32, "SDL_RenderFillRect");
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("PLAY-SDL2 PASS desktop=%dx%d pads=%d\n", mode.w, mode.h, pads);
    fflush(stdout);
    return 42;
}
