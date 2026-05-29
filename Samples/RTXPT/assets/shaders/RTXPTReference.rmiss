#include "RTXPTShaderShared.hlsli"
#include "RTXPTEnvironment.hlsli"

[shader("miss")]
void main(inout RTXPTPathTracerPayload Payload)
{
    Payload.WorldPos    = float3(0.0, 0.0, 0.0);
    Payload.HitDistance = -1.0;
    Payload.WorldNormal = float3(0.0, 1.0, 0.0);
    Payload.HitFlag     = 0u;
    Payload.BaseColor   = float3(0.0, 0.0, 0.0);
    Payload.Emission    = RTXPTEvalSky(WorldRayDirection());
    Payload.Metallic    = 0.0;
    Payload.Roughness   = 1.0;
}

// TODO(RTXPT-Port Phase 5.4): Replace the procedural sky with an importance-sampled HDR environment map (EnvMapBaker) and add environment-map MIS.
