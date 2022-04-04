#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "../common.h"
#include "../landscape_raymarch.glsl"


layout(push_constant) uniform params_t
{
    mat4 mProj;
    mat4 mView;
} params;

layout(binding = 0, set = 0) uniform AppData
{
    UniformParams Params;
};

layout(std430, binding = 3) buffer tiles_t
{
    uint tiles[];
};

layout(location = 0) in uint inInstanceIndex[];


layout(vertices = 3) out;
layout(location = 0) patch out vec3 wBladeBasePos;
layout(location = 1) patch out float yaw;
layout(location = 2) patch out float size;
layout(location = 3) patch out float shadow;



float Halton(int b, int i)
{
    float r = 0.0;
    float f = 1.0;
    while (i > 0) {
        f = f / float(b);
        r = r + f * float(i % b);
        i = int(floor(float(i) / float(b)));
    }
    return r;
}

vec2 Halton23(int i)
{
    return vec2(Halton(2, i), Halton(3, i));
}

float calcLod(float dist)
{
    const float LODMIN_DIST = 5.f;
    const float LODMAX_DIST = 75.f;
    
    return pow(clamp((LODMAX_DIST - dist) / (LODMAX_DIST - LODMIN_DIST), 0, 1), 2);
}

float hash(float n)
{
    return fract(sin(n)*43758.5453);
}

void main()
{
    if (gl_InvocationID == 0)
    {
        const uint tileIndex = 1 + (inInstanceIndex[0] - 1) / landscapeInfo.grassDensity;
        const uint bladeIndex = 1 + (inInstanceIndex[0] - 1) % landscapeInfo.grassDensity;

        const uint tileId = tiles[tileIndex];
        const uvec2 totalTiles =
            uvec2(landscapeInfo.width, landscapeInfo.height)
                / landscapeInfo.tileSize;
        const uvec2 tilePos = uvec2(tileId % totalTiles.x, tileId / totalTiles.x);
        const vec2 mTileSize = 1.f/vec2(totalTiles);
        const vec2 mTilePos = vec2(tilePos) * mTileSize;

        const vec2 mBladePos2 = mTilePos + fract(Halton23(int(bladeIndex)))*mTileSize;

        const vec3 mBladePos =
            vec3(mBladePos2.x, textureLod(heightmap, mBladePos2, 0).r, mBladePos2.y);

        const mat4 MV = params.mView * landscapeInfo.modelMat;

        const vec4 cBladePos = MV * vec4(mBladePos, 1);

        // we want a zig-zag pattern
        gl_TessLevelInner[0] = 1;

        const float lod = calcLod(length(cBladePos));
        const float maxBladeDetail = 16;

        gl_TessLevelOuter[0] = maxBladeDetail * lod;
        gl_TessLevelOuter[1] = 1;
        gl_TessLevelOuter[2] = maxBladeDetail * lod;

        wBladeBasePos = vec3(landscapeInfo.modelMat * vec4(mBladePos, 1));
        yaw = 6.28318f * hash(int(bladeIndex));
        size = 1.f - hash(-int(bladeIndex))*0.5f;
        shadow = Params.enableLandscapeShadows ? landscapeShade(mBladePos2, Params.lightPos) : 1.f;
    }

    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
}
