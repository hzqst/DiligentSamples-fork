#pragma pack_matrix(row_major)

#define NUM_COMPUTE_THREADS_PER_DIM 8

#ifndef VK_IMAGE_FORMAT
#    define VK_IMAGE_FORMAT(format)
#endif

Texture2D<float4> t_RTXPTMotionVectors;

VK_IMAGE_FORMAT("rg16f") RWTexture2D<float2> u_SRMotionVectors;

[numthreads(NUM_COMPUTE_THREADS_PER_DIM, NUM_COMPUTE_THREADS_PER_DIM, 1)]
void main(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width;
    uint Height;
    t_RTXPTMotionVectors.GetDimensions(Width, Height);

    const uint2 PixelPos = DispatchThreadID.xy;
    if (PixelPos.x >= Width || PixelPos.y >= Height)
        return;

    u_SRMotionVectors[PixelPos] = t_RTXPTMotionVectors[PixelPos].xy;
}
