#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "../common.h"



layout (constant_id = 0) const uint SHADOW_MAP_CASCADE_COUNT = 4;

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
layout (set = 1, binding = 4) uniform sampler2DArray inShadowmaps;

layout (set = 1, binding = 5) uniform ShadowmapUBO
{
	mat4 cascadeViewProjMat[SHADOW_MAP_CASCADE_COUNT];
	vec4 cascadeSplitDepths[SHADOW_MAP_CASCADE_COUNT/4];
} shadowmapUbo;


// For compat with quad3_vert
layout (location = 0) in FS_IN { vec2 texCoord; } vIn;


float sq(float x) { return x*x; }

const mat4 biasMat = mat4( 
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0 
);
const float ambient = 0.1f;

mat4 invView = inverse(params.mView);

float shade(vec3 cPos, uint cascadeIndex)
{
	vec4 shadowCoord = (biasMat * shadowmapUbo.cascadeViewProjMat[cascadeIndex]) * invView * vec4(cPos, 1.f);	

	float shadow = 1.f;
	float bias = .005f;

	if (shadowCoord.z > 0.f && shadowCoord.z < 1.f) {
		float dist = texture(inShadowmaps, vec3(shadowCoord.st, cascadeIndex)).r;
		if (shadowCoord.w > 0 && dist < shadowCoord.z - bias) {
			shadow = ambient;
		}
	}
	return shadow;

}


uint cascadeForDepth(float z)
{
	uint cascadeIndex = 0;
	for(uint i = 0; i < SHADOW_MAP_CASCADE_COUNT - 1; ++i) {
		if(z < shadowmapUbo.cascadeSplitDepths[i/4][i%4]) {	
			cascadeIndex = i + 1;
		}
	}
    return cascadeIndex;
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


    const float shadow = shade(position, cascadeForDepth(position.z));

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

    out_fragColor = vec4(shadow*diffuse * albedo, 0.5f);
}
