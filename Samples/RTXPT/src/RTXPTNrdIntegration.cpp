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

#if RTXPT_HAS_NRD
#    include "GraphicsTypesX.hpp"
#    include "ShaderMacroHelper.hpp"

#    include <algorithm>
#    include <array>
#    include <cstdlib>
#    include <cstddef>
#    include <sstream>
#    include <string>
#    include <utility>
#    include <vector>
#endif

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
                                     Uint32         Width,
                                     Uint32         Height,
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

#if RTXPT_HAS_NRD

namespace
{

struct ParsedShaderIdentifier
{
    std::string                                      FilePath;
    std::vector<std::pair<std::string, std::string>> Macros;
};

void* NRD_CALL NrdAllocate(void*, size_t Size, size_t)
{
    return std::malloc(Size);
}

void* NRD_CALL NrdReallocate(void*, void* Memory, size_t Size, size_t)
{
    return std::realloc(Memory, Size);
}

void NRD_CALL NrdFree(void*, void* Memory)
{
    std::free(Memory);
}

void AppendShaderSearchDir(std::string& SearchDirs, const char* Path)
{
    if (Path == nullptr || Path[0] == '\0')
        return;

    if (!SearchDirs.empty())
        SearchDirs += ';';
    SearchDirs += Path;
}

std::string BuildNrdShaderSearchDirs()
{
    std::string SearchDirs;
    AppendShaderSearchDir(SearchDirs, RTXPT_NRD_SHADER_SOURCE_DIR);
    AppendShaderSearchDir(SearchDirs, RTXPT_NRD_SHADER_INCLUDE_DIR);
    AppendShaderSearchDir(SearchDirs, RTXPT_NRD_SHADER_CONFIG_DIR);
    AppendShaderSearchDir(SearchDirs, RTXPT_NRD_MATHLIB_INCLUDE_DIR);
    return SearchDirs;
}

ParsedShaderIdentifier ParseShaderIdentifier(const char* ShaderIdentifier)
{
    ParsedShaderIdentifier Parsed;
    if (ShaderIdentifier == nullptr)
        return Parsed;

    std::istringstream Stream{ShaderIdentifier};
    std::string        Token;
    if (!std::getline(Stream, Parsed.FilePath, '|'))
        return Parsed;

    while (std::getline(Stream, Token, '|'))
    {
        const std::string::size_type EqualPos = Token.find('=');
        if (EqualPos == std::string::npos || EqualPos == 0)
            continue;

        Parsed.Macros.emplace_back(Token.substr(0, EqualPos), Token.substr(EqualPos + 1));
    }

    return Parsed;
}

void AddNrdRequiredMacros(ShaderMacroHelper& Macros, const nrd::InstanceDesc& InstanceDesc)
{
    Macros.Add("NRD_INTERNAL", 1);
    Macros.Add("NRD_COMPILER_DXC", 1);
    Macros.Add("NRD_NORMAL_ENCODING", 2);
    Macros.Add("NRD_ROUGHNESS_ENCODING", 1);
    Macros.Add("NRD_CONSTANT_BUFFER_REGISTER_INDEX", static_cast<int>(InstanceDesc.constantBufferRegisterIndex));
    Macros.Add("NRD_SAMPLERS_BASE_REGISTER_INDEX", static_cast<int>(InstanceDesc.samplersBaseRegisterIndex));
    Macros.Add("NRD_RESOURCES_BASE_REGISTER_INDEX", static_cast<int>(InstanceDesc.resourcesBaseRegisterIndex));
    Macros.Add("NRD_CONSTANT_BUFFER_AND_SAMPLERS_SPACE_INDEX", static_cast<int>(InstanceDesc.constantBufferAndSamplersSpaceIndex));
    Macros.Add("NRD_RESOURCES_SPACE_INDEX", static_cast<int>(InstanceDesc.resourcesSpaceIndex));
}

TEXTURE_FORMAT ToDiligentFormat(nrd::Format Format)
{
    static constexpr std::array<TEXTURE_FORMAT, static_cast<std::size_t>(nrd::Format::MAX_NUM)> FormatMap =
        {
            TEX_FORMAT_R8_UNORM,
            TEX_FORMAT_R8_SNORM,
            TEX_FORMAT_R8_UINT,
            TEX_FORMAT_R8_SINT,
            TEX_FORMAT_RG8_UNORM,
            TEX_FORMAT_RG8_SNORM,
            TEX_FORMAT_RG8_UINT,
            TEX_FORMAT_RG8_SINT,
            TEX_FORMAT_RGBA8_UNORM,
            TEX_FORMAT_RGBA8_SNORM,
            TEX_FORMAT_RGBA8_UINT,
            TEX_FORMAT_RGBA8_SINT,
            TEX_FORMAT_RGBA8_UNORM_SRGB,
            TEX_FORMAT_R16_UNORM,
            TEX_FORMAT_R16_SNORM,
            TEX_FORMAT_R16_UINT,
            TEX_FORMAT_R16_SINT,
            TEX_FORMAT_R16_FLOAT,
            TEX_FORMAT_RG16_UNORM,
            TEX_FORMAT_RG16_SNORM,
            TEX_FORMAT_RG16_UINT,
            TEX_FORMAT_RG16_SINT,
            TEX_FORMAT_RG16_FLOAT,
            TEX_FORMAT_RGBA16_UNORM,
            TEX_FORMAT_RGBA16_SNORM,
            TEX_FORMAT_RGBA16_UINT,
            TEX_FORMAT_RGBA16_SINT,
            TEX_FORMAT_RGBA16_FLOAT,
            TEX_FORMAT_R32_UINT,
            TEX_FORMAT_R32_SINT,
            TEX_FORMAT_R32_FLOAT,
            TEX_FORMAT_RG32_UINT,
            TEX_FORMAT_RG32_SINT,
            TEX_FORMAT_RG32_FLOAT,
            TEX_FORMAT_RGB32_UINT,
            TEX_FORMAT_RGB32_SINT,
            TEX_FORMAT_RGB32_FLOAT,
            TEX_FORMAT_RGBA32_UINT,
            TEX_FORMAT_RGBA32_SINT,
            TEX_FORMAT_RGBA32_FLOAT,
            TEX_FORMAT_RGB10A2_UNORM,
            TEX_FORMAT_UNKNOWN,
            TEX_FORMAT_R11G11B10_FLOAT,
            TEX_FORMAT_UNKNOWN,
        };

    const auto Index = static_cast<std::size_t>(Format);
    return Index < FormatMap.size() ? FormatMap[Index] : TEX_FORMAT_UNKNOWN;
}

} // namespace

void RTXPTNrdIntegration::Reset()
{
    if (m_Instance != nullptr)
    {
        nrd::DestroyInstance(*m_Instance);
        m_Instance = nullptr;
    }

    m_Device.Release();
    m_ConstantBuffer.Release();
    m_Pipelines.clear();
    m_Samplers.clear();
    m_PermanentTextures.clear();
    m_TransientTextures.clear();
    m_Stats = {};
}

bool RTXPTNrdIntegration::Initialize(IRenderDevice*  pDevice,
                                     IEngineFactory* pEngineFactory,
                                     RTXPTNrdMethod  Method,
                                     Uint32          Width,
                                     Uint32          Height,
                                     bool            ComputeSupported)
{
    Reset();
    m_Stats.Method = Method;
    m_Stats.Width  = Width;
    m_Stats.Height = Height;

    if (!ComputeSupported)
        return Fail("RTXPT NRD requires compute shader support");
    if (pDevice == nullptr || pEngineFactory == nullptr)
        return Fail("RTXPT NRD requires a device and engine factory");
    if (Width == 0 || Height == 0)
        return Fail("RTXPT NRD requires a non-zero render size");

    m_Device = pDevice;
    if (!CreateInstance(Method) ||
        !CreateConstantBuffer(pDevice) ||
        !CreateSamplers(pDevice) ||
        !CreatePipelines(pDevice, pEngineFactory) ||
        !CreatePoolTextures(pDevice, Width, Height))
        return false;

    m_Stats.Ready = true;
    return true;
}

bool RTXPTNrdIntegration::CreateInstance(RTXPTNrdMethod Method)
{
    const nrd::DenoiserDesc DenoiserDescs[] =
        {
            {m_Identifier, RTXPTToNrdDenoiser(Method)},
        };

    nrd::InstanceCreationDesc Desc      = {};
    Desc.allocationCallbacks.Allocate   = NrdAllocate;
    Desc.allocationCallbacks.Reallocate = NrdReallocate;
    Desc.allocationCallbacks.Free       = NrdFree;
    Desc.denoisers                      = DenoiserDescs;
    Desc.denoisersNum                   = 1;

    const nrd::Result Result = nrd::CreateInstance(Desc, m_Instance);
    return Result == nrd::Result::SUCCESS && m_Instance != nullptr ?
        true :
        Fail("Failed to create NRD instance");
}

bool RTXPTNrdIntegration::CreateConstantBuffer(IRenderDevice* pDevice)
{
    const nrd::InstanceDesc* pInstanceDesc = nrd::GetInstanceDesc(*m_Instance);
    if (pInstanceDesc == nullptr)
        return Fail("Failed to query NRD instance description");

    BufferDesc Desc;
    Desc.Name           = "RTXPT NRD constants";
    Desc.Size           = pInstanceDesc->constantBufferMaxDataSize;
    Desc.BindFlags      = BIND_UNIFORM_BUFFER;
    Desc.Usage          = USAGE_DYNAMIC;
    Desc.CPUAccessFlags = CPU_ACCESS_WRITE;

    pDevice->CreateBuffer(Desc, nullptr, &m_ConstantBuffer);
    return m_ConstantBuffer ? true : Fail("Failed to create NRD constant buffer");
}

bool RTXPTNrdIntegration::CreateSamplers(IRenderDevice* pDevice)
{
    const nrd::InstanceDesc* pInstanceDesc = nrd::GetInstanceDesc(*m_Instance);
    if (pInstanceDesc == nullptr)
        return Fail("Failed to query NRD instance description");

    m_Samplers.resize(pInstanceDesc->samplersNum);
    for (Uint32 SamplerIndex = 0; SamplerIndex < pInstanceDesc->samplersNum; ++SamplerIndex)
    {
        const nrd::Sampler NrdSampler = pInstanceDesc->samplers[SamplerIndex];
        if (NrdSampler != nrd::Sampler::NEAREST_CLAMP && NrdSampler != nrd::Sampler::LINEAR_CLAMP)
            return Fail("Unsupported NRD sampler");

        const bool  Linear = NrdSampler == nrd::Sampler::LINEAR_CLAMP;
        SamplerDesc Desc;
        Desc.Name      = Linear ? "RTXPT NRD linear clamp sampler" : "RTXPT NRD nearest clamp sampler";
        Desc.MinFilter = Linear ? FILTER_TYPE_LINEAR : FILTER_TYPE_POINT;
        Desc.MagFilter = Desc.MinFilter;
        Desc.MipFilter = Desc.MinFilter;
        Desc.AddressU  = TEXTURE_ADDRESS_CLAMP;
        Desc.AddressV  = TEXTURE_ADDRESS_CLAMP;
        Desc.AddressW  = TEXTURE_ADDRESS_CLAMP;

        pDevice->CreateSampler(Desc, &m_Samplers[SamplerIndex]);
        if (!m_Samplers[SamplerIndex])
            return Fail("Failed to create NRD sampler");
    }

    return true;
}

bool RTXPTNrdIntegration::CreatePipelines(IRenderDevice* pDevice, IEngineFactory* pEngineFactory)
{
    const nrd::InstanceDesc* pInstanceDesc = nrd::GetInstanceDesc(*m_Instance);
    if (pInstanceDesc == nullptr)
        return Fail("Failed to query NRD instance description");

    const std::string                              SearchDirs = BuildNrdShaderSearchDirs();
    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    pEngineFactory->CreateDefaultShaderSourceStreamFactory(SearchDirs.c_str(), &pShaderSourceFactory);
    if (!pShaderSourceFactory)
        return Fail("Failed to create NRD shader source factory");

    m_Pipelines.resize(pInstanceDesc->pipelinesNum);
    for (Uint32 PipelineIndex = 0; PipelineIndex < pInstanceDesc->pipelinesNum; ++PipelineIndex)
    {
        const nrd::PipelineDesc& NrdPipeline = pInstanceDesc->pipelines[PipelineIndex];
        ParsedShaderIdentifier   ShaderInfo  = ParseShaderIdentifier(NrdPipeline.shaderIdentifier);
        if (ShaderInfo.FilePath.empty())
            return Fail("NRD pipeline shader identifier is empty");

        ShaderMacroHelper Macros;
        AddNrdRequiredMacros(Macros, *pInstanceDesc);
        for (const auto& Macro : ShaderInfo.Macros)
            Macros.Update(Macro.first.c_str(), Macro.second.c_str());

        ShaderCreateInfo ShaderCI;
        ShaderCI.Desc.ShaderType            = SHADER_TYPE_COMPUTE;
        ShaderCI.Desc.Name                  = ShaderInfo.FilePath.c_str();
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.ShaderCompiler             = SHADER_COMPILER_DXC;
        ShaderCI.CompileFlags               = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
        ShaderCI.FilePath                   = ShaderInfo.FilePath.c_str();
        ShaderCI.EntryPoint                 = pInstanceDesc->shaderEntryPoint != nullptr && pInstanceDesc->shaderEntryPoint[0] != '\0' ? pInstanceDesc->shaderEntryPoint : "NRD_CS_MAIN";
        ShaderCI.Macros                     = Macros;
        ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

        RefCntAutoPtr<IShader> pCS;
        pDevice->CreateShader(ShaderCI, &pCS);
        if (!pCS)
            return Fail("Failed to create NRD compute shader");

        ComputePipelineStateCreateInfo PSOCreateInfo;
        PSOCreateInfo.PSODesc.Name         = ShaderInfo.FilePath.c_str();
        PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
        PSOCreateInfo.pCS                  = pCS;

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout.DefaultVariableType   = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
        PSOCreateInfo.PSODesc.ResourceLayout = ResourceLayout;

        pDevice->CreateComputePipelineState(PSOCreateInfo, &m_Pipelines[PipelineIndex].PSO);
        if (!m_Pipelines[PipelineIndex].PSO)
            return Fail("Failed to create NRD compute PSO");

        m_Pipelines[PipelineIndex].PSO->CreateShaderResourceBinding(&m_Pipelines[PipelineIndex].SRB, true);
        if (!m_Pipelines[PipelineIndex].SRB)
            return Fail("Failed to create NRD shader resource binding");
    }

    return true;
}

bool RTXPTNrdIntegration::CreatePoolTextures(IRenderDevice* pDevice, Uint32 Width, Uint32 Height)
{
    const nrd::InstanceDesc* pInstanceDesc = nrd::GetInstanceDesc(*m_Instance);
    if (pInstanceDesc == nullptr)
        return Fail("Failed to query NRD instance description");

    auto CreatePoolTexture = [&](const nrd::TextureDesc& NrdDesc, const char* PoolName, Uint32 Index, RefCntAutoPtr<ITexture>& Texture) {
        const TEXTURE_FORMAT Format = ToDiligentFormat(NrdDesc.format);
        if (Format == TEX_FORMAT_UNKNOWN)
            return Fail("Unsupported NRD pool texture format");

        std::ostringstream Name;
        Name << "RTXPT NRD " << PoolName << " texture " << Index;
        const std::string TextureName = Name.str();

        const Uint32 DownsampleFactor = std::max(static_cast<Uint32>(NrdDesc.downsampleFactor), 1u);
        TextureDesc  Desc;
        Desc.Name      = TextureName.c_str();
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = std::max(Width / DownsampleFactor, 1u);
        Desc.Height    = std::max(Height / DownsampleFactor, 1u);
        Desc.Format    = Format;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;

        pDevice->CreateTexture(Desc, nullptr, &Texture);
        return Texture ? true : Fail("Failed to create NRD pool texture");
    };

    m_PermanentTextures.resize(pInstanceDesc->permanentPoolSize);
    for (Uint32 Index = 0; Index < pInstanceDesc->permanentPoolSize; ++Index)
    {
        if (!CreatePoolTexture(pInstanceDesc->permanentPool[Index], "permanent", Index, m_PermanentTextures[Index]))
            return false;
    }

    m_TransientTextures.resize(pInstanceDesc->transientPoolSize);
    for (Uint32 Index = 0; Index < pInstanceDesc->transientPoolSize; ++Index)
    {
        if (!CreatePoolTexture(pInstanceDesc->transientPool[Index], "transient", Index, m_TransientTextures[Index]))
            return false;
    }

    return true;
}

#endif

} // namespace Diligent
