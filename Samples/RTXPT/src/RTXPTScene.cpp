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
#include "FileSystem.hpp"
#include "GraphicsAccessories.hpp"
#include "json.hpp"

#include <algorithm>
#include <fstream>
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

bool ReadFloatArray(const nlohmann::json& Object, const char* Key, float* Values, size_t Count)
{
    const auto It = Object.find(Key);
    if (It == Object.end() || !It->is_array() || It->size() < Count)
        return false;

    for (size_t Idx = 0; Idx < Count; ++Idx)
    {
        if (!(*It)[Idx].is_number())
            return false;
        Values[Idx] = (*It)[Idx].get<float>();
    }

    return true;
}

float ReadOptionalFloat(const nlohmann::json& Object, const char* Key, float DefaultValue)
{
    const auto It = Object.find(Key);
    return It != Object.end() && It->is_number() ? It->get<float>() : DefaultValue;
}

void AppendSceneCameras(const nlohmann::json& Node, std::vector<RTXPTSceneCamera>& Cameras)
{
    if (!Node.is_object())
        return;

    const auto TypeIt = Node.find("type");
    if (TypeIt != Node.end() && TypeIt->is_string() && TypeIt->get<std::string>() == "PerspectiveCamera")
    {
        float Translation[3] = {};
        float Rotation[4]    = {0.0f, 0.0f, 0.0f, 1.0f};
        if (ReadFloatArray(Node, "translation", Translation, sizeof(Translation) / sizeof(Translation[0])) &&
            ReadFloatArray(Node, "rotation", Rotation, sizeof(Rotation) / sizeof(Rotation[0])))
        {
            RTXPTSceneCamera Camera;

            const auto NameIt = Node.find("name");
            Camera.Name       = NameIt != Node.end() && NameIt->is_string() ? NameIt->get<std::string>() : std::string{};
            if (Camera.Name.empty())
                Camera.Name = std::string{"Camera "} + std::to_string(Cameras.size());

            Camera.Position = float3{Translation[0], Translation[1], Translation[2]};
            Camera.Rotation = QuaternionF{Rotation[0], Rotation[1], Rotation[2], Rotation[3]};

            const float RotationLength = length(Camera.Rotation.q);
            Camera.Rotation           = RotationLength > 1e-5f ? QuaternionF{Camera.Rotation.q / RotationLength} : QuaternionF{};

            Camera.VerticalFov = ReadOptionalFloat(Node, "verticalFov", Camera.VerticalFov);
            Camera.NearPlane   = ReadOptionalFloat(Node, "zNear", Camera.NearPlane);
            Camera.FarPlane    = ReadOptionalFloat(Node, "zFar", Camera.FarPlane);

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

void CollectAnimatedCameraChannels(const nlohmann::json& Channels,
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

bool ReadAnimatedCameraKey(const nlohmann::json& TranslationKey,
                           const nlohmann::json& RotationKey,
                           const RTXPTSceneCamera& Defaults,
                           RTXPTSceneCamera& Camera)
{
    if (!TranslationKey.is_object() || !RotationKey.is_object())
        return false;

    float Translation[3] = {};
    float Rotation[4]    = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!ReadFloatArray(TranslationKey, "value", Translation, sizeof(Translation) / sizeof(Translation[0])) ||
        !ReadFloatArray(RotationKey, "value", Rotation, sizeof(Rotation) / sizeof(Rotation[0])))
        return false;

    Camera          = Defaults;
    Camera.Position = float3{Translation[0], Translation[1], Translation[2]};
    Camera.Rotation = QuaternionF{Rotation[0], Rotation[1], Rotation[2], Rotation[3]};

    const float RotationLength = length(Camera.Rotation.q);
    Camera.Rotation = RotationLength > 1e-5f ? QuaternionF{Camera.Rotation.q / RotationLength} : QuaternionF{};
    return true;
}

void AppendAnimatedCameraKeys(const std::string& AnimationName,
                              const std::string& Target,
                              const AnimatedCameraChannels& Channels,
                              const RTXPTSceneCamera& Defaults,
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

        const float Time = ReadOptionalFloat(TranslationKey, "time", static_cast<float>(Key));
        Camera.Name      = FormatAnimatedCameraName(AnimationName, CameraName, Time);
        Cameras.emplace_back(std::move(Camera));
    }
}

void AppendAnimatedCameras(const nlohmann::json& SceneJson,
                           const RTXPTSceneCamera& Defaults,
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

} // namespace

void RTXPTScene::ResetLoadedData()
{
    m_Model.reset();
    m_Transforms = {};
    m_Cameras.clear();
    m_LoadedSceneName.clear();
    m_LastError.clear();
    m_IndexType      = VT_UINT32;
    m_SceneIndex     = 0;
    m_MeshNodeCount  = 0;
    m_PrimitiveCount = 0;
    m_MaterialCount  = 0;
    m_LightCount     = 0;
    m_VertexStride0  = 0;
}

void RTXPTScene::CacheSceneData()
{
    if (!m_Model || m_Model->Scenes.empty())
        return;

    m_SceneIndex = static_cast<Uint32>(m_Model->DefaultSceneId >= 0 ? m_Model->DefaultSceneId : 0);
    if (m_SceneIndex >= m_Model->Scenes.size())
        m_SceneIndex = 0;

    m_Model->ComputeTransforms(m_SceneIndex, m_Transforms);

    const GLTF::Scene& Scene = m_Model->Scenes[m_SceneIndex];
    m_MeshNodeCount          = CountMeshNodes(Scene);
    m_PrimitiveCount         = CountPrimitives(Scene);
    m_MaterialCount          = static_cast<Uint32>(m_Model->Materials.size());
    m_LightCount             = CountLightNodes(Scene);

    m_VertexStride0 = 0;
    if (m_Model && m_Model->GetVertexBufferCount() > 0)
    {
        for (Uint32 i = 0; i < m_Model->GetNumVertexAttributes(); ++i)
        {
            const GLTF::VertexAttributeDesc& Desc = m_Model->GetVertexAttribute(i);
            if (Desc.BufferId == 0)
            {
                const Uint32 RelOffset = Desc.RelativeOffset == ~0u ? 0u : Desc.RelativeOffset;
                m_VertexStride0        = std::max(m_VertexStride0, RelOffset + GetValueSize(Desc.ValueType) * Desc.NumComponents);
            }
        }
    }
}

const RTXPTSceneCamera* RTXPTScene::GetCamera(Uint32 CameraIndex) const
{
    return CameraIndex < m_Cameras.size() ? &m_Cameras[CameraIndex] : nullptr;
}

bool RTXPTScene::LoadSceneCameras(const std::string& ScenePath)
{
    m_Cameras.clear();

    std::ifstream SceneFile{ScenePath};
    if (!SceneFile)
        return false;

    nlohmann::json SceneJson = nlohmann::json::parse(SceneFile, nullptr, false);
    if (SceneJson.is_discarded() || !SceneJson.is_object())
        return false;

    const auto GraphIt = SceneJson.find("graph");
    if (GraphIt == SceneJson.end() || !GraphIt->is_array())
        return false;

    for (const auto& Node : *GraphIt)
        AppendSceneCameras(Node, m_Cameras);

    RTXPTSceneCamera CameraDefaults;
    if (!m_Cameras.empty())
    {
        CameraDefaults.VerticalFov = m_Cameras.front().VerticalFov;
        CameraDefaults.NearPlane   = m_Cameras.front().NearPlane;
        CameraDefaults.FarPlane    = m_Cameras.front().FarPlane;
    }
    AppendAnimatedCameras(SceneJson, CameraDefaults, m_Cameras);

    return !m_Cameras.empty();
}

bool RTXPTScene::LoadDefaultScene(IRenderDevice* pDevice, IDeviceContext* pContext, const std::string& AssetsRoot)
{
    ResetLoadedData();

    m_AssetsRoot                = FileSystem::SimplifyPath(AssetsRoot.c_str());
    const std::string ScenePath = JoinPath(m_AssetsRoot, "bistro-programmer-art.scene.json");
    m_ModelPath                 = JoinPath(m_AssetsRoot, "Models/Bistro/bistro.gltf");

    if (!FileSystem::FileExists(ScenePath.c_str()))
    {
        m_LastError = "Missing scene file: " + ScenePath;
        return false;
    }

    if (!FileSystem::FileExists(m_ModelPath.c_str()))
    {
        m_LastError = "Missing glTF file: " + m_ModelPath;
        return false;
    }

    LoadSceneCameras(ScenePath);

    GLTF::ModelCreateInfo ModelCI;
    ModelCI.FileName             = m_ModelPath.c_str();
    ModelCI.ComputeBoundingBoxes = true;
    ModelCI.IndexType            = m_IndexType;
    ModelCI.IndBufferBindFlags   = BIND_INDEX_BUFFER | BIND_RAY_TRACING | BIND_SHADER_RESOURCE;
    for (BIND_FLAGS& BindFlags : ModelCI.VertBufferBindFlags)
        BindFlags = BIND_VERTEX_BUFFER | BIND_RAY_TRACING;
    // Buffer 0 is the path-tracer vertex stream (POSITION + NORMAL + TEXCOORD_0); chit reads it as a StructuredBuffer<GeometryVertexData>.
    ModelCI.VertBufferBindFlags[0] = BIND_VERTEX_BUFFER | BIND_RAY_TRACING | BIND_SHADER_RESOURCE;

    try
    {
        m_Model           = std::make_unique<GLTF::Model>(pDevice, pContext, ModelCI);
        m_LoadedSceneName = "bistro-programmer-art.scene.json";
        CacheSceneData();
    }
    catch (const std::exception& e)
    {
        m_Model.reset();
        m_LoadedSceneName.clear();
        m_LastError = e.what();
    }

    // TODO(RTXPT-Port Phase 2): add full material parsing for RTXPT extension fields.
    // TODO(RTXPT-Port Phase 3): add dynamic/skinned BLAS update, AS compaction, and alpha/OMM geometry flags;
    // current path builds static opaque geometry.
    return m_Model != nullptr;
}

void RTXPTScene::Update(double CurrTime, double ElapsedTime)
{
    (void)CurrTime;
    (void)ElapsedTime;
}

bool RTXPTScene::HasValidContent() const
{
    return m_Model != nullptr;
}

IBuffer* RTXPTScene::GetVertexBuffer0(IRenderDevice* pDevice, IDeviceContext* pContext) const
{
    return m_Model ? m_Model->GetVertexBuffer(0, pDevice, pContext) : nullptr;
}

IBuffer* RTXPTScene::GetIndexBuffer(IRenderDevice* pDevice, IDeviceContext* pContext) const
{
    return m_Model ? m_Model->GetIndexBuffer(pDevice, pContext) : nullptr;
}

} // namespace Diligent
