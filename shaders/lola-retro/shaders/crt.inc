// Lola Retro - pass 6: electron beam, phosphor mask and glass, at screen resolution.
// Shared by crt.slang (SDR) and crt-hdr.slang (HDR).
// - Each source line is a beam with a Gaussian profile whose width grows with brightness
//   (per colour gun), so bright lines merge and dark lines show gaps.
// - Interlaced sources alternate fields; VGA 200-line modes are scanned twice.
// - Mask: aperture grille (with damper wires), slot mask, shadow mask or a projection screen.
// - Glass: curvature, halation, white point, vignette, room reflection, phosphor afterglow.
// Options for the GLSL port (tools/make_glsl.py), off in RetroArch:
//   LR_FIELDS       an Amiga frame with every line drawn twice is one line per row pair
//                   (the fields passes find out), not an interlaced frame
//   LR_NO_FEEDBACK  no afterglow (the emulator has no feedback textures)
//   LR_FIT_43       the tube stays 4:3 whatever shape the picture area has
//   LR_PACKED_SPOT  the beam pass has no alpha channel, the spot is packed onto red
//   LR_ROWS_UP      rows count from the bottom of the screen (OpenGL), for light from above

#include "common.inc"

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
layout(location = 1) flat out vec3 vWhite;
layout(location = 2) flat out vec4 vTiming;   // scan mode, PAL, interlaced, internal resolution
layout(location = 3) flat out float vPhos;    // phosphor set
layout(location = 4) flat out float vShare;   // share of the active line the source covers
#ifdef LR_FIELDS
layout(location = 5) flat out float vAmiga;
#endif

// Blackbody colour of the TV's white, relative to D65
vec3 kelvinRGB(float T)
{
    float t = T / 100.0;
    float r = t <= 66.0 ? 255.0 : 329.698727446 * pow(t - 60.0, -0.1332047592);
    float g = t <= 66.0 ? 99.4708025861 * log(t) - 161.1195681661 : 288.1221695283 * pow(t - 60.0, -0.0755148492);
    float b = t >= 66.0 ? 255.0 : (t <= 19.0 ? 0.0 : 138.5177312231 * log(t - 10.0) - 305.0447927307);
    return clamp(vec3(r, g, b), 1.0, 255.0) / 255.0;
}

void main()
{
    gl_Position = global.MVP * Position;
    vTexCoord = TexCoord;
    // keep white equally bright and only shift its tint
    vec3 c = pow(kelvinRGB(global.LR_WHITE) / kelvinRGB(6500.0), vec3(2.2));
    vWhite = c / dot(c, vec3(0.2126, 0.7152, 0.0722));
    Timing T = getTiming(params.OriginalSize.xy, params.FrameCount);
    vTiming = vec4(T.scan, T.pal, T.interlace, T.scale);
    vPhos = phosphorSet(T);
    vShare = activeShare(T, params.OriginalSize.xy);
#ifdef LR_FIELDS
    vAmiga = isAmiga(T) ? 1.0 : 0.0;
#endif
    // the white is set on the phosphor drives so that it ends up at the chosen colour temperature
    if (vPhos > 1.5) vWhite = inverse(phosphorMatrix(vPhos)) * vWhite;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 1) flat in vec3 vWhite;
layout(location = 2) flat in vec4 vTiming;
layout(location = 3) flat in float vPhos;
layout(location = 4) flat in float vShare;
#ifdef LR_FIELDS
layout(location = 5) flat in float vAmiga;
#endif
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D LR_Beam;
layout(set = 0, binding = 3) uniform sampler2D LR_Glow;
#ifndef LR_NO_FEEDBACK
layout(set = 0, binding = 4) uniform sampler2D LR_CRTFeedback;
#endif
#ifdef LR_FIELDS
layout(set = 0, binding = 5) uniform sampler2D LR_Fields;
#endif

// Beam of source line i at position x along the line (from the beam pass):
// rgb = square root of its linear brightness, a = which spot made it (PC monitor);
// 0 outside the picture
vec4 beamAt(float i, float x, float lines)
{
    if (i < 0.0 || i >= lines) return vec4(0.0);
#ifdef LR_PACKED_SPOT
    // the spot is packed onto red (see the beam pass), so the two samples are read and blended
    // here instead of by the texture filter
    int w = textureSize(LR_Beam, 0).x;
    float t = x * float(w);
    int m = int(floor(t));
    vec3 a = texelFetch(LR_Beam, ivec2(clamp(m, 0, w - 1), int(i)), 0).rgb;
    vec3 b = texelFetch(LR_Beam, ivec2(clamp(m + 1, 0, w - 1), int(i)), 0).rgb;
    float qa = floor(a.r * 0.25), qb = floor(b.r * 0.25);
    a.r -= 4.0 * qa;
    b.r -= 4.0 * qb;
    return mix(vec4(a, qa / 255.0), vec4(b, qb / 255.0), fract(t));
#else
    vec2 bs = vec2(textureSize(LR_Beam, 0));
    return textureLod(LR_Beam, vec2(x + 0.5 / bs.x, (i + 0.5) / bs.y), 0.0);
#endif
}

// Rear-projection screen: lenticular sheet with thin black stripes, no RGB pattern
float lenticular(float x)
{
    float f = fract(x);
    return smoothstep(0.0, 0.18, f) * smoothstep(1.0, 0.82, f);
}

// Shadow mask: staggered round dots
vec3 shadowDots(vec2 mp)
{
    float s = mp.x * 3.0;
    float ci = floor(s), fx = fract(s);
    float fy = fract(mp.y * 1.732 + mod(ci, 2.0) * 0.5);
    float dt = smoothstep(0.52, 0.30, length(vec2(fx - 0.5, fy - 0.5)));
    float chn = mod(ci, 3.0);
    return vec3(chn < 0.5 ? dt : 0.0, (chn > 0.5 && chn < 1.5) ? dt : 0.0, chn > 1.5 ? dt : 0.0);
}

// Aperture grille / slot mask: vertical RGB stripes
vec3 stripes(float x)
{
    float s = x * 3.0;
    vec3 m;
    for (int ch = 0; ch < 3; ch++)
    {
        float d = abs(fract((s - (float(ch) + 0.5)) / 3.0 + 0.5) - 0.5) * 3.0;
        m[ch] = smoothstep(0.75, 0.35, d);
    }
    return m;
}

// Slot mask: horizontal breaks, staggered per triad column
float slotGate(float x, float y)
{
    float yy = fract(y * 0.5 + mod(floor(x), 2.0) * 0.5);
    return smoothstep(0.0, 0.12, yy) * smoothstep(1.0, 0.88, yy);
}

// Mask averaged over 2x2 points (a and b are the corners) to keep moire down.
// Parts that only depend on x are evaluated once per column.
vec3 maskAvg(vec2 a, vec2 b, float mt)
{
    if (mt > 3.5) return vec3(0.5 * (lenticular(a.x) + lenticular(b.x)));
    if (mt > 2.5) return 0.25 * (shadowDots(a) + shadowDots(vec2(b.x, a.y)) + shadowDots(vec2(a.x, b.y)) + shadowDots(b));
    vec3 ma = stripes(a.x), mb = stripes(b.x);
    if (mt > 1.5)
        return 0.25 * (ma * (slotGate(a.x, a.y) + slotGate(a.x, b.y)) + mb * (slotGate(b.x, a.y) + slotGate(b.x, b.y)));
    return 0.5 * (ma + mb);
}

vec3 shoulder(vec3 x)
{
    const float k = 0.8;
    vec3 e = max(x - k, 0.0);
    return min(x, vec3(k)) + (1.0 - k) * (1.0 - exp(-e / (1.0 - k)));
}

// Signed distance from p to a box of half size b with corners of radius r (negative inside)
float roundBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// Direction away from that box at p
vec2 boxOut(vec2 p, vec2 b, float r)
{
    return sign(p) * normalize(max(abs(p) - b + r, 1e-3));
}

// Light on a surface facing n (z toward the viewer): from above and in front, plus some from
// the rest of the room. 1 on a surface facing the viewer.
float lampOn(vec3 n) { return 0.3 + 0.7 * max(dot(n, vec3(0.0, 0.5, 0.866)), 0.0) / 0.866; }

// Smooth value noise, 0..1
float valueNoise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash13(vec3(i, 5.0)), hash13(vec3(i + vec2(1.0, 0.0), 5.0)), f.x),
               mix(hash13(vec3(i + vec2(0.0, 1.0), 5.0)), hash13(vec3(i + 1.0, 5.0)), f.x), f.y);
}

// Space (Around the tube 2): layers of stars that slowly come closer and drift, as if floating
// through space, over a faint nebula. Decoration, not a simulation. s: screen position from the
// centre in screen heights, t: seconds, pix: one screen pixel in screen heights.
vec3 space(vec2 s, float t, float pix)
{
    vec2 drift = vec2(sin(t * 0.013), sin(t * 0.0091 + 1.3)) * 0.08;
    vec2 np = (s + 0.2 * drift) * 1.6 + vec2(0.003 * t, 0.0);
    float n = 0.0, a = 0.5;
    for (int o = 0; o < 4; o++)
    {
        n += a * valueNoise(np);
        np = np * 2.03 + 17.0;
        a *= 0.5;
    }
    vec3 sum = mix(vec3(0.05, 0.07, 0.22), vec3(0.20, 0.05, 0.18), valueNoise(s * 0.7 + 40.0)) * 0.09 * smoothstep(0.4, 0.9, n);
    for (int i = 0; i < 5; i++)
    {
        float k = t * 0.008 + float(i) * 0.2;
        float z = fract(k);                                   // 0 far away, 1 close
        float seed = floor(k) * 5.0 + float(i);
        float cell = 0.016 * exp2(3.0 * z);                   // stars spread apart as they come closer
        vec2 q = (s + drift * (0.15 + z)) / cell + 97.0 * vec2(hash13(vec3(seed, 1.0, 2.0)), hash13(vec3(seed, 3.0, 4.0)));
        vec2 id = floor(q);
        float h = hash13(vec3(id, seed + 11.0));
        if (h < 0.55) continue;
        vec2 at = 0.2 + 0.6 * vec2(hash13(vec3(id, seed + 3.0)), hash13(vec3(id, seed + 7.0)));
        float d = length(fract(q) - at) * cell;               // in screen heights
        float r0 = mix(0.00026, 0.0007, z) * (0.7 + 0.6 * fract(h * 13.0));
        float r = max(r0, 0.6 * pix);                         // at least about a pixel, same light
        float b = mix(0.15, 1.5, z * z) * (0.12 + 0.88 * pow(fract(h * 37.0), 3.0)) * (r0 * r0) / (r * r);
        b *= smoothstep(0.0, 0.3, z) * (1.0 - smoothstep(0.85, 1.0, z));
        b *= 1.0 + 0.35 * (1.0 - z) * sin(t * (1.5 + 3.0 * fract(h * 53.0)) + h * 100.0);
        float hue = fract(h * 91.0);
        vec3 tint = hue < 0.5 ? vec3(0.78, 0.86, 1.0) : hue < 0.85 ? vec3(1.0) : vec3(1.0, 0.86, 0.68);
        sum += b * tint * (exp(-0.5 * d * d / (r * r)) + 0.07 * exp(-0.5 * d * d / (16.0 * r * r)));
    }
    return sum;
}

// What is around the tube (Around the tube), drawn where the glass ends. Linear light, like the
// picture. c: tube position (-1..1, after curvature); dist, qq: how far outside the tube edge, in
// the same units; tc: position on the tube before curvature (0..1); tubePx: tube size in pixels.
// Sizes are in mm (the tube is Monitor size), so the casing is as large as the real one.
vec3 backdrop(vec2 c, float dist, vec2 qq, vec2 tc, vec2 tubePx)
{
    vec2 tube = monTubeMM();
    float mmpp = tube.y / tubePx.y;                               // mm per screen pixel
    float aa = mmpp;                                              // one screen pixel
    vec2 p = (tc - 0.5) * tubePx * mmpp;                          // mm from the tube centre
#ifndef LR_ROWS_UP
    p.y = -p.y;                                                   // y up
#endif
    bool inSpace = global.LR_BACKDROP > 1.5;

    // The monitor's casing, from real ones: the Amiga monitors (1084S cabinet 352 x 326 mm,
    // M1438S), a 15-inch PC monitor of the 90s, a TV. Front margins around the tube face (side,
    // top, bottom), the width of the bevel that slopes down to the glass, the plastic.
    bool tv = !inMM(vTiming.x);
    bool amiga = global.LR_MON_MM > 0.5;
    vec3 m = amiga ? vec3(44.0, 33.0, 95.0) : tv ? vec3(50.0, 40.0, 90.0) : vec3(39.0, 35.0, 75.0);
    float bw = amiga ? 24.0 : 20.0;
    vec3 albedo = tv ? vec3(0.035, 0.035, 0.038) : amiga ? vec3(0.50, 0.48, 0.42) : vec3(0.62, 0.56, 0.42);

    // the light: the room's (Room light: 1 a dim room with warm lamps, 20 daylight, whiter and
    // more even), brightest near the monitor; in space a faint cold light
    float day = smoothstep(1.0, 20.0, global.LR_ROOM);
    float near = mix(1.0 / (1.0 + dot(p, p) / (260.0 * 260.0)), 1.0, 0.6 * day);
    vec3 light = inSpace ? vec3(0.005, 0.0055, 0.007)
               : mix(vec3(1.0, 0.93, 0.82), vec3(1.0), day) * (0.0167 * global.LR_ROOM * near);

    // from the glass out: a shadowed slit, the bevel, a rounded lip, the front
    vec2 inner = 0.5 * tube;
    float d = dist * mix(1.0, tubePx.x / tubePx.y, smoothstep(-0.05, 0.05, qq.x - qq.y)) * 0.5 * tube.y;   // mm from the glass
    float gap = smoothstep(0.0, max(2.0, aa), d);
    float ob = roundBox(p, inner + bw, amiga ? 8.0 : 14.0);       // < 0 on the bevel
    vec2 half_ = inner + vec2(m.x, 0.5 * (m.y + m.z));
    vec2 at = p - vec2(0.0, 0.5 * (m.y - m.z));                   // from the centre of the front
    float o = roundBox(at, half_, 22.0);                          // < 0 on the front
    float front = 1.0 - smoothstep(-aa, aa, o);

    // which way the surface faces: the bevel toward the tube (mitred corners on the Amiga
    // monitors, rounded on the others), the front toward the viewer, rolling over to the sides
    // in its last 8 mm. The top bevel faces down and gets the least light, the bottom one the most.
    vec2 nb = boxOut(p, inner, 15.0);
    if (amiga)
    {
        vec2 e = abs(p) - inner;
        nb = normalize(mix(vec2(0.0, sign(p.y)), vec2(sign(p.x), 0.0), smoothstep(-aa, aa, e.x - e.y)));
    }
    float roll = 1.5708 * clamp(1.0 + o / 8.0, 0.0, 1.0);
    vec3 nFront = vec3(boxOut(at, half_, 22.0) * sin(roll), cos(roll));
    vec3 n = normalize(mix(vec3(-0.77 * nb, 0.64), nFront, smoothstep(-1.5 - aa, 1.5 + aa, ob)));
    float shade = lampOn(n);

    // the seam above the front panel with the controls, and the green power light
    float yb = at.y + half_.y;                                    // mm above the bottom edge
    if (!tv)
    {
        float g = yb - 20.0, sw = max(0.5, 0.6 * aa);
        float span = 1.0 - smoothstep(-aa, aa, abs(p.x) - half_.x + 28.0);
        shade *= 1.0 - 0.6 * span * exp(-0.5 * g * g / (sw * sw));
        shade += 0.25 * span * exp(-0.5 * (g + 1.0) * (g + 1.0) / (sw * sw));
    }
    float ld = length(vec2(at.x - half_.x + 30.0, yb - 10.0));
    vec3 led = vec3(0.12, 1.0, 0.2) * (0.5 * (1.0 - smoothstep(1.5 - aa, 1.5 + aa, ld)) + 0.02 * exp(-ld * ld / 32.0));

    // the picture lights the bevel next to it, weakly: its light leaves the glass at a grazing
    // angle. Three samples along the edge, wider apart further from it.
    float ov = 1.0 - global.LR_OVERSCAN;
    vec2 edge = clamp(c * 0.5 * ov + 0.5, 0.0, 1.0);
    vec2 nt = boxOut((tc - 0.5) * tube, inner, 15.0);             // in texture orientation
    vec2 along = vec2(-nt.y, nt.x) * (4.0 + d) / tube * ov;
    vec3 gl = 0.5 * textureLod(LR_Glow, edge, 0.0).rgb
            + 0.25 * textureLod(LR_Glow, clamp(edge - along, 0.0, 1.0), 0.0).rgb
            + 0.25 * textureLod(LR_Glow, clamp(edge + along, 0.0, 1.0), 0.0).rgb;
    vec3 pic = gl * vWhite * 0.035 * exp(-d / 6.0) * mix(0.1, 1.0, 1.0 - smoothstep(-aa, aa, ob));

    vec3 casing = albedo * (light * shade + pic) * gap + led;
    vec3 behind = inSpace ? space((vTexCoord - 0.5) * params.OutputSize.xy * params.OutputSize.w,
                                  float(params.FrameCount % 1000000u) / 60.0, params.OutputSize.w)
                          : vec3(0.20, 0.195, 0.18) * light;   // the wall, further from the light
    return mix(behind, casing, front);
}

void main()
{
    float scan = vTiming.x, pal = vTiming.y, interlace = vTiming.z;

    // tube geometry: barrel curvature and rounded corners
    vec2 tc = vTexCoord;
    vec2 tubePx = params.OutputSize.xy;                   // size of the tube face in screen pixels
    float px = 2.0 * params.OutputSize.z;
    float va = params.OutputSize.x * params.OutputSize.w;
#ifndef LR_FIT_43
    // RetroArch: the picture area is the tube, unless something is drawn around it; then
    // it may fill the screen (Aspect Ratio: Full)
    if (global.LR_BACKDROP > 0.5 && abs(0.75 * va - 1.0) > 0.02)
#endif
    {
        // the picture area may be any shape: the 4:3 tube sits in its middle
        vec2 fit = va > 4.0 / 3.0 ? vec2(4.0 / 3.0 / va, 1.0) : vec2(1.0, 0.75 * va);
        tc = (tc - 0.5) / fit + 0.5;
        tubePx *= fit;
        px /= fit.x;
    }
    // real size: the tube as large as the monitor it describes (Monitor size), on a screen whose
    // height is given; the picture area must be as tall as the screen (full screen)
    if (global.LR_SCREEN_H > 0.0)
    {
        float screenMM = global.LR_SCREEN_H * (global.LR_SCREEN_UNIT > 0.5 ? 25.4 : 10.0);
        float s = min(params.OutputSize.y * monTubeMM().y / (screenMM * tubePx.y), 1.0);
        tc = (tc - 0.5) / s + 0.5;
        tubePx *= s;
        px /= s;
    }
    vec2 c = tc * 2.0 - 1.0;
    c *= 1.0 + vec2(global.LR_CURV_X, global.LR_CURV_Y) * (c.yx * c.yx);
    float corner = global.LR_CORNER;
    vec2 qq = abs(c) - 1.0 + corner;
    float dist = length(max(qq, 0.0)) + min(max(qq.x, qq.y), 0.0) - corner;
    float inside = 1.0 - smoothstep(-px, px, dist);

    vec3 col = vec3(0.0);
    if (inside > 0.0)
    {
        vec2 ut = c * 0.5 + 0.5;
        vec2 ua = (ut - 0.5) * (1.0 - global.LR_OVERSCAN) + 0.5;
        float srcH = params.OriginalSize.y / vTiming.w;   // native lines
        float rpl = 1.0, ph = 0.0;                         // beam rows per line, row of line 0
#ifdef LR_FIELDS
        // Amiga emulators draw every line twice (720x568 for a 256-line game) and weave the
        // two fields of an interlaced screen into the same rows. Identical row pairs are one
        // line: the gaps between lines stay, as on the real monitor. (0.5/255: 8-bit frames.)
        if (interlace > 0.5 && vAmiga > 0.5)
        {
            vec2 fd = texelFetch(LR_Fields, ivec2(0, 0), 0).rg;   // largest pair difference, both pairings
            if (min(fd.x, fd.y) < 0.5 / 255.0)
            {
                interlace = 0.0;
                rpl = 2.0;
                ph = fd.x < 0.5 / 255.0 ? 0.0 : 1.0;
                srcH = floor((srcH - ph) * 0.5);
            }
        }
#endif

        // raster: TV modes sit in a 240/288-line field, VGA and 24 kHz arcade fill the screen
        bool vga = scan > 0.5 && scan < 1.5;
        bool mm = inMM(scan);                              // tube described in mm (PC monitor, 1084S)
        // position along the source line: a 15 kHz monitor in mm shows the whole active line,
        // so a source shorter than the line leaves side borders
        float xs = (ua.x - 0.5) / vShare + 0.5;
        float dbl = (vga && srcH <= 300.0) ? 2.0 : 1.0;
        float BH = srcH * dbl;
        float visLines = scan > 0.5 ? BH : (pal > 0.5 ? 288.0 : 240.0) * (interlace > 0.5 ? 2.0 : 1.0);
        bool il = interlace > 0.5 && global.LR_STEADY < 0.5;
        float stepL = il ? 2.0 : 1.0;
        float offL = il ? float(params.FrameCount % 2u) : 0.0;
        float sgk = (interlace > 0.5 && global.LR_STEADY > 0.5) ? 2.0 : 1.0;

        float Lf = ua.y * visLines - (visLines - BH) * 0.5;
        float u = (Lf - offL - 0.5) / stepL;
        float j0 = floor(u);

        // vertical convergence error grows toward the edges (the horizontal one is in the beam pass)
        float convV = global.LR_CONV * 0.5 * (0.3 + 0.7 * c.y * abs(c.y));
        // spot height in lines: TV in line units; a tube in mm from its line pitch, so the same
        // spot covers more lines in 768-line modes than in 400-line ones, and NTSC lines (fewer,
        // further apart) show more than PAL lines
        vec2 sig = vec2(global.LR_SIG_LO, max(global.LR_SIG_LO, global.LR_SIG_HI)) * sgk;
        if (mm)
            sig = vec2(global.LR_MON_SPOT_LO, max(global.LR_MON_SPOT_LO, global.LR_MON_SPOT_HI))
                * (FWHM_TO_SIGMA * monCornerGrowth(c) * visLines / monScanMM().y);

        // every line within 3.5 spot widths: the lines left out are at least kn + 1 away
        // (4 lines for 240p TV, more for wide spots: merged fields, 1024x768 on a PC monitor)
        int kn = int(clamp(ceil(3.5 * sig.y - 1.0), 1.0, 6.0));
        for (int k = -kn; k <= kn + 1; k++)
        {
            float j = j0 + float(k);
            float i = j * stepL + offL;
            vec4 bb = beamAt(floor(i / dbl) * rpl + ph, xs, srcH * rpl + ph);
            vec3 b = bb.rgb;
            vec3 lc = b * b;                                      // linear brightness
            // beam height: TV from the local brightness per gun; PC from the spot that made the light
            vec3 isg = 1.0 / mix(vec3(sig.x), vec3(sig.y), mm ? vec3(bb.a) : b);
            vec3 d = vec3(u - j) + vec3(convV, 0.0, -convV);
            col += lc * exp(-0.5 * d * d * isg * isg) * isg * 0.39894228;
        }
        col *= smoothstep(-0.002, 0.002, xs) * smoothstep(1.002, 0.998, xs);

        // phosphor mask
        float mt = global.LR_MASK_TYPE;
        float triads = global.LR_MASK_TRIADS;
        // tube in mm: from the mask pitch (a dot mask's pitch is diagonal, 0.866 of it horizontal)
        if (mm) triads = monTubeMM().y / (global.LR_MON_PITCH * (mt > 2.5 && mt < 3.5 ? 0.866 : 1.0));
        float pitchPx = tubePx.y / triads;
        float str = global.LR_MASK_STR * smoothstep(1.6, 2.6, pitchPx);
        if (mt > 0.5 && str > 0.0)
        {
            vec2 mp = ut * tubePx / pitchPx;
            float dp = 0.25 / pitchPx;
            vec3 m = maskAvg(mp - dp, mp + dp, mt);
            float avg = mt < 1.5 ? 0.367 : (mt < 2.5 ? 0.31 : (mt < 3.5 ? 0.19 : 0.84));
            col *= mix(vec3(1.0 - str), vec3(1.0), m) / mix(1.0 - str, 1.0, avg);
        }
        if (mt > 0.5 && mt < 1.5)
        {
            // aperture grille: the two horizontal damper wires
            float hp = tubePx.y;
            float sw = max(0.45, hp / 4000.0);
            float d1 = (ut.y - 1.0 / 3.0) * hp, d2 = (ut.y - 2.0 / 3.0) * hp;
            col *= 1.0 - 0.2 * (exp(-0.5 * d1 * d1 / (sw * sw)) + exp(-0.5 * d2 * d2 / (sw * sw)));
        }

        col *= vWhite;

        // halation: light diffused in the glass (energy-conserving)
        float gy = Lf / BH;
        float gin = smoothstep(-0.03, 0.03, gy) * smoothstep(1.03, 0.97, gy) * smoothstep(-0.03, 0.03, xs) * smoothstep(1.03, 0.97, xs);
        col = mix(col, textureLod(LR_Glow, vec2(xs, gy), 0.0).rgb * vWhite, global.LR_GLOW * gin);

        // the tube's phosphors are not exactly the display's primaries; colours outside
        // the display gamut are clipped in SDR and kept in HDR
        if (vPhos > 1.5) col = phosphorMatrix(vPhos) * col;

#ifdef LR_HDR
        // HDR: the display has headroom above paper white, so bright phosphor dots and
        // scanline centres keep their full brightness; the shoulder only starts near the peak
        float room = max(global.LR_NITS_PEAK / global.LR_NITS_WHITE, 1.0);
        col = room * shoulder(col / room);
#else
        col = shoulder(col);
#endif
        float r2 = clamp(dot(c, c) * 0.5, 0.0, 1.0);
        col *= 1.0 - global.LR_VIGNETTE * pow(r2, 1.6);
        col += global.LR_AMBIENT * global.LR_ROOM * vec3(0.80, 0.92, 0.87);
        col *= inside;
    }
    if (global.LR_BACKDROP > 0.5 && inside < 1.0)
        col += (1.0 - inside) * backdrop(c, max(dist, 0.0), qq, tc, tubePx);

#ifndef LR_NO_FEEDBACK
    vec3 prev = textureLod(LR_CRTFeedback, vTexCoord, 0.0).rgb;
    col = max(col, prev * global.LR_PERSIST);
#endif
    FragColor = vec4(col, 1.0);
}
