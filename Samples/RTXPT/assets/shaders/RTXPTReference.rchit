#define RTXPT_ENABLE_HIT_BRIDGE 1
#include "RTXPTSceneBridge.hlsli"
#include "RTXPTMaterialBridge.hlsli"

[shader("closesthit")]
void main(inout RTXPTPathTracerPayload Payload,
          in BuiltInTriangleIntersectionAttributes Attributes)
{
    Payload.HitFlag     = 1u;
    Payload.HitDistance = RayTCurrent();
    Payload.Emission    = float3(0.0, 0.0, 0.0);

    // Default to a barycentric debug color so we still see something if the bridge tables are unbound.
    float3 BaseColor = float3(Attributes.barycentrics.x,
                              Attributes.barycentrics.y,
                              1.0 - Attributes.barycentrics.x - Attributes.barycentrics.y);
    float3 WorldNormal = -WorldRayDirection();
    float3 WorldPos    = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

    if (Bridge::HasSubInstanceTable() && Bridge::HasMaterialTable())
    {
        const RTXPTSubInstanceData SubInstance = Bridge::GetSubInstanceData();
        const RTXPTMaterialAttribs Material    = Bridge::GetMaterial(SubInstance.MaterialID);

        RTXPTVertex V0;
        RTXPTVertex V1;
        RTXPTVertex V2;
        Bridge::GetTriangleVertices(SubInstance, PrimitiveIndex(), V0, V1, V2);

        WorldPos    = Bridge::ComputeWorldHitPosition(V0, V1, V2, Attributes.barycentrics);
        WorldNormal = Bridge::InterpolateNormal(V0, V1, V2, Attributes.barycentrics);
        // Renormalize against the geometric normal if the interpolated normal is nearly zero
        // (degenerate vertex data) - keeps the shader robust on bad assets.
        if (dot(WorldNormal, WorldNormal) < 1e-6)
            WorldNormal = Bridge::ComputeGeometricNormal(V0, V1, V2);
        // Flip the shading normal to face the camera (single-sided diffuse lighting; transmission lands in Phase 5.3).
        if (dot(WorldNormal, WorldRayDirection()) > 0.0)
            WorldNormal = -WorldNormal;

        BaseColor = Material.BaseColorFactor.rgb;
    }

    Payload.WorldPos    = WorldPos;
    Payload.WorldNormal = normalize(WorldNormal);
    Payload.BaseColor   = BaseColor;
}

// TODO(RTXPT-Port Phase 5.3): Honor RTXPTMaterialAttribs.AlphaMode/AlphaCutoff via any-hit specialization instead of forcing opaque rays.
// TODO(RTXPT-Port Phase 5.3): Sample base color / normal / metallic-roughness textures using TextureShaderAttribs UV selectors.
// TODO(RTXPT-Port Phase 5.5): Add NEE shadow rays toward analytic and environment lights.
