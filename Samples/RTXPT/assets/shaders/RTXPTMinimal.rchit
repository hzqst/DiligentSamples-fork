#define RTXPT_ENABLE_HIT_BRIDGE 1
#include "RTXPTSceneBridge.hlsli"
#include "RTXPTMaterialBridge.hlsli"

static float3 ComputeBarycentricFallback(in BuiltInTriangleIntersectionAttributes Attributes)
{
    const float3 Barycentrics = float3(1.0 - Attributes.barycentrics.x - Attributes.barycentrics.y,
                                       Attributes.barycentrics.x,
                                       Attributes.barycentrics.y);
    const float InstanceTint = frac(float(InstanceID() * 17 + PrimitiveIndex() * 3) * 0.037);
    const float Depth        = saturate(RayTCurrent() / 150.0);
    return lerp(Barycentrics, float3(Depth, 1.0 - Depth, InstanceTint), 0.35);
}

[shader("closesthit")]
void main(inout RTXPTPrimaryPayload Payload,
          in BuiltInTriangleIntersectionAttributes Attributes)
{
    const float Depth = saturate(RayTCurrent() / 150.0);
    float3      Color = ComputeBarycentricFallback(Attributes);

    if (Bridge::HasSubInstanceTable() && Bridge::HasMaterialTable())
    {
        const RTXPTSubInstanceData SubInstance = Bridge::GetSubInstanceData();
        const RTXPTMaterialAttribs Material    = Bridge::GetMaterial(SubInstance.MaterialID);
        const float NdotV                      = saturate(dot(-WorldRayDirection(), float3(0.0, 1.0, 0.0)) * 0.5 + 0.5);
        Color                                  = Material.BaseColorFactor.rgb * (0.4 + 0.6 * NdotV);
    }

    Payload.ColorDepth = float4(Color, Depth);
}

// TODO(RTXPT-Port Phase 5.2): Replace flat base color with the reference path tracer shading (BSDF + light sampling + NEE).
// TODO(RTXPT-Port Phase 5.3): Honor RTXPTMaterialAttribs.AlphaMode/AlphaCutoff via any-hit specialization instead of forcing opaque rays.
