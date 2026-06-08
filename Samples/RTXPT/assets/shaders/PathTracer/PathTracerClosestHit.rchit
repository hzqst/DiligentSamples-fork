#include "Config.h"

#define ENABLE_HIT_BRIDGE 1
#include "PathTracer.hlsli"
#include "Rendering/Materials/MaterialBridge.hlsli"

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
        float  emissiveLightPdf                 = 0.0;
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
        vertexNormal = worldNormal;

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
        surfaceEmission                 = Bridge::getEmission(material, texCoord);
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
                const float3 normal   = ng / ngLen;
                const float  cosTheta = abs(dot(normal, -rayDir));
                if (cosTheta > 2e-9)
                    emissiveLightPdf = min(kMaxSolidAnglePdf, (1.0 / area) * (RayTCurrent() * RayTCurrent()) / cosTheta);
            }
        }
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
                                 0xFFFFFFFFu,
                                  emissiveLightPdf);
    }

    inline void HandleHit(inout PathState path,
                          BuiltInTriangleIntersectionAttributes Attributes,
                          const WorkingContext workingContext)
    {
        float3 surfaceEmission;
        SurfaceData surfaceData = LoadCurrentSurfaceData(Attributes, surfaceEmission);
        const float3 rayOrigin    = WorldRayOrigin();
        const float3 rayDir       = WorldRayDirection();
        const float  rayTCurrent  = RayTCurrent();

#if PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE
        UpdatePathTravelled(path, rayOrigin, rayDir, rayTCurrent, workingContext);

        path.CaptureReferencePrimaryDepth(rayTCurrent);

#if RTXPT_NESTED_DIELECTRICS_QUALITY > 0 || defined(__INTELLISENSE__)
        if (workingContext.PtConsts.nestedDielectricsQuality != 0u &&
            !path.interiorList.isEmpty())
        {
            const HomogeneousVolumeData volume = Bridge::loadHomogeneousVolumeData(path.interiorList.getTopMaterialID());
            const float3 transmittance = HomogeneousVolumeSampler::evalTransmittance(volume, rayTCurrent);
            UpdatePathThroughput(path, transmittance);
        }
#endif

        const bool rejectedFalseHit = !HandleNestedDielectrics(surfaceData, path, workingContext);
        if (rejectedFalseHit)
            return;

        float3 referenceSurfaceEmission = surfaceEmission;
        const NEEBSDFMISInfo prevMISInfo = NEEBSDFMISInfo::Unpack16bit(path.GetPackedMISInfo());
        const bool didEmissiveNEE =
            prevMISInfo.LightSamplingEnabled &&
            prevMISInfo.FullSamples > 0u &&
            Bridge::getEmissiveTriangleCount() > 0u;
        if (didEmissiveNEE && path.GetBsdfScatterPdf() > 0.0 && surfaceData.emissiveLightPdf > 0.0)
        {
            referenceSurfaceEmission *= ComputeBSDFMISForEmissiveTriangle(path.GetPixelPos(),
                                                                          path.GetBsdfScatterPdf(),
                                                                          surfaceData.emissiveLightPdf,
                                                                          prevMISInfo.FullSamples);
        }
        referenceSurfaceEmission = ApplyReferenceFireflyFilter(referenceSurfaceEmission, path, workingContext);

        if (any(referenceSurfaceEmission > 0.0))
        {
            const float3 radiance = path.GetThp() * referenceSurfaceEmission;
            const float specRadianceAvg =
                path.hasFlag(PathFlags::stablePlaneBaseScatterDiff) ? 0.0 : Average(radiance);
            AccumulatePathRadiance(workingContext,
                                   path,
                                   radiance,
                                   specRadianceAvg,
                                   path.hasFlag(PathFlags::stablePlaneOnBranch),
                                   ShouldCollectGISecondaryRadiance(path));
        }

        const bool pathStopping = path.isTerminatingAtNextBounce();

        if (pathStopping)
        {
            path.terminate();
            return;
        }

        UpdatePathThroughput(path, path.GetThpRuRuCorrection().xxx);

        const PathState preScatterPath = path;
        BSDFSample bs;
        const bool scatterValid = GenerateScatterRay(surfaceData, path, workingContext, bs);

        SampleGenerator sgDirect = SampleGenerator_makeStateless(preScatterPath.GetPixelPos(),
                                                                 preScatterPath.getVertexIndex(),
                                                                 Bridge::getSampleIndex(),
                                                                 kSampleEffect_NEELightSampler);
        SampleGenerator sgEnv = SampleGenerator_makeStateless(preScatterPath.GetPixelPos(),
                                                              preScatterPath.getVertexIndex(),
                                                              Bridge::getSampleIndex(),
                                                              kSampleEffect_NextEventEstimation);
        NEEResult neeResult = HandleNEE(preScatterPath, surfaceData, sgDirect, sgEnv, workingContext);
        path.SetPackedMISInfo_ThpRuRuCorrection(neeResult.BSDFMISInfo.Pack16bit(), path.GetThpRuRuCorrection());
        AccumulateNEERadiance(workingContext, path, preScatterPath, neeResult);

        if (!scatterValid)
        {
            path.terminate();
            return;
        }

        const bool shouldTerminate = HasFinishedSurfaceBounces(path.getVertexIndex() + 1,
                                                               path.getCounter(PackedCounters::DiffuseBounces));
        if (shouldTerminate)
        {
            path.setTerminateAtNextBounce();
        }
        else if (!HandleRussianRoulette(path, preScatterPath, workingContext))
        {
            return;
        }
#else
        HandleHit(path, surfaceData, surfaceEmission, rayOrigin, rayDir, rayTCurrent, workingContext);
        return;
#endif
    }
}

[shader("closesthit")]
void main(inout ActiveRayPayload Payload,
          in BuiltInTriangleIntersectionAttributes Attributes)
{
    PathState path = PathPayload::unpack(Payload);
    PathTracer::WorkingContext workingContext = GetWorkingContext();
    PathTracer::HandleHit(path, Attributes, workingContext);
    Payload = PathPayload::pack(path);
}

// TODO(RTXPT-Port Phase R2): Emissive triangles feed area-light NEE + MIS (constant emitters only). Textured
// emissive triangles stay BSDF-only, and emitters are two-sided rather than RTXPT-fork's one-sided TriangleLight.
