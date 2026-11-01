# Lane gkit2 notes (exported from the local LANE_NOTES.md)

Lines this lane added to the shared LANE_NOTES.md between 8231a2991 and 4653030c4.

# lane/gkit2 (kit follow-up to gkit), off-device only
- build-kit.sh now also builds+packages hid, mlp, tls, swc (build.sh modes smoke-all/run-*.sh use) plus extra swc-test.exe. cs32 (throwaway 32-bit spike, concluded negative, uninstalled) intentionally not shipped. Kit = 9 packages + 2 extras.
- Versions: README table is generated from manifest.json (identity/version read from each AppxManifest.xml) by new sync-readme-versions.sh, run at the end of build-kit.sh; `--check` fails on drift. Removed hard-coded versions from README prose and dp examples (use jq on manifest.json). INSTALL.md has no version numbers; package list/steps (4b, 6) updated.
- Evidence: kit built to /workspace/x64-run/kit-gkit2/20261026-141607 (build log /tmp/gkit2-build.log, 1m45s); verify-kit.sh PASS (11 entries), verify-kit-offline.sh PASS (9 packages), check-install-guide.sh PASS (46 checks), sync-readme-versions.sh --check PASS.
- Not done / caveats: console not used (nothing installed); launcher in the kit still comes from /workspace/wt/dl (0.4.6.0), not lane/gdet (0.5.0.4, not merged); on-screen descriptions for hid/mlp/tls/swc are "same host window as uwph" / green frame from lane notes, not freshly observed; mlp needs Megabonk files (not shipped).
