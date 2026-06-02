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

#include "Buffer.h"
#include "DeviceContext.h"
#include "EngineFactory.h"
#include "PipelineState.h"
#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "Shader.h"
#include "ShaderResourceBinding.h"
#include "Texture.h"
#include "TextureView.h"

namespace Diligent
{

struct RTXPTPostProcessParameters
{
    bool  EnableHdrTest       = false;
    bool  EnableEdgeDetection = false;
    float EdgeThreshold       = 0.1f;
};

struct RTXPTPostProcessRenderAttribs
{
    ITextureView*              pProcessedOutputUAV     = nullptr;
    ITexture*                  pLdrColorTexture        = nullptr;
    ITexture*                  pLdrColorScratchTexture = nullptr;
    ITextureView*              pLdrColorScratchSRV     = nullptr;
    ITextureView*              pLdrColorUAV            = nullptr;
    Uint32                     Width                   = 0;
    Uint32                     Height                  = 0;
    RTXPTPostProcessParameters Params;
};

struct RTXPTPostProcessPassStats
{
    bool   Ready                      = false;
    bool   LastHdrTestExecuted        = false;
    bool   LastEdgeDetectionExecuted  = false;
    Uint32 HdrTestDispatchCount       = 0;
    Uint32 EdgeDetectionDispatchCount = 0;
};

class RTXPTPostProcessPass
{
public:
    void Reset();
    bool Initialize(IRenderDevice* pDevice, IEngineFactory* pEngineFactory, bool ComputeSupported);
    bool RunHdrTest(IDeviceContext* pContext, const RTXPTPostProcessRenderAttribs& Attribs);
    bool RunEdgeDetection(IDeviceContext* pContext, const RTXPTPostProcessRenderAttribs& Attribs);

    bool                             IsReady() const { return m_Stats.Ready; }
    const RTXPTPostProcessPassStats& GetStats() const { return m_Stats; }

private:
    bool CreatePostProcessPSO(IRenderDevice*                         pDevice,
                              const ShaderCreateInfo&                BaseShaderCI,
                              const char*                            ShaderName,
                              const char*                            PSOName,
                              const char*                            ModeMacro,
                              RefCntAutoPtr<IPipelineState>&         PSO,
                              RefCntAutoPtr<IShaderResourceBinding>& SRB);
    bool UpdateConstants(IDeviceContext* pContext, Uint32 Width, Uint32 Height, float EdgeThreshold);

    RefCntAutoPtr<IPipelineState>         m_HdrTestPSO;
    RefCntAutoPtr<IPipelineState>         m_EdgeDetectionPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_HdrTestSRB;
    RefCntAutoPtr<IShaderResourceBinding> m_EdgeDetectionSRB;
    RefCntAutoPtr<IBuffer>                m_Constants;
    RTXPTPostProcessPassStats             m_Stats;
};

} // namespace Diligent
