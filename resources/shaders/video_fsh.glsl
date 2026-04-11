#version 460

layout (location = 0) in vec2 vTexCoord;
layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D plane0;
layout (binding = 1) uniform sampler2D plane1;

#ifdef DITHER_NOISE
float interleavedGradientNoise(vec2 uv) {
    return fract(52.9829189 * fract(dot(uv, vec2(0.06711056, 0.00583715))));
}
#endif

void main()
{
    float y_raw = texture(plane0, vTexCoord).r;
    float u_raw = texture(plane1, vTexCoord).r;
    float v_raw = texture(plane1, vTexCoord).g;

    float y = (y_raw - 16.0/255.0) / ((235.0 - 16.0) / 255.0);
    float u = (u_raw - 16.0/255.0) / ((240.0 - 16.0) / 255.0) - 0.5;
    float v = (v_raw - 16.0/255.0) / ((240.0 - 16.0) / 255.0) - 0.5;

    float r = y + 1.5748 * v;
    float g = y - 0.18733 * u - 0.46812 * v;
    float b = y + 1.85563 * u;

    vec3 rgb = vec3(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0));
#ifdef DITHER_NOISE
    rgb += (DITHER_STRENGTH / 255.0) * interleavedGradientNoise(gl_FragCoord.xy) - (DITHER_STRENGTH / 510.0);
#endif
    outColor = vec4(rgb, 1.0);
}
