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

#include "RTXPTVBufferExportPass.hpp"

#include "DebugUtilities.hpp"
#include "GraphicsTypesX.hpp"

namespace Diligent
{

namespace
{

constexpr const char* VBufferExportName = "RTXPT VBufferExport pass";

bool SetStaticVariable(IPipelineState* pPSO, const char* Name, IDeviceObject* pObject, const char* ObjectName, bool Required)
{
    IShaderResourceVariable* pVar = pPSO->GetStaticVariableByName(SHADER_TYPE_COMPUTE, Name);
    if (pVar == nullptr)
    {
        if (Required)
            UNEXPECTED("RTXPT VBufferExport static shader variable is missing: ", Name);
        return !Required;
    }

    if (pObject == nullptr)
    {
        DEV_ERROR("RTXPT VBufferExport static resource object is null: ", ObjectName);
        return false;
    }

    pVar->Set(pObject);
    return true;
}

} // namespace

void RTXPTVBufferExportPass::Reset()
{
    m_PSO.Release();
    m_SRB.Release();
}

bool RTXPTVBufferExportPass::Initialize(IRenderDevice* pDevice, IEngineFactory* pEngineFactory, IBuffer* pFrameConstants, IBuffer* pMiniConstants)
{
    Reset();
    if (pDevice == nullptr || pEngineFactory == nullptr)
    {
        DEV_ERROR(VBufferExportName, " requires a render device and engine factory");
        return false;
    }

    if (pFrameConstants == nullptr || pMiniConstants == nullptr)
    {
        DEV_ERROR(VBufferExportName, " requires frame constants and mini constants");
        return false;
    }

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    pEngineFactory->CreateDefaultShaderSourceStreamFactory("shaders;shaders\\PathTracer", &pShaderSourceFactory);

    ShaderCreateInfo ShaderCI;
    ShaderCI.Desc.ShaderType            = SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name                  = VBufferExportName;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.ShaderCompiler             = SHADER_COMPILER_DXC;
    ShaderCI.CompileFlags               = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
    ShaderCI.FilePath                   = "PathTracer/ExportVisibilityBuffer.csh";
    ShaderCI.EntryPoint                 = "main";
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

    RefCntAutoPtr<IShader> pCS;
    pDevice->CreateShader(ShaderCI, &pCS);
    VERIFY(pCS, "Failed to create RTXPT VBufferExport shader");
    if (!pCS)
        return false;

    ComputePipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name         = VBufferExportName;
    PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
    PSOCreateInfo.pCS                  = pCS;

    PipelineResourceLayoutDescX ResourceLayout;
    ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    ResourceLayout
        .AddVariable(SHADER_TYPE_COMPUTE, "g_Const", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_COMPUTE, "g_MiniConst", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);
    PSOCreateInfo.PSODesc.ResourceLayout = ResourceLayout;

    pDevice->CreateComputePipelineState(PSOCreateInfo, &m_PSO);
    VERIFY(m_PSO, "Failed to create RTXPT VBufferExport PSO");
    if (!m_PSO)
        return false;

    if (!SetStaticVariable(m_PSO, "g_Const", pFrameConstants, "frame constants", true) ||
        !SetStaticVariable(m_PSO, "g_MiniConst", pMiniConstants, "mini constants", false))
        return false;

    m_PSO->CreateShaderResourceBinding(&m_SRB, true);
    VERIFY(m_SRB, "Failed to create RTXPT VBufferExport SRB");
    return m_SRB != nullptr;
}

bool RTXPTVBufferExportPass::Dispatch(IDeviceContext* pContext, Uint32 Width, Uint32 Height)
{
    if (!IsReady() || pContext == nullptr || Width == 0 || Height == 0)
        return false;

    pContext->SetPipelineState(m_PSO);
    pContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DispatchComputeAttribs DispatchAttribs;
    DispatchAttribs.ThreadGroupCountX = (Width + 7) / 8;
    DispatchAttribs.ThreadGroupCountY = (Height + 7) / 8;
    DispatchAttribs.ThreadGroupCountZ = 1;
    pContext->DispatchCompute(DispatchAttribs);
    return true;
}

} // namespace Diligent
