#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(push_constant) uniform Frame { vec2 extent; int alpha_mask; } frame;
layout(location=0) out vec2 texcoord;
layout(location=1) out vec4 tint;
void main()
{
    gl_Position = vec4(position.x * 2.0 / frame.extent.x - 1.0,
                       1.0 - position.y * 2.0 / frame.extent.y, 0.0, 1.0);
    texcoord = uv;
    tint = color;
}
