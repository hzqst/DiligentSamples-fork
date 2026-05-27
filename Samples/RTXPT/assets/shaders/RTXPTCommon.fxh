#ifndef RTXPT_COMMON_FXH
#define RTXPT_COMMON_FXH

struct RTXPTFrameConstants
{
    float4x4 ViewProj;
    float4x4 ViewProjInv;
    float4   CameraPosition_Time;
    float4   ViewportSize_FrameIdx;
};

ConstantBuffer<RTXPTFrameConstants> g_FrameConstants;

struct RTXPTPrimaryPayload
{
    float4 ColorDepth;
};

#endif
