#!/usr/bin/env python3
"""Compile-check every .slang pass with glslangValidator.

It preprocesses the files the way RetroArch does: expands #include, drops the
#pragma parameter/name/format lines, and splits the vertex and fragment stages.

Usage: python3 tools/check_shaders.py
Needs glslangValidator (package glslang-tools / glslang).
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

SHADERS = Path(__file__).resolve().parent.parent / "shaders" / "lola-retro" / "shaders"


def expand(path, seen=None):
    seen = seen or set()
    out = []
    for line in path.read_text().splitlines():
        m = re.match(r'\s*#include\s+"(.+)"', line)
        if m:
            inc = (path.parent / m.group(1)).resolve()
            if inc in seen:
                raise SystemExit(f"include loop: {inc}")
            out += expand(inc, seen | {inc})
        else:
            out.append(line)
    return out


def split_stages(lines):
    common, stages, current = [], {"vertex": [], "fragment": []}, None
    for line in lines:
        m = re.match(r"\s*#pragma\s+stage\s+(\w+)", line)
        if m:
            current = m.group(1)
            continue
        if re.match(r"\s*#pragma\s+(parameter|name|format)\b", line):
            continue
        if line.strip().startswith("#version"):
            continue
        (stages[current] if current else common).append(line)
    return {k: ["#version 450"] + common + v for k, v in stages.items()}


def main():
    ok = True
    for slang in sorted(SHADERS.glob("*.slang")):
        stages = split_stages(expand(slang))
        for stage, src in stages.items():
            ext = "vert" if stage == "vertex" else "frag"
            with tempfile.NamedTemporaryFile("w", suffix="." + ext, delete=False) as f:
                f.write("\n".join(src) + "\n")
                tmp = f.name
            r = subprocess.run(["glslangValidator", "-V", "-o", "/dev/null", tmp], capture_output=True, text=True)
            status = "ok" if r.returncode == 0 else "FAILED"
            print(f"{slang.name:14} {stage:8} {status}")
            if r.returncode != 0:
                ok = False
                print(r.stdout + r.stderr)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
