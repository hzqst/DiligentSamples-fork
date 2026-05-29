#ifndef RTXPT_SHADER_SHARED_HLSLI
#define RTXPT_SHADER_SHARED_HLSLI

// Mirrors Diligent::kRTXPTSubInstanceFlag_Indexed in RTXPTAccelerationStructures.hpp.
static const uint kRTXPTSubInstanceFlagIndexed = 0x1u;

// Mirrors Diligent::RTXPTPathTracerSettings (the new sub-struct embedded in RTXPTFrameConstants).
struct RTXPTPathTracerSettings
{
    uint MaxBounces;        // Maximum number of secondary bounces; 0 means primary-ray only.
    uint AccumulationFrame; // 0-based index of the sample being added this frame.
    uint ResetAccumulation; // Non-zero means raygen should overwrite the accumulation buffer instead of blending.
    uint MinBounces;        // Reserved for Phase 5.3 Russian roulette; ignored by Phase 5.2.
};

// Mirrors Diligent::RTXPTFrameConstants in RTXPTSample.hpp (must keep order and layout in sync).
struct RTXPTFrameConstants
{
    float4x4                ViewProj;
    float4x4                ViewProjInv;
    float4                  CameraPosition_Time;
    float4                  ViewportSize_FrameIdx;
    RTXPTPathTracerSettings PathTracer;
};

// Primary ray payload (Phase 5.1 compatibility - kept for the bridge sanity helpers).
struct RTXPTPrimaryPayload
{
    float4 ColorDepth;
};

// Reference path tracer payload (Phase 5.2).
//   HitFlag    : 1 on closest hit, 0 on miss.
//   HitDistance: RayTCurrent() on hit; <= 0 on miss.
//   WorldPos   : world-space hit position.
//   WorldNormal: world-space shading normal (interpolated and renormalized).
//   BaseColor  : material base color RGB (sampled via the material bridge).
//   Emission   : RGB emission written by miss/emissive paths and accumulated by raygen.
struct RTXPTPathTracerPayload
{
    float3 WorldPos;
    float  HitDistance;

    float3 WorldNormal;
    uint   HitFlag;

    float3 BaseColor;
    float  Padding0;

    float3 Emission;
    float  Padding1;
};

// Mirrors Diligent::RTXPTSubInstanceData in RTXPTAccelerationStructures.hpp.
// One entry per (BLAS instance, geometry) pair. The C++ side stores the per-instance
// sub-instance base in TLAS CustomId, exposed in closest-hit shaders as InstanceID().
// index = InstanceID() + GeometryIndex().
struct RTXPTSubInstanceData
{
    uint MaterialID;
    uint Flags;
    uint FirstIndex;
    uint IndexCount;
    uint FirstVertex;
    uint VertexCount;
    uint Padding0;
    uint Padding1;
};

// Mirrors Diligent::RTXPTMaterialData in RTXPTMaterials.hpp (must keep order/size in sync; total size 64 bytes).
struct RTXPTMaterialData
{
    float4 BaseColorFactor; // offset 0

    float3 EmissiveFactor; // offset 16
    float  AlphaCutoff;    // offset 28

    uint  Flags;                 // offset 32
    uint  BaseColorTextureIndex; // offset 36
    uint  EmissiveTextureIndex;  // offset 40
    float MetallicFactor;        // offset 44

    float RoughnessFactor;       // offset 48
    float BaseColorTextureSlice; // offset 52
    float EmissiveTextureSlice;  // offset 56
    float Padding0;              // offset 60
};

// Mirrors the kRTXPTMaterialFlag_* constants in RTXPTMaterials.hpp.
static const uint kRTXPTMaterialFlagHasBaseColorTexture = 0x1u;
static const uint kRTXPTMaterialFlagAlphaTested         = 0x2u;
static const uint kRTXPTMaterialFlagHasEmissiveTexture  = 0x4u;

// Mirrors Diligent::RTXPTLightData in RTXPTLights.hpp.
struct RTXPTLightData
{
    float4 ColorIntensity;
    float4 PositionRange;
    float4 DirectionType;
    float4 SpotAngles;
};

// Per-vertex layout for vertex buffer 0 of the default Diligent GLTF model
// (POSITION + NORMAL + TEXCOORD_0). Total size = 32 bytes; must equal the
// vertex stride captured by RTXPTScene::GetVertexStride0().
struct RTXPTVertex
{
    float3 Position;
    float3 Normal;
    float2 TexCoord0;
};

#endif // RTXPT_SHADER_SHARED_HLSLI
