#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require


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

layout(quads, equal_spacing, cw) in;

layout (location = 0) out VS_OUT
{
    vec3 sNorm;
    vec3 sTangent;
    vec2 texCoord;
} vOut;

vec3 calcNormal(vec2 pos)
{
    const vec2 dx = vec2(1.f/float(landscapeInfo.width), 0);
    const vec2 dy = vec2(1.f/float(landscapeInfo.height), 0);
    const float l = textureLod(heightmap, pos + dx, 0).r;
    const float r = textureLod(heightmap, pos - dx, 0).r;
    const float u = textureLod(heightmap, pos + dy, 0).r;
    const float d = textureLod(heightmap, pos - dy, 0).r;

    return vec3(r - l, u - d, -2.f) / 2.f;   
}

void main()
{
    const vec3 mPos = vec3(gl_TessCoord.xy, textureLod(heightmap, gl_TessCoord.xy, 0).r);
    const vec3 mNorm = calcNormal(gl_TessCoord.xy);
    const vec3 mTang = vec3(0);

    mat4 modelView = params.mView * landscapeInfo.modelMat;

    mat4 normalModelView = transpose(inverse(modelView));

    gl_Position   = params.mProj * modelView * vec4(mPos, 1);
    vOut.sNorm    = mat3(normalModelView) * mNorm;
    vOut.sTangent = mat3(normalModelView) * mTang;
    vOut.texCoord = gl_TessCoord.xy;
}
