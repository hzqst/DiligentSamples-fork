#ifndef RTXPT_SHADER_SHARED_HLSLI
#define RTXPT_SHADER_SHARED_HLSLI

// Mirrors RTXPTFrameConstants in RTXPTSample.hpp.
struct RTXPTFrameConstants
{
    float4x4 ViewProj;
    float4x4 ViewProjInv;
    float4   CameraPosition_Time;
    float4   ViewportSize_FrameIdx;
};

// Primary ray payload shared by raygen/miss/chit.
struct RTXPTPrimaryPayload
{
    float4 ColorDepth;
};

// Mirrors RTXPTSubInstanceData in RTXPTAccelerationStructures.hpp.
// One entry per (BLAS instance, geometry) pair. C++ stores the per-instance
// sub-instance base in TLAS CustomId, exposed to closest-hit shaders as InstanceID().
// index = InstanceID() + GeometryIndex().
struct RTXPTSubInstanceData
{
    uint MaterialID;
    uint Flags; // Reserved for Phase 5.3 alpha mode/any-hit specialization.
    uint Padding0;
    uint Padding1;
};

// Mirrors Diligent::GLTF::Material::ShaderAttribs from DiligentTools/AssetLoader/interface/GLTFLoader.hpp.
// Keep field order/sizes synchronized; total size is 96 bytes (16-byte aligned).
struct RTXPTMaterialAttribs
{
    float4 BaseColorFactor; // offset 0

    float3 EmissiveFactor; // offset 16
    float  NormalScale;

    float3 SpecularFactor; // offset 32
    float  ClearcoatNormalScale;

    int   Workflow; // offset 48
    int   AlphaMode;
    float AlphaCutoff;
    float MetallicFactor;

    float RoughnessFactor; // offset 64
    float OcclusionFactor;
    float ClearcoatFactor;
    float ClearcoatRoughnessFactor;

    float4 CustomData; // offset 80
};

// Mirrors Diligent::RTXPTLightData in RTXPTLights.hpp.
struct RTXPTLightData
{
    float4 ColorIntensity;
    float4 PositionRange;
    float4 DirectionType;
    float4 SpotAngles;
};

#endif // RTXPT_SHADER_SHARED_HLSLI
