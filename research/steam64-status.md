> Local integration correction (2026-10-27): the supplied research below is preserved as historical source material. Its off-console/pinned-DLL feasibility conclusion is superseded by João’s Xbox-only requirement. No service replacement is selected. Local `llvm-readobj` evidence now verifies 147 installed PE files: see `real-client-evidence/all-client-pe.json` and `GAMENATIVE-STEAM.md`. Game-mode memory is accepted; it is not an investigation or planning gate.

# Steam 64-bit Windows client: status and fit for the x64-only Xbox stack

Researched 2026-10-27 (BRT). Only pages actually fetched are cited. Read-only research, nothing posted anywhere.

## Short answer
- **Confirmed (Valve primary source):** Valve shipped a 64-bit Windows Steam client on **2025-12-19**. The release note says "The Steam client is now 64-bit on Windows 11 and Windows 10 64-bit."
- **Confirmed (Valve):** 32-bit Windows support ended **2026-01-01**, and "Future versions of Steam will run on 64-bit versions of Windows only" (quoted by TechPowerUp from the Valve support note).
- **Still 32-bit:** `SteamService.exe` ("Steam Client Service"). This comes from user reports on Task Manager and file paths, not from a Valve statement. It is consistent across several independent observers (see below), but I found no Valve page that states it.
- **Not found:** an authoritative per-binary list of 32-bit vs 64-bit files in the Steam directory. Nobody I could fetch published one, so every other helper is **uncertain**.
- **Fit for an x64-only stack:** the CPU architecture now fits (x64 `steam.exe`/webhelper/`steamclient64.dll`). The Windows process model is the real blocker (see "Fit"). That part is our own analysis, not a Valve fact.

## What is confirmed, with sources

| Claim | Status | Source (fetched) |
|---|---|---|
| Steam client became 64-bit on Win10 x64 / Win11 on 2025-12-19 (re-released 2025-12-22 with small fixes) | Confirmed, Valve release notes | Steam News "Steam Client Update - December 19th", fetched through Steam's public news API (appid 593110): https://steamstore-a.akamaihd.net/news/externalpost/steam_community_announcements/1819386365096998 (community mirror: https://steamcommunity.com/games/593110/announcements/detail/528740542771627405 appears in search results but I did not fetch it) |
| Same release note, also at GamingOnLinux (2025-12-20) | Confirmed, verbatim copy | https://www.gamingonlinux.com/2025/12/latest-steam-stable-update-is-live-as-windows-gets-64-bit/ |
| 32-bit Windows 10 support ends 2026-01-01 10am; 32-bit *games* keep running on 64-bit Windows | Confirmed. The Steam client update of 2025-10-02 links to the support page. | Steam News "Steam Client Update - October 2nd" (Steam news API): https://steamcommunity.com/ogg/593110/announcements/detail/507340830949770871 ; support page https://help.steampowered.com/en/faqs/view/49A1-B944-48B8-FF00 (title "Windows 32-bit OS Support" loads, but the body did not render for my fetcher, so the wording comes from TechPowerUp's quote: https://www.techpowerup.com/341141/steam-confirms-end-of-support-for-32-bit-windows-os-in-2026 via search result) |
| Valve's stated reason: core features rely on "system drivers and other libraries that are not supported on 32-bit versions of Windows" | Confirmed (second-hand) | Ars Technica, 2025-09: https://arstechnica.com/gaming/2025/09/steam-will-drop-support-for-the-last-32-bit-windows-systems-in-january-2026/ (read via search excerpt; page not fetched directly) |
| Press coverage: Dec 2025 "Steam is now 64-bit" | Confirmed, but secondary and imprecise | TechPowerUp 2025-12-22 https://www.techpowerup.com/344312/steam-client-officially-moves-to-64-bit-on-windows-11-and-windows-10 ; gHacks 2025-12-22 (updated 2026-02-06) https://www.ghacks.net/2025/12/22/steams-december-update-moves-the-windows-client-fully-to-64-bit/ |
| No Valve release note since then (checked all Steam Client Update posts to 2026-10-21 through the news API) mentions making any other Windows helper 64-bit. The 2026-10-21 note only mentions a "Steam Service Error dialog" fix. | Confirmed absence in release notes. This is not proof that nothing changed. | https://steamstore-a.akamaihd.net/news/externalpost/steam_community_announcements/1847286114030478 |

## Helper processes: 32-bit vs 64-bit

| Binary | Bitness | Confidence | Evidence |
|---|---|---|---|
| `steam.exe` (main client) | 64-bit on Win10 x64 / Win11 | **Confirmed by Valve** ("client is now 64-bit"). The exact PE machine type was not inspected by me. | Release note above |
| `steamwebhelper.exe` (CEF/Chromium UI helper) | 64-bit | Likely. A 2021 Steam Client Beta thread says the webhelper "has been 64-bit for years" and ships in multiple builds. TechPowerUp and gHacks commenters say it is part of the 64-bit client. I have no Valve statement for the current build. | Steam Client Beta thread (2021) via search excerpt: https://steamcommunity.com/groups/SteamClientBeta/discussions/1/828934089694687152/ ; TechPowerUp comments |
| `steamclient64.dll`, `GameOverlayRenderer64.dll`, `steamerrorreporter64.exe`, and other `*64` files | 64-bit builds have shipped for years alongside 32-bit builds | Likely. The 2021 beta-thread poster lists them; a Steam forum post from 2026-01-06 mentions `Steam Overlayui64.exe` in the install dir. Not verified from the files. | Same beta thread; https://steamcommunity.com/discussions/forum/1/694249410478227875/ (fetched) |
| `steamclient.dll`, `steam_api.dll` (32-bit) | Still needed for 32-bit games | Likely. No Valve statement; this is inferred from "32-bit games continue to be supported" (Oct 2 note). A gHacks commenter argues the same. A commenter is not an authority. | Oct 2 note; gHacks comments |
| **`SteamService.exe`** (installed at `C:\Program Files (x86)\Common Files\Steam\`) | **32-bit** (as reported 2025-12-22 to 2026-01-06) | **Reported, not Valve-confirmed.** Several independent users saw it in Task Manager/Details: TechPowerUp comments (2025-12-22), gHacks comment (2025-12-25: path plus "wiped the service and re-installed, still 32-bit"), Steam forum thread (2026-01-06: "Steam Client Service (32bit)" process), Reddit r/Steam (2025-12-22, via search excerpt). I could not find any report that it later became 64-bit. | TechPowerUp, gHacks and the Steam forum thread (fetched); Reddit https://www.reddit.com/r/Steam/comments/1pstfgx/ (search excerpt only; fetch timed out) |
| Other helpers (`gameoverlayui.exe`, `steamerrorreporter.exe`, `crashhandler`, `drivers/`, `vulkan` layers and similar) | **Unknown** | Uncertain. Not enumerated by any fetched source. The only way to settle this is to download the current Steam install and run `llvm-readobj --file-headers` (or `file`) on every PE. | n/a |

## Fit for an x64-only stack on Xbox (analysis, not sourced fact)
1. **ISA: fits.** The 64-bit client means the main process, its web UI helper and the 64-bit Steamworks client DLL are x64. That matches the project's pinned `steamclient64` approach for 64-bit games like Megabonk (BACKLOG item 5 `steam-map`).
2. **Process model: does not fit as-is.** The real client is a desktop Win32 program that spawns child processes (webhelper's CEF renderer/GPU/utility children, error reporter, game launches) and relies on a Windows service (`SteamService`) for elevation, runtimes and driver setup. `LANES.md` §1 already records that `CreateProcessW` is blocked in AppContainer and that there is no classic window station or HWND. That is a project document, not an independent source. So the real Valve client cannot run in a UWP dev-mode app without the same uwph/Wine-style host work, and it would still need:
   - Chromium/CEF (webhelper) to run under that host, which is a very large surface;
   - a replacement for `SteamService` (it is 32-bit, so even a perfect x64-only host cannot run it, and it needs elevation/service control that AppContainer does not give).
3. **What stays uncertain:** whether the real client has any hard dependency on `SteamService.exe` when only launching/authenticating (rather than installing redistributables). I found no documentation either way.
4. **Practical reading for the project:** "legitimate real-client path" (PLAYABLE.md item 5) is feasible only in the narrow form already planned: an x64 `steamclient64.dll`-style IPC peer or Valve's own client running elsewhere. Running the full 64-bit Windows client on the console is not unblocked by Valve's 64-bit switch, because the process model, not bitness, is the blocker.

## Caveats
- Several sources were read through the search tool's excerpts, not as full pages (marked above). The Steam help page "Windows 32-bit OS Support" returned only its title to my fetcher.
- No binary was downloaded or inspected. Every per-file bitness claim except "client is 64-bit" rests on user reports.
- gHacks' headline says "fully 64-bit". Valve's own note does not say "fully", and `SteamService.exe` being 32-bit contradicts it.
