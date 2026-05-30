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
#include "FirstPersonCamera.hpp"
#include "RefCntAutoPtr.hpp"
#include "RTXPTAccelerationStructures.hpp"
#include "RTXPTBlitPass.hpp"
#include "RTXPTComputePass.hpp"
#include "RTXPTLights.hpp"
#include "RTXPTMaterials.hpp"
#include "RTXPTRayTracingPass.hpp"
#include "RTXPTRenderTargets.hpp"
#include "SampleBase.hpp"
#include "RTXPTScene.hpp"

namespace Diligent
{

struct RTXPTFeatureCaps
{
    bool RayTracing                  = false;
    bool StandaloneRayTracingShaders = false;
    bool RayQuery                    = false;
    bool BindlessResources           = false;
    bool ComputeShaders              = false;
    bool DXILCompiler                = false;
    bool SPIRVCompiler               = false;
};

struct RTXPTPathTracerSettings
{
    Uint32 MaxBounces        = 4;
    Uint32 AccumulationFrame = 0;
    Uint32 ResetAccumulation = 1;
    Uint32 MinBounces        = 0;

    Uint32 EnableNEE           = 1;    // Non-zero enables next-event estimation (direct light sampling).
    Uint32 EnableEnvNEE        = 1;    // Non-zero adds environment (sky) NEE with MIS alongside analytic lights.
    float  EnvIntensity        = 1.0f; // Scales the procedural-sky environment radiance.
    float  LightIntensityScale = 1.0f; // Scales analytic (punctual) light radiance.

    Uint32 MaxNEEBounces     = 1; // Limits NEE work to the first N path bounces to avoid TDR-heavy dispatches.
    Uint32 AnalyticLightCount = 0; // CPU-side count of valid analytic lights; the uploaded dummy light is not sampled.
    Uint32 Padding1           = 0;
    Uint32 Padding2           = 0;
};
static_assert(sizeof(RTXPTPathTracerSettings) == 48, "RTXPTPathTracerSettings layout must match PathTracer/PathTracerShared.h");

// Reference-mode UI state, mirroring the reference subset of RTXPT-fork's SampleUIData
// (D:/RTXPT-fork/Rtxpt/SampleUI.h). These fields back the present-but-disabled placeholder
// controls in UpdateUI(): each one is implemented in a later phase (R1/R3/R4/R5/R6 or the
// separate tone-mapping Phase 6) and is intentionally NOT yet wired into
// RTXPTPathTracerSettings / the GPU frame constants. Wiring a field in is part of the phase
// that enables its control, at which point the matching BeginDisabled() guard is removed.
struct RTXPTReferenceUIState
{
    bool  AccumulationAA                  = true;  // Jitter AA: always on in our port (no toggle yet).
    bool  EnableRussianRoulette           = true;  // RR: always on; start bounce == Min bounces (RR start).
    bool  ReferenceFireflyFilterEnabled   = true;  // Phase R1 (G1): adaptive firefly filter.
    float ReferenceFireflyFilterThreshold = 5.0f;  // Phase R1 (G1).
    int   DiffuseBounceCount              = 2;     // Phase R5 (G9): separate diffuse-bounce limit.
    bool  EnableToneMapping               = true;  // Phase 6: configurable tone-map pass (ACES is always applied now).
    int   NEEType                         = 1;     // Phase R3 (G5): 0=Uniform, 1=Power+, 2=NEE-AT.
    int   NEECandidateSamples             = 5;     // Phase R3 (G5): RIS candidate count.
    int   NEEFullSamples                  = 1;     // Phase R3 (G5): visibility-tested full samples.
    int   NEEMISType                      = 0;     // Phase R3 (G5): 0=Full, 1=ApproxInRealtime, 2=Approximate.
    int   NestedDielectricsQuality        = 1;     // Phase R6 (G10): 0=Off, 1=Fast, 2=Quality.
    bool  EnableLDSamplerForBSDF          = true;  // Phase R5 (G9): low-discrepancy (Sobol/Owen) sampler.
    bool  EnvironmentMapEnabled           = false; // Phase R4 (G7): HDR env-map loading (procedural sky is always active).
};

struct RTXPTFrameConstants
{
    float4x4                ViewProj              = float4x4::Identity();
    float4x4                ViewProjInv           = float4x4::Identity();
    float4                  CameraPosition_Time   = float4{0, 0, 0, 0};
    float4                  ViewportSize_FrameIdx = float4{0, 0, 0, 0};
    RTXPTPathTracerSettings PathTracer            = {};
};
static_assert(sizeof(RTXPTFrameConstants) == 208, "RTXPTFrameConstants layout must match PathTracer/PathTracerShared.h");

class RTXPTSample final : public SampleBase
{
public:
    virtual void        Initialize(const SampleInitInfo& InitInfo) override final;
    virtual void        Render() override final;
    virtual void        Update(double CurrTime, double ElapsedTime, bool DoUpdateUI) override final;
    virtual void        WindowResize(Uint32 Width, Uint32 Height) override final;
    virtual const Char* GetSampleName() const override final { return "RTXPT"; }
    virtual void        ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs) override final;

protected:
    virtual void UpdateUI() override final;

private:
    void CreateFrameResources();
    void InitializeCamera();
    bool ApplySceneCamera(Uint32 CameraIndex);
    void UpdateCameraProjection(Uint32 Width, Uint32 Height);
    void UpdateFrameConstants(double CurrTime);
    void CreatePhase4Passes();
    bool EnsureRenderTargets();
    void ClearFallback(const float4& ClearColor);
    void RequestAccumulationReset(const char* Reason);

    RTXPTFeatureCaps            m_FeatureCaps;
    std::string                 m_AssetsRoot;
    RTXPTScene                  m_Scene;
    RTXPTMaterials              m_Materials;
    RTXPTLights                 m_Lights;
    RTXPTAccelerationStructures m_AccelerationStructures;
    RTXPTRenderTargets          m_RenderTargets;
    RTXPTRayTracingPass         m_RayTracingPass;
    RTXPTComputePass            m_DebugComputePass;
    RTXPTBlitPass               m_BlitPass;
    FirstPersonCamera           m_Camera;
    RefCntAutoPtr<IBuffer>      m_FrameConstantsCB;
    RTXPTFrameConstants         m_LastFrameConstants;
    RTXPTReferenceUIState       m_ReferenceUI;
    float4x4                    m_LastCameraView           = float4x4::Identity();
    float4x4                    m_LastCameraProj           = float4x4::Identity();
    float                       m_CameraVerticalFov        = PI_F / 4.0f;
    float                       m_CameraNearPlane          = 0.1f;
    float                       m_CameraFarPlane           = 10000.0f;
    Uint32                      m_FrameIndex                = 0;
    Uint32                      m_AccumulationFrame         = 0;
    Uint32                      m_MaxBounces                = 4;
    Uint32                      m_MinBounces                = 3;
    Uint32                      m_MaxNEEBounces             = 1;
    bool                        m_EnableNEE                 = true;
    bool                        m_EnableEnvNEE              = true;
    float                       m_EnvIntensity              = 1.0f;
    float                       m_LightIntensityScale       = 1.0f;
    int                         m_SelectedSceneCamera       = -1;
    bool                        m_EnableDebugComputePass    = false;
    bool                        m_ResetAccumulationPending  = true;
    bool                        m_AccumulationActive        = false;
    bool                        m_HasLastCameraMatrices     = false;
};

} // namespace Diligent
