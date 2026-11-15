#!/usr/bin/env python3
"""Parse one play-test evidence directory into a safe sweep CSV row."""
import argparse
import csv
import json
import re
import sys
from pathlib import Path

FIELDS = ["appid", "name", "arch", "terminal_result", "exit_code",
          "first_failure", "gap_summary", "frames_reached", "elapsed"]


def scrub(value):
    value = re.sub(r"(?<!\d)7656119\d{10}(?!\d)", "<steamid>", value)
    value = re.sub(r"(?<!\d)(?:\d{1,3}\.){3}\d{1,3}(?!\d)", "<ip>", value)
    value = re.sub(r"(?i)/home/[^\s,;]+", "<path>", value)
    value = re.sub(r"(?i)[A-Z]:\\[^\r\n,;]*?(?=\s+(?:error=|at\b)|$)", "<path>", value)
    value = re.sub(r"(?i)\b(user(name)?|account|steamid|token)(\s*[=:]\s*)[^,;\s]+", r"\1\3<redacted>", value)
    value = re.sub(r"\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b", "<email>", value, flags=re.I)
    return value.strip()


def load_sources(path):
    if path.is_file():
        text = path.read_text(errors="replace")
        match = re.search(r"^request-json:\s*(\{.*\})\s*$", text, re.M)
        request = json.loads(match.group(1)) if match else {}
        return request, {}, text, text
    request_path = path / "launch_request.json"
    result_path = path / "launch_result.json"
    request = json.loads(request_path.read_text()) if request_path.exists() else {}
    result = json.loads(result_path.read_text()) if result_path.exists() else {}
    boot_path = path / "boot.session.log"
    if not boot_path.exists():
        boot_path = path / "boot.log"
    boot = boot_path.read_text(errors="replace") if boot_path.exists() else ""
    probe_path = path / "launch_probe.log"
    probe = probe_path.read_text(errors="replace") if probe_path.exists() else ""
    return request, result, boot, probe


def parse(appid, path, fallback_elapsed=""):
    request, result, boot, probe = load_sources(path)
    text = boot + "\n" + probe
    name = str(request.get("name", ""))
    exe = str(request.get("exe", ""))
    terminal = ""
    attempt = str(request.get("attemptId", ""))
    result_lines = re.findall(r"^\s*\[\s*([0-9.]+)\].*launch: RESULT attempt=([^\s]+).*", boot, re.M)
    if not result_lines:
        result_lines = re.findall(r"^\s*\[\s*([0-9.]+)\].*launch: RESULT.*", boot, re.M)
    result_line = ""
    result_time = ""
    if result_lines:
        last = result_lines[-1]
        if len(last) == 2:
            result_time, result_attempt = last
            result_line = next((line for line in boot.splitlines() if "launch: RESULT" in line and result_attempt in line), "")
            if attempt and result_attempt != attempt:
                result_line = ""
        else:
            result_time = last[0]
            result_line = next((line for line in boot.splitlines() if "launch: RESULT" in line), "")
    if not result_line:
        result_line = next((line for line in reversed(boot.splitlines()) if "launch: RESULT" in line), "")
    if result_line:
        terminal = "PASS" if re.search(r"\bok=1\b", result_line) else "FAIL"
    elif result.get("terminal") in (True, "True", "true"):
        terminal = "PASS" if result.get("ok") in (True, "True", "true") else "FAIL"

    arch = "x86" if re.search(r"PE32 guest|32-bit guest|x86 route", text, re.I) else ""
    if not arch and (re.search(r"PE32\+|64-bit guest|x64 guest", text, re.I) or
                     re.search(r"(?:win|x)64", exe, re.I) or result_line):
        arch = "x64"

    failure = ""
    for line in boot.splitlines():
        if re.search(r"(?:unhandled )?exception code=|unresolved import|LoadLibrary failed", line, re.I):
            failure = scrub(line.strip())
            break
    if not failure:
        candidate = str(result.get("lastEvent", result.get("result", "")))
        if re.search(r"exception|unresolved import|LoadLibrary failed", candidate, re.I):
            failure = scrub(candidate)

    gap = ""
    gap_match = re.search(r"^\s*(?:\[\s*[0-9.]+\]\s*)?(?:launch-probe:\s*)?(GAP: [^\r\n]+)", probe + "\n" + boot, re.M)
    if gap_match:
        gap = gap_match.group(1).strip()
    frame_lines = re.findall(r"frame=(\d+)[^\r\n]*?fps~([0-9.]+)", boot)
    frames = max((int(frame) for frame, _ in frame_lines), default="")
    fps = frame_lines[-1][1] if frame_lines else ""

    exit_code = result.get("exitCode", "")
    if exit_code in (None, "None"):
        exit_code = ""
    if not exit_code:
        code = re.search(r"ExitProcess code=(\d+)", text)
        if code:
            exit_code = code.group(1)

    elapsed = ""
    request_time = re.search(r"^\s*\[\s*([0-9.]+)\].*launch: request written", boot, re.M)
    if request_time and result_time:
        elapsed = f"{max(0.0, float(result_time) - float(request_time.group(1))):.3f}"
    elif fallback_elapsed:
        elapsed = str(fallback_elapsed)

    row = [appid, scrub(name), arch, terminal, str(exit_code), failure, gap,
           str(frames), elapsed]
    safe_excerpt = []
    if name:
        safe_excerpt.append("name=" + scrub(name))
    if exe:
        safe_excerpt.append("exe=" + scrub(Path(exe).name))
    if result_line:
        safe_excerpt.append(scrub(result_line.strip()))
    elif result.get("result"):
        safe_excerpt.append(scrub(str(result["result"])))
    if failure and failure not in safe_excerpt:
        safe_excerpt.append("first failure: " + failure)
    if gap:
        safe_excerpt.append(gap)
    if frame_lines:
        safe_excerpt.append(f"frames={frames} fps~{fps}")
    return row, "\n".join(safe_excerpt) + ("\n" if safe_excerpt else "")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("appid")
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--elapsed", default="")
    parser.add_argument("--redacted-out", type=Path)
    args = parser.parse_args()
    row, excerpt = parse(args.appid, args.evidence, args.elapsed)
    if args.redacted_out:
        args.redacted_out.write_text(excerpt)
    csv.writer(sys.stdout, lineterminator="\n").writerow(row)


if __name__ == "__main__":
    main()
