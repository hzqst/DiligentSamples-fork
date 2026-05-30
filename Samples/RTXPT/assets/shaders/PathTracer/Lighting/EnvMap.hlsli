#ifndef RTXPT_ENVIRONMENT_HLSLI
#define RTXPT_ENVIRONMENT_HLSLI

// Shared procedural sky used by the miss shader and raygen environment NEE.
float3 RTXPTEvalSky(float3 RayDir)
{
    const float  T       = saturate(RayDir.y * 0.5 + 0.5);
    const float3 Horizon = float3(0.48, 0.58, 0.68);
    const float3 Zenith  = float3(0.05, 0.08, 0.14);
    return lerp(Horizon, Zenith, T);
}

#endif // RTXPT_ENVIRONMENT_HLSLI
