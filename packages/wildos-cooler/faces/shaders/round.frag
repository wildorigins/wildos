#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float feather;
};

layout(binding = 1) uniform sampler2D source;

void main()
{
    float r = length(qt_TexCoord0 - vec2(0.5)) * 2.0;
    float inside = 1.0 - smoothstep(1.0 - feather, 1.0, r);
    fragColor = texture(source, qt_TexCoord0) * inside * qt_Opacity;
}
