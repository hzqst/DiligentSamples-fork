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
#include "BufferView.h"
#include "DeviceContext.h"
#include "DeviceObject.h"
#include "EngineFactory.h"
#include "PipelineState.h"
#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "ShaderBindingTable.h"
#include "ShaderResourceBinding.h"
#include "TextureView.h"
#include "TopLevelAS.h"

namespace Diligent
{

struct RTXPTRayTracingPassStats
{
    bool        Ready                    = false;
    bool        LastTraceExecuted        = false;
    bool        MaterialBridgeBound      = false;
    bool        SubInstanceBound         = false;
    bool        LightBridgeBound         = false;
    bool        LightsBakerBridgeBound   = false;
    bool        EnvironmentBridgeBound   = false;
    bool        EmissiveLightBridgeBound = false;
    bool        VertexBufferBound        = false;
    bool        SkinnedVertexBufferBound = false;
    bool        IndexBufferBound         = false;
    bool        AccumulationBound        = false;
    bool        MaterialTexturesBound    = false;
    bool        AnyHitEnabled            = false;
    Uint32      MaterialTextureCount     = 0;
    Uint32      TraceCount               = 0;
    std::string DisabledReason;
};

class RTXPTRayTracingPass
{
public:
    void Reset();

    bool Initialize(IRenderDevice*        pDevice,
                    IDeviceContext*       pContext,
                    IEngineFactory*       pEngineFactory,
                    IBuffer*              pFrameConstants,
                    IBuffer*              pMaterialBuffer,
                    IBuffer*              pSubInstanceBuffer,
                    IBuffer*              pLightBuffer,
                    IBuffer*              pLightingControlBuffer,
                    IBuffer*              pLightProxyCounters,
                    IBuffer*              pLightSamplingProxies,
                    IBuffer*              pLocalSamplingBuffer,
                    ITextureView*         pFeedbackTotalWeightUAV,
                    ITextureView*         pFeedbackCandidatesUAV,
                    ITextureView*         pEnvironmentMapSRV,
                    ITextureView*         pEnvironmentImportanceMapSRV,
                    ITextureView*         pEnvironmentRadianceMapSRV,
                    ISampler*             pEnvironmentSampler,
                    ISampler*             pEnvironmentImportanceSampler,
                    IBuffer*              pEmissiveTriangleBuffer,
                    IBuffer*              pVertexBuffer,
                    IBuffer*              pSkinnedVertexBuffer,
                    IBuffer*              pIndexBuffer,
                    VALUE_TYPE            IndexValueType,
                    ITopLevelAS*          pTLAS,
                    IDeviceObject* const* pMaterialTextures,
                    Uint32                MaterialTextureCount,
                    bool                  EnableMaterialTextures,
                    bool                  EnableLDSamplerForBSDF,
                    bool                  RayTracingSupported,
                    bool                  StandaloneRTShadersSupported);

    bool Trace(IDeviceContext* pContext,
               ITextureView*   pOutputUAV,
               ITextureView*   pAccumulationUAV,
               Uint32          Width,
               Uint32          Height);

    bool                            IsReady() const { return m_Stats.Ready; }
    const RTXPTRayTracingPassStats& GetStats() const { return m_Stats; }

private:
    RefCntAutoPtr<IPipelineState>         m_PSO;
    RefCntAutoPtr<IShaderResourceBinding> m_SRB;
    RefCntAutoPtr<IShaderBindingTable>    m_SBT;
    RefCntAutoPtr<ITopLevelAS>            m_TLAS;
    RefCntAutoPtr<IBufferView>            m_IndexBufferView;
    RTXPTRayTracingPassStats              m_Stats;
};

} // namespace Diligent
