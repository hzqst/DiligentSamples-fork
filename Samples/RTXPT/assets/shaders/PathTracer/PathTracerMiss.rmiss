#ifndef RTXPT_MINIMAL_TRACE_RAY_DIAGNOSTIC
#    define RTXPT_MINIMAL_TRACE_RAY_DIAGNOSTIC 0
#endif

#if RTXPT_MINIMAL_TRACE_RAY_DIAGNOSTIC

struct DiagnosticPayload
{
    float3 color;
    uint   hit;
};

[shader("miss")]
void main(inout DiagnosticPayload Payload)
{
    const float3 rawDirection = WorldRayDirection();
    const float  length2      = dot(rawDirection, rawDirection);
    const bool   badDirection = rawDirection.x != rawDirection.x ||
        rawDirection.y != rawDirection.y ||
        rawDirection.z != rawDirection.z ||
        length2 < 1e-12 ||
        length2 > 1e16;

    const float3 direction = badDirection ? float3(0.0, 0.0, 1.0) : rawDirection * rsqrt(length2);
    const float3 heatmap   = float3(0.10 + 0.70 * (direction.x * 0.5 + 0.5),
                                    0.15 + 0.75 * (direction.y * 0.5 + 0.5),
                                    0.25 + 0.65 * (direction.z * 0.5 + 0.5));

    Payload.hit   = 0u;
    Payload.color = badDirection ? float3(1.0, 0.0, 0.0) : saturate(heatmap);
}

#else

#include "PathTracerBridge.hlsli"
#include "Lighting/EnvMap.hlsli"

[shader("miss")]
void main(inout PathPayload Payload)
{
    Payload.worldPos    = float3(0.0, 0.0, 0.0);
    Payload.hitDistance = -1.0;
    Payload.worldNormal = float3(0.0, 1.0, 0.0);
    Payload.hitFlag     = 0u;
    Payload.baseColor   = float3(0.0, 0.0, 0.0);
    EnvMapSampler EnvSampler = RTXPTCreateEnvMapSampler(Bridge::getEnvMapConstants());
    Payload.emission         = EnvSampler.Eval(WorldRayDirection(), 0.0);
    Payload.metallic         = 0.0;
    Payload.roughness        = 1.0;
}

#endif
