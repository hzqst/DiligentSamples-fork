#ifndef __ENVMAP_HLSLI__
#define __ENVMAP_HLSLI__

#include "../PathTracerShared.h"

static const float kEnvMapInvFourPi = 0.07957747154594767;
static const float kEnvMapPi         = 3.14159265358979323846;

float3 RTXPTEnvFallback(float3 worldDir)
{
    const float  t       = saturate(worldDir.y * 0.5 + 0.5);
    const float3 horizon = float3(0.48, 0.58, 0.68);
    const float3 zenith  = float3(0.05, 0.08, 0.14);
    return lerp(horizon, zenith, t);
}

float3 RTXPTEnvMul3x3(float3 v, float4 r0, float4 r1, float4 r2)
{
    // r0/r1/r2 store matrix rows; this matches mul(v, M) row-vector semantics.
    return float3(dot(v, float3(r0.x, r1.x, r2.x)),
                  dot(v, float3(r0.y, r1.y, r2.y)),
                  dot(v, float3(r0.z, r1.z, r2.z)));
}

float2 RTXPTDirToOctEqualArea(float3 n)
{
    const float r   = sqrt(max(0.0, 1.0 - abs(n.z)));
    const float phi = atan2(abs(n.y), abs(n.x));

    float2 p;
    p.y = r * phi * (2.0 / kEnvMapPi);
    p.x = r - p.y;

    if (n.z < 0.0)
        p = 1.0 - p.yx;

    p *= sign(n.xy);
    return saturate(p * 0.5 + 0.5);
}

float3 RTXPTEnvOctToDirEqualArea(float2 uv)
{
    const float2 p   = uv * 2.0 - 1.0;
    const float  d   = 1.0 - (abs(p.x) + abs(p.y));
    const float  r   = 1.0 - abs(d);
    const float  phi = r > 0.0 ? ((abs(p.y) - abs(p.x)) / r + 1.0) * (0.25 * kEnvMapPi) : 0.0;
    const float  f   = r * sqrt(max(0.0, 2.0 - r * r));
    const float  x   = f * sign(p.x) * cos(phi);
    const float  y   = f * sign(p.y) * sin(phi);
    const float  z   = sign(d) * (1.0 - r * r);
    return float3(x, y, z);
}

struct DistantLightSample
{
    float3 Dir;
    float  Pdf;
    float3 Le;
};

struct EnvMapSampler
{
    RTXPTEnvMapConstants Constants;

    float3 ToLocal(float3 dir)
    {
        return RTXPTEnvMul3x3(dir, Constants.WorldToLocal0, Constants.WorldToLocal1, Constants.WorldToLocal2);
    }

    float3 ToWorld(float3 dir)
    {
        return RTXPTEnvMul3x3(dir, Constants.LocalToWorld0, Constants.LocalToWorld1, Constants.LocalToWorld2);
    }

    float3 EvalLocal(float3 localDir, float lod)
    {
        if (Constants.ColorEnabled.w <= 0.0)
            return RTXPTEnvFallback(ToWorld(localDir)) * Constants.ColorEnabled.rgb;

        return t_EnvironmentMap.SampleLevel(s_EnvironmentMapSampler, localDir, lod).rgb * Constants.ColorEnabled.rgb;
    }

    float3 Eval(float3 worldDir, float lod)
    {
        return EvalLocal(ToLocal(worldDir), lod);
    }

    bool HasImportance()
    {
        return Constants.ImportanceMetadata.w > 0.0;
    }

    DistantLightSample UniformSample(float2 rnd)
    {
        const float  z        = 1.0 - 2.0 * rnd.x;
        const float  r        = sqrt(max(0.0, 1.0 - z * z));
        const float  phi      = 2.0 * kEnvMapPi * rnd.y;
        const float3 localDir = float3(r * cos(phi), r * sin(phi), z);

        DistantLightSample result;
        result.Dir = ToWorld(localDir);
        result.Pdf = kEnvMapInvFourPi;
        result.Le  = EvalLocal(localDir, 0.0);
        return result;
    }

    float UniformEvalPdf(float3 worldDir)
    {
        return kEnvMapInvFourPi;
    }

    DistantLightSample MIPDescentSample(float2 rnd)
    {
        if (Constants.ColorEnabled.w <= 0.0 || !HasImportance())
            return UniformSample(rnd);

        const int importanceBaseMip = (int)Constants.ImportanceMetadata.z;
        float2    p                 = rnd;
        uint2     pos               = uint2(0u, 0u);

        [loop]
        for (int mip = importanceBaseMip - 1; mip >= 0; --mip)
        {
            pos *= 2u;

            const float w00 = t_EnvironmentImportanceMap.Load(int3(pos, mip));
            const float w10 = t_EnvironmentImportanceMap.Load(int3(pos + uint2(1u, 0u), mip));
            const float w01 = t_EnvironmentImportanceMap.Load(int3(pos + uint2(0u, 1u), mip));
            const float w11 = t_EnvironmentImportanceMap.Load(int3(pos + uint2(1u, 1u), mip));

            const float q0    = w00 + w01;
            const float q1    = w10 + w11;
            const float total = q0 + q1;
            const float d     = total > 0.0 ? saturate(q0 / total) : 0.5;
            uint2       off   = uint2(0u, 0u);

            if (p.x < d)
            {
                off.x = 0u;
                p.x   = d > 0.0 ? p.x / d : 0.0;
            }
            else
            {
                off.x = 1u;
                p.x   = (p.x - d) / max(1.0 - d, 1e-20);
            }

            const float columnWeight = off.x == 0u ? q0 : q1;
            const float rowWeight    = off.x == 0u ? w00 : w10;
            const float e            = columnWeight > 0.0 ? rowWeight / columnWeight : 0.5;

            if (p.y < e)
            {
                off.y = 0u;
                p.y   = e > 0.0 ? p.y / e : 0.0;
            }
            else
            {
                off.y = 1u;
                p.y   = (p.y - e) / max(1.0 - e, 1e-20);
            }

            pos += off;
        }

        const float2 uv        = (float2(pos) + p) * Constants.ImportanceMetadata.xy;
        const float3 localDir  = RTXPTEnvOctToDirEqualArea(uv);
        const float  avgWeight = t_EnvironmentImportanceMap.SampleLevel(s_EnvironmentImportanceSampler,
                                                                        float2(0.5, 0.5),
                                                                        Constants.ImportanceMetadata.z);
        if (avgWeight <= 0.0)
            return UniformSample(rnd);

        const float importance = t_EnvironmentImportanceMap.Load(int3(pos, 0));

        DistantLightSample result;
        result.Dir = ToWorld(localDir);
        result.Pdf = importance / avgWeight * kEnvMapInvFourPi;
        result.Le  = EvalLocal(localDir, 0.0);
        return result;
    }

    float MIPDescentEvalPdf(float3 worldDir)
    {
        if (Constants.ColorEnabled.w <= 0.0 || !HasImportance())
            return UniformEvalPdf(worldDir);

        const float2 uv         = RTXPTDirToOctEqualArea(ToLocal(worldDir));
        const float  avgWeight  = t_EnvironmentImportanceMap.SampleLevel(s_EnvironmentImportanceSampler,
                                                                         float2(0.5, 0.5),
                                                                         Constants.ImportanceMetadata.z);
        if (avgWeight <= 0.0)
            return UniformEvalPdf(worldDir);

        const float importance = t_EnvironmentImportanceMap.SampleLevel(s_EnvironmentImportanceSampler, uv, 0.0);
        return importance / avgWeight * kEnvMapInvFourPi;
    }
};

EnvMapSampler RTXPTCreateEnvMapSampler(RTXPTEnvMapConstants Constants)
{
    EnvMapSampler sampler;
    sampler.Constants = Constants;
    return sampler;
}

#endif // __ENVMAP_HLSLI__
