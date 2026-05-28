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

namespace Diligent
{

void RTXPTRenderTargets::Reset()
{
    m_OutputColor.Release();
    m_ComputeColor.Release();
    m_AccumColor.Release();
    m_AccumulationUnavailable = false;
    m_Width  = 0;
    m_Height = 0;
    m_Format = TEX_FORMAT_UNKNOWN;
    m_LastError.clear();
}

bool RTXPTRenderTargets::CreateTarget(IRenderDevice* pDevice, const char* Name, RefCntAutoPtr<ITexture>& Target)
{
    TextureDesc Desc;
    Desc.Name      = Name;
    Desc.Type      = RESOURCE_DIM_TEX_2D;
    Desc.Width     = m_Width;
    Desc.Height    = m_Height;
    Desc.Format    = m_Format;
    Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;

    Target.Release();
    pDevice->CreateTexture(Desc, nullptr, &Target);
    if (!Target)
    {
        m_LastError = std::string{"Failed to create "} + Name;
        return false;
    }

    if (Target->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) == nullptr ||
        Target->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) == nullptr)
    {
        m_LastError = std::string{Name} + " is missing SRV or UAV";
        Target.Release();
        return false;
    }

    return true;
}

bool RTXPTRenderTargets::Resize(IRenderDevice* pDevice,
                                Uint32         Width,
                                Uint32         Height,
                                TEXTURE_FORMAT Format,
                                bool           CreateComputeOutput,
                                bool           CreateAccumulation)
{
    if (Width == 0 || Height == 0)
        return false;

    const bool HasRequestedTargets =
        m_OutputColor != nullptr &&
        (!CreateComputeOutput || m_ComputeColor != nullptr) &&
        (CreateComputeOutput || m_ComputeColor == nullptr) &&
        (!CreateAccumulation || m_AccumColor != nullptr || m_AccumulationUnavailable) &&
        (CreateAccumulation || m_AccumColor == nullptr);

    if (HasRequestedTargets && m_Width == Width && m_Height == Height && m_Format == Format)
        return true;

    Reset();
    m_Width  = Width;
    m_Height = Height;
    m_Format = Format;

    if (!CreateTarget(pDevice, "RTXPT OutputColor", m_OutputColor))
        return false;

    if (CreateComputeOutput && !CreateTarget(pDevice, "RTXPT ComputeColor", m_ComputeColor))
        return false;

    if (CreateAccumulation)
    {
        const TEXTURE_FORMAT AccumFormat = TEX_FORMAT_RGBA32_FLOAT;
        const auto&          FmtInfo     = pDevice->GetTextureFormatInfoExt(AccumFormat);
        const bool           SupportsUAV = (FmtInfo.BindFlags & BIND_UNORDERED_ACCESS) != 0;
        if (!SupportsUAV)
        {
            m_LastError                 = "RGBA32F UAV is not supported; reference path tracer accumulation is disabled";
            m_AccumulationUnavailable  = true;
            return true;
        }

        TextureDesc Desc;
        Desc.Name      = "RTXPT AccumColor";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = m_Width;
        Desc.Height    = m_Height;
        Desc.Format    = AccumFormat;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
        pDevice->CreateTexture(Desc, nullptr, &m_AccumColor);

        if (!m_AccumColor)
        {
            m_LastError = "Failed to create RTXPT AccumColor";
            return false;
        }
    }

    return true;
}

ITextureView* RTXPTRenderTargets::GetOutputColorUAV() const
{
    return m_OutputColor ? m_OutputColor->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetOutputColorSRV() const
{
    return m_OutputColor ? m_OutputColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetComputeColorUAV() const
{
    return m_ComputeColor ? m_ComputeColor->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetComputeColorSRV() const
{
    return m_ComputeColor ? m_ComputeColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetAccumColorUAV() const
{
    return m_AccumColor ? m_AccumColor->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetAccumColorSRV() const
{
    return m_AccumColor ? m_AccumColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RTXPTRenderTargets::GetDisplaySRV(bool UseComputeOutput) const
{
    if (UseComputeOutput && m_ComputeColor)
        return GetComputeColorSRV();

    return GetOutputColorSRV();
}

} // namespace Diligent
