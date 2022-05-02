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


void main()
{
    outNormal = vec4(surf.cNorm, shadingModel);
}
