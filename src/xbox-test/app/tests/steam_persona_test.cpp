// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression for the Steam persona bridge (steam_persona.h) against a
// fake CM service that answers with the real CMsgClientPersonaState shape, and
// for the private on-device name cache. Every SteamID and name here is
// synthetic: no real account appears in this file.
#include "../steam_persona.h"
#include <cassert>
#include <cstdio>
#include <thread>
using namespace steam_persona;

// A fake Steam backend: CMsgClientRequestFriendData (EMSG 815) is recorded, and
// the answer is built as CMsgClientPersonaState (EMSG 766) the way Steam sends
// it, with a name per user and an online persona state.
struct Server {
    std::map<uint64_t, std::string> names;
    std::vector<uint64_t> asked;
    unsigned requests = 0;
    bool answer = true;
    // The push the launcher's CM reader would deliver.
    std::string state_body(const std::vector<uint64_t>& ids) const {
        std::string body;
        for (auto id : ids) {
            auto it = names.find(id);
            if (it == names.end()) continue;
            std::string f;
            pb_fixed64(f, 1, id); pb_u(f, 2, 1 /* Online */); pb_s(f, 15, it->second);
            pb_s(body, 2, f);
        }
        return body;
    }
    bool send(uint32_t emsg, const std::string& body) {
        if (emsg != ClientRequestFriendData) return false;
        ++requests;
        std::vector<uint64_t> ids;
        for (auto& f : pb_parse(body)) if (f.f == 2) ids.push_back(f.u);
        for (auto id : ids) asked.push_back(id);
        return answer;
    }
};

int main() {
    // ---- wire: the request carries the flags and every SteamID
    {
        std::vector<uint64_t> ids = {0x110000100000002ull, 0x110000100000003ull};
        auto body = request_body(ids);
        auto f = pb_parse(body);
        assert(pb_get(f, 1)->u == (FlagStatus | FlagPlayerName));
        int seen = 0;
        for (auto& x : f) if (x.f == 2) { assert(x.u == ids[size_t(seen)]); ++seen; }
        assert(seen == 2);
    }
    // ---- wire: a persona push carries many users, and a user without a name
    // (Steam sends those too) is not turned into an empty name
    {
        std::string body;
        {
            std::string a; pb_fixed64(a, 1, 0x110000100000002ull); pb_u(a, 2, 1); pb_s(a, 15, "Player Two");
            std::string b; pb_fixed64(b, 1, 0x110000100000003ull); pb_u(b, 2, 0);
            pb_s(body, 2, a); pb_s(body, 2, b);
        }
        auto got = parse_state(body);
        assert(got.size() == 2);
        assert(got[0].steamid == 0x110000100000002ull && got[0].name == "Player Two" && got[0].has_state);
        assert(got[1].steamid == 0x110000100000003ull && got[1].name.empty() && got[1].state == 0 && got[1].has_state);
    }

    // ---- the bridge: names arrive on the reader thread, the game's thread waits
    // for them, and only names Steam answered are handed out
    {
        Server server;
        server.names = {{0x110000100000002ull, "Player Two"}, {0x110000100000003ull, "Player Three"}};
        std::vector<std::string> log;
        Io io;
        io.send = [&](uint32_t emsg, const std::string& body) { return server.send(emsg, body); };
        io.log = [&](const std::string& line) { log.push_back(line); };
        Bridge bridge;
        std::vector<uint64_t> ids = {0x110000100000002ull, 0x110000100000003ull, 0x110000100000009ull};
        // The answer arrives while the game's thread is inside ensure(), as the
        // CM reader thread would deliver it.
        std::thread reader([&] {
            while (server.requests == 0) std::this_thread::yield();
            bridge.on_state(server.state_body(ids));
        });
        bridge.ensure(io, ids, 3000);
        reader.join();
        assert(server.requests == 1 && server.asked.size() == 3);
        assert(bridge.known(0x110000100000002ull) && bridge.known(0x110000100000003ull));
        // Steam had no name for the third: the provider's own answer stands.
        assert(!bridge.known(0x110000100000009ull));
        assert(std::string(bridge.name(0x110000100000002ull)) == "Player Two");
        assert(bridge.name(0x110000100000009ull) == nullptr);
        // Counts only in the log: never a SteamID or a persona name.
        bool counted = false;
        for (auto& l : log) {
            if (l.find("2 of 3 leaderboard names resolved") != std::string::npos) counted = true;
            assert(l.find("Player Two") == std::string::npos);
            assert(l.find("1100001000") == std::string::npos);
        }
        assert(counted);
        // A name is asked for once: the second ensure of the same ids sends nothing.
        bridge.ensure(io, ids, 100);
        assert(server.requests == 1);
    }

    // ---- a transport that cannot send leaves the game with no names, and says so
    {
        std::vector<std::string> log;
        Io io;
        io.send = [](uint32_t, const std::string&) { return false; };
        io.log = [&](const std::string& line) { log.push_back(line); };
        Bridge bridge;
        bridge.ensure(io, {0x110000100000002ull}, 50);
        assert(!bridge.known(0x110000100000002ull));
        bool told = false;
        for (auto& l : log) if (l.find("name request not sent") != std::string::npos) told = true;
        assert(told);
    }

    // ---- a reply that never comes does not hang the game's thread
    {
        Io io;
        io.send = [](uint32_t, const std::string&) { return true; };
        io.log = [](const std::string&) {};
        Bridge bridge;
        auto start = std::chrono::steady_clock::now();
        bridge.ensure(io, {0x110000100000002ull}, 60);
        auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        assert(waited >= 50 && waited < 2000);
        assert(!bridge.known(0x110000100000002ull));
    }

    // ---- the reply names some of the rows and not others (Steam does not name
    // a private profile): the wait ends when the names stop arriving, not at the
    // timeout, because the game's thread is drawing behind it
    {
        Server server;
        server.names = {{0x110000100000002ull, "Player Two"}};
        Io io;
        io.send = [&](uint32_t emsg, const std::string& body) { return server.send(emsg, body); };
        io.log = [](const std::string&) {};
        Bridge bridge;
        std::vector<uint64_t> ids = {0x110000100000002ull, 0x110000100000009ull};
        std::thread reader([&] {
            while (server.requests == 0) std::this_thread::yield();
            bridge.on_state(server.state_body(ids));
        });
        auto start = std::chrono::steady_clock::now();
        bridge.ensure(io, ids, 5000);
        auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        reader.join();
        assert(bridge.known(0x110000100000002ull) && !bridge.known(0x110000100000009ull));
        assert(waited < Bridge::SeedWaitMs + 150);   // the push answered at once, no quiet window needed
    }

    // ---- the seed never holds the game's thread for the caller's full
    // timeout, even when the caller asks for seconds: the game is drawing
    // behind it.
    // A reply that comes later still lands, and the tap answers it then.
    {
        Server server;
        server.names = {{0x110000100000002ull, "Player Two"}};
        Io io;
        io.send = [&](uint32_t emsg, const std::string& body) { return server.send(emsg, body); };
        io.log = [](const std::string&) {};
        Bridge bridge;
        std::vector<uint64_t> ids = {0x110000100000002ull};
        std::thread reader([&] {
            while (server.requests == 0) std::this_thread::yield();
            std::this_thread::sleep_for(std::chrono::milliseconds(Bridge::SeedWaitMs + 300));
            bridge.on_state(server.state_body(ids));
        });
        auto start = std::chrono::steady_clock::now();
        bridge.ensure(io, ids, 5000);   // asks for 5 s; the seed still returns within SeedWaitMs
        auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        // The seed gave up before the push; the row reads the provider's answer.
        assert(!bridge.known(0x110000100000002ull));
        assert(waited < Bridge::SeedWaitMs + 150);
        // The push arrives afterwards, and the tap answers it from then on.
        reader.join();
        assert(std::string(bridge.name(0x110000100000002ull)) == "Player Two");
    }

    // ---- a push that names nobody is still Steam's answer: the wait ends on it
    // rather than burning the whole timeout on the game's thread
    {
        Server server;
        Io io;
        io.send = [&](uint32_t emsg, const std::string& body) { return server.send(emsg, body); };
        std::vector<std::string> log;
        io.log = [&](const std::string& line) { log.push_back(line); };
        Bridge bridge;
        std::vector<uint64_t> ids = {0x110000100000009ull};
        std::thread reader([&] {
            while (server.requests == 0) std::this_thread::yield();
            bridge.on_state(std::string());   // a persona push with no friends
        });
        auto start = std::chrono::steady_clock::now();
        bridge.ensure(io, ids, 5000);
        auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        reader.join();
        assert(!bridge.known(0x110000100000009ull));
        assert(waited < Bridge::SeedWaitMs + 150);
        bool counted = false;
        for (auto& l : log) if (l.find("0 of 1 leaderboard names resolved") != std::string::npos) counted = true;
        assert(counted);
    }

    // ---- names the game asked for outside the seeded rows are queued, not sent
    // on the game's thread, and are requested once by the keeper
    {
        Bridge bridge;
        bridge.want({0x110000100000005ull, 0x110000100000006ull});
        bridge.want({0x110000100000005ull});   // duplicate
        auto waiting = bridge.take_pending();
        assert(waiting.size() == 2);
        assert(bridge.take_pending().empty());
        // Already requested: asking again this launch would be a duplicate.
        bridge.want({0x110000100000005ull});
        assert(bridge.take_pending().empty());
    }

    // ---- the queue is bounded by distinct ids, so a rank screen polling the
    // same unresolved rows never exhausts it
    {
        Bridge bridge;
        std::vector<uint64_t> ids;
        for (size_t i = 0; i < MaxTracked + 50; ++i) ids.push_back(0x110000200000000ull + i);
        bridge.want(ids);
        assert(bridge.take_pending().size() == MaxTracked);
        // Repeats of ids already queued or asked add nothing.
        bridge.want(ids);
        assert(bridge.take_pending().empty());
    }

    // ---- a name change Steam pushes later replaces the cached name in place,
    // and a pointer already handed out stays valid
    {
        Bridge bridge;
        bridge.on_state([] {
            std::string f; pb_fixed64(f, 1, 0x110000100000002ull); pb_u(f, 2, 1); pb_s(f, 15, "Before");
            std::string body; pb_s(body, 2, f); return body;
        }());
        const char* first = bridge.name(0x110000100000002ull);
        assert(std::string(first) == "Before");
        // A rename is a new name; the old pointer is not reused (the deque never
        // moves, so it stays readable), and the map now answers the new one.
        bridge.on_state([] {
            std::string f; pb_fixed64(f, 1, 0x110000100000002ull); pb_u(f, 2, 1); pb_s(f, 15, "After");
            std::string body; pb_s(body, 2, f); return body;
        }());
        assert(std::string(first) == "Before");
        assert(std::string(bridge.name(0x110000100000002ull)) == "After");
    }

    // ---- the private on-device cache: round-trips, expires, and refuses junk
    {
        namespace fs = std::filesystem;
        fs::path dir = fs::temp_directory_path() / "xbn-persona-test";
        fs::remove_all(dir);
        auto path = dir / "persona_names.txt";
        std::map<uint64_t, std::string> names = {{0x110000100000002ull, "Player Two"},
                                                 {0x110000100000003ull, "Name With Space\nAnd Newline"}};
        assert(save_cache(path, 1000, names));
        std::map<uint64_t, std::string> back;
        assert(load_cache(path, 1000, 100, back));
        assert(back == names);
        // Older than the TTL: ignored, so a name change is not cached forever.
        assert(!load_cache(path, 1200, 100, back));
        assert(back.empty());
        // A stamp in the future is not trusted either.
        assert(!load_cache(path, 900, 100, back));
        // A file that is not ours, or carries a broken record, is refused or
        // skipped rather than loaded as a name. The ids are written in decimal
        // (as the bridge writes them) but built from synthetic constants: a
        // committed 17-digit account literal is what the secret scan refuses.
        {
            const uint64_t good = 0x110000100000002ull, odd = 0x110000100000003ull, unnamed = 0x110000100000004ull;
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << "#xbn-persona 1 1000\n";
            out << good << ' ' << hex_encode("Good") << "\n";
            out << "notanumber zz\n";
            out << odd << " 6a6\n";   // odd-length hex
            out << unnamed << ' ' << hex_encode("") << "\n";   // empty name
        }
        back.clear();
        assert(load_cache(path, 1000, 100, back));
        assert(back.size() == 1);
        assert(back[0x110000100000002ull] == "Good");
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << "someone else's file\n";
        }
        assert(!load_cache(path, 1000, 100, back));
        fs::remove_all(dir);
    }

    // ---- names from the last launch are adopted, so a row drawn before Steam
    // answers still reads correctly; Steam's fresh answer then wins
    {
        Bridge bridge;
        bridge.adopt({{0x110000100000002ull, "Cached Two"}});
        assert(std::string(bridge.name(0x110000100000002ull)) == "Cached Two");
        bridge.on_state([] {
            std::string f; pb_fixed64(f, 1, 0x110000100000002ull); pb_u(f, 2, 1); pb_s(f, 15, "Fresh Two");
            std::string body; pb_s(body, 2, f); return body;
        }());
        assert(std::string(bridge.name(0x110000100000002ull)) == "Fresh Two");
    }

    // ---- the account's own name, so GetPersonaName is not the staged placeholder
    {
        Bridge bridge;
        bridge.self = 0x110000100000001ull;
        assert(bridge.self_name() == nullptr);
        bridge.on_state([] {
            std::string f; pb_fixed64(f, 1, 0x110000100000001ull); pb_u(f, 2, 1); pb_s(f, 15, "The Owner");
            std::string body; pb_s(body, 2, f); return body;
        }());
        assert(std::string(bridge.self_name()) == "The Owner");
    }

    puts("STEAM-PERSONA PASS: request/state wire shapes, wait policy, queue, rename in place, private cache round-trip and expiry, adopt, self name");
    return 0;
}
