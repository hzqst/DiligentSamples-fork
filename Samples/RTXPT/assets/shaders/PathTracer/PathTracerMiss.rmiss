#include "PathTracerShared.h"
#include "Lighting/EnvMap.hlsli"

[shader("miss")]
void main(inout PathPayload Payload)
{
    Payload.worldPos    = float3(0.0, 0.0, 0.0);
    Payload.hitDistance = -1.0;
    Payload.worldNormal = float3(0.0, 1.0, 0.0);
    Payload.hitFlag     = 0u;
    Payload.baseColor   = float3(0.0, 0.0, 0.0);
    Payload.emission    = EnvMap::Eval(WorldRayDirection());
    Payload.metallic    = 0.0;
    Payload.roughness   = 1.0;
}

// TODO(RTXPT-Port Phase 5.4): Replace the procedural sky with an importance-sampled HDR environment map (EnvMapBaker) and add environment-map MIS.
