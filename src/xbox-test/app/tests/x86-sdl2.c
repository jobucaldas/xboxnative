// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the pinned official i686 SDL2 DLL through the PE32 interpreter.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>

#ifndef SDL2TEST_INIT_MASK
#define SDL2TEST_INIT_MASK (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS)
#endif

static int fail(int code, const char* stage) {
    printf("SDL2TEST FAIL stage=%s error=%s\n", stage, SDL_GetError());
    fflush(stdout);
    return code;
}

int main(void) {
    puts("SDL2TEST STEP main entered"); fflush(stdout);
    SDL_SetMainReady();
    if (SDL_Init(SDL2TEST_INIT_MASK) != 0)
        return fail(10, "SDL_Init");
    puts("SDL2TEST STEP SDL_Init ok"); fflush(stdout);

    SDL_Window* window = SDL_CreateWindow("x86-sdl2", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 96, 64, SDL_WINDOW_HIDDEN);
    if (!window) return fail(20, "SDL_CreateWindow");
    puts("SDL2TEST STEP SDL_CreateWindow ok"); fflush(stdout);

    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d");
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) return fail(21, "SDL_CreateRenderer(direct3d)");
    puts("SDL2TEST STEP SDL_CreateRenderer ok"); fflush(stdout);

    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(renderer, &info) != 0 || SDL_strcmp(info.name, "direct3d") != 0)
        return fail(22, "SDL_GetRendererInfo");

    SDL_AudioSpec wanted;
    SDL_zero(wanted);
    wanted.freq = 44100;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples = 512;
    SDL_AudioSpec obtained;
    SDL_zero(obtained);
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, 0);
    if (audio) {
        SDL_PauseAudioDevice(audio, 1);
        SDL_CloseAudioDevice(audio);
        printf("SDL2TEST AUDIO available freq=%d channels=%u\n", obtained.freq, obtained.channels);
    } else {
        const char* error = SDL_GetError();
        if (!error || !*error) return fail(23, "SDL_OpenAudioDevice missing error");
        printf("SDL2TEST AUDIO unavailable error=%s\n", error);
    }
    fflush(stdout);

    int joysticks = SDL_NumJoysticks();
    if (joysticks < 0) return fail(24, "SDL_NumJoysticks");
    printf("SDL2TEST STEP SDL_NumJoysticks=%d\n", joysticks); fflush(stdout);
    int controllers = 0;
    for (int i = 0; i < joysticks; ++i) {
        if (!SDL_IsGameController(i)) continue;
        SDL_GameController* controller = SDL_GameControllerOpen(i);
        if (controller) {
            ++controllers;
            SDL_GameControllerClose(controller);
        }
    }

    if (SDL_SetRenderDrawColor(renderer, 255, 32, 16, 255) != 0)
        return fail(30, "SDL_SetRenderDrawColor");
    if (SDL_RenderClear(renderer) != 0) return fail(31, "SDL_RenderClear");
    puts("SDL2TEST STEP SDL_RenderClear ok"); fflush(stdout);
    Uint32 pixel = 0;
    if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, &pixel, sizeof(pixel)) != 0)
        return fail(32, "SDL_RenderReadPixels");
    printf("SDL2TEST STEP SDL_RenderReadPixels=0x%08lx\n", (unsigned long)pixel); fflush(stdout);
    SDL_RenderPresent(renderer);

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) return fail(33, "unexpected SDL_QUIT");
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("SDL2TEST PASS renderer=%s pixel=0x%08lx joysticks=%d controllers=%d\n",
        info.name, (unsigned long)pixel, joysticks, controllers);
    fflush(stdout);
    return 42;
}
