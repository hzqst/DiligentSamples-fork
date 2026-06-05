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
 */

#include "RTXPTTemporalAAPass.hpp"

#include <algorithm>

#include "DebugUtilities.hpp"

namespace Diligent
{

namespace HLSL
{
#include "Shaders/Common/public/ShaderDefinitions.fxh"
#include "Shaders/Common/public/BasicStructures.fxh"
} // namespace HLSL

namespace
{

float Halton(Uint32 Base, Uint32 Index)
{
    float Result = 0.0f;
    float Factor = 1.0f;
    while (Index > 0)
    {
        Factor /= static_cast<float>(Base);
        Result += Factor * static_cast<float>(Index % Base);
        Index /= Base;
    }
    return Result;
}

HLSL::CameraAttribs MakeCameraAttribs(const SampleConstants&      Constants,
                                      const PathTracerViewData&   View,
                                      const PathTracerCameraData& Camera,
                                      Uint32                      FrameIndex)
{
    const float4x4 JitteredProj = TemporalAntiAliasing::GetJitteredProjMatrix(View.MatViewToClip, View.PixelOffset);
    const float4x4 ViewProj     = View.MatWorldToView * JitteredProj;

    HLSL::CameraAttribs Attribs = {};
    Attribs.f4Position          = float4{Camera.PosW, 1.0f};
    Attribs.f4ViewportSize      = float4{View.ViewportSize.x, View.ViewportSize.y, View.ViewportSizeInv.x, View.ViewportSizeInv.y};
    Attribs.SetClipPlanes(Camera.NearZ, Camera.FarZ);
    Attribs.fHandness      = 1.0f;
    Attribs.uiFrameIndex   = FrameIndex;
    Attribs.fFocusDistance = Camera.FocalDistance;
    Attribs.fFStop         = Camera.ApertureRadius > 0.0f ? 1.0f / Camera.ApertureRadius : 5.6f;
    Attribs.fFocalLength   = 50.0f;
    Attribs.fSensorWidth   = 36.0f;
    Attribs.fSensorHeight  = 24.0f;
    Attribs.fExposure      = 0.0f;
    Attribs.f2Jitter       = View.PixelOffset;
    Attribs.mView          = View.MatWorldToView;
    Attribs.mProj          = JitteredProj;
    Attribs.mViewProj      = ViewProj;
    Attribs.mViewInv       = View.MatWorldToView.Inverse();
    Attribs.mProjInv       = JitteredProj.Inverse();
    Attribs.mViewProjInv   = ViewProj.Inverse();
    Attribs.f4ExtraData[0] = Constants.cameraPositionAndTime;
    return Attribs;
}

bool ValidateTemporalInputs(const RTXPTTemporalAAFrameAttribs& Attribs, std::string& Reason)
{
    if (Attribs.pDevice == nullptr)
        Reason = "render device is null";
    else if (Attribs.pDeviceContext == nullptr)
        Reason = "device context is null";
    else if (Attribs.pRenderTargets == nullptr)
        Reason = "render targets are null";
    else if (Attribs.pFrameConstants == nullptr)
        Reason = "frame constants are null";
    else if (Attribs.pRenderTargets->GetOutputColorSRV() == nullptr)
        Reason = "OutputColor SRV is null";
    else if (Attribs.pRenderTargets->GetProcessedOutputColorRTV() == nullptr)
        Reason = "ProcessedOutputColor RTV is null";
    else if (Attribs.pRenderTargets->GetDepthSRV() == nullptr)
        Reason = "Depth SRV is null";
    else if (Attribs.pRenderTargets->GetPreviousDepthSRV() == nullptr)
        Reason = "PreviousDepth SRV is null";
    else if (Attribs.pRenderTargets->GetPreviousDepthRTV() == nullptr)
        Reason = "PreviousDepth RTV is null";
    else if (Attribs.pRenderTargets->GetScreenMotionVectorsSRV() == nullptr)
        Reason = "ScreenMotionVectors SRV is null";
    else
        return true;
    return false;
}

} // namespace

void RTXPTTemporalAAPass::Reset()
{
    m_TemporalAA.reset();
    m_PostFXContext.reset();
    m_Stats = {};
}

bool RTXPTTemporalAAPass::Initialize(IRenderDevice* pDevice)
{
    Reset();
    if (pDevice == nullptr)
    {
        m_Stats.DisabledReason = "render device is null";
        return false;
    }

    PostFXContext::CreateInfo PostFXCI;
    PostFXCI.EnableAsyncCreation = false;
    PostFXCI.PackMatrixRowMajor  = false;
    m_PostFXContext              = std::make_unique<PostFXContext>(pDevice, PostFXCI);

    TemporalAntiAliasing::CreateInfo TAACI;
    TAACI.EnableAsyncCreation = false;
    m_TemporalAA              = std::make_unique<TemporalAntiAliasing>(pDevice, TAACI);

    m_Stats.Ready = m_PostFXContext != nullptr && m_TemporalAA != nullptr;
    if (!m_Stats.Ready)
        m_Stats.DisabledReason = "failed to create DiligentFX TAA objects";
    return m_Stats.Ready;
}

float2 RTXPTTemporalAAPass::ComputeJitter(Uint32 FrameIndex, Uint32 Width, Uint32 Height)
{
    const float    SafeWidth   = static_cast<float>(std::max(Width, Uint32{1}));
    const float    SafeHeight  = static_cast<float>(std::max(Height, Uint32{1}));
    constexpr auto SampleCount = Uint32{16};
    const Uint32   Sample      = (FrameIndex % SampleCount) + 1u;
    return float2{
        (Halton(2u, Sample) - 0.5f) / (0.5f * SafeWidth),
        (Halton(3u, Sample) - 0.5f) / (0.5f * SafeHeight)};
}

bool RTXPTTemporalAAPass::PreparePostFX(const RTXPTTemporalAAFrameAttribs& Attribs)
{
    PostFXContext::FrameDesc FrameDesc;
    FrameDesc.Index        = Attribs.FrameIndex;
    FrameDesc.Width        = Attribs.pRenderTargets->GetRenderWidth();
    FrameDesc.Height       = Attribs.pRenderTargets->GetRenderHeight();
    FrameDesc.OutputWidth  = Attribs.pRenderTargets->GetDisplayWidth();
    FrameDesc.OutputHeight = Attribs.pRenderTargets->GetDisplayHeight();

    m_PostFXContext->PrepareResources(Attribs.pDevice, FrameDesc, PostFXContext::FEATURE_FLAG_NONE);
    m_TemporalAA->PrepareResources(Attribs.pDevice,
                                   Attribs.pDeviceContext,
                                   m_PostFXContext.get(),
                                   TemporalAntiAliasing::FEATURE_FLAG_BICUBIC_FILTER);
    return true;
}

bool RTXPTTemporalAAPass::CopyOutputToProcessed(IRenderDevice*            pDevice,
                                                IDeviceContext*           pContext,
                                                const RTXPTRenderTargets& RenderTargets)
{
    m_Stats.LastCopyToProcessed = false;
    if (!m_PostFXContext)
    {
        m_Stats.DisabledReason = "PostFXContext is not initialized";
        return false;
    }
    if (pDevice == nullptr || pContext == nullptr)
    {
        m_Stats.DisabledReason = "copy requires a device and context";
        return false;
    }
    if (RenderTargets.GetOutputColorSRV() == nullptr || RenderTargets.GetProcessedOutputColorRTV() == nullptr)
    {
        m_Stats.DisabledReason = "copy requires OutputColor SRV and ProcessedOutputColor RTV";
        return false;
    }

    PostFXContext::TextureOperationAttribs CopyAttribs;
    CopyAttribs.pDevice        = pDevice;
    CopyAttribs.pDeviceContext = pContext;
    m_PostFXContext->CopyTextureColor(CopyAttribs,
                                      RenderTargets.GetOutputColorSRV(),
                                      RenderTargets.GetProcessedOutputColorRTV());
    m_Stats.DisabledReason.clear();
    m_Stats.LastCopyToProcessed = true;
    return true;
}

bool RTXPTTemporalAAPass::CopyCurrentDepthToPrevious(IRenderDevice*            pDevice,
                                                     IDeviceContext*           pContext,
                                                     const RTXPTRenderTargets& RenderTargets)
{
    m_Stats.LastPreviousDepthCopy = false;
    if (RenderTargets.GetDepthSRV() == nullptr || RenderTargets.GetPreviousDepthRTV() == nullptr)
    {
        m_Stats.DisabledReason = "previous-depth update requires Depth SRV and PreviousDepth RTV";
        return false;
    }

    PostFXContext::TextureOperationAttribs CopyAttribs;
    CopyAttribs.pDevice        = pDevice;
    CopyAttribs.pDeviceContext = pContext;
    m_PostFXContext->CopyTextureDepth(CopyAttribs,
                                      RenderTargets.GetDepthSRV(),
                                      RenderTargets.GetPreviousDepthRTV());
    m_Stats.LastPreviousDepthCopy = true;
    return true;
}

bool RTXPTTemporalAAPass::Execute(const RTXPTTemporalAAFrameAttribs& Attribs)
{
    m_Stats.LastExecute           = false;
    m_Stats.LastCopyToProcessed   = false;
    m_Stats.LastPreviousDepthCopy = false;

    if (!m_Stats.Ready || !m_PostFXContext || !m_TemporalAA)
    {
        m_Stats.DisabledReason = "Temporal AA pass is not initialized";
        return false;
    }

    std::string Reason;
    if (!ValidateTemporalInputs(Attribs, Reason))
    {
        m_Stats.DisabledReason = Reason;
        return false;
    }

    PreparePostFX(Attribs);

    const SampleConstants&    Constants          = *Attribs.pFrameConstants;
    const bool                UsePrevious        = Attribs.PreviousViewValid && !Attribs.ResetHistory;
    const Uint32              PreviousFrameIndex = Attribs.FrameIndex > 0 ? Attribs.FrameIndex - 1u : 0u;
    const HLSL::CameraAttribs CurrentCamera =
        MakeCameraAttribs(Constants, Constants.view, Constants.ptConsts.camera, Attribs.FrameIndex);
    const HLSL::CameraAttribs PreviousCamera =
        UsePrevious ?
        MakeCameraAttribs(Constants, Constants.previousView, Constants.ptConsts.prevCamera, PreviousFrameIndex) :
        CurrentCamera;

    PostFXContext::RenderAttributes PostFXAttribs;
    PostFXAttribs.pDevice             = Attribs.pDevice;
    PostFXAttribs.pDeviceContext      = Attribs.pDeviceContext;
    PostFXAttribs.pCurrDepthBufferSRV = Attribs.pRenderTargets->GetDepthSRV();
    PostFXAttribs.pPrevDepthBufferSRV = UsePrevious ?
        Attribs.pRenderTargets->GetPreviousDepthSRV() :
        Attribs.pRenderTargets->GetDepthSRV();
    PostFXAttribs.pMotionVectorsSRV   = Attribs.pRenderTargets->GetScreenMotionVectorsSRV();
    PostFXAttribs.pCurrCamera         = &CurrentCamera;
    PostFXAttribs.pPrevCamera         = &PreviousCamera;
    m_PostFXContext->Execute(PostFXAttribs);

    HLSL::TemporalAntiAliasingAttribs TAAAttribs = {};
    TAAAttribs.TemporalStabilityFactor           = std::clamp(Attribs.Settings.TemporalStabilityFactor, 0.0f, 1.0f);
    TAAAttribs.ResetAccumulation                 = Attribs.ResetHistory ? TRUE : FALSE;
    TAAAttribs.SkipRejection                     = Attribs.Settings.SkipRejection ? TRUE : FALSE;

    TemporalAntiAliasing::RenderAttributes TAARenderAttribs;
    TAARenderAttribs.pDevice         = Attribs.pDevice;
    TAARenderAttribs.pDeviceContext  = Attribs.pDeviceContext;
    TAARenderAttribs.pPostFXContext  = m_PostFXContext.get();
    TAARenderAttribs.pColorBufferSRV = Attribs.pRenderTargets->GetOutputColorSRV();
    TAARenderAttribs.pTAAAttribs     = &TAAAttribs;
    m_TemporalAA->Execute(TAARenderAttribs);

    ITextureView* pTaaOutputSRV = m_TemporalAA->GetAccumulatedFrameSRV(false);
    if (pTaaOutputSRV == nullptr)
    {
        m_Stats.DisabledReason = "Temporal AA accumulated output SRV is null";
        return false;
    }

    PostFXContext::TextureOperationAttribs CopyAttribs;
    CopyAttribs.pDevice        = Attribs.pDevice;
    CopyAttribs.pDeviceContext = Attribs.pDeviceContext;
    m_PostFXContext->CopyTextureColor(CopyAttribs,
                                      pTaaOutputSRV,
                                      Attribs.pRenderTargets->GetProcessedOutputColorRTV());
    m_Stats.LastCopyToProcessed = true;

    if (!CopyCurrentDepthToPrevious(Attribs.pDevice, Attribs.pDeviceContext, *Attribs.pRenderTargets))
        return false;

    m_Stats.DisabledReason.clear();
    m_Stats.LastExecute = true;
    ++m_Stats.ExecuteCount;
    return true;
}

} // namespace Diligent
