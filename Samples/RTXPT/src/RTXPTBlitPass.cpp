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

#include "RTXPTBlitPass.hpp"
#include "DebugUtilities.hpp"

#include "GraphicsTypesX.hpp"

namespace Diligent
{

void RTXPTBlitPass::Reset()
{
    m_PSO.Release();
    m_SRB.Release();
    m_DrawCount = 0;
}

bool RTXPTBlitPass::Initialize(IRenderDevice* pDevice, IEngineFactory* pEngineFactory, ISwapChain* pSwapChain)
{
    Reset();

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    pEngineFactory->CreateDefaultShaderSourceStreamFactory("shaders", &pShaderSourceFactory);

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.ShaderCompiler             = SHADER_COMPILER_DXC;
    ShaderCI.CompileFlags               = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
    ShaderCI.ShaderOptimizationLevel    = SHADER_OPTIMIZATION_LEVEL_3;
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

    RefCntAutoPtr<IShader> pVS;
    ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
    ShaderCI.Desc.Name       = "RTXPT blit VS";
    ShaderCI.FilePath        = "RTXPTBlit.vsh";
    ShaderCI.EntryPoint      = "main";
    pDevice->CreateShader(ShaderCI, &pVS);

    RefCntAutoPtr<IShader> pPS;
    ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
    ShaderCI.Desc.Name       = "RTXPT blit PS";
    ShaderCI.FilePath        = "RTXPTBlit.psh";
    ShaderCI.EntryPoint      = "main";
    pDevice->CreateShader(ShaderCI, &pPS);

    VERIFY(pVS && pPS, "Failed to create RTXPT blit shaders");
    if (!pVS || !pPS)
        return false;

    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name                                  = "RTXPT blit PSO";
    PSOCreateInfo.PSODesc.PipelineType                          = PIPELINE_TYPE_GRAPHICS;
    PSOCreateInfo.GraphicsPipeline.NumRenderTargets             = 1;
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0]                = pSwapChain->GetDesc().ColorBufferFormat;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;
    PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType    = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
    PSOCreateInfo.pVS                                           = pVS;
    PSOCreateInfo.pPS                                           = pPS;

    pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_PSO);
    VERIFY(m_PSO, "Failed to create RTXPT blit PSO");
    if (!m_PSO)
        return false;

    m_PSO->CreateShaderResourceBinding(&m_SRB, true);
    VERIFY(m_SRB, "Failed to create RTXPT blit SRB");
    if (!m_SRB)
        return false;

    return true;
}

bool RTXPTBlitPass::Render(IDeviceContext* pContext, ISwapChain* pSwapChain, ITextureView* pSourceSRV)
{
    if (!IsReady())
    {
        DEV_ERROR("RTXPT blit pass is not ready");
        return false;
    }

    if (pSourceSRV == nullptr)
    {
        DEV_ERROR("RTXPT blit source SRV is null");
        return false;
    }

    IShaderResourceVariable* pTextureVar = m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture");
    if (pTextureVar == nullptr)
    {
        UNEXPECTED("RTXPT blit texture binding is unavailable");
        return false;
    }

    pTextureVar->Set(pSourceSRV);

    ITextureView* pRTV = pSwapChain->GetCurrentBackBufferRTV();
    pContext->SetRenderTargets(1, &pRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->SetPipelineState(m_PSO);
    pContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});

    ++m_DrawCount;
    return true;
}

} // namespace Diligent
