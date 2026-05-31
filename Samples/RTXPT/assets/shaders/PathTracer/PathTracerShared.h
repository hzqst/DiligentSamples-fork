#ifndef __PATH_TRACER_SHARED_H__
#define __PATH_TRACER_SHARED_H__

// Mirrors Diligent::kSubInstanceFlag_Indexed in RTXPTAccelerationStructures.hpp.
static const uint kSubInstanceFlagIndexed = 0x1u;
static const uint kSubInstanceFlagSkinned = 0x2u;

// Maximum area-light solid-angle pdf (G4). Mirrors RTXPT-fork PolymorphicLight.hlsli:25 MAX_SOLID_ANGLE_PDF;
// clamps the area->solid-angle conversion for near-grazing / very close emissive-triangle samples.
static const float kMaxSolidAnglePdf = 1e10;

// Mirrors Diligent::PathTracerConstants (the sub-struct embedded in SampleConstants; total size 64 bytes).
struct PathTracerConstants
{
    uint bounceCount;       // Maximum number of secondary bounces; 0 means primary-ray only.
    uint sampleIndex;       // 0-based index of the sample being added this frame.
    uint resetAccumulation; // Non-zero means raygen should overwrite the accumulation buffer instead of blending.
    uint minBounceCount;    // Russian-roulette start bounce.

    uint  NEEEnabled;            // Non-zero enables next-event estimation (direct light sampling) at each hit.
    uint  environmentNEEEnabled; // bit 0: environment NEE enabled; bits 1..31: emissive triangle count (G4).
    float environmentIntensity;  // Scales the procedural-sky environment radiance.
    float lightIntensityScale;   // Scales analytic (punctual) light radiance.

    uint  maxNEEBounceCount;   // Limits NEE work to the first N path bounces.
    uint  analyticLightCount;  // CPU-side count of valid analytic lights; the dummy light is not sampled.
    uint  NEEType;             // G5: 0=Uniform, 1=Power+, 2=NEE-AT.
    uint  NEECandidateSamples; // G5: RIS candidate count per full sample.

    uint  NEEFullSamples;         // G5: visibility-tested full samples.
    uint  NEEMISType;             // G5 UI parity: 0=Full; approximate modes remain disabled in this plan.
    float fireflyFilterThreshold; // G1 adaptive firefly filter: soft-cap level; 0 disables the filter entirely.
    float exposureScale;          // Scene camera exposure multiplier applied before the in-raygen ACES curve.
};

struct RTXPTEnvMapConstants
{
    float4 LocalToWorld0;
    float4 LocalToWorld1;
    float4 LocalToWorld2;
    float4 WorldToLocal0;
    float4 WorldToLocal1;
    float4 WorldToLocal2;
    float4 ColorEnabled;
    float4 ImportanceMetadata;
};

// Mirrors Diligent::SampleConstants in RTXPTFrameConstants.hpp (must keep order and layout in sync; total size 352 bytes).
struct SampleConstants
{
    float4x4            viewProj;
    float4x4            viewProjInv;
    float4              cameraPositionAndTime;
    float4              viewportSizeAndFrameIndex;
    PathTracerConstants ptConsts;
    RTXPTEnvMapConstants envMap;
};

// Primary ray payload (Phase 5.1 compatibility - kept for the bridge sanity helpers).
struct PrimaryPayload
{
    float4 ColorDepth;
};

// Reference path tracer payload (Phase 5.2 / 5.3 / G4). Size is 80 bytes (20 floats); do not grow without
// updating RTXPTRayTracingPass::Initialize MaxPayloadSize.
//   hitFlag    : 1 on closest hit, 0 on miss.
//   hitDistance: RayTCurrent() on hit; <= 0 on miss.
//   worldPos   : world-space hit position.
//   worldNormal: world-space shading normal (interpolated, normal-mapped, renormalized).
//   baseColor  : material base color RGB (sampled via the material bridge).
//   metallic   : glTF metallic value at the hit (factor * texture .b).
//   emission   : RGB emission written by miss/emissive paths and accumulated by raygen.
//   roughness  : glTF perceptual roughness at the hit (factor * texture .g).
//   emissiveLightPdf: area-light solid-angle pdf for G4 emissive BSDF-hit MIS (0 when not applicable).
struct PathPayload
{
    float3 worldPos;
    float  hitDistance;

    float3 worldNormal;
    uint   hitFlag;

    float3 baseColor;
    float  metallic;

    float3 emission;
    float  roughness;

    float emissiveLightPdf;
    float _pad0;
    float _pad1;
    float _pad2;
};

// Mirrors Diligent::SubInstanceData in RTXPTAccelerationStructures.hpp.
// One entry per (BLAS instance, geometry) pair. The C++ side stores the per-instance
// sub-instance base in TLAS CustomId, exposed in closest-hit shaders as InstanceID().
// index = InstanceID() + GeometryIndex().
struct SubInstanceData
{
    uint MaterialID;
    uint Flags;
    uint IndexOffset;
    uint IndexCount;
    uint VertexOffset;
    uint VertexCount;
    uint emissiveTriangleOffset; // First entry in t_EmissiveTriangles for this sub-instance (G4).
    uint _padding1;
};

// Mirrors Diligent::MaterialPTData in RTXPTMaterials.hpp (must keep order/size in sync; total size 96 bytes).
struct MaterialPTData
{
    float4 baseColorFactor; // offset 0

    float3 emissiveFactor; // offset 16
    float  alphaCutoff;    // offset 28

    uint  flags;                 // offset 32
    uint  baseColorTextureIndex; // offset 36
    uint  emissiveTextureIndex;  // offset 40
    float metallicFactor;        // offset 44

    float roughnessFactor;               // offset 48
    float baseColorTextureSlice;         // offset 52
    float emissiveTextureSlice;          // offset 56
    uint  metallicRoughnessTextureIndex; // offset 60

    float metallicRoughnessTextureSlice; // offset 64
    uint  normalTextureIndex;            // offset 68
    float normalTextureSlice;            // offset 72
    float normalScale;                   // offset 76

    float _padding0; // offset 80
    float _padding1; // offset 84
    float _padding2; // offset 88
    float _padding3; // offset 92
};

// Mirrors the kMaterialFlag_* constants in RTXPTMaterials.hpp.
static const uint kMaterialFlagHasBaseColorTexture         = 0x1u;
static const uint kMaterialFlagAlphaTested                 = 0x2u;
static const uint kMaterialFlagHasEmissiveTexture          = 0x4u;
static const uint kMaterialFlagHasMetallicRoughnessTexture = 0x8u;
static const uint kMaterialFlagHasNormalTexture            = 0x10u;
static const uint kMaterialFlagEmissiveAreaLight           = 0x20u;

// Mirrors Diligent::PolymorphicLightInfo in RTXPTLights.hpp.
struct PolymorphicLightInfo
{
    float4 colorType;
    float4 positionRadius;
    float4 directionRange;
    float4 shaping;
};

static const uint kPolymorphicLightTypeSphere      = 0u;
static const uint kPolymorphicLightTypeDirectional = 2u;
static const uint kPolymorphicLightTypePoint       = 4u;

static const uint kLightProxyKindAnalytic       = 0u;
static const uint kLightProxyKindEmissiveBucket = 1u;

// Mirrors Diligent::EmissiveTriangle in RTXPTLights.hpp (total size 64 bytes). One world-space,
// NEE-eligible emissive triangle (constant emitter). Stores base + two edges + radiance like
// RTXPT-fork TriangleLight; the surface normal and area are recomputed on the fly.
struct EmissiveTriangle
{
    float4 base;     // xyz: world-space vertex 0 (.w unused)
    float4 edge1;    // xyz: world-space (vertex1 - vertex0) (.w unused)
    float4 edge2;    // xyz: world-space (vertex2 - vertex0) (.w unused)
    float4 radiance; // rgb: emitted radiance (.w unused)
};

// Per-vertex layout for vertex buffer 0 of the default Diligent GLTF model
// (POSITION + NORMAL + TEXCOORD_0). Total size = 32 bytes; must equal the
// vertex stride captured by RTXPTScene::GetVertexStride0().
struct GeometryVertexData
{
    float3 position;
    float3 normal;
    float2 texCoord0;
};

// Mirrors the default GLTF skin stream in vertex buffer 1: JOINTS_0 followed by WEIGHTS_0.
struct SkinVertexData
{
    float4 joints;
    float4 weights;
};

#endif // __PATH_TRACER_SHARED_H__
