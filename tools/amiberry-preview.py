#!/usr/bin/env python3
"""Render a frame through an Amiberry .glslp preset offscreen, the way Amiberry runs it.

Follows Amiberry's shader_preset.cpp and external_shader.cpp:
- the frame is flipped on upload, so texture row 0 is the bottom of the picture (GL convention);
- one quad: VertexCoord -1..1, TexCoord 0..1 with (0, 0) bottom-left, MVPMatrix = identity;
- per pass: TextureSize and InputSize (input), OutputSize, FrameCount (int), OrigTextureSize,
  OrigInputSize, and the parameter uniforms of that pass's own #pragma parameter lines;
- unit 0 Source (filter of this pass), unit 1 Original (filter of pass 0, clamp_to_edge),
  aliases from unit 2 (filter the next pass set on them, wrap mode of their own pass);
- sizes truncated to whole pixels, float_framebuffer is RGBA16F, the last pass draws the viewport;
- each file is compiled with "#define VERTEX" or "#define FRAGMENT" after its #version line.

Needs: pip install moderngl numpy pillow (Linux uses EGL, so no display is needed).

Usage: tools/amiberry-preview.py PRESET.glslp FRAME.png OUT.png [--size 1440x1080] [--frames 3]
       [--set NAME=VALUE ...] [--times]
FRAME is what Amiberry would hand over: for a normal PAL screen 720x568 (hires, every line twice).
"""
import argparse
import re
import time
from pathlib import Path

import moderngl
import numpy as np
from PIL import Image

PARAM_RE = re.compile(r'#pragma\s+parameter\s+(\w+)\s+"([^"]+)"\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)')


def parse_preset(path):
    kv = {}
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        kv[k.strip()] = v.split("#")[0].strip().strip('"')
    return kv


def with_stage(src, define):
    cut = src.find("\n", src.find("#version")) + 1
    extra = f"#define {define}\n" + ("#define PARAMETER_UNIFORM\n" if PARAM_RE.search(src) else "")
    return src[:cut] + extra + src[cut:]


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
    ap.add_argument("--size", default="1440x1080", help="viewport (Amiberry's picture area)")
    ap.add_argument("--frames", type=int, default=3)
    ap.add_argument("--set", action="append", default=[], help="NAME=VALUE parameter override")
    ap.add_argument("--times", action="store_true", help="print the time of every pass")
    a = ap.parse_args()

    vw, vh = map(int, a.size.split("x"))
    preset = Path(a.preset).resolve()
    kv = parse_preset(preset)
    ctx = context()

    img = np.asarray(Image.open(a.frame).convert("RGBA"))
    oh, ow = img.shape[:2]
    orig = ctx.texture((ow, oh), 4, np.ascontiguousarray(img[::-1]).tobytes())

    passes = []
    for i in range(int(kv["shaders"])):
        path = (preset.parent / kv[f"shader{i}"]).resolve()
        src = path.read_text()
        p = dict(i=i, path=path, src=src, alias=kv.get(f"alias{i}", ""),
                 filter=kv.get(f"filter_linear{i}", "false") in ("true", "1"),
                 wrap=kv.get(f"wrap_mode{i}", "clamp_to_edge"),
                 float=kv.get(f"float_framebuffer{i}", "false") in ("true", "1"),
                 params={m.group(1): float(m.group(3)) for m in PARAM_RE.finditer(src)})
        for ax in "xy":
            p["st" + ax] = kv.get(f"scale_type_{ax}{i}", kv.get(f"scale_type{i}", "source"))
            p["sc" + ax] = float(kv.get(f"scale_{ax}{i}", kv.get(f"scale{i}", "1.0")))
        passes.append(p)
    overrides = [(n.strip(), float(kv[n.strip()])) for n in kv.get("parameters", "").split(";") if n.strip() in kv]
    overrides += [(k, float(v)) for k, v in (s.split("=") for s in a.set)]
    for name, value in overrides:
        users = [p for p in passes if name in p["params"]]
        if not users:
            raise SystemExit(f"no pass declares the parameter {name}")
        for p in users:
            p["params"][name] = value

    size = (ow, oh)
    for p in passes:
        p["input"] = size
        dims = []
        for ax, src_dim, vp_dim in (("x", size[0], vw), ("y", size[1], vh)):
            st, sc = p["st" + ax], p["sc" + ax]
            dims.append(int(vp_dim * sc) if st == "viewport" else int(sc) if st == "absolute" else int(src_dim * sc))
        size = (vw, vh) if p is passes[-1] else (max(1, dims[0]), max(1, dims[1]))
        p["size"] = size

    quad = ctx.buffer(np.array([-1, -1, 0, 1, 0, 0, 1, -1, 0, 1, 1, 0,
                                1, 1, 0, 1, 1, 1, -1, 1, 0, 1, 0, 1], dtype="f4").tobytes())
    for p in passes:
        try:
            p["prog"] = ctx.program(vertex_shader=with_stage(p["src"], "VERTEX"),
                                    fragment_shader=with_stage(p["src"], "FRAGMENT"))
        except Exception as e:
            raise SystemExit(f"pass {p['i']} ({p['path'].name}) does not build:\n{e}")
        if "TexCoord" in p["prog"]:
            p["vao"] = ctx.vertex_array(p["prog"], [(quad, "4f 2f", "VertexCoord", "TexCoord")])
        else:
            p["vao"] = ctx.vertex_array(p["prog"], [(quad, "4f 8x", "VertexCoord")])
        p["tex"] = ctx.texture(p["size"], 4, dtype="f2" if p["float"] else "f1")
        p["fbo"] = ctx.framebuffer(color_attachments=[p["tex"]])
        p["seen_filter"] = p["filter"]

    def sampler(tex, linear, wrap):
        f = moderngl.LINEAR if linear else moderngl.NEAREST
        s = ctx.sampler(texture=tex, filter=(f, f), repeat_x=wrap == "repeat", repeat_y=wrap == "repeat")
        if wrap == "clamp_to_border":
            s.border_color = (0.0, 0.0, 0.0, 0.0)
        return s

    def put(prog, name, value):
        if name in prog:
            prog[name].value = value

    times = np.zeros((a.frames, len(passes)))
    for frame in range(a.frames):
        for p in passes:
            prog, bound = p["prog"], []
            if p["i"] == 0:
                src_tex, src_wrap = orig, "clamp_to_edge"
            else:
                prev = passes[p["i"] - 1]
                prev["seen_filter"] = p["filter"]          # Amiberry sets it on the texture object
                src_tex, src_wrap = prev["tex"], prev["wrap"]
            units = [("Source", sampler(src_tex, p["filter"], src_wrap)),
                     ("Original", sampler(orig, passes[0]["filter"], "clamp_to_edge"))]
            put(prog, "Texture", 0)
            put(prog, "OrigTexture", 1)
            for q in passes[:p["i"]]:
                if q["alias"] and (q["alias"] in prog or q["alias"] + "Texture" in prog):
                    units.append((q["alias"], sampler(q["tex"], q["seen_filter"], q["wrap"])))
                    put(prog, q["alias"] + "Texture", len(units) - 1)
                    put(prog, q["alias"] + "TextureSize", tuple(map(float, q["size"])))
            for unit, (name, s) in enumerate(units):
                s.use(location=unit)
                bound.append(s)
                put(prog, name, unit)
            put(prog, "OrigTextureSize", (float(ow), float(oh)))
            put(prog, "OrigInputSize", (float(ow), float(oh)))
            put(prog, "TextureSize", tuple(map(float, p["input"])))
            put(prog, "InputSize", tuple(map(float, p["input"])))
            put(prog, "OutputSize", tuple(map(float, p["size"])))
            put(prog, "FinalViewportSize", (float(vw), float(vh)))
            put(prog, "FrameCount", frame)
            put(prog, "FrameDirection", 1)
            put(prog, "MVPMatrix", tuple(np.eye(4, dtype="f4").flatten()))
            for name, value in p["params"].items():
                put(prog, name, value)
            p["fbo"].use()
            ctx.viewport = (0, 0, *p["size"])
            p["fbo"].clear(0.0, 0.0, 0.0, 0.0)
            ctx.finish()
            t0 = time.perf_counter()
            p["vao"].render(moderngl.TRIANGLE_FAN)
            ctx.finish()
            times[frame, p["i"]] = time.perf_counter() - t0
            for s in bound:
                s.release()

    if a.times:
        med = np.median(times[1:], axis=0) if a.frames > 1 else times[0]
        for p, t in zip(passes, med):
            print(f"pass {p['i']} {p['path'].stem:8} {p['size'][0]:5}x{p['size'][1]:<5} {t * 1000:8.2f} ms")
        print(f"total {med.sum() * 1000:.2f} ms")
    last = passes[-1]
    data = np.frombuffer(last["tex"].read(), dtype="f2" if last["float"] else "u1").reshape(vh, vw, 4)[::-1]
    if last["float"]:
        data = (np.clip(data.astype("f4"), 0.0, 1.0) * 255.0 + 0.5).astype("u1")
    Image.fromarray(np.ascontiguousarray(data[:, :, :3])).save(a.out)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
