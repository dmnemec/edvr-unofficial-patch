// The replacement vertex shader for stretched, age-ramped particle billboards.
//
// Written against the disassembly of the game's own vs 68DDDEF04D9894AF
// (dumped 2026-09-06, kept at docs/shaders/particle-stretched-vs.asm), and
// written as a MECHANICAL TRANSCRIPTION of it: register for register,
// instruction for instruction, with the same outputs feeding the pixel shader.
//
// ONE thing is changed, and it is the bug:
// In the original:
//     corner = pos + cb1[277].xyz * cornerOffset.x + cb1[278].xyz * cornerOffset.y;
// cb1[277] and cb1[278] are the camera's right and up vectors, so every quad
// rolls when you roll your head. Here, the basis is rebuilt per vertex aimed
// at the viewer, referenced to world up:
//     face  = normalize(particlePosition - viewerPosition)
//     right = normalize(cross(worldUp, face))
//     up    = cross(face, right)
//
// The viewer position arrives in our own constant buffer at b3 (CBP), solved
// CPU-side from the game's own view-projection rows.
#pragma once

namespace edvr {

constexpr const char kParticleStretchedWorldVS[] = R"HLSL(
cbuffer CB0 : register(b0) { float4 cb0[2]; };
cbuffer CB1 : register(b1) { float4 cb1[280]; };
cbuffer CB2 : register(b2) { float4 cb2[6]; };
// Ours: the viewer's position in the same space the particle positions end
// up in, and the world's up axis. pCam.w is a flag -- zero means the solve
// was not available this draw and the original camera-plane basis is used,
// so a missing feed degrades to the game's own look rather than to garbage.
cbuffer CBP : register(b3) {
    float4 pCam;
    float4 pUp;
};

struct VSIn {
    float4 v0 : TEXCOORD0;    // xy = UV, zw = corner offsets
    float  v1 : TEXCOORD2;    // stretch threshold
    float4 v2 : POSITION0;    // xyz = direction, w = birth time
    float3 v3 : POSITION1;    // color
    float4 v4 : TEXCOORD3;    // per-particle parameters (x = size curve)
};

struct VSOut {
    float3 o0 : TEXCOORD0;
    float4 o1 : TEXCOORD1;
    float3 o2 : TEXCOORD2;
    float3 o3 : TEXCOORD3;
    float4 o4 : TEXCOORD5;
    float2 o5 : TEXCOORD6;
    float4 o6 : SV_POSITION;
};

VSOut main(VSIn i) {
    VSOut o = (VSOut)0;
    float4 r0, r1, r2, r3;

    r0.x = -cb1[72].z + 1.0;
    r0.x = r0.x * cb2[1].x;
    r0.y = cb1[72].z - 1.0;
    r0.x = r0.x * cb2[0].w;
    r0.y = r0.y * cb2[0].x;
    r0.x = (cb1[72].z >= 1.0) ? -r0.y : r0.x;
    r0.x = r0.x + i.v2.w;
    r0.y = r0.x - cb2[1].x;
    r0.x = -cb2[0].z * r0.y + r0.x;
    r0.y = (r0.x >= cb2[1].x) ? 1.0 : 0.0;

    r1.xyz = cb1[279].xyz * 10.0;
    r2.xyz = i.v2.xyz * cb0[0].xyz + cb0[1].xyz;
    r0.z = rsqrt(dot(r2.xyz, r2.xyz));
    r2.xyz = r0.z * r2.xyz;
    r3.x = dot(cb1[64].xyz, r2.xyz);
    r3.y = dot(cb1[65].xyz, r2.xyz);
    r3.z = dot(cb1[66].xyz, r2.xyz);
    r1.xyz = -r3.xyz * 10.0 - r1.xyz;
    r2.xyz = r3.xyz * 10.0;
    r0.yzw = r0.y * r1.xyz + r2.xyz;
    r1.xyz = -cb1[279].xyz * 10.0 - r0.yzw;
    r1.w = (i.v1 >= 0.01) ? 1.0 : 0.0;
    r0.yzw = r1.w * r1.xyz + r0.yzw;
    o.o0 = r0.yzw;

    r1.x = r0.x - cb2[1].x;
    r0.x = r0.x - cb2[5].z;
    r1.y = cb2[0].y - cb2[1].x;
    r1.x = saturate(r1.x / r1.y);
    r1.y = (0.0 < r1.x) ? 1.0 : 0.0;
    r1.z = -cb2[3].x + 1.0;
    r1.w = -cb2[3].y + 1.0;
    r1.x = r1.z * r1.x + cb2[3].x;
    r1.x = (r1.y != 0.0) ? r1.x : 0.0;
    r1.x = exp2(log2(max(r1.x, 1e-12)) * cb2[5].x);
    r1.x = min(r1.x, 1.0);
    o.o1.w = r1.x;
    o.o1.xyz = i.v3.xyz * cb1[72].w;

    r1.y = cb2[0].y - cb2[5].z;
    r1.y = 1.0 / r1.y;
    r0.x = saturate(r0.x * r1.y);
    r1.y = r0.x * -2.0 + 3.0;
    r0.x = r0.x * r0.x;
    r0.x = -r1.y * r0.x + 1.0;
    r0.x = exp2(log2(max(r0.x, 1e-12)) * cb2[5].w);
    r1.y = saturate(r0.x - cb2[3].y);
    r1.y = r1.y / r1.w;
    r2.z = r1.y * cb2[3].z;
    r1.y = saturate(r0.x - cb2[2].z);
    r1.z = -cb2[2].z + 1.0;
    r1.y = r1.y / r1.z;
    r2.x = r1.y * cb2[2].w;
    r1.y = saturate(r0.x - cb2[4].z);
    r1.z = -cb2[4].z + 1.0;
    r1.y = r1.y / r1.z;
    r2.y = r1.y * cb2[4].w;
    o.o2 = r2.xyz * cb2[1].z;

    r1.y = cb2[3].w - cb2[4].x;
    r1.x = r1.x * r1.y + cb2[4].x;
    r1.y = saturate(-cb2[3].w + cb2[4].y);
    r0.x = r0.x * r1.y + r1.x;
    r0.x = exp2(log2(max(r0.x, 1e-12)) * cb2[5].y);

    r1.x = i.v4.x + 1.0;
    r0.x = r0.x * r1.x;
    r1.xy = r0.xx * i.v0.zw;

    // ---- Billboard basis: stock vs steady ----
    float3 faceAxis = cb1[279].xyz;
    if (pCam.w > 0.5) {
        float3 toCam = r0.yzw - pCam.xyz;
        float lenToCam = length(toCam);
        if (lenToCam > 1e-4) faceAxis = toCam / lenToCam;
    }
    float3 basisUp = (pCam.w > 0.5) ? pUp.xyz : cb1[278].xyz;

    float3 bRight = cross(basisUp, faceAxis);
    float bLen2 = dot(bRight, bRight);
    if (bLen2 < 1e-8) {
        bRight = cross(float3(0.0, 0.0, 1.0), faceAxis);
        bLen2 = dot(bRight, bRight);
        if (bLen2 < 1e-8) {
            bRight = float3(1.0, 0.0, 0.0);
            bLen2 = 1.0;
        }
    }
    r2.xyz = bRight * rsqrt(bLen2);
    r3.xyz = cross(faceAxis, r2.xyz);
    float uLen2 = max(dot(r3.xyz, r3.xyz), 1e-12);
    r3.xyz = r3.xyz * rsqrt(uLen2);

    float3 rightAxis = (pCam.w > 0.5) ? r2.xyz : cb1[277].xyz;
    float3 upAxis    = (pCam.w > 0.5) ? r3.xyz : cb1[278].xyz;

    r0.xyz = r0.yzw + rightAxis * r1.x + upAxis * r1.y;

    o.o3 = r0.xyz;
    o.o4 = i.v4;
    o.o5 = i.v0.xy;

    r1 = r0.y * cb1[271];
    r1 = r0.x * cb1[270] + r1;
    r0 = r0.z * cb1[272] + r1;
    o.o6 = r0 + cb1[273];

    return o;
}
)HLSL";

}  // namespace edvr
