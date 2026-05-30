/*
 *  Copyright 2026 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include "RTXPTAccelerationStructures.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

#include "GraphicsAccessories.hpp"
#include "RTXPTMaterials.hpp"

namespace Diligent
{

namespace
{

constexpr Uint32 MaxTLASCustomID         = (1u << 24) - 1u;
constexpr Uint32 MaxSubInstanceDataCount = MaxTLASCustomID + 1u;

struct PositionLayout
{
    bool       Valid          = false;
    Uint8      BufferId       = 0;
    VALUE_TYPE ValueType      = VT_UNDEFINED;
    Uint8      ComponentCount = 0;
    Uint32     RelativeOffset = 0;
};

PositionLayout FindPositionLayout(const GLTF::Model& Model)
{
    for (Uint32 Attr = 0; Attr < Model.GetNumVertexAttributes(); ++Attr)
    {
        const GLTF::VertexAttributeDesc& Desc = Model.GetVertexAttribute(Attr);
        if (Desc.Name != nullptr && std::strcmp(Desc.Name, GLTF::PositionAttributeName) == 0)
        {
            PositionLayout Layout;
            Layout.Valid          = true;
            Layout.BufferId       = Desc.BufferId;
            Layout.ValueType      = Desc.ValueType;
            Layout.ComponentCount = Desc.NumComponents;
            Layout.RelativeOffset = Desc.RelativeOffset == ~0U ? 0 : Desc.RelativeOffset;
            return Layout;
        }
    }
    return {};
}

Uint32 GetVertexStride(const GLTF::Model& Model, Uint8 BufferId)
{
    Uint32 Stride = 0;
    for (Uint32 Attr = 0; Attr < Model.GetNumVertexAttributes(); ++Attr)
    {
        const GLTF::VertexAttributeDesc& Desc = Model.GetVertexAttribute(Attr);
        if (Desc.BufferId == BufferId)
            Stride = std::max(Stride, Desc.RelativeOffset + GetValueSize(Desc.ValueType) * Desc.NumComponents);
    }
    return Stride;
}

bool HasBindFlag(const IBuffer* pBuffer, BIND_FLAGS Flag)
{
    return pBuffer != nullptr && (pBuffer->GetDesc().BindFlags & Flag) != 0;
}

InstanceMatrix ToInstanceMatrix(const float4x4& Transform)
{
    InstanceMatrix Matrix;
    Matrix.SetRotation(&Transform._11, 4);
    Matrix.SetTranslation(Transform._41, Transform._42, Transform._43);
    return Matrix;
}

} // namespace

void RTXPTAccelerationStructures::Reset()
{
    m_BLASRecords.clear();
    m_TLAS.Release();
    m_BLASScratch.Release();
    m_TLASScratch.Release();
    m_InstanceBuffer.Release();
    m_SubInstanceBuffer.Release();
    m_Stats = {};
}

bool RTXPTAccelerationStructures::BuildStaticScene(IRenderDevice*               pDevice,
                                                   IDeviceContext*              pContext,
                                                   const GLTF::Model&           Model,
                                                   Uint32                       SceneIndex,
                                                   VALUE_TYPE                   IndexType,
                                                   const GLTF::ModelTransforms& Transforms,
                                                   bool                         RayTracingSupported)
{
    Reset();
    m_Stats.RayTracingSupported = RayTracingSupported;

    if (!RayTracingSupported)
    {
        m_Stats.DisabledReason = "Ray tracing is not supported by this device";
        return false;
    }

    if (SceneIndex >= Model.Scenes.size())
    {
        m_Stats.LastError = "Invalid RTXPT scene index for acceleration structure build";
        return false;
    }

    const PositionLayout Position = FindPositionLayout(Model);
    if (!Position.Valid || Position.ValueType != VT_FLOAT32 || Position.ComponentCount != 3)
    {
        m_Stats.LastError = "RTXPT BLAS build requires float3 POSITION vertex data";
        return false;
    }

    IBuffer* pVertexBuffer = Model.GetVertexBuffer(Position.BufferId, pDevice, pContext);
    if (!HasBindFlag(pVertexBuffer, BIND_RAY_TRACING))
    {
        m_Stats.LastError = "RTXPT POSITION vertex buffer is missing BIND_RAY_TRACING";
        return false;
    }

    IBuffer* pIndexBuffer = Model.GetIndexBuffer(pDevice, pContext);
    if (pIndexBuffer != nullptr && !HasBindFlag(pIndexBuffer, BIND_RAY_TRACING))
    {
        m_Stats.LastError = "RTXPT index buffer is missing BIND_RAY_TRACING";
        return false;
    }

    const GLTF::Scene& Scene        = Model.Scenes[SceneIndex];
    const Uint32       VertexStride = GetVertexStride(Model, Position.BufferId);
    const Uint32       BaseVertex   = Model.GetBaseVertex();
    const Uint32       FirstIndex   = pIndexBuffer != nullptr ? Model.GetFirstIndexLocation() : 0;

    if (VertexStride == 0)
    {
        m_Stats.LastError = "RTXPT POSITION vertex buffer has an invalid stride";
        return false;
    }

    const Uint64 ModelVertexOffset = Uint64{BaseVertex} * Uint64{VertexStride} + Uint64{Position.RelativeOffset};
    if (ModelVertexOffset >= pVertexBuffer->GetDesc().Size)
    {
        m_Stats.LastError = "RTXPT POSITION vertex buffer is too small for the model base vertex";
        return false;
    }
    const Uint64 ModelVertexCount64 = (pVertexBuffer->GetDesc().Size - ModelVertexOffset) / VertexStride;
    if (ModelVertexCount64 == 0 || ModelVertexCount64 > Uint64{std::numeric_limits<Uint32>::max()})
    {
        m_Stats.LastError = "RTXPT POSITION vertex buffer has an invalid model vertex count";
        return false;
    }
    const Uint32 ModelVertexCount = static_cast<Uint32>(ModelVertexCount64);

    if (pIndexBuffer != nullptr && IndexType != VT_UINT16 && IndexType != VT_UINT32)
    {
        m_Stats.LastError = "RTXPT index buffer has an unsupported index size";
        return false;
    }
    const Uint32 IndexSize = GetValueSize(IndexType);

    std::vector<TLASBuildInstanceData> Instances;
    std::vector<std::string>           InstanceNames;
    std::vector<SubInstanceData>       SubInstances;
    Instances.reserve(Scene.LinearNodes.size());
    InstanceNames.reserve(Scene.LinearNodes.size());
    SubInstances.reserve(Scene.LinearNodes.size());

    Uint32 SubInstanceBase = 0;
    for (const GLTF::Node* pNode : Scene.LinearNodes)
    {
        if (pNode == nullptr || pNode->pMesh == nullptr)
            continue;

        if (pNode->Index < 0 || static_cast<size_t>(pNode->Index) >= Transforms.NodeGlobalMatrices.size())
            continue;

        std::vector<std::string>           GeometryNames;
        std::vector<BLASTriangleDesc>      TriangleDescs;
        std::vector<BLASBuildTriangleData> TriangleData;

        GeometryNames.reserve(pNode->pMesh->Primitives.size());
        TriangleDescs.reserve(pNode->pMesh->Primitives.size());
        TriangleData.reserve(pNode->pMesh->Primitives.size());

        Uint32 PrimitiveIndex = 0;
        for (const GLTF::Primitive& Primitive : pNode->pMesh->Primitives)
        {
            if (Primitive.VertexCount == 0 && Primitive.IndexCount == 0)
                continue;

            GeometryNames.emplace_back((pNode->Name.empty() ? "RTXPTGeometry" : pNode->Name) + "_" + std::to_string(PrimitiveIndex));

            SubInstanceData SubEntry;
            SubEntry.MaterialID     = Primitive.MaterialId;
            SubEntry.VertexOffset   = BaseVertex + Primitive.FirstVertex;
            SubEntry.VertexCount    = Primitive.VertexCount;
            if (Primitive.HasIndices())
            {
                SubEntry.Flags |= kSubInstanceFlag_Indexed;
                SubEntry.IndexOffset = FirstIndex + Primitive.FirstIndex;
                SubEntry.IndexCount  = Primitive.IndexCount;
            }
            SubInstances.emplace_back(SubEntry);

            // The GLTF builder bakes VertexStart into indexed primitives and resets FirstVertex to 0.
            // BLAS indexed geometry therefore needs the whole model vertex range, not Primitive.VertexCount.
            const bool   IsIndexed          = Primitive.HasIndices();
            const Uint64 PrimitiveVertexOff = IsIndexed ?
                ModelVertexOffset :
                (Uint64{BaseVertex + Primitive.FirstVertex} * Uint64{VertexStride} + Uint64{Position.RelativeOffset});
            const Uint32 PrimitiveVertexCnt = IsIndexed ? ModelVertexCount : Primitive.VertexCount;

            BLASTriangleDesc TriangleDesc;
            TriangleDesc.GeometryName         = GeometryNames.back().c_str();
            TriangleDesc.MaxVertexCount       = PrimitiveVertexCnt;
            TriangleDesc.VertexValueType      = Position.ValueType;
            TriangleDesc.VertexComponentCount = Position.ComponentCount;
            TriangleDesc.MaxPrimitiveCount    = IsIndexed ? Primitive.IndexCount / 3 : Primitive.VertexCount / 3;
            TriangleDesc.IndexType            = IsIndexed ? IndexType : VT_UNDEFINED;

            BLASBuildTriangleData BuildData;
            BuildData.GeometryName         = TriangleDesc.GeometryName;
            BuildData.pVertexBuffer        = pVertexBuffer;
            BuildData.VertexOffset         = PrimitiveVertexOff;
            BuildData.VertexStride         = VertexStride;
            BuildData.VertexCount          = PrimitiveVertexCnt;
            BuildData.VertexValueType      = Position.ValueType;
            BuildData.VertexComponentCount = Position.ComponentCount;
            BuildData.PrimitiveCount       = TriangleDesc.MaxPrimitiveCount;
            // Alpha-masked geometry must be non-opaque so the runtime invokes the alpha-test any-hit shader.
            // Everything else stays opaque to skip any-hit entirely.
            const bool GeometryAlphaTested =
                Primitive.MaterialId < Model.Materials.size() &&
                RTXPTMaterialIsAlphaTested(Model.Materials[Primitive.MaterialId]);
            BuildData.Flags = GeometryAlphaTested ? RAYTRACING_GEOMETRY_FLAG_NONE : RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            if (GeometryAlphaTested)
                ++m_Stats.AlphaTestedGeometryCount;

            if (IsIndexed)
            {
                BuildData.pIndexBuffer = pIndexBuffer;
                BuildData.IndexOffset  = (FirstIndex + Primitive.FirstIndex) * IndexSize;
                BuildData.IndexType    = IndexType;
            }

            TriangleDescs.emplace_back(TriangleDesc);
            TriangleData.emplace_back(BuildData);
            ++PrimitiveIndex;
        }

        if (TriangleDescs.empty())
            continue;

        BLASRecord        Record;
        const std::string NodeName = pNode->Name.empty() ? "node" : pNode->Name;
        Record.Name                = "RTXPT BLAS " + NodeName + " node_" + std::to_string(pNode->Index);
        Record.GeometryCount       = static_cast<Uint32>(TriangleDescs.size());

        if (Uint64{SubInstanceBase} + Uint64{Record.GeometryCount} > Uint64{MaxSubInstanceDataCount})
        {
            m_Stats.LastError = "RTXPT sub-instance table exceeds the 24-bit TLAS CustomId range";
            return false;
        }

        BottomLevelASDesc BLASDesc;
        BLASDesc.Name          = Record.Name.c_str();
        BLASDesc.Flags         = RAYTRACING_BUILD_AS_NONE;
        BLASDesc.pTriangles    = TriangleDescs.data();
        BLASDesc.TriangleCount = static_cast<Uint32>(TriangleDescs.size());
        pDevice->CreateBLAS(BLASDesc, &Record.BLAS);

        if (!Record.BLAS)
        {
            m_Stats.LastError = "Failed to create RTXPT BLAS";
            return false;
        }

        m_Stats.BLASScratchSize = std::max(m_Stats.BLASScratchSize, Record.BLAS->GetScratchBufferSizes().Build);

        if (!m_BLASScratch || m_BLASScratch->GetDesc().Size < Record.BLAS->GetScratchBufferSizes().Build)
        {
            BufferDesc ScratchDesc;
            ScratchDesc.Name      = "RTXPT BLAS scratch buffer";
            ScratchDesc.Usage     = USAGE_DEFAULT;
            ScratchDesc.BindFlags = BIND_RAY_TRACING;
            ScratchDesc.Size      = Record.BLAS->GetScratchBufferSizes().Build;
            m_BLASScratch.Release();
            pDevice->CreateBuffer(ScratchDesc, nullptr, &m_BLASScratch);
        }

        BuildBLASAttribs BLASAttribs;
        BLASAttribs.pBLAS                       = Record.BLAS;
        BLASAttribs.BLASTransitionMode          = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        BLASAttribs.GeometryTransitionMode      = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        BLASAttribs.pTriangleData               = TriangleData.data();
        BLASAttribs.TriangleDataCount           = static_cast<Uint32>(TriangleData.size());
        BLASAttribs.pScratchBuffer              = m_BLASScratch;
        BLASAttribs.ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        pContext->BuildBLAS(BLASAttribs);

        InstanceNames.emplace_back(Record.Name);

        TLASBuildInstanceData Instance;
        Instance.InstanceName = InstanceNames.back().c_str();
        Instance.pBLAS        = Record.BLAS;
        Instance.Transform    = ToInstanceMatrix(Transforms.NodeGlobalMatrices[pNode->Index]);
        Instance.CustomId     = SubInstanceBase;
        Instance.Flags        = RAYTRACING_INSTANCE_NONE;
        Instance.Mask         = 0xFF;
        Instances.emplace_back(Instance);

        SubInstanceBase += Record.GeometryCount;
        m_Stats.GeometryCount += Record.GeometryCount;
        m_BLASRecords.emplace_back(std::move(Record));
    }

    if (Instances.empty())
    {
        m_Stats.LastError = "No RTXPT mesh instances were available for TLAS build";
        return false;
    }

    TopLevelASDesc TLASDesc;
    TLASDesc.Name             = "RTXPT TLAS";
    TLASDesc.MaxInstanceCount = static_cast<Uint32>(Instances.size());
    TLASDesc.Flags            = RAYTRACING_BUILD_AS_NONE;
    pDevice->CreateTLAS(TLASDesc, &m_TLAS);

    if (!m_TLAS)
    {
        m_Stats.LastError = "Failed to create RTXPT TLAS";
        return false;
    }

    m_Stats.TLASScratchSize = m_TLAS->GetScratchBufferSizes().Build;

    BufferDesc InstanceBufferDesc;
    InstanceBufferDesc.Name      = "RTXPT TLAS instance buffer";
    InstanceBufferDesc.Usage     = USAGE_DEFAULT;
    InstanceBufferDesc.BindFlags = BIND_RAY_TRACING;
    InstanceBufferDesc.Size      = Uint64{TLAS_INSTANCE_DATA_SIZE} * Uint64{Instances.size()};
    pDevice->CreateBuffer(InstanceBufferDesc, nullptr, &m_InstanceBuffer);

    BufferDesc TLASScratchDesc;
    TLASScratchDesc.Name      = "RTXPT TLAS scratch buffer";
    TLASScratchDesc.Usage     = USAGE_DEFAULT;
    TLASScratchDesc.BindFlags = BIND_RAY_TRACING;
    TLASScratchDesc.Size      = m_TLAS->GetScratchBufferSizes().Build;
    pDevice->CreateBuffer(TLASScratchDesc, nullptr, &m_TLASScratch);

    BuildTLASAttribs TLASAttribs;
    TLASAttribs.pTLAS                        = m_TLAS;
    TLASAttribs.TLASTransitionMode           = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    TLASAttribs.BLASTransitionMode           = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    TLASAttribs.pInstances                   = Instances.data();
    TLASAttribs.InstanceCount                = static_cast<Uint32>(Instances.size());
    TLASAttribs.pInstanceBuffer              = m_InstanceBuffer;
    TLASAttribs.InstanceBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    TLASAttribs.HitGroupStride               = 1;
    TLASAttribs.BindingMode                  = HIT_GROUP_BINDING_MODE_PER_GEOMETRY;
    TLASAttribs.pScratchBuffer               = m_TLASScratch;
    TLASAttribs.ScratchBufferTransitionMode  = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    pContext->BuildTLAS(TLASAttribs);

    if (SubInstances.empty())
    {
        // Always upload at least one dummy entry so the bridge buffer can be bound unconditionally.
        SubInstances.push_back(SubInstanceData{});
    }

    BufferDesc SubInstanceDesc;
    SubInstanceDesc.Name              = "RTXPT sub-instance buffer";
    SubInstanceDesc.Usage             = USAGE_IMMUTABLE;
    SubInstanceDesc.BindFlags         = BIND_SHADER_RESOURCE;
    SubInstanceDesc.Mode              = BUFFER_MODE_STRUCTURED;
    SubInstanceDesc.ElementByteStride = sizeof(SubInstanceData);
    SubInstanceDesc.Size              = sizeof(SubInstanceData) * SubInstances.size();

    BufferData SubInstanceData{SubInstances.data(), SubInstanceDesc.Size};
    pDevice->CreateBuffer(SubInstanceDesc, &SubInstanceData, &m_SubInstanceBuffer);
    if (!m_SubInstanceBuffer)
    {
        m_Stats.LastError = "Failed to create RTXPT sub-instance buffer";
        return false;
    }

    m_Stats.BLASCount        = static_cast<Uint32>(m_BLASRecords.size());
    m_Stats.InstanceCount    = static_cast<Uint32>(Instances.size());
    m_Stats.SubInstanceCount = static_cast<Uint32>(SubInstances.size());
    m_Stats.Built            = true;
    return true;
}

} // namespace Diligent
