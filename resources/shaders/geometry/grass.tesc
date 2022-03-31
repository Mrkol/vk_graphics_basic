#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require


layout(location = 0) in uint in_index[];


layout(vertices = 3) out;
layout(location = 0) patch out uint index;


void main()
{
    if (gl_InvocationID == 0)
    {
        gl_TessLevelInner[0] = 0;

        const float tess = clamp(1.f/gl_in[0].gl_Position.z, 1, 10);

        gl_TessLevelOuter[0] = 1;
        gl_TessLevelOuter[1] = tess;
        gl_TessLevelOuter[2] = tess;

        index = in_index[0];
    }

    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
}
