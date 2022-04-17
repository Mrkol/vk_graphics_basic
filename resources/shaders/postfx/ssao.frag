#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "../common.h"


layout (constant_id = 0) const uint SSAO_KERNEL_SIZE = 64;
layout (constant_id = 1) const float SSAO_RADIUS = 0.5;


layout(push_constant) uniform params_t
{
    mat4 mProj;
    mat4 mView;
} params;

layout(binding = 0, set = 0) uniform AppData
{
    UniformParams Params;
};

layout(binding = 1, set = 0) uniform sampler2D inDepth;
layout (binding = 2) uniform sampler2D inNormal;
layout (binding = 3) uniform sampler2D ssaoNoise;

layout (binding = 4) uniform UBOSSAOKernel
{
	vec4 samples[SSAO_KERNEL_SIZE];
} uboSSAOKernel;


// For compat with quad3_vert
layout (location = 0) in FS_IN { vec2 texCoord; } vIn;

layout(location = 0) out vec4 _;
layout(location = 1) out float out_fragColor;




mat4 invProj = inverse(params.mProj);

vec3 screenToCam(vec2 pos, float depth)
{
    const vec4 sPos = vec4(2.0 * pos - 1.0, depth, 1.0);
    const vec4 cPos = invProj * sPos;
    return cPos.xyz / cPos.w;
}

float sq(float v) { return v*v; }

void main()
{
	const uvec2 renderingRes = uvec2(Params.screenWidth, Params.screenHeight)/Params.postFxDownscaleFactor;
    const vec2 fragPos = gl_FragCoord.xy / vec2(renderingRes);
    const mat4 invView = inverse(params.mView);

	const float depth = textureLod(inDepth, fragPos, 0).r;
    const vec3 cPosition = screenToCam(fragPos, depth);

	if (-cPosition.z > 100)
	{
		out_fragColor = 1.0;
		return;
	}


	const ivec2 noiseRes = textureSize(ssaoNoise, 0);
	const vec2 noiseScale = vec2(renderingRes)/vec2(noiseRes);  
    const vec3 randomVec = vec3(textureLod(ssaoNoise, fragPos*noiseScale, 0).xy, 0);

    const vec3 cNormal = textureLod(inNormal, fragPos, 0).xyz;
    const vec3 cTangent = normalize(randomVec - cNormal * dot(randomVec, cNormal));
	const vec3 cBitangent = cross(cTangent, cNormal);
	const mat3 TBN = mat3(cTangent, cBitangent, cNormal);


    float occlusion = 0.0f;
	// remove banding
	const float bias = 0.025f;
	for(uint i = 0; i < SSAO_KERNEL_SIZE; i++)
	{
		const vec3 tSampleDir = uboSSAOKernel.samples[i].xyz;
		const vec3 cSampleDir = (TBN * tSampleDir) * SSAO_RADIUS;
		const vec3 cSamplePos = cPosition + cSampleDir;
		
		// project
		vec4 offset = vec4(cSamplePos, 1.0f);
		offset = params.mProj * offset;
		offset /= offset.w;
		offset.xyz = offset.xyz * 0.5f + 0.5f;
		
		vec3 cSample = screenToCam(offset.xy, texture(inDepth, offset.xy).x);

		float rangeCheck = smoothstep(0.0f, 1.0f, SSAO_RADIUS / abs(cPosition.z - cSample.z));
		occlusion += float(cSample.z >= cPosition.z + bias)
			// MAGICAL HACK
			* float(normalize(tSampleDir).z >= 0.5)
			* rangeCheck;
	}

	out_fragColor = 1.0 - (occlusion / float(SSAO_KERNEL_SIZE));
}
