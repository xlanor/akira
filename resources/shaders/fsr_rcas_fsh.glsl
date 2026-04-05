#version 460

layout (location = 0) in vec2 vTexCoord;
layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D inputTexture;

layout (binding = 0, std140) uniform FsrRcasConstants {
    uvec4 Const0;
};

#define FSR_RCAS_LIMIT (0.25 - (1.0 / 16.0))

float APrxMedRcpF1(float a) {
    float b = uintBitsToFloat(0x7ef19fffu - floatBitsToUint(a));
    return b * (-b * a + 2.0);
}

float AMin3F1(float x, float y, float z) { return min(x, min(y, z)); }
float AMax3F1(float x, float y, float z) { return max(x, max(y, z)); }

vec4 FsrRcasLoadF(ivec2 p) { return texelFetch(inputTexture, p, 0); }

void main()
{
    ivec2 sp = ivec2(gl_FragCoord.xy);

    vec3 b = FsrRcasLoadF(sp + ivec2( 0, -1)).rgb;
    vec3 d = FsrRcasLoadF(sp + ivec2(-1,  0)).rgb;
    vec3 e = FsrRcasLoadF(sp).rgb;
    vec3 f = FsrRcasLoadF(sp + ivec2( 1,  0)).rgb;
    vec3 h = FsrRcasLoadF(sp + ivec2( 0,  1)).rgb;

    float bL = b.b * 0.5 + (b.r * 0.5 + b.g);
    float dL = d.b * 0.5 + (d.r * 0.5 + d.g);
    float eL = e.b * 0.5 + (e.r * 0.5 + e.g);
    float fL = f.b * 0.5 + (f.r * 0.5 + f.g);
    float hL = h.b * 0.5 + (h.r * 0.5 + h.g);

    float nz = 0.25 * bL + 0.25 * dL + 0.25 * fL + 0.25 * hL - eL;
    nz = clamp(abs(nz) * APrxMedRcpF1(AMax3F1(AMax3F1(bL, dL, eL), fL, hL) - AMin3F1(AMin3F1(bL, dL, eL), fL, hL)), 0.0, 1.0);
    nz = -0.5 * nz + 1.0;

    float mn4R = min(AMin3F1(b.r, d.r, f.r), h.r);
    float mn4G = min(AMin3F1(b.g, d.g, f.g), h.g);
    float mn4B = min(AMin3F1(b.b, d.b, f.b), h.b);
    float mx4R = max(AMax3F1(b.r, d.r, f.r), h.r);
    float mx4G = max(AMax3F1(b.g, d.g, f.g), h.g);
    float mx4B = max(AMax3F1(b.b, d.b, f.b), h.b);

    float hitMinR = mn4R / (4.0 * mx4R);
    float hitMinG = mn4G / (4.0 * mx4G);
    float hitMinB = mn4B / (4.0 * mx4B);
    float hitMaxR = (1.0 - mx4R) / (4.0 * mn4R + (-4.0));
    float hitMaxG = (1.0 - mx4G) / (4.0 * mn4G + (-4.0));
    float hitMaxB = (1.0 - mx4B) / (4.0 * mn4B + (-4.0));
    float lobeR = max(-hitMinR, hitMaxR);
    float lobeG = max(-hitMinG, hitMaxG);
    float lobeB = max(-hitMinB, hitMaxB);
    float lobe = max(-FSR_RCAS_LIMIT, min(AMax3F1(lobeR, lobeG, lobeB), 0.0)) * uintBitsToFloat(Const0.x);

    lobe *= nz;

    float rcpL = APrxMedRcpF1(4.0 * lobe + 1.0);
    float pixR = (lobe * b.r + lobe * d.r + lobe * h.r + lobe * f.r + e.r) * rcpL;
    float pixG = (lobe * b.g + lobe * d.g + lobe * h.g + lobe * f.g + e.g) * rcpL;
    float pixB = (lobe * b.b + lobe * d.b + lobe * h.b + lobe * f.b + e.b) * rcpL;

    outColor = vec4(pixR, pixG, pixB, 1.0);
}
