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

} // namespace

bool RTXPTScene::LoadDefaultScene(IRenderDevice* pDevice, IDeviceContext* pContext, const std::string& AssetsRoot)
{
    m_Model.reset();
    m_LoadedSceneName.clear();
    m_LastError.clear();

    m_AssetsRoot = FileSystem::SimplifyPath(AssetsRoot.c_str());
    const std::string ScenePath = JoinPath(m_AssetsRoot, "bistro-programmer-art.scene.json");
    m_ModelPath                = JoinPath(m_AssetsRoot, "Models/Bistro/bistro.gltf");

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

    try
    {
        m_Model           = std::make_unique<GLTF::Model>(pDevice, pContext, ModelCI);
        m_LoadedSceneName = "bistro-programmer-art.scene.json";
    }
    catch (const std::exception& e)
    {
        m_Model.reset();
        m_LoadedSceneName.clear();
        m_LastError = e.what();
    }

    // TODO(RTXPT-Port Phase 2): add full material parsing for RTXPT extension fields.
    // TODO(RTXPT-Port Phase 3): build BLAS/TLAS from scene geometry.
    // TODO(RTXPT-Port Phase 4): add TraceRays path and RT PSO/SBT.
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

} // namespace Diligent
