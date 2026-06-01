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
#include "RTXPTCameraBasis.hpp"
#include "GraphicsAccessories.hpp"
#include "GraphicsUtilities.h"
#include "FileSystem.hpp"
#include "MapHelper.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Diligent
{

namespace
{

constexpr float       kDefaultCameraNearPlane                 = 1.0f;
constexpr float       kDefaultCameraFarPlane                  = 10000.0f;
constexpr float       kMinClipPlaneSeparation                 = 1e-3f;
constexpr int         kMaxBounceSliderValue                   = 48;
constexpr bool        kDefaultToneMappingAutoExposure         = false;
constexpr float       kDefaultToneMappingExposureCompensation = 0.0f;
constexpr float       kDefaultToneMappingExposureValue        = 0.0f;
constexpr float       kDefaultToneMappingExposureValueMin     = -16.0f;
constexpr float       kDefaultToneMappingExposureValueMax     = 16.0f;
constexpr const char* kPreferredSceneName                     = "bistro-programmer-art.scene.json";
constexpr const char* kSceneFileSuffix                        = ".scene.json";
constexpr Uint32      kMaxPackedEmissiveTriangleCount         = 0x7fffffffu;
constexpr bool        kReferencePathTraceMode                 = true;

Uint32 PackEnvironmentNEEAndEmissiveTriangleCount(bool EnableEnvNEE, Uint32 EmissiveTriangleCount)
{
    const Uint32 ClampedCount = std::min(EmissiveTriangleCount, kMaxPackedEmissiveTriangleCount);
    return (ClampedCount << 1u) | (EnableEnvNEE ? 1u : 0u);
}

PathTracerCameraData MakePathTracerCameraData(FirstPersonCamera& Camera,
                                              Uint32             Width,
                                              Uint32             Height,
                                              float              FocalDistance,
                                              float              ApertureRadius)
{
    const auto& Proj              = Camera.GetProjAttribs();
    const float SafeHeight        = static_cast<float>(std::max(Height, Uint32{1}));
    const float AspectRatio       = Height > 0 ? static_cast<float>(Width) / static_cast<float>(Height) : 1.0f;
    const float SafeFocalDistance = std::max(FocalDistance, 0.001f);
    const float TanHalfFov        = std::tan(Proj.FOV * 0.5f);

    const float3 CameraDir   = normalize(Camera.GetWorldAhead());
    const float3 CameraUp    = normalize(Camera.GetWorldUp());
    const float3 CameraW     = CameraDir * SafeFocalDistance;
    const float3 CameraUUnit = normalize(cross(CameraW, CameraUp));
    const float3 CameraVUnit = normalize(cross(CameraUUnit, CameraW));

    PathTracerCameraData Data;
    Data.PosW                 = Camera.GetPos();
    Data.NearZ                = Proj.NearClipPlane;
    Data.DirectionW           = CameraDir;
    Data.PixelConeSpreadAngle = std::atan(2.0f * TanHalfFov / SafeHeight);
    Data.CameraU              = CameraUUnit * (SafeFocalDistance * TanHalfFov * AspectRatio);
    Data.FarZ                 = Proj.FarClipPlane;
    Data.CameraV              = CameraVUnit * (SafeFocalDistance * TanHalfFov);
    Data.FocalDistance        = SafeFocalDistance;
    Data.CameraW              = CameraW;
    Data.AspectRatio          = AspectRatio;
    Data.ViewportWidth        = std::max(Width, Uint32{1});
    Data.ViewportHeight       = std::max(Height, Uint32{1});
    Data.ApertureRadius       = std::max(ApertureRadius, 0.0f);
    return Data;
}

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

bool EndsWith(const std::string& Text, const char* Suffix)
{
    const std::string SuffixString{Suffix};
    return Text.size() >= SuffixString.size() &&
        Text.compare(Text.size() - SuffixString.size(), SuffixString.size(), SuffixString) == 0;
}

std::vector<std::string> EnumerateSceneFiles(const std::string& AssetsRoot)
{
    std::vector<std::string> SceneFiles;

    std::error_code             Error;
    const std::filesystem::path RootPath{AssetsRoot};
    if (!std::filesystem::is_directory(RootPath, Error))
        return SceneFiles;

    std::filesystem::directory_iterator       It{RootPath, Error};
    const std::filesystem::directory_iterator End;
    while (!Error && It != End)
    {
        const std::filesystem::directory_entry& Entry = *It;
        std::error_code                         StatusError;
        if (Entry.is_regular_file(StatusError))
        {
            const std::string FileName = Entry.path().filename().string();
            if (EndsWith(FileName, kSceneFileSuffix))
                SceneFiles.push_back(FileName);
        }
        It.increment(Error);
    }

    std::sort(SceneFiles.begin(), SceneFiles.end());
    return SceneFiles;
}

bool IsRTXPTAssetsRoot(const std::string& Path)
{
    return !EnumerateSceneFiles(Path).empty();
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

void ResetToneMappingSettings(RTXPTReferenceUIState& UI)
{
    UI.ToneMappingAutoExposure         = kDefaultToneMappingAutoExposure;
    UI.ToneMappingExposureCompensation = kDefaultToneMappingExposureCompensation;
    UI.ToneMappingExposureValue        = kDefaultToneMappingExposureValue;
    UI.ToneMappingExposureValueMin     = kDefaultToneMappingExposureValueMin;
    UI.ToneMappingExposureValueMax     = kDefaultToneMappingExposureValueMax;
}

float ComputeToneMappingExposureScale(const RTXPTReferenceUIState& UI)
{
    const float ExposureValue = UI.ToneMappingAutoExposure ? 0.0f : UI.ToneMappingExposureValue;
    return std::pow(2.0f, UI.ToneMappingExposureCompensation - ExposureValue);
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

void RTXPTSample::EnumerateAvailableScenes()
{
    m_AvailableScenes = EnumerateSceneFiles(m_AssetsRoot);
}

void RTXPTSample::EnumerateEnvironmentMaps()
{
    m_EnvMapSources        = RTXPTEnvMapBaker::EnumerateEnvironmentSources(m_AssetsRoot);
    m_SelectedEnvMapSource = 0;
}

void RTXPTSample::ApplySceneEnvironmentSettings()
{
    m_EnvMapSettings                    = RTXPTEnvMapBaker::MakeSceneDefaultSettings(m_Scene.GetSceneGraphData());
    m_ReferenceUI.EnvironmentMapEnabled = m_EnvMapSettings.Enabled;
    m_EnvIntensity                      = m_EnvMapSettings.Intensity;

    m_SelectedEnvMapSource = 0;
    for (size_t Index = 0; Index < m_EnvMapSources.size(); ++Index)
    {
        if (m_EnvMapSources[Index].RelativePath == m_EnvMapSettings.SourceRelativePath)
        {
            m_SelectedEnvMapSource = static_cast<int>(Index);
            break;
        }
    }

    m_EnvMapBakerDirty         = true;
    m_EnvMapBakerSettingsDirty = true;
}

void RTXPTSample::ResetSceneDependentResources()
{
    m_Materials.Reset();
    m_Lights.Reset();
    m_LightsBaker.SceneReloaded();
    m_EnvMapBaker.SceneReloaded();
    m_AccelerationStructures.Reset();
    m_SkinnedGeometry.Reset();
    m_RayTracingPass.Reset();
    m_EmissiveTrianglePass.Reset();

    m_SelectedSceneCamera   = -1;
    m_EnableSceneAnimations = !kReferencePathTraceMode;
    m_CameraVerticalFov     = PI_F / 4.0f;
    m_CameraNearPlane       = kDefaultCameraNearPlane;
    m_CameraFarPlane        = kDefaultCameraFarPlane;
    ResetToneMappingSettings(m_ReferenceUI);
    m_AccumulationFrame        = 0;
    m_AccumulationActive       = false;
    m_HasLastCameraMatrices    = false;
    m_HasDynamicGeometry       = false;
    m_EmissiveTrianglesDirty   = true;
    m_LightsBakerSettingsDirty = false;
    m_EnvMapBakerDirty         = true;
    m_EnvMapBakerSettingsDirty = true;
}

bool RTXPTSample::UpdateEnvMapBaker(bool ForceRebuild)
{
    if (!m_pDevice || !m_pImmediateContext || !m_pEngineFactory)
        return false;

    m_EnvMapSettings.Enabled   = m_ReferenceUI.EnvironmentMapEnabled;
    m_EnvMapSettings.Intensity = m_EnvIntensity;
    if (m_SelectedEnvMapSource >= 0 && m_SelectedEnvMapSource < static_cast<int>(m_EnvMapSources.size()))
        m_EnvMapSettings.SourceRelativePath = m_EnvMapSources[static_cast<size_t>(m_SelectedEnvMapSource)].RelativePath;

    const bool Updated = m_EnvMapBaker.Update(m_pDevice, m_pImmediateContext, m_pEngineFactory,
                                              m_AssetsRoot, m_EnvMapSettings, ForceRebuild || m_EnvMapBakerDirty,
                                              m_FeatureCaps.ComputeShaders);
    if (Updated)
    {
        m_EnvMapBakerDirty         = false;
        m_EnvMapBakerSettingsDirty = false;
    }
    return Updated;
}

bool RTXPTSample::RebuildSceneDependentResources()
{
    const RTXPTSceneGraphData& SceneData = m_Scene.GetSceneGraphData();
    if (SceneData.ModelAssets.empty() || SceneData.ModelInstances.empty())
    {
        ResetSceneDependentResources();
        CreatePhase4Passes();
        return false;
    }

    bool ResourcesReady = true;
    if (m_Scene.HasSkinnedGeometry() && m_Scene.HasAnimation())
    {
        // Seed the initial animated pose before scene-dependent resources snapshot transforms.
        // Reference mode keeps animation playback disabled, but still starts from frame zero.
        m_Scene.Update(0.0, 0.0);
    }

    ResourcesReady &= m_Materials.Upload(m_pDevice, SceneData);
    ResourcesReady &= m_Lights.Upload(m_pDevice, SceneData);
    ResourcesReady &= m_Lights.UploadEmissiveTriangles(m_pDevice, SceneData);

    const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
    ResourcesReady &= m_EnvMapBaker.CreateResources(m_pDevice, m_pImmediateContext, m_pEngineFactory, m_FeatureCaps.ComputeShaders);
    ResourcesReady &= UpdateEnvMapBaker(true);
    ResourcesReady &= m_LightsBaker.CreateResources(m_pDevice, m_pEngineFactory, SCDesc.Width, SCDesc.Height, m_FeatureCaps.ComputeShaders);
    ResourcesReady &= UpdateLightsBaker(true);

    ResourcesReady &= m_SkinnedGeometry.Initialize(m_pDevice,
                                                   m_pEngineFactory,
                                                   SceneData,
                                                   m_FeatureCaps.ComputeShaders);

    m_HasDynamicGeometry     = m_SkinnedGeometry.HasSkinnedGeometry();
    m_EmissiveTrianglesDirty = true;

    if (m_SkinnedGeometry.HasSkinnedGeometry() && m_SkinnedGeometry.IsReady())
        ResourcesReady &= m_SkinnedGeometry.Update(m_pImmediateContext, SceneData);

    ResourcesReady &=
        m_AccelerationStructures.BuildScene(m_pDevice,
                                            m_pImmediateContext,
                                            SceneData,
                                            m_Scene.GetIndexType(),
                                            &m_SkinnedGeometry,
                                            m_FeatureCaps.RayTracing);

    if (ResourcesReady)
    {
        m_Scene.ClearGeometryDirty();
        CreatePhase4Passes();
        ResourcesReady &= BuildEmissiveTriangles();
    }
    else
    {
        m_RayTracingPass.Reset();
        m_EmissiveTrianglePass.Reset();
    }
    return ResourcesReady;
}

bool RTXPTSample::SetCurrentScene(const std::string& SceneName, bool ForceReload)
{
    if (SceneName.empty())
        return false;

    if (!ForceReload && SceneName == m_CurrentSceneName)
        return m_Scene.HasValidContent();

    m_CurrentSceneName = SceneName;
    ResetSceneDependentResources();

    const bool SceneLoaded = m_Scene.LoadScene(m_pDevice, m_pImmediateContext, m_AssetsRoot, SceneName);
    if (SceneLoaded)
    {
        const RTXPTSceneSettings& SceneSettings = m_Scene.GetSceneGraphData().Settings;
        if (SceneSettings.EnableAnimations.has_value())
            m_EnableSceneAnimations = SceneSettings.EnableAnimations.value() && !kReferencePathTraceMode;
        if (SceneSettings.MaxBounces.has_value())
            m_MaxBounces = SceneSettings.MaxBounces.value();
        ApplySceneEnvironmentSettings();
    }

    const bool ResourcesReady = SceneLoaded && RebuildSceneDependentResources();
    if (!SceneLoaded)
        CreatePhase4Passes();

    auto InitializeFreeFlightCamera = [this]() {
        m_CameraVerticalFov = PI_F / 4.0f;
        m_CameraNearPlane   = kDefaultCameraNearPlane;
        m_CameraFarPlane    = kDefaultCameraFarPlane;
        InitializeCamera();
        m_SelectedSceneCamera = -1;
    };

    if (SceneLoaded)
    {
        const RTXPTSceneSettings& SceneSettings = m_Scene.GetSceneGraphData().Settings;
        if (SceneSettings.StartingCamera.has_value())
        {
            const int StartingCamera = SceneSettings.StartingCamera.value();
            if (StartingCamera < 0 || !ApplySceneCamera(static_cast<Uint32>(StartingCamera)))
                InitializeFreeFlightCamera();
        }
        else if (m_Scene.GetCameraCount() == 0 || !ApplySceneCamera(0))
        {
            InitializeFreeFlightCamera();
        }
    }
    else
    {
        InitializeFreeFlightCamera();
    }

    m_HasLastCameraMatrices = false;
    RequestAccumulationReset("Scene changed");
    return SceneLoaded && ResourcesReady;
}

void RTXPTSample::InitializeCamera()
{
    m_Camera.SetReferenceAxes(float3{1.0f, 0.0f, 0.0f}, float3{0.0f, 1.0f, 0.0f}, true);
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

    const RTXPTCameraBasis CameraBasis = MakeRTXPTDonutCameraBasis(pCamera->Rotation);

    m_SelectedSceneCamera = static_cast<int>(CameraIndex);
    m_CameraVerticalFov   = pCamera->VerticalFov;
    if (pCamera->HasExplicitClipPlanes)
    {
        m_CameraNearPlane = pCamera->NearPlane;
        m_CameraFarPlane  = pCamera->FarPlane;
    }

    m_ReferenceUI.ToneMappingAutoExposure         = pCamera->EnableAutoExposure.value_or(kDefaultToneMappingAutoExposure);
    m_ReferenceUI.ToneMappingExposureCompensation = pCamera->ExposureCompensation.value_or(kDefaultToneMappingExposureCompensation);
    m_ReferenceUI.ToneMappingExposureValue        = pCamera->ExposureValue.value_or(kDefaultToneMappingExposureValue);
    m_ReferenceUI.ToneMappingExposureValueMin     = pCamera->ExposureValueMin.value_or(kDefaultToneMappingExposureValueMin);
    m_ReferenceUI.ToneMappingExposureValueMax     = pCamera->ExposureValueMax.value_or(kDefaultToneMappingExposureValueMax);

    // Align scene-camera basis with original RTXPT (Donut): Donut flips local Z for scene cameras
    // and builds the image-plane U vector as cross(CameraW, camUp). Using a left-handed
    // FirstPersonCamera reference makes ReferenceAhead equal the Donut forward vector.
    m_Camera.SetReferenceAxes(CameraBasis.Right, CameraBasis.Up, false);
    m_Camera.SetPos(pCamera->Position);
    m_Camera.SetLookAt(pCamera->Position + CameraBasis.Forward);

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
    EnumerateEnvironmentMaps();
    EnumerateAvailableScenes();

    std::string InitialScene;
    auto        PreferredIt = std::find(m_AvailableScenes.begin(), m_AvailableScenes.end(), kPreferredSceneName);
    if (PreferredIt != m_AvailableScenes.end())
        InitialScene = *PreferredIt;
    else if (!m_AvailableScenes.empty())
        InitialScene = m_AvailableScenes.front();

    if (!InitialScene.empty())
    {
        SetCurrentScene(InitialScene, true);
    }
    else
    {
        ResetSceneDependentResources();
        CreatePhase4Passes();
    }

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
    m_LastFrameConstants.camera                    = MakePathTracerCameraData(m_Camera,
                                                           SCDesc.Width,
                                                           SCDesc.Height,
                                                           m_ReferenceUI.CameraFocalDistance,
                                                           m_ReferenceUI.CameraAperture);

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

    m_LastFrameConstants.ptConsts.bounceCount       = m_MaxBounces;
    m_LastFrameConstants.ptConsts.sampleIndex       = m_AccumulationFrame;
    m_LastFrameConstants.ptConsts.resetAccumulation = m_ResetAccumulationPending ? 1u : 0u;
    m_LastFrameConstants.ptConsts.minBounceCount    = m_MinBounces;
    m_LastFrameConstants.ptConsts.NEEEnabled        = m_EnableNEE ? 1u : 0u;
    const Uint32 EmissiveTriangleCount              = (m_EnableNEE && m_EnableEmissiveNEE && m_EmissiveTrianglePass.IsReady() &&
                                          !m_EmissiveTrianglesDirty) ?
        m_Lights.GetEmissiveTriangleCount() :
        0u;
    m_LastFrameConstants.ptConsts.environmentNEEEnabled = PackEnvironmentNEEAndEmissiveTriangleCount(m_EnableEnvNEE, EmissiveTriangleCount);
    m_LastFrameConstants.ptConsts.lightIntensityScale   = m_LightIntensityScale;
    m_LastFrameConstants.ptConsts.maxNEEBounceCount     = m_MaxNEEBounces;
    m_LastFrameConstants.ptConsts.analyticLightCount    = m_Lights.GetStats().LightCount;
    m_LastFrameConstants.ptConsts.NEEType               = static_cast<Uint32>(std::clamp(m_ReferenceUI.NEEType, 0, 2));
    m_LastFrameConstants.ptConsts.NEECandidateSamples   = static_cast<Uint32>(std::clamp(m_ReferenceUI.NEECandidateSamples, 1, 32));
    m_LastFrameConstants.ptConsts.NEEFullSamples        = static_cast<Uint32>(std::clamp(m_ReferenceUI.NEEFullSamples, 0, 32));
    m_LastFrameConstants.ptConsts.NEEMISType            = static_cast<Uint32>(std::clamp(m_ReferenceUI.NEEMISType, 0, 2));
    m_LastFrameConstants.ptConsts.diffuseBounceCount =
        static_cast<Uint32>(std::clamp(m_ReferenceUI.DiffuseBounceCount, 0, 16));
    m_LastFrameConstants.ptConsts.nestedDielectricsQuality =
        static_cast<Uint32>(std::clamp(m_ReferenceUI.NestedDielectricsQuality, 0, 2));
    // G1: a disabled firefly filter uploads a zero threshold, so the soft cap is a no-op and the
    // converged image is identical to the filter-on image (only per-sample variance differs).
    m_LastFrameConstants.ptConsts.fireflyFilterThreshold =
        m_ReferenceUI.ReferenceFireflyFilterEnabled ? m_ReferenceUI.ReferenceFireflyFilterThreshold : 0.0f;
    m_LastFrameConstants.ptConsts.exposureScale = ComputeToneMappingExposureScale(m_ReferenceUI);
    m_LastFrameConstants.envMap                 = m_EnvMapBaker.GetConstants();

    if (m_FrameConstantsCB)
    {
        MapHelper<SampleConstants> Constants{m_pImmediateContext, m_FrameConstantsCB, MAP_WRITE, MAP_FLAG_DISCARD};
        *Constants = m_LastFrameConstants;
    }

    m_ResetAccumulationPending = false;

    ++m_FrameIndex;
}

bool RTXPTSample::UpdateLightsBaker(bool ResetFeedback)
{
    if (!m_pDevice || !m_pSwapChain || !m_Scene.HasValidContent())
        return false;

    const SwapChainDesc&     SCDesc = m_pSwapChain->GetDesc();
    RTXPTLightsBakerSettings BakerSettings;
    BakerSettings.ImportanceSamplingType        = static_cast<Uint32>(std::clamp(m_ReferenceUI.NEEType, 0, 2));
    BakerSettings.CameraPosition                = m_Camera.GetPos();
    BakerSettings.ViewProjMatrix                = m_Camera.GetViewMatrix() * m_Camera.GetProjMatrix();
    BakerSettings.GlobalTemporalFeedbackWeight  = m_ReferenceUI.NEEAT_GlobalTemporalFeedbackWeight;
    BakerSettings.LocalToGlobalSampleRatio      = m_ReferenceUI.NEEAT_LocalToGlobalSampleRatio;
    BakerSettings.DistantVsLocalImportanceScale = m_ReferenceUI.NEEAT_DistantVsLocalImportance;
    BakerSettings.ResetFeedback                 = ResetFeedback;
    BakerSettings.ViewportSize                  = float2{static_cast<float>(SCDesc.Width), static_cast<float>(SCDesc.Height)};
    BakerSettings.PrevViewportSize              = BakerSettings.ViewportSize;
    BakerSettings.EnvMapParams                  = m_EnvMapBaker.GetLightsBakerParams();
    BakerSettings.EnvMapImportanceMapResolution = m_EnvMapBaker.GetStats().ImportanceResolution;
    BakerSettings.EnvMapImportanceMapMipCount   = m_EnvMapBaker.GetStats().ImportanceMipLevels;
    BakerSettings.FrameIndex                    = static_cast<Int64>(m_FrameIndex);

    const bool Updated = m_LightsBaker.UpdateBegin(m_pDevice, m_Lights, BakerSettings);
    if (Updated)
        m_LightsBakerSettingsDirty = false;
    else
        m_LightsBakerSettingsDirty = true;
    return Updated;
}

void RTXPTSample::CreatePhase4Passes()
{
    m_BlitPass.Initialize(m_pDevice, m_pEngineFactory, m_pSwapChain);

    // Material-texture sampling needs descriptor indexing. Gate it on bindless support; if the textured
    // pipeline fails to build, fall back to the Phase 5.2 factor-only path so the sample still renders.
    const bool EnableMaterialTextures = m_FeatureCaps.BindlessResources && m_Materials.GetTextureCount() > 0;
    const bool EnableAnyHit           = EnableMaterialTextures || m_AccelerationStructures.GetStats().AlphaBlendedGeometryCount > 0;

    m_EmissiveTrianglePass.Initialize(m_pDevice,
                                      m_pEngineFactory,
                                      m_Materials.GetMaterialBuffer(),
                                      m_AccelerationStructures.GetSubInstanceBuffer(),
                                      m_AccelerationStructures.GetSubInstanceTransformBuffer(),
                                      m_Scene.GetVertexBuffer0(m_pDevice, m_pImmediateContext),
                                      m_SkinnedGeometry.GetSkinnedVertexBuffer(),
                                      m_Scene.GetIndexBuffer(m_pDevice, m_pImmediateContext),
                                      m_Scene.GetIndexType(),
                                      m_Lights.GetEmissiveTriangleBuffer(),
                                      m_FeatureCaps.ComputeShaders);

    const bool RTReady =
        m_RayTracingPass.Initialize(m_pDevice,
                                    m_pImmediateContext,
                                    m_pEngineFactory,
                                    m_FrameConstantsCB,
                                    m_Materials.GetMaterialBuffer(),
                                    m_AccelerationStructures.GetSubInstanceBuffer(),
                                    m_Lights.GetLightBuffer(),
                                    m_LightsBaker.GetControlBuffer(),
                                    m_LightsBaker.GetLightProxyCounters(),
                                    m_LightsBaker.GetLightSamplingProxies(),
                                    m_LightsBaker.GetLocalSamplingBuffer(),
                                    m_LightsBaker.GetFeedbackTotalWeightUAV(),
                                    m_LightsBaker.GetFeedbackCandidatesUAV(),
                                    m_EnvMapBaker.GetEnvironmentMapSRV(),
                                    m_EnvMapBaker.GetImportanceMapSRV(),
                                    m_EnvMapBaker.GetRadianceMapSRV(),
                                    m_EnvMapBaker.GetEnvironmentSampler(),
                                    m_EnvMapBaker.GetImportanceSampler(),
                                    m_Lights.GetEmissiveTriangleBuffer(),
                                    m_Scene.GetVertexBuffer0(m_pDevice, m_pImmediateContext),
                                    m_SkinnedGeometry.GetSkinnedVertexBuffer(),
                                    m_Scene.GetIndexBuffer(m_pDevice, m_pImmediateContext),
                                    m_Scene.GetIndexType(),
                                    m_AccelerationStructures.GetTLAS(),
                                    m_Materials.GetTextureBindings(),
                                    m_Materials.GetTextureCount(),
                                    EnableMaterialTextures,
                                    EnableAnyHit,
                                    m_ReferenceUI.EnableLDSamplerForBSDF,
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
                                    m_LightsBaker.GetControlBuffer(),
                                    m_LightsBaker.GetLightProxyCounters(),
                                    m_LightsBaker.GetLightSamplingProxies(),
                                    m_LightsBaker.GetLocalSamplingBuffer(),
                                    m_LightsBaker.GetFeedbackTotalWeightUAV(),
                                    m_LightsBaker.GetFeedbackCandidatesUAV(),
                                    m_EnvMapBaker.GetEnvironmentMapSRV(),
                                    m_EnvMapBaker.GetImportanceMapSRV(),
                                    m_EnvMapBaker.GetRadianceMapSRV(),
                                    m_EnvMapBaker.GetEnvironmentSampler(),
                                    m_EnvMapBaker.GetImportanceSampler(),
                                    m_Lights.GetEmissiveTriangleBuffer(),
                                    m_Scene.GetVertexBuffer0(m_pDevice, m_pImmediateContext),
                                    m_SkinnedGeometry.GetSkinnedVertexBuffer(),
                                    m_Scene.GetIndexBuffer(m_pDevice, m_pImmediateContext),
                                    m_Scene.GetIndexType(),
                                    m_AccelerationStructures.GetTLAS(),
                                    nullptr,
                                    0,
                                    false,
                                    m_AccelerationStructures.GetStats().AlphaBlendedGeometryCount > 0,
                                    m_ReferenceUI.EnableLDSamplerForBSDF,
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

bool RTXPTSample::BuildEmissiveTriangles()
{
    const Uint32 EmissiveTriangleCount = m_Lights.GetEmissiveTriangleCount();
    if (EmissiveTriangleCount == 0u)
    {
        m_EmissiveTrianglesDirty = false;
        return true;
    }

    if (!m_EmissiveTrianglePass.IsReady() || m_AccelerationStructures.GetStats().SubInstanceCount == 0)
        return false;

    const bool Executed = m_EmissiveTrianglePass.Dispatch(m_pImmediateContext, m_AccelerationStructures.GetStats().SubInstanceCount);
    if (Executed)
        m_EmissiveTrianglesDirty = false;
    return Executed;
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

    if (!m_LightsBaker.UpdateEnd(m_pImmediateContext))
    {
        ClearFallback(float4{1.0f, 0.35f, 0.0f, 1.0f});
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

    if (m_EnableSceneAnimations)
    {
        m_Scene.Update(CurrTime, ElapsedTime);
        if (m_Scene.IsGeometryDirty() && m_SkinnedGeometry.HasSkinnedGeometry() && m_SkinnedGeometry.IsReady())
        {
            const RTXPTSceneGraphData& SceneData        = m_Scene.GetSceneGraphData();
            const bool                 SkinningExecuted = m_SkinnedGeometry.Update(m_pImmediateContext, SceneData);
            const bool                 ASUpdated =
                SkinningExecuted &&
                m_AccelerationStructures.UpdateDynamicBLAS(m_pImmediateContext, SceneData, m_SkinnedGeometry);
            if (ASUpdated)
            {
                m_EmissiveTrianglesDirty = true;
                BuildEmissiveTriangles();
                RequestAccumulationReset("Skinned geometry updated");
                m_Scene.ClearGeometryDirty();
            }
        }
    }

    bool RecreatePhase4Passes = false;
    if ((m_EnvMapBakerDirty || m_EnvMapBakerSettingsDirty) && UpdateEnvMapBaker(m_EnvMapBakerDirty))
    {
        m_LightsBakerSettingsDirty = true;
        RequestAccumulationReset("Environment map changed");
    }

    if (m_LightsBakerSettingsDirty && UpdateLightsBaker(true))
        RecreatePhase4Passes = true;

    if (m_RayTracingPassSettingsDirty)
    {
        RecreatePhase4Passes          = true;
        m_RayTracingPassSettingsDirty = false;
    }

    if (RecreatePhase4Passes)
        CreatePhase4Passes();

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
        if (m_Scene.HasValidContent())
        {
            const bool EnvUpdated = UpdateEnvMapBaker(false);
            const bool LightsResourcesReady =
                m_LightsBaker.CreateResources(m_pDevice, m_pEngineFactory, Width, Height, m_FeatureCaps.ComputeShaders);
            const bool LightsUpdated = LightsResourcesReady && UpdateLightsBaker(true);
            if (EnvUpdated && LightsUpdated)
                CreatePhase4Passes();
            else
            {
                if (!LightsResourcesReady)
                    m_LightsBakerSettingsDirty = true;
                m_RayTracingPass.Reset();
                m_EmissiveTrianglePass.Reset();
            }
        }
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
    auto ResetLightsBakerOnChange = [this, &ResetOnChange](bool Changed, const char* Reason) -> bool {
        if (ResetOnChange(Changed, Reason))
        {
            m_LightsBakerSettingsDirty = true;
            return true;
        }
        return false;
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
            if (ResetOnChange(ImGui::SliderInt("Max bounces", &MaxBouncesUI, 1, kMaxBounceSliderValue), "Max bounces changed"))
                m_MaxBounces = static_cast<Uint32>(MaxBouncesUI);

            int DiffuseBouncesUI = m_ReferenceUI.DiffuseBounceCount;
            if (ResetOnChange(ImGui::SliderInt("Max diffuse bounces", &DiffuseBouncesUI, 0, 16), "Max diffuse bounces changed"))
                m_ReferenceUI.DiffuseBounceCount = std::clamp(DiffuseBouncesUI, 0, 16);

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
            ResetOnChange(ImGui::Checkbox("Emissive mesh NEE + MIS", &m_EnableEmissiveNEE), "Emissive NEE toggled");
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
                    // Light importance sampling + MIS type: G5/R3.
                    const char* SamplingTechniqueItems = "Uniform\0Power+\0NEE-AT\0\0";
                    if (ResetLightsBakerOnChange(ImGui::Combo("Sampling technique", &m_ReferenceUI.NEEType, SamplingTechniqueItems),
                                                 "NEE sampling technique changed"))
                        m_ReferenceUI.NEEType = std::clamp(m_ReferenceUI.NEEType, 0, 2);

                    if (ResetOnChange(ImGui::InputInt("Candidate samples", &m_ReferenceUI.NEECandidateSamples, 1),
                                      "NEE candidate count changed"))
                        m_ReferenceUI.NEECandidateSamples = std::clamp(m_ReferenceUI.NEECandidateSamples, 1, 32);

                    if (ResetOnChange(ImGui::InputInt("Full samples", &m_ReferenceUI.NEEFullSamples, 1),
                                      "NEE full sample count changed"))
                        m_ReferenceUI.NEEFullSamples = std::clamp(m_ReferenceUI.NEEFullSamples, 0, 32);

                    ImGui::BeginDisabled(true);
                    ImGui::Combo("MIS Type", &m_ReferenceUI.NEEMISType, "Full\0ApproxInRealtime\0Approximate\0\0");
                    ImGui::EndDisabled();
                    PlaceholderTooltip("G5 uses the full light-vs-BSDF MIS path; approximate MIS modes remain deferred.");

                    ImGui::TextColored(CategoryColor, "NEE-AT settings:");
                    if (m_ReferenceUI.NEEType != 2)
                    {
                        ImGui::TextWrapped("NOTE: NEE-AT inactive (enable in Path Tracer -> Next Event Estimation settings).");
                    }
                    else
                    {
                        ResetLightsBakerOnChange(ImGui::SliderFloat("Global feedback weight", &m_ReferenceUI.NEEAT_GlobalTemporalFeedbackWeight, 0.0f, 0.95f),
                                                 "NEE-AT global feedback changed");
                        ResetLightsBakerOnChange(ImGui::SliderFloat("Local to global sampler ratio", &m_ReferenceUI.NEEAT_LocalToGlobalSampleRatio, 0.0f, 0.95f),
                                                 "NEE-AT local/global ratio changed");
                        ResetLightsBakerOnChange(ImGui::SliderFloat("Distant vs Local initial importance", &m_ReferenceUI.NEEAT_DistantVsLocalImportance, 0.01f, 100.0f, "%.2f"),
                                                 "NEE-AT distant/local importance changed");
                    }
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

        if (ResetOnChange(ImGui::Combo("Nested Dielectrics", &m_ReferenceUI.NestedDielectricsQuality, "Off\0Fast\0Quality\0\0"),
                          "Nested dielectric quality changed"))
        {
            m_ReferenceUI.NestedDielectricsQuality = std::clamp(m_ReferenceUI.NestedDielectricsQuality, 0, 2);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Priority-based nested dielectrics. Fast allows fewer false-hit rejections; Quality allows more.");

        if (ResetOnChange(ImGui::Checkbox("Enable LD sampler for BSDF", &m_ReferenceUI.EnableLDSamplerForBSDF),
                          "BSDF LD sampler toggled"))
            m_RayTracingPassSettingsDirty = true;

        ImGui::Unindent(Indent);
    }

    // ------------------------------------------------------------------ Environment Map
    if (ImGui::CollapsingHeader("Environment Map"))
    {
        ImGui::Indent(Indent);

        if (ResetOnChange(ImGui::Checkbox("Enabled", &m_ReferenceUI.EnvironmentMapEnabled), "Environment map toggled"))
            m_EnvMapBakerSettingsDirty = true;

        const char* EnvPreview = "none";
        if (!m_EnvMapSources.empty())
        {
            const int ClampedSource = std::clamp(m_SelectedEnvMapSource, 0, static_cast<int>(m_EnvMapSources.size()) - 1);
            EnvPreview              = m_EnvMapSources[static_cast<size_t>(ClampedSource)].DisplayName.c_str();
        }

        if (ImGui::BeginCombo("Source", EnvPreview))
        {
            for (size_t Index = 0; Index < m_EnvMapSources.size(); ++Index)
            {
                const bool Selected = static_cast<int>(Index) == m_SelectedEnvMapSource;
                if (ImGui::Selectable(m_EnvMapSources[Index].DisplayName.c_str(), Selected))
                {
                    m_SelectedEnvMapSource              = static_cast<int>(Index);
                    m_EnvMapSettings.SourceRelativePath = m_EnvMapSources[Index].RelativePath;
                    m_EnvMapBakerDirty                  = true;
                    m_EnvMapBakerSettingsDirty          = true;
                    RequestAccumulationReset("Environment source changed");
                }
                if (Selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (ResetOnChange(ImGui::SliderFloat("Intensity", &m_EnvIntensity, 0.0f, 20.0f), "Environment intensity changed"))
            m_EnvMapBakerSettingsDirty = true;

        if (ResetOnChange(ImGui::SliderFloat("Rotation", &m_EnvMapSettings.RotationRadians, -PI_F, PI_F), "Environment rotation changed"))
            m_EnvMapBakerSettingsDirty = true;

        const RTXPTEnvMapBakerStats& EnvStats = m_EnvMapBaker.GetStats();
        if (EnvStats.Ready)
            ImGui::Text("Cube resolution: %u", EnvStats.CubeResolution);
        else
            ImGui::Text("Cube resolution: pending");
        PlaceholderTooltip("DiligentFX PBR precompute currently owns the processed cubemap resolution.");

        ImGui::Text("BC6H compression: not wired");
        PlaceholderTooltip("BC6H output compression is not wired into the current DiligentFX precompute path.");

        m_EnvMapBaker.InfoGUI(Indent);

        ImGui::Unindent(Indent);
    }

    // ------------------------------------------------------------------------- Scene
    if (ImGui::CollapsingHeader("Scene"))
    {
        ImGui::Indent(Indent);

        const char* ScenePreview = "none";
        if (!m_CurrentSceneName.empty())
            ScenePreview = m_CurrentSceneName.c_str();
        else if (m_AvailableScenes.empty())
            ScenePreview = "no scenes found";

        ImGui::BeginDisabled(m_AvailableScenes.empty());
        if (ImGui::BeginCombo("Scene##SceneSelection", ScenePreview))
        {
            for (const std::string& SceneName : m_AvailableScenes)
            {
                const bool IsSelected = SceneName == m_CurrentSceneName;
                if (ImGui::Selectable(SceneName.c_str(), IsSelected))
                    SetCurrentScene(SceneName);
                if (IsSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();

        if (m_AvailableScenes.empty())
            ImGui::TextWrapped("No RTXPT scene files found under assets root: %s", m_AssetsRoot.c_str());

        const RTXPTSceneGeometryStats& GeometryStats = m_Scene.GetGeometryStats();
        ImGui::BeginDisabled(!GeometryStats.HasAnimations || kReferencePathTraceMode);
        ResetOnChange(ImGui::Checkbox("Enable animations", &m_EnableSceneAnimations), "Scene animations toggled");
        ImGui::EndDisabled();
        if (kReferencePathTraceMode)
            PlaceholderTooltip("Animations are not available in reference mode.");
        else if (!GeometryStats.HasAnimations)
            PlaceholderTooltip("This scene does not contain animations.");

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

        if (ResetOnChange(ImGui::InputFloat("Aperture", &m_ReferenceUI.CameraAperture, 0.001f, 0.01f, "%.4f"),
                          "Camera aperture changed"))
        {
            m_ReferenceUI.CameraAperture = std::clamp(m_ReferenceUI.CameraAperture, 0.0f, 1.0f);
        }

        if (ResetOnChange(ImGui::InputFloat("Focal Distance", &m_ReferenceUI.CameraFocalDistance, 0.1f),
                          "Camera focal distance changed"))
        {
            m_ReferenceUI.CameraFocalDistance = std::clamp(m_ReferenceUI.CameraFocalDistance, 0.001f, 1.0e16f);
        }

        m_ReferenceUI.CameraAperture      = std::clamp(m_ReferenceUI.CameraAperture, 0.0f, 1.0f);
        m_ReferenceUI.CameraFocalDistance = std::clamp(m_ReferenceUI.CameraFocalDistance, 0.001f, 1.0e16f);

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

        const RTXPTSceneAdapterStats& AdapterStats = m_Scene.GetAdapterStats();
        ImGui::Text("Graph nodes: %u", AdapterStats.GraphNodeCount);
        ImGui::Text("Model assets: %u", AdapterStats.ModelAssetCount);
        ImGui::Text("Model instances: %u", AdapterStats.ModelInstanceCount);
        ImGui::Text("Material extensions: %u", AdapterStats.MaterialExtensionCount);
        ImGui::Text("Material fallbacks: %u", AdapterStats.MaterialFallbackCount);
        ImGui::Text("Directional lights: %u", AdapterStats.DirectionalLightCount);
        ImGui::Text("Point lights: %u", AdapterStats.PointLightCount);
        ImGui::Text("Spot lights: %u", AdapterStats.SpotLightCount);
        ImGui::Text("Environment lights: %u", AdapterStats.EnvironmentLightCount);
        ImGui::Text("Unknown typed nodes: %u", AdapterStats.UnknownTypedNodeCount);
        ImGui::Text("Skinned instances: %u", AdapterStats.SkinnedInstanceCount);
        ImGui::Text("Mesh nodes: %u", m_Scene.GetMeshNodeCount());
        ImGui::Text("Primitives: %u", m_Scene.GetPrimitiveCount());
        ImGui::Text("Materials: %u", m_Materials.GetStats().MaterialCount);
        ImGui::Text("Lights: %u", m_Lights.GetStats().LightCount);
        ImGui::Text("Emissive triangles: %u", m_Lights.GetStats().EmissiveTriangleCount);
        ImGui::Text("Animations: %s", GeometryStats.HasAnimations ? "yes" : "no");
        ImGui::Text("Skinned nodes: %u", GeometryStats.SkinnedNodeCount);
        ImGui::Text("Skinned primitives: %u", GeometryStats.SkinnedPrimitiveCount);

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
        ImGui::Text("Alpha-blended geometries: %u", ASStats.AlphaBlendedGeometryCount);
        if (!ASStats.DisabledReason.empty())
            ImGui::TextWrapped("AS disabled: %s", ASStats.DisabledReason.c_str());
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
        ImGui::Text("Environment bridge: %s", RTPassStats.EnvironmentBridgeBound ? "bound" : "missing");
        const RTXPTEnvMapBakerStats& EnvStats = m_EnvMapBaker.GetStats();
        ImGui::Text("EnvMapBaker: %s", EnvStats.Ready ? "ready" : "not ready");
        ImGui::Text("Env source: %s", EnvStats.SourceName.empty() ? "none" : EnvStats.SourceName.c_str());
        ImGui::Text("Env importance: %s", EnvStats.ImportanceReady ? "ready" : "missing");
        const RTXPTLightsBakerStats& BakerStats = m_LightsBaker.GetStats();
        ImGui::Text("LightsBaker lights: %u", BakerStats.TotalLightCount);
        ImGui::Text("LightsBaker proxies: %u", BakerStats.SamplingProxyCount);
        ImGui::Text("LightsBaker proxy weight: %.3f", BakerStats.ProxyTotalWeight);
        ImGui::Text("LightsBaker feedback: %s", BakerStats.FeedbackReady ? "ready" : "missing");
        ImGui::Text("LightsBaker update: %u", BakerStats.UpdateCounter);
        ImGui::Text("LightsBaker bridge: %s", RTPassStats.LightsBakerBridgeBound ? "bound" : "missing");
        m_EnvMapBaker.DebugGUI(Indent);
        ImGui::Text("Emissive triangle pass: %s", m_EmissiveTrianglePass.IsReady() ? "ready" : "not ready");
        ImGui::Text("Emissive triangle dispatch count: %u", m_EmissiveTrianglePass.GetStats().DispatchCount);
        if (!m_EmissiveTrianglePass.GetStats().DisabledReason.empty())
            ImGui::TextWrapped("Emissive triangle pass disabled: %s", m_EmissiveTrianglePass.GetStats().DisabledReason.c_str());
        ImGui::Text("Vertex buffer: %s", RTPassStats.VertexBufferBound ? "bound" : "fallback");
        ImGui::Text("Skinned vertex buffer: %s", RTPassStats.SkinnedVertexBufferBound ? "bound" : "fallback");
        ImGui::Text("Skinning pass: %s", m_SkinnedGeometry.IsReady() ? "ready" : "not ready");
        if (!m_SkinnedGeometry.GetStats().DisabledReason.empty())
            ImGui::TextWrapped("Skinning disabled: %s", m_SkinnedGeometry.GetStats().DisabledReason.c_str());
        ImGui::Text("Index buffer: %s", RTPassStats.IndexBufferBound ? "bound" : "fallback");
        ImGui::Text("Material textures loaded: %u", m_Materials.GetStats().TextureCount);
        ImGui::Text("Material textures bound: %s (%u)", RTPassStats.MaterialTexturesBound ? "yes" : "no", RTPassStats.MaterialTextureCount);
        ImGui::Text("Alpha-test/blend any-hit: %s", RTPassStats.AnyHitEnabled ? "enabled" : "disabled");
        ImGui::Text("Accumulation target: %s", m_AccumulationActive ? "active (RGBA32F)" : "inactive (RGBA8 fallback)");
        ImGui::Text("Accumulation frame: %u", m_AccumulationFrame);
        ImGui::Text("TraceRays executed: %s", RTPassStats.LastTraceExecuted ? "yes" : "no");
        ImGui::Text("TraceRays count: %u", RTPassStats.TraceCount);
        if (!RTPassStats.DisabledReason.empty())
            ImGui::TextWrapped("TraceRays disabled: %s", RTPassStats.DisabledReason.c_str());
        ImGui::Separator();
        ImGui::Checkbox("Debug compute pass", &m_EnableDebugComputePass);
        ImGui::Text("Compute dispatch: %s", m_DebugComputePass.IsReady() ? "ready" : "not ready");
        ImGui::Text("Compute executed: %s", ComputeStats.LastDispatchExecuted ? "yes" : "no");
        ImGui::Text("Compute dispatch count: %u", ComputeStats.DispatchCount);
        if (!ComputeStats.DisabledReason.empty())
            ImGui::TextWrapped("Compute disabled: %s", ComputeStats.DisabledReason.c_str());
        ImGui::Text("Blit draw count: %u", m_BlitPass.GetDrawCount());

        ImGui::Unindent(Indent);
    }

    ImGui::End();
}

void RTXPTSample::RequestAccumulationReset(const char* /*Reason*/)
{
    m_AccumulationFrame        = 0;
    m_ResetAccumulationPending = true;
    m_LightsBaker.RequestFeedbackReset();
}

} // namespace Diligent
