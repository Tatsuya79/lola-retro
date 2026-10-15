# Lola Retro for RetroArch

A CRT shader that simulates the whole analog chain instead of drawing scanlines on top of pixels:

**console video encoder → cable → TV decoder → electron beam → phosphor mask → glass**

Named after Lola, my retriever. Retro because it is retro.

## Quick start

1. Copy this `lola-retro` folder into RetroArch's shader folder (see [Install](#install)).
2. Start a game, open **Quick Menu → Shaders → Load** and pick **`lola-retro/curved/consumer-tv-composite.slangp`**.

That is the TV most people had, on the cable that came in the box. Everything else adjusts itself:
the console is detected from the frame size and the mask adapts to your screen resolution.
3D consoles can run at a higher internal resolution: the shader reads the upscaled frame as blocks of one
console pixel, so the signal stays right and the extra detail only makes edges smoother.
A few cases need one setting, see [RECOMMENDED-CORES.md](RECOMMENDED-CORES.md).

## Curved or flat

Both folders have the same 8 presets.

| Folder | Screen |
|---|---|
| `curved/` | The glass bulges like a real tube, with rounded corners. |
| `flat/` | Same presets, flat screen and square corners. Pick this if curvature bothers you. |

## Presets

| Preset | Use it for | What it simulates |
|---|---|---|
| `consumer-tv-composite` | **Start here.** Everything from NES to PS1, especially Mega Drive and other games drawn for composite. | Ordinary living-room TV on the composite cable. Dithered waterfalls turn translucent, colours bleed. |
| `consumer-tv-scart-rgb` | The same games with a clean picture. The European SCART setup. | The same TV fed RGB. Sharp, no composite artefacts, same tube. |
| `trinitron-s-video` | PS2, GameCube, N64 and other late-90s consoles. | Sony Trinitron on S-Video. Aperture grille with the two damper wires, cool white. |
| `studio-monitor-rgb` | The sharpest picture with strong scanlines (the PVM/BVM look). | Broadcast/studio monitor on RGB, calibrated 6500 K white. |
| `old-tv-rf` | Nostalgia for the antenna cable. Noisy on purpose. | Old TV on the antenna input. RF snow, ghosting, soft focus, round tube. |
| `crt-projector` | 24 kHz arcade games such as Daytona USA (Sega Model 2). | Arcade CRT projector (three tubes on a flat screen). No mask, colour fringes, soft. |
| `pc-monitor-vga` | DOS games, Dreamcast on the VGA cable. | PC VGA monitor. 31 kHz line doubling for 320×200 games, fine dot mask, black border. |
| `amiga-1084s` | Amiga, and 15 kHz arcade games (CPS1/2, Neo Geo MVS). | Commodore 1084S on RGB. 15 kHz like a TV, sharp. |

The CRT projector already has a flat screen, so its `curved` version only adds rounded corners.

## Install

1. In RetroArch, open **Settings → Directory → Video Shaders** to see where your shader folder is.
2. Copy this whole `lola-retro` folder into that folder. If an older copy is there, replace it.
3. Start a game, open **Quick Menu → Shaders → Load**, go into `lola-retro`, then `curved` or `flat`, and pick a preset.

The shaders are in the slang format, so the video driver must be **Vulkan, glcore, D3D11, D3D12 or Metal**
(**Settings → Drivers → Video**). The old `gl` driver does not run slang shaders.

To use a preset automatically for one system, load it and choose **Quick Menu → Shaders → Save → Save Core Preset**.

## Core settings that matter

For every system from NES to PS2 (core, preset, console timing and settings), see
[RECOMMENDED-CORES.md](RECOMMENDED-CORES.md). The short version:

The composite simulation needs the console's real pixel grid:

- **PlayStation** (Beetle PSX, SwanStation): no texture filtering, dithering on. Internal resolution 1× or 3× and up
  works by itself; at 2× set **Console timing** to 4.
- **Mega Drive / Genesis** (Genesis Plus GX, PicoDrive): any built-in "Blargg NTSC" filter **off**.
- **SNES** (Snes9x, bsnes): built-in NTSC/blargg filters **off**, hi-res blending **off**.
- **DOS** (DOSBox Pure): leave the default VGA output; use `pc-monitor-vga`.
- RetroArch: **Settings → Video → Scaling → Aspect Ratio**: *Core provided* or *4:3*. Integer scaling is optional.

**Internal resolution.** Frames with more than 576 lines are read as an upscaled 240-line mode (3× and up).
At 2× an upscaled PS1 frame looks like 480i, so set Console timing 4 by hand. 480-line consoles
(PS2, GameCube, Dreamcast) upscaled: set Console timing 5, then the frame is read as upscaled 480i.
Higher internal resolutions cost more GPU time in the encoder and decoder.

## Parameters

Every preset only sets parameters, so you can tweak any preset in **Quick Menu → Shaders → Shader Parameters**.

| Parameter | Meaning |
|---|---|
| Console timing | Pixel clock and colour-subcarrier rhythm of the console. 0 = auto (see below). |
| TV standard | 0 auto (by line count), 1 NTSC, 2 PAL. |
| Cable | 0 RF, 1 Composite, 2 S-Video, 3 RGB. |
| Comb filter | Better TVs separated colour with a comb filter. Only works when the console flips the subcarrier phase every line (PlayStation, Amiga). |
| Luma trap in the console | How much of the fine luma detail the console's encoder filtered out before mixing in colour. Lower it for a stronger rainbow on fine stripes (Sonic's waterfall), raise it for a cleaner picture. Real consoles differed a lot here: 0 is like an early Mega Drive Model 1 (strong rainbow), 0.75 (default) a typical console, 1 a later model with a better encoder. |
| Sharpness, Colour, Tint, Brightness, Contrast | The TV's own knobs. Tint only exists on NTSC sets. |
| RF noise | Snow on the antenna input. |
| No flicker | On by default. Freezes dot crawl and draws both interlaced fields every frame. Turn it off for the real moving dot crawl and 480i field flicker. |
| Beam width in dark areas / on white | The beam spot grows with brightness, in both directions: bright lines merge and bright detail rounds off, dark lines show gaps and dark detail stays sharp. |
| Horizontal focus | Horizontal spot size relative to the vertical one; 0.5 = a round spot. |
| Tube gamma | 2.4 is typical. |
| Phosphor afterglow | Real phosphors fade within about a millisecond, so keep this low. |
| Misconvergence | How far the red and blue beams land from green, growing toward the edges. |
| Mask | 0 none, 1 aperture grille, 2 slot mask, 3 shadow mask, 4 projection screen. |
| Mask strength / triads per screen height | Lower the triad count for a coarser mask. Below about 2 screen pixels per triad the mask fades out to avoid moiré, so the fine PC mask shows best at 4K. |
| White point | 6500 K neutral, 9300 K is the bluish white many TVs and monitors had. |
| Curvature, Rounded corners, Glass glow, Glass glow size, Darker edges, Overscan, Room reflection | The glass. Glass glow sets how strong the halo around bright things is, Glass glow size how far it reaches. Set curvature and corners to 0 for a flat screen (that is all the `flat` presets change). Negative overscan adds a black border, like PC monitors. |

### Console timing ids

| Id | Console | Why it matters |
|---|---|---|
| 1 | SNES / NES | 227⅓ subcarrier cycles per line: dot crawl moves diagonally. |
| 2 | Mega Drive H40 (320 wide) | Exactly 228 cycles: still vertical stripes; 1-pixel dithers become rainbow transparency. |
| 3 | Mega Drive H32 (256 wide) | Same rhythm as H40 at a slower pixel clock. |
| 4 | PlayStation | 227½ cycles: the pattern flips every line and every field, so a comb filter can remove it. |
| 5 | PlayStation 2 | 480i at 13.5 MHz. |
| 6 | Amiga | 320×256 PAL / 320×200 NTSC, 640-wide hi-res. |
| 7 | PC VGA | 31 kHz; 200-line modes are drawn twice (400 lines). |
| 8 | Arcade 15 kHz | Generic 15 kHz RGB. |
| 9 | Arcade 24 kHz | Medium-res arcade, e.g. Sega Model 2 at 496×384 progressive. |

**Auto** guesses from the frame size: 256 wide → SNES, 320×224 → Mega Drive, 320×240 and other 240-line
modes → PlayStation, 320×200 or 640×400 → VGA, 256 or 512 lines → Amiga, 336–400 lines → arcade 24 kHz,
more than 300 lines → interlaced, more than 576 lines → upscaled (see Internal resolution). Set the id by hand when the guess is wrong (for example a Mega Drive game
in 240-line PAL mode).

## Performance

The chain has seven passes and the encoder and decoder do real signal filtering, so it needs a reasonably
modern GPU. Integrated graphics from the last few years should be fine at 1080p.
The beam pass works out each source line once on an 8192-pixel-wide texture, so the GPU has to support
textures that wide (every desktop GPU of the last decade does).

## Tested

Rendered in RetroArch 1.18 with the glcore and Vulkan drivers, and reported working with Vulkan and glcore on
Windows (NVIDIA). Direct3D 10/11/12 failed in 1.0 and 1.1; 1.2 fixes the cause (every pass now samples
without derivatives, which the Direct3D compiler requires inside loops). Metal has not been confirmed yet.
Reports are welcome.

## License

MIT, see the LICENSE file in this folder.
