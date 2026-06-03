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
    m_SuperResolutionPass.Reset();
    m_BloomPass.Reset();
    m_ToneMappingPass.Reset();
    m_Device.Release();
    m_Stats = {};
}

bool RTXPTPostProcessPipeline::Initialize(IRenderDevice*  pDevice,
                                          IEngineFactory* pEngineFactory,
                                          ISwapChain*     pSwapChain,
                                          bool            ComputeSupported)
{
    Reset();

    if (pDevice == nullptr || pEngineFactory == nullptr || pSwapChain == nullptr)
    {
        DEV_ERROR("RTXPT post-process pipeline requires a device, engine factory, and swap chain");
        return false;
    }

    if (!ComputeSupported)
    {
        DEV_ERROR("RTXPT post-process pipeline requires compute shader support");
        return false;
    }

    m_Device = pDevice;

    m_Stats.SuperResolutionStageReady = m_SuperResolutionPass.Initialize(pDevice);
    if (!m_Stats.SuperResolutionStageReady)
    {
        DEV_ERROR("RTXPT super-resolution pass failed to initialize");
        return false;
    }

    m_Stats.AccumulationStageReady = m_AccumulationPass.Initialize(pDevice, pEngineFactory, ComputeSupported);
    if (!m_Stats.AccumulationStageReady)
    {
        DEV_ERROR("RTXPT accumulation pass failed to initialize");
        return false;
    }

    m_Stats.ToneMappingStageReady =
        m_ToneMappingPass.Initialize(pDevice, pEngineFactory, TEX_FORMAT_RGBA8_UNORM, ComputeSupported);
    if (!m_Stats.ToneMappingStageReady)
    {
        DEV_ERROR("RTXPT tone mapping pass failed to initialize");
        return false;
    }

    m_Stats.BloomStageReady = m_BloomPass.Initialize(pDevice, pEngineFactory);
    if (!m_Stats.BloomStageReady)
    {
        DEV_ERROR("RTXPT bloom pass failed to initialize");
        return false;
    }

    m_Stats.Ready = true;
    return true;
}

bool RTXPTPostProcessPipeline::ValidateRenderTargets(const RTXPTRenderTargets& RenderTargets)
{
    const bool AccumulationResourcesValid =
        !RenderTargets.IsAccumulationActive() ||
        (RenderTargets.GetAccumulatedRadianceSRV() != nullptr && RenderTargets.GetAccumulatedRadianceUAV() != nullptr);

    m_Stats.ResourcesValid =
        RenderTargets.GetOutputColorSRV() != nullptr &&
        RenderTargets.GetOutputColorUAV() != nullptr &&
        AccumulationResourcesValid &&
        RenderTargets.GetAccumulationOutputUAV() != nullptr &&
        RenderTargets.GetDepthUAV() != nullptr &&
        RenderTargets.GetDepthSRV() != nullptr &&
        RenderTargets.GetScreenMotionVectorsUAV() != nullptr &&
        RenderTargets.GetScreenMotionVectorsSRV() != nullptr &&
        RenderTargets.GetTemporalFeedback1UAV() != nullptr &&
        RenderTargets.GetTemporalFeedback2UAV() != nullptr &&
        RenderTargets.GetCombinedHistoryClampRelaxUAV() != nullptr &&
        RenderTargets.GetProcessedOutputColorSRV() != nullptr &&
        RenderTargets.GetProcessedOutputColorUAV() != nullptr &&
        RenderTargets.GetProcessedOutputColorRTV() != nullptr &&
        RenderTargets.GetLdrColorSRV() != nullptr &&
        RenderTargets.GetLdrColorUAV() != nullptr &&
        RenderTargets.GetLdrColorRTV() != nullptr &&
        (!RenderTargets.IsSuperResolutionActive() || RenderTargets.GetSuperResolutionInputColorSRV() != nullptr);

    if (!m_Stats.ResourcesValid)
        DEV_ERROR("RTXPT post-process render targets are incomplete");

    return m_Stats.ResourcesValid;
}

RTXPTSuperResolutionFrameDesc RTXPTPostProcessPipeline::ResolveSuperResolutionFrameDesc(const RTXPTSuperResolutionSettings& Settings,
                                                                                        Uint32                              DisplayWidth,
                                                                                        Uint32                              DisplayHeight,
                                                                                        TEXTURE_FORMAT                      OutputFormat,
                                                                                        bool                                ResetHistory,
                                                                                        float                               TimeDeltaSeconds)
{
    return m_SuperResolutionPass.ResolveFrameDesc(Settings, DisplayWidth, DisplayHeight, OutputFormat, ResetHistory, TimeDeltaSeconds);
}

bool RTXPTPostProcessPipeline::RunAccumulation(IDeviceContext*           pContext,
                                               const RTXPTRenderTargets& RenderTargets,
                                               Uint32                    SampleIndex,
                                               bool                      ResetAccumulation)
{
    if (!m_AccumulationPass.IsReady())
        return false;
    if (RenderTargets.GetAccumulatedRadianceSRV() == nullptr || RenderTargets.GetAccumulatedRadianceUAV() == nullptr)
    {
        DEV_ERROR("RTXPT accumulation pass requires accumulated radiance SRV and UAV");
        return false;
    }

    const Uint32 ClampedSampleIndex = std::max(SampleIndex, 1u);
    const float  BlendFactor        = ResetAccumulation ? 1.0f : 1.0f / static_cast<float>(ClampedSampleIndex);

    RTXPTAccumulationDispatch Dispatch;
    Dispatch.pInputColorSRV          = RenderTargets.GetOutputColorSRV();
    Dispatch.pAccumulatedRadianceUAV = RenderTargets.GetAccumulatedRadianceUAV();
    Dispatch.pProcessedOutputUAV     = RenderTargets.GetAccumulationOutputUAV();
    Dispatch.InputWidth              = RenderTargets.GetRenderWidth();
    Dispatch.InputHeight             = RenderTargets.GetRenderHeight();
    Dispatch.OutputWidth             = RenderTargets.GetRenderWidth();
    Dispatch.OutputHeight            = RenderTargets.GetRenderHeight();
    Dispatch.PixelOffset             = float2{0.0f, 0.0f};
    Dispatch.BlendFactor             = BlendFactor;

    const bool Executed            = m_AccumulationPass.Render(pContext, Dispatch);
    m_Stats.AccumulationStageReady = m_AccumulationPass.IsReady();
    return Executed;
}

bool RTXPTPostProcessPipeline::RunSuperResolution(IDeviceContext*                      pContext,
                                                  const RTXPTRenderTargets&            RenderTargets,
                                                  const RTXPTSuperResolutionFrameDesc& FrameDesc,
                                                  float                                CameraNear,
                                                  float                                CameraFar,
                                                  float                                CameraFovAngleVert)
{
    const bool  Executed = m_SuperResolutionPass.Execute(pContext, RenderTargets, FrameDesc, CameraNear, CameraFar, CameraFovAngleVert);
    const auto& SRStats  = m_SuperResolutionPass.GetStats();

    m_Stats.SuperResolutionStageReady = !FrameDesc.Enabled || (Executed && SRStats.UpscalerReady);
    m_Stats.LastSuperResolutionActive = Executed && SRStats.LastExecute && FrameDesc.Enabled;
    if (!Executed && SRStats.DisabledReason.empty())
        DEV_ERROR("RTXPT temporal super-resolution pass failed");
    return Executed;
}

bool RTXPTPostProcessPipeline::RunPreToneMapping(IDeviceContext*             pContext,
                                                 const RTXPTRenderTargets&   RenderTargets,
                                                 const RTXPTBloomParameters& BloomParams)
{
    const bool BloomEnabled =
        BloomParams.Enabled &&
        BloomParams.Intensity > 0.0f &&
        BloomParams.Radius > 0.0f;
    if (BloomEnabled && !m_BloomPass.ResizeResources(m_Device, RenderTargets.GetDisplayWidth(), RenderTargets.GetDisplayHeight(), RenderTargets.GetProcessedOutputColorFormat()))
    {
        m_Stats.BloomStageReady = m_BloomPass.IsReady();
        DEV_ERROR("RTXPT bloom pass failed to resize resources");
        return false;
    }

    RTXPTBloomRenderAttribs BloomAttribs;
    BloomAttribs.pSourceSRV = RenderTargets.GetProcessedOutputColorSRV();
    BloomAttribs.pTargetRTV = RenderTargets.GetProcessedOutputColorRTV();
    BloomAttribs.Width      = RenderTargets.GetDisplayWidth();
    BloomAttribs.Height     = RenderTargets.GetDisplayHeight();
    BloomAttribs.Format     = RenderTargets.GetProcessedOutputColorFormat();
    BloomAttribs.Params     = BloomParams;

    const bool BloomExecuted = m_BloomPass.Render(pContext, BloomAttribs);
    m_Stats.BloomStageReady  = m_BloomPass.IsReady();
    if (!BloomExecuted)
    {
        DEV_ERROR("RTXPT bloom pass failed to render");
        return false;
    }

    return true;
}

bool RTXPTPostProcessPipeline::RunToneMapping(IDeviceContext*                   pContext,
                                              const RTXPTRenderTargets&         RenderTargets,
                                              const RTXPTToneMappingParameters& Params,
                                              bool                              Enabled)
{
    if (!m_ToneMappingPass.ResizeResources(m_Device, RenderTargets.GetDisplayWidth(), RenderTargets.GetDisplayHeight(), RenderTargets.GetProcessedOutputColorFormat()))
    {
        m_Stats.ToneMappingStageReady = m_ToneMappingPass.IsReady();
        DEV_ERROR("RTXPT tone mapping pass failed to resize resources");
        return false;
    }

    RTXPTToneMappingRenderAttribs Attribs;
    Attribs.pSourceSRV = RenderTargets.GetProcessedOutputColorSRV();
    Attribs.pLdrRTV    = RenderTargets.GetLdrColorRTV();
    Attribs.Width      = RenderTargets.GetDisplayWidth();
    Attribs.Height     = RenderTargets.GetDisplayHeight();
    Attribs.Enabled    = Enabled;
    Attribs.pParams    = &Params;

    const bool Executed           = m_ToneMappingPass.Render(pContext, Attribs);
    m_Stats.ToneMappingStageReady = m_ToneMappingPass.IsReady();
    if (!Executed)
        DEV_ERROR("RTXPT tone mapping pass failed to render");
    return Executed;
}

} // namespace Diligent
