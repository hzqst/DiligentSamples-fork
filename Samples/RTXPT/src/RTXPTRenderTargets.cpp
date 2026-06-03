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

#include "RTXPTRenderTargets.hpp"
#include "DebugUtilities.hpp"

namespace Diligent
{

namespace
{

bool FormatsMatch(const RTXPTRenderTargetFormats& Lhs, const RTXPTRenderTargetFormats& Rhs)
{
    return Lhs.OutputColor == Rhs.OutputColor &&
        Lhs.AccumulatedRadiance == Rhs.AccumulatedRadiance &&
        Lhs.SuperResolutionInputColor == Rhs.SuperResolutionInputColor &&
        Lhs.ProcessedOutputColor == Rhs.ProcessedOutputColor &&
        Lhs.LdrColor == Rhs.LdrColor &&
        Lhs.ComputeColor == Rhs.ComputeColor &&
        Lhs.Depth == Rhs.Depth &&
        Lhs.ScreenMotionVectors == Rhs.ScreenMotionVectors &&
        Lhs.TemporalFeedback == Rhs.TemporalFeedback &&
        Lhs.CombinedHistoryClampRelax == Rhs.CombinedHistoryClampRelax;
}

} // namespace

void RTXPTRenderTargets::Reset()
{
    m_OutputColor.Release();
    m_AccumulatedRadiance.Release();
    m_SuperResolutionInputColor.Release();
    m_ProcessedOutputColor.Release();
    m_LdrColor.Release();
    m_ComputeColor.Release();
    m_Depth.Release();
    m_ScreenMotionVectors.Release();
    m_TemporalFeedback1.Release();
    m_TemporalFeedback2.Release();
    m_CombinedHistoryClampRelax.Release();
    m_AccumulatedRadianceUnavailable = false;
    m_AccumulatedRadianceRequested   = false;
    m_Dimensions                     = {0, 0, 0, 0, false};
    m_Formats                        = {};
}

bool RTXPTRenderTargets::CreateTarget(IRenderDevice*           pDevice,
                                      const char*              Name,
                                      Uint32                   Width,
                                      Uint32                   Height,
                                      TEXTURE_FORMAT           TargetFormat,
                                      BIND_FLAGS               BindFlags,
                                      RefCntAutoPtr<ITexture>& Target)
{
    TextureDesc Desc;
    Desc.Name      = Name;
    Desc.Type      = RESOURCE_DIM_TEX_2D;
    Desc.Width     = Width;
    Desc.Height    = Height;
    Desc.Format    = TargetFormat;
    Desc.BindFlags = BindFlags;

    Target.Release();
    pDevice->CreateTexture(Desc, nullptr, &Target);
    if (!Target)
    {
        LOG_ERROR_MESSAGE("Failed to create ", Name);
        return false;
    }

    if (((BindFlags & BIND_SHADER_RESOURCE) != 0 && Target->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) == nullptr) ||
        ((BindFlags & BIND_UNORDERED_ACCESS) != 0 && Target->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) == nullptr) ||
        ((BindFlags & BIND_RENDER_TARGET) != 0 && Target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET) == nullptr))
    {
        LOG_ERROR_MESSAGE(Name, " is missing one or more required default views");
        Target.Release();
        return false;
    }

    return true;
}

bool RTXPTRenderTargets::SupportsBindFlags(IRenderDevice* pDevice, TEXTURE_FORMAT TargetFormat, BIND_FLAGS BindFlags) const
{
    const auto& FmtInfo = pDevice->GetTextureFormatInfoExt(TargetFormat);
    return (FmtInfo.BindFlags & BindFlags) == BindFlags;
}

bool RTXPTRenderTargets::Resize(IRenderDevice*                     pDevice,
                                const RTXPTRenderTargetDimensions& Dimensions,
                                const RTXPTRenderTargetFormats&    Formats,
                                bool                               CreateComputeOutput,
                                bool                               CreateAccumulatedRadiance)
{
    if (pDevice == nullptr || !Dimensions.IsValid())
        return false;

    const bool HasCorePostProcessTargets =
        m_OutputColor != nullptr &&
        m_ProcessedOutputColor != nullptr &&
        m_LdrColor != nullptr &&
        m_Depth != nullptr &&
        m_ScreenMotionVectors != nullptr &&
        (!Dimensions.SuperResolutionActive || m_SuperResolutionInputColor != nullptr);
    const bool HasP6PostProcessTargets =
        HasCorePostProcessTargets &&
        m_TemporalFeedback1 != nullptr &&
        m_TemporalFeedback2 != nullptr &&
        m_CombinedHistoryClampRelax != nullptr;
    const bool HasRequestedTargets =
        HasP6PostProcessTargets &&
        (!CreateComputeOutput || m_ComputeColor != nullptr) &&
        (CreateComputeOutput || m_ComputeColor == nullptr) &&
        m_AccumulatedRadianceRequested == CreateAccumulatedRadiance &&
        (!CreateAccumulatedRadiance || m_AccumulatedRadiance != nullptr || m_AccumulatedRadianceUnavailable) &&
        (CreateAccumulatedRadiance || m_AccumulatedRadiance == nullptr);

    if (HasRequestedTargets && m_Dimensions == Dimensions && FormatsMatch(m_Formats, Formats))
        return true;

    Reset();
    m_Dimensions                   = Dimensions;
    m_Formats                      = Formats;
    m_AccumulatedRadianceRequested = CreateAccumulatedRadiance;

    const auto Fail = [this]() {
        Reset();
        return false;
    };

    const auto FailAccumulatedRadianceUnavailable = [this]() {
        Reset();
        m_AccumulatedRadianceUnavailable = true;
        return false;
    };

    const BIND_FLAGS UavFlags   = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
    const BIND_FLAGS HdrRtFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS | BIND_RENDER_TARGET;
    const BIND_FLAGS LdrRtFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS | BIND_RENDER_TARGET;

    if (!SupportsBindFlags(pDevice, m_Formats.OutputColor, UavFlags))
    {
        LOG_ERROR_MESSAGE("RGBA16F UAV OutputColor is not supported; RTXPT post-processing resource graph is unavailable");
        return Fail();
    }

    if (CreateAccumulatedRadiance && !SupportsBindFlags(pDevice, m_Formats.AccumulatedRadiance, UavFlags))
    {
        LOG_ERROR_MESSAGE("RGBA32F UAV AccumulatedRadiance is not supported; reference accumulation is unavailable");
        return FailAccumulatedRadianceUnavailable();
    }

    if (m_Dimensions.SuperResolutionActive && !SupportsBindFlags(pDevice, m_Formats.SuperResolutionInputColor, UavFlags))
    {
        LOG_ERROR_MESSAGE("RGBA16F UAV SuperResolutionInputColor is not supported; RTXPT post-processing resource graph is unavailable");
        return Fail();
    }

    if (!SupportsBindFlags(pDevice, m_Formats.Depth, UavFlags))
    {
        LOG_ERROR_MESSAGE("R32F UAV Depth is not supported; RTXPT post-processing resource graph is unavailable");
        return Fail();
    }

    if (!SupportsBindFlags(pDevice, m_Formats.ScreenMotionVectors, UavFlags))
    {
        LOG_ERROR_MESSAGE("RG16F UAV ScreenMotionVectors is not supported; RTXPT post-processing resource graph is unavailable");
        return Fail();
    }

    if (!SupportsBindFlags(pDevice, m_Formats.ProcessedOutputColor, HdrRtFlags))
    {
        LOG_ERROR_MESSAGE("HDR UAV/RTV ProcessedOutputColor is not supported; RTXPT post-processing resource graph is unavailable");
        return Fail();
    }

    if (!SupportsBindFlags(pDevice, m_Formats.LdrColor, LdrRtFlags))
    {
        LOG_ERROR_MESSAGE("LDR UAV/RTV targets are not supported; RTXPT post-processing resource graph is unavailable");
        return Fail();
    }

    if (!SupportsBindFlags(pDevice, m_Formats.TemporalFeedback, UavFlags))
    {
        LOG_ERROR_MESSAGE("RGBA16_SNORM UAV TemporalFeedback is not supported; RTXPT post-processing resource graph is unavailable");
        return Fail();
    }

    if (!SupportsBindFlags(pDevice, m_Formats.CombinedHistoryClampRelax, UavFlags))
    {
        LOG_ERROR_MESSAGE("R8 UAV CombinedHistoryClampRelax is not supported; RTXPT post-processing resource graph is unavailable");
        return Fail();
    }

    if (CreateComputeOutput && !SupportsBindFlags(pDevice, m_Formats.ComputeColor, UavFlags))
    {
        LOG_ERROR_MESSAGE("RGBA8 UAV ComputeColor is not supported; RTXPT post-processing resource graph is unavailable");
        return Fail();
    }

    const Uint32 RenderWidth   = m_Dimensions.RenderWidth;
    const Uint32 RenderHeight  = m_Dimensions.RenderHeight;
    const Uint32 DisplayWidth  = m_Dimensions.DisplayWidth;
    const Uint32 DisplayHeight = m_Dimensions.DisplayHeight;

    if (!CreateTarget(pDevice, "RTXPT OutputColor", RenderWidth, RenderHeight, m_Formats.OutputColor, UavFlags, m_OutputColor))
        return Fail();

    if (CreateAccumulatedRadiance &&
        !m_AccumulatedRadianceUnavailable &&
        !CreateTarget(pDevice, "RTXPT AccumulatedRadiance", RenderWidth, RenderHeight, m_Formats.AccumulatedRadiance, UavFlags, m_AccumulatedRadiance))
        return Fail();

    if (m_Dimensions.SuperResolutionActive &&
        !CreateTarget(pDevice, "RTXPT SuperResolutionInputColor", RenderWidth, RenderHeight, m_Formats.SuperResolutionInputColor, UavFlags, m_SuperResolutionInputColor))
        return Fail();

    if (!CreateTarget(pDevice, "RTXPT Depth", RenderWidth, RenderHeight, m_Formats.Depth, UavFlags, m_Depth))
        return Fail();

    if (!CreateTarget(pDevice, "RTXPT ScreenMotionVectors", RenderWidth, RenderHeight, m_Formats.ScreenMotionVectors, UavFlags, m_ScreenMotionVectors))
        return Fail();

    if (!CreateTarget(pDevice, "RTXPT ProcessedOutputColor", DisplayWidth, DisplayHeight, m_Formats.ProcessedOutputColor, HdrRtFlags, m_ProcessedOutputColor))
        return Fail();

    if (!CreateTarget(pDevice, "RTXPT LdrColor", DisplayWidth, DisplayHeight, m_Formats.LdrColor, LdrRtFlags, m_LdrColor))
        return Fail();

    if (!CreateTarget(pDevice, "RTXPT TemporalFeedback1", DisplayWidth, DisplayHeight, m_Formats.TemporalFeedback, UavFlags, m_TemporalFeedback1))
        return Fail();

    if (!CreateTarget(pDevice, "RTXPT TemporalFeedback2", DisplayWidth, DisplayHeight, m_Formats.TemporalFeedback, UavFlags, m_TemporalFeedback2))
        return Fail();

    if (!CreateTarget(pDevice, "RTXPT CombinedHistoryClampRelax", DisplayWidth, DisplayHeight, m_Formats.CombinedHistoryClampRelax, UavFlags, m_CombinedHistoryClampRelax))
        return Fail();

    if (CreateComputeOutput &&
        !CreateTarget(pDevice, "RTXPT ComputeColor", DisplayWidth, DisplayHeight, m_Formats.ComputeColor, UavFlags, m_ComputeColor))
        return Fail();

    return true;
}

bool RTXPTRenderTargets::Resize(IRenderDevice*                  pDevice,
                                Uint32                          Width,
                                Uint32                          Height,
                                const RTXPTRenderTargetFormats& Formats,
                                bool                            CreateComputeOutput,
                                bool                            CreateAccumulatedRadiance)
{
    RTXPTRenderTargetDimensions Dimensions;
    Dimensions.RenderWidth           = Width;
    Dimensions.RenderHeight          = Height;
    Dimensions.DisplayWidth          = Width;
    Dimensions.DisplayHeight         = Height;
    Dimensions.SuperResolutionActive = false;

    return Resize(pDevice, Dimensions, Formats, CreateComputeOutput, CreateAccumulatedRadiance);
}

bool RTXPTRenderTargets::HasPostProcessTargets() const
{
    return m_OutputColor != nullptr &&
        (!m_AccumulatedRadianceRequested || m_AccumulatedRadiance != nullptr) &&
        m_ProcessedOutputColor != nullptr &&
        m_LdrColor != nullptr &&
        m_Depth != nullptr &&
        m_ScreenMotionVectors != nullptr &&
        m_TemporalFeedback1 != nullptr &&
        m_TemporalFeedback2 != nullptr &&
        m_CombinedHistoryClampRelax != nullptr &&
        (!m_Dimensions.SuperResolutionActive || m_SuperResolutionInputColor != nullptr);
}

ITextureView* RTXPTRenderTargets::GetOutputColorUAV() const
{
    return m_OutputColor ? m_OutputColor->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetOutputColorSRV() const
{
    return m_OutputColor ? m_OutputColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetAccumulatedRadianceUAV() const
{
    return m_AccumulatedRadiance ? m_AccumulatedRadiance->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetAccumulatedRadianceSRV() const
{
    return m_AccumulatedRadiance ? m_AccumulatedRadiance->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetProcessedOutputColorUAV() const
{
    return m_ProcessedOutputColor ? m_ProcessedOutputColor->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetProcessedOutputColorSRV() const
{
    return m_ProcessedOutputColor ? m_ProcessedOutputColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetProcessedOutputColorRTV() const
{
    return m_ProcessedOutputColor ? m_ProcessedOutputColor->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetLdrColorUAV() const
{
    return m_LdrColor ? m_LdrColor->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetLdrColorSRV() const
{
    return m_LdrColor ? m_LdrColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetLdrColorRTV() const
{
    return m_LdrColor ? m_LdrColor->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET) : nullptr;
}

ITexture* RTXPTRenderTargets::GetLdrColorTexture() const
{
    return m_LdrColor;
}

ITextureView* RTXPTRenderTargets::GetComputeColorUAV() const
{
    return m_ComputeColor ? m_ComputeColor->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetComputeColorSRV() const
{
    return m_ComputeColor ? m_ComputeColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetPresentationSRV() const
{
    return GetLdrColorSRV();
}

ITextureView* RTXPTRenderTargets::GetSuperResolutionInputColorUAV() const
{
    return m_SuperResolutionInputColor ? m_SuperResolutionInputColor->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetSuperResolutionInputColorSRV() const
{
    return m_SuperResolutionInputColor ? m_SuperResolutionInputColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetDepthUAV() const
{
    return m_Depth ? m_Depth->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetDepthSRV() const
{
    return m_Depth ? m_Depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetScreenMotionVectorsUAV() const
{
    return m_ScreenMotionVectors ? m_ScreenMotionVectors->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetScreenMotionVectorsSRV() const
{
    return m_ScreenMotionVectors ? m_ScreenMotionVectors->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetTemporalFeedback1UAV() const
{
    return m_TemporalFeedback1 ? m_TemporalFeedback1->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetTemporalFeedback1SRV() const
{
    return m_TemporalFeedback1 ? m_TemporalFeedback1->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetTemporalFeedback2UAV() const
{
    return m_TemporalFeedback2 ? m_TemporalFeedback2->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetTemporalFeedback2SRV() const
{
    return m_TemporalFeedback2 ? m_TemporalFeedback2->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetCombinedHistoryClampRelaxUAV() const
{
    return m_CombinedHistoryClampRelax ? m_CombinedHistoryClampRelax->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetCombinedHistoryClampRelaxSRV() const
{
    return m_CombinedHistoryClampRelax ? m_CombinedHistoryClampRelax->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetAccumulationOutputUAV() const
{
    return IsSuperResolutionActive() ? GetSuperResolutionInputColorUAV() : GetProcessedOutputColorUAV();
}

ITextureView* RTXPTRenderTargets::GetSuperResolutionColorSRV() const
{
    return IsSuperResolutionActive() ? GetSuperResolutionInputColorSRV() : GetProcessedOutputColorSRV();
}

ITextureView* RTXPTRenderTargets::GetSuperResolutionOutputUAV() const
{
    return GetProcessedOutputColorUAV();
}

} // namespace Diligent
