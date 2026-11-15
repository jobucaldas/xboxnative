// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// SteamTokenLogin.kt's config.vdf ConnectCache representation. This is the
// client's reversible token obfuscation format, not encryption or protection.
static const uint32_t steam_token_store_obfuscation_words[] = {
    0x1739a3b0, 0xb8907fe1, 0x8290d3b7, 0x72839cd0, 0x242df096,
    0x3829750b, 0x38de7a77, 0x72f0924c, 0x44783927, 0x01925372,
    0x20902714, 0x27585920, 0x27890632, 0x82910476, 0x72906721,
    0x28798904, 0x78592700
};
#define STEAM_TOKEN_STORE_KEY_STEP 0x25fe6761u

static inline uint32_t steam_token_store_read_le32(const unsigned char* bytes) {
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
        (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}
static inline void steam_token_store_hex_byte(char* out, unsigned char value) {
    static const char hex[] = "0123456789abcdef";
    out[0] = hex[value >> 4]; out[1] = hex[value & 15];
}
static inline size_t steam_token_store_obfuscate(const char* token, size_t token_size,
                                                  uint64_t mtbf, char* out, size_t capacity) {
    const size_t plain_size = token_size + 1;
    const size_t byte_size = plain_size + 8;
    if (!token || !out || capacity < byte_size * 2 + 1) return 0;
    uint32_t k1 = uint32_t(mtbf >> 31), k2 = uint32_t(mtbf), checksum = 0;
    size_t written = 0, at = 0;
    memcpy(out, "02000000", 8); written = 8;
    while (plain_size - at >= 4) {
        unsigned char block[4];
        for (unsigned i = 0; i < 4; ++i) block[i] = at + i == token_size ? 0 : (unsigned char)token[at + i];
        const uint32_t word = steam_token_store_read_le32(block);
        k1 += STEAM_TOKEN_STORE_KEY_STEP; ++k2; checksum += word;
        const uint32_t encoded = steam_token_store_obfuscation_words[k2 % 17] ^ k1 ^ word;
        for (unsigned i = 0; i < 4; ++i) steam_token_store_hex_byte(out + written + i * 2, (unsigned char)(encoded >> (i * 8)));
        written += 8; at += 4;
    }
    while (at < plain_size) {
        const unsigned char byte = at == token_size ? 0 : (unsigned char)token[at];
        steam_token_store_hex_byte(out + written, byte); written += 2; ++at;
    }
    k1 += STEAM_TOKEN_STORE_KEY_STEP; ++k2;
    const uint32_t tail = steam_token_store_obfuscation_words[k2 % 17] ^ k1 ^ checksum;
    for (unsigned i = 0; i < 4; ++i) steam_token_store_hex_byte(out + written + i * 2, (unsigned char)(tail >> (i * 8)));
    written += 8; out[written] = 0;
    return written;
}
static inline bool steam_token_store_safe_value(const char* value) {
    if (!value || !*value) return false;
    for (const unsigned char* p=(const unsigned char*)value; *p; ++p)
        if (*p < 0x20 || *p == '"' || *p == '\\') return false;
    return true;
}
static inline size_t steam_token_store_cache_key(const char* login, char* out, size_t capacity) {
    if (!login || !out || capacity < 10) return 0;
    uint32_t crc = 0xffffffffu;
    for (const unsigned char* p=(const unsigned char*)login; *p; ++p) {
        crc ^= *p;
        for (unsigned bit=0; bit<8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    int n=snprintf(out,capacity,"%x1",crc ^ 0xffffffffu);
    return n > 0 && (size_t)n < capacity ? (size_t)n : 0;
}
static inline size_t steam_token_store_config_vdf(const char* token, const char* login,
                                                   const char* steam_id, uint64_t mtbf,
                                                   char* out, size_t capacity) {
    if (!token || !*token || !steam_token_store_safe_value(login) ||
        !steam_token_store_safe_value(steam_id) || mtbf < 1000000000ULL || mtbf >= 2000000000ULL || !out) return 0;
    for (const char* p=steam_id; *p; ++p) if (*p < '0' || *p > '9') return 0;
    char key[16], encoded[16403];
    if (!steam_token_store_cache_key(login,key,sizeof key) ||
        !steam_token_store_obfuscate(token,strlen(token),mtbf,encoded,sizeof encoded)) return 0;
    int n=snprintf(out,capacity,
        "\"InstallConfigStore\"\n{\n\t\"Software\"\n\t{\n\t\t\"Valve\"\n\t\t{\n\t\t\t\"Steam\"\n\t\t\t{\n"
        "\t\t\t\t\"MTBF\"\t\t\"%llu\"\n"
        "\t\t\t\t\"ConnectCache\"\n\t\t\t\t{\n\t\t\t\t\t\"%s\"\t\t\"%s\"\n\t\t\t\t}\n"
        "\t\t\t\t\"Accounts\"\n\t\t\t\t{\n\t\t\t\t\t\"%s\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"SteamID\"\t\t\"%s\"\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n",
        (unsigned long long)mtbf,key,encoded,login,steam_id);
    return n > 0 && (size_t)n < capacity ? (size_t)n : 0;
}
