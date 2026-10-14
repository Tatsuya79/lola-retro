#version 450
// Lola Retro - pass 1: console video encoder.
// RGB -> YUV, chroma band-limited and QAM-modulated onto the colour subcarrier.
// Output: R = composite (or luma for S-Video), G = chroma (S-Video only).
// The output is 4 samples per source pixel.

#include "common.inc"

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main()
{
    gl_Position = global.MVP * Position;
    vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

// Source pixel at x position xs on the native line that starts at row y.
// An upscaled frame has k rows per native line; they are averaged.
vec3 srcPix(float xs, float y, float k)
{
    if (xs < 0.0 || xs >= params.SourceSize.x) return vec3(0.0);
    float u = (floor(xs) + 0.5) * params.SourceSize.z;
    vec3 c = vec3(0.0);
    for (float r = 0.0; r < k; r += 1.0)
        c += texture(Source, vec2(u, (y + r + 0.5) * params.SourceSize.w)).rgb;
    return c / k;
}

// Luma and modulated chroma at sample position n (in samples)
vec2 enc(float n, float y, float fs, Timing T)
{
    float ppx = T.pclk / fs;                 // source pixels per sample
    vec3 c = srcPix((n + 0.5) * ppx, y, T.scale);
    float Y = dot(c, vec3(0.299, 0.587, 0.114));

    // the encoder limits chroma to about 1.5 MHz before modulation
    float sigUs = 0.1325 / 1.5;
    vec2 uv = vec2(0.0);
    float ws = 0.0;
    for (int k = -8; k <= 8; k++)
    {
        float u = float(k) * 0.4;            // in sigmas, covers +-3.2
        float w = exp(-0.5 * u * u);
        vec3 cc = srcPix((n + 0.5) * ppx + u * sigUs * T.pclk, y, T.scale);
        float yy = dot(cc, vec3(0.299, 0.587, 0.114));
        uv += w * vec2(0.492 * (cc.b - yy), 0.877 * (cc.r - yy));
        ws += w;
    }
    uv /= ws;

    float ph = phaseAt(n / fs, y, T);
    return vec2(Y, uv.x * sin(ph) + vSign(y, T) * uv.y * cos(ph));
}

void main()
{
    Timing T = getTiming(params.OriginalSize.xy, params.FrameCount);
    float SW = params.OutputSize.x;
    float fs = T.pclk * SW / params.SourceSize.x;     // sample rate (MHz)
    float n = floor(vTexCoord.x * SW);
    float y = floor(vTexCoord.y * params.SourceSize.y);
    float cable = global.LR_CABLE;

    // RGB: no encoding, the decoder reads the original frame.
    // Upscaled frame: only the first row of each native line carries the signal.
    if (cable > 2.5 || mod(y, T.scale) > 0.5)
    {
        FragColor = vec4(0.0);
        return;
    }

    vec2 a = enc(n, y, fs, T);
    vec2 r;
    if (cable < 0.5)
    {
        // RF: composite plus an echo and snow
        float comp = a.x + a.y;
        vec2 g = enc(n - 0.8 * fs, y, fs, T);
        comp += 0.12 * (g.x + g.y);
        float seed = float(params.FrameCount % 997u);
        comp += (hash13(vec3(n, y, seed)) - 0.5) * global.LR_NOISE;
        comp += (hash13(vec3(floor(n / 7.0), y, seed + 3.0)) - 0.5) * global.LR_NOISE * 0.8;
        comp += sin(nativeLine(y, T) * 0.045 + seed * 0.37) * global.LR_NOISE * 0.12;
        r = vec2(comp, 0.0);
    }
    else if (cable < 1.5)
    {
        r = vec2(a.x + a.y, 0.0);
    }
    else
    {
        r = a;   // S-Video: luma and chroma on separate wires
    }
    FragColor = vec4(r, 0.0, 1.0);
}
