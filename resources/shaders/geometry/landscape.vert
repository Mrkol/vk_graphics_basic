#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require


vec2 positions[4] = vec2[](
    vec2(0.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 1.0),
    vec2(1.0, 0.0)
);

layout(location = 0) out flat uint instanceIndex;

void main(void)
{
    instanceIndex = gl_InstanceIndex;
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
