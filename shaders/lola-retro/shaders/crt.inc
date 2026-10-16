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

// A surface turned by phi radians from facing the viewer, toward dir (z toward the viewer)
vec3 tilt(vec2 dir, float phi) { return vec3(dir * sin(phi), cos(phi)); }

// Plastic lit by the room. Diffuse: light from above, a little from the right and in front (as in
// the photos of the real monitors), plus some from the rest of the room; 1 on a surface facing
// the viewer. Sheen of a satin finish (gloss 0.5: 4% reflection head-on, more at grazing angles;
// 1 is twice that and sharper), seen from 60 cm in front of the tube: the room reflected, blurred
// (a light ceiling above, a darker desk and floor below, so faces and edges turned up look
// lighter), and the ceiling lamp, 1.2 m above the monitor, in front of it and a little to the
// right, as a soft highlight. P: position in mm (y up, z toward the viewer), n: which way the
// surface faces, occ: how much of the room's light reaches it.
vec3 plastic(vec3 albedo, vec3 n, vec3 P, vec3 light, float occ, float gloss)
{
    const vec3 L = vec3(0.3, 0.45, 0.84);
    float diffuse = 0.45 + 0.55 * max(dot(n, L), 0.0) / 0.84;
    vec3 col = albedo * diffuse;
    if (gloss > 0.0)
    {
        vec3 v = normalize(vec3(0.0, 0.0, 600.0) - P);
        vec3 r = reflect(-v, n);
        float F = 0.04 + 0.96 * pow(1.0 - max(dot(v, n), 0.0), 5.0);
        float room = 0.45 * (0.4 + 2.3 * smoothstep(-0.3, 0.7, r.y));
        vec3 l = normalize(vec3(250.0, 1200.0, 900.0) - P);
        float s = mix(12.0, 80.0, gloss);
        float lamp = 0.55 / 0.84 * max(dot(n, l), 0.0) * (s + 8.0) / 8.0 * pow(max(dot(n, normalize(l + v)), 0.0), s);
        col += 2.0 * gloss * F * (room + lamp);
    }
    return light * occ * col;
}

// Signed distance to a pill (a horizontal capsule): straight part 2h long, radius r
float pill(vec2 q, float h, float r)
{
    q.x = max(abs(q.x) - h, 0.0);
    return length(q) - r;
}

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

    // The casing, measured on photos of real monitors.
    // Amiga monitor (Commodore M1438S): warm off-white, a thick flat front, a 12.5 mm tunnel with
    //   mitred corners down to a thin black mask at the glass, a plain chin, a green light.
    // PC monitor (Compaq 151FS): aged cream, a wide front, a raised rim around a shallower
    //   recess, a blank plaque on the chin, a control strip below with buttons and a green light.
    // TV: the PC shape in dark plastic.
    // Margins from the glass to the outline (side, top, bottom), recess width (with the mask),
    // mask width, the shell band around the front.
    bool tv = !inMM(vTiming.x);
    bool amiga = !tv && global.LR_MON_MM > 0.5;
    bool pc = !tv && !amiga;
    vec3 m = amiga ? vec3(41.0, 41.0, 48.0) : pc ? vec3(44.0, 46.0, 39.0) : vec3(50.0, 42.0, 80.0);
    float rw = amiga ? 12.5 : 12.0, mw = 1.2, sh = amiga ? 4.0 : 3.0;
    vec3 albedo = amiga ? vec3(0.60, 0.56, 0.44) : pc ? vec3(0.66, 0.57, 0.33) : vec3(0.035, 0.035, 0.038);

    // the light: the room's (Room light: 1 a dim room with warm lamps, 20 daylight, whiter and
    // more even), brightest near the monitor; in space a faint cold light
    float day = smoothstep(1.0, 20.0, global.LR_ROOM);
    float near = mix(1.0 / (1.0 + dot(p, p) / (260.0 * 260.0)), 1.0, 0.6 * day);
    vec3 light = inSpace ? vec3(0.005, 0.0055, 0.007)
               : mix(vec3(1.0, 0.93, 0.82), vec3(1.0), day) * (0.0167 * global.LR_ROOM * near);

    vec2 inner = 0.5 * tube;
    float d = dist * mix(1.0, tubePx.x / tubePx.y, smoothstep(-0.05, 0.05, qq.x - qq.y)) * 0.5 * tube.y;   // mm from the glass
    vec2 hs = inner + vec2(m.x, 0.5 * (m.y + m.z));               // half size of the outline
    vec2 at = p - vec2(0.0, 0.5 * (m.y - m.z));                   // from the centre of the outline
    float s = roundBox(at, hs, 7.0);                              // < 0 on the casing
    float u = -roundBox(p, inner + rw, amiga ? 2.0 : 6.0);        // > 0 in the recess
    float rec = smoothstep(-aa, aa, u);

    // Which way the surface faces. Away from the glass: rounded corners, mitred on the Amiga.
    vec2 nIn = boxOut(p, inner, 6.0);
    if (amiga)
    {
        vec2 e = abs(p) - inner;
        nIn = normalize(mix(vec2(0.0, sign(p.y)), vec2(sign(p.x), 0.0), smoothstep(-aa, aa, e.x - e.y)));
    }
    float phi;                                                    // < 0 toward the glass, > 0 outward
    vec2 dir = nIn;
    if (u > -5.0)
    {
        // a rounded lip, then the walls down to the glass: steep on the Amiga monitor (55 degrees),
        // shallower on the PC monitor and the TV (45 degrees)
        phi = (amiga ? -0.96 : -0.79) * smoothstep(-0.6, 1.2, u);
    }
    else
    {
        // the front's edge rolls over to a seam, then the casing's shell rolls away
        dir = boxOut(at, hs, 7.0);
        phi = s < -sh ? 0.87 * smoothstep(-sh - 2.0, -sh, s) : mix(0.44, 1.4, smoothstep(-1.5, 0.0, s));
    }
    vec3 n = tilt(dir, phi);
    if (pc)              // the top of the front slopes back a little
        n = normalize(n + vec3(0.0, 0.25 * smoothstep(inner.y + rw + 12.0, inner.y + m.y - sh, p.y), 0.0));

    // less of the room's light gets into the recess, least near the glass; the seam is dark;
    // the mask next to the glass is black
    float occ = (1.0 - rec * (0.05 + 0.15 * smoothstep(0.0, rw, u)))
              * (1.0 - 0.6 * exp(-0.5 * (s + sh) * (s + sh) / max(0.16, aa * aa)));
    vec3 alb = albedo * mix(1.0, 0.92, rec);
    if (!tv)             // the plastic has aged a little unevenly
        alb *= 1.0 - (pc ? 0.06 : 0.03) * valueNoise(p / 30.0 + 7.0);
    float gloss = 0.5;
    alb = mix(alb, vec3(0.02), 1.0 - smoothstep(mw - aa, mw + aa, d));

    vec3 lamp = vec3(0.0);                                        // the power light
    vec3 stripCol = vec3(0.0);
    float strip = 0.0;
    float yb = -inner.y - m.z;                                    // bottom of the outline
    if (pc)
    {
        // a blank pill-shaped plaque on the chin, left: a grey raised rim around a light face
        vec2 pq = p - vec2(-0.406 * tube.x, -inner.y - rw - 12.0);
        float pd = pill(pq, 20.5, 7.5);
        if (pd < 1.5)
        {
            vec2 pdir = normalize(vec2(sign(pq.x) * max(abs(pq.x) - 20.5, 0.0), pq.y) + 1e-4);
            float on = 1.0 - smoothstep(-aa, aa, pd);
            float rim = on * smoothstep(-1.5 - aa, -1.5 + aa, pd);
            n = normalize(mix(n, tilt(pdir, 0.6), rim));
            alb = mix(alb, mix(vec3(0.86, 0.84, 0.79), vec3(0.55, 0.55, 0.56), rim), on);
            gloss = mix(gloss, min(gloss + 0.3, 1.0), on);   // the plaque is glossier
            occ *= 1.0 - 0.35 * (1.0 - on) * (1.0 - smoothstep(0.0, 1.5, pd)) * max(-pdir.y, 0.0);   // its shadow below
        }
        // grime: small brown specks in a patch on the chin, right of the middle
        float blob = exp(-pow((p.x - 0.12 * tube.x) / (0.14 * tube.x), 2.0) - pow((p.y + inner.y + rw + 10.0) / 9.0, 2.0));
        vec2 g = p / 0.8, gi = floor(g);
        float gh = hash13(vec3(gi, 31.0));
        if (gh > 1.0 - 0.3 * blob)
        {
            vec2 gc = gi + 0.25 + 0.5 * vec2(hash13(vec3(gi, 32.0)), hash13(vec3(gi, 33.0)));
            float gr = 0.15 + 0.2 * fract(gh * 17.0);
            float cov = clamp((gr - length(g - gc) * 0.8) / aa + 0.5, 0.0, 1.0) * min(1.0, 2.0 * gr / aa);
            alb *= mix(vec3(1.0), vec3(0.6, 0.52, 0.42), cov);
        }
        alb *= 1.0 - 0.08 * blob * valueNoise(p / 3.0 + 3.0);   // and faint smudges around them

        // the control strip, a separate part under the front: a recessed panel in the middle,
        // the power light and two buttons on the right, seams between the parts
        vec2 sp = p - vec2(0.0, yb - 6.0);
        strip = 1.0 - smoothstep(-aa, aa, roundBox(sp, vec2(110.5, 7.0), 2.0));
        if (strip > 0.0)
        {
            float sphi = 0.0;
            vec2 sdir = vec2(0.0, 1.0);
            float pan = roundBox(sp - vec2(8.5, -0.5), vec2(50.5, 4.5), 1.0);
            if (pan < 0.0)
            {
                sdir = boxOut(sp - vec2(8.5, -0.5), vec2(50.5, 4.5), 1.0);
                sphi = -0.8 * smoothstep(-1.0, 0.0, pan);         // its walls face inward
            }
            for (int k = 0; k < 2; k++)
            {
                vec2 bc = vec2(k == 0 ? 85.5 : 102.25, -0.5);
                vec2 bh = vec2(k == 0 ? 7.5 : 7.25, 4.5);
                float bd = roundBox(sp - bc, bh, 0.8);
                if (bd < 0.0)
                {
                    sdir = boxOut(sp - bc, bh, 0.8);
                    sphi = 0.6 * smoothstep(-0.8, 0.0, bd);        // bevelled buttons
                }
            }
            float seam = 0.0;
            for (int k = 0; k < 4; k++)
            {
                float x0 = k == 0 ? -42.0 : k == 1 ? 59.0 : k == 2 ? 77.0 : 94.0;
                seam = max(seam, exp(-0.5 * (sp.x - x0) * (sp.x - x0) / max(0.09, aa * aa)));
            }
            float socc = mix(0.7, 1.0, smoothstep(5.0, 2.5, sp.y))    // in the shadow of the front above
                       * (1.0 - 0.6 * seam);
            stripCol = plastic(albedo * 0.97, tilt(sdir, sphi), vec3(p, 0.0), light, socc, 0.5);
            float ld = roundBox(sp - vec2(70.0, -0.5), vec2(1.2, 0.6), 0.3);
            lamp += vec3(0.12, 1.0, 0.2) * (0.6 * (1.0 - smoothstep(-aa, aa, ld)) + 0.03 * exp(-0.5 * max(ld, 0.0) * max(ld, 0.0) / 9.0)) * strip;
        }
    }
    else
    {
        // the power light: the Amiga's under the front, right of the middle; the TV's on its chin
        vec2 lp = amiga ? vec2(0.356 * tube.x, yb + 2.2) : vec2(inner.x + m.x - 30.0, yb + 14.0);
        float ld = roundBox(p - lp, vec2(1.3, 0.6), 0.4);
        lamp += vec3(0.12, 1.0, 0.2) * (0.6 * (1.0 - smoothstep(-aa, aa, ld)) + 0.03 * exp(-0.5 * max(ld, 0.0) * max(ld, 0.0) / 9.0));
    }

    // The screen lights the walls of the recess, which face the picture beside them, so they
    // change with it (Screen light on the casing). A point on a wall z mm in front of the phosphor
    // (which sits about 10 mm behind the front of the glass) sees the picture from about 0.7 z to
    // 3 z beyond the edge; a Lambert screen of luminance L gives it 0.67 L (the Amiga's 55-degree
    // walls) or 0.46 L (45 degrees) of illuminance, so the wall shines at 0.21 or 0.15 of L times
    // its albedo. On these slanted paths the tinted glass lets about 30% out, and the dot mask
    // and the lines leave about 70% of the picture's light: 0.045 and 0.03. Plastic scatters it,
    // it does not mirror the picture. Six samples of the glowing picture over that region.
    vec3 pic = vec3(0.0);
    float facing = smoothstep(-0.6, 1.2, u) * global.LR_SPILL;
    if (facing > 0.0)
    {
        float ov = 1.0 - global.LR_OVERSCAN;
        vec2 edge = clamp(c * 0.5 * ov + 0.5, 0.0, 1.0);          // the tube's edge on the picture
        vec2 nt = boxOut((tc - 0.5) * tube, inner, 15.0);         // outward, in texture orientation
        vec2 inward = -nt / tube * ov, side = vec2(-nt.y, nt.x) / tube * ov;   // per mm
        float z = (amiga ? 1.43 : 1.0) * max(d - mw, 0.0) + 10.0;
        vec3 gl = vec3(0.0);
        for (int i = 0; i < 2; i++)
            for (int j = -1; j <= 1; j++)
            {
                vec2 q = edge + inward * ((i == 0 ? 0.9 : 2.4) * z) + side * (1.3 * float(j) * z);
                gl += (j == 0 ? 0.25 : 0.125) * textureLod(LR_Glow, clamp(q, 0.0, 1.0), 0.0).rgb;
            }
        pic = gl * vWhite * (amiga ? 0.045 : 0.03) * facing;
    }

    vec3 casing = plastic(alb, n, vec3(p, 0.0), light, occ, gloss) + alb * pic;
    vec3 behind = inSpace ? space((vTexCoord - 0.5) * params.OutputSize.xy * params.OutputSize.w,
                                  float(params.FrameCount % 1000000u) / 60.0, params.OutputSize.w)
                          : vec3(0.20, 0.195, 0.18) * light;   // the wall, further from the light
    behind = mix(behind, stripCol, strip);
    return mix(behind, casing, 1.0 - smoothstep(-aa, aa, s)) + lamp;
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
