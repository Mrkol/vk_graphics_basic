#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

layout(location = 0) out vec2 out_M1M2;

layout (constant_id = 0) const uint RADIUS = 3;

layout(binding = 0) uniform sampler2D depthMap;

// For compat with quad3_vert
layout (location = 0) in FS_IN { vec2 texCoord; } vIn;

void main()
{
    vec2 dims = vec2(textureSize(depthMap, 0));

    int r = int(RADIUS);

    out_M1M2 = vec2(0, 0);
    for (int i = -r; i <= r; ++i)
    {
        for (int j = -r; j <= r; ++j)
        {
            vec2 offsetCoords = (gl_FragCoord.xy + vec2(i, j)) / dims;

            float depth = textureLod(depthMap, offsetCoords, 0).x;
            out_M1M2 += vec2(depth, depth*depth);
        }
    }

    out_M1M2 /= vec2((2*r + 1)*(2*r + 1));
}
