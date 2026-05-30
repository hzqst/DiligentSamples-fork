#ifndef RTXPT_BSDF_HLSLI
#define RTXPT_BSDF_HLSLI

// glTF 2.0 metallic-roughness Cook-Torrance BSDF for the reference path tracer.
// Two lobes: Lambertian diffuse + GGX specular. Evaluation and importance sampling live here so
// raygen only needs base color / metallic / roughness / shading normal from the payload.

#include "Utils/SampleGenerators.hlsli"

static const float RTXPT_PI            = 3.14159265358979323846;
static const float RTXPT_INV_PI        = 0.31830988618379067154;
// Clamp roughness away from a perfect mirror: a zero-roughness GGX lobe is a delta we cannot importance sample.
static const float RTXPT_MIN_ROUGHNESS = 0.045;

// Shading inputs resolved into the form the lobes consume.
struct RTXPTSurface
{
    float3 N;             // shading normal (world space, unit)
    float3 DiffuseAlbedo; // Lambertian albedo (base color scaled by (1 - metallic))
    float3 F0;            // specular reflectance at normal incidence
    float  Alpha;         // GGX alpha = roughness^2
};

RTXPTSurface RTXPTMakeSurface(float3 N, float3 BaseColor, float Metallic, float Roughness)
{
    const float R = clamp(Roughness, RTXPT_MIN_ROUGHNESS, 1.0);

    RTXPTSurface S;
    S.N             = N;
    S.Alpha         = R * R;
    S.DiffuseAlbedo = BaseColor * (1.0 - Metallic);
    S.F0            = lerp(float3(0.04, 0.04, 0.04), BaseColor, Metallic);
    return S;
}

float3 RTXPTFresnelSchlick(float3 F0, float VoH)
{
    const float F = pow(saturate(1.0 - VoH), 5.0);
    return F0 + (1.0 - F0) * F;
}

// Trowbridge-Reitz (GGX) normal distribution function.
float RTXPTDistributionGGX(float NoH, float Alpha)
{
    const float A2 = Alpha * Alpha;
    const float D  = (NoH * NoH) * (A2 - 1.0) + 1.0;
    return A2 / max(RTXPT_PI * D * D, 1e-7);
}

// Smith height-correlated visibility term = G / (4 * NoV * NoL).
float RTXPTVisibilitySmithGGX(float NoV, float NoL, float Alpha)
{
    const float A2 = Alpha * Alpha;
    const float V  = NoL * sqrt(NoV * NoV * (1.0 - A2) + A2);
    const float L  = NoV * sqrt(NoL * NoL * (1.0 - A2) + A2);
    return 0.5 / max(V + L, 1e-7);
}

float RTXPTLuminance(float3 C)
{
    return dot(C, float3(0.2126, 0.7152, 0.0722));
}

float RTXPTPowerHeuristic(float PdfA, float PdfB)
{
    const float A2 = PdfA * PdfA;
    const float B2 = PdfB * PdfB;
    return A2 / max(A2 + B2, 1e-7);
}

float RTXPTSpecularProbability(RTXPTSurface S, float3 Wo)
{
    const float  NoV     = saturate(dot(S.N, Wo));
    const float3 Fapprox = RTXPTFresnelSchlick(S.F0, NoV);
    const float  SpecLum = RTXPTLuminance(Fapprox);
    const float  DiffLum = RTXPTLuminance(S.DiffuseAlbedo * (1.0 - Fapprox));
    return clamp(SpecLum / max(SpecLum + DiffLum, 1e-4), 0.1, 0.9);
}

// Evaluate f(Wo,Wi) * NoL and the single-sample MIS pdf for the given (unit, away-from-surface) directions.
// SpecProb is the probability the sampler used to pick the specular lobe.
void RTXPTEvalBSDF(RTXPTSurface S, float3 Wo, float3 Wi, float SpecProb, out float3 FTimesNoL, out float Pdf)
{
    FTimesNoL = float3(0.0, 0.0, 0.0);
    Pdf       = 0.0;

    const float NoL = dot(S.N, Wi);
    const float NoV = dot(S.N, Wo);
    if (NoL <= 0.0 || NoV <= 0.0)
        return;

    const float3 H   = normalize(Wo + Wi);
    const float  NoH = saturate(dot(S.N, H));
    const float  VoH = saturate(dot(Wo, H));

    const float  D    = RTXPTDistributionGGX(NoH, S.Alpha);
    const float  Vis  = RTXPTVisibilitySmithGGX(NoV, NoL, S.Alpha);
    const float3 F    = RTXPTFresnelSchlick(S.F0, VoH);
    const float3 Spec = D * Vis * F; // Vis already carries 1 / (4 NoV NoL)

    const float3 Diff = S.DiffuseAlbedo * RTXPT_INV_PI * (1.0 - F);

    FTimesNoL = (Diff + Spec) * NoL;

    const float PdfDiffuse  = NoL * RTXPT_INV_PI;
    const float PdfSpecular = D * NoH / max(4.0 * VoH, 1e-7);
    Pdf = SpecProb * PdfSpecular + (1.0 - SpecProb) * PdfDiffuse;
}

// Importance-sample an incident direction Wi. Returns false for invalid samples.
// Weight = f(Wo,Wi) * NoL / pdf is the throughput multiplier the path tracer applies.
bool RTXPTSampleBSDF(RTXPTSurface S, float3 Wo, inout SampleGenerator sg,
                     out float3 Wi, out float3 Weight, out float Pdf)
{
    Wi     = float3(0.0, 0.0, 0.0);
    Weight = float3(0.0, 0.0, 0.0);
    Pdf    = 0.0;

    const float NoV = dot(S.N, Wo);
    if (NoV <= 0.0)
        return false;

    // Pick the lobe from the Fresnel-weighted specular vs diffuse luminance, clamped so neither lobe starves.
    const float SpecProb = RTXPTSpecularProbability(S, Wo);

    float3 Tangent;
    float3 Bitangent;
    BranchlessONB(S.N, Tangent, Bitangent);

    const float2 Rand2 = sampleNext2D(sg);
    const float  Lobe  = sampleNext1D(sg);

    if (Lobe < SpecProb)
    {
        // GGX half-vector (NDF) sampling in the local frame, then reflect Wo about H.
        const float A    = S.Alpha;
        const float Phi  = 2.0 * RTXPT_PI * Rand2.x;
        const float CosT = sqrt((1.0 - Rand2.y) / max(1.0 + (A * A - 1.0) * Rand2.y, 1e-7));
        const float SinT = sqrt(max(0.0, 1.0 - CosT * CosT));

        const float3 HLocal = float3(SinT * cos(Phi), SinT * sin(Phi), CosT);
        const float3 H      = normalize(Tangent * HLocal.x + Bitangent * HLocal.y + S.N * HLocal.z);
        Wi                  = reflect(-Wo, H);
        if (dot(S.N, Wi) <= 0.0)
            return false;
    }
    else
    {
        // Cosine-weighted diffuse hemisphere sample.
        float PdfUnused;
        Wi = sampleCosineHemisphere(Rand2, S.N, PdfUnused);
        if (dot(S.N, Wi) <= 0.0)
            return false;
    }

    float3 FTimesNoL;
    RTXPTEvalBSDF(S, Wo, Wi, SpecProb, FTimesNoL, Pdf);
    if (Pdf <= 0.0)
        return false;

    Weight = FTimesNoL / Pdf;
    return true;
}

#endif // RTXPT_BSDF_HLSLI
