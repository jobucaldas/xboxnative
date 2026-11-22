// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression for the callback ABI half of real Steam lobbies: the
// launcher queue spliced into SteamAPI_ManualDispatch_*, and the ISteamMatchmaking
// adapter that posts CM answers into it. The provider's real exports are faked
// with plain functions, and the CM with a fake that answers the CMsgClientMMS*
// shapes, so every invariant the hooks promise is checked off-device:
//   1 no tap -> pass-through          6 CreateLobby success and refusal
//   2 no ManualDispatch_Init -> same  7 list / by-index / join / leave
//   3 launcher handles                10 teardown while calls are in flight
//   4 call results
//   5 FreeLastCallback and ordering
// (8 and 9, the pinned provider's own behaviour, live in steam-matchmaking-gbe.sh.)
#include "../steam_matchmaking.h"
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <thread>
using namespace steam_matchmaking;

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); } } while (0)

namespace {

constexpr uint32_t kAppId = 3405340;
constexpr int32_t kPipe = 7;
constexpr uint64_t kOwnerId = 0x110000100000001ull;   // fixture ids, never a real account
constexpr uint64_t kPeerId = 0x110000100000004ull;

// ---- the provider's exports, faked with plain functions and recorded calls
struct Provider {
    int init = 0, pipe = 0, next = 0, release = 0, result = 0, create = 0, list = 0, by_index = 0, join = 0, leave = 0;
    int outstanding = 0, bad_release = 0;
    std::deque<int> queue;                  // callback ids waiting in the provider
    uint32_t param = 0;                     // the buffer GetNextCallback hands out
    void* self = nullptr; int type = 0, max = 0, index = 0; uint64_t lobby = 0;
    int32_t pipe_arg = 0; uint64_t call = 0; void* buffer = nullptr; int cub = 0, expected = 0; bool* failed = nullptr;
};
Provider P;

void reset_provider() { P = Provider(); }
void p_init() { ++P.init; }
int32_t p_pipe() { ++P.pipe; return kPipe; }
bool p_next(int32_t pipe, CallbackMsg_t* msg) {
    ++P.next; P.pipe_arg = pipe;
    if (P.queue.empty()) return false;
    int id = P.queue.front(); P.queue.pop_front();
    P.param = uint32_t(id) * 10;
    msg->m_hSteamUser = 1; msg->m_iCallback = id;
    msg->m_pubParam = reinterpret_cast<uint8_t*>(&P.param); msg->m_cubParam = 4;
    ++P.outstanding;
    return true;
}
void p_free(int32_t pipe) {
    ++P.release; P.pipe_arg = pipe;
    if (P.outstanding > 0) --P.outstanding; else ++P.bad_release;
}
// A handle like the pinned gbe_fork's: random 64 bits, the top bit set. Its result is the
// provider's, and the only one the fake provider can give.
constexpr uint64_t kProviderTopBitCall = 0x9f1234567890abcdull;
bool p_result(int32_t pipe, uint64_t call, void* buffer, int cub, int expected, bool* failed) {
    ++P.result; P.pipe_arg = pipe; P.call = call; P.buffer = buffer; P.cub = cub; P.expected = expected; P.failed = failed;
    if (call != kProviderTopBitCall || expected != CallbackLobbyCreated || cub < int(sizeof(LobbyCreated_t)) || !buffer) return false;
    auto reply = zeroed<LobbyCreated_t>();
    reply.m_eResult = 1; reply.m_ulSteamIDLobby = 0x1100001000099ull;
    memcpy(buffer, &reply, sizeof reply);
    if (failed) *failed = false;
    return true;
}
uint64_t p_create(void* self, int type, int max) { ++P.create; P.self = self; P.type = type; P.max = max; return 0x501; }
uint64_t p_list(void* self) { ++P.list; P.self = self; return 0x502; }
uint64_t p_by_index(void* self, int index) { ++P.by_index; P.self = self; P.index = index; return 0x503; }
uint64_t p_join(void* self, uint64_t lobby) { ++P.join; P.self = self; P.lobby = lobby; return 0x504; }
void p_leave(void* self, uint64_t lobby) { ++P.leave; P.self = self; P.lobby = lobby; }

// ---- a fake CM answering the lobby shapes
struct Cm {
    std::mutex m;
    std::vector<uint32_t> emsgs, routed;
    bool down = false;
    int32_t create_eresult = steam_lobbies::ResultOK;
    uint32_t join_response = EnterSuccess;
    int32_t list_eresult = steam_lobbies::ResultOK;
    std::vector<uint64_t> listed;
    uint64_t next_id = 0x1100001000000ull;
    std::function<void()> on_call;          // runs inside the request, for the in-flight test

    bool call(uint32_t emsg, uint32_t routing, const std::string& body, std::string& resp) {
        if (on_call) on_call();
        std::lock_guard<std::mutex> lock(m);
        emsgs.push_back(emsg); routed.push_back(routing);
        if (down) return false;
        resp.clear();
        auto fields = pb_parse(body);
        switch (emsg) {
        case steam_lobbies::CreateLobby:
            pb_u(resp, 1, kAppId);
            if (create_eresult == steam_lobbies::ResultOK) { pb_fixed64(resp, 2, next_id); }
            pb_u(resp, 3, uint64_t(int64_t(create_eresult)));
            return true;
        case steam_lobbies::GetLobbyData: {
            std::string member;
            pb_fixed64(member, 1, kOwnerId); pb_s(member, 2, "owner");
            pb_u(resp, 1, kAppId); pb_fixed64(resp, 2, next_id); pb_u(resp, 4, 4); pb_u(resp, 5, 2);
            pb_fixed64(resp, 7, kOwnerId); pb_s(resp, 9, member);
            return true;
        }
        case steam_lobbies::JoinLobby: {
            uint64_t id = 0;
            for (auto& f : fields) if (f.f == 2) id = f.u;
            pb_u(resp, 1, kAppId); pb_fixed64(resp, 2, id); pb_u(resp, 3, join_response);
            pb_u(resp, 4, 4); pb_u(resp, 5, 2); pb_fixed64(resp, 7, kPeerId);
            return true;
        }
        case steam_lobbies::LeaveLobby:
            pb_u(resp, 1, kAppId); pb_u(resp, 3, steam_lobbies::ResultOK);
            return true;
        case steam_lobbies::GetLobbyList:
            pb_u(resp, 1, kAppId); pb_u(resp, 3, uint64_t(int64_t(list_eresult)));
            for (auto id : listed) { std::string row; pb_fixed64(row, 1, id); pb_s(resp, 4, row); }
            return true;
        default:
            return false;
        }
    }
    size_t count(uint32_t emsg) {
        std::lock_guard<std::mutex> lock(m);
        size_t n = 0;
        for (auto e : emsgs) if (e == emsg) ++n;
        return n;
    }
};

// One launch: a Hub with a Service whose CM round trips wait on a manual executor
// until drain() runs them, or run inline.
struct Rig {
    Cm cm;
    Hub hub;
    std::shared_ptr<steam_lobbies::Bridge> bridge = std::make_shared<steam_lobbies::Bridge>();
    std::vector<std::function<void()>> tasks;
    bool manual;
    std::shared_ptr<Service> svc;

    explicit Rig(bool manual_executor = true) : manual(manual_executor) {
        Config config;
        config.bridge = bridge;
        config.io.call = [this](uint32_t emsg, uint32_t routing, const std::string& body, std::string& resp) {
            return cm.call(emsg, routing, body, resp);
        };
        config.io.log = [](const std::string&) {};
        config.executor = [this](std::function<void()> task) {
            if (manual) tasks.push_back(std::move(task)); else task();
        };
        config.appid = kAppId;
        config.persona = "tester";
        svc = std::make_shared<Service>(config);
        hub.set(svc);
    }
    void drain() {
        auto run = std::move(tasks);
        tasks.clear();
        for (auto& task : run) task();
    }
    // The game's start-up: ManualDispatch_Init, then its pipe.
    void game_starts() { hub.init(p_init); CHECK(hub.pipe(p_pipe) == kPipe); }
};

struct Got { int callback; std::vector<uint8_t> data; };
// The game's pump: GetNextCallback / read / FreeLastCallback until empty.
std::vector<Got> pump(Hub& hub) {
    std::vector<Got> out;
    CallbackMsg_t msg{};
    while (hub.next(p_next, kPipe, &msg)) {
        out.push_back({msg.m_iCallback, std::vector<uint8_t>(msg.m_pubParam, msg.m_pubParam + msg.m_cubParam)});
        hub.free_last(p_free, kPipe);
    }
    return out;
}
template <class T> T as(const Got& got) {
    CHECK(got.data.size() == sizeof(T));
    T value; memcpy(&value, got.data.data(), sizeof value);
    return value;
}
// GetAPICallResult as the game calls it after a 703.
template <class T> bool result_of(Hub& hub, uint64_t call, int callback, T& out, bool* failed) {
    return hub.api_call_result(p_result, kPipe, call, &out, int(sizeof out), callback, failed);
}

// ---- 1: no tap installed -> every export reaches the provider unchanged
void passthrough_without_tap() {
    reset_provider();
    Hub hub;
    void* self = reinterpret_cast<void*>(0x1234);
    CHECK(hub.create_lobby(p_create, self, 2, 4) == 0x501 && P.create == 1 && P.self == self && P.type == 2 && P.max == 4);
    CHECK(hub.request_lobby_list(p_list, self) == 0x502 && P.list == 1 && P.self == self);
    CHECK(hub.lobby_by_index(p_by_index, self, 3) == 0x503 && P.by_index == 1 && P.index == 3);
    CHECK(hub.join_lobby(p_join, self, 0x99) == 0x504 && P.join == 1 && P.lobby == 0x99);
    hub.leave_lobby(p_leave, self, 0x98);
    CHECK(P.leave == 1 && P.lobby == 0x98);
    hub.init(p_init); CHECK(P.init == 1);
    CHECK(hub.pipe(p_pipe) == kPipe && P.pipe == 1);
    P.queue = {1100, 1101};
    CallbackMsg_t msg{};
    CHECK(hub.next(p_next, kPipe, &msg) && msg.m_iCallback == 1100 && P.next == 1);
    hub.free_last(p_free, kPipe); CHECK(P.release == 1);
    bool failed = true; char buffer[8];
    uint64_t launcher_looking = LauncherHandleBit | 1;
    CHECK(!hub.api_call_result(p_result, kPipe, launcher_looking, buffer, 8, 513, &failed));
    CHECK(P.result == 1 && P.call == launcher_looking && P.buffer == buffer && P.cub == 8 && P.expected == 513 && P.failed == &failed);
    CHECK(failed);   // nothing but the provider touched it
    // A null `real` never crashes either.
    CHECK(hub.create_lobby(nullptr, self, 2, 4) == 0);
    CHECK(!hub.next(nullptr, kPipe, &msg));
}

// ---- 2: a game that never called ManualDispatch_Init is never handed a launcher handle
void passthrough_without_manual_dispatch() {
    reset_provider();
    Rig rig;
    void* self = reinterpret_cast<void*>(0x55);
    // The game pumps with SteamAPI_RunCallbacks: it asks for the pipe but never calls Init.
    CHECK(rig.hub.pipe(p_pipe) == kPipe);
    CHECK(rig.hub.create_lobby(p_create, self, 2, 4) == 0x501 && P.create == 1);
    CHECK(rig.hub.request_lobby_list(p_list, self) == 0x502);
    CHECK(rig.hub.lobby_by_index(p_by_index, self, 0) == 0x503);
    CHECK(rig.hub.join_lobby(p_join, self, 5) == 0x504);
    rig.hub.leave_lobby(p_leave, self, 5); CHECK(P.leave == 1);
    CHECK(rig.tasks.empty() && rig.cm.emsgs.empty());
    P.queue = {1100};
    CallbackMsg_t msg{};
    CHECK(rig.hub.next(p_next, kPipe, &msg) && msg.m_iCallback == 1100);
    rig.hub.free_last(p_free, kPipe); CHECK(P.release == 1);
    CHECK(!rig.svc->active());

    // Init without the pipe is the same: the splice does not know whose pipe to serve.
    Rig early;
    early.hub.init(p_init);
    CHECK(early.hub.create_lobby(p_create, self, 2, 4) == 0x501);
    CHECK(early.tasks.empty());
    // Both seen: now the launcher answers and the provider is not asked.
    int before = P.create;
    CHECK(early.hub.pipe(p_pipe) == kPipe);
    uint64_t call = early.hub.create_lobby(p_create, self, 2, 4);
    CHECK(P.create == before && is_launcher_handle(call));
}

// ---- 3: launcher handles
void launcher_handles() {
    reset_provider();
    Rig a, b;
    a.game_starts(); b.game_starts();
    std::set<uint64_t> seen;
    for (Rig* rig : {&a, &b}) {
        for (int i = 0; i < 50; ++i) {
            uint64_t call = rig->hub.request_lobby_list(p_list, nullptr);
            CHECK(call != 0 && is_launcher_handle(call) && (call & LauncherHandleBit) == LauncherHandleBit);
            CHECK(seen.insert(call).second);
        }
    }
    CHECK(P.list == 0);
    // Every kind of call hands out the same kind of handle.
    CHECK(is_launcher_handle(a.hub.create_lobby(p_create, nullptr, 2, 4)));
    CHECK(is_launcher_handle(a.hub.join_lobby(p_join, nullptr, 9)));
    a.tasks.clear(); b.tasks.clear();
}

// ---- 4: call results
void call_results() {
    reset_provider();
    Rig rig;
    rig.game_starts();
    uint64_t call = rig.hub.create_lobby(p_create, nullptr, 2, 4);
    // Nothing is posted until the CM answered.
    CHECK(pump(rig.hub).empty());
    {   // In flight: answered here, as "not yet", and the provider is never asked.
        LobbyCreated_t early = zeroed<LobbyCreated_t>();
        bool failed = true;
        CHECK(!rig.hub.api_call_result(p_result, kPipe, call, &early, int(sizeof early), 513, &failed));
        CHECK(!failed && P.result == 0 && early.m_ulSteamIDLobby == 0);
        CHECK(!rig.hub.api_call_result(p_result, kPipe, call, nullptr, 0, 0, nullptr) && P.result == 0);
    }
    rig.drain();

    CallbackMsg_t msg{};
    CHECK(rig.hub.next(p_next, kPipe, &msg));
    CHECK(msg.m_iCallback == 703 && msg.m_cubParam == 16 && msg.m_pubParam);
    SteamAPICallCompleted_t done;
    memcpy(&done, msg.m_pubParam, sizeof done);
    CHECK(done.m_hAsyncCall == call && done.m_iCallback == 513 && done.m_cubParam == sizeof(LobbyCreated_t));
    rig.hub.free_last(p_free, kPipe);

    // A buffer one byte short, or the wrong callback id, writes nothing and
    // answers false; the result is still there for the correct call.
    struct Guarded { LobbyCreated_t result; uint8_t after[16]; } g;
    memset(&g, 0xAB, sizeof g);
    bool failed = true;
    CHECK(!rig.hub.api_call_result(p_result, kPipe, call, &g, int(sizeof g.result) - 1, 513, &failed));
    CHECK(!rig.hub.api_call_result(p_result, kPipe, call, &g, int(sizeof g.result), 504, &failed));
    CHECK(!rig.hub.api_call_result(p_result, kPipe, call, &g, 0, 513, &failed));
    CHECK(!rig.hub.api_call_result(p_result, kPipe, call, nullptr, int(sizeof g.result), 513, &failed));
    for (size_t i = 0; i < sizeof g; ++i) CHECK(reinterpret_cast<uint8_t*>(&g)[i] == 0xAB);
    CHECK(failed && P.result == 0);

    // Exactly sized: an exact copy, failed = false, and nothing past cub.
    CHECK(rig.hub.api_call_result(p_result, kPipe, call, &g, int(sizeof g.result), 513, &failed));
    CHECK(!failed && g.result.m_eResult == steam_lobbies::ResultOK && g.result.m_ulSteamIDLobby == rig.bridge->lobby);
    for (size_t i = 0; i < sizeof g.after; ++i) CHECK(g.after[i] == 0xAB);
    // Read once: the entry is consumed, so a second ask is no longer ours and goes to the provider.
    CHECK(!rig.hub.api_call_result(p_result, kPipe, call, &g, int(sizeof g.result), 513, &failed));
    CHECK(P.result == 1 && P.call == call);

    // A handle that is not the launcher's is the provider's to answer.
    CHECK(!rig.hub.api_call_result(p_result, kPipe, 0x42, &g, int(sizeof g.result), 513, &failed));
    CHECK(P.result == 2 && P.call == 0x42);
    // ...including one with the top bit set, as half of gbe_fork's random handles
    // are: routed by ownership, never hijacked by the launcher queue.
    memset(&g, 0xAB, sizeof g); failed = true;
    CHECK(rig.hub.api_call_result(p_result, kPipe, kProviderTopBitCall, &g, int(sizeof g.result), 513, &failed));
    CHECK(P.result == 3 && P.call == kProviderTopBitCall && !failed);
    CHECK(g.result.m_eResult == 1 && g.result.m_ulSteamIDLobby == 0x1100001000099ull);
    for (size_t i = 0; i < sizeof g.after; ++i) CHECK(g.after[i] == 0xAB);
    // A completed launcher handle with the wrong id or a short buffer never reaches the provider.
    uint64_t again = rig.svc->queue().reserve();
    LobbyCreated_t stored = zeroed<LobbyCreated_t>(); stored.m_eResult = 1; stored.m_ulSteamIDLobby = 0x77;
    rig.svc->queue().complete(again, 513, &stored, sizeof stored);
    memset(&g, 0xAB, sizeof g);
    CHECK(!rig.hub.api_call_result(p_result, kPipe, again, &g, int(sizeof g.result), 504, &failed));
    CHECK(!rig.hub.api_call_result(p_result, kPipe, again, &g, int(sizeof g.result) - 1, 513, &failed));
    CHECK(P.result == 3);
    for (size_t i = 0; i < sizeof g; ++i) CHECK(reinterpret_cast<uint8_t*>(&g)[i] == 0xAB);
    CHECK(rig.hub.api_call_result(p_result, kPipe, again, &g, int(sizeof g.result), 513, &failed) && g.result.m_ulSteamIDLobby == 0x77);

    // A call that failed: failed = true, true returned, nothing written.
    uint64_t dead = rig.svc->queue().reserve();
    LobbyCreated_t junk = zeroed<LobbyCreated_t>(); junk.m_eResult = 99;
    rig.svc->queue().complete(dead, 513, &junk, sizeof junk, true);
    memset(&g, 0xAB, sizeof g); failed = false;
    CHECK(rig.hub.api_call_result(p_result, kPipe, dead, &g, int(sizeof g.result), 513, &failed));
    CHECK(failed && g.result.m_eResult == int32_t(0xABABABAB));
}

// ---- 5: FreeLastCallback frees whoever produced the last message
void free_and_ordering() {
    reset_provider();
    Rig rig(false);
    rig.game_starts();
    rig.hub.create_lobby(p_create, nullptr, 2, 4);        // posts 703 then 504
    rig.hub.request_lobby_list(p_list, nullptr);          // posts 703
    P.queue = {1100, 1101, 1102};

    std::vector<int> order;
    CallbackMsg_t msg{};
    bool posted = false;
    while (order.size() < 40 && rig.hub.next(p_next, kPipe, &msg)) {
        order.push_back(msg.m_iCallback);
        std::vector<uint8_t> snapshot(msg.m_pubParam, msg.m_pubParam + msg.m_cubParam);
        if (!posted) {
            // The buffer is intact from next until free, even while more is posted.
            posted = true;
            rig.svc->queue().broadcast(504, "xxxx", 4);
            rig.svc->queue().broadcast(510, "yyyy", 4);
            CHECK(std::vector<uint8_t>(msg.m_pubParam, msg.m_pubParam + msg.m_cubParam) == snapshot);
        }
        int before = P.release;
        rig.hub.free_last(p_free, kPipe);
        const bool provider_message = msg.m_iCallback >= 1100;
        CHECK(P.release == before + (provider_message ? 1 : 0));
    }
    // The launcher's five come first (create: 703, 504; list: 703; then the two
    // posted mid-pump), then the provider's three in order, each once.
    CHECK((order == std::vector<int>{703, 504, 703, 504, 510, 1100, 1101, 1102}));
    CHECK(P.bad_release == 0 && P.outstanding == 0 && P.release == 3 && P.queue.empty());

    // A free with nothing outstanding is the provider's harmless no-op.
    Rig idle;
    idle.game_starts();
    reset_provider();
    idle.hub.free_last(p_free, kPipe);
    CHECK(P.release == 1);
}

// ---- 6: CreateLobby
void create_lobby() {
    reset_provider();
    {   // success: LobbyCreated_t as the call result, LobbyEnter_t as a callback
        Rig rig;
        rig.game_starts();
        uint64_t call = rig.hub.create_lobby(p_create, nullptr, 2, 4);
        rig.drain();
        auto got = pump(rig.hub);
        CHECK(got.size() == 2 && got[0].callback == 703 && got[1].callback == 504);
        uint64_t lobby = rig.bridge->lobby;
        CHECK(lobby != 0);
        LobbyCreated_t created{};
        bool failed = true;
        CHECK(result_of(rig.hub, call, 513, created, &failed) && !failed);
        CHECK(created.m_eResult == steam_lobbies::ResultOK && created.m_ulSteamIDLobby == lobby);
        auto enter = as<LobbyEnter_t>(got[1]);
        CHECK(enter.m_ulSteamIDLobby == lobby && enter.m_EChatRoomEnterResponse == EnterSuccess &&
              !enter.m_bLocked && enter.m_rgfChatPermissions == 0);
        CHECK(rig.cm.count(steam_lobbies::CreateLobby) == 1);
        for (auto routed : rig.cm.routed) CHECK(routed == kAppId);
    }
    {   // Steam refuses: the game still gets LobbyCreated_t, with the error and no lobby
        Rig rig;
        rig.game_starts();
        rig.cm.create_eresult = steam_lobbies::ResultAccessDenied;
        uint64_t call = rig.hub.create_lobby(p_create, nullptr, 2, 4);
        rig.drain();
        auto got = pump(rig.hub);
        CHECK(got.size() == 1 && got[0].callback == 703);
        LobbyCreated_t created{};
        bool failed = true;
        CHECK(result_of(rig.hub, call, 513, created, &failed) && !failed);
        CHECK(created.m_eResult == steam_lobbies::ResultFail && created.m_ulSteamIDLobby == 0);
        CHECK(!rig.bridge->in_lobby());
    }
    {   // the request never gets an answer: same, and the error says so
        Rig rig;
        rig.game_starts();
        rig.cm.down = true;
        uint64_t call = rig.hub.create_lobby(p_create, nullptr, 2, 4);
        rig.drain();
        auto got = pump(rig.hub);
        CHECK(got.size() == 1 && got[0].callback == 703);
        LobbyCreated_t created{};
        bool failed = true;
        CHECK(result_of(rig.hub, call, 513, created, &failed) && !failed);
        CHECK(created.m_eResult == ResultNoConnection && created.m_ulSteamIDLobby == 0);
    }
}

// ---- 7: list, by-index, join, leave
void list_join_leave() {
    reset_provider();
    Rig rig;
    rig.game_starts();
    rig.cm.listed = {0x1100001000010ull, 0x1100001000020ull, 0x1100001000030ull};
    uint64_t call = rig.hub.request_lobby_list(p_list, nullptr);
    rig.drain();
    auto got = pump(rig.hub);
    CHECK(got.size() == 1 && got[0].callback == 703);
    LobbyMatchList_t list{};
    bool failed = true;
    CHECK(result_of(rig.hub, call, 510, list, &failed) && !failed && list.m_nLobbiesMatching == 3);
    for (int i = 0; i < 3; ++i) CHECK(rig.hub.lobby_by_index(p_by_index, nullptr, i) == rig.cm.listed[size_t(i)]);
    for (int out_of_range : {-1, 3, 4, 1000}) CHECK(rig.hub.lobby_by_index(p_by_index, nullptr, out_of_range) == 0);
    CHECK(P.by_index == 0);

    // A later list replaces the earlier one, and a refusal leaves it empty.
    rig.cm.listed = {0x1100001000040ull};
    rig.hub.request_lobby_list(p_list, nullptr); rig.drain(); pump(rig.hub);
    CHECK(rig.hub.lobby_by_index(p_by_index, nullptr, 0) == 0x1100001000040ull);
    CHECK(rig.hub.lobby_by_index(p_by_index, nullptr, 1) == 0);
    rig.cm.list_eresult = steam_lobbies::ResultFail;
    call = rig.hub.request_lobby_list(p_list, nullptr); rig.drain(); pump(rig.hub);
    CHECK(result_of(rig.hub, call, 510, list, &failed) && list.m_nLobbiesMatching == 0);
    CHECK(rig.hub.lobby_by_index(p_by_index, nullptr, 0) == 0);

    // join: LobbyEnter_t as the call result, and the bridge is in the lobby
    const uint64_t target = 0x1100001000050ull;
    call = rig.hub.join_lobby(p_join, nullptr, target);
    rig.drain();
    got = pump(rig.hub);
    CHECK(got.size() == 1 && got[0].callback == 703);
    LobbyEnter_t enter{};
    CHECK(result_of(rig.hub, call, 504, enter, &failed) && !failed);
    CHECK(enter.m_ulSteamIDLobby == target && enter.m_EChatRoomEnterResponse == EnterSuccess);
    CHECK(rig.bridge->lobby == target);

    // leave: the bridge is cleared, the CM is told, and nothing is posted
    rig.hub.leave_lobby(p_leave, nullptr, 0xdead);        // not the lobby it is in: ignored
    rig.drain();
    CHECK(rig.bridge->lobby == target && rig.cm.count(steam_lobbies::LeaveLobby) == 0);
    rig.hub.leave_lobby(p_leave, nullptr, target);
    rig.drain();
    CHECK(!rig.bridge->in_lobby() && rig.cm.count(steam_lobbies::LeaveLobby) == 1);
    CHECK(pump(rig.hub).empty() && P.leave == 0);

    // join refused: the game still hears about it, as an error response
    rig.cm.join_response = 3;                              // k_EChatRoomEnterResponseNotAllowed
    call = rig.hub.join_lobby(p_join, nullptr, target);
    rig.drain(); pump(rig.hub);
    CHECK(result_of(rig.hub, call, 504, enter, &failed) && enter.m_ulSteamIDLobby == target &&
          enter.m_EChatRoomEnterResponse == EnterError);
    CHECK(!rig.bridge->in_lobby());
}

// ---- 10: tearing the tap down with work in flight
void teardown_in_flight() {
    reset_provider();
    {   // queued tasks and queued messages vanish; later calls reach the provider
        Rig rig;
        rig.game_starts();
        uint64_t call = rig.hub.create_lobby(p_create, nullptr, 2, 4);
        rig.hub.request_lobby_list(p_list, nullptr);
        rig.drain();                                   // two results and a LobbyEnter_t are queued
        CallbackMsg_t msg{};
        CHECK(rig.hub.next(p_next, kPipe, &msg));      // the game is holding one
        std::vector<uint8_t> held(msg.m_pubParam, msg.m_pubParam + msg.m_cubParam);
        uint64_t late = rig.hub.create_lobby(p_create, nullptr, 2, 4);  // a task still waiting for its CM round trip
        Queue* queue = &rig.svc->queue();
        rig.svc.reset();                                // from here only the hub keeps the service alive
        rig.hub.set(nullptr);
        CHECK(queue->pending() == 0);
        rig.drain();                                    // the late task posts nothing and does not crash
        CHECK(queue->pending() == 0);
        // The held message is still readable until the game frees it (ASAN
        // reports a freed buffer here).
        CHECK(std::vector<uint8_t>(msg.m_pubParam, msg.m_pubParam + msg.m_cubParam) == held);
        rig.hub.free_last(p_free, kPipe);               // now the provider's own (harmless) free
        CHECK(P.release == 1 && P.bad_release == 1);
        // Everything reaches the provider again.
        CHECK(rig.hub.create_lobby(p_create, nullptr, 2, 4) == 0x501 && P.create == 1);
        CHECK(rig.hub.request_lobby_list(p_list, nullptr) == 0x502);
        bool failed = false; char buffer[16];
        CHECK(!rig.hub.api_call_result(p_result, kPipe, call, buffer, 16, 513, &failed) && P.result == 1 && P.call == call);
        // The handle that was still in flight at teardown is the provider's to refuse too.
        CHECK(!rig.hub.api_call_result(p_result, kPipe, late, buffer, 16, 513, &failed) && P.result == 2 && P.call == late);
        P.queue = {1100};
        CHECK(rig.hub.next(p_next, kPipe, &msg) && msg.m_iCallback == 1100);
        rig.hub.free_last(p_free, kPipe);
        // A replacement service starts clean.
        Rig second;
        rig.hub.set(second.svc);
        CHECK(rig.hub.pipe(p_pipe) == kPipe);
        CHECK(!second.svc->active());                   // it has not seen this game's Init yet
        rig.hub.set(nullptr);
    }
    {   // a CM round trip running on another thread while the tap is cleared
        Rig rig(false);
        rig.game_starts();
        std::mutex gate_m; std::condition_variable gate_cv;
        bool inside = false, release = false;
        rig.cm.on_call = [&] {
            std::unique_lock<std::mutex> lock(gate_m);
            inside = true; gate_cv.notify_all();
            gate_cv.wait(lock, [&] { return release; });
        };
        std::thread worker([&] { rig.hub.create_lobby(p_create, nullptr, 2, 4); });
        { std::unique_lock<std::mutex> lock(gate_m); gate_cv.wait(lock, [&] { return inside; }); }
        rig.hub.set(nullptr);                           // cleared with the request on the wire
        { std::lock_guard<std::mutex> lock(gate_m); release = true; } gate_cv.notify_all();
        worker.join();
        CHECK(rig.svc->queue().pending() == 0);
        CHECK(rig.hub.create_lobby(p_create, nullptr, 2, 4) == 0x501);
    }
}

}  // namespace

int main() {
    passthrough_without_tap();
    passthrough_without_manual_dispatch();
    launcher_handles();
    call_results();
    free_and_ordering();
    create_lobby();
    list_join_leave();
    teardown_in_flight();
    puts("STEAM-MATCHMAKING PASS: Win64 callback layouts, launcher handles, ManualDispatch splice, "
         "create/list/join/leave results, pass-through without a tap or ManualDispatch, teardown in flight");
    return 0;
}
