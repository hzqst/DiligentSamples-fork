#ifndef __EMISSIVE_TRIANGLE_BUILD_HLSL__
#define __EMISSIVE_TRIANGLE_BUILD_HLSL__

#include "PathTracerShared.h"

StructuredBuffer<MaterialPTData>     t_PTMaterialData;
StructuredBuffer<SubInstanceData>    t_SubInstanceData;
StructuredBuffer<float4x4>           t_SubInstanceTransforms;
StructuredBuffer<GeometryVertexData> t_VertexBuffer;
StructuredBuffer<GeometryVertexData> t_SkinnedVertexBuffer;
Buffer<uint>                         t_IndexBuffer;
RWStructuredBuffer<EmissiveTriangle> u_EmissiveTriangles;

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

float3 transformPosition(float4x4 transform, float3 position)
{
    return mul(float4(position, 1.0), transform).xyz;
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint subInstanceCount = 0;
    uint subInstanceStride = 0;
    t_SubInstanceData.GetDimensions(subInstanceCount, subInstanceStride);
    if (dispatchThreadId.x >= subInstanceCount)
        return;

    uint materialCount = 0;
    uint materialStride = 0;
    t_PTMaterialData.GetDimensions(materialCount, materialStride);
    if (materialCount == 0u)
        return;

    const uint           subInstanceIndex = dispatchThreadId.x;
    const SubInstanceData subInstance     = t_SubInstanceData[subInstanceIndex];
    const MaterialPTData  material        = t_PTMaterialData[min(subInstance.MaterialID, materialCount - 1u)];
    if ((material.flags & kMaterialFlagEmissiveAreaLight) == 0u)
        return;

    const uint triangleCount = ((subInstance.Flags & kSubInstanceFlagIndexed) != 0u ? subInstance.IndexCount : subInstance.VertexCount) / 3u;
    if (triangleCount == 0u)
        return;

    const float4x4 transform = t_SubInstanceTransforms[min(subInstanceIndex, subInstanceCount - 1u)];
    const float3   emission  = material.emissiveFactor;

    [loop]
    for (uint localPrimitiveIndex = 0u; localPrimitiveIndex < triangleCount; ++localPrimitiveIndex)
    {
        const uint3 indices = getTriangleIndices(subInstance, localPrimitiveIndex);
        const GeometryVertexData v0 = getGeometryVertex(subInstance, indices.x);
        const GeometryVertexData v1 = getGeometryVertex(subInstance, indices.y);
        const GeometryVertexData v2 = getGeometryVertex(subInstance, indices.z);

        const float3 base  = transformPosition(transform, v0.position);
        const float3 edge1 = transformPosition(transform, v1.position) - base;
        const float3 edge2 = transformPosition(transform, v2.position) - base;

        EmissiveTriangle tri;
        tri.base     = float4(base, 0.0);
        tri.edge1    = float4(edge1, 0.0);
        tri.edge2    = float4(edge2, 0.0);
        tri.radiance = float4(emission, 0.0);
        u_EmissiveTriangles[subInstance.emissiveTriangleOffset + localPrimitiveIndex] = tri;
    }
}

#endif // __EMISSIVE_TRIANGLE_BUILD_HLSL__
