#ifndef __PATH_TRACER_HLSLI__
#define __PATH_TRACER_HLSLI__

#include "PathTracerBridge.hlsli"
#include "PathTracerHelpers.hlsli"
#include "Lighting/EnvMap.hlsli"
#include "Lighting/PolymorphicLight.hlsli"
#include "Utils/SampleGenerators.hlsli"
#include "Rendering/Materials/BxDF.hlsli"

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
        TraceRay(g_TLAS,
                 RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
                 0xFF,
                 0,
                 1,
                 0,
                 ray,
                 payload);
        return payload.hitFlag == 0u;
    }

    float3 SampleAnalyticNEE(StandardBSDFData bsdfData, float3 hitPos, float3 visibilityOrigin,
                             float3 wo, inout SampleGenerator sg)
    {
        const uint lightCount = Bridge::getLightCount();
        if (lightCount == 0u || g_FrameConstants.ptConsts.lightIntensityScale <= 0.0)
            return float3(0.0, 0.0, 0.0);

        const uint lightIndex = min(uint(sampleNext1D(sg) * float(lightCount)), lightCount - 1u);

        LightSample light = EvalAnalyticLight(Bridge::getLight(lightIndex), hitPos);
        if (!light.valid)
            return float3(0.0, 0.0, 0.0);

        const float specProb = getSpecularProbability(bsdfData, wo);
        float3      f;
        float       bsdfPdf;
        EvalBSDF(bsdfData, wo, light.dir, specProb, f, bsdfPdf);
        if (bsdfPdf <= 0.0)
            return float3(0.0, 0.0, 0.0);

        if (!TraceVisibilityRay(visibilityOrigin, light.dir, light.distance))
            return float3(0.0, 0.0, 0.0);

        return f * light.radiance * g_FrameConstants.ptConsts.lightIntensityScale * float(lightCount);
    }

    float3 SampleEnvironmentNEE(StandardBSDFData bsdfData, float3 visibilityOrigin,
                                float3 wo, inout SampleGenerator sg)
    {
        if (g_FrameConstants.ptConsts.environmentIntensity <= 0.0)
            return float3(0.0, 0.0, 0.0);

        float envPdf;
        const float3 wi = sampleCosineHemisphere(sampleNext2D(sg), bsdfData.N, envPdf);
        if (envPdf <= 0.0)
            return float3(0.0, 0.0, 0.0);

        const float specProb = getSpecularProbability(bsdfData, wo);
        float3      f;
        float       bsdfPdf;
        EvalBSDF(bsdfData, wo, wi, specProb, f, bsdfPdf);
        if (bsdfPdf <= 0.0)
            return float3(0.0, 0.0, 0.0);

        if (!TraceVisibilityRay(visibilityOrigin, wi, kVisibilityRayTMax))
            return float3(0.0, 0.0, 0.0);

        const float3 envRadiance = EnvMap::Eval(wi) * g_FrameConstants.ptConsts.environmentIntensity;
        const float  misWeight   = PowerHeuristic(1.0, envPdf, 1.0, bsdfPdf);
        return f * envRadiance * (misWeight / envPdf);
    }

    float ComputeBSDFEnvMISWeight(bool didEnvNEE, float prevBsdfPdf, float3 prevNormal, float3 rayDir)
    {
        if (!didEnvNEE || prevBsdfPdf <= 0.0)
            return 1.0;

        const float envPdf = max(dot(prevNormal, rayDir), 0.0) * K_1_PI;
        return PowerHeuristic(1.0, prevBsdfPdf, 1.0, envPdf);
    }
} // namespace PathTracer

#endif // __PATH_TRACER_HLSLI__
