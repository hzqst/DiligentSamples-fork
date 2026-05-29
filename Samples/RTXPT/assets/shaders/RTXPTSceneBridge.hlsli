#ifndef RTXPT_SCENE_BRIDGE_HLSLI
#define RTXPT_SCENE_BRIDGE_HLSLI

#include "RTXPTShaderShared.hlsli"

// Global shader resources used by the scene bridge. C++ binds these as static SRVs.
ConstantBuffer<RTXPTFrameConstants>    g_FrameConstants;
StructuredBuffer<RTXPTSubInstanceData> g_SubInstanceData;
StructuredBuffer<RTXPTLightData>       g_Lights;
StructuredBuffer<RTXPTVertex>          g_VertexBuffer;
Buffer<uint>                           g_IndexBuffer;

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

    // Fetch the 3 vertex indices for triangle `LocalPrimitiveIndex` within the geometry
    // described by `SubInstance`. Falls back to a fan (i,i,i+1,i+2 sequence) for non-indexed
    // primitives so the math stays valid; non-indexed geometries are flagged via Flags.
    uint3 GetTriangleIndices(RTXPTSubInstanceData SubInstance, uint LocalPrimitiveIndex)
    {
        const uint Base = LocalPrimitiveIndex * 3u;
        if ((SubInstance.Flags & kRTXPTSubInstanceFlagIndexed) != 0u)
        {
            return uint3(
                g_IndexBuffer[SubInstance.FirstIndex + Base + 0u],
                g_IndexBuffer[SubInstance.FirstIndex + Base + 1u],
                g_IndexBuffer[SubInstance.FirstIndex + Base + 2u]);
        }
        return uint3(Base + 0u, Base + 1u, Base + 2u);
    }

    // Fetch the 3 vertex records for the current closest-hit triangle.
    void GetTriangleVertices(RTXPTSubInstanceData SubInstance,
                             uint                 LocalPrimitiveIndex,
                             out RTXPTVertex      V0,
                             out RTXPTVertex      V1,
                             out RTXPTVertex      V2)
    {
        const uint3 Indices = GetTriangleIndices(SubInstance, LocalPrimitiveIndex);
        V0                  = g_VertexBuffer[SubInstance.FirstVertex + Indices.x];
        V1                  = g_VertexBuffer[SubInstance.FirstVertex + Indices.y];
        V2                  = g_VertexBuffer[SubInstance.FirstVertex + Indices.z];
    }

    // Barycentric-interpolated object-space normal -> world-space, renormalized.
    float3 InterpolateNormal(RTXPTVertex V0, RTXPTVertex V1, RTXPTVertex V2, float2 Barycentrics)
    {
        const float3 Bary        = float3(1.0 - Barycentrics.x - Barycentrics.y, Barycentrics.x, Barycentrics.y);
        const float3 ObjNormal   = V0.Normal * Bary.x + V1.Normal * Bary.y + V2.Normal * Bary.z;
        const float3 WorldNormal = mul((float3x3)ObjectToWorld3x4(), ObjNormal);
        const float  Len         = length(WorldNormal);
        return Len > 1e-6 ? WorldNormal / Len : float3(0.0, 1.0, 0.0);
    }

    // Geometric (face) normal in world space; used as a fallback when interpolated normals
    // collapse (e.g. degenerate triangles or missing data).
    float3 ComputeGeometricNormal(RTXPTVertex V0, RTXPTVertex V1, RTXPTVertex V2)
    {
        const float3 ObjFaceNormal   = cross(V1.Position - V0.Position, V2.Position - V0.Position);
        const float3 WorldFaceNormal = mul((float3x3)ObjectToWorld3x4(), ObjFaceNormal);
        const float  Len             = length(WorldFaceNormal);
        return Len > 1e-6 ? WorldFaceNormal / Len : float3(0.0, 1.0, 0.0);
    }

    // World-space hit position using ObjectToWorld3x4().
    float3 ComputeWorldHitPosition(RTXPTVertex V0, RTXPTVertex V1, RTXPTVertex V2, float2 Barycentrics)
    {
        const float3 Bary   = float3(1.0 - Barycentrics.x - Barycentrics.y, Barycentrics.x, Barycentrics.y);
        const float3 ObjPos = V0.Position * Bary.x + V1.Position * Bary.y + V2.Position * Bary.z;
        return mul(ObjectToWorld3x4(), float4(ObjPos, 1.0));
    }

    // Barycentric-interpolated TEXCOORD_0 for the current closest-hit / any-hit triangle.
    float2 InterpolateTexCoord(RTXPTVertex V0, RTXPTVertex V1, RTXPTVertex V2, float2 Barycentrics)
    {
        const float3 Bary = float3(1.0 - Barycentrics.x - Barycentrics.y, Barycentrics.x, Barycentrics.y);
        return V0.TexCoord0 * Bary.x + V1.TexCoord0 * Bary.y + V2.TexCoord0 * Bary.z;
    }

    // World-space tangent derived from triangle edges + UV deltas (vertex buffer 0 carries no tangent attribute).
    // Returned tangent is orthonormalized against WorldNormal; .w is the bitangent handedness for cross(N,T) * w.
    // Degenerate UVs fall back to an arbitrary perpendicular so the TBN frame is always valid.
    float4 ComputeWorldTangent(RTXPTVertex V0, RTXPTVertex V1, RTXPTVertex V2, float3 WorldNormal)
    {
        const float3 E1  = V1.Position - V0.Position;
        const float3 E2  = V2.Position - V0.Position;
        const float2 dU1 = V1.TexCoord0 - V0.TexCoord0;
        const float2 dU2 = V2.TexCoord0 - V0.TexCoord0;
        const float  Det = dU1.x * dU2.y - dU2.x * dU1.y;

        // Fallback perpendicular for degenerate UVs (det ~ 0): cross with the least-aligned axis.
        const float3 Axis     = abs(WorldNormal.x) > 0.9 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        float3       Fallback = normalize(cross(Axis, WorldNormal));

        if (abs(Det) < 1e-12)
            return float4(Fallback, 1.0);

        const float  InvDet       = 1.0 / Det;
        const float3 ObjTangent   = (E1 * dU2.y - E2 * dU1.y) * InvDet;
        const float3 ObjBitangent = (E2 * dU1.x - E1 * dU2.x) * InvDet;
        float3       WorldTangent   = mul((float3x3)ObjectToWorld3x4(), ObjTangent);
        float3       WorldBitangent = mul((float3x3)ObjectToWorld3x4(), ObjBitangent);

        // Gram-Schmidt against the (already world-space) shading normal.
        WorldTangent = WorldTangent - WorldNormal * dot(WorldNormal, WorldTangent);
        const float Len = length(WorldTangent);
        if (Len <= 1e-8)
            return float4(Fallback, 1.0);

        WorldTangent = WorldTangent / Len;
        const float Handedness = dot(cross(WorldNormal, WorldTangent), WorldBitangent) < 0.0 ? -1.0 : 1.0;
        return float4(WorldTangent, Handedness);
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

#endif // RTXPT_SCENE_BRIDGE_HLSLI
