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

#include "RTXPTLightsBaker.hpp"

#include "DebugUtilities.hpp"
#include "GraphicsTypesX.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace Diligent
{

namespace
{

constexpr Uint32 kProxyRatio     = 12u;
constexpr Uint32 kMinProxyCount  = 1u;
constexpr Uint32 kLocalProxyCount = 128u;
constexpr Uint32 kTileSize        = 8u;
constexpr float  kDistantVsLocalImportanceBaseScale = 0.0002f;

struct RTXPTLightingControlDataCPU
{
    Uint32 TotalLightCount = 0;
    Uint32 EnvmapQuadNodeCount = 0;
    Uint32 AnalyticLightCount = 0;
    Uint32 TriangleLightCount = 0;
    Uint32 SamplingProxyCount = 0;
    Uint32 HistoricTotalLightCount = 0;
    Uint32 LastFrameTemporalFeedbackAvailable = 0;
    Uint32 LastFrameLocalSamplesAvailable = 0;
    Uint32 ProxyBuildTaskCount = 0;
    Uint32 WeightsSumUINT = 0;
    Uint32 ImportanceSamplingType = 1;
    Uint32 _padding0 = 0;
    Uint32 TemporalFeedbackRequired = 0;
    Uint32 TotalMaxFeedbackCount = 0;
    float  GlobalFeedbackUseWeight = 0.75f;
    float  LocalToGlobalSampleRatio = 0.65f;
    Uint32 TileBufferHeight = 0;
    float  ScreenSpaceVsWorldSpaceThreshold = 0.3f;
    Uint32 LocalSamplingResolution[2] = {};
    Uint32 LocalSamplingTileJitter[2] = {};
    Uint32 LocalSamplingTileJitterPrev[2] = {};
    Uint32 ValidFeedbackCount = 0;
    Uint32 _padding1 = 0;
    Uint32 _padding2 = 0;
    Uint32 _padding3 = 0;
    Uint32 BakerPadding[464 / 4] = {};
};
static_assert(sizeof(RTXPTLightingControlDataCPU) == 112 + 464, "LightingControlData CPU mirror must match LightingTypes.hlsli");

float EstimateAnalyticWeight(const PolymorphicLightInfo& Light)
{
    const float Luma = Light.colorType.x * 0.2126f + Light.colorType.y * 0.7152f + Light.colorType.z * 0.0722f;
    const float Radius = std::max(Light.positionRadius.w, 0.0f);
    const float AreaOrSolidAngle = Light.colorType.w == 2.0f ?
        std::max(Light.shaping.w, 1.0e-8f) :
        std::max(PI_F * Radius * Radius, 1.0f);
    return std::max(1.0e-6f, Luma * AreaOrSolidAngle);
}

Uint32 DivRoundUp(Uint32 Value, Uint32 Divisor)
{
    return (Value + Divisor - 1u) / Divisor;
}

Uint32 FloatAsUint(float Value)
{
    Uint32 Bits = 0;
    static_assert(sizeof(Bits) == sizeof(Value), "float and uint bit widths must match");
    std::memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

} // namespace

void RTXPTLightsBaker::Reset()
{
    m_ClearLocalSamplingPass.Reset();
    m_FillLocalSamplingPass.Reset();
    m_ClearFeedbackPass.Reset();
    m_ControlBuffer.Release();
    m_LightProxyCounters.Release();
    m_LightSamplingProxies.Release();
    m_LocalSamplingBuffer.Release();
    m_FeedbackTotalWeight.Release();
    m_FeedbackCandidates.Release();
    m_FeedbackTotalWeightSRV.Release();
    m_FeedbackTotalWeightUAV.Release();
    m_FeedbackCandidatesSRV.Release();
    m_FeedbackCandidatesUAV.Release();
    m_ProxyCounters.clear();
    m_ProxyIndices.clear();
    m_AllocatedWidth  = 0;
    m_AllocatedHeight = 0;
    m_ResetFeedbackPending = false;
    m_LocalSamplingEnabled = false;
    m_Stats           = {};
}

void RTXPTLightsBaker::SceneReloaded()
{
    m_ProxyCounters.clear();
    m_ProxyIndices.clear();
    RequestFeedbackReset();
    m_LocalSamplingEnabled = false;
    m_Stats.TotalLightCount    = 0;
    m_Stats.AnalyticLightCount = 0;
    m_Stats.TriangleLightCount = 0;
    m_Stats.SamplingProxyCount = 0;
    m_Stats.ProxyTotalWeight   = 0.0f;
}

void RTXPTLightsBaker::RequestFeedbackReset()
{
    m_ResetFeedbackPending = true;
}

bool RTXPTLightsBaker::CreateResources(IRenderDevice* pDevice, IEngineFactory* pEngineFactory, Uint32 Width, Uint32 Height, bool ComputeSupported)
{
    if (pDevice == nullptr)
    {
        m_Stats.LastError = "RTXPT LightsBaker requires a render device";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }
    if (pEngineFactory == nullptr)
    {
        m_Stats.LastError = "RTXPT LightsBaker requires an engine factory";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }
    if (!ComputeSupported)
    {
        m_Stats.LastError = "RTXPT LightsBaker requires compute shader support";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    const bool FeedbackOk = CreateFeedbackTextures(pDevice, Width, Height);
    const bool LocalOk    = CreateLocalSamplingBuffer(pDevice, Width, Height);
    const bool ClearFeedbackPassOk = FeedbackOk &&
        m_ClearFeedbackPass.Initialize(pDevice, pEngineFactory, "RTXPT LightsBaker clear feedback", "ClearFeedbackCS");
    const bool ClearPassOk = FeedbackOk && LocalOk && ClearFeedbackPassOk &&
        m_ClearLocalSamplingPass.Initialize(pDevice, pEngineFactory, "RTXPT LightsBaker clear local sampling", "ClearLocalSamplingCS");
    const bool FillPassOk = ClearPassOk &&
        m_FillLocalSamplingPass.Initialize(pDevice, pEngineFactory, "RTXPT LightsBaker fill local sampling", "FillLocalSamplingCS");
    m_Stats.Ready         = FeedbackOk && LocalOk && ClearFeedbackPassOk && ClearPassOk && FillPassOk;
    return m_Stats.Ready;
}

bool RTXPTLightsBaker::UpdateBegin(IRenderDevice* pDevice, const RTXPTLights& Lights, const RTXPTLightsBakerSettings& Settings)
{
    m_ControlBuffer.Release();
    m_LightProxyCounters.Release();
    m_LightSamplingProxies.Release();
    m_LocalSamplingEnabled = false;

    if (Settings.ResetFeedback)
        RequestFeedbackReset();

    if (!BuildGlobalProxies(Lights, Settings))
        return false;
    if (!UploadProxyBuffers(pDevice))
        return false;
    if (!UploadControlBuffer(pDevice, Lights, Settings))
        return false;

    ++m_Stats.UpdateCounter;
    m_Stats.Ready = true;
    m_Stats.LastError.clear();
    return true;
}

bool RTXPTLightsBaker::UpdateEnd(IDeviceContext* pContext)
{
    if (!m_Stats.Ready)
        return false;

    if (m_ResetFeedbackPending)
    {
        const Uint32 ClearGroupsX = (m_AllocatedWidth + 7u) / 8u;
        const Uint32 ClearGroupsY = (m_AllocatedHeight + 7u) / 8u;
        const bool   ClearFeedbackOk =
            m_ClearFeedbackPass.Bind(m_ControlBuffer, m_LightProxyCounters, m_LightSamplingProxies, m_LocalSamplingBuffer,
                                     nullptr, nullptr, m_FeedbackTotalWeightUAV, m_FeedbackCandidatesUAV) &&
            m_ClearFeedbackPass.Dispatch(pContext, ClearGroupsX, ClearGroupsY);
        if (!ClearFeedbackOk)
        {
            m_Stats.LastError = "RTXPT LightsBaker feedback clear failed";
            return false;
        }
        m_ResetFeedbackPending = false;
    }

    if (!m_LocalSamplingEnabled)
    {
        m_Stats.LastError.clear();
        return true;
    }

    const Uint32 GroupsX = (m_Stats.LocalSamplingTileCountX + 7u) / 8u;
    const Uint32 GroupsY = (m_Stats.LocalSamplingTileCountY + 7u) / 8u;
    if (GroupsX == 0u || GroupsY == 0u)
    {
        m_Stats.LastError = "RTXPT LightsBaker local sampling has no dispatchable tile groups";
        return false;
    }

    const bool ClearOk = m_ClearLocalSamplingPass.Bind(m_ControlBuffer, m_LightProxyCounters, m_LightSamplingProxies, m_LocalSamplingBuffer,
                                                       nullptr, nullptr, m_FeedbackTotalWeightUAV, m_FeedbackCandidatesUAV) &&
        m_ClearLocalSamplingPass.Dispatch(pContext, GroupsX, GroupsY);
    if (!ClearOk)
    {
        m_Stats.LastError = "RTXPT LightsBaker local sampling clear failed";
        return false;
    }

    const bool FillOk = m_FillLocalSamplingPass.Bind(m_ControlBuffer, m_LightProxyCounters, m_LightSamplingProxies, m_LocalSamplingBuffer,
                                                     m_FeedbackTotalWeightSRV, m_FeedbackCandidatesSRV, nullptr, nullptr) &&
        m_FillLocalSamplingPass.Dispatch(pContext, GroupsX, GroupsY);
    if (!FillOk)
    {
        m_Stats.LastError = "RTXPT LightsBaker local sampling fill failed";
        return false;
    }

    m_Stats.LastError.clear();
    return true;
}

bool RTXPTLightsBaker::InfoGUI(float Indent)
{
    ImGui::Indent(Indent);
    ImGui::Text("Ready: %s", m_Stats.Ready ? "yes" : "no");
    ImGui::Text("Lights: %u", m_Stats.TotalLightCount);
    ImGui::Text("Proxies: %u", m_Stats.SamplingProxyCount);
    ImGui::Text("Feedback: %s", m_Stats.FeedbackReady ? "ready" : "missing");
    ImGui::Text("Local tiles: %u x %u", m_Stats.LocalSamplingTileCountX, m_Stats.LocalSamplingTileCountY);
    if (!m_Stats.LastError.empty())
        ImGui::TextWrapped("LightsBaker error: %s", m_Stats.LastError.c_str());
    ImGui::Unindent(Indent);
    return false;
}

bool RTXPTLightsBaker::DebugGUI(float Indent)
{
    ImGui::Indent(Indent);
    ImGui::Text("Update counter: %u", m_Stats.UpdateCounter);
    ImGui::Text("Proxy total weight: %.3f", m_Stats.ProxyTotalWeight);
    ImGui::Text("Allocated feedback: %u x %u", m_AllocatedWidth, m_AllocatedHeight);
    ImGui::Unindent(Indent);
    return false;
}

bool RTXPTLightsBaker::CreateFeedbackTextures(IRenderDevice* pDevice, Uint32 Width, Uint32 Height)
{
    const Uint32 SafeWidth  = std::max(Width, 1u);
    const Uint32 SafeHeight = std::max(Height, 1u);
    const Uint64 PixelCount = Uint64{SafeWidth} * Uint64{SafeHeight};

    m_FeedbackTotalWeight.Release();
    m_FeedbackCandidates.Release();
    m_FeedbackTotalWeightSRV.Release();
    m_FeedbackTotalWeightUAV.Release();
    m_FeedbackCandidatesSRV.Release();
    m_FeedbackCandidatesUAV.Release();

    std::vector<float>  ZeroWeights(static_cast<size_t>(PixelCount), 0.0f);
    std::vector<Uint32> InvalidCandidates(static_cast<size_t>(PixelCount), std::numeric_limits<Uint32>::max());

    TextureSubResData WeightSubres{ZeroWeights.data(), Uint64{SafeWidth} * sizeof(float)};
    TextureData       WeightData{&WeightSubres, 1};

    TextureDesc WeightDesc;
    WeightDesc.Name      = "RTXPT LightsBaker feedback total weight";
    WeightDesc.Type      = RESOURCE_DIM_TEX_2D;
    WeightDesc.Width     = SafeWidth;
    WeightDesc.Height    = SafeHeight;
    WeightDesc.Format    = TEX_FORMAT_R32_FLOAT;
    WeightDesc.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
    pDevice->CreateTexture(WeightDesc, &WeightData, &m_FeedbackTotalWeight);

    TextureSubResData CandidateSubres{InvalidCandidates.data(), Uint64{SafeWidth} * sizeof(Uint32)};
    TextureData       CandidateData{&CandidateSubres, 1};

    TextureDesc CandidateDesc = WeightDesc;
    CandidateDesc.Name        = "RTXPT LightsBaker feedback candidates";
    CandidateDesc.Format      = TEX_FORMAT_R32_UINT;
    pDevice->CreateTexture(CandidateDesc, &CandidateData, &m_FeedbackCandidates);

    if (!m_FeedbackTotalWeight || !m_FeedbackCandidates)
    {
        m_Stats.LastError = "Failed to create RTXPT LightsBaker feedback textures";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    m_FeedbackTotalWeightSRV = m_FeedbackTotalWeight->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    m_FeedbackTotalWeightUAV = m_FeedbackTotalWeight->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS);
    m_FeedbackCandidatesSRV  = m_FeedbackCandidates->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    m_FeedbackCandidatesUAV  = m_FeedbackCandidates->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS);
    m_Stats.FeedbackReady    = m_FeedbackTotalWeightSRV && m_FeedbackTotalWeightUAV &&
        m_FeedbackCandidatesSRV && m_FeedbackCandidatesUAV;
    if (!m_Stats.FeedbackReady)
    {
        m_Stats.LastError = "Failed to create RTXPT LightsBaker feedback texture views";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    m_AllocatedWidth  = SafeWidth;
    m_AllocatedHeight = SafeHeight;
    return true;
}

bool RTXPTLightsBaker::CreateLocalSamplingBuffer(IRenderDevice* pDevice, Uint32 Width, Uint32 Height)
{
    m_LocalSamplingBuffer.Release();

    const Uint32 TileCountX   = DivRoundUp(std::max(Width, 1u), kTileSize);
    const Uint32 TileCountY   = DivRoundUp(std::max(Height, 1u), kTileSize);
    const Uint64 ElementCount = Uint64{TileCountX} * Uint64{TileCountY} * kLocalProxyCount;

    BufferDesc Desc;
    Desc.Name              = "RTXPT LightsBaker local sampling buffer";
    Desc.Usage             = USAGE_DEFAULT;
    Desc.BindFlags         = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
    Desc.Mode              = BUFFER_MODE_STRUCTURED;
    Desc.ElementByteStride = sizeof(Uint32);
    Desc.Size              = ElementCount * sizeof(Uint32);
    pDevice->CreateBuffer(Desc, nullptr, &m_LocalSamplingBuffer);

    if (!m_LocalSamplingBuffer)
    {
        m_Stats.LastError = "Failed to create RTXPT LightsBaker local sampling buffer";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    m_Stats.LocalSamplingTileCountX = TileCountX;
    m_Stats.LocalSamplingTileCountY = TileCountY;
    return true;
}

bool RTXPTLightsBaker::BuildGlobalProxies(const RTXPTLights& Lights, const RTXPTLightsBakerSettings& Settings)
{
    const auto&  AnalyticLights    = Lights.GetAnalyticLights();
    const Uint32 AnalyticCount     = static_cast<Uint32>(AnalyticLights.size());
    const bool   HasEmissiveBucket = Lights.GetEmissiveTriangleCount() > 0;
    const Uint32 TotalLightCount   = AnalyticCount + (HasEmissiveBucket ? 1u : 0u);

    m_ProxyCounters.assign(std::max(TotalLightCount, 1u), 0u);
    m_ProxyIndices.clear();
    m_Stats.ProxyTotalWeight = 0.0f;

    m_Stats.TotalLightCount    = TotalLightCount;
    m_Stats.AnalyticLightCount = AnalyticCount;
    m_Stats.TriangleLightCount = Lights.GetEmissiveTriangleCount();
    m_Stats.SamplingProxyCount = 0;

    if (TotalLightCount == 0)
        return true;

    std::vector<ProxyBuildItem> Items;
    Items.reserve(TotalLightCount);
    for (Uint32 LightIndex = 0; LightIndex < AnalyticCount; ++LightIndex)
    {
        const float Weight = Settings.ImportanceSamplingType == 0 ? 1.0f : EstimateAnalyticWeight(AnalyticLights[LightIndex]);
        Items.push_back(ProxyBuildItem{LightIndex, 0u, Weight});
        m_Stats.ProxyTotalWeight += Weight;
    }

    if (HasEmissiveBucket)
    {
        const float Weight = Settings.ImportanceSamplingType == 0 ? 1.0f : std::max(1.0e-6f, Lights.GetEmissiveProxyWeight());
        Items.push_back(ProxyBuildItem{AnalyticCount, 0u, Weight});
        m_Stats.ProxyTotalWeight += Weight;
    }

    const Uint32 TargetProxyCount  = std::max<Uint32>(TotalLightCount, TotalLightCount * kProxyRatio);
    Uint32       AssignedProxyCount = 0;
    for (ProxyBuildItem& Item : Items)
    {
        const float Normalized = m_Stats.ProxyTotalWeight > 0.0f ?
            Item.Weight / m_Stats.ProxyTotalWeight :
            1.0f / float(TotalLightCount);
        Item.Count = std::max(kMinProxyCount, static_cast<Uint32>(std::round(Normalized * float(TargetProxyCount))));
        AssignedProxyCount += Item.Count;
    }

    m_ProxyIndices.reserve(AssignedProxyCount);
    for (const ProxyBuildItem& Item : Items)
    {
        m_ProxyCounters[Item.LightIndex] = Item.Count;
        for (Uint32 ProxyIndex = 0; ProxyIndex < Item.Count; ++ProxyIndex)
            m_ProxyIndices.push_back(Item.LightIndex);
    }

    m_Stats.SamplingProxyCount = static_cast<Uint32>(m_ProxyIndices.size());
    return true;
}

bool RTXPTLightsBaker::UploadProxyBuffers(IRenderDevice* pDevice)
{
    if (pDevice == nullptr)
    {
        m_Stats.LastError = "RTXPT LightsBaker proxy upload requires a render device";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    if (m_ProxyCounters.empty())
        m_ProxyCounters.push_back(0u);
    if (m_ProxyIndices.empty())
        m_ProxyIndices.push_back(0u);

    BufferDesc CounterDesc;
    CounterDesc.Name              = "RTXPT LightsBaker proxy counters";
    CounterDesc.Usage             = USAGE_IMMUTABLE;
    CounterDesc.BindFlags         = BIND_SHADER_RESOURCE;
    CounterDesc.Mode              = BUFFER_MODE_STRUCTURED;
    CounterDesc.ElementByteStride = sizeof(Uint32);
    CounterDesc.Size              = Uint64{m_ProxyCounters.size()} * sizeof(Uint32);
    BufferData CounterData{m_ProxyCounters.data(), CounterDesc.Size};
    pDevice->CreateBuffer(CounterDesc, &CounterData, &m_LightProxyCounters);

    BufferDesc ProxyDesc = CounterDesc;
    ProxyDesc.Name       = "RTXPT LightsBaker sampling proxies";
    ProxyDesc.Size       = Uint64{m_ProxyIndices.size()} * sizeof(Uint32);
    BufferData ProxyData{m_ProxyIndices.data(), ProxyDesc.Size};
    pDevice->CreateBuffer(ProxyDesc, &ProxyData, &m_LightSamplingProxies);

    if (!m_LightProxyCounters || !m_LightSamplingProxies)
    {
        m_Stats.LastError = "Failed to upload RTXPT LightsBaker proxy buffers";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }
    return true;
}

bool RTXPTLightsBaker::UploadControlBuffer(IRenderDevice* pDevice, const RTXPTLights&, const RTXPTLightsBakerSettings& Settings)
{
    if (pDevice == nullptr)
    {
        m_Stats.LastError = "RTXPT LightsBaker control upload requires a render device";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    RTXPTLightingControlDataCPU Control;
    Control.TotalLightCount        = m_Stats.TotalLightCount;
    Control.AnalyticLightCount     = m_Stats.AnalyticLightCount;
    Control.TriangleLightCount     = m_Stats.TriangleLightCount;
    Control.SamplingProxyCount     = m_Stats.SamplingProxyCount;
    Control.HistoricTotalLightCount = m_Stats.TotalLightCount;
    Control.ProxyBuildTaskCount    = m_Stats.TotalLightCount;
    Control.WeightsSumUINT         = FloatAsUint(m_Stats.ProxyTotalWeight);
    const bool  NEEATEnabled       = Settings.ImportanceSamplingType == 2u;
    const float FeedbackUseWeight  = NEEATEnabled ? std::clamp(Settings.GlobalTemporalFeedbackWeight, 0.0f, 0.95f) : 0.0f;
    const float LocalToGlobalRatio = NEEATEnabled ? std::clamp(Settings.LocalToGlobalSampleRatio, 0.0f, 0.95f) : 0.0f;
    m_LocalSamplingEnabled =
        NEEATEnabled &&
        m_Stats.SamplingProxyCount > 0u &&
        m_Stats.LocalSamplingTileCountX > 0u &&
        m_Stats.LocalSamplingTileCountY > 0u;

    Control.ImportanceSamplingType   = Settings.ImportanceSamplingType;
    Control.TemporalFeedbackRequired = NEEATEnabled ? 1u : 0u;
    Control.GlobalFeedbackUseWeight  = FeedbackUseWeight;
    Control.LocalToGlobalSampleRatio = LocalToGlobalRatio;
    Control.TileBufferHeight         = m_Stats.LocalSamplingTileCountY;
    Control.ScreenSpaceVsWorldSpaceThreshold = 0.3f;
    Control.LocalSamplingResolution[0] = m_Stats.LocalSamplingTileCountX;
    Control.LocalSamplingResolution[1] = m_Stats.LocalSamplingTileCountY;
    Control.BakerPadding[0]            = FloatAsUint(Settings.DistantVsLocalImportanceScale * kDistantVsLocalImportanceBaseScale);
    Control.BakerPadding[1]            = Settings.EnvMapImportanceMapMipCount;
    Control.BakerPadding[2]            = Settings.EnvMapImportanceMapResolution;
    Control.BakerPadding[4]            = m_AllocatedWidth;
    Control.BakerPadding[5]            = m_AllocatedHeight;
    Control.BakerPadding[6]            = m_AllocatedWidth;
    Control.BakerPadding[7]            = m_AllocatedHeight;
    Control.BakerPadding[8]            = Settings.MouseCursorX;
    Control.BakerPadding[9]            = Settings.MouseCursorY;
    Control.BakerPadding[10]           = FloatAsUint(Settings.ViewportSize.x > 0.0f ? Settings.PrevViewportSize.x / Settings.ViewportSize.x : 1.0f);
    Control.BakerPadding[11]           = FloatAsUint(Settings.ViewportSize.y > 0.0f ? Settings.PrevViewportSize.y / Settings.ViewportSize.y : 1.0f);
    Control.BakerPadding[14]           = m_Stats.UpdateCounter + 1u;
    Control.BakerPadding[20]           = FloatAsUint(Settings.CameraPosition.x);
    Control.BakerPadding[21]           = FloatAsUint(Settings.CameraPosition.y);
    Control.BakerPadding[22]           = FloatAsUint(Settings.CameraPosition.z);
    Control.BakerPadding[23]           = FloatAsUint(Settings.AverageContentsDistance);
    const auto WriteFloat4 = [&Control](Uint32 BaseIndex, const float4& Value) {
        Control.BakerPadding[BaseIndex + 0] = FloatAsUint(Value.x);
        Control.BakerPadding[BaseIndex + 1] = FloatAsUint(Value.y);
        Control.BakerPadding[BaseIndex + 2] = FloatAsUint(Value.z);
        Control.BakerPadding[BaseIndex + 3] = FloatAsUint(Value.w);
    };
    WriteFloat4(72u, Settings.EnvMapParams.Transform.Row0);
    WriteFloat4(76u, Settings.EnvMapParams.Transform.Row1);
    WriteFloat4(80u, Settings.EnvMapParams.Transform.Row2);
    WriteFloat4(84u, Settings.EnvMapParams.InvTransform.Row0);
    WriteFloat4(88u, Settings.EnvMapParams.InvTransform.Row1);
    WriteFloat4(92u, Settings.EnvMapParams.InvTransform.Row2);
    Control.BakerPadding[96]           = FloatAsUint(Settings.EnvMapParams.ColorMultiplier.x);
    Control.BakerPadding[97]           = FloatAsUint(Settings.EnvMapParams.ColorMultiplier.y);
    Control.BakerPadding[98]           = FloatAsUint(Settings.EnvMapParams.ColorMultiplier.z);
    Control.BakerPadding[99]           = FloatAsUint(Settings.EnvMapParams.Enabled);

    BufferDesc Desc;
    Desc.Name              = "RTXPT LightsBaker control buffer";
    Desc.Usage             = USAGE_IMMUTABLE;
    Desc.BindFlags         = BIND_SHADER_RESOURCE;
    Desc.Mode              = BUFFER_MODE_STRUCTURED;
    Desc.ElementByteStride = sizeof(RTXPTLightingControlDataCPU);
    Desc.Size              = sizeof(RTXPTLightingControlDataCPU);
    BufferData Data{&Control, Desc.Size};
    pDevice->CreateBuffer(Desc, &Data, &m_ControlBuffer);

    if (!m_ControlBuffer)
    {
        m_Stats.LastError = "Failed to upload RTXPT LightsBaker control buffer";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }
    return true;
}

} // namespace Diligent
