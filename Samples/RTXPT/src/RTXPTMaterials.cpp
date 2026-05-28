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
#include <cstddef>
#include <vector>

namespace Diligent
{

namespace
{

using MaterialShaderAttribs = GLTF::Material::ShaderAttribs;
static_assert(sizeof(MaterialShaderAttribs) == 96, "MaterialShaderAttribs layout must match RTXPTShaderShared.hlsli");
static_assert(offsetof(MaterialShaderAttribs, BaseColorFactor) == 0, "Unexpected BaseColorFactor offset");
static_assert(offsetof(MaterialShaderAttribs, EmissiveFactor) == 16, "Unexpected EmissiveFactor offset");
static_assert(offsetof(MaterialShaderAttribs, SpecularFactor) == 32, "Unexpected SpecularFactor offset");
static_assert(offsetof(MaterialShaderAttribs, Workflow) == 48, "Unexpected Workflow offset");
static_assert(offsetof(MaterialShaderAttribs, RoughnessFactor) == 64, "Unexpected RoughnessFactor offset");
static_assert(offsetof(MaterialShaderAttribs, CustomData) == 80, "Unexpected CustomData offset");

} // namespace

void RTXPTMaterials::Reset()
{
    m_MaterialBuffer.Release();
    m_Stats = {};
}

bool RTXPTMaterials::Upload(IRenderDevice* pDevice, const GLTF::Model& Model)
{
    Reset();

    m_Stats.MaterialCount = static_cast<Uint32>(Model.Materials.size());

    std::vector<GLTF::Material::ShaderAttribs> Materials;
    Materials.reserve(std::max<size_t>(Model.Materials.size(), 1));
    for (const GLTF::Material& Material : Model.Materials)
        Materials.emplace_back(Material.Attribs);

    if (Materials.empty())
    {
        // Always upload at least one default material so the shader-side bridge SRV is never null.
        Materials.emplace_back();
    }

    BufferDesc Desc;
    Desc.Name              = "RTXPT material buffer";
    Desc.Usage             = USAGE_IMMUTABLE;
    Desc.BindFlags         = BIND_SHADER_RESOURCE;
    Desc.Mode              = BUFFER_MODE_STRUCTURED;
    Desc.ElementByteStride = sizeof(GLTF::Material::ShaderAttribs);
    Desc.Size              = sizeof(GLTF::Material::ShaderAttribs) * Materials.size();

    BufferData Data{Materials.data(), Desc.Size};
    pDevice->CreateBuffer(Desc, &Data, &m_MaterialBuffer);

    if (!m_MaterialBuffer)
    {
        m_Stats.LastError = "Failed to create RTXPT material buffer";
        return false;
    }

    return true;
}

} // namespace Diligent
