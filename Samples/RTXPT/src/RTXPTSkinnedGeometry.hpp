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

#pragma once

#include <cstddef>
#include <vector>

#include "Buffer.h"
#include "DeviceContext.h"
#include "EngineFactory.h"
#include "GLTFLoader.hpp"
#include "PipelineState.h"
#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "RTXPTSceneGraph.hpp"
#include "ShaderResourceBinding.h"

namespace Diligent
{

struct RTXPTGeometryVertex
{
    float3 position  = float3{0, 0, 0};
    float3 normal    = float3{0, 1, 0};
    float2 texCoord0 = float2{0, 0};
};
static_assert(sizeof(RTXPTGeometryVertex) == 32, "RTXPTGeometryVertex layout must match GeometryVertexData");
static_assert(offsetof(RTXPTGeometryVertex, normal) == 12, "RTXPTGeometryVertex normal offset must match GeometryVertexData");
static_assert(offsetof(RTXPTGeometryVertex, texCoord0) == 24, "RTXPTGeometryVertex texCoord0 offset must match GeometryVertexData");

struct RTXPTSkinnedSceneNodeGeometry
{
    RTXPTSceneId      ModelAssetId     = InvalidRTXPTSceneId;
    RTXPTSceneId      ModelInstanceId  = InvalidRTXPTSceneId;
    const GLTF::Node* pNode            = nullptr;
    Uint32            SourceVertexBase = 0;
    Uint32            VertexBase       = 0;
    Uint32            VertexCount      = 0;
    Uint32            JointBase        = 0;
    Uint32            JointCount       = 0;
};

struct RTXPTSkinnedSceneAssetBinding
{
    RTXPTSceneId                          ModelAssetId        = InvalidRTXPTSceneId;
    IBuffer*                              pSourceVertexBuffer = nullptr;
    IBuffer*                              pSourceSkinBuffer   = nullptr;
    RefCntAutoPtr<IShaderResourceBinding> pSRB;
};

struct RTXPTSkinnedSceneGeometryStats
{
    bool   Ready                = false;
    bool   LastDispatchExecuted = false;
    Uint32 SkinnedInstanceCount = 0;
    Uint32 SkinningJobCount     = 0;
    Uint32 SkinnedVertexCount   = 0;
    Uint32 JointMatrixCount     = 0;
    Uint32 DispatchCount        = 0;
};

class RTXPTSkinnedSceneGeometry
{
public:
    void Reset();

    bool Initialize(IRenderDevice*             pDevice,
                    IEngineFactory*            pEngineFactory,
                    const RTXPTSceneGraphData& SceneData,
                    bool                       ComputeSupported);

    bool Update(IDeviceContext* pContext, const RTXPTSceneGraphData& SceneData);

    bool HasSkinnedGeometry() const { return !m_Nodes.empty(); }
    bool IsReady() const { return m_Stats.Ready && m_SkinnedVertexBuffer; }

    IBuffer*                                          GetSkinnedVertexBuffer() const { return m_SkinnedVertexBuffer; }
    const std::vector<RTXPTSkinnedSceneNodeGeometry>& GetNodes() const { return m_Nodes; }
    const RTXPTSkinnedSceneNodeGeometry*              FindNode(RTXPTSceneId      ModelAssetId,
                                                               RTXPTSceneId      ModelInstanceId,
                                                               const GLTF::Node* pNode) const;
    const RTXPTSkinnedSceneGeometryStats&             GetStats() const { return m_Stats; }

private:
    struct SkinningConstants
    {
        Uint32 SourceVertexBase = 0;
        Uint32 DestVertexBase   = 0;
        Uint32 JointBase        = 0;
        Uint32 VertexCount      = 0;
    };
    static_assert(sizeof(SkinningConstants) == 16, "SkinningConstants must stay 16-byte aligned");

    bool CreateBuffers(IRenderDevice* pDevice);
    bool CreatePipeline(IRenderDevice* pDevice, IEngineFactory* pEngineFactory);
    bool CreateAssetBindings(IRenderDevice* pDevice, const RTXPTSceneGraphData& SceneData);
    bool BuildNodeTable(const RTXPTSceneGraphData& SceneData);
    bool UploadJointMatrices(IDeviceContext* pContext, const RTXPTSceneGraphData& SceneData);

    std::vector<RTXPTSkinnedSceneNodeGeometry> m_Nodes;
    std::vector<float4x4>                      m_JointMatrices;
    std::vector<RTXPTSkinnedSceneAssetBinding> m_AssetBindings;
    RefCntAutoPtr<IBuffer>                     m_SkinnedVertexBuffer;
    RefCntAutoPtr<IBuffer>                     m_JointMatrixBuffer;
    RefCntAutoPtr<IBuffer>                     m_SkinningConstantsCB;
    RefCntAutoPtr<IPipelineState>              m_PSO;
    RTXPTSkinnedSceneGeometryStats             m_Stats;
};

using RTXPTSkinnedNodeGeometry  = RTXPTSkinnedSceneNodeGeometry;
using RTXPTSkinnedGeometryStats = RTXPTSkinnedSceneGeometryStats;
using RTXPTSkinnedGeometry      = RTXPTSkinnedSceneGeometry;

} // namespace Diligent
