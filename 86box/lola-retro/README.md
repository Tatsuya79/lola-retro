# Lola Retro for 86Box

The PC monitor (VGA) preset of [Lola Retro](https://github.com/zoran123456/lola-retro) for the PC emulator
[86Box](https://86box.net). It is the RetroArch shader converted to GLSL, and it gives the same picture: a
typical mid-90s 15-inch monitor described the way its spec sheet would (14-inch picture, 0.28 mm dot mask,
a beam spot of 0.30–0.45 mm), so every video mode looks as it did on that one tube. 320×200 games show
chunky pixels with fine lines, 640×480 is sharp, 1024×768 a little soft.

![The PC monitor preset at 320x200, 640x480 and 1024x768](https://raw.githubusercontent.com/zoran123456/lola-retro/main/docs/pc-monitor-modes.jpg)

<sub>The PC monitor preset in RetroArch. In 86Box it gives the same picture.</sub>

## Install

1. 86Box 6.0 or newer.
2. Put this `lola-retro` folder anywhere, for example next to 86Box.
3. **View → Renderer → OpenGL (3.0 Core)** (newer builds call it just OpenGL).
4. **View → Renderer options…** opens the Shader Manager. **Add**, choose `lola-retro/pc-monitor-vga.glslp`, then **OK**.
   Keep it the only shader in the list: it needs the emulator's frame as its input.

## 86Box settings

| Setting | Use | Why |
|---|---|---|
| View → Window scale factor | **2x** or more, or full screen | The tube needs screen pixels: at 1x the lines and the dot mask are finer than the window's pixels. Full screen on a 1080p or 4K display is best. |
| View → Force 4:3 display ratio | on | The window is then 4:3, like the tube. Without it the tube still stays 4:3, with black bars at the sides. |
| View → OpenGL input stretch mode | **Full screen stretch** (default) | The shader needs the frame exactly as the video card drew it. The other modes resize it, and the shader would take it for a different video mode. (OpenGL input scale then makes no difference.) |
| View → Filter method | either | The shader reads the frame pixel by pixel, so this makes no difference. |
| Settings → Display → Monitor → VGA screen type | **RGB Color** (default) | The preset is a colour monitor. Amber, green and white monitors had no dot mask. |
| Settings → Display → Monitor → Overscan | off (default) | The shader draws the monitor's black border itself. |

86Box hands over the frame as the VGA card drew it: 320×200 games arrive as 640×400 (the card already draws
every line twice), text mode as 720×400. The shader draws the 400 lines the monitor showed, the same picture
as RetroArch's 320×200.

## Parameters

**View → Renderer options… → Configure** lists 23 of them. They mean the same as in RetroArch (see the
[shader pack's README](https://github.com/zoran123456/lola-retro/blob/main/shaders/lola-retro/README.md));
**Reset** returns to the preset. The rest (cable, console timing, the TV controls) is fixed at the preset's values.
To fill more of the screen, raise the shader's **Overscan** (like turning up the monitor's width and height).

## Differences from the RetroArch version

- No phosphor afterglow: 86Box has no feedback textures. A still picture is the same; moving objects
  leave no faint trail.
- The tube stays 4:3 whatever shape the window has.

Checked against 86Box's source with a test renderer that runs the preset the way 86Box does
(`tools/86box-preview.py`): in text mode, 640×350, 640×400, 640×480, 800×600 and 1024×768 the picture matches
the RetroArch version within 2/255 per pixel. Not yet tried in 86Box itself; reports are welcome.

## For developers

The files in this folder are generated; don't edit them. In the [repository](https://github.com/zoran123456/lola-retro),
change the slang passes in `shaders/lola-retro/shaders`, then run `python3 tools/make_glsl.py`. How the
conversion works is described at the top of that script.

## License

MIT, see the LICENSE file in this folder.
