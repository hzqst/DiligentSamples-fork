#ifndef RTXPT_LIGHT_SAMPLING_HLSLI
#define RTXPT_LIGHT_SAMPLING_HLSLI

#include "RTXPTShaderShared.hlsli"

struct RTXPTLightSample
{
    float3 Wi;
    float  Distance;
    float3 Radiance;
    bool   Valid;
};

RTXPTLightSample RTXPTInvalidLightSample()
{
    RTXPTLightSample Sample;
    Sample.Wi       = float3(0.0, 1.0, 0.0);
    Sample.Distance = 0.0;
    Sample.Radiance = float3(0.0, 0.0, 0.0);
    Sample.Valid    = false;
    return Sample;
}

bool RTXPTNormalizeDirection(float3 V, out float3 Dir)
{
    const float LenSq = dot(V, V);
    if (LenSq <= 1e-12)
    {
        Dir = float3(0.0, 0.0, 0.0);
        return false;
    }

    Dir = V * rsqrt(LenSq);
    return true;
}

RTXPTLightSample RTXPTEvalAnalyticLight(RTXPTLightData Light, float3 SurfacePos)
{
    const float  Type      = Light.DirectionType.w;
    const float3 Radiance  = max(Light.ColorIntensity.rgb, float3(0.0, 0.0, 0.0)) * max(Light.ColorIntensity.a, 0.0);
    const float  MaxEnergy = max(Radiance.x, max(Radiance.y, Radiance.z));
    if (Type < -0.5 || MaxEnergy <= 0.0)
        return RTXPTInvalidLightSample();

    RTXPTLightSample Sample = RTXPTInvalidLightSample();

    if (Type < 0.5)
    {
        if (!RTXPTNormalizeDirection(-Light.DirectionType.xyz, Sample.Wi))
            return RTXPTInvalidLightSample();

        Sample.Distance = 1e30;
        Sample.Radiance = Radiance;
        Sample.Valid    = true;
        return Sample;
    }

    if (Type >= 2.5)
        return RTXPTInvalidLightSample();

    const float3 ToLight  = Light.PositionRange.xyz - SurfacePos;
    const float  DistSq   = dot(ToLight, ToLight);
    const float  Distance = sqrt(DistSq);
    if (Distance <= 1e-4)
        return RTXPTInvalidLightSample();

    const float Range = Light.PositionRange.w;
    if (Range > 0.0 && Distance > Range)
        return RTXPTInvalidLightSample();

    Sample.Wi       = ToLight / Distance;
    Sample.Distance = Distance;

    float Attenuation = 1.0 / max(DistSq, 1e-6);

    if (Type >= 1.5)
    {
        float3 LightDir;
        if (!RTXPTNormalizeDirection(Light.DirectionType.xyz, LightDir))
            return RTXPTInvalidLightSample();

        const float CosTheta = dot(LightDir, -Sample.Wi);
        const float InnerCos = cos(Light.SpotAngles.x);
        const float OuterCos = cos(Light.SpotAngles.y);
        const float Cone     = saturate((CosTheta - OuterCos) / max(InnerCos - OuterCos, 1e-4));
        if (Cone <= 0.0)
            return RTXPTInvalidLightSample();

        Attenuation *= Cone * Cone;
    }

    Sample.Radiance = Radiance * Attenuation;
    Sample.Valid    = true;
    return Sample;
}

#endif // RTXPT_LIGHT_SAMPLING_HLSLI
