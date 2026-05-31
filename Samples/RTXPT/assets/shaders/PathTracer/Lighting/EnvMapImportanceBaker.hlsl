// Diligent-owned RTXPT environment importance-map baker skeleton.
// Builds equal-area octahedral importance/radiance maps for later MIP-descent sampling.

#ifndef __ENVMAP_IMPORTANCE_BAKER_HLSL__
#define __ENVMAP_IMPORTANCE_BAKER_HLSL__

#include "../Config.h"

#define RTXPT_ENVMAP_IMPORTANCE_THREADS 16

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
RWTexture2D<float>  u_ImportanceMap;
RWTexture2D<float4> u_RadianceMap;
SamplerState        s_LinearWrap;

float RTXPTEnvMapLuminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float3 OctToDirEqualArea(float2 uv)
{
    float2 f = uv * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0)
    {
        const float2 old = n.xy;
        n.x              = (1.0 - abs(old.y)) * (old.x >= 0.0 ? 1.0 : -1.0);
        n.y              = (1.0 - abs(old.x)) * (old.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
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
