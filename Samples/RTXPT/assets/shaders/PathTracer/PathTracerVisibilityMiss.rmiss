#include "Config.h"

#ifndef RTXPT_MINIMAL_TRACE_RAY_DIAGNOSTIC
#    define RTXPT_MINIMAL_TRACE_RAY_DIAGNOSTIC 0
#endif

#if RTXPT_MINIMAL_TRACE_RAY_DIAGNOSTIC

struct DiagnosticPayload
{
    float3 color;
    float  hitDistance;
    uint   hit;
};

[shader("miss")]
void main(inout DiagnosticPayload Payload)
{
    Payload.hit         = 0u;
    Payload.hitDistance = 1.0;
    Payload.color       = float3(0.0, 0.0, 0.0);
}

#else

#if PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE
#    include "PathTracerBridge.hlsli"
using ActiveRayPayload = RTXPTMaterialHitPayload;
#else
#    include "PathState.hlsli"
#    include "PathPayload.hlsli"
using ActiveRayPayload = PathPayload;
#endif

[shader("miss")]
void main(inout ActiveRayPayload Payload)
{
#if PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE
    Payload.hitFlag = 0u;
#else
    PathState path = PathPayload::unpack(Payload);
    path.terminate();
    Payload = PathPayload::pack(path);
#endif
}

#endif
