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

#include "RTXPTEnvMapBaker.hpp"
#include "RTXPTSceneJson.hpp"

#include "DebugUtilities.hpp"
#include "FileSystem.hpp"
#include "TextureUtilities.h"
#include "PBR_Renderer.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

constexpr const char* kProceduralSkyPath = "==PROCEDURAL_SKY==";
constexpr Uint32      kFallbackCubeSize  = 4;

std::string ToLower(std::string Value)
{
    std::transform(Value.begin(), Value.end(), Value.begin(),
                   [](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
    return Value;
}

bool IsProceduralSkyPath(const std::string& Path)
{
    return Path.empty() || Path == kProceduralSkyPath;
}

bool IsEnvironmentFile(const std::filesystem::path& Path)
{
    const std::string Ext = ToLower(Path.extension().string());
    return Ext == ".hdr" || Ext == ".exr" || Ext == ".dds" || Ext == ".ktx" || Ext == ".ktx2";
}

bool EnvMapSourceChanged(const RTXPTEnvMapSettings& Lhs, const RTXPTEnvMapSettings& Rhs)
{
    return Lhs.SourceRelativePath != Rhs.SourceRelativePath ||
        Lhs.TargetCubeResolution != Rhs.TargetCubeResolution;
}

bool CreateCubeTexture(IRenderDevice* pDevice, const char* Name, Uint32 Size, Uint32 Color,
                       RefCntAutoPtr<ITexture>& Texture, RefCntAutoPtr<ITextureView>& SRV)
{
    Texture.Release();
    SRV.Release();

    TextureDesc Desc;
    Desc.Name      = Name;
    Desc.Type      = RESOURCE_DIM_TEX_CUBE;
    Desc.Usage     = USAGE_IMMUTABLE;
    Desc.BindFlags = BIND_SHADER_RESOURCE;
    Desc.Format    = TEX_FORMAT_RGBA8_UNORM;
    Desc.Width     = Size;
    Desc.Height    = Size;
    Desc.MipLevels = 1;
    Desc.ArraySize = 6;

    std::vector<Uint32>            Data(static_cast<size_t>(Desc.ArraySize) * Size * Size, Color);
    std::vector<TextureSubResData> SubResData(Desc.ArraySize);
    for (Uint32 Face = 0; Face < Desc.ArraySize; ++Face)
    {
        SubResData[Face].pData  = Data.data() + static_cast<size_t>(Face) * Size * Size;
        SubResData[Face].Stride = Uint64{Size} * sizeof(Uint32);
    }
    TextureData InitData{SubResData.data(), static_cast<Uint32>(SubResData.size())};
    pDevice->CreateTexture(Desc, &InitData, &Texture);

    SRV = Texture ? Texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
    return SRV != nullptr;
}

bool CreateRGBA8Texture(IRenderDevice* pDevice, const char* Name, Uint32 Color,
                        RefCntAutoPtr<ITexture>& Texture, RefCntAutoPtr<ITextureView>& SRV)
{
    Texture.Release();
    SRV.Release();

    TextureDesc Desc;
    Desc.Name      = Name;
    Desc.Type      = RESOURCE_DIM_TEX_2D;
    Desc.Usage     = USAGE_IMMUTABLE;
    Desc.BindFlags = BIND_SHADER_RESOURCE;
    Desc.Format    = TEX_FORMAT_RGBA8_UNORM;
    Desc.Width     = 1;
    Desc.Height    = 1;
    Desc.MipLevels = 1;

    TextureSubResData Subres{&Color, sizeof(Color)};
    TextureData       InitData{&Subres, 1};
    pDevice->CreateTexture(Desc, &InitData, &Texture);

    SRV = Texture ? Texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
    return SRV != nullptr;
}

bool CreateR32FloatTexture(IRenderDevice* pDevice, const char* Name, float Value,
                           RefCntAutoPtr<ITexture>& Texture, RefCntAutoPtr<ITextureView>& SRV)
{
    Texture.Release();
    SRV.Release();

    TextureDesc Desc;
    Desc.Name      = Name;
    Desc.Type      = RESOURCE_DIM_TEX_2D;
    Desc.Usage     = USAGE_IMMUTABLE;
    Desc.BindFlags = BIND_SHADER_RESOURCE;
    Desc.Format    = TEX_FORMAT_R32_FLOAT;
    Desc.Width     = 1;
    Desc.Height    = 1;
    Desc.MipLevels = 1;

    TextureSubResData Subres{&Value, sizeof(Value)};
    TextureData       InitData{&Subres, 1};
    pDevice->CreateTexture(Desc, &InitData, &Texture);

    SRV = Texture ? Texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
    return SRV != nullptr;
}

} // namespace

RTXPTEnvMapBaker::~RTXPTEnvMapBaker() = default;

void RTXPTEnvMapBaker::Reset()
{
    m_SourceTexture.Release();
    m_SourceSRV.Release();
    m_FallbackEnvironmentMap.Release();
    m_FallbackDiffuseIrradiance.Release();
    m_FallbackImportanceMap.Release();
    m_FallbackRadianceMap.Release();
    m_FallbackBRDFLUT.Release();
    m_EnvironmentMapSRV.Release();
    m_DiffuseIrradianceSRV.Release();
    m_ImportanceMapSRV.Release();
    m_RadianceMapSRV.Release();
    m_BRDFLUTSRV.Release();
    m_EnvironmentSampler.Release();
    m_ImportanceSampler.Release();
    m_IBLPrecompute.reset();
    m_Constants        = {};
    m_LightsBakerParams = {};
    m_LastSettings     = {};
    m_Stats            = {};
}

void RTXPTEnvMapBaker::SceneReloaded()
{
    m_LastSettings.SourceRelativePath.clear();
    m_Stats.SourceLoaded    = false;
    m_Stats.ImportanceReady = false;
    m_Stats.SourceName.clear();
    ++m_Stats.Version;
}

bool RTXPTEnvMapBaker::CreateResources(IRenderDevice* pDevice, IDeviceContext*, IEngineFactory*, bool)
{
    if (pDevice == nullptr)
    {
        m_Stats.Ready     = false;
        m_Stats.LastError = "RTXPT EnvMapBaker requires a render device";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    return CreateSamplers(pDevice) && CreateFallbackTextures(pDevice);
}

bool RTXPTEnvMapBaker::Update(IRenderDevice* pDevice, IDeviceContext* pContext, IEngineFactory* pEngineFactory,
                              const std::string& AssetsRoot, const RTXPTEnvMapSettings& Settings, bool ForceRebuild, bool ComputeSupported)
{
    if (pDevice == nullptr || pContext == nullptr || pEngineFactory == nullptr)
    {
        m_Stats.Ready     = false;
        m_Stats.LastError = "RTXPT EnvMapBaker update requires a render device, device context, and engine factory";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    if (!m_EnvironmentSampler || !m_ImportanceSampler || !m_FallbackEnvironmentMap || !m_FallbackDiffuseIrradiance ||
        !m_FallbackImportanceMap || !m_FallbackRadianceMap || !m_FallbackBRDFLUT)
    {
        if (!CreateResources(pDevice, pContext, pEngineFactory, ComputeSupported))
            return false;
    }

    const bool SourceChanged = ForceRebuild || !m_Stats.Ready || !m_SourceTexture || !m_SourceSRV ||
        !m_EnvironmentMapSRV || !m_DiffuseIrradianceSRV || !m_BRDFLUTSRV ||
        EnvMapSourceChanged(m_LastSettings, Settings);
    bool       UpdateOk     = true;
    if (SourceChanged)
    {
        UpdateOk = LoadSourceTexture(pDevice, AssetsRoot, Settings) &&
            PrecomputeCubemap(pDevice, pContext, Settings);
        if (UpdateOk)
        {
            m_LastSettings = Settings;
            ++m_Stats.Version;
            const bool KeepFallbackLoadError = !IsProceduralSkyPath(Settings.SourceRelativePath) &&
                m_Stats.Procedural && !m_Stats.LastError.empty();
            if (!KeepFallbackLoadError)
                m_Stats.LastError.clear();
        }
    }
    else
    {
        m_LastSettings = Settings;
    }

    m_Stats.CompressedOutput = false;
    m_Stats.Ready            = UpdateOk &&
        m_EnvironmentSampler && m_ImportanceSampler &&
        m_EnvironmentMapSRV && m_DiffuseIrradianceSRV && m_BRDFLUTSRV &&
        m_FallbackImportanceMap && m_FallbackRadianceMap &&
        m_ImportanceMapSRV && m_RadianceMapSRV;
    UpdateConstants(Settings);
    return m_Stats.Ready;
}

bool RTXPTEnvMapBaker::InfoGUI(float Indent)
{
    ImGui::Indent(Indent);
    ImGui::Text("Ready: %s", m_Stats.Ready ? "yes" : "no");
    ImGui::Text("Source: %s", m_Stats.SourceName.empty() ? "<none>" : m_Stats.SourceName.c_str());
    ImGui::Text("Cube: %u px, %u mip(s)", m_Stats.CubeResolution, m_Stats.CubeMipLevels);
    ImGui::Text("Importance: %s", m_Stats.ImportanceReady ? "ready" : "fallback");
    if (!m_Stats.LastError.empty())
        ImGui::TextWrapped("EnvMapBaker error: %s", m_Stats.LastError.c_str());
    ImGui::Unindent(Indent);
    return false;
}

bool RTXPTEnvMapBaker::DebugGUI(float Indent)
{
    ImGui::Indent(Indent);
    ImGui::Text("Version: %llu", static_cast<unsigned long long>(m_Stats.Version));
    ImGui::Text("Procedural: %s", m_Stats.Procedural ? "yes" : "no");
    ImGui::Text("BRDF LUT: %s", m_Stats.BRDFLUTReady ? "ready" : "fallback");
    ImGui::Text("Compressed output: %s", m_Stats.CompressedOutput ? "yes" : "no");
    ImGui::Unindent(Indent);
    return false;
}

std::vector<RTXPTEnvMapSource> RTXPTEnvMapBaker::EnumerateEnvironmentSources(const std::string& AssetsRoot)
{
    std::vector<RTXPTEnvMapSource> Sources;
    Sources.push_back(RTXPTEnvMapSource{RTXPTEnvMapSourceKind::ProceduralSky, "Procedural Sky", kProceduralSkyPath, ""});

    const std::filesystem::path EnvRoot = std::filesystem::path{AssetsRoot} / "EnvironmentMaps";
    std::error_code             Error;
    if (!std::filesystem::is_directory(EnvRoot, Error))
        return Sources;

    for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator{EnvRoot, Error})
    {
        if (Error || !Entry.is_regular_file() || !IsEnvironmentFile(Entry.path()))
            continue;

        RTXPTEnvMapSource Source;
        Source.Kind         = RTXPTEnvMapSourceKind::TextureFile;
        Source.DisplayName  = Entry.path().filename().string();
        Source.RelativePath = std::string{"EnvironmentMaps/"} + Source.DisplayName;
        Source.ResolvedPath = Entry.path().string();
        FileSystem::CorrectSlashes(Source.ResolvedPath);
        Sources.push_back(std::move(Source));
    }

    std::sort(Sources.begin() + 1, Sources.end(),
              [](const RTXPTEnvMapSource& Lhs, const RTXPTEnvMapSource& Rhs) { return Lhs.DisplayName < Rhs.DisplayName; });
    return Sources;
}

RTXPTEnvMapSettings RTXPTEnvMapBaker::MakeSceneDefaultSettings(const RTXPTSceneGraphData& SceneData)
{
    RTXPTEnvMapSettings Settings;
    Settings.SourceRelativePath = kProceduralSkyPath;
    Settings.Enabled            = true;

    for (const RTXPTSceneLightMetadata& Light : SceneData.Lights)
    {
        if (Light.Type != "EnvironmentLight")
            continue;

        Settings.SourceRelativePath = ReadRTXPTOptionalString(Light.RawJson, "path", kProceduralSkyPath);
        float Scale[3] = {1.0f, 1.0f, 1.0f};
        if (ReadRTXPTFloatArray(Light.RawJson, "radianceScale", Scale, 3))
            Settings.RadianceScale = float3{Scale[0], Scale[1], Scale[2]};

        float Rotation[1] = {0.0f};
        if (ReadRTXPTFloatArray(Light.RawJson, "rotation", Rotation, 1))
            Settings.RotationRadians = Rotation[0];

        return Settings;
    }

    return Settings;
}

bool RTXPTEnvMapBaker::LoadSourceTexture(IRenderDevice* pDevice, const std::string& AssetsRoot, const RTXPTEnvMapSettings& Settings)
{
    if (pDevice == nullptr)
    {
        m_Stats.Ready     = false;
        m_Stats.LastError = "RTXPT EnvMapBaker source loading requires a render device";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    m_SourceTexture.Release();
    m_SourceSRV.Release();
    m_Stats.SourceLoaded = false;
    m_Stats.Procedural   = IsProceduralSkyPath(Settings.SourceRelativePath);
    m_Stats.SourceName   = m_Stats.Procedural ? kProceduralSkyPath : Settings.SourceRelativePath;

    if (IsProceduralSkyPath(Settings.SourceRelativePath))
    {
        const bool Loaded = CreateProceduralSourceTexture(pDevice, Settings);
        if (Loaded)
            m_Stats.LastError.clear();
        return Loaded;
    }

    std::string ResolvedPath = (std::filesystem::path{AssetsRoot} / Settings.SourceRelativePath).string();
    FileSystem::CorrectSlashes(ResolvedPath);
    ResolvedPath = FileSystem::SimplifyPath(ResolvedPath.c_str());

    CreateTextureFromFile(ResolvedPath.c_str(), TextureLoadInfo{"RTXPT environment source"}, pDevice, &m_SourceTexture);
    m_SourceSRV = m_SourceTexture ? m_SourceTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
    if (m_SourceTexture && m_SourceSRV)
    {
        m_Stats.SourceLoaded = true;
        m_Stats.Procedural   = false;
        m_Stats.SourceName   = Settings.SourceRelativePath;
        m_Stats.LastError.clear();
        return true;
    }

    const std::string LoadError = "Failed to load RTXPT environment source: " + ResolvedPath;
    m_Stats.LastError           = LoadError;
    LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
    m_Stats.Procedural = true;
    m_Stats.SourceName = kProceduralSkyPath;

    if (!CreateProceduralSourceTexture(pDevice, Settings))
    {
        if (!m_Stats.LastError.empty())
            m_Stats.LastError = LoadError + " | " + m_Stats.LastError;
        else
            m_Stats.LastError = LoadError;
        return false;
    }

    m_Stats.LastError = LoadError;
    return true;
}

bool RTXPTEnvMapBaker::CreateProceduralSourceTexture(IRenderDevice* pDevice, const RTXPTEnvMapSettings&)
{
    if (pDevice == nullptr)
    {
        m_Stats.Ready     = false;
        m_Stats.LastError = "RTXPT EnvMapBaker procedural source creation requires a render device";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    m_SourceTexture.Release();
    m_SourceSRV.Release();
    m_Stats.SourceLoaded = false;
    constexpr Uint32   Width = 512, Height = 256;
    const float3       HorizonColor{0.48f, 0.58f, 0.68f};
    const float3       ZenithColor{0.05f, 0.08f, 0.14f};
    std::vector<float4> Texels(static_cast<size_t>(Width) * Height);

    for (Uint32 Y = 0; Y < Height; ++Y)
    {
        const float T = static_cast<float>(Y) / static_cast<float>(Height - 1);
        const float4 RowColor{
            HorizonColor.x + (ZenithColor.x - HorizonColor.x) * T,
            HorizonColor.y + (ZenithColor.y - HorizonColor.y) * T,
            HorizonColor.z + (ZenithColor.z - HorizonColor.z) * T,
            1.0f};
        std::fill_n(Texels.data() + static_cast<size_t>(Y) * Width, Width, RowColor);
    }
    const TextureDesc Desc{"RTXPT EnvMapBaker procedural source", RESOURCE_DIM_TEX_2D, Width, Height, 1, TEX_FORMAT_RGBA32_FLOAT,
                           1, 1, USAGE_IMMUTABLE, BIND_SHADER_RESOURCE};
    TextureSubResData SubRes{Texels.data(), Uint64{Width} * sizeof(float4)};
    TextureData       InitData{&SubRes, 1};
    pDevice->CreateTexture(Desc, &InitData, &m_SourceTexture);
    m_SourceSRV = m_SourceTexture ? m_SourceTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
    if (!m_SourceTexture || !m_SourceSRV)
    {
        m_Stats.Ready     = false;
        m_Stats.LastError = "Failed to create RTXPT EnvMapBaker procedural source texture";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        m_SourceTexture.Release();
        m_SourceSRV.Release();
        return false;
    }

    m_Stats.SourceLoaded   = true;
    m_Stats.Procedural     = true;
    m_Stats.SourceName     = kProceduralSkyPath;
    m_Stats.LastError.clear();
    return true;
}

bool RTXPTEnvMapBaker::PrecomputeCubemap(IRenderDevice* pDevice, IDeviceContext* pContext, const RTXPTEnvMapSettings&)
{
    if (pDevice == nullptr || pContext == nullptr || m_SourceSRV == nullptr)
    {
        m_Stats.Ready     = false;
        m_Stats.LastError = "RTXPT EnvMapBaker precompute requires a render device, device context, and source texture";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    if (!m_IBLPrecompute)
        m_IBLPrecompute = std::make_unique<PBR_Renderer>(pDevice, nullptr, pContext, PBR_Renderer::CreateInfo{});

    m_IBLPrecompute->PrecomputeCubemaps(pContext, m_SourceSRV);
    m_EnvironmentMapSRV    = m_IBLPrecompute->GetPrefilteredEnvMapSRV();
    m_DiffuseIrradianceSRV = m_IBLPrecompute->GetIrradianceCubeSRV();
    m_BRDFLUTSRV           = m_IBLPrecompute->GetPreintegratedGGX_SRV();

    if (!m_EnvironmentMapSRV || !m_DiffuseIrradianceSRV || !m_BRDFLUTSRV)
    {
        m_Stats.Ready     = false;
        m_Stats.LastError = "RTXPT EnvMapBaker precompute did not produce all IBL textures";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    const ITexture* pEnvTexture = m_EnvironmentMapSRV->GetTexture();
    if (pEnvTexture == nullptr)
    {
        m_Stats.Ready     = false;
        m_Stats.LastError = "RTXPT EnvMapBaker precompute produced an invalid environment texture";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    const TextureDesc& EnvDesc = pEnvTexture->GetDesc();
    m_Stats.CubeResolution     = EnvDesc.Width;
    m_Stats.CubeMipLevels      = EnvDesc.MipLevels;
    m_Stats.BRDFLUTReady       = true;
    m_Stats.ImportanceReady    = false;
    m_Stats.CompressedOutput   = false;
    return true;
}

bool RTXPTEnvMapBaker::CreateImportanceMaps(IRenderDevice*, IDeviceContext*, IEngineFactory*,
                                            const RTXPTEnvMapSettings&, bool)
{
    m_Stats.ImportanceReady = false;
    return false;
}

bool RTXPTEnvMapBaker::CreateFallbackTextures(IRenderDevice* pDevice)
{
    if (pDevice == nullptr)
    {
        m_Stats.Ready     = false;
        m_Stats.LastError = "RTXPT EnvMapBaker fallback texture creation requires a render device";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    constexpr Uint32 BlackRGBA = 0xff000000u;
    constexpr Uint32 WhiteRGBA = 0xffffffffu;

    m_SourceTexture.Release();
    m_SourceSRV.Release();

    const bool EnvOk = CreateCubeTexture(pDevice, "RTXPT EnvMapBaker fallback environment cube",
                                         kFallbackCubeSize, BlackRGBA, m_FallbackEnvironmentMap, m_EnvironmentMapSRV);
    const bool DiffuseOk = CreateCubeTexture(pDevice, "RTXPT EnvMapBaker fallback diffuse irradiance cube",
                                             kFallbackCubeSize, BlackRGBA, m_FallbackDiffuseIrradiance, m_DiffuseIrradianceSRV);
    const bool ImportanceOk = CreateR32FloatTexture(pDevice, "RTXPT EnvMapBaker fallback importance map",
                                                    1.0f, m_FallbackImportanceMap, m_ImportanceMapSRV);
    const bool RadianceOk = CreateRGBA8Texture(pDevice, "RTXPT EnvMapBaker fallback radiance map",
                                               BlackRGBA, m_FallbackRadianceMap, m_RadianceMapSRV);
    const bool BRDFOk = CreateRGBA8Texture(pDevice, "RTXPT EnvMapBaker fallback BRDF LUT",
                                            WhiteRGBA, m_FallbackBRDFLUT, m_BRDFLUTSRV);

    m_SourceSRV                 = m_EnvironmentMapSRV;
    m_Stats.Ready = m_EnvironmentSampler && m_ImportanceSampler &&
        EnvOk && DiffuseOk && ImportanceOk && RadianceOk && BRDFOk;
    m_Stats.SourceLoaded         = false;
    m_Stats.Procedural           = true;
    m_Stats.ImportanceReady      = false;
    m_Stats.BRDFLUTReady         = false;
    m_Stats.CompressedOutput     = false;
    m_Stats.CubeResolution       = kFallbackCubeSize;
    m_Stats.CubeMipLevels        = 1;
    m_Stats.ImportanceResolution = 1;
    m_Stats.ImportanceMipLevels  = 1;
    m_Stats.SourceName           = kProceduralSkyPath;

    if (!m_Stats.Ready)
    {
        m_Stats.LastError = "Failed to create RTXPT EnvMapBaker fallback textures";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    m_Stats.LastError.clear();
    UpdateConstants(m_LastSettings);
    return true;
}

bool RTXPTEnvMapBaker::CreateSamplers(IRenderDevice* pDevice)
{
    if (pDevice == nullptr)
    {
        m_Stats.LastError = "RTXPT EnvMapBaker sampler creation requires a render device";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    m_EnvironmentSampler.Release();
    m_ImportanceSampler.Release();

    SamplerDesc LinearWrap;
    LinearWrap.Name      = "RTXPT environment map sampler";
    LinearWrap.MinFilter = FILTER_TYPE_LINEAR;
    LinearWrap.MagFilter = FILTER_TYPE_LINEAR;
    LinearWrap.MipFilter = FILTER_TYPE_LINEAR;
    LinearWrap.AddressU  = TEXTURE_ADDRESS_WRAP;
    LinearWrap.AddressV  = TEXTURE_ADDRESS_WRAP;
    LinearWrap.AddressW  = TEXTURE_ADDRESS_WRAP;
    pDevice->CreateSampler(LinearWrap, &m_EnvironmentSampler);

    SamplerDesc PointClamp;
    PointClamp.Name      = "RTXPT environment importance sampler";
    PointClamp.MinFilter = FILTER_TYPE_POINT;
    PointClamp.MagFilter = FILTER_TYPE_POINT;
    PointClamp.MipFilter = FILTER_TYPE_POINT;
    PointClamp.AddressU  = TEXTURE_ADDRESS_CLAMP;
    PointClamp.AddressV  = TEXTURE_ADDRESS_CLAMP;
    PointClamp.AddressW  = TEXTURE_ADDRESS_CLAMP;
    pDevice->CreateSampler(PointClamp, &m_ImportanceSampler);

    if (!m_EnvironmentSampler || !m_ImportanceSampler)
    {
        m_Stats.LastError = "Failed to create RTXPT EnvMapBaker samplers";
        LOG_ERROR_MESSAGE(m_Stats.LastError.c_str());
        return false;
    }

    return true;
}

void RTXPTEnvMapBaker::UpdateConstants(const RTXPTEnvMapSettings& Settings)
{
    const float Rotation = Settings.RotationRadians;
    const float CosA     = std::cos(Rotation);
    const float SinA     = std::sin(Rotation);
    const float Enabled  = Settings.Enabled ? 1.0f : 0.0f;
    const float Intensity = std::max(Settings.Intensity, 0.0f);
    const float InvImportanceDim = m_Stats.ImportanceResolution > 0 ?
        1.0f / static_cast<float>(m_Stats.ImportanceResolution) :
        0.0f;

    m_Constants.LocalToWorld0 = float4{CosA, 0.0f, -SinA, 0.0f};
    m_Constants.LocalToWorld1 = float4{0.0f, 1.0f, 0.0f, 0.0f};
    m_Constants.LocalToWorld2 = float4{SinA, 0.0f, CosA, 0.0f};
    m_Constants.WorldToLocal0 = float4{CosA, 0.0f, SinA, 0.0f};
    m_Constants.WorldToLocal1 = float4{0.0f, 1.0f, 0.0f, 0.0f};
    m_Constants.WorldToLocal2 = float4{-SinA, 0.0f, CosA, 0.0f};
    m_Constants.ColorEnabled  = float4{
        Settings.RadianceScale.x * Intensity,
        Settings.RadianceScale.y * Intensity,
        Settings.RadianceScale.z * Intensity,
        Enabled};
    m_Constants.ImportanceMetadata = float4{InvImportanceDim, InvImportanceDim, 0.0f, m_Stats.ImportanceReady ? 1.0f : 0.0f};

    m_LightsBakerParams.Transform.Row0    = m_Constants.LocalToWorld0;
    m_LightsBakerParams.Transform.Row1    = m_Constants.LocalToWorld1;
    m_LightsBakerParams.Transform.Row2    = m_Constants.LocalToWorld2;
    m_LightsBakerParams.InvTransform.Row0 = m_Constants.WorldToLocal0;
    m_LightsBakerParams.InvTransform.Row1 = m_Constants.WorldToLocal1;
    m_LightsBakerParams.InvTransform.Row2 = m_Constants.WorldToLocal2;
    m_LightsBakerParams.ColorMultiplier   = float3{
        m_Constants.ColorEnabled.x,
        m_Constants.ColorEnabled.y,
        m_Constants.ColorEnabled.z};
    m_LightsBakerParams.Enabled = Enabled;
}

} // namespace Diligent
