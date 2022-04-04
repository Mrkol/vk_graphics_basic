#ifndef VK_GRAPHICS_BASIC_LANDSCAPE_RAYMARCH_H
#define VK_GRAPHICS_BASIC_LANDSCAPE_RAYMARCH_H

layout(binding = 1, set = 0) uniform sampler2D heightmap;

layout(binding = 2, set = 0) uniform LandscapeInfo
{
    mat4 modelMat;
    uint width;
    uint height;
    // In heightmap pixels
    uint tileSize;
    // Amount of grass blades per tile
    uint grassDensity;
} landscapeInfo;

vec3 sampleHeightmap(vec2 mPos)
{
    return vec3(mPos.x, textureLod(heightmap, mPos, 0).r, mPos.y);
}

float landscapeShade(vec3 mPos, vec3 light)
{
    const vec3 mLightPos = (inverse(landscapeInfo.modelMat) * vec4(light, 1)).xyz;

    // Performed in terrain's model space
    const float h = 1.5f/float(landscapeInfo.width + landscapeInfo.height);
    const vec3 dir = normalize(mLightPos - mPos);
    const uint maxIters = 512;

    float result = 0;

    vec3 current = mPos;
    for (uint i = 0; i < maxIters
        && current.x >=  0 && current.x <= 1
        && current.y >= -1 && current.y <= 1
        && current.z >=  0 && current.z <= 1;
        ++i)
    {
        current += h*dir;

        if (textureLod(heightmap, current.xz, 0).r > current.y)
            result += 1.f;
    }

    const float minHits = 1.f/(h*30.f);
    return (minHits - min(result, minHits))/minHits;
}

#endif // VK_GRAPHICS_BASIC_LANDSCAPE_RAYMARCH_H
