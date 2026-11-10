// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// In-process host for Valve's signed Steam client (steamclient64.dll).
//
// Play runs an owned game inside the launcher process; there is no steam.exe to
// own the account's global user, and no way to spawn one (AGENTS.md rule 3 keeps
// the client genuine, and Play refuses guest processes). Valve's client exports
// the same engine entry points steam.exe itself calls, so the launcher can host
// the engine in its own process: create the global user, create a pipe, connect
// to that user. The game's own steam_api64.dll then does what it always does —
// CreateSteamPipe + ConnectToGlobalUser — and finds a live user.
//
// Off-device evidence for the shape (Wine 11.0, the pinned client package):
//   LoadLibrary steamclient64.dll        -> loaded
//   CreateInterface("SteamClient021")    -> object
//   Steam_CreateGlobalUser               -> 1 (hUser)
//   Steam_CreateSteamPipe                -> 2 (hPipe)
//   Steam_ConnectToGlobalUser            -> 0 (no logged-on user yet)
// and the client's own log says "no bootstrapper found" until the host module
// exports GetBootstrapperVersion; with it exported the same run logs
// "Client version: ...". So the client resolves a small bootstrapper surface
// from the host module before it will dial a CM. Both facts are encoded here.
//
// Portable: every client call is injected, so the sequence, the retry policy
// and the failure reporting are tested natively (app/tests/steam_host_test.cpp).
// See research/STEAM-REAL-ONLINE.md for the plan and the open gates.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace steam_host {

// The interface version the client's own launcher asks for.
inline constexpr const char* ClientInterface = "SteamClient021";

// Names the client resolves from the host module before it will connect. The
// first is the version probe (without it the client logs "no bootstrapper
// found" and never dials a CM); the other two are the universe and logging
// directory it expects the host to supply.
inline constexpr const char* BootstrapperExports[] = {
    "GetBootstrapperVersion",
    "SteamBootstrapper_GetEUniverse",
    "SteamBootstrapper_GetLoggingDir",
};
inline constexpr size_t BootstrapperExportCount =
    sizeof(BootstrapperExports) / sizeof(BootstrapperExports[0]);

// The client entry points the host drives, in the order steam.exe uses them.
// Each is a plain function pointer so the header stays free of windows.h and
// the test can supply its own.
struct Api {
    // CreateInterface(version, &result) -> interface object, or null.
    std::function<void*(const char* version, int* result)> create_interface;
    // Steam_CreateGlobalUser(&hUser) -> non-zero on success.
    std::function<int(int*)> create_global_user;
    // Steam_CreateSteamPipe() -> pipe handle, 0 on failure.
    std::function<int()> create_pipe;
    // Steam_ConnectToGlobalUser(hPipe) -> user handle, 0 while no user is
    // logged on (this is the call that fails until the host has a session).
    std::function<unsigned(int)> connect_to_global_user;
    // Steam_BLoggedOn(hUser) / Steam_BConnected(hPipe).
    std::function<bool(unsigned)> blogged_on;
    std::function<bool(unsigned)> bconnected;
    // Steam_BReleaseSteamPipe(hPipe).
    std::function<int(int)> release_pipe;
    std::function<void(const std::string&)> log;
};

// How far bring_up() got. The stage names the first step that did not
// complete, so a console log says exactly which client call refused.
enum class Stage { None, Interface, GlobalUser, Pipe, Connected, Failed };

inline const char* stage_name(Stage s) {
    switch (s) {
        case Stage::None: return "none";
        case Stage::Interface: return "interface";
        case Stage::GlobalUser: return "global-user";
        case Stage::Pipe: return "pipe";
        case Stage::Connected: return "connected";
        case Stage::Failed: return "failed";
    }
    return "?";
}

struct Result {
    Stage stage = Stage::None;
    int hUser = 0;
    int hPipe = 0;
    unsigned hConnected = 0;
    bool logged_on = false;
    bool connected = false;
    std::string error;
    bool ok() const { return stage == Stage::Connected && hConnected != 0; }
};

// Brings the client up in this process: interface, global user, pipe, then
// connects to the global user. `attempts` bounds the connect retries (the
// connect answers 0 until the engine has a session, so one attempt is not
// enough); `wait_ms` is the pause between them, and `sleep` is injected so the
// test does not wait. On failure the pipe is released and `error` names the
// call and the value it returned.
inline Result bring_up(Api& api, int attempts = 10, int wait_ms = 500,
                       std::function<void(int)> sleep = {}) {
    Result out;
    if (!api.create_interface) { out.stage = Stage::Failed; out.error = "no client module"; return out; }
    if (!api.create_global_user || !api.create_pipe || !api.connect_to_global_user) {
        out.stage = Stage::Failed; out.error = "client module is missing an engine entry point"; return out;
    }
    auto log = [&](const std::string& line) { if (api.log) api.log(line); };
    auto fail = [&](Stage stage, const std::string& why) {
        out.stage = Stage::Failed; out.error = why;
        log("steam host: " + why + " (stage " + stage_name(stage) + ")");
        if (out.hPipe && api.release_pipe) api.release_pipe(out.hPipe);
        out.hPipe = 0;
        return out;
    };

    int result = 0;
    void* client = api.create_interface(ClientInterface, &result);
    out.stage = Stage::Interface;
    if (!client) return fail(Stage::Interface, std::string("CreateInterface(") + ClientInterface + ") returned null");
    log("steam host: client interface up");

    int hUser = 0;
    if (!api.create_global_user(&hUser) || hUser == 0)
        return fail(Stage::GlobalUser, "Steam_CreateGlobalUser refused");
    out.hUser = hUser;
    out.stage = Stage::GlobalUser;
    log("steam host: global user created");

    int hPipe = api.create_pipe();
    if (hPipe == 0) return fail(Stage::Pipe, "Steam_CreateSteamPipe refused");
    out.hPipe = hPipe;
    out.stage = Stage::Pipe;
    log("steam host: pipe created");

    if (attempts < 1) attempts = 1;
    for (int i = 0; i < attempts; ++i) {
        unsigned connected = api.connect_to_global_user(hPipe);
        if (connected) {
            out.hConnected = connected;
            out.stage = Stage::Connected;
            out.logged_on = api.blogged_on ? api.blogged_on(connected) : false;
            out.connected = api.bconnected ? api.bconnected(hPipe) : false;
            log("steam host: connected to the global user");
            return out;
        }
        if (i + 1 < attempts) {
            if (sleep) sleep(wait_ms);
            else if (wait_ms > 0) {
                // No sleep supplied: the caller owns the pacing. Retrying
                // without a pause would spin, so stop here instead.
                break;
            }
        }
    }
    return fail(Stage::Connected, "Steam_ConnectToGlobalUser never returned a user");
}

// True when the host module exports the bootstrapper surface the client probes
// before it will dial a CM. `has` answers GetProcAddress(host, name) != null.
template <typename Has>
inline bool bootstrapper_complete(Has&& has) {
    for (size_t i = 0; i < BootstrapperExportCount; ++i)
        if (!has(BootstrapperExports[i])) return false;
    return true;
}

} // namespace steam_host
