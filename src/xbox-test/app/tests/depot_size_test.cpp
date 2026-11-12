// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression for the game page's pre-download size (depot_size.h): the
// figure must come from the depots the downloader would actually install.
#include "../depot_size.h"
#include <cassert>
#include <cstdio>
#include <string>

static const char* kAppInfo = R"("appinfo"
{
	"appid"		"100"
	"common" { "name" "Fixture" }
	"depots"
	{
		"baselanguages"		"english"
		"101"
		{
			"config" { "oslist" "windows" "osarch" "64" }
			"manifests" { "public" { "gid" "11" "size" "900" "download" "700" } }
		}
		"102"
		{
			"config" { "oslist" "windows" "osarch" "32" }
			"manifests" { "public" { "gid" "12" "size" "800" "download" "600" } }
		}
		"103"
		{
			"config" { "oslist" "macos" }
			"manifests" { "public" { "gid" "13" "size" "500" "download" "400" } }
		}
		"104"
		{
			"config" { "oslist" "windows" "language" "german" }
			"manifests" { "public" { "gid" "14" "size" "300" "download" "200" } }
		}
		"105"
		{
			"depotfromapp"		"228980"
			"config" { "oslist" "windows" }
			"manifests" { "public" { "gid" "15" "size" "100" "download" "90" } }
		}
		"106"
		{
			"config" { "oslist" "windows" }
			"manifests" { "public" { "gid" "16" "size" "50" } }
		}
		"branches" { "public" { "buildid" "1" } }
	}
}
)";

int main() {
    // 64-bit build wins over the 32-bit one; macos, german and the shared
    // redistributable depot are not installed; a depot with no "download"
    // figure contributes nothing but does not hide the rest.
    std::vector<uint32_t> chosen;
    const uint64_t b = depot_size::from_appinfo(kAppInfo, &chosen);
    assert(b == 700);
    assert((chosen == std::vector<uint32_t>{101, 106}));

    // no product info, or one with no depots: no figure (the caller falls back)
    assert(depot_size::from_appinfo("") == 0);
    assert(depot_size::from_appinfo("\"appinfo\" { \"appid\" \"1\" }") == 0);

    // every kept depot adds up
    std::string two = R"("appinfo" { "depots" {
        "201" { "config" { "oslist" "windows" } "manifests" { "public" { "gid" "1" "download" "1000" } } }
        "202" { "config" { } "manifests" { "public" { "gid" "2" "download" "2500" } } }
    } })";
    assert(depot_size::from_appinfo(two) == 3500);

    std::printf("DEPOT-SIZE PASS\n");
    return 0;
}
