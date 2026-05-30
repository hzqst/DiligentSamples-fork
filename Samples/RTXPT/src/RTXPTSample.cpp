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

constexpr float kDefaultCameraNearPlane      = 1.0f;
constexpr float kMinClipPlaneSeparation      = 1e-3f;

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

void SanitizeCameraClipPlanes(float& NearPlane, float& FarPlane)
{
    if (!(NearPlane > 0.0f))
        NearPlane = kDefaultCameraNearPlane;

    if (!(FarPlane > NearPlane))
        FarPlane = NearPlane + kMinClipPlaneSeparation;
}

} // namespace

SampleBase* CreateSample()
{
    return new RTXPTSample();
}

void RTXPTSample::ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs)
{
    SampleBase::ModifyEngineInitInfo(Attribs);

#ifdef DILIGENT_DEBUG
    Attribs.EngineCI.EnableValidation = true;
    Attribs.EngineCI.SetValidationLevel(VALIDATION_LEVEL_2);
#endif
}

void RTXPTSample::CreateFrameResources()
{
    CreateUniformBuffer(m_pDevice, sizeof(SampleConstants), "RTXPT frame constants", &m_FrameConstantsCB);
}

void RTXPTSample::InitializeCamera()
{
    m_Camera.SetPos(float3{0.0f, 1.5f, -6.0f});
    m_Camera.SetLookAt(float3{0.0f, 1.5f, 0.0f});
    m_Camera.SetRotationSpeed(0.005f);
    m_Camera.SetMoveSpeed(5.0f);
    m_Camera.SetSpeedUpScales(5.0f, 10.0f);

    const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
    UpdateCameraProjection(SCDesc.Width, SCDesc.Height);
    m_Camera.Update(m_InputController, 0.0f);

    m_LastCameraView        = m_Camera.GetViewMatrix();
    m_LastCameraProj        = m_Camera.GetProjMatrix();
    m_HasLastCameraMatrices = true;
}

void RTXPTSample::UpdateCameraProjection(Uint32 Width, Uint32 Height)
{
    if (Width == 0 || Height == 0)
        return;

    const float AspectRatio = static_cast<float>(Width) / static_cast<float>(Height);
    m_Camera.SetProjAttribs(m_CameraNearPlane,
                            m_CameraFarPlane,
                            AspectRatio,
                            m_CameraVerticalFov,
                            m_pSwapChain->GetDesc().PreTransform,
                            m_pDevice->GetDeviceInfo().NDC.MinZ == -1);
}

bool RTXPTSample::ApplySceneCamera(Uint32 CameraIndex)
{
    const RTXPTSceneCamera* pCamera = m_Scene.GetCamera(CameraIndex);
    if (pCamera == nullptr)
        return false;

    float3      Forward       = pCamera->Rotation.RotateVector(float3{0.0f, 0.0f, -1.0f});
    const float ForwardLength = length(Forward);
    Forward                   = ForwardLength > 1e-5f ? Forward / ForwardLength : float3{0.0f, 0.0f, -1.0f};

    m_SelectedSceneCamera = static_cast<int>(CameraIndex);
    m_CameraVerticalFov   = pCamera->VerticalFov;
    if (pCamera->HasExplicitClipPlanes)
    {
        m_CameraNearPlane = pCamera->NearPlane;
        m_CameraFarPlane  = pCamera->FarPlane;
    }

    m_Camera.SetPos(pCamera->Position);
    m_Camera.SetLookAt(pCamera->Position + Forward);

    const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
    UpdateCameraProjection(SCDesc.Width, SCDesc.Height);
    m_Camera.Update(m_InputController, 0.0f);

    m_HasLastCameraMatrices = false;
    RequestAccumulationReset("Scene camera changed");
    return true;
}

void RTXPTSample::Initialize(const SampleInitInfo& InitInfo)
{
    SampleBase::Initialize(InitInfo);
    m_FeatureCaps = MakeFeatureCaps(m_pDevice);
    InitializeCamera();
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

    if (m_Scene.GetCameraCount() > 0)
        ApplySceneCamera(0);

    CreatePhase4Passes();
    EnsureRenderTargets();
}

void RTXPTSample::UpdateFrameConstants(double CurrTime)
{
    const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
    const float          Width  = static_cast<float>(SCDesc.Width);
    const float          Height = static_cast<float>(SCDesc.Height);

    const float3   CameraPosition = m_Camera.GetPos();
    const float4x4 CameraView     = m_Camera.GetViewMatrix();
    const float4x4 CameraProj     = m_Camera.GetProjMatrix();
    const float4x4 ViewProj       = CameraView * CameraProj;

    m_LastFrameConstants.viewProj                  = ViewProj;
    m_LastFrameConstants.viewProjInv               = ViewProj.Inverse();
    m_LastFrameConstants.cameraPositionAndTime     = float4{CameraPosition.x, CameraPosition.y, CameraPosition.z, static_cast<float>(CurrTime)};
    m_LastFrameConstants.viewportSizeAndFrameIndex = float4{Width, Height, Width > 0.0f ? 1.0f / Width : 0.0f, static_cast<float>(m_FrameIndex)};

    if (m_AccumulationActive)
    {
        if (m_ResetAccumulationPending)
            m_AccumulationFrame = 1;
        else
            ++m_AccumulationFrame;
    }
    else
    {
        m_AccumulationFrame = 0;
    }

    m_LastFrameConstants.ptConsts.bounceCount           = m_MaxBounces;
    m_LastFrameConstants.ptConsts.sampleIndex           = m_AccumulationFrame;
    m_LastFrameConstants.ptConsts.resetAccumulation     = m_ResetAccumulationPending ? 1u : 0u;
    m_LastFrameConstants.ptConsts.minBounceCount        = m_MinBounces;
    m_LastFrameConstants.ptConsts.NEEEnabled            = m_EnableNEE ? 1u : 0u;
    m_LastFrameConstants.ptConsts.environmentNEEEnabled = m_EnableEnvNEE ? 1u : 0u;
    m_LastFrameConstants.ptConsts.environmentIntensity  = m_EnvIntensity;
    m_LastFrameConstants.ptConsts.lightIntensityScale   = m_LightIntensityScale;
    m_LastFrameConstants.ptConsts.maxNEEBounceCount     = m_MaxNEEBounces;
    m_LastFrameConstants.ptConsts.analyticLightCount    = m_Lights.GetStats().LightCount;
    // G1: a disabled firefly filter uploads a zero threshold, so the soft cap is a no-op and the
    // converged image is identical to the filter-on image (only per-sample variance differs).
    m_LastFrameConstants.ptConsts.fireflyFilterThreshold =
        m_ReferenceUI.ReferenceFireflyFilterEnabled ? m_ReferenceUI.ReferenceFireflyFilterThreshold : 0.0f;

    if (m_FrameConstantsCB)
    {
        MapHelper<SampleConstants> Constants{m_pImmediateContext, m_FrameConstantsCB, MAP_WRITE, MAP_FLAG_DISCARD};
        *Constants = m_LastFrameConstants;
    }

    m_ResetAccumulationPending = false;

    ++m_FrameIndex;
}

void RTXPTSample::CreatePhase4Passes()
{
    m_BlitPass.Initialize(m_pDevice, m_pEngineFactory, m_pSwapChain);

    // Material-texture sampling needs descriptor indexing. Gate it on bindless support; if the textured
    // pipeline fails to build, fall back to the Phase 5.2 factor-only path so the sample still renders.
    const bool EnableMaterialTextures = m_FeatureCaps.BindlessResources && m_Materials.GetTextureCount() > 0;

    const bool RTReady =
        m_RayTracingPass.Initialize(m_pDevice,
                                    m_pImmediateContext,
                                    m_pEngineFactory,
                                    m_FrameConstantsCB,
                                    m_Materials.GetMaterialBuffer(),
                                    m_AccelerationStructures.GetSubInstanceBuffer(),
                                    m_Lights.GetLightBuffer(),
                                    m_Scene.GetVertexBuffer0(m_pDevice, m_pImmediateContext),
                                    m_Scene.GetIndexBuffer(m_pDevice, m_pImmediateContext),
                                    m_Scene.GetIndexType(),
                                    m_AccelerationStructures.GetTLAS(),
                                    m_Materials.GetTextureBindings(),
                                    m_Materials.GetTextureCount(),
                                    EnableMaterialTextures,
                                    m_FeatureCaps.RayTracing,
                                    m_FeatureCaps.StandaloneRayTracingShaders);

    if (!RTReady && EnableMaterialTextures)
    {
        m_RayTracingPass.Initialize(m_pDevice,
                                    m_pImmediateContext,
                                    m_pEngineFactory,
                                    m_FrameConstantsCB,
                                    m_Materials.GetMaterialBuffer(),
                                    m_AccelerationStructures.GetSubInstanceBuffer(),
                                    m_Lights.GetLightBuffer(),
                                    m_Scene.GetVertexBuffer0(m_pDevice, m_pImmediateContext),
                                    m_Scene.GetIndexBuffer(m_pDevice, m_pImmediateContext),
                                    m_Scene.GetIndexType(),
                                    m_AccelerationStructures.GetTLAS(),
                                    nullptr,
                                    0,
                                    false,
                                    m_FeatureCaps.RayTracing,
                                    m_FeatureCaps.StandaloneRayTracingShaders);
    }

    m_DebugComputePass.Initialize(m_pDevice,
                                  m_pEngineFactory,
                                  "RTXPT debug compute pass",
                                  "RTXPTDebugCompute.csh",
                                  m_FrameConstantsCB,
                                  m_FeatureCaps.ComputeShaders);
}

bool RTXPTSample::EnsureRenderTargets()
{
    const SwapChainDesc& SCDesc                = m_pSwapChain->GetDesc();
    const bool           WasValid              = m_RenderTargets.IsValid();
    const Uint32         OldWidth              = m_RenderTargets.GetWidth();
    const Uint32         OldHeight             = m_RenderTargets.GetHeight();
    const bool           WasAccumulationActive = m_RenderTargets.IsAccumulationActive();

    const bool Ok = m_RenderTargets.Resize(m_pDevice,
                                           SCDesc.Width,
                                           SCDesc.Height,
                                           TEX_FORMAT_RGBA8_UNORM,
                                           m_FeatureCaps.ComputeShaders,
                                           m_FeatureCaps.RayTracing);

    m_AccumulationActive = Ok && m_RenderTargets.IsAccumulationActive();
    if (Ok &&
        (!WasValid ||
         OldWidth != SCDesc.Width ||
         OldHeight != SCDesc.Height ||
         WasAccumulationActive != m_AccumulationActive))
    {
        RequestAccumulationReset("Render targets (re)created");
    }
    return Ok;
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
        ClearFallback(float4{1.0f, 0.0f, 0.0f, 1.0f});
        return;
    }

    const bool TraceExecuted =
        m_RayTracingPass.Trace(m_pImmediateContext,
                               m_RenderTargets.GetOutputColorUAV(),
                               m_RenderTargets.GetAccumColorUAV(),
                               m_RenderTargets.GetWidth(),
                               m_RenderTargets.GetHeight());

    if (!TraceExecuted)
    {
        if (!m_RayTracingPass.IsReady())
            ClearFallback(float4{1.0f, 0.0f, 1.0f, 1.0f});
        else if (m_RenderTargets.GetOutputColorUAV() == nullptr)
            ClearFallback(float4{1.0f, 0.45f, 0.0f, 1.0f});
        else if (m_RenderTargets.GetAccumColorUAV() == nullptr)
            ClearFallback(float4{0.0f, 0.2f, 1.0f, 1.0f});
        else if (m_RenderTargets.GetWidth() == 0 || m_RenderTargets.GetHeight() == 0)
            ClearFallback(float4{1.0f, 1.0f, 1.0f, 1.0f});
        else
            ClearFallback(float4{1.0f, 1.0f, 0.0f, 1.0f});
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
        ClearFallback(float4{0.0f, 1.0f, 1.0f, 1.0f});
        return;
    }
}

void RTXPTSample::Update(double CurrTime, double ElapsedTime, bool DoUpdateUI)
{
    SampleBase::Update(CurrTime, ElapsedTime, DoUpdateUI);

    m_Camera.Update(m_InputController, static_cast<float>(ElapsedTime));
    if (m_HasLastCameraMatrices &&
        (m_Camera.GetViewMatrix() != m_LastCameraView ||
         m_Camera.GetProjMatrix() != m_LastCameraProj))
    {
        RequestAccumulationReset("Camera changed");
    }
    m_LastCameraView        = m_Camera.GetViewMatrix();
    m_LastCameraProj        = m_Camera.GetProjMatrix();
    m_HasLastCameraMatrices = true;

    m_Scene.Update(CurrTime, ElapsedTime);
    UpdateFrameConstants(CurrTime);
}

void RTXPTSample::WindowResize(Uint32 Width, Uint32 Height)
{
    if (Width == 0 || Height == 0)
        return;

    UpdateCameraProjection(Width, Height);
    m_HasLastCameraMatrices = false;

    if (m_RenderTargets.Resize(m_pDevice,
                               Width,
                               Height,
                               TEX_FORMAT_RGBA8_UNORM,
                               m_FeatureCaps.ComputeShaders,
                               m_FeatureCaps.RayTracing))
    {
        m_AccumulationActive = m_RenderTargets.IsAccumulationActive();
        RequestAccumulationReset("Window resized");
    }
}

void RTXPTSample::UpdateUI()
{
    // RESET_ON_CHANGE equivalent (cf. D:/RTXPT-fork/Rtxpt/SampleUI.cpp:49): when a control
    // reports a change, restart progressive accumulation. Returns the change flag so callers
    // can also write the edited value back.
    auto ResetOnChange = [this](bool Changed, const char* Reason) -> bool {
        if (Changed)
            RequestAccumulationReset(Reason);
        return Changed;
    };
    // Tooltip for a present-but-disabled placeholder control. AllowWhenDisabled lets the
    // tooltip appear even though the preceding item was inside BeginDisabled()/EndDisabled().
    auto PlaceholderTooltip = [](const char* Text) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", Text);
    };

    const ImVec4 CategoryColor{0.60f, 0.85f, 1.00f, 1.00f};
    const float  Indent = 16.0f;

    ImGui::Begin("RTXPT");

    // ------------------------------------------------------------------ Path Tracer
    if (ImGui::CollapsingHeader("Path Tracer", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent(Indent);

        // Mode (Reference only; Realtime track is out of scope).
        {
            int ModeIndex = 0;
            ImGui::BeginDisabled(true);
            ImGui::Combo("Mode", &ModeIndex, "Reference\0Realtime\0\0");
            ImGui::EndDisabled();
            PlaceholderTooltip("Realtime mode is out of scope for the reference path tracer (umbrella Phase 5.5+).");
        }

        ImGui::TextColored(CategoryColor, "Setup:");
        ImGui::Indent(Indent);
        {
            if (ImGui::Button("Reset##REFMACC"))
                RequestAccumulationReset("User reset");
            ImGui::SameLine();
            ImGui::Text("Accumulated samples: %u", m_AccumulationFrame);

            // Jitter AA: always on in our port; shown checked + disabled.
            ImGui::BeginDisabled(true);
            ImGui::Checkbox("Jitter anti-aliasing", &m_ReferenceUI.AccumulationAA);
            ImGui::EndDisabled();
            PlaceholderTooltip("Per-sample pixel jitter is always enabled in the reference path tracer.");

            int MaxBouncesUI = static_cast<int>(m_MaxBounces);
            if (ResetOnChange(ImGui::SliderInt("Max bounces", &MaxBouncesUI, 1, 16), "Max bounces changed"))
                m_MaxBounces = static_cast<Uint32>(MaxBouncesUI);

            // Max diffuse bounces: placeholder until the BSDF/sampler work (Phase R5).
            ImGui::BeginDisabled(true);
            ImGui::SliderInt("Max diffuse bounces", &m_ReferenceUI.DiffuseBounceCount, 0, 16);
            ImGui::EndDisabled();
            PlaceholderTooltip("Separate diffuse-bounce limit lands with the BSDF/sampler work (Phase R5).");

            int MinBouncesUI = static_cast<int>(m_MinBounces);
            if (ResetOnChange(ImGui::SliderInt("Min bounces (RR start)", &MinBouncesUI, 0, 16), "Min bounces changed"))
                m_MinBounces = static_cast<Uint32>(MinBouncesUI);

            // Russian roulette: always on; start bounce is "Min bounces (RR start)".
            ImGui::BeginDisabled(true);
            ImGui::Checkbox("Use Russian Roulette early out", &m_ReferenceUI.EnableRussianRoulette);
            ImGui::EndDisabled();
            PlaceholderTooltip("Russian roulette is always enabled; its start bounce is 'Min bounces (RR start)'.");

            // Adaptive firefly filter (G1, live). Disabling it uploads a zero threshold so the soft
            // cap is a no-op and the converged image is identical to the filter-on image.
            ResetOnChange(ImGui::Checkbox("FireflyFilter (reference *)", &m_ReferenceUI.ReferenceFireflyFilterEnabled),
                          "Firefly filter toggled");
            if (m_ReferenceUI.ReferenceFireflyFilterEnabled)
            {
                ImGui::Indent(Indent);
                ResetOnChange(ImGui::InputFloat("FF Threshold", &m_ReferenceUI.ReferenceFireflyFilterThreshold, 0.1f, 0.2f, "%.5f"),
                              "Firefly threshold changed");
                ImGui::Unindent(Indent);
            }
        }
        ImGui::Unindent(Indent); // end Setup

        ImGui::TextColored(CategoryColor, "Post processing:");
        ImGui::Indent(Indent);
        {
            // Tone mapping: ACES is always applied in raygen today; a configurable
            // tone-map pass is tracked separately as Phase 6.
            ImGui::BeginDisabled(true);
            ImGui::Checkbox("Enable tone mapping", &m_ReferenceUI.EnableToneMapping);
            ImGui::EndDisabled();
            PlaceholderTooltip("ACES tone mapping is always applied in raygen; a configurable tone-map pass is tracked as Phase 6.");
        }
        ImGui::Unindent(Indent); // end Post processing

        ImGui::TextColored(CategoryColor, "Light sampling:");
        ImGui::Indent(Indent);
        {
            ResetOnChange(ImGui::Checkbox("Use Next Event Estimation", &m_EnableNEE), "NEE toggled");
            ResetOnChange(ImGui::Checkbox("Environment NEE + MIS", &m_EnableEnvNEE), "Environment NEE toggled");

            int MaxNEEBouncesUI = static_cast<int>(m_MaxNEEBounces);
            if (ResetOnChange(ImGui::SliderInt("NEE bounces", &MaxNEEBouncesUI, 0, 16), "NEE bounce budget changed"))
                m_MaxNEEBounces = static_cast<Uint32>(MaxNEEBouncesUI);

            ResetOnChange(ImGui::SliderFloat("Light intensity scale", &m_LightIntensityScale, 0.0f, 10.0f), "Light intensity changed");

            if (m_EnableNEE)
            {
                ImGui::TextColored(CategoryColor, "NEE settings:");
                ImGui::Indent(Indent);
                {
                    // Light importance sampling (RIS/WRS) + MIS type: Phase R3 (G5).
                    ImGui::BeginDisabled(true);
                    ImGui::Combo("Sampling technique", &m_ReferenceUI.NEEType, "Uniform\0Power+\0NEE-AT\0\0");
                    ImGui::EndDisabled();
                    PlaceholderTooltip("Light importance sampling (RIS/WRS) lands in Phase R3.");

                    ImGui::BeginDisabled(true);
                    ImGui::InputInt("Candidate samples", &m_ReferenceUI.NEECandidateSamples, 1);
                    ImGui::EndDisabled();
                    PlaceholderTooltip("RIS candidate count lands in Phase R3.");

                    ImGui::BeginDisabled(true);
                    ImGui::InputInt("Full samples", &m_ReferenceUI.NEEFullSamples, 1);
                    ImGui::EndDisabled();
                    PlaceholderTooltip("Visibility-tested full-sample count lands in Phase R3.");

                    ImGui::BeginDisabled(true);
                    ImGui::Combo("MIS Type", &m_ReferenceUI.NEEMISType, "Full\0ApproxInRealtime\0Approximate\0\0");
                    ImGui::EndDisabled();
                    PlaceholderTooltip("Selectable MIS type lands in Phase R3.");
                }
                ImGui::Unindent(Indent);
            }
        }
        ImGui::Unindent(Indent); // end Light sampling

        ImGui::Unindent(Indent); // end Path Tracer
    }

    // ------------------------------------------------------------ PT: Advanced Settings
    if (ImGui::CollapsingHeader("PT: Advanced Settings"))
    {
        ImGui::Indent(Indent);

        // Nested dielectrics: Phase R6 (G10).
        ImGui::BeginDisabled(true);
        ImGui::Combo("Nested Dielectrics", &m_ReferenceUI.NestedDielectricsQuality, "Off\0Fast\0Quality\0\0");
        ImGui::EndDisabled();
        PlaceholderTooltip("Nested dielectrics land in Phase R6.");

        // Low-discrepancy sampler for BSDF: Phase R5 (G9).
        ImGui::BeginDisabled(true);
        ImGui::Checkbox("Enable LD sampler for BSDF", &m_ReferenceUI.EnableLDSamplerForBSDF);
        ImGui::EndDisabled();
        PlaceholderTooltip("Low-discrepancy (Sobol/Owen) sampler lands in Phase R5.");

        ImGui::Unindent(Indent);
    }

    // ------------------------------------------------------------------ Environment Map
    if (ImGui::CollapsingHeader("Environment Map"))
    {
        ImGui::Indent(Indent);

        // HDR env-map loading: Phase R4 (G7). A procedural sky is always active.
        ImGui::BeginDisabled(true);
        ImGui::Checkbox("Enabled", &m_ReferenceUI.EnvironmentMapEnabled);
        ImGui::EndDisabled();
        PlaceholderTooltip("HDR environment-map loading lands in Phase R4; a procedural sky is always active.");

        ResetOnChange(ImGui::SliderFloat("Intensity", &m_EnvIntensity, 0.0f, 5.0f), "Environment intensity changed");

        ImGui::Unindent(Indent);
    }

    // ------------------------------------------------------------------------- Scene
    if (ImGui::CollapsingHeader("Scene"))
    {
        ImGui::Indent(Indent);

        ImGui::Text("Scene: %s", m_Scene.HasValidContent() ? "loaded" : "missing");
        ImGui::Text("Scene file: %s", m_Scene.GetLoadedSceneName().empty() ? "none" : m_Scene.GetLoadedSceneName().c_str());
        ImGui::Text("Model path: %s", m_Scene.GetModelPath().empty() ? "none" : m_Scene.GetModelPath().c_str());
        ImGui::Text("Scene cameras: %u", m_Scene.GetCameraCount());
        if (m_Scene.GetCameraCount() > 0)
        {
            const char* PreviewName = "none";
            if (m_SelectedSceneCamera >= 0)
            {
                if (const RTXPTSceneCamera* pSelectedCamera = m_Scene.GetCamera(static_cast<Uint32>(m_SelectedSceneCamera)))
                    PreviewName = pSelectedCamera->Name.c_str();
            }

            if (ImGui::BeginCombo("Scene camera", PreviewName))
            {
                for (Uint32 CameraIdx = 0; CameraIdx < m_Scene.GetCameraCount(); ++CameraIdx)
                {
                    const RTXPTSceneCamera* pCamera = m_Scene.GetCamera(CameraIdx);
                    if (pCamera == nullptr)
                        continue;

                    const bool IsSelected = static_cast<int>(CameraIdx) == m_SelectedSceneCamera;
                    ImGui::PushID(static_cast<int>(CameraIdx));
                    if (ImGui::Selectable(pCamera->Name.c_str(), IsSelected))
                        ApplySceneCamera(CameraIdx);
                    if (IsSelected)
                        ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        }

        bool ClipPlanesChanged = false;
        ClipPlanesChanged |= ImGui::InputFloat("zNear", &m_CameraNearPlane, 0.1f, 1.0f, "%.6f");
        ClipPlanesChanged |= ImGui::InputFloat("zFar", &m_CameraFarPlane, 10.0f, 100.0f, "%.1f");
        if (ClipPlanesChanged)
        {
            SanitizeCameraClipPlanes(m_CameraNearPlane, m_CameraFarPlane);
            const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
            UpdateCameraProjection(SCDesc.Width, SCDesc.Height);
            RequestAccumulationReset("Camera clip planes changed");
        }

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

        ImGui::Unindent(Indent);
    }

    // ------------------------------------------------------------------ Status / Debug
    if (ImGui::CollapsingHeader("Status / Debug"))
    {
        ImGui::Indent(Indent);

        const RTXPTAccelerationStructureStats& ASStats      = m_AccelerationStructures.GetStats();
        const RTXPTRayTracingPassStats&        RTPassStats  = m_RayTracingPass.GetStats();
        const RTXPTComputePassStats&           ComputeStats = m_DebugComputePass.GetStats();

        ImGui::Text("Backend: %s", GetRenderDeviceTypeString(m_pDevice->GetDeviceInfo().Type));
        ImGui::Text("RayTracing: %s", m_FeatureCaps.RayTracing ? "yes" : "no");
        ImGui::Text("Standalone RT shaders: %s", m_FeatureCaps.StandaloneRayTracingShaders ? "yes" : "no");
        ImGui::Text("RayQuery: %s", m_FeatureCaps.RayQuery ? "yes" : "no");
        ImGui::Text("Bindless: %s", m_FeatureCaps.BindlessResources ? "yes" : "no");
        ImGui::Text("Compute: %s", m_FeatureCaps.ComputeShaders ? "yes" : "no");
        ImGui::Text("Assets root: %s", m_AssetsRoot.c_str());
        ImGui::Separator();
        ImGui::Text("Acceleration structures: %s", m_AccelerationStructures.IsBuilt() ? "built" : "not built");
        ImGui::Text("BLAS: %u", ASStats.BLASCount);
        ImGui::Text("TLAS instances: %u", ASStats.InstanceCount);
        ImGui::Text("RT geometries: %u", ASStats.GeometryCount);
        ImGui::Text("Sub-instances: %u", ASStats.SubInstanceCount);
        ImGui::Text("Alpha-tested geometries: %u", ASStats.AlphaTestedGeometryCount);
        if (!ASStats.DisabledReason.empty())
            ImGui::TextWrapped("AS disabled: %s", ASStats.DisabledReason.c_str());
        if (!ASStats.LastError.empty())
            ImGui::TextWrapped("AS error: %s", ASStats.LastError.c_str());
        ImGui::Separator();
        ImGui::Text("Frame constants: %s", m_FrameConstantsCB ? "created" : "missing");
        ImGui::Text("Frame index: %u", m_FrameIndex);
        ImGui::Text("Viewport: %.0f x %.0f", m_LastFrameConstants.viewportSizeAndFrameIndex.x, m_LastFrameConstants.viewportSizeAndFrameIndex.y);
        ImGui::Separator();
        ImGui::Text("OutputColor: %s", m_RenderTargets.IsValid() ? "created" : "missing");
        ImGui::Text("TraceRays pass: %s", m_RayTracingPass.IsReady() ? "ready" : "not ready");
        ImGui::Text("Material bridge: %s", RTPassStats.MaterialBridgeBound ? "bound" : "fallback");
        ImGui::Text("Sub-instance bridge: %s", RTPassStats.SubInstanceBound ? "bound" : "fallback");
        ImGui::Text("Light bridge: %s", RTPassStats.LightBridgeBound ? "bound" : "fallback");
        ImGui::Text("Vertex buffer: %s", RTPassStats.VertexBufferBound ? "bound" : "fallback");
        ImGui::Text("Index buffer: %s", RTPassStats.IndexBufferBound ? "bound" : "fallback");
        ImGui::Text("Material textures loaded: %u", m_Materials.GetStats().TextureCount);
        ImGui::Text("Material textures bound: %s (%u)", RTPassStats.MaterialTexturesBound ? "yes" : "no", RTPassStats.MaterialTextureCount);
        ImGui::Text("Alpha-test any-hit: %s", RTPassStats.AnyHitEnabled ? "enabled" : "disabled");
        ImGui::Text("Accumulation target: %s", m_AccumulationActive ? "active (RGBA32F)" : "inactive (RGBA8 fallback)");
        ImGui::Text("Accumulation frame: %u", m_AccumulationFrame);
        ImGui::Text("TraceRays executed: %s", RTPassStats.LastTraceExecuted ? "yes" : "no");
        ImGui::Text("TraceRays count: %u", RTPassStats.TraceCount);
        if (!RTPassStats.DisabledReason.empty())
            ImGui::TextWrapped("TraceRays disabled: %s", RTPassStats.DisabledReason.c_str());
        if (!RTPassStats.LastError.empty())
            ImGui::TextWrapped("TraceRays error: %s", RTPassStats.LastError.c_str());
        ImGui::Separator();
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
        ImGui::Separator();
        ImGui::TextColored(CategoryColor, "Roadmap (open work):");
        ImGui::TextWrapped("TODO(RTXPT-Port Phase R2): emissive-triangle area-light NEE + MIS.");
        ImGui::TextWrapped("TODO(RTXPT-Port Phase R3): light importance sampling (RIS/WRS) + photometric units.");
        ImGui::TextWrapped("TODO(RTXPT-Port Phase R4): HDR environment map with importance sampling + MIS.");
        ImGui::TextWrapped("TODO(RTXPT-Port Phase R5): VNDF/Frostbite/multi-scatter BSDF + low-discrepancy sampler.");
        ImGui::TextWrapped("TODO(RTXPT-Port Phase R6): transmission / nested dielectrics / ALPHA_MODE_BLEND.");

        ImGui::Unindent(Indent);
    }

    ImGui::End();
}

void RTXPTSample::RequestAccumulationReset(const char* /*Reason*/)
{
    m_AccumulationFrame        = 0;
    m_ResetAccumulationPending = true;
}

} // namespace Diligent
