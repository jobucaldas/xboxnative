#!/usr/bin/env bash
# Restore drill for x64-run/backup.sh bundles. Local only: never pushes, never touches a remote.
# For the newest bundle of the main repo (and of every worktree that still exists), build a
# fresh repo in /tmp (remote-tracking base copied from the live repo, i.e. what a re-clone
# of origin gives), fetch the bundle's branches, and compare x64-spike + every lane/* head
# with the live repo. Missing branch or differing head => non-zero exit.
# usage: restore-drill.sh            (checks the backups as they are)
#        restore-drill.sh --backup   (runs x64-run/backup.sh first)
# env: BACKUP_DIR, LIVE_REPO, WT_DIR override the defaults (used by tests).
set -uo pipefail
S=${BACKUP_DIR:-/cursor/stores/store-a55f5844-0c99-40e9-a339-1ccf00723a82/x64-backup}
LIVE=${LIVE_REPO:-/workspace/Boxedwine-xbox-64}
WT=${WT_DIR:-/workspace/wt}
case "${1:-}" in
    --backup) bash /workspace/x64-run/backup.sh >/dev/null || { echo "RESTORE-DRILL FAIL: backup.sh failed"; exit 1; } ;;
    '') ;;
    *) echo "usage: $0 [--backup]" >&2; exit 2 ;;
esac
[[ -d $S ]] || { echo "RESTORE-DRILL FAIL: backup dir missing: $S"; exit 1; }
tmp=$(mktemp -d /tmp/restore-drill.XXXXXX)
trap 'rm -rf "$tmp"' EXIT
bad=0; checked=0
mapfile -t want < <(git -C "$LIVE" for-each-ref --format='%(refname:short) %(objectname)' refs/heads |
                    awk '$1=="x64-spike" || $1 ~ /^lane\//')
(( ${#want[@]} )) || { echo "RESTORE-DRILL FAIL: live repo has no x64-spike/lane/* branches"; exit 1; }

# newest bundle for name N: plain N-TS.bundle or N-TS.bundle.part.NN (32 MB splits)
newest() {
    ls -1 "$S" | sed -nE "s/^($1-[0-9]{8}-[0-9]{6}\.bundle)(\.part\.[0-9]+)?$/\1/p" | sort -u | tail -1
}
restore_bundle() {   # $1=bundle base name -> path to joined bundle file in $tmp
    local b=$1 out="$tmp/$1"
    if [[ -f $S/$b ]]; then cp "$S/$b" "$out"
    else cat "$S/$b".part.* > "$out"; fi
    printf %s "$out"
}
drill() {   # $1=repo label
    local label=$1 b f d line br sha got
    b=$(newest "$label")
    if [[ -z $b ]]; then echo "MISSING bundle: no $label-*.bundle in backup store"; bad=$((bad+1)); return; fi
    f=$(restore_bundle "$b")
    d="$tmp/clone-$label"; git init -q --bare "$d"
    git -C "$d" fetch -q "$LIVE" '+refs/remotes/*:refs/remotes/*' 2>/dev/null || true
    if ! git -C "$d" bundle verify "$f" >/dev/null 2>&1; then
        echo "BAD bundle: $b does not verify against a fresh repo"; bad=$((bad+1)); return
    fi
    if ! git -C "$d" fetch -q "$f" '+refs/heads/*:refs/heads/*' 2>"$tmp/err"; then
        echo "BAD bundle: $b fetch failed: $(head -1 "$tmp/err")"; bad=$((bad+1)); return
    fi
    for line in "${want[@]}"; do
        br=${line% *}; sha=${line#* }
        got=$(git -C "$d" rev-parse -q --verify "refs/heads/$br" 2>/dev/null || true)
        checked=$((checked+1))
        if [[ -z $got ]]; then echo "MISSING $br in $b (live ${sha:0:12})"; bad=$((bad+1))
        elif [[ $got != "$sha" ]]; then echo "STALE $br in $b: bundle ${got:0:12} != live ${sha:0:12}"; bad=$((bad+1))
        fi
    done
    echo "checked $b"
}
drill Boxedwine-xbox-64
if [[ -d $WT ]]; then
    for w in "$WT"/*/; do
        [[ -e ${w}.git ]] || continue
        drill "wt_$(basename "$w")"
    done
fi
# bundles of removed worktrees are informational only
for b in $(ls -1 "$S" | sed -nE 's/^(wt_[^-]+)-[0-9]{8}-[0-9]{6}\.bundle.*/\1/p' | sort -u); do
    [[ -e $WT/${b#wt_}/.git ]] || echo "note: orphan bundles for removed worktree ${b#wt_} (ignored)"
done
if (( bad )); then
    echo "RESTORE-DRILL FAIL: $bad problem(s) over $checked branch checks"
    exit 1
fi
echo "RESTORE-DRILL OK branches=${#want[@]}"
