#ifndef __PATH_STATE_HLSLI__
#define __PATH_STATE_HLSLI__

#define PATH_STATE_DEFINED

#include "Config.h"
#include "PathTracerHelpers.hlsli"
#include "PathTracerShared.h"
#include "Rendering/Materials/InteriorList.hlsli"
#include "Utils/SampleGenerators.hlsli"

static const uint kVertexIndexBitCount       = 10u;
static const uint kVertexIndexBitMask        = (1u << kVertexIndexBitCount) - 1u;
static const uint kPathFlagsBitCount         = 32u - kVertexIndexBitCount;
static const uint kPathFlagsBitMask          = ((1u << kPathFlagsBitCount) - 1u) << kVertexIndexBitCount;
static const uint kStablePlaneIndexBitOffset = 14u + kVertexIndexBitCount;
static const uint kStablePlaneIndexBitMask   = ((1u << 2u) - 1u) << kStablePlaneIndexBitOffset;

enum class PathFlags
{
    active                          = (1 << 0),
    hit                             = (1 << 1),
    transmission                    = (1 << 2),
    specular                        = (1 << 3),
    delta                           = (1 << 4),
    insideDielectricVolume          = (1 << 5),
    terminateAtNextBounce           = (1 << 6),
    restirGIStarted                 = (1 << 7),
    restirGICollectSecondarySurface = (1 << 8),
    enableThreadReorder             = (1 << 9),
    deltaTransmissionPath           = (1 << 11),
    deltaOnlyPath                   = (1 << 12),
    deltaTreeExplorer               = (1 << 13),
    stablePlaneIndexBit0            = (1 << 14),
    stablePlaneIndexBit1            = (1 << 15),
    stablePlaneOnPlane              = (1 << 16),
    stablePlaneOnBranch             = (1 << 17),
    stablePlaneBaseScatterDiff      = (1 << 18),
    exportSpecHitTQueued            = (1 << 19),
    stablePlaneOnDominantBranch     = (1 << 20),
};

enum class PackedCounters
{
    DiffuseBounces         = 0,
    RejectedHits           = 1,
    BouncesFromStablePlane = 2,
};

struct PathState
{
    uint4 PackOriginId;
    uint4 PackDirSceneLength;

    // Mode-specific payload lane: BuildStablePlanes stores imageXformPacked here,
    // other modes store packed L. Both variants must remain uint2 to keep PathPayload 5xuint4.
    uint2 pack23;
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
    uint2 imageXformPacked;
#else
    uint2 pack45;
#endif

#if RTXPT_NESTED_DIELECTRICS_QUALITY > 0 || defined(__INTELLISENSE__)
    InteriorList interiorList;
#endif
    uint packedCounters;
    uint stableBranchID;

    RayCone rayCone;
    uint    pack0;
    uint    pack1;
    // [PathState packing refactor — Gate 1] The flags bits and the vertex index used to share one
    // packed word (flagsAndVertexIndex). They are split into two independent members so DXC no longer
    // has to track them as sub-lanes of a single uint. The single 32-bit wire layout is unchanged:
    // they are recombined only at the PathPayload::pack/unpack boundary.
    uint    flags;        // PathFlags region, bit[10..31] (kPathFlagsBitMask); low 10 bits always 0
    uint    vertexIndex;  // vertex index, low 10 bits (kVertexIndexBitMask)
    // [PathState packing refactor — Gate 3, 2026-06-07] stablePlaneIndex used to live as a 2-bit
    // subfield (bits 24..25) inside the shared flags word. The FILL plane transition
    // (StablePlanesOnScatter) is the only site that writes it with a non-zero value, and it does so
    // interleaved with several setFlag() read-modify-writes on that same flags word — the exact
    // intra-word aliasing pattern DXC miscompiles (cf. the vertexIndex split in Gate 1, which fixed the
    // opaque path). It is split into its own member so the masked RMW no longer aliases the flag writes.
    // The single 32-bit wire layout is unchanged: it is recombined into packed[4].w bits 24..25 only at
    // the PathPayload::pack/unpack boundary.
    uint    stablePlaneIndex;

    float3 GetOrigin() { return asfloat(PackOriginId.xyz); }
    void SetOrigin(float3 origin) { PackOriginId.xyz = asuint(origin); }

    uint GetId() { return PackOriginId.w; }
    void SetId(uint id) { PackOriginId.w = id; }

    float3 GetDir() { return asfloat(PackDirSceneLength.xyz); }
    void SetDir(float3 dir) { PackDirSceneLength.xyz = asuint(dir); }

    float GetSceneLength() { return asfloat(PackDirSceneLength.w); }
    void SetSceneLength(float sceneLength) { PackDirSceneLength.w = asuint(sceneLength); }

#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
    float3x3 GetImageXform() { return UnpackOrthoMatrix(imageXformPacked); }
    void SetImageXform(float3x3 p) { imageXformPacked = PackOrthoMatrix(p); }

    void SetMotionVectorSceneLength(float l) { pack0 = asuint(l); }
    float GetMotionVectorSceneLength() { return asfloat(pack0); }
    lpuint GetPackedMISInfo() { return 0; }
    lpfloat GetBsdfScatterPdf() { return 0.0; }
    lpfloat GetThpRuRuCorrection() { return 1.0; }
#else
    void SetFireflyFilterK_BsdfScatterPdf(float fireflyFilterK, float bsdfScatterPdf)
    {
        pack0 = (f32tof16(clamp(fireflyFilterK, 0.0, HLF_MAX)) << 16) |
            f32tof16(clamp(bsdfScatterPdf, 0.0, HLF_MAX));
    }
    lpfloat GetFireflyFilterK() { return lpfloat(f16tof32(pack0 >> 16)); }
    lpfloat GetBsdfScatterPdf() { return lpfloat(f16tof32(pack0 & 0xffff)); }

    void SetPackedMISInfo_ThpRuRuCorrection(uint packedMISInfo, float thpRuRuCorrection)
    {
        pack1 = (packedMISInfo << 16) | f32tof16(clamp(thpRuRuCorrection, 0.0, HLF_MAX));
    }
    lpuint GetPackedMISInfo() { return lpuint(pack1 >> 16); }
    lpfloat GetThpRuRuCorrection() { return lpfloat(f16tof32(pack1 & 0xffff)); }
#endif

    void SetThp(float3 thp)
    {
        pack23 = Fp32ToFp16NoClamp(float4(clamp(thp, 0.xxx, HLF_MAX.xxx), 0.0));
    }
    float3 GetThp() { return Fp16ToFp32(pack23).xyz; }

#if PATH_TRACER_MODE != PATH_TRACER_MODE_BUILD_STABLE_PLANES
    void SetL(float4 l)
    {
        pack45 = Fp32ToFp16NoClamp(clamp(l, 0.xxxx, HLF_MAX.xxxx));
    }
    float4 GetL() { return Fp16ToFp32(pack45); }
#endif

    bool isTerminated() { return !isActive(); }
    bool isActive() { return hasFlag(PathFlags::active); }
    bool isHit() { return hasFlag(PathFlags::hit); }
    bool wasScatterTransmission() { return hasFlag(PathFlags::transmission); }
    bool wasScatterSpecular() { return hasFlag(PathFlags::specular); }
    bool wasScatterDelta() { return hasFlag(PathFlags::delta); }
    bool isInsideDielectricVolume() { return hasFlag(PathFlags::insideDielectricVolume); }
    bool isDeltaTransmissionPath() { return hasFlag(PathFlags::deltaTransmissionPath); }
    bool isDeltaOnlyPath() { return hasFlag(PathFlags::deltaOnlyPath); }
    bool isTerminatingAtNextBounce() { return hasFlag(PathFlags::terminateAtNextBounce); }

    void terminate() { setFlag(PathFlags::active, false); }
    void setActive() { setFlag(PathFlags::active); }
    void clearHit() { setFlag(PathFlags::hit, false); }
    void setTerminateAtNextBounce() { setFlag(PathFlags::terminateAtNextBounce); }

    void clearScatterEventFlags()
    {
        const uint bits = (((uint)PathFlags::transmission) |
                           ((uint)PathFlags::specular) |
                           ((uint)PathFlags::delta)) << kVertexIndexBitCount;
        flags &= ~bits;
    }

    void setScatterTransmission(bool value = true) { setFlag(PathFlags::transmission, value); }
    void setScatterSpecular(bool value = true) { setFlag(PathFlags::specular, value); }
    void setScatterDelta(bool value = true) { setFlag(PathFlags::delta, value); }
    void setInsideDielectricVolume(bool value = true) { setFlag(PathFlags::insideDielectricVolume, value); }
    void setDeltaTransmissionPath(bool value = true) { setFlag(PathFlags::deltaTransmissionPath, value); }
    void setDeltaOnlyPath(bool value = true) { setFlag(PathFlags::deltaOnlyPath, value); }

    bool hasFlag(PathFlags flag)
    {
        const uint bit = ((uint)flag) << kVertexIndexBitCount;
        return (flags & bit) != 0;
    }

    void setFlag(PathFlags flag, bool value = true)
    {
        const uint bit = ((uint)flag) << kVertexIndexBitCount;
        flags = value ? (flags | bit) : (flags & ~bit);
    }

    uint getCounter(PackedCounters type)
    {
        const uint shift = ((uint)type) << 3;
        return (packedCounters >> shift) & 0xff;
    }

    void setCounter(PackedCounters type, uint bounces)
    {
        const uint shift = ((uint)type) << 3;
        packedCounters = (packedCounters & ~((uint)0xff << shift)) | ((bounces & 0xff) << shift);
    }

    void incrementCounter(PackedCounters type)
    {
        packedCounters += 1u << (((uint)type) << 3);
    }

    uint2 GetPixelPos() { return PathIDToPixel(GetId()); }

    // [PathState packing refactor — Gate 1, 2026-06-07] vertexIndex now lives in its own member, so
    // these accessors operate on an isolated word instead of doing masked read-modify-write inside the
    // shared flags word. This removes the intra-word aliasing that DXC miscompiles when realtime
    // HandleHit contains nested-dielectric handling. getVertexIndex keeps the mask to stay bit-for-bit
    // equivalent to the old combined form after a pack/unpack roundtrip.
    void setVertexIndex(uint index) { vertexIndex = index; }
    uint getVertexIndex() { return vertexIndex & kVertexIndexBitMask; }
    void incrementVertexIndex() { vertexIndex += 1; }
    void decrementVertexIndex() { vertexIndex -= 1; }

    Ray getScatterRay()
    {
        return Ray::make(GetOrigin(), GetDir(), 0.0, kMaxRayTravel);
    }

    // [Gate 3] stablePlaneIndex now lives in its own member instead of bits 24..25 of the flags word,
    // removing the intra-word aliasing that DXC miscompiles during the FILL plane transition.
    uint getStablePlaneIndex()
    {
        return stablePlaneIndex;
    }

    void setStablePlaneIndex(uint index)
    {
        stablePlaneIndex = index;
    }

#if PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE || defined(__INTELLISENSE__)
    void InitReferencePrimaryDepth(float depth)
    {
        stableBranchID   = asuint(depth);
        stablePlaneIndex = 0u;
    }

    bool HasReferencePrimaryDepth()
    {
        return stablePlaneIndex != 0u;
    }

    void CaptureReferencePrimaryDepth(float depth)
    {
        if (!HasReferencePrimaryDepth())
        {
            stableBranchID   = asuint(depth);
            stablePlaneIndex = 1u;
        }
    }

    float GetReferencePrimaryDepth()
    {
        return asfloat(stableBranchID);
    }
#endif
};

#endif // __PATH_STATE_HLSLI__
