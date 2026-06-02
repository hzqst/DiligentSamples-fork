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

#include "RTXPTPostProcessPipeline.hpp"

#include <algorithm>

#include "DebugUtilities.hpp"

namespace Diligent
{

void RTXPTPostProcessPipeline::Reset()
{
    m_AccumulationPass.Reset();
    m_ToneMappingPass.Reset();
    m_Device.Release();
    m_Stats                 = {};
    m_FeatureDisabledReason = {};
}

bool RTXPTPostProcessPipeline::Initialize(IRenderDevice*  pDevice,
                                          IEngineFactory* pEngineFactory,
                                          ISwapChain*     pSwapChain,
                                          bool            ComputeSupported)
{
    Reset();

    if (pDevice == nullptr || pEngineFactory == nullptr || pSwapChain == nullptr)
    {
        m_Stats.DisabledReason = "post-process pipeline missing device, engine factory, or swap chain";
        return false;
    }

    m_Stats.Ready = true;
    m_Device      = pDevice;
    if (!ComputeSupported)
    {
        m_FeatureDisabledReason = "compute shaders are unavailable; post-process compute stages remain disabled";
        m_Stats.DisabledReason  = m_FeatureDisabledReason;
    }
    else
    {
        m_Stats.AccumulationStageReady = m_AccumulationPass.Initialize(pDevice, pEngineFactory, ComputeSupported);
        if (!m_Stats.AccumulationStageReady)
        {
            m_FeatureDisabledReason = m_AccumulationPass.GetStats().DisabledReason.empty() ?
                "RTXPT accumulation pass failed to initialize" :
                m_AccumulationPass.GetStats().DisabledReason;
            m_Stats.DisabledReason = m_FeatureDisabledReason;
        }
    }

    m_Stats.ToneMappingStageReady =
        m_ToneMappingPass.Initialize(pDevice, pEngineFactory, TEX_FORMAT_RGBA8_UNORM, ComputeSupported);
    if (!m_Stats.ToneMappingStageReady && m_Stats.DisabledReason.empty())
    {
        m_FeatureDisabledReason = m_ToneMappingPass.GetStats().DisabledReason.empty() ?
            "RTXPT tone mapping pass failed to initialize" :
            m_ToneMappingPass.GetStats().DisabledReason;
        m_Stats.DisabledReason = m_FeatureDisabledReason;
    }

    return true;
}

bool RTXPTPostProcessPipeline::ValidateRenderTargets(const RTXPTRenderTargets& RenderTargets)
{
    m_Stats.ResourcesValid =
        RenderTargets.GetOutputColorSRV() != nullptr &&
        RenderTargets.GetOutputColorUAV() != nullptr &&
        RenderTargets.GetAccumulatedRadianceSRV() != nullptr &&
        RenderTargets.GetAccumulatedRadianceUAV() != nullptr &&
        RenderTargets.GetProcessedOutputColorSRV() != nullptr &&
        RenderTargets.GetProcessedOutputColorUAV() != nullptr &&
        RenderTargets.GetProcessedOutputColorRTV() != nullptr &&
        RenderTargets.GetLdrColorSRV() != nullptr &&
        RenderTargets.GetLdrColorUAV() != nullptr &&
        RenderTargets.GetLdrColorRTV() != nullptr &&
        RenderTargets.GetLdrColorScratchSRV() != nullptr &&
        RenderTargets.GetLdrColorScratchUAV() != nullptr;

    m_Stats.DisabledReason = m_Stats.ResourcesValid ?
        m_FeatureDisabledReason :
        "post-process render targets are incomplete";

    return m_Stats.ResourcesValid;
}

bool RTXPTPostProcessPipeline::RunAccumulation(IDeviceContext*            pContext,
                                               const RTXPTRenderTargets& RenderTargets,
                                               Uint32                    SampleIndex,
                                               bool                      ResetAccumulation)
{
    if (!m_AccumulationPass.IsReady())
        return false;

    const Uint32 ClampedSampleIndex = std::max(SampleIndex, 1u);
    const float  BlendFactor        = ResetAccumulation ? 1.0f : 1.0f / static_cast<float>(ClampedSampleIndex);

    RTXPTAccumulationDispatch Dispatch;
    Dispatch.pInputColorSRV          = RenderTargets.GetOutputColorSRV();
    Dispatch.pAccumulatedRadianceUAV = RenderTargets.GetAccumulatedRadianceUAV();
    Dispatch.pProcessedOutputUAV     = RenderTargets.GetProcessedOutputColorUAV();
    Dispatch.InputWidth              = RenderTargets.GetWidth();
    Dispatch.InputHeight             = RenderTargets.GetHeight();
    Dispatch.OutputWidth             = RenderTargets.GetWidth();
    Dispatch.OutputHeight            = RenderTargets.GetHeight();
    Dispatch.PixelOffset             = float2{0.0f, 0.0f};
    Dispatch.BlendFactor             = BlendFactor;

    const bool Executed = m_AccumulationPass.Render(pContext, Dispatch);
    m_Stats.AccumulationStageReady = m_AccumulationPass.IsReady();
    return Executed;
}

bool RTXPTPostProcessPipeline::RunToneMapping(IDeviceContext*                    pContext,
                                              const RTXPTRenderTargets&         RenderTargets,
                                              const RTXPTToneMappingParameters& Params,
                                              bool                              Enabled)
{
    if (!m_ToneMappingPass.ResizeResources(m_Device, RenderTargets.GetWidth(), RenderTargets.GetHeight(), RenderTargets.GetProcessedOutputColorFormat()))
    {
        m_Stats.ToneMappingStageReady = m_ToneMappingPass.IsReady();
        m_Stats.DisabledReason        = m_ToneMappingPass.GetStats().DisabledReason.empty() ?
            "RTXPT tone mapping pass failed to resize resources" :
            m_ToneMappingPass.GetStats().DisabledReason;
        return false;
    }

    RTXPTToneMappingRenderAttribs Attribs;
    Attribs.pSourceSRV = RenderTargets.GetProcessedOutputColorSRV();
    Attribs.pLdrRTV    = RenderTargets.GetLdrColorRTV();
    Attribs.Width      = RenderTargets.GetWidth();
    Attribs.Height     = RenderTargets.GetHeight();
    Attribs.Enabled    = Enabled;
    Attribs.pParams    = &Params;

    const bool Executed = m_ToneMappingPass.Render(pContext, Attribs);
    m_Stats.ToneMappingStageReady = m_ToneMappingPass.IsReady();
    if (!Executed)
    {
        m_Stats.DisabledReason = m_ToneMappingPass.GetStats().DisabledReason.empty() ?
            "RTXPT tone mapping pass failed to render" :
            m_ToneMappingPass.GetStats().DisabledReason;
    }
    return Executed;
}

} // namespace Diligent
