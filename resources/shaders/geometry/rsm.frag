#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "../common.h"


layout(push_constant) uniform params_t
{
    mat4 mProj;
    mat4 mView;
} params;

layout(binding = 0, set = 0) uniform AppData
{
    UniformParams Params;
};


layout (location = 0) in VS_OUT
{
    vec3 cNorm;
    vec3 cTangent;
    vec2 texCoord;
} surf;

layout (location = 3) flat in uint shadingModel;



layout (location = 0) out vec4 outNormal;
layout (location = 1) out vec4 outAlbedo;


void main()
{
    vec3 color = Params.baseColor;
    if (shadingModel == 2) color = vec3(0.5, 0.8, 0.1);
    if (shadingModel == 0) color = vec3(0.6, 0.4, 0.2);

    outNormal = vec4((params.mProj * vec4(surf.cNorm, 0.0)).xyz, shadingModel);
    outAlbedo = vec4(color, 1.0);
}
