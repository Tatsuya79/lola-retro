#version 450
// Lola Retro - pass 5: horizontal beam profile of every source line.
// The spot shape, misconvergence and tube gamma only depend on the position along a
// line, so they are worked out here once per line, on a fine grid (the pass is 8192
// samples wide), instead of four times for every screen pixel in the CRT pass.
// Output: one row per native line, holding the square root of the beam's linear brightness.

#include "common.inc"

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
layout(location = 1) flat out float vScale;

void main()
{
    gl_Position = global.MVP * Position;
    vTexCoord = TexCoord;
    vScale = getTiming(params.OriginalSize.xy, params.FrameCount).scale;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 1) flat in float vScale;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 3) uniform sampler2D LR_Decoded;

void main()
{
    // one row per native line; the rows past the last line are spare
    float i = floor(vTexCoord.y * params.OutputSize.y);
    if (i >= params.OriginalSize.y / vScale)
    {
        FragColor = vec4(0.0);
        return;
    }
    // sample m sits at x = m / width (the CRT pass reads it with the same half-texel offset)
    float x = vTexCoord.x - 0.5 * params.OutputSize.z;

    // RF: every line lands slightly off (unstable sync)
    float jitter = global.LR_CABLE < 0.5 ? 0.5 + global.LR_NOISE * 4.0 : 0.0;
    float seed = float(params.FrameCount % 997u);
    float jit = jitter > 0.0 ? (hash13(vec3(i, seed, 5.0)) - 0.5) * jitter * params.OriginalSize.z : 0.0;
    vec2 tc = vec2(x + jit, (i * vScale + 0.5) * params.OriginalSize.w);   // first row of the line

    // Horizontal spot: like the vertical one, a Gaussian whose width grows with brightness,
    // so bright detail spreads and rounds off while dark detail stays sharp.
    // Every point along the line spreads its own light with its own width (energy is kept).
    // Units: 1 = one pixel of a 320-wide mode, the same physical size as one TV line;
    // Horizontal focus 0.5 makes the spot round.
    float f = 2.0 * max(global.LR_HFOCUS, 0.05) / 320.0;
    float sLo = global.LR_SIG_LO * f;
    float sHi = max(global.LR_SIG_LO, global.LR_SIG_HI) * f;
    float dt = sLo;                                        // tap spacing (a Gaussian sampled at 1 sigma still sums exactly)
    int nt = int(min(ceil(3.0 * sHi / dt), 16.0));
    vec3 acc = vec3(0.0), dacc = vec3(0.0);
    for (int k = -nt; k <= nt; k++)
    {
        float o = float(k) * dt;                           // tap position minus x
        vec3 b = pow(clamp(textureLod(LR_Decoded, tc + vec2(o, 0.0), 0.0).rgb, 0.0, 1.0), vec3(0.5 * global.LR_GAMMA));
        vec3 is = 1.0 / mix(vec3(sLo), vec3(sHi), b);      // 1 / spot width of that point
        vec3 g = exp(-0.5 * o * o * is * is) * is * (0.39894228 * dt);
        acc += b * b * g;                                  // linear light
        dacc += b * b * g * o * is * is;                   // derivative along x, for convergence
    }

    // misconvergence: red and blue land a fraction of a pixel off green, more toward the edges
    float cx = (x - 0.5) * 2.0 / (1.0 - global.LR_OVERSCAN);   // position on the tube face, -1..1
    float conv = clamp(global.LR_CONV * (0.35 + 0.65 * cx * abs(cx)) * params.OriginalSize.x / 320.0, -1.0, 1.0);
    float sh = conv * params.OriginalSize.z;
    acc.r -= dacc.r * sh;
    acc.b += dacc.b * sh;

    // the square root is stored because the CRT pass needs both it and its square
    FragColor = vec4(sqrt(max(acc, 0.0)), 1.0);
}
