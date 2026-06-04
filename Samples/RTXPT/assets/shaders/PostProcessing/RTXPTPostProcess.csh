#pragma pack_matrix(row_major)

#ifndef RTXPT_POST_PROCESS_MODE
#define RTXPT_POST_PROCESS_MODE 0
#endif

#define RTXPT_POST_PROCESS_HDR_TEST                 1
#define RTXPT_POST_PROCESS_EDGE_DETECTION           2
#define RTXPT_POST_PROCESS_STABLE_PLANES_DEBUG_VIZ  3
#define RTXPT_POST_PROCESS_RELAX_PREPARE_INPUTS     4
#define RTXPT_POST_PROCESS_REBLUR_PREPARE_INPUTS    5
#define RTXPT_POST_PROCESS_RELAX_FINAL_MERGE        6
#define RTXPT_POST_PROCESS_REBLUR_FINAL_MERGE       7
#define RTXPT_POST_PROCESS_NO_DENOISER_FINAL_MERGE  8

// TODO(RTXPT-Realtime-DLSS-RR): DLSSRRDenoiserPrepareInputs is deferred to the DLSS-RR phase.

#define NUM_COMPUTE_THREADS_PER_DIM 8

#ifndef USE_RELAX
#    if RTXPT_POST_PROCESS_MODE == RTXPT_POST_PROCESS_RELAX_PREPARE_INPUTS || RTXPT_POST_PROCESS_MODE == RTXPT_POST_PROCESS_RELAX_FINAL_MERGE
#        define USE_RELAX 1
#    else
#        define USE_RELAX 0
#    endif
#endif

#include "../PathTracer/PathTracerShared.h"
#include "../PathTracer/PathTracerHelpers.hlsli"
#include "../PathTracer/StablePlanes.hlsli"
#include "RTXPTDenoiserNRD.hlsli"

struct RTXPTPostProcessConstants
{
    uint  Width;
    uint  Height;
    float EdgeDetectionThreshold;
    float Padding0;
};

cbuffer g_PostProcessConstants
{
    RTXPTPostProcessConstants g_Params;
};

Texture2D<float4> t_LdrColorScratch;

VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_ProcessedOutputColor;
VK_IMAGE_FORMAT("rgba8")   RWTexture2D<float4> u_PostTonemapOutputColor;

cbuffer g_Const
{
    SampleConstants g_Frame;
};

cbuffer g_MiniConst
{
    SampleMiniConstants g_Mini;
};

Texture2D<float>        t_Depth;
Texture2D<float2>       t_MotionVectors;
Texture2D<float>        t_SpecularHitT;
Texture2D<float4>       t_DenoiserOutDiffRadianceHitDist;
Texture2D<float4>       t_DenoiserOutSpecRadianceHitDist;
Texture2D<float4>       t_DenoiserOutValidation;
Texture2D<float>        t_DenoiserViewspaceZ;
Texture2D<float>        t_DenoiserDisocclusionThresholdMix;

VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4>      u_OutputColor;
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4>      u_StableRadiance;
VK_IMAGE_FORMAT("r32ui")  RWTexture2DArray<uint>    u_StablePlanesHeader;
RWStructuredBuffer<StablePlane>                     u_StablePlanesBuffer;
VK_IMAGE_FORMAT("r32f")   RWTexture2D<float>        u_DenoiserViewspaceZ;
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4>      u_DenoiserMotionVectors;
VK_IMAGE_FORMAT("rgb10_a2") RWTexture2D<float4>     u_DenoiserNormalRoughness;
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4>      u_DenoiserDiffRadianceHitDist;
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4>      u_DenoiserSpecRadianceHitDist;
VK_IMAGE_FORMAT("r8")      RWTexture2D<float>       u_DenoiserDisocclusionThresholdMix;
VK_IMAGE_FORMAT("r8")      RWTexture2D<float>       u_CombinedHistoryClampRelax;

float LinearToSRGB(float Lin)
{
    if (Lin <= 0.0031308f)
        return Lin * 12.92f;
    return pow(Lin, 1.0f / 2.4f) * 1.055f - 0.055f;
}

float3 LinearToSRGB(float3 Lin)
{
    return float3(LinearToSRGB(Lin.x), LinearToSRGB(Lin.y), LinearToSRGB(Lin.z));
}

uint2 ClampPixelCoord(int2 PixelPos)
{
    const int2 MaxPixelPos = int2(int(g_Params.Width) - 1, int(g_Params.Height) - 1);
    return uint2(clamp(PixelPos, int2(0, 0), MaxPixelPos));
}

float3 LoadLDR(uint2 PixelPos, int2 Offset)
{
    return t_LdrColorScratch[ClampPixelCoord(int2(PixelPos) + Offset)].rgb;
}

void SaveLDR(uint2 PixelPos, float3 LinearColor)
{
    u_PostTonemapOutputColor[PixelPos] = float4(LinearToSRGB(LinearColor), 1.0);
}

float RTXPTLuminance(float3 Color)
{
    return dot(Color, float3(0.2126, 0.7152, 0.0722));
}

void RTXPTClampRadiance(inout float3 Radiance, float RangeK)
{
    const float SafeRangeK = max(RangeK, 1.0);
    const float ClampMax   = min(255.0, g_Frame.ptConsts.preExposedGrayLuminance * SafeRangeK);
    const float Lum        = RTXPTLuminance(Radiance);
    if (Lum > ClampMax)
        Radiance *= ClampMax / Lum;
}

float RTXPTComputeNeighbourDisocclusionRelaxation(StablePlanesContext StablePlanes,
                                                  int2 PixelPos,
                                                  int2 ImageSize,
                                                  uint StablePlaneIndex,
                                                  float3 RayDirC,
                                                  int2 Offset)
{
    const float kEdge     = 0.02;
    uint2       PixelPosN = clamp(PixelPos + Offset, int2(0, 0), ImageSize - 1.xx);
    uint        BranchID  = StablePlanes.GetBranchID(PixelPosN, StablePlaneIndex);
    if (BranchID == cStablePlaneInvalidBranchID)
        return kEdge;

    StablePlane SP      = StablePlanes.LoadStablePlane(PixelPosN, StablePlaneIndex);
    float3      RayDirN = SP.GetNormal();
    return 1.0 - dot(RayDirC, RayDirN);
}

float RTXPTComputeDisocclusionRelaxation(StablePlanesContext StablePlanes,
                                         uint2 PixelPos,
                                         uint StablePlaneIndex,
                                         StablePlane SP)
{
    const int2   ImageSize = int2(g_Frame.ptConsts.imageWidth, g_Frame.ptConsts.imageHeight);
    const float3 RayDirC   = SP.GetNormal();
    float        Relax     = 0.0;

    Relax += RTXPTComputeNeighbourDisocclusionRelaxation(StablePlanes, PixelPos, ImageSize, StablePlaneIndex, RayDirC, int2(-1, 0));
    Relax += RTXPTComputeNeighbourDisocclusionRelaxation(StablePlanes, PixelPos, ImageSize, StablePlaneIndex, RayDirC, int2(1, 0));
    Relax += RTXPTComputeNeighbourDisocclusionRelaxation(StablePlanes, PixelPos, ImageSize, StablePlaneIndex, RayDirC, int2(0, -1));
    Relax += RTXPTComputeNeighbourDisocclusionRelaxation(StablePlanes, PixelPos, ImageSize, StablePlaneIndex, RayDirC, int2(0, 1));
    return saturate((Relax - 0.00002) * 25.0);
}

[numthreads(NUM_COMPUTE_THREADS_PER_DIM, NUM_COMPUTE_THREADS_PER_DIM, 1)]
void main(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    const uint2 PixelPos = DispatchThreadID.xy;
#if RTXPT_POST_PROCESS_MODE == RTXPT_POST_PROCESS_HDR_TEST || RTXPT_POST_PROCESS_MODE == RTXPT_POST_PROCESS_EDGE_DETECTION
    if (PixelPos.x >= g_Params.Width || PixelPos.y >= g_Params.Height)
        return;
#else
    if (PixelPos.x >= g_Frame.ptConsts.imageWidth || PixelPos.y >= g_Frame.ptConsts.imageHeight)
        return;
#endif

#if RTXPT_POST_PROCESS_MODE == RTXPT_POST_PROCESS_RELAX_PREPARE_INPUTS || RTXPT_POST_PROCESS_MODE == RTXPT_POST_PROCESS_REBLUR_PREPARE_INPUTS
    const uint StablePlaneIndex       = g_Mini.params.x;
    const bool InitWithStableRadiance = g_Mini.params.y != 0;
    const uint ActiveStablePlaneCount = min(g_Frame.ptConsts.GetActiveStablePlaneCount(), cStablePlaneCount);
    if (StablePlaneIndex >= ActiveStablePlaneCount)
        return;

    StablePlanesContext StablePlanes = StablePlanesContext::make(
        u_StablePlanesHeader, u_StablePlanesBuffer, u_StableRadiance, g_Frame.ptConsts);

    if (InitWithStableRadiance)
    {
        u_OutputColor[PixelPos]               = float4(StablePlanes.LoadStableRadiance(PixelPos), 1.0);
        u_CombinedHistoryClampRelax[PixelPos] = 0.0;
    }

    bool HasSurface = false;
    uint BranchID   = StablePlanes.GetBranchID(PixelPos, StablePlaneIndex);
    if (BranchID != cStablePlaneInvalidBranchID)
    {
        StablePlane SP         = StablePlanes.LoadStablePlane(PixelPos, StablePlaneIndex);
        const bool  HitSurface = isfinite(SP.SceneLength);
        if (HitSurface)
        {
            HasSurface = true;

            float3 DiffBSDFEstimate;
            float3 SpecBSDFEstimate;
            UnpackTwoFp32ToFp16(SP.DenoiserPackedBSDFEstimate, DiffBSDFEstimate, SpecBSDFEstimate);
            DiffBSDFEstimate = max(DiffBSDFEstimate, 1e-4.xxx);
            SpecBSDFEstimate = max(SpecBSDFEstimate, 1e-4.xxx);

            float3 Throughput;
            float3 MotionVectors;
            UnpackTwoFp32ToFp16(SP.PackedThpAndMVs, Throughput, MotionVectors);

            CameraRay    PrimaryCameraRay = ComputeRayThinlens(g_Frame.ptConsts.camera, PixelPos, g_Frame.ptConsts.camera.Jitter, 0.5.xx);
            const float3 VirtualWorldPos  = PrimaryCameraRay.origin + PrimaryCameraRay.dir * SP.SceneLength;
            const float4 ViewPos         = mul(float4(VirtualWorldPos, 1.0), g_Frame.view.MatWorldToView);
            const float  VirtualViewZ    = ViewPos.z;

            float FinalRoughness     = max(0.2, SP.GetRoughness());
            float DisocclusionRelax  = 0.0;
            if (StablePlanesVertexIndexFromBranchID(BranchID) > 1)
                DisocclusionRelax = RTXPTComputeDisocclusionRelaxation(StablePlanes, PixelPos, StablePlaneIndex, SP);

            u_DenoiserViewspaceZ[PixelPos]               = VirtualViewZ;
            u_DenoiserMotionVectors[PixelPos]            = float4(MotionVectors, 0.0);
            u_DenoiserDisocclusionThresholdMix[PixelPos] = DisocclusionRelax;
            u_CombinedHistoryClampRelax[PixelPos]        = saturate(u_CombinedHistoryClampRelax[PixelPos] + DisocclusionRelax * saturate(RTXPTLuminance(Throughput)));

            FinalRoughness = saturate(FinalRoughness + DisocclusionRelax);

            float3 DenoiserDiffRadiance = SP.GetNoisyDiffRadiance() / DiffBSDFEstimate;
            float3 DenoiserSpecRadiance = SP.GetNoisySpecRadiance() / SpecBSDFEstimate;

            if (StablePlaneIndex == 0 &&
                g_Frame.ptConsts.stablePlanesSuppressPrimaryIndirectSpecularK != 0.0 &&
                g_Frame.ptConsts.GetActiveStablePlaneCount() > 1)
            {
                bool ShouldSuppress = true;
                for (uint Plane = 1; Plane < g_Frame.ptConsts.GetActiveStablePlaneCount(); ++Plane)
                    ShouldSuppress = ShouldSuppress && StablePlanes.GetBranchID(PixelPos, Plane) != cStablePlaneInvalidBranchID;
                DenoiserSpecRadiance *= ShouldSuppress ? saturate(1.0 - g_Frame.ptConsts.stablePlanesSuppressPrimaryIndirectSpecularK) : 1.0;
            }

            RTXPTClampRadiance(DenoiserDiffRadiance, g_Frame.ptConsts.denoiserRadianceClampK * 16.0);
            RTXPTClampRadiance(DenoiserSpecRadiance, g_Frame.ptConsts.denoiserRadianceClampK * 16.0);

            float SpecHitT = 0.0;
            if (StablePlanes.LoadDominantIndex(PixelPos) == StablePlaneIndex)
                SpecHitT = t_SpecularHitT[PixelPos];

            u_DenoiserNormalRoughness[PixelPos] = RTXPTDenoiserPackNormalAndRoughness(SP.GetNormal(), FinalRoughness);

#    if RTXPT_POST_PROCESS_MODE == RTXPT_POST_PROCESS_RELAX_PREPARE_INPUTS
            u_DenoiserDiffRadianceHitDist[PixelPos] = RTXPTDenoiserRelaxPackRadianceHitDist(DenoiserDiffRadiance, 0.0);
            u_DenoiserSpecRadianceHitDist[PixelPos] = RTXPTDenoiserRelaxPackRadianceHitDist(DenoiserSpecRadiance, SpecHitT);
#    else
            const float DiffNormHitDist = 0.0;
            const float SpecNormHitDist = RTXPTDenoiserReblurGetNormHitDist(SpecHitT, VirtualViewZ, SP.GetRoughness());
            u_DenoiserDiffRadianceHitDist[PixelPos] = RTXPTDenoiserReblurPackRadianceNormHitDist(DenoiserDiffRadiance, DiffNormHitDist);
            u_DenoiserSpecRadianceHitDist[PixelPos] = RTXPTDenoiserReblurPackRadianceNormHitDist(DenoiserSpecRadiance, SpecNormHitDist);
#    endif
        }
    }

    if (!HasSurface)
        u_DenoiserViewspaceZ[PixelPos] = RTXPT_VIEWZ_SKY_MARKER;
#endif

#if RTXPT_POST_PROCESS_MODE == RTXPT_POST_PROCESS_RELAX_FINAL_MERGE || RTXPT_POST_PROCESS_MODE == RTXPT_POST_PROCESS_REBLUR_FINAL_MERGE
    const uint StablePlaneIndex = g_Mini.params.x;
    const bool HasValidation    = g_Mini.params.y != 0;
    const uint ActiveStablePlaneCount = min(g_Frame.ptConsts.GetActiveStablePlaneCount(), cStablePlaneCount);
    if (StablePlaneIndex >= ActiveStablePlaneCount)
        return;

    const bool HasSurface = t_DenoiserViewspaceZ[PixelPos] != RTXPT_VIEWZ_SKY_MARKER;
    const uint SPAddress  = GenericTSPixelToAddress(PixelPos,
                                                    StablePlaneIndex,
                                                    g_Frame.ptConsts.genericTSLineStride,
                                                    g_Frame.ptConsts.genericTSPlaneStride);

    float4 DiffRadiance = 0.0.xxxx;
    float4 SpecRadiance = 0.0.xxxx;

    if (HasSurface)
    {
        float3 DiffBSDFEstimate;
        float3 SpecBSDFEstimate;
        UnpackTwoFp32ToFp16(u_StablePlanesBuffer[SPAddress].DenoiserPackedBSDFEstimate, DiffBSDFEstimate, SpecBSDFEstimate);

        DiffRadiance = t_DenoiserOutDiffRadianceHitDist[PixelPos];
        SpecRadiance = t_DenoiserOutSpecRadianceHitDist[PixelPos];
        RTXPTDenoiserPostDenoiseProcess(DiffBSDFEstimate, SpecBSDFEstimate, DiffRadiance, SpecRadiance);

        u_OutputColor[PixelPos].xyz += max(0.0.xxx, DiffRadiance.rgb + SpecRadiance.rgb);
    }

    if (HasValidation)
    {
        const float4 Validation = t_DenoiserOutValidation[PixelPos];
        if (Validation.a > 0.0)
            u_OutputColor[PixelPos] = float4(Validation.rgb, 1.0);
    }
#endif

#if RTXPT_POST_PROCESS_MODE == RTXPT_POST_PROCESS_NO_DENOISER_FINAL_MERGE
    StablePlanesContext StablePlanes = StablePlanesContext::make(
        u_StablePlanesHeader, u_StablePlanesBuffer, u_StableRadiance, g_Frame.ptConsts);
    u_OutputColor[PixelPos] = float4(StablePlanes.GetAllRadiance(PixelPos), 1.0);
#endif

#if RTXPT_POST_PROCESS_MODE == RTXPT_POST_PROCESS_STABLE_PLANES_DEBUG_VIZ
    const int   DebugPlaneIndex = int(g_Mini.params.x) - 1;
    const uint3 DebugCoord      = StablePlaneDebugVizFourWaySplitCoord(DebugPlaneIndex, PixelPos, uint2(g_Frame.ptConsts.imageWidth, g_Frame.ptConsts.imageHeight));

    StablePlanesContext StablePlanes = StablePlanesContext::make(
        u_StablePlanesHeader, u_StablePlanesBuffer, u_StableRadiance, g_Frame.ptConsts);

    float3 Color = 0.0.xxx;
    if (DebugCoord.z < g_Frame.ptConsts.GetActiveStablePlaneCount())
    {
        const uint BranchID = StablePlanes.GetBranchID(DebugCoord.xy, DebugCoord.z);
        Color = BranchID == cStablePlaneInvalidBranchID ?
            float3(((PixelPos.x + PixelPos.y) & 1u) != 0u, 0.0, 0.0) :
            StablePlaneDebugVizColor(DebugCoord.z);
    }

    u_OutputColor[PixelPos] = float4(Color, 1.0);
#endif

#if RTXPT_POST_PROCESS_MODE == RTXPT_POST_PROCESS_HDR_TEST
    float3 ExistingColor = u_ProcessedOutputColor[PixelPos].rgb;
    if (length(float2(PixelPos.xy) - float2(800.0, 500.0)) < 100.0)
        ExistingColor.z += 10.0;
    u_ProcessedOutputColor[PixelPos] = float4(ExistingColor, 1.0);
#elif RTXPT_POST_PROCESS_MODE == RTXPT_POST_PROCESS_EDGE_DETECTION
    const int OffX = 1;
    const int OffY = 1;

    const float3 S00 = LoadLDR(PixelPos, int2(-OffX, -OffY));
    const float3 S01 = LoadLDR(PixelPos, int2(0, -OffY));
    const float3 S02 = LoadLDR(PixelPos, int2(OffX, -OffY));
    const float3 S10 = LoadLDR(PixelPos, int2(-OffX, 0));
    const float3 S12 = LoadLDR(PixelPos, int2(OffX, 0));
    const float3 S20 = LoadLDR(PixelPos, int2(-OffX, OffY));
    const float3 S21 = LoadLDR(PixelPos, int2(0, OffY));
    const float3 S22 = LoadLDR(PixelPos, int2(OffX, OffY));

    const float3 SobelX = S00 + 2.0 * S10 + S20 - S02 - 2.0 * S12 - S22;
    const float3 SobelY = S00 + 2.0 * S01 + S02 - S20 - 2.0 * S21 - S22;
    const float3 EdgeSqr = SobelX * SobelX + SobelY * SobelY;
    const float  Threshold = g_Params.EdgeDetectionThreshold;
    const float3 EdgeColor = 1.0.xxx - (EdgeSqr > Threshold.xxx * Threshold.xxx);
    SaveLDR(PixelPos, saturate(EdgeColor));
#endif
}
