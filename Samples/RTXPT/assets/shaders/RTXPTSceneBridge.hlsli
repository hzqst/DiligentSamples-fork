#ifndef RTXPT_SCENE_BRIDGE_HLSLI
#define RTXPT_SCENE_BRIDGE_HLSLI

#include "RTXPTShaderShared.hlsli"

// Global shader resources used by the scene bridge. C++ binds these as static SRVs.
ConstantBuffer<RTXPTFrameConstants>    g_FrameConstants;
StructuredBuffer<RTXPTSubInstanceData> g_SubInstanceData;
StructuredBuffer<RTXPTLightData>       g_Lights;

namespace Bridge
{
#ifdef RTXPT_ENABLE_HIT_BRIDGE
    // Linear index for the SubInstanceData entry that describes the currently hit (instance, geometry).
    // C++ stores the per-instance sub-instance base in InstanceID(), and GeometryIndex() is used to
    // select the geometry within the BLAS.
    uint GetSubInstanceIndex()
    {
        return InstanceID() + GeometryIndex();
    }

    // Returns the SubInstanceData entry for the current hit.
    // The caller is responsible for guarding against an empty/unbound table via HasSubInstanceTable().
    RTXPTSubInstanceData GetSubInstanceData()
    {
        return g_SubInstanceData[GetSubInstanceIndex()];
    }

    // True when g_SubInstanceData has at least one entry. The C++ side guarantees a dummy entry
    // is bound when the scene has no real geometry so that this helper still returns a defined value.
    bool HasSubInstanceTable()
    {
        uint Count  = 0;
        uint Stride = 0;
        g_SubInstanceData.GetDimensions(Count, Stride);
        return Count > 0;
    }
#endif

    // Total active light count. May be zero on scenes without lights.
    uint GetLightCount()
    {
        uint Count  = 0;
        uint Stride = 0;
        g_Lights.GetDimensions(Count, Stride);
        return Count;
    }

    RTXPTLightData GetLight(uint Index)
    {
        return g_Lights[Index];
    }
} // namespace Bridge

// TODO(RTXPT-Port Phase 5.2): Add reference-path-tracer scene accessors: per-vertex normal/UV fetch, ray cone construction, and tangent frame reconstruction.
// TODO(RTXPT-Port Phase 5.3): Add alpha-mask/transparent flags to RTXPTSubInstanceData and propagate them into any-hit specialization.

#endif // RTXPT_SCENE_BRIDGE_HLSLI
