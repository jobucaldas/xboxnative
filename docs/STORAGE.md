# Install locations (internal drive and USB)

The launcher installs games under its own `LocalState` on the console's internal drive by default. The detail page
of a game that is not installed also offers other drives: **X** cycles *Internal (N GB free)* and
*USB E: (N GB free)*; **A** installs there. The choice is stored in the game's install record, so download,
launch, delete and Steam Cloud (`GameInstall`) all use that game's own folder. Existing installs are unchanged.

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

## Open questions for the console

1. Is the USB drive visible to the app as a drive letter, and can `CreateDirectory` on it succeed (look for
   `storage:` lines in `boot.log`)?
2. Does mapping a PE and running from it work from NTFS / exFAT (Play uses Win32 reads, so it should)?
3. If the drive is listed but writes fail, the ACL step above or `broadFileSystemAccess` is the next thing to try.
