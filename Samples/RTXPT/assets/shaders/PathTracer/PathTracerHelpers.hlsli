#ifndef __PATH_TRACER_HELPERS_HLSLI__
#define __PATH_TRACER_HELPERS_HLSLI__

float ComputeRayOriginComponent(float worldPosition, float faceNormal)
{
    const float originScale = 1.0 / 16.0;
    const float floatScale  = 3.0 / 65536.0;
    const float intScale    = 3.0 * 256.0;

    const int   intOffset = int(faceNormal * intScale);
    const int   intPos    = asint(worldPosition) + (worldPosition < 0.0 ? -intOffset : intOffset);
    const float fpOffset  = worldPosition + faceNormal * floatScale;
    return abs(worldPosition) < originScale ? fpOffset : asfloat(intPos);
}

float3 ComputeRayOrigin(float3 worldPosition, float3 faceNormal)
{
    return float3(ComputeRayOriginComponent(worldPosition.x, faceNormal.x),
                  ComputeRayOriginComponent(worldPosition.y, faceNormal.y),
                  ComputeRayOriginComponent(worldPosition.z, faceNormal.z));
}

float ComputeLowGrazingAngleFalloff(float3 lightDirection, float3 interpolatedGeometryNormal, float falloffFrom, float falloffRange)
{
    return saturate((dot(lightDirection, interpolatedGeometryNormal) - falloffFrom) / max(falloffRange, 1e-6));
}

// Power heuristic for MIS (Veach). Matches RTXPT-fork PathTracerHelpers.hlsli signature.
float PowerHeuristic(float nf, float fPdf, float ng, float gPdf)
{
    const float f  = nf * fPdf;
    const float g  = ng * gPdf;
    const float f2 = f * f;
    const float g2 = g * g;
    return f2 / max(f2 + g2, 1e-7);
}

// Mean of the RGB channels. RTXPT uses Average() (not luminance) for the firefly soft cap to
// avoid a hue shift toward blue when clamping. (Self-contained: pi literals avoid coupling to
// BxDF.hlsli's K_PI, which is included after this header.)
float Average(float3 v)
{
    return (v.x + v.y + v.z) * 0.3333333333333333;
}

// Ray-cone spread angle implied by a scatter pdf (RTXPT ComputeRayConeSpreadAngleExpansionByScatterPDF,
// growthFactor folded to 1.0). A delta lobe (pdf==0 sentinel) has zero spread.
float ComputeRayConeSpreadAngleExpansionByScatterPDF(float bsdfScatterPdf)
{
    const float twoPi = 6.28318530717958647692;
    return 2.0 * acos(max(-1.0, 1.0 - (1.0 / bsdfScatterPdf) / twoPi));
}

// Adaptive firefly-filter K (RTXPT ComputeNewScatterFireflyFilterK): K shrinks as the path
// spreads. currentK is the path's running K, bouncePdf the scatter pdf (0 == delta event),
// lobeP the probability of the lobe that was sampled.
float ComputeNewScatterFireflyFilterK(float currentK, float bouncePdf, float lobeP)
{
    const float minK  = 0.00001;
    const float angle = (bouncePdf == 0.0) ? 0.0 : ComputeRayConeSpreadAngleExpansionByScatterPDF(bouncePdf);
    const float k     = 32.0; // empirical
    float       p     = k / (k + angle * angle);
    p *= sqrt(lobeP); // sqrt behaves better empirically
    return max(minK, currentK * p);
}

// Soft-cap a vector signal to threshold*K of its own average (RTXPT FireflyFilter).
float3 FireflyFilter(float3 signalIn, float threshold, float fireflyFilterK)
{
    const float fft  = threshold * fireflyFilterK;
    const float maxR = Average(signalIn);
    if (maxR > fft)
        signalIn = signalIn / maxR * fft;
    return signalIn;
}

// Scalar dampening factor for the same soft cap (RTXPT FireflyFilterShort); multiply a radiance by it.
float FireflyFilterShort(float signalAverage, float threshold, float fireflyFilterK)
{
    const float fft = threshold * fireflyFilterK;
    return (signalAverage > fft) ? (fft / signalAverage) : 1.0;
}

#endif // __PATH_TRACER_HELPERS_HLSLI__
