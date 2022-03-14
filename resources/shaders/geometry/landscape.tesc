#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require


layout(binding = 2, set = 0) uniform LandscapeInfo
{
    mat4 modelMat;
    uint width;
    uint height;
} landscapeInfo;

layout(vertices = 4) out;

void main()
{
    if (gl_InvocationID == 0)
    {
        gl_TessLevelInner[0] = landscapeInfo.width;
        gl_TessLevelInner[1] = landscapeInfo.height;

        gl_TessLevelOuter[0] = landscapeInfo.width;
        gl_TessLevelOuter[1] = landscapeInfo.height;
        gl_TessLevelOuter[2] = landscapeInfo.width;
        gl_TessLevelOuter[3] = landscapeInfo.height;
    }

    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
}
