#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require


#include "../unpack_attributes.h"


layout(location = 0) in vec4 vPosNorm;
layout(location = 1) in vec4 vTexCoordAndTang;

layout(push_constant) uniform params_t
{
    mat4 mProj;
    mat4 mView;
} params;

layout(binding = 1, set = 0) buffer ModelMatrices
{
    mat4 modelMatrices[];
};

layout(binding = 0, set = 1) buffer InstanceMapping
{
    uint instanceMapping[];
};

layout (location = 0 ) out VS_OUT
{
    vec3 sNorm;
    vec3 sTangent;
    vec2 texCoord;
} vOut;

layout (location = 3) flat out uint shadingModel;

void main(void)
{
    const vec4 wNorm = vec4(DecodeNormal(floatBitsToInt(vPosNorm.w)),         0.0f);
    const vec4 wTang = vec4(DecodeNormal(floatBitsToInt(vTexCoordAndTang.z)), 0.0f);

    mat4 modelView = params.mView * modelMatrices[instanceMapping[gl_InstanceIndex]];

    mat4 normalModelView = transpose(inverse(modelView));

    vOut.sNorm    = mat3(normalModelView) * wNorm.xyz;
    vOut.sTangent = mat3(normalModelView) * wTang.xyz;
    vOut.texCoord = vTexCoordAndTang.xy;
    shadingModel = 1;

    gl_Position   = params.mProj * modelView * vec4(vPosNorm.xyz, 1.0f);
}
