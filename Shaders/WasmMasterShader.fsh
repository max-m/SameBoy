precision mediump float;

uniform sampler2D image;
uniform sampler2D previous_image;
uniform bool mix_previous;

uniform vec2 input_resolution;
uniform vec2 output_resolution;
uniform vec2 origin;

#define equal(x, y) ((x) == (y))
#define inequal(x, y) ((x) != (y))
#define STATIC

#if VERSION >= 0x300
#define FRAG_COLOR fragColor
    out vec4 FRAG_COLOR;
#else
#define FRAG_COLOR gl_FragColor
    vec4 texture(sampler2D s, vec2 c)          { return texture2D(s, c); }
    vec4 texture(sampler2D s, vec2 c, float b) { return texture2D(s, c, b); }
#endif

#line 1
{filter}

void main()
{
    vec2 position = gl_FragCoord.xy - origin;
    position /= output_resolution;
    position.y = 1.0 - position.y;

    if (mix_previous) {
        FRAG_COLOR = mix(scale(image, position, input_resolution, output_resolution),
                         scale(previous_image, position, input_resolution, output_resolution), 0.5);
    }
    else {
        FRAG_COLOR = scale(image, position, input_resolution, output_resolution);
    }
}
