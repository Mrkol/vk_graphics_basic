#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require


layout(location = 0) out uint index;

vec2 grass[3] = vec2[](
    vec2(-0.1, 0.0),
    vec2( 0.1, 0.0),
    vec2( 0.0, 1.0)
);

void main()
{
    index = gl_InstanceIndex;
    gl_Position = vec4(grass[gl_VertexIndex], 0.0, 1.0);
}
