#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "../common.h"
#include "../shadowmap.glsl"


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



vec3 irradiance(vec3 cPosition, vec3 cNormal, uint shadingModel, uint cascadeIndex)
{
    const vec3 cLightPosition = (params.mView * vec4(Params.lightPos, 1)).xyz;
    const vec3 lightColor = vec3(1, 0.9, 0.9);

    const vec3 toLightVec = cLightPosition - cPosition;
    const vec3 cLightDir = normalize(toLightVec);
    
    vec3 diffuse = lightColor;
    switch (shadingModel)
    {
        case 0:
            diffuse *= max(dot(cNormal, cLightDir), 0.0f);
            break;
        case 1:
            diffuse *= abs(dot(cNormal, cLightDir));
            break;
        case 2:
            diffuse *= 0.5f*dot(cNormal, cLightDir) + 0.5f;
            break;
    }
    
    const float shadow = shade((invView * vec4(cPosition, 1.f)).xyz, cascadeIndex);

    return shadow*diffuse;
}

vec3 rsm(vec3 cPos, vec3 cNormal, uint cascadeIndex)
{
	const vec4 shadowCoord = (biasMat * shadowmapUbo.cascadeViewProjMat[cascadeIndex]) * invView * vec4(cPos, 1.f);	

    const mat4 fromShadowNDC = inverse(biasMat * shadowmapUbo.cascadeViewProjMat[cascadeIndex]);
    const mat3 fromShadowSJacobi = mat3(transpose(shadowmapUbo.cascadeViewProjMat[cascadeIndex]));

    const float CS = cascadeSize(cascadeIndex);

	vec3 ambient = vec3(0);

	for (uint i = 0; i < RSM_KERNEL_SIZE; ++i)
	{
		const float weight = rsmKernel.samples[i].z * CS*CS;

		const vec3 ndcPoint = shadowCoord.xyz + vec3(rsmKernel.samples[i].xy*CS, 0);
        const vec3 wPoint = (fromShadowNDC * vec4(ndcPoint, 1.0)).xyz;
        const vec3 cPoint = (params.mView * vec4(wPoint, 1.0)).xyz;
        vec4 rsmValue = texture(inRsmNormal, vec3(ndcPoint.st, cascadeIndex));
	    vec3 cPointNormal = mat3(params.mView) * fromShadowSJacobi * rsmValue.xyz;

        const uint shadingModel = uint(rsmValue.z);

        float skip = cPointNormal == vec3(0, 0, 0) ? 0.f : 1.f;
        cPointNormal = normalize(cPointNormal);

        const vec3 toLight = cPoint - cPos;

        const float NpotL = dot(cPointNormal, toLight);
        const float NofL = dot(cNormal, toLight);
        const float dist2 = dot(toLight, -toLight);
        // hack: albedo is constant right now
        const vec3 phi = Params.baseColor;
		ambient += weight*phi*skip*max(0, NpotL)*max(0, NofL)/sq(dist2);
	}

    // Article authors talk of some "global normalization".
    // I assume they used the PODGONYAN operator as well.
    return clamp(ambient / (CS*CS*7000.f), 0, 1);
}

void main()
{
    const uint shadingModel = uint(subpassLoad(inNormal).w);

    const vec4 screenSpacePos = vec4(
        2.0 * gl_FragCoord.xy / vec2(Params.screenWidth, Params.screenHeight) - 1.0,
        subpassLoad(inDepth).r,
        1.0);
    const vec4 camSpacePos = inverse(params.mProj) * screenSpacePos;

    const vec3 position = camSpacePos.xyz / camSpacePos.w;
    const vec3 normal = subpassLoad(inNormal).xyz;
    const vec3 tangent = subpassLoad(inTangent).xyz;
    const vec3 albedo = subpassLoad(inAlbedo).rgb;


    const uint cascadeIdx = cascadeForDepth(position.z);
    const vec3 direct = irradiance(position, normal, shadingModel, cascadeIdx);
    

    vec3 ambient = vec3(0.05f);

    if (Params.enableRsm)
    {
        ambient = rsm(position, normal, cascadeIdx);
    }

    out_fragColor = vec4((ambient + direct) * albedo, 0.5f);
}
