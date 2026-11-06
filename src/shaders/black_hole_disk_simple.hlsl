// HLSL Vertex and Pixel Shader for Analytic Black Hole Accretion Disk
// visual.black_hole_disk = simple
//
// Models:
// 1. Central Event Horizon Shadow (~ 5.2 rg Schwarzschild shadow radius)
// 2. Sharp intense photon ring (~ 3.0 rg)
// 3. Equatorial thin accretion disk with inner edge at ISCO (r_in = 6.0 rg) and smooth outer falloff
// 4. Relativistic gravitational lensing:
//    - Direct disk view
//    - Gravitationally lensed upper arch (rays curving over top of event horizon)
//    - Gravitationally lensed lower arch (rays curving beneath event horizon)
// 5. Relativistic Doppler beaming / boosting:
//    - Keplerian orbital motion of emitting gas in accretion disk
//    - Approaching side boosted (blue-shifted / intense), receding side dimmed (red-shifted)
// 6. Cockpit canopy occlusion via read-only reversed-Z scene depth buffer (t0):
//    - Cockpit glass/struts have depth > far_depth, occluding the black hole at astronomical distance.

// Constant Buffer CB2 (slot b2) matching Frontier's lens draw CB2:
cbuffer BlackHoleCB : register(b2)
{
    float4 cb2[9];
    // cb2[0]: Lens optic parameters / base tint
    // cb2[1]: Lensing wavelength dispersion / falloff exponent
    // cb2[2]: Output color scaling and saturation terms
    // cb2[3]: Deflection scale (x), noise phase (y), falloff power (z), unused (w)
    // cb2[4]: xyz = Black Hole unit direction in camera/view space; w = distance L in light-seconds
    // cb2[5]: xyz = Body-aligned Right basis vector (orthonormal, length 1.0)
    // cb2[6]: xyz = Body-aligned Up basis vector (orthonormal, length 1.0)
    // cb2[7]: xyz = Body-aligned Forward basis vector (unit direction, length 1.0)
    // cb2[8]: Occlusion mask / visibility flags
};

// Accretion Disk Custom Parameters (slot b3) - optional extra parameters, or default fallback
cbuffer AccretionDiskParams : register(b3)
{
    float4 diskParams0; // x: r_shadow (e.g. 5.2), y: r_photon (3.0), z: r_isco (6.0), w: r_out (25.0)
    float4 diskParams1; // x: angular_scale, y: doppler_strength (e.g. 3.0), z: brightness_scale (1.0), w: temperature
    float4 diskParams2; // xyz: disk normal override (if 0, uses cb2[6]), w: reserved
    float4 diskParams3; // reserved
};

// Depth texture (t0) and Point Clamp Sampler (s0)
Texture2D<float> SceneDepth : register(t0);
SamplerState PointClampSampler : register(s0);

// Vertex Shader Output / Pixel Shader Input
struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float3 rayDir : TEXCOORD1; // View-space unnormalized ray direction
};

// Full-screen triangle vertex shader
VSOutput VSMain(uint vid : SV_VertexID)
{
    VSOutput o;
    // Standard full-screen triangle: vid 0 -> (-1, -1), vid 1 -> (-1, 3), vid 2 -> (3, -1)
    float2 grid = float2((vid == 2) ? 3.0 : -1.0, (vid == 1) ? 3.0 : -1.0);
    o.pos = float4(grid, 0.0, 1.0);
    o.uv = grid * float2(0.5, -0.5) + 0.5;

    // View-space ray reconstruction from UV:
    // cb2[5] = Right, cb2[6] = Up, cb2[7] = Forward
    // For a camera-relative pass, or standard perspective:
    // grid.x maps horizontally, grid.y vertically.
    o.rayDir = float3(grid.x, grid.y, 1.0);
    return o;
}

// Simple blackbody / temperature color mapping
float3 TemperatureToRGB(float T)
{
    // T normalized [0, 1] mapped from warm orange (1500K) to fiery golden-white (6000K) to blue-white (12000K)
    float3 colRed   = float3(1.2, 0.25, 0.03);
    float3 colGold  = float3(1.5, 0.85, 0.25);
    float3 colWhite = float3(1.2, 1.3, 1.5);
    float3 colBlue  = float3(0.5, 0.8, 2.0);

    float3 col = lerp(colRed, colGold, saturate(T * 2.0));
    col = lerp(col, colWhite, saturate((T - 0.5) * 2.0));
    col = lerp(col, colBlue, saturate((T - 0.85) * 6.67));
    return col;
}

// Analytic emission profile of the equatorial disk at radius r (in units of gravitational radius rg = GM/c^2)
float DiskProfile(float r, float r_in, float r_out)
{
    if (r < r_in || r > r_out)
        return 0.0;
    // Standard Novikov-Thorne / Shakura-Sunyaev profile: peaks near ISCO then drops as r^-3
    float x = r_in / max(r, 0.001);
    float f = sqrt(max(0.0, 1.0 - sqrt(x))) * pow(max(0.0, x), 1.5);
    // Smooth boundary cutoffs
    float edgeIn = smoothstep(r_in, r_in * 1.08, r);
    float edgeOut = smoothstep(r_out, r_out * 0.7, r);
    return f * edgeIn * edgeOut;
}

// Pixel Shader
float4 PSMain(VSOutput input) : SV_TARGET
{
    int2 pixelCoord = int2(input.pos.xy);

    // 1. Scene Depth Occlusion:
    // Elite Dangerous uses reversed-Z depth: 1.0 is near, 0.0 is infinite far.
    // Cockpit canopy frames and interior struts have significant reversed-Z depth (> 1e-4).
    // The black hole is at astronomical distance (far plane, depth ~ 0.0).
    float sceneDepth = SceneDepth.Load(int3(pixelCoord, 0));
    // If foreground cockpit geometry is present, discard to prevent drawing over canopy struts
    if (sceneDepth > 0.0001)
    {
        discard;
    }

    // 2. Black Hole Direction and Basis Frame from CB2:
    float3 bhDir = cb2[4].xyz; // Unit vector towards black hole center in view space
    float bhDist = cb2[4].w;   // Distance in light-seconds

    // Right, Up, Forward orthonormal basis from CB2
    float3 bhRight = cb2[5].xyz;
    float3 bhUp    = cb2[6].xyz;
    float3 bhFwd   = cb2[7].xyz;

    // View-space ray for this pixel:
    float3 ray = normalize(input.rayDir);

    // Impact parameter and angular separation:
    float cosTheta = dot(ray, bhDir);
    if (cosTheta < 0.0)
    {
        // Behind camera
        discard;
    }

    // Offset in angular space from black hole center:
    // x = displacement along bhRight, y = displacement along bhUp
    float2 angleCoord = float2(dot(ray, bhRight), dot(ray, bhUp));
    float angleRadius = length(angleCoord);

    // Angular size scale of 1 rg (gravitational radius):
    // Angular radius of the shadow theta_s = r_shadow_rg * (rg / D).
    // Default or CB-driven angular scale:
    float rgAngularScale = (diskParams1.x > 0.0) ? diskParams1.x : 0.012; // ~12 mrad default if not provided
    
    // Impact parameter in units of rg:
    float b = angleRadius / max(rgAngularScale, 1e-6);

    // Radii definitions (in units of rg):
    float r_shadow = (diskParams0.x > 0.0) ? diskParams0.x : 5.2;  // Schwarzschild shadow radius ~ sqrt(27) = 5.196 rg
    float r_photon = (diskParams0.y > 0.0) ? diskParams0.y : 3.0;  // Photon sphere ~ 3.0 rg
    float r_isco   = (diskParams0.z > 0.0) ? diskParams0.z : 6.0;  // Innermost stable circular orbit (ISCO)
    float r_out    = (diskParams0.w > 0.0) ? diskParams0.w : 28.0; // Outer disk edge

    // 3. Central Event Horizon Shadow:
    // Ray falls into event horizon if impact parameter b < r_shadow
    float shadowMask = smoothstep(r_shadow - 0.15, r_shadow + 0.05, b);

    // 4. Photon Ring:
    // Ultra-sharp, bright ring right at the edge of the shadow (~ 5.2 rg impact parameter)
    float photonRingDelta = abs(b - 5.2);
    float photonRing = exp(-photonRingDelta * photonRingDelta / 0.008) * 8.0;

    // 5. Gravitational Lensing & Accretion Disk Branches:
    // A distant observer sees 3 primary contributions from a thin equatorial disk:
    //   Branch A: Direct equatorial disk (unlensed / weakly deflected rays hitting the disk plane)
    //   Branch B: Lensed Upper Arch (rays passing above the black hole bent downwards through the disk behind)
    //   Branch C: Lensed Lower Arch (rays passing beneath the black hole bent upwards through the disk behind)

    // Local coordinates in disk plane:
    // Disk inclination angle: angle between line of sight (bhDir) and disk normal.
    // If disk normal is in the Up direction (cb2[6]), inclination is near 90 deg (edge-on) or tilted.
    // We model inclination i:
    float cosInc = 0.35; // ~70 degrees inclination for dramatic Gargantua-style arches
    float sinInc = sqrt(max(0.0, 1.0 - cosInc * cosInc));

    float3 totalRadiance = float3(0.0, 0.0, 0.0);

    // --- Branch A: Direct Disk (Front / Middle) ---
    // Project apparent sky-plane position (x, y) back to disk equatorial plane
    // Analytic mapping for direct rays:
    // In the disk plane: x_disk = x / rgAngularScale, y_disk = y / (rgAngularScale * cosInc)
    float direct_x = angleCoord.x / rgAngularScale;
    float direct_y = angleCoord.y / (rgAngularScale * cosInc);
    float r_direct = length(float2(direct_x, direct_y));
    
    // Direct disk is visible in front of the black hole, and outside the shadow
    // For y > 0 (behind), rays are lensed; for y < 0 (in front), rays pass directly.
    float directDiskVis = DiskProfile(r_direct, r_isco, r_out);
    // In front of the hole (angleCoord.y < 0), direct disk is clearly visible
    // Behind the hole, direct disk is partially occluded / replaced by lensed arch
    float frontWeight = (angleCoord.y < 0.0) ? 1.0 : smoothstep(r_shadow * 1.1, r_out, r_direct);

    // Relativistic orbital velocity beta = v/c ~ sqrt(1 / (2 * (r - 1))) for Schwarzschild
    float beta_direct = sqrt(saturate(1.0 / max(r_direct, r_isco)));
    // Line-of-sight velocity component:
    // Rotation is in disk plane: approaching on the left (x < 0), receding on the right (x > 0)
    float v_los_direct = -beta_direct * sinInc * (direct_x / max(r_direct, 0.001));

    // Relativistic Doppler Factor: delta = sqrt(1 - beta^2) / (1 - v_los)
    // Redshift factor g = delta / sqrt(1 - 2/r)
    float gamma_direct = 1.0 / sqrt(max(0.01, 1.0 - beta_direct * beta_direct));
    float g_direct = 1.0 / (gamma_direct * (1.0 - v_los_direct));
    // Gravitational redshift at radius r:
    g_direct *= sqrt(max(0.01, 1.0 - 2.0 / max(r_direct, 2.1)));

    // Relativistic beaming: I_obs = g^4 * I_emit
    float beaming_direct = pow(max(g_direct, 0.01), 3.5);

    float3 colDirect = TemperatureToRGB(saturate(g_direct * 0.45));
    totalRadiance += colDirect * (directDiskVis * frontWeight * beaming_direct * 2.5);

    // --- Branch B: Lensed Upper Arch (Primary Einstein Ring / Arch) ---
    // Rays passing over the top of the black hole (y > 0) are bent downwards to intersect the rear disk plane.
    // The apparent radius in the sky plane b maps to an emission radius r_arch in the rear disk:
    // Analytic deflection relation (Luminet 1979 / Ohanian 1987):
    if (angleCoord.y > 0.0 && b > r_shadow)
    {
        // Map impact parameter b to radius in the back of the disk
        float r_lensed = r_isco + pow(max(0.0, (b - r_shadow) / 1.4), 1.35) * r_isco;
        float archEmission = DiskProfile(r_lensed, r_isco, r_out);

        // Orbital velocity in the rear disk:
        float beta_arch = sqrt(saturate(1.0 / max(r_lensed, r_isco)));
        // On the rear disk, gas moves from left to right as well:
        float v_los_arch = -beta_arch * sinInc * (angleCoord.x / max(angleRadius, 0.001));
        
        float gamma_arch = 1.0 / sqrt(max(0.01, 1.0 - beta_arch * beta_arch));
        float g_arch = 1.0 / (gamma_arch * (1.0 - v_los_arch));
        g_arch *= sqrt(max(0.01, 1.0 - 2.0 / max(r_lensed, 2.1)));

        float beaming_arch = pow(max(g_arch, 0.01), 3.8);
        float3 colArch = TemperatureToRGB(saturate(g_arch * 0.5));
        
        // Arch is most prominent near the top
        float archShape = smoothstep(0.0, 0.5, angleCoord.y / max(angleRadius, 1e-5));
        totalRadiance += colArch * (archEmission * archShape * beaming_arch * 3.5);
    }

    // --- Branch C: Lensed Lower Arch (Secondary Arch underneath the shadow) ---
    // Rays passing below the event horizon (y < 0) bend upwards to intersect the lower rear face of the disk:
    if (angleCoord.y < 0.0 && b > r_shadow && b < r_shadow * 2.2)
    {
        float r_lower = r_isco + pow(max(0.0, (b - r_shadow) / 1.1), 1.4) * r_isco;
        float lowerEmission = DiskProfile(r_lower, r_isco, r_out);

        float beta_lower = sqrt(saturate(1.0 / max(r_lower, r_isco)));
        float v_los_lower = -beta_lower * sinInc * (angleCoord.x / max(angleRadius, 0.001));

        float gamma_lower = 1.0 / sqrt(max(0.01, 1.0 - beta_lower * beta_lower));
        float g_lower = 1.0 / (gamma_lower * (1.0 - v_los_lower));
        g_lower *= sqrt(max(0.01, 1.0 - 2.0 / max(r_lower, 2.1)));

        float beaming_lower = pow(max(g_lower, 0.01), 3.8);
        float3 colLower = TemperatureToRGB(saturate(g_lower * 0.48));

        float lowerShape = smoothstep(0.0, 0.4, -angleCoord.y / max(angleRadius, 1e-5));
        totalRadiance += colLower * (lowerEmission * lowerShape * beaming_lower * 1.8);
    }

    // --- Combine with Photon Ring & Central Shadow ---
    // Add intense photon ring:
    float3 photonRingCol = float3(1.6, 1.4, 1.8) * photonRing;
    totalRadiance += photonRingCol;

    // Apply central event horizon shadow:
    totalRadiance *= shadowMask;

    // Outer subtle glow / hot corona halo
    float coronaGlow = exp(-max(0.0, b - r_shadow) * 0.25) * 0.08;
    totalRadiance += float3(0.8, 0.4, 0.1) * (coronaGlow * shadowMask);

    // Global brightness scale
    float brightness = (diskParams1.z > 0.0) ? diskParams1.z : 1.0;
    totalRadiance *= brightness;

    // If total radiance is completely black and we are outside the shadow, discard to preserve background
    if (dot(totalRadiance, float3(1, 1, 1)) < 0.001 && b >= r_shadow)
    {
        discard;
    }

    // Alpha channel: pre-multiplied alpha or additive contribution
    // For HDR scene blend (e.g. ONE, ONE or ONE, INV_SRC_ALPHA):
    // Pre-multiplied alpha = 1.0 in the opaque shadow so it cuts through background skybox,
    // and smooth alpha at outer edges.
    float alpha = (b < r_shadow) ? 1.0 : saturate(length(totalRadiance) * 0.5);

    return float4(totalRadiance, alpha);
}
