#version 450
#extension GL_ARB_separate_shader_objects : enable


layout(push_constant) uniform params_t
{
    mat4 mProj;
    mat4 mView;
} params;

layout (points) in;

layout (location = 0) flat in uint InstanceIndexIn[];

layout (triangle_strip, max_vertices = 4) out;

out gl_PerVertex {
    vec4 gl_Position;
    float gl_PointSize;
    float gl_ClipDistance[];
    float gl_CullDistance[];
};

layout (location = 0) flat out uint InstanceIndexOut;


vec3 project(vec3 a, vec3 b)
{
	return dot(a,b)/dot(b,b)*(b);
}

void gramSchmidt(vec3 A, vec3 B, vec3 C, out vec3 Ao, out vec3 Bo, out vec3 Co)
{
    Ao = A;
    Bo = B - project(B,A);
    Co = C - project(C,Bo) - project(C,Ao);
    
    Ao = normalize(Ao);
    Bo = normalize(Bo);
    Co = normalize(Co);
}

void main()
{
    vec3 center = gl_in[0].gl_Position.xyz;
    float radius = gl_in[0].gl_Position.w;

    if (dot(center, center) < radius*radius)
    {
        gl_Position = vec4(-1, -1, 0.5, 1);
        InstanceIndexOut = InstanceIndexIn[0];
        EmitVertex();
        gl_Position = vec4(-1, 1, 0.5, 1);
        InstanceIndexOut = InstanceIndexIn[0];
        EmitVertex();
        gl_Position = vec4(1, -1, 0.5, 1);
        InstanceIndexOut = InstanceIndexIn[0];
        EmitVertex();
        gl_Position = vec4(1, 1, 0.5, 1);
        InstanceIndexOut = InstanceIndexIn[0];
        EmitVertex();
    }
    else
    {
        vec3 forward, right, up;
        gramSchmidt(center, vec3(0, 1, 0), vec3(1, 0, 0), forward, up, right);

        /*
                 .
                /|\
               / b \
              /  | .\
             /   *   \
            /____|____\
                   r2
        */

        // sqrt(b^2 - r^2)/r = (b+r)/r2
        // r2 = r*(b+r)/sqrt(b^2 - r^2)
        // r2 = r*(b+r)/sqrt((b - r)(b+r))
        // r2 = r * sqrt((b+r) / (b-r))

        float b = length(center);
        float radius2 = radius * sqrt((b + radius) / (b - radius));
        


        vec3 quad[4] = {
            - right - up,
              right - up,
            - right + up,
              right + up,
        };

        for (uint i = 0; i < 4; ++i)
        {
            gl_Position = params.mProj * vec4(center + forward*radius + quad[i]*radius2, 1.0);
            InstanceIndexOut = InstanceIndexIn[0];
            EmitVertex();
        }
    }
    
    EndPrimitive();
}
