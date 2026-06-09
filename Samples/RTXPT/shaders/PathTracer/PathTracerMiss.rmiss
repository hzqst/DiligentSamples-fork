#include "Config.h"

#include "PathTracer.hlsli"

using ActiveRayPayload = PathPayload;

[shader("miss")]
void main(inout ActiveRayPayload Payload)
{
    PathState path = PathPayload::unpack(Payload);
    PathTracer::WorkingContext workingContext = GetWorkingContext();
    PathTracer::HandleMiss(path, WorldRayOrigin(), WorldRayDirection(), RayTCurrent(), workingContext);
    Payload = PathPayload::pack(path);
}
