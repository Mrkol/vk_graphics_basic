#ifndef VK_GRAPHICS_BASIC_SHADOWMAP_H
#define VK_GRAPHICS_BASIC_SHADOWMAP_H

layout (constant_id = 0) const uint SHADOW_MAP_CASCADE_COUNT = 4u;


layout (set = 1, binding = 4) uniform sampler2DArray inVsm;
layout (set = 1, binding = 5) uniform sampler2DArray inShadowmaps;

layout (set = 1, binding = 6) uniform ShadowmapUBO
{
	mat4 cascadeViewProjMat[SHADOW_MAP_CASCADE_COUNT];
	vec4 cascadeSplitDepths[SHADOW_MAP_CASCADE_COUNT/4];
	vec4 cascadeMatrixNorms[SHADOW_MAP_CASCADE_COUNT/4];
} shadowmapUbo;



const mat4 biasMat = mat4( 
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0 
);
const float ambient = 0.1f;

float cascadeSize(uint cascadeIndex)
{
	return shadowmapUbo.cascadeMatrixNorms[cascadeIndex/4][cascadeIndex%4];
}

float shade(vec3 wPos, uint cascadeIndex)
{
	vec4 shadowCoord = (biasMat * shadowmapUbo.cascadeViewProjMat[cascadeIndex]) * vec4(wPos, 1.f);	

	float shadow = 1.f;
	float bias = .005f;

	if (shadowCoord.z > 0.f && shadowCoord.z < 1.f && shadowCoord.w > 0)
    {
	    const vec2 M1M2 = texture(inVsm, vec3(shadowCoord.st, cascadeIndex)).rg;
        const float M1 = M1M2.x;
        const float M2 = M1M2.y;

        const float mu = M1;
        const float sigma2 = max(M2 - M1*M1, 0.001f);

        const float t = shadowCoord.z;
        const float p = float(t <= mu);
        const float pmax = sigma2 / (sigma2 + (t - mu)*(t - mu));

        return max(p, pmax);
	}
	return shadow;
}

uint cascadeForDepth(float z)
{
	uint cascadeIndex = 0;
	for(uint i = 0; i < SHADOW_MAP_CASCADE_COUNT - 1; ++i)
	{
		if(z < shadowmapUbo.cascadeSplitDepths[i/4][i%4])
		{
			cascadeIndex = i + 1;
		}
	}
    return cascadeIndex;
}

#endif // VK_GRAPHICS_BASIC_SHADOWMAP_H
