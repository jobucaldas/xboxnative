// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The callback ABI half of real Steam lobbies (issue #179, phases 1 and 2).
//
// steam_lobbies.h talks to the CM; this header is how the answer reaches a game
// that pumps callbacks with SteamAPI_ManualDispatch_*. The provider (gbe_fork)
// owns that queue, so the launcher cannot write into it. Instead it keeps its
// own queue of launcher messages and splices it into the four ManualDispatch
// exports the game calls:
//
//   * Launcher SteamAPICall_t handles are the top bit plus a small counter, and
//     the queue remembers every handle it issued. GetAPICallResult is routed by
//     that membership, NEVER by the top bit: the pinned gbe_fork issues random
//     64-bit call handles, about half of them with the top bit set, so a handle
//     the queue does not own is the provider's whatever its bits are. The
//     chance of a random provider handle equalling a launcher counter value is
//     about 2^-60.
//   * GetNextCallback hands out launcher messages first (one outstanding at a
//     time, buffer valid until FreeLastCallback), then the provider's, which
//     are delegated untouched and so arrive in order, without loss.
//   * Every launcher call result is announced the way Steam announces one: a
//     SteamAPICallCompleted_t (703) message, then the game's own
//     GetAPICallResult for the handle.
//
// A game that pumps with SteamAPI_RunCallbacks never calls ManualDispatch_Init,
// so it would never read a launcher message. The hooks therefore only take over
// once the game has called ManualDispatch_Init AND asked for its pipe; before
// that every export passes straight to the provider with the same arguments.
//
// Phase 2 answers the rest of ISteamMatchmaking from the same CM state: lobby
// and member data, members and owner, the game server, the list filters and
// chat, and turns the CM's own pushes (lobby data, members joining and leaving,
// chat, game server) into the broadcast callbacks a game waits for. With no tap
// installed, or before the game has called ManualDispatch_Init, every one of
// those exports still passes straight to the provider.
//
// Portable (no Windows headers): the CM transport, the executor and the
// provider's real exports are all injected, so this compiles under Linux g++
// and the UWP clang-cl surface alike.
#include "steam_lobbies.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace steam_matchmaking {

// Callback ids (isteammatchmaking.h k_iSteamMatchmakingCallbacks = 500;
// steam_api_common.h / isteamutils.h for 703).
enum : int { CallbackLobbyEnter = 504, CallbackLobbyDataUpdate = 505, CallbackLobbyChatUpdate = 506,
             CallbackLobbyChatMsg = 507, CallbackLobbyGameCreated = 509, CallbackLobbyMatchList = 510,
             CallbackLobbyCreated = 513, CallbackAPICallCompleted = 703 };
// EChatMemberStateChange / EChatEntryType, as LobbyChatUpdate_t and LobbyChatMsg_t carry them.
enum : uint32_t { ChatMemberEntered = 0x0001, ChatMemberLeft = 0x0002 };
enum : uint8_t { ChatEntryTypeChatMsg = 1 };
// EResult values beyond steam_lobbies.h's ResultOK / ResultFail.
enum : int32_t { ResultNoConnection = 3 };
// EChatRoomEnterResponse.
enum : uint32_t { EnterSuccess = 1, EnterError = 5 };

// The Win64 Steamworks SDK packs its callback structs at 8.
#pragma pack(push, 8)
struct CallbackMsg_t {
    int32_t m_hSteamUser;
    int32_t m_iCallback;
    uint8_t* m_pubParam;
    int32_t m_cubParam;
};
struct SteamAPICallCompleted_t {
    enum { k_iCallback = 703 };
    uint64_t m_hAsyncCall;
    int32_t m_iCallback;
    uint32_t m_cubParam;
};
struct LobbyCreated_t {
    enum { k_iCallback = 513 };
    int32_t m_eResult;
    uint64_t m_ulSteamIDLobby;
};
struct LobbyEnter_t {
    enum { k_iCallback = 504 };
    uint64_t m_ulSteamIDLobby;
    uint32_t m_rgfChatPermissions;
    bool m_bLocked;
    uint32_t m_EChatRoomEnterResponse;
};
struct LobbyMatchList_t {
    enum { k_iCallback = 510 };
    uint32_t m_nLobbiesMatching;
};
struct LobbyDataUpdate_t {
    enum { k_iCallback = 505 };
    uint64_t m_ulSteamIDLobby;
    uint64_t m_ulSteamIDMember;   // the member whose data changed, or the lobby itself
    uint8_t m_bSuccess;
};
struct LobbyChatUpdate_t {
    enum { k_iCallback = 506 };
    uint64_t m_ulSteamIDLobby;
    uint64_t m_ulSteamIDUserChanged;
    uint64_t m_ulSteamIDMakingChange;
    uint32_t m_rgfChatMemberStateChange;
};
struct LobbyChatMsg_t {
    enum { k_iCallback = 507 };
    uint64_t m_ulSteamIDLobby;
    uint64_t m_ulSteamIDUser;
    uint8_t m_eChatEntryType;
    uint32_t m_iChatID;
};
struct LobbyGameCreated_t {
    enum { k_iCallback = 509 };
    uint64_t m_ulSteamIDLobby;
    uint64_t m_ulSteamIDGameServer;
    uint32_t m_unIP;
    uint16_t m_usPort;
};
#pragma pack(pop)

static_assert(sizeof(CallbackMsg_t) == 24 && offsetof(CallbackMsg_t, m_hSteamUser) == 0 &&
              offsetof(CallbackMsg_t, m_iCallback) == 4 && offsetof(CallbackMsg_t, m_pubParam) == 8 &&
              offsetof(CallbackMsg_t, m_cubParam) == 16, "CallbackMsg_t layout");
static_assert(sizeof(SteamAPICallCompleted_t) == 16 && offsetof(SteamAPICallCompleted_t, m_hAsyncCall) == 0 &&
              offsetof(SteamAPICallCompleted_t, m_iCallback) == 8 &&
              offsetof(SteamAPICallCompleted_t, m_cubParam) == 12, "SteamAPICallCompleted_t layout");
static_assert(sizeof(LobbyCreated_t) == 16 && offsetof(LobbyCreated_t, m_eResult) == 0 &&
              offsetof(LobbyCreated_t, m_ulSteamIDLobby) == 8, "LobbyCreated_t layout");
static_assert(sizeof(LobbyEnter_t) == 24 && offsetof(LobbyEnter_t, m_ulSteamIDLobby) == 0 &&
              offsetof(LobbyEnter_t, m_rgfChatPermissions) == 8 && offsetof(LobbyEnter_t, m_bLocked) == 12 &&
              offsetof(LobbyEnter_t, m_EChatRoomEnterResponse) == 16, "LobbyEnter_t layout");
static_assert(sizeof(LobbyMatchList_t) == 4 && offsetof(LobbyMatchList_t, m_nLobbiesMatching) == 0,
              "LobbyMatchList_t layout");
static_assert(sizeof(LobbyDataUpdate_t) == 24 && offsetof(LobbyDataUpdate_t, m_ulSteamIDLobby) == 0 &&
              offsetof(LobbyDataUpdate_t, m_ulSteamIDMember) == 8 && offsetof(LobbyDataUpdate_t, m_bSuccess) == 16 &&
              LobbyDataUpdate_t::k_iCallback == 505, "LobbyDataUpdate_t layout");
static_assert(sizeof(LobbyChatUpdate_t) == 32 && offsetof(LobbyChatUpdate_t, m_ulSteamIDLobby) == 0 &&
              offsetof(LobbyChatUpdate_t, m_ulSteamIDUserChanged) == 8 &&
              offsetof(LobbyChatUpdate_t, m_ulSteamIDMakingChange) == 16 &&
              offsetof(LobbyChatUpdate_t, m_rgfChatMemberStateChange) == 24 &&
              LobbyChatUpdate_t::k_iCallback == 506, "LobbyChatUpdate_t layout");
static_assert(sizeof(LobbyChatMsg_t) == 24 && offsetof(LobbyChatMsg_t, m_ulSteamIDLobby) == 0 &&
              offsetof(LobbyChatMsg_t, m_ulSteamIDUser) == 8 && offsetof(LobbyChatMsg_t, m_eChatEntryType) == 16 &&
              offsetof(LobbyChatMsg_t, m_iChatID) == 20 && LobbyChatMsg_t::k_iCallback == 507,
              "LobbyChatMsg_t layout");
static_assert(sizeof(LobbyGameCreated_t) == 24 && offsetof(LobbyGameCreated_t, m_ulSteamIDLobby) == 0 &&
              offsetof(LobbyGameCreated_t, m_ulSteamIDGameServer) == 8 && offsetof(LobbyGameCreated_t, m_unIP) == 16 &&
              offsetof(LobbyGameCreated_t, m_usPort) == 20 && LobbyGameCreated_t::k_iCallback == 509,
              "LobbyGameCreated_t layout");

// A launcher SteamAPICall_t is this bit plus a small counter. The bit only makes
// launcher handles recognisable in logs; the provider's random handles may have
// it too, so nothing routes on it (see Queue::take).
constexpr uint64_t LauncherHandleBit = 1ull << 63;
inline bool is_launcher_handle(uint64_t call) { return (call & LauncherHandleBit) != 0; }
// One counter for the process: a handle a finished launch handed out is never
// handed out again by the next one.
inline uint64_t next_launcher_handle() {
    static std::atomic<uint64_t> counter{0};
    return LauncherHandleBit | (counter.fetch_add(1) + 1);
}

// A zeroed callback struct: the padding bytes are part of what the game copies.
template <class T> inline T zeroed() { T value; memset(&value, 0, sizeof value); return value; }

// ---- the launcher's own callback / call-result queue
class Queue {
public:
    // A fresh handle for a call the game just made, remembered as in flight.
    // Nothing completes it until complete() runs, so the caller must always
    // finish what it reserved.
    uint64_t reserve() {
        const uint64_t call = next_launcher_handle();
        std::lock_guard<std::mutex> lock(m_);
        if (!closed_) inflight_.insert(call);
        return call;
    }

    // Completes a reserved `call`: stores the result for GetAPICallResult and
    // queues the SteamAPICallCompleted_t that announces it. A failed call stores
    // no data. A handle the queue did not reserve (or already completed) is
    // ignored.
    void complete(uint64_t call, int callback, const void* data, size_t size, bool failed = false) {
        std::lock_guard<std::mutex> lock(m_);
        if (closed_ || inflight_.erase(call) == 0) return;
        Result& r = results_[call];
        r.callback = callback; r.failed = failed;
        r.data.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + (failed ? 0 : size));
        auto done = zeroed<SteamAPICallCompleted_t>();
        done.m_hAsyncCall = call; done.m_iCallback = callback; done.m_cubParam = uint32_t(size);
        push(CallbackAPICallCompleted, &done, sizeof done);
    }
    // A callback with no call behind it (LobbyEnter_t after a create, or a CM
    // push). A game that never drains its callbacks must not grow this without
    // bound, so a broadcast past MaxQueued is dropped; call results are not.
    void broadcast(int callback, const void* data, size_t size) {
        std::lock_guard<std::mutex> lock(m_);
        if (closed_ || messages_.size() >= MaxQueued) return;
        push(callback, data, size);
    }
    static constexpr size_t MaxQueued = 2048;

    // Moves the next message into the outstanding slot and describes it in
    // `out`. The param buffer stays valid until release() or the next pop().
    bool pop(CallbackMsg_t& out) {
        std::lock_guard<std::mutex> lock(m_);
        if (closed_ || messages_.empty()) return false;
        current_ = std::move(messages_.front());
        messages_.pop_front();
        out.m_hSteamUser = user;
        out.m_iCallback = current_.callback;
        out.m_pubParam = current_.data.data();
        out.m_cubParam = int32_t(current_.data.size());
        return true;
    }
    void release() {
        std::lock_guard<std::mutex> lock(m_);
        current_ = Message();
    }

    // GetAPICallResult, routed by what the queue issued. The caller sends the
    // call to the provider only for NotOwned; every other answer is final.
    enum class Take {
        NotOwned,   // not a handle this queue issued (or it was torn down): the provider's
        Pending,    // issued, not completed yet: false, *failed = false
        Mismatch,   // completed, but the callback id or buffer is wrong: false, nothing written
        Done,       // completed and copied out: true, the entry is consumed
    };
    // A call that completed as failed reports Done (true) with *failed set and
    // nothing written. With `failed == nullptr` that failure cannot be told
    // apart from success, so a caller that passes no flag reads true either way;
    // Steam's own contract has the same blind spot.
    Take take(uint64_t call, void* buffer, int cub, int expected, bool* failed) {
        std::lock_guard<std::mutex> lock(m_);
        if (closed_) return Take::NotOwned;
        if (inflight_.count(call)) {
            if (failed) *failed = false;
            return Take::Pending;
        }
        auto it = results_.find(call);
        if (it == results_.end()) return Take::NotOwned;
        // A mismatch writes nothing and leaves the result for a correct retry.
        if (it->second.callback != expected) return Take::Mismatch;
        if (!it->second.failed) {
            if (!buffer || cub < 0 || size_t(cub) < it->second.data.size()) return Take::Mismatch;
            if (!it->second.data.empty()) memcpy(buffer, it->second.data.data(), it->second.data.size());
        }
        if (failed) *failed = it->second.failed;
        results_.erase(it);
        return Take::Done;
    }

    // Teardown: drops everything queued, in flight and completed, and stops
    // accepting more, so a task that finishes after this is a no-op and every
    // handle it issued is the provider's to refuse. The outstanding message, if
    // the game holds one, is left alone: its owner frees it with FreeLastCallback.
    void close() {
        std::lock_guard<std::mutex> lock(m_);
        closed_ = true;
        messages_.clear();
        results_.clear();
        inflight_.clear();
    }
    bool closed() const { std::lock_guard<std::mutex> lock(m_); return closed_; }
    size_t pending() const {
        std::lock_guard<std::mutex> lock(m_);
        return messages_.size() + results_.size() + inflight_.size();
    }

    // m_hSteamUser of the messages handed out.
    int32_t user = 1;

private:
    struct Message { int callback = 0; std::vector<uint8_t> data; };
    struct Result { int callback = 0; bool failed = false; std::vector<uint8_t> data; };
    void push(int callback, const void* data, size_t size) {
        Message msg;
        msg.callback = callback;
        msg.data.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
        messages_.push_back(std::move(msg));
    }
    mutable std::mutex m_;
    bool closed_ = false;
    std::deque<Message> messages_;
    std::map<uint64_t, Result> results_;
    std::set<uint64_t> inflight_;
    Message current_;
};

// The provider's real exports, as the hooks are handed them.
using InitFn = void (*)();
using PipeFn = int32_t (*)();
using NextFn = bool (*)(int32_t pipe, CallbackMsg_t* msg);
using FreeFn = void (*)(int32_t pipe);
using ResultFn = bool (*)(int32_t pipe, uint64_t call, void* buffer, int cub, int expected, bool* failed);
using CreateFn = uint64_t (*)(void* self, int lobby_type, int max_members);
using ListFn = uint64_t (*)(void* self);
using ByIndexFn = uint64_t (*)(void* self, int index);
using JoinFn = uint64_t (*)(void* self, uint64_t lobby);
using LeaveFn = void (*)(void* self, uint64_t lobby);
// Phase 2: lobby data, members, game server, list filters and chat.
using LobbyStrFn = const char* (*)(void* self, uint64_t lobby, const char* key);          // GetLobbyData
using SetDataFn = bool (*)(void* self, uint64_t lobby, const char* key, const char* value); // SetLobbyData
using DelDataFn = bool (*)(void* self, uint64_t lobby, const char* key);                  // DeleteLobbyData
using LobbyIntFn = int (*)(void* self, uint64_t lobby);        // GetLobbyDataCount, GetNumLobbyMembers, GetLobbyMemberLimit
using LobbyBoolFn = bool (*)(void* self, uint64_t lobby);      // RequestLobbyData
using DataByIndexFn = bool (*)(void* self, uint64_t lobby, int index, char* key, int key_size, char* value, int value_size);
using MemberDataFn = const char* (*)(void* self, uint64_t lobby, uint64_t member, const char* key);
using SetMemberDataFn = void (*)(void* self, uint64_t lobby, const char* key, const char* value);
using MemberByIndexFn = uint64_t (*)(void* self, uint64_t lobby, int index);
using SetOwnerFn = bool (*)(void* self, uint64_t lobby, uint64_t new_owner);
using LobbyIntSetFn = bool (*)(void* self, uint64_t lobby, int value);  // SetLobbyMemberLimit, SetLobbyType
using JoinableFn = bool (*)(void* self, uint64_t lobby, bool joinable);
using GetServerFn = bool (*)(void* self, uint64_t lobby, uint32_t* ip, uint16_t* port, uint64_t* server_id);
using SetServerFn = void (*)(void* self, uint64_t lobby, uint32_t ip, uint16_t port, uint64_t server_id);
using StringFilterFn = void (*)(void* self, const char* key, const char* value, int comparison);
using NumericalFilterFn = void (*)(void* self, const char* key, int value, int comparison);
using NearFilterFn = void (*)(void* self, const char* key, int value);
using IntArgFn = void (*)(void* self, int value);              // slots available, distance, result count
using U64ArgFn = void (*)(void* self, uint64_t value);         // compatible members
using SendChatFn = bool (*)(void* self, uint64_t lobby, const void* body, int size);
using ChatEntryFn = int (*)(void* self, uint64_t lobby, int chat_id, uint64_t* user, void* data, int size, int32_t* type);

// A serial worker for the CM round trips: tasks run one at a time in the order
// they were posted, off the game's thread. stop() drops what has not started.
// The thread is detached, so stop() never waits on a round trip in flight; the
// task owns what it touches, and a stopped executor runs nothing new.
class SerialExecutor {
public:
    // `thread_init` runs once on the worker before its first task (the launcher
    // enters a COM apartment there).
    explicit SerialExecutor(std::function<void()> thread_init = nullptr) : s_(std::make_shared<State>()) {
        auto s = s_;
        std::thread([s, thread_init = std::move(thread_init)] {
            if (thread_init) thread_init();
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(s->m);
                    s->cv.wait(lock, [&] { return s->stopped || !s->tasks.empty(); });
                    if (s->stopped) return;
                    task = std::move(s->tasks.front());
                    s->tasks.pop_front();
                }
                try { task(); } catch (...) {}
            }
        }).detach();
    }
    ~SerialExecutor() { stop(); }
    SerialExecutor(const SerialExecutor&) = delete;
    SerialExecutor& operator=(const SerialExecutor&) = delete;
    // The Executor a Matchmaking is configured with. It owns the worker's state,
    // so it stays callable (and does nothing) after this object is gone.
    std::function<void(std::function<void()>)> executor() const {
        auto s = s_;
        return [s](std::function<void()> task) {
            { std::lock_guard<std::mutex> lock(s->m); if (s->stopped) return; s->tasks.push_back(std::move(task)); }
            s->cv.notify_one();
        };
    }
    void stop() {
        std::deque<std::function<void()>> dropped;
        { std::lock_guard<std::mutex> lock(s_->m); s_->stopped = true; dropped.swap(s_->tasks); }
        s_->cv.notify_all();
    }

private:
    struct State {
        std::mutex m;
        std::condition_variable cv;
        std::deque<std::function<void()>> tasks;
        bool stopped = false;
    };
    std::shared_ptr<State> s_;
};

// ---- the ManualDispatch splice
class Splice {
public:
    explicit Splice(std::shared_ptr<Queue> queue) : queue_(std::move(queue)) {}

    void init_seen() { init_.store(true); }
    void set_pipe(int32_t pipe) { if (pipe > 0) pipe_.store(pipe); }
    // The game pumps with ManualDispatch and we know its pipe: a launcher
    // message would be read. Anything less and the game must not be handed one.
    bool active() const { return init_.load() && pipe_.load() > 0 && !queue_->closed(); }

    bool next(NextFn real, int32_t pipe, CallbackMsg_t* msg) {
        const bool ours = pipe > 0 && pipe == pipe_.load();
        if (msg && ours && active()) {
            std::lock_guard<std::mutex> lock(m_);
            // One launcher message outstanding at a time: while the game has not
            // freed the last one, the provider answers.
            if (source_ != Source::Launcher && queue_->pop(*msg)) { source_ = Source::Launcher; return true; }
        }
        const bool got = real ? real(pipe, msg) : false;
        if (got && ours) { std::lock_guard<std::mutex> lock(m_); source_ = Source::Provider; }
        return got;
    }
    void free_last(FreeFn real, int32_t pipe) {
        if (pipe > 0 && pipe == pipe_.load()) {
            std::lock_guard<std::mutex> lock(m_);
            if (source_ == Source::Launcher) { source_ = Source::None; queue_->release(); return; }
            source_ = Source::None;
        }
        if (real) real(pipe);
    }
    bool result(ResultFn real, int32_t pipe, uint64_t call, void* buffer, int cub, int expected, bool* failed) {
        // Routed by ownership, not by the handle's bits: the provider's own
        // handles may have the top bit set too. Anything the queue did not issue
        // goes to the provider unchanged.
        switch (queue_->take(call, buffer, cub, expected, failed)) {
        case Queue::Take::Done: return true;
        case Queue::Take::Pending:
        case Queue::Take::Mismatch: return false;
        case Queue::Take::NotOwned: break;
        }
        return real ? real(pipe, call, buffer, cub, expected, failed) : false;
    }

private:
    enum class Source { None, Launcher, Provider };
    std::shared_ptr<Queue> queue_;
    std::atomic<bool> init_{false};
    std::atomic<int32_t> pipe_{0};
    std::mutex m_;
    Source source_ = Source::None;
};

// ---- ISteamMatchmaking, answered from the CM
// Runs a task off the game's thread. Tasks must run in submission order (a
// serial executor): a leave followed by a create is two CM round trips that must
// not overtake each other. With no executor the task runs inline.
using Executor = std::function<void(std::function<void()>)>;

struct Config {
    std::shared_ptr<steam_lobbies::Bridge> bridge;
    steam_lobbies::Io io;
    Executor executor;
    uint32_t appid = 0;
    uint64_t self_id = 0;     // this account's SteamID: it owns "its" member data and may own the lobby
    std::string persona;
    int want_lobbies = 50;
};

class Matchmaking {
public:
    Matchmaking(std::shared_ptr<Queue> queue, Config config) : s_(std::make_shared<State>()) {
        s_->queue = std::move(queue);
        s_->cfg = std::move(config);
        if (!s_->cfg.bridge) s_->cfg.bridge = std::make_shared<steam_lobbies::Bridge>();
    }

    // Each call returns a launcher handle at once; the CM round trip runs on the
    // executor and posts the result. Every create/list/join posts something even
    // when Steam refuses or the transport fails, so the game never waits forever.
    uint64_t create(int lobby_type, int max_members) {
        auto s = s_; const uint64_t call = s->queue->reserve();
        run(s, [s, call, lobby_type, max_members] {
            Op op(*s);
            const bool ok = s->cfg.bridge->create(op.io, s->cfg.appid, max_members, lobby_type, 0, std::string(), s->cfg.persona);
            const uint64_t lobby = ok ? bridge_lobby(*s) : 0;
            auto created = zeroed<LobbyCreated_t>();
            if (lobby) {
                created.m_eResult = steam_lobbies::ResultOK; created.m_ulSteamIDLobby = lobby;
                s->queue->complete(call, CallbackLobbyCreated, &created, sizeof created);
                auto enter = zeroed<LobbyEnter_t>();
                enter.m_ulSteamIDLobby = lobby; enter.m_EChatRoomEnterResponse = EnterSuccess;
                s->queue->broadcast(CallbackLobbyEnter, &enter, sizeof enter);
            } else {
                created.m_eResult = op.transport_failed ? int32_t(ResultNoConnection) : int32_t(steam_lobbies::ResultFail);
                s->queue->complete(call, CallbackLobbyCreated, &created, sizeof created);
            }
        });
        return call;
    }
    uint64_t request_list() {
        auto s = s_; const uint64_t call = s->queue->reserve();
        // The filters the game added since its last request belong to this one
        // and are spent by it, as Steam's are.
        std::vector<steam_lobbies::Filter> filters; int want;
        {
            std::lock_guard<std::mutex> lock(s->m);
            filters.swap(s->filters);
            want = s->result_count > 0 ? s->result_count : s->cfg.want_lobbies;
            s->result_count = 0;
        }
        run(s, [s, call, filters = std::move(filters), want] {
            Op op(*s);
            std::vector<steam_lobbies::LobbySummary> rows;
            const bool ok = steam_lobbies::list(op.io, s->cfg.appid, want, filters, rows);
            auto matches = zeroed<LobbyMatchList_t>();
            {
                std::lock_guard<std::mutex> lock(s->m);
                s->listed.clear(); s->rows.clear();
                if (ok) for (auto& row : rows) { s->listed.push_back(row.steamid); s->rows[row.steamid] = row; }
                matches.m_nLobbiesMatching = uint32_t(s->listed.size());
            }
            s->queue->complete(call, CallbackLobbyMatchList, &matches, sizeof matches);
        });
        return call;
    }
    // The CM's lobby ids from the last list; 0 out of range.
    uint64_t lobby_by_index(int index) const {
        std::lock_guard<std::mutex> lock(s_->m);
        return index >= 0 && size_t(index) < s_->listed.size() ? s_->listed[size_t(index)] : 0;
    }
    uint64_t join(uint64_t lobby) {
        auto s = s_; const uint64_t call = s->queue->reserve();
        run(s, [s, call, lobby] {
            Op op(*s);
            const bool ok = s->cfg.bridge->join(op.io, s->cfg.appid, lobby, s->cfg.persona);
            auto enter = zeroed<LobbyEnter_t>();
            enter.m_ulSteamIDLobby = lobby;
            enter.m_EChatRoomEnterResponse = ok ? EnterSuccess : EnterError;
            s->queue->complete(call, CallbackLobbyEnter, &enter, sizeof enter);
        });
        return call;
    }
    // Leaves the bridge's lobby; posts nothing (LeaveLobby has no result).
    void leave(uint64_t lobby) {
        auto s = s_;
        run(s, [s, lobby] {
            if (!lobby || bridge_lobby(*s) != lobby) return;
            Op op(*s);
            s->cfg.bridge->leave(op.io, s->cfg.appid);
        });
    }

    // ---- the list filters: kept for the next RequestLobbyList
    void add_filter(steam_lobbies::Filter filter) {
        std::lock_guard<std::mutex> lock(s_->m);
        if (s_->filters.size() < MaxFilters) s_->filters.push_back(std::move(filter));
    }
    void string_filter(const char* key, const char* value, int comparison) {
        if (!key || !*key) return;
        add_filter({key, value ? value : "", comparison, steam_lobbies::FilterString});
    }
    void numerical_filter(const char* key, int value, int comparison) {
        if (!key || !*key) return;
        add_filter({key, std::to_string(value), comparison, steam_lobbies::FilterNumerical});
    }
    void near_filter(const char* key, int value) {
        if (!key || !*key) return;
        add_filter({key, std::to_string(value), steam_lobbies::CompareEqual, steam_lobbies::FilterNearValue});
    }
    void slots_filter(int slots) {
        add_filter({std::string(), std::to_string(slots), steam_lobbies::CompareEqualOrGreater, steam_lobbies::FilterSlotsAvailable});
    }
    // CMsgClientMMSGetLobbyList has no field for a distance or a compatible-members
    // filter, so those two are accepted and not sent.
    void result_count_filter(int count) {
        std::lock_guard<std::mutex> lock(s_->m);
        s_->result_count = count > 0 ? count : 0;
    }

    // ---- lobby data. The lobby the bridge is in answers from the bridge; a lobby
    // from the last list answers from its row; any other id has nothing.
    // Returned strings live in a ring of MaxStrings slots, so a game reading
    // several keys in one expression keeps every pointer until it has read that many more.
    const char* get_data(uint64_t lobby, const char* key) {
        std::string value, blob;
        if (key && lobby_blob(*s_, lobby, blob)) {
            auto kv = steam_lobbies::kv_decode(blob);
            auto it = kv.find(key);
            if (it != kv.end()) value = it->second;
        }
        return keep(*s_, std::move(value));
    }
    int data_count(uint64_t lobby) const {
        std::string blob;
        return lobby_blob(*s_, lobby, blob) ? int(steam_lobbies::kv_decode(blob).size()) : 0;
    }
    bool data_by_index(uint64_t lobby, int index, char* key, int key_size, char* value, int value_size) const {
        std::string blob;
        if (index < 0 || !lobby_blob(*s_, lobby, blob)) return false;
        auto kv = steam_lobbies::kv_decode(blob);
        if (size_t(index) >= kv.size()) return false;
        auto it = kv.begin(); std::advance(it, index);
        copy_out(key, key_size, it->first); copy_out(value, value_size, it->second);
        return true;
    }
    // Only the owner changes lobby data. The local copy changes at once, as Steam's
    // does; the CM learns of it on the executor.
    bool set_data(uint64_t lobby, const char* key, const char* value) {
        if (!key || !*key || strlen(key) > steam_lobbies::MaxMetadataKey) return false;
        if (value && strlen(value) > steam_lobbies::MaxMetadataValue) return false;
        std::string blob;
        {
            auto& b = *s_->cfg.bridge;
            std::lock_guard<std::mutex> lock(b.m);
            if (!owns(*s_, lobby)) return false;
            auto kv = steam_lobbies::kv_decode(b.metadata);
            kv[key] = value ? value : "";
            b.metadata = blob = steam_lobbies::kv_encode(kv);
        }
        send_data(lobby, 0, blob);
        return true;
    }
    bool delete_data(uint64_t lobby, const char* key) {
        if (!key) return false;
        std::string blob;
        {
            auto& b = *s_->cfg.bridge;
            std::lock_guard<std::mutex> lock(b.m);
            if (!owns(*s_, lobby)) return false;
            auto kv = steam_lobbies::kv_decode(b.metadata);
            if (kv.erase(key) == 0) return false;
            b.metadata = blob = steam_lobbies::kv_encode(kv);
        }
        send_data(lobby, 0, blob);
        return true;
    }
    // Asks the CM for the lobby again; the answer is a LobbyDataUpdate_t, success or not.
    bool request_data(uint64_t lobby) {
        if (!lobby) return false;
        auto s = s_;
        run(s, [s, lobby] {
            Op op(*s);
            auto fresh = steam_lobbies::data(op.io, s->cfg.appid, lobby);
            if (fresh.found) {
                if (!s->cfg.bridge->apply_lobby_data(fresh)) {
                    std::lock_guard<std::mutex> lock(s->m);
                    auto it = s->rows.find(lobby);
                    if (it != s->rows.end()) {
                        it->second.metadata = fresh.metadata; it->second.max_members = fresh.max_members;
                        it->second.lobby_type = fresh.lobby_type; it->second.num_members = int32_t(fresh.members.size());
                    }
                }
            }
            auto update = zeroed<LobbyDataUpdate_t>();
            update.m_ulSteamIDLobby = lobby; update.m_ulSteamIDMember = lobby; update.m_bSuccess = fresh.found ? 1 : 0;
            s->queue->broadcast(CallbackLobbyDataUpdate, &update, sizeof update);
        });
        return true;
    }

    // ---- members and owner
    const char* get_member_data(uint64_t lobby, uint64_t member, const char* key) {
        std::string value;
        if (key) {
            auto& b = *s_->cfg.bridge;
            std::string blob;
            {
                std::lock_guard<std::mutex> lock(b.m);
                auto it = b.lobby == lobby ? b.member_metadata.find(member) : b.member_metadata.end();
                if (it != b.member_metadata.end()) blob = it->second;
            }
            auto kv = steam_lobbies::kv_decode(blob);
            auto it = kv.find(key);
            if (it != kv.end()) value = it->second;
        }
        return keep(*s_, std::move(value));
    }
    // A member sets only its own data.
    void set_member_data(uint64_t lobby, const char* key, const char* value) {
        const uint64_t self = s_->cfg.self_id;
        if (!self || !key || !*key || strlen(key) > steam_lobbies::MaxMetadataKey) return;
        if (value && strlen(value) > steam_lobbies::MaxMetadataValue) return;
        std::string blob;
        {
            auto& b = *s_->cfg.bridge;
            std::lock_guard<std::mutex> lock(b.m);
            if (!b.lobby || b.lobby != lobby) return;
            auto kv = steam_lobbies::kv_decode(b.member_metadata[self]);
            kv[key] = value ? value : "";
            b.member_metadata[self] = blob = steam_lobbies::kv_encode(kv);
        }
        send_data(lobby, self, blob);
    }
    int member_count(uint64_t lobby) const {
        auto& b = *s_->cfg.bridge;
        {
            std::lock_guard<std::mutex> lock(b.m);
            if (b.lobby && b.lobby == lobby) return int(b.members.size());
        }
        std::lock_guard<std::mutex> lock(s_->m);
        auto it = s_->rows.find(lobby);
        return it == s_->rows.end() ? 0 : it->second.num_members;
    }
    uint64_t member_by_index(uint64_t lobby, int index) const {
        auto& b = *s_->cfg.bridge;
        std::lock_guard<std::mutex> lock(b.m);
        if (!b.lobby || b.lobby != lobby || index < 0 || size_t(index) >= b.members.size()) return 0;
        auto it = b.members.begin(); std::advance(it, index);
        return it->first;
    }
    uint64_t owner(uint64_t lobby) const {
        auto& b = *s_->cfg.bridge;
        std::lock_guard<std::mutex> lock(b.m);
        return b.lobby && b.lobby == lobby ? b.owner : 0;
    }
    // Hands the lobby to a member. The CM decides; the bridge's owner changes
    // when it agrees (its pushed lobby data then confirms).
    bool set_owner(uint64_t lobby, uint64_t new_owner) {
        {
            auto& b = *s_->cfg.bridge;
            std::lock_guard<std::mutex> lock(b.m);
            if (!owns(*s_, lobby) || !b.members.count(new_owner)) return false;
        }
        auto s = s_;
        run(s, [s, lobby, new_owner] {
            Op op(*s);
            if (!steam_lobbies::set_owner(op.io, s->cfg.appid, lobby, new_owner)) return;
            auto& b = *s->cfg.bridge;
            std::lock_guard<std::mutex> lock(b.m);
            if (b.lobby == lobby) { b.owner = new_owner; b.owner_self = new_owner == s->cfg.self_id; }
        });
        return true;
    }
    int member_limit(uint64_t lobby) const {
        auto& b = *s_->cfg.bridge;
        {
            std::lock_guard<std::mutex> lock(b.m);
            if (b.lobby && b.lobby == lobby) return b.max_members;
        }
        std::lock_guard<std::mutex> lock(s_->m);
        auto it = s_->rows.find(lobby);
        return it == s_->rows.end() ? 0 : it->second.max_members;
    }
    bool set_member_limit(uint64_t lobby, int limit) {
        if (limit < 0) return false;
        std::string blob;
        {
            auto& b = *s_->cfg.bridge;
            std::lock_guard<std::mutex> lock(b.m);
            if (!owns(*s_, lobby)) return false;
            b.max_members = limit; blob = b.metadata;
        }
        send_data(lobby, 0, blob, limit);
        return true;
    }
    bool set_type(uint64_t lobby, int type) {
        if (type < steam_lobbies::LobbyTypePrivate || type > steam_lobbies::LobbyTypeInvisible) return false;
        std::string blob;
        {
            auto& b = *s_->cfg.bridge;
            std::lock_guard<std::mutex> lock(b.m);
            if (!owns(*s_, lobby)) return false;
            b.lobby_type = type; blob = b.metadata;
        }
        send_data(lobby, 0, blob, std::nullopt, type);
        return true;
    }
    // SetLobbyJoinable is a lobby flag on the wire, and the flag's value is not
    // documented anywhere this code can check, so it is refused rather than
    // guessed: the game is told it did not take effect.
    bool set_joinable(uint64_t, bool) { return false; }

    // ---- the game server the lobby advertises
    bool get_server(uint64_t lobby, uint32_t* ip, uint16_t* port, uint64_t* server_id) const {
        auto& b = *s_->cfg.bridge;
        std::lock_guard<std::mutex> lock(b.m);
        if (!b.lobby || b.lobby != lobby || (!b.server.ipv4 && !b.server.steamid)) return false;
        if (ip) *ip = b.server.ipv4;
        if (port) *port = uint16_t(b.server.port);
        if (server_id) *server_id = b.server.steamid;
        return true;
    }
    void set_server(uint64_t lobby, uint32_t ip, uint16_t port, uint64_t server_id) {
        {
            auto& b = *s_->cfg.bridge;
            std::lock_guard<std::mutex> lock(b.m);
            if (!owns(*s_, lobby)) return;
            b.server.lobby = lobby; b.server.ipv4 = ip; b.server.port = port; b.server.steamid = server_id;
        }
        auto s = s_;
        run(s, [s, lobby, ip, port, server_id] {
            Op op(*s);
            steam_lobbies::set_server(op.io, s->cfg.appid, lobby, ip, port, server_id);
        });
    }

    // ---- chat
    bool send_chat(uint64_t lobby, const void* body, int size) {
        if (!body || size <= 0 || unsigned(size) > steam_lobbies::MaxChatMsg) return false;
        {
            auto& b = *s_->cfg.bridge;
            std::lock_guard<std::mutex> lock(b.m);
            if (!b.lobby || b.lobby != lobby) return false;
        }
        std::string text(static_cast<const char*>(body), size_t(size));
        auto s = s_;
        run(s, [s, lobby, text = std::move(text)] {
            Op op(*s);
            steam_lobbies::send_chat(op.io, s->cfg.appid, lobby, 0, text);
        });
        return true;
    }
    // Copies what fits; returns the bytes copied, 0 for an unknown entry.
    int chat_entry(uint64_t lobby, int chat_id, uint64_t* user, void* data, int size, int32_t* type) const {
        std::lock_guard<std::mutex> lock(s_->m);
        for (auto& e : s_->chats) {
            if (e.lobby != lobby || int64_t(e.id) != chat_id) continue;
            if (user) *user = e.user;
            if (type) *type = ChatEntryTypeChatMsg;
            const size_t n = data && size > 0 ? std::min(size_t(size), e.text.size()) : 0;
            if (n) memcpy(data, e.text.data(), n);
            return int(n);
        }
        return 0;
    }

    // ---- what Steam pushes on its own (the CM session's unsolicited messages).
    // Runs on the CM reader thread, so it only takes short locks and queues.
    // Returns whether `emsg` is a lobby push this layer takes.
    static bool is_push(uint32_t emsg) {
        return emsg == steam_lobbies::LobbyData || emsg == steam_lobbies::LobbyChatMsg ||
               emsg == steam_lobbies::LobbyGameServerSet || emsg == steam_lobbies::UserJoinedLobby ||
               emsg == steam_lobbies::UserLeftLobby;
    }
    bool on_push(uint32_t emsg, const std::string& body) {
        auto s = s_;
        if (!is_push(emsg)) return false;
        if (s->closed.load()) return true;
        auto& b = *s->cfg.bridge;
        switch (emsg) {
        case steam_lobbies::LobbyData: {
            auto data = steam_lobbies::parse_lobby_data(body, steam_lobbies::ResultOK);
            struct Old { std::string metadata; uint64_t owner; int32_t max, type, flags; std::map<uint64_t, std::string> members; } old;
            {
                std::lock_guard<std::mutex> lock(b.m);
                if (!b.lobby || data.steamid != b.lobby) return true;
                old = {b.metadata, b.owner, b.max_members, b.lobby_type, b.lobby_flags, b.member_metadata};
            }
            if (!b.apply_lobby_data(data)) return true;
            if (old.metadata != data.metadata || old.owner != data.owner || old.max != data.max_members ||
                old.type != data.lobby_type || old.flags != data.lobby_flags)
                data_update(*s, data.steamid, data.steamid);
            for (auto& mem : data.members) {
                auto was = old.members.find(mem.steamid);
                if ((was == old.members.end() ? std::string() : was->second) != mem.metadata)
                    data_update(*s, data.steamid, mem.steamid);
            }
            return true;
        }
        case steam_lobbies::UserJoinedLobby:
        case steam_lobbies::UserLeftLobby: {
            auto change = steam_lobbies::parse_membership(body, emsg == steam_lobbies::UserJoinedLobby);
            if (!b.apply_membership(change)) return true;
            auto update = zeroed<LobbyChatUpdate_t>();
            update.m_ulSteamIDLobby = change.lobby; update.m_ulSteamIDUserChanged = change.user;
            update.m_ulSteamIDMakingChange = change.user;
            update.m_rgfChatMemberStateChange = change.joined ? ChatMemberEntered : ChatMemberLeft;
            s->queue->broadcast(CallbackLobbyChatUpdate, &update, sizeof update);
            return true;
        }
        case steam_lobbies::LobbyChatMsg: {
            auto msg = steam_lobbies::parse_chat(body);
            {
                std::lock_guard<std::mutex> lock(b.m);
                if (!b.accepts_chat(msg)) return true;
            }
            uint32_t id;
            {
                std::lock_guard<std::mutex> lock(s->m);
                id = s->next_chat++;
                s->chats.push_back({id, msg.lobby, msg.sender, std::move(msg.message)});
                while (s->chats.size() > MaxChats) s->chats.pop_front();
            }
            auto note = zeroed<LobbyChatMsg_t>();
            note.m_ulSteamIDLobby = msg.lobby; note.m_ulSteamIDUser = msg.sender;
            note.m_eChatEntryType = ChatEntryTypeChatMsg; note.m_iChatID = id;
            s->queue->broadcast(CallbackLobbyChatMsg, &note, sizeof note);
            return true;
        }
        case steam_lobbies::LobbyGameServerSet: {
            auto server = steam_lobbies::parse_server_set(body);
            if (!b.apply_server(server)) return true;
            auto made = zeroed<LobbyGameCreated_t>();
            made.m_ulSteamIDLobby = server.lobby; made.m_ulSteamIDGameServer = server.steamid;
            made.m_unIP = server.ipv4; made.m_usPort = uint16_t(server.port);
            s->queue->broadcast(CallbackLobbyGameCreated, &made, sizeof made);
            return true;
        }
        }
        return false;
    }

    // Teardown: tasks still in flight finish their round trip and post nothing.
    void close() { s_->closed.store(true); }

private:
    static constexpr size_t MaxFilters = 64, MaxChats = 128, MaxStrings = 16;
    struct State {
        std::shared_ptr<Queue> queue;
        Config cfg;
        std::atomic<bool> closed{false};
        mutable std::mutex m;
        std::vector<uint64_t> listed;
        std::map<uint64_t, steam_lobbies::LobbySummary> rows;   // the last list, by lobby
        std::vector<steam_lobbies::Filter> filters;             // for the next RequestLobbyList
        int result_count = 0;
        struct Chat { uint32_t id; uint64_t lobby, user; std::string text; };
        std::deque<Chat> chats;
        uint32_t next_chat = 0;
        std::array<std::string, MaxStrings> strings;
        size_t next_string = 0;
    };
    // The CM transport as one task sees it: notes whether a request never got an
    // answer, which is "no connection" to the game rather than a Steam refusal.
    struct Op {
        explicit Op(State& s) {
            auto call = s.cfg.io.call;
            io.call = [this, call](uint32_t emsg, uint32_t routing, const std::string& body, std::string& response) {
                const bool ok = call ? call(emsg, routing, body, response) : false;
                if (!ok) transport_failed = true;
                return ok;
            };
            io.log = s.cfg.io.log ? s.cfg.io.log : [](const std::string&) {};
        }
        steam_lobbies::Io io;
        bool transport_failed = false;
    };
    // The lobby's metadata blob, from the bridge or the last list.
    static bool lobby_blob(State& s, uint64_t lobby, std::string& blob) {
        {
            std::lock_guard<std::mutex> lock(s.cfg.bridge->m);
            if (s.cfg.bridge->lobby && s.cfg.bridge->lobby == lobby) { blob = s.cfg.bridge->metadata; return true; }
        }
        std::lock_guard<std::mutex> lock(s.m);
        auto it = s.rows.find(lobby);
        if (it == s.rows.end()) return false;
        blob = it->second.metadata;
        return true;
    }
    // This account owns the lobby the bridge is in. The caller holds bridge->m.
    static bool owns(const State& s, uint64_t lobby) {
        const auto& b = *s.cfg.bridge;
        if (!b.lobby || b.lobby != lobby) return false;
        return (s.cfg.self_id && b.owner == s.cfg.self_id) || (b.owner == 0 && b.owner_self);
    }
    static const char* keep(State& s, std::string value) {
        std::lock_guard<std::mutex> lock(s.m);
        std::string& slot = s.strings[s.next_string++ % MaxStrings];
        slot = std::move(value);
        return slot.c_str();
    }
    static void copy_out(char* out, int size, const std::string& text) {
        if (!out || size <= 0) return;
        const size_t n = std::min(size_t(size - 1), text.size());
        memcpy(out, text.data(), n); out[n] = 0;
    }
    static void data_update(State& s, uint64_t lobby, uint64_t member) {
        auto update = zeroed<LobbyDataUpdate_t>();
        update.m_ulSteamIDLobby = lobby; update.m_ulSteamIDMember = member; update.m_bSuccess = 1;
        s.queue->broadcast(CallbackLobbyDataUpdate, &update, sizeof update);
    }
    // One CMsgClientMMSSetLobbyData on the executor; a member id makes it that member's data.
    void send_data(uint64_t lobby, uint64_t member, const std::string& blob, std::optional<int> max = std::nullopt,
                   std::optional<int> type = std::nullopt) {
        auto s = s_;
        run(s, [s, lobby, member, blob, max, type] {
            Op op(*s);
            steam_lobbies::set_data(op.io, s->cfg.appid, lobby, member, blob, max, type);
        });
    }
    static uint64_t bridge_lobby(State& s) {
        std::lock_guard<std::mutex> lock(s.cfg.bridge->m);
        return s.cfg.bridge->lobby;
    }
    static void run(const std::shared_ptr<State>& s, std::function<void()> task) {
        // The task owns the state, so it stays valid however long the executor
        // holds it; a closed adapter turns it into a no-op.
        auto guarded = [s, task = std::move(task)] { if (!s->closed.load()) task(); };
        if (s->cfg.executor) s->cfg.executor(std::move(guarded)); else guarded();
    }
    std::shared_ptr<State> s_;
};

// ---- one launch's worth of the above
class Service {
public:
    explicit Service(Config config)
        : queue_(std::make_shared<Queue>()), splice_(queue_), matchmaking_(queue_, std::move(config)) {}
    Queue& queue() { return *queue_; }
    Splice& splice() { return splice_; }
    Matchmaking& matchmaking() { return matchmaking_; }
    bool active() const { return splice_.active(); }
    void close() { matchmaking_.close(); queue_->close(); }

private:
    std::shared_ptr<Queue> queue_;
    Splice splice_;
    Matchmaking matchmaking_;
};

// ---- the hooks' entry points
// Holds the installed Service. Every hook copies the shared_ptr out before it
// runs, so a Service replaced mid-call stays alive until that call returns, and
// with none installed every method calls the provider's export unchanged.
class Hub {
public:
    // Installs `service` (null clears). The previous one is closed: what it had
    // queued is dropped and its tasks go quiet. It is kept until the game frees
    // its last callback, because a game that already holds a launcher message
    // between GetNextCallback and FreeLastCallback still reads its buffer, however
    // many times the tap is replaced meanwhile. What the game did before the
    // install (ManualDispatch_Init, its pipe) is remembered here and handed to
    // the new service, so a tap installed after those calls is active at once.
    void set(std::shared_ptr<Service> service) {
        std::vector<std::shared_ptr<Service>> dropped;   // destroyed after the lock is released
        std::shared_ptr<Service> old;
        bool init; int32_t game_pipe;
        {
            std::lock_guard<std::mutex> lock(m_);
            old = std::move(current_); current_ = service;
            init = init_seen_; game_pipe = pipe_;
            if (old) retired_.push_back(old);
            while (retired_.size() > MaxRetired) { dropped.push_back(std::move(retired_.front())); retired_.pop_front(); }
        }
        if (old) old->close();
        if (service) {
            if (init) service->splice().init_seen();
            if (game_pipe > 0) service->splice().set_pipe(game_pipe);
        }
    }
    // Per-launch reset: closes and clears any installed service (retiring it in
    // case a game still holds a message buffer) and forgets the previous launch's
    // ManualDispatch_Init and HSteamPipe observations so the next launch starts
    // inactive until that game calls ManualDispatch.
    void reset() {
        std::vector<std::shared_ptr<Service>> dropped;
        std::shared_ptr<Service> old;
        {
            std::lock_guard<std::mutex> lock(m_);
            old = std::move(current_);
            init_seen_ = false;
            pipe_ = 0;
            if (old) retired_.push_back(old);
            while (retired_.size() > MaxRetired) { dropped.push_back(std::move(retired_.front())); retired_.pop_front(); }
        }
        if (old) old->close();
    }
    std::shared_ptr<Service> get() const { std::lock_guard<std::mutex> lock(m_); return current_; }

    void init(InitFn real) {
        if (real) real();
        std::shared_ptr<Service> s;
        { std::lock_guard<std::mutex> lock(m_); init_seen_ = true; s = current_; }
        if (s) s->splice().init_seen();
    }
    int32_t pipe(PipeFn real) {
        const int32_t pipe = real ? real() : 0;
        std::shared_ptr<Service> s;
        { std::lock_guard<std::mutex> lock(m_); if (pipe > 0) pipe_ = pipe; s = current_; }
        if (s) s->splice().set_pipe(pipe);
        return pipe;
    }
    bool next(NextFn real, int32_t pipe, CallbackMsg_t* msg) {
        if (auto s = get()) return s->splice().next(real, pipe, msg);
        return real ? real(pipe, msg) : false;
    }
    void free_last(FreeFn real, int32_t pipe) {
        if (auto s = get()) s->splice().free_last(real, pipe);
        else if (real) real(pipe);
        // The game is done with whatever it held, so a retired service's buffer can go.
        std::deque<std::shared_ptr<Service>> dropped;
        { std::lock_guard<std::mutex> lock(m_); dropped.swap(retired_); }
    }
    bool api_call_result(ResultFn real, int32_t pipe, uint64_t call, void* buffer, int cub, int expected, bool* failed) {
        if (auto s = get()) return s->splice().result(real, pipe, call, buffer, cub, expected, failed);
        return real ? real(pipe, call, buffer, cub, expected, failed) : false;
    }
    uint64_t create_lobby(CreateFn real, void* self, int lobby_type, int max_members) {
        if (auto s = active()) return s->matchmaking().create(lobby_type, max_members);
        return real ? real(self, lobby_type, max_members) : 0;
    }
    uint64_t request_lobby_list(ListFn real, void* self) {
        if (auto s = active()) return s->matchmaking().request_list();
        return real ? real(self) : 0;
    }
    uint64_t lobby_by_index(ByIndexFn real, void* self, int index) {
        if (auto s = active()) return s->matchmaking().lobby_by_index(index);
        return real ? real(self, index) : 0;
    }
    uint64_t join_lobby(JoinFn real, void* self, uint64_t lobby) {
        if (auto s = active()) return s->matchmaking().join(lobby);
        return real ? real(self, lobby) : 0;
    }
    void leave_lobby(LeaveFn real, void* self, uint64_t lobby) {
        if (auto s = active()) return s->matchmaking().leave(lobby);
        if (real) real(self, lobby);
    }

    // ---- phase 2: each answers from the CM state while the tap is active and
    // otherwise calls the provider's export with the same arguments.
    const char* lobby_data(LobbyStrFn real, void* self, uint64_t lobby, const char* key) {
        return route(real, "", [&](Matchmaking& m) { return m.get_data(lobby, key); }, self, lobby, key);
    }
    bool set_lobby_data(SetDataFn real, void* self, uint64_t lobby, const char* key, const char* value) {
        return route(real, false, [&](Matchmaking& m) { return m.set_data(lobby, key, value); }, self, lobby, key, value);
    }
    bool delete_lobby_data(DelDataFn real, void* self, uint64_t lobby, const char* key) {
        return route(real, false, [&](Matchmaking& m) { return m.delete_data(lobby, key); }, self, lobby, key);
    }
    int lobby_data_count(LobbyIntFn real, void* self, uint64_t lobby) {
        return route(real, 0, [&](Matchmaking& m) { return m.data_count(lobby); }, self, lobby);
    }
    bool lobby_data_by_index(DataByIndexFn real, void* self, uint64_t lobby, int index, char* key, int key_size,
                             char* value, int value_size) {
        return route(real, false, [&](Matchmaking& m) { return m.data_by_index(lobby, index, key, key_size, value, value_size); },
                     self, lobby, index, key, key_size, value, value_size);
    }
    bool request_lobby_data(LobbyBoolFn real, void* self, uint64_t lobby) {
        return route(real, false, [&](Matchmaking& m) { return m.request_data(lobby); }, self, lobby);
    }
    const char* lobby_member_data(MemberDataFn real, void* self, uint64_t lobby, uint64_t member, const char* key) {
        return route(real, "", [&](Matchmaking& m) { return m.get_member_data(lobby, member, key); }, self, lobby, member, key);
    }
    void set_lobby_member_data(SetMemberDataFn real, void* self, uint64_t lobby, const char* key, const char* value) {
        route_void(real, [&](Matchmaking& m) { m.set_member_data(lobby, key, value); }, self, lobby, key, value);
    }
    int num_lobby_members(LobbyIntFn real, void* self, uint64_t lobby) {
        return route(real, 0, [&](Matchmaking& m) { return m.member_count(lobby); }, self, lobby);
    }
    uint64_t lobby_member_by_index(MemberByIndexFn real, void* self, uint64_t lobby, int index) {
        return route(real, uint64_t(0), [&](Matchmaking& m) { return m.member_by_index(lobby, index); }, self, lobby, index);
    }
    uint64_t lobby_owner(JoinFn real, void* self, uint64_t lobby) {
        return route(real, uint64_t(0), [&](Matchmaking& m) { return m.owner(lobby); }, self, lobby);
    }
    bool set_lobby_owner(SetOwnerFn real, void* self, uint64_t lobby, uint64_t new_owner) {
        return route(real, false, [&](Matchmaking& m) { return m.set_owner(lobby, new_owner); }, self, lobby, new_owner);
    }
    int lobby_member_limit(LobbyIntFn real, void* self, uint64_t lobby) {
        return route(real, 0, [&](Matchmaking& m) { return m.member_limit(lobby); }, self, lobby);
    }
    bool set_lobby_member_limit(LobbyIntSetFn real, void* self, uint64_t lobby, int limit) {
        return route(real, false, [&](Matchmaking& m) { return m.set_member_limit(lobby, limit); }, self, lobby, limit);
    }
    bool set_lobby_type(LobbyIntSetFn real, void* self, uint64_t lobby, int type) {
        return route(real, false, [&](Matchmaking& m) { return m.set_type(lobby, type); }, self, lobby, type);
    }
    bool set_lobby_joinable(JoinableFn real, void* self, uint64_t lobby, bool joinable) {
        return route(real, false, [&](Matchmaking& m) { return m.set_joinable(lobby, joinable); }, self, lobby, joinable);
    }
    bool lobby_game_server(GetServerFn real, void* self, uint64_t lobby, uint32_t* ip, uint16_t* port, uint64_t* server_id) {
        return route(real, false, [&](Matchmaking& m) { return m.get_server(lobby, ip, port, server_id); },
                     self, lobby, ip, port, server_id);
    }
    void set_lobby_game_server(SetServerFn real, void* self, uint64_t lobby, uint32_t ip, uint16_t port, uint64_t server_id) {
        route_void(real, [&](Matchmaking& m) { m.set_server(lobby, ip, port, server_id); }, self, lobby, ip, port, server_id);
    }
    void string_filter(StringFilterFn real, void* self, const char* key, const char* value, int comparison) {
        route_void(real, [&](Matchmaking& m) { m.string_filter(key, value, comparison); }, self, key, value, comparison);
    }
    void numerical_filter(NumericalFilterFn real, void* self, const char* key, int value, int comparison) {
        route_void(real, [&](Matchmaking& m) { m.numerical_filter(key, value, comparison); }, self, key, value, comparison);
    }
    void near_value_filter(NearFilterFn real, void* self, const char* key, int value) {
        route_void(real, [&](Matchmaking& m) { m.near_filter(key, value); }, self, key, value);
    }
    void slots_available_filter(IntArgFn real, void* self, int slots) {
        route_void(real, [&](Matchmaking& m) { m.slots_filter(slots); }, self, slots);
    }
    void distance_filter(IntArgFn real, void* self, int distance) {
        route_void(real, [&](Matchmaking&) {}, self, distance);
    }
    void result_count_filter(IntArgFn real, void* self, int count) {
        route_void(real, [&](Matchmaking& m) { m.result_count_filter(count); }, self, count);
    }
    void compatible_members_filter(U64ArgFn real, void* self, uint64_t lobby) {
        route_void(real, [&](Matchmaking&) {}, self, lobby);
    }
    bool send_lobby_chat(SendChatFn real, void* self, uint64_t lobby, const void* body, int size) {
        return route(real, false, [&](Matchmaking& m) { return m.send_chat(lobby, body, size); }, self, lobby, body, size);
    }
    int lobby_chat_entry(ChatEntryFn real, void* self, uint64_t lobby, int chat_id, uint64_t* user, void* data, int size,
                         int32_t* type) {
        return route(real, 0, [&](Matchmaking& m) { return m.chat_entry(lobby, chat_id, user, data, size, type); },
                     self, lobby, chat_id, user, data, size, type);
    }

    // A CM push (on the CM reader thread). Only an active tap takes it: a game
    // that is not reading launcher callbacks must not have them queue up.
    bool on_push(uint32_t emsg, const std::string& body) {
        if (auto s = active()) return s->matchmaking().on_push(emsg, body);
        return false;
    }

private:
    static constexpr size_t MaxRetired = 16;
    template <class R, class Ours, class Fn, class... A>
    R route(Fn real, R fallback, Ours ours, void* self, A... args) {
        if (auto s = active()) return ours(s->matchmaking());
        return real ? real(self, args...) : fallback;
    }
    template <class Ours, class Fn, class... A>
    void route_void(Fn real, Ours ours, void* self, A... args) {
        if (auto s = active()) { ours(s->matchmaking()); return; }
        if (real) real(self, args...);
    }
    // The installed Service, only while the game pumps with ManualDispatch.
    std::shared_ptr<Service> active() const {
        auto s = get();
        return s && s->active() ? s : nullptr;
    }
    mutable std::mutex m_;
    std::shared_ptr<Service> current_;
    std::deque<std::shared_ptr<Service>> retired_;
    bool init_seen_ = false;
    int32_t pipe_ = 0;
};

} // namespace steam_matchmaking
