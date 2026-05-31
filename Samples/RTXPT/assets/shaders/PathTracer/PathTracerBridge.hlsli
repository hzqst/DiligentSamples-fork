#ifndef __PATH_TRACER_BRIDGE_HLSLI__
#define __PATH_TRACER_BRIDGE_HLSLI__

#include "PathTracerShared.h"
#include "Lighting/LightingTypes.hlsli"

// Global shader resources used by the scene bridge. C++ binds these as static SRVs/UAVs.
ConstantBuffer<SampleConstants>        g_Const;
StructuredBuffer<SubInstanceData>      t_SubInstanceData;
StructuredBuffer<PolymorphicLightInfo> t_Lights;
StructuredBuffer<LightingControlData>  t_LightingControl;
Buffer<uint>                           t_LightProxyCounters;
Buffer<uint>                           t_LightSamplingProxies;
Buffer<uint>                           t_LocalSamplingBuffer;
RWTexture2D<float>                     u_FeedbackTotalWeight;
RWTexture2D<uint>                      u_FeedbackCandidates;
StructuredBuffer<EmissiveTriangle>     t_EmissiveTriangles;
StructuredBuffer<GeometryVertexData>   t_VertexBuffer;
StructuredBuffer<GeometryVertexData>   t_SkinnedVertexBuffer;
Buffer<uint>                           t_IndexBuffer;

namespace Bridge
{
#ifdef ENABLE_HIT_BRIDGE
    // Linear index for the SubInstanceData entry that describes the currently hit (instance, geometry).
    // C++ stores the per-instance sub-instance base in InstanceID(), and GeometryIndex() is used to
    // select the geometry within the BLAS.
    uint getSubInstanceIndex()
    {
        return InstanceID() + GeometryIndex();
    }

    // Returns the SubInstanceData entry for the current hit.
    // The caller is responsible for guarding against an empty/unbound table via hasSubInstanceTable().
    SubInstanceData getSubInstanceData()
    {
        return t_SubInstanceData[getSubInstanceIndex()];
    }

    // True when t_SubInstanceData has at least one entry. The C++ side guarantees a dummy entry
    // is bound when the scene has no real geometry so that this helper still returns a defined value.
    bool hasSubInstanceTable()
    {
        uint count  = 0;
        uint stride = 0;
        t_SubInstanceData.GetDimensions(count, stride);
        return count > 0;
    }

    // Fetch the 3 vertex indices for triangle `localPrimitiveIndex` within the geometry
    // described by `subInstance`. Falls back to a fan (i,i,i+1,i+2 sequence) for non-indexed
    // primitives so the math stays valid; non-indexed geometries are flagged via Flags.
    uint3 getTriangleIndices(SubInstanceData subInstance, uint localPrimitiveIndex)
    {
        const uint baseIndex = localPrimitiveIndex * 3u;
        if ((subInstance.Flags & kSubInstanceFlagIndexed) != 0u)
        {
            return uint3(
                t_IndexBuffer[subInstance.IndexOffset + baseIndex + 0u],
                t_IndexBuffer[subInstance.IndexOffset + baseIndex + 1u],
                t_IndexBuffer[subInstance.IndexOffset + baseIndex + 2u]);
        }
        return uint3(baseIndex + 0u, baseIndex + 1u, baseIndex + 2u);
    }

    GeometryVertexData getGeometryVertex(SubInstanceData subInstance, uint vertexIndex)
    {
        if ((subInstance.Flags & kSubInstanceFlagSkinned) != 0u)
            return t_SkinnedVertexBuffer[subInstance.VertexOffset + vertexIndex];

        return t_VertexBuffer[subInstance.VertexOffset + vertexIndex];
    }

    // Fetch the 3 vertex records for the current closest-hit triangle.
    void getTriangleVertices(SubInstanceData subInstance,
                             uint            localPrimitiveIndex,
                             out GeometryVertexData v0,
                             out GeometryVertexData v1,
                             out GeometryVertexData v2)
    {
        const uint3 indices = getTriangleIndices(subInstance, localPrimitiveIndex);
        v0                  = getGeometryVertex(subInstance, indices.x);
        v1                  = getGeometryVertex(subInstance, indices.y);
        v2                  = getGeometryVertex(subInstance, indices.z);
    }

    // Barycentric-interpolated object-space normal -> world-space, renormalized.
    float3 interpolateNormal(GeometryVertexData v0, GeometryVertexData v1, GeometryVertexData v2, float2 barycentrics)
    {
        const float3 bary        = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
        const float3 objNormal   = v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z;
        const float3 worldNormal = mul((float3x3)ObjectToWorld3x4(), objNormal);
        const float  len         = length(worldNormal);
        return len > 1e-6 ? worldNormal / len : float3(0.0, 1.0, 0.0);
    }

    // Geometric (face) normal in world space; used as a fallback when interpolated normals
    // collapse (e.g. degenerate triangles or missing data).
    float3 computeGeometricNormal(GeometryVertexData v0, GeometryVertexData v1, GeometryVertexData v2)
    {
        const float3 objFaceNormal   = cross(v1.position - v0.position, v2.position - v0.position);
        const float3 worldFaceNormal = mul((float3x3)ObjectToWorld3x4(), objFaceNormal);
        const float  len             = length(worldFaceNormal);
        return len > 1e-6 ? worldFaceNormal / len : float3(0.0, 1.0, 0.0);
    }

    // World-space hit position using ObjectToWorld3x4().
    float3 computeWorldHitPosition(GeometryVertexData v0, GeometryVertexData v1, GeometryVertexData v2, float2 barycentrics)
    {
        const float3 bary   = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
        const float3 objPos = v0.position * bary.x + v1.position * bary.y + v2.position * bary.z;
        return mul(ObjectToWorld3x4(), float4(objPos, 1.0));
    }

    // Barycentric-interpolated TEXCOORD_0 for the current closest-hit / any-hit triangle.
    float2 interpolateTexCoord(GeometryVertexData v0, GeometryVertexData v1, GeometryVertexData v2, float2 barycentrics)
    {
        const float3 bary = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
        return v0.texCoord0 * bary.x + v1.texCoord0 * bary.y + v2.texCoord0 * bary.z;
    }

    // World-space tangent derived from triangle edges + UV deltas (vertex buffer 0 carries no tangent attribute).
    // Returned tangent is orthonormalized against worldNormal; .w is the bitangent handedness for cross(N,T) * w.
    // Degenerate UVs fall back to an arbitrary perpendicular so the TBN frame is always valid.
    float4 computeWorldTangent(GeometryVertexData v0, GeometryVertexData v1, GeometryVertexData v2, float3 worldNormal)
    {
        const float3 e1  = v1.position - v0.position;
        const float3 e2  = v2.position - v0.position;
        const float2 dU1 = v1.texCoord0 - v0.texCoord0;
        const float2 dU2 = v2.texCoord0 - v0.texCoord0;
        const float  det = dU1.x * dU2.y - dU2.x * dU1.y;

        // Fallback perpendicular for degenerate UVs (det ~ 0): cross with the least-aligned axis.
        const float3 axis     = abs(worldNormal.x) > 0.9 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        float3       fallback = normalize(cross(axis, worldNormal));

        if (abs(det) < 1e-12)
            return float4(fallback, 1.0);

        const float  invDet       = 1.0 / det;
        const float3 objTangent   = (e1 * dU2.y - e2 * dU1.y) * invDet;
        const float3 objBitangent = (e2 * dU1.x - e1 * dU2.x) * invDet;
        float3       worldTangent   = mul((float3x3)ObjectToWorld3x4(), objTangent);
        float3       worldBitangent = mul((float3x3)ObjectToWorld3x4(), objBitangent);

        // Gram-Schmidt against the (already world-space) shading normal.
        worldTangent = worldTangent - worldNormal * dot(worldNormal, worldTangent);
        const float len = length(worldTangent);
        if (len <= 1e-8)
            return float4(fallback, 1.0);

        worldTangent = worldTangent / len;
        const float handedness = dot(cross(worldNormal, worldTangent), worldBitangent) < 0.0 ? -1.0 : 1.0;
        return float4(worldTangent, handedness);
    }
#endif

    LightingControlData getLightingControl()
    {
        return t_LightingControl[0];
    }

    uint getTotalLightCount()
    {
        return t_LightingControl[0].TotalLightCount;
    }

    uint getAnalyticLightCount()
    {
        return t_LightingControl[0].AnalyticLightCount;
    }

    uint getEmissiveBucketLightIndex()
    {
        return t_LightingControl[0].AnalyticLightCount;
    }

    // Total valid analytic light count. The C++ side may upload one disabled dummy light for binding safety;
    // that dummy is intentionally excluded from sampling.
    uint getLightCount()
    {
        return t_LightingControl[0].AnalyticLightCount;
    }

    PolymorphicLightInfo getLight(uint index)
    {
        return t_Lights[index];
    }

    uint getEmissiveTriangleCount()
    {
        return t_LightingControl[0].TriangleLightCount;
    }

    EmissiveTriangle getEmissiveTriangle(uint index)
    {
        return t_EmissiveTriangles[index];
    }
} // namespace Bridge

#endif // __PATH_TRACER_BRIDGE_HLSLI__
