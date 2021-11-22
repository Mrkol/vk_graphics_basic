#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "../../../../resources/shaders/common_old.h"

layout(location = 0) out vec4 out_fragColor;

layout (location = 0 ) in VS_OUT
{
    vec3 wPos;
    vec3 wNorm;
    vec3 wTangent;
    vec2 texCoord;
} surf;

layout(binding = 0, set = 0) uniform AppData
{
    UniformParams Params;
};

float fog_density(float z) {
    return 0.0003 * min(10000, z * 100.0);
}

void main()
{
    vec3 lightDir1 = normalize(Params.lightPos - surf.wPos);
    vec3 lightDir2 = vec3(0.0f, 0.0f, 1.0f);

    const vec4 dark_violet = vec4(0.75f, 1.0f, 0.25f, 1.0f);
    const vec4 chartreuse  = vec4(0.75f, 1.0f, 0.25f, 1.0f);

    vec4 lightColor1 = mix(dark_violet, chartreuse, 0.5f);
    if(Params.animateLightColor)
        lightColor1 = mix(dark_violet, chartreuse, abs(sin(Params.time)));

    vec4 lightColor2 = vec4(1.0f, 1.0f, 1.0f, 1.0f);

    vec3 N = surf.wNorm; 

    vec4 color1 = max(dot(N, lightDir1), 0.0f) * lightColor1;
    vec4 color2 = max(dot(N, lightDir2), 0.0f) * lightColor2;
    vec4 color_lights = mix(color1, color2, 0.2f);

    float wave = sin(Params.time) * 0.1 + 0.1;
    vec3 time_dependent_color = vec3(wave, max(sin(Params.time * 1.231) - 0.99, 0) * 30, -wave);
    
    // the sums that were present here effectively evaluated to 0
    // (checked using wolfram alpha)

    vec3 kek = abs(fract(surf.wPos.xyz) - 0.5);
    //third argument had a ) in the wrong place probably
    time_dependent_color *= pow(max(kek.x, max(kek.y, kek.z)) * 2.0, 30);

    vec3 screenFog = mix(vec3(1, 0, 0), vec3(0, 0, 1), gl_FragCoord.x / 1024) * fog_density(1 / gl_FragCoord.w);

    out_fragColor = color_lights * vec4(Params.baseColor, 1.0f) + vec4(time_dependent_color + vec3(0, 0, 0.1) + screenFog, 0);
}