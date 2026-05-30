#ifndef __PATH_TRACER_HELPERS_HLSLI__
#define __PATH_TRACER_HELPERS_HLSLI__

// Power heuristic for MIS (Veach). Matches RTXPT-fork PathTracerHelpers.hlsli signature.
float PowerHeuristic(float nf, float fPdf, float ng, float gPdf)
{
    const float f  = nf * fPdf;
    const float g  = ng * gPdf;
    const float f2 = f * f;
    const float g2 = g * g;
    return f2 / max(f2 + g2, 1e-7);
}

#endif // __PATH_TRACER_HELPERS_HLSLI__
