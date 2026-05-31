#ifndef __LIGHTING_TYPES_HLSLI__
#define __LIGHTING_TYPES_HLSLI__

#include "LightingConfig.h"

#if defined(__cplusplus)
#    define ROW_MAJOR
#else
#    define ROW_MAJOR row_major
#endif

struct LightsBakerEnvMapParams
{
    ROW_MAJOR float3x4 Transform;
    ROW_MAJOR float3x4 InvTransform;
    float3             ColorMultiplier;
    float              Enabled;
};

#define NEEAT_LIGHTS_BAKER_CONSTANTS_SIZE 464

struct LightsBakerConstants
{
    float  DistantVsLocalRelativeImportance;
    uint   EnvMapImportanceMapMIPCount;
    uint   EnvMapImportanceMapResolution;
    uint   TriangleLightTaskCount;

    uint2  FeedbackResolution;
    uint2  BlendedFeedbackResolution;
    uint2  MouseCursorPos;
    float2 PrevOverCurrentViewportSize;

    int    DebugDrawType;
    uint   DebugDrawTileLights;
    uint   UpdateCounter;
    uint   DebugDrawFrustum;

    float  ImportanceBoostIntensityDelta;
    float  ImportanceBoostFrustumMul;
    float  ImportanceBoostFrustumFadeDistance;
    float  _padding3;

    float3 SceneCameraPos;
    float  SceneAverageContentsDistance;

    float  DepthDisocclusionThreshold;
    uint   EnableMotionReprojection;
    float  ReservoirHistoryDropoff;
    uint   _padding0;

    uint   CurrentWeightsBufferOffset;
    uint   HistoricWeightsBufferOffset;
    uint   _padding1;
    uint   _padding2;

    float4 FrustumPlanes[6];
    float4 FrustumCorners[8];

    LightsBakerEnvMapParams EnvMapParams;
};

struct LightingControlData
{
    uint TotalLightCount;
    uint EnvmapQuadNodeCount;
    uint AnalyticLightCount;
    uint TriangleLightCount;

    uint SamplingProxyCount;
    uint HistoricTotalLightCount;
    uint LastFrameTemporalFeedbackAvailable;
    uint LastFrameLocalSamplesAvailable;

    uint ProxyBuildTaskCount;
    uint WeightsSumUINT;
    uint ImportanceSamplingType;
    uint _padding0;

    uint  TemporalFeedbackRequired;
    uint  TotalMaxFeedbackCount;
    float GlobalFeedbackUseWeight;
    float LocalToGlobalSampleRatio;

    uint  TileBufferHeight;
    float ScreenSpaceVsWorldSpaceThreshold;
    uint2 LocalSamplingResolution;

    uint2 LocalSamplingTileJitter;
    uint2 LocalSamplingTileJitterPrev;

    uint ValidFeedbackCount;
    uint _padding1;
    uint _padding2;
    uint _padding3;

    LightsBakerConstants BakerConstants;

#if !defined(__cplusplus)
    float WeightsSum()
    {
        return asfloat(WeightsSumUINT);
    }
#endif
};

uint ComputeCandidateSampleLocalCount(float localToGlobalRatio, uint totalCandidateSamples)
{
    return (uint)((float)(totalCandidateSamples - 1u) * localToGlobalRatio + 0.75);
}

uint ComputeCandidateSampleGlobalCount(float localToGlobalRatio, uint totalCandidateSamples)
{
    return totalCandidateSamples - ComputeCandidateSampleLocalCount(localToGlobalRatio, totalCandidateSamples);
}

#if !defined(__cplusplus)
uint PackMiniListLightAndCount(uint globalLightIndex, uint counter)
{
    return ((globalLightIndex & 0x007FFFFFu) << 9u) | ((counter - 1u) & 0x1FFu);
}

void UnpackMiniListLightAndCount(uint value, out uint globalLightIndex, out uint counter)
{
    globalLightIndex = value >> 9u;
    counter          = (value & 0x1FFu) + 1u;
}

uint UnpackMiniListLight(uint value)
{
    return value >> 9u;
}

uint UnpackMiniListCount(uint value)
{
    return (value & 0x1FFu) + 1u;
}

uint LLSB_ComputeBaseAddress(uint2 tilePos, uint2 localSamplingResolution)
{
    return (tilePos.x + tilePos.y * localSamplingResolution.x) * RTXPT_LIGHTING_LOCAL_PROXY_COUNT;
}
#endif

#endif // __LIGHTING_TYPES_HLSLI__
