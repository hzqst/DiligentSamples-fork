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

#include "RTXPTSuperResolutionPass.hpp"

#include <algorithm>

#include "DebugUtilities.hpp"
#include "SuperResolutionFactoryLoader.h"

namespace Diligent
{
namespace
{
constexpr const char* kSRProviderUnavailableReason = "super resolution provider unavailable";
constexpr const char* kSRVariantsUnavailableReason = "super resolution variants unavailable";

bool UpscalerDescMatches(const SuperResolutionDesc& Lhs, const SuperResolutionDesc& Rhs)
{
    return Lhs.VariantId == Rhs.VariantId &&
        Lhs.InputWidth == Rhs.InputWidth &&
        Lhs.InputHeight == Rhs.InputHeight &&
        Lhs.OutputWidth == Rhs.OutputWidth &&
        Lhs.OutputHeight == Rhs.OutputHeight &&
        Lhs.OutputFormat == Rhs.OutputFormat &&
        Lhs.ColorFormat == Rhs.ColorFormat &&
        Lhs.DepthFormat == Rhs.DepthFormat &&
        Lhs.MotionFormat == Rhs.MotionFormat &&
        Lhs.Flags == Rhs.Flags;
}

RTXPTSuperResolutionFrameDesc MakeDirectFrameDesc(Uint32                              DisplayWidth,
                                                  Uint32                              DisplayHeight,
                                                  TEXTURE_FORMAT                      OutputFormat,
                                                  const RTXPTSuperResolutionSettings& Settings,
                                                  bool                                ResetHistory,
                                                  float                               TimeDeltaSeconds)
{
    RTXPTSuperResolutionFrameDesc  FrameDesc;
    const Uint32                   Width  = std::max(DisplayWidth, 1u);
    const Uint32                   Height = std::max(DisplayHeight, 1u);
    const RTXPTRenderTargetFormats Formats;

    FrameDesc.Dimensions.RenderWidth           = Width;
    FrameDesc.Dimensions.RenderHeight          = Height;
    FrameDesc.Dimensions.DisplayWidth          = Width;
    FrameDesc.Dimensions.DisplayHeight         = Height;
    FrameDesc.Dimensions.SuperResolutionActive = false;
    FrameDesc.ColorFormat                      = Formats.SuperResolutionInputColor;
    FrameDesc.DepthFormat                      = Formats.Depth;
    FrameDesc.MotionFormat                     = Formats.ScreenMotionVectors;
    FrameDesc.OutputFormat                     = OutputFormat;
    FrameDesc.ResetHistory                     = ResetHistory;
    FrameDesc.Sharpness                        = Settings.Sharpness;
    FrameDesc.TimeDeltaSeconds                 = TimeDeltaSeconds;

    return FrameDesc;
}

void UpdateFrameStats(RTXPTSuperResolutionStats& Stats, const RTXPTSuperResolutionFrameDesc& FrameDesc)
{
    Stats.RenderWidth       = FrameDesc.Dimensions.RenderWidth;
    Stats.RenderHeight      = FrameDesc.Dimensions.RenderHeight;
    Stats.DisplayWidth      = FrameDesc.Dimensions.DisplayWidth;
    Stats.DisplayHeight     = FrameDesc.Dimensions.DisplayHeight;
    Stats.LastFrameTemporal = FrameDesc.Temporal;
}
} // namespace

void RTXPTSuperResolutionPass::Reset()
{
    m_Upscaler.Release();
    m_Factory.Release();
    m_Device.Release();
    m_Variants.clear();
    m_Stats        = {};
    m_UpscalerDesc = {};
}

bool RTXPTSuperResolutionPass::Initialize(IRenderDevice* pDevice)
{
    Reset();

    if (pDevice == nullptr)
    {
        m_Stats.DisabledReason = "render device is null";
        DEV_ERROR("RTXPT super resolution pass requires a render device");
        return false;
    }

    m_Device = pDevice;
    LoadAndCreateSuperResolutionFactory(pDevice, &m_Factory);
    m_Stats.FactoryReady = m_Factory != nullptr;
    if (!m_Factory)
    {
        m_Stats.DisabledReason = kSRProviderUnavailableReason;
        return true;
    }

    Uint32 NumVariants = 0;
    m_Factory->EnumerateVariants(NumVariants, nullptr);
    m_Variants.resize(NumVariants);
    if (NumVariants > 0)
    {
        m_Factory->EnumerateVariants(NumVariants, m_Variants.data());
        m_Variants.resize(NumVariants);
    }

    m_Stats.VariantCount   = static_cast<Uint32>(m_Variants.size());
    m_Stats.DisabledReason = m_Variants.empty() ? kSRVariantsUnavailableReason : "";
    return true;
}

const SuperResolutionInfo* RTXPTSuperResolutionPass::GetActiveVariant(const RTXPTSuperResolutionSettings& Settings) const
{
    if (m_Variants.empty())
        return nullptr;

    const Int32 LastVariantIdx   = static_cast<Int32>(m_Variants.size() - 1);
    const Int32 ActiveVariantIdx = std::min(std::max(Settings.ActiveVariantIdx, 0), LastVariantIdx);
    return &m_Variants[static_cast<size_t>(ActiveVariantIdx)];
}

bool RTXPTSuperResolutionPass::HasTemporalVariant() const
{
    return std::any_of(m_Variants.begin(), m_Variants.end(), [](const SuperResolutionInfo& Info) {
        return Info.Type == SUPER_RESOLUTION_TYPE_TEMPORAL;
    });
}

bool RTXPTSuperResolutionPass::SupportsSharpness(const SuperResolutionInfo& Info) const
{
    return (Info.Type == SUPER_RESOLUTION_TYPE_SPATIAL && (Info.SpatialCapFlags & SUPER_RESOLUTION_SPATIAL_CAP_FLAG_SHARPNESS) != 0) ||
        (Info.Type == SUPER_RESOLUTION_TYPE_TEMPORAL && (Info.TemporalCapFlags & SUPER_RESOLUTION_TEMPORAL_CAP_FLAG_SHARPNESS) != 0);
}

SUPER_RESOLUTION_FLAGS RTXPTSuperResolutionPass::GetFlags(const SuperResolutionInfo& Variant, float Sharpness) const
{
    SUPER_RESOLUTION_FLAGS Flags =
        Variant.Type == SUPER_RESOLUTION_TYPE_TEMPORAL ? SUPER_RESOLUTION_FLAG_AUTO_EXPOSURE : SUPER_RESOLUTION_FLAG_NONE;

    if (SupportsSharpness(Variant) && Sharpness > 0.0f)
        Flags = Flags | SUPER_RESOLUTION_FLAG_ENABLE_SHARPENING;

    return Flags;
}

RTXPTSuperResolutionFrameDesc RTXPTSuperResolutionPass::ResolveFrameDesc(const RTXPTSuperResolutionSettings& Settings,
                                                                         Uint32                              DisplayWidth,
                                                                         Uint32                              DisplayHeight,
                                                                         TEXTURE_FORMAT                      OutputFormat,
                                                                         bool                                ResetHistory,
                                                                         float                               TimeDeltaSeconds)
{
    m_Stats.LastExecute   = false;
    m_Stats.UpscalerReady = false;

    auto DirectFrameDesc = MakeDirectFrameDesc(DisplayWidth, DisplayHeight, OutputFormat, Settings, ResetHistory, TimeDeltaSeconds);
    m_Stats.DisabledReason.clear();
    UpdateFrameStats(m_Stats, DirectFrameDesc);

    if (!Settings.Enabled)
    {
        m_Stats.DisabledReason = "super resolution disabled by settings";
        return DirectFrameDesc;
    }
    if (!m_Factory)
    {
        m_Stats.DisabledReason = kSRProviderUnavailableReason;
        return DirectFrameDesc;
    }
    if (m_Variants.empty())
    {
        m_Stats.DisabledReason = kSRVariantsUnavailableReason;
        return DirectFrameDesc;
    }

    const SuperResolutionInfo* pVariant = GetActiveVariant(Settings);
    if (pVariant == nullptr)
    {
        m_Stats.DisabledReason = "active super resolution variant is invalid";
        return DirectFrameDesc;
    }
    if (pVariant->Type != SUPER_RESOLUTION_TYPE_TEMPORAL)
    {
        m_Stats.DisabledReason = "P6 HDR path requires temporal variant";
        return DirectFrameDesc;
    }

    SuperResolutionSourceSettingsAttribs SRAttribs;
    SRAttribs.VariantId        = pVariant->VariantId;
    SRAttribs.OutputWidth      = DirectFrameDesc.Dimensions.DisplayWidth;
    SRAttribs.OutputHeight     = DirectFrameDesc.Dimensions.DisplayHeight;
    SRAttribs.OutputFormat     = DirectFrameDesc.OutputFormat;
    SRAttribs.Flags            = GetFlags(*pVariant, Settings.Sharpness);
    SRAttribs.OptimizationType = Settings.OptimizationType;

    SuperResolutionSourceSettings SRSettings;
    m_Factory->GetSourceSettings(SRAttribs, SRSettings);
    if (SRSettings.OptimalInputWidth == 0 || SRSettings.OptimalInputHeight == 0)
    {
        m_Stats.DisabledReason = "super resolution source settings are invalid";
        return DirectFrameDesc;
    }

    RTXPTSuperResolutionFrameDesc FrameDesc    = DirectFrameDesc;
    FrameDesc.Dimensions.RenderWidth           = SRSettings.OptimalInputWidth;
    FrameDesc.Dimensions.RenderHeight          = SRSettings.OptimalInputHeight;
    FrameDesc.Dimensions.SuperResolutionActive = true;
    FrameDesc.Enabled                          = true;
    FrameDesc.Temporal                         = true;
    FrameDesc.Type                             = pVariant->Type;
    FrameDesc.VariantId                        = pVariant->VariantId;
    if (!FrameDesc.Dimensions.IsValid())
    {
        m_Stats.DisabledReason = "super resolution dimensions are invalid";
        return DirectFrameDesc;
    }

    if (!EnsureUpscaler(FrameDesc))
    {
        const std::string DisabledReason = m_Stats.DisabledReason;
        m_Stats.DisabledReason           = DisabledReason.empty() ? "failed to create super resolution upscaler" : DisabledReason;
        return DirectFrameDesc;
    }

    m_Upscaler->GetJitterOffset(m_Stats.ExecuteCount, FrameDesc.Jitter.x, FrameDesc.Jitter.y);
    m_Stats.DisabledReason.clear();
    UpdateFrameStats(m_Stats, FrameDesc);
    return FrameDesc;
}

bool RTXPTSuperResolutionPass::EnsureUpscaler(const RTXPTSuperResolutionFrameDesc& FrameDesc)
{
    if (!FrameDesc.Enabled || !FrameDesc.Temporal)
        return true;

    const auto VariantIt = std::find_if(m_Variants.begin(), m_Variants.end(), [&FrameDesc](const SuperResolutionInfo& Info) {
        return Info.VariantId == FrameDesc.VariantId;
    });
    if (!m_Factory)
    {
        m_Stats.UpscalerReady  = false;
        m_Stats.DisabledReason = kSRProviderUnavailableReason;
        return false;
    }
    if (!m_Device)
    {
        m_Stats.UpscalerReady  = false;
        m_Stats.DisabledReason = "render device is unavailable";
        return false;
    }
    if (VariantIt == m_Variants.end())
    {
        m_Stats.UpscalerReady  = false;
        m_Stats.DisabledReason = "super resolution variant is unavailable";
        return false;
    }

    SuperResolutionDesc Desc;
    Desc.Name         = "RTXPT super resolution upscaler";
    Desc.VariantId    = FrameDesc.VariantId;
    Desc.InputWidth   = FrameDesc.Dimensions.RenderWidth;
    Desc.InputHeight  = FrameDesc.Dimensions.RenderHeight;
    Desc.OutputWidth  = FrameDesc.Dimensions.DisplayWidth;
    Desc.OutputHeight = FrameDesc.Dimensions.DisplayHeight;
    Desc.OutputFormat = FrameDesc.OutputFormat;
    Desc.ColorFormat  = FrameDesc.ColorFormat;
    Desc.DepthFormat  = FrameDesc.DepthFormat;
    Desc.MotionFormat = FrameDesc.MotionFormat;
    Desc.Flags        = GetFlags(*VariantIt, FrameDesc.Sharpness);

    if (m_Upscaler && UpscalerDescMatches(m_UpscalerDesc, Desc))
    {
        m_Stats.UpscalerReady = true;
        return true;
    }

    m_Upscaler.Release();
    m_Factory->CreateSuperResolution(Desc, &m_Upscaler);
    m_Stats.UpscalerReady = m_Upscaler != nullptr;
    if (!m_Upscaler)
    {
        m_Stats.DisabledReason = "failed to create super resolution upscaler";
        return false;
    }

    m_UpscalerDesc = Desc;
    return true;
}

bool RTXPTSuperResolutionPass::Execute(IDeviceContext*                      pContext,
                                        const RTXPTRenderTargets&            RenderTargets,
                                        const RTXPTSuperResolutionFrameDesc& FrameDesc,
                                        float                                CameraNear,
                                        float                                CameraFar,
                                        float                                CameraFovAngleVert,
                                        ITextureView*                        pColorSRV,
                                        ITextureView*                        pOutputUAV)
{
    m_Stats.LastExecute = false;
    if (!FrameDesc.Enabled)
        return true;

    if (!EnsureUpscaler(FrameDesc))
    {
        if (m_Stats.DisabledReason.empty())
            m_Stats.DisabledReason = "failed to prepare super resolution upscaler";
        DEV_ERROR("RTXPT super resolution pass failed: ", m_Stats.DisabledReason.c_str());
        return false;
    }

    ExecuteSuperResolutionAttribs Attribs;
    Attribs.pContext            = pContext;
    Attribs.pColorTextureSRV    = pColorSRV != nullptr ? pColorSRV : RenderTargets.GetSuperResolutionColorSRV();
    Attribs.pDepthTextureSRV    = RenderTargets.GetDepthSRV();
    Attribs.pMotionVectorsSRV   = RenderTargets.GetScreenMotionVectorsSRV();
    Attribs.pOutputTextureView  = pOutputUAV != nullptr ? pOutputUAV : RenderTargets.GetSuperResolutionOutputUAV();
    Attribs.StateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.JitterX             = FrameDesc.Jitter.x;
    Attribs.JitterY             = FrameDesc.Jitter.y;
    Attribs.PreExposure         = 1.0f;
    Attribs.MotionVectorScaleX  = 1.0f;
    Attribs.MotionVectorScaleY  = 1.0f;
    Attribs.ExposureScale       = 1.0f;
    Attribs.Sharpness           = FrameDesc.Sharpness;
    Attribs.CameraNear          = CameraNear;
    Attribs.CameraFar           = CameraFar;
    Attribs.CameraFovAngleVert  = CameraFovAngleVert;
    Attribs.TimeDeltaInSeconds  = FrameDesc.TimeDeltaSeconds;
    Attribs.ResetHistory        = FrameDesc.ResetHistory;

    const auto FailExecute = [this](const char* Reason) {
        m_Stats.DisabledReason = Reason;
        DEV_ERROR("RTXPT super resolution pass failed: ", Reason);
        return false;
    };
    if (Attribs.pContext == nullptr)
        return FailExecute("device context is null");
    if (!RenderTargets.IsSuperResolutionActive())
        return FailExecute("super resolution render targets are inactive");
    if (!(RenderTargets.GetDimensions() == FrameDesc.Dimensions))
        return FailExecute("super resolution render target dimensions do not match frame desc");
    if (Attribs.pColorTextureSRV == nullptr)
        return FailExecute("super resolution color SRV is null");
    if (Attribs.pDepthTextureSRV == nullptr)
        return FailExecute("super resolution depth SRV is null");
    if (Attribs.pMotionVectorsSRV == nullptr)
        return FailExecute("super resolution motion vectors SRV is null");
    if (Attribs.pOutputTextureView == nullptr)
        return FailExecute("super resolution output UAV is null");
    if (m_Upscaler == nullptr)
        return FailExecute("super resolution upscaler is null");

    m_Upscaler->Execute(Attribs);
    m_Stats.DisabledReason.clear();
    m_Stats.LastExecute = true;
    ++m_Stats.ExecuteCount;
    return true;
}
} // namespace Diligent
