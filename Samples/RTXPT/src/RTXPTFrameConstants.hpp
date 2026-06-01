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

#include "BasicMath.hpp"

namespace Diligent
{

struct PathTracerConstants
{
    Uint32 bounceCount       = 4;
    Uint32 sampleIndex       = 0;
    Uint32 resetAccumulation = 1;
    Uint32 minBounceCount    = 0;

    Uint32 NEEEnabled            = 1;    // Non-zero enables next-event estimation (direct light sampling).
    Uint32 environmentNEEEnabled = 1;    // bit 0 enables environment NEE; bits 1..31 pack emissive triangle count (G4).
    float  environmentIntensity  = 1.0f; // Scales the procedural-sky environment radiance.
    float  lightIntensityScale   = 1.0f; // Scales analytic (punctual) light radiance.

    Uint32 maxNEEBounceCount   = 16; // NEE budget clamp; default covers the full bounce budget.
    Uint32 analyticLightCount  = 0;  // CPU-side valid analytic lights; the dummy binding light is not sampled.
    Uint32 NEEType             = 1;  // G5: 0=Uniform, 1=Power+, 2=NEE-AT.
    Uint32 NEECandidateSamples = 5;  // G5: RIS candidate count per full sample.

    Uint32 NEEFullSamples           = 1;    // G5: visibility-tested full samples.
    Uint32 NEEMISType               = 0;    // G5 UI parity: 0=Full; approximate modes remain disabled.
    float  fireflyFilterThreshold   = 0.0f; // G1 adaptive firefly filter; 0 disables the filter.
    float  exposureScale            = 1.0f; // Scene camera exposure multiplier before in-raygen ACES.
    Uint32 diffuseBounceCount       = 2;    // R5/G9: max diffuse bounces and BSDF LD sampling window.
    Uint32 nestedDielectricsQuality = 1;    // Nested dielectrics quality: 0=Off, 1=Fast, 2=Quality.
    Uint32 _paddingR6_0             = 0;
    Uint32 _paddingR6_1             = 0;
};
static_assert(sizeof(PathTracerConstants) == 80, "PathTracerConstants layout must match PathTracer/PathTracerShared.h");

struct RTXPTEnvMapConstants
{
    float4 LocalToWorld0      = float4{1, 0, 0, 0};
    float4 LocalToWorld1      = float4{0, 1, 0, 0};
    float4 LocalToWorld2      = float4{0, 0, 1, 0};
    float4 WorldToLocal0      = float4{1, 0, 0, 0};
    float4 WorldToLocal1      = float4{0, 1, 0, 0};
    float4 WorldToLocal2      = float4{0, 0, 1, 0};
    float4 ColorEnabled       = float4{1, 1, 1, 1}; // rgb = tint * intensity, w = enabled.
    float4 ImportanceMetadata = float4{1, 1, 0, 0}; // xy = inv dim, z = base mip, w = importance enabled.
};
static_assert(sizeof(RTXPTEnvMapConstants) == 128, "RTXPTEnvMapConstants layout must match PathTracer/PathTracerShared.h");

struct SampleConstants
{
    float4x4             viewProj                  = float4x4::Identity();
    float4x4             viewProjInv               = float4x4::Identity();
    float4               cameraPositionAndTime     = float4{0, 0, 0, 0};
    float4               viewportSizeAndFrameIndex = float4{0, 0, 0, 0};
    PathTracerConstants  ptConsts                  = {};
    RTXPTEnvMapConstants envMap                    = {};
};
static_assert(sizeof(SampleConstants) == 368, "SampleConstants layout must match PathTracer/PathTracerShared.h");

} // namespace Diligent
