#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "common.h"

layout (location = 0 ) in VS_OUT
{
    vec3 sNorm;
    vec3 sTangent;
    vec2 texCoord;
} surf;

layout(binding = 0, set = 0) uniform AppData
{
    UniformParams Params;
};

layout (location = 0) out vec4 outNormal;
layout (location = 1) out vec4 outTangent;
layout (location = 2) out vec4 outAlbedo;

void main()
{
    outNormal = vec4(surf.sNorm, 0.0);
    outTangent = vec4(surf.sTangent, 0.0);
    outAlbedo = vec4(Params.baseColor, 1.0);
}
