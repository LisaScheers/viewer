#version 450
layout(set=0,binding=0) uniform sampler2D image;
layout(push_constant) uniform Frame { vec2 extent; int alpha_mask; } frame;
layout(location=0) in vec2 texcoord;
layout(location=1) in vec4 tint;
layout(location=0) out vec4 result;
void main()
{
    vec4 pixel = texture(image, texcoord);
    result = (frame.alpha_mask != 0 ? vec4(1.0, 1.0, 1.0, pixel.a) : pixel) * tint;
}
