#ifndef __LIGHT_SAMPLER_HLSLI__
#define __LIGHT_SAMPLER_HLSLI__

#include "../PathTracerBridge.hlsli"
#include "../Rendering/Materials/BxDF.hlsli"
#include "../Utils/SampleGenerators.hlsli"
#include "PolymorphicLight.hlsli"

struct DirectLightSample
{
    float3 dir;
    float  distance;
    float3 radianceOverPdf;
    float  proposalPdf;
    float3 bsdfF;
    float  bsdfPdf;
    uint   kind;
    uint   index;
    bool   valid;
    bool   sampleableByBSDF;
};

DirectLightSample DirectLightSample_make_empty()
{
    DirectLightSample sample;
    sample.dir               = float3(0.0, 1.0, 0.0);
    sample.distance          = 0.0;
    sample.radianceOverPdf   = float3(0.0, 0.0, 0.0);
    sample.proposalPdf       = 0.0;
    sample.bsdfF             = float3(0.0, 0.0, 0.0);
    sample.bsdfPdf           = 0.0;
    sample.kind              = kLightProxyKindAnalytic;
    sample.index             = 0u;
    sample.valid             = false;
    sample.sampleableByBSDF  = false;
    return sample;
}

struct NEEWeightedReservoirSampler
{
    DirectLightSample candidate;
    float             weightSum;
    float             candidateWeight;

    static NEEWeightedReservoirSampler make()
    {
        NEEWeightedReservoirSampler sampler;
        sampler.candidate       = DirectLightSample_make_empty();
        sampler.weightSum       = 0.0;
        sampler.candidateWeight = 0.0;
        return sampler;
    }

    void Add(float randomValue, DirectLightSample sample, float targetWeight)
    {
        if (targetWeight <= 0.0)
            return;

        weightSum += targetWeight;
        if (randomValue * weightSum <= targetWeight)
        {
            candidate = sample;
            candidateWeight = targetWeight;
        }
    }

    float CandidateProbability()
    {
        return weightSum > 0.0 ? candidateWeight / weightSum : 0.0;
    }
};

uint SamplePowerProxyIndex(float randomValue)
{
    const uint proxyCount = Bridge::getLightProxyCount();
    if (proxyCount == 0u)
        return 0u;

    const float totalWeight = Bridge::getLightProxyTotalWeight();
    if (totalWeight <= 0.0)
        return 0u;

    const float targetWeight = randomValue * totalWeight;
    uint        lo           = 0u;
    uint        hi           = proxyCount;
    [loop]
    while (lo + 1u < hi)
    {
        const uint mid = (lo + hi) >> 1u;
        if (targetWeight <= Bridge::getLightProxy(mid).prefixWeight)
            hi = mid;
        else
            lo = mid;
    }

    return targetWeight <= Bridge::getLightProxy(lo).prefixWeight ? lo : min(lo + 1u, proxyCount - 1u);
}

float GetProxySelectionPdf(uint proxyIndex, bool usePowerSampling)
{
    const uint proxyCount = Bridge::getLightProxyCount();
    if (proxyCount == 0u || proxyIndex >= proxyCount)
        return 0.0;

    if (!usePowerSampling)
        return 1.0 / float(proxyCount);

    const float totalWeight = Bridge::getLightProxyTotalWeight();
    if (totalWeight <= 0.0)
        return 0.0;

    return Bridge::getLightProxy(proxyIndex).weight / totalWeight;
}

uint SampleProxyIndex(float randomValue, bool usePowerSampling)
{
    const uint proxyCount = Bridge::getLightProxyCount();
    if (proxyCount == 0u)
        return 0u;

    if (usePowerSampling)
        return SamplePowerProxyIndex(randomValue);

    return min(uint(randomValue * float(proxyCount)), proxyCount - 1u);
}

float GetEmissiveTriangleSelectionPdf(bool usePowerSampling)
{
    const uint proxyCount = Bridge::getLightProxyCount();
    const uint triCount = Bridge::getEmissiveTriangleCount();
    if (proxyCount == 0u || triCount == 0u)
        return 0.0;

    const uint emissiveProxyIndex = proxyCount - 1u;
    if (Bridge::getLightProxy(emissiveProxyIndex).kind != kLightProxyKindEmissiveBucket)
        return 0.0;

    return GetProxySelectionPdf(emissiveProxyIndex, usePowerSampling) / float(triCount);
}

DirectLightSample GenerateDirectLightCandidate(StandardBSDFData bsdfData, float3 hitPos, float3 wo,
                                               inout SampleGenerator sg, bool usePowerSampling)
{
    DirectLightSample sample = DirectLightSample_make_empty();

    const uint proxyCount = Bridge::getLightProxyCount();
    if (proxyCount == 0u)
        return sample;

    const uint proxyIndex = SampleProxyIndex(sampleNext1D(sg), usePowerSampling);
    const RTXPTLightProxy proxy = Bridge::getLightProxy(proxyIndex);
    const float selectionPdf    = GetProxySelectionPdf(proxyIndex, usePowerSampling);
    if (selectionPdf <= 0.0)
        return sample;

    if (proxy.kind == kLightProxyKindAnalytic)
    {
        const LightSample light = EvalAnalyticLight(Bridge::getLight(proxy.index), hitPos);
        if (!light.valid)
            return sample;

        const float3 radiance = light.radiance * g_Const.ptConsts.lightIntensityScale;
        const float  bsdfProb = getSpecularProbability(bsdfData, wo);
        float3       f;
        float        bsdfPdf;
        EvalBSDF(bsdfData, wo, light.dir, bsdfProb, f, bsdfPdf);

        sample.dir              = light.dir;
        sample.distance         = light.distance;
        sample.radianceOverPdf  = radiance / selectionPdf;
        sample.proposalPdf      = selectionPdf;
        sample.bsdfF            = f;
        sample.bsdfPdf          = bsdfPdf;
        sample.kind             = proxy.kind;
        sample.index            = proxy.index;
        sample.valid            = true;
        sample.sampleableByBSDF = false;
    }
    else if (proxy.kind == kLightProxyKindEmissiveBucket)
    {
        const uint triCount = Bridge::getEmissiveTriangleCount();
        if (triCount == 0u)
            return sample;

        const uint triIndex = min(uint(sampleNext1D(sg) * float(triCount)), triCount - 1u);
        const EmissiveTriangle tri = Bridge::getEmissiveTriangle(triIndex);

        const float3 ng    = cross(tri.edge1.xyz, tri.edge2.xyz);
        const float  ngLen = length(ng);
        if (ngLen <= 0.0)
            return sample;

        const float  area   = 0.5 * ngLen;
        const float3 normal = ng / ngLen;
        const float3 bary   = SampleTriangleUniform(sampleNext2D(sg));
        const float3 P      = tri.base.xyz + tri.edge1.xyz * bary.y + tri.edge2.xyz * bary.z;

        const float3 toLight = P - hitPos;
        const float  distSq  = max(1e-9, dot(toLight, toLight));
        const float  dist    = sqrt(distSq);
        const float3 wi      = toLight / dist;
        const float  cosTheta = abs(dot(normal, -wi));
        if (cosTheta <= 2e-9)
            return sample;

        const float solidAnglePdf = min(kMaxSolidAnglePdf, pdfAtoW(1.0 / area, dist, cosTheta));
        const float trianglePdf   = selectionPdf * (1.0 / float(triCount)) * solidAnglePdf;
        if (trianglePdf <= 0.0)
            return sample;

        const float bsdfProb = getSpecularProbability(bsdfData, wo);
        float3      f;
        float       bsdfPdf;
        EvalBSDF(bsdfData, wo, wi, bsdfProb, f, bsdfPdf);

        sample.dir              = wi;
        sample.distance         = dist;
        sample.radianceOverPdf  = tri.radiance.rgb / trianglePdf;
        sample.proposalPdf      = trianglePdf;
        sample.bsdfF            = f;
        sample.bsdfPdf          = bsdfPdf;
        sample.kind             = proxy.kind;
        sample.index            = triIndex;
        sample.valid            = true;
        sample.sampleableByBSDF = true;
    }

    if (!sample.valid || sample.bsdfPdf <= 0.0)
        return DirectLightSample_make_empty();

    return sample;
}

float EvalDirectLightCandidateWeight(DirectLightSample sample)
{
    if (!sample.valid || sample.proposalPdf <= 0.0 || sample.bsdfPdf <= 0.0)
        return 0.0;

    return max(sample.radianceOverPdf.x, max(sample.radianceOverPdf.y, sample.radianceOverPdf.z)) *
        sample.bsdfPdf;
}

#endif // __LIGHT_SAMPLER_HLSLI__
