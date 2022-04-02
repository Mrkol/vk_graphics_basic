#version 450
#extension GL_ARB_separate_shader_objects : enable


layout (triangles) in;

layout (location = 0) in GS_IN
{
    vec3 wNorm;
    vec3 wTangent;
    vec2 texCoord;
} vIn[];

layout (location = 3) flat in uint inShadingModel[];

layout (line_strip, max_vertices = 4) out;

layout (location = 0) out GS_OUT
{
    vec3 wNorm;
    vec3 wTangent;
    vec2 texCoord;
} vOut;

layout (location = 3) flat out uint outShadingModel;

out gl_PerVertex {
    vec4 gl_Position;
    float gl_PointSize;
    float gl_ClipDistance[];
    float gl_CullDistance[];
};


void main()
{
    for (int i = 0; i < 4; i++)
    {
        gl_Position = gl_in[i % 3].gl_Position;

        vOut.wNorm = vIn[i % 3].wNorm;
        vOut.wTangent = vIn[i % 3].wTangent;
        vOut.texCoord = vIn[i % 3].texCoord;
        outShadingModel = inShadingModel[i % 3];

        EmitVertex();
    }

    EndPrimitive();
}
