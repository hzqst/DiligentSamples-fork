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
#include "RTXPTMaterials.hpp"
#include "RTXPTSceneGraph.hpp"
#include "RTXPTSceneJson.hpp"

#include "DebugUtilities.hpp"

#include <algorithm>
#include <limits>
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
        case GLTF::Light::TYPE::POINT: return 1.0f;
        case GLTF::Light::TYPE::SPOT: return 2.0f;
        default: return -1.0f;
    }
}

float DegreesToRadians(float AngleInDegrees)
{
    return AngleInDegrees * (PI_F / 180.0f);
}

float ReadRTXPTSpotAngleRadians(const nlohmann::json& Json, const char* DegreesKey, const char* LegacyRadiansKey, float DefaultDegrees = 0.0f)
{
    const auto DegreesIt = Json.find(DegreesKey);
    if (DegreesIt != Json.end() && DegreesIt->is_number())
        return DegreesToRadians(DegreesIt->get<float>());

    // Legacy fallback keeps the old glTF-style cone field names in their original units.
    const auto LegacyIt = Json.find(LegacyRadiansKey);
    if (LegacyIt != Json.end() && LegacyIt->is_number())
        return LegacyIt->get<float>();

    return DegreesToRadians(DefaultDegrees);
}

PolymorphicLightInfo MakeLightData(const GLTF::Light& Light, const float4x4& NodeTransform)
{
    PolymorphicLightInfo Data;
    Data.colorIntensity = float4{Light.Color.x, Light.Color.y, Light.Color.z, Light.Intensity};
    Data.positionRange  = float4{NodeTransform._41, NodeTransform._42, NodeTransform._43, Light.Range};
    Data.directionType  = float4{-NodeTransform._31, -NodeTransform._32, -NodeTransform._33, LightTypeToShaderValue(Light.Type)};
    Data.spotAngles     = float4{Light.InnerConeAngle, Light.OuterConeAngle, 0.0f, 0.0f};
    return Data;
}

float ReadRTXPTLightIntensity(const nlohmann::json& Json)
{
    return ReadRTXPTOptionalFloat(Json, "intensity",
                                  ReadRTXPTOptionalFloat(Json, "irradiance", 1.0f));
}

Uint32 GetPrimitiveTriangleCount(const GLTF::Primitive& Primitive)
{
    return Primitive.HasIndices() ? Primitive.IndexCount / 3u : Primitive.VertexCount / 3u;
}

float MaxRGB(const float3& V)
{
    return std::max(V.x, std::max(V.y, V.z));
}

float GetMaterialEmissionMagnitude(const GLTF::Material& Material, const RTXPTMaterialExtension* pExtension)
{
    if (pExtension != nullptr && pExtension->Loaded)
        return MaxRGB(pExtension->EmissiveFactor);

    return MaxRGB(Material.Attribs.EmissiveFactor);
}

} // namespace

void RTXPTLights::Reset()
{
    m_LightBuffer.Release();
    m_EmissiveTriangleBuffer.Release();
    m_LightProxyBuffer.Release();
    m_AnalyticLights.clear();
    m_EmissiveProxyWeight = 0.0f;
    m_Stats = {};
}

bool RTXPTLights::UploadLightBuffer(IRenderDevice* pDevice, std::vector<PolymorphicLightInfo>& Lights)
{
    m_AnalyticLights = Lights;
    m_Stats.LightCount = static_cast<Uint32>(m_AnalyticLights.size());
    if (Lights.empty())
    {
        // Always upload at least one default (disabled) light so the shader-side bridge SRV is never null.
        PolymorphicLightInfo Default;
        Default.colorIntensity = float4{0, 0, 0, 0};
        Default.directionType  = float4{0, -1, 0, -1.0f}; // Type < 0 means unused.
        Lights.emplace_back(Default);
    }

    BufferDesc Desc;
    Desc.Name              = "RTXPT light buffer";
    Desc.Usage             = USAGE_IMMUTABLE;
    Desc.BindFlags         = BIND_SHADER_RESOURCE;
    Desc.Mode              = BUFFER_MODE_STRUCTURED;
    Desc.ElementByteStride = sizeof(PolymorphicLightInfo);
    Desc.Size              = sizeof(PolymorphicLightInfo) * Lights.size();

    BufferData Data{Lights.data(), Desc.Size};
    pDevice->CreateBuffer(Desc, &Data, &m_LightBuffer);

    VERIFY(m_LightBuffer, "Failed to create RTXPT light buffer");
    return m_LightBuffer != nullptr;
}

bool RTXPTLights::UploadLightProxyBuffer(IRenderDevice* pDevice)
{
    m_LightProxyBuffer.Release();
    if (pDevice == nullptr)
    {
        m_Stats.LastError = "RTXPT light proxy buffer requires a render device";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    std::vector<RTXPTLightProxy> Proxies;
    Proxies.reserve(m_AnalyticLights.size() + (m_Stats.EmissiveTriangleCount > 0 ? 1u : 0u));

    float Prefix = 0.0f;
    for (Uint32 LightIndex = 0; LightIndex < static_cast<Uint32>(m_AnalyticLights.size()); ++LightIndex)
    {
        const PolymorphicLightInfo& Light = m_AnalyticLights[LightIndex];
        const float3 Radiance = float3{Light.colorIntensity.x, Light.colorIntensity.y, Light.colorIntensity.z} *
            std::max(Light.colorIntensity.w, 0.0f);
        const float Weight = std::max(1e-6f, MaxRGB(Radiance));
        Prefix += Weight;
        Proxies.push_back(RTXPTLightProxy{Prefix, Weight, LightIndex, kLightProxyKind_Analytic});
    }

    if (m_Stats.EmissiveTriangleCount > 0)
    {
        const float Weight = std::max(1e-6f, m_EmissiveProxyWeight);
        Prefix += Weight;
        Proxies.push_back(RTXPTLightProxy{Prefix, Weight, 0u, kLightProxyKind_EmissiveBucket});
    }

    m_Stats.LightProxyCount       = static_cast<Uint32>(Proxies.size());
    m_Stats.LightProxyTotalWeight = Prefix;
    if (Proxies.empty())
        Proxies.emplace_back();

    BufferDesc Desc;
    Desc.Name              = "RTXPT light proxy buffer";
    Desc.Usage             = USAGE_IMMUTABLE;
    Desc.BindFlags         = BIND_SHADER_RESOURCE;
    Desc.Mode              = BUFFER_MODE_STRUCTURED;
    Desc.ElementByteStride = sizeof(RTXPTLightProxy);
    Desc.Size              = sizeof(RTXPTLightProxy) * Proxies.size();

    BufferData Data{Proxies.data(), Desc.Size};
    pDevice->CreateBuffer(Desc, &Data, &m_LightProxyBuffer);

    VERIFY(m_LightProxyBuffer, "Failed to create RTXPT light proxy buffer");
    return m_LightProxyBuffer != nullptr;
}

bool RTXPTLights::UploadEmissiveTriangleBuffer(IRenderDevice* pDevice, Uint32 EmissiveTriangleCount)
{
    m_EmissiveTriangleBuffer.Release();

    if (pDevice == nullptr)
    {
        m_Stats.LastError = "RTXPT emissive triangle buffer requires a render device";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    const Uint32 ElementCount = std::max<Uint32>(EmissiveTriangleCount, 1u);

    BufferDesc Desc;
    Desc.Name              = "RTXPT emissive triangle buffer";
    Desc.Usage             = USAGE_DEFAULT;
    Desc.BindFlags         = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
    Desc.Mode              = BUFFER_MODE_STRUCTURED;
    Desc.ElementByteStride = sizeof(EmissiveTriangle);
    Desc.Size              = Uint64{ElementCount} * sizeof(EmissiveTriangle);

    pDevice->CreateBuffer(Desc, nullptr, &m_EmissiveTriangleBuffer);

    VERIFY(m_EmissiveTriangleBuffer, "Failed to create RTXPT emissive triangle buffer");
    if (!m_EmissiveTriangleBuffer)
    {
        m_Stats.LastError = "Failed to create RTXPT emissive triangle buffer";
        return false;
    }

    return true;
}

bool RTXPTLights::Upload(IRenderDevice* pDevice, const GLTF::Scene& Scene, const GLTF::ModelTransforms& Transforms)
{
    Reset();

    std::vector<PolymorphicLightInfo> Lights;
    for (const GLTF::Node* pNode : Scene.LinearNodes)
    {
        if (pNode == nullptr || pNode->pLight == nullptr)
            continue;

        if (pNode->Index < 0 || static_cast<size_t>(pNode->Index) >= Transforms.NodeGlobalMatrices.size())
            continue;

        Lights.emplace_back(MakeLightData(*pNode->pLight, Transforms.NodeGlobalMatrices[pNode->Index]));
    }

    return UploadLightBuffer(pDevice, Lights);
}

bool RTXPTLights::Upload(IRenderDevice* pDevice, const RTXPTSceneGraphData& SceneData)
{
    Reset();

    std::vector<PolymorphicLightInfo> Lights;
    for (const RTXPTSceneLightMetadata& LightMeta : SceneData.Lights)
    {
        if (LightMeta.Type == "EnvironmentLight")
            continue;

        PolymorphicLightInfo Light;
        float                Color[3] = {1.0f, 1.0f, 1.0f};
        ReadRTXPTFloatArray(LightMeta.RawJson, "color", Color, 3);

        Light.colorIntensity = float4{Color[0], Color[1], Color[2], ReadRTXPTLightIntensity(LightMeta.RawJson)};
        Light.positionRange  = float4{LightMeta.GlobalTransform._41,
                                      LightMeta.GlobalTransform._42,
                                      LightMeta.GlobalTransform._43,
                                      ReadRTXPTOptionalFloat(LightMeta.RawJson, "range", 0.0f)};
        Light.directionType  = float4{-LightMeta.GlobalTransform._31,
                                      -LightMeta.GlobalTransform._32,
                                      -LightMeta.GlobalTransform._33,
                                      0.0f};
        Light.spotAngles     = float4{ReadRTXPTSpotAngleRadians(LightMeta.RawJson, "innerAngle", "innerConeAngle"),
                                      ReadRTXPTSpotAngleRadians(LightMeta.RawJson, "outerAngle", "outerConeAngle"),
                                      0.0f,
                                      0.0f};

        if (LightMeta.Type == "DirectionalLight")
            Light.directionType.w = 0.0f;
        else if (LightMeta.Type == "PointLight")
            Light.directionType.w = 1.0f;
        else if (LightMeta.Type == "SpotLight")
            Light.directionType.w = 2.0f;
        else
            continue;

        Lights.emplace_back(Light);
    }

    return UploadLightBuffer(pDevice, Lights);
}

bool RTXPTLights::UploadEmissiveTriangles(IRenderDevice* pDevice, const RTXPTSceneGraphData& SceneData)
{
    m_Stats.EmissiveTriangleCount = 0;
    m_Stats.LastError.clear();
    m_EmissiveProxyWeight = 0.0f;

    Uint64 EmissiveTriangleCount = 0;
    for (Uint32 InstanceId = 0; InstanceId < SceneData.ModelInstances.size(); ++InstanceId)
    {
        const RTXPTModelInstance& Instance = SceneData.ModelInstances[InstanceId];
        if (Instance.ModelAssetId >= SceneData.ModelAssets.size())
            continue;

        const RTXPTModelAsset& Asset = SceneData.ModelAssets[Instance.ModelAssetId];
        if (!Asset.Model || Asset.SceneIndex >= Asset.Model->Scenes.size())
            continue;

        const GLTF::Model& Model = *Asset.Model;
        const GLTF::Scene& Scene = Model.Scenes[Asset.SceneIndex];
        const GLTF::ModelTransforms& Transforms = Instance.Transforms.NodeGlobalMatrices.empty() ?
            Asset.StaticTransforms :
            Instance.Transforms;
        for (const GLTF::Node* pNode : Scene.LinearNodes)
        {
            if (pNode == nullptr || pNode->pMesh == nullptr)
                continue;
            if (pNode->Index < 0 || static_cast<size_t>(pNode->Index) >= Transforms.NodeGlobalMatrices.size())
                continue;

            for (const GLTF::Primitive& Primitive : pNode->pMesh->Primitives)
            {
                if (Primitive.MaterialId >= Model.Materials.size())
                    continue;

                const RTXPTMaterialExtension* pExtension = RTXPTGetMaterialExtension(SceneData, Asset, Primitive.MaterialId);
                if (!RTXPTMaterialIsEmissiveAreaLight(Model.Materials[Primitive.MaterialId], pExtension))
                    continue;

                const Uint32 TriangleCount = GetPrimitiveTriangleCount(Primitive);
                const Uint64 NewCount = EmissiveTriangleCount + TriangleCount;
                if (NewCount > Uint64{std::numeric_limits<Uint32>::max()})
                {
                    m_Stats.LastError = "RTXPT emissive triangle count exceeds Uint32";
                    LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
                    return false;
                }

                EmissiveTriangleCount = NewCount;
                m_EmissiveProxyWeight += static_cast<float>(TriangleCount) *
                    std::max(1e-6f, GetMaterialEmissionMagnitude(Model.Materials[Primitive.MaterialId], pExtension));
            }
        }
    }

    m_Stats.EmissiveTriangleCount = static_cast<Uint32>(EmissiveTriangleCount);
    return UploadEmissiveTriangleBuffer(pDevice, m_Stats.EmissiveTriangleCount) && UploadLightProxyBuffer(pDevice);
}

} // namespace Diligent
