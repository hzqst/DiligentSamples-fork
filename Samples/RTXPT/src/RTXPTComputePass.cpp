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

#include "RTXPTComputePass.hpp"

#include "GraphicsTypesX.hpp"

namespace Diligent
{

void RTXPTComputePass::Reset()
{
    m_PSO.Release();
    m_SRB.Release();
    m_Stats = {};
    m_Name.clear();
}

bool RTXPTComputePass::Initialize(IRenderDevice*  pDevice,
                                  IEngineFactory* pEngineFactory,
                                  const char*     PassName,
                                  const char*     ShaderFilePath,
                                  IBuffer*        pFrameConstants,
                                  bool            ComputeSupported)
{
    Reset();
    m_Name = PassName != nullptr ? PassName : "RTXPT compute pass";

    if (!ComputeSupported)
    {
        m_Stats.DisabledReason = "Compute shaders are not supported by this device";
        return false;
    }

    if (pFrameConstants == nullptr)
    {
        m_Stats.DisabledReason = "Frame constants are unavailable";
        return false;
    }

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);

    ShaderCreateInfo ShaderCI;
    ShaderCI.Desc.ShaderType            = SHADER_TYPE_COMPUTE;
    ShaderCI.Desc.Name                  = m_Name.c_str();
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.ShaderCompiler             = SHADER_COMPILER_DXC;
    ShaderCI.CompileFlags               = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
    ShaderCI.FilePath                   = ShaderFilePath;
    ShaderCI.EntryPoint                 = "main";
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

    RefCntAutoPtr<IShader> pCS;
    pDevice->CreateShader(ShaderCI, &pCS);
    if (!pCS)
    {
        m_Stats.LastError = "Failed to create " + m_Name + " shader";
        return false;
    }

    ComputePipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name         = m_Name.c_str();
    PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
    PSOCreateInfo.pCS                  = pCS;

    PipelineResourceLayoutDescX ResourceLayout;
    ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    ResourceLayout
        .AddVariable(SHADER_TYPE_COMPUTE, "g_FrameConstants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_COMPUTE, "g_InputColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
        .AddVariable(SHADER_TYPE_COMPUTE, "g_OutputColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);
    PSOCreateInfo.PSODesc.ResourceLayout = ResourceLayout;

    pDevice->CreateComputePipelineState(PSOCreateInfo, &m_PSO);
    if (!m_PSO)
    {
        m_Stats.LastError = "Failed to create " + m_Name + " PSO";
        return false;
    }

    m_PSO->GetStaticVariableByName(SHADER_TYPE_COMPUTE, "g_FrameConstants")->Set(pFrameConstants);
    m_PSO->CreateShaderResourceBinding(&m_SRB, true);
    if (!m_SRB)
    {
        m_Stats.LastError = "Failed to create " + m_Name + " SRB";
        return false;
    }

    // TODO(RTXPT-Port Phase 4): Restore RTXDI DI/GI, light feedback, and denoising-guide compute chains; current helper runs only the debug color pass.
    m_Stats.Ready = true;
    return true;
}

bool RTXPTComputePass::Dispatch(IDeviceContext* pContext, ITextureView* pInputSRV, ITextureView* pOutputUAV, Uint32 Width, Uint32 Height)
{
    m_Stats.LastDispatchExecuted = false;

    if (!IsReady() || pInputSRV == nullptr || pOutputUAV == nullptr || Width == 0 || Height == 0)
        return false;

    m_SRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_InputColor")->Set(pInputSRV);
    m_SRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_OutputColor")->Set(pOutputUAV);

    pContext->SetPipelineState(m_PSO);
    pContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DispatchComputeAttribs DispatchAttribs;
    DispatchAttribs.ThreadGroupCountX = (Width + 7) / 8;
    DispatchAttribs.ThreadGroupCountY = (Height + 7) / 8;
    DispatchAttribs.ThreadGroupCountZ = 1;
    pContext->DispatchCompute(DispatchAttribs);

    m_Stats.LastDispatchExecuted = true;
    ++m_Stats.DispatchCount;
    return true;
}

} // namespace Diligent
