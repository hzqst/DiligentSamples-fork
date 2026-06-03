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

#pragma once

#include "RenderDevice.h"
#include "RefCntAutoPtr.hpp"
#include "Texture.h"
#include "TextureView.h"

namespace Diligent
{

struct RTXPTRenderTargetFormats
{
    TEXTURE_FORMAT OutputColor          = TEX_FORMAT_RGBA16_FLOAT;
    TEXTURE_FORMAT AccumulatedRadiance  = TEX_FORMAT_RGBA32_FLOAT;
    TEXTURE_FORMAT ProcessedOutputColor = TEX_FORMAT_RGBA16_FLOAT;
    TEXTURE_FORMAT LdrColor             = TEX_FORMAT_RGBA8_UNORM;
    TEXTURE_FORMAT ComputeColor         = TEX_FORMAT_RGBA8_UNORM;
};

class RTXPTRenderTargets
{
public:
    void Reset();
    bool Resize(IRenderDevice*                  pDevice,
                Uint32                          Width,
                Uint32                          Height,
                const RTXPTRenderTargetFormats& Formats,
                bool                            CreateComputeOutput,
                bool                            CreateAccumulatedRadiance);

    bool IsValid() const { return HasPostProcessTargets(); }
    bool HasPostProcessTargets() const;
    bool IsAccumulationActive() const { return m_AccumulatedRadiance != nullptr; }

    ITextureView* GetOutputColorUAV() const;
    ITextureView* GetOutputColorSRV() const;
    ITextureView* GetAccumulatedRadianceUAV() const;
    ITextureView* GetAccumulatedRadianceSRV() const;
    ITextureView* GetProcessedOutputColorUAV() const;
    ITextureView* GetProcessedOutputColorSRV() const;
    ITextureView* GetProcessedOutputColorRTV() const;
    ITextureView* GetLdrColorUAV() const;
    ITextureView* GetLdrColorSRV() const;
    ITextureView* GetLdrColorRTV() const;
    ITexture*     GetLdrColorTexture() const;
    ITextureView* GetLdrColorScratchUAV() const;
    ITextureView* GetLdrColorScratchSRV() const;
    ITexture*     GetLdrColorScratchTexture() const;
    ITextureView* GetComputeColorUAV() const;
    ITextureView* GetComputeColorSRV() const;
    ITextureView* GetPresentationSRV() const;

    Uint32         GetWidth() const { return m_Width; }
    Uint32         GetHeight() const { return m_Height; }
    TEXTURE_FORMAT GetOutputColorFormat() const { return m_Formats.OutputColor; }
    TEXTURE_FORMAT GetAccumulatedRadianceFormat() const { return m_Formats.AccumulatedRadiance; }
    TEXTURE_FORMAT GetProcessedOutputColorFormat() const { return m_Formats.ProcessedOutputColor; }
    TEXTURE_FORMAT GetLdrColorFormat() const { return m_Formats.LdrColor; }

private:
    bool CreateTarget(IRenderDevice* pDevice,
                      const char*    Name,
                      TEXTURE_FORMAT TargetFormat,
                      BIND_FLAGS     BindFlags,
                      RefCntAutoPtr<ITexture>& Target);
    bool SupportsBindFlags(IRenderDevice* pDevice, TEXTURE_FORMAT TargetFormat, BIND_FLAGS BindFlags) const;

    RefCntAutoPtr<ITexture>  m_OutputColor;
    RefCntAutoPtr<ITexture>  m_AccumulatedRadiance;
    RefCntAutoPtr<ITexture>  m_ProcessedOutputColor;
    RefCntAutoPtr<ITexture>  m_LdrColor;
    RefCntAutoPtr<ITexture>  m_LdrColorScratch;
    RefCntAutoPtr<ITexture>  m_ComputeColor;
    bool                     m_AccumulatedRadianceUnavailable = false;
    Uint32                   m_Width                          = 0;
    Uint32                   m_Height                         = 0;
    RTXPTRenderTargetFormats m_Formats;
};

} // namespace Diligent
