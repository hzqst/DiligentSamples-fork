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

#include "RTXPTLights.hpp"

#include <vector>

namespace Diligent
{

namespace
{

float LightTypeToShaderValue(GLTF::Light::TYPE Type)
{
    switch (Type)
    {
        case GLTF::Light::TYPE::DIRECTIONAL: return 0.0f;
        case GLTF::Light::TYPE::POINT:       return 1.0f;
        case GLTF::Light::TYPE::SPOT:        return 2.0f;
        default:                             return -1.0f;
    }
}

RTXPTLightData MakeLightData(const GLTF::Light& Light, const float4x4& NodeTransform)
{
    RTXPTLightData Data;
    Data.ColorIntensity = float4{Light.Color.x, Light.Color.y, Light.Color.z, Light.Intensity};
    Data.PositionRange  = float4{NodeTransform._41, NodeTransform._42, NodeTransform._43, Light.Range};
    Data.DirectionType  = float4{-NodeTransform._31, -NodeTransform._32, -NodeTransform._33, LightTypeToShaderValue(Light.Type)};
    Data.SpotAngles     = float4{Light.InnerConeAngle, Light.OuterConeAngle, 0.0f, 0.0f};
    return Data;
}

} // namespace

void RTXPTLights::Reset()
{
    m_LightBuffer.Release();
    m_Stats = {};
}

bool RTXPTLights::Upload(IRenderDevice* pDevice, const GLTF::Scene& Scene, const GLTF::ModelTransforms& Transforms)
{
    Reset();

    std::vector<RTXPTLightData> Lights;
    for (const GLTF::Node* pNode : Scene.LinearNodes)
    {
        if (pNode == nullptr || pNode->pLight == nullptr)
            continue;

        if (pNode->Index < 0 || static_cast<size_t>(pNode->Index) >= Transforms.NodeGlobalMatrices.size())
            continue;

        Lights.emplace_back(MakeLightData(*pNode->pLight, Transforms.NodeGlobalMatrices[pNode->Index]));
    }

    m_Stats.LightCount = static_cast<Uint32>(Lights.size());
    if (Lights.empty())
        return true;

    BufferDesc Desc;
    Desc.Name              = "RTXPT light buffer";
    Desc.Usage             = USAGE_IMMUTABLE;
    Desc.BindFlags         = BIND_SHADER_RESOURCE;
    Desc.Mode              = BUFFER_MODE_STRUCTURED;
    Desc.ElementByteStride = sizeof(RTXPTLightData);
    Desc.Size              = sizeof(RTXPTLightData) * Lights.size();

    BufferData Data{Lights.data(), Desc.Size};
    pDevice->CreateBuffer(Desc, &Data, &m_LightBuffer);

    if (!m_LightBuffer)
    {
        m_Stats.LastError = "Failed to create RTXPT light buffer";
        return false;
    }

    return true;
}

} // namespace Diligent
