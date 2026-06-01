#ifndef __PATH_TRACER_NESTED_DIELECTRICS_HLSLI__
#define __PATH_TRACER_NESTED_DIELECTRICS_HLSLI__

#include "Rendering/Materials/MaterialBridge.hlsli"
#include "Rendering/Materials/InteriorList.hlsli"
#include "Rendering/Materials/LobeType.hlsli"

namespace PathTracer
{
    float ComputeOutsideIoR(InteriorList interiorList, uint materialID, bool entering)
    {
        uint outsideMaterialID = interiorList.getTopMaterialID();
        if (!entering && outsideMaterialID == materialID)
            outsideMaterialID = interiorList.getNextMaterialID();

        if (outsideMaterialID == InteriorList::kNoMaterial)
            return 1.0;

        return Bridge::loadIoR(outsideMaterialID);
    }

    uint GetMaxRejectedDielectricHits(uint nestedQuality)
    {
        return nestedQuality == 2u ? 16u : 4u;
    }

    bool HandleNestedDielectrics(PathPayload payload,
                                 uint nestedQuality,
                                 inout InteriorList interiorList,
                                 inout uint rejectedHits,
                                 out float outsideIoR)
    {
        const bool entering = payload.frontFacing != 0u;
        outsideIoR          = ComputeOutsideIoR(interiorList, payload.materialID, entering);

        if (nestedQuality == 0u || payload.thinSurface != 0u)
            return true;

        const uint maxRejectedHits = GetMaxRejectedDielectricHits(nestedQuality);
        if (rejectedHits < maxRejectedHits && !interiorList.isTrueIntersection(payload.nestedPriority))
        {
            ++rejectedHits;
            interiorList.handleIntersection(payload.materialID, payload.nestedPriority, entering);
            return false;
        }

        if (nestedQuality == 2u && rejectedHits >= maxRejectedHits && !interiorList.isTrueIntersection(payload.nestedPriority))
            return false;

        return true;
    }

    void UpdateNestedDielectricsOnScatterTransmission(PathPayload payload, uint lobe, inout InteriorList interiorList)
    {
        if ((lobe & kLobeTypeTransmission) == 0u || payload.thinSurface != 0u)
            return;

        interiorList.handleIntersection(payload.materialID, payload.nestedPriority, payload.frontFacing != 0u);
    }
} // namespace PathTracer

#endif // __PATH_TRACER_NESTED_DIELECTRICS_HLSLI__
