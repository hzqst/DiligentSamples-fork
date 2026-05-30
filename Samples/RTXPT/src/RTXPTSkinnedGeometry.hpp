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

#include <string>
#include <vector>

#include "Buffer.h"
#include "DeviceContext.h"
#include "EngineFactory.h"
#include "GLTFLoader.hpp"
#include "PipelineState.h"
#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
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

struct RTXPTSkinnedNodeGeometry
{
    const GLTF::Node* pNode            = nullptr;
    Uint32            SourceVertexBase = 0;
    Uint32            VertexBase       = 0;
    Uint32            VertexCount      = 0;
    Uint32            JointBase        = 0;
    Uint32            JointCount       = 0;
};

struct RTXPTSkinnedGeometryStats
{
    bool        Ready                = false;
    bool        LastDispatchExecuted = false;
    Uint32      SkinnedNodeCount     = 0;
    Uint32      SkinnedVertexCount   = 0;
    Uint32      JointMatrixCount     = 0;
    Uint32      DispatchCount        = 0;
    std::string DisabledReason;
    std::string LastError;
};

class RTXPTSkinnedGeometry
{
public:
    void Reset();

    bool Initialize(IRenderDevice*     pDevice,
                    IEngineFactory*    pEngineFactory,
                    const GLTF::Model& Model,
                    Uint32             SceneIndex,
                    IBuffer*           pSourceVertexBuffer,
                    IBuffer*           pSourceSkinBuffer,
                    bool               ComputeSupported);

    bool Update(IDeviceContext*              pContext,
                const GLTF::ModelTransforms& Transforms);

    bool HasSkinnedGeometry() const { return !m_Nodes.empty(); }
    bool IsReady() const { return m_Stats.Ready && m_SkinnedVertexBuffer; }

    IBuffer*                                     GetSkinnedVertexBuffer() const { return m_SkinnedVertexBuffer; }
    const std::vector<RTXPTSkinnedNodeGeometry>& GetNodes() const { return m_Nodes; }
    const RTXPTSkinnedGeometryStats&             GetStats() const { return m_Stats; }

private:
    struct SkinningConstants
    {
        Uint32 SourceVertexBase = 0;
        Uint32 DestVertexBase   = 0;
        Uint32 JointBase        = 0;
        Uint32 VertexCount      = 0;
    };
    static_assert(sizeof(SkinningConstants) == 16, "SkinningConstants must stay 16-byte aligned");

    bool CreateBuffers(IRenderDevice* pDevice, IBuffer* pSourceVertexBuffer, IBuffer* pSourceSkinBuffer);
    bool CreatePipeline(IRenderDevice* pDevice, IEngineFactory* pEngineFactory);
    void BuildNodeTable(const GLTF::Model& Model, Uint32 SceneIndex);
    bool UploadJointMatrices(IDeviceContext* pContext, const GLTF::ModelTransforms& Transforms);

    std::vector<RTXPTSkinnedNodeGeometry> m_Nodes;
    std::vector<float4x4>                 m_JointMatrices;
    RefCntAutoPtr<IBuffer>                m_SourceVertexBuffer;
    RefCntAutoPtr<IBuffer>                m_SourceSkinBuffer;
    RefCntAutoPtr<IBuffer>                m_SkinnedVertexBuffer;
    RefCntAutoPtr<IBuffer>                m_JointMatrixBuffer;
    RefCntAutoPtr<IBuffer>                m_SkinningConstantsCB;
    RefCntAutoPtr<IPipelineState>         m_PSO;
    RefCntAutoPtr<IShaderResourceBinding> m_SRB;
    RTXPTSkinnedGeometryStats             m_Stats;
};

} // namespace Diligent
