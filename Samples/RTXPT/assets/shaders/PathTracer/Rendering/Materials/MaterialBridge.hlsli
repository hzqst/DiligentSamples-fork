#ifndef __MATERIAL_BRIDGE_HLSLI__
#define __MATERIAL_BRIDGE_HLSLI__

#include "../../Config.h"
#include "../../PathTracerShared.h"
#include "../../Scene/Material/HomogeneousVolumeData.hlsli"

StructuredBuffer<MaterialPTData> t_PTMaterialData;

#ifdef ENABLE_MATERIAL_TEXTURES
// One Texture2DArray per loaded GLTF texture (the Diligent loader creates RESOURCE_DIM_TEX_2D_ARRAY textures).
// MATERIAL_TEXTURE_COUNT is supplied at compile time and equals RTXPTMaterials::GetTextureCount().
Texture2DArray t_BindlessTextures[MATERIAL_TEXTURE_COUNT];
SamplerState   s_MaterialSampler;
#endif

namespace Bridge
{
    bool hasMaterialTable()
    {
        uint count  = 0;
        uint stride = 0;
        t_PTMaterialData.GetDimensions(count, stride);
        return count > 0;
    }

    uint getMaterialCount()
    {
        uint count  = 0;
        uint stride = 0;
        t_PTMaterialData.GetDimensions(count, stride);
        return count;
    }

    // Out-of-range indices clamp to the last material so a bad materialID never UB-reads.
    MaterialPTData getMaterial(uint materialID)
    {
        const uint lastIndex = max(getMaterialCount(), 1u) - 1u;
        const uint index     = min(materialID, lastIndex);
        return t_PTMaterialData[index];
    }

#ifdef ENABLE_MATERIAL_TEXTURES
    // Ray tracing shaders cannot derive LOD, so we sample the most detailed level. Slice is 0 for the
    // non-atlas bistro load; it is carried so an atlas path can be added later without touching call sites.
    float4 sampleMaterialTexture(uint textureIndex, float slice, float2 uv)
    {
        return t_BindlessTextures[NonUniformResourceIndex(textureIndex)].SampleLevel(s_MaterialSampler, float3(uv, slice), 0.0);
    }

    float4 getBaseColor(MaterialPTData material, float2 uv)
    {
        float4 color = material.baseColorFactor;
        if ((material.flags & kMaterialFlagHasBaseColorTexture) != 0u)
            color *= sampleMaterialTexture(material.baseColorTextureIndex, material.baseColorTextureSlice, uv);
        return color;
    }

    float3 getEmission(MaterialPTData material, float2 uv)
    {
        float3 emission = material.emissiveFactor;
        if ((material.flags & kMaterialFlagHasEmissiveTexture) != 0u)
            emission *= sampleMaterialTexture(material.emissiveTextureIndex, material.emissiveTextureSlice, uv).rgb;
        return emission;
    }

    // True when the hit passes the alpha test (or is not alpha tested).
    bool alphaTestPasses(MaterialPTData material, float2 uv)
    {
        if ((material.flags & kMaterialFlagAlphaTested) == 0u)
            return true;
        return getBaseColor(material, uv).a >= material.alphaCutoff;
    }

    // glTF metallic-roughness packing: roughness in .g, metallic in .b, each scaled by the material factor.
    float2 getMetallicRoughness(MaterialPTData material, float2 uv)
    {
        float metallic  = material.metallicFactor;
        float roughness = material.roughnessFactor;
        if ((material.flags & kMaterialFlagHasMetallicRoughnessTexture) != 0u)
        {
            const float4 mr = sampleMaterialTexture(material.metallicRoughnessTextureIndex, material.metallicRoughnessTextureSlice, uv);
            roughness *= mr.g;
            metallic  *= mr.b;
        }
        return float2(metallic, roughness);
    }

    // Tangent-space normal unpacked to [-1, 1] with NormalScale applied to xy. Returns (0,0,1) when there is no
    // normal map, which the caller treats as "no perturbation".
    float3 getTangentNormal(MaterialPTData material, float2 uv)
    {
        if ((material.flags & kMaterialFlagHasNormalTexture) == 0u)
            return float3(0.0, 0.0, 1.0);

        float3 n = sampleMaterialTexture(material.normalTextureIndex, material.normalTextureSlice, uv).xyz * 2.0 - 1.0;
        n.xy *= material.normalScale;
        return normalize(n);
    }

    float getTransmission(MaterialPTData material, float2 uv)
    {
        float transmission = material.transmissionFactor;
        if ((material.flags & kMaterialFlagHasTransmissionTexture) != 0u)
            transmission *= sampleMaterialTexture(material.transmissionTextureIndex, material.transmissionTextureSlice, uv).r;
        return saturate(transmission);
    }

    float getDiffuseTransmission(MaterialPTData material, float2 uv)
    {
        float transmission = material.diffuseTransmissionFactor;
        if ((material.flags & kMaterialFlagHasTransmissionTexture) != 0u)
            transmission *= sampleMaterialTexture(material.transmissionTextureIndex, material.transmissionTextureSlice, uv).r;
        return saturate(transmission);
    }

    float getThickness(MaterialPTData material, float2 uv)
    {
        float thickness = material.thicknessFactor;
        if ((material.flags & kMaterialFlagHasThicknessTexture) != 0u)
            thickness *= sampleMaterialTexture(material.thicknessTextureIndex, material.thicknessTextureSlice, uv).g;
        return max(thickness, 0.0);
    }
#else
    // Factor-only fallback (bindless material textures unavailable): no texture sampling, never alpha tested.
    float4 getBaseColor(MaterialPTData material, float2 uv) { return material.baseColorFactor; }
    float3 getEmission(MaterialPTData material, float2 uv) { return material.emissiveFactor; }
    bool   alphaTestPasses(MaterialPTData material, float2 uv) { return true; }
    float2 getMetallicRoughness(MaterialPTData material, float2 uv) { return float2(material.metallicFactor, material.roughnessFactor); }
    float3 getTangentNormal(MaterialPTData material, float2 uv) { return float3(0.0, 0.0, 1.0); }
    float  getTransmission(MaterialPTData material, float2 uv) { return saturate(material.transmissionFactor); }
    float  getDiffuseTransmission(MaterialPTData material, float2 uv) { return saturate(material.diffuseTransmissionFactor); }
    float  getThickness(MaterialPTData material, float2 uv) { return max(material.thicknessFactor, 0.0); }
#endif

    float loadIoR(uint materialID)
    {
        const uint count = getMaterialCount();
        if (materialID >= count)
            return 1.0;

        MaterialPTData material = getMaterial(materialID);
        return max(material.ior, 1.0);
    }

    HomogeneousVolumeData loadHomogeneousVolumeData(uint materialID)
    {
        HomogeneousVolumeData volume;
        volume.sigmaS = float3(0.0, 0.0, 0.0);
        volume.sigmaA = float3(0.0, 0.0, 0.0);
        volume.g      = 0.0;

        const uint count = getMaterialCount();
        if (materialID >= count)
            return volume;

        MaterialPTData material = getMaterial(materialID);
        if ((material.flags & kMaterialFlagHasVolume) == 0u)
            return volume;

        const float3 attenuationColor = clamp(material.volumeAttenuationColor, 1e-7, 1.0);
        const float  attenuationDistance = max(material.volumeAttenuationDistance, 1e-30);
        volume.sigmaA = -log(attenuationColor) / attenuationDistance.xxx;
        return volume;
    }

    bool isThinSurface(MaterialPTData material)
    {
        return (material.flags & kMaterialFlagThinSurface) != 0u || (material.flags & kMaterialFlagHasTransmission) == 0u;
    }
} // namespace Bridge

// TODO(RTXPT-Port Phase 5.3): Honor TextureShaderAttribs UV selectors / wrap modes / atlas transform (currently assumes TEXCOORD_0 + wrap + slice).

#endif // __MATERIAL_BRIDGE_HLSLI__
