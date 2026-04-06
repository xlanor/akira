#version 460

// AMD FidelityFX Super Resolution 1.0 - EASU (Edge Adaptive Spatial Upsampling)
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// MIT License - see references/FidelityFX-FSR/license.txt

layout (location = 0) in vec2 vTexCoord;
layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D inputTexture;

layout (binding = 0, std140) uniform FsrEasuConstants {
    uvec4 Const0;
    uvec4 Const1;
    uvec4 Const2;
    uvec4 Const3;
};

float APrxLoRcpF1(float a) { return uintBitsToFloat(0x7ef07ebbu - floatBitsToUint(a)); }
float APrxLoRsqF1(float a) { return uintBitsToFloat(0x5f347d74u - (floatBitsToUint(a) >> 1u)); }
float AMin3F1(float x, float y, float z) { return min(x, min(y, z)); }
float AMax3F1(float x, float y, float z) { return max(x, max(y, z)); }
vec3 AMin3F3(vec3 x, vec3 y, vec3 z) { return min(x, min(y, z)); }
vec3 AMax3F3(vec3 x, vec3 y, vec3 z) { return max(x, max(y, z)); }

vec2 _ts = vec2(textureSize(inputTexture, 0));

void FsrEasuFetch12(vec2 p0,
    out vec4 bczzR, out vec4 bczzG, out vec4 bczzB,
    out vec4 ijfeR, out vec4 ijfeG, out vec4 ijfeB,
    out vec4 klhgR, out vec4 klhgG, out vec4 klhgB,
    out vec4 zzonR, out vec4 zzonG, out vec4 zzonB)
{
    ivec2 f = ivec2(floor(p0 * _ts - 0.5));

    vec3 tb = texelFetch(inputTexture, f + ivec2(0,-1), 0).rgb;
    vec3 tc = texelFetch(inputTexture, f + ivec2(1,-1), 0).rgb;
    vec3 te = texelFetch(inputTexture, f + ivec2(-1,0), 0).rgb;
    vec3 tf = texelFetch(inputTexture, f,               0).rgb;
    vec3 tg = texelFetch(inputTexture, f + ivec2(1, 0), 0).rgb;
    vec3 th = texelFetch(inputTexture, f + ivec2(2, 0), 0).rgb;
    vec3 ti = texelFetch(inputTexture, f + ivec2(-1,1), 0).rgb;
    vec3 tj = texelFetch(inputTexture, f + ivec2(0, 1), 0).rgb;
    vec3 tk = texelFetch(inputTexture, f + ivec2(1, 1), 0).rgb;
    vec3 tl = texelFetch(inputTexture, f + ivec2(2, 1), 0).rgb;
    vec3 tn = texelFetch(inputTexture, f + ivec2(0, 2), 0).rgb;
    vec3 to = texelFetch(inputTexture, f + ivec2(1, 2), 0).rgb;

    bczzR = vec4(tb.r, tc.r, 0.0, 0.0);
    bczzG = vec4(tb.g, tc.g, 0.0, 0.0);
    bczzB = vec4(tb.b, tc.b, 0.0, 0.0);
    ijfeR = vec4(ti.r, tj.r, tf.r, te.r);
    ijfeG = vec4(ti.g, tj.g, tf.g, te.g);
    ijfeB = vec4(ti.b, tj.b, tf.b, te.b);
    klhgR = vec4(tk.r, tl.r, th.r, tg.r);
    klhgG = vec4(tk.g, tl.g, th.g, tg.g);
    klhgB = vec4(tk.b, tl.b, th.b, tg.b);
    zzonR = vec4(0.0, 0.0, to.r, tn.r);
    zzonG = vec4(0.0, 0.0, to.g, tn.g);
    zzonB = vec4(0.0, 0.0, to.b, tn.b);
}

void FsrEasuTapF(
    inout vec3 aC,
    inout float aW,
    vec2 off,
    vec2 dir,
    vec2 len,
    float lob,
    float clp,
    vec3 c)
{
    vec2 v;
    v.x = (off.x * dir.x) + (off.y * dir.y);
    v.y = (off.x * (-dir.y)) + (off.y * dir.x);
    v *= len;
    float d2 = v.x * v.x + v.y * v.y;
    d2 = min(d2, clp);
    float wB = (2.0 / 5.0) * d2 + (-1.0);
    float wA = lob * d2 + (-1.0);
    wB *= wB;
    wA *= wA;
    wB = (25.0 / 16.0) * wB + (-(25.0 / 16.0 - 1.0));
    float w = wB * wA;
    aC += c * w;
    aW += w;
}

void FsrEasuSet(
    inout vec2 dir,
    inout float len,
    float w,
    float lA, float lB, float lC, float lD, float lE)
{
    float dc = lD - lC;
    float cb = lC - lB;
    float lenX = max(abs(dc), abs(cb));
    lenX = APrxLoRcpF1(lenX);
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    lenX *= lenX;
    len += lenX * w;
    float ec = lE - lC;
    float ca = lC - lA;
    float lenY = max(abs(ec), abs(ca));
    lenY = APrxLoRcpF1(lenY);
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    lenY *= lenY;
    len += lenY * w;
}

void FsrEasuF(out vec3 pix, uvec2 ip, uvec4 con0, uvec4 con1, uvec4 con2, uvec4 con3)
{
    vec2 pp = vec2(ip) * uintBitsToFloat(con0.xy) + uintBitsToFloat(con0.zw);
    vec2 fp = floor(pp);
    pp -= fp;

    vec2 p0 = fp * uintBitsToFloat(con1.xy) + uintBitsToFloat(con1.zw);
    vec2 p1 = p0 + uintBitsToFloat(con2.xy);
    vec2 p2 = p0 + uintBitsToFloat(con2.zw);
    vec2 p3 = p0 + uintBitsToFloat(con3.xy);

    vec4 bczzR, bczzG, bczzB;
    vec4 ijfeR, ijfeG, ijfeB;
    vec4 klhgR, klhgG, klhgB;
    vec4 zzonR, zzonG, zzonB;
    FsrEasuFetch12(p0,
        bczzR, bczzG, bczzB,
        ijfeR, ijfeG, ijfeB,
        klhgR, klhgG, klhgB,
        zzonR, zzonG, zzonB);

    vec4 bczzL = bczzB * 0.5 + (bczzR * 0.5 + bczzG);
    vec4 ijfeL = ijfeB * 0.5 + (ijfeR * 0.5 + ijfeG);
    vec4 klhgL = klhgB * 0.5 + (klhgR * 0.5 + klhgG);
    vec4 zzonL = zzonB * 0.5 + (zzonR * 0.5 + zzonG);

    float bL = bczzL.x; float cL = bczzL.y;
    float iL = ijfeL.x; float jL = ijfeL.y; float fL = ijfeL.z; float eL = ijfeL.w;
    float kL = klhgL.x; float lL = klhgL.y; float hL = klhgL.z; float gL = klhgL.w;
    float oL = zzonL.z; float nL = zzonL.w;

    vec2 dir = vec2(0.0);
    float len = 0.0;
    FsrEasuSet(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
    FsrEasuSet(dir, len, pp.x * (1.0 - pp.y),          cL, fL, gL, hL, kL);
    FsrEasuSet(dir, len, (1.0 - pp.x) * pp.y,          fL, iL, jL, kL, nL);
    FsrEasuSet(dir, len, pp.x * pp.y,                   gL, jL, kL, lL, oL);

    vec2 dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    bool zro = dirR < (1.0 / 32768.0);
    dirR = APrxLoRsqF1(dirR);
    dirR = zro ? 1.0 : dirR;
    dir.x = zro ? 1.0 : dir.x;
    dir *= vec2(dirR);

    len = len * 0.5;
    len *= len;

    float stretch = (dir.x * dir.x + dir.y * dir.y) * APrxLoRcpF1(max(abs(dir.x), abs(dir.y)));
    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 + (-0.5) * len);
    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp = APrxLoRcpF1(lob);

    vec3 min4 = min(AMin3F3(vec3(ijfeR.z, ijfeG.z, ijfeB.z), vec3(klhgR.w, klhgG.w, klhgB.w), vec3(ijfeR.y, ijfeG.y, ijfeB.y)),
                    vec3(klhgR.x, klhgG.x, klhgB.x));
    vec3 max4 = max(AMax3F3(vec3(ijfeR.z, ijfeG.z, ijfeB.z), vec3(klhgR.w, klhgG.w, klhgB.w), vec3(ijfeR.y, ijfeG.y, ijfeB.y)),
                    vec3(klhgR.x, klhgG.x, klhgB.x));

    vec3 aC = vec3(0.0);
    float aW = 0.0;
    FsrEasuTapF(aC, aW, vec2( 0.0,-1.0) - pp, dir, len2, lob, clp, vec3(bczzR.x, bczzG.x, bczzB.x));
    FsrEasuTapF(aC, aW, vec2( 1.0,-1.0) - pp, dir, len2, lob, clp, vec3(bczzR.y, bczzG.y, bczzB.y));
    FsrEasuTapF(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, vec3(ijfeR.x, ijfeG.x, ijfeB.x));
    FsrEasuTapF(aC, aW, vec2( 0.0, 1.0) - pp, dir, len2, lob, clp, vec3(ijfeR.y, ijfeG.y, ijfeB.y));
    FsrEasuTapF(aC, aW, vec2( 0.0, 0.0) - pp, dir, len2, lob, clp, vec3(ijfeR.z, ijfeG.z, ijfeB.z));
    FsrEasuTapF(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, vec3(ijfeR.w, ijfeG.w, ijfeB.w));
    FsrEasuTapF(aC, aW, vec2( 1.0, 1.0) - pp, dir, len2, lob, clp, vec3(klhgR.x, klhgG.x, klhgB.x));
    FsrEasuTapF(aC, aW, vec2( 2.0, 1.0) - pp, dir, len2, lob, clp, vec3(klhgR.y, klhgG.y, klhgB.y));
    FsrEasuTapF(aC, aW, vec2( 2.0, 0.0) - pp, dir, len2, lob, clp, vec3(klhgR.z, klhgG.z, klhgB.z));
    FsrEasuTapF(aC, aW, vec2( 1.0, 0.0) - pp, dir, len2, lob, clp, vec3(klhgR.w, klhgG.w, klhgB.w));
    FsrEasuTapF(aC, aW, vec2( 1.0, 2.0) - pp, dir, len2, lob, clp, vec3(zzonR.z, zzonG.z, zzonB.z));
    FsrEasuTapF(aC, aW, vec2( 0.0, 2.0) - pp, dir, len2, lob, clp, vec3(zzonR.w, zzonG.w, zzonB.w));

    pix = min(max4, max(min4, aC * vec3(1.0 / aW)));
}

void main()
{
    uvec2 ip = uvec2(gl_FragCoord.xy);
    vec3 c;
    FsrEasuF(c, ip, Const0, Const1, Const2, Const3);
    outColor = vec4(c, 1.0);
}
