#include "RTXPTSceneBridge.hlsli"

[shader("miss")]
void main(inout RTXPTPrimaryPayload Payload)
{
    const float3 RayDir  = WorldRayDirection();
    const float  T       = saturate(RayDir.y * 0.5 + 0.5);
    const float3 Horizon = float3(0.48, 0.58, 0.68);
    const float3 Zenith  = float3(0.05, 0.08, 0.14);
    float3       Sky     = lerp(Horizon, Zenith, T);

    // Light bridge sanity exercise: tint the sky toward the first directional light, if any.
    // This both validates the binding and serves as a placeholder until Phase 5.5 lands a real
    // environment / sun sampler.
    if (Bridge::GetLightCount() > 0)
    {
        const RTXPTLightData L    = Bridge::GetLight(0);
        const float          Type = L.DirectionType.w;
        // Type encoding matches LightTypeToShaderValue in RTXPTLights.cpp: 0=Directional, 1=Point, 2=Spot.
        if (Type >= 0.0 && Type < 0.5)
        {
            const float SunDot = saturate(dot(RayDir, -L.DirectionType.xyz));
            const float Disk   = pow(SunDot, 32.0);
            Sky += L.ColorIntensity.rgb * L.ColorIntensity.a * Disk * 0.05;
        }
    }

    Payload.ColorDepth = float4(Sky, 1.0);
}

// TODO(RTXPT-Port Phase 5.5): Replace the placeholder sun disk with environment map / NEE-driven sun sampling once the lighting baker is restored.
