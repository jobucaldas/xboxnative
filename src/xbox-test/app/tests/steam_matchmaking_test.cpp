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
//   11 lobby data, members, owner, game server, chat    14 SerialExecutor
//   12 list filters and listed lobbies                  15 KeyValues blobs
//   13 CM pushes -> broadcast callbacks                 16 tap replacement, retired buffers, Hub memory
//   17 the phase-2 exports pass through without a tap or ManualDispatch
// (8 and 9, the pinned provider's own behaviour, live in steam-matchmaking-gbe.sh.)
#include "../steam_matchmaking.h"
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    std::vector<std::string> set_data, list_body, data_body, chat_body, server_body, owner_body;   // each request seen
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
            std::string member, peer;
            pb_fixed64(member, 1, kOwnerId); pb_s(member, 2, "owner");
            pb_s(member, 3, steam_lobbies::kv_encode({{"role", "host"}}));
            pb_fixed64(peer, 1, kPeerId); pb_s(peer, 2, "peer");
            pb_u(resp, 1, kAppId); pb_fixed64(resp, 2, next_id); pb_u(resp, 4, 4); pb_u(resp, 5, 2);
            pb_fixed64(resp, 7, kOwnerId);
            pb_s(resp, 8, steam_lobbies::kv_encode({{"mode", "coop"}, {"map", "alpha"}}));
            pb_s(resp, 9, member); pb_s(resp, 9, peer);
            data_body.push_back(body);
            return true;
        }
        case steam_lobbies::SetLobbyData:
            set_data.push_back(body);
            pb_u(resp, 1, kAppId); pb_u(resp, 3, steam_lobbies::ResultOK);
            return true;
        case steam_lobbies::SetLobbyOwner:
            owner_body.push_back(body);
            pb_u(resp, 1, kAppId); pb_u(resp, 3, steam_lobbies::ResultOK);
            return true;
        case steam_lobbies::SendLobbyChatMsg: chat_body.push_back(body); return true;      // no response of its own
        case steam_lobbies::SetLobbyGameServer: server_body.push_back(body); return true;
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
            list_body.push_back(body);
            pb_u(resp, 1, kAppId); pb_u(resp, 3, uint64_t(int64_t(list_eresult)));
            for (auto id : listed) {
                std::string row;
                pb_fixed64(row, 1, id); pb_u(row, 2, 8);
                pb_s(row, 5, steam_lobbies::kv_encode({{"name", "row"}}));
                pb_u(row, 6, 3);
                pb_s(resp, 4, row);
            }
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
        config.self_id = kOwnerId;
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
        // A replacement service starts clean (nothing queued), and the hub hands
        // it what the game already did, so it is active without a second Init.
        Rig second;
        rig.hub.set(second.svc);
        CHECK(second.svc->active() && second.svc->queue().pending() == 0);
        CHECK(rig.hub.pipe(p_pipe) == kPipe);
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


// ---- helpers for the phase-2 sections
// A rig that has created a lobby: the game owns it, with the fake CM's two members.
struct Lobbied {
    Rig rig;
    uint64_t lobby = 0;
    explicit Lobbied(bool manual = false) : rig(manual) {
        rig.game_starts();
        rig.hub.create_lobby(p_create, nullptr, 2, 4);
        if (manual) rig.drain();
        lobby = rig.bridge->lobby;
        CHECK(lobby != 0);
        pump(rig.hub);
    }
    Hub& hub() { return rig.hub; }
};
std::map<std::string, std::string> kv_of(const std::string& body, int field) {
    auto fields = pb_parse(body);
    auto f = pb_get(fields, field);
    return f ? steam_lobbies::kv_decode(f->s) : std::map<std::string, std::string>{};
}
// What the CM pushes, as the CM reader hands it over.
std::string member_row(uint64_t id, const char* persona, const std::map<std::string, std::string>& kv = {}) {
    std::string row;
    pb_fixed64(row, 1, id); pb_s(row, 2, persona);
    if (!kv.empty()) pb_s(row, 3, steam_lobbies::kv_encode(kv));
    return row;
}
std::string lobby_data_push(uint64_t lobby, uint64_t owner, const std::map<std::string, std::string>& kv,
                            const std::vector<std::string>& members, int max = 4, int type = 2) {
    std::string b;
    pb_u(b, 1, kAppId); pb_fixed64(b, 2, lobby); pb_u(b, 4, uint64_t(max)); pb_u(b, 5, uint64_t(type));
    pb_fixed64(b, 7, owner); pb_s(b, 8, steam_lobbies::kv_encode(kv));
    for (auto& m : members) pb_s(b, 9, m);
    return b;
}
std::string membership_push(uint64_t lobby, uint64_t user, const char* persona) {
    std::string b; pb_u(b, 1, kAppId); pb_fixed64(b, 2, lobby); pb_fixed64(b, 3, user); pb_s(b, 4, persona); return b;
}
std::string chat_push(uint64_t lobby, uint64_t sender, const std::string& text) {
    std::string b; pb_u(b, 1, kAppId); pb_fixed64(b, 2, lobby); pb_fixed64(b, 3, sender); pb_s(b, 4, text); return b;
}
std::string server_push(uint64_t lobby, uint32_t ip, uint32_t port, uint64_t server) {
    std::string b; pb_u(b, 1, kAppId); pb_fixed64(b, 2, lobby); pb_u(b, 4, port); pb_fixed64(b, 5, server);
    pb_s(b, 6, steam_lobbies::ip_address(ip)); return b;
}

// ---- 11: lobby data, members, owner, game server and chat answered from the CM state
void lobby_data_members_chat() {
    reset_provider();
    Lobbied g;
    Hub& hub = g.hub(); Cm& cm = g.rig.cm; const uint64_t lobby = g.lobby;

    // Reads. Two keys in one expression must both stay readable.
    const char* mode = hub.lobby_data(nullptr, nullptr, lobby, "mode");
    const char* map = hub.lobby_data(nullptr, nullptr, lobby, "map");
    CHECK(!strcmp(mode, "coop") && !strcmp(map, "alpha") && mode != map);
    CHECK(!strcmp(hub.lobby_data(nullptr, nullptr, lobby, "absent"), ""));
    CHECK(!strcmp(hub.lobby_data(nullptr, nullptr, lobby + 1, "mode"), "") && !strcmp(hub.lobby_data(nullptr, nullptr, lobby, nullptr), ""));
    CHECK(hub.lobby_data_count(nullptr, nullptr, lobby) == 2 && hub.lobby_data_count(nullptr, nullptr, lobby + 1) == 0);
    char key[16], value[16];
    CHECK(hub.lobby_data_by_index(nullptr, nullptr, lobby, 0, key, sizeof key, value, sizeof value));
    CHECK(!strcmp(key, "map") && !strcmp(value, "alpha"));            // sorted, so stable
    CHECK(hub.lobby_data_by_index(nullptr, nullptr, lobby, 1, key, 3, value, 2));
    CHECK(!strcmp(key, "mo") && !strcmp(value, "c"));                  // truncated, always terminated
    CHECK(!hub.lobby_data_by_index(nullptr, nullptr, lobby, 2, key, sizeof key, value, sizeof value));
    CHECK(!hub.lobby_data_by_index(nullptr, nullptr, lobby, -1, key, sizeof key, value, sizeof value));
    CHECK(hub.lobby_data_by_index(nullptr, nullptr, lobby, 0, nullptr, 0, nullptr, 0));   // sizes only, never a write

    // Writes: the owner's copy changes at once, and the CM is told the whole blob.
    CHECK(hub.set_lobby_data(nullptr, nullptr, lobby, "mode", "pvp"));
    CHECK(!strcmp(hub.lobby_data(nullptr, nullptr, lobby, "mode"), "pvp"));
    CHECK(cm.set_data.size() == 1 && kv_of(cm.set_data[0], 7) == (std::map<std::string, std::string>{{"map", "alpha"}, {"mode", "pvp"}}));
    {
        auto fields = pb_parse(cm.set_data[0]);
        CHECK(pb_get(fields, 2) && pb_get(fields, 2)->u == lobby && !pb_get(fields, 3));   // lobby data, not member data
    }
    CHECK(!hub.set_lobby_data(nullptr, nullptr, lobby, "", "x") && !hub.set_lobby_data(nullptr, nullptr, lobby, nullptr, "x"));
    CHECK(!hub.set_lobby_data(nullptr, nullptr, lobby + 1, "mode", "x"));                  // not the lobby it is in
    CHECK(hub.set_lobby_data(nullptr, nullptr, lobby, "blank", nullptr) && !strcmp(hub.lobby_data(nullptr, nullptr, lobby, "blank"), ""));
    CHECK(hub.delete_lobby_data(nullptr, nullptr, lobby, "blank") && !hub.delete_lobby_data(nullptr, nullptr, lobby, "blank"));
    CHECK(hub.delete_lobby_data(nullptr, nullptr, lobby, "map") && hub.lobby_data_count(nullptr, nullptr, lobby) == 1);
    CHECK(kv_of(cm.set_data.back(), 7) == (std::map<std::string, std::string>{{"mode", "pvp"}}));
    CHECK(!hub.set_lobby_data(nullptr, nullptr, lobby, std::string(256, 'k').c_str(), "x"));
    CHECK(!hub.set_lobby_data(nullptr, nullptr, lobby, "k", std::string(8193, 'v').c_str()));

    // Member data: anyone's is readable, only the account's own is writable.
    CHECK(!strcmp(hub.lobby_member_data(nullptr, nullptr, lobby, kOwnerId, "role"), "host"));
    CHECK(!strcmp(hub.lobby_member_data(nullptr, nullptr, lobby, kPeerId, "role"), ""));
    hub.set_lobby_member_data(nullptr, nullptr, lobby, "ready", "1");
    CHECK(!strcmp(hub.lobby_member_data(nullptr, nullptr, lobby, kOwnerId, "ready"), "1"));
    {
        auto fields = pb_parse(cm.set_data.back());
        CHECK(pb_get(fields, 3) && pb_get(fields, 3)->u == kOwnerId);
        CHECK(kv_of(cm.set_data.back(), 7) == (std::map<std::string, std::string>{{"ready", "1"}, {"role", "host"}}));
    }
    const size_t sent = cm.set_data.size();
    hub.set_lobby_member_data(nullptr, nullptr, lobby + 1, "x", "y");
    CHECK(cm.set_data.size() == sent);

    // Members, owner, limit, type.
    CHECK(hub.num_lobby_members(nullptr, nullptr, lobby) == 2);
    CHECK(hub.lobby_member_by_index(nullptr, nullptr, lobby, 0) == kOwnerId && hub.lobby_member_by_index(nullptr, nullptr, lobby, 1) == kPeerId);
    for (int bad : {-1, 2, 99}) CHECK(hub.lobby_member_by_index(nullptr, nullptr, lobby, bad) == 0);
    CHECK(hub.lobby_owner(nullptr, nullptr, lobby) == kOwnerId && hub.lobby_owner(nullptr, nullptr, lobby + 1) == 0);
    CHECK(hub.lobby_member_limit(nullptr, nullptr, lobby) == 4);
    CHECK(hub.set_lobby_member_limit(nullptr, nullptr, lobby, 6) && hub.lobby_member_limit(nullptr, nullptr, lobby) == 6);
    CHECK(!hub.set_lobby_member_limit(nullptr, nullptr, lobby, -1));
    {
        auto fields = pb_parse(cm.set_data.back());
        CHECK(pb_get(fields, 4) && pb_get(fields, 4)->u == 6 && !pb_get(fields, 5));
    }
    CHECK(hub.set_lobby_type(nullptr, nullptr, lobby, steam_lobbies::LobbyTypeFriendsOnly) && g.rig.bridge->lobby_type == 1);
    {
        auto fields = pb_parse(cm.set_data.back());
        CHECK(pb_get(fields, 5) && pb_get(fields, 5)->u == 1 && !pb_get(fields, 4));
    }
    CHECK(!hub.set_lobby_type(nullptr, nullptr, lobby, 4) && !hub.set_lobby_type(nullptr, nullptr, lobby, -1));
    CHECK(!hub.set_lobby_joinable(nullptr, nullptr, lobby, false));    // unverified wire semantics: refused, not guessed
    CHECK(!hub.set_lobby_owner(nullptr, nullptr, lobby, 0x123));       // not a member
    CHECK(hub.set_lobby_owner(nullptr, nullptr, lobby, kPeerId) && cm.owner_body.size() == 1);
    CHECK(g.rig.bridge->owner == kPeerId);
    // Not the owner any more: every owner-only write is refused and sends nothing.
    const size_t after = cm.set_data.size();
    CHECK(!hub.set_lobby_data(nullptr, nullptr, lobby, "mode", "x") && !hub.delete_lobby_data(nullptr, nullptr, lobby, "mode"));
    CHECK(!hub.set_lobby_type(nullptr, nullptr, lobby, 2) && !hub.set_lobby_member_limit(nullptr, nullptr, lobby, 2));
    CHECK(!hub.set_lobby_owner(nullptr, nullptr, lobby, kOwnerId));
    hub.set_lobby_game_server(nullptr, nullptr, lobby, 1, 2, 3);
    CHECK(cm.set_data.size() == after && cm.server_body.empty());

    // Game server: set by the owner, read back, sent with the address and port.
    Lobbied h;
    uint32_t ip = 9; uint16_t port = 9; uint64_t sid = 9;
    CHECK(!h.hub().lobby_game_server(nullptr, nullptr, h.lobby, &ip, &port, &sid) && ip == 9 && port == 9 && sid == 9);
    h.hub().set_lobby_game_server(nullptr, nullptr, h.lobby, 0x7f000001, 27015, 0x1860000000042ull);
    CHECK(h.hub().lobby_game_server(nullptr, nullptr, h.lobby, &ip, &port, &sid));
    CHECK(ip == 0x7f000001 && port == 27015 && sid == 0x1860000000042ull);
    CHECK(h.hub().lobby_game_server(nullptr, nullptr, h.lobby, nullptr, nullptr, nullptr));   // any may be null
    CHECK(!h.hub().lobby_game_server(nullptr, nullptr, h.lobby + 1, &ip, &port, &sid));
    {
        auto got = steam_lobbies::parse_server_set(h.rig.cm.server_body.at(0));
        CHECK(got.lobby == h.lobby && got.ipv4 == 0x7f000001 && got.port == 27015 && got.steamid == 0x1860000000042ull);
    }

    // Chat: sent to the CM, bounded, and only into the lobby the account is in.
    const char hello[] = "hello";
    CHECK(h.hub().send_lobby_chat(nullptr, nullptr, h.lobby, hello, 5));
    CHECK(h.rig.cm.chat_body.size() == 1 && steam_lobbies::parse_chat(h.rig.cm.chat_body[0]).message == "hello");
    CHECK(!h.hub().send_lobby_chat(nullptr, nullptr, h.lobby, hello, 0) && !h.hub().send_lobby_chat(nullptr, nullptr, h.lobby, nullptr, 5));
    CHECK(!h.hub().send_lobby_chat(nullptr, nullptr, h.lobby + 1, hello, 5));
    std::string big(steam_lobbies::MaxChatMsg + 1, 'x');
    CHECK(!h.hub().send_lobby_chat(nullptr, nullptr, h.lobby, big.data(), int(big.size())));
    CHECK(h.rig.cm.chat_body.size() == 1);
    char out[8]; uint64_t user = 0; int32_t type = -1;
    CHECK(h.hub().lobby_chat_entry(nullptr, nullptr, h.lobby, 0, &user, out, sizeof out, &type) == 0 && user == 0 && type == -1);   // nothing received yet

    // The ring: sixteen answers stay valid at once.
    const char* kept[16];
    for (int i = 0; i < 16; ++i) kept[i] = g.hub().lobby_data(nullptr, nullptr, lobby, i % 2 ? "mode" : "absent");
    for (int i = 0; i < 16; ++i) CHECK(!strcmp(kept[i], i % 2 ? "pvp" : ""));
}

// ---- 12: list filters, and lobbies from the last list
void filters_and_listed_lobbies() {
    reset_provider();
    Rig rig(false);
    rig.game_starts();
    rig.cm.listed = {0x1100001000010ull, 0x1100001000020ull};
    Hub& hub = rig.hub;
    hub.string_filter(nullptr, nullptr, "mode", "coop", steam_lobbies::CompareEqual);
    hub.numerical_filter(nullptr, nullptr, "level", 7, steam_lobbies::CompareEqualOrGreater);
    hub.near_value_filter(nullptr, nullptr, "skill", 1200);
    hub.slots_available_filter(nullptr, nullptr, 2);
    hub.distance_filter(nullptr, nullptr, 3);
    hub.compatible_members_filter(nullptr, nullptr, kPeerId);
    hub.string_filter(nullptr, nullptr, "", "dropped", 0);
    hub.result_count_filter(nullptr, nullptr, 7);
    hub.request_lobby_list(p_list, nullptr);
    pump(hub);
    CHECK(rig.cm.list_body.size() == 1 && P.list == 0);
    {
        auto fields = pb_parse(rig.cm.list_body[0]);
        CHECK(pb_get(fields, 3) && pb_get(fields, 3)->u == 7);
        std::vector<steam_lobbies::Filter> got;
        for (auto& f : fields) {
            if (f.f != 6) continue;
            steam_lobbies::Filter flt;
            for (auto& x : pb_parse(f.s)) {
                if (x.f == 1) flt.key = x.s; else if (x.f == 2) flt.value = x.s;
                else if (x.f == 3) flt.comparison = int(int64_t(x.u)); else if (x.f == 4) flt.type = int(x.u);
            }
            got.push_back(flt);
        }
        CHECK(got.size() == 4);
        CHECK(got[0].key == "mode" && got[0].value == "coop" && got[0].type == steam_lobbies::FilterString);
        CHECK(got[1].key == "level" && got[1].value == "7" && got[1].type == steam_lobbies::FilterNumerical &&
              got[1].comparison == steam_lobbies::CompareEqualOrGreater);
        CHECK(got[2].key == "skill" && got[2].value == "1200" && got[2].type == steam_lobbies::FilterNearValue);
        CHECK(got[3].value == "2" && got[3].type == steam_lobbies::FilterSlotsAvailable);
    }
    // Spent by that request: the next one starts clean, with the default count.
    hub.request_lobby_list(p_list, nullptr); pump(hub);
    {
        auto fields = pb_parse(rig.cm.list_body.at(1));
        CHECK(!pb_get(fields, 6) && pb_get(fields, 3) && pb_get(fields, 3)->u == 50);
    }
    // Filters are bounded.
    for (int i = 0; i < 200; ++i) hub.near_value_filter(nullptr, nullptr, "k", i);
    hub.request_lobby_list(p_list, nullptr); pump(hub);
    size_t count = 0;
    for (auto& f : pb_parse(rig.cm.list_body.at(2))) count += f.f == 6;
    CHECK(count == 64);

    // The listed lobbies answer from their rows, though the account is in none.
    const uint64_t first = rig.cm.listed[0];
    CHECK(!strcmp(hub.lobby_data(nullptr, nullptr, first, "name"), "row"));
    CHECK(hub.lobby_data_count(nullptr, nullptr, first) == 1);
    CHECK(hub.num_lobby_members(nullptr, nullptr, first) == 3 && hub.lobby_member_limit(nullptr, nullptr, first) == 8);
    CHECK(hub.lobby_member_by_index(nullptr, nullptr, first, 0) == 0 && hub.lobby_owner(nullptr, nullptr, first) == 0);
    CHECK(!hub.set_lobby_data(nullptr, nullptr, first, "name", "x") && !hub.send_lobby_chat(nullptr, nullptr, first, "x", 1));
    CHECK(hub.num_lobby_members(nullptr, nullptr, 0x77) == 0 && hub.lobby_member_limit(nullptr, nullptr, 0x77) == 0);
    // A newer list replaces the rows.
    rig.cm.listed = {0x1100001000030ull};
    hub.request_lobby_list(p_list, nullptr); pump(hub);
    CHECK(!strcmp(hub.lobby_data(nullptr, nullptr, first, "name"), "") && !strcmp(hub.lobby_data(nullptr, nullptr, rig.cm.listed[0], "name"), "row"));

    // RequestLobbyData: a LobbyDataUpdate_t either way.
    CHECK(hub.request_lobby_data(nullptr, nullptr, rig.cm.listed[0]));
    auto got = pump(hub);
    CHECK(got.size() == 1 && got[0].callback == 505);
    auto update = as<LobbyDataUpdate_t>(got[0]);
    CHECK(update.m_ulSteamIDLobby == rig.cm.listed[0] && update.m_ulSteamIDMember == rig.cm.listed[0] && update.m_bSuccess == 1);
    CHECK(!strcmp(hub.lobby_data(nullptr, nullptr, rig.cm.listed[0], "mode"), "coop"));   // the row now carries the fresh data
    rig.cm.down = true;
    CHECK(hub.request_lobby_data(nullptr, nullptr, rig.cm.listed[0]) && !hub.request_lobby_data(nullptr, nullptr, 0));
    got = pump(hub);
    CHECK(got.size() == 1 && got[0].callback == 505 && as<LobbyDataUpdate_t>(got[0]).m_bSuccess == 0);
}

// ---- 13: what the CM pushes becomes the callbacks a game waits for
void cm_pushes() {
    reset_provider();
    Lobbied g;
    Hub& hub = g.hub(); const uint64_t lobby = g.lobby;
    auto push = [&](uint32_t emsg, const std::string& body) { return hub.on_push(emsg, body); };

    // Chat and membership.
    CHECK(push(steam_lobbies::UserJoinedLobby, membership_push(lobby, 0x777, "guest")));
    auto got = pump(hub);
    CHECK(got.size() == 1 && got[0].callback == 506 && got[0].data.size() == 32);
    auto joined = as<LobbyChatUpdate_t>(got[0]);
    CHECK(joined.m_ulSteamIDLobby == lobby && joined.m_ulSteamIDUserChanged == 0x777 && joined.m_ulSteamIDMakingChange == 0x777 &&
          joined.m_rgfChatMemberStateChange == ChatMemberEntered);
    CHECK(hub.num_lobby_members(nullptr, nullptr, lobby) == 3);
    CHECK(push(steam_lobbies::UserLeftLobby, membership_push(lobby, 0x777, "guest")));
    got = pump(hub);
    CHECK(got.size() == 1 && as<LobbyChatUpdate_t>(got[0]).m_rgfChatMemberStateChange == ChatMemberLeft);
    CHECK(hub.num_lobby_members(nullptr, nullptr, lobby) == 2);
    CHECK(push(steam_lobbies::UserLeftLobby, membership_push(lobby, kPeerId, "peer")));   // another member leaves
    pump(hub);
    CHECK(hub.num_lobby_members(nullptr, nullptr, lobby) == 1);
    // A lobby the account is not in changes nothing and posts nothing.
    CHECK(push(steam_lobbies::UserJoinedLobby, membership_push(lobby + 9, 0x888, "x")) && pump(hub).empty());

    // Chat messages: ids count up, entries are readable by id.
    CHECK(push(steam_lobbies::LobbyChatMsg, chat_push(lobby, kPeerId, "hi there")));
    CHECK(push(steam_lobbies::LobbyChatMsg, chat_push(lobby, 0x777, "second")));
    CHECK(push(steam_lobbies::LobbyChatMsg, chat_push(lobby + 9, 0x777, "elsewhere")));
    got = pump(hub);
    CHECK(got.size() == 2 && got[0].callback == 507 && got[0].data.size() == 24);
    auto first = as<LobbyChatMsg_t>(got[0]), second = as<LobbyChatMsg_t>(got[1]);
    CHECK(first.m_ulSteamIDLobby == lobby && first.m_ulSteamIDUser == kPeerId && first.m_eChatEntryType == 1 && first.m_iChatID == 0);
    CHECK(second.m_iChatID == 1);
    char text[32]; uint64_t user = 0; int32_t type = 0;
    CHECK(hub.lobby_chat_entry(nullptr, nullptr, lobby, 0, &user, text, sizeof text, &type) == 8 && !memcmp(text, "hi there", 8));
    CHECK(user == kPeerId && type == 1);
    CHECK(hub.lobby_chat_entry(nullptr, nullptr, lobby, 1, nullptr, text, 3, nullptr) == 3 && !memcmp(text, "sec", 3));   // truncated to the buffer
    CHECK(hub.lobby_chat_entry(nullptr, nullptr, lobby, 2, &user, text, sizeof text, &type) == 0);
    CHECK(hub.lobby_chat_entry(nullptr, nullptr, lobby + 9, 0, &user, text, sizeof text, &type) == 0);
    CHECK(hub.lobby_chat_entry(nullptr, nullptr, lobby, 0, &user, nullptr, 0, &type) == 0);

    // Pushed lobby data: a room update for changed lobby data, one per member whose data changed.
    auto owner = member_row(kOwnerId, "owner", {{"role", "host"}});
    CHECK(push(steam_lobbies::LobbyData, lobby_data_push(lobby, kOwnerId, {{"mode", "coop"}, {"map", "alpha"}}, {owner})) && pump(hub).empty());   // unchanged
    CHECK(push(steam_lobbies::LobbyData, lobby_data_push(lobby, kOwnerId, {{"mode", "race"}}, {owner, member_row(kPeerId, "peer", {{"team", "red"}})})));
    got = pump(hub);
    CHECK(got.size() == 2 && got[0].callback == 505 && got[1].callback == 505 && got[0].data.size() == 24);
    auto room = as<LobbyDataUpdate_t>(got[0]), member = as<LobbyDataUpdate_t>(got[1]);
    CHECK(room.m_ulSteamIDLobby == lobby && room.m_ulSteamIDMember == lobby && room.m_bSuccess == 1);
    CHECK(member.m_ulSteamIDLobby == lobby && member.m_ulSteamIDMember == kPeerId && member.m_bSuccess == 1);
    CHECK(!strcmp(hub.lobby_data(nullptr, nullptr, lobby, "mode"), "race") && !strcmp(hub.lobby_data(nullptr, nullptr, lobby, "map"), ""));
    CHECK(!strcmp(hub.lobby_member_data(nullptr, nullptr, lobby, kPeerId, "team"), "red"));
    CHECK(hub.num_lobby_members(nullptr, nullptr, lobby) == 2);
    // A new owner, announced by the data push.
    CHECK(push(steam_lobbies::LobbyData, lobby_data_push(lobby, kPeerId, {{"mode", "race"}}, {owner, member_row(kPeerId, "peer", {{"team", "red"}})})));
    got = pump(hub);
    CHECK(got.size() == 1 && as<LobbyDataUpdate_t>(got[0]).m_ulSteamIDMember == lobby && hub.lobby_owner(nullptr, nullptr, lobby) == kPeerId);
    CHECK(push(steam_lobbies::LobbyData, lobby_data_push(lobby + 9, kPeerId, {{"x", "y"}}, {})) && pump(hub).empty());

    // The game server the lobby advertises.
    uint32_t ip = 0; uint16_t port = 0; uint64_t sid = 0;
    CHECK(push(steam_lobbies::LobbyGameServerSet, server_push(lobby, 0x0a000002, 4000, 0x1860000000099ull)));
    got = pump(hub);
    CHECK(got.size() == 1 && got[0].callback == 509 && got[0].data.size() == 24);
    auto made = as<LobbyGameCreated_t>(got[0]);
    CHECK(made.m_ulSteamIDLobby == lobby && made.m_ulSteamIDGameServer == 0x1860000000099ull && made.m_unIP == 0x0a000002 && made.m_usPort == 4000);
    CHECK(hub.lobby_game_server(nullptr, nullptr, lobby, &ip, &port, &sid) && ip == 0x0a000002 && port == 4000 && sid == 0x1860000000099ull);
    CHECK(push(steam_lobbies::LobbyGameServerSet, server_push(lobby + 9, 1, 2, 3)) && pump(hub).empty());

    // Not a lobby push: not ours.
    CHECK(!push(6000, "") && !push(steam_lobbies::CreateLobbyResponse, ""));
    CHECK(Matchmaking::is_push(6612) && Matchmaking::is_push(6614) && Matchmaking::is_push(6618) &&
          Matchmaking::is_push(6619) && Matchmaking::is_push(6620) && !Matchmaking::is_push(6613));
    // Garbage never crashes and posts nothing.
    for (uint32_t emsg : {6612u, 6614u, 6618u, 6619u, 6620u}) CHECK(push(emsg, std::string("\xff\xff\xff", 3)) && pump(hub).empty());

    // Nothing is queued for a game that is not reading launcher callbacks.
    Rig idle(false);
    CHECK(!idle.hub.on_push(steam_lobbies::LobbyChatMsg, chat_push(1, 2, "x")) && idle.svc->queue().pending() == 0);
    // A queue the game never drains is bounded.
    Lobbied flood;
    for (int i = 0; i < 5000; ++i) flood.hub().on_push(steam_lobbies::LobbyChatMsg, chat_push(flood.lobby, kPeerId, "spam"));
    // pending() also counts the create's stored call result, which the game never took.
    CHECK(flood.rig.svc->queue().pending() <= Queue::MaxQueued + 1);
    // The chat history is bounded too: the oldest entry is gone, the newest is there.
    char one[8]; uint64_t who = 0; int32_t kind = 0;
    CHECK(flood.hub().lobby_chat_entry(nullptr, nullptr, flood.lobby, 0, &who, one, sizeof one, &kind) == 0);
    CHECK(flood.hub().lobby_chat_entry(nullptr, nullptr, flood.lobby, 4999, &who, one, sizeof one, &kind) == 4);

    // After the tap is cleared a push is ignored.
    g.rig.hub.set(nullptr);
    CHECK(!push(steam_lobbies::LobbyChatMsg, chat_push(lobby, 1, "late")));
}

// ---- 14: the serial executor
void serial_executor() {
    std::mutex m; std::condition_variable cv;
    std::vector<int> order; bool inited = false;
    {
        SerialExecutor exec([&] { std::lock_guard<std::mutex> lock(m); inited = true; });
        auto post = exec.executor();
        for (int i = 0; i < 100; ++i) post([&, i] { std::lock_guard<std::mutex> lock(m); order.push_back(i); cv.notify_all(); });
        std::unique_lock<std::mutex> lock(m);
        CHECK(cv.wait_for(lock, std::chrono::seconds(10), [&] { return order.size() == 100; }));
        CHECK(inited);
        for (int i = 0; i < 100; ++i) CHECK(order[size_t(i)] == i);   // submission order
    }
    // A task that throws does not kill the worker; stop() drops what has not started and never waits.
    SerialExecutor exec;
    auto post = exec.executor();
    std::mutex gate_m; std::condition_variable gate_cv; bool started = false, release = false;
    std::atomic<int> ran{0};
    post([&] {
        std::unique_lock<std::mutex> lock(gate_m);
        started = true; gate_cv.notify_all();
        gate_cv.wait(lock, [&] { return release; });
        throw 1;
    });
    { std::unique_lock<std::mutex> lock(gate_m); gate_cv.wait(lock, [&] { return started; }); }
    post([&] { ++ran; });
    exec.stop();                                           // the first task is still running
    { std::lock_guard<std::mutex> lock(gate_m); release = true; } gate_cv.notify_all();
    post([&] { ++ran; });                                  // a stopped executor takes nothing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(ran == 0);
}

// ---- 15: KeyValues blobs
void key_values() {
    using KV = std::map<std::string, std::string>;
    KV in{{"a", "1"}, {"b", ""}, {"key with space", "value=with;chars"}};
    CHECK(steam_lobbies::kv_decode(steam_lobbies::kv_encode(in)) == in);
    CHECK(steam_lobbies::kv_decode(steam_lobbies::kv_encode({})).empty() && steam_lobbies::kv_decode("").empty());
    CHECK(steam_lobbies::kv_encode({{"k", "v"}}) == std::string("\x01k\0v\0\x08", 6));
    // A sub-key header around the pairs, and values of other types, read as text.
    std::string wrapped("\x00" "root\0", 6);
    wrapped += std::string("\x01s\0txt\0", 7);
    int32_t i32 = -5; wrapped += std::string("\x02i\0", 3) + std::string(reinterpret_cast<char*>(&i32), 4);
    uint64_t u64 = 1ull << 40; wrapped += std::string("\x07u\0", 3) + std::string(reinterpret_cast<char*>(&u64), 8);
    wrapped += std::string("\x08\x08", 2);
    CHECK(steam_lobbies::kv_decode(wrapped) == (KV{{"s", "txt"}, {"i", "-5"}, {"u", "1099511627776"}}));
    // Truncated or hostile blobs stop cleanly, keeping what was whole.
    CHECK(steam_lobbies::kv_decode(std::string("\x01k\0v", 4)).empty());
    CHECK(steam_lobbies::kv_decode(std::string("\x01k\0v\0\x01z", 7)) == (KV{{"k", "v"}}));
    CHECK(steam_lobbies::kv_decode(std::string("\x02n\0\x01\x02", 5)).empty());
    for (unsigned seed = 0; seed < 2000; ++seed) {
        std::string junk; unsigned x = seed * 2654435761u + 1;
        for (int n = 0; n < 40; ++n) { x = x * 1664525u + 1013904223u; junk += char(x >> 24); }
        steam_lobbies::kv_decode(junk);
    }
}

// ---- 16: tap replacement while a launcher message is outstanding, and what the hub remembers
void tap_replacement() {
    reset_provider();
    {   // The game's Init and pipe came before any tap: a tap installed afterwards is active at once.
        Hub hub;
        hub.init(p_init); CHECK(hub.pipe(p_pipe) == kPipe);
        Rig rig;                                         // its own hub; its service is reused below
        hub.set(rig.svc);
        CHECK(rig.svc->active());
        CHECK(is_launcher_handle(hub.create_lobby(p_create, nullptr, 2, 4)) && P.create == 0);
        // Re-installing never loses it, and clearing then installing again neither.
        Rig again;
        hub.set(again.svc); CHECK(again.svc->active());
        hub.set(nullptr);
        Rig third;
        hub.set(third.svc); CHECK(third.svc->active());
        // A failed pipe lookup (0) is not remembered over a good one.
        CHECK(hub.pipe([]() -> int32_t { return 0; }) == 0 && third.svc->active());
    }
    {   // Rapid replacement while the game holds a launcher message: the buffer survives until it is freed.
        Rig rig(false);
        rig.game_starts();
        rig.hub.create_lobby(p_create, nullptr, 2, 4);
        CallbackMsg_t msg{};
        CHECK(rig.hub.next(p_next, kPipe, &msg) && msg.m_iCallback == 703);
        const std::vector<uint8_t> held(msg.m_pubParam, msg.m_pubParam + msg.m_cubParam);
        rig.svc.reset();
        std::vector<std::unique_ptr<Rig>> later;
        for (int i = 0; i < 12; ++i) {                  // more replacements than a lone "retired" slot could cover
            later.push_back(std::make_unique<Rig>(false));
            rig.hub.set(later.back()->svc);
            CHECK(std::vector<uint8_t>(msg.m_pubParam, msg.m_pubParam + msg.m_cubParam) == held);   // ASAN: freed buffer
        }
        rig.hub.set(nullptr);
        CHECK(std::vector<uint8_t>(msg.m_pubParam, msg.m_pubParam + msg.m_cubParam) == held);
        rig.hub.free_last(p_free, kPipe);               // the game is done; the retired services may go
        later.clear();
        rig.hub.set(nullptr);
    }
    {   // Teardown with a held message of a launcher service nobody else references.
        Hub hub;
        auto make = [&] {
            Config config;
            config.io.call = [](uint32_t, uint32_t, const std::string&, std::string&) { return false; };
            config.appid = kAppId;
            return std::make_shared<Service>(config);
        };
        hub.init(p_init); hub.pipe(p_pipe);
        auto svc = make();
        hub.set(svc);
        svc->queue().broadcast(504, "abcd", 4);
        svc.reset();
        CallbackMsg_t msg{};
        CHECK(hub.next(p_next, kPipe, &msg) && msg.m_iCallback == 504);
        for (int i = 0; i < 5; ++i) hub.set(make());   // each replacement drops the one before it from the hub
        hub.set(nullptr);
        CHECK(!memcmp(msg.m_pubParam, "abcd", 4));
        hub.free_last(p_free, kPipe);
    }
}

// ---- 17: the phase-2 exports reach the provider, unchanged, without a tap or a ManualDispatch game
struct Phase2 { int calls = 0; uint64_t lobby = 0; } Q;
void passthrough_phase2(Hub& hub, int expected_calls) {
    void* self = reinterpret_cast<void*>(0x4242);
    Q = Phase2();
    CHECK(!strcmp(hub.lobby_data([](void*, uint64_t l, const char* k) -> const char* { Q.lobby = l; ++Q.calls; return k; }, self, 5, "key"), "key"));
    CHECK(hub.set_lobby_data([](void*, uint64_t l, const char*, const char* v) { Q.lobby = l; ++Q.calls; return v != nullptr; }, self, 6, "k", "v"));
    CHECK(hub.delete_lobby_data([](void*, uint64_t, const char*) { ++Q.calls; return true; }, self, 6, "k"));
    CHECK(hub.lobby_data_count([](void*, uint64_t l) { ++Q.calls; return int(l); }, self, 7) == 7);
    CHECK(hub.lobby_data_by_index([](void*, uint64_t, int i, char*, int, char*, int) { ++Q.calls; return i == 3; }, self, 1, 3, nullptr, 0, nullptr, 0));
    CHECK(hub.request_lobby_data([](void*, uint64_t) { ++Q.calls; return true; }, self, 1));
    CHECK(!strcmp(hub.lobby_member_data([](void*, uint64_t, uint64_t, const char* k) -> const char* { ++Q.calls; return k; }, self, 1, 2, "mk"), "mk"));
    hub.set_lobby_member_data([](void*, uint64_t l, const char*, const char*) { ++Q.calls; Q.lobby = l; }, self, 8, "a", "b");
    CHECK(hub.num_lobby_members([](void*, uint64_t) { ++Q.calls; return 9; }, self, 1) == 9);
    CHECK(hub.lobby_member_by_index([](void*, uint64_t, int i) -> uint64_t { ++Q.calls; return uint64_t(i) + 100; }, self, 1, 2) == 102);
    CHECK(hub.lobby_owner([](void*, uint64_t l) -> uint64_t { ++Q.calls; return l + 1; }, self, 41) == 42);
    CHECK(hub.set_lobby_owner([](void*, uint64_t, uint64_t) { ++Q.calls; return true; }, self, 1, 2));
    CHECK(hub.lobby_member_limit([](void*, uint64_t) { ++Q.calls; return 12; }, self, 1) == 12);
    CHECK(hub.set_lobby_member_limit([](void*, uint64_t, int) { ++Q.calls; return true; }, self, 1, 2));
    CHECK(hub.set_lobby_type([](void*, uint64_t, int) { ++Q.calls; return true; }, self, 1, 2));
    CHECK(hub.set_lobby_joinable([](void*, uint64_t, bool j) { ++Q.calls; return j; }, self, 1, true));
    uint32_t ip = 0; uint16_t port = 0; uint64_t sid = 0;
    CHECK(hub.lobby_game_server([](void*, uint64_t, uint32_t* i, uint16_t* p, uint64_t* s) { ++Q.calls; *i = 1; *p = 2; *s = 3; return true; }, self, 1, &ip, &port, &sid));
    CHECK(ip == 1 && port == 2 && sid == 3);
    hub.set_lobby_game_server([](void*, uint64_t, uint32_t, uint16_t, uint64_t) { ++Q.calls; }, self, 1, 2, 3, 4);
    hub.string_filter([](void*, const char*, const char*, int) { ++Q.calls; }, self, "k", "v", 0);
    hub.numerical_filter([](void*, const char*, int, int) { ++Q.calls; }, self, "k", 1, 0);
    hub.near_value_filter([](void*, const char*, int) { ++Q.calls; }, self, "k", 1);
    hub.slots_available_filter([](void*, int) { ++Q.calls; }, self, 1);
    hub.distance_filter([](void*, int) { ++Q.calls; }, self, 1);
    hub.result_count_filter([](void*, int) { ++Q.calls; }, self, 1);
    hub.compatible_members_filter([](void*, uint64_t) { ++Q.calls; }, self, 1);
    CHECK(hub.send_lobby_chat([](void*, uint64_t, const void*, int n) { ++Q.calls; return n == 2; }, self, 1, "ab", 2));
    CHECK(hub.lobby_chat_entry([](void*, uint64_t, int, uint64_t*, void*, int, int32_t*) { ++Q.calls; return 5; }, self, 1, 0, nullptr, nullptr, 0, nullptr) == 5);
    CHECK(Q.calls == expected_calls);
    // A null provider export never crashes either.
    CHECK(!strcmp(hub.lobby_data(nullptr, self, 1, "k"), "") && !hub.set_lobby_data(nullptr, self, 1, "k", "v") &&
          hub.lobby_data_count(nullptr, self, 1) == 0 && hub.lobby_owner(nullptr, self, 1) == 0);
    hub.string_filter(nullptr, self, "k", "v", 0);
}
void passthrough_phase2_exports() {
    reset_provider();
    constexpr int kExports = 27;
    {   Hub hub; passthrough_phase2(hub, kExports); }                       // no tap
    {   // a tap, but the game never called ManualDispatch_Init
        Rig rig;
        CHECK(rig.hub.pipe(p_pipe) == kPipe);
        passthrough_phase2(rig.hub, kExports);
        CHECK(rig.tasks.empty() && rig.cm.emsgs.empty());
        // the tap's own filter state is untouched by a pass-through filter call
        rig.hub.string_filter([](void*, const char*, const char*, int) {}, nullptr, "k", "v", 0);
    }
    {   // Init without the pipe is the same
        Rig rig;
        rig.hub.init(p_init);
        passthrough_phase2(rig.hub, kExports);
        CHECK(rig.tasks.empty() && rig.cm.emsgs.empty());
    }
    {   // a cleared tap
        Rig rig;
        rig.game_starts();
        rig.hub.set(nullptr);
        passthrough_phase2(rig.hub, kExports);
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
    lobby_data_members_chat();
    filters_and_listed_lobbies();
    cm_pushes();
    serial_executor();
    key_values();
    tap_replacement();
    passthrough_phase2_exports();
    puts("STEAM-MATCHMAKING PASS: Win64 callback layouts, launcher handles, ManualDispatch splice, "
         "create/list/join/leave results, lobby data/members/owner/game server/chat, list filters, CM pushes -> "
         "callbacks, tap replacement, pass-through without a tap or ManualDispatch, teardown in flight");
    return 0;
}
