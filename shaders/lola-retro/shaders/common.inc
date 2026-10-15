// Lola Retro - shared parameters, uniforms and helpers for every pass.
//
// The chain simulates the whole analog path:
//   console encoder -> cable -> TV decoder -> electron beam -> phosphor mask -> glass.
// Console timing (pixel clock, subcarrier cycles per line) decides how composite
// artefacts look, so it is part of the model.

layout(push_constant) uniform Push
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

#pragma parameter LR_HDR_SRC "--- Source ---" 0.0 0.0 0.0 1.0
#pragma parameter LR_CONSOLE "Console timing (0 auto, see README)" 0.0 0.0 9.0 1.0
#pragma parameter LR_NORM "TV standard (0 auto, 1 NTSC, 2 PAL)" 0.0 0.0 2.0 1.0
#pragma parameter LR_HDR_SIG "--- Signal ---" 0.0 0.0 0.0 1.0
#pragma parameter LR_CABLE "Cable (0 RF, 1 Composite, 2 S-Video, 3 RGB)" 1.0 0.0 3.0 1.0
#pragma parameter LR_COMB "Comb filter (NTSC composite only)" 0.0 0.0 1.0 1.0
#pragma parameter LR_YTRAP "Luma trap in the console (less rainbow)" 0.0 0.0 1.0 0.05
#pragma parameter LR_DBLEND "Dither blend on RGB and S-Video (modern, off on real TVs)" 0.0 0.0 1.0 0.05
#pragma parameter LR_SHARP "Sharpness" 0.35 0.0 1.0 0.01
#pragma parameter LR_SAT "Colour" 1.25 0.0 2.0 0.01
#pragma parameter LR_TINT "Tint (NTSC, degrees)" 0.0 -30.0 30.0 1.0
#pragma parameter LR_DEMOD "Colour decoder (0 auto, 1 standard, 2 US TV, 3 Japanese TV)" 0.0 0.0 3.0 1.0
#pragma parameter LR_BRIGHT "Brightness" 0.0 -0.12 0.12 0.005
#pragma parameter LR_CONTRAST "Contrast" 1.05 0.6 1.4 0.01
#pragma parameter LR_NOISE "RF noise" 0.0 0.0 0.3 0.005
#pragma parameter LR_STEADY "No flicker (frozen dot crawl, merged fields)" 1.0 0.0 1.0 1.0
#pragma parameter LR_HDR_BEAM "--- Beam ---" 0.0 0.0 0.0 1.0
#pragma parameter LR_SIG_LO "Beam width in dark areas" 0.2 0.08 0.4 0.005
#pragma parameter LR_SIG_HI "Beam width on white" 0.44 0.2 0.7 0.005
#pragma parameter LR_HFOCUS "Horizontal focus" 0.5 0.0 1.5 0.01
#pragma parameter LR_GAMMA "Tube gamma" 2.4 2.0 2.8 0.01
#pragma parameter LR_PERSIST "Phosphor afterglow" 0.08 0.0 0.8 0.01
#pragma parameter LR_CONV "Misconvergence (px)" 0.4 0.0 1.5 0.01
#pragma parameter LR_HDR_MASK "--- Mask ---" 0.0 0.0 0.0 1.0
#pragma parameter LR_MASK_TYPE "Mask (0 none, 1 aperture, 2 slot, 3 shadow, 4 projection)" 2.0 0.0 4.0 1.0
#pragma parameter LR_MASK_STR "Mask strength" 0.5 0.0 1.0 0.01
#pragma parameter LR_MASK_TRIADS "Mask triads per screen height" 360.0 150.0 900.0 10.0
#pragma parameter LR_WHITE "White point (K)" 8500.0 6500.0 9300.0 100.0
#pragma parameter LR_PHOS "Phosphors (0 auto, 1 sRGB, 2 SMPTE-C, 3 EBU, 4 Japanese P22)" 0.0 0.0 4.0 1.0
#pragma parameter LR_HDR_GLASS "--- Glass ---" 0.0 0.0 0.0 1.0
#pragma parameter LR_CURV_X "Curvature horizontal" 0.045 0.0 0.15 0.005
#pragma parameter LR_CURV_Y "Curvature vertical" 0.06 0.0 0.15 0.005
#pragma parameter LR_CORNER "Rounded corners" 0.045 0.0 0.1 0.005
#pragma parameter LR_GLOW "Glass glow" 0.06 0.0 0.4 0.01
#pragma parameter LR_GLOW_SIZE "Glass glow size" 0.5 0.2 1.5 0.05
#pragma parameter LR_VIGNETTE "Darker edges" 0.3 0.0 0.7 0.01
#pragma parameter LR_OVERSCAN "Overscan (negative adds a black border)" 0.05 -0.08 0.12 0.005
#pragma parameter LR_AMBIENT "Room reflection on the glass" 0.002 0.0 0.03 0.001
#pragma parameter LR_HDR_PC "--- PC monitor (31 kHz modes) ---" 0.0 0.0 0.0 1.0
#pragma parameter LR_PC_SIZE "Monitor size (inches)" 15.0 14.0 21.0 1.0
#pragma parameter LR_PC_PITCH "Dot pitch (mm)" 0.28 0.20 0.45 0.01
#pragma parameter LR_PC_SPOT_LO "Spot size in dark areas (mm)" 0.30 0.10 0.80 0.01
#pragma parameter LR_PC_SPOT_HI "Spot size on white (mm)" 0.45 0.15 1.00 0.01
#pragma parameter LR_PC_CORNER "Spot size in the corners (x centre)" 1.4 1.0 2.5 0.05

layout(std140, set = 0, binding = 0) uniform UBO
{
    mat4 MVP;
    float LR_HDR_SRC;
    float LR_CONSOLE;
    float LR_NORM;
    float LR_HDR_SIG;
    float LR_CABLE;
    float LR_COMB;
    float LR_YTRAP;
    float LR_DBLEND;
    float LR_SHARP;
    float LR_SAT;
    float LR_TINT;
    float LR_DEMOD;
    float LR_BRIGHT;
    float LR_CONTRAST;
    float LR_NOISE;
    float LR_STEADY;
    float LR_HDR_BEAM;
    float LR_SIG_LO;
    float LR_SIG_HI;
    float LR_HFOCUS;
    float LR_GAMMA;
    float LR_PERSIST;
    float LR_CONV;
    float LR_HDR_MASK;
    float LR_MASK_TYPE;
    float LR_MASK_STR;
    float LR_MASK_TRIADS;
    float LR_WHITE;
    float LR_PHOS;
    float LR_HDR_GLASS;
    float LR_CURV_X;
    float LR_CURV_Y;
    float LR_CORNER;
    float LR_GLOW;
    float LR_GLOW_SIZE;
    float LR_VIGNETTE;
    float LR_OVERSCAN;
    float LR_AMBIENT;
    float LR_HDR_PC;
    float LR_PC_SIZE;
    float LR_PC_PITCH;
    float LR_PC_SPOT_LO;
    float LR_PC_SPOT_HI;
    float LR_PC_CORNER;
#ifdef LR_HDR
    float LR_HDR_OUT;
    float LR_NITS_WHITE;
    float LR_NITS_PEAK;
#endif
} global;

const float TAU = 6.28318530718;

// Dither blend (RGB and S-Video, off by default): composite blended 1-pixel dither
// patterns into new colours or transparency; on RGB they stay visible as stripes.
// The blend finds the part of the picture that alternates from one console pixel to
// the next (a Gaussian window of 1.25 pixels with alternating signs, a narrow notch at
// half the pixel clock) and takes it out. Flat areas, gradients and wider detail are
// left alone. Weights for offsets 0..4 and the normalisation 4 * (w1 + w3):
const float DBW[5] = float[5](1.0, 0.726149, 0.278037, 0.056135, 0.005976);
const float DBN = 3.129136;
float dbSign(int k) { return (abs(k) & 1) == 1 ? -1.0 : 1.0; }

// RF: the echo (multipath ghost) added by the antenna path
const float RF_ECHO = 0.12;      // strength
const float RF_ECHO_US = 0.8;    // delay in microseconds

float hash13(vec3 p3)
{
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

// ---------------------------------------------------------------------------
// Console timing
// ---------------------------------------------------------------------------
// Console ids: 1 SNES/NES, 2 Mega Drive H40, 3 Mega Drive H32, 4 PlayStation,
// 5 PlayStation 2, 6 Amiga, 7 PC VGA, 8 Arcade 15 kHz, 9 Arcade 24 kHz.

float autoConsole(vec2 s)
{
    float w = s.x, h = s.y;
    if ((w == 320.0 && h == 200.0) || (w == 640.0 && (h == 350.0 || h == 400.0))) return 7.0;
    if (h == 256.0 || h == 512.0 || (w == 640.0 && h == 200.0)) return 6.0;
    if (h >= 336.0 && h <= 400.0 && w >= 400.0) return 9.0;
    if (h > 300.0) return 5.0;
    if (w > 420.0) return 4.0;
    if (w <= 300.0) return 1.0;
    if (h >= 232.0) return 4.0;
    return 2.0;
}

// a: pixel clock NTSC, pixel clock PAL (MHz), nominal width, scan mode (0 TV, 1 VGA, 2 fill)
// b: subcarrier phase step per line (NTSC, PAL), per field (NTSC, PAL)
// c: x = 1 when the PAL line count per field is odd (V-switch flips every field)
void consoleTable(float id, out vec4 a, out vec4 b, out float c)
{
    if (id < 1.5)      { a = vec4(5.369318, 5.320342, 256.0, 0.0); b = vec4(1.0 / 3.0, 1.0 / 6.0, 1.0 / 3.0, 0.0);  c = 0.0; }
    else if (id < 2.5) { a = vec4(6.711647, 6.650428, 320.0, 0.0); b = vec4(0.0, 0.0, 0.0, 0.0);                    c = 1.0; }
    else if (id < 3.5) { a = vec4(5.369318, 5.320342, 256.0, 0.0); b = vec4(0.0, 0.0, 0.0, 0.0);                    c = 1.0; }
    else if (id < 4.5) { a = vec4(6.711647, 6.652800, 320.0, 0.0); b = vec4(0.5, 0.75, 0.5, 0.5);                  c = 0.0; }
    else if (id < 5.5) { a = vec4(13.5, 13.5, 640.0, 0.0);        b = vec4(0.5, 0.75, 0.75, 0.875);                c = 0.0; }
    else if (id < 6.5) { a = vec4(7.15909, 7.09379, 320.0, 0.0);  b = vec4(0.5, 0.75, 0.75, 0.875);                c = 0.0; }
    else if (id < 7.5) { a = vec4(12.5875, 12.5875, 320.0, 1.0);  b = vec4(0.5, 0.75, 0.75, 0.875);                c = 0.0; }
    else if (id < 8.5) { a = vec4(6.711647, 6.711647, 320.0, 0.0); b = vec4(0.5, 0.75, 0.5, 0.5);                  c = 0.0; }
    else               { a = vec4(16.0, 16.0, 496.0, 2.0);        b = vec4(0.5, 0.75, 0.75, 0.875);                c = 0.0; }
}

// Internal resolution: an upscaled frame (more than 576 lines, or a console set by hand
// whose native mode is smaller) is read as k x k blocks of one native pixel.
// Returns k (1 for native frames).
float upscaleOf(vec2 orig)
{
    float id = global.LR_CONSOLE;
    bool tv = id < 5.5 || (id > 7.5 && id < 8.5);       // VGA, Amiga and 24 kHz modes are never scaled
    float base = (id > 4.5 && id < 5.5) ? 480.0 : 240.0; // PS2 by hand: 480i class, otherwise 240p class
    bool scaled = id < 0.5 ? orig.y > 576.0 : (tv && orig.y > base * 1.5);
    if (!scaled) return 1.0;
    for (float k = 2.0; k <= 16.0; k += 1.0)
    {
        float lines = orig.y / k;
        if (lines == floor(lines) && lines >= base * 0.83 && lines <= base * 1.21) return k;
    }
    return max(1.0, floor(orig.y / base + 0.5));
}

struct Timing
{
    float pclk;        // pixel clock for this source width (MHz)
    float fsc;         // colour subcarrier (MHz)
    float lineFrac;    // subcarrier phase advance per line (cycles)
    float fieldStep;   // phase advance per field (cycles)
    float framePhase;  // phase offset of the current field (cycles)
    float vsw;         // PAL V-switch offset for this field
    float pal;         // 1 = PAL
    float scan;        // 0 TV raster, 1 VGA line doubling, 2 fill (24 kHz arcade)
    float interlace;   // 1 = 480i/576i source
    float scale;       // internal resolution factor of the core (1 = native)
    float id;          // console timing id (see consoleTable)
};

Timing getTiming(vec2 orig, uint frameCount)
{
    Timing t;
    t.scale = upscaleOf(orig);
    vec2 nat = orig / t.scale;                          // the console's native mode
    float id = global.LR_CONSOLE < 0.5 ? autoConsole(nat) : global.LR_CONSOLE;
    vec4 a; vec4 b; float c;
    consoleTable(id, a, b, c);
    t.id = id;
    if (id > 4.5 && id < 5.5 && nat.x > 700.0) a.z = 720.0;   // PS2 720-wide modes keep 13.5 MHz
    t.scan = a.w;
    t.interlace = (t.scan < 0.5 && nat.y > 300.0) ? 1.0 : 0.0;
    if (global.LR_NORM > 1.5) t.pal = 1.0;
    else if (global.LR_NORM > 0.5) t.pal = 0.0;
    else
    {
        float lines = t.interlace > 0.5 ? nat.y * 0.5 : nat.y;
        t.pal = (t.scan < 0.5 && lines > 240.5) ? 1.0 : 0.0;
    }
    // full source width: a wider (upscaled) line still takes the same time
    t.pclk = mix(a.x, a.y, t.pal) * orig.x / a.z;
    // SVGA modes run faster lines: VESA 75 Hz clocks (800x600 49.5 MHz, 1024x768 78.75 MHz)
    if (t.scan > 0.5 && t.scan < 1.5 && orig.x > 720.0) t.pclk = 78.75 * orig.x * orig.y / (1024.0 * 768.0);
    t.fsc = t.pal > 0.5 ? 4.433619 : 3.579545;
    t.lineFrac = mix(b.x, b.y, t.pal);
    t.fieldStep = mix(b.z, b.w, t.pal);
    // 24 is a multiple of every field cycle above (1, 2, 3, 4, 8), so the phase never drifts
    float fc = global.LR_STEADY > 0.5 ? 0.0 : float(frameCount % 24u);
    t.framePhase = fract(fc * t.fieldStep);
    t.vsw = mod(fc * c, 2.0);
    return t;
}

// y is a source row; rows of one upscaled block belong to the same native line
float nativeLine(float y, Timing t) { return floor(y / t.scale); }
float physLine(float y, Timing t) { float l = nativeLine(y, t); return t.interlace > 0.5 ? floor(l * 0.5) : l; }
bool oddField(float y, Timing t) { return t.interlace > 0.5 && mod(nativeLine(y, t), 2.0) > 0.5; }

// Subcarrier phase at time tUs (microseconds from the start of the active line) on line y
float phaseAt(float tUs, float y, Timing t)
{
    float fo = oddField(y, t) ? t.fieldStep : 0.0;
    return TAU * fract(fract(t.fsc * tUs) + fract(t.lineFrac * physLine(y, t)) + fo + t.framePhase);
}

// PAL alternates the sign of V on every line
float vSign(float y, Timing t)
{
    if (t.pal < 0.5) return 1.0;
    float pl = physLine(y, t) + t.vsw + (oddField(y, t) ? 1.0 : 0.0);
    return mod(pl, 2.0) < 0.5 ? 1.0 : -1.0;
}

// ---------------------------------------------------------------------------
// PC monitor (31 kHz modes): the tube is described in millimetres, so every video mode
// (320x200 doubled to 400 lines, 640x480, 800x600, 1024x768) follows from the same tube.
// Typical mid-90s 15-inch monitor: 13.8-14 inch viewable, 0.28 mm dot pitch, a spot of
// about 0.45 mm on white (sharp at 800x600, soft at 1024x768), dynamic focus keeping the
// corners below 1.5 x the centre spot.
// ---------------------------------------------------------------------------

const float FWHM_TO_SIGMA = 0.4246609;   // Gaussian: sigma = FWHM / (2 sqrt(2 ln 2))

bool isPC(float scan) { return scan > 0.5 && scan < 1.5; }

// Viewable tube face in mm: one inch less than the nominal size, 4:3
vec2 pcTubeMM()
{
    float d = (global.LR_PC_SIZE - 1.0) * 25.4;
    return vec2(0.8 * d, 0.6 * d);
}

// The picture is smaller than the tube face when overscan is negative (black border)
vec2 pcPictureMM() { return pcTubeMM() / (1.0 - global.LR_OVERSCAN); }

// The spot grows toward the corners (deflection defocus); c is the position on the tube, -1..1
float pcCornerGrowth(vec2 c) { return 1.0 + (global.LR_PC_CORNER - 1.0) * 0.5 * dot(c, c); }

// Standard normal density and cumulative distribution
// (erf after Abramowitz & Stegun 7.1.26, error below 1.5e-7)
vec3 gaussPdf(vec3 z) { return 0.39894228 * exp(-0.5 * z * z); }
vec3 gaussCdf(vec3 z)
{
    vec3 x = abs(z) * 0.70710678;
    vec3 t = 1.0 / (1.0 + 0.3275911 * x);
    vec3 p = ((((1.061405429 * t - 1.453152027) * t + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t;
    return 0.5 + 0.5 * sign(z) * (1.0 - p * exp(-x * x));
}

// ---------------------------------------------------------------------------
// Colour of the TV: its chroma decoder and its phosphors
// ---------------------------------------------------------------------------

// Chroma decoder: 1 = by the standard. American and Japanese TVs demodulated R-Y and G-Y
// on shifted axes with their own gains ("red push", warmer skin tones). PAL TVs, broadcast
// monitors and RGB inputs use the standard axes.
float decoderType(Timing t, float cable)
{
    if (global.LR_DEMOD > 0.5) return global.LR_DEMOD;
    return (t.pal < 0.5 && cable < 2.5) ? 2.0 : 1.0;
}

// R, G, B from Y and the demodulated U, V (U = 0.492 (B-Y), V = 0.877 (R-Y)).
// Consumer axes and gains from the Sony CXA2025AS datasheet, relative to B-Y:
// US mode R-Y 112 deg x0.83, G-Y 252 deg x0.30; Japanese mode R-Y 95 deg x0.78, G-Y 240 deg x0.30.
vec3 decodeRGB(float Y, vec2 uv, float dec)
{
    if (dec < 1.5)
        return vec3(Y + 1.13983 * uv.y, Y - 0.39465 * uv.x - 0.58060 * uv.y, Y + 2.03211 * uv.x);
    bool us = dec < 2.5;
    float ar = radians(us ? 112.0 : 95.0), gr = us ? 0.83 : 0.78;
    float ag = radians(us ? 252.0 : 240.0), gg = 0.30;
    float ry = (uv.x * cos(ar) + uv.y * sin(ar)) * gr / 0.492;
    float gy = (uv.x * cos(ag) + uv.y * sin(ag)) * gg / 0.492;
    return vec3(Y + ry, Y + gy, Y + uv.x / 0.492);
}

// Phosphors: 1 sRGB / Rec.709, 2 SMPTE-C (NTSC sets), 3 EBU (PAL sets, broadcast monitors),
// 4 Japanese P22 (Sony Trinitron and Japanese sets, arcade monitors).
float phosphorSet(Timing t)
{
    if (global.LR_PHOS > 0.5) return global.LR_PHOS;
    if (t.scan > 0.5 && t.scan < 1.5) return 1.0;   // PC monitors: sRGB was defined from them
    if (t.id > 7.5) return 4.0;                       // arcade monitors
    return t.pal > 0.5 ? 3.0 : 2.0;
}

// Linear phosphor RGB -> linear Rec.709 RGB, white (D65) kept. Primaries (x, y):
// SMPTE-C R .630 .340 G .310 .595 B .155 .070; EBU R .640 .330 G .290 .600 B .150 .060;
// Japanese P22 R .625 .350 G .280 .605 B .152 .062 (Sony KV-20M20 and others, via Dogway's grade).
mat3 phosphorMatrix(float p)
{
    if (p < 2.5) return mat3(0.939542, 0.017772, -0.001622, 0.050181, 0.965793, -0.004370, 0.010277, 0.016435, 1.005991);
    if (p < 3.5) return mat3(1.044043, 0.0, 0.0, -0.044043, 1.0, 0.011793, 0.0, 0.0, 0.988207);
    return mat3(1.081942, 0.038032, -0.007481, -0.088427, 0.959991, 0.015170, 0.006485, 0.001977, 0.992311);
}
