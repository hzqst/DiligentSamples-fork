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

#include <stdexcept>

namespace Diligent
{

bool RTXPTScene::LoadDefaultScene(IRenderDevice* pDevice, IDeviceContext* pContext, const std::string& AssetsRoot)
{
    const std::string ModelPath = AssetsRoot + "/Models/Bistro/bistro.gltf";

    GLTF::ModelCreateInfo ModelCI;
    ModelCI.FileName             = ModelPath.c_str();
    ModelCI.ComputeBoundingBoxes = true;

    try
    {
        m_Model           = std::make_unique<GLTF::Model>(pDevice, pContext, ModelCI);
        m_LoadedSceneName = "bistro-programmer-art.scene.json";
    }
    catch (const std::exception&)
    {
        m_Model.reset();
        m_LoadedSceneName.clear();
    }

    // TODO(RTXPT-Port Phase 2): parse bistro-programmer-art.scene.json and merge RTXPT camera, material and light metadata.
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
