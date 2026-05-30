#ifndef __MATERIAL_BRIDGE_HLSLI__
#define __MATERIAL_BRIDGE_HLSLI__

#include "PathTracerShared.h"

StructuredBuffer<MaterialPTData> g_Materials;

#ifdef RTXPT_ENABLE_MATERIAL_TEXTURES
// One Texture2DArray per loaded GLTF texture (the Diligent loader creates RESOURCE_DIM_TEX_2D_ARRAY textures).
// RTXPT_MATERIAL_TEXTURE_COUNT is supplied at compile time and equals RTXPTMaterials::GetTextureCount().
Texture2DArray g_MaterialTextures[RTXPT_MATERIAL_TEXTURE_COUNT];
SamplerState   g_MaterialSampler;
#endif

namespace Bridge
{
    bool hasMaterialTable()
    {
        uint count  = 0;
        uint stride = 0;
        g_Materials.GetDimensions(count, stride);
        return count > 0;
    }

    uint getMaterialCount()
    {
        uint count  = 0;
        uint stride = 0;
        g_Materials.GetDimensions(count, stride);
        return count;
    }

    // Out-of-range indices clamp to the last material so a bad materialID never UB-reads.
    MaterialPTData getMaterial(uint materialID)
    {
        const uint lastIndex = max(getMaterialCount(), 1u) - 1u;
        const uint index     = min(materialID, lastIndex);
        return g_Materials[index];
    }

#ifdef RTXPT_ENABLE_MATERIAL_TEXTURES
    // Ray tracing shaders cannot derive LOD, so we sample the most detailed level. Slice is 0 for the
    // non-atlas bistro load; it is carried so an atlas path can be added later without touching call sites.
    float4 sampleMaterialTexture(uint textureIndex, float slice, float2 uv)
    {
        return g_MaterialTextures[NonUniformResourceIndex(textureIndex)].SampleLevel(g_MaterialSampler, float3(uv, slice), 0.0);
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
#else
    // Factor-only fallback (bindless material textures unavailable): no texture sampling, never alpha tested.
    float4 getBaseColor(MaterialPTData material, float2 uv) { return material.baseColorFactor; }
    float3 getEmission(MaterialPTData material, float2 uv) { return material.emissiveFactor; }
    bool   alphaTestPasses(MaterialPTData material, float2 uv) { return true; }
    float2 getMetallicRoughness(MaterialPTData material, float2 uv) { return float2(material.metallicFactor, material.roughnessFactor); }
    float3 getTangentNormal(MaterialPTData material, float2 uv) { return float3(0.0, 0.0, 1.0); }
#endif
} // namespace Bridge

// TODO(RTXPT-Port Phase 5.3): Honor TextureShaderAttribs UV selectors / wrap modes / atlas transform (currently assumes TEXCOORD_0 + wrap + slice).

#endif // __MATERIAL_BRIDGE_HLSLI__
