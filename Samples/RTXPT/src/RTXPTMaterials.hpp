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

#include <cstddef>
#include <string>
#include <vector>

#include "Buffer.h"
#include "DeviceObject.h"
#include "GLTFLoader.hpp"
#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "Texture.h"
#include "TextureView.h"

namespace Diligent
{

// GPU material record consumed by the reference path tracer (mirrors MaterialPTData in PathTracer/PathTracerShared.h).
// One entry per GLTF material; the closest-hit / any-hit shaders index it via SubInstanceData::MaterialID.
// All texture indices/slices reference the shared bindless material-texture table (one entry per GLTF texture).
struct MaterialPTData
{
    float4 baseColorFactor = float4{1, 1, 1, 1};

    float3 emissiveFactor = float3{0, 0, 0};
    float  alphaCutoff    = 0.5f;

    Uint32 flags                 = 0;
    Uint32 baseColorTextureIndex = 0;
    Uint32 emissiveTextureIndex  = 0;
    float  metallicFactor        = 1.0f;

    float  roughnessFactor               = 1.0f;
    float  baseColorTextureSlice         = 0.0f;
    float  emissiveTextureSlice          = 0.0f;
    Uint32 metallicRoughnessTextureIndex = 0;

    float  metallicRoughnessTextureSlice = 0.0f;
    Uint32 normalTextureIndex            = 0;
    float  normalTextureSlice            = 0.0f;
    float  normalScale                   = 1.0f;

    float _padding0 = 0.0f;
    float _padding1 = 0.0f;
    float _padding2 = 0.0f;
    float _padding3 = 0.0f;
};
static_assert(sizeof(MaterialPTData) == 96, "MaterialPTData layout must match PathTracer/PathTracerShared.h");
static_assert(offsetof(MaterialPTData, metallicRoughnessTextureIndex) == 60,
              "MaterialPTData metallicRoughnessTextureIndex offset must match PathTracer/PathTracerShared.h");
static_assert(offsetof(MaterialPTData, normalTextureIndex) == 68,
              "MaterialPTData normalTextureIndex offset must match PathTracer/PathTracerShared.h");
static_assert(offsetof(MaterialPTData, normalScale) == 76,
              "MaterialPTData normalScale offset must match PathTracer/PathTracerShared.h");

// Flag bits for MaterialPTData::flags. Keep in sync with kMaterialFlag* in PathTracer/PathTracerShared.h.
constexpr Uint32 kMaterialFlag_HasBaseColorTexture         = 0x1u;
constexpr Uint32 kMaterialFlag_AlphaTested                 = 0x2u;
constexpr Uint32 kMaterialFlag_HasEmissiveTexture          = 0x4u;
constexpr Uint32 kMaterialFlag_HasMetallicRoughnessTexture = 0x8u;
constexpr Uint32 kMaterialFlag_HasNormalTexture            = 0x10u;

// A material is alpha tested only when it uses ALPHA_MODE_MASK and actually has a base-color texture to
// sample the alpha from. The acceleration-structure geometry flags and the GPU material flags must agree,
// so both sides call this single helper.
bool RTXPTMaterialIsAlphaTested(const GLTF::Material& Material);

struct RTXPTMaterialStats
{
    Uint32      MaterialCount = 0;
    Uint32      TextureCount  = 0;
    std::string LastError;
};

class RTXPTMaterials
{
public:
    void Reset();
    bool Upload(IRenderDevice* pDevice, const GLTF::Model& Model);

    const RTXPTMaterialStats& GetStats() const { return m_Stats; }
    IBuffer*                  GetMaterialBuffer() const { return m_MaterialBuffer; }

    // Bindless material-texture table. Indices match GLTF::Model texture indices and are referenced by
    // MaterialPTData::baseColorTextureIndex / emissiveTextureIndex. The SRV views are owned here and keep
    // the underlying texture resources alive.
    Uint32                GetTextureCount() const { return static_cast<Uint32>(m_TextureBindings.size()); }
    IDeviceObject* const* GetTextureBindings() const { return m_TextureBindings.empty() ? nullptr : m_TextureBindings.data(); }

private:
    RefCntAutoPtr<IBuffer>                   m_MaterialBuffer;
    std::vector<RefCntAutoPtr<ITextureView>> m_TextureViews;
    std::vector<IDeviceObject*>              m_TextureBindings;
    RTXPTMaterialStats                       m_Stats;
};

} // namespace Diligent
