#pragma pack_matrix(row_major)

#ifndef RTXPT_POST_PROCESS_MODE
#define RTXPT_POST_PROCESS_MODE 0
#endif

#define RTXPT_POST_PROCESS_HDR_TEST       1
#define RTXPT_POST_PROCESS_EDGE_DETECTION 2

struct RTXPTPostProcessConstants
{
    uint  Width;
    uint  Height;
    float EdgeDetectionThreshold;
    float Padding0;
};

cbuffer g_PostProcessConstants
{
    RTXPTPostProcessConstants g_Params;
};

Texture2D<float4> t_LdrColorScratch;

VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_ProcessedOutputColor;
VK_IMAGE_FORMAT("rgba8")   RWTexture2D<float4> u_PostTonemapOutputColor;

float LinearToSRGB(float Lin)
{
    if (Lin <= 0.0031308f)
        return Lin * 12.92f;
    return pow(Lin, 1.0f / 2.4f) * 1.055f - 0.055f;
}

float3 LinearToSRGB(float3 Lin)
{
    return float3(LinearToSRGB(Lin.x), LinearToSRGB(Lin.y), LinearToSRGB(Lin.z));
}

uint2 ClampPixelCoord(int2 PixelPos)
{
    const int2 MaxPixelPos = int2(int(g_Params.Width) - 1, int(g_Params.Height) - 1);
    return uint2(clamp(PixelPos, int2(0, 0), MaxPixelPos));
}

float3 LoadLDR(uint2 PixelPos, int2 Offset)
{
    return t_LdrColorScratch[ClampPixelCoord(int2(PixelPos) + Offset)].rgb;
}

void SaveLDR(uint2 PixelPos, float3 LinearColor)
{
    u_PostTonemapOutputColor[PixelPos] = float4(LinearToSRGB(LinearColor), 1.0);
}

[numthreads(8, 8, 1)]
void main(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    const uint2 PixelPos = DispatchThreadID.xy;
    if (PixelPos.x >= g_Params.Width || PixelPos.y >= g_Params.Height)
        return;

#if RTXPT_POST_PROCESS_MODE == RTXPT_POST_PROCESS_HDR_TEST
    float3 ExistingColor = u_ProcessedOutputColor[PixelPos].rgb;
    if (length(float2(PixelPos.xy) - float2(800.0, 500.0)) < 100.0)
        ExistingColor.z += 10.0;
    u_ProcessedOutputColor[PixelPos] = float4(ExistingColor, 1.0);
#elif RTXPT_POST_PROCESS_MODE == RTXPT_POST_PROCESS_EDGE_DETECTION
    const int OffX = 1;
    const int OffY = 1;

    const float3 S00 = LoadLDR(PixelPos, int2(-OffX, -OffY));
    const float3 S01 = LoadLDR(PixelPos, int2(0, -OffY));
    const float3 S02 = LoadLDR(PixelPos, int2(OffX, -OffY));
    const float3 S10 = LoadLDR(PixelPos, int2(-OffX, 0));
    const float3 S12 = LoadLDR(PixelPos, int2(OffX, 0));
    const float3 S20 = LoadLDR(PixelPos, int2(-OffX, OffY));
    const float3 S21 = LoadLDR(PixelPos, int2(0, OffY));
    const float3 S22 = LoadLDR(PixelPos, int2(OffX, OffY));

    const float3 SobelX = S00 + 2.0 * S10 + S20 - S02 - 2.0 * S12 - S22;
    const float3 SobelY = S00 + 2.0 * S01 + S02 - S20 - 2.0 * S21 - S22;
    const float3 EdgeSqr = SobelX * SobelX + SobelY * SobelY;
    const float  Threshold = g_Params.EdgeDetectionThreshold;
    const float3 EdgeColor = 1.0.xxx - (EdgeSqr > Threshold.xxx * Threshold.xxx);
    SaveLDR(PixelPos, saturate(EdgeColor));
#endif
}
