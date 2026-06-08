#ifndef __PATH_TRACER_NESTED_DIELECTRICS_HLSLI__
#define __PATH_TRACER_NESTED_DIELECTRICS_HLSLI__

#include "Rendering/Materials/MaterialBridge.hlsli"
#include "Rendering/Materials/InteriorList.hlsli"

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

} // namespace PathTracer

#endif // __PATH_TRACER_NESTED_DIELECTRICS_HLSLI__
