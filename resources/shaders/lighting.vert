#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require


#include "unpack_attributes.h"

layout(push_constant) uniform params_t
{
    mat4 mProj;
    mat4 mView;
} params;

struct PointLight
{
    vec4 posAndRadius;
    vec4 color;
};

layout(binding = 1, set = 0) buffer PointLights
{
    PointLight pointLights[];
};

layout (location = 0) flat out uint InstanceIndexOut;

void main()
{
    vec4 pnr = pointLights[gl_InstanceIndex].posAndRadius;
    gl_Position = vec4(vec3(params.mView * vec4(pnr.xyz, 1.0f)), pnr.w);
    InstanceIndexOut = gl_InstanceIndex;
}
