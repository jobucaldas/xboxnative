// SPDX-License-Identifier: GPL-3.0-or-later
// x86_file_guard.h: a Win32 path must normalize to the file the native API
// opens, or a protected root can be reached under another spelling.
#include "../host/x86/x86_file_guard.h"
#include <cstdio>
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)
using x86_file_guard::ambiguous;
static std::u16string u(const char* s) { std::u16string r; while (*s) r.push_back(char16_t(*s++)); return r; }

int main() {
    const auto protectedRoot = u("C:\\Game\\LocalState");
    x86_file_guard::Policy local({protectedRoot}, {}, u("C:\\Game"));
    // Device namespace and drive-relative spellings of the protected root.
    CHECK(!local.allow("create", u("\\\\.\\C:\\Game\\LocalState\\save.dat")));
    CHECK(!local.allow("create", u("C:LocalState\\save.dat")));
    // Win32 drops trailing periods and spaces from components.
    CHECK(!local.allow("delete", u("C:\\Game\\LocalState.\\save.dat")));
    CHECK(!local.allow("delete", u("C:\\Game\\LocalState \\save.dat")));
    // Dots-and-spaces-only names other than "." and ".." are refused: they could escape to LocalState's sibling.
    CHECK(!local.allow("create", u("C:\\Game\\Other\\.. \\LocalState\\save.dat")));
    CHECK(!local.allow("create", u("C:\\Game\\Other\\..\x20.\\LocalState\\save.dat")));
    CHECK(!local.allow("create", u("C:\\Game\\Other\\. .\\LocalState\\save.dat")));
    CHECK(!local.allow("create", u("C:\\Game\\Other\\ ..\\LocalState\\save.dat")));
    CHECK(!local.allow("delete", u("C:\\Game\\Other\\...\\LocalState\\save.dat")));
    CHECK(!local.allow("delete", u("C:\\Game\\Other\\...\\..\\LocalState\\save.dat")));
    CHECK(ambiguous(u("C:\\Game\\Other\\.. \\LocalState")) && ambiguous(u("a\\. .\\b")) && ambiguous(u("a\\ ..\\b")));
    CHECK(!ambiguous(u("C:\\Game\\a.\\.\\..\\b")) && !ambiguous(u("C:\\Game\\LocalState \\save")));
    // Ordinary trailing dots and spaces on real names are still stripped; "." and ".." keep their meaning.
    CHECK(x86_file_guard::normalize(u("C:\\Game\\LocalState. \\x"), {}) == u("c:\\game\\localstate\\x"));
    CHECK(x86_file_guard::normalize(u("C:\\Game\\Other\\..\\LocalState\\x"), {}) == u("c:\\game\\localstate\\x"));
    // A rooted name is on the cwd drive.
    CHECK(!local.allow("create", u("\\Game\\LocalState\\save.dat")));
    // Ordinary cwd-relative files stay allowed.
    CHECK(local.allow("create", u("save.dat")));
    CHECK(x86_file_guard::normalize(u("\\\\.\\C:\\x"), {}) == u("c:\\x"));
    CHECK(x86_file_guard::normalize(u("C:file"), u("C:\\Game")) == u("c:\\game\\file"));

    // UNC names are absolute and must not gain the cwd drive.
    x86_file_guard::Policy unc({u("\\\\server\\share\\LocalState")}, {}, u("C:\\Game"));
    CHECK(!unc.allow("create", u("\\\\server\\share\\LocalState\\save.dat")));
    CHECK(x86_file_guard::normalize(u("\\\\server\\share\\a"), u("c:\\game")) == u("\\\\server\\share\\a"));

    printf("x86-file-guard: checks=%d failed=%d\n", checks, failures);
    return failures ? 1 : 0;
}
