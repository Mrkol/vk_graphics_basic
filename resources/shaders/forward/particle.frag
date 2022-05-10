#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

layout (location = 0) in vec2 ndcPos;
layout (location = 1) in float ndcRadius;

layout (location = 0) out vec4 outFragColor;

void main () 
{
	vec2 d = (gl_FragCoord.xy - ndcPos) / ndcRadius;

	outFragColor = vec4(1, 0.5, 0, 0.5*max(0, 1 - dot(d, d)));
}
