#!/usr/bin/env python3
"""Generate the Lola Retro .slangp presets.

All presets share the same pass chain and differ only in parameter values,
so they are generated from one table to keep them consistent.
Every preset is written twice: curved/ keeps each tube's curvature,
flat/ is the same preset with a flat screen and square corners.
hdr/curved/ and hdr/flat/ are the same presets for HDR displays: their last two
passes write HDR10 (crt-hdr.slang, present-hdr.slang).

Usage: python3 tools/make_presets.py
"""
from pathlib import Path

PACK = Path(__file__).resolve().parent.parent / "shaders" / "lola-retro"

PASSES = """shaders = 7

shader0 = ../shaders/encode.slang
alias0 = LR_Signal
filter_linear0 = false
wrap_mode0 = clamp_to_border
scale_type_x0 = source
scale_x0 = 4.0
scale_type_y0 = source
scale_y0 = 1.0
float_framebuffer0 = true

shader1 = ../shaders/decode.slang
alias1 = LR_Decoded
filter_linear1 = true
wrap_mode1 = clamp_to_border
scale_type1 = source
scale1 = 1.0
float_framebuffer1 = true

shader2 = ../shaders/glow-h.slang
filter_linear2 = true
wrap_mode2 = clamp_to_edge
scale_type_x2 = source
scale_x2 = 0.125
scale_type_y2 = source
scale_y2 = 0.5
float_framebuffer2 = true

shader3 = ../shaders/glow-v.slang
alias3 = LR_Glow
filter_linear3 = true
wrap_mode3 = clamp_to_edge
scale_type3 = source
scale3 = 1.0
float_framebuffer3 = true

shader4 = ../shaders/beam.slang
alias4 = LR_Beam
filter_linear4 = true
wrap_mode4 = clamp_to_edge
scale_type_x4 = absolute
scale_x4 = 8192
scale_type_y4 = source
scale_y4 = 2.05
float_framebuffer4 = true

shader5 = ../shaders/crt.slang
alias5 = LR_CRT
filter_linear5 = true
wrap_mode5 = clamp_to_edge
scale_type5 = viewport
scale5 = 1.0
float_framebuffer5 = true

shader6 = ../shaders/present.slang
filter_linear6 = false
wrap_mode6 = clamp_to_edge
scale_type6 = viewport
scale6 = 1.0
"""

# The beam pass (shader4) is 8192 samples wide for any source, and at least one row per
# source line: 2.05 x the half-height glow output covers odd line counts too.

# Values for every parameter a preset sets. Anything not listed keeps the default from common.inc.
BASE = dict(
    LR_CONSOLE=0, LR_NORM=0, LR_CABLE=1, LR_COMB=0, LR_YTRAP=0.75, LR_SHARP=0.35, LR_SAT=1.25, LR_TINT=0,
    LR_BRIGHT=0, LR_CONTRAST=1.05, LR_NOISE=0, LR_STEADY=1,
    LR_SIG_LO=0.2, LR_SIG_HI=0.44, LR_HFOCUS=0.5, LR_GAMMA=2.4, LR_PERSIST=0.08, LR_CONV=0.4,
    LR_MASK_TYPE=2, LR_MASK_STR=0.5, LR_MASK_TRIADS=360, LR_WHITE=8500,
    LR_CURV_X=0.045, LR_CURV_Y=0.06, LR_CORNER=0.045, LR_GLOW=0.06, LR_GLOW_SIZE=0.5, LR_VIGNETTE=0.3, LR_OVERSCAN=0.05, LR_AMBIENT=0.002,
)

PRESETS = {
    # Ordinary living-room TV on the composite cable that came in the box
    "consumer-tv-composite": dict(),
    # The same TV fed RGB through SCART (common in Europe)
    "consumer-tv-scart-rgb": dict(LR_CABLE=3),
    # Sony Trinitron on S-Video: aperture grille, flat vertically, cool white
    "trinitron-s-video": dict(
        LR_CABLE=2, LR_SHARP=0.3, LR_SAT=1.15, LR_SIG_LO=0.18, LR_SIG_HI=0.4, LR_HFOCUS=0.4,
        LR_PERSIST=0.06, LR_CONV=0.18, LR_MASK_TYPE=1, LR_MASK_TRIADS=380, LR_WHITE=9300,
        LR_CURV_X=0.04, LR_CURV_Y=0.0, LR_GLOW=0.05, LR_VIGNETTE=0.22, LR_AMBIENT=0.001),
    # Broadcast/studio monitor on RGB: sharp, fine grille, calibrated D65
    "studio-monitor-rgb": dict(
        LR_CABLE=3, LR_SHARP=0.0, LR_SAT=1.0, LR_CONTRAST=1.0, LR_SIG_LO=0.14, LR_SIG_HI=0.32,
        LR_HFOCUS=0.3, LR_PERSIST=0.04, LR_CONV=0.05, LR_MASK_TYPE=1, LR_MASK_STR=0.6,
        LR_MASK_TRIADS=450, LR_WHITE=6500, LR_CURV_X=0.02, LR_CURV_Y=0.02, LR_GLOW=0.03,
        LR_VIGNETTE=0.15, LR_OVERSCAN=0.03, LR_AMBIENT=0.0),
    # Old TV on the antenna input: RF snow, ghosting, soft focus, round tube
    "old-tv-rf": dict(
        LR_CABLE=0, LR_YTRAP=0.6, LR_SHARP=0.5, LR_NOISE=0.09, LR_SAT=1.15, LR_SIG_LO=0.24, LR_SIG_HI=0.5,
        LR_HFOCUS=0.7, LR_PERSIST=0.12, LR_CONV=0.6, LR_MASK_TYPE=3, LR_MASK_STR=0.45,
        LR_MASK_TRIADS=320, LR_WHITE=7500, LR_CURV_X=0.07, LR_CURV_Y=0.09, LR_GLOW=0.1,
        LR_VIGNETTE=0.4, LR_OVERSCAN=0.06, LR_AMBIENT=0.005),
    # Arcade CRT projector (three tubes on a flat screen), e.g. Daytona USA deluxe
    "crt-projector": dict(
        LR_CABLE=3, LR_SHARP=0.0, LR_SAT=1.18, LR_CONTRAST=1.0, LR_SIG_LO=0.24, LR_SIG_HI=0.46,
        LR_HFOCUS=0.6, LR_PERSIST=0.05, LR_CONV=0.85, LR_MASK_TYPE=4, LR_MASK_STR=0.4,
        LR_MASK_TRIADS=400, LR_WHITE=7500, LR_CURV_X=0.0, LR_CURV_Y=0.0, LR_GLOW=0.08,
        LR_VIGNETTE=0.45, LR_OVERSCAN=0.02, LR_AMBIENT=0.006),
    # PC VGA monitor: 31 kHz line doubling, fine dot mask, black border
    "pc-monitor-vga": dict(
        LR_CONSOLE=7, LR_CABLE=3, LR_SHARP=0.0, LR_SAT=1.08, LR_SIG_LO=0.3, LR_SIG_HI=0.5,
        LR_HFOCUS=0.3, LR_PERSIST=0.03, LR_CONV=0.12, LR_MASK_TYPE=3, LR_MASK_STR=0.45,
        LR_MASK_TRIADS=600, LR_WHITE=9300, LR_CURV_X=0.035, LR_CURV_Y=0.045, LR_GLOW=0.04,
        LR_VIGNETTE=0.15, LR_OVERSCAN=-0.06, LR_AMBIENT=0.002),
    # Commodore 1084S on RGB: 15 kHz like a TV, sharp, no composite artefacts
    "amiga-1084s": dict(
        LR_CABLE=3, LR_SHARP=0.1, LR_SAT=1.12, LR_SIG_LO=0.2, LR_SIG_HI=0.42, LR_HFOCUS=0.45,
        LR_PERSIST=0.05, LR_CONV=0.25, LR_MASK_TYPE=2, LR_MASK_STR=0.5, LR_MASK_TRIADS=400,
        LR_WHITE=8000, LR_CURV_X=0.05, LR_CURV_Y=0.06, LR_GLOW=0.06, LR_VIGNETTE=0.25,
        LR_OVERSCAN=0.02, LR_AMBIENT=0.002),
}


def fmt(v):
    return f"{float(v):.4f}".rstrip("0").rstrip(".") if "." in f"{float(v):.4f}" else str(v)


# What the flat/ folder changes on top of every preset
FLAT = dict(LR_CURV_X=0.0, LR_CURV_Y=0.0, LR_CORNER=0.0)

# HDR presets: the shader writes HDR10 itself in the last pass. That pass must render
# straight to the screen (no scale_type), otherwise RetroArch adds a copy pass and
# converts the already-HDR picture a second time.
HDR = dict(LR_NITS_WHITE=200, LR_NITS_PEAK=400)


def hdr_passes(passes):
    p = passes.replace("../shaders/", "../../shaders/")
    p = p.replace("/crt.slang", "/crt-hdr.slang").replace("/present.slang", "/present-hdr.slang")
    p = p.replace("scale_type6 = viewport\nscale6 = 1.0\n", "")
    assert "crt-hdr.slang" in p and "present-hdr.slang" in p and "scale_type6" not in p
    return p


VARIANTS = {
    "curved": ({}, PASSES),
    "flat": (FLAT, PASSES),
    "hdr/curved": (HDR, hdr_passes(PASSES)),
    "hdr/flat": ({**FLAT, **HDR}, hdr_passes(PASSES)),
}


def main():
    for folder, (variant, passes) in VARIANTS.items():
        (PACK / folder).mkdir(parents=True, exist_ok=True)
        for name, over in PRESETS.items():
            values = {**BASE, **over, **variant}
            lines = [f"# Lola Retro - {name} ({folder})", "# Generated by tools/make_presets.py", "",
                     passes.rstrip(), ""]
            lines.append('parameters = "' + ";".join(values) + '"')
            lines += [f"{k} = {fmt(v)}" for k, v in values.items()]
            out = PACK / folder / f"{name}.slangp"
            out.write_text("\n".join(lines) + "\n")
            print("wrote", out.relative_to(PACK.parent.parent))


if __name__ == "__main__":
    main()
