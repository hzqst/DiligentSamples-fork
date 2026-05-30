#ifndef __ENVMAP_HLSLI__
#define __ENVMAP_HLSLI__

// Procedural-sky environment (RTXPT-fork's EnvMap importance-sampled HDR map lands in Phase R4).
namespace EnvMap
{
    float3 Eval(float3 worldDir)
    {
        const float  t       = saturate(worldDir.y * 0.5 + 0.5);
        const float3 horizon = float3(0.48, 0.58, 0.68);
        const float3 zenith  = float3(0.05, 0.08, 0.14);
        return lerp(horizon, zenith, t);
    }
} // namespace EnvMap

#endif // __ENVMAP_HLSLI__
