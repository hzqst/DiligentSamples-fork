#include "Lighting/LightingTypes.hlsli"

StructuredBuffer<LightingControlData> t_LightingControl;
Buffer<uint>                          t_LightProxyCounters;
Buffer<uint>                          t_LightSamplingProxies;
Texture2D<float>                      t_FeedbackTotalWeight;
Texture2D<uint>                       t_FeedbackCandidates;
RWTexture2D<float>                    u_FeedbackTotalWeight;
RWTexture2D<uint>                     u_FeedbackCandidates;
RWBuffer<uint>                        u_LocalSamplingBuffer;

[numthreads(8, 8, 1)]
void ClearFeedbackCS(uint3 tid : SV_DispatchThreadID)
{
    const LightingControlData ctrl = t_LightingControl[0];
    if (tid.x >= ctrl.BakerConstants.FeedbackResolution.x || tid.y >= ctrl.BakerConstants.FeedbackResolution.y)
        return;

    u_FeedbackTotalWeight[tid.xy] = 0.0;
    u_FeedbackCandidates[tid.xy]  = RTXPT_INVALID_LIGHT_INDEX;
}

[numthreads(8, 8, 1)]
void ClearLocalSamplingCS(uint3 tid : SV_DispatchThreadID)
{
    const LightingControlData ctrl = t_LightingControl[0];
    if (tid.x >= ctrl.LocalSamplingResolution.x || tid.y >= ctrl.LocalSamplingResolution.y)
        return;

    const uint base = LLSB_ComputeBaseAddress(tid.xy, ctrl.LocalSamplingResolution);
    [loop]
    for (uint i = 0; i < RTXPT_LIGHTING_LOCAL_PROXY_COUNT; ++i)
        u_LocalSamplingBuffer[base + i] = PackMiniListLightAndCount(0u, 1u);
}

[numthreads(8, 8, 1)]
void FillLocalSamplingCS(uint3 tid : SV_DispatchThreadID)
{
    const LightingControlData ctrl = t_LightingControl[0];
    if (tid.x >= ctrl.LocalSamplingResolution.x || tid.y >= ctrl.LocalSamplingResolution.y || ctrl.SamplingProxyCount == 0u)
        return;

    const uint2 feedbackResolution = ctrl.BakerConstants.FeedbackResolution;
    const uint2 pixelBase          = tid.xy * RTXPT_LIGHTING_SAMPLING_BUFFER_TILE_SIZE;
    const uint  base               = LLSB_ComputeBaseAddress(tid.xy, ctrl.LocalSamplingResolution);

    uint  feedbackLight  = RTXPT_INVALID_LIGHT_INDEX;
    float feedbackWeight = 0.0;
    [loop]
    for (uint y = 0; y < RTXPT_LIGHTING_SAMPLING_BUFFER_TILE_SIZE; ++y)
    {
        [loop]
        for (uint x = 0; x < RTXPT_LIGHTING_SAMPLING_BUFFER_TILE_SIZE; ++x)
        {
            const uint2 pixel     = pixelBase + uint2(x, y);
            if (pixel.x >= feedbackResolution.x || pixel.y >= feedbackResolution.y)
                continue;

            const uint  candidate = t_FeedbackCandidates.Load(int3(pixel, 0)) & ~LFR_SCREEN_SPACE_COHERENT_FLAG;
            const float weight    = t_FeedbackTotalWeight.Load(int3(pixel, 0));
            if (candidate != RTXPT_INVALID_LIGHT_INDEX && weight > feedbackWeight)
            {
                feedbackLight  = candidate;
                feedbackWeight = weight;
            }
        }
    }

    [loop]
    for (uint i = 0; i < RTXPT_LIGHTING_LOCAL_PROXY_COUNT; ++i)
    {
        uint lightIndex = t_LightSamplingProxies[min(i % ctrl.SamplingProxyCount, ctrl.SamplingProxyCount - 1u)];
        if (feedbackLight != RTXPT_INVALID_LIGHT_INDEX && i < RTXPT_LIGHTING_SAMPLING_BUFFER_WINDOW_SIZE)
        {
            lightIndex = feedbackLight;
        }
        u_LocalSamplingBuffer[base + i] = PackMiniListLightAndCount(lightIndex, 1u);
    }
}
