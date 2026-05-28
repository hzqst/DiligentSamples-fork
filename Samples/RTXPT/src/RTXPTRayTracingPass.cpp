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

#include "RTXPTRayTracingPass.hpp"

#include <algorithm>

#include "GraphicsTypesX.hpp"

namespace Diligent
{

void RTXPTRayTracingPass::Reset()
{
    m_PSO.Release();
    m_SRB.Release();
    m_SBT.Release();
    m_TLAS.Release();
    m_Stats = {};
}

bool RTXPTRayTracingPass::Initialize(IRenderDevice*  pDevice,
                                     IDeviceContext* pContext,
                                     IEngineFactory* pEngineFactory,
                                     IBuffer*        pFrameConstants,
                                     IBuffer*        pMaterialBuffer,
                                     IBuffer*        pSubInstanceBuffer,
                                     IBuffer*        pLightBuffer,
                                     ITopLevelAS*    pTLAS,
                                     bool            RayTracingSupported,
                                     bool            StandaloneRTShadersSupported)
{
    Reset();

    if (!RayTracingSupported)
    {
        m_Stats.DisabledReason = "Ray tracing is not supported by this device";
        return false;
    }

    if (!StandaloneRTShadersSupported)
    {
        m_Stats.DisabledReason = "Standalone ray tracing shaders are not supported by this device";
        return false;
    }

    if (pFrameConstants == nullptr || pTLAS == nullptr)
    {
        m_Stats.DisabledReason = "Frame constants or TLAS are unavailable";
        return false;
    }

    m_TLAS = pTLAS;

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    pEngineFactory->CreateDefaultShaderSourceStreamFactory("shaders", &pShaderSourceFactory);

    ShaderCreateInfo ShaderCI;
    ShaderCI.Desc.UseCombinedTextureSamplers = false;
    ShaderCI.SourceLanguage                  = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.ShaderCompiler                  = SHADER_COMPILER_DXC;
    ShaderCI.CompileFlags                    = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
    ShaderCI.HLSLVersion                     = {6, 5};
    ShaderCI.pShaderSourceStreamFactory      = pShaderSourceFactory;

    RefCntAutoPtr<IShader> pRayGen;
    ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_GEN;
    ShaderCI.Desc.Name       = "RTXPT minimal raygen";
    ShaderCI.FilePath        = "RTXPTMinimal.rgen";
    ShaderCI.EntryPoint      = "main";
    pDevice->CreateShader(ShaderCI, &pRayGen);

    RefCntAutoPtr<IShader> pMiss;
    ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_MISS;
    ShaderCI.Desc.Name       = "RTXPT minimal miss";
    ShaderCI.FilePath        = "RTXPTMinimal.rmiss";
    ShaderCI.EntryPoint      = "main";
    pDevice->CreateShader(ShaderCI, &pMiss);

    RefCntAutoPtr<IShader> pClosestHit;
    ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_CLOSEST_HIT;
    ShaderCI.Desc.Name       = "RTXPT minimal closest hit";
    ShaderCI.FilePath        = "RTXPTMinimal.rchit";
    ShaderCI.EntryPoint      = "main";
    pDevice->CreateShader(ShaderCI, &pClosestHit);

    if (!pRayGen || !pMiss || !pClosestHit)
    {
        m_Stats.LastError = "Failed to create RTXPT minimal ray tracing shaders";
        return false;
    }

    RayTracingPipelineStateCreateInfoX PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name         = "RTXPT minimal RT PSO";
    PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_RAY_TRACING;
    PSOCreateInfo.AddGeneralShader("Main", pRayGen);
    PSOCreateInfo.AddGeneralShader("PrimaryMiss", pMiss);
    PSOCreateInfo.AddTriangleHitShader("PrimaryHit", pClosestHit);
    PSOCreateInfo.RayTracingPipeline.MaxRecursionDepth = 1;
    PSOCreateInfo.RayTracingPipeline.ShaderRecordSize  = 0;
    PSOCreateInfo.MaxAttributeSize                     = static_cast<Uint32>(sizeof(float) * 2);
    PSOCreateInfo.MaxPayloadSize                       = static_cast<Uint32>(sizeof(float) * 4);

    // Bind each bridge resource only to the stage that actually references it. DXC strips unused
    // declarations during compilation, so declaring them in stages that never call the bridge
    // helpers would leave dangling layout entries that GetStaticVariableByName cannot resolve.
    //
    // Stage map for Phase 5.1:
    //   g_FrameConstants  -> raygen    (camera state for primary rays)
    //   g_TLAS            -> raygen (the only stage that issues TraceRay)
    //   g_Materials       -> closest hit (Bridge::GetMaterial)
    //   g_SubInstanceData -> closest hit (Bridge::GetSubInstanceData)
    //   g_Lights          -> miss      (Bridge::GetLight for the sun tint helper)
    //   g_OutputColor     -> raygen    (write target)
    PipelineResourceLayoutDescX ResourceLayout;
    ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    ResourceLayout
        .AddVariable(SHADER_TYPE_RAY_GEN, "g_FrameConstants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_RAY_GEN, "g_TLAS", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_RAY_CLOSEST_HIT, "g_Materials", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_RAY_CLOSEST_HIT, "g_SubInstanceData", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_RAY_MISS, "g_Lights", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_RAY_GEN, "g_OutputColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);
    PSOCreateInfo.PSODesc.ResourceLayout = ResourceLayout;

    pDevice->CreateRayTracingPipelineState(PSOCreateInfo, &m_PSO);
    if (!m_PSO)
    {
        m_Stats.LastError = "Failed to create RTXPT minimal RT PSO";
        return false;
    }

    auto SetStatic = [&](SHADER_TYPE Stage, const char* Name, IDeviceObject* pObject) {
        if (pObject == nullptr)
            return false;

        IShaderResourceVariable* pVar = m_PSO->GetStaticVariableByName(Stage, Name);
        if (pVar == nullptr)
            return false;

        pVar->Set(pObject);
        return true;
    };

    const bool FrameConstantsBound = SetStatic(SHADER_TYPE_RAY_GEN, "g_FrameConstants", pFrameConstants);
    const bool TLASBound           = SetStatic(SHADER_TYPE_RAY_GEN, "g_TLAS", m_TLAS);

    if (!FrameConstantsBound || !TLASBound)
    {
        m_Stats.LastError = "Failed to bind required RTXPT frame constants or TLAS";
        return false;
    }

    IDeviceObject* pMaterialsView   = nullptr;
    IDeviceObject* pSubInstanceView = nullptr;
    IDeviceObject* pLightsView      = nullptr;

    if (pMaterialBuffer != nullptr)
        pMaterialsView = pMaterialBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE);
    if (pSubInstanceBuffer != nullptr)
        pSubInstanceView = pSubInstanceBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE);
    if (pLightBuffer != nullptr)
        pLightsView = pLightBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE);

    m_Stats.MaterialBridgeBound = SetStatic(SHADER_TYPE_RAY_CLOSEST_HIT, "g_Materials", pMaterialsView);
    m_Stats.SubInstanceBound    = SetStatic(SHADER_TYPE_RAY_CLOSEST_HIT, "g_SubInstanceData", pSubInstanceView);
    m_Stats.LightBridgeBound    = SetStatic(SHADER_TYPE_RAY_MISS, "g_Lights", pLightsView);

    if (!m_Stats.MaterialBridgeBound || !m_Stats.SubInstanceBound || !m_Stats.LightBridgeBound)
    {
        m_Stats.LastError = "Failed to bind required RTXPT bridge buffers";
        return false;
    }

    m_PSO->CreateShaderResourceBinding(&m_SRB, true);
    if (!m_SRB)
    {
        m_Stats.LastError = "Failed to create RTXPT minimal RT SRB";
        return false;
    }

    ShaderBindingTableDesc SBTDesc;
    SBTDesc.Name = "RTXPT minimal SBT";
    SBTDesc.pPSO = m_PSO;
    pDevice->CreateSBT(SBTDesc, &m_SBT);
    if (!m_SBT)
    {
        m_Stats.LastError = "Failed to create RTXPT minimal SBT";
        return false;
    }

    m_SBT->BindRayGenShader("Main");
    m_SBT->BindMissShader("PrimaryMiss", 0);
    m_SBT->BindHitGroupForTLAS(m_TLAS, 0, "PrimaryHit");
    pContext->UpdateSBT(m_SBT);

    // TODO(RTXPT-Port Phase 4): Restore stable-plane pre-pass and fill-stable-planes dispatch; current path traces one minimal primary-ray pass.
    // TODO(RTXPT-Port Phase 5.2): Replace flat-shaded closest hit with the reference path tracer core (BxDF + multi-bounce + accumulation).
    m_Stats.Ready = true;
    return true;
}

bool RTXPTRayTracingPass::Trace(IDeviceContext* pContext, ITextureView* pOutputUAV, Uint32 Width, Uint32 Height)
{
    m_Stats.LastTraceExecuted = false;

    if (!IsReady() || pOutputUAV == nullptr || Width == 0 || Height == 0)
        return false;

    IShaderResourceVariable* pOutputColorVar = m_SRB->GetVariableByName(SHADER_TYPE_RAY_GEN, "g_OutputColor");
    if (pOutputColorVar == nullptr)
    {
        m_Stats.LastError = "Failed to find RTXPT output color binding";
        return false;
    }

    pOutputColorVar->Set(pOutputUAV);

    pContext->SetPipelineState(m_PSO);
    pContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    TraceRaysAttribs Attribs;
    Attribs.DimensionX = Width;
    Attribs.DimensionY = Height;
    Attribs.pSBT       = m_SBT;
    pContext->TraceRays(Attribs);

    m_Stats.LastTraceExecuted = true;
    ++m_Stats.TraceCount;
    return true;
}

} // namespace Diligent
