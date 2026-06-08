#include "Config.h"

#define ENABLE_HIT_BRIDGE 1
#if PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE
#    include "PathTracerBridge.hlsli"
#else
#    include "PathTracer.hlsli"
#endif
#include "Rendering/Materials/MaterialBridge.hlsli"

#if PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE
using ActiveRayPayload = RTXPTMaterialHitPayload;
#else
#    include "PathState.hlsli"
#    include "PathPayload.hlsli"
using ActiveRayPayload = PathPayload;

namespace PathTracer
{
    inline float3 MakeFallbackTangent(float3 normal)
    {
        const float3 axis = abs(normal.x) > 0.9 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        return normalize(cross(axis, normal));
    }

    inline SurfaceData LoadCurrentSurfaceData(BuiltInTriangleIntersectionAttributes Attributes,
                                              out float3 surfaceEmission)
    {
        const float3 rayDir = WorldRayDirection();

        float3 worldNormal  = -rayDir;
        float3 faceNormal   = -rayDir;
        float3 worldPos     = WorldRayOrigin() + rayDir * RayTCurrent();
        float3 tangent      = MakeFallbackTangent(normalize(worldNormal));
        float  tangentSign  = 1.0;
        float  roughness    = 1.0;
        float3 baseColor    = float3(1.0, 1.0, 1.0);
        float  metallic     = 0.0;
        float  transmissionFactor        = 0.0;
        float  diffuseTransmissionFactor = 0.0;
        bool   thinSurface               = true;
        float  shadowNoLFadeout                 = 0.0;
        float3 vertexNormal                     = worldNormal;
        uint   materialID                       = 0u;
        bool   frontFacing                      = true;
        float  ior                              = 1.5;
        uint   materialFlags                    = 0u;
        uint   nestedPriority                   = 14u;
        uint   psdExclude                       = 0u;
        uint   psdBlockMotionVectorsAtSurface   = 0u;
        uint   psdDominantDeltaLobeP1           = 0u;
        surfaceEmission                         = float3(0.0, 0.0, 0.0);

        const SubInstanceData subInstance = Bridge::getSubInstanceData();
        const MaterialPTData  material    = Bridge::getMaterial(subInstance.MaterialID);
        materialID                        = subInstance.MaterialID;

        GeometryVertexData V0;
        GeometryVertexData V1;
        GeometryVertexData V2;
        Bridge::getTriangleVertices(subInstance, PrimitiveIndex(), V0, V1, V2);

        const float2 texCoord = Bridge::interpolateTexCoord(V0, V1, V2, Attributes.barycentrics);

        const float3 geometricNormal = Bridge::computeGeometricNormal(V0, V1, V2);
        frontFacing                  = dot(-rayDir, geometricNormal) >= 0.0;
        faceNormal                   = frontFacing ? geometricNormal : -geometricNormal;
        worldPos                     = Bridge::computeWorldHitPosition(V0, V1, V2, Attributes.barycentrics);
        worldNormal                  = Bridge::interpolateNormal(V0, V1, V2, Attributes.barycentrics);
        if (dot(worldNormal, worldNormal) < 1e-6)
            worldNormal = faceNormal;
        if (dot(worldNormal, faceNormal) < 0.0)
            worldNormal = -worldNormal;

        const float3 tangentNormal = Bridge::ignoreMeshTangentSpace(material) ? float3(0.0, 0.0, 1.0) :
            Bridge::getTangentNormal(material, texCoord);
        if (abs(tangentNormal.x) + abs(tangentNormal.y) > 1e-5)
        {
            const float4 worldTangent = Bridge::computeWorldTangent(V0, V1, V2, worldNormal);
            const float3 T            = worldTangent.xyz;
            const float3 B            = cross(worldNormal, T) * worldTangent.w;
            const float3 mappedNormal = T * tangentNormal.x + B * tangentNormal.y + worldNormal * tangentNormal.z;
            const float  lenSq        = dot(mappedNormal, mappedNormal);
            if (lenSq > 1e-8)
            {
                worldNormal = mappedNormal * rsqrt(lenSq);
                if (dot(worldNormal, faceNormal) < 0.0)
                    worldNormal = -worldNormal;
            }
        }

        const float4 worldTangent = Bridge::computeWorldTangent(V0, V1, V2, worldNormal);
        tangent                   = worldTangent.xyz;
        tangentSign               = worldTangent.w;

        const float2 metalRough = Bridge::getMetallicRoughness(material, texCoord);
        metallic                = metalRough.x;
        roughness               = metalRough.y;
        const float4 baseColorWithAlpha = Bridge::getBaseColor(material, texCoord);
        baseColor                       = baseColorWithAlpha.rgb;
        transmissionFactor              = Bridge::getTransmission(material, texCoord);
        diffuseTransmissionFactor       = Bridge::getDiffuseTransmission(material, texCoord);
        thinSurface                     = Bridge::isThinSurface(material);
        shadowNoLFadeout                = Bridge::loadShadowNoLFadeout(materialID);
        vertexNormal                    = worldNormal;
        surfaceEmission                 = Bridge::getEmission(material, texCoord);
        ior                             = Bridge::loadIoR(materialID);
        materialFlags                   = material.flags;
        nestedPriority                  = min(material.nestedPriority, 14u);
        psdExclude                      = Bridge::isPSDExclude(material) ? 1u : 0u;
        psdBlockMotionVectorsAtSurface  = Bridge::isPSDBlockMotionVectorsAtSurface(material) ? 1u : 0u;
        psdDominantDeltaLobeP1          = Bridge::getPSDDominantDeltaLobeP1(material);

        worldNormal = normalize(worldNormal);
        tangent     = normalize(tangent);

        StablePlaneMaterialState mtl;
        mtl.flags                          = materialFlags;
        mtl.nestedPriority                 = nestedPriority;
        mtl.activeLobes                    = kLobeTypeAll;
        mtl.psdExclude                     = psdExclude;
        mtl.psdBlockMotionVectorsAtSurface = psdBlockMotionVectorsAtSurface;
        mtl.psdDominantDeltaLobeP1         = psdDominantDeltaLobeP1;

        StablePlaneShadingData shadingData;
        shadingData.posW        = worldPos;
        shadingData.N           = worldNormal;
        shadingData.V           = normalize(-rayDir);
        shadingData.T           = tangent;
        shadingData.B           = normalize(cross(worldNormal, tangent) * tangentSign);
        shadingData.materialID  = materialID;
        shadingData.frontFacing = frontFacing;
        shadingData.mtl         = mtl;
        shadingData.faceNCorrected    = faceNormal;
        shadingData.vertexN           = vertexNormal;
        shadingData.shadowNoLFadeout  = shadowNoLFadeout;
        shadingData.emission          = surfaceEmission;

        ActiveBSDF bsdf;
        bsdf.data.roughness = roughness;
        bsdf.standardData = MakeStandardBSDFData(worldNormal,
                                                 baseColor,
                                                 metallic,
                                                 roughness,
                                                 ior,
                                                 1.0,
                                                 transmissionFactor,
                                                 diffuseTransmissionFactor,
                                                 thinSurface,
                                                 frontFacing,
                                                 mtl.getActiveLobes(),
                                                 mtl.isPSDExclude(),
                                                 mtl.isPSDBlockMotionVectorsAtSurface(),
                                                 mtl.getPSDDominantDeltaLobeP1());

        return SurfaceData::make(shadingData,
                                 bsdf,
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
                                 worldPos,
#endif
                                 ior,
                                 0xFFFFFFFFu,
                                 0xFFFFFFFFu);
    }

    inline void HandleHit(inout PathState path,
                          BuiltInTriangleIntersectionAttributes Attributes,
                          const WorkingContext workingContext)
    {
        float3 surfaceEmission;
        SurfaceData surfaceData = LoadCurrentSurfaceData(Attributes, surfaceEmission);
        HandleHit(path,
                  surfaceData,
                  surfaceEmission,
                  WorldRayOrigin(),
                  WorldRayDirection(),
                  RayTCurrent(),
                  workingContext);
    }
}
#endif

[shader("closesthit")]
void main(inout ActiveRayPayload Payload,
          in BuiltInTriangleIntersectionAttributes Attributes)
{
#if PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE
    Payload.hitFlag     = 1u;
    Payload.hitDistance = RayTCurrent();
    Payload.emission    = float3(0.0, 0.0, 0.0);
    Payload.emissiveLightPdf = 0.0;

    // Initialize outputs before material bridge data overwrites them below.
    float3 BaseColor   = float3(Attributes.barycentrics.x,
                                Attributes.barycentrics.y,
                                1.0 - Attributes.barycentrics.x - Attributes.barycentrics.y);
    float3 WorldNormal = -WorldRayDirection();
    float3 VertexNormal = WorldNormal;
    float3 FaceNormal  = -WorldRayDirection();
    float3 WorldPos    = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float  Metallic    = 0.0;
    float  Roughness   = 1.0;
    uint   MaterialID  = 0u;
    bool   FrontFacing = true;

    const SubInstanceData subInstance = Bridge::getSubInstanceData();
    const MaterialPTData  material    = Bridge::getMaterial(subInstance.MaterialID);
    MaterialID                        = subInstance.MaterialID;

    GeometryVertexData V0;
    GeometryVertexData V1;
    GeometryVertexData V2;
    Bridge::getTriangleVertices(subInstance, PrimitiveIndex(), V0, V1, V2);

    const float2 texCoord = Bridge::interpolateTexCoord(V0, V1, V2, Attributes.barycentrics);

    const float3 RayDir          = WorldRayDirection();
    const float3 geometricNormal = Bridge::computeGeometricNormal(V0, V1, V2);
    const bool   frontFacing     = dot(-RayDir, geometricNormal) >= 0.0;
    FrontFacing                  = frontFacing;
    FaceNormal                   = frontFacing ? geometricNormal : -geometricNormal;
    WorldPos                     = Bridge::computeWorldHitPosition(V0, V1, V2, Attributes.barycentrics);
    WorldNormal                  = Bridge::interpolateNormal(V0, V1, V2, Attributes.barycentrics);
    // Renormalize against the geometric normal if the interpolated normal is nearly zero
    // (degenerate vertex data) - keeps the shader robust on bad assets.
    if (dot(WorldNormal, WorldNormal) < 1e-6)
        WorldNormal = FaceNormal;
    if (dot(WorldNormal, FaceNormal) < 0.0)
        WorldNormal = -WorldNormal;
    VertexNormal = WorldNormal;

    // Perturb the shading normal with the tangent-space normal map (tangent derived from UV gradients).
    const float3 tangentNormal = Bridge::ignoreMeshTangentSpace(material) ? float3(0.0, 0.0, 1.0) :
        Bridge::getTangentNormal(material, texCoord);
    if (abs(tangentNormal.x) + abs(tangentNormal.y) > 1e-5)
    {
        const float4 worldTangent = Bridge::computeWorldTangent(V0, V1, V2, WorldNormal);
        const float3 T            = worldTangent.xyz;
        const float3 B            = cross(WorldNormal, T) * worldTangent.w;
        const float3 mappedNormal = T * tangentNormal.x + B * tangentNormal.y + WorldNormal * tangentNormal.z;
        const float  lenSq        = dot(mappedNormal, mappedNormal);
        if (lenSq > 1e-8)
        {
            WorldNormal = mappedNormal * rsqrt(lenSq);
            if (dot(WorldNormal, FaceNormal) < 0.0)
                WorldNormal = -WorldNormal;
        }
    }

    const float2 metalRough = Bridge::getMetallicRoughness(material, texCoord);
    Metallic                = metalRough.x;
    Roughness               = metalRough.y;

    const float4 BaseColorWithAlpha = Bridge::getBaseColor(material, texCoord);
    BaseColor                       = BaseColorWithAlpha.rgb;
    Payload.emission                = Bridge::getEmission(material, texCoord);
    Payload.ior                     = Bridge::loadIoR(MaterialID);
    Payload.transmissionFactor      = Bridge::getTransmission(material, texCoord);
    Payload.diffuseTransmissionFactor  = Bridge::getDiffuseTransmission(material, texCoord);
    Payload.transmissionColor          = BaseColorWithAlpha.rgb;
    Payload.volumeAttenuationDistance  = material.volumeAttenuationDistance;
    Payload.volumeAttenuationColor     = material.volumeAttenuationColor;
    Payload.materialFlags              = material.flags;
    Payload.nestedPriority             = min(material.nestedPriority, 14u);
    Payload.thinSurface                = Bridge::isThinSurface(material) ? 1u : 0u;
    Payload.alpha                      = BaseColorWithAlpha.a;
    Payload.shadowNoLFadeout           = Bridge::loadShadowNoLFadeout(MaterialID);

    // Precompute the emissive triangle's area-light solid-angle pdf so raygen can MIS-weight
    // the BSDF-hit emission against emissive-triangle NEE. Only NEE-eligible constant emitters
    // get a non-zero pdf; textured emissive surfaces stay BSDF-only for now.
    if ((material.flags & kMaterialFlagEmissiveAreaLight) != 0u)
    {
        const float3 wp0   = mul(ObjectToWorld3x4(), float4(V0.position, 1.0));
        const float3 wp1   = mul(ObjectToWorld3x4(), float4(V1.position, 1.0));
        const float3 wp2   = mul(ObjectToWorld3x4(), float4(V2.position, 1.0));
        const float3 ng    = cross(wp1 - wp0, wp2 - wp0);
        const float  ngLen = length(ng);
        const float  area  = 0.5 * ngLen;
        if (area > 1e-9)
        {
            const float3 normal  = ng / ngLen;
            const float  cosTheta = abs(dot(normal, -RayDir));
            if (cosTheta > 2e-9)
                Payload.emissiveLightPdf = min(kMaxSolidAnglePdf, (1.0 / area) * (RayTCurrent() * RayTCurrent()) / cosTheta);
        }
    }

    Payload.worldPos    = WorldPos;
    Payload.worldNormal = normalize(WorldNormal);
    Payload.faceNormal  = normalize(FaceNormal);
    Payload.vertexNormal = normalize(VertexNormal);
    Payload.materialID  = MaterialID;
    Payload.frontFacing = FrontFacing ? 1u : 0u;
    Payload.baseColor   = BaseColor;
    Payload.metallic    = Metallic;
    Payload.roughness   = Roughness;
#else
    PathState path = PathPayload::unpack(Payload);
    PathTracer::WorkingContext workingContext = GetWorkingContext();
    PathTracer::HandleHit(path, Attributes, workingContext);
    Payload = PathPayload::pack(path);
#endif
}

// TODO(RTXPT-Port Phase R2): Emissive triangles feed area-light NEE + MIS (constant emitters only). Textured
// emissive triangles stay BSDF-only, and emitters are two-sided rather than RTXPT-fork's one-sided TriangleLight.
