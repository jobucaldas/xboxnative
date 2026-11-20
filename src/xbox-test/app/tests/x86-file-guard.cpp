// SPDX-License-Identifier: GPL-3.0-or-later
// x86_file_guard.h: a Win32 path must normalize to the file the native API
// opens, or a protected root can be reached under another spelling.
#include "../host/x86/x86_file_guard.h"
#include <cstdio>
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)
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
    // ".. " and "..\x20." strip to the parent (Win32 strips trailing spaces and dots), so they escape to LocalState's sibling.
    CHECK(!local.allow("create", u("C:\\Game\\Other\\.. \\LocalState\\save.dat")));
    CHECK(!local.allow("create", u("C:\\Game\\Other\\..\x20.\\LocalState\\save.dat")));
    CHECK(x86_file_guard::normalize(u("C:\\Game\\Other\\.. \\LocalState"), {}) == u("c:\\game\\localstate"));
    // "..." has no safe meaning (dropped or parent), so the guard refuses it.
    CHECK(!local.allow("delete", u("C:\\Game\\Other\\...\\LocalState\\save.dat")));
    CHECK(!local.allow("delete", u("C:\\Game\\Other\\...\\..\\LocalState\\save.dat")));
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
