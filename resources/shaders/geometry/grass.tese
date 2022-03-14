#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require


layout(push_constant) uniform params_t
{
    mat4 mProj;
    mat4 mView;
} params;

layout(triangles, equal_spacing, cw) in;

layout (location = 0) out VS_OUT
{
    vec3 cNorm;
    vec3 cTangent;
    vec2 texCoord;
} vOut;

void main()
{
    gl_Position = params.mProj * vec4(gl_TessCoord, 1);
}
