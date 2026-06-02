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

#include "RTXPTPostProcessPass.hpp"

#include "DebugUtilities.hpp"
#include "GraphicsTypesX.hpp"
#include "MapHelper.hpp"
#include "ShaderMacroHelper.hpp"

namespace Diligent
{

namespace
{

struct RTXPTPostProcessConstants
{
    Uint32 Width         = 0;
    Uint32 Height        = 0;
    float  EdgeThreshold = 0.1f;
    float  Padding0      = 0.0f;
};
static_assert(sizeof(RTXPTPostProcessConstants) == 16, "RTXPTPostProcessConstants must match RTXPTPostProcess.csh");

bool SetStaticVariable(IPipelineState* pPSO, SHADER_TYPE ShaderType, const char* Name, IDeviceObject* pObject, bool Required)
{
    IShaderResourceVariable* pVar = pPSO != nullptr ? pPSO->GetStaticVariableByName(ShaderType, Name) : nullptr;
    if (pVar == nullptr)
        return !Required;
    if (pObject == nullptr)
        return !Required;

    pVar->Set(pObject);
    return true;
}

bool SetSRBVariable(IShaderResourceBinding* pSRB, SHADER_TYPE ShaderType, const char* Name, IDeviceObject* pObject, bool Required)
{
    IShaderResourceVariable* pVar = pSRB != nullptr ? pSRB->GetVariableByName(ShaderType, Name) : nullptr;
    if (pVar == nullptr)
        return !Required;
    if (pObject == nullptr)
        return !Required;

    pVar->Set(pObject);
    return true;
}

} // namespace

void RTXPTPostProcessPass::Reset()
{
    m_HdrTestPSO.Release();
    m_EdgeDetectionPSO.Release();
    m_HdrTestSRB.Release();
    m_EdgeDetectionSRB.Release();
    m_Constants.Release();
    m_Stats = {};
}

bool RTXPTPostProcessPass::Initialize(IRenderDevice* pDevice, IEngineFactory* pEngineFactory, bool ComputeSupported)
{
    Reset();

    if (!ComputeSupported)
    {
        DEV_ERROR("RTXPT post-process pass requires compute shader support");
        return false;
    }

    if (pDevice == nullptr)
    {
        DEV_ERROR("RTXPT post-process pass requires a render device");
        return false;
    }

    if (pEngineFactory == nullptr)
    {
        DEV_ERROR("RTXPT post-process pass requires an engine factory");
        return false;
    }

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    pEngineFactory->CreateDefaultShaderSourceStreamFactory("shaders;shaders\\PostProcessing", &pShaderSourceFactory);

    ShaderCreateInfo ShaderCI;
    ShaderCI.Desc.ShaderType            = SHADER_TYPE_COMPUTE;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.ShaderCompiler             = SHADER_COMPILER_DXC;
    ShaderCI.CompileFlags               = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
    ShaderCI.FilePath                   = "PostProcessing/RTXPTPostProcess.csh";
    ShaderCI.EntryPoint                 = "main";
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

    BufferDesc ConstantsDesc;
    ConstantsDesc.Name           = "RTXPT post-process constants";
    ConstantsDesc.Size           = sizeof(RTXPTPostProcessConstants);
    ConstantsDesc.BindFlags      = BIND_UNIFORM_BUFFER;
    ConstantsDesc.Usage          = USAGE_DYNAMIC;
    ConstantsDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
    pDevice->CreateBuffer(ConstantsDesc, nullptr, &m_Constants);
    VERIFY(m_Constants, "Failed to create RTXPT post-process constants");
    if (!m_Constants)
        return false;

    const bool PipelinesReady =
        CreatePostProcessPSO(pDevice,
                             ShaderCI,
                             "RTXPT HDR post-process test",
                             "RTXPT HDR post-process test PSO",
                             "RTXPT_POST_PROCESS_HDR_TEST",
                             m_HdrTestPSO,
                             m_HdrTestSRB) &&
        CreatePostProcessPSO(pDevice,
                             ShaderCI,
                             "RTXPT LDR edge detection",
                             "RTXPT LDR edge detection PSO",
                             "RTXPT_POST_PROCESS_EDGE_DETECTION",
                             m_EdgeDetectionPSO,
                             m_EdgeDetectionSRB);
    VERIFY(PipelinesReady, "Failed to create RTXPT post-process pipelines");
    if (!PipelinesReady)
        return false;

    m_Stats.Ready = true;
    return true;
}

bool RTXPTPostProcessPass::CreatePostProcessPSO(IRenderDevice*                         pDevice,
                                                const ShaderCreateInfo&                BaseShaderCI,
                                                const char*                            ShaderName,
                                                const char*                            PSOName,
                                                const char*                            ModeMacro,
                                                RefCntAutoPtr<IPipelineState>&         PSO,
                                                RefCntAutoPtr<IShaderResourceBinding>& SRB)
{
    PSO.Release();
    SRB.Release();

    ShaderMacroHelper Macros;
    Macros.Add("RTXPT_POST_PROCESS_MODE", ModeMacro);

    ShaderCreateInfo ShaderCI = BaseShaderCI;
    ShaderCI.Desc.Name        = ShaderName;
    ShaderCI.Macros           = Macros;

    RefCntAutoPtr<IShader> pCS;
    pDevice->CreateShader(ShaderCI, &pCS);
    VERIFY(pCS, "Failed to create shader: ", ShaderName);
    if (!pCS)
        return false;

    ComputePipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name         = PSOName;
    PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
    PSOCreateInfo.pCS                  = pCS;

    PipelineResourceLayoutDescX ResourceLayout;
    ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    ResourceLayout
        .AddVariable(SHADER_TYPE_COMPUTE, "g_PostProcessConstants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_COMPUTE, "t_LdrColorScratch", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
        .AddVariable(SHADER_TYPE_COMPUTE, "u_ProcessedOutputColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
        .AddVariable(SHADER_TYPE_COMPUTE, "u_PostTonemapOutputColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);
    PSOCreateInfo.PSODesc.ResourceLayout = ResourceLayout;

    pDevice->CreateComputePipelineState(PSOCreateInfo, &PSO);
    VERIFY(PSO, "Failed to create RTXPT post-process PSO: ", PSOName);
    if (!PSO)
        return false;

    if (!SetStaticVariable(PSO, SHADER_TYPE_COMPUTE, "g_PostProcessConstants", m_Constants, true))
    {
        DEV_ERROR("RTXPT post-process pass failed to bind constants for ", PSOName);
        return false;
    }

    PSO->CreateShaderResourceBinding(&SRB, true);
    VERIFY(SRB, "Failed to create RTXPT post-process SRB: ", PSOName);
    return SRB != nullptr;
}

bool RTXPTPostProcessPass::UpdateConstants(IDeviceContext* pContext, Uint32 Width, Uint32 Height, float EdgeThreshold)
{
    if (pContext == nullptr || !m_Constants)
        return false;

    RTXPTPostProcessConstants Constants;
    Constants.Width         = Width;
    Constants.Height        = Height;
    Constants.EdgeThreshold = EdgeThreshold;

    MapHelper<RTXPTPostProcessConstants> Mapped{pContext, m_Constants, MAP_WRITE, MAP_FLAG_DISCARD};
    VERIFY(Mapped, "Failed to map RTXPT post-process constants");
    if (!Mapped)
        return false;

    *Mapped = Constants;
    return true;
}

bool RTXPTPostProcessPass::RunHdrTest(IDeviceContext* pContext, const RTXPTPostProcessRenderAttribs& Attribs)
{
    m_Stats.LastHdrTestExecuted = false;

    if (!Attribs.Params.EnableHdrTest)
        return true;

    if (!IsReady() || pContext == nullptr ||
        Attribs.pProcessedOutputUAV == nullptr ||
        Attribs.Width == 0 ||
        Attribs.Height == 0 ||
        !m_HdrTestPSO ||
        !m_HdrTestSRB)
        return false;

    pContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    if (!UpdateConstants(pContext, Attribs.Width, Attribs.Height, Attribs.Params.EdgeThreshold))
        return false;

    if (!SetSRBVariable(m_HdrTestSRB, SHADER_TYPE_COMPUTE, "u_ProcessedOutputColor", Attribs.pProcessedOutputUAV, true))
        return false;

    pContext->SetPipelineState(m_HdrTestPSO);
    pContext->CommitShaderResources(m_HdrTestSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DispatchComputeAttribs DispatchAttribs;
    DispatchAttribs.ThreadGroupCountX = (Attribs.Width + 7u) / 8u;
    DispatchAttribs.ThreadGroupCountY = (Attribs.Height + 7u) / 8u;
    DispatchAttribs.ThreadGroupCountZ = 1;
    pContext->DispatchCompute(DispatchAttribs);

    m_Stats.LastHdrTestExecuted = true;
    ++m_Stats.HdrTestDispatchCount;
    return true;
}

bool RTXPTPostProcessPass::RunEdgeDetection(IDeviceContext* pContext, const RTXPTPostProcessRenderAttribs& Attribs)
{
    m_Stats.LastEdgeDetectionExecuted = false;

    if (!Attribs.Params.EnableEdgeDetection)
        return true;

    if (!IsReady() || pContext == nullptr ||
        Attribs.pLdrColorTexture == nullptr ||
        Attribs.pLdrColorScratchTexture == nullptr ||
        Attribs.pLdrColorScratchSRV == nullptr ||
        Attribs.pLdrColorUAV == nullptr ||
        Attribs.Width == 0 ||
        Attribs.Height == 0 ||
        !m_EdgeDetectionPSO ||
        !m_EdgeDetectionSRB)
        return false;

    pContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    CopyTextureAttribs CopyAttribs{Attribs.pLdrColorTexture,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                   Attribs.pLdrColorScratchTexture,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
    pContext->CopyTexture(CopyAttribs);

    if (!UpdateConstants(pContext, Attribs.Width, Attribs.Height, Attribs.Params.EdgeThreshold))
        return false;

    const bool Bound =
        SetSRBVariable(m_EdgeDetectionSRB, SHADER_TYPE_COMPUTE, "t_LdrColorScratch", Attribs.pLdrColorScratchSRV, true) &&
        SetSRBVariable(m_EdgeDetectionSRB, SHADER_TYPE_COMPUTE, "u_PostTonemapOutputColor", Attribs.pLdrColorUAV, true);
    if (!Bound)
        return false;

    pContext->SetPipelineState(m_EdgeDetectionPSO);
    pContext->CommitShaderResources(m_EdgeDetectionSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DispatchComputeAttribs DispatchAttribs;
    DispatchAttribs.ThreadGroupCountX = (Attribs.Width + 7u) / 8u;
    DispatchAttribs.ThreadGroupCountY = (Attribs.Height + 7u) / 8u;
    DispatchAttribs.ThreadGroupCountZ = 1;
    pContext->DispatchCompute(DispatchAttribs);

    m_Stats.LastEdgeDetectionExecuted = true;
    ++m_Stats.EdgeDetectionDispatchCount;
    return true;
}

} // namespace Diligent
