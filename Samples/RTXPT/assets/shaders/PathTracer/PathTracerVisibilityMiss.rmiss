#include "Config.h"

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
