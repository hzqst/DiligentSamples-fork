#ifndef RTXPT_MINIMAL_TRACE_RAY_DIAGNOSTIC
#    define RTXPT_MINIMAL_TRACE_RAY_DIAGNOSTIC 0
#endif

#if RTXPT_MINIMAL_TRACE_RAY_DIAGNOSTIC

struct DiagnosticPayload
{
    float3 color;
    uint   hit;
};

uint RTXPTDiagnosticHash(uint Value)
{
    Value ^= Value >> 16;
    Value *= 0x7feb352du;
    Value ^= Value >> 15;
    Value *= 0x846ca68bu;
    Value ^= Value >> 16;
    return Value;
}

float3 RTXPTDiagnosticColor(uint Value)
{
    const uint Hash = RTXPTDiagnosticHash(Value);
    return float3(float((Hash >> 0) & 0xffu),
                  float((Hash >> 8) & 0xffu),
                  float((Hash >> 16) & 0xffu)) /
        255.0;
}

[shader("closesthit")]
void main(inout DiagnosticPayload Payload,
          in BuiltInTriangleIntersectionAttributes Attributes)
{
    const float3 barycentrics = float3(Attributes.barycentrics.x,
                                       Attributes.barycentrics.y,
                                       1.0 - Attributes.barycentrics.x - Attributes.barycentrics.y);
    const float  depthShade   = saturate(1.0 / (1.0 + RayTCurrent() * 0.01));
    const uint   hitKey       = ((InstanceIndex() + 1u) * 0x9e3779b9u) ^
        ((GeometryIndex() + 1u) * 0x85ebca6bu) ^
        ((InstanceID() + 1u) * 0xc2b2ae35u);
    const float3 objectColor = RTXPTDiagnosticColor(hitKey);

    Payload.hit   = 1u;
    Payload.color = lerp(objectColor, barycentrics, depthShade * 0.45);
}

#else

#define ENABLE_HIT_BRIDGE 1
#include "PathTracerBridge.hlsli"
#include "Rendering/Materials/MaterialBridge.hlsli"

[shader("closesthit")]
void main(inout PathPayload Payload,
          in BuiltInTriangleIntersectionAttributes Attributes)
{
    Payload.hitFlag     = 1u;
    Payload.hitDistance = RayTCurrent();
    Payload.emission    = float3(0.0, 0.0, 0.0);

    // Default to a barycentric debug color so we still see something if the bridge tables are unbound.
    float3 BaseColor   = float3(Attributes.barycentrics.x,
                                Attributes.barycentrics.y,
                                1.0 - Attributes.barycentrics.x - Attributes.barycentrics.y);
    float3 WorldNormal = -WorldRayDirection();
    float3 WorldPos    = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float  Metallic    = 0.0;
    float  Roughness   = 1.0;

    if (Bridge::hasSubInstanceTable() && Bridge::hasMaterialTable())
    {
        const SubInstanceData subInstance = Bridge::getSubInstanceData();
        const MaterialPTData  material    = Bridge::getMaterial(subInstance.MaterialID);

        GeometryVertexData V0;
        GeometryVertexData V1;
        GeometryVertexData V2;
        Bridge::getTriangleVertices(subInstance, PrimitiveIndex(), V0, V1, V2);

        const float2 texCoord = Bridge::interpolateTexCoord(V0, V1, V2, Attributes.barycentrics);

        const float3 RayDir = WorldRayDirection();
        const float3 geometricNormal = Bridge::computeGeometricNormal(V0, V1, V2);
        WorldPos    = Bridge::computeWorldHitPosition(V0, V1, V2, Attributes.barycentrics);
        WorldNormal = Bridge::interpolateNormal(V0, V1, V2, Attributes.barycentrics);
        // Renormalize against the geometric normal if the interpolated normal is nearly zero
        // (degenerate vertex data) - keeps the shader robust on bad assets.
        if (dot(WorldNormal, WorldNormal) < 1e-6)
            WorldNormal = geometricNormal;
        // Flip the shading normal to face the camera (single-sided shading; transmission is deferred).
        if (dot(WorldNormal, RayDir) > 0.0)
            WorldNormal = -WorldNormal;

        // Perturb the shading normal with the tangent-space normal map (tangent derived from UV gradients).
        const float3 tangentNormal = Bridge::getTangentNormal(material, texCoord);
        if (abs(tangentNormal.x) + abs(tangentNormal.y) > 1e-5)
        {
            const float4 worldTangent = Bridge::computeWorldTangent(V0, V1, V2, WorldNormal);
            const float3 T            = worldTangent.xyz;
            const float3 B            = cross(WorldNormal, T) * worldTangent.w;
            const float3 mappedNormal = T * tangentNormal.x + B * tangentNormal.y + WorldNormal * tangentNormal.z;
            const float  lenSq        = dot(mappedNormal, mappedNormal);
            if (lenSq > 1e-8)
            {
                WorldNormal = mappedNormal * rsqrt(lenSq);
                if (dot(WorldNormal, RayDir) > 0.0)
                    WorldNormal = -WorldNormal;
            }
        }

        const float2 metalRough = Bridge::getMetallicRoughness(material, texCoord);
        Metallic                = metalRough.x;
        Roughness               = metalRough.y;

        BaseColor        = Bridge::getBaseColor(material, texCoord).rgb;
        Payload.emission = Bridge::getEmission(material, texCoord);
    }

    Payload.worldPos    = WorldPos;
    Payload.worldNormal = normalize(WorldNormal);
    Payload.baseColor   = BaseColor;
    Payload.metallic    = Metallic;
    Payload.roughness   = Roughness;
}

// TODO(RTXPT-Port Phase 5.4): Emissive surfaces are gathered by BSDF sampling only; add emissive-triangle area-light NEE + MIS once an emissive light list exists.

#endif
