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

#include "RTXPTNrdIntegration.hpp"

#include "DebugUtilities.hpp"

namespace Diligent
{

RTXPTNrdIntegration::~RTXPTNrdIntegration()
{
    Reset();
}

bool RTXPTNrdIntegration::Fail(const char* Reason)
{
    m_Stats.LastFailureReason = Reason != nullptr ? Reason : "NRD integration failed";
    DEV_ERROR(m_Stats.LastFailureReason.c_str());
    return false;
}

#if !RTXPT_HAS_NRD

void RTXPTNrdIntegration::Reset()
{
    m_Stats = {};
}

bool RTXPTNrdIntegration::Initialize(IRenderDevice*,
                                     IEngineFactory*,
                                     RTXPTNrdMethod Method,
                                     Uint32 Width,
                                     Uint32 Height,
                                     bool)
{
    m_Stats.Method = Method;
    m_Stats.Width  = Width;
    m_Stats.Height = Height;
    return Fail(RTXPTGetNrdUnavailableReason());
}

bool RTXPTNrdIntegration::Dispatch(IDeviceContext*, const RTXPTNrdFrameAttribs&)
{
    m_Stats.LastDispatchExecuted = false;
    return Fail(RTXPTGetNrdUnavailableReason());
}

#endif

} // namespace Diligent
