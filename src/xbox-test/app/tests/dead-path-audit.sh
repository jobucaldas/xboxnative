#!/usr/bin/env bash
# Dead-path source audit (bugbot r4559305219, round-6 redesign): while the
# guest is declared dead, the launcher must NEVER touch the guest's device,
# context or swap chain — the guest may be blocked inside Present and the
# D3D11 immediate context is not free-threaded, so any guest-side call from
# the UI thread can wedge the launcher on the exact hang the watchdog exists
# to outlive. The dead page is presented from the launcher's OWN composition
# surface (m_deadChain/m_deadBB on m_dev/m_ctx).
#
# This is a grep-style structural audit of src/xbox-test/app/main.cpp: the
# dead branch of Run() (from the m_guestChain guard to the end of its block)
# must reference no guest-side object. It fails the build (CI step) if the
# dead path ever regains a guest-chain or guest-context call.
set -u

root="$(cd "$(dirname "$0")/../.." && pwd)"   # src/xbox-test
main="$root/app/main.cpp"
[ -f "$main" ] || { echo "dead-path audit: $main not found"; exit 1; }

checks=0; failures=0
fail() { failures=$((failures+1)); echo "AUDIT FAIL: $1"; }
ok() { checks=$((checks+1)); echo "audit ok: $1"; }

# Extract the dead branch of Run(): from the `if(m_guestChain) {` line that
# follows the guest-watchdog comment block, to its closing `}` at the same
# indent (the loop's dead-page branch).
dead_branch=$(awk '
    /^            if\(m_guestChain\) \{$/ { on=1; depth=0 }
    on {
        print
        n=gsub(/\{/,"{"); m=gsub(/\}/,"}"); depth+=n-m
        if (NR>1 && depth==0 && /\}/) { exit }
    }
' "$main")

[ -n "$dead_branch" ] || { echo "dead-path audit: dead branch not found in main.cpp (marker: if(m_guestChain))"; exit 1; }

# 1. No Present on the guest chain in the dead branch (the launcher presents
#    m_deadChain; a bare `->Present(` must name the launcher's chain only).
if echo "$dead_branch" | grep -q 'm_guestChain->Present\|m_guestChain\.Present'; then
    fail "dead branch presents through the guest chain (m_guestChain->Present)"
else ok "no guest-chain Present in the dead branch"; fi

# 2. No UpdateSubresource/Map/CopyResource on anything but the launcher's own
#    objects (m_deadBB/m_bb are launcher-owned; m_guestChainBB would be guest).
if echo "$dead_branch" | grep -qE 'UpdateSubresource|->Map\(|CopyResource|CopySubresourceRegion'; then
    # allow only calls naming the launcher's own back buffer
    if echo "$dead_branch" | grep -E 'UpdateSubresource|->Map\(|CopyResource|CopySubresourceRegion' \
         | grep -qvE 'm_deadBB|m_bb'; then
        fail "dead branch touches a non-launcher resource with UpdateSubresource/Map/Copy"
    else ok "resource writes in the dead branch target launcher-owned buffers only"; fi
else ok "no resource writes in the dead branch beyond the launcher's own"; fi

# 3. The dead branch must not call watchdog_present (removed with the
#    redesign) or the guest overlay's present helpers.
if echo "$dead_branch" | grep -q 'watchdog_present\|realPresent'; then
    fail "dead branch routes through the guest present helper (watchdog_present/realPresent)"
else ok "no guest present helper in the dead branch"; fi

# 4. watchdog_present must be gone entirely (the redesign removed its only
#    caller; keeping a dead helper invites re-use).
if grep -q 'watchdog_present' "$root/app/guest_overlay.h"; then
    fail "guest_overlay.h still defines watchdog_present (removed by the redesign)"
else ok "watchdog_present is gone from guest_overlay.h"; fi

# 5. dead_upload (the per-guest-format dead-page upload) must be gone: the
#    launcher's own surface is always B8G8R8A8 at design size, so no
#    format-dependent path exists.
if grep -q 'dead_upload' "$root/app/host/guest_watchdog.h" "$main"; then
    fail "dead_upload still exists (the redesign removed the guest-format dead path)"
else ok "dead_upload is gone (no guest-format dead path)"; fi

# 6. The dead branch must present the launcher's own surface.
if echo "$dead_branch" | grep -q 'm_deadChain->Present'; then
    ok "dead branch presents the launcher-owned m_deadChain"
else
    fail "dead branch does not present m_deadChain (the launcher's own surface)"
fi

# 7. input()'s dead gate must swallow everything (return before any launcher
#    screen logic while guestDead is set).
input_dead=$(awk '
    /guest_overlay::guestDead\.load\(std::memory_order_acquire\)\) return;/ { found=1 }
    found && !done { print; if (++n>=2) done=1 }
' "$main")
if [ -n "$input_dead" ]; then
    ok "input() dead gate swallows all edges (returns while guestDead)"
else
    fail "input() has no swallow-all dead gate (guestDead early return missing)"
fi

# 8. The launcher image must not statically import dcomp.dll (bugbot
#    r4560022516): a desktop-affinity import the Xbox/UWP loader rejects at
#    activation. DCompositionCreateDevice2 resolves at RUNTIME (GetProcAddress)
#    and build.sh links no dcomp.lib; the dead page degrades to the fail-safe
#    loop (force quit still reachable) when dcomp.dll is absent.
if grep -qE '(^|[[:space:]])--link-arg[[:space:]]+dcomp\.lib' "$root/app/build.sh"; then
    fail "build.sh links dcomp.lib (plants a dcomp.dll import in the launcher image)"
else ok "build.sh links no dcomp.lib"; fi
if grep -qE '(^|[^"A-Za-z])DCompositionCreateDevice2\(' "$main"; then
    fail "main.cpp calls DCompositionCreateDevice2 directly (static dcomp.dll import)"
else ok "DCompositionCreateDevice2 resolves at runtime (no static dcomp import)"; fi
if grep -q 'GetProcAddress(module,"DCompositionCreateDevice2")' "$main"; then
    ok "runtime lookup for DCompositionCreateDevice2 present"
else
    fail "no GetProcAddress lookup for DCompositionCreateDevice2 (dead page has no dcomp path)"
fi

# 9. Pad ownership (bugbot r4562634978): the guest's pad publish must gate on
#    the overlay-ownership predicate (dead implies ownership), never on the bare
#    guide flag, or a dead guest's pad keeps reaching the live game.
poll=$(awk '/void PollGuestControllers\(\) \{/ {on=1} on {print} on && /^    }$/ {exit}' "$main")
if echo "$poll" | grep -q 'pad_owned_by_overlay()'; then
    ok "PollGuestControllers gates the guest pad on pad_owned_by_overlay()"
else
    fail "PollGuestControllers does not use pad_owned_by_overlay() for the guest pad"
fi
if echo "$poll" | grep -q 'inputBlocked'; then
    fail "PollGuestControllers reads the bare inputBlocked flag (dead must imply ownership)"
else ok "PollGuestControllers reads no bare inputBlocked flag"; fi
if grep -q 'overlay_owns_pad' "$root/app/host/guest_watchdog.h"; then
    ok "overlay_owns_pad predicate is defined in guest_watchdog.h"
else
    fail "overlay_owns_pad predicate missing from guest_watchdog.h"
fi

# 10. Every guest-facing input hook uses that same ownership decision:
#     keyboard/mouse through guest_input(), XInput through
#     PollGuestControllers, and WGI Gamepad/RawGameController through blocking().
guest_input=$(awk '/bool guest_input\(\) \{/ {on=1} on {print} on && /^    }$/ {exit}' "$main")
if echo "$guest_input" | grep -q 'pad_owned_by_overlay()'; then
    ok "guest_input gates keyboard/mouse on pad_owned_by_overlay()"
else
    fail "guest_input does not use pad_owned_by_overlay()"
fi
if echo "$guest_input" | grep -qE 'inputBlocked|menuOpen|guestDead'; then
    fail "guest_input reads a bypass flag instead of the ownership predicate"
else ok "guest_input reads no bypass flag"; fi

overlay="$root/app/guest_overlay.h"
pad_ownership=$(awk '/inline bool pad_owned_by_overlay\(\)/ {on=1} on {print} on && /^}/ {exit}' "$overlay")
if echo "$pad_ownership" | grep -q 'guest_watchdog::overlay_owns_pad'; then
    ok "guest_overlay pad ownership delegates to the pure predicate"
else
    fail "guest_overlay pad ownership bypasses the pure predicate"
fi
blocking=$(awk '/inline bool blocking\(\)/ {print; exit}' "$overlay")
if echo "$blocking" | grep -q 'pad_owned_by_overlay()'; then
    ok "WGI/raw blocking() delegates to pad_owned_by_overlay()"
else
    fail "WGI/raw blocking() does not use pad_owned_by_overlay()"
fi
if echo "$blocking" | grep -qE 'inputBlocked|menuOpen|guestDead'; then
    fail "WGI/raw blocking() reads a bypass flag instead of the ownership predicate"
else ok "WGI/raw blocking() reads no bypass flag"; fi
for hook in gamepad_read raw_read; do
    body=$(awk -v marker="inline int32_t __stdcall $hook" 'index($0,marker) {on=1} on {print} on && /^}$/ {exit}' "$overlay")
    if echo "$body" | grep -q 'blocking()'; then
        ok "$hook gates its guest reading through blocking()"
    else
        fail "$hook does not gate its guest reading through blocking()"
    fi
    if echo "$body" | grep -qE 'inputBlocked|menuOpen|guestDead'; then
        fail "$hook reads an ownership flag directly"
    else ok "$hook reads no ownership flag directly"; fi
done

echo "dead-path audit: $checks checks, $failures failures"
[ "$failures" -eq 0 ]
