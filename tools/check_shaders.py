#!/usr/bin/env python3
"""Compile-check every .slang pass with glslangValidator, and the generated GLSL ports.

The .slang files are preprocessed the way RetroArch does: expands #include, drops the
#pragma parameter/name/format lines, and splits the vertex and fragment stages.
The .glsl files (tools/make_glsl.py) are compiled the way Amiberry does: once with
"#define VERTEX" and once with "#define FRAGMENT" right after the #version line.

Usage: python3 tools/check_shaders.py
Needs glslangValidator (package glslang-tools / glslang).
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SHADERS = ROOT / "shaders" / "lola-retro" / "shaders"
GLSL = sorted((ROOT / "amiberry").glob("**/*.glsl"))


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


def compile_stage(name, stage, text, args):
    ext = "vert" if stage == "vertex" else "frag"
    with tempfile.NamedTemporaryFile("w", suffix="." + ext, delete=False) as f:
        f.write(text)
        tmp = f.name
    r = subprocess.run(["glslangValidator", *args, tmp], capture_output=True, text=True)
    print(f"{name:18} {stage:8} {'ok' if r.returncode == 0 else 'FAILED'}")
    if r.returncode != 0:
        print(r.stdout + r.stderr)
    return r.returncode == 0


def main():
    ok = True
    for slang in sorted(SHADERS.glob("*.slang")):
        for stage, src in split_stages(expand(slang)).items():
            ok &= compile_stage(slang.name, stage, "\n".join(src) + "\n", ["-V", "-o", "/dev/null"])
    for glsl in GLSL:
        src = glsl.read_text()
        cut = src.index("\n") + 1                      # after the #version line
        for stage, define in (("vertex", "VERTEX"), ("fragment", "FRAGMENT")):
            text = src[:cut] + f"#define {define}\n#define PARAMETER_UNIFORM\n" + src[cut:]
            ok &= compile_stage(glsl.name, stage, text, [])
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
