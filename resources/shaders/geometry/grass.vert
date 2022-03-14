#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require


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
} landscapeInfo;

layout(binding = 3, set = 0) buffer GrassInfo
{
    vec2 offset[];
} grassInfos;

vec2 grass[3] = vec2[](
    vec2(-0.1, 0.0),
    vec2( 0.1, 0.0),
    vec2( 0.0, 0.5)
);

void main()
{
    const vec2 offset = grassInfos.offset[gl_InstanceIndex];
    const vec3 mPos = vec3(offset.x, textureLod(heightmap, offset, 0).r, offset.y) + vec3(grass[gl_VertexIndex], 0.0);
    gl_Position = params.mView * landscapeInfo.modelMat * vec4(mPos, 1.0);
}
