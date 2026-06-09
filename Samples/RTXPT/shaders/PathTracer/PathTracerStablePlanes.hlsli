#ifndef __PATH_TRACER_STABLE_PLANES_HLSLI__
#define __PATH_TRACER_STABLE_PLANES_HLSLI__

#include "PathTracerTypes.hlsli"
#include "StablePlanes.hlsli"

static const float kSpecularRoughnessThreshold = 0.25;

namespace PathTracer
{
    inline void UpdatePathTravelledLengthOnly(inout PathState path, const float rayTCurrent)
    {
        path.SetSceneLength(min(path.GetSceneLength() + rayTCurrent, kMaxRayTravel));
    }

#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES || defined(__INTELLISENSE__)
    inline PathState SplitDeltaPath(const PathState oldPath,
                                    const float3 rayDir,
                                    const SurfaceData surfaceData,
                                    const DeltaLobe lobe,
                                    const uint deltaLobeIndex,
                                    const bool verifyDominantFlag,
                                    const WorkingContext workingContext)
    {
        const StablePlaneShadingData shadingData = surfaceData.shadingData;
        PathState newPath                       = oldPath;
        newPath.SetDir(lobe.dir);
        newPath.SetThp(newPath.GetThp() * lobe.thp);
        newPath.SetOrigin(shadingData.computeNewRayOrigin(lobe.transmission == 0));
        newPath.stableBranchID = StablePlanesAdvanceBranchID(oldPath.stableBranchID, deltaLobeIndex);
        newPath.setScatterDelta();

        if (lobe.transmission == 0)
        {
            newPath.setScatterSpecular();
        }
        else
        {
            newPath.setScatterTransmission();
#if RTXPT_NESTED_DIELECTRICS_QUALITY > 0 || defined(__INTELLISENSE__)
            if (!shadingData.mtl.isThinSurface())
            {
                uint nestedPriority = shadingData.mtl.getNestedPriority();
                newPath.interiorList.handleIntersection(shadingData.materialID, nestedPriority, shadingData.frontFacing);
                newPath.setInsideDielectricVolume(!newPath.interiorList.isEmpty());
            }
#endif
        }

        if (newPath.GetMotionVectorSceneLength() == 0)
        {
            lpfloat3x3 localT;
            if (lobe.transmission != 0)
            {
                localT = (lpfloat3x3)MatrixRotateFromTo(lobe.dir, rayDir);
            }
            else
            {
                const lpfloat3x3 toTangent = lpfloat3x3(shadingData.T, shadingData.B, shadingData.N);
                const lpfloat3x3 mirror    = lpfloat3x3(1, 0, 0, 0, 1, 0, 0, 0, -1);
                localT = mul(mirror, toTangent);
                localT = mul(transpose(toTangent), localT);
            }
            newPath.SetImageXform(mul(newPath.GetImageXform(), localT));
        }

        if (verifyDominantFlag && newPath.hasFlag(PathFlags::stablePlaneOnDominantBranch))
        {
            int psdDominantDeltaLobeIndex = int(shadingData.mtl.getPSDDominantDeltaLobeP1()) - 1;
            if (deltaLobeIndex != psdDominantDeltaLobeIndex)
                newPath.setFlag(PathFlags::stablePlaneOnDominantBranch, false);
        }

        return newPath;
    }
#endif

#if PATH_TRACER_MODE != PATH_TRACER_MODE_REFERENCE
    inline void StablePlanesHandleHit(inout PathState path,
                                      const float3 rayOrigin,
                                      const float3 rayDir,
                                      const float rayTCurrent,
                                      const WorkingContext workingContext,
                                      const SurfaceData surfaceData,
                                      float volumeAbsorption,
                                      float3 surfaceEmission,
                                      bool pathStopping)
    {
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES || defined(__INTELLISENSE__)
        const uint  vertexIndex    = path.getVertexIndex();
        const uint  currentSPIndex = path.getStablePlaneIndex();
        const uint2 pixelPos       = path.GetPixelPos();

        if (surfaceData.shadingData.mtl.isPSDBlockMotionVectorsAtSurface() &&
            path.GetMotionVectorSceneLength() == 0)
        {
            path.SetMotionVectorSceneLength(path.GetSceneLength());
        }

        if (vertexIndex == 1)
            workingContext.StablePlanes.StoreFirstHitRayLengthAndClearDominantToZero(pixelPos, path.GetSceneLength());

        bool  setAsBase          = true;
        float passthroughOverride = 0.0;
        if ((vertexIndex < workingContext.PtConsts.maxStablePlaneVertexDepth) && !pathStopping)
        {
            DeltaLobe deltaLobes[cMaxDeltaLobes];
            uint      deltaLobeCount;
            float     nonDeltaPart;
            surfaceData.bsdf.evalDeltaLobes(surfaceData.shadingData, deltaLobes, deltaLobeCount, nonDeltaPart);
            deltaLobeCount = max(cMaxDeltaLobes - 1, deltaLobeCount);

            bool  potentiallyVolumeTransmission = false;
            float pathThp                       = Average(path.GetThp());
            const float nonDeltaIgnoreThreshold = 1e-5;
            const float deltaIgnoreThreshold    = 0.001f;
            bool hasNonDeltaLobes               = nonDeltaPart > nonDeltaIgnoreThreshold;
            passthroughOverride                 = saturate(1.0 - nonDeltaPart * 10.0);

            int nonZeroDeltaLobes[cMaxDeltaLobes];
            for (int i = 0; i < cMaxDeltaLobes; i++)
                nonZeroDeltaLobes[i] = 0;
            int nonZeroDeltaLobeCount = 0;
            for (int k = 0; k < deltaLobeCount; k++)
            {
                DeltaLobe lobe = deltaLobes[k];
                const float thp = Average(lobe.thp);
                if (thp > deltaIgnoreThreshold)
                {
                    nonZeroDeltaLobes[nonZeroDeltaLobeCount] = k;
                    nonZeroDeltaLobeCount++;
                    potentiallyVolumeTransmission |= lobe.transmission != 0;
                }
            }

            if (nonZeroDeltaLobeCount > 0)
            {
                bool allowPSR = workingContext.PtConsts.allowPrimarySurfaceReplacement != 0 &&
                    (nonZeroDeltaLobeCount == 1) &&
                    (currentSPIndex == 0) &&
                    !potentiallyVolumeTransmission;
                allowPSR &= !surfaceData.shadingData.mtl.isPSDBlockMotionVectorsAtSurface();

                bool canReuseExisting = (currentSPIndex != 0) && (nonZeroDeltaLobeCount > 0);
                canReuseExisting |= allowPSR;
                canReuseExisting &= !hasNonDeltaLobes;

                int availablePlaneCount = 0;
                int availablePlanes[cStablePlaneCount];
                workingContext.StablePlanes.GetAvailableEmptyPlanes(pixelPos, availablePlaneCount, availablePlanes);

                canReuseExisting &= (currentSPIndex == 0) ||
                    (surfaceData.shadingData.mtl.getPSDDominantDeltaLobeP1() > 0);
                nonZeroDeltaLobeCount = min(nonZeroDeltaLobeCount, availablePlaneCount + canReuseExisting);

                int lobeForReuse = -1;
                if (canReuseExisting)
                {
                    lobeForReuse = nonZeroDeltaLobes[nonZeroDeltaLobeCount - 1];
                    nonZeroDeltaLobeCount--;
                }

                for (int i = 0; i < nonZeroDeltaLobeCount; i++)
                {
                    const int lobeToExplore = nonZeroDeltaLobes[i];
                    PathState splitPath = PathTracer::SplitDeltaPath(
                        path, rayDir, surfaceData, deltaLobes[lobeToExplore], lobeToExplore, true, workingContext);
                    splitPath.setStablePlaneIndex(availablePlanes[i]);
                    PathPayload splitPayload = PathPayload::pack(splitPath);
                    uint4       splitPayloadPacked[RTXPT_PATH_PAYLOAD_UINT4_COUNT];
                    PathPayload::toArray(splitPayload, splitPayloadPacked);
                    workingContext.StablePlanes.StoreExplorationStart(
                        pixelPos, availablePlanes[i], splitPayloadPacked);
                }

                if (lobeForReuse != -1)
                {
                    setAsBase = false;
                    path = PathTracer::SplitDeltaPath(
                        path, rayDir, surfaceData, deltaLobes[lobeForReuse], lobeForReuse, nonZeroDeltaLobeCount > 0, workingContext);
                }
            }
        }

        if (setAsBase)
        {
            const Ray cameraRay = Bridge::computeCameraRay(pixelPos);
            const float3x3 imageXform = path.GetImageXform();
            const bool blockedAtSurface = path.GetMotionVectorSceneLength() != 0;

            float sceneLengthForMVs = blockedAtSurface ? path.GetMotionVectorSceneLength() : path.GetSceneLength();
            float3 virtualWorldPos  = cameraRay.origin + cameraRay.dir * sceneLengthForMVs;
            float3 worldMotion      = surfaceData.prevPosW - surfaceData.shadingData.posW;
            float3 virtualWorldMotion = mul(imageXform, worldMotion);
            float3 motionVectors      = Bridge::computeMotionVector(virtualWorldPos, virtualWorldPos + virtualWorldMotion);

            float roughness    = saturate(surfaceData.bsdf.data.Roughness());
            float3 worldNormal = normalize(mul((float3x3)imageXform, surfaceData.shadingData.N));

            float3 diffBSDFEstimate;
            float3 specBSDFEstimate;
            surfaceData.bsdf.estimateSpecDiffBSDF(
                diffBSDFEstimate, specBSDFEstimate, surfaceData.shadingData.N, surfaceData.shadingData.V);

            if (blockedAtSurface)
                roughness *= kSpecularRoughnessThreshold * 0.95;

            bool isDominant = path.hasFlag(PathFlags::stablePlaneOnDominantBranch);
            workingContext.StablePlanes.StoreStablePlane(pixelPos,
                                                         currentSPIndex,
                                                         vertexIndex,
                                                         rayOrigin,
                                                         rayDir,
                                                         path.stableBranchID,
                                                         path.GetSceneLength(),
                                                         rayTCurrent,
                                                         path.GetThp(),
                                                         motionVectors,
                                                         roughness,
                                                         worldNormal,
                                                         diffBSDFEstimate,
                                                         specBSDFEstimate,
                                                         isDominant,
                                                         0,
                                                         0);

            if (isDominant)
                Bridge::ExportSurface(path, surfaceData, sceneLengthForMVs, motionVectors);

            path.terminate();
        }
#endif
    }

#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES || defined(__INTELLISENSE__)
    inline void StablePlanesOnScatter(inout PathState path, const BSDFSample bs, const WorkingContext workingContext)
    {
        const uint2 pixelPos = path.GetPixelPos();
        const bool wasOnStablePlane = path.hasFlag(PathFlags::stablePlaneOnPlane);
        if (wasOnStablePlane)
            path.setFlag(PathFlags::stablePlaneBaseScatterDiff,
                         (bs.lobe & (kBSDFLobeDiffuseReflection | kBSDFLobeDiffuseTransmission)) != 0);
        path.setFlag(PathFlags::stablePlaneOnPlane, false);

        const uint nextVertexIndex = path.getVertexIndex() + 1;
        if (path.hasFlag(PathFlags::stablePlaneOnBranch) && nextVertexIndex <= cStablePlaneMaxVertexIndex)
        {
            path.stableBranchID = StablePlanesAdvanceBranchID(path.stableBranchID, bs.getDeltaLobeIndex());
            bool onStablePath   = false;
            for (uint spi = 0; spi < cStablePlaneCount; spi++)
            {
                const uint planeBranchID = workingContext.StablePlanes.GetBranchID(pixelPos, spi);
                if (planeBranchID == cStablePlaneInvalidBranchID)
                    continue;

                if (StablePlaneIsOnPlane(planeBranchID, path.stableBranchID))
                {
                    workingContext.StablePlanes.CommitDenoiserRadiance(path);
                    path.setStablePlaneIndex(spi);
                    path.setFlag(PathFlags::stablePlaneOnDominantBranch,
                                 spi == workingContext.StablePlanes.LoadDominantIndex(pixelPos));
                    path.setFlag(PathFlags::stablePlaneOnPlane, true);
                    path.setCounter(PackedCounters::BouncesFromStablePlane, 0);
                    onStablePath = true;
                    break;
                }

                onStablePath |= StablePlaneIsOnStablePath(planeBranchID,
                                                          StablePlanesVertexIndexFromBranchID(planeBranchID),
                                                          path.stableBranchID,
                                                          nextVertexIndex);
            }
            path.setFlag(PathFlags::stablePlaneOnBranch, onStablePath);
        }
        else
        {
            path.stableBranchID = cStablePlaneInvalidBranchID;
            path.setFlag(PathFlags::stablePlaneOnBranch, false);
            path.incrementCounter(PackedCounters::BouncesFromStablePlane);
        }

        if (!path.hasFlag(PathFlags::stablePlaneOnPlane))
            path.incrementCounter(PackedCounters::BouncesFromStablePlane);
    }
#endif

    inline void StablePlanesHandleMiss(inout PathState path,
                                       float3 emission,
                                       const float3 rayOrigin,
                                       const float3 rayDir,
                                       const float rayTCurrent,
                                       const WorkingContext workingContext)
    {
        const uint2 pixelPos = path.GetPixelPos();
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES || defined(__INTELLISENSE__)
        const uint vertexIndex = path.getVertexIndex();
        if (vertexIndex == 1)
            workingContext.StablePlanes.StoreFirstHitRayLengthAndClearDominantToZero(pixelPos, kMaxRayTravel);

        const Ray cameraRay = Bridge::computeCameraRay(pixelPos);
        const bool blockedAtSurface = path.GetMotionVectorSceneLength() != 0;
        const float sceneLengthForMVs = blockedAtSurface ?
            path.GetMotionVectorSceneLength() :
            kEnvironmentMapSceneDistance;
        const float3 virtualWorldPos = cameraRay.origin + cameraRay.dir * sceneLengthForMVs;
        float3 motionVectors = Bridge::computeMotionVector(virtualWorldPos, virtualWorldPos);

        bool isDominant = path.hasFlag(PathFlags::stablePlaneOnDominantBranch);
        float3 skyAlbedo = sqrt(ReinhardMax(emission));
        const float missSceneLength = blockedAtSurface ? sceneLengthForMVs : asfloat(0x7f800000);

        workingContext.StablePlanes.StoreStablePlane(pixelPos,
                                                     path.getStablePlaneIndex(),
                                                     vertexIndex,
                                                     rayOrigin,
                                                     rayDir,
                                                     path.stableBranchID,
                                                     missSceneLength,
                                                     0.0,
                                                     path.GetThp(),
                                                     motionVectors,
                                                     blockedAtSurface ? 0.1 : 1.0,
                                                     -rayDir,
                                                     skyAlbedo,
                                                     blockedAtSurface ? 0.5.xxx : 0.xxx,
                                                     isDominant,
                                                     0,
                                                     0);

        if (isDominant)
            Bridge::ExportNonSurface(path, virtualWorldPos, motionVectors);
#endif
    }
#endif
}

#endif // __PATH_TRACER_STABLE_PLANES_HLSLI__
