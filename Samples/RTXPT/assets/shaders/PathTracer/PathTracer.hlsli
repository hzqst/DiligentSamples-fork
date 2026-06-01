#ifndef __PATH_TRACER_HLSLI__
#define __PATH_TRACER_HLSLI__

#include "PathTracerBridge.hlsli"
#include "PathTracerHelpers.hlsli"
#include "Lighting/EnvMap.hlsli"
#include "Lighting/LightSampler.hlsli"

static const float kVisibilityRayTMin = 1e-4;
static const float kVisibilityRayTMax = 1e30;

namespace PathTracer
{
    PathPayload MakeEmptyPayload(uint hitFlag)
    {
        PathPayload payload;
        payload.worldPos    = float3(0.0, 0.0, 0.0);
        payload.hitDistance = -1.0;
        payload.worldNormal = float3(0.0, 1.0, 0.0);
        payload.hitFlag     = hitFlag;
        payload.baseColor   = float3(0.0, 0.0, 0.0);
        payload.emission    = float3(0.0, 0.0, 0.0);
        payload.metallic    = 0.0;
        payload.roughness   = 1.0;
        payload.emissiveLightPdf = 0.0;
        return payload;
    }

    bool TraceVisibilityRay(float3 origin, float3 dir, float tMax)
    {
        if (tMax <= kVisibilityRayTMin)
            return false;

        RayDesc ray;
        ray.Origin    = origin;
        ray.Direction = dir;
        ray.TMin      = kVisibilityRayTMin;
        ray.TMax      = tMax;

        // Initial HitFlag = blocked. A true miss runs the miss shader, which clears HitFlag to visible.
        PathPayload payload = MakeEmptyPayload(1u);
        TraceRay(t_SceneBVH,
                 RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
                 0xFF,
                 0,
                 1,
                 0,
                 ray,
                 payload);
        return payload.hitFlag == 0u;
    }

    float ComputeLightVsBSDFMISForLightSample(DirectLightSample sample, uint fullSamples)
    {
        if (!sample.sampleableByBSDF || sample.bsdfPdf <= 0.0 || sample.proposalPdf <= 0.0)
            return 1.0;

        const float lightPdf = sample.proposalPdf * float(max(fullSamples, 1u));
        return PowerHeuristic(1.0, lightPdf, 1.0, sample.bsdfPdf);
    }

    float3 SampleEnvironmentNEE(StandardBSDFData bsdfData, float3 visibilityOrigin,
                                float3 wo, inout SampleGenerator sg, float fireflyFilterK)
    {
        EnvMapSampler envSampler = RTXPTCreateEnvMapSampler(Bridge::getEnvMapConstants());
        const DistantLightSample envSample = envSampler.MIPDescentSample(sampleNext2D(sg));
        if (envSample.Pdf <= 0.0)
            return float3(0.0, 0.0, 0.0);

        const float specProb = getSpecularProbability(bsdfData, wo);
        float3      f;
        float       bsdfPdf;
        EvalBSDF(bsdfData, wo, envSample.Dir, specProb, f, bsdfPdf);
        if (bsdfPdf <= 0.0)
            return float3(0.0, 0.0, 0.0);

        if (!TraceVisibilityRay(visibilityOrigin, envSample.Dir, kVisibilityRayTMax))
            return float3(0.0, 0.0, 0.0);

        const float misWeight = PowerHeuristic(1.0, envSample.Pdf, 1.0, bsdfPdf);
        float3      contribution = f * envSample.Le * (misWeight / envSample.Pdf);

        const float ffThreshold = g_Const.ptConsts.fireflyFilterThreshold;
        if (ffThreshold != 0.0)
        {
            const float neeK = ComputeNewScatterFireflyFilterK(fireflyFilterK, envSample.Pdf, 1.0);
            contribution *= FireflyFilterShort(Average(contribution), ffThreshold, neeK);
        }

        return contribution;
    }

    float ComputeBSDFMISForEmissiveTriangle(uint2 pixelPos, float bsdfPdf, float emissiveSolidAnglePdf, uint fullSamples)
    {
        if (bsdfPdf <= 0.0 || emissiveSolidAnglePdf <= 0.0)
            return 1.0;

        const float selectionPdf = GetEmissiveTriangleSelectionPdf(pixelPos);
        if (selectionPdf <= 0.0)
            return 1.0;

        const float lightPdf = selectionPdf * emissiveSolidAnglePdf * float(max(fullSamples, 1u));
        return PowerHeuristic(1.0, bsdfPdf, 1.0, lightPdf);
    }

    float3 SampleDirectLightNEE(StandardBSDFData bsdfData, float3 hitPos, float3 visibilityOrigin,
                                float3 wo, uint2 pixelPos, inout SampleGenerator sg, float fireflyFilterK,
                                out bool sampledEmissive)
    {
        sampledEmissive = false;

        const uint fullSamples = min(32u, g_Const.ptConsts.NEEFullSamples);
        if (fullSamples == 0u || Bridge::getTotalLightCount() == 0u)
            return float3(0.0, 0.0, 0.0);

        const uint candidateSamples = max(1u, min(32u, g_Const.ptConsts.NEECandidateSamples));

        float3 result = float3(0.0, 0.0, 0.0);
        [loop]
        for (uint sampleIndex = 0u; sampleIndex < fullSamples; ++sampleIndex)
        {
            NEEWeightedReservoirSampler wrs = NEEWeightedReservoirSampler::make();

            [loop]
            for (uint candidateIndex = 0u; candidateIndex < candidateSamples; ++candidateIndex)
            {
                DirectLightSample candidate = GenerateDirectLightCandidate(bsdfData, hitPos, wo, pixelPos, sg);
                wrs.Add(sampleNext1D(sg), candidate, EvalDirectLightCandidateWeight(candidate));
            }

            DirectLightSample picked               = wrs.candidate;
            const float       candidateProbability = wrs.CandidateProbability();
            if (!picked.valid || candidateProbability <= 0.0)
                continue;

            const float visibilityDistance =
                picked.kind == kLightProxyKindEmissiveBucket ? picked.distance * 0.9985 : picked.distance;
            if (!TraceVisibilityRay(visibilityOrigin, picked.dir, visibilityDistance))
                continue;

            const float wrsScale   = 1.0 / (candidateProbability * float(candidateSamples));
            const float misWeight  = ComputeLightVsBSDFMISForLightSample(picked, fullSamples);
            float3      contribution = picked.bsdfF * picked.radianceOverPdf * (wrsScale * misWeight / float(fullSamples));

            const float ffThreshold = g_Const.ptConsts.fireflyFilterThreshold;
            if (ffThreshold != 0.0)
            {
                const float neeK = ComputeNewScatterFireflyFilterK(fireflyFilterK, picked.proposalPdf, 1.0);
                contribution *= FireflyFilterShort(Average(contribution), ffThreshold, neeK);
            }

            const uint feedbackLightIndex =
                picked.kind == kLightProxyKindAnalytic ? picked.index : Bridge::getEmissiveBucketLightIndex();
            InsertFeedbackFromNEE(pixelPos, feedbackLightIndex, contribution, sampleNext1D(sg));
            sampledEmissive = sampledEmissive || picked.kind == kLightProxyKindEmissiveBucket;
            result += contribution;
        }

        return result;
    }

    float ComputeBSDFEnvMISWeight(bool didEnvNEE, float prevBsdfPdf, float3 rayDir)
    {
        if (!didEnvNEE || prevBsdfPdf <= 0.0)
            return 1.0;

        EnvMapSampler envSampler = RTXPTCreateEnvMapSampler(Bridge::getEnvMapConstants());
        const float envPdf = envSampler.MIPDescentEvalPdf(rayDir);
        if (envPdf <= 0.0)
            return 1.0;

        return PowerHeuristic(1.0, prevBsdfPdf, 1.0, envPdf);
    }
} // namespace PathTracer

#endif // __PATH_TRACER_HLSLI__
