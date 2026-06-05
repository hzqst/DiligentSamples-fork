#ifndef __BXDF_HLSLI__
#define __BXDF_HLSLI__

// glTF 2.0 metallic-roughness Cook-Torrance BSDF for the reference path tracer.
// Diffuse and GGX lobes are evaluated here so raygen only needs base color,
// metallic, roughness, and shading normal from the payload.

#include "../../Utils/SampleGenerators.hlsli"
#include "../../Scene/Material/MaterialData.hlsli"
#include "LobeType.hlsli"

static const float K_PI   = 3.14159265358979323846;
static const float K_1_PI = 0.31830988618379067154;

static const float kMinCosTheta = 1e-6;
static const float kMinGGXAlpha = 0.0064;

static const uint kBSDFLobeDiffuseReflection    = kLobeTypeDiffuseReflection;
static const uint kBSDFLobeSpecularReflection   = kLobeTypeSpecularReflection;
static const uint kBSDFLobeDeltaReflection      = kLobeTypeDeltaReflection;
static const uint kBSDFLobeDiffuseTransmission  = kLobeTypeDiffuseTransmission;
static const uint kBSDFLobeSpecularTransmission = kLobeTypeSpecularTransmission;
static const uint kBSDFLobeDeltaTransmission    = kLobeTypeDeltaTransmission;
static const uint kBSDFLobeDelta                = kLobeTypeDelta;
static const uint kBSDFLobeTransmission         = kLobeTypeTransmission;

// Shading inputs resolved into the form the lobes consume.
struct StandardBSDFData
{
    float3 N;
    float3 diffuse;
    float3 specular;
    float3 transmission;
    float  roughness;
    float  alpha;
    float  eta;
    float  metallic;
    float  diffuseTransmission;
    float  specularTransmission;
    bool   thinSurface;
    uint   activeLobes;
    uint   psdExclude;
    uint   psdBlockMotionVectorsAtSurface;
    uint   psdDominantDeltaLobeP1;

    float3 Diffuse() { return diffuse; }
    float3 Specular() { return specular; }
    float3 Transmission() { return transmission; }
    float  Roughness() { return roughness; }
    float  Metallic() { return metallic; }
    float  Eta() { return eta; }
    float  DiffuseTransmission() { return diffuseTransmission; }
    float  SpecularTransmission() { return specularTransmission; }

    void SetEta(float value)
    {
        eta = value;
    }

    void SetRoughness(float value)
    {
        roughness            = saturate(value);
        const float newAlpha = roughness * roughness;
        alpha                = (newAlpha < kMinGGXAlpha) ? 0.0 : max(newAlpha, kMinGGXAlpha);
    }
};

MaterialHeader MakeMaterialHeader(StandardBSDFData bsdfData)
{
    MaterialHeader header = MaterialHeader::make();
    header.setActiveLobes(bsdfData.activeLobes == 0u ? kLobeTypeAll : bsdfData.activeLobes);
    header.setThinSurface(bsdfData.thinSurface);
    header.setPSDExclude(bsdfData.psdExclude != 0u);
    header.setPSDBlockMotionVectorsAtSurface(bsdfData.psdBlockMotionVectorsAtSurface != 0u);
    header.setPSDDominantDeltaLobeP1(bsdfData.psdDominantDeltaLobeP1);
    return header;
}

StandardBSDFData MakeStandardBSDFData(float3 N,
                                      float3 baseColor,
                                      float  metallic,
                                      float  roughness,
                                      float  materialIoR,
                                      float  outsideIoR,
                                      float  transmissionFactor,
                                      float  diffuseTransmissionFactor,
                                      bool   thinSurface,
                                      bool   frontFacing,
                                      uint   activeLobes,
                                      bool   psdExclude,
                                      bool   psdBlockMotionVectorsAtSurface,
                                      uint   psdDominantDeltaLobeP1)
{
    const float r               = saturate(roughness);
    const float m               = saturate(metallic);
    const float safeMaterialIoR = max(materialIoR, 1.0);
    const float safeOutsideIoR  = max(outsideIoR, 1.0);
    const float f0Sqrt          = (safeMaterialIoR - 1.0) / max(safeMaterialIoR + 1.0, 1e-4);
    const float dielectric      = f0Sqrt * f0Sqrt;
    const float alpha           = r * r;

    StandardBSDFData bsdfData;
    bsdfData.N                              = N;
    bsdfData.diffuse                        = baseColor * (1.0 - m);
    bsdfData.specular                       = lerp(float3(dielectric, dielectric, dielectric), baseColor, m);
    bsdfData.transmission                   = baseColor;
    bsdfData.roughness                      = r;
    bsdfData.alpha                          = (alpha < kMinGGXAlpha) ? 0.0 : max(alpha, kMinGGXAlpha);
    bsdfData.eta                            = frontFacing ? safeOutsideIoR / safeMaterialIoR : safeMaterialIoR / safeOutsideIoR;
    bsdfData.metallic                       = m;
    bsdfData.diffuseTransmission            = saturate(diffuseTransmissionFactor) * (1.0 - m);
    bsdfData.specularTransmission           = saturate(transmissionFactor) * (1.0 - m);
    bsdfData.thinSurface                    = thinSurface;
    bsdfData.activeLobes                    = activeLobes == 0u ? kLobeTypeAll : activeLobes;
    bsdfData.psdExclude                     = psdExclude ? 1u : 0u;
    bsdfData.psdBlockMotionVectorsAtSurface = psdBlockMotionVectorsAtSurface ? 1u : 0u;
    bsdfData.psdDominantDeltaLobeP1         = psdDominantDeltaLobeP1;
    return bsdfData;
}

StandardBSDFData MakeStandardBSDFData(float3 N,
                                      float3 baseColor,
                                      float  metallic,
                                      float  roughness,
                                      float  materialIoR,
                                      float  outsideIoR,
                                      float  transmissionFactor,
                                      float  diffuseTransmissionFactor,
                                      bool   thinSurface,
                                      bool   frontFacing)
{
    return MakeStandardBSDFData(N,
                                baseColor,
                                metallic,
                                roughness,
                                materialIoR,
                                outsideIoR,
                                transmissionFactor,
                                diffuseTransmissionFactor,
                                thinSurface,
                                frontFacing,
                                kLobeTypeAll,
                                false,
                                false,
                                0u);
}

StandardBSDFData MakeStandardBSDFData(float3 N, float3 baseColor, float metallic, float roughness)
{
    return MakeStandardBSDFData(N, baseColor, metallic, roughness, 1.5, 1.0, 0.0, 0.0, true, true);
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

float evalFresnelDielectric(float eta, float cosThetaI, out float cosThetaT)
{
    eta       = max(eta, 1e-7);
    cosThetaI = clamp(cosThetaI, -1.0, 1.0);
    if (cosThetaI < 0.0)
    {
        eta       = 1.0 / eta;
        cosThetaI = -cosThetaI;
    }

    const float sin2ThetaT = eta * eta * max(0.0, 1.0 - cosThetaI * cosThetaI);
    if (sin2ThetaT > 1.0)
    {
        cosThetaT = 0.0;
        return 1.0;
    }

    cosThetaT       = sqrt(max(0.0, 1.0 - sin2ThetaT));
    const float Rs  = (eta * cosThetaI - cosThetaT) / max(eta * cosThetaI + cosThetaT, 1e-7);
    const float Rp  = (eta * cosThetaT - cosThetaI) / max(eta * cosThetaT + cosThetaI, 1e-7);
    return saturate(0.5 * (Rs * Rs + Rp * Rp));
}

float evalFresnelDielectric(float eta, float cosThetaI)
{
    float cosThetaT;
    return evalFresnelDielectric(eta, cosThetaI, cosThetaT);
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

struct BSDFLobeProbabilities
{
    float pDiffuseReflection;
    float pDiffuseTransmission;
    float pSpecularReflection;
    float pSpecularTransmission;
};

BSDFLobeProbabilities GetBSDFLobeProbabilities(StandardBSDFData bsdfData, float3 wo)
{
    const float specTrans     = saturate(bsdfData.specularTransmission);
    const float diffTrans     = saturate(bsdfData.diffuseTransmission);
    const float metallicBRDF  = saturate(bsdfData.metallic) * (1.0 - specTrans);
    const float dielectricBSDF = (1.0 - saturate(bsdfData.metallic)) * (1.0 - specTrans);
    const float diffuseWeight = luminance(bsdfData.diffuse);
    const float specWeight    = luminance(evalFresnelSchlick(bsdfData.specular, float3(1.0, 1.0, 1.0), saturate(dot(bsdfData.N, wo))));

    BSDFLobeProbabilities p;
    p.pDiffuseReflection    = diffuseWeight * dielectricBSDF * (1.0 - diffTrans);
    p.pDiffuseTransmission  = diffuseWeight * dielectricBSDF * diffTrans;
    p.pSpecularReflection   = specWeight * (metallicBRDF + dielectricBSDF);
    p.pSpecularTransmission = specTrans;

    const float norm = p.pDiffuseReflection + p.pDiffuseTransmission + p.pSpecularReflection + p.pSpecularTransmission;
    if (norm > 0.0)
    {
        const float invNorm = 1.0 / norm;
        p.pDiffuseReflection *= invNorm;
        p.pDiffuseTransmission *= invNorm;
        p.pSpecularReflection *= invNorm;
        p.pSpecularTransmission *= invNorm;
    }

    return p;
}

float getSpecularProbability(StandardBSDFData bsdfData, float3 wo)
{
    BSDFLobeProbabilities p = GetBSDFLobeProbabilities(bsdfData, wo);
    return p.pSpecularReflection + p.pSpecularTransmission;
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

float3 sampleCosineHemisphereLocal(float2 rand, out float pdf)
{
    const float R     = sqrt(rand.x);
    const float theta = 2.0 * K_PI * rand.y;
    const float x     = R * cos(theta);
    const float y     = R * sin(theta);
    const float z     = sqrt(max(0.0, 1.0 - rand.x));
    pdf               = z * K_1_PI;
    return float3(x, y, z);
}

float3 GetTransmissionAlbedo(StandardBSDFData bsdfData)
{
    return bsdfData.thinSurface ? bsdfData.transmission : sqrt(max(bsdfData.transmission, float3(0.0, 0.0, 0.0)));
}

struct DiffuseReflectionFrostbite
{
    float3 albedo;
    float  roughness;

    float3 eval(const float3 wi, const float3 wo)
    {
        if (min(wi.z, wo.z) < kMinCosTheta)
            return float3(0.0, 0.0, 0.0);

        return evalWeight(wi, wo) * K_1_PI * wo.z;
    }

    bool sample(const float3 wi, out float3 wo, out float pdf, out float3 weight, out uint lobe, out float lobeP, float3 preGeneratedSample)
    {
        wo   = sampleCosineHemisphereLocal(preGeneratedSample.xy, pdf);
        lobe = kLobeTypeDiffuseReflection;

        if (min(wi.z, wo.z) < kMinCosTheta)
        {
            weight = float3(0.0, 0.0, 0.0);
            lobeP  = 0.0;
            return false;
        }

        weight = evalWeight(wi, wo);
        lobeP  = 1.0;
        return true;
    }

    float evalPdf(const float3 wi, const float3 wo)
    {
        if (min(wi.z, wo.z) < kMinCosTheta)
            return 0.0;

        return K_1_PI * wo.z;
    }

    float3 evalWeight(float3 wi, float3 wo)
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
};

struct DiffuseTransmissionLambert
{
    float3 albedo;

    float3 eval(const float3 wi, const float3 wo)
    {
        if (min(wi.z, -wo.z) < kMinCosTheta)
            return float3(0.0, 0.0, 0.0);

        return K_1_PI * albedo * -wo.z;
    }

    bool sample(const float3 wi, out float3 wo, out float pdf, out float3 weight, out uint lobe, out float lobeP, float3 preGeneratedSample)
    {
        wo   = sampleCosineHemisphereLocal(preGeneratedSample.xy, pdf);
        wo.z = -wo.z;
        lobe = kLobeTypeDiffuseTransmission;

        if (min(wi.z, -wo.z) < kMinCosTheta)
        {
            weight = float3(0.0, 0.0, 0.0);
            lobeP  = 0.0;
            return false;
        }

        weight = albedo;
        lobeP  = 1.0;
        return true;
    }

    float evalPdf(const float3 wi, const float3 wo)
    {
        if (min(wi.z, -wo.z) < kMinCosTheta)
            return 0.0;

        return K_1_PI * -wo.z;
    }
};

bool IsFiniteVector(float3 v)
{
    return all(v == v) && all(abs(v) < float3(3.402823466e+38, 3.402823466e+38, 3.402823466e+38));
}

bool IsFiniteScalar(float v)
{
    return (v == v) && (abs(v) < 3.402823466e+38);
}

struct SpecularReflectionMicrofacet
{
    float3 albedo;
    float  alpha;
    uint   activeLobes;

    bool hasLobe(uint lobe)
    {
        return (activeLobes & lobe) != 0u;
    }

    float3 eval(const float3 wi, const float3 wo)
    {
        if (alpha == 0.0 || min(wi.z, wo.z) < kMinCosTheta || !hasLobe(kLobeTypeSpecularReflection))
            return float3(0.0, 0.0, 0.0);

        const float3 hLocal = normalize(wi + wo);
        const float  wiDotH = saturate(dot(wi, hLocal));
        const float  d      = evalNdfGGX(alpha, hLocal.z);
        const float  vis    = evalVisibilitySmithGGXCorrelated(alpha, wi.z, wo.z);
        const float3 fresnel = evalFresnelSchlick(albedo, float3(1.0, 1.0, 1.0), wiDotH);
        const float3 msSpecular = MultiScatterSpecularApprox(alpha, wi.z, albedo);
        return d * vis * fresnel * msSpecular * wo.z;
    }

    bool sample(const float3 wi, out float3 wo, out float pdf, out float3 weight, out uint lobe, out float lobeP, float3 preGeneratedSample)
    {
        wo     = float3(0.0, 0.0, 0.0);
        pdf    = 0.0;
        weight = float3(0.0, 0.0, 0.0);
        lobe   = kLobeTypeSpecularReflection;
        lobeP  = 1.0;

        if (wi.z < kMinCosTheta)
            return false;

        if (alpha == 0.0)
        {
            if (!hasLobe(kLobeTypeDeltaReflection))
                return false;

            wo     = float3(-wi.x, -wi.y, wi.z);
            weight = evalFresnelSchlick(albedo, float3(1.0, 1.0, 1.0), wi.z);
            lobe   = kLobeTypeDeltaReflection;
            return IsFiniteVector(wo);
        }

        if (!hasLobe(kLobeTypeSpecularReflection))
            return false;

        const float3 hLocal = sampleGGX_BVNDF(alpha, wi, preGeneratedSample.xy);
        const float  wiDotH = dot(wi, hLocal);
        if (wiDotH <= 0.0)
            return false;

        wo = 2.0 * wiDotH * hLocal - wi;
        if (wo.z < kMinCosTheta)
            return false;

        pdf = evalPdf(wi, wo);
        if (!IsFiniteScalar(pdf) || pdf <= 0.0)
            return false;

        weight = eval(wi, wo) / pdf;
        lobe   = kLobeTypeSpecularReflection;
        return IsFiniteVector(weight);
    }

    float evalPdf(const float3 wi, const float3 wo)
    {
        if (alpha == 0.0 || min(wi.z, wo.z) < kMinCosTheta || !hasLobe(kLobeTypeSpecularReflection))
            return 0.0;

        const float3 hLocal = normalize(wi + wo);
        return evalPdfGGX_BVNDF(alpha, wi, hLocal);
    }
};

struct SpecularReflectionTransmissionMicrofacet
{
    float3 transmissionAlbedo;
    float  alpha;
    float  eta;
    uint   activeLobes;
    bool   isThinSurface;

    bool hasLobe(uint lobe)
    {
        return (activeLobes & lobe) != 0u;
    }

    float3 eval(const float3 wi, const float3 wo)
    {
        if (alpha == 0.0 || wi.z < kMinCosTheta || abs(wo.z) < kMinCosTheta)
            return float3(0.0, 0.0, 0.0);

        const bool isReflection = wo.z > 0.0;
        if ((isReflection && !hasLobe(kLobeTypeSpecularReflection)) ||
            (!isReflection && !hasLobe(kLobeTypeSpecularTransmission)))
            return float3(0.0, 0.0, 0.0);

        const float actualEta = (isThinSurface && !isReflection) ? 1.0 : eta;
        if (!isReflection && abs(actualEta - 1.0) <= 1e-6)
            return float3(0.0, 0.0, 0.0);

        const float3 hUnnormalized = wo + wi * (isReflection ? 1.0 : actualEta);
        const float  hLen2         = dot(hUnnormalized, hUnnormalized);
        if (!IsFiniteScalar(hLen2) || hLen2 <= 1e-14)
            return float3(0.0, 0.0, 0.0);

        float3 hLocal = hUnnormalized * rsqrt(hLen2);
        if (hLocal.z < 0.0)
            hLocal = -hLocal;

        const float viewDotH  = dot(wi, hLocal);
        const float lightDotH = dot(wo, hLocal);
        const float d         = evalNdfGGX(alpha, hLocal.z);
        const float vis       = evalVisibilitySmithGGXCorrelated(alpha, wi.z, abs(wo.z));
        const float g         = vis * 4.0 * wi.z * abs(wo.z);
        const float fresnel   = evalFresnelDielectric(actualEta, viewDotH);

        if (isReflection)
            return float3(fresnel, fresnel, fresnel) * d * g * 0.25 / max(wi.z, kMinCosTheta);

        const float sqrtDenom = lightDotH + actualEta * viewDotH;
        const float t         = actualEta * actualEta * viewDotH * lightDotH /
            max(wi.z * sqrtDenom * sqrtDenom, 1e-7);
        return transmissionAlbedo * (1.0 - fresnel) * d * g * abs(t);
    }

    bool sample(const float3 wi, out float3 wo, out float pdf, out float3 weight, out uint lobe, out float lobeP, float3 preGeneratedSample)
    {
        wo     = float3(0.0, 0.0, 0.0);
        pdf    = 0.0;
        weight = float3(0.0, 0.0, 0.0);
        lobe   = kLobeTypeSpecularReflection;
        lobeP  = 1.0;

        if (wi.z < kMinCosTheta)
            return false;

        const bool hasRoughReflection    = hasLobe(kLobeTypeSpecularReflection);
        const bool hasDeltaReflection    = hasLobe(kLobeTypeDeltaReflection);
        const bool hasRoughTransmission  = hasLobe(kLobeTypeSpecularTransmission);
        const bool hasDeltaTransmission  = hasLobe(kLobeTypeDeltaTransmission);
        const bool canReflect            = alpha == 0.0 ? hasDeltaReflection : hasRoughReflection;
        const bool canTransmit           = alpha == 0.0 ? hasDeltaTransmission : (hasRoughTransmission || hasDeltaTransmission);
        if (!(canReflect || canTransmit))
            return false;

        const bool   delta   = alpha == 0.0;
        const float3 hLocal  = delta ? float3(0.0, 0.0, 1.0) : sampleGGX_BVNDF(alpha, wi, preGeneratedSample.xy);
        const float  wiDotH  = delta ? wi.z : dot(wi, hLocal);
        float        cosThetaT;
        float        fresnel = evalFresnelDielectric(eta, wiDotH, cosThetaT);

        bool isReflection = canReflect;
        if (canReflect && canTransmit)
            isReflection = preGeneratedSample.z < fresnel;
        else if (canTransmit && fresnel == 1.0)
            return false;

        float actualEta = eta;
        if (isThinSurface && !isReflection)
        {
            actualEta = 1.0;
            fresnel   = evalFresnelDielectric(actualEta, wi.z, cosThetaT);
        }

        const bool deltaEvent = delta || (!isReflection && abs(actualEta - 1.0) <= 1e-6);
        if (deltaEvent && !isReflection)
        {
            if (!hasDeltaTransmission)
                return false;
            fresnel = evalFresnelDielectric(actualEta, wi.z, cosThetaT);
        }

        if (isReflection)
        {
            if ((deltaEvent && !hasDeltaReflection) || (!deltaEvent && !hasRoughReflection))
                return false;
        }
        else
        {
            if ((deltaEvent && !hasDeltaTransmission) || (!deltaEvent && !hasRoughTransmission))
                return false;
        }

        if (isReflection)
            wo = deltaEvent ? float3(-wi.x, -wi.y, wi.z) : 2.0 * wiDotH * hLocal - wi;
        else
            wo = deltaEvent ? float3(-wi.x * actualEta, -wi.y * actualEta, -cosThetaT) :
                (actualEta * wiDotH - cosThetaT) * hLocal - actualEta * wi;

        if (abs(wo.z) < kMinCosTheta || ((wo.z > 0.0) != isReflection))
            return false;

        lobeP = isReflection ? fresnel : (1.0 - fresnel);
        lobe  = isReflection ?
            (deltaEvent ? kLobeTypeDeltaReflection : kLobeTypeSpecularReflection) :
            (deltaEvent ? kLobeTypeDeltaTransmission : kLobeTypeSpecularTransmission);

        if (deltaEvent)
        {
            weight = isReflection ? float3(1.0, 1.0, 1.0) : transmissionAlbedo;
            pdf    = 0.0;
            return IsFiniteVector(wo);
        }

        pdf = evalPdf(wi, wo);
        if (!IsFiniteScalar(pdf) || pdf <= 0.0)
            return false;

        weight = eval(wi, wo) / pdf;
        return IsFiniteVector(weight);
    }

    float evalPdf(const float3 wi, const float3 wo)
    {
        if (alpha == 0.0 || wi.z < kMinCosTheta || abs(wo.z) < kMinCosTheta)
            return 0.0;

        const bool isReflection = wo.z > 0.0;
        const bool hasReflection = hasLobe(kLobeTypeSpecularReflection);
        const bool hasTransmission = hasLobe(kLobeTypeSpecularTransmission);
        if ((isReflection && !hasReflection) || (!isReflection && !hasTransmission))
            return 0.0;

        const float actualEta = (isThinSurface && !isReflection) ? 1.0 : eta;
        if (!isReflection && abs(actualEta - 1.0) <= 1e-6)
            return 0.0;

        const float3 hUnnormalized = wo + wi * (isReflection ? 1.0 : actualEta);
        const float  hLen2         = dot(hUnnormalized, hUnnormalized);
        if (!IsFiniteScalar(hLen2) || hLen2 <= 1e-14)
            return 0.0;

        float3 hLocal = hUnnormalized * rsqrt(hLen2);
        if (hLocal.z < 0.0)
            hLocal = -hLocal;

        const float viewDotH  = dot(wi, hLocal);
        const float lightDotH = dot(wo, hLocal);
        float       pdf       = evalPdfGGX_BVNDF(alpha, wi, hLocal);

        if (isReflection)
        {
            if (lightDotH <= 0.0)
                return 0.0;
            pdf *= viewDotH / max(lightDotH, 1e-7);
        }
        else
        {
            if (lightDotH > 0.0)
                return 0.0;
            const float sqrtDenom = lightDotH + actualEta * viewDotH;
            pdf *= viewDotH * 4.0 * abs(lightDotH) / max(sqrtDenom * sqrtDenom, 1e-7);
        }

        if (hasReflection && hasTransmission)
        {
            const float fresnel = evalFresnelDielectric(actualEta, viewDotH);
            pdf *= isReflection ? fresnel : (1.0 - fresnel);
        }

        if (!IsFiniteScalar(pdf))
            return 0.0;

        return clamp(pdf, 0.0, 3.402823466e+38);
    }
};

// Evaluate f(Wo,Wi) * NoL and the single-sample MIS pdf for the given unit directions.
// specProb is the probability the sampler used to pick the specular lobe.
void EvalBSDF(StandardBSDFData bsdfData, float3 wo, float3 wi, float specProb, out float3 f, out float pdf)
{
    f   = float3(0.0, 0.0, 0.0);
    pdf = 0.0;

    const float NdotV = dot(bsdfData.N, wo);
    if (NdotV < kMinCosTheta)
        return;

    float3 tangent;
    float3 bitangent;
    BranchlessONB(bsdfData.N, tangent, bitangent);

    const float NdotL  = dot(bsdfData.N, wi);
    const float3 woLocal = float3(dot(wo, tangent), dot(wo, bitangent), NdotV);
    const float3 wiLocal = float3(dot(wi, tangent), dot(wi, bitangent), NdotL);
    if (abs(wiLocal.z) < kMinCosTheta)
        return;

    BSDFLobeProbabilities p = GetBSDFLobeProbabilities(bsdfData, wo);
    const float specTrans = saturate(bsdfData.specularTransmission);
    const float diffTrans = saturate(bsdfData.diffuseTransmission);

    float3 diffuseReflection     = float3(0.0, 0.0, 0.0);
    float3 diffuseTransmission   = float3(0.0, 0.0, 0.0);
    float3 specularReflection    = float3(0.0, 0.0, 0.0);
    float3 specularTransmission  = float3(0.0, 0.0, 0.0);
    float  pdfDiffuseReflection  = 0.0;
    float  pdfDiffuseTransmission = 0.0;
    float  pdfSpecularReflection = 0.0;
    float  pdfSpecularTransmission = 0.0;

    if (wiLocal.z > 0.0)
    {
        DiffuseReflectionFrostbite diffuseReflectionBxDF;
        diffuseReflectionBxDF.albedo    = bsdfData.diffuse;
        diffuseReflectionBxDF.roughness = bsdfData.roughness;

        diffuseReflection    = (1.0 - specTrans) * (1.0 - diffTrans) * diffuseReflectionBxDF.eval(woLocal, wiLocal);
        pdfDiffuseReflection = diffuseReflectionBxDF.evalPdf(woLocal, wiLocal);

        SpecularReflectionMicrofacet specularReflectionBxDF;
        specularReflectionBxDF.albedo      = bsdfData.specular;
        specularReflectionBxDF.alpha       = bsdfData.alpha;
        specularReflectionBxDF.activeLobes = bsdfData.activeLobes;

        specularReflection    = (1.0 - specTrans) * specularReflectionBxDF.eval(woLocal, wiLocal);
        pdfSpecularReflection = specularReflectionBxDF.evalPdf(woLocal, wiLocal);
    }
    else
    {
        DiffuseTransmissionLambert diffuseTransmissionBxDF;
        diffuseTransmissionBxDF.albedo = GetTransmissionAlbedo(bsdfData);

        diffuseTransmission    = (1.0 - specTrans) * diffTrans * diffuseTransmissionBxDF.eval(woLocal, wiLocal);
        pdfDiffuseTransmission = diffuseTransmissionBxDF.evalPdf(woLocal, wiLocal);
    }

    SpecularReflectionTransmissionMicrofacet specularTransmissionBxDF;
    specularTransmissionBxDF.transmissionAlbedo = GetTransmissionAlbedo(bsdfData);
    specularTransmissionBxDF.alpha              = bsdfData.alpha;
    specularTransmissionBxDF.eta                = bsdfData.eta;
    specularTransmissionBxDF.activeLobes        = bsdfData.activeLobes;
    specularTransmissionBxDF.isThinSurface      = bsdfData.thinSurface;

    specularTransmission    = specTrans * specularTransmissionBxDF.eval(woLocal, wiLocal);
    pdfSpecularTransmission = specularTransmissionBxDF.evalPdf(woLocal, wiLocal);

    f = diffuseReflection + diffuseTransmission + specularReflection + specularTransmission;
    pdf = p.pDiffuseReflection * pdfDiffuseReflection +
          p.pDiffuseTransmission * pdfDiffuseTransmission +
          p.pSpecularReflection * pdfSpecularReflection +
          p.pSpecularTransmission * pdfSpecularTransmission;
}

// Importance-sample an incident direction Wi. preGeneratedSample.xy samples the chosen lobe,
// and preGeneratedSample.z selects between BSDF lobe families.
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

    float3 tangent;
    float3 bitangent;
    BranchlessONB(bsdfData.N, tangent, bitangent);

    const float3 woLocal = float3(dot(wo, tangent), dot(wo, bitangent), NdotV);
    BSDFLobeProbabilities p = GetBSDFLobeProbabilities(bsdfData, wo);
    const float uSelect = preGeneratedSample.z;
    const float cDiffuseTransmission = p.pDiffuseReflection + p.pDiffuseTransmission;
    const float cSpecularReflection = cDiffuseTransmission + p.pSpecularReflection;

    float3 wiLocal = float3(0.0, 0.0, 0.0);

    if (uSelect < p.pDiffuseReflection)
    {
        DiffuseReflectionFrostbite diffuseReflectionBxDF;
        diffuseReflectionBxDF.albedo    = bsdfData.diffuse;
        diffuseReflectionBxDF.roughness = bsdfData.roughness;

        float3 lobeWeight;
        uint   componentLobe;
        float  componentLobeP;
        if (!diffuseReflectionBxDF.sample(woLocal, wiLocal, pdf, lobeWeight, componentLobe, componentLobeP, preGeneratedSample))
            return false;

        lobeP = p.pDiffuseReflection;
        lobe  = componentLobe;
    }
    else if (uSelect < cDiffuseTransmission)
    {
        DiffuseTransmissionLambert diffuseTransmissionBxDF;
        diffuseTransmissionBxDF.albedo = GetTransmissionAlbedo(bsdfData);

        float3 lobeWeight;
        uint   componentLobe;
        float  componentLobeP;
        if (!diffuseTransmissionBxDF.sample(woLocal, wiLocal, pdf, lobeWeight, componentLobe, componentLobeP, preGeneratedSample))
            return false;

        lobeP = p.pDiffuseTransmission;
        lobe  = componentLobe;
    }
    else if (uSelect < cSpecularReflection)
    {
        SpecularReflectionMicrofacet specularReflectionBxDF;
        specularReflectionBxDF.albedo      = bsdfData.specular;
        specularReflectionBxDF.alpha       = bsdfData.alpha;
        specularReflectionBxDF.activeLobes = bsdfData.activeLobes;

        float3 lobeWeight;
        uint   componentLobe;
        float  componentLobeP;
        if (!specularReflectionBxDF.sample(woLocal, wiLocal, pdf, lobeWeight, componentLobe, componentLobeP, preGeneratedSample))
            return false;

        lobeP = p.pSpecularReflection * componentLobeP;
        lobe  = componentLobe;

        if (lobe == kLobeTypeDeltaReflection)
        {
            wi     = normalize(tangent * wiLocal.x + bitangent * wiLocal.y + bsdfData.N * wiLocal.z);
            weight = (1.0 - saturate(bsdfData.specularTransmission)) * lobeWeight / max(p.pSpecularReflection, 1e-7);
            pdf    = 0.0;
            return IsFiniteVector(wi);
        }
    }
    else if (p.pSpecularTransmission > 0.0)
    {
        const float branchSample = clamp((uSelect - cSpecularReflection) / max(p.pSpecularTransmission, 1e-7), 0.0, 0.99999994);
        float3 localSample = preGeneratedSample;
        localSample.z      = branchSample;

        SpecularReflectionTransmissionMicrofacet specularTransmissionBxDF;
        specularTransmissionBxDF.transmissionAlbedo = GetTransmissionAlbedo(bsdfData);
        specularTransmissionBxDF.alpha              = bsdfData.alpha;
        specularTransmissionBxDF.eta                = bsdfData.eta;
        specularTransmissionBxDF.activeLobes        = bsdfData.activeLobes;
        specularTransmissionBxDF.isThinSurface      = bsdfData.thinSurface;

        float3 lobeWeight;
        uint   componentLobe;
        float  componentLobeP;
        if (!specularTransmissionBxDF.sample(woLocal, wiLocal, pdf, lobeWeight, componentLobe, componentLobeP, localSample))
            return false;

        lobeP = p.pSpecularTransmission * componentLobeP;
        lobe  = componentLobe;

        if ((lobe & kLobeTypeDelta) != 0u)
        {
            weight = lobeWeight * saturate(bsdfData.specularTransmission) / max(p.pSpecularTransmission, 1e-7);
            wi  = normalize(tangent * wiLocal.x + bitangent * wiLocal.y + bsdfData.N * wiLocal.z);
            pdf = 0.0;
            return IsFiniteVector(wi);
        }
    }
    else
    {
        return false;
    }

    if (abs(wiLocal.z) < kMinCosTheta)
        return false;

    wi = normalize(tangent * wiLocal.x + bitangent * wiLocal.y + bsdfData.N * wiLocal.z);
    if (!IsFiniteVector(wi))
        return false;

    float3 f;
    EvalBSDF(bsdfData, wo, wi, 0.0, f, pdf);
    if (!IsFiniteScalar(pdf) || pdf <= 0.0 || !IsFiniteVector(f))
        return false;

    weight = f / pdf;
    if (!IsFiniteVector(weight))
        return false;

    return true;
}

#endif // __BXDF_HLSLI__
