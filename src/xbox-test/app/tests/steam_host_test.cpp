// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression for the in-process Steam client host: the entry-point
// sequence Valve's own launcher uses, the retry policy around
// Steam_ConnectToGlobalUser (which answers 0 until the engine has a session),
// and the bootstrapper surface the client probes before it will dial a CM.
// The fake client below reproduces the shape measured off-device under Wine
// (issue #179): CreateGlobalUser and CreateSteamPipe succeed,
// ConnectToGlobalUser answers 0 until a session exists.
#include "../steam_host.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace steam_host;

struct FakeClient {
    // What the fake answers. `connect_after` is how many connect calls return 0
    // before one returns a user handle; -1 means never.
    int connect_after = 0;
    bool interface_ok = true;
    bool global_user_ok = true;
    int pipe_value = 2;
    bool logged_on = true;
    bool connected = true;
    int connect_calls = 0;
    int release_calls = 0;
    int released_pipe = 0;
    std::string last_interface;

    Api api(std::vector<std::string>& log) {
        Api a;
        a.create_interface = [this](const char* version, int* result) -> void* {
            last_interface = version;
            if (result) *result = interface_ok ? 0 : 1;
            return interface_ok ? this : nullptr;
        };
        a.create_global_user = [this](int* hUser) -> int {
            if (!global_user_ok) return 0;
            if (hUser) *hUser = 1;
            return 1;
        };
        a.create_pipe = [this]() { return pipe_value; };
        a.connect_to_global_user = [this](int) -> unsigned {
            ++connect_calls;
            if (connect_after < 0) return 0;
            return connect_calls > connect_after ? 7u : 0u;
        };
        a.blogged_on = [this](unsigned) { return logged_on; };
        a.bconnected = [this](unsigned) { return connected; };
        a.release_pipe = [this](int pipe) { ++release_calls; released_pipe = pipe; return 1; };
        a.log = [&log](const std::string& line) { log.push_back(line); };
        return a;
    }
};

static bool has_line(const std::vector<std::string>& log, const std::string& needle) {
    for (auto& l : log) if (l.find(needle) != std::string::npos) return true;
    return false;
}

int main() {
    // ---- the names the client resolves, as its own string table spells them
    assert(std::string(ClientInterface) == "SteamClient021");
    assert(BootstrapperExportCount == 3);
    assert(std::string(BootstrapperExports[0]) == "GetBootstrapperVersion");
    assert(std::string(BootstrapperExports[1]) == "SteamBootstrapper_GetEUniverse");
    assert(std::string(BootstrapperExports[2]) == "SteamBootstrapper_GetLoggingDir");

    // ---- happy path: every step succeeds on the first call
    {
        FakeClient client;
        std::vector<std::string> log;
        auto api = client.api(log);
        int sleeps = 0;
        auto r = bring_up(api, 10, 500, [&](int) { ++sleeps; });
        assert(r.ok());
        assert(r.stage == Stage::Connected);
        assert(r.hUser == 1 && r.hPipe == 2 && r.hConnected == 7);
        assert(r.logged_on && r.connected);
        assert(r.error.empty());
        assert(client.last_interface == "SteamClient021");
        assert(client.connect_calls == 1);
        assert(sleeps == 0);
        assert(client.release_calls == 0);
        assert(has_line(log, "steam host: connected to the global user"));
    }

    // ---- the measured behaviour: connect answers 0 until a session exists
    {
        FakeClient client;
        client.connect_after = 3;
        std::vector<std::string> log;
        auto api = client.api(log);
        std::vector<int> waits;
        auto r = bring_up(api, 10, 500, [&](int ms) { waits.push_back(ms); });
        assert(r.ok());
        assert(client.connect_calls == 4);
        // One pause between each pair of attempts, never after the last.
        assert(waits.size() == 3);
        for (int ms : waits) assert(ms == 500);
    }

    // ---- no sleep supplied: do not spin, stop at the first refusal
    {
        FakeClient client;
        client.connect_after = -1;
        std::vector<std::string> log;
        auto api = client.api(log);
        auto r = bring_up(api, 10, 500, {});
        assert(!r.ok());
        assert(r.stage == Stage::Failed);
        assert(client.connect_calls == 1);
        assert(has_line(log, "Steam_ConnectToGlobalUser never returned a user"));
    }

    // ---- connect never succeeds: the pipe is released and the failure named
    {
        FakeClient client;
        client.connect_after = -1;
        std::vector<std::string> log;
        auto api = client.api(log);
        int sleeps = 0;
        auto r = bring_up(api, 4, 250, [&](int) { ++sleeps; });
        assert(!r.ok());
        assert(r.stage == Stage::Failed);
        assert(r.error.find("Steam_ConnectToGlobalUser") != std::string::npos);
        assert(client.connect_calls == 4);
        assert(sleeps == 3);
        assert(client.release_calls == 1 && client.released_pipe == 2);
        assert(r.hPipe == 0);
        assert(has_line(log, "(stage connected)"));
    }

    // ---- attempts is clamped: one attempt still runs
    {
        FakeClient client;
        client.connect_after = 0;
        std::vector<std::string> log;
        auto api = client.api(log);
        auto r = bring_up(api, 0, 500, [](int) {});
        assert(r.ok());
        assert(client.connect_calls == 1);
    }

    // ---- the client module refuses the interface
    {
        FakeClient client;
        client.interface_ok = false;
        std::vector<std::string> log;
        auto api = client.api(log);
        auto r = bring_up(api, 10, 500, [](int) {});
        assert(!r.ok());
        assert(r.stage == Stage::Failed);
        assert(r.error.find("CreateInterface(SteamClient021)") != std::string::npos);
        assert(client.connect_calls == 0 && client.release_calls == 0);
        assert(has_line(log, "(stage interface)"));
    }

    // ---- the client module refuses the global user
    {
        FakeClient client;
        client.global_user_ok = false;
        std::vector<std::string> log;
        auto api = client.api(log);
        auto r = bring_up(api, 10, 500, [](int) {});
        assert(!r.ok());
        assert(r.error.find("Steam_CreateGlobalUser") != std::string::npos);
        assert(client.connect_calls == 0 && client.release_calls == 0);
        assert(has_line(log, "(stage global-user)"));
    }

    // ---- the client module refuses the pipe
    {
        FakeClient client;
        client.pipe_value = 0;
        std::vector<std::string> log;
        auto api = client.api(log);
        auto r = bring_up(api, 10, 500, [](int) {});
        assert(!r.ok());
        assert(r.error.find("Steam_CreateSteamPipe") != std::string::npos);
        assert(client.connect_calls == 0 && client.release_calls == 0);
        assert(has_line(log, "(stage pipe)"));
    }

    // ---- a module missing an entry point is refused before anything is called
    {
        FakeClient client;
        std::vector<std::string> log;
        auto api = client.api(log);
        api.connect_to_global_user = nullptr;
        auto r = bring_up(api, 10, 500, [](int) {});
        assert(!r.ok());
        assert(r.error.find("missing an engine entry point") != std::string::npos);
        assert(client.connect_calls == 0);
    }

    // ---- no client module at all
    {
        Api empty;
        auto r = bring_up(empty);
        assert(!r.ok());
        assert(r.error == "no client module");
    }

    // ---- the bootstrapper surface the client probes before dialling a CM
    {
        assert(bootstrapper_complete([](const char*) { return true; }));
        for (size_t missing = 0; missing < BootstrapperExportCount; ++missing) {
            auto has = [&](const char* name) {
                return std::string(name) != BootstrapperExports[missing];
            };
            assert(!bootstrapper_complete(has));
        }
    }

    puts("STEAM-HOST PASS: entry-point sequence, connect retry policy, pipe release on failure, bootstrapper surface");
    return 0;
}
