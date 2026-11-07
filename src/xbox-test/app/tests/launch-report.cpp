// SPDX-License-Identifier: GPL-3.0-or-later
// launch_report.h: a game's page shows only that game's finished, failed launch.
#include "../launch_report.h"
#include <cstdio>
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)
static std::string result(const char* appid, const char* terminal, const char* ok, const char* text) {
    return std::string("{\"appid\":") + appid + ",\"attemptId\":\"1:1\",\"state\":\"failed\",\"terminal\":" + terminal +
           ",\"ok\":" + ok + ",\"gameCodeExecuted\":false,\"lastEvent\":\"x\",\"result\":\"" + text + "\"}\n";
}
int main() {
    const char* missing = "unresolved static import: dinput8.dll!DirectInput8Create";
    CHECK(launch_report::failure(result("1259790", "true", "false", missing), 1259790) == missing);
    // json_esc escapes quotes and backslashes.
    CHECK(launch_report::failure(result("7", "true", "false", "path C:\\\\x \\\"q\\\""), 7) == "path C:\\x \"q\"");
    CHECK(launch_report::failure(result("7", "true", "false", ""), 7) == "unknown reason");
    // Another game, a launch still running, a successful one, or a malformed file: nothing.
    CHECK(launch_report::failure(result("1259790", "true", "false", missing), 125979).empty());
    CHECK(launch_report::failure(result("12597900", "true", "false", missing), 1259790).empty());
    CHECK(launch_report::failure(result("1259790", "false", "false", missing), 1259790).empty());
    CHECK(launch_report::failure(result("1259790", "true", "true", missing), 1259790).empty());
    CHECK(launch_report::failure("{\"appid\":1259790,\"terminal\":true,\"ok\":false,\"result\":\"cut", 1259790).empty());
    CHECK(launch_report::failure("", 1259790).empty());
    printf("launch-report: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
