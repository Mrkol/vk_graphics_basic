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

layout (location = 0) in vec4 inPosSize;


out gl_PerVertex
{
	vec4 gl_Position;
	float gl_PointSize;
};

layout (location = 0) out vec2 ndcPos;
layout (location = 1) out float ndcRadius;

void main()
{
    gl_Position = params.mProj * params.mView * vec4(inPosSize.xyz, 1.0);
    ndcPos = (0.5 * gl_Position.xy/gl_Position.w + 0.5)
    * vec2(Params.screenWidth, Params.screenHeight);

    gl_PointSize = inPosSize.w
    * Params.screenHeight // NDC to window transform norm
    * abs(params.mProj[1][1]) / gl_Position.w; // world to NDC norm
    ndcRadius = gl_PointSize / 2.f;
}
