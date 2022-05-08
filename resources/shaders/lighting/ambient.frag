#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "../common.h"
#include "../shadowmap.glsl"


layout (constant_id = 1) const uint RSM_KERNEL_SIZE = 64u;
layout (constant_id = 2) const float RSM_RADIUS = 2.f;

layout (set = 1, binding = 7) uniform sampler2DArray inRsmNormal;
layout (set = 1, binding = 8) uniform sampler2DArray inRsmAlbedo;
layout (set = 1, binding = 9) uniform RsmKernel
{
	// x, y, weight
	vec4 samples[RSM_KERNEL_SIZE];
} rsmKernel;


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
layout (location = 0) in FS_IN { vec2 texCoord; } vIn;


float sq(float x) { return x*x; }


mat4 invView = inverse(params.mView);
mat4 invProj = inverse(params.mProj);
vec3 cLightPosition = (params.mView * vec4(Params.lightPos, 1)).xyz;


void main()
{
    const uint shadingModel = uint(subpassLoad(inNormal).w);

    const vec4 screenSpacePos = vec4(
        2.0 * gl_FragCoord.xy / vec2(Params.screenWidth, Params.screenHeight) - 1.0,
        subpassLoad(inDepth).r,
        1.0);
    const vec4 camSpacePos = invProj * screenSpacePos;

    const vec3 cPosition = camSpacePos.xyz / camSpacePos.w;
    const vec3 cNormal = subpassLoad(inNormal).xyz;
    const vec3 cTangent = subpassLoad(inTangent).xyz;
    const vec3 albedo = subpassLoad(inAlbedo).rgb;


    const uint cascadeIndex = cascadeForDepth(cPosition.z);
    
    // RSM
	const vec4 shadowCoord = (biasMat * shadowmapUbo.cascadeViewProjMat[cascadeIndex])
        * invView * vec4(cPosition, 1.f);	

    const mat4 fromShadowNDC = inverse(biasMat * shadowmapUbo.cascadeViewProjMat[cascadeIndex]);
    const mat3 fromShadowSJacobi = mat3(transpose(shadowmapUbo.cascadeViewProjMat[cascadeIndex]));

    const float CS = cascadeSize(cascadeIndex);

	vec4 ambient = vec4(0);

    float scale = 1.;

	for (uint i = 0; i < RSM_KERNEL_SIZE; ++i)
	{
		const float weight = rsmKernel.samples[i].z * CS*CS;

		vec3 ndcPointLight = shadowCoord.xyz + vec3(rsmKernel.samples[i].xy*CS, 0);
        ndcPointLight.z = texture(inShadowmaps, vec3(ndcPointLight.st, cascadeIndex)).r;
        const vec3 wPointLight = (fromShadowNDC * vec4(ndcPointLight, 1.0)).xyz;
        const vec3 cPointLight = (params.mView * vec4(wPointLight, 1.0)).xyz;
        vec4 rsmValue = texture(inRsmNormal, vec3(ndcPointLight.st, cascadeIndex));
	    vec3 cPointLightNormal = mat3(params.mView) * fromShadowSJacobi * rsmValue.xyz;

        const uint shadingModel = uint(rsmValue.z);

        // outside of shadowmap
        if (cPointLightNormal == vec3(0, 0, 0))
        {
            continue;
        }
        cPointLightNormal = normalize(cPointLightNormal);

        const vec3 toLight = cPointLight - cPosition;

        const float NpofL = dot(cPointLightNormal, -toLight);
        const float NotL = dot(cNormal, toLight);
        const float dist2 = 0.0001f + dot(toLight, toLight);

        const float attenuation = weight*max(0, NpofL)*max(0, NotL)/sq(dist2);
        
        const vec3 phi = texture(inRsmAlbedo, vec3(ndcPointLight.st, cascadeIndex)).rgb;
		ambient += vec4(albedo*phi*attenuation,
            attenuation*0.1f); // PODGON constant
	}

    ambient /= (CS*CS);

    out_fragColor = ambient;
}
