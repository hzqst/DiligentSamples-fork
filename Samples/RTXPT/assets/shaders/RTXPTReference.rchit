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
    float3 BaseColor   = float3(Attributes.barycentrics.x,
                                Attributes.barycentrics.y,
                                1.0 - Attributes.barycentrics.x - Attributes.barycentrics.y);
    float3 WorldNormal = -WorldRayDirection();
    float3 WorldPos    = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float  Metallic    = 0.0;
    float  Roughness   = 1.0;

    if (Bridge::HasSubInstanceTable() && Bridge::HasMaterialTable())
    {
        const RTXPTSubInstanceData SubInstance = Bridge::GetSubInstanceData();
        const RTXPTMaterialData    Material    = Bridge::GetMaterial(SubInstance.MaterialID);

        RTXPTVertex V0;
        RTXPTVertex V1;
        RTXPTVertex V2;
        Bridge::GetTriangleVertices(SubInstance, PrimitiveIndex(), V0, V1, V2);

        const float2 TexCoord = Bridge::InterpolateTexCoord(V0, V1, V2, Attributes.barycentrics);

        const float3 RayDir = WorldRayDirection();
        const float3 GeometricNormal = Bridge::ComputeGeometricNormal(V0, V1, V2);
        WorldPos    = Bridge::ComputeWorldHitPosition(V0, V1, V2, Attributes.barycentrics);
        WorldNormal = Bridge::InterpolateNormal(V0, V1, V2, Attributes.barycentrics);
        // Renormalize against the geometric normal if the interpolated normal is nearly zero
        // (degenerate vertex data) - keeps the shader robust on bad assets.
        if (dot(WorldNormal, WorldNormal) < 1e-6)
            WorldNormal = GeometricNormal;
        // Flip the shading normal to face the camera (single-sided shading; transmission is deferred).
        if (dot(WorldNormal, RayDir) > 0.0)
            WorldNormal = -WorldNormal;

        // Perturb the shading normal with the tangent-space normal map (tangent derived from UV gradients).
        const float3 TangentNormal = Bridge::GetTangentNormal(Material, TexCoord);
        if (abs(TangentNormal.x) + abs(TangentNormal.y) > 1e-5)
        {
            const float4 WorldTangent = Bridge::ComputeWorldTangent(V0, V1, V2, WorldNormal);
            const float3 T            = WorldTangent.xyz;
            const float3 B            = cross(WorldNormal, T) * WorldTangent.w;
            const float3 MappedNormal = T * TangentNormal.x + B * TangentNormal.y + WorldNormal * TangentNormal.z;
            const float  LenSq        = dot(MappedNormal, MappedNormal);
            if (LenSq > 1e-8)
            {
                WorldNormal = MappedNormal * rsqrt(LenSq);
                if (dot(WorldNormal, RayDir) > 0.0)
                    WorldNormal = -WorldNormal;
            }
        }

        const float2 MetalRough = Bridge::GetMetallicRoughness(Material, TexCoord);
        Metallic                = MetalRough.x;
        Roughness               = MetalRough.y;

        BaseColor        = Bridge::GetBaseColor(Material, TexCoord).rgb;
        Payload.Emission = Bridge::GetEmission(Material, TexCoord);
    }

    Payload.WorldPos    = WorldPos;
    Payload.WorldNormal = normalize(WorldNormal);
    Payload.BaseColor   = BaseColor;
    Payload.Metallic    = Metallic;
    Payload.Roughness   = Roughness;
}

// TODO(RTXPT-Port Phase 5.4): Emissive surfaces are gathered by BSDF sampling only; add emissive-triangle area-light NEE + MIS once an emissive light list exists.
