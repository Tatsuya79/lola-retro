#version 450
// Lola Retro - pass 6: electron beam, phosphor mask and glass, at screen resolution.
// - Each source line is a beam with a Gaussian profile whose width grows with brightness
//   (per colour gun), so bright lines merge and dark lines show gaps.
// - Interlaced sources alternate fields; VGA 200-line modes are scanned twice.
// - Mask: aperture grille (with damper wires), slot mask, shadow mask or a projection screen.
// - Glass: curvature, halation, white point, vignette, room reflection, phosphor afterglow.

#include "common.inc"

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
layout(location = 1) flat out vec3 vWhite;
layout(location = 2) flat out vec4 vTiming;   // scan mode, PAL, interlaced, internal resolution

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
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 1) flat in vec3 vWhite;
layout(location = 2) flat in vec4 vTiming;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D LR_Beam;
layout(set = 0, binding = 3) uniform sampler2D LR_Glow;
layout(set = 0, binding = 4) uniform sampler2D LR_CRTFeedback;

// Beam of source line i at position x along the line (from the beam pass):
// square root of its linear brightness, 0 outside the picture
vec3 beamAt(float i, float x, float lines)
{
    if (i < 0.0 || i >= lines) return vec3(0.0);
    vec2 bs = vec2(textureSize(LR_Beam, 0));
    return textureLod(LR_Beam, vec2(x + 0.5 / bs.x, (i + 0.5) / bs.y), 0.0).rgb;
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

void main()
{
    float scan = vTiming.x, pal = vTiming.y, interlace = vTiming.z;

    // tube geometry: barrel curvature and rounded corners
    vec2 c = vTexCoord * 2.0 - 1.0;
    c *= 1.0 + vec2(global.LR_CURV_X, global.LR_CURV_Y) * (c.yx * c.yx);
    float corner = global.LR_CORNER;
    vec2 qq = abs(c) - 1.0 + corner;
    float dist = length(max(qq, 0.0)) + min(max(qq.x, qq.y), 0.0) - corner;
    float px = 2.0 * params.OutputSize.z;
    float inside = 1.0 - smoothstep(-px, px, dist);

    vec3 col = vec3(0.0);
    if (inside > 0.0)
    {
        vec2 ut = c * 0.5 + 0.5;
        vec2 ua = (ut - 0.5) * (1.0 - global.LR_OVERSCAN) + 0.5;
        float srcH = params.OriginalSize.y / vTiming.w;   // native lines

        // raster: TV modes sit in a 240/288-line field, VGA and 24 kHz arcade fill the screen
        bool vga = scan > 0.5 && scan < 1.5;
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
        vec2 sig = vec2(global.LR_SIG_LO, max(global.LR_SIG_LO, global.LR_SIG_HI)) * sgk;

        for (int k = -1; k <= 2; k++)
        {
            float j = j0 + float(k);
            float i = j * stepL + offL;
            vec3 b = beamAt(floor(i / dbl), ua.x, srcH);
            vec3 lc = b * b;                                      // linear brightness
            vec3 isg = 1.0 / mix(vec3(sig.x), vec3(sig.y), b);    // 1 / beam width
            vec3 d = vec3(u - j) + vec3(convV, 0.0, -convV);
            col += lc * exp(-0.5 * d * d * isg * isg) * isg * 0.39894228;
        }
        col *= smoothstep(-0.002, 0.002, ua.x) * smoothstep(1.002, 0.998, ua.x);

        // phosphor mask
        float pitchPx = params.OutputSize.y / global.LR_MASK_TRIADS;
        float mt = global.LR_MASK_TYPE;
        float str = global.LR_MASK_STR * smoothstep(1.6, 2.6, pitchPx);
        if (mt > 0.5 && str > 0.0)
        {
            vec2 mp = ut * params.OutputSize.xy / pitchPx;
            float dp = 0.25 / pitchPx;
            vec3 m = maskAvg(mp - dp, mp + dp, mt);
            float avg = mt < 1.5 ? 0.367 : (mt < 2.5 ? 0.31 : (mt < 3.5 ? 0.19 : 0.84));
            col *= mix(vec3(1.0 - str), vec3(1.0), m) / mix(1.0 - str, 1.0, avg);
        }
        if (mt > 0.5 && mt < 1.5)
        {
            // aperture grille: the two horizontal damper wires
            float hp = params.OutputSize.y;
            float sw = max(0.45, hp / 4000.0);
            float d1 = (ut.y - 1.0 / 3.0) * hp, d2 = (ut.y - 2.0 / 3.0) * hp;
            col *= 1.0 - 0.2 * (exp(-0.5 * d1 * d1 / (sw * sw)) + exp(-0.5 * d2 * d2 / (sw * sw)));
        }

        col *= vWhite;

        // halation: light diffused in the glass (energy-conserving)
        float gy = Lf / BH;
        float gin = smoothstep(-0.03, 0.03, gy) * smoothstep(1.03, 0.97, gy) * smoothstep(-0.03, 0.03, ua.x) * smoothstep(1.03, 0.97, ua.x);
        col = mix(col, textureLod(LR_Glow, vec2(ua.x, gy), 0.0).rgb * vWhite, global.LR_GLOW * gin);

        col = shoulder(col);
        float r2 = clamp(dot(c, c) * 0.5, 0.0, 1.0);
        col *= 1.0 - global.LR_VIGNETTE * pow(r2, 1.6);
        col += global.LR_AMBIENT * vec3(0.80, 0.92, 0.87);
        col *= inside;
    }

    vec3 prev = textureLod(LR_CRTFeedback, vTexCoord, 0.0).rgb;
    col = max(col, prev * global.LR_PERSIST);
    FragColor = vec4(col, 1.0);
}
