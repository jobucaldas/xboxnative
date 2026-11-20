// SPDX-License-Identifier: GPL-3.0-or-later
// Host side of kernel32 GetTimeFormatW/GetDateFormatW (host/x86/x86_play.h
// play_locale_call) under real Windows semantics: a size query (cch 0) must
// report the length without copying any characters, and a sized call must copy
// exactly the returned length. Cross-compiled with mingw and run under wine by
// x86-play-locale.sh.
#include <cstdio>
#include "../host/x86/x86_play.h"
#if defined(_WIN32)
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); } } while (0)
int main() {
    // Size query: lpTimeStr non-null, cchTime 0. Per GetTimeFormatEx: "If this
    // value is 0, the function returns the size of the buffer required to hold
    // the time string, and lpTimeStr is not used."
    x86::Host::LocaleCall size{};
    size.locale = 0x0409; size.has_output_buffer = true; size.buffer_chars = 0;
    x86::play_locale_call(size);
    CHECK(size.succeeded);
    CHECK(size.result > 0);
    CHECK(size.data.empty());
    // Sized call: the copied bytes are exactly result characters (UTF-16).
    x86::Host::LocaleCall sized{};
    sized.locale = 0x0409; sized.has_output_buffer = true; sized.buffer_chars = 64;
    x86::play_locale_call(sized);
    CHECK(sized.succeeded);
    CHECK(sized.result > 0 && sized.result <= 64);
    CHECK(sized.data.size() == size_t(sized.result) * sizeof(wchar_t));
    // Date with a size query, same contract as GetDateFormatEx.
    x86::Host::LocaleCall date{};
    date.date = true; date.locale = 0x0409; date.has_output_buffer = true; date.buffer_chars = 0;
    x86::play_locale_call(date);
    CHECK(date.succeeded && date.result > 0 && date.data.empty());
    std::printf("x86-play-locale: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
#else
int main() { std::printf("x86-play-locale: Windows only (cross-compile with mingw and run under wine)\n"); return 0; }
#endif
