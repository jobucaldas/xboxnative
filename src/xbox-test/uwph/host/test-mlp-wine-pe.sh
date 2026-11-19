#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
grep -Fq 'if(!strcmp(kind,"wine-pe"))' "$here/mlp_probe.h"
grep -Fq 'LoadPackagedLibrary(wide.c_str(),0)' "$here/mlp_probe.h"
grep -Fq 'if(!address&&name&&key!="kernel32.dll"&&strcmp(kind,"wine-pe")) {' "$here/mlp_probe.h"
grep -Fq 'if [[ ${2:-uwph} == mlp ]]' "$here/build.sh"
grep -Fq 'cp "$donor_dir"/{avrt,dinput8,hid,psapi,setupapi,shcore,version}.dll' "$here/build.sh"
bash -n "$here/build.sh"
printf 'MLP WINE-PE ROUTE PASS: packaged donor branch and staging contract present\n'
