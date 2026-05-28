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

#include <string>

#include "Buffer.h"
#include "GLTFLoader.hpp"
#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"

namespace Diligent
{

struct RTXPTLightData
{
    float4 ColorIntensity = float4{1, 1, 1, 0};
    float4 PositionRange  = float4{0, 0, 0, 0};
    float4 DirectionType  = float4{0, -1, 0, 0};
    float4 SpotAngles     = float4{0, 0, 0, 0};
};
static_assert(sizeof(RTXPTLightData) == 64, "RTXPTLightData layout must match RTXPTShaderShared.hlsli");

struct RTXPTLightStats
{
    Uint32      LightCount = 0;
    std::string LastError;
};

class RTXPTLights
{
public:
    void Reset();
    bool Upload(IRenderDevice* pDevice, const GLTF::Scene& Scene, const GLTF::ModelTransforms& Transforms);

    const RTXPTLightStats& GetStats() const { return m_Stats; }
    IBuffer*               GetLightBuffer() const { return m_LightBuffer; }

private:
    RefCntAutoPtr<IBuffer> m_LightBuffer;
    RTXPTLightStats        m_Stats;
};

} // namespace Diligent
