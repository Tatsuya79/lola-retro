#!/usr/bin/env python3
"""Convert the Lola Retro slang passes to GLSL for Amiberry.

Amiberry loads RetroArch-style .glslp presets and runs them on OpenGL 3.3 core (macOS, Linux
and Windows desktops). This tool turns the slang passes into .glsl files it can load:

- the includes are inlined, and each pass becomes one file with a VERTEX and a FRAGMENT part;
- the Vulkan uniform blocks become the plain uniforms Amiberry sets (TextureSize, OutputSize,
  OrigTextureSize, FrameCount, MVPMatrix);
- the preset's values are built in: the ones worth adjusting stay parameters, the rest become
  constants, so the compiler drops the code they switch off (composite, RF, the TV beam model);
- every parameter is declared once, in the first pass, which writes the values into a spare
  channel of its output (blue, first row) for the later passes. Amiberry lists the parameters
  of every pass, so this keeps each one in its list once.

Amiberry differs from RetroArch in a few ways the shaders are built for (see crt.inc):
LR_FIELDS (it draws every Amiga line twice), LR_NO_FEEDBACK (no feedback textures) and
LR_FIT_43 (its picture area is not always 4:3).

Only the Amiga 1084S preset is built for now.

Usage: python3 tools/make_glsl.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SLANG = ROOT / "shaders" / "lola-retro" / "shaders"
sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_presets import BASE, PRESETS, fmt  # noqa: E402

# The parameters left adjustable in Amiberry, in the order its list shows them.
# Everything else is fixed at the preset's value.
AMIGA_PARAMS = [
    "LR_NORM", "LR_DBLEND", "LR_SAT", "LR_BRIGHT", "LR_CONTRAST", "LR_STEADY",
    "LR_GAMMA", "LR_CONV",
    "LR_MASK_TYPE", "LR_MASK_STR", "LR_WHITE", "LR_PHOS",
    "LR_CURV_X", "LR_CURV_Y", "LR_CORNER", "LR_GLOW", "LR_GLOW_SIZE", "LR_VIGNETTE", "LR_OVERSCAN", "LR_AMBIENT",
    "LR_MON_SIZE", "LR_MON_PITCH", "LR_MON_SPOT_LO", "LR_MON_SPOT_HI", "LR_MON_CORNER",
]

# Amiberry's pass chain. Amiberry gives a pass's output texture that pass's own wrap mode,
# RetroArch samples it with the next pass's, so wrap modes are the RetroArch preset's moved
# one pass up. Filters work the same in both (the next pass sets it when reading its Source),
# so the pass after the beam must be linear for the CRT pass to read LR_Beam smoothly.
AMIBERRY_PASSES = [
    dict(src="encode", alias="LR_Signal", filter=False, wrap="clamp_to_border",
         sx=("source", 4.0), sy=("source", 1.0), float=True),
    dict(src="decode", alias="LR_Decoded", filter=True, wrap="clamp_to_edge",
         sx=("source", 1.0), sy=("source", 1.0), float=True),
    dict(src="glow-h", filter=True, wrap="clamp_to_edge",
         sx=("source", 0.125), sy=("source", 0.5), float=True),
    dict(src="glow-v", alias="LR_Glow", filter=True, wrap="clamp_to_edge",
         sx=("source", 1.0), sy=("source", 1.0), float=True),
    dict(src="beam", alias="LR_Beam", filter=True, wrap="clamp_to_edge",
         sx=("absolute", 8192), sy=("source", 2.05), float=True),
    dict(src="fields", filter=True, wrap="clamp_to_edge",
         sx=("absolute", 1), sy=("source", 0.5), float=True),
    dict(src="fields", alias="LR_Fields", filter=False, wrap="clamp_to_edge",
         sx=("absolute", 1), sy=("absolute", 1), float=True),
    dict(src="crt", filter=True, wrap="clamp_to_edge",
         sx=("viewport", 1.0), sy=("viewport", 1.0), float=True),
    dict(src="present", filter=False, wrap="clamp_to_edge",
         sx=("viewport", 1.0), sy=("viewport", 1.0), float=False),
]

TARGETS = {
    "amiberry": dict(
        preset="amiga-1084s",
        out=ROOT / "amiberry" / "lola-retro",
        title="Commodore 1084S",
        defines=["LR_FIELDS", "LR_NO_FEEDBACK", "LR_FIT_43"],
        params=AMIGA_PARAMS,
        fixed=dict(LR_CONSOLE=6),     # Amiberry only shows Amiga modes
        passes=AMIBERRY_PASSES,
    ),
}

TYPES = r"(?:void|bool|int|uint|float|vec2|vec3|vec4|ivec2|mat3|mat4|Timing)"


def expand(path, seen=None):
    """The file with its #include lines replaced by the included files."""
    seen = seen or {path.resolve()}
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


def parameter_table():
    """name -> (description, default, min, max, step) from the #pragma parameter lines."""
    table = {}
    for line in (SLANG / "common.inc").read_text().splitlines():
        m = re.match(r'\s*#pragma\s+parameter\s+(\w+)\s+"([^"]*)"\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)', line)
        if m:
            table[m.group(1)] = (m.group(2), *map(float, m.group(3, 4, 5, 6)))
    return table


def functions(code):
    """name -> body of every function defined in code (brace matched)."""
    found = {}
    for m in re.finditer(r"\b" + TYPES + r"\s+(\w+)\s*\(([^;{}()]|\([^()]*\))*\)\s*\{", code):
        depth, i = 1, m.end()
        while depth:
            depth += {"{": 1, "}": -1}.get(code[i], 0)
            i += 1
        found.setdefault(m.group(1), "")
        found[m.group(1)] += code[m.start():i]
    return found


def used_parameters(common, stages, names):
    """The parameters the code reachable from main() reads, in the order of names."""
    used = set()
    for stage in stages:
        funcs = functions(common + "\n" + stage)
        todo, seen = ["lr_main"], set()
        while todo:
            f = todo.pop()
            if f in seen or f not in funcs:
                continue
            seen.add(f)
            body = funcs[f]
            used |= set(re.findall(r"\b(LR_\w+)\b", body))
            todo += re.findall(r"\b(\w+)\s*\(", body)
        # constants and globals outside functions
        outside = common + "\n" + stage
        for body in funcs.values():
            outside = outside.replace(body, "")
        used |= set(re.findall(r"\b(LR_\w+)\b", outside))
    return [n for n in names if n in used]


def convert(src_name, target, values, table, owner):
    """GLSL source of one pass. The owner (the first pass) declares the adjustable parameters."""
    relay_from = target["passes"][0]["alias"]
    path = SLANG / f"{src_name}.slang"
    lines = [l for l in expand(path)
             if not l.strip().startswith("#version")
             and not re.match(r"\s*#pragma\s+(parameter|name|format)\b", l)]
    text = "\n".join(lines)

    # uniform blocks: the push constants and the UBO become plain uniforms
    text, n1 = re.subn(r"layout\(push_constant\)\s*uniform\s+Push\s*\{.*?\}\s*params\s*;\n?", "", text, flags=re.S)
    text, n2 = re.subn(r"layout\([^)]*\)\s*uniform\s+UBO\s*\{.*?\}\s*global\s*;\n?", "", text, flags=re.S)
    assert n1 == 1 and n2 == 1, f"{src_name}: uniform blocks not found"
    text = text.replace("global.MVP", "MVPMatrix")
    text = re.sub(r"\bglobal\.(LR_\w+)", r"\1", text)
    text = re.sub(r"\bparams\.(SourceSize|OriginalSize|OutputSize|FrameCount)\b", r"LR_\1", text)
    assert "global." not in text and "params." not in text, src_name
    # GLSL 3.30 has no layout locations on varyings and no descriptor sets
    text = re.sub(r"layout\(\s*location\s*=\s*\d+\s*\)\s*", "", text)
    text = re.sub(r"layout\(\s*set\s*=\s*\d+\s*,\s*binding\s*=\s*\d+\s*\)\s*", "", text)
    assert "layout(" not in text, src_name

    parts = re.split(r"^\s*#pragma\s+stage\s+(\w+)\s*$", text, flags=re.M)
    common, stages = parts[0], dict(zip(parts[1::2], parts[2::2]))
    vs, fs = stages["vertex"], stages["fragment"]
    vs = re.sub(r"\bPosition\b", "VertexCoord", vs)
    # each stage's main() becomes lr_main(); the generated main() around it handles the parameters
    vs, n_vs = re.subn(r"\bvoid\s+main\s*\(\s*\)", "void lr_main()", vs)
    fs, n_fs = re.subn(r"\bvoid\s+main\s*\(\s*\)", "void lr_main()", fs)
    assert n_vs == 1 and n_fs == 1, src_name

    adjustable = target["params"]
    named = set(re.findall(r"\b(LR_[A-Z_]+)\b", text))
    fixed = sorted(n for n in named if n in table and n not in adjustable)
    # every adjustable value the code names is declared; the ones it can reach are loaded
    declared = [n for n in adjustable if n in named]
    relay = [] if owner else used_parameters(common, [vs, fs], adjustable)

    out = ["#version 330 core",
           f"// Lola Retro - {src_name} pass for Amiberry ({target['title']}).",
           f"// Generated by tools/make_glsl.py from shaders/lola-retro/shaders/{src_name}.slang. Do not edit.",
           ""]
    out += [f"#define {d}" for d in target["defines"]]
    out.append("")
    if owner:
        out.append("// Adjustable values. They are set here only; this pass hands them to the others.")
        for n in adjustable:
            desc, _, lo, hi, step = table[n]
            out.append(f'#pragma parameter {n} "{desc}" {fmt(values[n])} {fmt(lo)} {fmt(hi)} {fmt(step)}')
        out.append("")
    out += ["uniform vec2 TextureSize;       // Source: the previous pass (the frame for the first pass)",
            "uniform vec2 OrigTextureSize;   // Original: the emulator's frame",
            "uniform vec2 OutputSize;        // this pass",
            "uniform int FrameCount;",
            "#define LR_SourceSize vec4(TextureSize, 1.0 / TextureSize)",
            "#define LR_OriginalSize vec4(OrigTextureSize, 1.0 / OrigTextureSize)",
            "#define LR_OutputSize vec4(OutputSize, 1.0 / OutputSize)",
            "#define LR_FrameCount uint(FrameCount)",
            ""]
    if fixed:
        out.append("// fixed by the preset")
        out += [f"const float {n} = {float(values[n])!r};" for n in fixed]
        out.append("")
    if owner:
        out += [f"uniform float {n};" for n in adjustable]
        out.append("")
    elif declared:
        out.append("// adjustable values, read from the first pass's output in main()")
        if relay:
            out.append(f"uniform sampler2D {relay_from};")
        out += [f"float {n};" for n in declared]
        out.append("")
    out.append(common.strip("\n"))
    out += ["", "#if defined(VERTEX)", "uniform mat4 MVPMatrix;"]
    out.append(vs.strip("\n"))
    out.append("")
    nv = (len(relay) + 3) // 4
    if relay:
        out.append(f"flat out vec4 lrParams[{nv}];")
        out.append("")
        out.append("void main()")
        out.append("{")
        out.append("    // the adjustable values: blue channel of the first pass's first row")
        for n in relay:
            out.append(f"    {n} = texelFetch({relay_from}, ivec2({adjustable.index(n)}, 0), 0).b;")
        for i in range(nv):
            group = relay[4 * i: 4 * i + 4] + ["0.0"] * (4 - len(relay[4 * i: 4 * i + 4]))
            out.append(f"    lrParams[{i}] = vec4({', '.join(group)});")
        out.append("    lr_main();")
        out.append("}")
    else:
        out += ["void main()", "{", "    lr_main();", "}"]
    out += ["", "#elif defined(FRAGMENT)"]
    out.append(fs.strip("\n"))
    out.append("")
    if owner:
        k = len(adjustable)
        out += ["// The adjustable values go to the later passes: texel k of the first row, blue channel.",
                "float lr_param(int k)",
                "{",
                f"    float p[{k}] = float[{k}]({', '.join(adjustable)});",
                "    return p[k];",
                "}",
                "",
                "void main()",
                "{",
                "    lr_main();",
                "    ivec2 q = ivec2(gl_FragCoord.xy);",
                f"    if (q.y == 0 && q.x < {k}) FragColor.b = lr_param(q.x);",
                "}"]
    elif relay:
        out.append(f"flat in vec4 lrParams[{nv}];")
        out.append("")
        out.append("void main()")
        out.append("{")
        for i, n in enumerate(relay):
            out.append(f"    {n} = lrParams[{i // 4}].{'xyzw'[i % 4]};")
        out.append("    lr_main();")
        out.append("}")
    else:
        out += ["void main()", "{", "    lr_main();", "}"]
    out += ["#endif", ""]
    glsl = "\n".join(out)
    return re.sub(r"\n{3,}", "\n\n", glsl)


def preset_text(target):
    lines = [f"# Lola Retro - {target['title']} for Amiberry",
             "# Generated by tools/make_glsl.py from the RetroArch slang shaders. Do not edit.",
             "",
             f"shaders = {len(target['passes'])}",
             ""]
    for i, p in enumerate(target["passes"]):
        lines.append(f"shader{i} = shaders/{p['src']}.glsl")
        if p.get("alias"):
            lines.append(f"alias{i} = {p['alias']}")
        lines.append(f"filter_linear{i} = {'true' if p['filter'] else 'false'}")
        lines.append(f"wrap_mode{i} = {p['wrap']}")
        for ax in "xy":
            st, sc = p["s" + ax]
            lines.append(f"scale_type_{ax}{i} = {st}")
            lines.append(f"scale_{ax}{i} = {sc}")
        if p["float"]:
            lines.append(f"float_framebuffer{i} = true")
        lines.append("")
    return "\n".join(lines)


def main():
    table = parameter_table()
    for name, target in TARGETS.items():
        values = {n: d[1] for n, d in table.items()}
        values.update(BASE)
        values.update(PRESETS[target["preset"]])
        values.update(target["fixed"])
        out = target["out"]
        (out / "shaders").mkdir(parents=True, exist_ok=True)
        written = set()
        for i, p in enumerate(target["passes"]):
            if p["src"] in written:
                continue
            glsl = convert(p["src"], target, values, table, owner=(i == 0))
            dest = out / "shaders" / f"{p['src']}.glsl"
            dest.write_text(glsl)
            written.add(p["src"])
            print("wrote", dest.relative_to(ROOT))
        dest = out / f"{target['preset']}.glslp"
        dest.write_text(preset_text(target))
        print("wrote", dest.relative_to(ROOT))
        (out / "LICENSE").write_text((ROOT / "LICENSE").read_text())


if __name__ == "__main__":
    main()
