#pragma once
#include <string>

namespace session_restore {
enum class Result { restored, retry, qr };
inline bool token_invalid(int er) {
    return er == 15 || er == 5 || er == 63 || er == 65 || er == 18 || er == 50 || er == 43;
}
// Callbacks keep storage/UI effects in the platform layer; success never mutates storage.
template<class Reject, class ShowQr>
Result complete(bool connected, int er, std::string& refresh, Reject reject, ShowQr showQr) {
    if (connected) return Result::restored;
    if (!token_invalid(er)) return Result::retry;
    reject(refresh);
    refresh.clear();
    showQr();
    return Result::qr;
}
}

// Compare before modifying storage, so a replacement login cannot be erased by an old rejection.
namespace session_restore {
template<class Read, class Write, class Erase>
bool clear_rejected(const std::string& rejected, Read read, Write write, Erase erase) {
    std::string current, access;
    if (!read(current, access) || current != rejected) return false;
    if (access.empty()) erase(); else write(std::string(), access);
    return true;
}
}
