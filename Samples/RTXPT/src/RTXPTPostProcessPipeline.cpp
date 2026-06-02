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

#include "DebugUtilities.hpp"

namespace Diligent
{

void RTXPTPostProcessPipeline::Reset()
{
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
        m_Stats.DisabledReason = "post-process pipeline missing device, engine factory, or swap chain";
        return false;
    }

    m_Stats.Ready = true;
    if (!ComputeSupported)
        m_Stats.DisabledReason = "compute shaders are unavailable; post-process compute stages remain disabled";

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
        RenderTargets.GetLdrColorSRV() != nullptr &&
        RenderTargets.GetLdrColorUAV() != nullptr &&
        RenderTargets.GetLdrColorScratchSRV() != nullptr &&
        RenderTargets.GetLdrColorScratchUAV() != nullptr;

    if (!m_Stats.ResourcesValid)
        m_Stats.DisabledReason = "post-process render targets are incomplete";

    return m_Stats.ResourcesValid;
}

} // namespace Diligent
