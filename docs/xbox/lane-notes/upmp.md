# Lane upmp notes (exported from the local LANE_NOTES.md)

Lines this lane added to the shared LANE_NOTES.md between 86960ae8a and a69e63839.



### upmp — source audit and blocked acceptance (2026-10-26)

Branch lane/upmp starts at 86960ae8; initial worktree was clean. Read REBUILD.md
first, inherited lane notes, report, and Wine source/spec interfaces. No product
code or test gates changed. No console access or fresh smoke/Play run claimed.

Wine audit: user32/user32.spec declares GetMessage/PeekMessage, PostMessage,
SendMessage, DispatchMessage and SetTimer with distinct ABI contracts.
user32/message.c:692,711,732,790 forwards posting, peeking, getting and translation
to NtUser; SendMessageW:580 uses NtUserMessageCall, including callback machinery.
DispatchMessageW:866 handles timer callbacks and routes WM_PAINT through NtUser.
win32u/message.c:3461 checks driver events and message filters; :3510 blocks on
its server queue, returns -1 for errors and 0 for WM_QUIT. A dispatcher iteration
counter alone cannot demonstrate these semantics or execution by UnityPlayer.
The corrected external import oracle has 628 rows; its classifications remain
static reachability findings, not executable routes. Historical CSVs are retained.

Report correction: item 43's dependency list (37 and 42) is insufficient for its
Unity acceptance gate. Actual launcher lp_run_impl in xbox-test/app/launch.h:63
calls wxl::run with a non-null Image pointer (:77). The mapper mapped branch in
xbox-test/app/host/wxl_mapper.h:268 returns before TLS initialization, unwind
registration, DllMain and entry execution. launch.h:85 logs no guest execution;
its launch_result.json explicitly writes gameCodeExecuted=false (:112).
Thus UnityPlayer cannot reach any GetMessage/PeekMessage through this Play path.
The mapper does support execution for small standalone test PEs; that does not
supply a full-game loader, dependency initialization or guest process environment.

swc_dxgi.h provides the documented CreateSwapChainForCoreWindow bridge and a
CoreWindow dispatcher callback for its own test. It does not execute Unity from
Play. Reference xenia-canary-uwp/App.cpp also pumps the CoreWindow dispatcher;
UnleashedRecomp uses CreateSwapChainForCoreWindow in plume_d3d12.cpp. Existing
u32c core.cpp provides window records, not a dispatcher-backed queue. Neither a
300-iteration standalone test nor the launcher's non-black UI would satisfy the
requested Unity acceptance. Adding those would not remove the missing prerequisite.

Blocked before implementation: a real Play runtime must initialize and enter
UnityPlayer and retain mapped modules, with resolved runtime dependencies and
TLS/unwind/process state. Do not switch the probe into executing unresolved game
imports. Once that prerequisite exists, implement the queue and CoreWindow event
bridge, then run unchanged Play, screenshot and smoke acceptance on the console.
Per user stop rule, recorded /workspace/x64-run/BLOCKED-upmp.md. LANE_DONE absent.

### upmp — resumed prerequisite recheck (2026-10-26)

Re-read REBUILD.md, inherited notes, committed report correction, and the existing
BLOCKED-upmp.md. Worktree was clean at 8b3f8f6a. Rechecked actual launch.h and
wxl_mapper.h: Play still supplies the mapped output, returns before initialization
and guest entry, and writes gameCodeExecuted=false. Rechecked Wine message.c and
user32.spec queue entry points; previous audit remains accurate. Coverage CSVs
live in /workspace/x64-run/research/wine-coverage, not this worktree.
No product changes, console actions, or fresh acceptance passes are claimed.
The missing real Play runtime still prevents the specified Unity acceptance.
Per the explicit blocked-step stop rule, retain BLOCKED-upmp.md and stop;
LANE_DONE remains absent.

### upmp — latest resume verification (2026-10-26)

Resumed at f04bc4f3 with a clean worktree. Re-read REBUILD.md and prior lane
findings. Rechecked launch.h:77 and wxl_mapper.h:268: the real Play path still
returns mapped images without guest initialization or entry execution and writes
gameCodeExecuted=false. Rechecked Wine user32.spec, user32/message.c queue
wrappers, and win32u/message.c NtUserGetMessage blocking/WM_QUIT behavior;
the existing report correction remains accurate. No implementation, console
access, or fresh acceptance pass is claimed. The missing real Play runtime
still makes the requested Unity acceptance impossible on this branch. Retain
the blocker and stop under the explicit lane rule; do not create LANE_DONE.

### upmp — current resume verification (2026-10-26)

Resumed at 9e4350b3 with a clean worktree. Re-read REBUILD.md and lane notes;
rechecked the Wine user32.spec queue ABI declarations and existing report correction.
The Play path still passes a non-null mapped Image to wxl::run, whose mapped branch
returns before guest initialization and entry execution. launch_result.json still
records gameCodeExecuted=false. The specified Unity pump acceptance remains blocked
by the missing real Play runtime. No product edits, console actions, or fresh test
passes are claimed. Updated the external blocker; stop under the explicit lane rule.
LANE_DONE remains absent.

### upmp — resume at bffa3013 (2026-10-26)

Clean worktree; verified the existing blocker against current launch.h and
wxl_mapper.h. Play still returns from the mapped-image branch before guest
initialization and entry execution; launch_result.json records
gameCodeExecuted=false. Wine user32.spec still confirms the audited queue ABI.
The report prerequisite correction remains applicable. No console actions,
product changes, or acceptance passes claimed. Stop under the explicit blocked
acceptance rule; LANE_DONE remains absent.
