#!/bin/bash
# Render one frame of an image through a shader preset in headless RetroArch.
# Linux only. Needs: retroarch, xvfb-run, and a GL or Vulkan driver (Mesa llvmpipe/lavapipe work).
#
# Usage: tools/render-preview.sh PRESET.slangp IMAGE.png OUT.png [glcore|vulkan]
set -euo pipefail

PRESET="$(realpath "$1")"
IMAGE="$(realpath "$2")"
OUT="$(realpath -m "$3")"
DRIVER="${4:-glcore}"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/retroarch.cfg" <<CFG
video_driver = "$DRIVER"
video_shader_enable = "true"
video_fullscreen = "true"
video_fullscreen_x = "1440"
video_fullscreen_y = "1080"
aspect_ratio_index = "0"
video_scale_integer = "false"
menu_driver = "null"
audio_driver = "null"
input_driver = "null"
builtin_imageviewer_enable = "true"
pause_nonactive = "false"
video_gpu_screenshot = "true"
config_save_on_exit = "false"
video_font_enable = "false"
notification_show_screenshot = "false"
CFG

rm -f "$OUT"
HOME="$WORK" xvfb-run -a -s "-screen 0 1440x1080x24" \
  retroarch --config "$WORK/retroarch.cfg" --set-shader "$PRESET" \
  --max-frames=14 --max-frames-ss --max-frames-ss-path="$OUT" "$IMAGE" > "$WORK/retroarch.log" 2>&1 || true

if [ -f "$OUT" ]; then
  echo "wrote $OUT"
else
  echo "no screenshot was written; RetroArch log:" >&2
  grep -iE "slang|shader|error" "$WORK/retroarch.log" >&2 || cat "$WORK/retroarch.log" >&2
  exit 1
fi
