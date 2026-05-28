#include "RTXPTSceneBridge.hlsli"

[shader("miss")]
void main(inout RTXPTPathTracerPayload Payload)
{
    const float3 RayDir  = WorldRayDirection();
    const float  T       = saturate(RayDir.y * 0.5 + 0.5);
    const float3 Horizon = float3(0.48, 0.58, 0.68);
    const float3 Zenith  = float3(0.05, 0.08, 0.14);
    float3       Sky     = lerp(Horizon, Zenith, T);

    // Optional sun-disk tint from the first directional light. Type encoding matches
    // LightTypeToShaderValue in RTXPTLights.cpp: 0 = Directional, 1 = Point, 2 = Spot.
    if (Bridge::GetLightCount() > 0)
    {
        const RTXPTLightData L    = Bridge::GetLight(0);
        const float          Type = L.DirectionType.w;
        if (Type < 0.5)
        {
            const float SunDot = saturate(dot(RayDir, -L.DirectionType.xyz));
            const float Disk   = pow(SunDot, 32.0);
            Sky += L.ColorIntensity.rgb * L.ColorIntensity.a * Disk * 0.05;
        }
    }

    Payload.WorldPos    = float3(0.0, 0.0, 0.0);
    Payload.HitDistance = -1.0;
    Payload.WorldNormal = float3(0.0, 1.0, 0.0);
    Payload.HitFlag     = 0u;
    Payload.BaseColor   = float3(0.0, 0.0, 0.0);
    Payload.Emission    = Sky;
}

// TODO(RTXPT-Port Phase 5.5): Replace the placeholder sun disk with environment map / NEE-driven sun sampling once the lighting baker is restored.
