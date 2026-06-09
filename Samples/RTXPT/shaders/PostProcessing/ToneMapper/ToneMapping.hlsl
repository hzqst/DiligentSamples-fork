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
