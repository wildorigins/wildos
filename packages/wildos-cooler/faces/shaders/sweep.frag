#version 440

// The Neon face's sweep: a green arc that brightens towards its head, turning
// in a band near the rim. The board blurs it a little before cutting it to
// the band; the arc only changes along its length, so the blur is taken
// along the arc here, plus the fade the blur gives the rim itself.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float turn;
    vec4 tint;
};

const float blur = 7.43;
const float tau = 6.2831853;

float arc(float f)
{
    f = fract(f);
    if (f < 0.78)
        return 0.0;
    if (f < 0.99)
        return 0.85 * (f - 0.78) / 0.21;
    return 0.85 * (1.0 - (f - 0.99) / 0.01);
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
    float spread = blur / (tau * r);

    float strength = 0.0;
    float total = 0.0;
    for (int i = -4; i <= 4; ++i) {
        float shift = 0.625 * float(i);
        float weight = exp(-0.5 * shift * shift);
        strength += weight * arc(along + shift * spread);
        total += weight;
    }
    strength /= total;

    float rim = r / 320.0;
    float band = clamp((rim - 0.82) / 0.01, 0.0, 1.0) * (1.0 - clamp((rim - 0.96) / 0.01, 0.0, 1.0));
    float fade = 0.5 * (1.0 + erf((320.0 - r) / (blur * 1.4142136)));

    float alpha = strength * band * fade * qt_Opacity;
    fragColor = vec4(tint.rgb * alpha, alpha);
}
