#!/usr/bin/env python3
"""Render a frame through an 86Box .glslp preset offscreen, the way 86Box's OpenGL renderer runs it.

Follows 86Box 6.0 (src/qt/qt_openglrenderer.cpp, qt_glsl_parser.cpp):
- the frame goes into a "scene" texture rounded up to a power of two (RGB8, clamp_to_border,
  86Box's own filter setting), the right way up, picture in the lower left corner;
- every pass draws into a power-of-two texture as well (RGB8, or RGB32F for float_framebuffer),
  cleared to black first. Its picture fills the lower left corner, truncated to whole pixels,
  while the size handed on to the next pass stays as computed (InputSize can be fractional);
- the texture a pass draws gets filter_linear of the NEXT pass (the frame gets 86Box's own filter
  setting instead of filter_linear0) and that pass's own wrap_mode;
- TexCoord spans the previous pass's picture: 0..InputSize/TextureSize;
- uniforms: Texture (unit 0), InputSize, TextureSize, OutputSize (the window, for every pass),
  FrameCount (int), MVPMatrix (identity), every parameter of the preset by name; from the second
  pass on also OrigTexture, OrigInputSize, OrigTextureSize, and PassNTexture (output of pass N-1)
  with PassNInputSize and PassNTextureSize (the INPUT of pass N-1);
- the last pass draws straight into the window when its scale is source 1.0; during that pass its
  input texture gets 86Box's own filter setting;
- a parameter whose name or description is longer than 63 characters is skipped, and a uniform
  whose type does not fit 86Box's glUniform call stays unset (checked here);
- each file is compiled as "#version NNN" (the rest of that line is dropped), then
  "#extension GL_ARB_shading_language_420pack : enable", "#define VERTEX" or "#define FRAGMENT",
  "#define PARAMETER_UNIFORM", "#line 1" and the file without its #pragma parameter lines.

Needs: pip install moderngl numpy pillow (Linux uses EGL, so no display is needed).

Usage: tools/86box-preview.py PRESET.glslp FRAME.png OUT.png [--size 1440x1080] [--frames 3]
       [--filter linear|nearest] [--set NAME=VALUE ...] [--times]
FRAME is what 86Box shows: VGA mode 13h arrives as 640x400, text mode as 720x400.
--size is the picture area in the window ("Force 4:3 display ratio" makes it 4:3).
"""
import argparse
import re
import time
from pathlib import Path

import moderngl
import numpy as np
from PIL import Image

PARAM_RE = re.compile(r'#pragma\s+parameter\s+(\S+)\s+"([^"]*)"\s+(\S+)\s+(\S+)\s+(\S+)')


def parse_preset(path):
    """86Box's ini reader (utils/ini.c): the name ends at '=' or a space (a tab stays part of it),
    a last line without a newline is dropped, keys after a [section] are not found, and the value
    is cut at its first '#', loses its quotes and is trimmed. The first of two equal names wins."""
    kv = {}
    for line in Path(path).read_text(encoding="utf-8-sig").split("\n")[:-1]:
        line = line.rstrip("\r").lstrip(" \t")
        if not line or line[0] in "#;":
            continue
        if line[0] == "[":
            if "]" in line:
                break
            continue
        name = re.match(r"[^= ]*", line).group(0)
        value = line[len(name):].lstrip("= ")
        if not value:
            continue
        value = value.split("#")[0]
        value = value[1:] if value.startswith('"') else value
        value = value[:-1] if value.endswith('"') else value
        kv.setdefault(name, value.split('"')[0].strip())
    return kv


def ini_bool(kv, name, default):
    v = kv.get(name)
    if v is None:
        return default
    if v.lower() in ("true", "false"):
        return v.lower() == "true"
    m = re.match(r"\s*[-+]?\d+", v)
    return bool(int(m.group(0))) if m else False


def check_uniforms(prog, index, params):
    """86Box sets every uniform with a fixed call (glUniform1i, glUniform2fv, ...). A different type
    in the shader fails silently there, while moderngl would set it anyway, so check the types."""
    for name in prog:
        u = prog[name]
        if not isinstance(u, moderngl.Uniform):
            continue
        if name in params:
            want = (0x1406, "float (glUniform1f)")
        elif re.fullmatch(r"(Orig)?(Input|Texture|Output)Size|Pass(Prev)?\d+(Input|Texture)Size", name):
            want = (0x8B50, "vec2 (glUniform2fv)")
        elif re.fullmatch(r"(Orig|Pass(Prev)?\d+|Prev\d*)?Texture", name):
            want = (0x8B5E, "sampler2D (glUniform1i)")
        elif name in ("FrameCount", "FrameDirection"):
            want = (0x1404, "int (glUniform1i)")
        elif name == "MVPMatrix":
            want = (0x8B5C, "mat4 (glUniformMatrix4fv)")
        else:
            print(f"pass {index}: 86Box never sets the uniform {name}; it stays 0")
            continue
        if u.gl_type != want[0]:
            raise SystemExit(f"pass {index}: the uniform {name} must be {want[1]}")
        if index == 0 and name.startswith("Orig"):
            print(f"pass 0: 86Box does not set {name} in the first pass; it stays 0")
        m = re.match(r"Pass(\d+)", name)
        if m and int(m.group(1)) > index:
            print(f"pass {index}: 86Box only sets {name} for earlier passes; it stays 0")


def assemble(src, define):
    """The source 86Box hands to the GL driver."""
    for start in ("#pragma parameter", "#define texture"):
        i = src.find(start)
        while i >= 0:
            j = src.find("\n", i)
            j = len(src) if j < 0 else j
            src = src[:i] + " " * (j - i) + src[j:]
            i = src.find(start)
    version = re.search(r"^\s*(#version\s+\w+)", src, flags=re.M).group(1)
    src = re.sub(r"^\s*#version\s+\w+", "", src, flags=re.M)
    return (version + "\n" + "\n#extension GL_ARB_shading_language_420pack : enable\n"
            + f"#define {define}\n#define PARAMETER_UNIFORM\n" + "\n#line 1\n" + src)


def next_pow2(n):
    n = int(n)                    # 86Box passes the float size to an unsigned int
    return 1 << max(0, (n - 1).bit_length())


def context():
    try:
        return moderngl.create_standalone_context(require=330, backend="egl")
    except Exception:
        return moderngl.create_standalone_context(require=330)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("preset")
    ap.add_argument("frame")
    ap.add_argument("out")
    ap.add_argument("--size", default="1440x1080", help="picture area in the window")
    ap.add_argument("--frames", type=int, default=3)
    ap.add_argument("--filter", choices=("linear", "nearest"), default="linear",
                    help="86Box's View > Filter method")
    ap.add_argument("--set", action="append", default=[], help="NAME=VALUE parameter override")
    ap.add_argument("--times", action="store_true", help="print the time of every pass")
    a = ap.parse_args()

    vw, vh = map(int, a.size.split("x"))
    window = (float(vw), float(vh))
    preset = Path(a.preset).resolve()
    kv = parse_preset(preset)
    ctx = context()
    global_linear = a.filter == "linear"

    # the scene: the frame, the right way up, in the lower left corner of a power-of-two texture
    img = np.asarray(Image.open(a.frame).convert("RGB"))
    fh, fw = img.shape[:2]
    scene = dict(out=(float(fw), float(fh)), out_tex=(float(next_pow2(fw)), float(next_pow2(fh))),
                 linear=global_linear, wrap="clamp_to_border")
    scene["tex"] = ctx.texture((next_pow2(fw), next_pow2(fh)), 3, dtype="f1")
    scene["tex"].write(np.zeros((next_pow2(fh), next_pow2(fw), 3), dtype="u1").tobytes())
    scene["tex"].write(np.ascontiguousarray(img[::-1]).tobytes(), viewport=(0, 0, fw, fh), alignment=1)
    scene["coords"] = (0.0, 0.0, fw / next_pow2(fw), fh / next_pow2(fh))

    n = int(kv["shaders"])
    passes, params = [], {}
    for i in range(n):
        path = (preset.parent / kv[f"shader{i}"]).resolve()
        src = path.read_text()
        for line in src.splitlines():
            m = PARAM_RE.match(line.strip())
            if not m:
                continue
            if len(m.group(1)) > 63 or not m.group(2) or len(m.group(2)) > 63:
                print(f"86Box skips the parameter {m.group(1)} (name or description empty or over 63 characters)")
                continue
            params.setdefault(m.group(1), float(m.group(3)))
        p = dict(i=i, path=path, src=src,
                 linear=ini_bool(kv, f"filter_linear{i + 1}", False),     # the texture this pass draws
                 wrap=kv.get(f"wrap_mode{i}", ""),
                 float=ini_bool(kv, f"float_framebuffer{i}", False),
                 scale=[], last=i == n - 1)
        if p["wrap"] not in ("repeat", "mirrored_repeat", "clamp_to_edge"):
            p["wrap"] = "clamp_to_border"
        for ax in "xy":
            st = kv.get(f"scale_type{i}", kv.get(f"scale_type_{ax}{i}", "source"))
            sc = float(kv.get(f"scale{i}", kv.get(f"scale_{ax}{i}", "1.0")))
            p["scale"].append((st if st in ("viewport", "absolute") else "source", sc))
        p["window"] = p["last"] and all(st == "source" and sc == 1.0 for st, sc in p["scale"])
        passes.append(p)
    if not passes[-1]["window"]:
        raise SystemExit("the last pass has a scale, so 86Box adds a final copy pass; not emulated here")
    for name in [s for s in kv.get("parameters", "").split(";") if s]:
        if name in params:
            params[name] = float(kv.get(name, "0"))
    for s in a.set:
        name, value = s.split("=")
        if name not in params:
            raise SystemExit(f"the preset has no parameter {name}")
        params[name] = float(value)

    # sizes, exactly as 86Box computes them (floats; textures and viewports truncate)
    f32 = np.float32
    prev = scene
    for p in passes:
        p["in"], p["in_tex"], p["tc"] = prev["out"], prev["out_tex"], prev["coords"]
        out = []
        for (st, sc), src_dim, win in zip(p["scale"], p["in"], window):
            out.append(f32(win) * f32(sc) if st == "viewport" else f32(sc) if st == "absolute" else f32(src_dim) * f32(sc))
        p["out"] = tuple(float(v) for v in out)
        p["out_tex"] = tuple(float(next_pow2(v)) for v in p["out"])
        p["coords"] = (0.0, 0.0, float(f32(p["out"][0]) / f32(p["out_tex"][0])), float(f32(p["out"][1]) / f32(p["out_tex"][1])))
        prev = p

    for p in passes:
        try:
            p["prog"] = ctx.program(vertex_shader=assemble(p["src"], "VERTEX"),
                                    fragment_shader=assemble(p["src"], "FRAGMENT"))
        except Exception as e:
            raise SystemExit(f"pass {p['i']} ({p['path'].name}) does not build:\n{e}")
        check_uniforms(p["prog"], p["i"], params)
        _, _, mx, my = p["tc"]        # TexCoord spans the previous pass's picture
        quad = np.array([-1, -1, 0, 0, -1, 1, 0, my, 1, -1, mx, 0, 1, 1, mx, my], dtype="f4")
        p["vbo"] = ctx.buffer(quad.tobytes())
        attrs = [x for x in ("VertexCoord", "TexCoord") if x in p["prog"]]
        layout = " ".join("2f" if x in attrs else "8x" for x in ("VertexCoord", "TexCoord"))
        p["vao"] = ctx.vertex_array(p["prog"], [(p["vbo"], layout, *attrs)])
        if p["window"]:
            p["tex"] = ctx.texture((vw, vh), 3, dtype="f1")
            p["view"] = (0, 0, vw, vh)
        else:
            p["tex"] = ctx.texture(tuple(int(v) for v in p["out_tex"]), 3, dtype="f4" if p["float"] else "f1")
            p["view"] = (0, 0, int(p["out"][0]), int(p["out"][1]))
        p["fbo"] = ctx.framebuffer(color_attachments=[p["tex"]])

    def sampler(tex, linear, wrap):
        f = moderngl.LINEAR if linear else moderngl.NEAREST
        s = ctx.sampler(texture=tex, filter=(f, f), repeat_x=wrap == "repeat", repeat_y=wrap == "repeat")
        if wrap == "clamp_to_border":
            s.border_color = (0.0, 0.0, 0.0, 0.0)
        return s

    def put(prog, name, value):
        if name in prog:
            prog[name].value = value

    times = np.zeros((a.frames, n))
    for frame in range(a.frames):
        prev = scene
        for p in passes:
            prog, units = p["prog"], []
            linear = global_linear if p["window"] else prev["linear"]
            units.append(("Texture", sampler(prev["tex"], linear, prev["wrap"])))
            put(prog, "MVPMatrix", tuple(np.eye(4, dtype="f4").flatten()))
            put(prog, "FrameDirection", 1)
            put(prog, "FrameCount", frame)
            put(prog, "InputSize", p["in"])
            put(prog, "TextureSize", p["in_tex"])
            put(prog, "OutputSize", window)
            for name, value in params.items():
                put(prog, name, value)
            if p["i"] > 0:
                units.append(("OrigTexture", sampler(scene["tex"], scene["linear"], scene["wrap"])))
                put(prog, "OrigInputSize", scene["out"])
                put(prog, "OrigTextureSize", scene["out_tex"])
                for q in passes[:p["i"]]:
                    k = q["i"] + 1
                    units.append((f"Pass{k}Texture", sampler(q["tex"], linear if q is prev else q["linear"], q["wrap"])))
                    put(prog, f"Pass{k}TextureSize", q["in_tex"])
                    put(prog, f"Pass{k}InputSize", q["in"])
            bound = []
            for unit, (name, s) in enumerate(units):
                if name in prog:
                    s.use(location=unit)
                    prog[name].value = unit
                bound.append(s)
            p["fbo"].use()
            p["fbo"].clear(0.0, 0.0, 0.0, 1.0)
            ctx.viewport = p["view"]
            ctx.finish()
            t0 = time.perf_counter()
            p["vao"].render(moderngl.TRIANGLE_STRIP)
            ctx.finish()
            times[frame, p["i"]] = time.perf_counter() - t0
            for s in bound:
                s.release()
            prev = p

    if a.times:
        med = np.median(times[1:], axis=0) if a.frames > 1 else times[0]
        for p, t in zip(passes, med):
            print(f"pass {p['i']} {p['path'].stem:8} {p['view'][2]:5}x{p['view'][3]:<5} {t * 1000:8.2f} ms")
        print(f"total {med.sum() * 1000:.2f} ms")
    data = np.frombuffer(passes[-1]["tex"].read(alignment=1), dtype="u1").reshape(vh, vw, 3)[::-1]
    Image.fromarray(np.ascontiguousarray(data)).save(a.out)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
