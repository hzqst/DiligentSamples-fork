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
#include "RTXPTSceneGraph.hpp"

#include "DebugUtilities.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

constexpr Uint32 InvalidTextureIndex = ~Uint32{0};

RefCntAutoPtr<ITextureView> CreateMaterialTextureView(ITexture* pTexture)
{
    if (pTexture == nullptr)
        return {};

    TextureViewDesc ViewDesc;
    ViewDesc.ViewType   = TEXTURE_VIEW_SHADER_RESOURCE;
    ViewDesc.TextureDim = RESOURCE_DIM_TEX_2D_ARRAY;

    RefCntAutoPtr<ITextureView> pSRV;
    pTexture->CreateView(ViewDesc, &pSRV);
    return pSRV;
}

void FillMaterialPTDataFromGLTF(const GLTF::Material& Material, MaterialPTData& Data)
{
    const GLTF::Material::ShaderAttribs& Attribs = Material.Attribs;

    Data.baseColorFactor = Attribs.BaseColorFactor;
    Data.emissiveFactor  = Attribs.EmissiveFactor;
    Data.alphaCutoff     = Attribs.AlphaCutoff;
    Data.metallicFactor  = Attribs.MetallicFactor;
    Data.roughnessFactor = Attribs.RoughnessFactor;
    Data.normalScale     = Attribs.NormalScale;

    const int BaseColorTextureId = Material.GetTextureId(GLTF::DefaultBaseColorTextureAttribId);
    if (BaseColorTextureId >= 0)
    {
        Data.flags |= kMaterialFlag_HasBaseColorTexture;
        Data.baseColorTextureIndex = static_cast<Uint32>(BaseColorTextureId);
        Data.baseColorTextureSlice = Material.GetTextureAttrib(GLTF::DefaultBaseColorTextureAttribId).TextureSlice;
    }

    const int EmissiveTextureId = Material.GetTextureId(GLTF::DefaultEmissiveTextureAttribId);
    if (EmissiveTextureId >= 0)
    {
        Data.flags |= kMaterialFlag_HasEmissiveTexture;
        Data.emissiveTextureIndex = static_cast<Uint32>(EmissiveTextureId);
        Data.emissiveTextureSlice = Material.GetTextureAttrib(GLTF::DefaultEmissiveTextureAttribId).TextureSlice;
    }

    const int MetallicRoughnessTextureId = Material.GetTextureId(GLTF::DefaultMetallicRoughnessTextureAttribId);
    if (MetallicRoughnessTextureId >= 0)
    {
        Data.flags |= kMaterialFlag_HasMetallicRoughnessTexture;
        Data.metallicRoughnessTextureIndex = static_cast<Uint32>(MetallicRoughnessTextureId);
        Data.metallicRoughnessTextureSlice = Material.GetTextureAttrib(GLTF::DefaultMetallicRoughnessTextureAttribId).TextureSlice;
    }

    const int NormalTextureId = Material.GetTextureId(GLTF::DefaultNormalTextureAttribId);
    if (NormalTextureId >= 0)
    {
        Data.flags |= kMaterialFlag_HasNormalTexture;
        Data.normalTextureIndex = static_cast<Uint32>(NormalTextureId);
        Data.normalTextureSlice = Material.GetTextureAttrib(GLTF::DefaultNormalTextureAttribId).TextureSlice;
    }

    if (RTXPTMaterialIsAlphaTested(Material) && (Data.flags & kMaterialFlag_HasBaseColorTexture) != 0u)
        Data.flags |= kMaterialFlag_AlphaTested;
}

void RemapTextureIndex(Uint32 Flag, Uint32& Flags, Uint32& TextureIndex, const std::vector<Uint32>& TextureRemap)
{
    if ((Flags & Flag) == 0u)
        return;

    if (TextureIndex >= TextureRemap.size() || TextureRemap[TextureIndex] == InvalidTextureIndex)
    {
        Flags &= ~Flag;
        TextureIndex = 0;
        return;
    }

    TextureIndex = TextureRemap[TextureIndex];
}

void RemapMaterialTextureIndices(MaterialPTData& Data, const std::vector<Uint32>& TextureRemap)
{
    RemapTextureIndex(kMaterialFlag_HasBaseColorTexture, Data.flags, Data.baseColorTextureIndex, TextureRemap);
    RemapTextureIndex(kMaterialFlag_HasEmissiveTexture, Data.flags, Data.emissiveTextureIndex, TextureRemap);
    RemapTextureIndex(kMaterialFlag_HasMetallicRoughnessTexture, Data.flags, Data.metallicRoughnessTextureIndex, TextureRemap);
    RemapTextureIndex(kMaterialFlag_HasNormalTexture, Data.flags, Data.normalTextureIndex, TextureRemap);

    if ((Data.flags & kMaterialFlag_HasBaseColorTexture) == 0u)
        Data.flags &= ~kMaterialFlag_AlphaTested;
}

} // namespace

bool RTXPTMaterialIsAlphaTested(const GLTF::Material& Material)
{
    return Material.Attribs.AlphaMode == GLTF::Material::ALPHA_MODE_MASK &&
        Material.GetTextureId(GLTF::DefaultBaseColorTextureAttribId) >= 0;
}

const RTXPTMaterialExtension* RTXPTGetMaterialExtension(const RTXPTSceneGraphData& SceneData,
                                                        const RTXPTModelAsset&     Asset,
                                                        Uint32                     MaterialId)
{
    if (MaterialId >= Asset.MaterialRemap.size())
        return nullptr;

    const Uint32 ExtensionIdx = Asset.MaterialRemap[MaterialId];
    if (ExtensionIdx >= SceneData.MaterialExtensions.size())
        return nullptr;

    return &SceneData.MaterialExtensions[ExtensionIdx];
}

bool RTXPTMaterialHasBaseColorTexture(const GLTF::Model&             Model,
                                      const GLTF::Material&          Material,
                                      const RTXPTMaterialExtension*  pExtension)
{
    if (pExtension != nullptr && pExtension->Loaded && !pExtension->EnableBaseTexture)
        return false;

    const int BaseColorTextureId = Material.GetTextureId(GLTF::DefaultBaseColorTextureAttribId);
    if (BaseColorTextureId < 0)
        return false;

    const Uint32 TextureId = static_cast<Uint32>(BaseColorTextureId);
    if (TextureId >= static_cast<Uint32>(Model.GetTextureCount()))
        return false;

    RefCntAutoPtr<ITextureView> pSRV = CreateMaterialTextureView(Model.GetTexture(TextureId));
    return pSRV != nullptr;
}

bool RTXPTMaterialIsAlphaTested(const GLTF::Material&          Material,
                                const RTXPTMaterialExtension*  pExtension,
                                bool                           HasBaseColorTexture)
{
    if (!HasBaseColorTexture)
        return false;

    const bool ExtensionAlphaTested = pExtension != nullptr && pExtension->Loaded && pExtension->EnableAlphaTesting;
    return Material.Attribs.AlphaMode == GLTF::Material::ALPHA_MODE_MASK || ExtensionAlphaTested;
}

void RTXPTMaterials::Reset()
{
    m_MaterialBuffer.Release();
    m_TextureBindings.clear();
    m_TextureViews.clear();
    m_Stats = {};
}

void RTXPTMaterials::AppendTextureViews(const GLTF::Model& Model, std::vector<Uint32>& TextureRemap)
{
    const Uint32 ModelTextureCount = static_cast<Uint32>(Model.GetTextureCount());
    TextureRemap.assign(ModelTextureCount, InvalidTextureIndex);
    m_TextureViews.reserve(m_TextureViews.size() + ModelTextureCount);
    m_TextureBindings.reserve(m_TextureBindings.size() + ModelTextureCount);

    for (Uint32 i = 0; i < ModelTextureCount; ++i)
    {
        RefCntAutoPtr<ITextureView> pSRV = CreateMaterialTextureView(Model.GetTexture(i));
        if (!pSRV)
            continue;

        TextureRemap[i] = static_cast<Uint32>(m_TextureBindings.size());
        m_TextureViews.emplace_back(std::move(pSRV));
        m_TextureBindings.push_back(m_TextureViews.back().RawPtr<IDeviceObject>());
    }
}

bool RTXPTMaterials::CreateMaterialBuffer(IRenderDevice* pDevice, const std::vector<MaterialPTData>& MaterialData)
{
    BufferDesc Desc;
    Desc.Name              = "RTXPT material buffer";
    Desc.Usage             = USAGE_IMMUTABLE;
    Desc.BindFlags         = BIND_SHADER_RESOURCE;
    Desc.Mode              = BUFFER_MODE_STRUCTURED;
    Desc.ElementByteStride = sizeof(MaterialPTData);
    Desc.Size              = sizeof(MaterialPTData) * MaterialData.size();

    BufferData Data{MaterialData.data(), Desc.Size};
    pDevice->CreateBuffer(Desc, &Data, &m_MaterialBuffer);

    VERIFY(m_MaterialBuffer, "Failed to create RTXPT material buffer");
    return m_MaterialBuffer != nullptr;
}

bool RTXPTMaterials::Upload(IRenderDevice* pDevice, const GLTF::Model& Model)
{
    Reset();

    std::vector<Uint32> TextureRemap;
    AppendTextureViews(Model, TextureRemap);
    m_Stats.TextureCount = static_cast<Uint32>(m_TextureBindings.size());

    std::vector<MaterialPTData> MaterialData;
    MaterialData.reserve(std::max<size_t>(Model.Materials.size(), 1));
    for (const GLTF::Material& Material : Model.Materials)
    {
        MaterialPTData Data;
        FillMaterialPTDataFromGLTF(Material, Data);
        RemapMaterialTextureIndices(Data, TextureRemap);
        MaterialData.emplace_back(Data);
    }

    if (MaterialData.empty())
        MaterialData.emplace_back();

    m_Stats.MaterialCount = static_cast<Uint32>(MaterialData.size());
    return CreateMaterialBuffer(pDevice, MaterialData);
}

bool RTXPTMaterials::Upload(IRenderDevice* pDevice, const RTXPTSceneGraphData& SceneData)
{
    Reset();

    std::vector<std::vector<Uint32>> TextureRemaps(SceneData.ModelAssets.size());
    for (Uint32 AssetIdx = 0; AssetIdx < SceneData.ModelAssets.size(); ++AssetIdx)
    {
        const RTXPTModelAsset& Asset = SceneData.ModelAssets[AssetIdx];
        if (Asset.Model)
            AppendTextureViews(*Asset.Model, TextureRemaps[AssetIdx]);
    }
    m_Stats.TextureCount = static_cast<Uint32>(m_TextureBindings.size());

    std::vector<MaterialPTData> MaterialData;
    for (Uint32 AssetIdx = 0; AssetIdx < SceneData.ModelAssets.size(); ++AssetIdx)
    {
        const RTXPTModelAsset& Asset = SceneData.ModelAssets[AssetIdx];
        if (!Asset.Model)
            continue;

        for (Uint32 MatIdx = 0; MatIdx < Asset.Model->Materials.size(); ++MatIdx)
        {
            const GLTF::Material& Material = Asset.Model->Materials[MatIdx];

            MaterialPTData Data;
            FillMaterialPTDataFromGLTF(Material, Data);
            RemapMaterialTextureIndices(Data, TextureRemaps[AssetIdx]);

            const RTXPTMaterialExtension* pExtension = RTXPTGetMaterialExtension(SceneData, Asset, MatIdx);
            if (pExtension != nullptr && pExtension->Loaded)
            {
                const RTXPTMaterialExtension& Ext = *pExtension;
                Data.baseColorFactor             = Ext.BaseColorFactor;
                Data.emissiveFactor              = Ext.EmissiveFactor;
                Data.alphaCutoff                 = Ext.AlphaCutoff;
                Data.metallicFactor              = Ext.MetallicFactor;
                Data.roughnessFactor             = Ext.RoughnessFactor;
                if (!Ext.EnableBaseTexture)
                    Data.flags &= ~kMaterialFlag_HasBaseColorTexture;
                if (!Ext.EnableEmissiveTexture)
                    Data.flags &= ~kMaterialFlag_HasEmissiveTexture;
                if (!Ext.EnableNormalTexture)
                    Data.flags &= ~kMaterialFlag_HasNormalTexture;
                if (!Ext.EnableOcclusionRoughnessMetallicTexture)
                    Data.flags &= ~kMaterialFlag_HasMetallicRoughnessTexture;
            }

            Data.flags &= ~kMaterialFlag_AlphaTested;
            if (RTXPTMaterialIsAlphaTested(Material, pExtension, (Data.flags & kMaterialFlag_HasBaseColorTexture) != 0u))
                Data.flags |= kMaterialFlag_AlphaTested;

            MaterialData.emplace_back(Data);
        }
    }

    if (MaterialData.empty())
        MaterialData.emplace_back();

    m_Stats.MaterialCount = static_cast<Uint32>(MaterialData.size());
    return CreateMaterialBuffer(pDevice, MaterialData);
}

} // namespace Diligent
