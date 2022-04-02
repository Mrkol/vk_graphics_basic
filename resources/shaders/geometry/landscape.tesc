#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_debug_printf : enable


layout(push_constant) uniform params_t
{
    mat4 mProj;
    mat4 mView;
} params;

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

layout(location = 0) in flat uint inInstanceIndex[];

layout(vertices = 4) out;
layout(location = 0) patch out uint outInstanceIndex;


float calcLod(float dist)
{
    const float LODMIN_DIST = 10.f;
    const float LODMAX_DIST = 100.f;
    
    return pow(clamp((LODMAX_DIST - dist) / (LODMAX_DIST - LODMIN_DIST), 0, 1), 2);
}

void main()
{
    if (gl_InvocationID == 0)
    {
        const uint tileId = tiles[inInstanceIndex[0]];
        const uvec2 totalTiles =
            uvec2(landscapeInfo.width, landscapeInfo.height)
                / landscapeInfo.tileSize;
        const uvec2 tilePos = uvec2(tileId % totalTiles.x, tileId / totalTiles.x);
        const vec2 mTileSize = 1.f/vec2(totalTiles);
        const vec2 mTilePos2 = vec2(tilePos) * mTileSize;
        // TODO: y shouldn't be 0 but it works for now lol
        const vec3 mTilePos =
            vec3(mTilePos2.x + mTileSize.x/2.f, 0, mTilePos2.y + mTileSize.y/2.f);

        const vec3 mTiledx = vec3(mTileSize.x, 0, 0)/2.f;
        const vec3 mTiledy = vec3(0, 0, mTileSize.y)/2.f;

        const mat4 MV = params.mView * landscapeInfo.modelMat;

        const vec4 cTilePos = MV * vec4(mTilePos, 1);
        const vec4 cTileNeighborPos[] = {
                MV * vec4(mTilePos - mTiledx, 1),
                MV * vec4(mTilePos - mTiledy, 1),
                MV * vec4(mTilePos + mTiledx, 1),
                MV * vec4(mTilePos + mTiledy, 1),
            };

        
        const float lod = calcLod(length(cTilePos));
        const float neighborLods[] = {
                calcLod(length(cTileNeighborPos[0])),
                calcLod(length(cTileNeighborPos[1])),
                calcLod(length(cTileNeighborPos[2])),
                calcLod(length(cTileNeighborPos[3])),
            };
        
        const float maxTess = (landscapeInfo.tileSize - 1)*10;

        gl_TessLevelInner[0] = max(maxTess * lod, 1);
        gl_TessLevelInner[1] = max(maxTess * lod, 1);

        gl_TessLevelOuter[0] = max(maxTess * neighborLods[0], 1);
        gl_TessLevelOuter[1] = max(maxTess * neighborLods[1], 1);
        gl_TessLevelOuter[2] = max(maxTess * neighborLods[2], 1);
        gl_TessLevelOuter[3] = max(maxTess * neighborLods[3], 1);

        outInstanceIndex = inInstanceIndex[0];
    }

    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
}
