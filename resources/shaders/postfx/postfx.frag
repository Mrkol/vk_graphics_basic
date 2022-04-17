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
layout(binding = 3, set = 0) uniform sampler2D inSsao;

// For compat with quad3_vert
layout (location = 0 ) in FS_IN { vec2 texCoord; } vIn;

layout(location = 0) out vec4 out_fragColor;

const float gamma = 2.2;

vec3 reinhard(vec3 color)
{
    return color.xyz / (color.xyz + vec3(1.0));
}

vec3 exposure(vec3 color)
{
    return vec3(1.0) - exp(-color * Params.exposure);
}

vec3 uncharted2_tonemap_partial(vec3 x)
{
    float A = 0.15f;
    float B = 0.50f;
    float C = 0.10f;
    float D = 0.20f;
    float E = 0.02f;
    float F = 0.30f;
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

vec3 uncharted2_filmic(vec3 v)
{
    float exposure_bias = 2.0f;
    vec3 curr = uncharted2_tonemap_partial(v * exposure_bias);

    vec3 W = vec3(11.2f);
    vec3 white_scale = vec3(1.0f) / uncharted2_tonemap_partial(W);
    return curr * white_scale;
}

vec3 aces_approx(vec3 v)
{
    v *= 0.6f;
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((v*(a*v+b))/(v*(c*v+d)+e), 0.0f, 1.0f);
}

void main()
{
    const vec2 fragPos = gl_FragCoord.xy / vec2(Params.screenWidth, Params.screenHeight);
    const vec2 preDelta =
        1.f / vec2(Params.screenWidth/Params.postFxDownscaleFactor, Params.screenHeight/Params.postFxDownscaleFactor);
    
    const int blurRad = 2;
    vec4 fog = vec4(0);
    float occlusion = 0;
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
            occlusion += textureLod(inSsao, samplePos, 0).r*coeff;
            normCoeff += coeff;
        }
    }
    fog /= normCoeff;
    occlusion /= normCoeff;
    
    
    vec3 color = textureLod(inColor, fragPos, 0).rgb;

    if (Params.enableSsao)
    {
      color *= occlusion;
    }

    color = fog.a*color + (1 - fog.a)*fog.rgb;

    switch (Params.tonemappingMode)
    {
        default:
            // nop
            break;

        case 1:
            color = reinhard(color);
            break;

        case 2:
            color = uncharted2_filmic(color);
            break;

        case 3:
            color = exposure(color);
            break;

        case 4:
            color = aces_approx(color);
            break;
    }

    out_fragColor = vec4(color, 1.0);
}
