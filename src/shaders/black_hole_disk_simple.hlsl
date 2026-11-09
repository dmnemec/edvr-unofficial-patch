// HLSL Accretion Disk Shader: visual.black_hole_disk = simple | realistic
// Features:
// - World-space ray reconstruction from camera basis & asymmetric projection
// - Fixed 3D world-space disk spin axis & basis (diskNormal, diskTangentU, diskTangentV)
//   so orbiting around the black hole continuously updates the 3D viewing inclination & azimuth
// - Continuous C^infinity equator crossing (zero orientation pop when flying from top to bottom hemisphere!)
// - Unified Beloborodov (2002) / Luminet (1979) 3D relativistic geodesic plane intersection:
//   seamlessly bends the rear half of the equatorial accretion disk up and over the shadow
//   and forms the secondary lensed arch beneath the shadow
// - High-contrast concentric MRI accretion rings, dark radial gaps, and sheared spiral filaments
//   with crisp outer cutoff (zero diffuse yellow fog/haze)
// - Mode 1 (Simple): Interstellar / Gargantua-style warm golden-amber-white spectrum (T_eff ~ 5500 K)
// - Mode 2 (Realistic): Astrophysical Stellar-Mass Kerr Black Hole (M ~ 10 M_sun, a*=0.85, T_peak > 14000 K)
//   with O/B-type electric blue-white Planckian inner vortex, violet-indigo outer flare,
//   Boyer-Lindquist frame-dragging shadow shift, and 3D-aligned Kerr Geodesic LUT (n=2 sub-ring)
// - Reversed-Z read-only far plane output (z = 0.0, w = 1.0) with premultiplied alpha

cbuffer BlackHoleDiskCb : register(b0)
{
    float4 camRight;     // xyz = World Right, w = P00
    float4 camUp;        // xyz = World Up,    w = P11
    float4 camFwd;       // xyz = World Fwd,   w = P02
    float4 projParams;   // x = P12, y = timeSeconds, z = mode (1=simple, 2=realistic), w = spin a*
    float4 bhWorldDir;   // xyz = BH unit direction in world space, w = theta_g (GM / c^2 D)
    float4 diskNormal;   // xyz = Fixed world-space disk spin axis, w = r_isco (in M)
    float4 diskTangentU; // xyz = Fixed world-space in-plane U axis, w = r_out (in M)
    float4 diskTangentV; // xyz = Fixed world-space in-plane V axis, w = peakTempK
    float4 invViewProj0; // Sky dome InvViewProj row 0
    float4 invViewProj1; // Sky dome InvViewProj row 1
    float4 invViewProj2; // Sky dome InvViewProj row 2
    float4 invViewProj3; // Sky dome InvViewProj row 3
    float4 params;       // x = useInvViewProj, y = exposureScale, z = r_horizon (in M), w = b_crit (in M)
    float4 pad[3];       // Pad to 256 bytes
};

Texture2D<float4> GeodesicLut : register(t0);
SamplerState LinearClampSampler : register(s0);

struct VSOutput
{
    float4 pos  : SV_POSITION;
    float2 grid : TEXCOORD0;
};

VSOutput VSMain(uint vid : SV_VertexID)
{
    VSOutput o;
    // Standard full-screen triangle covering [-1, 1] NDC:
    // vid 0: (-1, -1), vid 1: (-1, 3), vid 2: (3, -1)
    float2 grid = float2((vid == 2) ? 3.0 : -1.0, (vid == 1) ? 3.0 : -1.0);
    
    // Reversed-Z far plane: z = 0.0, w = 1.0
    o.pos = float4(grid, 0.0, 1.0);
    o.grid = grid;
    return o;
}

// Normalized Novikov-Thorne / Page-Thorne radial flux with sharp outer falloff (zero diffuse haze):
float DiskEmissionProfile(float r, float r_isco, float r_out)
{
    if (r < r_isco || r > r_out) return 0.0;
    float u = r_isco / max(r, 0.001);
    float flux = sqrt(max(0.0, 1.0 - sqrt(max(u, 0.0)))) * pow(max(u, 0.0), 2.35) * 6.0;
    float innerEdge = smoothstep(r_isco, r_isco + 0.35, r);
    // Crisp outer boundary so the disk ends as distinct rings rather than a diffuse fog
    float outerNorm = saturate((r - r_isco) / max(r_out - r_isco, 1.0));
    float outerEdge = (1.0 - smoothstep(0.68, 0.98, outerNorm)) * exp(-2.2 * outerNorm * outerNorm);
    return flux * innerEdge * outerEdge;
}

// Relativistic thermal and Doppler spectrum mapping:
// - Mode 1 (Simple): Interstellar / Gargantua cinematic golden-amber-white spectrum (supermassive T_eff ~ 5500 K)
// - Mode 2 (Realistic): Astrophysical Stellar-Mass Black Hole (M ~ 5..20 M_sun, T_peak >= 14,000 K)
//   True Planckian O/B-type UV/X-ray thermal spectrum where T(r) * g shifts from deep crimson-violet
//   at the redshifted outer rim, through warm ivory-white at the midline, to dazzling ice-cyan and
//   actinic blue-white at the relativistic Kerr ISCO and approaching Doppler-beamed arm!
float3 AccretionTemperatureToRGB(float T_norm, float mode, float peakTempK)
{
    if (mode > 1.5)
    {
        // Scale by stellar mass temperature factor from journal (default 14000 K -> 1.0)
        float massTempScale = clamp(peakTempK / 14000.0, 0.75, 1.35);
        float T = T_norm * massTempScale;

        float3 colOuterRedshift = float3(0.75, 0.18, 0.38); // Deep Doppler-redshifted magenta-crimson outer edge
        float3 colMidWarm       = float3(1.35, 1.15, 1.30); // Neutral lavender-ivory transition (T ~ 8,500 K)
        float3 colCyanWhite     = float3(1.45, 1.95, 2.65); // Hot B-type stellar plasma (T ~ 15,000 K)
        float3 colActinicBlue   = float3(1.85, 2.35, 3.25); // Relativistic Doppler-beamed O-type UV/blue-white core

        if (T < 0.55)
        {
            return lerp(colOuterRedshift, colMidWarm, saturate(T / 0.55));
        }
        else if (T < 1.05)
        {
            return lerp(colMidWarm, colCyanWhite, saturate((T - 0.55) / 0.50));
        }
        else
        {
            return lerp(colCyanWhite, colActinicBlue, saturate((T - 1.05) / 0.85));
        }
    }
    else
    {
        float3 colDeepOrange    = float3(1.15, 0.20, 0.02);
        float3 colBrilliantGold = float3(1.95, 1.15, 0.22);
        float3 colYellowWhite   = float3(2.35, 1.95, 1.05);
        float3 colHotGoldWhite  = float3(2.55, 2.35, 1.75);

        if (T_norm < 0.65)
        {
            return lerp(colDeepOrange, colBrilliantGold, saturate(T_norm / 0.65));
        }
        else if (T_norm < 1.25)
        {
            return lerp(colBrilliantGold, colYellowWhite, saturate((T_norm - 0.65) / 0.60));
        }
        else
        {
            return lerp(colYellowWhite, colHotGoldWhite, saturate((T_norm - 1.25) / 0.95));
        }
    }
}

// High-contrast concentric MRI accretion rings + sheared Keplerian spiral filaments:
// Eliminates featureless haze by carving dark gaps between bright relativistic plasma bands
float DiskStructureAndTurbulence(float r, float phi, float timeSec, float r_isco, float a_spin)
{
    float omega_orb = 1.0 / (pow(max(r, 1.0), 1.5) + a_spin);
    float ln_r = log(max(r / max(r_isco, 1.0), 1.0));
    float radNorm = r - r_isco;

    // Multi-frequency concentric radial ring bands (creates crisp Gargantua-style dark gaps and bright rings)
    float ringWave1 = 0.5 + 0.5 * sin(radNorm * 2.65 - timeSec * 0.15);
    float ringWave2 = 0.5 + 0.5 * cos(radNorm * 5.80 + 1.1);
    float ringWave3 = 0.5 + 0.5 * sin(ln_r * 11.5 - 0.7);
    float ringBands = pow(ringWave1, 1.6) * (0.35 + 0.65 * pow(ringWave2, 1.8)) * (0.45 + 0.55 * ringWave3);

    // 5-mode sheared logarithmic spiral arms rotating at local relativistic Keplerian angular velocity
    const float m[5]     = { 1.0, 2.0, 3.0, 4.0, 6.0 };
    const float k[5]     = { 3.2, 5.8, 8.5, 11.4, 16.0 };
    const float phase[5] = { 0.4, 2.1, 4.6, 1.3, 5.5 };
    const float w[5]     = { 0.32, 0.26, 0.20, 0.13, 0.09 };

    float spiral = 0.0;
    [unroll]
    for (int j = 0; j < 5; ++j)
    {
        float arg = m[j] * phi + k[j] * ln_r - m[j] * omega_orb * (timeSec * 0.65) + phase[j];
        spiral += w[j] * cos(arg);
    }

    float spiralContrast = saturate(0.52 + 0.48 * spiral);
    float filaments = 0.12 + 1.55 * pow(spiralContrast, 2.15) * (0.18 + 0.82 * ringBands);
    return filaments;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float2 grid = input.grid; // NDC (-1..1, -1..1)
    float3 omega;
    if (params.x > 0.5)
    {
        // Perspective divide skyPos.xyz / skyPos.w
        float4 skyPos = grid.x * invViewProj0 + grid.y * invViewProj1 + invViewProj2 + invViewProj3;
        float3 skyDir = (abs(skyPos.w) > 1e-5) ? (skyPos.xyz / skyPos.w) : skyPos.xyz;
        omega = normalize(skyDir);
    }
    else
    {
        float p00 = camRight.w;
        float p11 = camUp.w;
        float p02 = camFwd.w;
        float p12 = projParams.x;

        float vx = (grid.x - p02) / ((abs(p00) > 1e-4) ? p00 : 1.0);
        float vy = (grid.y - p12) / ((abs(p11) > 1e-4) ? p11 : 1.0);
        float3 viewRay = normalize(float3(vx, vy, 1.0));

        omega = normalize(viewRay.x * camRight.xyz + viewRay.y * camUp.xyz + viewRay.z * camFwd.xyz);
    }

    float3 d_bh = normalize(bhWorldDir.xyz);
    float theta_g = max(bhWorldDir.w, 1e-7);

    float cosAlpha = dot(omega, d_bh);
    if (cosAlpha <= 0.0) discard; // Behind observer

    float sinAlpha = length(omega - cosAlpha * d_bh);
    float b_crit = (params.w > 0.0) ? params.w : 5.1961524;
    float r_isco = (diskNormal.w > 0.0) ? diskNormal.w : 5.5;
    float r_out  = (diskTangentU.w > 0.0) ? diskTangentU.w : 20.0;
    float peakTempK = (diskTangentV.w > 0.0) ? diskTangentV.w : 14000.0;
    float a_spin = projParams.w;
    float mode   = projParams.z;
    float timeSec = projParams.y;

    float thetaMax = (r_out + 4.5) * theta_g;
    if (sinAlpha > thetaMax) discard; // Outside accretion disk bounds

    // Impact parameter b in units of M = GM/c^2, and unit tangent direction e2 in the ray's orbital plane:
    float3 orthoVec = omega - cosAlpha * d_bh;
    float orthoLen = length(orthoVec);
    float3 e2 = (orthoLen > 1e-6) ? (orthoVec / orthoLen) : diskTangentU.xyz;
    float b = (sinAlpha / max(cosAlpha, 0.15)) / theta_g;

    // True 3D World-Space Spin Axis & Equatorial Basis (fixed in 3D space as you fly around the black hole!):
    float3 n_disk = normalize(diskNormal.xyz);
    float3 u_disk = normalize(diskTangentU.xyz);
    float3 v_disk = normalize(diskTangentV.xyz);

    // Fixed-chirality screen-plane projection (diskRight, diskUp) derived directly from n_disk (NOT n_eff!):
    // Because n_disk never flips sign when the observer crosses the equatorial plane, diskRight and diskUp
    // are 100% continuous everywhere in 3D space!
    float3 n_perp = n_disk - dot(n_disk, d_bh) * d_bh;
    float n_perp_len = length(n_perp);
    float3 diskUp = (n_perp_len > 1e-5) ? (n_perp / n_perp_len) : u_disk;
    float3 diskRight = cross(diskUp, d_bh);

    // Signed impact parameters along the disk's major axis (bx) and spin-axis projection (by):
    float bx = b * dot(e2, diskRight);
    float by = b * dot(e2, diskUp);

    // Observer's live signed 3D viewing inclination cos(i) = dot(-d_bh, n_disk):
    //   cosIncSigned > 0: observer is above the northern hemisphere of the disk
    //   cosIncSigned = 0: observer is crossing the equatorial plane edge-on
    //   cosIncSigned < 0: observer is below the southern hemisphere of the disk
    float cosIncSigned = clamp(dot(-d_bh, n_disk), -0.96, 0.96);
    float sinIncMag = sqrt(max(0.0, 1.0 - cosIncSigned * cosIncSigned));

    // Smooth, C^infinity grazing-angle regularizer for |cos(i)|:
    // Approaches |cosIncSigned| for |cosIncSigned| >> 0.12, and smoothly rounds to 0.12 at exact edge-on (cosIncSigned = 0)
    // WITHOUT any sign flip, so crossing the equatorial plane from top to bottom has ZERO pop!
    float cosIncReg = sqrt(cosIncSigned * cosIncSigned + 0.12 * 0.12);

    float bxShadow = bx;
    float bShadow  = length(float2(bxShadow, by));

    // Screen-angle decomposition relative to the disk's projected spin axis:
    // sinPhi = +1.0 directly along diskUp (top apex), -1.0 directly along -diskUp (bottom apex)
    // cosPhi = +/-1.0 directly along diskRight (horizontal wings)
    float sinPhi = by / max(bShadow, 1e-4);
    float cosPhi = bx / max(bShadow, 1e-4);
    float sin2Phi = sinPhi * sinPhi;
    float cos2Phi = cosPhi * cosPhi;

    // True physical 3D hit direction for procedural noise & turbulence on the equatorial disk plane:
    float3 hitDir1 = normalize(-sinPhi * diskUp + cosPhi * diskRight);
    float3 hitInPlane1 = hitDir1 - dot(hitDir1, n_disk) * n_disk;
    float phi1 = atan2(dot(hitInPlane1, v_disk), dot(hitInPlane1, u_disk));

    // =========================================================================
    // 1. FOREGROUND DISK PLANE (Crossing in Front of the Shadow)
    // =========================================================================
    // In front of the black hole, the line of sight intersects the tilted equatorial plane (Z = 0):
    // r_front = sqrt(bx^2 + (by / cosIncReg)^2).
    // Because cosIncReg is strictly positive and non-zero everywhere, r_front is C^infinity continuous!
    // As edge-on is approached (|cosIncSigned| -> 0), points off the midline naturally have r_front > r_out,
    // so the disk smoothly and gracefully flattens into a razor-thin line across the center with ZERO pop!
    float y_plane = by / cosIncReg;
    float r_front = sqrt(bx * bx + y_plane * y_plane);

    // Determines whether a ray looks toward the foreground side of the disk or background lensed arch:
    // When observer is above disk (cosIncSigned > 0), foreground disk is at by < 0 (bottom).
    // When observer is below disk (cosIncSigned < 0), foreground disk is at by > 0 (top).
    // Thus foreground side is unconditionally: -by * cosIncSigned > 0:
    float mu_side = (-by * cosIncSigned) / max(bShadow * cosIncReg, 0.06);
    float frontBridgeWeight = smoothstep(-0.16, 0.16, mu_side);

    // =========================================================================
    // 2. RELATIVISTIC LENSED ARCHES (Primary Majestic Arch + Secondary Crescent)
    // =========================================================================
    // Which hemisphere of the sky contains the primary lensed arch vs secondary crescent?
    // When cosIncSigned > 0 (above disk): primary arch is at the top (by > 0), secondary at bottom (by < 0).
    // When cosIncSigned < 0 (below disk): primary arch is at the bottom (by < 0), secondary at top (by > 0).
    // At cosIncSigned = 0 (edge-on): both arches are completely symmetric!
    float topIsPrimary = 0.5 + 0.5 * clamp(cosIncSigned / 0.22, -1.0, 1.0);
    float isTop = smoothstep(-0.06, 0.06, by);
    float primaryArchWeight = lerp(1.0 - topIsPrimary, topIsPrimary, isTop);

    // Relativistic lensed thickness of the primary arch:
    // At apex (sin2Phi = 1): arch thickness is deltaB_apex (majestic ~2.4M, hugging the shadow tightly).
    // At wings (cos2Phi = 1): arch smoothly fans out to (r_out - b_crit) to merge seamlessly with the outer disk.
    float deltaB_apex = (mode > 1.5) ? 2.15 : 2.45;
    float deltaB_primary = deltaB_apex * sin2Phi + (r_out - b_crit) * cos2Phi;
    float deltaB = max(bShadow - b_crit, 0.0);
    float r_primary = r_isco + (deltaB / max(deltaB_primary, 0.15)) * (r_out - r_isco);

    // Secondary lensed crescent (n = 1):
    // In general relativity, n = 1 image is compressed by exp(-pi) ~ 1/23.
    // It is a thin, delicate crescent hugging the opposite rim of the shadow:
    float deltaB_secondary = 0.42 * sin2Phi + (r_out - b_crit) * 0.25 * cos2Phi;
    float r_secondary = r_isco + (deltaB / max(deltaB_secondary, 0.05)) * (r_out - r_isco);

    // Smoothly blend primary arch on the dominant side and secondary crescent on the opposite side:
    // At edge-on (cosIncSigned = 0), both sides have equal symmetric intermediate thickness.
    float r_lensed = lerp(r_secondary, r_primary, primaryArchWeight);

    // Effective radial emission coordinate:
    // Inside the shadow (bShadow < b_crit), light from behind the black hole is captured by the event horizon.
    // Therefore, any emission inside the shadow can ONLY originate from the foreground disk plane (r_front).
    // Outside the shadow (bShadow >= b_crit), smoothly blend foreground bridge into background lensed arch:
    float r_direct;
    if (bShadow < b_crit)
    {
        r_direct = r_front;
    }
    else
    {
        r_direct = lerp(r_lensed, r_front, frontBridgeWeight);
    }

    // Visibility rule:
    // - On the rear side of the disk (wrapping behind the black hole), rays with bShadow < b_crit
    //   pass inside the photon sphere and are occluded by the black hole shadow (outsideShadowWeight = 0).
    // - On the front side of the disk (in front of the black hole), rays cross the equatorial plane
    //   in front of the black hole. Where r_front >= r_isco, the glowing disk is visible in front of the shadow.
    //   Where r_front < r_isco, DiskEmissionProfile(r_front) returns 0, preserving 100% pure black shadow!
    float outsideShadowWeight = smoothstep(b_crit * 0.96, b_crit * 1.03, bShadow);
    float vis1 = lerp(outsideShadowWeight, 1.0, frontBridgeWeight);

    float emission1 = DiskEmissionProfile(r_direct, r_isco, r_out);
    float3 hdr1 = float3(0.0, 0.0, 0.0);
    float opacity1 = 0.0;

    if (emission1 > 0.0005 && vis1 > 0.001)
    {
        float beta1 = sqrt(1.0 / max(r_direct - a_spin, 1.4));
        // Relativistic Doppler beaming from orbital velocity tangent to the disk:
        float v_los1 = -beta1 * sinIncMag * (bx / max(b, 0.35));
        float gamma1 = 1.0 / sqrt(max(1.0 - beta1 * beta1, 0.02));
        float gravRedshift1 = sqrt(max(1.0 - 1.5 / max(r_direct, 1.8), 0.05));
        float g1 = clamp(gravRedshift1 / (gamma1 * max(1.0 - v_los1, 0.12)), 0.25, 1.85);

        float turb1 = DiskStructureAndTurbulence(r_direct, phi1, timeSec, r_isco, a_spin);
        float boost1 = pow(g1, 3.0);
        float T_norm1 = pow(max(r_isco / max(r_direct, 0.1), 0.0), 0.55) * g1;
        float3 col1 = AccretionTemperatureToRGB(T_norm1, mode, peakTempK);

        float I1 = emission1 * boost1 * turb1 * 1.45;
        hdr1 = col1 * I1 * vis1;
        opacity1 = saturate(emission1 * turb1 * vis1 * 1.45);
    }

    // =========================================================================
    // 3. REALISTIC MODE KERR n=2 PHOTON SUB-RING FILAMENT
    // =========================================================================
    // In realistic mode (a* = 0.85), higher-order geodesics form an ultra-thin,
    // intense relativistic filament right outside the photon sphere (deltaB < 0.30M):
    float3 hdrKerr = float3(0.0, 0.0, 0.0);
    float opacityKerr = 0.0;
    if (mode > 1.5 && bShadow > b_crit && bShadow < b_crit + 0.30)
    {
        float deltaB_kerr = bShadow - b_crit;
        float r_kerr = r_isco + (deltaB_kerr / 0.30) * (r_out - r_isco);
        float emKerr = DiskEmissionProfile(r_kerr, r_isco, r_out) * 0.65;
        if (emKerr > 0.001)
        {
            float betaK = sqrt(1.0 / max(r_kerr - a_spin, 1.4));
            float v_losK = -betaK * sinIncMag * (bx / max(b, 0.35));
            float gammaK = 1.0 / sqrt(max(1.0 - betaK * betaK, 0.02));
            float gravK = sqrt(max(1.0 - 1.5 / max(r_kerr, 1.8), 0.05));
            float gK = clamp(gravK / (gammaK * max(1.0 - v_losK, 0.12)), 0.25, 1.85);

            float turbK = DiskStructureAndTurbulence(r_kerr, phi1, timeSec, r_isco, a_spin);
            float T_normK = pow(max(r_isco / max(r_kerr, 0.1), 0.0), 0.55) * gK;
            float3 colK = AccretionTemperatureToRGB(T_normK, mode, peakTempK);

            hdrKerr = colK * emKerr * pow(gK, 3.0) * turbK * 1.25;
            opacityKerr = saturate(emKerr * turbK);
        }
    }

    // =========================================================================
    // Thin Relativistic Photon Ring at bShadow = b_crit (occluded where front disk crosses!)
    // =========================================================================
    float deltaCrit = abs(bShadow - b_crit);
    float photonSpike = exp(-deltaCrit * deltaCrit / 0.008) * 2.25;
    // Doppler asymmetry along the photon ring: brighter on approaching (bx < 0) side
    float ringDoppler = clamp(1.0 - 0.40 * sinIncMag * (bx / max(b, 0.5)), 0.50, 1.55);
    float3 photonRingCol = (mode > 1.5) ? float3(1.85, 2.45, 3.85) : float3(2.65, 1.95, 0.95);
    // Front disk bridge attenuates the background photon ring behind it:
    float behindFrontDisk = 1.0 - saturate(opacity1 * frontBridgeWeight * 0.90);
    float3 hdrPhoton = photonRingCol * photonSpike * ringDoppler * behindFrontDisk;

    // Composite Front-over-Back: primary disk + Kerr photon sub-ring filament:
    float3 diskHdr = hdr1 + hdrKerr * (1.0 - opacity1 * 0.90);
    float diskOpacity = saturate(opacity1 + opacityKerr * (1.0 - opacity1));

    float3 totalHdr = diskHdr + hdrPhoton;

    // Event Horizon Shadow Mask inside bShadow < b_crit:
    // Black hole shadow extinguishes background stars (outAlpha = 1.0), while allowing
    // the front equatorial disk bridge (hdr1 where cosPsi1 > 0) to shine across the front of the shadow!
    float shadowMask = (bShadow < b_crit) ? 1.0 : 0.0;
    float outAlpha = (shadowMask > 0.5) ? 1.0 : saturate(diskOpacity + photonSpike * 0.35);

    // Discard if completely dark and outside the shadow to preserve depth and performance
    if (dot(totalHdr, float3(1, 1, 1)) < 0.0002 && shadowMask < 0.5)
    {
        discard;
    }

    return float4(totalHdr, outAlpha);
}
