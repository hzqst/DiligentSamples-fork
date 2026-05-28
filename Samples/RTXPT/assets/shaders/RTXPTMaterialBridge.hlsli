#ifndef RTXPT_MATERIAL_BRIDGE_HLSLI
#define RTXPT_MATERIAL_BRIDGE_HLSLI

#include "RTXPTShaderShared.hlsli"

StructuredBuffer<RTXPTMaterialAttribs> g_Materials;

namespace Bridge
{
    bool HasMaterialTable()
    {
        uint Count  = 0;
        uint Stride = 0;
        g_Materials.GetDimensions(Count, Stride);
        return Count > 0;
    }

    uint GetMaterialCount()
    {
        uint Count  = 0;
        uint Stride = 0;
        g_Materials.GetDimensions(Count, Stride);
        return Count;
    }

    // Out-of-range indices clamp to the last material so a bad MaterialID never UB-reads.
    RTXPTMaterialAttribs GetMaterial(uint MaterialID)
    {
        const uint LastIndex = max(GetMaterialCount(), 1u) - 1u;
        const uint Index     = min(MaterialID, LastIndex);
        return g_Materials[Index];
    }

    float4 GetMaterialBaseColor(uint MaterialID)
    {
        return GetMaterial(MaterialID).BaseColorFactor;
    }
} // namespace Bridge

// TODO(RTXPT-Port Phase 5.2): Replace the flat base color helper with a per-hit BSDF sampler that reads roughness/metallic/normal scale/IOR and feeds the reference path tracer.
// TODO(RTXPT-Port Phase 5.3): Bind material textures (base color, normal, MR, emissive, occlusion) and expose helpers that respect TextureShaderAttribs UV selectors and wrap modes.

#endif // RTXPT_MATERIAL_BRIDGE_HLSLI
