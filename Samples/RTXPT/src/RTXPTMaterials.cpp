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

#include "RTXPTMaterials.hpp"

#include <algorithm>
#include <vector>

namespace Diligent
{

bool RTXPTMaterialIsAlphaTested(const GLTF::Material& Material)
{
    return Material.Attribs.AlphaMode == GLTF::Material::ALPHA_MODE_MASK &&
        Material.GetTextureId(GLTF::DefaultBaseColorTextureAttribId) >= 0;
}

void RTXPTMaterials::Reset()
{
    m_MaterialBuffer.Release();
    m_TextureBindings.clear();
    m_Stats = {};
}

bool RTXPTMaterials::Upload(IRenderDevice* pDevice, const GLTF::Model& Model)
{
    Reset();

    m_Stats.MaterialCount = static_cast<Uint32>(Model.Materials.size());

    // Collect one shader-resource view per loaded GLTF texture. The loader always provides a (stub) texture,
    // so a null view should not happen; if it does, drop the whole table and fall back to factor-only shading.
    const Uint32 ModelTextureCount = static_cast<Uint32>(Model.GetTextureCount());
    m_TextureBindings.reserve(ModelTextureCount);
    for (Uint32 i = 0; i < ModelTextureCount; ++i)
    {
        ITexture*     pTexture = Model.GetTexture(i);
        ITextureView* pSRV     = pTexture != nullptr ? pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
        if (pSRV == nullptr)
        {
            m_TextureBindings.clear();
            m_Stats.LastError = "RTXPT material texture has no shader-resource view; texture sampling disabled";
            break;
        }
        m_TextureBindings.push_back(pSRV);
    }
    m_Stats.TextureCount = static_cast<Uint32>(m_TextureBindings.size());

    const Uint32 ValidTextureCount = m_Stats.TextureCount;

    std::vector<RTXPTMaterialData> MaterialData;
    MaterialData.reserve(std::max<size_t>(Model.Materials.size(), 1));
    for (const GLTF::Material& Material : Model.Materials)
    {
        const GLTF::Material::ShaderAttribs& Attribs = Material.Attribs;

        RTXPTMaterialData Data;
        Data.BaseColorFactor = Attribs.BaseColorFactor;
        Data.EmissiveFactor  = Attribs.EmissiveFactor;
        Data.AlphaCutoff     = Attribs.AlphaCutoff;
        Data.MetallicFactor  = Attribs.MetallicFactor;
        Data.RoughnessFactor = Attribs.RoughnessFactor;

        const int BaseColorTextureId = Material.GetTextureId(GLTF::DefaultBaseColorTextureAttribId);
        if (BaseColorTextureId >= 0 && static_cast<Uint32>(BaseColorTextureId) < ValidTextureCount)
        {
            Data.Flags |= kRTXPTMaterialFlag_HasBaseColorTexture;
            Data.BaseColorTextureIndex = static_cast<Uint32>(BaseColorTextureId);
            Data.BaseColorTextureSlice = Material.GetTextureAttrib(GLTF::DefaultBaseColorTextureAttribId).TextureSlice;
        }

        const int EmissiveTextureId = Material.GetTextureId(GLTF::DefaultEmissiveTextureAttribId);
        if (EmissiveTextureId >= 0 && static_cast<Uint32>(EmissiveTextureId) < ValidTextureCount)
        {
            Data.Flags |= kRTXPTMaterialFlag_HasEmissiveTexture;
            Data.EmissiveTextureIndex = static_cast<Uint32>(EmissiveTextureId);
            Data.EmissiveTextureSlice = Material.GetTextureAttrib(GLTF::DefaultEmissiveTextureAttribId).TextureSlice;
        }

        // Alpha test requires the base-color texture (its .a channel). Only set the flag when both agree.
        if (RTXPTMaterialIsAlphaTested(Material) && (Data.Flags & kRTXPTMaterialFlag_HasBaseColorTexture) != 0u)
            Data.Flags |= kRTXPTMaterialFlag_AlphaTested;

        MaterialData.emplace_back(Data);
    }

    if (MaterialData.empty())
    {
        // Always upload at least one default material so the shader-side bridge SRV is never null.
        MaterialData.emplace_back();
    }

    BufferDesc Desc;
    Desc.Name              = "RTXPT material buffer";
    Desc.Usage             = USAGE_IMMUTABLE;
    Desc.BindFlags         = BIND_SHADER_RESOURCE;
    Desc.Mode              = BUFFER_MODE_STRUCTURED;
    Desc.ElementByteStride = sizeof(RTXPTMaterialData);
    Desc.Size              = sizeof(RTXPTMaterialData) * MaterialData.size();

    BufferData Data{MaterialData.data(), Desc.Size};
    pDevice->CreateBuffer(Desc, &Data, &m_MaterialBuffer);

    if (!m_MaterialBuffer)
    {
        m_Stats.LastError = "Failed to create RTXPT material buffer";
        return false;
    }

    return true;
}

} // namespace Diligent
