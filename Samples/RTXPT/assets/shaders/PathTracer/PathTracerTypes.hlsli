#ifndef __PATH_TRACER_TYPES_HLSLI__
#define __PATH_TRACER_TYPES_HLSLI__

#include "Config.h"
#include "PathTracerHelpers.hlsli"
#include "Rendering/Materials/BxDF.hlsli"
#include "PathState.hlsli"
#include "PathPayload.hlsli"
#include "StablePlanes.hlsli"

static const uint cMaxDeltaLobes = 3;

struct DeltaLobe
{
    float3 dir;
    float3 thp;
    uint   transmission;
    float  probability;

    static DeltaLobe make()
    {
        DeltaLobe ret;
        ret.dir          = float3(0.0, 0.0, 1.0);
        ret.thp          = float3(0.0, 0.0, 0.0);
        ret.transmission = 0u;
        ret.probability  = 0.0;
        return ret;
    }
};

struct BSDFSample
{
    uint lobe;
    uint deltaLobeIndex;

    uint getDeltaLobeIndex() { return deltaLobeIndex; }
};

namespace PathTracer
{
    struct StablePlaneMaterialState
    {
        uint flags;
        uint nestedPriority;
        uint psdDominantDeltaLobeP1;

        bool isPSDBlockMotionVectorsAtSurface() { return false; }
        uint getPSDDominantDeltaLobeP1() { return psdDominantDeltaLobeP1; }
        bool isThinSurface() { return (flags & kMaterialFlagThinSurface) != 0u; }
        uint getNestedPriority() { return nestedPriority; }
    };

    struct StablePlaneShadingData
    {
        float3 posW;
        float3 N;
        float3 V;
        float3 T;
        float3 B;
        uint   materialID;
        bool   frontFacing;
        StablePlaneMaterialState mtl;

        float3 computeNewRayOrigin(bool viewSide)
        {
            return ComputeRayOrigin(posW, viewSide ? N : -N);
        }
    };

    struct StablePlaneBSDFData
    {
        float roughness;
        float Roughness() { return roughness; }
    };

    struct ActiveBSDF
    {
        StablePlaneBSDFData data;

        void evalDeltaLobes(StablePlaneShadingData shadingData,
                            out DeltaLobe deltaLobes[cMaxDeltaLobes],
                            out uint deltaLobeCount,
                            out float nonDeltaPart)
        {
            for (uint i = 0; i < cMaxDeltaLobes; ++i)
                deltaLobes[i] = DeltaLobe::make();
            deltaLobeCount = 0u;
            nonDeltaPart   = 1.0;
        }

        void estimateSpecDiffBSDF(out float3 diffBSDFEstimate, out float3 specBSDFEstimate, float3 normal, float3 view)
        {
            diffBSDFEstimate = 0.04.xxx;
            specBSDFEstimate = 0.04.xxx;
        }
    };

    struct WorkingContext
    {
        RWTexture2D<float4> OutputColor;
        PathTracerConstants PtConsts;
        StablePlanesContext StablePlanes;
    };

    struct SurfaceData
    {
        StablePlaneShadingData shadingData;
        ActiveBSDF             bsdf;
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
        float3 prevPosW;
#endif
        lpfloat interiorIoR;
        uint    neeTriangleLightIndex;
        uint    neeAnalyticLightIndex;

        static SurfaceData make(StablePlaneShadingData shadingData,
                                ActiveBSDF bsdf,
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
                                float3 prevPosW,
#endif
                                lpfloat interiorIoR,
                                uint neeTriangleLightIndex,
                                uint neeAnalyticLightIndex)
        {
            SurfaceData ret;
            ret.shadingData           = shadingData;
            ret.bsdf                  = bsdf;
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
            ret.prevPosW              = prevPosW;
#endif
            ret.interiorIoR           = interiorIoR;
            ret.neeTriangleLightIndex = neeTriangleLightIndex;
            ret.neeAnalyticLightIndex = neeAnalyticLightIndex;
            return ret;
        }
    };

    struct NEEBSDFMISInfo
    {
        bool LightSamplingEnabled;
#if PT_USE_RESTIR_DI
        bool SkipEmissiveBRDF;
#endif
        bool LightSamplingIsSSC;
        uint CandidateSamples;
        uint FullSamples;

        static NEEBSDFMISInfo empty()
        {
            NEEBSDFMISInfo ret;
            ret.LightSamplingEnabled = false;
#if PT_USE_RESTIR_DI
            ret.SkipEmissiveBRDF = false;
#endif
            ret.LightSamplingIsSSC = false;
            ret.CandidateSamples   = 0;
            ret.FullSamples        = 0;
            return ret;
        }

        static NEEBSDFMISInfo Unpack16bit(uint packed)
        {
            NEEBSDFMISInfo ret;
            ret.LightSamplingEnabled = (packed & (1 << 15)) != 0;
#if PT_USE_RESTIR_DI
            ret.SkipEmissiveBRDF = (packed & (1 << 14)) != 0;
#endif
            ret.LightSamplingIsSSC = (packed & (1 << 13)) != 0;
            ret.CandidateSamples   = (packed >> 6) & 0x3f;
            ret.FullSamples        = packed & 0x3f;
            return ret;
        }

        uint Pack16bit()
        {
            uint packed = 0;
            packed |= (LightSamplingEnabled ? 1 : 0) << 15;
#if PT_USE_RESTIR_DI
            packed |= (SkipEmissiveBRDF ? 1 : 0) << 14;
#endif
            packed |= (LightSamplingIsSSC ? 1 : 0) << 13;
            packed |= (CandidateSamples & 0x3f) << 6;
            packed |= FullSamples & 0x3f;
            return packed;
        }

        static const uint SampleCountLimit() { return (1 << 6) - 1; }

        static bool equals(const NEEBSDFMISInfo a, const NEEBSDFMISInfo b)
        {
            return (a.LightSamplingEnabled == b.LightSamplingEnabled) &&
#if PT_USE_RESTIR_DI
                (a.SkipEmissiveBRDF == b.SkipEmissiveBRDF) &&
#endif
                (a.LightSamplingIsSSC == b.LightSamplingIsSSC) &&
                (a.CandidateSamples == b.CandidateSamples) &&
                (a.FullSamples == b.FullSamples);
        }

#if PT_USE_RESTIR_DI
        bool GetSkipEmissiveBRDF() { return SkipEmissiveBRDF; }
#else
        bool GetSkipEmissiveBRDF() { return false; }
#endif
    };

#define RTXPT_NEE_RESULT_MANUAL_PACK 1

    struct NEEResult
    {
#if RTXPT_NEE_RESULT_MANUAL_PACK
        uint2 RadianceAndSpecAvgPkg;
#else
        float4 RadianceAndSpecAvg;
#endif
        NEEBSDFMISInfo BSDFMISInfo;

        static NEEResult empty()
        {
            NEEResult ret;
#if RTXPT_NEE_RESULT_MANUAL_PACK
            ret.RadianceAndSpecAvgPkg = Fp32ToFp16(float4(0, 0, 0, 0));
#else
            ret.RadianceAndSpecAvg = float4(0, 0, 0, 0);
#endif
            ret.BSDFMISInfo = NEEBSDFMISInfo::empty();
            return ret;
        }

        void AccumulateRadiance(const float3 radiance, const float specAvg)
        {
#if RTXPT_NEE_RESULT_MANUAL_PACK
            RadianceAndSpecAvgPkg = Fp32ToFp16(Fp16ToFp32(RadianceAndSpecAvgPkg) + float4(radiance, specAvg));
#else
            RadianceAndSpecAvg += float4(radiance, specAvg);
#endif
        }

        float4 GetRadianceAndSpecAvg()
        {
#if RTXPT_NEE_RESULT_MANUAL_PACK
            return Fp16ToFp32(RadianceAndSpecAvgPkg);
#else
            return RadianceAndSpecAvg;
#endif
        }
    };

    struct VisibilityPayload
    {
        uint missed;

        static VisibilityPayload make()
        {
            VisibilityPayload ret;
            ret.missed = 0;
            return ret;
        }
    };
}

#endif // __PATH_TRACER_TYPES_HLSLI__
