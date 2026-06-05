#pragma pack_matrix(row_major)

#define NUM_COMPUTE_THREADS_PER_DIM 8

#ifndef VK_IMAGE_FORMAT
#    define VK_IMAGE_FORMAT(format)
#endif

#include "../PathTracer/PathTracerShared.h"
#include "../PathTracer/PathTracerHelpers.hlsli"

cbuffer g_Const
{
    SampleConstants g_Frame;
};

Texture2D<float>  t_RTXPTDepth;
Texture2D<float4> t_RTXPTMotionVectors;

VK_IMAGE_FORMAT("r32f")  RWTexture2D<float>  u_TAADepth;
VK_IMAGE_FORMAT("rg16f") RWTexture2D<float2> u_TAAMotion;

float RTXPTNormalizedDeviceZToDepth(float NdcZ)
{
#if defined(NDC_MIN_Z) && defined(F3NDC_XYZ_TO_UVD_SCALE)
    return (NdcZ - NDC_MIN_Z) * F3NDC_XYZ_TO_UVD_SCALE.z;
#else
    return NdcZ;
#endif
}

float ComputeTAADepth(uint2 PixelPos, float RawDepth)
{
    const float FarDepth = RTXPTNormalizedDeviceZToDepth(1.0);
    if (!isfinite(RawDepth) || RawDepth <= 0.0 || RawDepth >= g_Frame.ptConsts.camera.FarZ)
        return FarDepth;

    CameraRay    Ray      = ComputeRayThinlens(g_Frame.ptConsts.camera, PixelPos, g_Frame.ptConsts.camera.Jitter, 0.5.xx);
    const float3 WorldPos = Ray.origin + Ray.dir * RawDepth;
    const float4 Clip     = mul(float4(WorldPos, 1.0), g_Frame.view.MatWorldToClip);
    if (!all(isfinite(Clip)) || Clip.w <= 0.0)
        return FarDepth;

    return saturate(RTXPTNormalizedDeviceZToDepth(Clip.z / Clip.w));
}

float2 ConvertMotionToTAANdc(float2 MotionPixels)
{
    const float2 InvSize = g_Frame.view.ViewportSizeInv;
    return float2(-2.0 * MotionPixels.x * InvSize.x,
                   2.0 * MotionPixels.y * InvSize.y);
}

[numthreads(NUM_COMPUTE_THREADS_PER_DIM, NUM_COMPUTE_THREADS_PER_DIM, 1)]
void main(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    const uint2 PixelPos = DispatchThreadID.xy;
    if (PixelPos.x >= g_Frame.ptConsts.imageWidth || PixelPos.y >= g_Frame.ptConsts.imageHeight)
        return;

    u_TAADepth[PixelPos]  = ComputeTAADepth(PixelPos, t_RTXPTDepth[PixelPos]);
    u_TAAMotion[PixelPos] = ConvertMotionToTAANdc(t_RTXPTMotionVectors[PixelPos].xy);
}
