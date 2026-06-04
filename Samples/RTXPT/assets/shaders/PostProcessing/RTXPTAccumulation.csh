struct RTXPTAccumulationConstants
{
    float2 OutputSize;
    float2 InputSize;
    float2 InputTextureSizeInv;
    float2 PixelOffset;
    float  BlendFactor;
    float3 _Padding0;
};

cbuffer g_AccumulationConstants
{
    RTXPTAccumulationConstants g_Const;
};

VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> u_AccumulatedColor;
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_OutputColor;
Texture2D<float4>   t_InputColor;
SamplerState        s_LinearSampler;

[numthreads(8, 8, 1)]
void main(uint2 GlobalIdx : SV_DispatchThreadID)
{
    if (any(GlobalIdx >= uint2(g_Const.OutputSize)))
        return;

    float4 CompositedColor;
    if (all(g_Const.InputSize == g_Const.OutputSize))
    {
        CompositedColor = t_InputColor[GlobalIdx];
    }
    else
    {
        const float2 InputPos = (float2(GlobalIdx) + 0.5.xx) * (g_Const.InputSize / g_Const.OutputSize) + g_Const.PixelOffset;
        const float2 InputUV  = InputPos * g_Const.InputTextureSizeInv;
        CompositedColor       = t_InputColor.SampleLevel(s_LinearSampler, InputUV, 0.0);
    }

    const float4 PreviousColor = u_AccumulatedColor[GlobalIdx];
    const float4 OutputColor   = g_Const.BlendFactor < 1.0 ?
        lerp(PreviousColor, CompositedColor, g_Const.BlendFactor) :
        CompositedColor;

    if (g_Const.BlendFactor > 0.0)
        u_AccumulatedColor[GlobalIdx] = OutputColor;

    u_OutputColor[GlobalIdx] = OutputColor;
}
