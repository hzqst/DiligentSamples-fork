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

#include "RTXPTSkinnedGeometry.hpp"

#include <algorithm>

#include "GraphicsTypesX.hpp"
#include "MapHelper.hpp"

namespace Diligent
{

void RTXPTSkinnedGeometry::Reset()
{
    m_Nodes.clear();
    m_JointMatrices.clear();
    m_SourceVertexBuffer.Release();
    m_SourceSkinBuffer.Release();
    m_SkinnedVertexBuffer.Release();
    m_JointMatrixBuffer.Release();
    m_SkinningConstantsCB.Release();
    m_PSO.Release();
    m_SRB.Release();
    m_Stats = {};
}

void RTXPTSkinnedGeometry::BuildNodeTable(const GLTF::Model& Model, Uint32 SceneIndex)
{
    if (SceneIndex >= Model.Scenes.size())
        return;

    const GLTF::Scene& Scene            = Model.Scenes[SceneIndex];
    Uint32             VertexBase       = 0;
    Uint32             JointBase        = 0;
    const Uint32       SourceVertexBase = Model.GetBaseVertex();

    const IBuffer* pVertexBuffer    = Model.GetVertexBufferCount() > 0 ? Model.GetVertexBuffer(0) : nullptr;
    const Uint64   VertexOffset     = Uint64{SourceVertexBase} * sizeof(RTXPTGeometryVertex);
    const Uint32   ModelVertexCount = pVertexBuffer != nullptr && pVertexBuffer->GetDesc().Size > VertexOffset ?
        static_cast<Uint32>((pVertexBuffer->GetDesc().Size - VertexOffset) / sizeof(RTXPTGeometryVertex)) :
        0;

    for (const GLTF::Node* pNode : Scene.LinearNodes)
    {
        if (pNode == nullptr || pNode->pMesh == nullptr || pNode->pSkin == nullptr)
            continue;

        const Uint32             JointCount = static_cast<Uint32>(pNode->pSkin->Joints.size());
        RTXPTSkinnedNodeGeometry Node;
        Node.pNode            = pNode;
        Node.SourceVertexBase = SourceVertexBase;
        Node.VertexBase       = VertexBase;
        Node.VertexCount      = ModelVertexCount;
        Node.JointBase        = JointBase;
        Node.JointCount       = JointCount;
        m_Nodes.push_back(Node);

        VertexBase += ModelVertexCount;
        JointBase += JointCount;
    }

    m_Stats.SkinnedNodeCount   = static_cast<Uint32>(m_Nodes.size());
    m_Stats.SkinnedVertexCount = VertexBase;
    m_Stats.JointMatrixCount   = JointBase;
}

bool RTXPTSkinnedGeometry::CreateBuffers(IRenderDevice* pDevice, IBuffer* pSourceVertexBuffer, IBuffer* pSourceSkinBuffer)
{
    const Uint32 VertexCountForBuffer = std::max<Uint32>(m_Stats.SkinnedVertexCount, 1);

    BufferDesc VertexDesc;
    VertexDesc.Name              = "RTXPT current skinned vertex arena";
    VertexDesc.Usage             = USAGE_DEFAULT;
    VertexDesc.BindFlags         = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS | BIND_RAY_TRACING;
    VertexDesc.Mode              = BUFFER_MODE_STRUCTURED;
    VertexDesc.ElementByteStride = sizeof(RTXPTGeometryVertex);
    VertexDesc.Size              = Uint64{VertexCountForBuffer} * sizeof(RTXPTGeometryVertex);
    pDevice->CreateBuffer(VertexDesc, nullptr, &m_SkinnedVertexBuffer);
    if (!m_SkinnedVertexBuffer)
    {
        m_Stats.LastError = "Failed to create RTXPT skinned vertex arena";
        return false;
    }

    if (m_Nodes.empty())
        return true;

    if (pSourceVertexBuffer == nullptr || pSourceSkinBuffer == nullptr)
    {
        m_Stats.LastError = "RTXPT skinned geometry requires source vertex and skin buffers";
        return false;
    }

    m_SourceVertexBuffer = pSourceVertexBuffer;
    m_SourceSkinBuffer   = pSourceSkinBuffer;

    BufferDesc JointDesc;
    JointDesc.Name              = "RTXPT skinned joint matrices";
    JointDesc.Usage             = USAGE_DYNAMIC;
    JointDesc.BindFlags         = BIND_SHADER_RESOURCE;
    JointDesc.CPUAccessFlags    = CPU_ACCESS_WRITE;
    JointDesc.Mode              = BUFFER_MODE_STRUCTURED;
    JointDesc.ElementByteStride = sizeof(float4x4);
    JointDesc.Size              = Uint64{std::max<Uint32>(m_Stats.JointMatrixCount, 1)} * sizeof(float4x4);
    pDevice->CreateBuffer(JointDesc, nullptr, &m_JointMatrixBuffer);
    if (!m_JointMatrixBuffer)
    {
        m_Stats.LastError = "Failed to create RTXPT skinned joint matrix buffer";
        return false;
    }

    BufferDesc ConstantsDesc;
    ConstantsDesc.Name           = "RTXPT skinning constants";
    ConstantsDesc.Usage          = USAGE_DYNAMIC;
    ConstantsDesc.BindFlags      = BIND_UNIFORM_BUFFER;
    ConstantsDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
    ConstantsDesc.Size           = sizeof(SkinningConstants);
    pDevice->CreateBuffer(ConstantsDesc, nullptr, &m_SkinningConstantsCB);
    if (!m_SkinningConstantsCB)
    {
        m_Stats.LastError = "Failed to create RTXPT skinning constants";
        return false;
    }

    return true;
}

bool RTXPTSkinnedGeometry::CreatePipeline(IRenderDevice* pDevice, IEngineFactory* pEngineFactory)
{
    if (m_Nodes.empty())
        return true;

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    pEngineFactory->CreateDefaultShaderSourceStreamFactory("shaders;shaders\\PathTracer", &pShaderSourceFactory);

    ShaderCreateInfo ShaderCI;
    ShaderCI.Desc.ShaderType            = SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name                  = "RTXPT skinned vertex build";
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.ShaderCompiler             = SHADER_COMPILER_DXC;
    ShaderCI.CompileFlags               = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
    ShaderCI.FilePath                   = "PathTracer/SkinnedVertexBuild.csh";
    ShaderCI.EntryPoint                 = "main";
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

    RefCntAutoPtr<IShader> pCS;
    pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS)
    {
        m_Stats.LastError = "Failed to create RTXPT skinned vertex build shader";
        return false;
    }

    ComputePipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name         = "RTXPT skinned vertex build PSO";
    PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
    PSOCreateInfo.pCS                  = pCS;

    PipelineResourceLayoutDescX ResourceLayout;
    ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    ResourceLayout
        .AddVariable(SHADER_TYPE_COMPUTE, "cbSkinningConstants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_COMPUTE, "t_SourceVertices", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_COMPUTE, "t_SourceSkinData", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_COMPUTE, "t_JointMatrices", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_COMPUTE, "u_SkinnedVertices", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);
    PSOCreateInfo.PSODesc.ResourceLayout = ResourceLayout;

    pDevice->CreateComputePipelineState(PSOCreateInfo, &m_PSO);
    if (!m_PSO)
    {
        m_Stats.LastError = "Failed to create RTXPT skinned vertex build PSO";
        return false;
    }

    auto SetStatic = [&](const char* Name, IDeviceObject* pObject) {
        IShaderResourceVariable* pVar = m_PSO->GetStaticVariableByName(SHADER_TYPE_COMPUTE, Name);
        if (pVar == nullptr || pObject == nullptr)
            return false;
        pVar->Set(pObject);
        return true;
    };

    const bool Bound =
        SetStatic("cbSkinningConstants", m_SkinningConstantsCB) &&
        SetStatic("t_SourceVertices", m_SourceVertexBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE)) &&
        SetStatic("t_SourceSkinData", m_SourceSkinBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE)) &&
        SetStatic("t_JointMatrices", m_JointMatrixBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE)) &&
        SetStatic("u_SkinnedVertices", m_SkinnedVertexBuffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
    if (!Bound)
    {
        m_Stats.LastError = "Failed to bind RTXPT skinned vertex build resources";
        return false;
    }

    m_PSO->CreateShaderResourceBinding(&m_SRB, true);
    if (!m_SRB)
    {
        m_Stats.LastError = "Failed to create RTXPT skinned vertex build SRB";
        return false;
    }

    return true;
}

bool RTXPTSkinnedGeometry::Initialize(IRenderDevice*     pDevice,
                                      IEngineFactory*    pEngineFactory,
                                      const GLTF::Model& Model,
                                      Uint32             SceneIndex,
                                      IBuffer*           pSourceVertexBuffer,
                                      IBuffer*           pSourceSkinBuffer,
                                      bool               ComputeSupported)
{
    Reset();

    if (pDevice == nullptr || pEngineFactory == nullptr)
    {
        m_Stats.LastError = "RTXPT skinned geometry requires a device and engine factory";
        return false;
    }

    BuildNodeTable(Model, SceneIndex);
    if (!m_Nodes.empty() && (pSourceVertexBuffer == nullptr || pSourceSkinBuffer == nullptr))
    {
        m_Stats.LastError = "Skinned GLTF nodes require buffer 0 POSITION/NORMAL/TEXCOORD_0 and buffer 1 JOINTS_0/WEIGHTS_0";
        return false;
    }
    if (!CreateBuffers(pDevice, pSourceVertexBuffer, pSourceSkinBuffer))
        return false;

    if (m_Nodes.empty())
    {
        m_Stats.Ready = true;
        return true;
    }

    if (!ComputeSupported)
    {
        m_Stats.DisabledReason = "Skinned RTXPT geometry requires compute shaders";
        return false;
    }

    if (!CreatePipeline(pDevice, pEngineFactory))
        return false;

    m_Stats.Ready = true;
    return true;
}

bool RTXPTSkinnedGeometry::UploadJointMatrices(IDeviceContext* pContext, const GLTF::ModelTransforms& Transforms)
{
    m_JointMatrices.assign(std::max<Uint32>(m_Stats.JointMatrixCount, 1), float4x4::Identity());

    for (const RTXPTSkinnedNodeGeometry& Node : m_Nodes)
    {
        if (Node.pNode == nullptr || Node.pNode->SkinTransformsIndex < 0)
        {
            m_Stats.LastError = "RTXPT skinned node is missing skin transform data";
            return false;
        }

        const Uint32 SkinIndex = static_cast<Uint32>(Node.pNode->SkinTransformsIndex);
        if (SkinIndex >= Transforms.Skins.size())
        {
            m_Stats.LastError = "RTXPT skinned node is missing skin transform data";
            return false;
        }

        const std::vector<float4x4>& Source = Transforms.Skins[SkinIndex].JointMatrices;
        if (Source.size() < Node.JointCount)
        {
            m_Stats.LastError = "RTXPT skinned joint matrix buffer is incomplete";
            return false;
        }

        for (Uint32 i = 0; i < Node.JointCount; ++i)
            m_JointMatrices[Node.JointBase + i] = Source[i];
    }

    MapHelper<float4x4> Mapped{pContext, m_JointMatrixBuffer, MAP_WRITE, MAP_FLAG_DISCARD};
    if (!Mapped)
    {
        m_Stats.LastError = "Failed to map RTXPT skinned joint matrix buffer";
        return false;
    }

    std::copy(m_JointMatrices.begin(), m_JointMatrices.end(), static_cast<float4x4*>(Mapped));
    return true;
}

bool RTXPTSkinnedGeometry::Update(IDeviceContext* pContext, const GLTF::ModelTransforms& Transforms)
{
    m_Stats.LastDispatchExecuted = false;

    if (!IsReady())
        return false;

    if (m_Nodes.empty())
        return true;

    if (pContext == nullptr)
    {
        m_Stats.LastError = "RTXPT skinned geometry requires a device context";
        return false;
    }

    if (!UploadJointMatrices(pContext, Transforms))
        return false;

    pContext->SetPipelineState(m_PSO);

    for (const RTXPTSkinnedNodeGeometry& Node : m_Nodes)
    {
        SkinningConstants Constants;
        Constants.SourceVertexBase = Node.SourceVertexBase;
        Constants.DestVertexBase   = Node.VertexBase;
        Constants.JointBase        = Node.JointBase;
        Constants.VertexCount      = Node.VertexCount;

        {
            if (MapHelper<SkinningConstants> Mapped{pContext, m_SkinningConstantsCB, MAP_WRITE, MAP_FLAG_DISCARD})
            {
                *Mapped = Constants;
            }
            else
            {
                m_Stats.LastError = "Failed to map RTXPT skinning constants";
                return false;
            }
        }

        pContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DispatchComputeAttribs DispatchAttribs;
        DispatchAttribs.ThreadGroupCountX = (Node.VertexCount + 127u) / 128u;
        DispatchAttribs.ThreadGroupCountY = 1;
        DispatchAttribs.ThreadGroupCountZ = 1;
        pContext->DispatchCompute(DispatchAttribs);
    }

    m_Stats.LastDispatchExecuted = true;
    ++m_Stats.DispatchCount;
    return true;
}

} // namespace Diligent
