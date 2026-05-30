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
#include <vector>

#include "BottomLevelAS.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "GLTFLoader.hpp"
#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "TopLevelAS.h"

namespace Diligent
{

struct SubInstanceData
{
    Uint32 MaterialID   = 0;
    Uint32 Flags        = 0;
    Uint32 IndexOffset  = 0;
    Uint32 IndexCount   = 0;
    Uint32 VertexOffset = 0;
    Uint32 VertexCount  = 0;
    Uint32 _padding0    = 0;
    Uint32 _padding1    = 0;
};
static_assert(sizeof(SubInstanceData) == 32, "SubInstanceData layout must match PathTracer/PathTracerShared.h");

// Flag bits for SubInstanceData::Flags.
constexpr Uint32 kSubInstanceFlag_Indexed = 0x1u;
constexpr Uint32 kSubInstanceFlag_Skinned = 0x2u;

struct RTXPTAccelerationStructureStats
{
    bool        RayTracingSupported      = false;
    bool        Built                    = false;
    Uint32      GeometryCount            = 0;
    Uint32      InstanceCount            = 0;
    Uint32      BLASCount                = 0;
    Uint32      SubInstanceCount         = 0;
    Uint32      AlphaTestedGeometryCount = 0;
    Uint64      BLASScratchSize          = 0;
    Uint64      TLASScratchSize          = 0;
    std::string DisabledReason;
    std::string LastError;
};

class RTXPTAccelerationStructures
{
public:
    void Reset();

    bool BuildStaticScene(IRenderDevice*               pDevice,
                          IDeviceContext*              pContext,
                          const GLTF::Model&           Model,
                          Uint32                       SceneIndex,
                          VALUE_TYPE                   IndexType,
                          const GLTF::ModelTransforms& Transforms,
                          bool                         RayTracingSupported);

    bool IsBuilt() const { return m_Stats.Built && m_TLAS; }

    ITopLevelAS* GetTLAS() const { return m_TLAS; }
    IBuffer*     GetSubInstanceBuffer() const { return m_SubInstanceBuffer; }

    const RTXPTAccelerationStructureStats& GetStats() const { return m_Stats; }

private:
    struct BLASRecord
    {
        std::string                   Name;
        RefCntAutoPtr<IBottomLevelAS> BLAS;
        Uint32                        GeometryCount = 0;
    };

    std::vector<BLASRecord>         m_BLASRecords;
    RefCntAutoPtr<ITopLevelAS>      m_TLAS;
    RefCntAutoPtr<IBuffer>          m_BLASScratch;
    RefCntAutoPtr<IBuffer>          m_TLASScratch;
    RefCntAutoPtr<IBuffer>          m_InstanceBuffer;
    RefCntAutoPtr<IBuffer>          m_SubInstanceBuffer;
    RTXPTAccelerationStructureStats m_Stats;
};

} // namespace Diligent
