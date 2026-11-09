# Install locations (internal drive and USB)

The launcher installs games under its own `LocalState` on the console's internal drive by default. The detail page
of a game that is not installed also offers other drives: **X** cycles *Internal (N GB free)* and
*USB E: (N GB free)*; **A** installs there. The choice is stored in the game's install record, so download,
launch, delete and Steam Cloud (`GameInstall`) all use that game's own folder. Existing installs are unchanged.

While a download runs or is queued the button reads *INSTALLING... A: cancel*: one **A** press cancels it (the
partial files and the queue entry are removed). **B** only goes back to the library and the download keeps running.
A finished install does not start the game: the page and the library show *Installed. Press A to play*, and **A**
launches it.

## How it works

- `LocalState\dl_<appid>.done` (and `dl_<appid>.partial` while downloading) always stays in `LocalState`.
  A game on another drive adds `root <drive>:\<folder>` before `installdir`; no `root` means `LocalState`
  (`game_library::parse_marker`, `valid_root`). Roots are plain ASCII drive paths without spaces.
- Extra roots are `<drive>:\XboxNativeGames`, created when the page opens. A drive where that folder cannot be created
  (not mounted, read-only, blocked by the sandbox) is not listed; the reason is written to `boot.log` (`storage:` lines).
- The downloader checks free space on the chosen drive, creates and verifies files there, and rolls back an unfinished
  download from that drive at the next start. Two installs with the same folder name on different drives are not
  treated as sharing files.
- Play and the PE mapper open guest files through Win32 paths (`_wfopen`, `FindFirstFile`, `CreateFile2`) built from
  the install folder, so no code path depends on the folder being under `LocalState`. If the drive is unplugged,
  Play reports that the drive is not connected.

## What a Dev Mode UWP app can use on Xbox (research, not console-verified)

| Route | What the sources say | Used here |
|---|---|---|
| `removableStorage` capability + `KnownFolders.RemovableDevices` | Brokered; only files of file types declared as file type associations in the manifest can be enumerated, created or changed ([docs](https://learn.microsoft.com/en-us/uwp/api/windows.storage.knownfolders.removabledevices)). Not suited to game files of arbitrary types. | Capability declared; the WinRT folder is not used. |
| Win32 on a drive letter (`E:\`) | Community reports show Dev Mode UWP apps reading and writing a USB drive by path; Dev Mode assigns the letter (E: reported) ([guide](https://forums.libretro.com/t/xbox-series-x-i-cant-load-games-from-my-external-drive/35794)). | Yes. Needs verifying on the console. |
| `broadFileSystemAccess` (restricted) | Same file-system access as the user, gated by a Settings > Privacy > File system toggle ([Windows blog](https://blogs.windows.com/windowsdeveloper/2018/05/18/console-uwp-applications-and-file-system-access/), [file access permissions](https://learn.microsoft.com/en-us/windows/uwp/files/file-access-permissions)). The user must enable it; this is not a bypass and it is not requested here. | Not declared. Fallback to try if `E:\` access is denied. |

File systems: only a drive formatted as **NTFS** is described as workable for apps, with `ALL APPLICATION PACKAGES`
having access in its ACL (grant it from a PC: drive Properties > Security > Advanced > Add > *All App Packages* >
Full control, inherit to children). exFAT/FAT32 carry no permissions, so nothing needs granting, but reports for them
conflict ([helper](https://github.com/ZebulanStanphill/xbox-media-drive-helper), [forum](https://forums.libretro.com/t/xbox-series-x-i-cant-load-games-from-my-external-drive/35794)).
A drive formatted by the console as *Games & apps* is encrypted and is not readable by apps; use a drive the console
treats as *media* storage (format it on a PC or choose the media option when the console asks).

The package path (`Q:\`-style package roots) is the app's own install and does not change: only game folders move.

## Why the free space the launcher sees is small

`GetDiskFreeSpaceExW` returns three figures, and the launcher now logs all three for every path it checks
(`storage:` lines in `boot.log`, and `LocalState\storage_report.txt`):

| Figure | Argument | What it is |
|---|---|---|
| available to this app | `lpFreeBytesAvailableToCaller` | What the app may still write. Under a per-user quota the docs say it "may be less than the total number of free bytes on a disk" ([GetDiskFreeSpaceExW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getdiskfreespaceexw)). This is the figure an install is checked against. |
| total | `lpTotalNumberOfBytes` | The size of the space the app is writing into: the volume, or the quota/pool when one applies. |
| total free | `lpTotalNumberOfFreeBytes` | The whole volume's free space, quota or not. |

A Dev Mode console keeps app data in a developer partition of its own, 5 GB by default and resized in
Dev Home → *Manage dev storage* (community reports: [yosonia](https://yosonia.com/en/510),
[freedom251](https://freedom251.com/xbox-developer-mode-storage-limits-and-how-to-manage-them/),
[reddit](https://www.reddit.com/r/XboxRetailHomebrew/comments/1466sr6/help_development_files_storage_capped_at_5_gigs/);
resizing wipes the dev storage). That is the pool
`LocalState` lives in, so the console's own storage settings show far more free space than an install through
`LocalState` can use. The console logs agree: the same install root read 4421 MB, 2789 MB, 4919 MB and 6078 MB at
different times, and 0–1 MB once the pool was full — a few-GB pool being filled and cleaned, not a full disk.
Installed games, unfinished downloads and the launcher's own caches all come out of that same pool.

The report says which case it is: `internal space: 4.3 GB for this app of 5.0 GB, 200.0 GB volume free` means a pool
smaller than the volume; `... of 5.0 GB` with no volume figure means the volume itself is the pool and is nearly full.

## What the launcher does about it

- **Every free-space query logs all three figures** with the path and the API, plus what WinRT reports
  (`System.FreeSpace` / `System.Capacity` for `LocalFolder`) as a cross-check: `storage: Internal path=... GetDiskFreeSpaceEx available=... total=... free_total=... WinRT System.FreeSpace=... System.Capacity=...`.
- **`LocalState\storage_report.txt`** holds the same, one line per install location plus the launcher's leftovers,
  written at start and whenever the detail page probes the drives.
- **The detail page shows the pool, not a bare figure**: *Internal (4.3 GB free of 5.0 GB)*, and on a refusal
  *No space on Internal: 0 MB of 5.0 GB.* with the way out on the next line (*X: install to USB E: ...* or
  *Delete an installed game to free space.*); the volume's own figure is in the log line and the report.
- **Launcher leftovers are removed at start**: `boot.prev.log`, `launch_probe.previous.log`, `frametimes.csv`,
  appinfo caches of games that are no longer installed, and store caches no page has touched for a week. They are
  KB-to-MB, not the GB an install needs, so they are cleaned quietly and reported
  (`storage: launcher leftovers: N files, X; removed Y`) rather than offered as a button. A game's own folder is
  never touched by this.
- **Unfinished downloads are already removed** at start (`installs::sweep_partials`), from the drive they were on.
- **The USB option is named in the refusal** when another location has more room, and every drive that is not
  listed says why in the log: `X: not usable, create folder error 19 (The media is write protected.), drive type fixed, label='...' fs=NTFS`
  or `X: skipped, drive type 1 (unknown)`. The drive type is logged because a fixed `D:` on a Dev Mode console is the
  developer scratch partition, not the USB stick; only a removable drive is named *USB X:*.

If the pool is the developer partition, no launcher change can grow it: the owner resizes it in Dev Home
(*Manage dev storage*) or installs to the USB drive. That is an Xbox setting, not something an app may change.

## Open questions for the console

1. Is the USB drive visible to the app as a drive letter, and can `CreateDirectory` on it succeed (look for
   `storage:` lines in `boot.log`)?
2. Does mapping a PE and running from it work from NTFS / exFAT (Play uses Win32 reads, so it should)?
3. If the drive is listed but writes fail, the ACL step above or `broadFileSystemAccess` is the next thing to try.
