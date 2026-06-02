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
        Lhs.ProcessedOutputColor == Rhs.ProcessedOutputColor &&
        Lhs.LdrColor == Rhs.LdrColor &&
        Lhs.ComputeColor == Rhs.ComputeColor;
}

} // namespace

void RTXPTRenderTargets::Reset()
{
    m_OutputColor.Release();
    m_AccumulatedRadiance.Release();
    m_ProcessedOutputColor.Release();
    m_LdrColor.Release();
    m_LdrColorScratch.Release();
    m_ComputeColor.Release();
    m_AccumulatedRadianceUnavailable = false;
    m_Width                          = 0;
    m_Height                         = 0;
    m_Formats                        = {};
}

bool RTXPTRenderTargets::CreateTarget(IRenderDevice* pDevice,
                                      const char*    Name,
                                      TEXTURE_FORMAT TargetFormat,
                                      BIND_FLAGS     BindFlags,
                                      RefCntAutoPtr<ITexture>& Target)
{
    TextureDesc Desc;
    Desc.Name      = Name;
    Desc.Type      = RESOURCE_DIM_TEX_2D;
    Desc.Width     = m_Width;
    Desc.Height    = m_Height;
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

bool RTXPTRenderTargets::Resize(IRenderDevice*                  pDevice,
                                Uint32                          Width,
                                Uint32                          Height,
                                const RTXPTRenderTargetFormats& Formats,
                                bool                            CreateComputeOutput,
                                bool                            CreateAccumulatedRadiance)
{
    if (pDevice == nullptr || Width == 0 || Height == 0)
        return false;

    const bool HasCorePostProcessTargets =
        m_OutputColor != nullptr &&
        m_ProcessedOutputColor != nullptr &&
        m_LdrColor != nullptr &&
        m_LdrColorScratch != nullptr;
    const bool HasRequestedTargets =
        HasCorePostProcessTargets &&
        (!CreateComputeOutput || m_ComputeColor != nullptr) &&
        (CreateComputeOutput || m_ComputeColor == nullptr) &&
        (!CreateAccumulatedRadiance || m_AccumulatedRadiance != nullptr || m_AccumulatedRadianceUnavailable) &&
        (CreateAccumulatedRadiance || m_AccumulatedRadiance == nullptr);

    if (HasRequestedTargets && m_Width == Width && m_Height == Height && FormatsMatch(m_Formats, Formats))
        return true;

    Reset();
    m_Width   = Width;
    m_Height  = Height;
    m_Formats = Formats;

    const BIND_FLAGS HdrUavFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
    const BIND_FLAGS HdrRtFlags  = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS | BIND_RENDER_TARGET;
    const BIND_FLAGS LdrRtFlags  = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS | BIND_RENDER_TARGET;

    if (!SupportsBindFlags(pDevice, m_Formats.OutputColor, HdrUavFlags))
    {
        LOG_ERROR_MESSAGE("RGBA16F UAV OutputColor is not supported; RTXPT post-processing resource graph is unavailable");
        return false;
    }

    if (CreateAccumulatedRadiance && !SupportsBindFlags(pDevice, m_Formats.AccumulatedRadiance, HdrUavFlags))
    {
        LOG_ERROR_MESSAGE("RGBA32F UAV AccumulatedRadiance is not supported; reference accumulation is unavailable");
        m_AccumulatedRadianceUnavailable = true;
        return false;
    }

    if (!SupportsBindFlags(pDevice, m_Formats.ProcessedOutputColor, HdrRtFlags))
    {
        LOG_ERROR_MESSAGE("HDR UAV/RTV ProcessedOutputColor is not supported; RTXPT post-processing resource graph is unavailable");
        return false;
    }

    if (!SupportsBindFlags(pDevice, m_Formats.LdrColor, LdrRtFlags))
    {
        LOG_ERROR_MESSAGE("LDR UAV/RTV targets are not supported; RTXPT post-processing resource graph is unavailable");
        return false;
    }

    if (!CreateTarget(pDevice, "RTXPT OutputColor", m_Formats.OutputColor, HdrUavFlags, m_OutputColor))
        return false;

    if (CreateAccumulatedRadiance &&
        !m_AccumulatedRadianceUnavailable &&
        !CreateTarget(pDevice, "RTXPT AccumulatedRadiance", m_Formats.AccumulatedRadiance, HdrUavFlags, m_AccumulatedRadiance))
        return false;

    if (!CreateTarget(pDevice, "RTXPT ProcessedOutputColor", m_Formats.ProcessedOutputColor, HdrRtFlags, m_ProcessedOutputColor))
        return false;

    if (!CreateTarget(pDevice, "RTXPT LdrColor", m_Formats.LdrColor, LdrRtFlags, m_LdrColor))
        return false;

    if (!CreateTarget(pDevice, "RTXPT LdrColorScratch", m_Formats.LdrColor, LdrRtFlags, m_LdrColorScratch))
        return false;

    if (CreateComputeOutput &&
        !CreateTarget(pDevice, "RTXPT ComputeColor", m_Formats.ComputeColor, HdrUavFlags, m_ComputeColor))
        return false;

    return true;
}

bool RTXPTRenderTargets::HasPostProcessTargets() const
{
    return m_OutputColor != nullptr &&
        m_AccumulatedRadiance != nullptr &&
        m_ProcessedOutputColor != nullptr &&
        m_LdrColor != nullptr &&
        m_LdrColorScratch != nullptr;
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

ITextureView* RTXPTRenderTargets::GetLdrColorUAV() const
{
    return m_LdrColor ? m_LdrColor->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetLdrColorSRV() const
{
    return m_LdrColor ? m_LdrColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetLdrColorScratchUAV() const
{
    return m_LdrColorScratch ? m_LdrColorScratch->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetLdrColorScratchSRV() const
{
    return m_LdrColorScratch ? m_LdrColorScratch->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetComputeColorUAV() const
{
    return m_ComputeColor ? m_ComputeColor->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetComputeColorSRV() const
{
    return m_ComputeColor ? m_ComputeColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetDisplaySRV(bool UseComputeOutput) const
{
    if (UseComputeOutput && m_ComputeColor)
        return GetComputeColorSRV();

    return GetOutputColorSRV();
}

} // namespace Diligent
