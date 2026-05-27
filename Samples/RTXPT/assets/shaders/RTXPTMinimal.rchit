#include "RTXPTCommon.fxh"

[shader("closesthit")]
void main(inout RTXPTPrimaryPayload Payload,
          in BuiltInTriangleIntersectionAttributes Attributes)
{
    const float3 Barycentrics = float3(1.0 - Attributes.barycentrics.x - Attributes.barycentrics.y,
                                       Attributes.barycentrics.x,
                                       Attributes.barycentrics.y);
    const float InstanceTint = frac(float(InstanceID() * 17 + PrimitiveIndex() * 3) * 0.037);
    const float Depth        = saturate(RayTCurrent() / 150.0);
    const float3 Color       = lerp(Barycentrics, float3(Depth, 1.0 - Depth, InstanceTint), 0.35);
    Payload.ColorDepth       = float4(Color, Depth);
}
