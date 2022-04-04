#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "../common.h"
#include "../perlin.glsl"



layout(push_constant) uniform params_t
{
    mat4 mProj;
    mat4 mView;
} params;

layout(binding = 0, set = 0) uniform AppData
{
    UniformParams Params;
};

layout(binding = 1, set = 0) uniform sampler2D inColor;
layout(binding = 2, set = 0) uniform sampler2D inFog;

// For compat with quad3_vert
layout (location = 0 ) in FS_IN { vec2 texCoord; } vIn;

layout(location = 0) out vec4 out_fragColor;


void main()
{
    const vec2 fragPos = gl_FragCoord.xy / vec2(Params.screenWidth, Params.screenHeight);
    const vec2 preDelta =
        1.f / vec2(Params.screenWidth/Params.postFxDownscaleFactor, Params.screenHeight/Params.postFxDownscaleFactor);

    const vec4 color = textureLod(inColor, fragPos, 0);
    
    const int blurRad = 2;
    vec4 fog = vec4(0);
    float normCoeff = 0;
    for (int i = -blurRad; i <= blurRad; ++i)
    {
        for (int j = -blurRad; j <= blurRad; ++j)
        {
            const vec2 delta = vec2(i, j)*preDelta;
            const vec2 samplePos = fragPos + delta;
            const float l = length(delta);
            const float coeff = exp(-l*l);
            fog += textureLod(inFog, samplePos, 0)*coeff;
            normCoeff += coeff;
        }
    }
    fog /= normCoeff;
    
    out_fragColor = fog.a*color + (1 - fog.a)*fog;
}
