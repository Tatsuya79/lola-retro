#version 450
// Lola Retro - pass 3: glass glow (halation), horizontal blur and downsample.
// Works in linear light so bright areas spread the right amount of light.

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
layout(set = 0, binding = 2) uniform sampler2D Source;

void main()
{
    float dx = params.OutputSize.z;
    vec2 tc = vTexCoord;
    if (vScale > 1.5)
    {
        // upscaled frame: only the first row of each native line is decoded
        float lines = params.SourceSize.y / vScale;
        tc.y = (floor(tc.y * lines) * vScale + 0.5) * params.SourceSize.w;
    }
    vec3 acc = vec3(0.0);
    float ws = 0.0;
    for (int k = -6; k <= 6; k++)
    {
        float w = exp(-0.5 * float(k * k) / 9.0);
        vec2 p = tc + vec2(float(k) * dx, 0.0);
        vec3 c = 0.5 * (textureLod(Source, p - vec2(0.25 * dx, 0.0), 0.0).rgb + textureLod(Source, p + vec2(0.25 * dx, 0.0), 0.0).rgb);
        acc += w * pow(c, vec3(global.LR_GAMMA));
        ws += w;
    }
    FragColor = vec4(acc / ws, 1.0);
}
