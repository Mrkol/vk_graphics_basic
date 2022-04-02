#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "../perlin.glsl"


layout(push_constant) uniform params_t
{
    mat4 mProj;
    mat4 mView;
} params;

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

layout(std430, binding = 3) buffer tiles_t
{
    uint tiles[];
};

layout(quads, equal_spacing, cw) in;

layout(location = 0) patch in uint instanceIndex;

layout (location = 0) out VS_OUT
{
    vec3 cNorm;
    vec3 cTangent;
    vec2 texCoord;
} vOut;

float calcHeight(vec2 pos)
{
    return textureLod(heightmap, pos, 0).r + cnoise(pos * 800.f)*0.001f;
}

vec3 calcNormal(vec2 pos)
{
    const float EPS = 1.f;
    const vec2 dx = vec2(EPS/float(landscapeInfo.width), 0);
    const vec2 dy = vec2(0, EPS/float(landscapeInfo.height));
    const float r = calcHeight(pos + dx);
    const float l = calcHeight(pos - dx);
    const float u = calcHeight(pos + dy);
    const float d = calcHeight(pos - dy);

    return normalize(vec3(r - l, 0.01f, d - u) / 2.f);
}

vec3 calcPos(vec2 pos)
{
    return vec3(pos.x, calcHeight(pos), pos.y);
}

void main()
{
    const uint tileId = tiles[instanceIndex];
    const uvec2 totalTiles =
        uvec2(landscapeInfo.width, landscapeInfo.height)
            / landscapeInfo.tileSize;
    const uvec2 tilePos = uvec2(tileId % totalTiles.x, tileId / totalTiles.x);
    const vec2 mTileSize = 1.f/vec2(totalTiles);
    const vec2 mTilePos = vec2(tilePos) * mTileSize;

    const vec2 mPos2 = mTilePos + gl_TessCoord.xy * mTileSize;

    const vec3 mPos = calcPos(mPos2);
    const vec3 mNorm = calcNormal(mPos2);
    const vec3 mTang = vec3(0);

    mat4 modelView = params.mView * landscapeInfo.modelMat;

    mat4 normalModelView = transpose(inverse(modelView));

    vOut.cNorm    = normalize(mat3(normalModelView) * mNorm);
    vOut.cTangent = mat3(normalModelView) * mTang;
    vOut.texCoord = mPos2;
    
    gl_Position   = params.mProj * modelView * vec4(mPos, 1);
}
