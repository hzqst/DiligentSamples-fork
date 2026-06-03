#include "PathTracer/PathTracerShared.h"
#include "PathTracer/PathTracerBridge.hlsli"

#ifndef NUM_COMPUTE_THREADS_PER_DIM
#    define NUM_COMPUTE_THREADS_PER_DIM 8
#endif

[numthreads(NUM_COMPUTE_THREADS_PER_DIM, NUM_COMPUTE_THREADS_PER_DIM, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    const uint2 pixel = tid.xy;
    if (pixel.x >= g_Const.ptConsts.imageWidth || pixel.y >= g_Const.ptConsts.imageHeight)
        return;

    // RTXPT-fork's current live VBufferExport only preserves debug visualization.
    // The actual depth/motion/throughput export is done in BUILD_STABLE_PLANES.
}
