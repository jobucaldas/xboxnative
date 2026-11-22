// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The callback ABI half of real Steam lobbies (issue #179, phase 1).
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
// Phase 1 only: nothing installs a Service at launch yet, so with no tap the
// whole layer is a pass-through. Lobby data, members and chat calls are not
// answered here.
//
// Portable (no Windows headers): the CM transport, the executor and the
// provider's real exports are all injected, so this compiles under Linux g++
// and the UWP clang-cl surface alike.
#include "steam_lobbies.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace steam_matchmaking {

// Callback ids (isteammatchmaking.h k_iSteamMatchmakingCallbacks = 500;
// steam_api_common.h / isteamutils.h for 703).
enum : int { CallbackLobbyEnter = 504, CallbackLobbyMatchList = 510, CallbackLobbyCreated = 513,
             CallbackAPICallCompleted = 703 };
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
    // A callback with no call behind it (LobbyEnter_t after a create).
    void broadcast(int callback, const void* data, size_t size) {
        std::lock_guard<std::mutex> lock(m_);
        if (closed_) return;
        push(callback, data, size);
    }

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
        run(s, [s, call] {
            Op op(*s);
            std::vector<steam_lobbies::LobbySummary> rows;
            const bool ok = steam_lobbies::list(op.io, s->cfg.appid, s->cfg.want_lobbies, {}, rows);
            auto matches = zeroed<LobbyMatchList_t>();
            {
                std::lock_guard<std::mutex> lock(s->m);
                s->listed.clear();
                if (ok) for (auto& row : rows) s->listed.push_back(row.steamid);
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
    // Teardown: tasks still in flight finish their round trip and post nothing.
    void close() { s_->closed.store(true); }

private:
    struct State {
        std::shared_ptr<Queue> queue;
        Config cfg;
        std::atomic<bool> closed{false};
        mutable std::mutex m;
        std::vector<uint64_t> listed;
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
    // queued is dropped and its tasks go quiet. It is kept until the next
    // install, because a game that already holds a launcher message between
    // GetNextCallback and FreeLastCallback still reads its buffer.
    void set(std::shared_ptr<Service> service) {
        std::shared_ptr<Service> old, dropped;
        { std::lock_guard<std::mutex> lock(m_); old = std::move(current_); current_ = std::move(service); }
        if (old) old->close();
        { std::lock_guard<std::mutex> lock(m_); dropped = std::move(retired_); retired_ = std::move(old); }
    }
    std::shared_ptr<Service> get() const { std::lock_guard<std::mutex> lock(m_); return current_; }

    void init(InitFn real) {
        if (real) real();
        if (auto s = get()) s->splice().init_seen();
    }
    int32_t pipe(PipeFn real) {
        const int32_t pipe = real ? real() : 0;
        if (auto s = get()) s->splice().set_pipe(pipe);
        return pipe;
    }
    bool next(NextFn real, int32_t pipe, CallbackMsg_t* msg) {
        if (auto s = get()) return s->splice().next(real, pipe, msg);
        return real ? real(pipe, msg) : false;
    }
    void free_last(FreeFn real, int32_t pipe) {
        if (auto s = get()) return s->splice().free_last(real, pipe);
        if (real) real(pipe);
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

private:
    // The installed Service, only while the game pumps with ManualDispatch.
    std::shared_ptr<Service> active() const {
        auto s = get();
        return s && s->active() ? s : nullptr;
    }
    mutable std::mutex m_;
    std::shared_ptr<Service> current_, retired_;
};

} // namespace steam_matchmaking
