#version 440

// The RGB ring face's two turning rainbows: a crisp ring near the rim, over a
// wide blurred one that glows inwards from it.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float turn;
};

const float blur = 28.57;
const float tau = 6.2831853;

// No arrays: the older GLSL that Qt also builds for OpenGL rejects constant
// arrays, and NVIDIA's driver uses that version.
vec3 rainbow(float f)
{
    const vec3 pink = vec3(255.0, 61.0, 110.0) / 255.0;
    const vec3 amber = vec3(255.0, 176.0, 46.0) / 255.0;
    const vec3 green = vec3(77.0, 255.0, 154.0) / 255.0;
    const vec3 blue = vec3(46.0, 197.0, 255.0) / 255.0;
    const vec3 violet = vec3(163.0, 91.0, 255.0) / 255.0;
    float at = fract(f) * 5.0;
    if (at < 1.0)
        return mix(pink, amber, at);
    if (at < 2.0)
        return mix(amber, green, at - 1.0);
    if (at < 3.0)
        return mix(green, blue, at - 2.0);
    if (at < 4.0)
        return mix(blue, violet, at - 3.0);
    return mix(violet, pink, min(at - 4.0, 1.0));
}

float erf(float x)
{
    float x2 = x * x;
    return sign(x) * sqrt(1.0 - exp(-x2 * (1.2732395 + 0.147 * x2) / (1.0 + 0.147 * x2)));
}

void main()
{
    vec2 p = qt_TexCoord0 * 640.0 - vec2(320.0);
    float r = max(length(p), 1.0);
    float along = atan(p.x, -p.y) / tau - turn;
    float rim = r / 320.0;

    float spread = blur / (tau * r);
    vec3 soft = vec3(0.0);
    float total = 0.0;
    for (int i = -2; i <= 2; ++i) {
        float weight = exp(-0.5 * float(i * i));
        soft += weight * rainbow(along + float(i) * spread);
        total += weight;
    }
    soft /= total;
    float glow = 0.6 * clamp((rim - 0.72) / 0.08, 0.0, 1.0)
            * 0.5 * (1.0 + erf((320.0 - r) / (blur * 1.4142136)));

    float ring = clamp((rim - 0.855) / 0.005, 0.0, 1.0) * (1.0 - clamp((rim - 0.95) / 0.005, 0.0, 1.0));

    vec3 colour = soft * glow * (1.0 - ring) + rainbow(along) * ring;
    float alpha = glow * (1.0 - ring) + ring;
    fragColor = vec4(colour, alpha) * qt_Opacity;
}
