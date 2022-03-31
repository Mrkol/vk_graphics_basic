#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "../common.h"


layout(push_constant) uniform params_t
{
    mat4 mProj;
    mat4 mView;
} params;

layout (location = 0 ) in VS_OUT
{
    vec3 cNorm;
    vec3 cTangent;
    vec2 texCoord;
} surf;

layout(binding = 0, set = 0) uniform AppData
{
    UniformParams Params;
};

layout(binding = 1, set = 0) uniform sampler2D heightmap;

layout(binding = 2, set = 0) uniform LandscapeInfo
{
    mat4 modelMat;
    uint width;
    uint height;
} landscapeInfo;

layout (location = 0) out vec4 outNormal;
layout (location = 1) out vec4 outTangent;
layout (location = 2) out vec4 outAlbedo;


float shade()
{
    const vec3 mLightPos = (inverse(landscapeInfo.modelMat) * vec4(Params.lightPos, 1)).xyz;

    // Performed in terrain's model space
    const vec3 start = vec3(surf.texCoord.x, textureLod(heightmap, surf.texCoord, 0).r, surf.texCoord.y);
    const float h = 1.5f/float(landscapeInfo.width + landscapeInfo.height);
    const vec3 dir = normalize(mLightPos - start);

    float result = 0;

    vec3 current = start;
    while (current.x >=  0 && current.x <= 1
        && current.y >= -1 && current.y <= 1
        && current.z >=  0 && current.z <= 1)
    {
        current += h*dir;

        if (textureLod(heightmap, current.xz, 0).r > current.y)
            result += 1.f;
    }

    const float minHits = 1.f/(h*30.f);
    return (minHits - min(result, minHits))/minHits;
}

void main()
{
    outNormal = vec4(surf.cNorm, 0.0);
    outTangent = vec4(surf.cTangent, 0.0);
    const float shadow = Params.enableLandscapeShadows ? shade() : 1.f;
    outAlbedo = vec4(Params.baseColor, shadow);
}
