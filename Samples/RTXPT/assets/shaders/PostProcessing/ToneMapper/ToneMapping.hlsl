#pragma pack_matrix(row_major)

#include "ToneMappingShared.h"
#include "ToneMapping.ps.hlsli"

struct PSInput
{
    float4 Pos : SV_POSITION;
    float2 UV  : TEX_COORD;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main_ps(in PSInput Input,
             out PSOutput Output)
{
    Output.Color = ApplyToneMapping(Input.UV);
}

RWBuffer<float>  u_CaptureTarget;
Texture2D<float> t_CaptureSource;

[numthreads(1, 1, 1)]
void capture_cs(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width;
    uint Height;
    uint MipLevels;
    t_CaptureSource.GetDimensions(0, Width, Height, MipLevels);
    u_CaptureTarget[0] = t_CaptureSource.Load(int3(0, 0, MipLevels - 1));
}
