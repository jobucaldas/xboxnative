#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
command -v x86_64-w64-mingw32-gcc >/dev/null
command -v wine >/dev/null
work=$(mktemp -d /tmp/steam-genuine-wine.XXXXXX)
trap 'rm -rf "$work"' EXIT
if [[ -d /nix/store ]]; then
    mcfg=$(find /nix/store -path '*/lib/libmcfgthread.a' -print -quit)
    [[ -z "$mcfg" ]] || export LIBRARY_PATH="${mcfg%/*}:${LIBRARY_PATH:-}"
fi
cat >"$work/fake.c" <<'EOF'
#include <windows.h>
#include <stdint.h>
__declspec(dllexport) int Steam_CreateSteamPipe(void){return 17;}
__declspec(dllexport) int Steam_CreateGlobalUser(int* pipe){return pipe&&*pipe==17?23:0;}
__declspec(dllexport) BOOL Steam_BLoggedOn(int user,int pipe){(void)user;(void)pipe;return FALSE;}
__declspec(dllexport) void Steam_LogOn(int user,int pipe,uint64_t steamid){(void)user;(void)pipe;(void)steamid;SetEnvironmentVariableA("STEAM_LOGON_CALLED","1");}
__declspec(dllexport) void Steam_LogOff(int user,int pipe){(void)user;(void)pipe;}
__declspec(dllexport) BOOL Steam_BReleaseSteamPipe(int pipe){return pipe==17?TRUE:FALSE;}
__declspec(dllexport) void Steam_ReleaseUser(int pipe,int user){(void)pipe;(void)user;}
EOF
cat >"$work/probe.c" <<'EOF'
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
typedef int (*SteamCreatePipeFn)(void);typedef int (*SteamCreateUserFn)(int*);typedef BOOL (*SteamLoggedOnFn)(int,int);
typedef void (*SteamLogOnFn)(int,int,uint64_t);typedef void (*SteamLogOffFn)(int,int);typedef BOOL (*SteamReleasePipeFn)(int);typedef void (*SteamReleaseUserFn)(int,int);
int main(void){
 const char* fake_token="synthetic-refresh-token";(void)fake_token;
 HMODULE d=LoadLibraryW(L"steamclient64.dll");if(!d){puts("STEAM genuine: step=exports ok=0");return 1;}
 SteamCreatePipeFn cp=(SteamCreatePipeFn)GetProcAddress(d,"Steam_CreateSteamPipe");SteamCreateUserFn cu=(SteamCreateUserFn)GetProcAddress(d,"Steam_CreateGlobalUser");
 SteamLoggedOnFn bl=(SteamLoggedOnFn)GetProcAddress(d,"Steam_BLoggedOn");SteamLogOnFn lo=(SteamLogOnFn)GetProcAddress(d,"Steam_LogOn");
 SteamLogOffFn off=(SteamLogOffFn)GetProcAddress(d,"Steam_LogOff");SteamReleasePipeFn rp=(SteamReleasePipeFn)GetProcAddress(d,"Steam_BReleaseSteamPipe");SteamReleaseUserFn ru=(SteamReleaseUserFn)GetProcAddress(d,"Steam_ReleaseUser");
 if(!cp||!cu||!bl||!lo||!off||!rp||!ru){puts("STEAM genuine: step=exports ok=0");return 2;}
 puts("STEAM genuine: step=exports ok=1");int p=cp();printf("STEAM genuine: step=create-pipe ok=%d\n",p?1:0);
 int u=p?cu(&p):0;printf("STEAM genuine: step=create-global-user ok=%d\n",u?1:0);if(!u)return 3;
 ULONGLONG start=GetTickCount64();lo(u,p,1234567890ULL);puts("STEAM genuine: step=log-on ok=1");
 BOOL online=bl(u,p);printf("STEAM genuine: logged-on=%d after=%llu\n",online?1:0,(unsigned long long)(GetTickCount64()-start));
 off(u,p);ru(p,u);rp(p);FreeLibrary(d);return online?4:0;
}
EOF
mcfg_args=()
[[ -z "${mcfg:-}" ]] || mcfg_args=(-L"${mcfg%/*}")
x86_64-w64-mingw32-gcc -Wall -Wextra -Werror "${mcfg_args[@]}" -shared "$work/fake.c" -o "$work/steamclient64.dll"
x86_64-w64-mingw32-gcc -Wall -Wextra -Werror -Wno-cast-function-type "${mcfg_args[@]}" "$work/probe.c" -o "$work/probe.exe"
if [[ -n "${mcfg:-}" ]]; then
    mcfg_dll="${mcfg%/lib/libmcfgthread.a}/bin/libmcfgthread-2.dll"
    [[ ! -f "$mcfg_dll" ]] || cp "$mcfg_dll" "$work/"
fi
output=$(cd "$work" && WINEDEBUG=-all wine ./probe.exe)
printf '%s\n' "$output"
grep -F 'STEAM genuine: step=log-on ok=1' <<<"$output" >/dev/null
grep -E 'STEAM genuine: logged-on=0 after=[0-9]+' <<<"$output" >/dev/null
