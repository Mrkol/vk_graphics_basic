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
layout(binding = 2, set = 0) uniform sampler2D inDepth;

// For compat with quad3_vert
layout (location = 0 ) in FS_IN { vec2 texCoord; } vIn;

layout(location = 0) out vec4 out_fragColor;




mat4 invViewProj = inverse(params.mProj*params.mView);

vec4 screenToWorld(vec2 pos, float depth)
{
    const vec4 sPos = vec4(2.0 * pos - 1.0, depth, 1.0);
    const vec4 wPos = invViewProj * sPos;
    return wPos / wPos.w;
}

float fogDensity(vec3 pos)
{
    const float MAX_FOG_DEPTH = -200;
    const float MIN_FOG_DEPTH = -50;
    const float MAX_FOG = 0.3;

    const float base = clamp((MIN_FOG_DEPTH - pos.y)/(MIN_FOG_DEPTH - MAX_FOG_DEPTH), 0, 1)*MAX_FOG;
    const float noise = (1 + cnoise(pos/10 + vec3(Params.time, 0, Params.time)))*0.05f;
    return clamp(base - noise, 0, 1);
}

float sq(float v) { return v*v; }

void main()
{
    const vec2 fragPos = gl_FragCoord.xy / vec2(Params.screenWidth, Params.screenHeight);
    const mat4 invView = inverse(params.mView);

    const vec4 wSurface = screenToWorld(fragPos, textureLod(inDepth, fragPos, 0).r);
    const vec4 wCamPos = screenToWorld(vec2(0.5), 0);

    const float stepLen = 5;
    const vec4 wDir = normalize(wSurface - wCamPos);

    vec4 wCurrent = screenToWorld(fragPos, 0);
    float translucency = 1;
    for (uint i = 0; i < 64; ++i)
    {
        translucency *= exp(-sq(fogDensity(wCurrent.xyz)*stepLen));
        if (dot(wCurrent, wDir) > dot(wSurface, wDir) || translucency < 0.0001f)
        {
            break;
        }
        wCurrent += wDir*stepLen;
    }

    const vec4 fogColor = vec4(0.5, 0.5, 0.5, 0.);

    out_fragColor = translucency*textureLod(inColor, fragPos, 0) + (1 - translucency)*fogColor;
}
