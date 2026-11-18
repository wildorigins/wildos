#version 440

// The Hive face's honeycomb in one pass. The board lays out 78 hexagons, each
// tinted by its bearing from the centre and lit in turn as a ripple runs
// outwards. Everything about a hexagon follows from where its centre is, so
// the grid is worked out per pixel instead of being held as 156 items.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
};

const vec3 ground = vec3(4.0, 4.0, 5.0) / 255.0;
const float apothem = 28.58;
const vec2 lattice = vec2(64.086, 111.0);
const float ripple = 2.6;

vec3 hsl(float hue, float lightness)
{
    vec3 k = mod(vec3(0.0, 8.0, 4.0) + hue / 30.0, 12.0);
    float a = min(lightness, 1.0 - lightness);
    return lightness - a * clamp(min(k - 3.0, 9.0 - k), -1.0, 1.0);
}

// CSS ease-out, cubic-bezier(0, 0, 0.58, 1).
float easeOut(float p)
{
    float s = sqrt(clamp(p, 0.0, 1.0) / 1.74);
    for (int i = 0; i < 5; ++i) {
        float f = 1.74 * s * s - 0.74 * s * s * s - p;
        float d = max(3.48 * s - 2.22 * s * s, 1e-4);
        s = clamp(s - f / d, 0.0, 1.0);
    }
    return 3.0 * s * s - 2.0 * s * s * s;
}

float lit(float p)
{
    if (p < 0.14)
        return easeOut(p / 0.14);
    if (p < 0.6)
        return 1.0 - 0.7 * easeOut((p - 0.14) / 0.46);
    return 0.3 * (1.0 - easeOut((p - 0.6) / 0.4));
}

void main()
{
    vec2 p = qt_TexCoord0 * 640.0 - vec2(320.0);

    vec2 even = round(p / lattice) * lattice;
    vec2 odd = (round(p / lattice - 0.5) + 0.5) * lattice;
    vec2 centre = dot(p - even, p - even) < dot(p - odd, p - odd) ? even : odd;

    vec3 colour = ground;
    float reach = length(centre);
    if (reach > 180.0 && reach < 344.0) {
        vec2 q = abs(p - centre);
        float edge = max(q.x, 0.5 * q.x + 0.8660254 * q.y) - apothem;
        float soft = max(fwidth(edge), 1e-3);
        float inside = 1.0 - smoothstep(-0.5 * soft, 0.5 * soft, edge);
        float outline = 1.0 - smoothstep(-0.5 * soft, 0.5 * soft, abs(edge) - 1.0);

        float hue = mod(degrees(atan(centre.y, centre.x)) + 240.0, 360.0);
        vec3 body = hsl(hue, 0.62);
        vec3 bright = hsl(hue, 0.78);

        colour = mix(colour, body, 0.13 * inside);
        colour = mix(colour, body, 0.5 * outline);

        float glow = lit(fract(time / ripple + 1.0 - reach / 344.0));
        colour = mix(colour, body, glow * inside);
        colour = mix(colour, bright, glow * outline);
    }

    fragColor = vec4(colour, 1.0) * qt_Opacity;
}
