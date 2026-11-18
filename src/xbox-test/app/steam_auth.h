// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Steam auth-session ticket registration.  The provider owns ticket creation;
// this bridge only copies the bytes it handed the game and registers them on
// the launcher's already-authenticated CM session.
#include "steam_wire.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace steam_auth {
constexpr uint32_t ClientRegisterAuthTicketWithCM = 5502;
constexpr uint32_t ClientAuthList = 5432;
constexpr uint32_t ClientAuthListAck = 5575;

struct Io {
    std::function<void(uint32_t, const std::string&)> send;
    std::function<void(const std::string&)> log;
};

struct Ticket {
    uint32_t handle = 0, appid = 0, crc = 0;
    uint64_t gc_token = 0;
    std::string bytes;
};

inline uint32_t crc32(const std::string& bytes) {
    uint32_t crc = 0xffffffffu;
    for (unsigned char byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

inline uint64_t gc_token(const std::string& bytes) {
    if (bytes.size() < 12) return 0;
    uint32_t gc_size = 0;
    memcpy(&gc_size, bytes.data(), sizeof gc_size);
    if (gc_size != 20) return 0; // gbe_fork's legacy and new ticket GC prefix
    uint64_t token = 0;
    memcpy(&token, bytes.data() + 4, sizeof token);
    return token;
}

inline std::string register_body(const std::string& ticket) {
    std::string body;
    pb_u(body, 1, SteamClientProtocolVersion); // CMsgClientRegisterAuthTicketWithCM.protocol_version
    pb_s(body, 3, ticket);
    return body;
}

inline std::string auth_list_body(uint32_t appid, const std::vector<Ticket>& tickets, uint32_t sequence) {
    std::string body;
    pb_u(body, 1, 0); // tokens_left; the provider has no CM token pool
    for (const auto& ticket : tickets) {
        std::string item;
        pb_fixed64(item, 4, appid); // CMsgAuthTicket.gameid (fixed64)
        pb_u(item, 6, ticket.crc);  // CMsgAuthTicket.ticket_crc
        pb_s(item, 7, ticket.bytes);// CMsgAuthTicket.ticket
        pb_s(body, 4, item);        // CMsgClientAuthList.tickets
    }
    pb_u(body, 5, appid); // keep the app in the list after the last ticket is canceled
    pb_u(body, 6, sequence); // message_sequence; Steam rejects repeated list numbers
    return body;
}

struct Bridge {
    mutable std::mutex m;
    uint32_t appid_ = 0;
    Io io_;
    std::map<uint32_t, Ticket> active_;
    std::map<uint32_t, uint32_t> handles_by_crc_;
    std::map<uint32_t, bool> acknowledged_;
    uint32_t message_sequence_ = 0;

    void reset() {
        std::lock_guard<std::mutex> lock(m);
        appid_ = 0; io_ = {}; active_.clear(); handles_by_crc_.clear(); acknowledged_.clear(); message_sequence_ = 0;
    }
    void start(uint32_t appid, Io io) {
        Io bound; std::vector<Ticket> list; uint32_t sequence = 0;
        {
            std::lock_guard<std::mutex> lock(m);
            appid_ = appid; io_ = std::move(io); bound = io_;
            for (const auto& entry : active_) list.push_back(entry.second);
            if (!list.empty()) sequence = ++message_sequence_;
        }
        // Steam forgets ticket registrations with the old CM connection.
        for (const auto& ticket : list) bound.send(ClientRegisterAuthTicketWithCM, register_body(ticket.bytes));
        if (!list.empty()) bound.send(ClientAuthList, auth_list_body(appid, list, sequence));
    }
    bool issue(uint32_t handle, const std::string& bytes) {
        if (!handle || bytes.empty()) return false;
        Ticket ticket; Io io; std::vector<Ticket> list; uint32_t sequence = 0;
        {   std::lock_guard<std::mutex> lock(m);
            if (!appid_ || !io_.send) return false;
            ticket = {handle, appid_, crc32(bytes), gc_token(bytes), bytes};
            active_[handle] = ticket; handles_by_crc_[ticket.crc] = handle; acknowledged_[handle] = false;
            io = io_;
            for (const auto& entry : active_) list.push_back(entry.second);
            sequence = ++message_sequence_;
        }
        io.send(ClientRegisterAuthTicketWithCM, register_body(ticket.bytes));
        io.send(ClientAuthList, auth_list_body(ticket.appid, list, sequence));
        return true;
    }
    bool cancel(uint32_t handle) {
        Io io; uint32_t appid = 0; std::vector<Ticket> list; uint32_t sequence = 0;
        {   std::lock_guard<std::mutex> lock(m);
            auto it = active_.find(handle); if (it == active_.end() || !io_.send) return false;
            handles_by_crc_.erase(it->second.crc); acknowledged_.erase(handle); active_.erase(it);
            appid = appid_; io = io_;
            for (const auto& entry : active_) list.push_back(entry.second);
            sequence = ++message_sequence_;
        }
        io.send(ClientAuthList, auth_list_body(appid, list, sequence));
        return true;
    }
    bool end() {
        Io io; uint32_t appid = 0, sequence = 0;
        {
            std::lock_guard<std::mutex> lock(m);
            if (appid_ && io_.send) { io = io_; appid = appid_; sequence = ++message_sequence_; }
            appid_ = 0; io_ = {}; active_.clear(); handles_by_crc_.clear(); acknowledged_.clear();
        }
        if (!appid) return false;
        io.send(ClientAuthList, auth_list_body(appid, {}, sequence));
        return true;
    }
    unsigned ack(const std::string& body) {
        unsigned count = 0; std::lock_guard<std::mutex> lock(m);
        auto mark = [&](uint64_t crc) {
            auto it = handles_by_crc_.find(uint32_t(crc));
            if (it == handles_by_crc_.end()) return;
            acknowledged_[it->second] = true; ++count;
        };
        for (const auto& field : pb_parse(body)) {
            if (field.f != 1) continue;
            if (field.wt == 0) mark(field.u);
            else if (field.wt == 2) {
                size_t i = 0;
                while (i < field.s.size()) {
                    uint64_t value = 0; int shift = 0;
                    while (i < field.s.size()) {
                        uint8_t byte = uint8_t(field.s[i++]);
                        value |= uint64_t(byte & 0x7f) << shift;
                        if (!(byte & 0x80)) { mark(value); break; }
                        shift += 7;
                    }
                }
            }
        }
        return count;
    }
    bool acknowledged(uint32_t handle) const {
        std::lock_guard<std::mutex> lock(m);
        auto it = acknowledged_.find(handle); return it != acknowledged_.end() && it->second;
    }
};
} // namespace steam_auth
