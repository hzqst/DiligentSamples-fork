#ifndef __PATH_PAYLOAD_HLSLI__
#define __PATH_PAYLOAD_HLSLI__

#include "Config.h"

#ifndef RTXPT_PATH_PAYLOAD_UINT4_COUNT
#    if defined(PATH_TRACER_MODE) && defined(PATH_TRACER_MODE_REFERENCE) && PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE
#        define RTXPT_PATH_PAYLOAD_UINT4_COUNT 7
#    else
#        define RTXPT_PATH_PAYLOAD_UINT4_COUNT 5
#    endif
#endif
#ifndef RTXPT_PATH_PAYLOAD_SIZE_BYTES
#    define RTXPT_PATH_PAYLOAD_SIZE_BYTES (RTXPT_PATH_PAYLOAD_UINT4_COUNT * 4 * 4)
#endif

// Packed and aligned representation of PathState in a pre-raytrace state.
struct PAYLOAD_QUALIFIER PathPayload
{
    uint4 packed[RTXPT_PATH_PAYLOAD_UINT4_COUNT] PAYLOAD_FIELD_RW_ALL;

#ifdef PATH_STATE_DEFINED
    static PathPayload pack(const PathState path);
    static PathState unpack(const PathPayload p);
    static PathPayload fromArray(const uint4 packed[RTXPT_PATH_PAYLOAD_UINT4_COUNT]);
    static void toArray(const PathPayload p, out uint4 packed[RTXPT_PATH_PAYLOAD_UINT4_COUNT]);
#endif
};

#ifdef PATH_STATE_DEFINED

PathPayload PathPayload::pack(const PathState path)
{
    PathPayload p;

    p.packed[0] = path.PackOriginId;
    p.packed[1] = path.PackDirSceneLength;

#if PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE
    p.packed[2] = asuint(float4(path.referenceThp, 0.0));
    p.packed[3] = asuint(path.referenceL);

#    if RTXPT_NESTED_DIELECTRICS_QUALITY > 0
    p.packed[4].xy = uint2(path.interiorList.slots[0], path.interiorList.slots[1]);
#    else
    p.packed[4].xy = 0;
#    endif
    p.packed[4].z = path.packedCounters;
    p.packed[4].w = (path.flags & ~kStablePlaneIndexBitMask)
                  | ((path.stablePlaneIndex << kStablePlaneIndexBitOffset) & kStablePlaneIndexBitMask)
                  | (path.vertexIndex & kVertexIndexBitMask);

    p.packed[5].x = path.rayCone.widthSpreadAngleFP16;
    p.packed[5].y = asuint(path.referenceFireflyFilterK);
    p.packed[5].z = asuint(path.referenceBsdfScatterPdf);
    p.packed[5].w = 0u;

    p.packed[6].x  = path.referencePackedMISInfo;
    p.packed[6].y  = asuint(path.referenceThpRuRuCorrection);
    p.packed[6].z  = path.stableBranchID;
    p.packed[6].w  = 0u;
#else
    p.packed[2].xy = path.pack23;
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
    p.packed[2].zw = path.imageXformPacked;
#else
    p.packed[2].zw = path.pack45;
#endif

#if RTXPT_NESTED_DIELECTRICS_QUALITY > 0
    p.packed[3].xy = uint2(path.interiorList.slots[0], path.interiorList.slots[1]);
#else
    p.packed[3].xy = 0;
#endif
    p.packed[3].z = path.packedCounters;
    p.packed[3].w = path.stableBranchID;

    p.packed[4].x = path.rayCone.widthSpreadAngleFP16;
    p.packed[4].y = path.pack0;
    p.packed[4].z = path.pack1;
    // Recombine the split flags / stablePlaneIndex / vertexIndex members into the single 32-bit wire
    // word. stablePlaneIndex occupies bits 24..25 (kStablePlaneIndexBitMask); flags no longer carries
    // those bits, so the result is bit-for-bit identical to the pre-Gate-3 layout.
    p.packed[4].w = (path.flags & ~kStablePlaneIndexBitMask)
                  | ((path.stablePlaneIndex << kStablePlaneIndexBitOffset) & kStablePlaneIndexBitMask)
                  | (path.vertexIndex & kVertexIndexBitMask);
#endif

    return p;
}

PathState PathPayload::unpack(const PathPayload p)
{
    PathState path;

    path.PackOriginId       = p.packed[0];
    path.PackDirSceneLength = p.packed[1];

#if PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE
    path.referenceThp = asfloat(p.packed[2].xyz);
    path.referenceL   = asfloat(p.packed[3]);

#    if RTXPT_NESTED_DIELECTRICS_QUALITY > 0
    path.interiorList.slots[0] = p.packed[4].x;
    path.interiorList.slots[1] = p.packed[4].y;
#    endif
    path.packedCounters = p.packed[4].z;
    path.flags          = p.packed[4].w & kPathFlagsBitMask & ~kStablePlaneIndexBitMask;
    path.stablePlaneIndex =
        (p.packed[4].w & kStablePlaneIndexBitMask) >> kStablePlaneIndexBitOffset;
    path.vertexIndex = p.packed[4].w & kVertexIndexBitMask;

    path.rayCone.widthSpreadAngleFP16 = p.packed[5].x;
    path.referenceFireflyFilterK      = asfloat(p.packed[5].y);
    path.referenceBsdfScatterPdf      = asfloat(p.packed[5].z);

    path.referencePackedMISInfo      = p.packed[6].x;
    path.referenceThpRuRuCorrection  = asfloat(p.packed[6].y);
    path.stableBranchID              = p.packed[6].z;
#else
    path.pack23 = p.packed[2].xy;
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
    path.imageXformPacked = p.packed[2].zw;
#else
    path.pack45 = p.packed[2].zw;
#endif

#if RTXPT_NESTED_DIELECTRICS_QUALITY > 0
    path.interiorList.slots[0] = p.packed[3].x;
    path.interiorList.slots[1] = p.packed[3].y;
#endif
    path.packedCounters = p.packed[3].z;
    path.stableBranchID = p.packed[3].w;

    path.rayCone.widthSpreadAngleFP16 = p.packed[4].x;
    path.pack0                        = p.packed[4].y;
    path.pack1                        = p.packed[4].z;
    // Split the single 32-bit wire word back into the independent flags / stablePlaneIndex / vertexIndex
    // members. The stablePlaneIndex bits are masked out of flags so the flag word stays isolated.
    path.flags                        = p.packed[4].w & kPathFlagsBitMask & ~kStablePlaneIndexBitMask;
    path.stablePlaneIndex             = (p.packed[4].w & kStablePlaneIndexBitMask) >> kStablePlaneIndexBitOffset;
    path.vertexIndex                  = p.packed[4].w & kVertexIndexBitMask;
#endif

    return path;
}

PathPayload PathPayload::fromArray(const uint4 packed[RTXPT_PATH_PAYLOAD_UINT4_COUNT])
{
    PathPayload p;
    p.packed[0] = packed[0];
    p.packed[1] = packed[1];
    p.packed[2] = packed[2];
    p.packed[3] = packed[3];
    p.packed[4] = packed[4];
#if RTXPT_PATH_PAYLOAD_UINT4_COUNT > 5
    p.packed[5] = packed[5];
    p.packed[6] = packed[6];
#endif
    return p;
}

void PathPayload::toArray(const PathPayload p, out uint4 packed[RTXPT_PATH_PAYLOAD_UINT4_COUNT])
{
    packed[0] = p.packed[0];
    packed[1] = p.packed[1];
    packed[2] = p.packed[2];
    packed[3] = p.packed[3];
    packed[4] = p.packed[4];
#if RTXPT_PATH_PAYLOAD_UINT4_COUNT > 5
    packed[5] = p.packed[5];
    packed[6] = p.packed[6];
#endif
}

#endif

#endif // __PATH_PAYLOAD_HLSLI__
