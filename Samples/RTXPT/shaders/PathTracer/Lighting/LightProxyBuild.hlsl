// Per-triangle, power-proportional light sampling proxy build (GPU).
//
// Replaces the CPU "emissive bucket" proxy build with RTXPT-fork's per-light power-proportional model:
// every analytic light AND every emissive triangle becomes an independent light with a weight derived from
// its power, and the sampling-proxy table is populated so each light is selected with probability
// proportional to its weight (see Lighting/LightSampler.hlsli SampleGlobalLightIndex).
//
// Unified light index space (matches LightSampler.hlsli / the closest-hit MIS):
//   [0, AnalyticLightCount)                                  -> analytic lights        (t_Lights)
//   [AnalyticLightCount, AnalyticLightCount+TriangleCount)   -> emissive triangles     (t_EmissiveTriangles)
//
// Pipeline (one Dispatch each, UAV barriers between):
//   ResetProxyBuildCS      - clear the GPU-computed control scalars
//   ComputeWeightsCS       - per-light weight + atomic weight-sum (RTXPT-fork ComputeWeights, simplified)
//   ComputeProxyCountsCS   - per-light proxy count = round(budget * weight / weightSum)  (ComputeProxyCounts)
//   ScatterProxiesCS       - fill the proxy table via an atomic running offset            (ExecuteProxyJobs)
//
// vs. RTXPT-fork: upstream balances the proxy scatter with a prefix-sum + task system (CreateProxyJobs /
// ExecuteProxyJobs). This port uses a single atomic running offset and a per-light fill loop bounded by
// RTXPT_LIGHTING_MAX_SAMPLING_PROXIES_PER_LIGHT, which yields an identical proxy distribution.

#ifndef __LIGHT_PROXY_BUILD_HLSL__
#define __LIGHT_PROXY_BUILD_HLSL__

#include "PathTracerShared.h"
#include "Lighting/LightingTypes.hlsli"

struct LightProxyBuildConstants
{
    uint TotalLightCount;
    uint AnalyticLightCount;
    uint ProxyBudget;          // capacity of u_LightSamplingProxies (kProxyRatio * TotalLightCount)
    uint ImportanceSamplingType; // 0 = uniform (1 proxy/light), otherwise power-proportional
};

ConstantBuffer<LightProxyBuildConstants> g_LightProxyBuildConstants;

RWStructuredBuffer<LightingControlData> u_LightingControl;     // GPU writes WeightsSumUINT/SamplingProxyCount/ProxyBuildTaskCount
StructuredBuffer<PolymorphicLightInfo>  t_Lights;              // analytic lights
StructuredBuffer<EmissiveTriangle>      t_EmissiveTriangles;   // emissive triangles (world-space, baked radiance)
RWStructuredBuffer<float>               u_LightWeights;        // per-light weight [TotalLightCount]
RWStructuredBuffer<uint>                u_LightProxyCounters;  // per-light proxy count [TotalLightCount]
RWStructuredBuffer<uint>                u_LightSamplingProxies;// flat proxy -> light index table [ProxyBudget]

static const float kLightProxyPi = 3.14159265358979323846;

float ProxyLuminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Mirrors RTXPTLightsBaker.cpp EstimateAnalyticWeight (keeps analytic-light weighting unchanged from the
// previous CPU proxy build).
float ComputeAnalyticWeight(uint lightIndex)
{
    const PolymorphicLightInfo light = t_Lights[lightIndex];
    const float luma             = ProxyLuminance(light.colorType.xyz);
    const float radius           = max(light.positionRadius.w, 0.0);
    const float areaOrSolidAngle = (light.colorType.w == 2.0) ? max(light.shaping.w, 1e-8) : max(kLightProxyPi * radius * radius, 1.0);
    return max(1e-6, luma * areaOrSolidAngle);
}

// Mirrors RTXPT-fork TriangleLight::GetPower (PolymorphicLight.hlsli) + ComputeWeight: power = area*PI*Luminance,
// shaped by pow(.,0.8). radiance already includes the baked emissive texture (EmissiveTriangleBuild.hlsl).
float ComputeEmissiveTriangleWeight(uint triIndex)
{
    const EmissiveTriangle tri = t_EmissiveTriangles[triIndex];
    const float3 ng    = cross(tri.edge1.xyz, tri.edge2.xyz);
    const float  area  = 0.5 * length(ng);
    const float  power = area * kLightProxyPi * ProxyLuminance(tri.radiance.rgb);
    float        weight = pow(max(0.0, power), 0.8);
    if (weight < RTXPT_LIGHTING_MIN_WEIGHT_THRESHOLD)
        weight = 0.0;
    return weight;
}

float ComputeLightWeight(uint lightIndex)
{
    if (lightIndex < g_LightProxyBuildConstants.AnalyticLightCount)
        return ComputeAnalyticWeight(lightIndex);
    return ComputeEmissiveTriangleWeight(lightIndex - g_LightProxyBuildConstants.AnalyticLightCount);
}

[numthreads(1, 1, 1)]
void ResetProxyBuildCS(uint dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId != 0u)
        return;
    u_LightingControl[0].WeightsSumUINT      = 0u;
    u_LightingControl[0].SamplingProxyCount  = 0u;
    u_LightingControl[0].ProxyBuildTaskCount = 0u; // reused as the proxy scatter cursor
}

// Single-group weight computation + reduction (avoids float atomics for cross-platform DXC/SPIR-V support):
// each of the 256 threads processes a contiguous block of lights, then a groupshared reduction writes the
// total weight sum directly. Dispatched with exactly one thread group.
#define LPB_REDUCE_THREADS 256u
groupshared float g_BlockWeightSums[LPB_REDUCE_THREADS];

[numthreads(LPB_REDUCE_THREADS, 1, 1)]
void ComputeWeightsCS(uint groupThreadId : SV_GroupThreadID)
{
    const uint total     = g_LightProxyBuildConstants.TotalLightCount;
    const uint perThread = (total + LPB_REDUCE_THREADS - 1u) / LPB_REDUCE_THREADS;
    const uint from      = groupThreadId * perThread;
    const uint to        = min(from + perThread, total);

    float sum = 0.0;
    [loop]
    for (uint lightIndex = from; lightIndex < to; ++lightIndex)
    {
        const float weight       = ComputeLightWeight(lightIndex);
        u_LightWeights[lightIndex] = weight;
        sum += weight;
    }

    g_BlockWeightSums[groupThreadId] = sum;
    GroupMemoryBarrierWithGroupSync();

    if (groupThreadId == 0u)
    {
        float totalWeight = 0.0;
        [loop]
        for (uint i = 0u; i < LPB_REDUCE_THREADS; ++i)
            totalWeight += g_BlockWeightSums[i];
        u_LightingControl[0].WeightsSumUINT = asuint(totalWeight);
    }
}

[numthreads(256, 1, 1)]
void ComputeProxyCountsCS(uint dispatchThreadId : SV_DispatchThreadID)
{
    const uint lightIndex = dispatchThreadId;
    if (lightIndex >= g_LightProxyBuildConstants.TotalLightCount)
        return;

    const uint  totalLights = g_LightProxyBuildConstants.TotalLightCount;
    const uint  budget      = g_LightProxyBuildConstants.ProxyBudget;
    // Reserve one proxy per light so the per-light counts (each rounded up) still sum to <= budget.
    const uint  proxyPool   = (budget > totalLights) ? (budget - totalLights) : 0u;
    const float weightSum   = asfloat(u_LightingControl[0].WeightsSumUINT);
    const float weight      = u_LightWeights[lightIndex];

    uint proxyCount = 0u;
    if (weight > 0.0 && weightSum > 0.0)
    {
        if (g_LightProxyBuildConstants.ImportanceSamplingType == 0u)
            proxyCount = 1u; // uniform mode (reference/testing only)
        else
            proxyCount = (uint)ceil((float(proxyPool) * weight) / weightSum);
    }
    proxyCount = min(proxyCount, RTXPT_LIGHTING_MAX_SAMPLING_PROXIES_PER_LIGHT - 1u);

    u_LightProxyCounters[lightIndex] = proxyCount;
    if (proxyCount > 0u)
        InterlockedAdd(u_LightingControl[0].SamplingProxyCount, proxyCount);
}

[numthreads(256, 1, 1)]
void ScatterProxiesCS(uint dispatchThreadId : SV_DispatchThreadID)
{
    const uint lightIndex = dispatchThreadId;
    if (lightIndex >= g_LightProxyBuildConstants.TotalLightCount)
        return;

    const uint proxyCount = u_LightProxyCounters[lightIndex];
    if (proxyCount == 0u)
        return;

    // Claim a contiguous range in the proxy table (ProxyBuildTaskCount is reused as the scatter cursor).
    uint base;
    InterlockedAdd(u_LightingControl[0].ProxyBuildTaskCount, proxyCount, base);

    const uint budget = g_LightProxyBuildConstants.ProxyBudget;
    [loop]
    for (uint k = 0u; k < proxyCount; ++k)
    {
        const uint slot = base + k;
        if (slot < budget)
            u_LightSamplingProxies[slot] = lightIndex;
    }
}

#endif // __LIGHT_PROXY_BUILD_HLSL__
