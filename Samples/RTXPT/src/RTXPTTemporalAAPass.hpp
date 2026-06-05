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

#pragma once

#include <memory>
#include <string>

#include "BasicMath.hpp"
#include "DeviceContext.h"
#include "RenderDevice.h"
#include "RTXPTFrameConstants.hpp"
#include "RTXPTRenderTargets.hpp"

#include "PostFXContext.hpp"
#include "TemporalAntiAliasing.hpp"

namespace Diligent
{

struct RTXPTTemporalAASettings
{
    float TemporalStabilityFactor = 0.9375f;
    bool  SkipRejection           = false;
};

struct RTXPTTemporalAAFrameAttribs
{
    IRenderDevice*            pDevice           = nullptr;
    IDeviceContext*           pDeviceContext    = nullptr;
    const RTXPTRenderTargets* pRenderTargets    = nullptr;
    const SampleConstants*    pFrameConstants   = nullptr;
    RTXPTTemporalAASettings   Settings          = {};
    Uint32                    FrameIndex        = 0;
    bool                      ResetHistory      = false;
    bool                      PreviousViewValid = false;
};

struct RTXPTTemporalAAStats
{
    bool        Ready                 = false;
    bool        LastExecute           = false;
    bool        LastCopyToProcessed   = false;
    bool        LastPreviousDepthCopy = false;
    Uint32      ExecuteCount          = 0;
    std::string DisabledReason;
};

class RTXPTTemporalAAPass
{
public:
    void Reset();
    bool Initialize(IRenderDevice* pDevice);

    bool CopyOutputToProcessed(IRenderDevice*            pDevice,
                               IDeviceContext*           pContext,
                               const RTXPTRenderTargets& RenderTargets);

    bool Execute(const RTXPTTemporalAAFrameAttribs& Attribs);

    static float2 ComputeJitter(Uint32 FrameIndex, Uint32 Width, Uint32 Height);

    bool                        IsReady() const { return m_Stats.Ready; }
    const RTXPTTemporalAAStats& GetStats() const { return m_Stats; }

private:
    bool PreparePostFX(const RTXPTTemporalAAFrameAttribs& Attribs);
    bool CopyCurrentDepthToPrevious(IRenderDevice*            pDevice,
                                    IDeviceContext*           pContext,
                                    const RTXPTRenderTargets& RenderTargets);

private:
    std::unique_ptr<PostFXContext>        m_PostFXContext;
    std::unique_ptr<TemporalAntiAliasing> m_TemporalAA;
    RTXPTTemporalAAStats                  m_Stats;
};

} // namespace Diligent
