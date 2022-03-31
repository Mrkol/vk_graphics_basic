#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

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

layout(binding = 1, set = 0) uniform sampler2D heightmap;

layout(binding = 2, set = 0) uniform LandscapeInfo
{
    mat4 modelMat;
    uint width;
    uint height;
} landscapeInfo;

layout(binding = 3, set = 0) buffer GrassInfo
{
    vec2 offset[];
} grassInfos;


layout(triangles, equal_spacing, cw) in;
layout(location = 0) patch in uint index;


layout (location = 0) out VS_OUT
{
    vec3 cNorm;
    vec3 cTangent;
    vec2 texCoord;
} vOut;

void main()
{
    const vec2 offset = grassInfos.offset[index];
    const vec3 mPos = vec3(offset.x, textureLod(heightmap, offset, 0).r, offset.y)
        + vec3(gl_TessCoord.xy, 0.0);
    
    const mat4 modelView = params.mView * landscapeInfo.modelMat;
    const mat4 normalModelView = transpose(inverse(modelView));

    const vec3 cPos = (modelView * vec4(mPos, 1.0)).xyz;
    vOut.cNorm = vec3(0, 0, -1); // ???
    vOut.cTangent = vec3(0, 0, 0);


    const float windStrength = 0.1;
    const vec3 windDirection = normalize(vec3(1, 0, 1));
    const vec3 displacement = gl_TessCoord.y
        * windStrength * windDirection * cos(Params.time);
    
    gl_Position = params.mProj * vec4(cPos + displacement, 1);
}
