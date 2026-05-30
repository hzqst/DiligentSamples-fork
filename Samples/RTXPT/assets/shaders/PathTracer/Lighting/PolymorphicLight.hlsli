#ifndef __POLYMORPHIC_LIGHT_HLSLI__
#define __POLYMORPHIC_LIGHT_HLSLI__

#include "PathTracerShared.h"

struct LightSample
{
    float3 dir;
    float  distance;
    float3 radiance;
    bool   valid;
};

LightSample LightSample_make_empty()
{
    LightSample ls;
    ls.dir      = float3(0.0, 1.0, 0.0);
    ls.distance = 0.0;
    ls.radiance = float3(0.0, 0.0, 0.0);
    ls.valid    = false;
    return ls;
}

bool tryNormalize(float3 v, out float3 dir)
{
    const float lenSq = dot(v, v);
    if (lenSq <= 1e-12)
    {
        dir = float3(0.0, 0.0, 0.0);
        return false;
    }

    dir = v * rsqrt(lenSq);
    return true;
}

LightSample EvalAnalyticLight(PolymorphicLightInfo light, float3 surfacePos)
{
    const float  type      = light.directionType.w;
    const float3 radiance  = max(light.colorIntensity.rgb, float3(0.0, 0.0, 0.0)) * max(light.colorIntensity.a, 0.0);
    const float  maxEnergy = max(radiance.x, max(radiance.y, radiance.z));
    if (type < -0.5 || maxEnergy <= 0.0)
        return LightSample_make_empty();

    LightSample ls = LightSample_make_empty();

    if (type < 0.5)
    {
        if (!tryNormalize(-light.directionType.xyz, ls.dir))
            return LightSample_make_empty();

        ls.distance = 1e30;
        ls.radiance = radiance;
        ls.valid    = true;
        return ls;
    }

    if (type >= 2.5)
        return LightSample_make_empty();

    const float3 toLight  = light.positionRange.xyz - surfacePos;
    const float  distSq   = dot(toLight, toLight);
    const float  distance = sqrt(distSq);
    if (distance <= 1e-4)
        return LightSample_make_empty();

    const float range = light.positionRange.w;
    if (range > 0.0 && distance > range)
        return LightSample_make_empty();

    ls.dir      = toLight / distance;
    ls.distance = distance;

    float attenuation = 1.0 / max(distSq, 1e-6);

    if (type >= 1.5)
    {
        float3 lightDir;
        if (!tryNormalize(light.directionType.xyz, lightDir))
            return LightSample_make_empty();

        const float cosTheta = dot(lightDir, -ls.dir);
        const float innerCos = cos(light.spotAngles.x);
        const float outerCos = cos(light.spotAngles.y);
        const float cone     = saturate((cosTheta - outerCos) / max(innerCos - outerCos, 1e-4));
        if (cone <= 0.0)
            return LightSample_make_empty();

        attenuation *= cone * cone;
    }

    ls.radiance = radiance * attenuation;
    ls.valid    = true;
    return ls;
}

#endif // __POLYMORPHIC_LIGHT_HLSLI__
