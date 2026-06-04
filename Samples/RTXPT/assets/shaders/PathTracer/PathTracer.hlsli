#ifndef __PATH_TRACER_HLSLI__
#define __PATH_TRACER_HLSLI__

#include "Config.h"
#include "PathTracerBridge.hlsli"
#include "PathTracerHelpers.hlsli"
#include "Lighting/EnvMap.hlsli"
#include "Lighting/LightSampler.hlsli"
#include "Rendering/Materials/BxDF.hlsli"
#include "Utils/StatelessSampleGenerators.hlsli"
#include "Rendering/Volumes/HomogeneousVolumeSampler.hlsli"
#include "PathTracerNestedDielectrics.hlsli"

#if PATH_TRACER_MODE != PATH_TRACER_MODE_REFERENCE
#    include "PathState.hlsli"
#    include "StablePlanes.hlsli"
#    include "PathTracerStablePlanes.hlsli"
#endif

static const float kVisibilityRayTMin = 0.0;
static const float kVisibilityRayTMax = 1e30;

namespace PathTracer
{
    static const float kSpecularRoughnessThreshold = 0.25;

#if PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE
    RTXPTMaterialHitPayload MakeEmptyPayload(uint hitFlag)
    {
        RTXPTMaterialHitPayload payload;
        payload.worldPos    = float3(0.0, 0.0, 0.0);
        payload.hitDistance = -1.0;
        payload.worldNormal = float3(0.0, 1.0, 0.0);
        payload.hitFlag     = hitFlag;
        payload.faceNormal  = float3(0.0, 1.0, 0.0);
        payload.materialID  = 0u;
        payload.baseColor   = float3(0.0, 0.0, 0.0);
        payload.emission    = float3(0.0, 0.0, 0.0);
        payload.metallic    = 0.0;
        payload.roughness   = 1.0;
        payload.emissiveLightPdf = 0.0;
        payload.ior = 1.5;
        payload.transmissionFactor = 0.0;
        payload.diffuseTransmissionFactor = 0.0;
        payload.transmissionColor = float3(1.0, 1.0, 1.0);
        payload.volumeAttenuationDistance = 3.402823466e+38;
        payload.volumeAttenuationColor = float3(1.0, 1.0, 1.0);
        payload.materialFlags = 0u;
        payload.nestedPriority = 14u;
        payload.frontFacing = 1u;
        payload.thinSurface = 1u;
        payload.alpha = 1.0;
        payload.vertexNormal = payload.worldNormal;
        payload.shadowNoLFadeout = 0.0;
        return payload;
    }
#else
    inline PathPayload MakeVisibilityPayload(uint2 pixelPos)
    {
        PathState visibilityPath = EmptyPathInitialize(pixelPos, 0.0);
        visibilityPath.setActive();
        visibilityPath.clearHit();
        visibilityPath.SetL(float4(0.0, 0.0, 0.0, 0.0));
        visibilityPath.SetThp(float3(0.0, 0.0, 0.0));
        return PathPayload::pack(visibilityPath);
    }
#endif

    bool TraceVisibilityRay(float3 origin, float3 dir, float tMax)
    {
        if (tMax <= kVisibilityRayTMin)
            return false;

        RayDesc ray;
        ray.Origin    = origin;
        ray.Direction = dir;
        ray.TMin      = kVisibilityRayTMin;
        ray.TMax      = tMax;

#if PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE
        RTXPTMaterialHitPayload payload = MakeEmptyPayload(1u);
        TraceRay(t_SceneBVH,
                 RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
                 0xFF,
                 RTXPT_VISIBILITY_RAY_INDEX,
                 RTXPT_HIT_GROUP_STRIDE,
                 RTXPT_VISIBILITY_RAY_INDEX,
                 ray,
                 payload);
        return payload.hitFlag == 0u;
#else
        PathPayload payload = MakeVisibilityPayload(Bridge::getPixelPosition());
        TraceRay(t_SceneBVH,
                 RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
                 0xFF,
                 RTXPT_VISIBILITY_RAY_INDEX,
                 RTXPT_HIT_GROUP_STRIDE,
                 RTXPT_VISIBILITY_RAY_INDEX,
                 ray,
                 payload);
        return !PathPayload::unpack(payload).isActive();
#endif
    }

#if PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE
    float3 MakeVisibilityOrigin(float3 hitPos, float3 faceNormal, float3 shadingNormal, float3 dir)
    {
        const float side = dot(shadingNormal, dir) >= 0.0 ? 1.0 : -1.0;
        return ComputeRayOrigin(hitPos, faceNormal * side);
    }

    float ComputeShadowNoLFadeout(float3 lightDir, float3 vertexNormal, float shadowNoLFadeout)
    {
        return shadowNoLFadeout > 0.0 ?
            ComputeLowGrazingAngleFalloff(lightDir, vertexNormal, shadowNoLFadeout, 2.0 * shadowNoLFadeout) :
            1.0;
    }

    float ComputeLightVsBSDFMISForLightSample(DirectLightSample sample, uint fullSamples)
    {
        if (!sample.sampleableByBSDF || sample.bsdfPdf <= 0.0 || sample.proposalPdf <= 0.0)
            return 1.0;

        const float lightPdf = sample.proposalPdf * float(max(fullSamples, 1u));
        return PowerHeuristic(1.0, lightPdf, 1.0, sample.bsdfPdf);
    }

    float3 SampleEnvironmentNEE(StandardBSDFData bsdfData, float3 hitPos, float3 faceNormal, float3 vertexNormal,
                                float shadowNoLFadeout, float3 wo, inout SampleGenerator sg, float fireflyFilterK)
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

        const float3 visibilityOrigin = MakeVisibilityOrigin(hitPos, faceNormal, bsdfData.N, envSample.Dir);
        if (!TraceVisibilityRay(visibilityOrigin, envSample.Dir, kVisibilityRayTMax))
            return float3(0.0, 0.0, 0.0);

        const float misWeight = PowerHeuristic(1.0, envSample.Pdf, 1.0, bsdfPdf);
        const float fadeOut   = ComputeShadowNoLFadeout(envSample.Dir, vertexNormal, shadowNoLFadeout);
        float3      contribution = f * envSample.Le * (fadeOut * misWeight / envSample.Pdf);

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

    float3 SampleDirectLightNEE(StandardBSDFData bsdfData, float3 hitPos, float3 faceNormal, float3 vertexNormal,
                                float shadowNoLFadeout, float3 wo, uint2 pixelPos, inout SampleGenerator sg,
                                float fireflyFilterK, out bool sampledEmissive)
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
            const float3 visibilityOrigin = MakeVisibilityOrigin(hitPos, faceNormal, bsdfData.N, picked.dir);
            if (!TraceVisibilityRay(visibilityOrigin, picked.dir, visibilityDistance))
                continue;

            const float wrsScale   = 1.0 / (candidateProbability * float(candidateSamples));
            const float misWeight  = ComputeLightVsBSDFMISForLightSample(picked, fullSamples);
            const float fadeOut    = ComputeShadowNoLFadeout(picked.dir, vertexNormal, shadowNoLFadeout);
            float3      contribution = picked.bsdfF * picked.radianceOverPdf * (fadeOut * wrsScale * misWeight / float(fullSamples));

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
#else
    inline bool HasFinishedSurfaceBounces(uint vertexIndex, uint diffuseBounces)
    {
        if (g_Const.ptConsts.bounceCount < vertexIndex)
            return true;
        return diffuseBounces > g_Const.ptConsts.diffuseBounceCount;
    }

    inline PathState EmptyPathInitialize(uint2 pixelPos, float pixelConeSpreadAngle)
    {
        PathState path;
        path.SetId(PathIDFromPixel(pixelPos));
        path.flagsAndVertexIndex = 0;
        path.SetSceneLength(0.0);
        path.packedCounters = 0;

#if RTXPT_NESTED_DIELECTRICS_QUALITY > 0 || defined(__INTELLISENSE__)
        for (uint i = 0; i < INTERIOR_LIST_SLOT_COUNT; ++i)
            path.interiorList.slots[i] = 0;
#endif

        path.SetOrigin(float3(0.0, 0.0, 0.0));
        path.SetDir(float3(0.0, 0.0, 0.0));
        path.SetThp(float3(1.0, 1.0, 1.0));
        path.setActive();
        path.setDeltaOnlyPath(true);
        path.rayCone = RayCone::make(0.0, pixelConeSpreadAngle);

#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
        path.SetImageXform(float3x3(1.0, 0.0, 0.0,
                                    0.0, 1.0, 0.0,
                                    0.0, 0.0, 1.0));
        path.setFlag(PathFlags::stablePlaneOnDominantBranch, true);
        path.SetMotionVectorSceneLength(0.0);
#else
        path.SetL(float4(0.0, 0.0, 0.0, 0.0));
        path.SetFireflyFilterK_BsdfScatterPdf(1.0, 0.0);
        path.SetPackedMISInfo_ThpRuRuCorrection(NEEBSDFMISInfo::empty().Pack16bit(), 1.0);
#endif

        path.setStablePlaneIndex(0);
        path.stableBranchID = 1u;

        if (HasFinishedSurfaceBounces(path.getVertexIndex() + 1,
                                      path.getCounter(PackedCounters::DiffuseBounces)))
            path.setTerminateAtNextBounce();

        return path;
    }

    inline void StartPixel(const PathState path, const WorkingContext workingContext)
    {
        workingContext.StablePlanes.StartPixel(path.GetPixelPos());

#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
        Bridge::ExportSurfaceInit(path.GetPixelPos());
#endif
    }

    inline void SetupPathPrimaryRay(inout PathState path, const Ray ray)
    {
        path.SetOrigin(ray.origin);
        path.SetDir(ray.dir);
    }

    inline void UpdatePathThroughput(inout PathState path, const float3 weight)
    {
        path.SetThp(path.GetThp() * weight);
    }

    inline bool ShouldCollectGISecondaryRadiance(const PathState path)
    {
        return false;
    }

    inline void AccumulatePathRadiance(const WorkingContext workingContext,
                                       inout PathState path,
                                       float3 radiance,
                                       const float specularRadianceAvg,
                                       bool stablePlaneOnBranch,
                                       bool collectGISecondaryRadiance)
    {
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
        workingContext.StablePlanes.AccumulateStableRadiance(path.GetPixelPos(), radiance);
#elif PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
        if (!stablePlaneOnBranch)
        {
            float4 newL = float4(radiance, specularRadianceAvg) * Bridge::getNoisyRadianceAttenuation();
            path.SetL(path.GetL() + newL);
        }
#else
#    error Unsupported PATH_TRACER_MODE.
#endif
    }

    inline void CommitPixel(const PathState path, const WorkingContext workingContext)
    {
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
        // Stable radiance and stable-plane data are committed incrementally while tracing.
#elif PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
        workingContext.StablePlanes.CommitDenoiserRadiance(path);
#else
#    error Unsupported PATH_TRACER_MODE.
#endif
    }

    inline bool HandleRussianRoulette(inout PathState path,
                                      inout UniformSampleSequenceGenerator sampleGenerator,
                                      const WorkingContext workingContext)
    {
        return false;
    }

    inline void UpdatePathTravelled(inout PathState path,
                                    const float3 rayOrigin,
                                    const float3 rayDir,
                                    const float rayTCurrent,
                                    const WorkingContext workingContext)
    {
        path.incrementVertexIndex();
        UpdatePathTravelledLengthOnly(path, rayTCurrent);
    }

    inline void HandleMiss(inout PathState path,
                           const float3 rayOrigin,
                           const float3 rayDir,
                           const float rayTCurrent,
                           const WorkingContext workingContext)
    {
        UpdatePathTravelled(path, rayOrigin, rayDir, rayTCurrent, workingContext);

        EnvMapSampler envSampler = RTXPTCreateEnvMapSampler(Bridge::getEnvMapConstants());
        float3 environmentEmission = envSampler.Eval(rayDir, 0.0);

        StablePlanesHandleMiss(path, environmentEmission, rayOrigin, rayDir, rayTCurrent, workingContext);

        if (any(environmentEmission > 0.0))
        {
            const float3 radiance = path.GetThp() * environmentEmission;
            const float specRadianceAvg =
                path.hasFlag(PathFlags::stablePlaneBaseScatterDiff) ? 0.0 : Average(radiance);
            AccumulatePathRadiance(workingContext,
                                   path,
                                   radiance,
                                   specRadianceAvg,
                                   path.hasFlag(PathFlags::stablePlaneOnBranch),
                                   ShouldCollectGISecondaryRadiance(path));
        }

        path.clearHit();
        path.terminate();
    }

    inline void HandleHit(inout PathState path,
                          const SurfaceData surfaceData,
                          const float3 surfaceEmission,
                          const float3 rayOrigin,
                          const float3 rayDir,
                          const float rayTCurrent,
                          const WorkingContext workingContext)
    {
        UpdatePathTravelled(path, rayOrigin, rayDir, rayTCurrent, workingContext);

        if (any(surfaceEmission > 0.0))
        {
            const float3 radiance = path.GetThp() * surfaceEmission;
            const float specRadianceAvg =
                path.hasFlag(PathFlags::stablePlaneBaseScatterDiff) ? 0.0 : Average(radiance);
            AccumulatePathRadiance(workingContext,
                                   path,
                                   radiance,
                                   specRadianceAvg,
                                   path.hasFlag(PathFlags::stablePlaneOnBranch),
                                   ShouldCollectGISecondaryRadiance(path));
        }

        bool pathStopping = path.isTerminatingAtNextBounce();
        StablePlanesHandleHit(path,
                              rayOrigin,
                              rayDir,
                              rayTCurrent,
                              workingContext,
                              surfaceData,
                              0.0,
                              surfaceEmission,
                              pathStopping);

        if (pathStopping)
        {
            path.terminate();
            return;
        }

#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
        if (path.hasFlag(PathFlags::stablePlaneOnDominantBranch))
        {
            // Placeholder until real scatter/delta-lobe plumbing records dominant-layer specular travel distance.
            Bridge::ExportSpecHitTStart(path);
            Bridge::ExportSpecHitTStop(path);
        }
#endif

        // Conservative Task 4 stopgap: material-plumbing must replace this termination with
        // real scatter/delta-lobe continuation when ActiveBSDF sampling is wired.
        path.terminate();
    }
#endif
} // namespace PathTracer

#endif // __PATH_TRACER_HLSLI__
