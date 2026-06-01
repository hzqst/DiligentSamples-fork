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
#include "DebugUtilities.hpp"

#include "GraphicsTypesX.hpp"
#include "ShaderMacroHelper.hpp"

namespace Diligent
{

namespace
{

enum class RTXPTDiagnosticMode
{
    FullPathTracer,
    ScreenPattern,
    MinimalTraceRay,
};

RTXPTDiagnosticMode GetRTXPTDiagnosticMode()
{
    return RTXPTDiagnosticMode::FullPathTracer;
}

} // namespace

void RTXPTRayTracingPass::Reset()
{
    m_PSO.Release();
    m_SRB.Release();
    m_SBT.Release();
    m_TLAS.Release();
    m_IndexBufferView.Release();
    m_Stats = {};
}

bool RTXPTRayTracingPass::Initialize(IRenderDevice*        pDevice,
                                     IDeviceContext*       pContext,
                                     IEngineFactory*       pEngineFactory,
                                     IBuffer*              pFrameConstants,
                                     IBuffer*              pMaterialBuffer,
                                     IBuffer*              pSubInstanceBuffer,
                                     IBuffer*              pLightBuffer,
                                     IBuffer*              pLightingControlBuffer,
                                     IBuffer*              pLightProxyCounters,
                                     IBuffer*              pLightSamplingProxies,
                                     IBuffer*              pLocalSamplingBuffer,
                                     ITextureView*         pFeedbackTotalWeightUAV,
                                     ITextureView*         pFeedbackCandidatesUAV,
                                     ITextureView*         pEnvironmentMapSRV,
                                     ITextureView*         pEnvironmentImportanceMapSRV,
                                     ITextureView*         pEnvironmentRadianceMapSRV,
                                     ISampler*             pEnvironmentSampler,
                                     ISampler*             pEnvironmentImportanceSampler,
                                     IBuffer*              pEmissiveTriangleBuffer,
                                     IBuffer*              pVertexBuffer,
                                     IBuffer*              pSkinnedVertexBuffer,
                                     IBuffer*              pIndexBuffer,
                                     VALUE_TYPE            IndexValueType,
                                     ITopLevelAS*          pTLAS,
                                     IDeviceObject* const* pMaterialTextures,
                                     Uint32                MaterialTextureCount,
                                     bool                  EnableMaterialTextures,
                                     bool                  EnableAnyHit,
                                     bool                  EnableLDSamplerForBSDF,
                                     bool                  RayTracingSupported,
                                     bool                  StandaloneRTShadersSupported)
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

    const RTXPTDiagnosticMode DiagnosticMode            = GetRTXPTDiagnosticMode();
    const bool                ScreenPatternDiagnostic   = DiagnosticMode == RTXPTDiagnosticMode::ScreenPattern;
    const bool                MinimalTraceRayDiagnostic = DiagnosticMode == RTXPTDiagnosticMode::MinimalTraceRay;
    const bool                FullPathTracer            = DiagnosticMode == RTXPTDiagnosticMode::FullPathTracer;

    if (!ScreenPatternDiagnostic && (pFrameConstants == nullptr || pTLAS == nullptr))
    {
        m_Stats.DisabledReason = "Frame constants or TLAS are unavailable";
        return false;
    }

    if (FullPathTracer && (pVertexBuffer == nullptr || pSkinnedVertexBuffer == nullptr || pIndexBuffer == nullptr))
    {
        m_Stats.DisabledReason = "Vertex, skinned vertex, or index buffer is unavailable for the reference path tracer";
        return false;
    }

    const bool UseTextures = FullPathTracer && EnableMaterialTextures && pMaterialTextures != nullptr && MaterialTextureCount > 0;
    const bool UseAnyHit   = FullPathTracer && EnableAnyHit;

    m_TLAS = pTLAS;

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    pEngineFactory->CreateDefaultShaderSourceStreamFactory("shaders;shaders\\PathTracer", &pShaderSourceFactory);

    ShaderCreateInfo ShaderCI;
    ShaderCI.Desc.UseCombinedTextureSamplers = false;
    ShaderCI.SourceLanguage                  = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.ShaderCompiler                  = SHADER_COMPILER_DXC;
    ShaderCI.CompileFlags                    = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
    ShaderCI.HLSLVersion                     = {6, 5};
    ShaderCI.pShaderSourceStreamFactory      = pShaderSourceFactory;

    ShaderMacroHelper Macros;
    if (ScreenPatternDiagnostic)
        Macros.Add("RTXPT_SCREEN_PATTERN_DIAGNOSTIC", 1);
    if (MinimalTraceRayDiagnostic)
        Macros.Add("RTXPT_MINIMAL_TRACE_RAY_DIAGNOSTIC", 1);
    if (UseTextures)
    {
        Macros.Add("ENABLE_MATERIAL_TEXTURES", 1);
        Macros.Add("MATERIAL_TEXTURE_COUNT", static_cast<int>(MaterialTextureCount));
    }
    Macros.Add("RTXPT_ENABLE_LOW_DISCREPANCY_SAMPLER_FOR_BSDF", EnableLDSamplerForBSDF ? 1 : 0);
    ShaderCI.Macros = Macros;

    RefCntAutoPtr<IShader> pRayGen;
    ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_GEN;
    ShaderCI.Desc.Name       = "RTXPT reference raygen";
    ShaderCI.FilePath        = "PathTracer/PathTracerSample.rgen";
    ShaderCI.EntryPoint      = "main";
    pDevice->CreateShader(ShaderCI, &pRayGen);

    RefCntAutoPtr<IShader> pMiss;
    if (!ScreenPatternDiagnostic)
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_MISS;
        ShaderCI.Desc.Name       = "RTXPT reference miss";
        ShaderCI.FilePath        = "PathTracer/PathTracerMiss.rmiss";
        ShaderCI.EntryPoint      = "main";
        pDevice->CreateShader(ShaderCI, &pMiss);
    }

    RefCntAutoPtr<IShader> pClosestHit;
    if (!ScreenPatternDiagnostic)
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_CLOSEST_HIT;
        ShaderCI.Desc.Name       = "RTXPT reference closest hit";
        ShaderCI.FilePath        = "PathTracer/PathTracerClosestHit.rchit";
        ShaderCI.EntryPoint      = "main";
        pDevice->CreateShader(ShaderCI, &pClosestHit);
    }

    RefCntAutoPtr<IShader> pAnyHit;
    if (!ScreenPatternDiagnostic && UseAnyHit)
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_ANY_HIT;
        ShaderCI.Desc.Name       = "RTXPT reference any hit";
        ShaderCI.FilePath        = "PathTracer/PathTracerAnyHit.rahit";
        ShaderCI.EntryPoint      = "main";
        pDevice->CreateShader(ShaderCI, &pAnyHit);
    }

    VERIFY(pRayGen && (ScreenPatternDiagnostic || (pMiss && pClosestHit && (!UseAnyHit || pAnyHit))),
           "Failed to create RTXPT reference ray tracing shaders");
    if (!pRayGen || (!ScreenPatternDiagnostic && (!pMiss || !pClosestHit || (UseAnyHit && !pAnyHit))))
        return false;

    RayTracingPipelineStateCreateInfoX PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name         = "RTXPT reference RT PSO";
    PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_RAY_TRACING;
    PSOCreateInfo.AddGeneralShader("Main", pRayGen);
    if (!ScreenPatternDiagnostic)
    {
        PSOCreateInfo.AddGeneralShader("PrimaryMiss", pMiss);
        if (UseAnyHit)
            PSOCreateInfo.AddTriangleHitShader("PrimaryHit", pClosestHit, pAnyHit);
        else
            PSOCreateInfo.AddTriangleHitShader("PrimaryHit", pClosestHit);
    }
    PSOCreateInfo.RayTracingPipeline.MaxRecursionDepth = 1; // Raygen drives bounces in a loop; chit/miss/anyhit do not recurse.
    PSOCreateInfo.RayTracingPipeline.ShaderRecordSize  = 0;
    PSOCreateInfo.MaxAttributeSize                     = static_cast<Uint32>(sizeof(float) * 2);
    // PathPayload = 10 * float4 = 160 bytes (R7 adds vertex normal + shadow fadeout).
    PSOCreateInfo.MaxPayloadSize = static_cast<Uint32>(sizeof(float) * 40);

    // Material data is read by raygen for nested dielectric media and by hit shaders for surface shading.
    const SHADER_TYPE HitStages = UseAnyHit ?
        (SHADER_TYPE_RAY_CLOSEST_HIT | SHADER_TYPE_RAY_ANY_HIT) :
        SHADER_TYPE_RAY_CLOSEST_HIT;
    const SHADER_TYPE MaterialStages = HitStages | SHADER_TYPE_RAY_GEN;
    const SHADER_TYPE EnvStages      = SHADER_TYPE_RAY_GEN | SHADER_TYPE_RAY_MISS;
    const SHADER_TYPE ConstStages    = UseAnyHit ? (EnvStages | SHADER_TYPE_RAY_ANY_HIT) : EnvStages;

    PipelineResourceLayoutDescX ResourceLayout;
    ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    ResourceLayout
        .AddVariable(SHADER_TYPE_RAY_GEN, "u_Output", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
        // TODO(RTXPT-Port Phase 6/P2): remove this raygen accumulation UAV
        // binding after RTXPTAccumulationPass owns accumulation.
        .AddVariable(SHADER_TYPE_RAY_GEN, "u_AccumulationBuffer", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

    if (!ScreenPatternDiagnostic)
    {
        ResourceLayout
            .AddVariable(ConstStages, "g_Const", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_RAY_GEN, "t_SceneBVH", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);
    }

    if (FullPathTracer)
    {
        ResourceLayout
            .AddVariable(MaterialStages, "t_PTMaterialData", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(HitStages, "t_SubInstanceData", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(HitStages, "t_VertexBuffer", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(HitStages, "t_SkinnedVertexBuffer", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(HitStages, "t_IndexBuffer", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_RAY_GEN, "t_Lights", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_RAY_GEN, "t_LightingControl", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_RAY_GEN, "t_LightProxyCounters", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_RAY_GEN, "t_LightSamplingProxies", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_RAY_GEN, "t_LocalSamplingBuffer", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_RAY_GEN, "u_FeedbackTotalWeight", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_RAY_GEN, "u_FeedbackCandidates", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(EnvStages, "t_EnvironmentMap", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(EnvStages, "t_EnvironmentImportanceMap", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(EnvStages, "t_EnvironmentRadianceMap", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(EnvStages, "s_EnvironmentMapSampler", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(EnvStages, "s_EnvironmentImportanceSampler", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_RAY_GEN, "t_EmissiveTriangles", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);
    }

    if (!ScreenPatternDiagnostic && UseTextures)
    {
        ResourceLayout.AddVariable(HitStages, "t_BindlessTextures", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);

        const SamplerDesc MaterialSamplerDesc{
            FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
            TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP};
        ResourceLayout.AddImmutableSampler(HitStages, "s_MaterialSampler", MaterialSamplerDesc);
    }
    PSOCreateInfo.PSODesc.ResourceLayout = ResourceLayout;

    pDevice->CreateRayTracingPipelineState(PSOCreateInfo, &m_PSO);
    VERIFY(m_PSO, "Failed to create RTXPT reference RT PSO");
    if (!m_PSO)
        return false;

    auto SetStaticForStages = [&](SHADER_TYPE Stages, const char* Name, IDeviceObject* pObject, const char* ObjectName,
                                  bool Required = true, bool* pFoundAny = nullptr) {
        bool Ok       = true;
        bool FoundAny = false;
        for (SHADER_TYPE Stage : {SHADER_TYPE_RAY_GEN, SHADER_TYPE_RAY_MISS, SHADER_TYPE_RAY_CLOSEST_HIT, SHADER_TYPE_RAY_ANY_HIT})
        {
            if ((Stages & Stage) == 0)
                continue;

            IShaderResourceVariable* pVar = m_PSO->GetStaticVariableByName(Stage, Name);
            if (pVar == nullptr)
                continue;
            FoundAny = true;

            if (pObject == nullptr)
            {
                DEV_ERROR("RTXPT static resource object is null: ", ObjectName);
                Ok = false;
                continue;
            }

            pVar->Set(pObject);
        }
        if (pFoundAny != nullptr)
            *pFoundAny = FoundAny;
        if (!FoundAny && Required)
        {
            UNEXPECTED("RTXPT static shader variable is missing: ", Name);
            return false;
        }
        return Ok;
    };

    const bool FrameConstantsBound =
        ScreenPatternDiagnostic || SetStaticForStages(ConstStages, "g_Const", pFrameConstants, "frame constants");
    const bool TLASBound =
        ScreenPatternDiagnostic || SetStaticForStages(SHADER_TYPE_RAY_GEN, "t_SceneBVH", m_TLAS, "TLAS");

    if (!FrameConstantsBound || !TLASBound)
        return false;

    if (!FullPathTracer)
    {
        m_Stats.MaterialBridgeBound      = true;
        m_Stats.SubInstanceBound         = true;
        m_Stats.LightBridgeBound         = true;
        m_Stats.LightsBakerBridgeBound   = true;
        m_Stats.EnvironmentBridgeBound   = true;
        m_Stats.EmissiveLightBridgeBound = true;
        m_Stats.VertexBufferBound        = true;
        m_Stats.SkinnedVertexBufferBound = true;
        m_Stats.IndexBufferBound         = true;
        m_Stats.MaterialTexturesBound    = true;
        m_Stats.MaterialTextureCount     = 0;
        m_Stats.AnyHitEnabled            = false;
    }

    IDeviceObject* pMaterialsView   = pMaterialBuffer != nullptr ? pMaterialBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) : nullptr;
    IDeviceObject* pSubInstanceView = pSubInstanceBuffer != nullptr ? pSubInstanceBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) : nullptr;
    IDeviceObject* pLightsView      = pLightBuffer != nullptr ? pLightBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) : nullptr;
    IDeviceObject* pLightingControlView =
        pLightingControlBuffer != nullptr ? pLightingControlBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) : nullptr;
    IDeviceObject* pLightProxyCountersView =
        pLightProxyCounters != nullptr ? pLightProxyCounters->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) : nullptr;
    IDeviceObject* pLightSamplingProxiesView =
        pLightSamplingProxies != nullptr ? pLightSamplingProxies->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) : nullptr;
    IDeviceObject* pLocalSamplingView =
        pLocalSamplingBuffer != nullptr ? pLocalSamplingBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) : nullptr;
    IDeviceObject* pEnvironmentMapView               = pEnvironmentMapSRV;
    IDeviceObject* pEnvironmentImportanceMapView     = pEnvironmentImportanceMapSRV;
    IDeviceObject* pEnvironmentRadianceMapView       = pEnvironmentRadianceMapSRV;
    IDeviceObject* pEnvironmentSamplerView           = pEnvironmentSampler;
    IDeviceObject* pEnvironmentImportanceSamplerView = pEnvironmentImportanceSampler;
    IDeviceObject* pEmissiveView =
        pEmissiveTriangleBuffer != nullptr ? pEmissiveTriangleBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) : nullptr;
    IDeviceObject* pVertexView =
        FullPathTracer && pVertexBuffer != nullptr ? pVertexBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) : nullptr;
    IDeviceObject* pSkinnedVertexView =
        FullPathTracer && pSkinnedVertexBuffer != nullptr ? pSkinnedVertexBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) : nullptr;

    // The GLTF loader creates the index buffer in BUFFER_MODE_FORMATTED but does not pre-create a typed view;
    // create one here so HLSL can declare it as Buffer<uint>.
    if (FullPathTracer && IndexValueType != VT_UINT16 && IndexValueType != VT_UINT32)
    {
        LOG_ERROR_MESSAGE("Reference path tracer requires VT_UINT16 or VT_UINT32 indices");
        return false;
    }
    if (FullPathTracer)
    {
        BufferViewDesc IndexViewDesc;
        IndexViewDesc.Name                 = "RTXPT reference index buffer SRV";
        IndexViewDesc.ViewType             = BUFFER_VIEW_SHADER_RESOURCE;
        IndexViewDesc.Format.ValueType     = IndexValueType;
        IndexViewDesc.Format.NumComponents = 1;
        IndexViewDesc.Format.IsNormalized  = false;
        pIndexBuffer->CreateView(IndexViewDesc, &m_IndexBufferView);
        VERIFY(m_IndexBufferView, "Failed to create RTXPT index buffer view");
        if (!m_IndexBufferView)
            return false;
    }

    if (FullPathTracer)
    {
        m_Stats.MaterialBridgeBound = SetStaticForStages(MaterialStages, "t_PTMaterialData", pMaterialsView, "material buffer");
        m_Stats.SubInstanceBound    = SetStaticForStages(HitStages, "t_SubInstanceData", pSubInstanceView, "sub-instance buffer");
        m_Stats.LightBridgeBound    = SetStaticForStages(SHADER_TYPE_RAY_GEN, "t_Lights", pLightsView, "light buffer");
        m_Stats.LightsBakerBridgeBound =
            SetStaticForStages(SHADER_TYPE_RAY_GEN, "t_LightingControl", pLightingControlView, "LightsBaker control buffer") &&
            SetStaticForStages(SHADER_TYPE_RAY_GEN, "t_LightProxyCounters", pLightProxyCountersView, "LightsBaker proxy counters") &&
            SetStaticForStages(SHADER_TYPE_RAY_GEN, "t_LightSamplingProxies", pLightSamplingProxiesView, "LightsBaker sampling proxies") &&
            SetStaticForStages(SHADER_TYPE_RAY_GEN, "t_LocalSamplingBuffer", pLocalSamplingView, "LightsBaker local sampling buffer") &&
            SetStaticForStages(SHADER_TYPE_RAY_GEN, "u_FeedbackTotalWeight", pFeedbackTotalWeightUAV, "LightsBaker feedback total weight") &&
            SetStaticForStages(SHADER_TYPE_RAY_GEN, "u_FeedbackCandidates", pFeedbackCandidatesUAV, "LightsBaker feedback candidates");
        bool       EnvironmentMapFound               = false;
        bool       EnvironmentImportanceMapFound     = false;
        bool       EnvironmentRadianceMapFound       = false;
        bool       EnvironmentSamplerFound           = false;
        bool       EnvironmentImportanceSamplerFound = false;
        const bool EnvironmentBridgeOk =
            SetStaticForStages(EnvStages, "t_EnvironmentMap", pEnvironmentMapView, "environment map", false, &EnvironmentMapFound) &&
            SetStaticForStages(EnvStages, "t_EnvironmentImportanceMap", pEnvironmentImportanceMapView, "environment importance map", false, &EnvironmentImportanceMapFound) &&
            SetStaticForStages(EnvStages, "t_EnvironmentRadianceMap", pEnvironmentRadianceMapView, "environment radiance map", false, &EnvironmentRadianceMapFound) &&
            SetStaticForStages(EnvStages, "s_EnvironmentMapSampler", pEnvironmentSamplerView, "environment map sampler", false, &EnvironmentSamplerFound) &&
            SetStaticForStages(EnvStages, "s_EnvironmentImportanceSampler", pEnvironmentImportanceSamplerView, "environment importance sampler", false, &EnvironmentImportanceSamplerFound);
        const bool EnvironmentBridgeReflected =
            EnvironmentMapFound || EnvironmentImportanceMapFound || EnvironmentRadianceMapFound ||
            EnvironmentSamplerFound || EnvironmentImportanceSamplerFound;
        m_Stats.EnvironmentBridgeBound = EnvironmentBridgeReflected && EnvironmentBridgeOk;
        m_Stats.EmissiveLightBridgeBound =
            SetStaticForStages(SHADER_TYPE_RAY_GEN, "t_EmissiveTriangles", pEmissiveView, "emissive triangle buffer");
        m_Stats.VertexBufferBound = SetStaticForStages(HitStages, "t_VertexBuffer", pVertexView, "vertex buffer");
        m_Stats.SkinnedVertexBufferBound =
            SetStaticForStages(HitStages, "t_SkinnedVertexBuffer", pSkinnedVertexView, "skinned vertex buffer");
        m_Stats.IndexBufferBound = SetStaticForStages(HitStages, "t_IndexBuffer", m_IndexBufferView, "index buffer");

        if (UseTextures)
        {
            m_Stats.MaterialBridgeBound = m_Stats.MaterialBridgeBound &&
                SetStaticForStages(MaterialStages, "t_PTMaterialData", pMaterialsView, "material buffer");
            m_Stats.SubInstanceBound = m_Stats.SubInstanceBound &&
                SetStaticForStages(HitStages, "t_SubInstanceData", pSubInstanceView, "sub-instance buffer");
            m_Stats.VertexBufferBound = m_Stats.VertexBufferBound &&
                SetStaticForStages(HitStages, "t_VertexBuffer", pVertexView, "vertex buffer");
            m_Stats.SkinnedVertexBufferBound = m_Stats.SkinnedVertexBufferBound &&
                SetStaticForStages(HitStages, "t_SkinnedVertexBuffer", pSkinnedVertexView, "skinned vertex buffer");
            m_Stats.IndexBufferBound = m_Stats.IndexBufferBound &&
                SetStaticForStages(HitStages, "t_IndexBuffer", m_IndexBufferView, "index buffer");
        }
    }

    const bool EnvironmentBridgeRequired = m_Stats.EnvironmentBridgeBound ||
        m_PSO->GetStaticVariableByName(SHADER_TYPE_RAY_GEN, "t_EnvironmentMap") != nullptr ||
        m_PSO->GetStaticVariableByName(SHADER_TYPE_RAY_MISS, "t_EnvironmentMap") != nullptr ||
        m_PSO->GetStaticVariableByName(SHADER_TYPE_RAY_GEN, "t_EnvironmentImportanceMap") != nullptr ||
        m_PSO->GetStaticVariableByName(SHADER_TYPE_RAY_MISS, "t_EnvironmentImportanceMap") != nullptr ||
        m_PSO->GetStaticVariableByName(SHADER_TYPE_RAY_GEN, "t_EnvironmentRadianceMap") != nullptr ||
        m_PSO->GetStaticVariableByName(SHADER_TYPE_RAY_MISS, "t_EnvironmentRadianceMap") != nullptr ||
        m_PSO->GetStaticVariableByName(SHADER_TYPE_RAY_GEN, "s_EnvironmentMapSampler") != nullptr ||
        m_PSO->GetStaticVariableByName(SHADER_TYPE_RAY_MISS, "s_EnvironmentMapSampler") != nullptr ||
        m_PSO->GetStaticVariableByName(SHADER_TYPE_RAY_GEN, "s_EnvironmentImportanceSampler") != nullptr ||
        m_PSO->GetStaticVariableByName(SHADER_TYPE_RAY_MISS, "s_EnvironmentImportanceSampler") != nullptr;
    if (!m_Stats.MaterialBridgeBound || !m_Stats.SubInstanceBound || !m_Stats.LightBridgeBound ||
        !m_Stats.LightsBakerBridgeBound || (EnvironmentBridgeRequired && !m_Stats.EnvironmentBridgeBound) || !m_Stats.EmissiveLightBridgeBound ||
        !m_Stats.VertexBufferBound || !m_Stats.SkinnedVertexBufferBound || !m_Stats.IndexBufferBound)
        return false;

    m_PSO->CreateShaderResourceBinding(&m_SRB, true);
    VERIFY(m_SRB, "Failed to create RTXPT reference RT SRB");
    if (!m_SRB)
        return false;

    if (!ScreenPatternDiagnostic && UseTextures)
    {
        IShaderResourceVariable* pClosestHitTexVar =
            m_SRB->GetVariableByName(SHADER_TYPE_RAY_CLOSEST_HIT, "t_BindlessTextures");
        IShaderResourceVariable* pAnyHitTexVar = UseAnyHit ?
            m_SRB->GetVariableByName(SHADER_TYPE_RAY_ANY_HIT, "t_BindlessTextures") :
            nullptr;
        if (pClosestHitTexVar == nullptr || (UseAnyHit && pAnyHitTexVar == nullptr))
        {
            UNEXPECTED("Failed to find RTXPT material texture array binding");
            return false;
        }
        pClosestHitTexVar->SetArray(pMaterialTextures, 0, MaterialTextureCount);
        if (pAnyHitTexVar != nullptr)
            pAnyHitTexVar->SetArray(pMaterialTextures, 0, MaterialTextureCount);
        m_Stats.MaterialTexturesBound = true;
        m_Stats.MaterialTextureCount  = MaterialTextureCount;
    }
    m_Stats.AnyHitEnabled = UseAnyHit;

    ShaderBindingTableDesc SBTDesc;
    SBTDesc.Name = "RTXPT reference SBT";
    SBTDesc.pPSO = m_PSO;
    pDevice->CreateSBT(SBTDesc, &m_SBT);
    VERIFY(m_SBT, "Failed to create RTXPT reference SBT");
    if (!m_SBT)
        return false;

    m_SBT->BindRayGenShader("Main");
    if (!ScreenPatternDiagnostic)
    {
        m_SBT->BindMissShader("PrimaryMiss", 0);
        m_SBT->BindHitGroupForTLAS(m_TLAS, 0, "PrimaryHit");
    }
    pContext->UpdateSBT(m_SBT);

    m_Stats.Ready = true;
    return true;
}

bool RTXPTRayTracingPass::Trace(IDeviceContext* pContext,
                                ITextureView*   pOutputUAV,
                                ITextureView*   pAccumulationUAV,
                                Uint32          Width,
                                Uint32          Height)
{
    m_Stats.LastTraceExecuted = false;
    m_Stats.AccumulationBound = false;

    if (!IsReady())
        return false;
    if (pOutputUAV == nullptr || pAccumulationUAV == nullptr || Width == 0 || Height == 0)
        return false;

    IShaderResourceVariable* pOutputColorVar = m_SRB->GetVariableByName(SHADER_TYPE_RAY_GEN, "u_Output");
    IShaderResourceVariable* pAccumColorVar  = m_SRB->GetVariableByName(SHADER_TYPE_RAY_GEN, "u_AccumulationBuffer");
    if (pOutputColorVar == nullptr || pAccumColorVar == nullptr)
    {
        UNEXPECTED("Failed to find RTXPT output bindings");
        return false;
    }

    pOutputColorVar->Set(pOutputUAV);
    pAccumColorVar->Set(pAccumulationUAV);
    m_Stats.AccumulationBound = true;

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
