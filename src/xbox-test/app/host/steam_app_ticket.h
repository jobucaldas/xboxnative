// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

// Steam's app ownership ticket, as the Steam servers issue it to the signed-in
// owner (CM ClientGetAppOwnershipTicket) and as the Steam client hands it out
// through ISteamAppTicket::GetAppOwnershipTicketData:
//   u32 signed length, u32 version, u64 SteamID, u32 app ID, ... (signed part)
//   followed by Valve's signature over the signed part.
// Play only passes the genuine ticket through; it never builds or alters one.
namespace steam_app_ticket {
struct Layout {
    uint32_t appid = 0, size = 0, appidOffset = 16, steamidOffset = 8, signatureOffset = 0, signatureSize = 0;
    uint64_t steamid = 0;
};
inline uint32_t u32(const std::string& b, size_t at) { uint32_t v; memcpy(&v, b.data() + at, 4); return v; }
inline bool parse(const std::string& bytes, Layout& out) {
    out = {};
    if (bytes.size() < 24 || bytes.size() > 4096) return false;
    uint32_t signedLength = u32(bytes, 0);
    if (signedLength < 24 || signedLength >= bytes.size()) return false;
    out.size = uint32_t(bytes.size());
    out.appid = u32(bytes, 16);
    memcpy(&out.steamid, bytes.data() + 8, 8);
    out.signatureOffset = signedLength;
    out.signatureSize = out.size - signedLength;
    return out.appid != 0 && out.steamid != 0;
}
// ISteamAppTicket::GetAppOwnershipTicketData: the ticket for `appid` only,
// copied whole, with the offsets the real client reports; 0 when there is no
// ticket for that app or the buffer is too small.
inline uint32_t copy(const std::string& bytes, uint32_t appid, void* buffer, uint32_t capacity,
                     uint32_t* appidOffset, uint32_t* steamidOffset, uint32_t* signatureOffset, uint32_t* signatureSize) {
    Layout t;
    if (!parse(bytes, t) || t.appid != appid || !buffer || capacity < t.size) return 0;
    memcpy(buffer, bytes.data(), t.size);
    if (appidOffset) *appidOffset = t.appidOffset;
    if (steamidOffset) *steamidOffset = t.steamidOffset;
    if (signatureOffset) *signatureOffset = t.signatureOffset;
    if (signatureSize) *signatureSize = t.signatureSize;
    return t.size;
}
}
