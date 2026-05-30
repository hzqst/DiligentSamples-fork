#ifndef __BXDF_HLSLI__
#define __BXDF_HLSLI__

// glTF 2.0 metallic-roughness Cook-Torrance BSDF for the reference path tracer.
// Two lobes: Lambertian diffuse + GGX specular. Evaluation and importance sampling live here so
// raygen only needs base color / metallic / roughness / shading normal from the payload.

#include "Utils/SampleGenerators.hlsli"

static const float K_PI                = 3.14159265358979323846;
static const float K_1_PI              = 0.31830988618379067154;
// Clamp roughness away from a perfect mirror: a zero-roughness GGX lobe is a delta we cannot importance sample.
static const float kMinRoughness       = 0.045;

// Shading inputs resolved into the form the lobes consume.
struct StandardBSDFData
{
    float3 N;        // shading normal (world space, unit)
    float3 diffuse;  // Lambertian albedo (base color scaled by (1 - metallic))
    float3 specular; // specular reflectance at normal incidence
    float  alpha;    // GGX alpha = roughness^2
};

StandardBSDFData MakeStandardBSDFData(float3 N, float3 BaseColor, float Metallic, float Roughness)
{
    const float R = clamp(Roughness, kMinRoughness, 1.0);

    StandardBSDFData bsdfData;
    bsdfData.N        = N;
    bsdfData.alpha    = R * R;
    bsdfData.diffuse  = BaseColor * (1.0 - Metallic);
    bsdfData.specular = lerp(float3(0.04, 0.04, 0.04), BaseColor, Metallic);
    return bsdfData;
}

float3 evalFresnelSchlick(float3 f0, float3 f90, float cosTheta)
{
    const float f = pow(saturate(1.0 - cosTheta), 5.0);
    return f0 + (f90 - f0) * f;
}

// Trowbridge-Reitz (GGX) normal distribution function.
float evalNdfGGX(float alpha, float cosTheta)
{
    const float A2 = alpha * alpha;
    const float D  = (cosTheta * cosTheta) * (A2 - 1.0) + 1.0;
    return A2 / max(K_PI * D * D, 1e-7);
}

// Smith height-correlated visibility term = G / (4 * NoV * NoL).
float evalVisibilitySmithGGXCorrelated(float alpha, float cosThetaO, float cosThetaI)
{
    const float A2 = alpha * alpha;
    const float V  = cosThetaI * sqrt(cosThetaO * cosThetaO * (1.0 - A2) + A2);
    const float L  = cosThetaO * sqrt(cosThetaI * cosThetaI * (1.0 - A2) + A2);
    return 0.5 / max(V + L, 1e-7);
}

float luminance(float3 C)
{
    return dot(C, float3(0.2126, 0.7152, 0.0722));
}

float getSpecularProbability(StandardBSDFData bsdfData, float3 wo)
{
    const float  NdotV   = saturate(dot(bsdfData.N, wo));
    const float3 fApprox = evalFresnelSchlick(bsdfData.specular, float3(1.0, 1.0, 1.0), NdotV);
    const float  specLum = luminance(fApprox);
    const float  diffLum = luminance(bsdfData.diffuse * (1.0 - fApprox));
    return clamp(specLum / max(specLum + diffLum, 1e-4), 0.1, 0.9);
}

// Evaluate f(Wo,Wi) * NoL and the single-sample MIS pdf for the given (unit, away-from-surface) directions.
// specProb is the probability the sampler used to pick the specular lobe.
void EvalBSDF(StandardBSDFData bsdfData, float3 wo, float3 wi, float specProb, out float3 f, out float pdf)
{
    f   = float3(0.0, 0.0, 0.0);
    pdf = 0.0;

    const float NdotL = dot(bsdfData.N, wi);
    const float NdotV = dot(bsdfData.N, wo);
    if (NdotL <= 0.0 || NdotV <= 0.0)
        return;

    const float3 h     = normalize(wo + wi);
    const float  NdotH = saturate(dot(bsdfData.N, h));
    const float  VdotH = saturate(dot(wo, h));

    const float  d       = evalNdfGGX(bsdfData.alpha, NdotH);
    const float  vis     = evalVisibilitySmithGGXCorrelated(bsdfData.alpha, NdotV, NdotL);
    const float3 fresnel = evalFresnelSchlick(bsdfData.specular, float3(1.0, 1.0, 1.0), VdotH);
    const float3 spec    = d * vis * fresnel; // vis already carries 1 / (4 NdotV NdotL)

    const float3 diff = bsdfData.diffuse * K_1_PI * (1.0 - fresnel);

    f = (diff + spec) * NdotL;

    const float pdfDiffuse  = NdotL * K_1_PI;
    const float pdfSpecular = d * NdotH / max(4.0 * VdotH, 1e-7);
    pdf = specProb * pdfSpecular + (1.0 - specProb) * pdfDiffuse;
}

// Importance-sample an incident direction Wi. Returns false for invalid samples.
// Weight = f(Wo,Wi) * NoL / pdf is the throughput multiplier the path tracer applies.
bool SampleBSDF(StandardBSDFData bsdfData, float3 wo, inout SampleGenerator sg,
                out float3 wi, out float3 weight, out float pdf, out float lobeP)
{
    wi     = float3(0.0, 0.0, 0.0);
    weight = float3(0.0, 0.0, 0.0);
    pdf    = 0.0;
    lobeP  = 0.0;

    const float NdotV = dot(bsdfData.N, wo);
    if (NdotV <= 0.0)
        return false;

    // Pick the lobe from the Fresnel-weighted specular vs diffuse luminance, clamped so neither lobe starves.
    const float specProb = getSpecularProbability(bsdfData, wo);

    float3 tangent;
    float3 bitangent;
    BranchlessONB(bsdfData.N, tangent, bitangent);

    const float2 rand2 = sampleNext2D(sg);
    const float  lobe  = sampleNext1D(sg);

    if (lobe < specProb)
    {
        lobeP = specProb;
        // GGX half-vector (NDF) sampling in the local frame, then reflect Wo about H.
        const float a    = bsdfData.alpha;
        const float phi  = 2.0 * K_PI * rand2.x;
        const float cosT = sqrt((1.0 - rand2.y) / max(1.0 + (a * a - 1.0) * rand2.y, 1e-7));
        const float sinT = sqrt(max(0.0, 1.0 - cosT * cosT));

        const float3 hLocal = float3(sinT * cos(phi), sinT * sin(phi), cosT);
        const float3 h      = normalize(tangent * hLocal.x + bitangent * hLocal.y + bsdfData.N * hLocal.z);
        wi                  = reflect(-wo, h);
        if (dot(bsdfData.N, wi) <= 0.0)
            return false;
    }
    else
    {
        lobeP = 1.0 - specProb;
        // Cosine-weighted diffuse hemisphere sample.
        float pdfUnused;
        wi = sampleCosineHemisphere(rand2, bsdfData.N, pdfUnused);
        if (dot(bsdfData.N, wi) <= 0.0)
            return false;
    }

    float3 f;
    EvalBSDF(bsdfData, wo, wi, specProb, f, pdf);
    if (pdf <= 0.0)
        return false;

    weight = f / pdf;
    return true;
}

#endif // __BXDF_HLSLI__
