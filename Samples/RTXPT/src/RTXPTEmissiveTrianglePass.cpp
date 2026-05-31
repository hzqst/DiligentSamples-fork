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

#include "RTXPTEmissiveTrianglePass.hpp"

#include "DebugUtilities.hpp"
#include "GraphicsTypesX.hpp"

namespace Diligent
{

void RTXPTEmissiveTrianglePass::Reset()
{
    m_PSO.Release();
    m_SRB.Release();
    m_IndexBufferView.Release();
    m_Stats = {};
}

bool RTXPTEmissiveTrianglePass::Initialize(IRenderDevice*  pDevice,
                                           IEngineFactory* pEngineFactory,
                                           IBuffer*        pMaterialBuffer,
                                           IBuffer*        pSubInstanceBuffer,
                                           IBuffer*        pSubInstanceTransformBuffer,
                                           IBuffer*        pVertexBuffer,
                                           IBuffer*        pSkinnedVertexBuffer,
                                           IBuffer*        pIndexBuffer,
                                           VALUE_TYPE      IndexValueType,
                                           IBuffer*        pEmissiveTriangleBuffer,
                                           bool            ComputeSupported)
{
    Reset();

    if (!ComputeSupported)
    {
        m_Stats.DisabledReason = "Compute shaders are not supported by this device";
        return false;
    }

    if (pDevice == nullptr || pEngineFactory == nullptr || pMaterialBuffer == nullptr || pSubInstanceBuffer == nullptr ||
        pSubInstanceTransformBuffer == nullptr || pVertexBuffer == nullptr || pSkinnedVertexBuffer == nullptr ||
        pIndexBuffer == nullptr || pEmissiveTriangleBuffer == nullptr)
    {
        m_Stats.DisabledReason = "RTXPT emissive triangle pass requires all scene buffers";
        return false;
    }

    if (IndexValueType != VT_UINT16 && IndexValueType != VT_UINT32)
    {
        m_Stats.DisabledReason = "RTXPT emissive triangle pass requires VT_UINT16 or VT_UINT32 indices";
        return false;
    }

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    pEngineFactory->CreateDefaultShaderSourceStreamFactory("shaders;shaders\\PathTracer", &pShaderSourceFactory);

    ShaderCreateInfo ShaderCI;
    ShaderCI.Desc.ShaderType            = SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name                  = "RTXPT emissive triangle build";
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.ShaderCompiler             = SHADER_COMPILER_DXC;
    ShaderCI.CompileFlags               = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
    ShaderCI.FilePath                   = "PathTracer/EmissiveTriangleBuild.hlsl";
    ShaderCI.EntryPoint                 = "main";
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

    RefCntAutoPtr<IShader> pCS;
    pDevice->CreateShader(ShaderCI, &pCS);
    VERIFY(pCS, "Failed to create RTXPT emissive triangle build shader");
    if (!pCS)
        return false;

    ComputePipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name         = "RTXPT emissive triangle build PSO";
    PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
    PSOCreateInfo.pCS                  = pCS;

    PipelineResourceLayoutDescX ResourceLayout;
    ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    ResourceLayout
        .AddVariable(SHADER_TYPE_COMPUTE, "t_PTMaterialData", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_COMPUTE, "t_SubInstanceData", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_COMPUTE, "t_SubInstanceTransforms", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_COMPUTE, "t_VertexBuffer", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_COMPUTE, "t_SkinnedVertexBuffer", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_COMPUTE, "t_IndexBuffer", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_COMPUTE, "u_EmissiveTriangles", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);
    PSOCreateInfo.PSODesc.ResourceLayout = ResourceLayout;

    pDevice->CreateComputePipelineState(PSOCreateInfo, &m_PSO);
    VERIFY(m_PSO, "Failed to create RTXPT emissive triangle build PSO");
    if (!m_PSO)
        return false;

    if (IndexValueType != VT_UINT16 && IndexValueType != VT_UINT32)
        return false;

    BufferViewDesc IndexViewDesc;
    IndexViewDesc.Name                 = "RTXPT emissive triangle index buffer SRV";
    IndexViewDesc.ViewType             = BUFFER_VIEW_SHADER_RESOURCE;
    IndexViewDesc.Format.ValueType     = IndexValueType;
    IndexViewDesc.Format.NumComponents = 1;
    IndexViewDesc.Format.IsNormalized  = false;
    pIndexBuffer->CreateView(IndexViewDesc, &m_IndexBufferView);
    VERIFY(m_IndexBufferView, "Failed to create RTXPT emissive triangle index buffer view");
    if (!m_IndexBufferView)
        return false;

    auto SetStatic = [&](const char* Name, IDeviceObject* pObject) {
        if (pObject == nullptr)
        {
            DEV_ERROR("RTXPT emissive triangle pass static resource is null: ", Name);
            return false;
        }

        IShaderResourceVariable* pVar = m_PSO->GetStaticVariableByName(SHADER_TYPE_COMPUTE, Name);
        if (pVar == nullptr)
        {
            UNEXPECTED("RTXPT emissive triangle pass static shader variable is missing: ", Name);
            return false;
        }

        pVar->Set(pObject);
        return true;
    };

    const bool Bound =
        SetStatic("t_PTMaterialData", pMaterialBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE)) &&
        SetStatic("t_SubInstanceData", pSubInstanceBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE)) &&
        SetStatic("t_SubInstanceTransforms", pSubInstanceTransformBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE)) &&
        SetStatic("t_VertexBuffer", pVertexBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE)) &&
        SetStatic("t_SkinnedVertexBuffer", pSkinnedVertexBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE)) &&
        SetStatic("t_IndexBuffer", m_IndexBufferView) &&
        SetStatic("u_EmissiveTriangles", pEmissiveTriangleBuffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
    VERIFY(Bound, "Failed to bind RTXPT emissive triangle build static resources");
    if (!Bound)
        return false;

    m_PSO->CreateShaderResourceBinding(&m_SRB, true);
    VERIFY(m_SRB, "Failed to create RTXPT emissive triangle build SRB");
    if (!m_SRB)
        return false;

    m_Stats.Ready = true;
    return true;
}

bool RTXPTEmissiveTrianglePass::Dispatch(IDeviceContext* pContext, Uint32 SubInstanceCount)
{
    m_Stats.LastDispatchExecuted = false;

    if (!IsReady() || pContext == nullptr || SubInstanceCount == 0)
        return false;

    pContext->SetPipelineState(m_PSO);
    pContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DispatchComputeAttribs DispatchAttribs;
    DispatchAttribs.ThreadGroupCountX = (SubInstanceCount + 63u) / 64u;
    DispatchAttribs.ThreadGroupCountY = 1;
    DispatchAttribs.ThreadGroupCountZ = 1;
    pContext->DispatchCompute(DispatchAttribs);

    m_Stats.LastDispatchExecuted = true;
    ++m_Stats.DispatchCount;
    return true;
}

} // namespace Diligent
