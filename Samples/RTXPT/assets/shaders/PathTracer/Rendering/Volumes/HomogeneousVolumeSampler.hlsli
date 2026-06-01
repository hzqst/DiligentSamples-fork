#ifndef __HOMOGENEOUS_VOLUME_SAMPLER_HLSLI__
#define __HOMOGENEOUS_VOLUME_SAMPLER_HLSLI__

#include "../../Scene/Material/HomogeneousVolumeData.hlsli"

struct HomogeneousVolumeSampler
{
    static float3 evalTransmittance(float3 sigmaT, float distance)
    {
        return exp(-max(distance, 0.0) * sigmaT);
    }

    static float3 evalTransmittance(HomogeneousVolumeData vd, float distance)
    {
        return evalTransmittance(vd.sigmaA + vd.sigmaS, distance);
    }
};

#endif // __HOMOGENEOUS_VOLUME_SAMPLER_HLSLI__
