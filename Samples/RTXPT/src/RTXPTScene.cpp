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

#include <algorithm>
#include <stdexcept>

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

} // namespace

void RTXPTScene::ResetLoadedData()
{
    m_Model.reset();
    m_Transforms = {};
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

    GLTF::ModelCreateInfo ModelCI;
    ModelCI.FileName             = m_ModelPath.c_str();
    ModelCI.ComputeBoundingBoxes = true;
    ModelCI.IndexType            = m_IndexType;
    ModelCI.IndBufferBindFlags   = BIND_INDEX_BUFFER | BIND_RAY_TRACING | BIND_SHADER_RESOURCE;
    for (BIND_FLAGS& BindFlags : ModelCI.VertBufferBindFlags)
        BindFlags = BIND_VERTEX_BUFFER | BIND_RAY_TRACING;
    // Buffer 0 is the path-tracer vertex stream (POSITION + NORMAL + TEXCOORD_0); chit reads it as a StructuredBuffer<RTXPTVertex>.
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
