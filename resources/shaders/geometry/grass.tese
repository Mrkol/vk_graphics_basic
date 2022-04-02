#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_debug_printf : enable

#include "../common.h"
#include "../perlin.glsl"


layout(push_constant) uniform params_t
{
    mat4 mProj;
    mat4 mView;
} params;

layout(binding = 0, set = 0) uniform AppData
{
    UniformParams Params;
};

layout(binding = 2, set = 0) uniform LandscapeInfo
{
    mat4 modelMat;
    uint width;
    uint height;
    // In heightmap pixels
    uint tileSize;
    // Amount of grass blades per tile
    uint grassDensity;
} landscapeInfo;

layout(triangles, equal_spacing, cw) in;
layout(location = 0) patch in vec3 wBladeBasePos;
layout(location = 1) patch in float yaw;
layout(location = 2) patch in float size;


layout (location = 0) out VS_OUT
{
    vec3 cNorm;
    vec3 cTangent;
    vec2 texCoord;
} vOut;

layout (location = 3) flat out uint shadingModel;

mat4 rotationMatrix(vec3 axis, float angle)
{
    axis = normalize(axis);
    float s = sin(angle);
    float c = cos(angle);
    float oc = 1.0 - c;
    
    return mat4(oc * axis.x * axis.x + c,           oc * axis.x * axis.y - axis.z * s,  oc * axis.z * axis.x + axis.y * s,  0.0,
                oc * axis.x * axis.y + axis.z * s,  oc * axis.y * axis.y + c,           oc * axis.y * axis.z - axis.x * s,  0.0,
                oc * axis.z * axis.x - axis.y * s,  oc * axis.y * axis.z + axis.x * s,  oc * axis.z * axis.z + c,           0.0,
                0.0,                                0.0,                                0.0,                                1.0);
}

void main()
{
    // b is for blade
    vec3 bPos =
        vec3(-0.075f, 0, 0)*gl_TessCoord.x +
        vec3(0, 1.f, 0)*gl_TessCoord.y +
        vec3(0.075f, 0, 0)*gl_TessCoord.z;

    bPos *= size;

    // transform in blade space
    const float windAngle = 6.28318f*cnoise(wBladeBasePos.xz/50 + vec2(Params.time/5));
    const float windAttenuation = (3 + sin(Params.time))/5;

    const vec3 windDir = vec3(cos(windAngle), 0, sin(windAngle));

    const mat4 model = rotationMatrix(vec3(0, 1, 0), yaw);
    bPos = vec3(model * vec4(bPos, 1));
    bPos += windDir * bPos.y * bPos.y * windAttenuation;

    // x = x + ay^2
    // y = y
    // z = z + by^2

    // (1 ay 0)
    // (0 1  0)
    // (0 by 1)
    
    vec3 bNorm = vec3(0, 0, -1);
    
    const mat3 jacobian =
        mat3(1, windDir.x*windAttenuation*bPos.y, 0,
             0, 1,                                0,
             0, windDir.y*windAttenuation*bPos.y, 1);
    const mat4 normalModelView = transpose(inverse(params.mView * landscapeInfo.modelMat));

    const vec3 cPos = vec3(params.mView * vec4(wBladeBasePos + bPos, 1));
    vOut.cNorm = normalize(mat3(normalModelView)*jacobian*mat3(model)*bNorm);
    vOut.cTangent = vec3(0, 0, 0);
    vOut.texCoord = vec2(0, 0);
    shadingModel = 2;

    gl_Position = params.mProj * vec4(cPos, 1);
}
