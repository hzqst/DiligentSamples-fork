#ifndef RTXPT_MATERIAL_BRIDGE_HLSLI
#define RTXPT_MATERIAL_BRIDGE_HLSLI

#include "RTXPTShaderShared.hlsli"

StructuredBuffer<RTXPTMaterialData> g_Materials;

#ifdef RTXPT_ENABLE_MATERIAL_TEXTURES
// One Texture2DArray per loaded GLTF texture (the Diligent loader creates RESOURCE_DIM_TEX_2D_ARRAY textures).
// RTXPT_MATERIAL_TEXTURE_COUNT is supplied at compile time and equals RTXPTMaterials::GetTextureCount().
Texture2DArray g_MaterialTextures[RTXPT_MATERIAL_TEXTURE_COUNT];
SamplerState   g_MaterialSampler;
#endif

namespace Bridge
{
    bool HasMaterialTable()
    {
        uint Count  = 0;
        uint Stride = 0;
        g_Materials.GetDimensions(Count, Stride);
        return Count > 0;
    }

    uint GetMaterialCount()
    {
        uint Count  = 0;
        uint Stride = 0;
        g_Materials.GetDimensions(Count, Stride);
        return Count;
    }

    // Out-of-range indices clamp to the last material so a bad MaterialID never UB-reads.
    RTXPTMaterialData GetMaterial(uint MaterialID)
    {
        const uint LastIndex = max(GetMaterialCount(), 1u) - 1u;
        const uint Index     = min(MaterialID, LastIndex);
        return g_Materials[Index];
    }

#ifdef RTXPT_ENABLE_MATERIAL_TEXTURES
    // Ray tracing shaders cannot derive LOD, so we sample the most detailed level. Slice is 0 for the
    // non-atlas bistro load; it is carried so an atlas path can be added later without touching call sites.
    float4 SampleMaterialTexture(uint TextureIndex, float Slice, float2 UV)
    {
        return g_MaterialTextures[NonUniformResourceIndex(TextureIndex)].SampleLevel(g_MaterialSampler, float3(UV, Slice), 0.0);
    }

    float4 GetBaseColor(RTXPTMaterialData Material, float2 UV)
    {
        float4 Color = Material.BaseColorFactor;
        if ((Material.Flags & kRTXPTMaterialFlagHasBaseColorTexture) != 0u)
            Color *= SampleMaterialTexture(Material.BaseColorTextureIndex, Material.BaseColorTextureSlice, UV);
        return Color;
    }

    float3 GetEmission(RTXPTMaterialData Material, float2 UV)
    {
        float3 Emission = Material.EmissiveFactor;
        if ((Material.Flags & kRTXPTMaterialFlagHasEmissiveTexture) != 0u)
            Emission *= SampleMaterialTexture(Material.EmissiveTextureIndex, Material.EmissiveTextureSlice, UV).rgb;
        return Emission;
    }

    // True when the hit passes the alpha test (or is not alpha tested).
    bool AlphaTestPasses(RTXPTMaterialData Material, float2 UV)
    {
        if ((Material.Flags & kRTXPTMaterialFlagAlphaTested) == 0u)
            return true;
        return GetBaseColor(Material, UV).a >= Material.AlphaCutoff;
    }
#else
    // Factor-only fallback (bindless material textures unavailable): no texture sampling, never alpha tested.
    float4 GetBaseColor(RTXPTMaterialData Material, float2 UV) { return Material.BaseColorFactor; }
    float3 GetEmission(RTXPTMaterialData Material, float2 UV) { return Material.EmissiveFactor; }
    bool   AlphaTestPasses(RTXPTMaterialData Material, float2 UV) { return true; }
#endif
} // namespace Bridge

// TODO(RTXPT-Port Phase 5.3): Shade with the metallic-roughness GGX BSDF and normal maps instead of textured Lambertian.
// TODO(RTXPT-Port Phase 5.3): Honor TextureShaderAttribs UV selectors / wrap modes / atlas transform (currently assumes TEXCOORD_0 + wrap + slice).

#endif // RTXPT_MATERIAL_BRIDGE_HLSLI
