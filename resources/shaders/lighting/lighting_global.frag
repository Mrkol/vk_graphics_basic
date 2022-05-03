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



float rsm(vec3 cPos, vec3 cNormal, uint cascadeIndex)
{
	const vec4 shadowCoord = (biasMat * shadowmapUbo.cascadeViewProjMat[cascadeIndex]) * invView * vec4(cPos, 1.f);	

    const mat4 fromShadowNDC = inverse(biasMat * shadowmapUbo.cascadeViewProjMat[cascadeIndex]);
    const mat3 fromShadowSJacobi = mat3(transpose(shadowmapUbo.cascadeViewProjMat[cascadeIndex]));

	float ambient = 0;

	for (uint i = 0; i < RSM_KERNEL_SIZE; ++i)
	{
		const float weight = rsmKernel.samples[i].z;

		const vec3 ndcPoint = shadowCoord.xyz + vec3(rsmKernel.samples[i].xy, 0);
        const vec3 cPoint = (params.mView * fromShadowNDC * vec4(ndcPoint, 1.0)).xyz;
	    vec3 cPointNormal = mat3(params.mView)
            * fromShadowSJacobi * texture(inRsmNormal, vec3(ndcPoint.st, cascadeIndex)).xyz;

        float skip = cPointNormal == vec3(0, 0, 0) ? 0.f : 1.f;
        cPointNormal = normalize(cPointNormal);

        const vec3 lightDir = cPos - cPoint;

        const float NpoL = dot(cPointNormal, lightDir);
        const float NomL = dot(cNormal, -lightDir);
        const float dist2 = dot(lightDir, lightDir);
        // 0 -> 0.01f
        // 1 -> 0.1f
        // 2 -> 1
        const float phi = pow(10, cascadeIndex)/100.f;
		ambient += phi*skip*max(0, NpoL)*max(0, NomL)/sq(dist2)*weight;
	}

    ambient /= float(RSM_KERNEL_SIZE);

    return ambient;
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
    const float shadow = shade((invView * vec4(position, 1.f)).xyz, cascadeIdx);

    const vec3 lightPosition = (params.mView * vec4(Params.lightPos, 1)).xyz;
    const vec3 lightColor = vec3(1, 0.9, 0.9);

    const vec3 toLightVec = lightPosition - position;
    const vec3 lightDir = normalize(toLightVec);
    
    vec3 diffuse = lightColor;
    switch (shadingModel)
    {
        case 0:
            diffuse *= max(dot(normal, lightDir), 0.0f);
            break;
        case 1:
            diffuse *= abs(dot(normal, lightDir));
            break;
        case 2:
            diffuse *= 0.5f*dot(normal, lightDir) + 0.5f;
            break;
    }

    float ambient = 0.05f;

    if (Params.enableRsm)
    {
        ambient = rsm(position, normal, cascadeIdx);
    }

    out_fragColor = vec4((ambient + shadow*diffuse) * albedo, 0.5f);
}
