// Diligent-owned RTXPT environment importance-map baker skeleton.
// Builds equal-area octahedral importance/radiance maps for later MIP-descent sampling.

#ifndef __ENVMAP_IMPORTANCE_BAKER_HLSL__
#define __ENVMAP_IMPORTANCE_BAKER_HLSL__

#include "../Config.h"

#define RTXPT_ENVMAP_IMPORTANCE_THREADS 16

static const float kEnvMapImportancePi = 3.14159265358979323846;

struct EnvMapImportanceBakerConstants
{
    uint  SourceCubeDim;
    uint  SourceCubeMipCount;
    uint  ImportanceMapDim;
    uint  ImportanceMapBaseMip;
    uint2 ImportanceMapDimInSamples;
    uint2 ImportanceMapNumSamples;
    float ImportanceMapInvSamples;
    uint  ReduceSrcMip;
    uint  ReduceDstMip;
    uint  _padding0;
};

ConstantBuffer<EnvMapImportanceBakerConstants> g_EnvMapImportanceBakerConsts;

TextureCube<float4> t_EnvMapCube;
Texture2D<float>    t_SourceImportanceMip;
Texture2D<float4>   t_SourceRadianceMip;
VK_IMAGE_FORMAT("r32f") RWTexture2D<float> u_ImportanceMap;
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_RadianceMap;
SamplerState        s_LinearWrap;

float RTXPTEnvMapLuminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float3 OctToDirEqualArea(float2 uv)
{
    const float2 p   = uv * 2.0 - 1.0;
    const float  d   = 1.0 - (abs(p.x) + abs(p.y));
    const float  r   = 1.0 - abs(d);
    const float  phi = r > 0.0 ? ((abs(p.y) - abs(p.x)) / r + 1.0) * (0.25 * kEnvMapImportancePi) : 0.0;
    const float  f   = r * sqrt(max(0.0, 2.0 - r * r));
    const float  x   = f * sign(p.x) * cos(phi);
    const float  y   = f * sign(p.y) * sin(phi);
    const float  z   = sign(d) * (1.0 - r * r);
    return float3(x, y, z);
}

// ------------------------------------------------------------------------------------------------
// High-resolution environment-cube bake (graphics pass).
//
// Renders the loaded source (a cube map, or a 2D equirectangular sky) into a high-resolution cube
// map that the path tracer samples directly as the distant environment. This mirrors upstream RTXPT
// (Rtxpt/Lighting/Distant/EnvMapBaker), which bakes a high-res environment cube instead of relying on
// the low-resolution GGX-prefiltered IBL cube; the latter washes out fine detail seen through small
// apertures such as windows. The vertex stage (EnvCubeBakeVS) replicates DiligentFX's CubemapFace.vsh
// and is driven with the same per-face rotations as PBR_Renderer::PrecomputeCubemaps, so the baked
// cube keeps the same orientation as the prefiltered cube it replaces.
// ENV_BAKE_SOURCE_CUBE selects between a cube source and a 2D equirectangular source.
cbuffer cbEnvCubeBake
{
    // Per-face rotation that maps the full-screen quad position to the world direction for that cube
    // face. Matches the rotations used by DiligentFX CubemapFace.vsh / PBR_Renderer::PrecomputeCubemaps,
    // so the baked cube keeps the same orientation as the previously used prefiltered cube.
    float4x4 g_EnvCubeRotation;
}

void EnvCubeBakeVS(in uint vertexId : SV_VertexID, out float4 position : SV_Position, out float3 worldPos : WORLD_POS)
{
    float2 quadXY[4];
    quadXY[0] = float2(-1.0, -1.0);
    quadXY[1] = float2(-1.0, +1.0);
    quadXY[2] = float2(+1.0, -1.0);
    quadXY[3] = float2(+1.0, +1.0);
    position             = float4(quadXY[vertexId], 1.0, 1.0);
    const float4 worldH  = mul(g_EnvCubeRotation, position);
    worldPos             = worldH.xyz / worldH.w;
}

#if defined(ENV_BAKE_SOURCE_CUBE)
TextureCube<float4> g_EnvBakeSource;
#else
Texture2D<float4>   g_EnvBakeSource;
#endif
SamplerState g_EnvBakeSourceSampler;

// Matches DiligentFX ShaderUtilities.fxh TransformDirectionToSphereMapUV so 2D sources keep their
// existing orientation.
float2 RTXPTDirToSphereMapUV(float3 dir)
{
    const float OneOverPi = 0.3183098862;
    return OneOverPi * float2(0.5 * atan2(dir.z, dir.x), asin(clamp(dir.y, -1.0, 1.0))) + float2(0.5, 0.5);
}

float4 SampleEnvToCubePS(float4 position : SV_Position, float3 worldPos : WORLD_POS) : SV_Target
{
    const float3 dir = normalize(worldPos);
#if defined(ENV_BAKE_SOURCE_CUBE)
    return g_EnvBakeSource.SampleLevel(g_EnvBakeSourceSampler, dir, 0.0);
#else
    return g_EnvBakeSource.SampleLevel(g_EnvBakeSourceSampler, RTXPTDirToSphereMapUV(dir), 0.0);
#endif
}

[numthreads(RTXPT_ENVMAP_IMPORTANCE_THREADS, RTXPT_ENVMAP_IMPORTANCE_THREADS, 1)]
void BuildImportanceBaseCS(uint3 tid : SV_DispatchThreadID)
{
    const uint2 dim = g_EnvMapImportanceBakerConsts.ImportanceMapDim.xx;
    if (any(tid.xy >= dim))
        return;

    float  importance = 0.0;
    float3 radiance   = float3(0.0, 0.0, 0.0);

    [loop]
    for (uint y = 0; y < g_EnvMapImportanceBakerConsts.ImportanceMapNumSamples.y; ++y)
    {
        [loop]
        for (uint x = 0; x < g_EnvMapImportanceBakerConsts.ImportanceMapNumSamples.x; ++x)
        {
            const uint2  samplePos      = tid.xy * g_EnvMapImportanceBakerConsts.ImportanceMapNumSamples + uint2(x, y);
            const float2 uv             = (float2(samplePos) + 0.5) / float2(g_EnvMapImportanceBakerConsts.ImportanceMapDimInSamples);
            const float3 dir            = OctToDirEqualArea(uv);
            const float3 sampleRadiance = t_EnvMapCube.SampleLevel(s_LinearWrap, dir, 0.0).rgb;
            importance += 0.5 * (RTXPTEnvMapLuminance(sampleRadiance) + (sampleRadiance.x + sampleRadiance.y + sampleRadiance.z) / 3.0);
            radiance += sampleRadiance;
        }
    }

    importance *= g_EnvMapImportanceBakerConsts.ImportanceMapInvSamples;
    radiance *= g_EnvMapImportanceBakerConsts.ImportanceMapInvSamples;
    u_ImportanceMap[tid.xy] = max(importance, 0.0);
    u_RadianceMap[tid.xy]   = float4(radiance, max(importance, 0.0));
}

[numthreads(RTXPT_ENVMAP_IMPORTANCE_THREADS, RTXPT_ENVMAP_IMPORTANCE_THREADS, 1)]
void ReduceImportanceMipCS(uint3 tid : SV_DispatchThreadID)
{
    uint width;
    uint height;
    u_ImportanceMap.GetDimensions(width, height);
    const uint2 dstDim = uint2(width, height);
    if (any(tid.xy >= dstDim))
        return;

    const uint2 srcBase = tid.xy * 2u;
    float       totalImportance = 0.0;
    float3      totalRadiance   = float3(0.0, 0.0, 0.0);

    [unroll]
    for (uint y = 0; y < 2u; ++y)
    {
        [unroll]
        for (uint x = 0; x < 2u; ++x)
        {
            const uint2 srcPos = srcBase + uint2(x, y);
            totalImportance += t_SourceImportanceMip.Load(int3(srcPos, 0));
            totalRadiance += t_SourceRadianceMip.Load(int3(srcPos, 0)).rgb;
        }
    }

    const float3 avg      = totalRadiance * 0.25;
    const float  mipValue = totalImportance * 0.25;
    u_RadianceMap[tid.xy] = float4(avg, mipValue);
    u_ImportanceMap[tid.xy] = mipValue;
}

#endif // __ENVMAP_IMPORTANCE_BAKER_HLSL__
