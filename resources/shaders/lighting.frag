#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "common.h"



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



struct PointLight
{
    vec4 posAndOuterRadius;
    vec4 colorAndInnerRadius;
};

layout(binding = 1, set = 0) buffer PointLights
{
    PointLight pointLights[];
};

layout (input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput inNormal;
layout (input_attachment_index = 1, set = 1, binding = 1) uniform subpassInput inTangent;
layout (input_attachment_index = 2, set = 1, binding = 2) uniform subpassInput inAlbedo;
layout (input_attachment_index = 3, set = 1, binding = 3) uniform subpassInput inDepth;

layout (location = 0) flat in uint InstanceIndex;

float sq(float x) { return x*x; }

void main()
{
    PointLight light = pointLights[InstanceIndex];
    vec4 screenSpacePos = vec4(
        2.0 * gl_FragCoord.xy / vec2(Params.screenWidth, Params.screenHeight) - 1.0,
        subpassLoad(inDepth).r,
        1.0);
    vec4 camSpacePos = inverse(params.mProj) * screenSpacePos;

    vec3 position = camSpacePos.xyz / camSpacePos.w;
    vec3 normal = subpassLoad(inNormal).xyz;
    vec3 tangent = subpassLoad(inTangent).xyz;
    vec3 albedo = subpassLoad(inAlbedo).rgb;


    vec3 lightPosition = (params.mView * vec4(light.posAndOuterRadius.xyz, 1.0)).xyz;
    vec3 lightColor = light.colorAndInnerRadius.rgb;
    float lightRmin2 = sq(light.colorAndInnerRadius.w);
    float lightRmax2 = sq(light.posAndOuterRadius.w);

    vec3 toLightVec = lightPosition - position;
    vec3 lightDir = normalize(toLightVec);
    float lightDist2 = dot(toLightVec, toLightVec);

    // from realtime rendering
    float lightSampleDist = mix(lightRmin2, lightRmax2, 0.05);
    float attenuation = lightSampleDist/max(lightRmin2, lightDist2)
        * sq(max(1 - sq(lightDist2 / lightRmax2), 0));

    vec3 diffuse = max(dot(normal, lightDir), 0.0f) * lightColor;

    out_fragColor = vec4(diffuse * albedo, attenuation);
}
