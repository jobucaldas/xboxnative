// SPDX-License-Identifier: GPL-3.0-or-later
// Owned SDL3 program run through launcher Play with the official SDL3.dll, as
// an SDL3 game would: video + events + gamepad init, a window, the Direct3D 11
// renderer, frames presented, events polled, clean shutdown. Exit 42 = pass;
// other codes name the first failing stage (the SDL error is printed).
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
static int fail(int code, const char* stage) {
    printf("PLAY-SDL3 FAIL %s: %s\n", stage, SDL_GetError());
    fflush(stdout);
    return code;
}
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) return fail(10, "SDL_Init");
    int displays = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&displays);
    if (!ids || displays < 1) return fail(11, "SDL_GetDisplays");
    const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(ids[0]);
    if (!mode || mode->w <= 0 || mode->h <= 0) return fail(12, "SDL_GetDesktopDisplayMode");
    SDL_free(ids);
    SDL_Window* window = SDL_CreateWindow("play-sdl3", 640, 360, 0);
    if (!window) return fail(20, "SDL_CreateWindow");
    SDL_Renderer* renderer = SDL_CreateRenderer(window, "direct3d11");
    if (!renderer) return fail(21, "SDL_CreateRenderer(direct3d11)");
    int pads = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&pads);
    if (!gamepads) return fail(22, "SDL_GetGamepads");
    SDL_free(gamepads);
    for (int frame = 0; frame < 60; ++frame) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) if (event.type == SDL_EVENT_QUIT) return fail(30, "unexpected quit");
        SDL_SetRenderDrawColor(renderer, 14, 17, 22, 255);
        if (!SDL_RenderClear(renderer)) return fail(31, "SDL_RenderClear");
        SDL_FRect box = {20.0f + frame * 8.0f, 120.0f, 120.0f, 120.0f};
        SDL_SetRenderDrawColor(renderer, 64, 160, 255, 255);
        if (!SDL_RenderFillRect(renderer, &box)) return fail(32, "SDL_RenderFillRect");
        if (!SDL_RenderPresent(renderer)) return fail(33, "SDL_RenderPresent");
        SDL_Delay(16);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("PLAY-SDL3 PASS displays=%d desktop=%dx%d pads=%d\n", displays, mode ? mode->w : 0, mode ? mode->h : 0, pads);
    fflush(stdout);
    return 42;
}
