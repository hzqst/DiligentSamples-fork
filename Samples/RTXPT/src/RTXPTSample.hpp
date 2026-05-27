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

struct RTXPTFrameConstants
{
    float4x4 ViewProj              = float4x4::Identity();
    float4x4 ViewProjInv           = float4x4::Identity();
    float4   CameraPosition_Time   = float4{0, 0, 0, 0};
    float4   ViewportSize_FrameIdx = float4{0, 0, 0, 0};
};

class RTXPTSample final : public SampleBase
{
public:
    virtual void        Initialize(const SampleInitInfo& InitInfo) override final;
    virtual void        Render() override final;
    virtual void        Update(double CurrTime, double ElapsedTime, bool DoUpdateUI) override final;
    virtual void        WindowResize(Uint32 Width, Uint32 Height) override final;
    virtual const Char* GetSampleName() const override final { return "RTXPT"; }

protected:
    virtual void UpdateUI() override final;

private:
    void CreateFrameResources();
    void UpdateFrameConstants(double CurrTime);
    void CreatePhase4Passes();
    bool EnsureRenderTargets();
    void ClearFallback(const float4& ClearColor);

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
    RefCntAutoPtr<IBuffer>      m_FrameConstantsCB;
    RTXPTFrameConstants         m_LastFrameConstants;
    Uint32                      m_FrameIndex             = 0;
    bool                        m_EnableDebugComputePass = true;
};

} // namespace Diligent
