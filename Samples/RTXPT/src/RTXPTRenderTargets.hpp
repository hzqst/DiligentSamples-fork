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

class RTXPTRenderTargets
{
public:
    void Reset();
    bool Resize(IRenderDevice* pDevice,
                Uint32         Width,
                Uint32         Height,
                TEXTURE_FORMAT Format,
                bool           CreateComputeOutput,
                bool           CreateAccumulation);

    bool IsValid() const { return m_OutputColor != nullptr; }
    bool IsAccumulationActive() const { return m_AccumColor != nullptr; }

    ITextureView* GetOutputColorUAV() const;
    ITextureView* GetOutputColorSRV() const;
    ITextureView* GetComputeColorUAV() const;
    ITextureView* GetComputeColorSRV() const;
    ITextureView* GetAccumColorUAV() const;
    ITextureView* GetAccumColorSRV() const;
    ITextureView* GetDisplaySRV(bool UseComputeOutput) const;

    Uint32         GetWidth() const { return m_Width; }
    Uint32         GetHeight() const { return m_Height; }
    TEXTURE_FORMAT GetFormat() const { return m_Format; }

private:
    bool CreateTarget(IRenderDevice* pDevice, const char* Name, RefCntAutoPtr<ITexture>& Target);

    RefCntAutoPtr<ITexture> m_OutputColor;
    RefCntAutoPtr<ITexture> m_ComputeColor;
    RefCntAutoPtr<ITexture> m_AccumColor;
    bool                    m_AccumulationUnavailable = false;
    Uint32                  m_Width                   = 0;
    Uint32                  m_Height                  = 0;
    TEXTURE_FORMAT          m_Format                  = TEX_FORMAT_UNKNOWN;
};

} // namespace Diligent
