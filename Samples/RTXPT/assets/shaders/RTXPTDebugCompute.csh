#include "RTXPTCommon.fxh"

Texture2D<float4> g_InputColor;
VK_IMAGE_FORMAT("rgba8") RWTexture2D<float4> g_OutputColor;

[numthreads(8, 8, 1)]
void main(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    const uint2 Pixel  = DispatchThreadId.xy;
    const uint  Width  = (uint)g_FrameConstants.viewportSizeAndFrameIndex.x;
    const uint  Height = (uint)g_FrameConstants.viewportSizeAndFrameIndex.y;

    if (Pixel.x >= Width || Pixel.y >= Height)
        return;

    const float2 UV        = (float2(Pixel) + 0.5) / float2(max(Width, 1u), max(Height, 1u));
    const float4 Input     = g_InputColor.Load(int3(Pixel, 0));
    const float  Vignette  = smoothstep(0.95, 0.20, length(UV - 0.5));
    const float  PhaseMark = frac(g_FrameConstants.viewportSizeAndFrameIndex.w * 0.0078125);
    const float3 Marked    = saturate(Input.rgb * (0.92 + 0.08 * Vignette) + float3(0.02, 0.015, 0.01) * PhaseMark);

    g_OutputColor[Pixel] = float4(Marked, 1.0);
}
