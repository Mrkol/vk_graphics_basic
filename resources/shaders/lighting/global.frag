#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "../common.h"
#include "../shadowmap.glsl"


layout(push_constant) uniform params_t
{
    mat4 mProj;
    mat4 mView;
} params;

layout(location = 0) out vec4 out_fragColor;

layout(binding = 0, set = 0) uniform AppData
{
    UniformParams Params;
};

layout (input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput inNormal;
layout (input_attachment_index = 1, set = 1, binding = 1) uniform subpassInput inTangent;
layout (input_attachment_index = 2, set = 1, binding = 2) uniform subpassInput inAlbedo;
layout (input_attachment_index = 3, set = 1, binding = 3) uniform subpassInput inDepth;

// For compat with quad3_vert
layout (location = 0) in FS_IN { vec2 texCoord; } vIn;


float sq(float x) { return x*x; }
const float PI = 3.14159265358979323846;
const float ONE_OVER_PI = 1.0 / PI;
const vec3 DIELECTRIC_SPECULAR = vec3(0.04);
const vec3 BLACK = vec3(0);
const vec3 SUN_COLOR = vec3(4, 3.5, 3);


mat4 invView = inverse(params.mView);
mat4 invProj = inverse(params.mProj);
vec3 cLightPosition = (params.mView * vec4(Params.lightPos, 1)).xyz;


float depth(vec3 cPos, vec3 cNormal, uint cascadeIndex)
{
    const vec4 cShrinkedpos = vec4(cPos - 0.005f * cNormal, 1.0f);

    const vec4 sampleCoord = (biasMat * shadowmapUbo.cascadeViewProjMat[cascadeIndex])
        * invView * cShrinkedpos;
	const vec3 sShadow = vec3(2*sampleCoord.st - 1,
        texture(inShadowmaps, vec3(sampleCoord.st, cascadeIndex)).r);

    const vec3 cShadow = (params.mView
        * inverse(shadowmapUbo.cascadeViewProjMat[cascadeIndex])
        * vec4(sShadow, 1.0)).xyz;

    return abs(cShrinkedpos.z - cShadow.z);
}

vec3 T(float s) {
    return
       vec3(0.233, 0.455, 0.649) * exp(-s*s/0.0064) +
       vec3(0.1, 0.336, 0.344) * exp(-s*s/0.0484) +
       vec3(0.118, 0.198, 0.0) * exp(-s*s/0.187) +
       vec3(0.113, 0.007, 0.007) * exp(-s*s/0.567) +
       vec3(0.358, 0.004, 0.0) * exp(-s*s/1.99) +
       vec3(0.078, 0.0, 0.0) * exp(-s*s/7.41);
}

vec3 diffuse_transmittance(
    vec3 albedo,
    vec3 cPosition,
    vec3 cNormal,
    uint cascadeIndex,
    uint shadingModel)
{
    if (shadingModel != 1 || !Params.enableSss)
    {
        // no transmittance
        return vec3(0);
    }

    // http://www.iryoku.com/translucency/downloads/Real-Time-Realistic-Skin-Translucency.pdf
    const vec3 cLightDir = normalize(cLightPosition - cPosition);

    float s = depth(cPosition, cNormal, cascadeIndex);
    float E = max(0.3 + dot(-cNormal, cLightDir), 0.0);
    return T(s) * SUN_COLOR * albedo * E * ONE_OVER_PI;
}

float SmithJoint_G(float alphaSq, float NoL, float NoV)
{
	float k = alphaSq / 2;
	float g_v = NoV / (NoV*(1 - k) + k);
	float g_l = NoL / (NoL*(1 - k) + k);
	return g_v * g_l;
}

float GGX_D(float alphaSq, float NoH)
{
    float c = (sq(NoH) * (alphaSq - 1.) + 1.);
    return alphaSq / sq(c) * ONE_OVER_PI;
}

vec3 diffuse_specular_reflectance(
    vec3 albedo,
    vec3 cPosition,
    vec3 cNormal,
    float metallic,
    float roughness,
    uint cascadeIndex,
    uint shadingModel)
{
    vec3 L = normalize(cLightPosition - cPosition);
    vec3 V = vec3(0, 0, 1);
    vec3 N = cNormal;

    
    vec3 H = normalize(L+V);

    float NoH = max(0.001, dot(N, H));
    float VoH = max(0.001, dot(V, H));
    float NoL = shadingModel == 2 ? abs(dot(N, L)) : max(0.001, dot(N, L));
    float NoV = max(0.001, dot(N, V));

    vec3 F0 = mix(DIELECTRIC_SPECULAR, albedo, metallic);
    vec3 cDiff = mix(albedo * (1. - DIELECTRIC_SPECULAR),
                     BLACK,
                     metallic);

    float alphaSq = sq(sq(roughness));

    // Schlick's approximation
    vec3 F = mix(F0, vec3(1), pow((1.01 - VoH), 5.));

    vec3 diffuse = (vec3(1.) - F) * cDiff * ONE_OVER_PI;

    float G = SmithJoint_G(alphaSq, NoL, NoV);

    float D = GGX_D(alphaSq, NoH);

    vec3 specular = (F * G * D) / (4. * NoL * NoV);

    return SUN_COLOR
        * (diffuse + specular)
        * NoL;
}

void main()
{
    const uint shadingModel = uint(subpassLoad(inNormal).w);

    const vec4 screenSpacePos = vec4(
        2.0 * gl_FragCoord.xy / vec2(Params.screenWidth, Params.screenHeight) - 1.0,
        subpassLoad(inDepth).r,
        1.0);
    const vec4 camSpacePos = invProj * screenSpacePos;

    const vec3 position = camSpacePos.xyz / camSpacePos.w;
    const vec3 normal = subpassLoad(inNormal).xyz;
    const vec3 tangent = subpassLoad(inTangent).xyz;
    const vec3 albedo = subpassLoad(inAlbedo).rgb;
    
    if (screenSpacePos.z == 1)
    {
        const vec2 d = cLightPosition.xy/cLightPosition.z - position.xy/position.z;
        const float scale = 0.001f;
        float a = 1 - min(dot(d, d), scale)/scale;
        a *= (cLightPosition.z < 0 ? 1 : 0);
        out_fragColor = mix(vec4(0.2, 0.5, 0.98, 1), vec4(SUN_COLOR, 1), a);
        return;
    }

    const uint cascadeIdx = cascadeForDepth(position.z);

    const float roughness = shadingModel == 2 ? 0.8f : 0.f;

    vec3 color =
          diffuse_specular_reflectance(albedo, position, normal, 0, roughness, cascadeIdx, shadingModel)
        + diffuse_transmittance(albedo, position, normal, cascadeIdx, shadingModel);

    out_fragColor = vec4(color, shade((invView * vec4(position, 1.f)).xyz, cascadeIdx));
}
