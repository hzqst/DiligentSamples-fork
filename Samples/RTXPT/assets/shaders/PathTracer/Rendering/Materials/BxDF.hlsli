#ifndef __BXDF_HLSLI__
#define __BXDF_HLSLI__

// glTF 2.0 metallic-roughness Cook-Torrance BSDF for the reference path tracer.
// Diffuse and GGX lobes are evaluated here so raygen only needs base color,
// metallic, roughness, and shading normal from the payload.

#include "../../Utils/SampleGenerators.hlsli"

static const float K_PI   = 3.14159265358979323846;
static const float K_1_PI = 0.31830988618379067154;

static const float kMinCosTheta = 1e-6;
static const float kMinGGXAlpha = 0.0064;

static const uint kBSDFLobeDiffuseReflection  = 0x01u;
static const uint kBSDFLobeSpecularReflection = 0x02u;
static const uint kBSDFLobeDeltaReflection    = 0x04u;
static const uint kBSDFLobeDelta              = kBSDFLobeDeltaReflection;

// Shading inputs resolved into the form the lobes consume.
struct StandardBSDFData
{
    float3 N;
    float3 diffuse;
    float3 specular;
    float  roughness;
    float  alpha;
};

StandardBSDFData MakeStandardBSDFData(float3 N, float3 baseColor, float metallic, float roughness)
{
    const float r     = saturate(roughness);
    const float alpha = r * r;

    StandardBSDFData bsdfData;
    bsdfData.N         = N;
    bsdfData.roughness = r;
    bsdfData.alpha     = (alpha < kMinGGXAlpha) ? 0.0 : max(alpha, kMinGGXAlpha);
    bsdfData.diffuse   = baseColor * (1.0 - metallic);
    bsdfData.specular  = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    return bsdfData;
}

float3 evalFresnelSchlick(float3 f0, float3 f90, float cosTheta)
{
    const float f = pow(saturate(1.0 - cosTheta), 5.0);
    return f0 + (f90 - f0) * f;
}

float evalFresnelSchlick(float f0, float f90, float cosTheta)
{
    const float f = pow(saturate(1.0 - cosTheta), 5.0);
    return f0 + (f90 - f0) * f;
}

// Trowbridge-Reitz (GGX) normal distribution function.
float evalNdfGGX(float alpha, float cosTheta)
{
    const float A2 = alpha * alpha;
    const float D  = (cosTheta * cosTheta) * (A2 - 1.0) + 1.0;
    return A2 / (K_PI * D * D);
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

float3 evalDiffuseFrostbiteWeight(float3 albedo, float roughness, float3 wi, float3 wo)
{
    const float3 h            = normalize(wi + wo);
    const float  woDotH       = saturate(dot(wo, h));
    const float  energyBias   = lerp(0.0, 0.5, roughness);
    const float  energyFactor = lerp(1.0, 1.0 / 1.51, roughness);
    const float  fd90         = energyBias + 2.0 * woDotH * woDotH * roughness;
    const float  wiScatter    = evalFresnelSchlick(1.0, fd90, wi.z);
    const float  woScatter    = evalFresnelSchlick(1.0, fd90, wo.z);
    return albedo * wiScatter * woScatter * energyFactor;
}

float EmsApprox(float r2, float NdV)
{
    const float r4  = r2 * r2;
    const float nv0 = 0.2 * r2;
    const float nv1 = 0.32 * r2 + 1.94 * r4;
    return lerp(nv0, nv1, NdV);
}

float3 MultiScatterSpecularApprox(float alpha, float NdV, float3 F0)
{
    return 1.0 + F0 * EmsApprox(alpha, NdV);
}

float evalPdfGGX_BVNDF(float alphaValue, float3 viewLocal, float3 hLocal)
{
    const float2 alpha = alphaValue.xx;
    const float  ndf   = evalNdfGGX(alphaValue, hLocal.z);
    const float2 ai    = alpha * viewLocal.xy;
    const float  len2  = dot(ai, ai);
    const float  t     = sqrt(len2 + viewLocal.z * viewLocal.z);
    const float  a     = saturate(min(alpha.x, alpha.y));
    const float  s     = 1.0 + length(viewLocal.xy);
    const float  a2    = a * a;
    const float  s2    = s * s;
    const float  k     = (1.0 - a2) * s2 / max(s2 + a2 * viewLocal.z * viewLocal.z, 1e-7);
    return ndf / max(2.0 * (k * viewLocal.z + t), 1e-7);
}

float3 sampleGGX_BVNDF(float alphaValue, float3 viewLocal, float2 rand)
{
    const float2 alpha    = alphaValue.xx;
    const float3 iStd     = normalize(float3(viewLocal.xy * alpha, viewLocal.z));
    const float  phi      = 2.0 * K_PI * rand.x;
    const float  a        = saturate(min(alpha.x, alpha.y));
    const float  s        = 1.0 + length(viewLocal.xy);
    const float  a2       = a * a;
    const float  s2       = s * s;
    const float  k        = (1.0 - a2) * s2 / max(s2 + a2 * viewLocal.z * viewLocal.z, 1e-7);
    const float  b        = (viewLocal.z > 0.0) ? k * iStd.z : iStd.z;
    const float  z        = mad(1.0 - rand.y, 1.0 + b, -b);
    const float  sinTheta = sqrt(saturate(1.0 - z * z));
    const float3 oStd     = float3(sinTheta * cos(phi), sinTheta * sin(phi), z);
    const float3 mStd     = iStd + oStd;
    return normalize(float3(mStd.xy * alpha, mStd.z));
}

// Evaluate f(Wo,Wi) * NoL and the single-sample MIS pdf for the given unit directions.
// specProb is the probability the sampler used to pick the specular lobe.
void EvalBSDF(StandardBSDFData bsdfData, float3 wo, float3 wi, float specProb, out float3 f, out float pdf)
{
    f   = float3(0.0, 0.0, 0.0);
    pdf = 0.0;

    const float NdotL = dot(bsdfData.N, wi);
    const float NdotV = dot(bsdfData.N, wo);
    if (min(NdotL, NdotV) < kMinCosTheta)
        return;

    float3 tangent;
    float3 bitangent;
    BranchlessONB(bsdfData.N, tangent, bitangent);

    const float3 woLocal = float3(dot(wo, tangent), dot(wo, bitangent), NdotV);
    const float3 wiLocal = float3(dot(wi, tangent), dot(wi, bitangent), NdotL);
    const float3 hLocal  = normalize(woLocal + wiLocal);
    const float  VdotH   = saturate(dot(woLocal, hLocal));

    float3 spec = float3(0.0, 0.0, 0.0);
    if (bsdfData.alpha != 0.0)
    {
        const float  d          = evalNdfGGX(bsdfData.alpha, hLocal.z);
        const float  vis        = evalVisibilitySmithGGXCorrelated(bsdfData.alpha, NdotV, NdotL);
        const float3 fresnel    = evalFresnelSchlick(bsdfData.specular, float3(1.0, 1.0, 1.0), VdotH);
        const float3 msSpecular = MultiScatterSpecularApprox(bsdfData.alpha, NdotV, bsdfData.specular);
        spec                    = d * vis * fresnel * msSpecular;
    }

    const float3 diff = evalDiffuseFrostbiteWeight(bsdfData.diffuse, bsdfData.roughness, wiLocal, woLocal) * K_1_PI;

    f = (diff + spec) * NdotL;

    const float pdfDiffuse  = NdotL * K_1_PI;
    const float pdfSpecular = (bsdfData.alpha == 0.0) ? 0.0 : evalPdfGGX_BVNDF(bsdfData.alpha, woLocal, hLocal);
    pdf                    = specProb * pdfSpecular + (1.0 - specProb) * pdfDiffuse;
}

// Importance-sample an incident direction Wi. preGeneratedSample.xy samples the chosen lobe,
// and preGeneratedSample.z selects between diffuse and specular reflection.
bool SampleBSDF(StandardBSDFData bsdfData, float3 wo, float3 preGeneratedSample,
                out float3 wi, out float3 weight, out float pdf, out uint lobe, out float lobeP)
{
    wi     = float3(0.0, 0.0, 0.0);
    weight = float3(0.0, 0.0, 0.0);
    pdf    = 0.0;
    lobe   = 0u;
    lobeP  = 0.0;

    const float NdotV = dot(bsdfData.N, wo);
    if (NdotV < kMinCosTheta)
        return false;

    const float specProb = getSpecularProbability(bsdfData, wo);

    if (preGeneratedSample.z < specProb)
    {
        lobeP = specProb;

        if (bsdfData.alpha == 0.0)
        {
            wi     = reflect(-wo, bsdfData.N);
            weight = evalFresnelSchlick(bsdfData.specular, float3(1.0, 1.0, 1.0), NdotV) / specProb;
            // Delta events use pdf == 0 as a sentinel; weight includes specProb selection compensation.
            pdf    = 0.0;
            lobe   = kBSDFLobeDeltaReflection;
            return true;
        }

        float3 tangent;
        float3 bitangent;
        BranchlessONB(bsdfData.N, tangent, bitangent);

        const float3 woLocal = float3(dot(wo, tangent), dot(wo, bitangent), NdotV);
        const float3 hLocal  = sampleGGX_BVNDF(bsdfData.alpha, woLocal, preGeneratedSample.xy);
        const float3 h       = normalize(tangent * hLocal.x + bitangent * hLocal.y + bsdfData.N * hLocal.z);
        wi                   = reflect(-wo, h);
        lobe                 = kBSDFLobeSpecularReflection;
    }
    else
    {
        lobeP = 1.0 - specProb;
        float pdfUnused;
        wi   = sampleCosineHemisphere(preGeneratedSample.xy, bsdfData.N, pdfUnused);
        lobe = kBSDFLobeDiffuseReflection;
    }

    if (dot(bsdfData.N, wi) < kMinCosTheta)
        return false;

    float3 f;
    EvalBSDF(bsdfData, wo, wi, specProb, f, pdf);
    if (pdf <= 0.0)
        return false;

    weight = f / pdf;
    return true;
}

#endif // __BXDF_HLSLI__
