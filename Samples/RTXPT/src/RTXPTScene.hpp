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

#include <memory>
#include <string>
#include <vector>

#include "GLTFLoader.hpp"

namespace Diligent
{

struct RTXPTSceneCamera
{
    std::string Name;
    float3      Position    = float3{0.0f, 0.0f, 0.0f};
    QuaternionF Rotation    = QuaternionF{};
    float       VerticalFov = PI_F / 4.0f;
    float       NearPlane   = 0.1f;
    float       FarPlane    = 10000.0f;
};

class RTXPTScene
{
public:
    bool LoadDefaultScene(IRenderDevice* pDevice, IDeviceContext* pContext, const std::string& AssetsRoot);
    void Update(double CurrTime, double ElapsedTime);
    bool HasValidContent() const;

    const std::string& GetLoadedSceneName() const { return m_LoadedSceneName; }
    const std::string& GetAssetsRoot() const { return m_AssetsRoot; }
    const std::string& GetModelPath() const { return m_ModelPath; }
    const std::string& GetLastError() const { return m_LastError; }

    const GLTF::Model*           GetModel() const { return m_Model.get(); }
    const GLTF::ModelTransforms& GetTransforms() const { return m_Transforms; }
    Uint32                       GetSceneIndex() const { return m_SceneIndex; }
    VALUE_TYPE                   GetIndexType() const { return m_IndexType; }
    Uint32                       GetMeshNodeCount() const { return m_MeshNodeCount; }
    Uint32                       GetPrimitiveCount() const { return m_PrimitiveCount; }
    Uint32                       GetMaterialCount() const { return m_MaterialCount; }
    Uint32                       GetLightCount() const { return m_LightCount; }
    Uint32                       GetCameraCount() const { return static_cast<Uint32>(m_Cameras.size()); }
    const RTXPTSceneCamera*      GetCamera(Uint32 CameraIndex) const;

    // Buffer 0 packs POSITION + NORMAL + TEXCOORD_0 (the Diligent GLTF default layout).
    // VertexStride0 is the per-vertex stride for buffer 0 and must equal sizeof(GeometryVertexData) on the shader side.
    IBuffer* GetVertexBuffer0(IRenderDevice* pDevice = nullptr, IDeviceContext* pContext = nullptr) const;
    IBuffer* GetIndexBuffer(IRenderDevice* pDevice = nullptr, IDeviceContext* pContext = nullptr) const;
    Uint32   GetVertexStride0() const { return m_VertexStride0; }

private:
    void ResetLoadedData();
    void CacheSceneData();
    bool LoadSceneCameras(const std::string& ScenePath);

    std::unique_ptr<GLTF::Model> m_Model;
    GLTF::ModelTransforms        m_Transforms;
    std::vector<RTXPTSceneCamera> m_Cameras;
    std::string                  m_LoadedSceneName;
    std::string                  m_AssetsRoot;
    std::string                  m_ModelPath;
    std::string                  m_LastError;
    VALUE_TYPE                   m_IndexType      = VT_UINT32;
    Uint32                       m_SceneIndex     = 0;
    Uint32                       m_MeshNodeCount  = 0;
    Uint32                       m_PrimitiveCount = 0;
    Uint32                       m_MaterialCount  = 0;
    Uint32                       m_LightCount     = 0;
    Uint32                       m_VertexStride0  = 0;
};

} // namespace Diligent
