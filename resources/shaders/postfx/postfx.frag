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

layout(binding = 1, set = 0) uniform sampler2D inColor;
layout(binding = 2, set = 0) uniform sampler2D inDepth;

// For compat with quad3_vert
layout (location = 0 ) in FS_IN { vec2 texCoord; } vIn;

layout(location = 0) out vec4 out_fragColor;

void main()
{
    vec2 fragPos = gl_FragCoord.xy / vec2(Params.screenWidth, Params.screenHeight);
    out_fragColor = textureLod(inColor, fragPos, 0);
}
