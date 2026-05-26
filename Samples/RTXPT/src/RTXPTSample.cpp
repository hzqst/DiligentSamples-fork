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

#include "RTXPTSample.hpp"
#include "GraphicsAccessories.hpp"
#include "imgui.h"

namespace Diligent
{

namespace
{

RTXPTFeatureCaps MakeFeatureCaps(const IRenderDevice* pDevice)
{
    RTXPTFeatureCaps Caps{};
    const auto&      DevInfo = pDevice->GetDeviceInfo();
    const auto&      RTProps = pDevice->GetAdapterInfo().RayTracing;

    Caps.RayTracing        = DevInfo.Features.RayTracing == DEVICE_FEATURE_STATE_ENABLED;
    Caps.BindlessResources = DevInfo.Features.BindlessResources == DEVICE_FEATURE_STATE_ENABLED;
    Caps.ComputeShaders    = DevInfo.Features.ComputeShaders == DEVICE_FEATURE_STATE_ENABLED;

    Caps.StandaloneRayTracingShaders =
        Caps.RayTracing &&
        (RTProps.CapFlags & RAY_TRACING_CAP_FLAG_STANDALONE_SHADERS) != 0;

    Caps.RayQuery =
        Caps.RayTracing &&
        (RTProps.CapFlags & RAY_TRACING_CAP_FLAG_INLINE_RAY_TRACING) != 0;

    // TODO(RTXPT-Port Phase 5): replace optimistic compiler flags with explicit DXC/ShaderMake availability checks.
    Caps.DXILCompiler  = true;
    Caps.SPIRVCompiler = true;

    return Caps;
}

} // namespace

SampleBase* CreateSample()
{
    return new RTXPTSample();
}

void RTXPTSample::Initialize(const SampleInitInfo& InitInfo)
{
    SampleBase::Initialize(InitInfo);
    m_FeatureCaps = MakeFeatureCaps(m_pDevice);

    const bool SceneLoaded = m_Scene.LoadDefaultScene(m_pDevice, m_pImmediateContext, ".");
    if (!SceneLoaded)
    {
        // TODO(RTXPT-Port Phase 2): report missing asset paths through the sample UI and keep the fallback clear path active.
    }
}

void RTXPTSample::Render()
{
    const auto ClearColor = float4{0.05f, 0.05f, 0.07f, 1.0f};
    auto*      pRTV       = m_pSwapChain->GetCurrentBackBufferRTV();
    m_pImmediateContext->SetRenderTargets(1, &pRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    if (!m_FeatureCaps.RayTracing)
    {
        // Fallback stays runnable until the RT path is wired in.
        m_pImmediateContext->ClearRenderTarget(pRTV, ClearColor.Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        return;
    }

    // TODO(RTXPT-Port Phase 4): add TraceRays path and RT PSO/SBT.
    m_pImmediateContext->ClearRenderTarget(pRTV, ClearColor.Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void RTXPTSample::Update(double CurrTime, double ElapsedTime, bool DoUpdateUI)
{
    SampleBase::Update(CurrTime, ElapsedTime, DoUpdateUI);
    m_Scene.Update(CurrTime, ElapsedTime);
}

void RTXPTSample::WindowResize(Uint32 Width, Uint32 Height)
{
    (void)Width;
    (void)Height;
}

void RTXPTSample::UpdateUI()
{
    ImGui::Begin("RTXPT Status");
    ImGui::Text("Backend: %s", GetRenderDeviceTypeString(m_pDevice->GetDeviceInfo().Type));
    ImGui::Text("RayTracing: %s", m_FeatureCaps.RayTracing ? "yes" : "no");
    ImGui::Text("Standalone RT shaders: %s", m_FeatureCaps.StandaloneRayTracingShaders ? "yes" : "no");
    ImGui::Text("RayQuery: %s", m_FeatureCaps.RayQuery ? "yes" : "no");
    ImGui::Text("Bindless: %s", m_FeatureCaps.BindlessResources ? "yes" : "no");
    ImGui::Text("Compute: %s", m_FeatureCaps.ComputeShaders ? "yes" : "no");
    ImGui::Text("Scene: %s", m_Scene.HasValidContent() ? "loaded" : "missing");
    ImGui::Text("Scene file: %s", m_Scene.GetLoadedSceneName().c_str());
    ImGui::Text("TODO(RTXPT-Port Phase 1): add backend-specific warnings and fallback explanations.");
    ImGui::Text("TODO(RTXPT-Port Phase 2): add full RTXPT scene/material/light metadata parsing.");
    ImGui::End();
}

} // namespace Diligent
