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
#include "RTXPTFrameConstants.hpp"

#include "GraphicsTypesX.hpp"
#include "MapHelper.hpp"
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

const char* GetVariantName(RTXPTPathTraceVariant Variant)
{
    switch (Variant)
    {
        case RTXPTPathTraceVariant::Reference: return "Reference";
        case RTXPTPathTraceVariant::BuildStablePlanes: return "BuildStablePlanes";
        case RTXPTPathTraceVariant::FillStablePlanes: return "FillStablePlanes";
        default: return "Unknown";
    }
}

int GetVariantModeMacro(RTXPTPathTraceVariant Variant)
{
    switch (Variant)
    {
        case RTXPTPathTraceVariant::Reference: return 0;
        case RTXPTPathTraceVariant::BuildStablePlanes: return 1;
        case RTXPTPathTraceVariant::FillStablePlanes: return 2;
        default: return 0;
    }
}

size_t GetVariantIndex(RTXPTPathTraceVariant Variant)
{
    return static_cast<size_t>(Variant);
}

} // namespace

void RTXPTRayTracingPass::Reset()
{
    for (VariantState& State : m_Variants)
    {
        State.PSO.Release();
        State.SRB.Release();
        State.SBT.Release();
        State.Stats = {};
    }
    m_MiniConstantsCB.Release();
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

    if (pDevice == nullptr || pContext == nullptr || pEngineFactory == nullptr)
    {
        DEV_ERROR("RTXPT path tracer requires device, context, and engine factory");
        return false;
    }

    if (!RayTracingSupported)
    {
        DEV_ERROR("RTXPT reference path tracer requires ray tracing support");
        return false;
    }

    if (!StandaloneRTShadersSupported)
    {
        DEV_ERROR("RTXPT reference path tracer requires standalone ray tracing shader support");
        return false;
    }

    const RTXPTDiagnosticMode DiagnosticMode            = GetRTXPTDiagnosticMode();
    const bool                ScreenPatternDiagnostic   = DiagnosticMode == RTXPTDiagnosticMode::ScreenPattern;
    const bool                MinimalTraceRayDiagnostic = DiagnosticMode == RTXPTDiagnosticMode::MinimalTraceRay;
    const bool                FullPathTracer            = DiagnosticMode == RTXPTDiagnosticMode::FullPathTracer;

    if (!ScreenPatternDiagnostic && (pFrameConstants == nullptr || pTLAS == nullptr))
    {
        DEV_ERROR("RTXPT reference path tracer requires frame constants and TLAS");
        return false;
    }

    if (FullPathTracer && (pVertexBuffer == nullptr || pSkinnedVertexBuffer == nullptr || pIndexBuffer == nullptr))
    {
        DEV_ERROR("RTXPT reference path tracer requires vertex, skinned vertex, and index buffers");
        return false;
    }

    const bool UseTextures = FullPathTracer && EnableMaterialTextures && pMaterialTextures != nullptr && MaterialTextureCount > 0;
    const bool UseAnyHit   = FullPathTracer && EnableAnyHit;

    m_TLAS = pTLAS;

    BufferDesc MiniCBDesc;
    MiniCBDesc.Name           = "RTXPT SampleMiniConstants";
    MiniCBDesc.Size           = sizeof(SampleMiniConstants);
    MiniCBDesc.BindFlags      = BIND_UNIFORM_BUFFER;
    MiniCBDesc.Usage          = USAGE_DYNAMIC;
    MiniCBDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
    pDevice->CreateBuffer(MiniCBDesc, nullptr, &m_MiniConstantsCB);
    if (!m_MiniConstantsCB)
    {
        DEV_ERROR("Failed to create RTXPT SampleMiniConstants buffer");
        return false;
    }

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    pEngineFactory->CreateDefaultShaderSourceStreamFactory("shaders;shaders\\PathTracer", &pShaderSourceFactory);
    if (!pShaderSourceFactory)
    {
        DEV_ERROR("Failed to create RTXPT shader source factory");
        return false;
    }

    // Material data is read by raygen for nested dielectric media and by hit shaders for surface shading.
    const SHADER_TYPE HitStages = UseAnyHit ?
        (SHADER_TYPE_RAY_CLOSEST_HIT | SHADER_TYPE_RAY_ANY_HIT) :
        SHADER_TYPE_RAY_CLOSEST_HIT;
    const SHADER_TYPE MaterialStages = HitStages | SHADER_TYPE_RAY_GEN;
    const SHADER_TYPE EnvStages      = SHADER_TYPE_RAY_GEN | SHADER_TYPE_RAY_MISS;
    const SHADER_TYPE ConstStages    = EnvStages | HitStages;
    const SHADER_TYPE DynamicStages  = ScreenPatternDiagnostic ?
        SHADER_TYPE_RAY_GEN :
        (SHADER_TYPE_RAY_GEN | SHADER_TYPE_RAY_MISS | HitStages);

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
    else
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
        m_Stats.MaterialTextureCount     = UseTextures ? MaterialTextureCount : 0;
    }

    IDeviceObject* pMaterialsView = pMaterialBuffer != nullptr ? pMaterialBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) : nullptr;
    IDeviceObject* pSubInstanceView =
        pSubInstanceBuffer != nullptr ? pSubInstanceBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) : nullptr;
    IDeviceObject* pLightsView = pLightBuffer != nullptr ? pLightBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) : nullptr;
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

    bool EnvironmentBridgeReflectedAny = false;

    auto CreateVariant = [&](RTXPTPathTraceVariant Variant) {
        VariantState& State = m_Variants[GetVariantIndex(Variant)];

        ShaderCreateInfo ShaderCI;
        ShaderCI.Desc.UseCombinedTextureSamplers = false;
        ShaderCI.SourceLanguage                  = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.ShaderCompiler                  = SHADER_COMPILER_DXC;
        ShaderCI.CompileFlags                    = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
        ShaderCI.HLSLVersion                     = {6, 5};
        ShaderCI.pShaderSourceStreamFactory      = pShaderSourceFactory;

        ShaderMacroHelper Macros;
        Macros.Add("PATH_TRACER_MODE", GetVariantModeMacro(Variant));
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
        ShaderCI.Desc.Name       = "RTXPT path trace raygen";
        ShaderCI.FilePath        = "PathTracer/PathTracerSample.rgen";
        ShaderCI.EntryPoint      = "main";
        pDevice->CreateShader(ShaderCI, &pRayGen);

        RefCntAutoPtr<IShader> pMiss;
        if (!ScreenPatternDiagnostic)
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_MISS;
            ShaderCI.Desc.Name       = "RTXPT path trace miss";
            ShaderCI.FilePath        = "PathTracer/PathTracerMiss.rmiss";
            ShaderCI.EntryPoint      = "main";
            pDevice->CreateShader(ShaderCI, &pMiss);
        }

        RefCntAutoPtr<IShader> pClosestHit;
        if (!ScreenPatternDiagnostic)
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_CLOSEST_HIT;
            ShaderCI.Desc.Name       = "RTXPT path trace closest hit";
            ShaderCI.FilePath        = "PathTracer/PathTracerClosestHit.rchit";
            ShaderCI.EntryPoint      = "main";
            pDevice->CreateShader(ShaderCI, &pClosestHit);
        }

        RefCntAutoPtr<IShader> pAnyHit;
        if (!ScreenPatternDiagnostic && UseAnyHit)
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_ANY_HIT;
            ShaderCI.Desc.Name       = "RTXPT path trace any hit";
            ShaderCI.FilePath        = "PathTracer/PathTracerAnyHit.rahit";
            ShaderCI.EntryPoint      = "main";
            pDevice->CreateShader(ShaderCI, &pAnyHit);
        }

        VERIFY(pRayGen && (ScreenPatternDiagnostic || (pMiss && pClosestHit && (!UseAnyHit || pAnyHit))),
               "Failed to create RTXPT path tracing shaders");
        if (!pRayGen || (!ScreenPatternDiagnostic && (!pMiss || !pClosestHit || (UseAnyHit && !pAnyHit))))
        {
            DEV_ERROR("Failed to create RTXPT path tracing shaders for variant: ", GetVariantName(Variant));
            return false;
        }

        RayTracingPipelineStateCreateInfoX PSOCreateInfo;
        PSOCreateInfo.PSODesc.Name         = "RTXPT path trace RT PSO";
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
        PSOCreateInfo.RayTracingPipeline.MaxRecursionDepth = 1;
        PSOCreateInfo.RayTracingPipeline.ShaderRecordSize  = 0;
        PSOCreateInfo.MaxAttributeSize                     = static_cast<Uint32>(sizeof(float) * 2);
        PSOCreateInfo.MaxPayloadSize                       = static_cast<Uint32>(sizeof(float) * 40);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        ResourceLayout
            .AddVariable(SHADER_TYPE_RAY_GEN, "u_Output", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_RAY_GEN, "u_ScreenMotionVectors", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(DynamicStages, "u_OutputColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(DynamicStages, "u_Depth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(DynamicStages, "u_MotionVectors", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(DynamicStages, "u_Throughput", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(DynamicStages, "u_SpecularHitT", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(DynamicStages, "u_StableRadiance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(DynamicStages, "u_StablePlanesHeader", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(DynamicStages, "u_StablePlanesBuffer", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

        if (!ScreenPatternDiagnostic)
        {
            ResourceLayout
                .AddVariable(ConstStages, "g_Const", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
                .AddVariable(ConstStages, "g_MiniConst", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
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

        pDevice->CreateRayTracingPipelineState(PSOCreateInfo, &State.PSO);
        VERIFY(State.PSO, "Failed to create RTXPT path trace RT PSO");
        if (!State.PSO)
        {
            DEV_ERROR("Failed to create RTXPT path trace RT PSO for variant: ", GetVariantName(Variant));
            return false;
        }

        auto SetStaticForStages = [&](SHADER_TYPE Stages, const char* Name, IDeviceObject* pObject, const char* ObjectName,
                                      bool Required = true, bool* pFoundAny = nullptr) {
            bool Ok       = true;
            bool FoundAny = false;
            for (SHADER_TYPE Stage : {SHADER_TYPE_RAY_GEN, SHADER_TYPE_RAY_MISS, SHADER_TYPE_RAY_CLOSEST_HIT, SHADER_TYPE_RAY_ANY_HIT})
            {
                if ((Stages & Stage) == 0)
                    continue;

                IShaderResourceVariable* pVar = State.PSO->GetStaticVariableByName(Stage, Name);
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
                UNEXPECTED("RTXPT static shader variable is missing: ", Name, " for variant: ", GetVariantName(Variant));
                return false;
            }
            return Ok;
        };

        const bool ReferenceVariant = Variant == RTXPTPathTraceVariant::Reference;
        const bool FrameConstantsBound =
            ScreenPatternDiagnostic || SetStaticForStages(ConstStages, "g_Const", pFrameConstants, "frame constants");
        // BUILD uses the current sub-sample to spawn primary camera rays. FILL starts from the
        // stable-plane state, so DXC may strip g_MiniConst until real scatter sampling is wired.
        const bool MiniConstantsRequired = Variant == RTXPTPathTraceVariant::BuildStablePlanes;
        const bool MiniConstantsBound =
            ScreenPatternDiagnostic ||
            SetStaticForStages(ConstStages | HitStages, "g_MiniConst", m_MiniConstantsCB, "mini constants", MiniConstantsRequired);
        const bool TLASBound =
            ScreenPatternDiagnostic || SetStaticForStages(SHADER_TYPE_RAY_GEN, "t_SceneBVH", m_TLAS, "TLAS");

        if (!FrameConstantsBound || !MiniConstantsBound || !TLASBound)
            return false;

        if (FullPathTracer)
        {
            const bool MaterialBridgeBound = SetStaticForStages(MaterialStages, "t_PTMaterialData", pMaterialsView, "material buffer");
            const bool SubInstanceBound    = SetStaticForStages(HitStages, "t_SubInstanceData", pSubInstanceView, "sub-instance buffer");
            // Stable-plane BUILD/FILL variants currently do not run direct-light NEE, so DXC strips
            // the light/feedback/emissive resources from those variants. Keep them mandatory for the
            // reference path and bind them opportunistically when a realtime variant actually reflects them.
            const bool LightBridgeBound =
                SetStaticForStages(SHADER_TYPE_RAY_GEN, "t_Lights", pLightsView, "light buffer", ReferenceVariant);
            const bool LightsBakerBridgeBound =
                SetStaticForStages(SHADER_TYPE_RAY_GEN, "t_LightingControl", pLightingControlView, "LightsBaker control buffer", ReferenceVariant) &&
                SetStaticForStages(SHADER_TYPE_RAY_GEN, "t_LightProxyCounters", pLightProxyCountersView, "LightsBaker proxy counters", ReferenceVariant) &&
                SetStaticForStages(SHADER_TYPE_RAY_GEN, "t_LightSamplingProxies", pLightSamplingProxiesView, "LightsBaker sampling proxies", ReferenceVariant) &&
                SetStaticForStages(SHADER_TYPE_RAY_GEN, "t_LocalSamplingBuffer", pLocalSamplingView, "LightsBaker local sampling buffer", ReferenceVariant) &&
                SetStaticForStages(SHADER_TYPE_RAY_GEN, "u_FeedbackTotalWeight", pFeedbackTotalWeightUAV, "LightsBaker feedback total weight", ReferenceVariant) &&
                SetStaticForStages(SHADER_TYPE_RAY_GEN, "u_FeedbackCandidates", pFeedbackCandidatesUAV, "LightsBaker feedback candidates", ReferenceVariant);
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
            EnvironmentBridgeReflectedAny = EnvironmentBridgeReflectedAny || EnvironmentBridgeReflected;
            const bool EmissiveLightBridgeBound =
                SetStaticForStages(SHADER_TYPE_RAY_GEN, "t_EmissiveTriangles", pEmissiveView, "emissive triangle buffer", ReferenceVariant);
            const bool VertexBufferBound = SetStaticForStages(HitStages, "t_VertexBuffer", pVertexView, "vertex buffer");
            const bool SkinnedVertexBufferBound =
                SetStaticForStages(HitStages, "t_SkinnedVertexBuffer", pSkinnedVertexView, "skinned vertex buffer");
            const bool IndexBufferBound = SetStaticForStages(HitStages, "t_IndexBuffer", m_IndexBufferView, "index buffer");

            m_Stats.MaterialBridgeBound      = m_Stats.MaterialBridgeBound && MaterialBridgeBound;
            m_Stats.SubInstanceBound         = m_Stats.SubInstanceBound && SubInstanceBound;
            m_Stats.LightBridgeBound         = m_Stats.LightBridgeBound && LightBridgeBound;
            m_Stats.LightsBakerBridgeBound   = m_Stats.LightsBakerBridgeBound && LightsBakerBridgeBound;
            m_Stats.EnvironmentBridgeBound   = m_Stats.EnvironmentBridgeBound && (!EnvironmentBridgeReflected || EnvironmentBridgeOk);
            m_Stats.EmissiveLightBridgeBound = m_Stats.EmissiveLightBridgeBound && EmissiveLightBridgeBound;
            m_Stats.VertexBufferBound        = m_Stats.VertexBufferBound && VertexBufferBound;
            m_Stats.SkinnedVertexBufferBound = m_Stats.SkinnedVertexBufferBound && SkinnedVertexBufferBound;
            m_Stats.IndexBufferBound         = m_Stats.IndexBufferBound && IndexBufferBound;

            if (!MaterialBridgeBound || !SubInstanceBound || !LightBridgeBound || !LightsBakerBridgeBound ||
                (EnvironmentBridgeReflected && !EnvironmentBridgeOk) || !EmissiveLightBridgeBound ||
                !VertexBufferBound || !SkinnedVertexBufferBound || !IndexBufferBound)
                return false;
        }

        State.PSO->CreateShaderResourceBinding(&State.SRB, true);
        VERIFY(State.SRB, "Failed to create RTXPT path trace RT SRB");
        if (!State.SRB)
        {
            DEV_ERROR("Failed to create RTXPT path trace RT SRB for variant: ", GetVariantName(Variant));
            return false;
        }

        if (!ScreenPatternDiagnostic && UseTextures)
        {
            IShaderResourceVariable* pClosestHitTexVar =
                State.SRB->GetVariableByName(SHADER_TYPE_RAY_CLOSEST_HIT, "t_BindlessTextures");
            IShaderResourceVariable* pAnyHitTexVar = UseAnyHit ?
                State.SRB->GetVariableByName(SHADER_TYPE_RAY_ANY_HIT, "t_BindlessTextures") :
                nullptr;
            if (pClosestHitTexVar == nullptr || (UseAnyHit && pAnyHitTexVar == nullptr))
            {
                UNEXPECTED("Failed to find RTXPT material texture array binding for variant: ", GetVariantName(Variant));
                m_Stats.MaterialTexturesBound = false;
                return false;
            }
            pClosestHitTexVar->SetArray(pMaterialTextures, 0, MaterialTextureCount);
            if (pAnyHitTexVar != nullptr)
                pAnyHitTexVar->SetArray(pMaterialTextures, 0, MaterialTextureCount);
        }

        ShaderBindingTableDesc SBTDesc;
        SBTDesc.Name = "RTXPT path trace SBT";
        SBTDesc.pPSO = State.PSO;
        pDevice->CreateSBT(SBTDesc, &State.SBT);
        VERIFY(State.SBT, "Failed to create RTXPT path trace SBT");
        if (!State.SBT)
        {
            DEV_ERROR("Failed to create RTXPT path trace SBT for variant: ", GetVariantName(Variant));
            return false;
        }

        State.SBT->BindRayGenShader("Main");
        if (!ScreenPatternDiagnostic)
        {
            State.SBT->BindMissShader("PrimaryMiss", 0);
            State.SBT->BindHitGroupForTLAS(m_TLAS, 0, "PrimaryHit");
        }
        pContext->UpdateSBT(State.SBT);

        State.Stats.Ready = true;
        return true;
    };

    const RTXPTPathTraceVariant Variants[] =
        {
            RTXPTPathTraceVariant::Reference,
            RTXPTPathTraceVariant::BuildStablePlanes,
            RTXPTPathTraceVariant::FillStablePlanes,
        };
    for (RTXPTPathTraceVariant Variant : Variants)
    {
        if (!CreateVariant(Variant))
        {
            Reset();
            return false;
        }
    }

    if (FullPathTracer)
    {
        m_Stats.EnvironmentBridgeBound = EnvironmentBridgeReflectedAny && m_Stats.EnvironmentBridgeBound;
        if (!UseTextures)
            m_Stats.MaterialTexturesBound = false;
    }
    m_Stats.AnyHitEnabled = UseAnyHit;
    m_Stats.Ready         = true;
    return true;
}

bool RTXPTRayTracingPass::Dispatch(IDeviceContext*                pContext,
                                   RTXPTPathTraceVariant          Variant,
                                   const RTXPTRayTracingDispatch& DispatchInfo)
{
    m_Stats.LastTraceExecuted = false;

    const size_t VariantIdx = GetVariantIndex(Variant);
    if (VariantIdx >= m_Variants.size())
    {
        DEV_ERROR("RTXPT dispatch received invalid path trace variant");
        return false;
    }

    VariantState& State           = m_Variants[VariantIdx];
    State.Stats.LastTraceExecuted = false;

    if (!IsReady(Variant))
    {
        DEV_ERROR("RTXPT path trace variant is not ready: ", GetVariantName(Variant));
        return false;
    }
    if (pContext == nullptr)
        return false;
    const bool ReferenceVariant         = Variant == RTXPTPathTraceVariant::Reference;
    const bool BuildStablePlanesVariant = Variant == RTXPTPathTraceVariant::BuildStablePlanes;
    const bool RealtimeVariant          = !ReferenceVariant;
    const bool FinalOutputRequired      = ReferenceVariant;
    const bool SurfaceExportsRequired   = ReferenceVariant || BuildStablePlanesVariant;
    const bool ThroughputRequired       = BuildStablePlanesVariant;
    const bool SpecularHitTRequired     = RealtimeVariant;
    const bool StableRadianceRequired   = BuildStablePlanesVariant;
    const bool StablePlanesRequired     = RealtimeVariant;

    if (DispatchInfo.Width == 0 || DispatchInfo.Height == 0)
        return false;
    if ((FinalOutputRequired && DispatchInfo.pOutputColorUAV == nullptr) ||
        (SurfaceExportsRequired &&
         (DispatchInfo.pDepthUAV == nullptr ||
          DispatchInfo.pMotionVectorsUAV == nullptr)) ||
        (ThroughputRequired && DispatchInfo.pThroughputUAV == nullptr) ||
        (SpecularHitTRequired && DispatchInfo.pSpecularHitTUAV == nullptr) ||
        (StableRadianceRequired && DispatchInfo.pStableRadianceUAV == nullptr) ||
        (StablePlanesRequired &&
         (DispatchInfo.pStablePlanesHeaderUAV == nullptr ||
          DispatchInfo.pStablePlanesBufferUAV == nullptr)))
        return false;

    auto SetDynamicForStages = [&](const char* Name, IDeviceObject* pObject, bool Required, bool* pFoundAny = nullptr) {
        bool FoundAny = false;
        bool Ok       = true;
        for (SHADER_TYPE Stage : {SHADER_TYPE_RAY_GEN, SHADER_TYPE_RAY_MISS, SHADER_TYPE_RAY_CLOSEST_HIT, SHADER_TYPE_RAY_ANY_HIT})
        {
            IShaderResourceVariable* pVar = State.SRB->GetVariableByName(Stage, Name);
            if (pVar == nullptr)
                continue;
            FoundAny = true;

            if (pObject == nullptr)
            {
                if (Required)
                {
                    DEV_ERROR("RTXPT dynamic resource object is null: ", Name, " for variant: ", GetVariantName(Variant));
                    Ok = false;
                }
                continue;
            }

            pVar->Set(pObject);
        }

        if (pFoundAny != nullptr)
            *pFoundAny = FoundAny;

        if (!FoundAny && Required)
        {
            UNEXPECTED("RTXPT dynamic shader variable is missing: ", Name, " for variant: ", GetVariantName(Variant));
            return false;
        }

        return Ok;
    };

    // Compatibility bridge: Reference currently reflects legacy output names, while realtime variants use source-compatible names.
    auto SetDynamicForNamePair = [&](const char* SourceName, const char* LegacyName, IDeviceObject* pObject, bool Required) {
        bool       SourceFound = false;
        bool       LegacyFound = false;
        const bool SourceOk    = SetDynamicForStages(SourceName, pObject, false, &SourceFound);
        const bool LegacyOk    = SetDynamicForStages(LegacyName, pObject, false, &LegacyFound);
        if (!SourceOk || !LegacyOk)
            return false;
        if (!SourceFound && !LegacyFound && Required)
        {
            UNEXPECTED("RTXPT dynamic shader variable is missing: ",
                       SourceName, " or ", LegacyName,
                       " for variant: ", GetVariantName(Variant));
            return false;
        }
        return true;
    };

    if (!SetDynamicForNamePair("u_OutputColor", "u_Output", DispatchInfo.pOutputColorUAV, FinalOutputRequired) ||
        !SetDynamicForStages("u_Depth", DispatchInfo.pDepthUAV, SurfaceExportsRequired) ||
        !SetDynamicForNamePair("u_MotionVectors", "u_ScreenMotionVectors", DispatchInfo.pMotionVectorsUAV, SurfaceExportsRequired) ||
        !SetDynamicForStages("u_Throughput", DispatchInfo.pThroughputUAV, ThroughputRequired) ||
        !SetDynamicForStages("u_SpecularHitT", DispatchInfo.pSpecularHitTUAV, SpecularHitTRequired) ||
        !SetDynamicForStages("u_StableRadiance", DispatchInfo.pStableRadianceUAV, StableRadianceRequired) ||
        !SetDynamicForStages("u_StablePlanesHeader", DispatchInfo.pStablePlanesHeaderUAV, StablePlanesRequired) ||
        !SetDynamicForStages("u_StablePlanesBuffer", DispatchInfo.pStablePlanesBufferUAV, StablePlanesRequired))
    {
        return false;
    }

    SampleMiniConstants Mini = DispatchInfo.pMiniConstants != nullptr ? *DispatchInfo.pMiniConstants : SampleMiniConstants{};
    {
        MapHelper<SampleMiniConstants> Mapped{pContext, m_MiniConstantsCB, MAP_WRITE, MAP_FLAG_DISCARD};
        if (Mapped == nullptr)
        {
            DEV_ERROR("Failed to map RTXPT SampleMiniConstants buffer");
            return false;
        }
        *Mapped = Mini;
    }

    pContext->SetPipelineState(State.PSO);
    pContext->CommitShaderResources(State.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    TraceRaysAttribs Attribs;
    Attribs.DimensionX = DispatchInfo.Width;
    Attribs.DimensionY = DispatchInfo.Height;
    Attribs.pSBT       = State.SBT;
    pContext->TraceRays(Attribs);

    State.Stats.LastTraceExecuted = true;
    ++State.Stats.TraceCount;
    m_Stats.LastTraceExecuted = true;
    ++m_Stats.TraceCount;
    return true;
}

bool RTXPTRayTracingPass::Trace(IDeviceContext* pContext,
                                ITextureView*   pOutputUAV,
                                ITextureView*   pDepthUAV,
                                ITextureView*   pScreenMotionVectorsUAV,
                                Uint32          Width,
                                Uint32          Height)
{
    RTXPTRayTracingDispatch DispatchInfo;
    DispatchInfo.pOutputColorUAV   = pOutputUAV;
    DispatchInfo.pDepthUAV         = pDepthUAV;
    DispatchInfo.pMotionVectorsUAV = pScreenMotionVectorsUAV;
    DispatchInfo.Width             = Width;
    DispatchInfo.Height            = Height;
    return Dispatch(pContext, RTXPTPathTraceVariant::Reference, DispatchInfo);
}

void RTXPTRayTracingPass::InsertUAVBarrier(IDeviceContext* pContext, ITextureView* pTextureUAV)
{
    if (pContext == nullptr || pTextureUAV == nullptr || pTextureUAV->GetTexture() == nullptr)
        return;

    StateTransitionDesc Barrier{pTextureUAV->GetTexture(),
                                RESOURCE_STATE_UNORDERED_ACCESS,
                                RESOURCE_STATE_UNORDERED_ACCESS,
                                STATE_TRANSITION_FLAG_UPDATE_STATE};
    pContext->TransitionResourceState(Barrier);
}

void RTXPTRayTracingPass::InsertUAVBarrier(IDeviceContext* pContext, IBuffer* pBuffer)
{
    if (pContext == nullptr || pBuffer == nullptr)
        return;

    StateTransitionDesc Barrier{pBuffer,
                                RESOURCE_STATE_UNORDERED_ACCESS,
                                RESOURCE_STATE_UNORDERED_ACCESS,
                                STATE_TRANSITION_FLAG_UPDATE_STATE};
    pContext->TransitionResourceState(Barrier);
}

void RTXPTRayTracingPass::InsertUAVBarrier(IDeviceContext* pContext, IBufferView* pBufferUAV)
{
    InsertUAVBarrier(pContext, pBufferUAV != nullptr ? pBufferUAV->GetBuffer() : nullptr);
}

bool RTXPTRayTracingPass::IsReady(RTXPTPathTraceVariant Variant) const
{
    const size_t VariantIdx = GetVariantIndex(Variant);
    return VariantIdx < m_Variants.size() && m_Variants[VariantIdx].Stats.Ready;
}

const RTXPTRayTracingVariantStats& RTXPTRayTracingPass::GetVariantStats(RTXPTPathTraceVariant Variant) const
{
    const size_t VariantIdx = GetVariantIndex(Variant);
    if (VariantIdx < m_Variants.size())
        return m_Variants[VariantIdx].Stats;

    static constexpr RTXPTRayTracingVariantStats EmptyStats{};
    return EmptyStats;
}

} // namespace Diligent
