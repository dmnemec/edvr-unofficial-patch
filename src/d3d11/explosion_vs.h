// The replacement vertex shader for explosion billboards.
//
// Written against the disassembly of the game's own vs 9F4BBCFCD3B68BC9
// (dumped 2026-09-26, kept at docs/shaders/vs_9F4BBCFCD3B68BC9.asm), and
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

constexpr const char kExplosionWorldVS[] = R"HLSL(
cbuffer CB0 : register(b0) { float4 cb0[12]; };
cbuffer CB1 : register(b1) { float4 cb1[280]; };
cbuffer CB2 : register(b2) { float4 cb2[3]; };
cbuffer CBP : register(b3) {
    float4 pCam;
    float4 pUp;
};

struct VSIn {
    float4 v0  : POSITION0;
    float3 v1  : DIRECTION0;
    float2 v2  : DIMENSIONS0;
    float2 v3  : ALIGNBLENDBRIGHT0;
    float  v4  : BRIGHTNESS0;
    float4 v5  : AXIS0;
    float2 v6  : VERTEXALPHA0;
    float4 v7  : UVSCURRENTDIFFUSE0;
    float4 v8  : UVSNEXTDIFFUSE0;
    float  v9  : TEXBLENDDIFFUSE0;
    uint   v10 : ATLASINDICESDIFFUSE0;
    float4 v11 : COLOUR0;
};

struct VSOut {
    float4 o0  : TEXCOORD1;
    float4 o1  : TEXCOORD5;
    float2 o2  : TEXCOORD9;
    float4 o3  : TEXCOORD11;
    float4 o4  : TEXCOORD12;
    float4 o5  : SV_POSITION;
};

VSOut main(VSIn i) {
    VSOut o = (VSOut)0;
    float4 r0, r1, r2, r3, r4;

    r0.xyz = i.v0.xyz * cb1[222].z;
    r1.xy  = i.v2.xy * cb1[222].z;
    o.o2.y = max(abs(r1.y), abs(r1.x));

    uint packed = asuint(i.v10);
    uint lowIdx = packed & 0x0000ffffu;
    uint highIdx = packed >> 16u;
    o.o0.y = (float)lowIdx;
    o.o0.z = (float)highIdx;

    o.o1.xy = i.v6.xy * i.v7.yw + i.v7.xz;

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
    r1.w = cb2[1].w * cb2[2].x;
    r2.w = -cb2[1].w * cb2[2].x + cb2[1].w;
    r3.w = -r0.w + cb2[1].w;
    r1.w = saturate(r3.w / r1.w);
    r1.w = -r1.w + 1.0;
    r0.w = (r2.w < r0.w) ? 1.0 : 0.0;
    r0.w = r0.w * r1.w;

    // ---- alignment mode ----
    r1.z = i.v0.w * 0.5;
    uint mode = (uint)(i.v3.x * 255.0 + 0.5);
    [branch] if (mode == 0) {
        r4.xyz = faceAxis;
    } else {
        float3 dir = i.v1.xyz * 2.007874 - 1.0;
        float3 r6;
        r6.x = dot(cb0[9].xyz,  dir);
        r6.y = dot(cb0[10].xyz, dir);
        r6.z = dot(cb0[11].xyz, dir);
        float3 r5 = r6 * rsqrt(dot(r6, r6));
        [branch] if (mode == 1 || mode == 2) {
            float3 crossPos = cross(r2.xyz, r5);
            r0.xyz = crossPos * rsqrt(dot(crossPos, crossPos));
            r4.xyz = cross(r0.xyz, cb1[279].xyz);
            r3.xyz = r5;
            r1.z = 0.0;
        } else {
            float3 axisIn = i.v5.xyz * 2.007874 - 1.0;
            float3 r7;
            r7.x = dot(cb0[9].xyz,  axisIn);
            r7.y = dot(cb0[10].xyz, axisIn);
            r7.z = dot(cb0[11].xyz, axisIn);
            r4.xyz = r7 * rsqrt(dot(r7, r7));
            [branch] if (mode != 4) {
                r0.xyz = cross(r4.xyz, r5);
                r3.xyz = cross(r0.xyz, r4.xyz);
            }
        }
    }

    // ---- quaternion spin ----
    float spinS, spinC;
    sincos(r1.z, spinS, spinC);
    float3 qv = r4.xyz * spinS;
    float  qc = spinC;
    float  twoC = qc + qc;
    float3 twoQv = qv + qv;
    float  scale = qc * twoC - 1.0;

    float3 crossR = cross(qv, r0.xyz);
    float  dotR   = dot(qv, r0.xyz);
    float3 rotR   = r0.xyz * scale + twoQv * dotR + crossR * twoC;

    float3 crossU = cross(qv, r3.xyz);
    float  dotU   = dot(qv, r3.xyz);
    float3 rotU   = r3.xyz * scale + twoQv * dotU + crossU * twoC;

    r0.xyz = rotR;
    r3.xyz = rotU;

    // ---- corner position ----
    float3 cornerPos = r0.xyz * r1.x + r2.xyz;
    cornerPos = r3.xyz * r1.y + cornerPos;
    float vis = (r0.w < 0.001) ? 0.0 : 1.0;
    r0.xyz = (cornerPos - r2.xyz) * vis + r2.xyz;

    // ---- clip position ----
    float4 clip = r0.y * cb1[271];
    clip = r0.x * cb1[270] + clip;
    clip = r0.z * cb1[272] + clip;
    clip = clip + cb1[273];
    o.o5 = clip;

    // ---- remaining outputs ----
    o.o3.xyz = i.v4.xxx * i.v11.xyz;
    o.o3.w   = r0.w * i.v11.w;
    o.o2.x   = dot(r0.xyz, cb1[279].xyz);
    o.o1.zw  = i.v6.xy * i.v8.yw + i.v8.xz;
    o.o0.x   = i.v5.w;
    o.o0.w   = i.v9;
    o.o4.xyz = float3(1.0, 1.0, 1.0);
    o.o4.w   = i.v3.y * 255.0;

    return o;
}
)HLSL";

}  // namespace edvr
