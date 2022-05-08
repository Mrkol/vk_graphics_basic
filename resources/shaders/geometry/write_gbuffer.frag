#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "../common.h"

layout (location = 0) in VS_OUT
{
    vec3 sNorm;
    vec3 sTangent;
    vec2 texCoord;
} surf;

layout (location = 3) flat in uint shadingModel;

layout(binding = 0, set = 0) uniform AppData
{
    UniformParams Params;
};

layout (location = 0) out vec4 outNormal;
layout (location = 1) out vec4 outTangent;
layout (location = 2) out vec4 outAlbedo;


void main()
{
    vec3 color = Params.baseColor;
    if (shadingModel == 2) color = vec3(0.5, 0.8, 0.1);
    if (shadingModel == 0) color = vec3(0.6, 0.4, 0.2);

    outNormal = vec4(normalize(surf.sNorm), float(shadingModel));
    outTangent = vec4(normalize(surf.sTangent), 0.0);
    outAlbedo = vec4(color, 1.0);
}
