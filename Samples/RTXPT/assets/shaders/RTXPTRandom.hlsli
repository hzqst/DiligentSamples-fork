#ifndef RTXPT_RANDOM_HLSLI
#define RTXPT_RANDOM_HLSLI

// Lightweight integer hash for PRNG seeding. Same constants used by RTXPT's
// IntroPathTracer (D:/RTXPT-fork/Rtxpt/Shaders/IntroSample/IntroPathTracer.hlsl)
// - chosen for good visual distribution rather than statistical strength.
uint Hash32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint Hash32Combine(uint Seed, uint Value)
{
    return Seed ^ (Hash32(Value) + 0x9e3779b9u + (Seed << 6) + (Seed >> 2));
}

// Map 23 random bits to [0, 1).
float ToFloat0To1(uint x)
{
    return asfloat(0x3f800000u | (x & 0x7fffffu)) - 1.0;
}

struct RTXPTRandom
{
    uint State;
};

RTXPTRandom RTXPTRandom_Init(uint2 PixelPos, uint FrameSeed)
{
    RTXPTRandom Rng;
    const uint  PixelSeed = (PixelPos.x << 16) | (PixelPos.y & 0xffffu);
    Rng.State             = Hash32Combine(Hash32(FrameSeed), PixelSeed);
    return Rng;
}

float NextFloat(inout RTXPTRandom Rng)
{
    Rng.State = Hash32(Rng.State);
    return ToFloat0To1(Rng.State);
}

float2 NextFloat2(inout RTXPTRandom Rng)
{
    const float X = NextFloat(Rng);
    const float Y = NextFloat(Rng);
    return float2(X, Y);
}

// Build an orthonormal basis (Tangent, Bitangent) from a unit normal. Frisvad 2012.
void BuildOrthonormalBasis(float3 Normal, out float3 Tangent, out float3 Bitangent)
{
    if (Normal.z < -0.9999999)
    {
        Tangent   = float3(0.0, -1.0, 0.0);
        Bitangent = float3(-1.0, 0.0, 0.0);
        return;
    }
    const float A = 1.0 / (1.0 + Normal.z);
    const float B = -Normal.x * Normal.y * A;
    Tangent       = float3(1.0 - Normal.x * Normal.x * A, B, -Normal.x);
    Bitangent     = float3(B, 1.0 - Normal.y * Normal.y * A, -Normal.y);
}

// Cosine-weighted hemisphere sample around `Normal`. Returns a unit vector and
// the matching PDF in `Pdf`. PDF for Lambertian sampling is cos(theta) / PI.
float3 SampleCosineHemisphere(float2 Rand, float3 Normal, out float Pdf)
{
    const float R     = sqrt(Rand.x);
    const float Theta = 6.28318530718 * Rand.y;
    const float X     = R * cos(Theta);
    const float Y     = R * sin(Theta);
    const float Z     = sqrt(max(0.0, 1.0 - Rand.x));

    float3 Tangent;
    float3 Bitangent;
    BuildOrthonormalBasis(Normal, Tangent, Bitangent);

    Pdf = Z * 0.318309886184; // Z / PI
    return normalize(Tangent * X + Bitangent * Y + Normal * Z);
}

#endif // RTXPT_RANDOM_HLSLI
