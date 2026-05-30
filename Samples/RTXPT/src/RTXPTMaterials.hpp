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

// GPU material record consumed by the reference path tracer (mirrors RTXPTMaterialData in PathTracer/PathTracerShared.h).
// One entry per GLTF material; the closest-hit / any-hit shaders index it via RTXPTSubInstanceData::MaterialID.
// All texture indices/slices reference the shared bindless material-texture table (one entry per GLTF texture).
struct RTXPTMaterialData
{
    float4 BaseColorFactor = float4{1, 1, 1, 1};

    float3 EmissiveFactor = float3{0, 0, 0};
    float  AlphaCutoff    = 0.5f;

    Uint32 Flags                 = 0;
    Uint32 BaseColorTextureIndex = 0;
    Uint32 EmissiveTextureIndex  = 0;
    float  MetallicFactor        = 1.0f;

    float  RoughnessFactor               = 1.0f;
    float  BaseColorTextureSlice         = 0.0f;
    float  EmissiveTextureSlice          = 0.0f;
    Uint32 MetallicRoughnessTextureIndex = 0;

    float  MetallicRoughnessTextureSlice = 0.0f;
    Uint32 NormalTextureIndex            = 0;
    float  NormalTextureSlice            = 0.0f;
    float  NormalScale                   = 1.0f;

    float Padding0 = 0.0f;
    float Padding1 = 0.0f;
    float Padding2 = 0.0f;
    float Padding3 = 0.0f;
};
static_assert(sizeof(RTXPTMaterialData) == 96, "RTXPTMaterialData layout must match PathTracer/PathTracerShared.h");
static_assert(offsetof(RTXPTMaterialData, MetallicRoughnessTextureIndex) == 60,
              "RTXPTMaterialData MetallicRoughnessTextureIndex offset must match PathTracer/PathTracerShared.h");
static_assert(offsetof(RTXPTMaterialData, NormalTextureIndex) == 68,
              "RTXPTMaterialData NormalTextureIndex offset must match PathTracer/PathTracerShared.h");
static_assert(offsetof(RTXPTMaterialData, NormalScale) == 76,
              "RTXPTMaterialData NormalScale offset must match PathTracer/PathTracerShared.h");

// Flag bits for RTXPTMaterialData::Flags. Keep in sync with kRTXPTMaterialFlag* in PathTracer/PathTracerShared.h.
constexpr Uint32 kRTXPTMaterialFlag_HasBaseColorTexture         = 0x1u;
constexpr Uint32 kRTXPTMaterialFlag_AlphaTested                 = 0x2u;
constexpr Uint32 kRTXPTMaterialFlag_HasEmissiveTexture          = 0x4u;
constexpr Uint32 kRTXPTMaterialFlag_HasMetallicRoughnessTexture = 0x8u;
constexpr Uint32 kRTXPTMaterialFlag_HasNormalTexture            = 0x10u;

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
    // RTXPTMaterialData::BaseColorTextureIndex / EmissiveTextureIndex. The SRV views are owned here and keep
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
