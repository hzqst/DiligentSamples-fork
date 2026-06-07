#include "Config.h"

#if PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE
#include "PathTracerBridge.hlsli"
#include "Lighting/EnvMap.hlsli"
using ActiveRayPayload = RTXPTMaterialHitPayload;
#else
#    include "PathTracer.hlsli"
#    include "PathState.hlsli"
#    include "PathPayload.hlsli"
using ActiveRayPayload = PathPayload;
#endif

[shader("miss")]
void main(inout ActiveRayPayload Payload)
{
#if PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE
    Payload.worldPos    = float3(0.0, 0.0, 0.0);
    Payload.hitDistance = -1.0;
    Payload.worldNormal = float3(0.0, 1.0, 0.0);
    Payload.hitFlag     = 0u;
    Payload.baseColor   = float3(0.0, 0.0, 0.0);
    EnvMapSampler EnvSampler = RTXPTCreateEnvMapSampler(Bridge::getEnvMapConstants());
    Payload.emission         = EnvSampler.Eval(WorldRayDirection(), 0.0);
    Payload.metallic         = 0.0;
    Payload.roughness        = 1.0;
#else
    PathState path = PathPayload::unpack(Payload);
    PathTracer::WorkingContext workingContext = GetWorkingContext();
    PathTracer::HandleMiss(path, WorldRayOrigin(), WorldRayDirection(), RayTCurrent(), workingContext);
    Payload = PathPayload::pack(path);
#endif
}
