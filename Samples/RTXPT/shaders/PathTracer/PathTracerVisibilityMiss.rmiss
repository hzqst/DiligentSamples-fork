#include "Config.h"

#include "PathState.hlsli"
#include "PathPayload.hlsli"

using ActiveRayPayload = PathPayload;

[shader("miss")]
void main(inout ActiveRayPayload Payload)
{
    PathState path = PathPayload::unpack(Payload);
    path.terminate();
    Payload = PathPayload::pack(path);
}
