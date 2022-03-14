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

layout (input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput inNormal;
layout (input_attachment_index = 1, set = 1, binding = 1) uniform subpassInput inTangent;
layout (input_attachment_index = 2, set = 1, binding = 2) uniform subpassInput inAlbedo;
layout (input_attachment_index = 3, set = 1, binding = 3) uniform subpassInput inDepth;

// For compat with quad3_vert
layout (location = 0 ) in FS_IN { vec2 texCoord; } vIn;


float sq(float x) { return x*x; }

void main()
{
    const vec4 screenSpacePos = vec4(
        2.0 * gl_FragCoord.xy / vec2(Params.screenWidth, Params.screenHeight) - 1.0,
        subpassLoad(inDepth).r,
        1.0);
    const vec4 camSpacePos = inverse(params.mProj) * screenSpacePos;

    const vec3 position = camSpacePos.xyz / camSpacePos.w;
    const vec3 normal = subpassLoad(inNormal).xyz;
    const vec3 tangent = subpassLoad(inTangent).xyz;
    const vec3 albedo = subpassLoad(inAlbedo).rgb;
    const float shade = subpassLoad(inAlbedo).a;


    const vec3 lightPosition = (params.mView * vec4(Params.lightPos, 1)).xyz;
    const vec3 lightColor = vec3(1, 0.9, 0.9);

    const vec3 toLightVec = lightPosition - position;
    const vec3 lightDir = normalize(toLightVec);

    const vec3 diffuse = max(dot(normal, lightDir), 0.0f) * lightColor;
    const vec3 ambient = vec3(0.1, 0.1, 0.1);

    out_fragColor = vec4((ambient + diffuse*shade) * albedo, 0.5f);
}
