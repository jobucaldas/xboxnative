#include "../session_restore.h"
#include <cstdlib>
#include <iostream>
#include <string>
static void check(bool ok) { if (!ok) std::abort(); }
int main() {
    // Canned CM ClientLogOnResponse eresult=15 enters the production restore path.
    std::string token = "canned-refresh", access = "canned-access", games = "installed";
    std::string stored = token;
    bool qr = false, focused = false, exists = true; int writes = 0;
    auto clear = [&](const std::string& rejected) {
        return session_restore::clear_rejected(rejected,
            [&](std::string& r, std::string& a) { r = stored; a = access; return exists; },
            [&](const std::string& r, const std::string& a) { stored = r; access = a; ++writes; },
            [&] { stored.clear(); exists = false; ++writes; });
    };
    auto run = [&](bool connected, int er) {
        return session_restore::complete(connected, er, token, clear,
            [&] { qr = focused = true; });
    };
    check(run(false, 15) == session_restore::Result::qr);
    check(token.empty() && stored.empty() && exists && qr && focused && writes == 1);
    check(access == "canned-access" && games == "installed");
    token = "valid-refresh"; qr = focused = false; writes = 0;
    check(run(true, 1) == session_restore::Result::restored);
    check(token == "valid-refresh" && !qr && writes == 0);
    for (int er : {0, 2, 16, 20, 48, 84}) {
        check(run(false, er) == session_restore::Result::retry);
        check(token == "valid-refresh" && !qr && writes == 0);
    }
    stored = "replacement-token";
    check(!clear("old-token") && stored == "replacement-token" && writes == 0);
    access.clear(); stored = "expired-token";
    check(clear("expired-token") && !exists && writes == 1);
    for (int er : {5, 63, 65, 18, 50, 43}) check(run(false, er) == session_restore::Result::qr);
    std::cout << "PASS: rejected-token QR fallback, valid persistence, transient retry\n";
}
