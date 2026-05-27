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

#include "RTXPTSample.hpp"
#include "GraphicsAccessories.hpp"
#include "GraphicsUtilities.h"
#include "FileSystem.hpp"
#include "MapHelper.hpp"
#include "imgui.h"

namespace Diligent
{

namespace
{

RTXPTFeatureCaps MakeFeatureCaps(const IRenderDevice* pDevice)
{
    RTXPTFeatureCaps Caps{};
    const auto&      DevInfo = pDevice->GetDeviceInfo();
    const auto&      RTProps = pDevice->GetAdapterInfo().RayTracing;

    Caps.RayTracing        = DevInfo.Features.RayTracing == DEVICE_FEATURE_STATE_ENABLED;
    Caps.BindlessResources = DevInfo.Features.BindlessResources == DEVICE_FEATURE_STATE_ENABLED;
    Caps.ComputeShaders    = DevInfo.Features.ComputeShaders == DEVICE_FEATURE_STATE_ENABLED;

    Caps.StandaloneRayTracingShaders =
        Caps.RayTracing &&
        (RTProps.CapFlags & RAY_TRACING_CAP_FLAG_STANDALONE_SHADERS) != 0;

    Caps.RayQuery =
        Caps.RayTracing &&
        (RTProps.CapFlags & RAY_TRACING_CAP_FLAG_INLINE_RAY_TRACING) != 0;

    // TODO(RTXPT-Port Phase 5): replace optimistic compiler flags with explicit DXC/ShaderMake availability checks.
    Caps.DXILCompiler  = true;
    Caps.SPIRVCompiler = true;

    return Caps;
}

std::string JoinPath(const std::string& Root, const char* RelativePath)
{
    if (Root.empty())
        return RelativePath;

    std::string Path = Root;
    if (!FileSystem::IsSlash(Path.back()))
        Path.push_back(FileSystem::SlashSymbol);
    Path += RelativePath;
    FileSystem::CorrectSlashes(Path);
    return FileSystem::SimplifyPath(Path.c_str());
}

bool IsRTXPTAssetsRoot(const std::string& Path)
{
    const std::string ScenePath = JoinPath(Path, "bistro-programmer-art.scene.json");
    const std::string ModelPath = JoinPath(Path, "Models/Bistro/bistro.gltf");
    return FileSystem::FileExists(ScenePath.c_str()) && FileSystem::FileExists(ModelPath.c_str());
}

std::string ResolveRTXPTAssetsRoot()
{
    const char* DirectCandidates[] =
        {
            "assets",
            "Samples/RTXPT/assets",
            "DiligentSamples/Samples/RTXPT/assets",
        };

    for (const char* Candidate : DirectCandidates)
    {
        const std::string Path = FileSystem::SimplifyPath(Candidate);
        if (IsRTXPTAssetsRoot(Path))
            return Path;
    }

    const char* AncestorPrefixes[] =
        {
            "../",
            "../../",
            "../../../",
            "../../../../",
            "../../../../../",
            "../../../../../../",
            "../../../../../../../",
            "../../../../../../../../",
        };
    const char* SourceTreeSuffixes[] =
        {
            "Samples/RTXPT/assets",
            "DiligentSamples/Samples/RTXPT/assets",
        };

    for (const char* Prefix : AncestorPrefixes)
    {
        for (const char* Suffix : SourceTreeSuffixes)
        {
            const std::string Path = FileSystem::SimplifyPath((std::string{Prefix} + Suffix).c_str());
            if (IsRTXPTAssetsRoot(Path))
                return Path;
        }
    }

    return FileSystem::SimplifyPath("DiligentSamples/Samples/RTXPT/assets");
}

} // namespace

SampleBase* CreateSample()
{
    return new RTXPTSample();
}

void RTXPTSample::CreateFrameResources()
{
    CreateUniformBuffer(m_pDevice, sizeof(RTXPTFrameConstants), "RTXPT frame constants", &m_FrameConstantsCB);
}

void RTXPTSample::Initialize(const SampleInitInfo& InitInfo)
{
    SampleBase::Initialize(InitInfo);
    m_FeatureCaps = MakeFeatureCaps(m_pDevice);
    CreateFrameResources();

    m_AssetsRoot = ResolveRTXPTAssetsRoot();
    m_Scene.LoadDefaultScene(m_pDevice, m_pImmediateContext, m_AssetsRoot);
    if (const GLTF::Model* pModel = m_Scene.GetModel())
    {
        m_Materials.Upload(m_pDevice, *pModel);
        if (m_Scene.GetSceneIndex() < pModel->Scenes.size())
            m_Lights.Upload(m_pDevice, pModel->Scenes[m_Scene.GetSceneIndex()], m_Scene.GetTransforms());

        m_AccelerationStructures.BuildStaticScene(m_pDevice,
                                                  m_pImmediateContext,
                                                  *pModel,
                                                  m_Scene.GetSceneIndex(),
                                                  m_Scene.GetIndexType(),
                                                  m_Scene.GetTransforms(),
                                                  m_FeatureCaps.RayTracing);
    }
    else
    {
        m_AccelerationStructures.Reset();
    }

    CreatePhase4Passes();
    EnsureRenderTargets();
}

void RTXPTSample::UpdateFrameConstants(double CurrTime)
{
    const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
    const float          Width  = static_cast<float>(SCDesc.Width);
    const float          Height = static_cast<float>(SCDesc.Height);

    const float3   CameraPosition = float3{0.0f, 1.5f, -6.0f};
    const float4x4 CameraView     = float4x4::Translation(-CameraPosition.x, -CameraPosition.y, -CameraPosition.z);
    const float4x4 CameraProj     = GetAdjustedProjectionMatrix(PI_F / 4.0f, 0.1f, 10000.0f);
    const float4x4 ViewProj       = CameraView * CameraProj;

    m_LastFrameConstants.ViewProj              = ViewProj;
    m_LastFrameConstants.ViewProjInv           = ViewProj.Inverse();
    m_LastFrameConstants.CameraPosition_Time   = float4{CameraPosition.x, CameraPosition.y, CameraPosition.z, static_cast<float>(CurrTime)};
    m_LastFrameConstants.ViewportSize_FrameIdx = float4{Width, Height, Width > 0.0f ? 1.0f / Width : 0.0f, static_cast<float>(m_FrameIndex)};

    if (m_FrameConstantsCB)
    {
        MapHelper<RTXPTFrameConstants> Constants{m_pImmediateContext, m_FrameConstantsCB, MAP_WRITE, MAP_FLAG_DISCARD};
        *Constants = m_LastFrameConstants;
    }

    ++m_FrameIndex;
}

void RTXPTSample::CreatePhase4Passes()
{
    m_BlitPass.Initialize(m_pDevice, m_pEngineFactory, m_pSwapChain);

    m_RayTracingPass.Initialize(m_pDevice,
                                m_pImmediateContext,
                                m_pEngineFactory,
                                m_FrameConstantsCB,
                                m_AccelerationStructures.GetTLAS(),
                                m_FeatureCaps.RayTracing,
                                m_FeatureCaps.StandaloneRayTracingShaders);

    m_DebugComputePass.Initialize(m_pDevice,
                                  m_pEngineFactory,
                                  "RTXPT debug compute pass",
                                  "RTXPTDebugCompute.csh",
                                  m_FrameConstantsCB,
                                  m_FeatureCaps.ComputeShaders);
}

bool RTXPTSample::EnsureRenderTargets()
{
    const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
    return m_RenderTargets.Resize(m_pDevice,
                                  SCDesc.Width,
                                  SCDesc.Height,
                                  TEX_FORMAT_RGBA8_UNORM,
                                  m_FeatureCaps.ComputeShaders);
}

void RTXPTSample::ClearFallback(const float4& ClearColor)
{
    ITextureView* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
    m_pImmediateContext->SetRenderTargets(1, &pRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_pImmediateContext->ClearRenderTarget(pRTV, ClearColor.Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void RTXPTSample::Render()
{
    const auto ClearColor = float4{0.05f, 0.05f, 0.07f, 1.0f};

    if (!EnsureRenderTargets())
    {
        ClearFallback(ClearColor);
        return;
    }

    const bool TraceExecuted =
        m_RayTracingPass.Trace(m_pImmediateContext,
                               m_RenderTargets.GetOutputColorUAV(),
                               m_RenderTargets.GetWidth(),
                               m_RenderTargets.GetHeight());

    if (!TraceExecuted)
    {
        ClearFallback(ClearColor);
        return;
    }

    const bool ComputeExecuted =
        m_EnableDebugComputePass &&
        m_DebugComputePass.Dispatch(m_pImmediateContext,
                                    m_RenderTargets.GetOutputColorSRV(),
                                    m_RenderTargets.GetComputeColorUAV(),
                                    m_RenderTargets.GetWidth(),
                                    m_RenderTargets.GetHeight());

    ITextureView* pDisplaySRV = m_RenderTargets.GetDisplaySRV(ComputeExecuted);
    if (!m_BlitPass.Render(m_pImmediateContext, m_pSwapChain, pDisplaySRV))
    {
        ClearFallback(ClearColor);
        return;
    }
}

void RTXPTSample::Update(double CurrTime, double ElapsedTime, bool DoUpdateUI)
{
    SampleBase::Update(CurrTime, ElapsedTime, DoUpdateUI);
    m_Scene.Update(CurrTime, ElapsedTime);
    UpdateFrameConstants(CurrTime);
}

void RTXPTSample::WindowResize(Uint32 Width, Uint32 Height)
{
    if (Width == 0 || Height == 0)
        return;

    m_RenderTargets.Resize(m_pDevice,
                           Width,
                           Height,
                           TEX_FORMAT_RGBA8_UNORM,
                           m_FeatureCaps.ComputeShaders);
}

void RTXPTSample::UpdateUI()
{
    ImGui::Begin("RTXPT Status");
    ImGui::Text("Backend: %s", GetRenderDeviceTypeString(m_pDevice->GetDeviceInfo().Type));
    ImGui::Text("RayTracing: %s", m_FeatureCaps.RayTracing ? "yes" : "no");
    ImGui::Text("Standalone RT shaders: %s", m_FeatureCaps.StandaloneRayTracingShaders ? "yes" : "no");
    ImGui::Text("RayQuery: %s", m_FeatureCaps.RayQuery ? "yes" : "no");
    ImGui::Text("Bindless: %s", m_FeatureCaps.BindlessResources ? "yes" : "no");
    ImGui::Text("Compute: %s", m_FeatureCaps.ComputeShaders ? "yes" : "no");
    ImGui::Separator();
    ImGui::Text("Assets root: %s", m_AssetsRoot.c_str());
    ImGui::Text("Scene: %s", m_Scene.HasValidContent() ? "loaded" : "missing");
    ImGui::Text("Scene file: %s", m_Scene.GetLoadedSceneName().empty() ? "none" : m_Scene.GetLoadedSceneName().c_str());
    ImGui::Text("Model path: %s", m_Scene.GetModelPath().empty() ? "none" : m_Scene.GetModelPath().c_str());
    if (!m_Scene.GetLastError().empty())
        ImGui::TextWrapped("Asset load error: %s", m_Scene.GetLastError().c_str());
    ImGui::Text("Mesh nodes: %u", m_Scene.GetMeshNodeCount());
    ImGui::Text("Primitives: %u", m_Scene.GetPrimitiveCount());
    ImGui::Text("Materials: %u", m_Materials.GetStats().MaterialCount);
    ImGui::Text("Lights: %u", m_Lights.GetStats().LightCount);
    if (!m_Materials.GetStats().LastError.empty())
        ImGui::TextWrapped("Material buffer error: %s", m_Materials.GetStats().LastError.c_str());
    if (!m_Lights.GetStats().LastError.empty())
        ImGui::TextWrapped("Light buffer error: %s", m_Lights.GetStats().LastError.c_str());
    const RTXPTAccelerationStructureStats& ASStats = m_AccelerationStructures.GetStats();
    ImGui::Separator();
    ImGui::Text("Acceleration structures: %s", m_AccelerationStructures.IsBuilt() ? "built" : "not built");
    ImGui::Text("BLAS: %u", ASStats.BLASCount);
    ImGui::Text("TLAS instances: %u", ASStats.InstanceCount);
    ImGui::Text("RT geometries: %u", ASStats.GeometryCount);
    if (!ASStats.DisabledReason.empty())
        ImGui::TextWrapped("AS disabled: %s", ASStats.DisabledReason.c_str());
    if (!ASStats.LastError.empty())
        ImGui::TextWrapped("AS error: %s", ASStats.LastError.c_str());
    ImGui::Separator();
    ImGui::Text("Frame constants: %s", m_FrameConstantsCB ? "created" : "missing");
    ImGui::Text("Frame index: %u", m_FrameIndex);
    ImGui::Text("Viewport: %.0f x %.0f", m_LastFrameConstants.ViewportSize_FrameIdx.x, m_LastFrameConstants.ViewportSize_FrameIdx.y);
    const RTXPTRayTracingPassStats& RTPassStats  = m_RayTracingPass.GetStats();
    const RTXPTComputePassStats&    ComputeStats = m_DebugComputePass.GetStats();
    ImGui::Separator();
    ImGui::Text("OutputColor: %s", m_RenderTargets.IsValid() ? "created" : "missing");
    ImGui::Text("TraceRays pass: %s", m_RayTracingPass.IsReady() ? "ready" : "not ready");
    ImGui::Text("TraceRays executed: %s", RTPassStats.LastTraceExecuted ? "yes" : "no");
    ImGui::Text("TraceRays count: %u", RTPassStats.TraceCount);
    if (!RTPassStats.DisabledReason.empty())
        ImGui::TextWrapped("TraceRays disabled: %s", RTPassStats.DisabledReason.c_str());
    if (!RTPassStats.LastError.empty())
        ImGui::TextWrapped("TraceRays error: %s", RTPassStats.LastError.c_str());
    ImGui::Checkbox("Debug compute pass", &m_EnableDebugComputePass);
    ImGui::Text("Compute dispatch: %s", m_DebugComputePass.IsReady() ? "ready" : "not ready");
    ImGui::Text("Compute executed: %s", ComputeStats.LastDispatchExecuted ? "yes" : "no");
    ImGui::Text("Compute dispatch count: %u", ComputeStats.DispatchCount);
    if (!ComputeStats.DisabledReason.empty())
        ImGui::TextWrapped("Compute disabled: %s", ComputeStats.DisabledReason.c_str());
    if (!ComputeStats.LastError.empty())
        ImGui::TextWrapped("Compute error: %s", ComputeStats.LastError.c_str());
    if (!m_RenderTargets.GetLastError().empty())
        ImGui::TextWrapped("Render target error: %s", m_RenderTargets.GetLastError().c_str());
    if (!m_BlitPass.GetLastError().empty())
        ImGui::TextWrapped("Blit error: %s", m_BlitPass.GetLastError().c_str());
    ImGui::Text("Blit draw count: %u", m_BlitPass.GetDrawCount());
    ImGui::Text("TODO(RTXPT-Port Phase 1): add backend-specific warnings and fallback explanations.");
    ImGui::Text("TODO(RTXPT-Port Phase 4): expose stable-plane, RTXDI, light feedback, and denoising-guide pass toggles after their shaders are ported.");
    ImGui::End();
}

} // namespace Diligent
