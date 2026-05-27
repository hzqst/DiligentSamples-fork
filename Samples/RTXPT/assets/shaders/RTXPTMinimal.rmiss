#include "RTXPTCommon.fxh"

[shader("miss")]
void main(inout RTXPTPrimaryPayload Payload)
{
    const float  T       = saturate(WorldRayDirection().y * 0.5 + 0.5);
    const float3 Horizon = float3(0.48, 0.58, 0.68);
    const float3 Zenith  = float3(0.05, 0.08, 0.14);
    Payload.ColorDepth   = float4(lerp(Horizon, Zenith, T), 1.0);
}
