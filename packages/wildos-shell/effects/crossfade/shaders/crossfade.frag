// Puts a window on screen part way between its look before a colour change
// and its look now. The effect prepends the #version line and the IN,
// TEXTURE and FRAG_COLOR macros.

#include "colormanagement.glsl"
#include "saturation.glsl"

// The window now, and its snapshot from before the change.
uniform sampler2D sampler;
uniform sampler2D sampler1;
uniform vec4 modulation;
// 0 the old look, 1 the new.
uniform float progress;

IN vec2 texcoord0;

void main()
{
    // Both are premultiplied, so mixing them mixes their coverage too: a
    // shadow that is the same before and after stays exactly as it is.
    vec4 result = mix(TEXTURE(sampler1, texcoord0), TEXTURE(sampler, texcoord0), progress);

    result = sourceEncodingToNitsInDestinationColorspace(result);
    result = adjustSaturation(result);
    result *= modulation;
    FRAG_COLOR = nitsToDestinationEncoding(result);
}
