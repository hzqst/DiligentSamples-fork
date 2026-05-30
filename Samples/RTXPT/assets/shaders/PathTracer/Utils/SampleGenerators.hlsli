#ifndef __SAMPLE_GENERATORS_HLSLI__
#define __SAMPLE_GENERATORS_HLSLI__

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

uint Hash32Combine(uint seed, uint value)
{
    return seed ^ (Hash32(value) + 0x9e3779b9u + (seed << 6) + (seed >> 2));
}

// Map 23 random bits to [0, 1).
float UintToFloat01(uint x)
{
    return asfloat(0x3f800000u | (x & 0x7fffffu)) - 1.0;
}

struct SampleGenerator
{
    uint State;
};

SampleGenerator SampleGenerator_make(uint2 pixelPos, uint frameSeed)
{
    SampleGenerator sg;
    const uint      PixelSeed = (pixelPos.x << 16) | (pixelPos.y & 0xffffu);
    sg.State                  = Hash32Combine(Hash32(frameSeed), PixelSeed);
    return sg;
}

// Per-effect decorrelation salts. Mirror RTXPT-fork's SampleGeneratorEffectSeed
// (D:/RTXPT-fork/Rtxpt/Shaders/PathTracer/Utils/SampleGenerators.hlsli:16).
static const uint kSampleEffect_Base                = 0u;
static const uint kSampleEffect_ScatterBSDF         = 1u;
static const uint kSampleEffect_NextEventEstimation = 2u;
static const uint kSampleEffect_NEELightSampler     = 3u;
static const uint kSampleEffect_RussianRoulette     = 6u;

// Stateless per-(pixel, vertex, sample) seeding (G3): each path vertex + effect draws a decorrelated
// sequence, so bounces and dimensions no longer share one forward hash chain. Mirrors RTXPT-fork's
// UniformSampleSequenceGenerator::make (StatelessSampleGenerators.hlsli:191). Full Sobol/Owen
// low-discrepancy sampling is deferred to Phase R5 (G9).
SampleGenerator SampleGenerator_makeStateless(uint2 pixelPos, uint vertexIndex, uint sampleIndex, uint effectSeed)
{
    SampleGenerator sg;
    const uint      baseHash = Hash32Combine(Hash32(vertexIndex + 0x035F9F29u), (pixelPos.x << 16) | (pixelPos.y & 0xffffu));
    uint            h        = Hash32Combine(baseHash, effectSeed);
    h                        = Hash32Combine(h, sampleIndex);
    sg.State                 = h;
    return sg;
}

float sampleNext1D(inout SampleGenerator sg)
{
    sg.State = Hash32(sg.State);
    return UintToFloat01(sg.State);
}

float2 sampleNext2D(inout SampleGenerator sg)
{
    const float X = sampleNext1D(sg);
    const float Y = sampleNext1D(sg);
    return float2(X, Y);
}

// Build an orthonormal basis (Tangent, Bitangent) from a unit normal. Frisvad 2012.
void BranchlessONB(float3 normal, out float3 tangent, out float3 bitangent)
{
    if (normal.z < -0.9999999)
    {
        tangent   = float3(0.0, -1.0, 0.0);
        bitangent = float3(-1.0, 0.0, 0.0);
        return;
    }
    const float A = 1.0 / (1.0 + normal.z);
    const float B = -normal.x * normal.y * A;
    tangent       = float3(1.0 - normal.x * normal.x * A, B, -normal.x);
    bitangent     = float3(B, 1.0 - normal.y * normal.y * A, -normal.y);
}

// Cosine-weighted hemisphere sample around `Normal`. Returns a unit vector and
// the matching PDF in `Pdf`. PDF for Lambertian sampling is cos(theta) / PI.
float3 sampleCosineHemisphere(float2 rand, float3 normal, out float pdf)
{
    const float R     = sqrt(rand.x);
    const float theta = 6.28318530718 * rand.y;
    const float X     = R * cos(theta);
    const float Y     = R * sin(theta);
    const float Z     = sqrt(max(0.0, 1.0 - rand.x));

    float3 tangent;
    float3 bitangent;
    BranchlessONB(normal, tangent, bitangent);

    pdf = Z * 0.318309886184; // Z / PI
    return normalize(tangent * X + bitangent * Y + normal * Z);
}

#endif // __SAMPLE_GENERATORS_HLSLI__
