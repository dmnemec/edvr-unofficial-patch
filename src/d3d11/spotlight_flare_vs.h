// The replacement vertex shader for spotlight and station lens flare billboards.
//
// Written against the disassembly of the game's own vs 78F5F08D02EE38CC
// (dumped 2026-09-26, kept at docs/shaders/vs_78F5F08D02EE38CC.asm), and
// written as a MECHANICAL TRANSCRIPTION of it: register for register,
// instruction for instruction, with the same outputs feeding the pixel shader.
//
// ONE thing is changed, and it is the bug:
// In the original:
//     right = normalize(cross(cb1[278], cb1[279]))
//     up    = normalize(cross(cb1[279], right))
// cb1[278] and cb1[279] are cameraUp and cameraForward, so every quad rolls
// when you roll your head. Here, the basis is rebuilt per vertex aimed at
// the viewer, referenced to world up:
//     face  = normalize(particlePosition - viewerPosition)
//     right = normalize(cross(worldUp, face))
//     up    = cross(face, right)
//
// The viewer position arrives in our own constant buffer at b3 (CBP), solved
// CPU-side from the game's view-projection rows.
#pragma once

namespace edvr {

constexpr const char kSpotlightFlareWorldVS[] = R"HLSL(
cbuffer CB0 : register(b0) { float4 cb0[12]; };
cbuffer CB1 : register(b1) { float4 cb1[280]; };
cbuffer CB2 : register(b2) { float4 cb2[4]; };
cbuffer CBP : register(b3) {
    float4 pCam;
    float4 pUp;
};

struct VSIn {
    float4 v0  : POSITION0;
    float3 v1  : DIRECTION0;
    float2 v2  : DIMENSIONS0;
    float  v3  : ALIGNBLENDBRIGHT0;
    float4 v4  : AXIS0;
    float2 v5  : VERTEXALPHA0;
    int4   v6  : LIGHTINGATLASINDEX0;
    float4 v7  : UVSCURRENTDIFFUSE0;
    float4 v8  : UVSNEXTDIFFUSE0;
    float  v9  : TEXBLENDDIFFUSE0;
    uint   v10 : ATLASINDICESDIFFUSE0;
};

struct VSOut {
    float4 o0 : TEXCOORD3;
    float4 o1 : TEXCOORD5;
    float4 o2 : TEXCOORD6;
    float4 o3 : TEXCOORD10;
    float4 o4 : TEXCOORD14;
    float4 o5 : TEXCOORD15;
    float2 o6 : TEXCOORD18;
    float3 o7 : TEXCOORD21;
    float4 o8 : SV_POSITION;
};

VSOut main(VSIn i) {
    VSOut o = (VSOut)0;
    float4 r0, r1, r2, r3, r4, r5;

    r0.xyz = i.v0.xyz * cb1[222].z;
    r1.xy  = i.v2.xy * cb1[222].z;
    r1.z   = max(abs(r1.y), abs(r1.x));

    uint packed = asuint(i.v10);
    uint lowIdx = packed & 0x0000ffffu;
    uint highIdx = packed >> 16u;
    o.o1.w = (float)lowIdx;
    o.o2.w = (float)highIdx;

    o.o3.xy = i.v5.xy * i.v7.yw + i.v7.xz;

    r0.w = 1.0;
    r2.x = dot(cb0[9],  r0);
    r2.y = dot(cb0[10], r0);
    r2.z = dot(cb0[11], r0);

    // ---- billboard basis ----
    float3 faceAxis = cb1[279].xyz;
    if (pCam.w > 0.5) {
        float3 toCam = r2.xyz - pCam.xyz;
        float lenToCam = length(toCam);
        if (lenToCam > 1e-4) faceAxis = toCam / lenToCam;
    }
    float3 basisUp = (pCam.w > 0.5) ? pUp.xyz : cb1[278].xyz;

    float3 bRight = cross(basisUp, faceAxis);
    float bLen2 = dot(bRight, bRight);
    [branch] if (bLen2 < 1e-8) {
        bRight = cross(float3(0.0, 0.0, 1.0), faceAxis);
        bLen2 = dot(bRight, bRight);
        [branch] if (bLen2 < 1e-8) {
            bRight = float3(1.0, 0.0, 0.0);
            bLen2 = 1.0;
        }
    }
    r0.xyz = bRight * rsqrt(bLen2);

    r3.xyz = cross(faceAxis, r0.xyz);
    r0.w = max(dot(r3.xyz, r3.xyz), 1e-12);
    r3.xyz = r3.xyz * rsqrt(r0.w);

    // ---- near fade ----
    r0.w = dot(cb1[279].xyz, r2.xyz);
    r1.w = cb2[2].w * cb2[3].x;
    float fadeEdge = -cb2[2].w * cb2[3].x + cb2[2].w;
    float fadeDist = -r0.w + cb2[2].w;
    r1.w = saturate(fadeDist / r1.w);
    r1.w = -r1.w + 1.0;
    r0.w = (fadeEdge < r0.w) ? 1.0 : 0.0;
    r0.w = r0.w * r1.w;

    // ---- alignment mode ----
    r1.w = i.v0.w * 0.5;
    uint mode = (uint)(i.v3 * 255.0 + 0.5);
    [branch] if (mode == 0) {
        r4.xyz = faceAxis;
    } else {
        float3 dir = i.v1.xyz * 2.007874 - 1.0;
        float3 r6;
        r6.x = dot(cb0[9].xyz,  dir);
        r6.y = dot(cb0[10].xyz, dir);
        r6.z = dot(cb0[11].xyz, dir);
        float3 r5_dir = r6 * rsqrt(dot(r6, r6));
        [branch] if (mode == 1 || mode == 2) {
            float3 crossPos = cross(r2.xyz, r5_dir);
            r0.xyz = crossPos * rsqrt(dot(crossPos, crossPos));
            r4.xyz = cross(r0.xyz, cb1[279].xyz);
            r3.xyz = r5_dir;
            r1.w = 0.0;
        } else {
            float3 axisIn = i.v4.xyz * 2.007874 - 1.0;
            float3 r7;
            r7.x = dot(cb0[9].xyz,  axisIn);
            r7.y = dot(cb0[10].xyz, axisIn);
            r7.z = dot(cb0[11].xyz, axisIn);
            r4.xyz = r7 * rsqrt(dot(r7, r7));
            [branch] if (mode != 4) {
                float3 c1 = cross(r4.xyz, r5_dir);
                r0.xyz = c1 * rsqrt(dot(c1, c1));
                r3.xyz = cross(r0.xyz, r4.xyz);
            }
        }
    }

    // ---- spin matrix ----
    float spinS, spinC;
    sincos(r1.w * 2.0, spinS, spinC);
    float3 axisN = normalize(r4.xyz);
    float oneMinusC = 1.0 - spinC;
    float3x3 spin;
    spin[0] = float3(axisN.x * axisN.x * oneMinusC + spinC,
                     axisN.x * axisN.y * oneMinusC - axisN.z * spinS,
                     axisN.x * axisN.z * oneMinusC + axisN.y * spinS);
    spin[1] = float3(axisN.y * axisN.x * oneMinusC + axisN.z * spinS,
                     axisN.y * axisN.y * oneMinusC + spinC,
                     axisN.y * axisN.z * oneMinusC - axisN.x * spinS);
    spin[2] = float3(axisN.z * axisN.x * oneMinusC - axisN.y * spinS,
                     axisN.z * axisN.y * oneMinusC + axisN.x * spinS,
                     axisN.z * axisN.z * oneMinusC + spinC);
    r0.xyz = normalize(mul(spin, r0.xyz));
    r3.xyz = normalize(mul(spin, r3.xyz));

    // ---- corner position ----
    r5.xyz = r0.xyz * r1.x + r2.xyz;
    r5.xyz = r3.xyz * r1.y + r5.xyz;
    r0.w = (r0.w < 0.001) ? 0.0 : 1.0;
    r5.xyz = -r2.xyz + r5.xyz;
    r2.xyz = r5.xyz * r0.w + r2.xyz;

    // ---- lighting normal ----
    float2 sgn;
    sgn.x = (float)((r1.x < 0.0 ? 1 : 0) - (0.0 < r1.x ? 1 : 0));
    sgn.y = (float)((r1.y < 0.0 ? 1 : 0) - (0.0 < r1.y ? 1 : 0));
    float3 normPart = r3.xyz * sgn.y + r0.xyz * sgn.x;
    normPart = -r4.xyz * r1.w + normPart;
    o.o5.xyz = normPart * rsqrt(dot(normPart, normPart));

    // ---- world position & clip space ----
    r4.xyz = r2.y * cb1[230].xyz;
    r4.xyz = r2.x * cb1[229].xyz + r4.xyz;
    r4.xyz = r2.z * cb1[231].xyz + r4.xyz;
    o.o7.xyz = r4.xyz + cb1[232].xyz;

    r4 = r2.y * cb1[271];
    r4 = r2.x * cb1[270] + r4;
    r4 = r2.z * cb1[272] + r4;
    r4 = r4 + cb1[273];
    o.o0.xyz = r4.xyw;
    o.o0.w = i.v4.w;

    // ---- atlas UV / screen position ----
    if (i.v6.x >= 0) {
        float2 r4_xy = r1.xy / r1.z;
        r4_xy = r4_xy + 1.0;
        r4_xy = r4_xy * 0.5;
        float4 idx = float4(i.v6.w, i.v6.z, i.v6.x, i.v6.y);
        float3 r6_xyz = idx.xxy * cb1[222].xyy;
        float r4_x = r4_xy.x * r6_xyz.x;
        float r4_z = r4_xy.y * r6_xyz.y + r6_xyz.z;
        float2 r4_xy2 = r6_xyz.xy * idx.zw + float2(r4_x, r4_z);
        r4_z = r4_xy2.y - 1.0;
        r4_xy2 = (float2(r4_xy2.x, r4_z) + float2(-0.5, 0.5)) * 2.0;
        o.o8.xyz = float3(r4_xy2.x, r4_xy2.y, 0.0);
    } else {
        o.o8.xyz = float3(1000.0, 10000.0, -10.0);
    }
    o.o8.w = 1.0;

    // ---- remaining outputs ----
    o.o3.zw = i.v5.xy * i.v8.yw + i.v8.xz;
    o.o1.xyz = r0.xyz;
    o.o2.xyz = r3.xyz;
    o.o4.xyz = r2.xyz;
    o.o4.w = i.v9;
    o.o5.w = r1.z;
    o.o6.xy = r1.xy;

    return o;
}
)HLSL";

}  // namespace edvr
