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

#include "RTXPTScene.hpp"
#include "RTXPTSceneJson.hpp"

#include "DebugUtilities.hpp"
#include "FileSystem.hpp"
#include "GraphicsAccessories.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace Diligent
{

namespace
{

std::string JoinPath(const std::string& Root, const char* RelativePath)
{
    if (Root.empty())
        return RelativePath;

    std::string Path = Root;
    if (!FileSystem::IsSlash(Path.back()))
        Path.push_back(FileSystem::SlashSymbol);
    Path += RelativePath;
    FileSystem::CorrectSlashes(Path);
    return FileSystem::SimplifyPath(Path.c_str());
}

std::unique_ptr<GLTF::Model> LoadRTXPTModelAsset(IRenderDevice*     pDevice,
                                                 IDeviceContext*    pContext,
                                                 const std::string& ModelPath,
                                                 VALUE_TYPE         IndexType)
{
    GLTF::ModelCreateInfo ModelCI;
    ModelCI.FileName             = ModelPath.c_str();
    ModelCI.ComputeBoundingBoxes = true;
    ModelCI.IndexType            = IndexType;
    ModelCI.IndBufferBindFlags   = BIND_INDEX_BUFFER | BIND_RAY_TRACING | BIND_SHADER_RESOURCE;
    for (BIND_FLAGS& BindFlags : ModelCI.VertBufferBindFlags)
        BindFlags = BIND_VERTEX_BUFFER | BIND_RAY_TRACING;
    // Buffer 0 is the path-tracer vertex stream (POSITION + NORMAL + TEXCOORD_0).
    ModelCI.VertBufferBindFlags[0] = BIND_VERTEX_BUFFER | BIND_RAY_TRACING | BIND_SHADER_RESOURCE;
    // Buffer 1 is the default GLTF skinning stream (JOINTS_0 + WEIGHTS_0).
    ModelCI.VertBufferBindFlags[1] = BIND_VERTEX_BUFFER | BIND_SHADER_RESOURCE;

    try
    {
        return std::make_unique<GLTF::Model>(pDevice, pContext, ModelCI);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR_MESSAGE("Failed to load RTXPT glTF model '", ModelPath, "': ", e.what());
        return {};
    }
}

Uint32 CountMeshNodes(const GLTF::Scene& Scene)
{
    Uint32 Count = 0;
    for (const GLTF::Node* pNode : Scene.LinearNodes)
    {
        if (pNode != nullptr && pNode->pMesh != nullptr)
            ++Count;
    }
    return Count;
}

Uint32 CountPrimitives(const GLTF::Scene& Scene)
{
    Uint32 Count = 0;
    for (const GLTF::Node* pNode : Scene.LinearNodes)
    {
        if (pNode == nullptr || pNode->pMesh == nullptr)
            continue;

        for (const GLTF::Primitive& Primitive : pNode->pMesh->Primitives)
        {
            if (Primitive.VertexCount != 0 || Primitive.IndexCount != 0)
                ++Count;
        }
    }
    return Count;
}

Uint32 CountLightNodes(const GLTF::Scene& Scene)
{
    Uint32 Count = 0;
    for (const GLTF::Node* pNode : Scene.LinearNodes)
    {
        if (pNode != nullptr && pNode->pLight != nullptr)
            ++Count;
    }
    return Count;
}

RTXPTSceneGeometryStats ComputeGeometryStats(const GLTF::Scene& Scene, const GLTF::Model& Model)
{
    RTXPTSceneGeometryStats Stats;
    Stats.HasAnimations = !Model.Animations.empty();

    for (const GLTF::Node* pNode : Scene.LinearNodes)
    {
        if (pNode == nullptr || pNode->pMesh == nullptr || pNode->pSkin == nullptr)
            continue;

        ++Stats.SkinnedNodeCount;
        Stats.HasSkinnedGeometry = true;

        for (const GLTF::Primitive& Primitive : pNode->pMesh->Primitives)
        {
            if (Primitive.VertexCount == 0 && Primitive.IndexCount == 0)
                continue;

            ++Stats.SkinnedPrimitiveCount;
            Stats.SkinnedVertexCount += Primitive.VertexCount;
        }
    }

    return Stats;
}

bool HasAssetSkinnedGeometry(const RTXPTModelAsset& Asset)
{
    if (!Asset.Model || Asset.SceneIndex >= Asset.Model->Scenes.size())
        return false;

    return ComputeGeometryStats(Asset.Model->Scenes[Asset.SceneIndex], *Asset.Model).HasSkinnedGeometry;
}

void AppendSceneCameras(const nlohmann::json& Node, std::vector<RTXPTSceneCamera>& Cameras)
{
    if (!Node.is_object())
        return;

    const auto TypeIt = Node.find("type");
    const bool IsPerspectiveCamera =
        TypeIt != Node.end() && TypeIt->is_string() &&
        (TypeIt->get<std::string>() == "PerspectiveCamera" ||
         TypeIt->get<std::string>() == "PerspectiveCameraEx");
    if (IsPerspectiveCamera)
    {
        float Translation[3] = {};
        float Rotation[4]    = {0.0f, 0.0f, 0.0f, 1.0f};
        if (ReadRTXPTFloatArray(Node, "translation", Translation, sizeof(Translation) / sizeof(Translation[0])) &&
            ReadRTXPTFloatArray(Node, "rotation", Rotation, sizeof(Rotation) / sizeof(Rotation[0])))
        {
            RTXPTSceneCamera Camera;

            Camera.Name = ReadRTXPTOptionalString(Node, "name");
            if (Camera.Name.empty())
                Camera.Name = std::string{"Camera "} + std::to_string(Cameras.size());

            Camera.Position = float3{Translation[0], Translation[1], Translation[2]};
            Camera.Rotation = QuaternionF{Rotation[0], Rotation[1], Rotation[2], Rotation[3]};

            const float RotationLength = length(Camera.Rotation.q);
            Camera.Rotation            = RotationLength > 1e-5f ? QuaternionF{Camera.Rotation.q / RotationLength} : QuaternionF{};

            Camera.VerticalFov = ReadRTXPTOptionalFloat(Node, "verticalFov", Camera.VerticalFov);

            const auto NearPlaneIt = Node.find("zNear");
            const auto FarPlaneIt  = Node.find("zFar");
            const bool HasZNear    = NearPlaneIt != Node.end() && NearPlaneIt->is_number();
            const bool HasZFar     = FarPlaneIt != Node.end() && FarPlaneIt->is_number();
            if (HasZNear)
                Camera.NearPlane = NearPlaneIt->get<float>();
            if (HasZFar)
                Camera.FarPlane = FarPlaneIt->get<float>();
            Camera.HasExplicitClipPlanes = HasZNear && HasZFar;

            if (Camera.VerticalFov > 0.0f && Camera.NearPlane > 0.0f && Camera.FarPlane > Camera.NearPlane)
                Cameras.emplace_back(std::move(Camera));
        }
    }

    const auto ChildrenIt = Node.find("children");
    if (ChildrenIt == Node.end() || !ChildrenIt->is_array())
        return;

    for (const auto& Child : *ChildrenIt)
        AppendSceneCameras(Child, Cameras);
}

std::string GetCameraNameFromTarget(const std::string& Target)
{
    const size_t Separator = Target.find_last_of('/');
    return Separator != std::string::npos && Separator + 1 < Target.size() ? Target.substr(Separator + 1) : Target;
}

std::string FormatAnimatedCameraName(const std::string& AnimationName, const std::string& CameraName, float Time)
{
    std::ostringstream Stream;
    Stream << AnimationName << "/" << CameraName << " " << std::fixed << std::setprecision(1) << Time << "s";
    return Stream.str();
}

struct AnimatedCameraChannels
{
    const nlohmann::json* pTranslation = nullptr;
    const nlohmann::json* pRotation    = nullptr;
};

void CollectAnimatedCameraChannels(const nlohmann::json&                                    Channels,
                                   std::unordered_map<std::string, AnimatedCameraChannels>& ChannelsByTarget)
{
    for (const auto& Channel : Channels)
    {
        const auto TargetIt    = Channel.find("target");
        const auto AttributeIt = Channel.find("attribute");
        if (TargetIt == Channel.end() || AttributeIt == Channel.end() ||
            !TargetIt->is_string() || !AttributeIt->is_string())
            continue;

        const std::string Target = TargetIt->get<std::string>();
        if (Target.rfind("/Cameras/", 0) != 0)
            continue;

        const std::string Attribute = AttributeIt->get<std::string>();
        if (Attribute == "translation")
            ChannelsByTarget[Target].pTranslation = &Channel;
        else if (Attribute == "rotation")
            ChannelsByTarget[Target].pRotation = &Channel;
    }
}

bool ReadAnimatedCameraKey(const nlohmann::json&   TranslationKey,
                           const nlohmann::json&   RotationKey,
                           const RTXPTSceneCamera& Defaults,
                           RTXPTSceneCamera&       Camera)
{
    if (!TranslationKey.is_object() || !RotationKey.is_object())
        return false;

    float Translation[3] = {};
    float Rotation[4]    = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!ReadRTXPTFloatArray(TranslationKey, "value", Translation, sizeof(Translation) / sizeof(Translation[0])) ||
        !ReadRTXPTFloatArray(RotationKey, "value", Rotation, sizeof(Rotation) / sizeof(Rotation[0])))
        return false;

    Camera          = Defaults;
    Camera.Position = float3{Translation[0], Translation[1], Translation[2]};
    Camera.Rotation = QuaternionF{Rotation[0], Rotation[1], Rotation[2], Rotation[3]};

    const float RotationLength = length(Camera.Rotation.q);
    Camera.Rotation            = RotationLength > 1e-5f ? QuaternionF{Camera.Rotation.q / RotationLength} : QuaternionF{};
    return true;
}

void AppendAnimatedCameraKeys(const std::string&             AnimationName,
                              const std::string&             Target,
                              const AnimatedCameraChannels&  Channels,
                              const RTXPTSceneCamera&        Defaults,
                              std::vector<RTXPTSceneCamera>& Cameras)
{
    if (Channels.pTranslation == nullptr || Channels.pRotation == nullptr)
        return;

    const auto TranslationDataIt = Channels.pTranslation->find("data");
    const auto RotationDataIt    = Channels.pRotation->find("data");
    if (TranslationDataIt == Channels.pTranslation->end() || RotationDataIt == Channels.pRotation->end() ||
        !TranslationDataIt->is_array() || !RotationDataIt->is_array())
        return;

    const size_t      KeyCount   = std::min(TranslationDataIt->size(), RotationDataIt->size());
    const std::string CameraName = GetCameraNameFromTarget(Target);
    for (size_t Key = 0; Key < KeyCount; ++Key)
    {
        const nlohmann::json& TranslationKey = (*TranslationDataIt)[Key];
        const nlohmann::json& RotationKey    = (*RotationDataIt)[Key];

        RTXPTSceneCamera Camera;
        if (!ReadAnimatedCameraKey(TranslationKey, RotationKey, Defaults, Camera))
            continue;

        const float Time = ReadRTXPTOptionalFloat(TranslationKey, "time", static_cast<float>(Key));
        Camera.Name      = FormatAnimatedCameraName(AnimationName, CameraName, Time);
        Cameras.emplace_back(std::move(Camera));
    }
}

void AppendAnimatedCameras(const nlohmann::json&          SceneJson,
                           const RTXPTSceneCamera&        Defaults,
                           std::vector<RTXPTSceneCamera>& Cameras)
{
    const auto AnimationsIt = SceneJson.find("animations");
    if (AnimationsIt == SceneJson.end() || !AnimationsIt->is_array())
        return;

    for (const auto& Animation : *AnimationsIt)
    {
        if (!Animation.is_object())
            continue;

        const auto ChannelsIt = Animation.find("channels");
        if (ChannelsIt == Animation.end() || !ChannelsIt->is_array())
            continue;

        std::unordered_map<std::string, AnimatedCameraChannels> ChannelsByTarget;
        CollectAnimatedCameraChannels(*ChannelsIt, ChannelsByTarget);

        const std::string AnimationName = Animation.value("name", "Animation");
        for (const auto& TargetAndChannels : ChannelsByTarget)
            AppendAnimatedCameraKeys(AnimationName, TargetAndChannels.first, TargetAndChannels.second, Defaults, Cameras);
    }
}

bool AppendRTXPTGraphNode(RTXPTSceneGraphData& Data,
                          const nlohmann::json& NodeJson,
                          RTXPTSceneId ParentId,
                          const float4x4& ParentTransform)
{
    if (!NodeJson.is_object())
    {
        LOG_ERROR_MESSAGE("RTXPT scene graph node is not an object");
        return false;
    }

    RTXPTGraphNode Node;
    Node.Name            = ReadRTXPTOptionalString(NodeJson, "name", "Node");
    Node.Type            = ReadRTXPTOptionalString(NodeJson, "type", "");
    Node.ParentId        = ParentId;
    Node.LocalTransform  = MakeRTXPTNodeTransform(NodeJson);
    Node.GlobalTransform = Node.LocalTransform * ParentTransform;
    Node.RawMetadata     = NodeJson;

    const auto ModelIt = NodeJson.find("model");
    if (ModelIt != NodeJson.end() && ModelIt->is_number_unsigned())
    {
        Node.ModelAssetId = ModelIt->get<Uint32>();
        if (Node.ModelAssetId >= Data.ModelAssets.size())
        {
            LOG_ERROR_MESSAGE("Scene graph model index is out of range: ", Node.ModelAssetId);
            return false;
        }
    }

    const RTXPTSceneId NodeId = static_cast<RTXPTSceneId>(Data.GraphNodes.size());
    Data.GraphNodes.emplace_back(std::move(Node));
    if (ParentId != InvalidRTXPTSceneId)
        Data.GraphNodes[ParentId].Children.push_back(NodeId);

    const RTXPTGraphNode& StoredNode = Data.GraphNodes[NodeId];
    if (StoredNode.ModelAssetId != InvalidRTXPTSceneId)
    {
        const RTXPTModelAsset& Asset = Data.ModelAssets[StoredNode.ModelAssetId];
        if (!Asset.Model)
        {
            LOG_ERROR_MESSAGE("Scene graph model asset is missing loaded glTF data: ", StoredNode.ModelAssetId);
            return false;
        }

        RTXPTModelInstance Instance;
        Instance.GraphNodeId     = NodeId;
        Instance.ModelAssetId    = StoredNode.ModelAssetId;
        Instance.Name            = StoredNode.Name;
        Instance.GlobalTransform = StoredNode.GlobalTransform;
        Asset.Model->ComputeTransforms(Asset.SceneIndex, Instance.Transforms, Instance.GlobalTransform);
        Data.ModelInstances.emplace_back(std::move(Instance));
    }

    const auto ChildrenIt = NodeJson.find("children");
    if (ChildrenIt != NodeJson.end() && ChildrenIt->is_array())
    {
        for (const auto& Child : *ChildrenIt)
        {
            if (!AppendRTXPTGraphNode(Data, Child, NodeId, StoredNode.GlobalTransform))
                return false;
        }
    }

    return true;
}

} // namespace

void RTXPTScene::ResetLoadedData()
{
    m_SceneGraph.Clear();
    m_Transforms = {};
    m_Cameras.clear();
    m_LoadedSceneName.clear();
    m_AssetsRoot.clear();
    m_ModelPath.clear();
    m_IndexType      = VT_UINT32;
    m_SceneIndex     = 0;
    m_MeshNodeCount  = 0;
    m_PrimitiveCount = 0;
    m_MaterialCount  = 0;
    m_LightCount     = 0;
    m_VertexStride0  = 0;
    m_GeometryStats  = {};
    m_AnimationTime  = 0.0f;
    m_AnimationIndex = -1;
    m_GeometryDirty  = false;
}

const RTXPTModelAsset* RTXPTScene::GetCompatibilityModelAsset() const
{
    const RTXPTModelInstance* pInstance = GetCompatibilityModelInstance();
    if (pInstance != nullptr && pInstance->ModelAssetId < m_SceneGraph.ModelAssets.size())
        return &m_SceneGraph.ModelAssets[pInstance->ModelAssetId];

    return m_SceneGraph.ModelAssets.empty() ? nullptr : &m_SceneGraph.ModelAssets.front();
}

const RTXPTModelInstance* RTXPTScene::GetCompatibilityModelInstance() const
{
    return m_SceneGraph.ModelInstances.empty() ? nullptr : &m_SceneGraph.ModelInstances.front();
}

void RTXPTScene::CacheSceneData()
{
    const RTXPTModelAsset*    pAsset    = GetCompatibilityModelAsset();
    const RTXPTModelInstance* pInstance = GetCompatibilityModelInstance();
    if (pAsset == nullptr || !pAsset->Model || pAsset->SceneIndex >= pAsset->Model->Scenes.size())
        return;

    m_SceneIndex = pAsset->SceneIndex;
    m_ModelPath  = pAsset->ResolvedPath;
    m_Transforms = pInstance != nullptr ? pInstance->Transforms : pAsset->StaticTransforms;

    const GLTF::Scene& Scene = pAsset->Model->Scenes[pAsset->SceneIndex];
    m_MeshNodeCount          = CountMeshNodes(Scene);
    m_PrimitiveCount         = CountPrimitives(Scene);
    m_MaterialCount          = static_cast<Uint32>(pAsset->Model->Materials.size());
    m_LightCount             = CountLightNodes(Scene);
    m_GeometryStats          = ComputeGeometryStats(Scene, *pAsset->Model);
    m_AnimationIndex         = m_GeometryStats.HasAnimations ? 0 : -1;
    m_GeometryDirty          = m_GeometryStats.HasSkinnedGeometry;

    m_VertexStride0 = 0;
    if (pAsset->Model->GetVertexBufferCount() > 0)
    {
        for (Uint32 i = 0; i < pAsset->Model->GetNumVertexAttributes(); ++i)
        {
            const GLTF::VertexAttributeDesc& Desc = pAsset->Model->GetVertexAttribute(i);
            if (Desc.BufferId == 0)
            {
                const Uint32 RelOffset = Desc.RelativeOffset == ~0u ? 0u : Desc.RelativeOffset;
                m_VertexStride0        = std::max(m_VertexStride0, RelOffset + GetValueSize(Desc.ValueType) * Desc.NumComponents);
            }
        }
    }
}

const GLTF::Model* RTXPTScene::GetModel() const
{
    const RTXPTModelAsset* pAsset = GetCompatibilityModelAsset();
    return pAsset != nullptr ? pAsset->Model.get() : nullptr;
}

const GLTF::ModelTransforms& RTXPTScene::GetTransforms() const
{
    return m_Transforms;
}

const RTXPTSceneCamera* RTXPTScene::GetCamera(Uint32 CameraIndex) const
{
    return CameraIndex < m_Cameras.size() ? &m_Cameras[CameraIndex] : nullptr;
}

bool RTXPTScene::LoadSceneCameras(const std::string& ScenePath)
{
    m_Cameras.clear();

    RTXPTJsonLoadResult SceneJsonResult;
    if (!LoadRTXPTRelaxedJsonFile(ScenePath, SceneJsonResult) || !SceneJsonResult.Json.is_object())
        return false;

    const auto GraphIt = SceneJsonResult.Json.find("graph");
    if (GraphIt == SceneJsonResult.Json.end() || !GraphIt->is_array())
        return false;

    for (const auto& Node : *GraphIt)
        AppendSceneCameras(Node, m_Cameras);

    RTXPTSceneCamera CameraDefaults;
    if (!m_Cameras.empty())
    {
        CameraDefaults.VerticalFov           = m_Cameras.front().VerticalFov;
        CameraDefaults.NearPlane             = m_Cameras.front().NearPlane;
        CameraDefaults.FarPlane              = m_Cameras.front().FarPlane;
        CameraDefaults.HasExplicitClipPlanes = m_Cameras.front().HasExplicitClipPlanes;
    }
    AppendAnimatedCameras(SceneJsonResult.Json, CameraDefaults, m_Cameras);

    return !m_Cameras.empty();
}

bool RTXPTScene::LoadScene(IRenderDevice*     pDevice,
                           IDeviceContext*    pContext,
                           const std::string& AssetsRoot,
                           const std::string& SceneName)
{
    ResetLoadedData();

    m_AssetsRoot = FileSystem::SimplifyPath(AssetsRoot.c_str());
    if (SceneName.empty())
    {
        LOG_ERROR_MESSAGE("Empty RTXPT scene file name");
        return false;
    }

    const std::string ScenePath = JoinPath(m_AssetsRoot, SceneName.c_str());
    if (!FileSystem::FileExists(ScenePath.c_str()))
    {
        LOG_ERROR_MESSAGE("Missing scene file: ", ScenePath);
        return false;
    }

    RTXPTJsonLoadResult SceneJsonResult;
    if (!LoadRTXPTRelaxedJsonFile(ScenePath, SceneJsonResult) || !SceneJsonResult.Json.is_object())
    {
        LOG_ERROR_MESSAGE("Invalid scene JSON: ", ScenePath);
        return false;
    }

    const auto ModelsIt = SceneJsonResult.Json.find("models");
    const auto GraphIt  = SceneJsonResult.Json.find("graph");
    if (ModelsIt == SceneJsonResult.Json.end() || !ModelsIt->is_array() ||
        GraphIt == SceneJsonResult.Json.end() || !GraphIt->is_array())
    {
        LOG_ERROR_MESSAGE("Scene JSON requires models and graph arrays: ", ScenePath);
        return false;
    }

    RTXPTSceneGraphData NewData;
    for (const auto& ModelJson : *ModelsIt)
    {
        if (!ModelJson.is_string())
        {
            LOG_ERROR_MESSAGE("Scene JSON model entry is not a string: ", ScenePath);
            return false;
        }

        RTXPTModelAsset Asset;
        Asset.RelativePath = ModelJson.get<std::string>();
        Asset.ResolvedPath = JoinPath(m_AssetsRoot, Asset.RelativePath.c_str());
        Asset.ModelName    = GetRTXPTModelNameFromPath(Asset.RelativePath);
        if (!FileSystem::FileExists(Asset.ResolvedPath.c_str()))
        {
            LOG_ERROR_MESSAGE("Missing glTF file: ", Asset.ResolvedPath);
            return false;
        }

        Asset.Model = LoadRTXPTModelAsset(pDevice, pContext, Asset.ResolvedPath, m_IndexType);
        if (!Asset.Model)
        {
            LOG_ERROR_MESSAGE("Failed to load RTXPT glTF model: ", Asset.ResolvedPath);
            return false;
        }

        Asset.SceneIndex = static_cast<Uint32>(Asset.Model->DefaultSceneId >= 0 ? Asset.Model->DefaultSceneId : 0);
        if (Asset.SceneIndex >= Asset.Model->Scenes.size())
            Asset.SceneIndex = 0;
        Asset.Model->ComputeTransforms(Asset.SceneIndex, Asset.StaticTransforms);
        NewData.ModelAssets.emplace_back(std::move(Asset));
    }

    for (const auto& NodeJson : *GraphIt)
    {
        if (!AppendRTXPTGraphNode(NewData, NodeJson, InvalidRTXPTSceneId, float4x4::Identity()))
            return false;
    }

    for (const RTXPTModelInstance& Instance : NewData.ModelInstances)
    {
        if (Instance.ModelAssetId >= NewData.ModelAssets.size())
        {
            LOG_ERROR_MESSAGE("Scene graph model index is out of range in scene: ", ScenePath);
            return false;
        }
    }

    if (NewData.ModelAssets.empty() || NewData.ModelInstances.empty())
    {
        LOG_ERROR_MESSAGE("Scene JSON did not produce model instances: ", ScenePath);
        return false;
    }

    NewData.Stats.ModelAssetCount    = static_cast<Uint32>(NewData.ModelAssets.size());
    NewData.Stats.GraphNodeCount     = static_cast<Uint32>(NewData.GraphNodes.size());
    NewData.Stats.ModelInstanceCount = static_cast<Uint32>(NewData.ModelInstances.size());
    for (const RTXPTModelInstance& Instance : NewData.ModelInstances)
    {
        if (Instance.ModelAssetId < NewData.ModelAssets.size() &&
            HasAssetSkinnedGeometry(NewData.ModelAssets[Instance.ModelAssetId]))
        {
            ++NewData.Stats.SkinnedInstanceCount;
        }
    }
    NewData.Stats.AdapterWarningCount = static_cast<Uint32>(NewData.Warnings.size());

    m_SceneGraph      = std::move(NewData);
    m_LoadedSceneName = SceneName;
    LoadSceneCameras(ScenePath);
    CacheSceneData();

    return HasValidContent();
}

bool RTXPTScene::LoadDefaultScene(IRenderDevice* pDevice, IDeviceContext* pContext, const std::string& AssetsRoot)
{
    return LoadScene(pDevice, pContext, AssetsRoot, "bistro-programmer-art.scene.json");
}

void RTXPTScene::Update(double CurrTime, double ElapsedTime)
{
    (void)CurrTime;

    if (m_SceneGraph.ModelInstances.empty())
    {
        m_GeometryDirty = false;
        return;
    }

    RTXPTModelInstance& Instance = m_SceneGraph.ModelInstances.front();
    if (Instance.ModelAssetId >= m_SceneGraph.ModelAssets.size())
    {
        m_GeometryDirty = false;
        return;
    }

    RTXPTModelAsset& Asset = m_SceneGraph.ModelAssets[Instance.ModelAssetId];
    if (!Asset.Model || m_AnimationIndex < 0 || !m_GeometryStats.HasSkinnedGeometry)
    {
        m_GeometryDirty = false;
        return;
    }

    if (m_AnimationIndex >= static_cast<Int32>(Asset.Model->Animations.size()))
        m_AnimationIndex = 0;

    const GLTF::Animation& Animation = Asset.Model->Animations[static_cast<Uint32>(m_AnimationIndex)];
    const float            Duration  = std::max(Animation.End - Animation.Start, 1e-5f);
    m_AnimationTime += static_cast<float>(ElapsedTime);
    const float WrappedTime = Animation.Start + std::fmod(m_AnimationTime, Duration);

    Asset.Model->ComputeTransforms(Asset.SceneIndex, Instance.Transforms, Instance.GlobalTransform, m_AnimationIndex, WrappedTime);
    m_Transforms    = Instance.Transforms;
    m_GeometryDirty = true;
}

bool RTXPTScene::HasValidContent() const
{
    return !m_SceneGraph.ModelAssets.empty() && !m_SceneGraph.ModelInstances.empty();
}

IBuffer* RTXPTScene::GetVertexBuffer0(IRenderDevice* pDevice, IDeviceContext* pContext) const
{
    const RTXPTModelAsset* pAsset = GetCompatibilityModelAsset();
    return pAsset != nullptr && pAsset->Model ? pAsset->Model->GetVertexBuffer(0, pDevice, pContext) : nullptr;
}

IBuffer* RTXPTScene::GetSkinningBuffer(IRenderDevice* pDevice, IDeviceContext* pContext) const
{
    const RTXPTModelAsset* pAsset = GetCompatibilityModelAsset();
    return pAsset != nullptr && pAsset->Model && pAsset->Model->GetVertexBufferCount() > 1 ?
        pAsset->Model->GetVertexBuffer(1, pDevice, pContext) :
        nullptr;
}

IBuffer* RTXPTScene::GetIndexBuffer(IRenderDevice* pDevice, IDeviceContext* pContext) const
{
    const RTXPTModelAsset* pAsset = GetCompatibilityModelAsset();
    return pAsset != nullptr && pAsset->Model ? pAsset->Model->GetIndexBuffer(pDevice, pContext) : nullptr;
}

} // namespace Diligent
